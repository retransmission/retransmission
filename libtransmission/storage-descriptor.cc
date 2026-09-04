// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <optional>
#include <string_view>

#include "libtransmission/storage-descriptor.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/types.h"

namespace tr
{

std::optional<tr_torrent_files::FoundFile> StorageDescriptor::find(tr_file_index_t const file_index) const
{
    auto paths = std::array<std::string_view, 2U>{};
    auto n_paths = size_t{};

    if (!std::empty(download_dir)) {
        paths[n_paths++] = download_dir;
    }

    if (!std::empty(incomplete_dir)) {
        paths[n_paths++] = incomplete_dir;
    }

    return files.find(file_index, { std::data(paths), n_paths });
}

} // namespace tr
