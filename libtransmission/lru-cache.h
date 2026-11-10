// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::min_element
#include <array>
#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <utility> // std::exchange
#include <vector>

// A fixed-size cache that erases least-recently-used items to make room for new ones.
//
// Whatever removes a value returns it instead of destroying it, so that a
// caller that guards the cache with a lock can destroy the value unlocked.
template<typename Key, typename Val, std::size_t N>
class tr_lru_cache
{
public:
    [[nodiscard]] constexpr Val* get(Key const& key) noexcept
    {
        if (auto const found = find(key); found != nullptr) {
            found->sequence_ = next_sequence_++;
            return &found->val_;
        }

        return nullptr;
    }

    [[nodiscard]] constexpr bool contains(Key const& key) const noexcept
    {
        return find(key) != nullptr;
    }

    // Returns the value evicted to make room, or an empty one.
    [[nodiscard]] Val add(Key&& key, Val&& val)
    {
        auto& entry = *std::min_element(std::begin(entries_), std::end(entries_), [](auto const& a, auto const& b) {
            return a.sequence_ < b.sequence_;
        });
        auto evicted = take(entry);
        entry.key_ = std::move(key);
        entry.val_ = std::move(val);
        entry.sequence_ = next_sequence_++;

        key = {};
        return evicted;
    }

    // Returns the value erased, or an empty one.
    [[nodiscard]] Val erase(Key const& key)
    {
        if (auto* const found = find(key); found != nullptr) {
            return take(*found);
        }

        return {};
    }

    [[nodiscard]] std::vector<Val> erase_if(std::function<bool(Key const&, Val const&)> const& test)
    {
        auto erased = std::vector<Val>{};
        for (auto& entry : entries_) {
            if (entry.sequence_ != InvalidSeq && test(entry.key_, entry.val_)) {
                erased.emplace_back(take(entry));
            }
        }
        return erased;
    }

    [[nodiscard]] std::vector<Val> clear()
    {
        return erase_if([](Key const& /*key*/, Val const& /*val*/) { return true; });
    }

private:
    struct Entry {
        Key key_ = {};
        Val val_ = {};
        uint64_t sequence_ = InvalidSeq;
    };

    [[nodiscard]] static Val take(Entry& entry)
    {
        return std::exchange(entry, Entry{}).val_;
    }

    [[nodiscard]] constexpr Entry* find(Key const& key) noexcept
    {
        for (auto& entry : entries_) {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key) {
                return &entry;
            }
        }

        return nullptr;
    }

    [[nodiscard]] constexpr Entry const* find(Key const& key) const noexcept
    {
        for (auto const& entry : entries_) {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key) {
                return &entry;
            }
        }

        return nullptr;
    }

    std::array<Entry, N> entries_;
    uint64_t next_sequence_ = 1U;
    static uint64_t constexpr InvalidSeq = 0U;
};
