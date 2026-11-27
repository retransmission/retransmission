// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::ranges::adjacent_find()
#include <functional> // std::invoke(), std::ranges::not_equal_to
#include <iterator> // std::indirect_result_t
#include <optional>
#include <ranges>
#include <type_traits> // std::remove_cvref_t

namespace tr::app
{

// The value that `getter` yields for every item in `items`, or nullopt when
// they disagree or there are none. `items` holds pointers, as a selection of
// torrents does, and `getter` takes what they point to.
template<typename Items, typename Getter>
    requires std::ranges::forward_range<Items const>
[[nodiscard]] std::optional<std::remove_cvref_t<std::indirect_result_t<Getter const&, std::ranges::range_value_t<Items const>>>>
uniform_value(Items const& items, Getter const& getter)
{
    auto const get = [&getter](auto const& item) -> decltype(auto) {
        return std::invoke(getter, *item);
    };

    if (std::ranges::empty(items) ||
        std::ranges::adjacent_find(items, std::ranges::not_equal_to{}, get) != std::ranges::end(items)) {
        return {};
    }
    return get(*std::ranges::begin(items));
}

} // namespace tr::app
