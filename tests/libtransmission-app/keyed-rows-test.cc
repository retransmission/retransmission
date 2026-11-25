// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <iterator> // std::size()
#include <string>
#include <utility> // std::pair
#include <vector>

#include <gtest/gtest.h>

#include "libtransmission-app/keyed-rows.h"

using namespace std::literals;

namespace
{

// A fake view: each row is an index into `labels`
class FakeView
{
public:
    // one refresh: `items` is each item's key and label
    void refresh(std::vector<std::pair<int, std::string>> const& items)
    {
        for (auto const& [key, label] : items) {
            rows_.add_or_update(
                key,
                [&] {
                    labels.push_back(label);
                    return std::size(labels) - 1U;
                },
                [&](size_t const row) { labels[row] = label; });
        }

        rows_.remove_unseen([this](size_t const row) { labels[row] = Removed; });
    }

    static inline auto const Removed = "removed"s;

    std::vector<std::string> labels;

private:
    tr::app::KeyedRows<int, size_t> rows_;
};

} // namespace

TEST(KeyedRowsTest, addsUpdatesAndRemoves)
{
    auto view = FakeView{};

    view.refresh({ { 1, "a" }, { 2, "b" } });
    EXPECT_EQ(view.labels, (std::vector{ "a"s, "b"s }));

    view.refresh({ { 2, "B" }, { 3, "c" } });
    EXPECT_EQ(view.labels, (std::vector{ FakeView::Removed, "B"s, "c"s }));

    view.refresh({});
    EXPECT_EQ(view.labels, (std::vector{ FakeView::Removed, FakeView::Removed, FakeView::Removed }));
}

TEST(KeyedRowsTest, aRepeatedKeyUpdatesOneRow)
{
    auto view = FakeView{};

    view.refresh({ { 1, "a" }, { 1, "A" } });
    EXPECT_EQ(view.labels, (std::vector{ "A"s }));

    // the repeat stays a single row in the next refresh too
    view.refresh({ { 1, "x" }, { 1, "y" } });
    EXPECT_EQ(view.labels, (std::vector{ "y"s }));
}

TEST(KeyedRowsTest, aKeyThatReturnsGetsANewRow)
{
    auto view = FakeView{};

    view.refresh({ { 1, "a" } });
    view.refresh({});
    view.refresh({ { 1, "b" } });
    EXPECT_EQ(view.labels, (std::vector{ FakeView::Removed, "b"s }));
}
