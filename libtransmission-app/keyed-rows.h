// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <concepts> // std::invocable
#include <functional> // std::invoke()
#include <iterator> // std::end()
#include <map>
#include <utility> // std::exchange(), std::forward(), std::move()

namespace tr::app
{

// A view's rows, matched to its items by key across refreshes,
// so that a row keeps its selection and scroll position.
// A refresh calls `add_or_update()` once per item, then `remove_unseen()`.
template<typename Key, typename Row>
class KeyedRows
{
public:
    // Calls `update(row)` on the row for `key`, or keeps `add()` as the row for a new key.
    // An item whose key repeats within one refresh updates the same row.
    template<std::invocable<> Add, std::invocable<Row&> Update>
    void add_or_update(Key const& key, Add&& add, Update&& update)
    {
        if (auto node = rows_.extract(key); node) {
            std::invoke(std::forward<Update>(update), node.mapped());
            seen_.insert(std::move(node));
        } else if (auto const iter = seen_.find(key); iter != std::end(seen_)) {
            std::invoke(std::forward<Update>(update), iter->second);
        } else {
            seen_.try_emplace(key, std::invoke(std::forward<Add>(add)));
        }
    }

    // Ends a refresh: passes each row that it did not name to `remove(row)`, then forgets them
    template<std::invocable<Row&> Remove>
    void remove_unseen(Remove&& remove)
    {
        for (auto& [key, row] : rows_) {
            std::invoke(remove, row);
        }

        rows_ = std::exchange(seen_, {});
    }

private:
    std::map<Key, Row> rows_; // the last refresh's rows, less those the current refresh has named
    std::map<Key, Row> seen_; // the rows the current refresh has named
};

} // namespace tr::app
