// This file Copyright © Mike Gelfand
// It may be used under the 3-clause BSD (SPDX: BSD-3-Clause).
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t, std::byte
#include <cstdint> // uint8_t, uint32_t
#include <numeric> // std::iota
#include <span>
#include <utility> // std::swap

#include "libtransmission/tr-assert.h"

// The hot loop is unrolled by calling step() several times per iteration.
// Those calls must be inlined whatever the surrounding translation unit
// looks like: when the inliner declines, every keystream byte becomes a
// call through reference parameters, which halves throughput.
#if defined(__GNUC__) || defined(__clang__)
#define TR_ARC4_ALWAYS_INLINE [[gnu::always_inline]] inline
#elif defined(_MSC_VER)
#define TR_ARC4_ALWAYS_INLINE __forceinline
#else
#define TR_ARC4_ALWAYS_INLINE inline
#endif

/**
 * This is a tiny and reusable implementation of alleged RC4 cipher.
 * https://en.wikipedia.org/wiki/RC4
 *
 * The use of RC4 is declining due to security concerns.
 * Popular cryptographic libraries have deprecated or removed it:
 *
 *  - OpenSSL: disabled by default in 1.1, moved to "legacy" in 3.0
 *  - WolfSSL (CyaSSL): disabled by default in 3.4.6
 *  - MbedTLS (PolarSSL): removed in 3.0
 *
 * Nonetheless it's still used in BitTorrent Protocol Encryption
 * https://en.wikipedia.org/wiki/BitTorrent_protocol_encryption,
 * so this header file provides an implementation.
 */
class tr_arc4
{
public:
    constexpr void init(std::span<std::byte const> key)
    {
        // NOLINTNEXTLINE(modernize-use-ranges): TODO(c++23) P2440, gcc 13.1, clang 23
        std::iota(std::begin(s_), std::end(s_), uint32_t{ 0 });

        for (size_t i = 0, j = 0; i < 256; ++i) {
            j = static_cast<uint8_t>(j + s_[i] + std::to_integer<uint8_t>(key[i % std::size(key)]));
            std::swap(s_[i], s_[j]);
        }
    }

    // tgt must hold at least std::size(src) bytes; src and tgt may be equal
    constexpr void process(std::span<std::byte const> src, std::span<std::byte> tgt)
    {
        // the loop below writes through a raw pointer, so nothing else checks this
        TR_ASSERT(std::size(tgt) >= std::size(src));

        auto remaining = std::size(src);

        // Each step writes back the swap for one byte, then loads the two
        // table entries the following byte needs before storing the output.
        // The output store goes through a std::byte pointer, which may alias
        // the table, so loads placed after it could not be hoisted above it.
        auto const* in = std::data(src);
        auto* out = std::data(tgt);
        auto i = static_cast<uint8_t>(i_ + 1);
        auto x = s_[i];
        auto j = static_cast<uint8_t>(j_ + x);
        auto y = s_[j];

        // Eight steps per iteration, written out: -O2 leaves an inner
        // counted loop as a loop instead of unrolling it.
        while (remaining >= 8) {
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            step(i, j, x, y, in, out);
            remaining -= 8;
        }
        while (remaining > 0) {
            step(i, j, x, y, in, out);
            --remaining;
        }

        // i, j, x, y describe the byte after the last one emitted; the
        // stored state is the last byte's indices.
        i_ = static_cast<uint8_t>(i - 1);
        j_ = static_cast<uint8_t>(j - x);
    }

    constexpr void discard(size_t length)
    {
        while (length-- > 0) {
            advance(i_, j_);
        }
    }

private:
    // Emits one keystream byte whose swap entries x = s_[i] and y = s_[j]
    // are already loaded, and leaves i, j, x, y loaded for the next byte.
    TR_ARC4_ALWAYS_INLINE constexpr void step(
        uint8_t& i,
        uint8_t& j,
        uint32_t& x,
        uint32_t& y,
        std::byte const*& in,
        std::byte*& out) noexcept
    {
        s_[i] = y;
        s_[j] = x;
        auto const keystream = s_[static_cast<uint8_t>(x + y)];
        i += 1;
        auto const next_x = s_[i];
        auto const next_j = static_cast<uint8_t>(j + next_x);
        auto const next_y = s_[next_j];
        *out++ = *in++ ^ static_cast<std::byte>(keystream);
        j = next_j;
        x = next_x;
        y = next_y;
    }

    // Advances the state past one byte without producing its keystream value.
    // discard() uses it; process() does the same work through step().
    constexpr void advance(uint8_t& i, uint8_t& j) noexcept
    {
        i += 1;
        auto const x = s_[i];
        j = static_cast<uint8_t>(j + x);
        auto const y = s_[j];

        // x and y are already loaded, so store them back directly
        s_[i] = y;
        s_[j] = x;
    }

    // Entries remain in [0, 255]. Word-sized slots make the hot loop
    // faster without changing the byte-sized indices or the keystream.
    std::array<uint32_t, 256> s_ = {};
    uint8_t i_ = 0;
    uint8_t j_ = 0;
};

#undef TR_ARC4_ALWAYS_INLINE
