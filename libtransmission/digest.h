// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <compare>
#include <cstddef> // size_t, std::byte
#include <string_view>

using tr_sha1_digest_t = std::array<std::byte, 20U>;

using tr_sha256_digest_t = std::array<std::byte, 32U>;

/**
 * A digest in its lowercase hex form, e.g. an info hash as it appears in
 * magnet links and in the names of torrent and resume files.
 *
 * Instances are either empty or complete -- there is no way to fill one
 * partway -- so a default-constructed instance doubles as the "no digest
 * yet" marker that `empty()` reports.
 */
template<size_t DigestLen>
class tr_hash_string
{
public:
    static auto constexpr Strlen = DigestLen * 2U;

    tr_hash_string() noexcept = default;

    explicit constexpr tr_hash_string(std::array<std::byte, DigestLen> const& digest) noexcept
    {
        for (size_t i = 0; i < DigestLen; ++i) {
            auto const val = std::to_integer<size_t>(digest[i]);
            chars_[i * 2U] = HexDigits[val >> 4U];
            chars_[(i * 2U) + 1U] = HexDigits[val & 0x0FU];
        }
    }

    [[nodiscard]] constexpr auto const* data() const noexcept
    {
        return std::data(chars_);
    }

    [[nodiscard]] constexpr auto const* c_str() const noexcept
    {
        return data();
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return chars_[0] == '\0';
    }

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return empty() ? 0U : Strlen;
    }

    [[nodiscard]] constexpr auto sv() const noexcept
    {
        return std::string_view{ data(), size() };
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return sv();
    }

    // Comparing the trailing zeroes along with the hex agrees with comparing
    // sv(): NUL sorts below every hex digit, so an empty instance orders
    // first, exactly as an empty string does.
    [[nodiscard]] constexpr auto operator<=>(tr_hash_string const&) const = default;
    [[nodiscard]] constexpr bool operator==(tr_hash_string const&) const = default;

    [[nodiscard]] constexpr bool operator==(std::string_view rhs) const noexcept
    {
        return sv() == rhs;
    }

private:
    static auto constexpr HexDigits = std::string_view{ "0123456789abcdef" };

    std::array<char, Strlen + 1U> chars_ = {};
};

// Lets fmt print a tr_hash_string as its string_view. fmt finds this by ADL,
// so it costs this header no fmt include; only the callers that format need one.
template<size_t DigestLen>
[[nodiscard]] constexpr auto format_as(tr_hash_string<DigestLen> const& hash) noexcept
{
    return hash.sv();
}

using tr_sha1_string = tr_hash_string<sizeof(tr_sha1_digest_t)>;

using tr_sha256_string = tr_hash_string<sizeof(tr_sha256_digest_t)>;
