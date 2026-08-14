// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy, std::fill_n, std::min, std::max
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector> // std::vector

#include "libtransmission/bitfield.h"
#include "libtransmission/tr-assert.h" // TR_ASSERT, TR_ENABLE_ASSERTS

// ---

namespace
{

[[nodiscard]] constexpr size_t getBytesNeeded(size_t const bit_count) noexcept
{
    return (bit_count >> 3) + ((bit_count & 7) != 0 ? 1 : 0);
}

void setAllTrue(std::span<std::byte> bytes, size_t const bit_count)
{
    static auto constexpr Val = std::byte{ 0xFF };
    auto const n = getBytesNeeded(bit_count);

    TR_ASSERT(bytes.size() >= n);
    if (n <= 0U || bytes.size() < n) {
        return;
    }

    bytes = bytes.first(n);
    std::ranges::fill(bytes, Val);

    // -bit_count & 7U. Since bitcount is unsigned do ~bitcount +
    // 1 to replace -bitcount as linters warn about negating
    // unsigned types. Any compiler will optimize ~x + 1 to -x in
    // the backend.
    uint32_t const shift = ((~bit_count) + 1) & 7U;
    bytes.back() = Val << shift;
}

} // namespace

// ---

size_t tr_bitfield::count(size_t const begin, size_t end) const
{
    if (has_none()) {
        return 0;
    }

    end = std::min(end, bit_count_);
    if (begin >= end) {
        return 0;
    }

    if (has_all()) {
        return end - begin;
    }

    if (!is_size_known()) [[unlikely]] {
        return 0;
    }

    auto ret = size_t{};
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (first_byte >= std::size(flags_)) {
        return 0;
    }
    TR_ASSERT(!std::empty(flags_));

    if (first_byte == last_byte) {
        auto val = flags_[first_byte];

        auto i = begin & 7U;
        val <<= i;
        i = (begin - end) & 7U;
        val >>= i;
        ret = popcount(val);
    } else {
        size_t const walk_end = std::min(std::size(flags_), last_byte);

        /* first byte */
        size_t const first_shift = begin & 7U;
        auto val = flags_[first_byte];
        val <<= first_shift;
        /* No need to shift back val for correct popcount. */
        ret = popcount(val);

        /* middle bytes */

        /* Use 2x accumulators to help alleviate high latency of
           popcnt instruction on many architectures. */
        size_t tmp_accum = 0;
        for (size_t i = first_byte + 1; i < walk_end;) {
            tmp_accum += popcount(flags_[i]);
            i += 2;
            if (i > walk_end) {
                break;
            }
            ret += popcount(flags_[i - 1]);
        }
        ret += tmp_accum;

        /* last byte */
        if (last_byte < std::size(flags_)) {
            /* -end & 7U. Since bitcount is unsigned do ~end + 1 to
               replace -end as linters warn about negating unsigned
               types. Any compiler will optimize ~x + 1 to -x in the
               backend. */
            uint32_t const last_shift = (~end + 1) & 7U;
            val = flags_[last_byte];
            val >>= last_shift;
            /* No need to shift back val for correct popcount. */
            ret += popcount(val);
        }
    }

    TR_ASSERT(ret <= (end - begin));
    return ret;
}

// ---

bool tr_bitfield::is_valid() const
{
    if (!is_size_known()) {
        // When the size is unknown, the only valid states are "have all" or "have none"
        return std::empty(flags_) && true_count_ == 0U && have_all_hint_ != have_none_hint_;
    }

    auto const bytes_needed = getBytesNeeded(bit_count_);
    return true_count_ <= bit_count_ && std::size(flags_) <= bytes_needed &&
        (std::empty(flags_) || true_count_ == count_flags());
}

std::vector<std::byte> tr_bitfield::raw() const
{
    if (!std::empty(flags_)) {
        return flags_;
    }

    auto const n = getBytesNeeded(bit_count_);

    auto raw = std::vector<std::byte>(n);

    if (has_all()) {
        setAllTrue(raw, bit_count_);
    }

    return raw;
}

