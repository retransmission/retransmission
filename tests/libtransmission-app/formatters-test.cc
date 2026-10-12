// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::ranges::copy()
#include <cstdint> // uint64_t
#include <ctime> // time_t
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/types.h>
#include <libtransmission/utils.h>

#include "libtransmission-app/formatters.h"

#include "test-fixtures.h"

using namespace std::literals;

using FormattersTest = TransmissionTest;

namespace
{
auto constexpr Minute = time_t{ 60 };
auto constexpr Hour = 60 * Minute;
auto constexpr Day = 24 * Hour;
} // namespace

TEST_F(FormattersTest, formatTime)
{
    EXPECT_EQ("now", tr::app::format_time(0));
    EXPECT_EQ("1 second", tr::app::format_time(1));
    EXPECT_EQ("59 seconds", tr::app::format_time(59));
    EXPECT_EQ("1 minute", tr::app::format_time(Minute + 30));
    EXPECT_EQ("2 hours", tr::app::format_time((2 * Hour) + (59 * Minute)));
    EXPECT_EQ("3 days", tr::app::format_time((3 * Day) + Hour));
}

TEST_F(FormattersTest, formatTimeLeft)
{
    EXPECT_EQ("now", tr::app::format_time_left(0));
    EXPECT_EQ("1 second left", tr::app::format_time_left(1));
    EXPECT_EQ("5 minutes left", tr::app::format_time_left(5 * Minute));
    EXPECT_EQ("1 hour left", tr::app::format_time_left(Hour + 1));
    EXPECT_EQ("2 days left", tr::app::format_time_left(2 * Day));
}

TEST_F(FormattersTest, formatTimeRelative)
{
    auto constexpr Now = time_t{ 1'700'000'000 };
    EXPECT_EQ("now", tr::app::format_time_relative(Now, Now));
    EXPECT_EQ("1 second ago", tr::app::format_time_relative(Now - 1, Now));
    EXPECT_EQ("5 minutes ago", tr::app::format_time_relative(Now - (5 * Minute), Now));
    EXPECT_EQ("1 day ago", tr::app::format_time_relative(Now - Day, Now));
    EXPECT_EQ("1 second from now", tr::app::format_time_relative(Now + 1, Now));
    EXPECT_EQ("2 hours from now", tr::app::format_time_relative(Now + (2 * Hour), Now));
}

TEST_F(FormattersTest, phrasesAreTranslatedWhole)
{
    // Russian inflects the unit differently after "in" and "ago".
    tr_set_translator(
        [](char const* msgid) noexcept { return msgid; },
        [](char const* msgid, char const* msgid_plural, uint64_t n) noexcept -> char const* {
            if (msgid == "{minutes_ago:L} minute ago"sv) {
                return n % 10U == 1U && n % 100U != 11U ? "{minutes_ago:L} минуту назад" : "{minutes_ago:L} минут назад";
            }
            if (msgid == "{minutes_from_now:L} minute from now"sv) {
                return "через {minutes_from_now:L} минуту";
            }
            return n == 1U ? msgid : msgid_plural;
        });

    auto constexpr Now = time_t{ 1'700'000'000 };
    EXPECT_EQ("21 минуту назад", tr::app::format_time_relative(Now - (21 * Minute), Now));
    EXPECT_EQ("через 1 минуту", tr::app::format_time_relative(Now + Minute, Now));

    tr_set_translator(nullptr, nullptr);
}

TEST_F(FormattersTest, trackerStatusLines)
{
    static auto constexpr Markup = tr::app::TrackerStatusMarkup{
        .success_begin = "<ok>",
        .success_end = "</ok>",
        .timeout_begin = "<slow>",
        .timeout_end = "</slow>",
        .error_begin = "<err>",
        .error_end = "</err>",
    };
    auto constexpr Now = time_t{ 1'700'000'000 };

    auto tracker = tr_tracker_view{};
    tracker.hasAnnounced = true;
    tracker.lastAnnounceSucceeded = true;
    tracker.lastAnnounceTime = Now - (5 * Minute);
    tracker.lastAnnouncePeerCount = 1;
    tracker.announceState = TR_TRACKER_WAITING;
    tracker.nextAnnounceTime = Now + (30 * Minute);
    tracker.hasScraped = true;
    tracker.lastScrapeSucceeded = true;
    tracker.lastScrapeTime = Now - Hour;
    tracker.seederCount = 3;
    tracker.leecherCount = 1;
    tracker.scrapeState = TR_TRACKER_QUEUED;

    using Lines = std::vector<std::string>;
    EXPECT_EQ(
        (Lines{ "Got a list of <ok>1 peer</ok> 5 minutes ago", "Asking for more peers 30 minutes from now" }),
        tr::app::tracker_status_lines(tracker, Now, false, Markup));
    EXPECT_EQ(
        (Lines{ "Got a list of <ok>1 peer</ok> 5 minutes ago",
                "Asking for more peers 30 minutes from now",
                "Tracker had <ok>3 seeders and 1 leecher</ok> 1 hour ago",
                "Queued to ask for peer counts" }),
        tr::app::tracker_status_lines(tracker, Now, true, Markup));

    // A tracker's errors are escaped, since they come from the tracker.
    tracker.lastAnnounceSucceeded = false;
    std::ranges::copy(
        R"(<b>Tom & Jerry's "torrent"</b>)"
        "\n"sv,
        std::begin(tracker.lastAnnounceResult));
    tracker.announceState = TR_TRACKER_ACTIVE;
    tracker.lastAnnounceStartTime = Now - 1;
    tracker.seederCount = -1;
    tracker.scrapeState = TR_TRACKER_INACTIVE;
    EXPECT_EQ(
        (Lines{ "Got an error '<err>&lt;b&gt;Tom &amp; Jerry&#39;s &quot;torrent&quot;&lt;/b&gt; </err>' 5 minutes ago",
                "Asked for more peers <small>1 second ago</small>",
                "Tracker had <ok>no information</ok> on peer counts 1 hour ago" }),
        tr::app::tracker_status_lines(tracker, Now, true, Markup));

    tracker.lastAnnounceTimedOut = true;
    tracker.announceState = TR_TRACKER_INACTIVE;
    tracker.hasAnnounced = false;
    EXPECT_EQ((Lines{ "No updates scheduled" }), tr::app::tracker_status_lines(tracker, Now, false, Markup));

    tracker.hasAnnounced = true;
    tracker.announceState = TR_TRACKER_QUEUED;
    EXPECT_EQ(
        (Lines{ "Peer list request <slow>timed out 5 minutes ago</slow>; will retry", "Queued to ask for more peers" }),
        tr::app::tracker_status_lines(tracker, Now, false, Markup));
}
