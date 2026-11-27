// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/tracker-status.h"

#include "libtransmission/types.h" // tr_tracker_state

#include "libtransmission-app/torrent.h"

namespace tr::app
{
namespace
{

using Phrase = TrackerStatusLine::Phrase;

void add_announce_lines(TrackerStat const& tracker, TrackerStatusLines& lines)
{
    // a stopped torrent's last announce is stale
    if (tracker.has_announced && tracker.announce_state != TR_TRACKER_INACTIVE) {
        if (tracker.last_announce_succeeded) {
            lines.push_back(
                { .phrase = Phrase::LastAnnounceGotPeers,
                  .time = tracker.last_announce_time,
                  .peer_count = tracker.last_announce_peer_count });
        } else if (tracker.last_announce_timed_out) {
            lines.push_back({ .phrase = Phrase::LastAnnounceTimedOut, .time = tracker.last_announce_time });
        } else {
            lines.push_back(
                { .phrase = Phrase::LastAnnounceError,
                  .time = tracker.last_announce_time,
                  .message = tracker.last_announce_result.sv() });
        }
    }

    switch (tracker.announce_state) {
    case TR_TRACKER_INACTIVE:
        lines.push_back({ .phrase = Phrase::AnnounceInactive });
        break;

    case TR_TRACKER_WAITING:
        lines.push_back({ .phrase = Phrase::AnnounceWaiting, .time = tracker.next_announce_time });
        break;

    case TR_TRACKER_QUEUED:
        lines.push_back({ .phrase = Phrase::AnnounceQueued });
        break;

    case TR_TRACKER_ACTIVE:
        lines.push_back({ .phrase = Phrase::AnnounceActive, .time = tracker.last_announce_start_time });
        break;
    }
}

void add_scrape_lines(TrackerStat const& tracker, TrackerStatusLines& lines)
{
    if (tracker.has_scraped) {
        if (!tracker.last_scrape_succeeded) {
            lines.push_back(
                { .phrase = Phrase::LastScrapeError,
                  .time = tracker.last_scrape_time,
                  .message = tracker.last_scrape_result.sv() });
        } else if (tracker.seeder_count >= 0 && tracker.leecher_count >= 0) {
            lines.push_back(
                { .phrase = Phrase::LastScrapeGotCounts,
                  .time = tracker.last_scrape_time,
                  .seeder_count = tracker.seeder_count,
                  .leecher_count = tracker.leecher_count });
        } else {
            lines.push_back({ .phrase = Phrase::LastScrapeNoCounts, .time = tracker.last_scrape_time });
        }
    }

    switch (tracker.scrape_state) {
    case TR_TRACKER_INACTIVE:
        break;

    case TR_TRACKER_WAITING:
        lines.push_back({ .phrase = Phrase::ScrapeWaiting, .time = tracker.next_scrape_time });
        break;

    case TR_TRACKER_QUEUED:
        lines.push_back({ .phrase = Phrase::ScrapeQueued });
        break;

    case TR_TRACKER_ACTIVE:
        lines.push_back({ .phrase = Phrase::ScrapeActive, .time = tracker.last_scrape_start_time });
        break;
    }
}

} // namespace

TrackerStatusLines tracker_status_lines(TrackerStat const& tracker, bool const with_scrape)
{
    auto lines = TrackerStatusLines{};

    if (tracker.is_backup) {
        return lines;
    }

    add_announce_lines(tracker, lines);

    if (with_scrape) {
        add_scrape_lines(tracker, lines);
    }

    return lines;
}

} // namespace tr::app
