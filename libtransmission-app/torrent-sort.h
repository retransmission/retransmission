// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <compare>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

// Orders two torrents by `mode`'s keys. Equivalent when those keys tie, and
// always for `SortByName`: the caller then compares names in its own collation,
// then info hashes, so the order is total.
[[nodiscard]] std::partial_ordering compare_torrents(Torrent const& a, Torrent const& b, SortMode mode);

// The fields `compare_torrents()` reads for `mode`, so a change to one of them may move a torrent.
// The caller's tie-breaks read the name and the info hash, which these leave out.
[[nodiscard]] constexpr TorrentFieldSet sort_mode_fields(SortMode const mode) noexcept
{
    auto constexpr Ratio = Torrent::fields<TR_KEY_upload_ratio>();
    auto constexpr Progress = Torrent::percent_complete_fields() |
        Torrent::effective_seed_ratio_fields() |
        Torrent::fields<TR_KEY_queue_position>() |
        Ratio;
    auto constexpr State = Torrent::fields<TR_KEY_error, TR_KEY_queue_position, TR_KEY_status>() | Progress;

    switch (mode) {
    case SortMode::SortByActivity:
        return Torrent::fields<
                   TR_KEY_peers_getting_from_us,
                   TR_KEY_rate_download,
                   TR_KEY_rate_upload,
                   TR_KEY_webseeds_sending_to_us>() |
            State;

    case SortMode::SortByAge:
        return Torrent::fields<TR_KEY_added_date>();

    case SortMode::SortByEta:
        return Torrent::fields<TR_KEY_eta>();

    case SortMode::SortByProgress:
        return Progress;

    case SortMode::SortByQueue:
        return Torrent::fields<TR_KEY_queue_position>();

    case SortMode::SortByRatio:
        return Ratio;

    case SortMode::SortBySize:
        return Torrent::fields<TR_KEY_size_when_done>();

    case SortMode::SortByState:
        return State;

    default:
        return {};
    }
}

} // namespace tr::app
