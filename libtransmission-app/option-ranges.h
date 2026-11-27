// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint16_t
#include <limits>

namespace tr::app
{

// The values that a limit's spin box offers, inclusive. Each spans what
// libtransmission stores, so a spin never clamps a value that another client
// or the session's settings file set; where libtransmission stores a wider
// type, the range stops at a round number that fits in an `int`.
template<typename T>
struct OptionRange {
    T min;
    T max;
};

// kB/s, for a torrent's limits and the session's normal and alt-speed limits
inline constexpr auto SpeedLimitRange = OptionRange<int>{ 0, 999'999'999 };

// for a torrent's seed ratio limit and the session's
inline constexpr auto SeedRatioLimitRange = OptionRange<double>{ 0.0, 999'999'999.0 };

// minutes, for a torrent's idle seeding limit and the session's
inline constexpr auto SeedIdleLimitRange = OptionRange<int>{ 1, std::numeric_limits<uint16_t>::max() };

// for a torrent's peer limit and the session's per-torrent and global limits
inline constexpr auto PeerLimitRange = OptionRange<int>{ 1, std::numeric_limits<uint16_t>::max() };

} // namespace tr::app
