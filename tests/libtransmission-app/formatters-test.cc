// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint> // uint64_t
#include <ctime> // time_t
#include <string_view>

#include <gtest/gtest.h>

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
