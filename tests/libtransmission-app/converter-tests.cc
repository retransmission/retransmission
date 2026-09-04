// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <ctime>
#include <regex>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/converters.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/converters.h"
#include "libtransmission-app/display-modes.h"

#include "test-fixtures.h"

using ConverterTest = TransmissionTest;
using namespace std::literals;
using tr::serializer::to_value;
using tr::serializer::to_variant;

namespace
{
constexpr std::chrono::sys_seconds make_sys_seconds(int year, unsigned month, unsigned day, int hour, int minute, int second)
{
    return std::chrono::sys_days{
        std::chrono::year_month_day{ std::chrono::year{ year }, std::chrono::month{ month }, std::chrono::day{ day } }
    } + std::chrono::hours{ hour } +
        std::chrono::minutes{ minute } + std::chrono::seconds{ second };
}

void expect_sys_seconds_eq(std::optional<std::chrono::sys_seconds> const& actual, std::chrono::sys_seconds expected)
{
    EXPECT_TRUE(actual.has_value());
    if (actual.has_value()) {
        EXPECT_EQ(actual->time_since_epoch().count(), expected.time_since_epoch().count());
    }
}

template<typename T, size_t N>
void testModeRoundtrip(std::array<std::pair<std::string_view, T>, N> const& items)
{
    for (auto const& [key, mode] : items) {
        auto const var = to_variant(mode);
        EXPECT_TRUE(var.template holds_alternative<std::string_view>());
        EXPECT_EQ(var.template value_if<std::string_view>().value_or(""sv), key);

        auto out = T{};
        EXPECT_TRUE(to_value(tr_variant{ key }, &out));
        EXPECT_EQ(out, mode);
    }
}

} // namespace

TEST_F(ConverterTest, showModeStringsRoundtrip)
{
    auto constexpr Items = std::to_array<std::pair<std::string_view, tr::app::ShowMode>>({
        { "show_active", tr::app::ShowMode::ShowActive },
        { "show_all", tr::app::ShowMode::ShowAll },
        { "show_downloading", tr::app::ShowMode::ShowDownloading },
        { "show_error", tr::app::ShowMode::ShowError },
        { "show_finished", tr::app::ShowMode::ShowFinished },
        { "show_paused", tr::app::ShowMode::ShowPaused },
        { "show_seeding", tr::app::ShowMode::ShowSeeding },
        { "show_verifying", tr::app::ShowMode::ShowVerifying },
    });
    static_assert(Items.size() == tr::app::ShowModeCount);

    testModeRoundtrip(Items);
}

TEST_F(ConverterTest, sortModeStringsRoundtrip)
{
    auto constexpr Items = std::to_array<std::pair<std::string_view, tr::app::SortMode>>({
        { "sort_by_activity", tr::app::SortMode::SortByActivity },
        { "sort_by_age", tr::app::SortMode::SortByAge },
        { "sort_by_eta", tr::app::SortMode::SortByEta },
        { "sort_by_id", tr::app::SortMode::SortById },
        { "sort_by_name", tr::app::SortMode::SortByName },
        { "sort_by_progress", tr::app::SortMode::SortByProgress },
        { "sort_by_queue", tr::app::SortMode::SortByQueue },
        { "sort_by_ratio", tr::app::SortMode::SortByRatio },
        { "sort_by_size", tr::app::SortMode::SortBySize },
        { "sort_by_state", tr::app::SortMode::SortByState },
    });
    static_assert(Items.size() == tr::app::SortModeCount);

    testModeRoundtrip(Items);
}

TEST_F(ConverterTest, statsModeStringsRoundtrip)
{
    auto constexpr Items = std::to_array<std::pair<std::string_view, tr::app::StatsMode>>({
        { "total_ratio", tr::app::StatsMode::TotalRatio },
        { "total_transfer", tr::app::StatsMode::TotalTransfer },
        { "session_ratio", tr::app::StatsMode::SessionRatio },
        { "session_transfer", tr::app::StatsMode::SessionTransfer },
    });
    static_assert(Items.size() == tr::app::StatsModeCount);

    testModeRoundtrip(Items);
}

TEST_F(ConverterTest, sysSecondsRoundtrip)
{
    using namespace std::chrono;
    using namespace tr::serializer;

    auto constexpr Expected = make_sys_seconds(2024, 2, 3, 4, 5, 6);
    auto const var = to_variant(Expected);
    EXPECT_TRUE(var.holds_alternative<std::string_view>());
    auto const serialized = var.value_if<std::string_view>().value_or(""sv);
    static auto const re = std::regex(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:Z|[+-]\d{4})$)");
    EXPECT_TRUE(std::regex_match(std::string{ serialized }, re));

    auto actual = to_value<std::chrono::sys_seconds>(tr_variant{ serialized });
    expect_sys_seconds_eq(actual, Expected);

    actual = to_value<std::chrono::sys_seconds>(tr_variant{ "2024-02-03T04:05:06Z"sv });
    expect_sys_seconds_eq(actual, Expected);

    actual = to_value<std::chrono::sys_seconds>(tr_variant{ "2024-02-03T04:05:06+00:00"sv });
    expect_sys_seconds_eq(actual, Expected);

    actual = to_value<std::chrono::sys_seconds>(tr_variant{ "2024-02-03T04:05:06+02:30"sv });
    expect_sys_seconds_eq(actual, Expected - (hours{ 2 } + minutes{ 30 }));

    EXPECT_FALSE(to_value<std::chrono::sys_seconds>(tr_variant{ "2023-02-29T00:00:00Z"sv }).has_value());
    EXPECT_FALSE(to_value<std::chrono::sys_seconds>(tr_variant{ "2024-04-31T00:00:00Z"sv }).has_value());
    EXPECT_TRUE(to_value<std::chrono::sys_seconds>(tr_variant{ "2024-02-29T00:00:00Z"sv }).has_value());

    auto constexpr Epoch = int64_t{ 1700000000 };
    auto const epoch_seconds = time_point_cast<seconds>(system_clock::from_time_t(static_cast<time_t>(Epoch)));
    actual = to_value<std::chrono::sys_seconds>(tr_variant{ Epoch });
    expect_sys_seconds_eq(actual, epoch_seconds);
}
