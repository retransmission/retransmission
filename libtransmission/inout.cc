// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <fmt/format.h>

#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/local-data.h" // tr::LocalData::BlockData
#include "libtransmission/log.h"
#include "libtransmission/open-files.h"
#include "libtransmission/session.h"
#include "libtransmission/storage-descriptor.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h" // tr_pathbuf
#include "libtransmission/types.h"
#include "libtransmission/utils.h" // _()

using namespace std::literals;

namespace
{

bool read_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t> buf, tr_error& error)
{
    while (!std::empty(buf)) {
        auto n_read = uint64_t{};

        if (!tr_sys_file_read_at(fd, std::data(buf), std::size(buf), file_offset, &n_read, &error)) {
            return false;
        }

        buf = buf.subspan(n_read);
        file_offset += n_read;
    }

    return true;
}

bool write_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t const> buf, tr_error& error)
{
    while (!std::empty(buf)) {
        auto n_written = uint64_t{};

        if (!tr_sys_file_write_at(fd, std::data(buf), std::size(buf), file_offset, &n_written, &error)) {
            return false;
        }

        buf = buf.subspan(n_written);
        file_offset += n_written;
    }

    return true;
}

// Returns a RAII reference to the open file.
// Sets `setme_created` if the file had to be created.
[[nodiscard]] tr_open_files::Handle get_file(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    bool const writable,
    tr_file_index_t const file_index,
    tr_error& error,
    bool& setme_created)
{
    auto const tor_id = desc.id;

    // is the file already open in the fd pool?
    if (auto file = open_files.get(tor_id, file_index, writable)) {
        return file;
    }

    // does the file exist?
    auto const file_size = desc.files.file_size(file_index);
    auto const prealloc = writable && desc.files_wanted.test(file_index) ? desc.preallocation : tr_file_preallocation::None;
    auto err = ENOENT;
    if (auto const found = desc.find(file_index)) {
        auto const filename = found->filename<tr_pathbuf>();
        if (auto file = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size); file) {
            return file;
        }

        // The file exists but can't be opened, e.g. its permissions
        // changed. Report that rather than letting the caller mistake
        // an empty handle for success.
        err = errno != 0 ? errno : EIO;
    } else if (writable) { // do we want to create it?
        auto const suffix = desc.partial_file_naming ? tr_torrent_files::PartialFileSuffix : ""sv;
        auto const filename = tr_pathbuf{ desc.current_dir, '/', desc.files.path(file_index), suffix };
        if (auto file = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size); file) {
            setme_created = true;
            return file;
        }

        err = errno;
    }

    error.set(
        err,
        fmt::format(
            fmt::runtime(_("Couldn't get '{path}': {error} ({error_code})")),
            fmt::arg("path", desc.files.path(file_index)),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err)));
    return {};
}

void read_bytes(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t> buf,
    tr_error& error)
{
    TR_ASSERT(file_index < desc.files.file_count());
    auto const file_size = desc.files.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U) {
        return;
    }

    auto created = false;
    auto const file = get_file(desc, open_files, false, file_index, error, created);
    if (!file || error) {
        return;
    }

    auto const io_lock = file->io_lock();
    read_entire_buf(file->fd(), file_offset, buf, error);

    if (error) {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", desc.files.path(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())),
            desc.name);
    }
}

void write_bytes(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<uint8_t const> buf,
    tr_error& error,
    size_t& n_files_created)
{
    TR_ASSERT(file_index < desc.files.file_count());
    auto const file_size = desc.files.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U) {
        return;
    }

    auto created = false;
    auto const file = get_file(desc, open_files, true, file_index, error, created);
    if (created) {
        ++n_files_created;
    }
    if (!file || error) {
        return;
    }

    auto const io_lock = file->io_lock();
    write_entire_buf(file->fd(), file_offset, buf, error);

    if (error) {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                fmt::arg("path", desc.files.path(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())),
            desc.name);
    }
}

} // namespace

tr_error_code_t tr_ioRead(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<uint8_t> const setme)
{
    if (std::empty(setme)) {
        return 0;
    }

    if (begin + std::size(setme) > desc.block_info.total_size()) {
        return TR_ERROR_EINVAL;
    }

    auto error = tr_error{};
    auto [file_index, file_offset] = desc.fpm.file_offset(begin);
    auto buf = setme;
    while (!std::empty(buf) && !error) {
        auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), desc.files.file_size(file_index) - file_offset);
        read_bytes(desc, open_files, file_index, file_offset, buf.first(bytes_this_pass), error);
        buf = buf.subspan(bytes_this_pass);
        ++file_index;
        file_offset = 0U;
    }

    return error.code();
}

tr_io_write_result tr_ioWrite(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<uint8_t const> const writeme)
{
    auto result = tr_io_write_result{};

    if (std::empty(writeme)) {
        return result;
    }

    if (begin + std::size(writeme) > desc.block_info.total_size()) {
        result.error = TR_ERROR_EINVAL;
        return result;
    }

    auto error = tr_error{};
    auto [file_index, file_offset] = desc.fpm.file_offset(begin);
    auto buf = writeme;
    while (!std::empty(buf) && !error) {
        auto const bytes_this_pass = std::min<uint64_t>(std::size(buf), desc.files.file_size(file_index) - file_offset);
        write_bytes(desc, open_files, file_index, file_offset, buf.first(bytes_this_pass), error, result.n_files_created);
        buf = buf.subspan(bytes_this_pass);
        ++file_index;
        file_offset = 0U;
    }

    result.error = error.code();
    return result;
}

tr_error_code_t tr_ioRecalculateHash(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_piece_index_t const piece,
    tr_sha1_digest_t& setme)
{
    auto const& block_info = desc.block_info;
    if (piece >= block_info.piece_count()) {
        return TR_ERROR_EINVAL;
    }

    auto buffer = tr::LocalData::BlockData{};
    auto err = tr_error_code_t{};
    auto const hash = tr_ioHashPiece(
        block_info,
        piece,
        [&desc, &open_files, &block_info, &buffer, &err](tr_block_index_t const block) -> std::span<uint8_t const> {
            auto const byte_span = block_info.byte_span_for_block(block);
            auto const len = static_cast<size_t>(byte_span.size());
            buffer.resize(len);
            err = tr_ioRead(desc, open_files, byte_span.begin, { std::data(buffer), len });
            return err == 0 ? std::span<uint8_t const>{ std::data(buffer), len } : std::span<uint8_t const>{};
        });

    if (!hash) {
        return err != 0 ? err : EIO;
    }

    setme = *hash;
    return 0;
}

bool tr_ioTestPiece(tr_torrent const& tor, tr_piece_index_t const piece)
{
    auto hash = tr_sha1_digest_t{};
    return tr_ioRecalculateHash(*tor.storage_descriptor(), tor.session->openFiles(), piece, hash) == 0 &&
        hash == tor.piece_hash(piece);
}
