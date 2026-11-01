// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <compare>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

// Orders two torrents by `mode`'s keys. Equivalent when those keys tie, and
// always for `SortByName`: the caller then compares names in its own collation,
// then info hashes, so the order is total.
[[nodiscard]] std::partial_ordering compare_torrents(Torrent const& a, Torrent const& b, SortMode mode);

} // namespace tr::app
