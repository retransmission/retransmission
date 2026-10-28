// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <compare>
#include <cstddef> // size_t
#include <cstdint> // for uint64_t
#include <locale>
#include <optional>
#include <string>
#include <string_view>

namespace tr::Values
{
enum class MemoryUnits : uint8_t { Bytes, KBytes, MBytes, GBytes, TBytes };

using StorageUnits = MemoryUnits;

enum class SpeedUnits : uint8_t { Byps, KByps, MByps, GByps, TByps };

struct Config {
    enum class Base : uint16_t { Kilo = 1000U, Kibi = 1024U };

    class UnitsBase
    {
    protected:
        // One per enumerator of `MemoryUnits` and `SpeedUnits`.
        static constexpr auto UnitCount = size_t{ 5U };

    public:
        [[nodiscard]] constexpr auto base() const noexcept
        {
            return static_cast<size_t>(base_);
        }

        [[nodiscard]] constexpr auto display_name(size_t const units) const noexcept
        {
            return std::string_view{ units < std::size(display_names_) ? std::data(display_names_[units]) : "" };
        }

        [[nodiscard]] std::array<std::string, UnitCount> display_names() const;

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

        std::array<std::array<char, 32>, UnitCount> display_names_ = {};
        std::array<uint64_t, UnitCount> multipliers_ = {};
        Base base_ = {}; // NOLINT(bugprone-invalid-enum-default-initialization): overwritten by set_base()
    };

    template<typename UnitsEnum>
    struct Units : UnitsBase {
        template<typename... Names> // NOLINTNEXTLINE(misc-explicit-constructor)
        Units(Base base, Names... names)
        {
            static_assert(sizeof...(Names) == UnitCount);
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

    // How `Ratio` shows its two non-numeric states.
    // Apps set these with the unit names, after loading translations.
    struct RatioStrings {
        std::string none;
        std::string infinity;
    };

    static RatioStrings ratio;
};

namespace detail
{
// Renders `quantity` in the largest unit that keeps it readable,
// e.g. 1'500'000 with Config::storage -> "1.50 MB".
[[nodiscard]] std::string to_string(uint64_t quantity, Config::UnitsBase const& units);

// `num * scale / den`, rounded half up, without overflowing; saturates at UINT64_MAX.
// `den` must not be zero.
[[nodiscard]] constexpr uint64_t scaled_quotient(uint64_t num, uint64_t den, uint64_t const scale) noexcept
{
    // keep `remainder * scale + den / 2` in range; the dropped bits are far below `scale`
    while (den > UINT64_MAX / (scale + 1U)) {
        num >>= 1U;
        den >>= 1U;
    }

    auto const whole = num / den;
    if (whole >= UINT64_MAX / scale) {
        return UINT64_MAX;
    }

    return (whole * scale) + ((((num % den) * scale) + (den / 2U)) / den);
}
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

// Progress in [0..1], e.g. a download's completion or a seed's progress toward its ratio.
// Exact about completion. Otherwise quantized to 0.01%, the finest step any display
// shows, so values that compare equal display the same and a poll that only moves
// within a step reports no change.
class Percent
{
public:
    // 0%, incomplete
    constexpr Percent() noexcept = default;

    // Complete iff `have >= total`, which includes an empty torrent (`total == 0`).
    // Metadata is the exception: a zero size means the size is unknown; pass `Percent{}` for that.
    constexpr Percent(uint64_t const have, uint64_t const total) noexcept
        : have_{ have }
        , total_{ total }
        , complete_{ have >= total }
    {
    }

    // For sources that only report a fraction. Complete iff `fraction >= 1.0`.
    [[nodiscard]] static Percent from_double(double fraction) noexcept;

    [[nodiscard]] constexpr bool is_complete() const noexcept
    {
        return complete_;
    }

    [[nodiscard]] constexpr auto have() const noexcept
    {
        return have_;
    }

    [[nodiscard]] constexpr auto total() const noexcept
    {
        return total_;
    }

    // `have / total` as is, e.g. for the RPC
    [[nodiscard]] constexpr double quotient() const noexcept
    {
        if (complete_) {
            return 1.0;
        }

        return total_ != 0U ? static_cast<double>(have_) / static_cast<double>(total_) : 0.0;
    }

    // In [0..1]; at most 0.9999 until complete, so a bar sized from it never fills early.
    [[nodiscard]] constexpr double to_fraction() const noexcept
    {
        return static_cast<double>(ten_thousandths()) / static_cast<double>(Granularity);
    }

