// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility> // std::move
#include <vector>

#include <fmt/format.h>

#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/constants.h" // TrBlockSize
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/log.h"
#include "libtransmission/open-files.h"
#include "libtransmission/profile.h"
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

// Calls `fn(file_index, file_offset, done, n_bytes)` for each file's share
// of torrent bytes [begin, begin + len), in order. `done` counts the bytes
// before that share. Stops, and returns false, when `fn` returns false.
template<typename Fn>
bool for_each_file_chunk(tr::StorageDescriptor const& desc, uint64_t const begin, uint64_t const len, Fn const& fn)
{
    auto [file_index, file_offset] = desc.fpm.file_offset(begin);
    for (auto done = uint64_t{}; done < len;) {
        auto const n_bytes = std::min<uint64_t>(len - done, desc.files.file_size(file_index) - file_offset);
        if (!fn(file_index, file_offset, done, n_bytes)) {
            return false;
        }

        done += n_bytes;
        ++file_index;
        file_offset = 0U;
    }

    return true;
}

// Fills `bufs` in turn, reading `fd` from `file_offset` on.
bool read_entire_bufs(tr_sys_file_t const fd, uint64_t file_offset, std::span<std::span<uint8_t>> bufs, tr_error& error)
{
    TR_PROFILE_SCOPE("disk.read_syscall_loop");
    for (;;) {
        while (!std::empty(bufs) && std::empty(bufs.front())) {
            bufs = bufs.subspan(1U);
        }
        if (std::empty(bufs)) {
            return true;
        }

        auto n_read = uint64_t{};
        if (!tr_sys_file_readv_at(fd, bufs, file_offset, &n_read, &error) || n_read == 0U) {
            // On POSIX, a read at the end of the file fails without an
            // error. The file is shorter than the torrent says it is.
            if (!error) {
                error.set_from_errno(EIO);
            }
            return false;
        }

        file_offset += n_read;
        while (n_read > 0U) {
            auto& front = bufs.front();
            auto const n_used = std::min<uint64_t>(n_read, std::size(front));
            front = front.subspan(n_used);
            n_read -= n_used;
            if (std::empty(front)) {
                bufs = bufs.subspan(1U);
            }
        }
    }
}

bool write_entire_buf(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t const> buf, tr_error& error)
{
    TR_PROFILE_SCOPE("disk.write_syscall_loop");
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
[[nodiscard]] tr_open_files::Handle get_file(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    bool const writable,
    tr_file_index_t const file_index,
    tr_error& error)
{
    auto const tor_id = desc.id;

    // is the file already open in the fd pool?
    if (auto file = open_files.get(tor_id, file_index, writable)) {
        return file;
    }

    // does the file exist?
    auto const file_size = desc.files.file_size(file_index);
    auto const prealloc = writable && desc.files_wanted.test(file_index) ? desc.preallocation : tr_file_preallocation::None;
    if (auto const found = desc.find(file_index)) {
        auto const filename = found->filename<tr_pathbuf>();
        if (auto file = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size, error); file) {
            return file;
        }

        // The file exists but can't be opened, e.g. no file descriptors
        // are left or its permissions changed. `error` is set so that
        // the caller doesn't mistake this for success and discard the
        // data it wanted to write.
    }
    // do we want to create it?
    else if (writable) {
        auto const suffix = desc.partial_file_naming ? tr_torrent_files::PartialFileSuffix : ""sv;
        auto const filename = tr_pathbuf{ desc.current_dir, '/', desc.files.path(file_index), suffix };
        if (auto file = open_files.get(tor_id, file_index, writable, filename, prealloc, file_size, error); file) {
            return file;
        }
    }

    if (error) {
        // open_files.get() reports why but not which file; the message
        // reaches the user via the torrent's local error, so put the
        // filename back in
        error.prefix_message(
            fmt::format(fmt::runtime(_("Couldn't get '{path}': ")), fmt::arg("path", desc.files.path(file_index))));
    } else { // the file doesn't exist, and we can't create it
        error.set(
            ENOENT,
            fmt::format(
                fmt::runtime(_("Couldn't get '{path}': {error} ({error_code})")),
                fmt::arg("path", desc.files.path(file_index)),
                fmt::arg("error", tr_strerror(ENOENT)),
                fmt::arg("error_code", ENOENT)));
    }
    return {};
}

// Reads `n_bytes` of one file into `bufs`, which hold that many in all.
void read_bytes(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_file_index_t const file_index,
    uint64_t const file_offset,
    std::span<std::span<uint8_t>> const bufs,
    [[maybe_unused]] uint64_t const n_bytes,
    tr_error& error)
{
    TR_ASSERT(file_index < desc.files.file_count());
    auto const file_size = desc.files.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + n_bytes <= file_size);
    if (file_size == 0U) {
        return;
    }

    auto const file = get_file(desc, open_files, false, file_index, error);
    if (!file || error) {
        return;
    }

    auto const io_lock = file->io_lock();
    read_entire_bufs(file->fd(), file_offset, bufs, error);

    if (error) {
        tr_logAddError(
            fmt::format(
                "Couldn't read '{path}': {error} ({error_code})",
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
    tr_error& error)
{
    TR_ASSERT(file_index < desc.files.file_count());
    auto const file_size = desc.files.file_size(file_index);
    TR_ASSERT(file_size == 0U || file_offset < file_size);
    TR_ASSERT(file_offset + std::size(buf) <= file_size);
    if (file_size == 0U) {
        return;
    }

    auto const file = get_file(desc, open_files, true, file_index, error);
    if (!file || error) {
        return;
    }

    auto const io_lock = file->io_lock();
    write_entire_buf(file->fd(), file_offset, buf, error);

    if (error) {
        tr_logAddError(
            fmt::format(
                "Couldn't save '{path}': {error} ({error_code})",
                fmt::arg("path", desc.files.path(file_index)),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())),
            desc.name);
    }
}

} // namespace

