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

// A pool of open files that are cached while reading / writing torrents' data
class tr_open_files
{
public:
    // An open descriptor, shared by everyone using the file.
    //
    // The pool holds one reference and hands out more. Evicting the entry
    // or closing the file drops the pool's reference only: the descriptor
    // survives until its last user is done with it. That is what lets a
    // caller keep reading through a descriptor that close_torrent() or an
    // LRU eviction has already removed from the pool, without the read
    // landing on a descriptor the kernel has since reassigned.
    class OpenFile
    {
    public:
        OpenFile(tr_sys_file_t fd, bool writable) noexcept
            : fd_{ fd }
            , writable_{ writable }
        {
        }

        OpenFile(OpenFile const&) = delete;
        OpenFile(OpenFile&&) = delete;
        OpenFile& operator=(OpenFile const&) = delete;
        OpenFile& operator=(OpenFile&&) = delete;
        ~OpenFile();

        [[nodiscard]] constexpr auto fd() const noexcept
        {
            return fd_;
        }

        [[nodiscard]] constexpr auto is_writable() const noexcept
        {
            return writable_;
        }

        // Hold this around positioned reads and writes.
        //
        // Everyone using a file shares one descriptor, and on Windows
        // positioned IO moves that descriptor's file pointer, so two
        // threads issuing it at once read or write through each other.
        // POSIX pread and pwrite take the offset per call and don't, so
        // the lock is absent there and reads stay parallel.
        // NOLINTNEXTLINE(readability-convert-member-functions-to-static): locks io_mutex_ on Windows
        [[nodiscard]] std::unique_lock<std::mutex> io_lock() const
        {
#ifdef _WIN32
            return std::unique_lock{ io_mutex_ };
#else
            return {};
#endif
        }

    private:
        tr_sys_file_t fd_ = TR_BAD_SYS_FILE;
        bool writable_ = false;

#ifdef _WIN32
        mutable std::mutex io_mutex_;
#endif
    };

    // A reference to a pooled descriptor. Empty if the file isn't available.
    using Handle = std::shared_ptr<OpenFile const>;

    [[nodiscard]] Handle get(tr_torrent_id_t tor_id, tr_file_index_t file_num, bool writable);

    [[nodiscard]] Handle get(
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

    // Guards pool_ only. Files are opened outside it: see get().
    std::mutex mutex_;
    tr_lru_cache<Key, Handle, MaxOpenFiles> pool_;
};
