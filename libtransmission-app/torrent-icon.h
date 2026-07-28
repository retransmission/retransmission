// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

namespace tr::app
{
// A multifile torrent's icon stacks a folder behind the mime type icon.
// These live here so that the gtk and qt clients draw the same composite.
//
// The mime type is the more important of the two, so it shifts down and right as
// far as it can and still fit; that offset is what leaves the folder's top and
// left edges showing. The folder is sized only to lengthen the edges it shows.
inline auto constexpr FolderIconScale = 0.6;
inline auto constexpr MimeIconScale = 0.9;
inline auto constexpr MimeIconOffset = 1.0 - MimeIconScale;

} // namespace tr::app
