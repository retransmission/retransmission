// This file Copyright © Mike Gelfand
// It may be used under the 3-clause BSD (SPDX: BSD-3-Clause).
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t, std::byte
#include <cstdint> // uint8_t
#include <numeric> // std::iota
#include <span>
#include <utility> // std::swap

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
        std::iota(std::begin(s_), std::end(s_), uint8_t{ 0 });

        for (size_t i = 0, j = 0; i < 256; ++i) {
            j = static_cast<uint8_t>(j + s_[i] + std::to_integer<uint8_t>(key[i % std::size(key)]));
            std::swap(s_[i], s_[j]);
        }
    }

    // tgt must hold at least std::size(src) bytes; src and tgt may be equal
    constexpr void process(std::span<std::byte const> src, std::span<std::byte> tgt)
    {
        // tldr: the compiler thinks the in-loop assignment call could
        // overwrite `i_` or `j_`, so it reloads them each time we loop.
        // Hosting them into locals avoids that work.
        auto i = i_;
        auto j = j_;

        for (size_t k = 0, n = std::size(src); k < n; ++k) {
            tgt[k] = src[k] ^ std::byte{ next(i, j) };
        }

        i_ = i;
        j_ = j;
    }

    constexpr void discard(size_t length)
    {
        while (length-- > 0) {
            next(i_, j_);
        }
    }

private:
    // state is passed by reference so that process() can keep it
    // in locals across its hot loop instead of touching members
    constexpr uint8_t next(uint8_t& i, uint8_t& j)
    {
        i += 1;
        j += s_[i];

        std::swap(s_[i], s_[j]);

        return s_[static_cast<uint8_t>(s_[i] + s_[j])];
    }

    std::array<uint8_t, 256> s_ = {};
    uint8_t i_ = 0;
    uint8_t j_ = 0;
};
