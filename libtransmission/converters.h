// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "libtransmission/types.h"
#include "libtransmission/variant.h"

struct tr_pex;

namespace tr::serializer
{

// These type traits are used by `to_variant()` and `to_value()` to sniff
// out containers that support `push_back()`, `insert()`, `reserve()`, etc.
// Example uses: (de)serializing std::vector<T>, QStringList, small::set<T>
namespace detail
{

template<std::floating_point T>
[[nodiscard]] constexpr bool values_differ(T lhs, T rhs) noexcept
{
    return std::isunordered(lhs, rhs) || std::isless(lhs, rhs) || std::isgreater(lhs, rhs);
}

template<typename T>
    requires(!std::floating_point<T>)
[[nodiscard]] constexpr bool values_differ(T const& lhs, T const& rhs)
{
    return lhs != rhs;
}

// NOLINTBEGIN(readability-identifier-naming)
// use std-style naming for these traits

// Type trait: is C a container with push_back (but not a string)?
template<typename C, typename = void>
inline constexpr bool is_push_back_range_v = false;

template<typename C>
inline constexpr bool is_push_back_range_v<
    C,
    std::void_t<
        typename C::value_type,
        decltype(std::begin(std::declval<C const&>())),
        decltype(std::end(std::declval<C const&>())),
        decltype(std::declval<C&>().push_back(
            std::declval<typename C::value_type const&>()))>> = !std::is_same_v<C, std::basic_string<typename C::value_type>>;

// Type trait: is C a container with insert (like std::set)?
template<typename C, typename = void>
inline constexpr bool is_insert_range_v = false;

template<typename C>
inline constexpr bool is_insert_range_v<
    C,
    std::void_t<
        typename C::value_type,
        decltype(std::begin(std::declval<C const&>())),
        decltype(std::end(std::declval<C const&>())),
        decltype(std::declval<C&>().insert(std::declval<typename C::value_type const&>()))>> = true;

// Type trait: is C a std::array?
template<typename C>
inline constexpr bool is_std_array_v = false;

template<typename T, std::size_t N>
inline constexpr bool is_std_array_v<std::array<T, N>> = true;

template<typename C>
inline constexpr bool is_optional_v = false;

template<typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

template<typename C>
tr_variant from_push_back_range(C const& src);

template<typename C>
bool to_push_back_range(tr_variant const& src, C* ptgt);

template<typename C>
tr_variant from_insert_range(C const& src);

template<typename C>
bool to_insert_range(tr_variant const& src, C* ptgt);

template<typename C>
tr_variant from_array(C const& src);

template<typename C>
bool to_array(tr_variant const& src, C* ptgt);

template<typename T>
tr_variant from_optional(std::optional<T> const& src);

template<typename T>
bool to_optional(tr_variant const& src, std::optional<T>* ptgt);

// Call reserve() if available, otherwise no-op
template<typename C>
auto reserve_if_possible(C& c, std::size_t n) -> decltype(c.reserve(n), void())
{
    c.reserve(n);
}
template<typename C>
void reserve_if_possible(C& /*c*/, ...) // NOLINT(cert-dcl50-cpp, modernize-avoid-variadic-functions)
{
}

// NOLINTEND(readability-identifier-naming)
} // namespace detail

/**
 * Customization point for converting `tr_variant` to/from a value of type `T`.
 *
 * Specialize this template for each supported type, providing:
 *   - `static tr_variant to_variant(T const& src);`
 *   - `static bool to_value(tr_variant const& src, T* tgt);`
 *
 * The primary template is undefined so that calling `to_variant()` or
 * `to_value()` on an unsupported type will fail to compile.
 *
 * Specializations for libtransmission's built-in types are declared in this
 * header (see below); specializations for app/UI types are in other folders.
 */
template<typename T>
struct Converter;

namespace detail
{

// True iff `Converter<T>` is specialized with the expected static methods.
template<typename T>
concept HasConverter = requires(T const& src, tr_variant const& var, T* tgt) {
    { Converter<T>::to_variant(src) } -> std::same_as<tr_variant>;
    { Converter<T>::to_value(var, tgt) } -> std::same_as<bool>;
};

} // namespace detail

// NOLINTBEGIN(bugprone-macro-parentheses)
#define TR_DECLARE_CONVERTER(type) \
    template<> \
    struct Converter<type> { \
        static tr_variant to_variant(type const& src); \
        static bool to_value(tr_variant const& src, type* tgt); \
    };
// NOLINTEND(bugprone-macro-parentheses)

/**
 * Compile-time dispatcher: routes `T` -> `tr_variant` conversion to
 * `Converter<T>` if specialized, otherwise to the generic container fallbacks
 * (push-back ranges, insert ranges, std::array, std::optional).
 */
template<typename T>
[[nodiscard]] tr_variant to_variant(T const& src)
{
    if constexpr (detail::HasConverter<T>) {
        return Converter<T>::to_variant(src);
    } else if constexpr (detail::is_push_back_range_v<T>) {
        return detail::from_push_back_range(src);
    } else if constexpr (detail::is_insert_range_v<T>) {
        return detail::from_insert_range(src);
    } else if constexpr (detail::is_std_array_v<T>) {
        return detail::from_array(src);
    } else if constexpr (detail::is_optional_v<T>) {
        return detail::from_optional(src);
    } else {
        static_assert(detail::HasConverter<T>, "No Converter<T> specialization for this type");
    }
}

/**
 * Compile-time dispatcher: routes `tr_variant` -> `T` conversion to
 * `Converter<T>` if specialized, otherwise to the generic container fallbacks
 * (push-back ranges, insert ranges, std::array, std::optional).
 *
 * Returns `true` on success and writes the decoded value to `*ptgt`.
 */
template<typename T>
bool to_value(tr_variant const& src, T* const ptgt)
{
    if constexpr (detail::HasConverter<T>) {
        return Converter<T>::to_value(src, ptgt);
    } else if constexpr (detail::is_push_back_range_v<T>) {
        return detail::to_push_back_range(src, ptgt);
    } else if constexpr (detail::is_insert_range_v<T>) {
        return detail::to_insert_range(src, ptgt);
    } else if constexpr (detail::is_std_array_v<T>) {
        return detail::to_array(src, ptgt);
    } else if constexpr (detail::is_optional_v<T>) {
        return detail::to_optional(src, ptgt);
    } else {
        static_assert(detail::HasConverter<T>, "No Converter<T> specialization for this type");
        return false;
    }
}

// Alternate version of `to_value()` that returns a std::optional
template<typename T>
[[nodiscard]] std::optional<T> to_value(tr_variant const& var)
{
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): for `T{}`
    if (auto ret = T{}; to_value<T>(var, &ret)) {
        return ret;
    }

