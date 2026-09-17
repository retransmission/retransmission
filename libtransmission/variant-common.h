// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_VARIANT_MODULE
#error only the libtransmission variant module should #include this header.
#endif

#include <algorithm> // std::ranges::sort
#include <string_view>
#include <utility> // std::pair

#include <small/vector.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace tr::variant::detail
{

// A map's entries sorted by key string.
// Both serializers write dictionary entries in this order.
[[nodiscard]] inline auto sorted_entries(tr_variant::Map const& map)
{
    static auto constexpr N = 32U;
    auto entries = small::vector<std::pair<std::string_view, tr_variant const*>, N>{};
    entries.reserve(map.size());
    for (auto const& [key, child] : map) {
        entries.emplace_back(tr_quark_get_string_view(key), &child);
    }
    std::ranges::sort(entries);
    return entries;
}

} // namespace tr::variant::detail
