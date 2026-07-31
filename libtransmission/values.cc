// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n(), std::min()
#include <array>
#include <cmath> // std::fabs(), std::floor()
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <iterator> // std::begin(), std::size()
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/values.h"

using namespace std::literals;

namespace tr::Values
{

// default values; can be overridden by client apps
Config::Units<MemoryUnits> Config::memory{ Config::Base::Kibi, "B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv };
Config::Units<SpeedUnits> Config::speed{ Config::Base::Kilo, "B/s"sv, "kB/s"sv, "MB/s"sv, "GB/s"sv, "TB/s"sv };
Config::Units<StorageUnits> Config::storage{ Config::Base::Kilo, "B"sv, "kB"sv, "MB"sv, "GB"sv, "TB"sv };

std::vector<std::string> Config::UnitsBase::display_names() const
{
    auto names = std::vector<std::string>{};
    names.reserve(display_names_.size());

    for (auto const& name : display_names_) {
        names.emplace_back(std::data(name));
    }

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

} // namespace tr::Values