    return {};
}

// Alternative version of `to_value()` that takes a map + key
template<typename T>
[[nodiscard]] bool to_value(tr_variant::Map const& src, tr_quark const key, T* const ptgt)
{
    auto const iter = src.find(key);
    return iter != src.end() && to_value(iter->second, ptgt);
}

// Alternative version of `to_value()` that takes a map + key and returns a std::optional
template<typename T>
[[nodiscard]] std::optional<T> to_value(tr_variant::Map const& src, tr_quark const key)
{
    auto const iter = src.find(key);
    return iter != src.end() ? to_value<T>(iter->second) : std::nullopt;
}

// returns true iff the value changed
template<typename T>
bool set(T& tgt, T src)
{
    if (detail::values_differ(tgt, src)) {
        tgt = std::move(src);
        return true;
    }

    return false;
}

template<typename T>
bool set(T& tgt, tr_variant const& src)
{
    auto val = to_value<T>(src);
    return val && set(tgt, std::move(*val));
}

// Generic integer specialization. Covers int64_t, uint64_t, uint32_t, size_t,
// time_t, etc. — including platform-dependent aliases (e.g. on Linux
// int64_t == long == time_t, uint64_t == unsigned long == size_t).
//
// `bool` and `uint16_t` are excluded:
//   - `bool` has its own specialization above.
//   - `uint16_t` is aliased by `tr_mode_t`, which needs octal-string handling.
template<typename T>
    requires(
        std::integral<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, uint16_t> && !std::is_same_v<T, char> &&
        !std::is_same_v<T, signed char> && !std::is_same_v<T, unsigned char> && !std::is_same_v<T, wchar_t> &&
        !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>)
struct Converter<T> {
    static tr_variant to_variant(T const& src)
    {
        return src;
    }

    static bool to_value(tr_variant const& src, T* const tgt)
    {
        if (auto const val = src.value_if<T>()) {
            *tgt = *val;
            return true;
        }
        return false;
    }
};

TR_DECLARE_CONVERTER(bool)
TR_DECLARE_CONVERTER(double)

TR_DECLARE_CONVERTER(std::chrono::milliseconds)
TR_DECLARE_CONVERTER(std::string)

TR_DECLARE_CONVERTER(tr_diffserv_t)
TR_DECLARE_CONVERTER(tr_encryption_mode)
TR_DECLARE_CONVERTER(tr_file_preallocation)
TR_DECLARE_CONVERTER(tr_log_level)
TR_DECLARE_CONVERTER(tr_mode_t)
TR_DECLARE_CONVERTER(tr_pex)
TR_DECLARE_CONVERTER(tr_port)
TR_DECLARE_CONVERTER(tr_sched_day)
TR_DECLARE_CONVERTER(tr_verify_added_mode)

// ---

// N.B. This second `detail` block contains the implementations of
// to_push_back_range, from_push_back_range, etc., which were forward-
// declared above. They must be defined after `to_variant`/`to_value`
// because they call them for each element.
namespace detail
{

template<typename C>
tr_variant from_push_back_range(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src) {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_push_back_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr) {
        return false;
    }

    auto tmp = C{};
    reserve_if_possible(tmp, std::size(*vec));

    for (auto const& elem : *vec) {
        typename C::value_type value{};
        if (!to_value(elem, &value)) {
            return false;
        }
        tmp.push_back(std::move(value));
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename C>
tr_variant from_insert_range(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src) {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_insert_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr) {
        return false;
    }

    auto tmp = C{};

    for (auto const& elem : *vec) {
        typename C::value_type value{};
        if (!to_value(elem, &value)) {
            return false;
        }
        tmp.insert(std::move(value));
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename C>
tr_variant from_array(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src) {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_array(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr) {
        return false;
    }

    if (std::size(*vec) != std::size(*ptgt)) {
        return false; // Array size mismatch
    }

    auto tmp = C{};
    for (std::size_t i = 0; i < std::size(*vec); ++i) {
        if (!to_value((*vec)[i], &tmp[i])) {
            return false;
        }
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename T>
tr_variant from_optional(std::optional<T> const& src)
{
    static_assert(!is_optional_v<T>);
    return src ? to_variant(*src) : nullptr;
}

template<typename T>
bool to_optional(tr_variant const& src, std::optional<T>* ptgt)
{
    static_assert(!is_optional_v<T>);
    if (src.index() == tr_variant::NullIndex) {
        ptgt->reset();
        return true;
    }

    if (auto val = to_value<T>(src)) {
        *ptgt = std::move(val);
        return true;
    }

    return false;
}

} // namespace detail
} // namespace tr::serializer
