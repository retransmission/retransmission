// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <optional>
#include <string_view>

#include <small/vector.hpp>

#include "libtransmission/storage-descriptor.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/types.h"

namespace tr
{

small::max_size_vector<std::string_view, 3> search_paths(
    std::string_view const download_dir,
    std::string_view const incomplete_dir,
    std::string_view const pending_move_dir)
{
    auto paths = small::max_size_vector<std::string_view, 3>{};

    for (auto const dir : { download_dir, incomplete_dir, pending_move_dir }) {
        if (!std::empty(dir) && std::ranges::find(paths, dir) == std::ranges::end(paths)) {
            paths.push_back(dir);
        }
    }

    return paths;
}

std::optional<tr_torrent_files::FoundFile> StorageDescriptor::find(tr_file_index_t const file_index) const
{
    return files.find(file_index, search_paths(download_dir, incomplete_dir, pending_move_dir));
}

} // namespace tr
