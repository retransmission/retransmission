// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef> // size_t
#include <cstdint>
#include <iterator> // std::begin(), std::end(), std::distance()
#include <span>

#include "libtransmission/quark.h"
#include "libtransmission/serializer.h" // tr::serializer::has_key()

#include "libtransmission-app/torrent.h"

namespace tr::app
{

// The `torrent_get` key sets a client asks for, named for the view that reads them.
// An `Info` set holds fields only an edit changes, so it is fetched on demand;
// a `Stats` set holds fields that change on their own, so it is what a poll refreshes.
// Every set carries `TR_KEY_id`, which `TorrentList` needs to place a row.
enum class TorrentKeySet : uint8_t {
    MainInfo, // the torrent list's fields that only an edit changes
    MainStats, // the torrent list's changing fields: what a poll and a verb re-fetch
    MainAll, // MainInfo and MainStats: what a new or edited torrent needs
    DetailInfo, // the details dialog's fields that only an edit changes
    DetailStats, // the details dialog's changing fields: what a `torrent_set` re-fetches
    DetailAll, // DetailInfo and DetailStats: what a details dialog needs for a torrent it starts to show
    Rename, // what a `torrent_rename_path` changes
};

// the keys of `set`, ascending and unique
[[nodiscard]] constexpr std::span<tr_quark const> torrent_keys(TorrentKeySet set) noexcept;

namespace detail
{

// `std::to_array<tr_quark>`: the `TR_KEY_*` enumerators are an anonymous enum,
// so a bare `std::array{ ... }` would deduce that enum as its element type.
// clang-format off
inline constexpr auto MainInfoKeys = std::to_array<tr_quark>({
    TR_KEY_added_date,
    TR_KEY_download_dir,
    TR_KEY_file_count,
    TR_KEY_hash_string,
    TR_KEY_id,
    TR_KEY_is_folder,
    TR_KEY_labels,
    TR_KEY_name,
    TR_KEY_primary_mime_type,
    TR_KEY_total_size,
    TR_KEY_tracker_list,
    TR_KEY_trackers,
});

inline constexpr auto MainStatsKeys = std::to_array<tr_quark>({
    TR_KEY_checked_bytes,
    TR_KEY_downloaded_ever,
    TR_KEY_edit_date,
    TR_KEY_error,
    TR_KEY_error_string,
    TR_KEY_eta,
    TR_KEY_have_unchecked,
    TR_KEY_have_valid,
    TR_KEY_id,
    TR_KEY_is_finished,
    TR_KEY_is_stalled,
    TR_KEY_left_until_done,
    TR_KEY_manual_announce_time,
    TR_KEY_metadata_have,
    TR_KEY_metadata_percent_complete,
    TR_KEY_metadata_size,
    TR_KEY_peers_connected,
    TR_KEY_peers_getting_from_us,
    TR_KEY_peers_sending_to_us,
    TR_KEY_queue_position,
    TR_KEY_rate_download,
    TR_KEY_rate_upload,
    TR_KEY_recheck_progress,
    TR_KEY_seed_ratio_limit,
    TR_KEY_seed_ratio_mode,
    TR_KEY_size_when_done,
    TR_KEY_status,
    TR_KEY_upload_ratio,
    TR_KEY_uploaded_ever,
    TR_KEY_webseeds_sending_to_us,
});

inline constexpr auto DetailInfoKeys = std::to_array<tr_quark>({
    TR_KEY_comment,
    TR_KEY_creator,
    TR_KEY_date_created,
    TR_KEY_files,
    TR_KEY_id,
    TR_KEY_is_private,
    TR_KEY_labels,
    TR_KEY_piece_count,
    TR_KEY_piece_size,
    TR_KEY_tracker_list,
    TR_KEY_trackers,
    TR_KEY_webseeds_ex,
});

// `edit_date` rides along so a `torrent_set` that edits the metainfo, e.g. labels
// or trackers, reports `TorrentList::Changes::edited` from its own re-fetch.
// `labels` and `tracker_list` ride along so such a set shows its result from that
// re-fetch even when `edit_date` did not move: two edits within a second share a
// date. (`tracker_stats` already carries the trackers.) The seed ratio keys ride
// along so the dialog's ratio widgets settle on that re-fetch too, instead of
// showing the old limit until the next list poll.
inline constexpr auto DetailStatsKeys = std::to_array<tr_quark>({
    TR_KEY_activity_date,
    TR_KEY_bandwidth_priority,
    TR_KEY_corrupt_ever,
    TR_KEY_desired_available,
    TR_KEY_download_limit,
    TR_KEY_download_limited,
    TR_KEY_downloaded_ever,
    TR_KEY_edit_date,
    TR_KEY_file_stats,
    TR_KEY_have_unchecked,
    TR_KEY_honors_session_limits,
    TR_KEY_id,
    TR_KEY_labels,
    TR_KEY_peer_limit,
    TR_KEY_peers,
    TR_KEY_seed_idle_limit,
    TR_KEY_seed_idle_mode,
    TR_KEY_seed_ratio_limit,
    TR_KEY_seed_ratio_mode,
    TR_KEY_start_date,
    TR_KEY_tracker_list,
    TR_KEY_tracker_stats,
    TR_KEY_upload_limit,
    TR_KEY_upload_limited,
});

inline constexpr auto RenameKeys = std::to_array<tr_quark>({
    TR_KEY_file_stats,
    TR_KEY_files,
    TR_KEY_id,
    TR_KEY_name,
});
// clang-format on

// The sorted union of two sorted unique key arrays. The arrays are template
// arguments because the result's length must be a constant expression, and a
// function parameter is never one, even in a consteval function.
template<auto Lhs, auto Rhs>
[[nodiscard]] consteval auto merge_keys()
{
    constexpr auto Count = [] consteval {
        auto scratch = std::array<tr_quark, std::size(Lhs) + std::size(Rhs)>{};
        auto const end = std::ranges::set_union(Lhs, Rhs, std::begin(scratch)).out;
        return static_cast<size_t>(std::distance(std::begin(scratch), end));
    }();

    auto keys = std::array<tr_quark, Count>{};
    std::ranges::set_union(Lhs, Rhs, std::begin(keys));
    return keys;
}

inline constexpr auto MainAllKeys = merge_keys<MainInfoKeys, MainStatsKeys>();
inline constexpr auto DetailAllKeys = merge_keys<DetailInfoKeys, DetailStatsKeys>();

// Sorted, unique, carries `id`, and every other key feeds a `Torrent` member.
// `Torrent::Fields` has no `id` entry: the id places the row, it is not a field.
template<size_t N>
[[nodiscard]] consteval bool is_valid_key_set(std::array<tr_quark, N> const& keys)
{
    return std::ranges::is_sorted(keys) && std::ranges::adjacent_find(keys) == std::end(keys) &&
        std::ranges::binary_search(keys, TR_KEY_id) &&
        std::ranges::all_of(keys, [](tr_quark const key) { return key == TR_KEY_id || tr::serializer::has_key<Torrent>(key); });
}

static_assert(is_valid_key_set(MainInfoKeys));
static_assert(is_valid_key_set(MainStatsKeys));
static_assert(is_valid_key_set(MainAllKeys));
static_assert(is_valid_key_set(DetailInfoKeys));
static_assert(is_valid_key_set(DetailStatsKeys));
static_assert(is_valid_key_set(DetailAllKeys));
static_assert(is_valid_key_set(RenameKeys));

} // namespace detail

constexpr std::span<tr_quark const> torrent_keys(TorrentKeySet const set) noexcept
{
    switch (set) {
    case TorrentKeySet::MainInfo:
        return detail::MainInfoKeys;
    case TorrentKeySet::MainStats:
        return detail::MainStatsKeys;
    case TorrentKeySet::MainAll:
        return detail::MainAllKeys;
    case TorrentKeySet::DetailInfo:
        return detail::DetailInfoKeys;
    case TorrentKeySet::DetailStats:
        return detail::DetailStatsKeys;
    case TorrentKeySet::DetailAll:
        return detail::DetailAllKeys;
    case TorrentKeySet::Rename:
        return detail::RenameKeys;
    }

    return {};
}

} // namespace tr::app
