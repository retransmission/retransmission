// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <ctime> // time_t
#include <iterator> // std::begin(), std::empty(), std::end(), std::size()
#include <ostream>
#include <string_view>
#include <utility> // std::pair
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/shared-string.h>
#include <libtransmission/types.h>

#include "libtransmission-app/torrent.h"
#include "libtransmission-app/tracker-status.h"

using namespace std::literals;
using tr::app::tracker_status_lines;
using tr::app::TrackerStat;
using tr::app::TrackerStatusLine;
using Phrase = TrackerStatusLine::Phrase;

namespace tr::app
{
// so failures print the lines instead of their bytes
void PrintTo(TrackerStatusLine const& line, std::ostream* os)
{
    *os << "{ phrase " << static_cast<int>(line.phrase) << ", time " << line.time << ", peers " << line.peer_count
        << ", seeders " << line.seeder_count << ", leechers " << line.leecher_count << ", message '" << line.message << "' }";
}
} // namespace tr::app

namespace
{

auto constexpr LastAnnounceStart = time_t{ 1000 };
auto constexpr LastAnnounce = time_t{ 1010 };
auto constexpr NextAnnounce = time_t{ 2800 };
auto constexpr LastScrapeStart = time_t{ 1100 };
auto constexpr LastScrape = time_t{ 1105 };
auto constexpr NextScrape = time_t{ 2900 };

// a running torrent's primary tracker that has announced and scraped, and waits to do both again
[[nodiscard]] TrackerStat make_tracker()
{
    auto st = TrackerStat{};
    st.announce_state = TR_TRACKER_WAITING;
    st.has_announced = true;
    st.last_announce_peer_count = 50;
    st.last_announce_result = tr::shared_string{ "Success" };
    st.last_announce_start_time = LastAnnounceStart;
    st.last_announce_succeeded = true;
    st.last_announce_time = LastAnnounce;
    st.next_announce_time = NextAnnounce;
    st.scrape_state = TR_TRACKER_WAITING;
    st.has_scraped = true;
    st.last_scrape_start_time = LastScrapeStart;
    st.last_scrape_succeeded = true;
    st.last_scrape_time = LastScrape;
    st.leecher_count = 7;
    st.seeder_count = 12;
    st.next_scrape_time = NextScrape;
    return st;
}

[[nodiscard]] std::vector<TrackerStatusLine> lines_of(TrackerStat const& st, bool const with_scrape)
{
    auto const lines = tracker_status_lines(st, with_scrape);
    return { std::begin(lines), std::end(lines) };
}

[[nodiscard]] std::vector<Phrase> phrases_of(TrackerStat const& st, bool const with_scrape)
{
    auto ret = std::vector<Phrase>{};
    for (auto const& line : tracker_status_lines(st, with_scrape)) {
        ret.push_back(line.phrase);
    }
    return ret;
}

} // namespace

TEST(AppTrackerStatusTest, listsLastAnnounceAnnounceStateLastScrapeAndScrapeStateInOrder)
{
    auto const expected = std::vector<TrackerStatusLine>{
        { .phrase = Phrase::LastAnnounceGotPeers, .time = LastAnnounce, .peer_count = 50 },
        { .phrase = Phrase::AnnounceWaiting, .time = NextAnnounce },
        { .phrase = Phrase::LastScrapeGotCounts, .time = LastScrape, .seeder_count = 12, .leecher_count = 7 },
        { .phrase = Phrase::ScrapeWaiting, .time = NextScrape },
    };
    EXPECT_EQ(expected, lines_of(make_tracker(), true));
}

TEST(AppTrackerStatusTest, omitsScrapeLinesUnlessAsked)
{
    auto const expected = std::vector{ Phrase::LastAnnounceGotPeers, Phrase::AnnounceWaiting };
    EXPECT_EQ(expected, phrases_of(make_tracker(), false));
}

TEST(AppTrackerStatusTest, backupTrackersHaveNoLines)
{
    auto st = make_tracker();
    st.is_backup = true;
    EXPECT_TRUE(std::empty(tracker_status_lines(st, true)));
}

TEST(AppTrackerStatusTest, lastAnnounceTimedOut)
{
    // libtransmission also sets an error message for timeouts
    auto st = make_tracker();
    st.last_announce_succeeded = false;
    st.last_announce_timed_out = true;
    st.last_announce_result = tr::shared_string{ "Tracker announce timed out" };

    auto const lines = tracker_status_lines(st, false);
    ASSERT_EQ(2U, std::size(lines));
    EXPECT_EQ((TrackerStatusLine{ .phrase = Phrase::LastAnnounceTimedOut, .time = LastAnnounce }), lines[0]);
}

