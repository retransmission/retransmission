// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "libtransmission/converters.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/converters.h"

using namespace std::literals;

namespace tr::app::detail
{
namespace
{
template<typename T>
inline constexpr bool HasTmGmtoffV = requires(T t) { t.tm_gmtoff; };

template<typename T, size_t N>
using LookupTable = std::array<std::pair<std::string_view, T>, N>;

// NOLINTBEGIN(modernize-avoid-c-arrays)
template<typename T, size_t N>
consteval LookupTable<std::remove_cv_t<T>, N> to_lookup(std::pair<std::string_view, T> (&&a)[N])
{
    return std::to_array(std::move(a));
}
// NOLINTEND(modernize-avoid-c-arrays)

// ---

auto constexpr ShowKeys = to_lookup<ShowMode>({
    { "show_active", ShowMode::ShowActive },
    { "show_all", ShowMode::ShowAll },
    { "show_downloading", ShowMode::ShowDownloading },
    { "show_error", ShowMode::ShowError },
    { "show_finished", ShowMode::ShowFinished },
    { "show_paused", ShowMode::ShowPaused },
    { "show_seeding", ShowMode::ShowSeeding },
    { "show_verifying", ShowMode::ShowVerifying },
});
static_assert(ShowKeys.size() == ShowModeCount);

bool to_show_mode(tr_variant const& src, ShowMode* tgt)
{
    static constexpr auto& Keys = ShowKeys;

    if (auto const str = src.value_if<std::string_view>()) {
        for (auto const& [key, val] : Keys) {
            if (str == key) {
                *tgt = val;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_show_mode(ShowMode const& src)
{
    static constexpr auto& Keys = ShowKeys;

    for (auto const& [key, val] : Keys) {
        if (src == val) {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_show_mode(DefaultShowMode);
}

// ---

auto constexpr SortKeys = to_lookup<SortMode>({
    { "sort_by_activity", SortMode::SortByActivity },
    { "sort_by_age", SortMode::SortByAge },
    { "sort_by_eta", SortMode::SortByEta },
    { "sort_by_id", SortMode::SortById },
    { "sort_by_name", SortMode::SortByName },
    { "sort_by_progress", SortMode::SortByProgress },
    { "sort_by_queue", SortMode::SortByQueue },
    { "sort_by_ratio", SortMode::SortByRatio },
    { "sort_by_size", SortMode::SortBySize },
    { "sort_by_state", SortMode::SortByState },
});
static_assert(SortKeys.size() == SortModeCount);

bool to_sort_mode(tr_variant const& src, SortMode* tgt)
{
    static constexpr auto& Keys = SortKeys;

    if (auto const str = src.value_if<std::string_view>()) {
        for (auto const& [key, val] : Keys) {
            if (str == key) {
                *tgt = val;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_sort_mode(SortMode const& src)
{
    static constexpr auto& Keys = SortKeys;

    for (auto const& [key, val] : Keys) {
        if (src == val) {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_sort_mode(DefaultSortMode);
}

// ---

auto constexpr StatsKeys = to_lookup<StatsMode>({
    { "session_ratio", StatsMode::SessionRatio },
    { "session_transfer", StatsMode::SessionTransfer },
    { "total_ratio", StatsMode::TotalRatio },
    { "total_transfer", StatsMode::TotalTransfer },
});
static_assert(StatsKeys.size() == StatsModeCount);

bool to_stats_mode(tr_variant const& src, StatsMode* tgt)
{
    static constexpr auto& Keys = StatsKeys;

    if (auto const str = src.value_if<std::string_view>()) {
        for (auto const& [key, val] : Keys) {
            if (str == key) {
                *tgt = val;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_stats_mode(StatsMode const& src)
{
    static constexpr auto& Keys = StatsKeys;

    for (auto const& [key, val] : Keys) {
        if (src == val) {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_stats_mode(DefaultStatsMode);
}

// ---

// c++20(P0355): use std::chrono::parse, GCC 14.1, clang https://github.com/llvm/llvm-project/issues/166051
[[nodiscard]] std::optional<std::chrono::sys_seconds> parse_sys_seconds(std::string_view str)
{
    auto const sv = tr_strv_strip(str);
    if ((std::size(sv) != 20U && std::size(sv) != 24U && std::size(sv) != 25U) || sv[4] != '-' || sv[7] != '-' ||
        sv[10] != 'T' || sv[13] != ':' || sv[16] != ':') {
        return {};
    }

    auto parse_int = [](std::string_view token, int min, int max, int* out) -> bool {
        if (auto const parsed = tr_num_parse<int>(token); parsed && *parsed >= min && *parsed <= max) {
            *out = *parsed;
            return true;
        }

        return false;
    };

    auto year = int{};
    auto month = int{};
    auto day = int{};
    auto hour = int{};
    auto minute = int{};
    auto second = int{};

    if (!parse_int(sv.substr(0, 4), 0, 9999, &year) || !parse_int(sv.substr(5, 2), 1, 12, &month) ||
        !parse_int(sv.substr(8, 2), 1, 31, &day) || !parse_int(sv.substr(11, 2), 0, 23, &hour) ||
        !parse_int(sv.substr(14, 2), 0, 59, &minute) || !parse_int(sv.substr(17, 2), 0, 59, &second)) {
        return {};
    }

    auto const ymd = std::chrono::year_month_day{
        std::chrono::year{ year },
        std::chrono::month{ static_cast<unsigned>(month) },
        std::chrono::day{ static_cast<unsigned>(day) },
    };
    if (!ymd.ok()) {
        return {};
    }

    auto const local_tp = std::chrono::sys_days{ ymd } + std::chrono::hours{ hour } + std::chrono::minutes{ minute } +
        std::chrono::seconds{ second };

    if (std::size(sv) == 20U) {
        if (sv[19] != 'Z') {
            return {};
        }

        return std::chrono::sys_seconds{ local_tp };
    }

    auto const sign = sv[19];
    if (sign != '+' && sign != '-') {
        return {};
    }

    auto off_hours = int{};
    auto off_minutes = int{};

    if (std::size(sv) == 24U) {
        if (!parse_int(sv.substr(20, 2), 0, 23, &off_hours) || !parse_int(sv.substr(22, 2), 0, 59, &off_minutes)) {
            return {};
        }
    } else {
        if (sv[22] != ':' || !parse_int(sv.substr(20, 2), 0, 23, &off_hours) ||
            !parse_int(sv.substr(23, 2), 0, 59, &off_minutes)) {
            return {};
        }
    }

    auto const offset = std::chrono::minutes{ (off_hours * 60) + off_minutes } * (sign == '-' ? -1 : 1);
    return std::chrono::sys_seconds{ local_tp - offset };
}

[[nodiscard]] std::string format_sys_seconds(std::chrono::sys_seconds const& src)
{
    auto const tp = std::chrono::time_point_cast<std::chrono::seconds>(src);
    auto const tt = std::chrono::system_clock::to_time_t(tp);

    // TODO(c++20): switch to std::chrono::zoned_time, GCC 13.1, clang 19 (or clang 21 with std::format), fmt 11.2
    // prefer localtime with TZ offset data when we can get it.
    if constexpr (HasTmGmtoffV<std::tm>) {
        if (auto const* local = std::localtime(&tt)) {
            // fmt::runtime to workaround FTBFS in clang
            return fmt::format(fmt::runtime("{:%FT%T%z}"), *local);
        }
    }

    return fmt::format("{:%FT%TZ}", src);
}

bool to_sys_seconds(tr_variant const& src, std::chrono::sys_seconds* tgt)
{
    if (auto const val = src.value_if<std::string_view>()) {
        if (auto const parsed = parse_sys_seconds(*val); parsed) {
            *tgt = *parsed;
            return true;
        }
    }

    if (auto const val = src.value_if<int64_t>()) {
        auto const tp = std::chrono::system_clock::from_time_t(static_cast<time_t>(*val));
        *tgt = std::chrono::time_point_cast<std::chrono::seconds>(tp);
        return true;
    }

    return false;
}

tr_variant from_sys_seconds(std::chrono::sys_seconds const& src)
{
    auto const formatted = format_sys_seconds(src);
    return tr_variant{ formatted };
}
} // unnamed namespace

} // namespace tr::app::detail

// ---
// `Converter<T>` out-of-line definitions for the types owned by
// `libtransmission-app`. Each one forwards to the matching helper in
// `tr::app::detail` above.

namespace tr::serializer
{
namespace ad = tr::app::detail;

// NOLINTBEGIN(bugprone-macro-parentheses)
#define TR_DEFINE_APP_CONVERTER(T, to_fn, from_fn) \
    tr_variant Converter<T>::to_variant(T const& src) \
    { \
        return ad::from_fn(src); \
    } \
    bool Converter<T>::to_value(tr_variant const& src, T* tgt) \
    { \
        return ad::to_fn(src, tgt); \
    }
// NOLINTEND(bugprone-macro-parentheses)

TR_DEFINE_APP_CONVERTER(tr::app::ShowMode, to_show_mode, from_show_mode)
TR_DEFINE_APP_CONVERTER(tr::app::SortMode, to_sort_mode, from_sort_mode)
TR_DEFINE_APP_CONVERTER(tr::app::StatsMode, to_stats_mode, from_stats_mode)
TR_DEFINE_APP_CONVERTER(std::chrono::sys_seconds, to_sys_seconds, from_sys_seconds)

#undef TR_DEFINE_APP_CONVERTER

} // namespace tr::serializer