tr_error tr_ioRead(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<uint8_t> const setme)
{
    return tr_ioReadv(desc, open_files, begin, std::array{ setme });
}

tr_error tr_ioReadv(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<std::span<uint8_t> const> const setme)
{
    TR_PROFILE_SCOPE("disk.read_block");
    auto len = uint64_t{};
    for (auto const& buf : setme) {
        len += std::size(buf);
    }

    if (len == 0U) {
        return {};
    }

    if (begin + len > desc.block_info.total_size()) {
        return tr_error::from_errno(EINVAL);
    }

    // Each file's share of `setme`, trimmed to that file's bytes
    auto file_bufs = std::vector<std::span<uint8_t>>{};
    file_bufs.reserve(std::size(setme));
    auto next = size_t{}; // the first buffer that isn't full yet
    auto next_used = uint64_t{}; // bytes of it that are

    auto error = tr_error{};
    for_each_file_chunk(
        desc,
        begin,
        len,
        [&](tr_file_index_t const file_index, uint64_t const file_offset, uint64_t /*done*/, uint64_t const n_bytes) {
            file_bufs.clear();
            for (auto remaining = n_bytes; remaining > 0U;) {
                auto const buf = setme[next].subspan(next_used);
                auto const n_used = std::min<uint64_t>(remaining, std::size(buf));
                file_bufs.emplace_back(buf.first(n_used));
                remaining -= n_used;
                next_used += n_used;
                if (next_used == std::size(setme[next])) {
                    ++next;
                    next_used = 0U;
                }
            }

            read_bytes(desc, open_files, file_index, file_offset, file_bufs, n_bytes, error);
            return !error;
        });

    return error;
}

bool tr_ioReadCached(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<uint8_t> const setme)
{
    if (!tr_sys_file_can_read_cached() || std::empty(setme) || begin + std::size(setme) > desc.block_info.total_size()) {
        return false;
    }

    return for_each_file_chunk(
        desc,
        begin,
        std::size(setme),
        [&](tr_file_index_t const file_index, uint64_t const file_offset, uint64_t const done, uint64_t const n_bytes) {
            if (n_bytes == 0U) {
                return true;
            }

            // Only a file that's already open: opening one can wait on the disk.
            auto const file = open_files.get(desc.id, file_index, false);
            if (!file) {
                return false;
            }

            auto const io_lock = file->io_lock();
            return tr_sys_file_read_cached_at(file->fd(), std::data(setme) + done, n_bytes, file_offset);
        });
}

tr_error tr_ioWrite(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t const begin,
    std::span<uint8_t const> const writeme)
{
    TR_PROFILE_SCOPE("disk.write_block");
    if (std::empty(writeme)) {
        return {};
    }

    if (begin + std::size(writeme) > desc.block_info.total_size()) {
        return tr_error::from_errno(EINVAL);
    }

    auto error = tr_error{};
    for_each_file_chunk(
        desc,
        begin,
        std::size(writeme),
        [&](tr_file_index_t const file_index, uint64_t const file_offset, uint64_t const done, uint64_t const n_bytes) {
            write_bytes(desc, open_files, file_index, file_offset, writeme.subspan(done, n_bytes), error);
            return !error;
        });

    return error;
}

tr_error tr_ioRecalculateHash(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_piece_index_t const piece,
    tr_sha1_digest_t& setme)
{
    TR_PROFILE_SCOPE("disk.recalculate_piece_hash");
    auto const& block_info = desc.block_info;
    if (piece >= block_info.piece_count()) {
        return tr_error::from_errno(EINVAL);
    }

    auto error = tr_error{};
    auto buffer = std::array<uint8_t, TrBlockSize>{};
    auto const hash = tr_ioHashPiece(
        block_info,
        piece,
        [&desc, &open_files, &block_info, &buffer, &error](tr_block_index_t const block) -> std::span<uint8_t const> {
            auto const byte_span = block_info.byte_span_for_block(block);
            auto const data = std::span{ buffer }.first(static_cast<size_t>(byte_span.size()));
            error = tr_ioRead(desc, open_files, byte_span.begin, data);
            return !error ? data : std::span<uint8_t>{};
        });

    if (!hash) {
        if (!error) {
            error.set_from_errno(EIO);
        }
        return error;
    }

    setme = *hash;
    return {};
}

bool tr_ioTestPiece(tr_torrent const& tor, tr_piece_index_t const piece)
{
    auto hash = tr_sha1_digest_t{};
    return !tr_ioRecalculateHash(*tor.storage_descriptor(), tor.session->openFiles(), piece, hash) &&
        hash == tor.piece_hash(piece);
}