    // Digits only, for a "{percent}%" template that the caller translates:
    // two decimals below 5, one below 100, none at 100. Rounds, but reads "99.9" until complete.
    // `loc` formats the digits and separators; the default is the global C++ locale.
    [[nodiscard]] std::string to_string(std::locale const& loc = std::locale{}) const;

    // Likewise in whole percents, for compact progress bars: "99" until complete.
    [[nodiscard]] std::string to_whole_string(std::locale const& loc = std::locale{}) const;

    [[nodiscard]] constexpr std::strong_ordering operator<=>(Percent const& that) const noexcept
    {
        return ten_thousandths() <=> that.ten_thousandths();
    }

    [[nodiscard]] constexpr bool operator==(Percent const& that) const noexcept
    {
        return ten_thousandths() == that.ten_thousandths();
    }

private:
    static constexpr auto Granularity = uint64_t{ 10'000U };

    // `Granularity` iff complete; otherwise capped one step below it
    [[nodiscard]] constexpr uint64_t ten_thousandths() const noexcept
    {
        if (complete_) {
            return Granularity;
        }

        if (total_ == 0U) {
            return 0U;
        }

        auto const rounded = detail::scaled_quotient(have_, total_, Granularity);
        return rounded < Granularity ? rounded : Granularity - 1U;
    }

    uint64_t have_ = 0U;
    uint64_t total_ = 0U;
    bool complete_ = false;
};

// An upload ratio, `uploaded / downloaded`, or one of the two states a zero
// denominator leaves: no data at all, or uploads against nothing downloaded.
// Quantized to 0.01, the finest step any display shows.
class Ratio
{
public:
    // none
    constexpr Ratio() noexcept = default;

    constexpr Ratio(uint64_t const numerator, uint64_t const denominator) noexcept
        : numerator_{ numerator }
        , denominator_{ denominator }
    {
    }

    // For a ratio that exists only as a number, e.g. the seed ratio limit pref,
    // or the RPC `upload_ratio`, which can carry TR_RATIO_NA (-1) or TR_RATIO_INF (-2).
    [[nodiscard]] static Ratio from_double(double ratio) noexcept;

    [[nodiscard]] constexpr bool is_none() const noexcept
    {
        return state() == State::None;
    }

    [[nodiscard]] constexpr bool is_infinite() const noexcept
    {
        return state() == State::Infinite;
    }

    [[nodiscard]] constexpr auto numerator() const noexcept
    {
        return numerator_;
    }

    [[nodiscard]] constexpr auto denominator() const noexcept
    {
        return denominator_;
    }

    // The ratio when it is a number, quantized; nullopt for the two states.
    [[nodiscard]] constexpr std::optional<double> to_double() const noexcept
    {
        if (state() != State::Finite) {
            return {};
        }

        return static_cast<double>(hundredths()) / static_cast<double>(Granularity);
    }

    // `numerator / denominator` as is, e.g. for the RPC; nullopt for the two states.
    [[nodiscard]] constexpr std::optional<double> quotient() const noexcept
    {
        if (state() != State::Finite) {
            return {};
        }

        return static_cast<double>(numerator_) / static_cast<double>(denominator_);
    }

    // `Config::ratio.none`, `Config::ratio.infinity`, or digits with Percent::to_string()'s tiers.
    // `loc` formats the digits and separators; the default is the global C++ locale.
    [[nodiscard]] std::string to_string(std::locale const& loc = std::locale{}) const;

    // No ordering: where the two states sort is a view's decision.
    [[nodiscard]] constexpr bool operator==(Ratio const& that) const noexcept
    {
        return state() == that.state() && (state() != State::Finite || hundredths() == that.hundredths());
    }

private:
    enum class State : uint8_t { Finite, None, Infinite };

    static constexpr auto Granularity = uint64_t{ 100U };

    [[nodiscard]] constexpr State state() const noexcept
    {
        if (denominator_ != 0U) {
            return State::Finite;
        }

        return numerator_ != 0U ? State::Infinite : State::None;
    }

    [[nodiscard]] constexpr uint64_t hundredths() const noexcept
    {
        return detail::scaled_quotient(numerator_, denominator_, Granularity);
    }

    uint64_t numerator_ = 0U;
    uint64_t denominator_ = 0U;
};

} // namespace tr::Values
