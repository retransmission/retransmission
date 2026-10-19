// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

#include "libtransmission/converters.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/converters.h"

namespace tr::app::detail
{
namespace
{
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

[[nodiscard]] bool to_show_mode(tr_variant const& src, ShowMode* tgt)
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

[[nodiscard]] tr_variant from_show_mode(ShowMode const& src)
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

[[nodiscard]] bool to_sort_mode(tr_variant const& src, SortMode* tgt)
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

[[nodiscard]] tr_variant from_sort_mode(SortMode const& src)
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

[[nodiscard]] bool to_stats_mode(tr_variant const& src, StatsMode* tgt)
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

[[nodiscard]] tr_variant from_stats_mode(StatsMode const& src)
{
    static constexpr auto& Keys = StatsKeys;

    for (auto const& [key, val] : Keys) {
        if (src == val) {
            return tr_variant::unmanaged_string(key);
        }
    }

    return from_stats_mode(DefaultStatsMode);
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

#undef TR_DEFINE_APP_CONVERTER

} // namespace tr::serializer
