// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/torrent-sort.h"

#include <compare>
#include <limits>

#include "libtransmission/values.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{
namespace
{

// None sorts below every number, Infinite above
[[nodiscard]] constexpr double ratio_sort_key(tr::Values::Ratio const& ratio) noexcept
{
    if (ratio.is_infinite()) {
        return std::numeric_limits<double>::infinity();
    }

    return ratio.to_double().value_or(-1.0);
}

[[nodiscard]] std::partial_ordering compare_ratio(Torrent const& a, Torrent const& b) noexcept
{
    return ratio_sort_key(a.upload_ratio()) <=> ratio_sort_key(b.upload_ratio());
}

// progress toward the seed ratio; a torrent without a ratio to reach sorts after one still working on it
[[nodiscard]] std::partial_ordering compare_seed_progress(Torrent const& a, Torrent const& b)
{
    auto const a_limit = a.effective_seed_ratio();
    auto const b_limit = b.effective_seed_ratio();

    if (!a_limit && !b_limit) {
        return compare_ratio(a, b);
    }

    auto const a_ratio = ratio_sort_key(a.upload_ratio());
    auto const b_ratio = ratio_sort_key(b.upload_ratio());

    if (!a_limit) {
        return b_ratio < *b_limit ? std::partial_ordering::greater : std::partial_ordering::less;
    }

    if (!b_limit) {
        return a_ratio < *a_limit ? std::partial_ordering::less : std::partial_ordering::greater;
    }

    if (!(*a_limit > 0) && !(*b_limit > 0)) {
        return compare_ratio(a, b);
    }

    if (!(*a_limit > 0)) {
        return std::partial_ordering::greater;
    }

    if (!(*b_limit > 0)) {
        return std::partial_ordering::less;
    }

    return a_ratio / *a_limit <=> b_ratio / *b_limit;
}

// a torrent without an ETA sorts before one with one
[[nodiscard]] std::strong_ordering compare_eta(Torrent const& a, Torrent const& b) noexcept
{
    auto const have_a = a.has_eta();
    auto const have_b = b.has_eta();

    if (have_a && have_b) {
        return a.eta() <=> b.eta();
    }

    if (have_a) {
        return std::strong_ordering::greater;
    }

    if (have_b) {
        return std::strong_ordering::less;
    }

    return std::strong_ordering::equal;
}

} // namespace

std::partial_ordering compare_torrents(Torrent const& a, Torrent const& b, SortMode const mode)
{
    auto val = std::partial_ordering::equivalent;

    switch (mode) {
    case SortMode::SortByQueue:
        val = b.queue_position() <=> a.queue_position();
        break;

    case SortMode::SortBySize:
        val = a.size_when_done() <=> b.size_when_done();
        break;

    case SortMode::SortByAge:
        val = a.added_date() <=> b.added_date();
        break;

    case SortMode::SortById:
        val = a.id() <=> b.id();
        break;

    case SortMode::SortByActivity:
        val = a.download_speed() + a.upload_speed() <=> b.download_speed() + b.upload_speed();

        if (val == 0) {
            val = a.peers_getting_from_us() + a.webseeds_sending_to_us() <=>
                b.peers_getting_from_us() + b.webseeds_sending_to_us();
        }

        [[fallthrough]];

    case SortMode::SortByState:
        if (val == 0) {
            val = static_cast<int>(b.is_paused()) <=> static_cast<int>(a.is_paused());
        }

        if (val == 0) {
            val = a.activity() <=> b.activity();
        }

        if (val == 0) {
            val = b.queue_position() <=> a.queue_position();
        }

        if (val == 0) {
            val = static_cast<int>(a.has_error()) <=> static_cast<int>(b.has_error());
        }

        [[fallthrough]];

    case SortMode::SortByProgress:
        if (val == 0) {
            val = a.metadata_percent_complete() <=> b.metadata_percent_complete();
        }

        if (val == 0) {
            val = a.percent_complete() <=> b.percent_complete();
        }

        if (val == 0) {
            val = compare_seed_progress(a, b);
        }

        if (val == 0) {
            val = b.queue_position() <=> a.queue_position();
        }

        [[fallthrough]];

    case SortMode::SortByRatio:
        if (val == 0) {
            val = compare_ratio(a, b);
        }

        break;

    case SortMode::SortByEta:
        val = compare_eta(a, b);
        break;

    default:
        break;
    }

    return val;
}

} // namespace tr::app
