// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string>
#include <type_traits> // std::is_same_v
#include <vector>

#include <gtest/gtest.h>

#include "libtransmission-app/uniform-value.h"

using namespace std::literals;

namespace
{

struct Item {
    [[nodiscard]] std::string const& name() const noexcept
    {
        return name_;
    }

    std::string name_;
    int size = 0;
};

} // namespace

TEST(UniformValueTest, noItemsHaveNoValue)
{
    auto const items = std::vector<Item const*>{};

    EXPECT_EQ(tr::app::uniform_value(items, &Item::size), std::nullopt);
}

TEST(UniformValueTest, oneItemHasItsValue)
{
    auto const a = Item{ "a", 1 };
    auto const items = std::vector{ &a };

    EXPECT_EQ(tr::app::uniform_value(items, &Item::size), 1);
}

TEST(UniformValueTest, agreeingItemsHaveTheirValue)
{
    auto const a = Item{ "a", 1 };
    auto const b = Item{ "b", 1 };
    auto const c = Item{ "c", 1 };
    auto const items = std::vector{ &a, &b, &c };

    EXPECT_EQ(tr::app::uniform_value(items, &Item::size), 1);
}

TEST(UniformValueTest, disagreeingItemsHaveNoValue)
{
    auto const a = Item{ "a", 1 };
    auto const b = Item{ "b", 1 };
    auto const c = Item{ "c", 2 };
    auto const items = std::vector{ &a, &b, &c };

    EXPECT_EQ(tr::app::uniform_value(items, &Item::size), std::nullopt);
}

TEST(UniformValueTest, copiesWhatAGetterReturnsByReference)
{
    auto a = Item{ "same", 1 };
    auto const b = Item{ "same", 2 };
    auto const items = std::vector<Item const*>{ &a, &b };

    auto const name = tr::app::uniform_value(items, &Item::name);
    a.name_ = "changed";

    static_assert(std::is_same_v<decltype(name), std::optional<std::string> const>);
    EXPECT_EQ(name, "same"s);
}

TEST(UniformValueTest, acceptsAnyCallable)
{
    auto const a = Item{ "a", 3 };
    auto const b = Item{ "b", 5 };
    auto const items = std::vector{ &a, &b };

    EXPECT_EQ(tr::app::uniform_value(items, [](Item const& item) { return item.size % 2; }), 1);
}
