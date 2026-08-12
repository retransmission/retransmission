// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // uint64_t
#include <optional>
#include <string>

#include "libtransmission/block-info.h"
#include "libtransmission/file-piece-map.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/types.h"

namespace tr
{

/**
 * An immutable snapshot of everything path resolution needs for one
 * torrent's data on disk.
 *
 * Disk workers resolve IO against a descriptor instead of touching
 * `tr_torrent` or `tr_session`, so nothing can change under them. The
 * session thread takes a fresh snapshot whenever the torrent's storage
 * state changes. Only barrier ops change that state, so ops admitted
 * after a barrier always resolve against the post-barrier snapshot.
 */
struct StorageDescriptor {
    // Which snapshot of the torrent's storage state this is.
    // tr_torrent bumps it whenever dirs, subpaths, or metainfo change.
    uint64_t generation = 0U;

    tr_block_info block_info;
    tr_torrent_files files;
    tr_file_piece_map fpm;

    std::string download_dir;
    std::string incomplete_dir;

    // Where the file lives on disk, searching download_dir first and
    // incomplete_dir second, with and without the partial-file suffix.
    [[nodiscard]] std::optional<tr_torrent_files::FoundFile> find(tr_file_index_t file_index) const;
};

} // namespace tr
