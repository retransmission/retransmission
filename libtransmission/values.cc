// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n(), std::min(), std::ranges::transform()
#include <array>
#include <cmath> // std::fabs(), std::floor(), std::llround()
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <iterator> // std::begin(), std::size()
#include <locale>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "libtransmission/values.h"

using namespace std::literals;

namespace tr::Values
{

// default values; can be overridden by client apps
Config::Units<MemoryUnits> Config::memory{ Config::Base::Kibi, "B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv };
Config::Units<SpeedUnits> Config::speed{ Config::Base::Kilo, "B/s"sv, "kB/s"sv, "MB/s"sv, "GB/s"sv, "TB/s"sv };
Config::Units<StorageUnits> Config::storage{ Config::Base::Kilo, "B"sv, "kB"sv, "MB"sv, "GB"sv, "TB"sv };
Config::RatioStrings Config::ratio{ .none = "None", .infinity = "\xE2\x88\x9E" };

std::array<std::string, Config::UnitsBase::UnitCount> Config::UnitsBase::display_names() const
{
    auto names = std::array<std::string, UnitCount>{};
    std::ranges::transform(display_names_, std::begin(names), [](auto const& name) { return std::string{ std::data(name) }; });
    return names;
}

void Config::UnitsBase::set_name(size_t idx, std::string_view name)
{
    auto& buf = display_names_[idx];
    auto const n_copied = std::min(std::size(name), std::size(buf) - 1U);
    *std::copy_n(std::begin(name), n_copied, std::begin(buf)) = '\0';
}

namespace detail
{

std::string to_string(uint64_t const quantity, Config::UnitsBase const& units)
{
    auto idx = size_t{ 0 };
    auto val = static_cast<double>(quantity);
    // Silence a clang-tidy bug that incorrectly reports a fmt problem
    // NOLINTBEGIN(clang-analyzer-security.ArrayBound)
    for (;;) {
        if (std::fabs(val - std::floor(val)) < 0.001 && (val < 999.5 || std::empty(units.display_name(idx + 1)))) {
            return fmt::format("{:.0Lf} {:s}", val, units.display_name(idx));
        }

        if (val < 99.995) // 0.98 to 99.99
        {
            return fmt::format("{:.2Lf} {:s}", val, units.display_name(idx));
        }

        if (val < 999.95 || std::empty(units.display_name(idx + 1))) // 100.0 to 999.9
        {
            return fmt::format("{:.1Lf} {:s}", val, units.display_name(idx));
        }

        val /= static_cast<double>(units.base());
        ++idx;
    }
    // NOLINTEND(clang-analyzer-security.ArrayBound)
}

} // namespace detail

// ---

Percent Percent::from_double(double const fraction) noexcept
{
    if (fraction >= 1.0) {
        return Percent{ 1U, 1U };
    }

    if (!(fraction > 0.0)) { // also NaN
        return {};
    }

    auto const have = static_cast<uint64_t>(std::llround(fraction * static_cast<double>(Granularity)));
    return Percent{ std::min(have, Granularity - 1U), Granularity };
}

std::string Percent::to_string(std::locale const& loc) const
{
    auto const hundredths = ten_thousandths(); // of a percent

    if (hundredths == Granularity) {
        return fmt::format(loc, "{:L}", 100U);
    }

    if (hundredths < 500U) {
        return fmt::format(loc, "{:.2Lf}", static_cast<double>(hundredths) / 100.0);
    }

    // incomplete never reads as 100.0
    auto const tenths = std::min((hundredths + 5U) / 10U, uint64_t{ 999U });
    return fmt::format(loc, "{:.1Lf}", static_cast<double>(tenths) / 10.0);
}

std::string Percent::to_whole_string(std::locale const& loc) const
{
    auto const hundredths = ten_thousandths();

    if (hundredths == Granularity) {
        return fmt::format(loc, "{:L}", 100U);
    }

    // incomplete never reads as 100
    return fmt::format(loc, "{:L}", std::min((hundredths + 50U) / 100U, uint64_t{ 99U }));
}

Ratio Ratio::from_double(double const ratio) noexcept
{
    if (ratio < -1.5 || std::isinf(ratio)) { // TR_RATIO_INF (-2) or an actual infinity, not TR_RATIO_NA (-1)
        return Ratio{ 1U, 0U };
    }

    if (!(ratio >= 0.0)) { // also NaN
        return {};
    }

    // from 2^64 hundredths up, saturate as `Ratio{ UINT64_MAX, 1U }` does
    static auto constexpr Max = 0x1p64;
    auto const scaled = std::round(ratio * static_cast<double>(Granularity));
    return Ratio{ scaled < Max ? static_cast<uint64_t>(scaled) : UINT64_MAX, Granularity };
}

std::string Ratio::to_string(std::locale const& loc) const
{
    if (is_none()) {
        return Config::ratio.none;
    }

    if (is_infinite()) {
        return Config::ratio.infinity;
    }

    auto const hundredths = this->hundredths();

    if (hundredths < 500U) {
        return fmt::format(loc, "{:.2Lf}", static_cast<double>(hundredths) / 100.0);
    }

    // `hundredths` may be saturated at UINT64_MAX; scaled_quotient() rounds without overflowing
    if (auto const tenths = detail::scaled_quotient(hundredths, 10U, 1U); tenths < 1000U) {
        return fmt::format(loc, "{:.1Lf}", static_cast<double>(tenths) / 10.0);
    }

    return fmt::format(loc, "{:L}", detail::scaled_quotient(hundredths, 100U, 1U));
}

} // namespace tr::Values
