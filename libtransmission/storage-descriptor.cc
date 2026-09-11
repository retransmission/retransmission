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

SearchPaths::SearchPaths(std::string_view const download_dir, std::string_view const incomplete_dir) noexcept
{
    if (!std::empty(download_dir)) {
        paths_[n_paths_++] = download_dir;
    }

    if (!std::empty(incomplete_dir)) {
        paths_[n_paths_++] = incomplete_dir;
    }
}

std::optional<tr_torrent_files::FoundFile> StorageDescriptor::find(tr_file_index_t const file_index) const
{
    return files.find(file_index, SearchPaths{ download_dir, incomplete_dir }.span());
}

} // namespace tr
