// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission-app/display-modes.h>

#include <QMetaType>

using ShowMode = tr::app::ShowMode;
Q_DECLARE_METATYPE(ShowMode)
inline auto constexpr ShowModeCount = tr::app::ShowModeCount;

using SortMode = tr::app::SortMode;
Q_DECLARE_METATYPE(SortMode)

using StatsMode = tr::app::StatsMode;
Q_DECLARE_METATYPE(StatsMode)
inline auto constexpr StatsModeCount = tr::app::StatsModeCount;
