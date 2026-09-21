// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t, std::byte
#include <cstdint> // int64_t
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <small/vector.hpp>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/benc.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant-common.h"
#include "libtransmission/variant.h"

using namespace std::literals;

// ---

namespace tr::benc::impl
{

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.
 * Example: i3e represents the integer "3"
 *
 * The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte
 */
std::optional<int64_t> ParseInt(std::string_view* benc)
{
    // skip the beginning delimiter
    auto walk = *benc;
    if (!walk.starts_with('i')) {
        return {};
    }
    walk.remove_prefix(1);

    // parse the number and make sure the ending delimiter follows it
    auto const number_begin = walk;
    auto const value = tr_num_parse<int64_t>(walk, &walk);
    if (!value || !walk.starts_with('e')) {
        return {};
    }

    // leading zeroes are not allowed
    auto digits = number_begin.substr(0, std::size(number_begin) - std::size(walk));
    if (digits.starts_with('-')) {
        digits.remove_prefix(1);
    }
    if (std::size(digits) > 1 && digits.front() == '0') {
        return {};
    }

    walk.remove_prefix(1);
    *benc = walk;
    return value;
}

/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam"
 */
std::optional<std::string_view> ParseString(std::string_view* benc)
{
    static auto constexpr MaxLength = size_t{ 128 * 1024 * 1024 }; // arbitrary

    // get the string length.
    // Parsing as unsigned rejects signs and whitespace,
    // so anything but a digit run ends the number.
    auto walk = *benc;
    auto const len = tr_num_parse<size_t>(walk, &walk);
    if (!len || *len >= MaxLength) {
        return {};
    }

    // skip the ':' delimiter
    if (!walk.starts_with(':')) {
        return {};
    }
    walk.remove_prefix(1);

    // do we have `len` bytes of string data?
    if (std::size(walk) < *len) {
        return {};
    }

    *benc = walk.substr(*len);
    return walk.substr(0, *len);
}

} // namespace tr::benc::impl

// ---

namespace
{
namespace parse_helpers
{
struct VariantBuilder : public tr::benc::Handler {
    VariantBuilder(tr_variant* const top, bool const inplace)
        : inplace_{ inplace }
    {
        stack_.push_back(top);
    }

    bool Int64(int64_t const value, Context const& /*context*/) final
    {
        return add(value) != nullptr;
    }

    bool String(std::string_view const sv, Context const& /*context*/) final
    {
        return add(inplace_ ? tr_variant::unmanaged_string(sv) : tr_variant{ sv }) != nullptr;
    }

    bool StartDict(Context const& /*context*/) final
    {
        return push(tr_variant::Map{});
    }

    bool Key(std::string_view const sv, Context const& /*context*/) final
    {
        key_ = tr_quark_new(sv);
        return true;
    }

    bool EndDict(Context const& /*context*/) final
    {
        pop();
        return true;
    }

    bool StartArray(Context const& /*context*/) final
    {
        return push(tr_variant::Vector{});
    }

    bool EndArray(Context const& /*context*/) final
    {
        pop();
        return true;
    }

private:
    template<typename Val>
    [[nodiscard]] tr_variant* add(Val val)
    {
        auto* const node = get_node();
        if (node != nullptr) {
            *node = std::move(val);
        }
        return node;
    }

    template<typename Container>
    [[nodiscard]] bool push(Container container)
    {
        auto* const node = add(std::move(container));
        if (node != nullptr) {
            stack_.push_back(node);
        }
        return node != nullptr;
    }

    void pop()
    {
        // `tr::benc::parse()` rejects an unbalanced 'e' before calling End*()
        TR_ASSERT(std::size(stack_) > 1U);
        stack_.pop_back();
    }

    // Returns where the next value goes, or nullptr if it has no place.
    [[nodiscard]] tr_variant* get_node()
    {
        auto* const parent = stack_.back();

        if (auto* const vec = parent->get_if<tr_variant::Vector>()) {
            return &vec->emplace_back();
        }

        if (auto* const map = parent->get_if<tr_variant::Map>()) {
            // `tr::benc::parse()` only calls Key() for strings,
            // so a non-string in the key position, e.g. `di1ei2ee`,
            // arrives here with no key.
            if (!key_) {
                return nullptr;
            }

            auto const key = *key_;
            key_.reset();
            return &(*map)[key];
        }

        // `parent` is the still-empty top
        return parent;
    }

    static auto constexpr TypicalMaxDepth = 16U;

    bool const inplace_;
    std::optional<tr_quark> key_;

    // The open containers, innermost last, atop the top-level variant.
    small::vector<tr_variant*, TypicalMaxDepth> stack_;
};
} // namespace parse_helpers
} // namespace

std::optional<tr_variant> tr_variant_serde::parse_benc(std::string_view input)
{
    auto top = tr_variant{};
    auto stack = tr::benc::ParserStack<512>{};
    auto handler = parse_helpers::VariantBuilder{ &top, parse_inplace_ };
    if (tr::benc::parse(input, stack, handler, &end_, &error_)) {
        return std::optional<tr_variant>{ std::move(top) };
    }

    return {};
}

// ---

namespace
{
namespace to_string_helpers
{
using OutBuf = fmt::memory_buffer;

struct BencWriter {
    void operator()(std::monostate /*unused*/) const
    {
    }

    void operator()(std::nullptr_t) const
    {
        write_string(""sv);
    }

    void operator()(bool val) const
    {
        append_literal(val ? "i1e"sv : "i0e"sv);
    }

    void operator()(int64_t val) const
    {
        write_int(val);
    }

    void operator()(double val) const
    {
        write_real(val);
    }

    void operator()(std::string_view sv) const
    {
        write_string(sv);
    }

    void operator()(tr_variant::Vector const& vec) const
    {
        out_.push_back('l');
        for (auto const& child : vec) {
            child.visit(*this);
        }
        out_.push_back('e');
    }

    void operator()(tr_variant::Map const& map) const
    {
        out_.push_back('d');
        for (auto const& [key, child] : tr::variant::detail::sorted_entries(map)) {
            write_string(key);
            child->visit(*this);
        }
        out_.push_back('e');
    }

    OutBuf& out_;

private:
    void write_string(std::string_view sv) const
    {
        fmt::format_to(fmt::appender(out_), "{:d}:{:s}", std::size(sv), sv);
    }

    void write_int(int64_t val) const
    {
        fmt::format_to(fmt::appender(out_), "i{:d}e", val);
    }

    void write_real(double val) const
    {
        auto buf = fmt::memory_buffer{};
        fmt::format_to(fmt::appender(buf), "{:f}", val);
        write_string({ std::data(buf), std::size(buf) });
    }

    void append_literal(std::string_view literal) const
    {
        out_.append(std::data(literal), std::data(literal) + std::size(literal));
    }
};

} // namespace to_string_helpers
} // namespace

std::string tr_variant_serde::to_benc_string(tr_variant const& var)
{
    using namespace to_string_helpers;

    auto buf = OutBuf{};
    var.visit(BencWriter{ buf });
    return fmt::to_string(buf);
}
