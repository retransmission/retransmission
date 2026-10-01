// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

template<typename FlagType>
class Flags
{
public:
    static_assert(std::is_enum_v<FlagType> && !std::is_convertible_v<FlagType, int>, "FlagType must be a scoped enum");

    constexpr Flags() noexcept = default;

    // NOLINTNEXTLINE(hicpp-explicit-conversions)
    constexpr Flags(FlagType const flag) noexcept
        : value_{ bit(flag) }
    {
    }

    [[nodiscard]] constexpr bool none() const noexcept
    {
        return value_ == 0U;
    }

    [[nodiscard]] constexpr bool any() const noexcept
    {
        return value_ != 0U;
    }

    [[nodiscard]] constexpr bool test(Flags const rhs) const noexcept
    {
        return (value_ & rhs.value_) != 0U;
    }

    constexpr void set(FlagType const flag) noexcept
    {
        value_ |= bit(flag);
    }

    [[nodiscard]] constexpr Flags operator|(Flags rhs) const noexcept
    {
        return rhs |= *this;
    }

    constexpr Flags& operator|=(Flags const rhs) noexcept
    {
        value_ |= rhs.value_;
        return *this;
    }

    [[nodiscard]] constexpr Flags operator~() const noexcept
    {
        auto ret = Flags{};
        ret.value_ = ~value_ & AllBits;
        return ret;
    }

private:
    using ValueType = std::uint64_t;

    static constexpr auto NFlags = static_cast<unsigned>(FlagType::N_FLAGS);
    static_assert(NFlags < std::numeric_limits<ValueType>::digits);
    static constexpr auto AllBits = ValueType{ (ValueType{ 1U } << NFlags) - 1U };

    [[nodiscard]] static constexpr ValueType bit(FlagType const flag) noexcept
    {
        return ValueType{ 1U } << static_cast<unsigned>(flag);
    }

    ValueType value_ = {};
};
