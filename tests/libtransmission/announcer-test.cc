// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef> // std::byte, size_t
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include <libtransmission/announcer-common.h>
#include <libtransmission/net.h>
#include <libtransmission/transmission.h>
#include <libtransmission/types.h>
#include <libtransmission/utils.h> // tr_time()

#include "loopback-tracker.h"
#include "test-fixtures.h"

using AnnouncerTest = ::tr::test::TransmissionTest;
using ManualAnnounceTest = ::tr::test::LoopbackTrackerTest;
using TrackerListChangeTest = ::tr::test::LoopbackTrackerTest;
using ::tr::test::waitFor;

using namespace std::literals;

static char const* const LogName = "LogName";

TEST_F(AnnouncerTest, parseHttpAnnounceResponseNoPeers)
{
    // clang-format off
    auto constexpr NoPeers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers" "0:"
            "11:external ip" "4:\x01\x02\x03\x04"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, NoPeers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    auto addr = tr_address::from_string("1.2.3.4");
    EXPECT_TRUE(addr.has_value());
    assert(addr.has_value());
    EXPECT_EQ(*addr, response.external_ip);
    EXPECT_EQ(0U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponsePexCompact)
{
    // clang-format off
    auto constexpr IPv4Peers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers"
            "6:\x7F\x00\x00\x01\xfc\x27"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));

    if (std::size(response.pex) == 1) {
        EXPECT_EQ("127.0.0.1:64551"sv, response.pex[0].display_name());
    }
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponsePexList)
{
    // clang-format off
    auto constexpr IPv4Peers =
        "d"
            "8:complete" "i3e"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers"
            "l"
                "d"
                    "7:peer id" "20:-TR300Z-0123456789AB"
                    "2:ip" "7:8.8.4.4"
                    "4:port" "i53e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, IPv4Peers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(""sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
    EXPECT_EQ(1U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));

    if (std::size(response.pex) == 1) {
        EXPECT_EQ("8.8.4.4:53"sv, response.pex[0].display_name());
    }
}

TEST_F(AnnouncerTest, parseHttpAnnounceResponseFailureReason)
{
    // clang-format off
    auto constexpr NoPeers =
        "d"
            "8:complete" "i3e"
            "14:failure reason" "6:foobar"
            "10:downloaded" "i2e"
            "10:incomplete" "i0e"
            "8:interval" "i1803e"
            "12:min interval" "i1800e"
            "5:peers" "0:"
        "e"sv;
    // clang-format on

    auto response = tr_announce_response{};
    tr_announcerParseHttpAnnounceResponse(response, NoPeers, LogName);
    EXPECT_EQ(1803, response.interval);
    EXPECT_EQ(1800, response.min_interval);
    EXPECT_EQ(3, response.seeders);
    EXPECT_EQ(0, response.leechers);
    EXPECT_EQ(2, response.downloads);
    EXPECT_EQ(0U, std::size(response.pex));
    EXPECT_EQ(0U, std::size(response.pex6));
    EXPECT_EQ("foobar"sv, response.errmsg);
    EXPECT_EQ(""sv, response.warning);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMulti)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:bbbbbbbbbbbbbbbbbbbb"
                "d"
                    "8:complete" "i4e"
                    "10:incomplete" "i5e"
                    "10:downloaded" "i6e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3U;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(4, response.rows[1].seeders);
    EXPECT_EQ(5, response.rows[1].leechers);
    EXPECT_EQ(6, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMultiWithUnexpected)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:bbbbbbbbbbbbbbbbbbbb"
                "d"
                    "8:complete" "i4e"
                    "10:incomplete" "i5e"
                    "10:downloaded" "i6e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"

                "20:dddddddddddddddddddd"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3U;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(4, response.rows[1].seeders);
    EXPECT_EQ(5, response.rows[1].leechers);
    EXPECT_EQ(6, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}

