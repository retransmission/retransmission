// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <optional>
#include <string>

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/file-piece-map.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/types.h"

namespace tr
{

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