TEST(AppTrackerStatusTest, lastAnnounceError)
{
    auto st = make_tracker();
    st.last_announce_succeeded = false;
    st.last_announce_result = tr::shared_string{ "<b>Unregistered torrent</b>" };

    auto const lines = tracker_status_lines(st, false);
    ASSERT_EQ(2U, std::size(lines));
    EXPECT_EQ(
        (TrackerStatusLine{ .phrase = Phrase::LastAnnounceError,
                            .time = LastAnnounce,
                            .message = "<b>Unregistered torrent</b>"sv }),
        lines[0]);
}

TEST(AppTrackerStatusTest, noLastAnnounceBeforeTheFirstAnnounce)
{
    auto st = make_tracker();
    st.has_announced = false;
    EXPECT_EQ(std::vector{ Phrase::AnnounceWaiting }, phrases_of(st, false));
}

TEST(AppTrackerStatusTest, inactiveAnnounceHidesTheLastAnnounce)
{
    // e.g. a paused torrent
    auto st = make_tracker();
    st.announce_state = TR_TRACKER_INACTIVE;
    st.last_announce_succeeded = false;
    st.last_announce_result = tr::shared_string{ "Could not connect to tracker" };
    EXPECT_EQ(std::vector{ Phrase::AnnounceInactive }, phrases_of(st, false));
}

TEST(AppTrackerStatusTest, announceStates)
{
    auto st = make_tracker();

    st.announce_state = TR_TRACKER_QUEUED;
    EXPECT_EQ((std::vector{ Phrase::LastAnnounceGotPeers, Phrase::AnnounceQueued }), phrases_of(st, false));

    st.announce_state = TR_TRACKER_ACTIVE;
    auto const lines = tracker_status_lines(st, false);
    ASSERT_EQ(2U, std::size(lines));
    EXPECT_EQ((TrackerStatusLine{ .phrase = Phrase::AnnounceActive, .time = LastAnnounceStart }), lines[1]);
}

TEST(AppTrackerStatusTest, lastScrapeError)
{
    auto st = make_tracker();
    st.last_scrape_succeeded = false;
    st.last_scrape_result = tr::shared_string{ "Tracker scrape timed out" };

    auto const lines = tracker_status_lines(st, true);
    ASSERT_EQ(4U, std::size(lines));
    EXPECT_EQ(
        (TrackerStatusLine{ .phrase = Phrase::LastScrapeError, .time = LastScrape, .message = "Tracker scrape timed out"sv }),
        lines[2]);
}

TEST(AppTrackerStatusTest, lastScrapeWithoutCounts)
{
    // a tracker may omit either count from its scrape response
    for (auto const [seeders, leechers] : { std::pair{ -1, 7 }, std::pair{ 12, -1 }, std::pair{ -1, -1 } }) {
        auto st = make_tracker();
        st.seeder_count = seeders;
        st.leecher_count = leechers;

        auto const lines = tracker_status_lines(st, true);
        ASSERT_EQ(4U, std::size(lines));
        EXPECT_EQ((TrackerStatusLine{ .phrase = Phrase::LastScrapeNoCounts, .time = LastScrape }), lines[2]);
    }
}

TEST(AppTrackerStatusTest, lastScrapeWithZeroCounts)
{
    auto st = make_tracker();
    st.seeder_count = 0;
    st.leecher_count = 0;

    auto const lines = tracker_status_lines(st, true);
    ASSERT_EQ(4U, std::size(lines));
    EXPECT_EQ((TrackerStatusLine{ .phrase = Phrase::LastScrapeGotCounts, .time = LastScrape }), lines[2]);
}

TEST(AppTrackerStatusTest, scrapeStates)
{
    auto st = make_tracker();
    st.has_scraped = false;

    st.scrape_state = TR_TRACKER_INACTIVE;
    EXPECT_EQ((std::vector{ Phrase::LastAnnounceGotPeers, Phrase::AnnounceWaiting }), phrases_of(st, true));

    st.scrape_state = TR_TRACKER_QUEUED;
    EXPECT_EQ(
        (std::vector{ Phrase::LastAnnounceGotPeers, Phrase::AnnounceWaiting, Phrase::ScrapeQueued }),
        phrases_of(st, true));

    st.scrape_state = TR_TRACKER_ACTIVE;
    auto const lines = tracker_status_lines(st, true);
    ASSERT_EQ(3U, std::size(lines));
    EXPECT_EQ((TrackerStatusLine{ .phrase = Phrase::ScrapeActive, .time = LastScrapeStart }), lines[2]);
}

TEST(AppTrackerStatusTest, inactiveScrapeKeepsTheLastScrape)
{
    auto st = make_tracker();
    st.scrape_state = TR_TRACKER_INACTIVE;
    EXPECT_EQ(
        (std::vector{ Phrase::LastAnnounceGotPeers, Phrase::AnnounceWaiting, Phrase::LastScrapeGotCounts }),
        phrases_of(st, true));
}
