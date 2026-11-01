// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <utility>

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
template<typename... KeyVals>
[[nodiscard]] tr_variant::Map make_row(tr_torrent_id_t const id, KeyVals... keyvals)
{
    auto row = tr_variant::Map{ 1U + sizeof...(keyvals) };
    row.try_emplace(TR_KEY_id, id);
    (row.insert_or_assign(keyvals.first, std::move(keyvals.second)), ...);
    return row;
}

template<typename... Items>
[[nodiscard]] tr_variant::Vector make_list(Items&&... items)
{
    auto list = tr_variant::Vector{};
    list.reserve(sizeof...(Items));
    (list.emplace_back(std::forward<Items>(items)), ...);
    return list;
}