bool tr_bitfield::ensure_bits_alloced(size_t const n)
{
    if (!is_size_known() || n > size()) [[unlikely]] {
        return false;
    }

    auto const has_all = this->has_all();

    /* Can't use getBytesNeededSafe as n can be > SIZE_MAX - 8. */
    auto const bytes_needed = getBytesNeeded(has_all ? std::max(n, true_count_) : n);

    if (std::size(flags_) < bytes_needed) {
        flags_.resize(bytes_needed);
        if (has_all) {
            setAllTrue(flags_, true_count_);
        }
    }

    TR_ASSERT(is_valid());
    return true;
}

bool tr_bitfield::ensure_nth_bit_alloced(size_t const nth)
{
    // count is zero-based, so we need to allocate nth+1 bits before setting the nth
    return ensure_bits_alloced(nth + 1U);
}

void tr_bitfield::set_true_count(size_t const n) noexcept
{
    TR_ASSERT(!is_size_known() || n <= size());

    true_count_ = n;
    have_all_hint_ = n == bit_count_;
    have_none_hint_ = n == 0;

    if (has_all() || has_none()) {
        free_array();
    }

    TR_ASSERT(is_valid());
}

void tr_bitfield::increment_true_count(size_t inc) noexcept
{
    TR_ASSERT(!is_size_known() || inc <= size());
    TR_ASSERT(!is_size_known() || true_count_ <= size() - inc);

    set_true_count(true_count_ + inc);
}

void tr_bitfield::decrement_true_count(size_t dec) noexcept
{
    TR_ASSERT(!is_size_known() || dec <= size());
    TR_ASSERT(!is_size_known() || true_count_ >= dec);

    set_true_count(true_count_ - dec);
}

// ---

tr_bitfield::tr_bitfield(size_t bit_count)
    : bit_count_{ bit_count }
{
    TR_ASSERT(is_valid());
}

void tr_bitfield::set_has_none() noexcept
{
    free_array();
    true_count_ = 0;
    have_all_hint_ = false;
    have_none_hint_ = true;

    TR_ASSERT(is_valid());
}

void tr_bitfield::set_has_all() noexcept
{
    free_array();
    true_count_ = bit_count_;
    have_all_hint_ = true;
    have_none_hint_ = false;

    TR_ASSERT(is_valid());
}

bool tr_bitfield::set_raw(std::span<std::byte const> const raw)
{
    if (!is_size_known()) {
        return false;
    }

    if (auto const bytes_needed = getBytesNeeded(bit_count_); std::size(raw) > bytes_needed) {
        return false;
    }

    flags_.assign(raw.begin(), raw.end());

    // ensure any excess bits at the end of the array are set to '0'.
    if (raw.size() == getBytesNeeded(bit_count_)) {
        auto const excess_bit_count = (raw.size() * 8) - bit_count_;

        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0) {
            flags_.back() &= std::byte{ 0xff } << excess_bit_count;
        }
    }

    rebuild_true_count();
    TR_ASSERT(is_valid());
    return true;
}

bool tr_bitfield::set_from_bools(std::span<bool const> const flags)
{
    if (!is_size_known() || std::size(flags) > size()) {
        return false;
    }

    flags_.assign(getBytesNeeded(flags.size()), {});

    size_t true_count = 0;
    for (size_t i = 0; i < flags.size(); ++i) {
        if (flags[i]) {
            ++true_count;
            flags_[i >> 3U] |= (std::byte{ 0x80 } >> (i & 7U));
        }
    }

    set_true_count(true_count);
    TR_ASSERT(is_valid());
    return true;
}

bool tr_bitfield::set(size_t const nth, bool const value)
{
    if (!is_size_known() || nth >= size()) {
        return false;
    }

    if (test(nth) == value) {
        return false;
    }

    if (!ensure_nth_bit_alloced(nth)) {
        return false;
    }

    /* Already tested that val != nth bit so just swap */
    auto& byte = flags_[nth >> 3U];
#ifdef TR_ENABLE_ASSERTS
    auto const old_byte_pop = popcount(byte);
#endif
    byte ^= std::byte{ 0x80 } >> (nth & 7U);
#ifdef TR_ENABLE_ASSERTS
    auto const new_byte_pop = popcount(byte);
#endif

    if (value) {
        ++true_count_;
        TR_ASSERT(old_byte_pop + 1 == new_byte_pop);
    } else {
        --true_count_;
        TR_ASSERT(new_byte_pop + 1 == old_byte_pop);
    }
    have_all_hint_ = true_count_ == bit_count_;
    have_none_hint_ = true_count_ == 0;

    TR_ASSERT(is_valid());
    return true;
}

