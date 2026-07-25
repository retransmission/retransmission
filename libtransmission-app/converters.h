// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>

#include "libtransmission/converters.h"

#include "libtransmission-app/display-modes.h"

namespace tr::serializer
{

TR_DECLARE_CONVERTER(std::chrono::sys_seconds)
TR_DECLARE_CONVERTER(tr::app::ShowMode)
TR_DECLARE_CONVERTER(tr::app::SortMode)
TR_DECLARE_CONVERTER(tr::app::StatsMode)

} // namespace tr::serializer