TEST_F(AnnouncerTest, parseHttpScrapeResponseMultiWithMissing)
{
    // clang-format off
    auto constexpr ResponseBenc =
        "d"
            "5:files"
            "d"
                "20:aaaaaaaaaaaaaaaaaaaa"
                "d"
                    "8:complete" "i1e"
                    "10:incomplete" "i2e"
                    "10:downloaded" "i3e"
                "e"

                "20:cccccccccccccccccccc"
                "d"
                    "8:complete" "i7e"
                    "10:incomplete" "i8e"
                    "10:downloaded" "i9e"
                "e"
            "e"
        "e"sv;
    // clang-format on

    auto response = tr_scrape_response{};
    std::fill_n(std::data(response.rows[0].info_hash), std::size(response.rows[0].info_hash), std::byte{ 'a' });
    std::fill_n(std::data(response.rows[1].info_hash), std::size(response.rows[1].info_hash), std::byte{ 'b' });
    std::fill_n(std::data(response.rows[2].info_hash), std::size(response.rows[2].info_hash), std::byte{ 'c' });
    response.row_count = 3U;
    tr_announcerParseHttpScrapeResponse(response, ResponseBenc, LogName);

    EXPECT_EQ(1, response.rows[0].seeders);
    EXPECT_EQ(2, response.rows[0].leechers);
    EXPECT_EQ(3, response.rows[0].downloads);

    EXPECT_EQ(std::nullopt, response.rows[1].seeders);
    EXPECT_EQ(std::nullopt, response.rows[1].leechers);
    EXPECT_EQ(std::nullopt, response.rows[1].downloads);

    EXPECT_EQ(7, response.rows[2].seeders);
    EXPECT_EQ(8, response.rows[2].leechers);
    EXPECT_EQ(9, response.rows[2].downloads);
}

// --- TRYING ORDER

namespace
{
// advancing a fresh tr_tracker_trying_order n_trackers times walks the
// full trying order once; collect it for asserting on later laps
[[nodiscard]] std::vector<size_t> advanceFullLap(tr_tracker_trying_order& order, size_t n_trackers)
{
    auto lap = std::vector<size_t>{};
    lap.reserve(n_trackers);

    for (size_t i = 0; i < n_trackers; ++i) {
        auto const current = order.advance();
        assert(current.has_value());
        lap.push_back(*current);
    }

    return lap;
}
} // namespace

TEST_F(AnnouncerTest, tryingOrderVisitsEveryTrackerOncePerLap)
{
    static auto constexpr TrackerCount = size_t{ 5 };
    auto order = tr_tracker_trying_order{ TrackerCount };

    // no tracker is current until the first advance()
    EXPECT_EQ(std::nullopt, order.current());

    // one lap visits each tracker exactly once...
    auto const lap = advanceFullLap(order, TrackerCount);
    EXPECT_EQ(TrackerCount, std::size(std::set<size_t>{ std::begin(lap), std::end(lap) }));
    for (auto const idx : lap) {
        EXPECT_LT(idx, TrackerCount);
    }

    // ...and the next lap repeats the same order
    EXPECT_EQ(lap, advanceFullLap(order, TrackerCount));
}

TEST_F(AnnouncerTest, tryingOrderIsShuffled)
{
    static auto constexpr TrackerCount = size_t{ 6 };

    auto distinct_orders = std::set<std::vector<size_t>>{};
    for (auto i = 0U; i < 32U; ++i) {
        auto order = tr_tracker_trying_order{ TrackerCount };
        distinct_orders.insert(advanceFullLap(order, TrackerCount));
    }

    // a fixed start-at-the-first-tracker order would collapse
    // all 32 orders into a single entry
    EXPECT_GT(std::size(distinct_orders), 1U);
}

TEST_F(AnnouncerTest, tryingOrderPromoteMovesCurrentToFront)
{
    static auto constexpr TrackerCount = size_t{ 5 };
    auto order = tr_tracker_trying_order{ TrackerCount };
    auto const lap = advanceFullLap(order, TrackerCount);

    // walk into the second lap, to the second tracker in the order
    order.advance();
    order.advance();
    EXPECT_EQ(lap[1], order.current());

    // promoting keeps it current...
    order.promote_current();
    EXPECT_EQ(lap[1], order.current());

    // ...and the rest of the order now follows it
    auto const expected = std::vector<size_t>{ lap[0], lap[2], lap[3], lap[4], lap[1] };
    EXPECT_EQ(expected, advanceFullLap(order, TrackerCount));
}