/* Sets bit range [begin, end) to 1 */
bool tr_bitfield::set_span(size_t const begin, size_t end, bool const value)
{
    // bounds check
    end = std::min(end, bit_count_);
    if (!is_size_known() || end > size() || begin >= end) {
        return false;
    }

    // NB: count(begin, end) can be quite expensive. Might be worth it
    // to fuse the count and set loop
    size_t const old_count = count(begin, end);
    size_t const new_count = value ? (end - begin) : 0;
    // did anything change?
    if (old_count == new_count) {
        return false;
    }

    --end;
    if (!ensure_nth_bit_alloced(end)) {
        return false;
    }

    auto walk = begin >> 3;
    auto const last_byte = end >> 3;

    auto first_mask = std::byte{ 0xff } >> (begin & 7U);
    auto last_mask = std::byte{ 0xff } << ((~end) & 7U);
    if (value) {
        if (walk == last_byte) {
            flags_[walk] |= first_mask & last_mask;
        } else {
            flags_[walk] |= first_mask;
            /* last_byte is expected to be hot in cache due to earlier
               count(begin, end) */
            flags_[last_byte] |= last_mask;
            if (++walk < last_byte) {
                std::ranges::fill(std::span{ flags_ }.subspan(walk, last_byte - walk), std::byte{ 0xff });
            }
        }

        increment_true_count(new_count - old_count);
    } else {
        first_mask = ~first_mask;
        last_mask = ~last_mask;
        if (walk == last_byte) {
            flags_[walk] &= first_mask | last_mask;
        } else {
            flags_[walk] &= first_mask;
            /* last_byte is expected to be hot in cache due to earlier
               count(begin, end) */
            flags_[last_byte] &= last_mask;
            if (++walk < last_byte) {
                std::ranges::fill(std::span{ flags_ }.subspan(walk, last_byte - walk), std::byte{});
            }
        }

        decrement_true_count(old_count);
    }

    TR_ASSERT(is_valid());
    return true;
}

tr_bitfield& tr_bitfield::operator|=(tr_bitfield const& that)
{
    if (has_all() || that.has_none()) {
        return *this;
    }

    if (that.has_all() || has_none()) {
        *this = that;
        return *this;
    }

    bit_count_ = std::max(bit_count_, that.bit_count_);
    flags_.resize(std::max(std::size(flags_), std::size(that.flags_)));

    for (size_t i = 0, n = std::size(that.flags_); i < n; ++i) {
        flags_[i] |= that.flags_[i];
    }

    rebuild_true_count();
    TR_ASSERT(is_valid());
    return *this;
}

tr_bitfield& tr_bitfield::operator&=(tr_bitfield const& that)
{
    if (has_none() || that.has_all()) {
        return *this;
    }

    if (that.has_none() || has_all()) {
        *this = that;
        return *this;
    }

    flags_.resize(std::min(std::size(flags_), std::size(that.flags_)));

    for (size_t i = 0, n = std::size(flags_); i < n; ++i) {
        flags_[i] &= that.flags_[i];
    }

    rebuild_true_count();
    TR_ASSERT(is_valid());
    return *this;
}

bool tr_bitfield::intersects(tr_bitfield const& that) const noexcept
{
    if (has_none() || that.has_none()) {
        return false;
    }

    if (has_all() || that.has_all()) {
        return true;
    }

    for (size_t i = 0, n = std::min(std::size(flags_), std::size(that.flags_)); i < n; ++i) {
        if ((flags_[i] & that.flags_[i]) != std::byte{}) {
            return true;
        }
    }

    return false;
}
