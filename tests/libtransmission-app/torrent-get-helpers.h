// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <libtransmission/quark.h>
#include <libtransmission/types.h> // tr_torrent_id_t
#include <libtransmission/variant.h>

// Builders for the `torrent_get` rows that `tr::app::Torrent` and `TorrentList` read.

using KeyVal = std::pair<tr_quark, tr_variant>;

template<typename Val>
[[nodiscard]] KeyVal kv(tr_quark const key, Val&& val)
{
    return { key, tr_variant{ std::forward<Val>(val) } };
}

// an object-format row: its `id`, then `keyvals`
template<std::same_as<KeyVal>... KeyVals>
[[nodiscard]] tr_variant::Map make_row(tr_torrent_id_t const id, KeyVals... keyvals)
{
    auto row = tr_variant::Map{ 1U + sizeof...(keyvals) };
    row.try_emplace(TR_KEY_id, id);
    (row.insert_or_assign(keyvals.first, std::move(keyvals.second)), ...);
    return row;
}

// a `trackers` entry; `Torrent` works out an omitted `sitename` from `announce`
[[nodiscard]] inline tr_variant::Map make_tracker(
    std::string_view const announce,
    std::string_view const sitename = {},
    int64_t const tier = 0)
{
    auto tracker = tr_variant::Map{ 3U };
    tracker.try_emplace(TR_KEY_announce, announce);
    if (!std::empty(sitename)) {
        tracker.try_emplace(TR_KEY_sitename, sitename);
    }
    tracker.try_emplace(TR_KEY_tier, tier);
    return tracker;
}

template<typename... Items>
[[nodiscard]] tr_variant::Vector make_list(Items&&... items)
{
    auto list = tr_variant::Vector{};
    list.reserve(sizeof...(Items));
    (list.emplace_back(std::forward<Items>(items)), ...);
    return list;
}

// the `arguments` of a `torrent_get` response; `removed` is omitted unless given
[[nodiscard]] inline tr_variant::Map make_args(
    tr_variant::Vector torrents,
    std::optional<std::vector<tr_torrent_id_t>> const& removed = {})
{
    auto args = tr_variant::Map{ 2U };
    args.try_emplace(TR_KEY_torrents, std::move(torrents));

    if (removed) {
        auto ids = tr_variant::Vector{};
        ids.reserve(std::size(*removed));
        for (auto const id : *removed) {
            ids.emplace_back(id);
        }
        args.try_emplace(TR_KEY_removed, std::move(ids));
    }

    return args;
}