TEST_F(AnnouncerTest, tryingOrderSetCurrent)
{
    static auto constexpr TrackerCount = size_t{ 4 };
    auto order = tr_tracker_trying_order{ TrackerCount };
    auto const lap = advanceFullLap(order, TrackerCount);

    // selecting a tracker by index leaves the trying order unchanged
    order.set_current(lap[2]);
    EXPECT_EQ(lap[2], order.current());
    EXPECT_EQ(lap[3], order.advance());

    // nullopt unsets the current tracker, and advancing
    // from there restarts at the front of the order
    order.set_current(std::nullopt);
    EXPECT_EQ(std::nullopt, order.current());
    EXPECT_EQ(lap[0], order.advance());
}

TEST_F(AnnouncerTest, tryingOrderEmpty)
{
    auto order = tr_tracker_trying_order{ 0U };

    EXPECT_EQ(std::nullopt, order.current());
    EXPECT_EQ(std::nullopt, order.advance());

    order.promote_current();
    order.set_current(std::nullopt);
    EXPECT_EQ(std::nullopt, order.current());
}

// --- MANUAL ANNOUNCE

TEST_F(ManualAnnounceTest, waitsForMinInterval)
{
    tracker_.setReply("/a", 3600, 1800);
    auto* const tor = startTorrent({ "/a" });

    auto const before = trackerView(tor, "/a");
    EXPECT_EQ(before.lastAnnounceTime + 1800, before.minAnnounceTime);
    EXPECT_EQ(before.lastAnnounceTime + 3600, before.nextAnnounceTime);

    tr_torrentManualUpdate(tor);

    auto const after = trackerView(tor, "/a");
    EXPECT_EQ(TR_TRACKER_WAITING, after.announceState);
    EXPECT_EQ(after.minAnnounceTime, after.nextAnnounceTime);
    EXPECT_EQ(1U, tracker_.announceCount("/a"));
}

TEST_F(ManualAnnounceTest, ignoreMinIntervalAnnouncesNow)
{
    tracker_.setReply("/a", 3600, 1800);
    auto* const tor = startTorrent({ "/a" });

    tr_torrentManualUpdate(tor, {}, tr_reannounce_flags::IgnoreMinInterval);

    EXPECT_TRUE(waitFor([this]() { return tracker_.announceCount("/a") == 2U; }, 5s));
}

TEST_F(ManualAnnounceTest, onlyTiersInTheirMinIntervalWait)
{
    tracker_.setReply("/ready", 3600, 1);
    tracker_.setReply("/cooling", 3600, 1800);
    auto* const tor = startTorrent({ "/ready", "/cooling" });

    auto const ready_at = trackerView(tor, "/ready").minAnnounceTime;
    EXPECT_TRUE(waitForInSessionThread([ready_at]() { return tr_time() >= ready_at; }, 5000));

    tr_torrentManualUpdate(tor);

    EXPECT_TRUE(waitFor([this]() { return tracker_.announceCount("/ready") == 2U; }, 5s));
    auto const cooling = trackerView(tor, "/cooling");
    EXPECT_EQ(cooling.minAnnounceTime, cooling.nextAnnounceTime);
    EXPECT_EQ(1U, tracker_.announceCount("/cooling"));
}

TEST_F(ManualAnnounceTest, neverDelaysAScheduledAnnounce)
{
    // A failed announce retries sooner than the default min interval ends
    tracker_.setFailure("/a", "try again");
    auto* const tor = startTorrent({ "/a" });

    auto const before = trackerView(tor, "/a");
    EXPECT_LT(before.nextAnnounceTime, before.minAnnounceTime);

    tr_torrentManualUpdate(tor);

    EXPECT_EQ(before.nextAnnounceTime, trackerView(tor, "/a").nextAnnounceTime);
}

TEST_F(ManualAnnounceTest, trackerIdsSelectTheirTiers)
{
    tracker_.setReply("/a", 3600, 1800);
    tracker_.setReply("/b", 3600, 1800);
    auto* const tor = startTorrent({ "/a", "/b" });

    // ids that aren't this torrent's trackers select nothing
    auto const unknown_ids = std::array{ std::numeric_limits<tr_tracker_id_t>::max() };
    tr_torrentManualUpdate(tor, unknown_ids);

    auto const a_before = trackerView(tor, "/a");
    auto const b_before = trackerView(tor, "/b");
    EXPECT_EQ(a_before.lastAnnounceTime + 3600, a_before.nextAnnounceTime);
    EXPECT_EQ(b_before.lastAnnounceTime + 3600, b_before.nextAnnounceTime);

    auto const b_ids = std::array{ b_before.id };
    tr_torrentManualUpdate(tor, b_ids);

    auto const a_after = trackerView(tor, "/a");
    auto const b_after = trackerView(tor, "/b");
    EXPECT_EQ(a_before.nextAnnounceTime, a_after.nextAnnounceTime);
    EXPECT_EQ(b_after.minAnnounceTime, b_after.nextAnnounceTime);
}

