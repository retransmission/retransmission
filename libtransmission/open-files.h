// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint> // for uintX_t
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include "libtransmission/file.h" // tr_sys_file_t
#include "libtransmission/lru-cache.h"
#include "libtransmission/types.h"

// A pool of open files that are cached while reading / writing torrents' data.
//
// The pool is thread-safe, and hands out files pinned: the fd stays open
// for as long as any pin to it exists, even if the pool evicts or closes
// the entry in the meantime. Whichever thread drops the last pin closes
// the fd.
class tr_open_files
{
    struct File {
        File() noexcept = default;
        File(tr_sys_file_t const fd, bool const writable) noexcept
            : fd_{ fd }
            , writable_{ writable }
        {
        }

        File(File const&) = delete;
        File& operator=(File const&) = delete;
        File(File&&) = delete;
        File& operator=(File&&) = delete;

        ~File()
        {
            if (fd_ != TR_BAD_SYS_FILE) {
                tr_sys_file_close(fd_);
            }
        }

        tr_sys_file_t fd_ = TR_BAD_SYS_FILE;
        bool writable_ = false;
    };

public:
    // A pinned open file, or empty. Models enough of std::optional's
    // surface that callers can test and dereference it like one.
    class Pinned
    {
    public:
        Pinned() noexcept = default;

        [[nodiscard]] bool has_value() const noexcept
        {
            return file_ != nullptr;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return has_value();
        }

        [[nodiscard]] tr_sys_file_t operator*() const noexcept
        {
            return file_->fd_;
        }

    private:
        friend class tr_open_files;

        explicit Pinned(std::shared_ptr<File const> file) noexcept
            : file_{ std::move(file) }
        {
        }

        std::shared_ptr<File const> file_;
    };

    // Get the file if it's already open in the pool, or an empty pin.
    [[nodiscard]] Pinned get(tr_torrent_id_t tor_id, tr_file_index_t file_num, bool writable);

    // Get the file, opening (and optionally preallocating) it if needed.
    [[nodiscard]] Pinned get(
        tr_torrent_id_t tor_id,
        tr_file_index_t file_num,
        bool writable,
        std::string_view filename,
        tr_file_preallocation allocation,
        uint64_t file_size);

    void close_all();
    void close_torrent(tr_torrent_id_t tor_id);
    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num);

private:
    using Key = std::pair<tr_torrent_id_t, tr_file_index_t>;

    [[nodiscard]] static Key make_key(tr_torrent_id_t tor_id, tr_file_index_t file_num) noexcept
    {
        return std::make_pair(tor_id, file_num);
    }

    static constexpr size_t MaxOpenFiles = 32U;

    std::mutex mutex_;
    tr_lru_cache<Key, std::shared_ptr<File const>, MaxOpenFiles> pool_;
};
