// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t, uint8_t
#include <ctime> // time_t
#include <string_view>

#include <small/vector.hpp>

#include "libtransmission-app/torrent.h"

namespace tr::app
{

// One line of a tracker's status text, below its host name.
// Each client maps `phrase` to its own sentence, filled from the fields that phrase names.
struct TrackerStatusLine {
    enum class Phrase : uint8_t {
        // the last announce, at `time`
        LastAnnounceGotPeers, // `peer_count`
        LastAnnounceTimedOut,
        LastAnnounceError, // `message`

        // the announce state
        AnnounceInactive,
        AnnounceWaiting, // next announce at `time`
        AnnounceQueued,
        AnnounceActive, // started at `time`

        // the last scrape, at `time`
        LastScrapeGotCounts, // `seeder_count` and `leecher_count`
        LastScrapeNoCounts, // the tracker omitted a count
        LastScrapeError, // `message`

        // the scrape state; an inactive scrape has no line
        ScrapeWaiting, // next scrape at `time`
        ScrapeQueued,
        ScrapeActive, // started at `time`
    };

    Phrase phrase = {};
    time_t time = 0;
    int64_t peer_count = 0;
    int64_t seeder_count = 0;
    int64_t leecher_count = 0;
    std::string_view message = {}; // NOLINT(readability-redundant-member-init): lets designated initializers omit it

    [[nodiscard]] bool operator==(TrackerStatusLine const&) const noexcept = default;
};

using TrackerStatusLines = small::max_size_vector<TrackerStatusLine, 4>;

// `tracker`'s last announce and announce state, then, if `with_scrape`, its last scrape and scrape state.
// A backup tracker has no lines.
// The views refer to `tracker`, not to the returned container.
[[nodiscard]] TrackerStatusLines tracker_status_lines(TrackerStat const& tracker, bool with_scrape);

} // namespace tr::app