TEST_F(ManualAnnounceTest, waitsForTheResponseToAnInFlightAnnounce)
{
    tracker_.setReply("/a", 3600, 1800);
    tracker_.holdReplies();
    auto* const tor = addTorrent({ "/a" });
    tr_torrentStart(tor);
    EXPECT_TRUE(waitFor([this]() { return tracker_.announceCount("/a") == 1U; }, 5s));

    tr_torrentManualUpdate(tor);
    EXPECT_EQ(TR_TRACKER_ACTIVE, trackerView(tor, "/a").announceState);

    tracker_.releaseReplies();
    EXPECT_TRUE(waitFor([this, tor]() { return trackerView(tor, "/a").hasAnnounced; }, 5s));

    // the manual announce waits out the min interval that the response set
    auto const view = trackerView(tor, "/a");
    EXPECT_EQ(view.lastAnnounceTime + 1800, view.minAnnounceTime);
    EXPECT_EQ(view.minAnnounceTime, view.nextAnnounceTime);
    EXPECT_EQ(1U, tracker_.announceCount("/a"));
}

// --- TRACKER LIST CHANGES

TEST_F(TrackerListChangeTest, keptTierKeepsItsSchedule)
{
    tracker_.setReply("/a", 3600, 1800);
    auto* const tor = startTorrent({ "/a" });
    auto const before = trackerView(tor, "/a");

    setTrackerList(tor, { "/a", "/b" });

    auto const after = trackerView(tor, "/a");
    EXPECT_EQ(TR_TRACKER_WAITING, after.announceState);
    EXPECT_EQ(before.nextAnnounceTime, after.nextAnnounceTime);
    EXPECT_EQ(before.minAnnounceTime, after.minAnnounceTime);
    EXPECT_EQ(before.lastAnnounceTime, after.lastAnnounceTime);
    EXPECT_EQ(before.lastAnnounceSucceeded, after.lastAnnounceSucceeded);
    EXPECT_EQ(before.nextScrapeTime, after.nextScrapeTime);
}

TEST_F(TrackerListChangeTest, keptTierGetsItsInFlightResponse)
{
    tracker_.setReply("/a", 3600, 1800);
    tracker_.holdReplies();
    auto* const tor = addTorrent({ "/a" });
    tr_torrentStart(tor);
    EXPECT_TRUE(waitFor([this]() { return tracker_.announceCount("/a") == 1U; }, 5s));

    setTrackerList(tor, { "/a", "/b" });
    EXPECT_EQ(TR_TRACKER_ACTIVE, trackerView(tor, "/a").announceState);

    tracker_.releaseReplies();
    EXPECT_TRUE(waitFor([this, tor]() { return trackerView(tor, "/a").hasAnnounced; }, 5s));

    auto const view = trackerView(tor, "/a");
    EXPECT_EQ(view.lastAnnounceTime + 1800, view.minAnnounceTime);
    EXPECT_EQ(view.lastAnnounceTime + 3600, view.nextAnnounceTime);
}

TEST_F(TrackerListChangeTest, addedTierAnnounces)
{
    tracker_.setReply("/a", 3600, 1800);
    auto* const tor = startTorrent({ "/a" });

    setTrackerList(tor, { "/a", "/b" });

    EXPECT_TRUE(waitFor([this]() { return tracker_.announceCount("/b") == 1U; }, 5s));
}

TEST_F(TrackerListChangeTest, trackerResponsesStillReachTheTorrent)
{
    tracker_.setReply("/a", 3600, 1800);
    auto* const tor = startTorrent({ "/a" });

    tracker_.setFailure("/a", "go away");
    setTrackerList(tor, { "/a" });
    tr_torrentManualUpdate(tor, {}, tr_reannounce_flags::IgnoreMinInterval);

    EXPECT_TRUE(waitFor([tor]() { return tr_torrentStat(tor).error == tr_stat::Error::TrackerError; }, 5s));
}
