// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t
#include <compare>
#include <cstdint> // for uint64_t
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace tr::Values
{
enum class MemoryUnits : uint8_t { Bytes, KBytes, MBytes, GBytes, TBytes };

using StorageUnits = MemoryUnits;

enum class SpeedUnits : uint8_t { Byps, KByps, MByps, GByps, TByps };

struct Config {
    enum class Base : uint16_t { Kilo = 1000U, Kibi = 1024U };

    class UnitsBase
    {
    public:
        [[nodiscard]] constexpr auto base() const noexcept
        {
            return static_cast<size_t>(base_);
        }

        [[nodiscard]] constexpr auto display_name(size_t const units) const noexcept
        {
            return std::string_view{ units < std::size(display_names_) ? std::data(display_names_[units]) : "" };
        }

        [[nodiscard]] std::vector<std::string> display_names() const;

    protected:
        constexpr void set_base(Base base) noexcept
        {
            base_ = base;

            auto val = uint64_t{ 1U };
            for (auto& multiplier : multipliers_) {
                multiplier = val;
                val *= static_cast<size_t>(base);
            }
        }

        // Copies as much of `name` as fits, truncating the rest.
        void set_name(size_t idx, std::string_view name);

        std::array<std::array<char, 32>, 5> display_names_ = {};
        std::array<uint64_t, 5> multipliers_ = {};
        Base base_ = {}; // NOLINT(bugprone-invalid-enum-default-initialization): overwritten by set_base()
    };

    template<typename UnitsEnum>
    struct Units : UnitsBase {
        template<typename... Names> // NOLINTNEXTLINE(google-explicit-constructor)
        Units(Base base, Names... names)
        {
            static_assert(sizeof...(Names) == std::tuple_size_v<decltype(display_names_)>);
            set_base(base);

            auto idx = size_t{ 0U };
            (set_name(idx++, names), ...);
        }

        [[nodiscard]] constexpr auto display_name(UnitsEnum multiplier) const noexcept
        {
            return UnitsBase::display_name(static_cast<size_t>(multiplier));
        }

        [[nodiscard]] constexpr auto multiplier(UnitsEnum multiplier) const noexcept
        {
            return multipliers_[static_cast<size_t>(multiplier)];
        }
    };

    static Units<MemoryUnits> memory;
    static Units<SpeedUnits> speed;
    static Units<StorageUnits> storage;
};

namespace detail
{
// Renders `quantity` in the largest unit that keeps it readable,
// e.g. 1'500'000 with Config::storage -> "1.50 MB".
[[nodiscard]] std::string to_string(uint64_t quantity, Config::UnitsBase const& units);
} // namespace detail

template<typename UnitsEnum, Config::Units<UnitsEnum> const& units_>
class Value
{
public:
    using Units = UnitsEnum;

    constexpr Value() = default;

    constexpr Value(uint64_t value, Units multiple)
        : base_quantity_{ value * units_.multiplier(multiple) }
    {
    }

    template<typename Number>
    Value(Number value, Units multiple)
        : base_quantity_{ static_cast<uint64_t>(value * units_.multiplier(multiple)) }
    {
    }

    [[nodiscard]] constexpr auto base_quantity() const noexcept
    {
        return base_quantity_;
    }

    [[nodiscard]] constexpr auto is_zero() const noexcept
    {
        return base_quantity_ == 0U;
    }

    [[nodiscard]] constexpr auto count(Units tgt) const noexcept
    {
        return base_quantity_ / (1.0 * units_.multiplier(tgt));
    }

    constexpr auto& operator+=(Value const& that) noexcept
    {
        base_quantity_ += that.base_quantity_;
        return *this;
    }

    [[nodiscard]] constexpr auto operator+(Value const& that) const noexcept
    {
        auto ret = *this;
        return ret += that;
    }

    constexpr auto& operator*=(uint64_t mult) noexcept
    {
        base_quantity_ *= mult;
        return *this;
    }

    [[nodiscard]] constexpr auto operator*(uint64_t mult) const noexcept
    {
        auto ret = *this;
        return ret *= mult;
    }

    constexpr auto& operator/=(uint64_t mult) noexcept
    {
        base_quantity_ /= mult;
        return *this;
    }

    [[nodiscard]] constexpr auto operator/(uint64_t mult) const noexcept
    {
        auto ret = *this;
        return ret /= mult;
    }

    [[nodiscard]] constexpr auto operator<=>(Value const& that) const noexcept = default;

    [[nodiscard]] constexpr bool operator==(Value const& that) const noexcept = default;

    [[nodiscard]] std::string to_string() const
    {
        return detail::to_string(base_quantity_, units_);
    }

    [[nodiscard]] static constexpr auto const& units() noexcept
    {
        return units_;
    }

private:
    uint64_t base_quantity_ = {};
};

using Memory = Value<MemoryUnits, Config::memory>;
using Storage = Value<StorageUnits, Config::storage>;
using Speed = Value<SpeedUnits, Config::speed>;

} // namespace tr::Values
