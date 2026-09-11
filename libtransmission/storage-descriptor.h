// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/file-piece-map.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/types.h"

namespace tr
{

// Where a torrent's files may live, in search order: the download dir,
// then the incomplete dir, skipping either that is unset.
class SearchPaths
{
public:
    SearchPaths(std::string_view download_dir, std::string_view incomplete_dir) noexcept;

    [[nodiscard]] std::span<std::string_view const> span() const noexcept
    {
        return { std::data(paths_), n_paths_ };
    }

private:
    std::array<std::string_view, 2U> paths_;
    size_t n_paths_ = 0U;
};

/**
 * An immutable snapshot of everything disk IO needs to know about one
 * torrent's data on disk.
 *
 * Disk ops resolve paths against a descriptor instead of touching
 * `tr_torrent` or `tr_session`, so they can run on any thread and
 * nothing changes under them. `tr_torrent::storage_descriptor()` takes
 * a fresh snapshot whenever the torrent's storage state changes.
 */
struct StorageDescriptor {
    tr_torrent_id_t id = {};

    tr_block_info block_info;
    tr_torrent_files files;
    tr_file_piece_map fpm;

    // Only wanted files get preallocated when they're created.
    tr_bitfield files_wanted{ 0 };

    // The torrent name, for log messages.
    std::string name;

    std::string download_dir;
    std::string incomplete_dir;

    // Where new files are created. One of the two dirs above.
    std::string current_dir;

    tr_file_preallocation preallocation = tr_file_preallocation::None;

    // Whether new files get tr_torrent_files::PartialFileSuffix.
    bool partial_file_naming = false;

    // Where the file lives on disk. Looks under download_dir first and
    // incomplete_dir second, with and without the partial-file suffix.
    [[nodiscard]] std::optional<tr_torrent_files::FoundFile> find(tr_file_index_t file_index) const;
};

} // namespace tr
