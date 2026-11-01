// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/api-compat.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

template<>
[[nodiscard]] std::optional<double> tr_variant::value_if() const noexcept
{
    switch (index()) {
    case DoubleIndex:
        return *get_if<DoubleIndex>();

    case IntIndex:
        return static_cast<double>(*get_if<IntIndex>());

    case StringIndex:
    case StringViewIndex:
        if (auto const sv = value_if<std::string_view>()) {
            return tr_num_parse<double>(*sv);
        }
        break;

    default:
        break;
    }

    return {};
}

// ---

tr_variant tr_variant::clone() const
{
    return visit([](auto const& val) -> tr_variant {
        using ValueType = std::remove_cvref_t<decltype(val)>;

        if constexpr (std::is_same_v<ValueType, Vector>) {
            auto vec = Vector{};
            vec.reserve(std::size(val));
            for (auto const& child : val) {
                vec.emplace_back(child.clone());
            }
            return vec;
        } else if constexpr (std::is_same_v<ValueType, Map>) {
            return val.clone();
        } else {
            // operator=() copies a std::string_view into an owned std::string
            return val;
        }
    });
}

tr_variant::Map tr_variant::Map::clone() const
{
    auto ret = Map{ size() };
    for (auto const& [key, child] : vec_) {
        ret.vec_.emplace_back(key, child.clone());
    }
    return ret;
}

tr_variant::Map& tr_variant::Map::merge(Map const& that)
{
    reserve(size() + that.size());
    for (auto const& [key, child] : that) {
        if (!contains(key)) {
            vec_.emplace_back(key, child.clone());
        }
    }
    return *this;
}

tr_variant::Map& tr_variant::Map::merge(Map&& that)
{
    reserve(size() + that.size());
    for (auto& [key, child] : std::move(that)) {
        try_emplace(key, std::move(child));
    }
    return *this;
}

// ---

std::optional<tr_variant> tr_variant_serde::parse(std::string_view input)
{
    error_ = {};
    return type_ == Type::Json ? parse_json(input) : parse_benc(input);
}

[[nodiscard]] std::optional<tr_variant> tr_variant_serde::parse_file(std::string_view filename)
{
    TR_ASSERT_MSG(!parse_inplace_, "not supported in parse_file()");
    parse_inplace_ = false;

    if (auto buf = std::vector<char>{}; tr_file_read(filename, buf, &error_)) {
        return parse(buf);
    }

    return {};
}

std::string tr_variant_serde::to_string(tr_variant const& var) const
{
    return type_ == Type::Json ? to_json_string(var) : to_benc_string(var);
}

bool tr_variant_serde::to_file(tr_variant const& var, std::string_view filename)
{
    if (!tr_file_save(filename, to_string(var), &error_)) {
        tr_logAddError(
            fmt::format(
                "Couldn't save '{path}': {error} ({error_code})",
                fmt::arg("path", filename),
                fmt::arg("error", error_.message()),
                fmt::arg("error_code", error_.code())));
        return false;
    }

    return true;
}

namespace tr::settings
{

Settings load(std::string_view filename)
{
    if (tr_sys_path_exists(filename)) {
        if (auto var = tr_variant_serde::json().parse_file(filename)) {
            if (auto* settings = var->get_if<tr_variant::Map>()) {
                tr::api_compat::convert_incoming_data(*settings);
                return std::move(*settings);
            }
        }
    }

    return {};
}

bool save(std::string_view filename, Settings const& settings)
{
    auto copy = settings.clone();
    tr::api_compat::convert_outgoing_data(copy);

    return tr_variant_serde::json().to_file(tr_variant{ std::move(copy) }, filename);
}

} // namespace tr::settings
