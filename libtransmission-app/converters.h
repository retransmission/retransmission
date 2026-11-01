// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "libtransmission/converters.h"
#include "libtransmission/values.h"

#include "libtransmission-app/display-modes.h"

namespace tr::serializer
{

TR_DECLARE_CONVERTER(tr::app::ShowMode)
TR_DECLARE_CONVERTER(tr::app::SortMode)
TR_DECLARE_CONVERTER(tr::app::StatsMode)

// Bytes, and bytes per second: the units of `torrent_get`'s sizes and rates.
// libtransmission declares no converters for these types because their units
// vary by key, e.g. speed limits are in KB/s.
TR_DECLARE_CONVERTER(tr::Values::Storage)
TR_DECLARE_CONVERTER(tr::Values::Speed)

} // namespace tr::serializer
