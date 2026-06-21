// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <cmath>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "libtransmission/quark.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"

struct tr_pex;

namespace tr::serializer
{

// These type traits are used for serialize() and deserialize() to sniff
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
void reserve_if_possible(C& /*c*/, ...) // NOLINT(cert-dcl50-cpp)
{
}

// NOLINTEND(readability-identifier-naming)
} // namespace detail

/**
 * Customization point for converting `tr_variant` to/from a value of type `T`.
 *
 * Specialize this template for each supported type, providing:
 *   - `static tr_variant serialize(T const& src);`
 *   - `static bool deserialize(tr_variant const& src, T* tgt);`
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
    { Converter<T>::serialize(src) } -> std::same_as<tr_variant>;
    { Converter<T>::deserialize(var, tgt) } -> std::same_as<bool>;
};

} // namespace detail

/**
 * Compile-time dispatcher: routes `T` -> `tr_variant` conversion to
 * `Converter<T>` if specialized, otherwise to the generic container fallbacks
 * (push-back ranges, insert ranges, std::array, std::optional).
 */
template<typename T>
[[nodiscard]] tr_variant to_variant(T const& src)
{
    if constexpr (detail::HasConverter<T>)
    {
        return Converter<T>::serialize(src);
    }
    else if constexpr (detail::is_push_back_range_v<T>)
    {
        return detail::from_push_back_range(src);
    }
    else if constexpr (detail::is_insert_range_v<T>)
    {
        return detail::from_insert_range(src);
    }
    else if constexpr (detail::is_std_array_v<T>)
    {
        return detail::from_array(src);
    }
    else if constexpr (detail::is_optional_v<T>)
    {
        return detail::from_optional(src);
    }
    else
    {
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
    if constexpr (detail::HasConverter<T>)
    {
        return Converter<T>::deserialize(src, ptgt);
    }
    else if constexpr (detail::is_push_back_range_v<T>)
    {
        return detail::to_push_back_range(src, ptgt);
    }
    else if constexpr (detail::is_insert_range_v<T>)
    {
        return detail::to_insert_range(src, ptgt);
    }
    else if constexpr (detail::is_std_array_v<T>)
    {
        return detail::to_array(src, ptgt);
    }
    else if constexpr (detail::is_optional_v<T>)
    {
        return detail::to_optional(src, ptgt);
    }
    else
    {
        static_assert(detail::HasConverter<T>, "No Converter<T> specialization for this type");
        return false;
    }
}

// Alternate version of `to_value()` that returns a std::optional
template<typename T>
[[nodiscard]] std::optional<T> to_value(tr_variant const& var)
{
    if (auto ret = T{}; to_value<T>(var, &ret))
    {
        return ret;
    }

    return {};
}

// ---
// Built-in `Converter<T>` specializations defined in `serializer.cc`.

template<>
struct Converter<bool>
{
    static tr_variant serialize(bool const& src);
    static bool deserialize(tr_variant const& src, bool* tgt);
};

template<>
struct Converter<double>
{
    static tr_variant serialize(double const& src);
    static bool deserialize(tr_variant const& src, double* tgt);
};

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
struct Converter<T>
{
    static tr_variant serialize(T const& src)
    {
        return src;
    }

    static bool deserialize(tr_variant const& src, T* const tgt)
    {
        if (auto const val = src.value_if<T>())
        {
            *tgt = *val;
            return true;
        }
        return false;
    }
};

template<>
struct Converter<std::string>
{
    static tr_variant serialize(std::string const& src);
    static bool deserialize(tr_variant const& src, std::string* tgt);
};

template<>
struct Converter<std::chrono::milliseconds>
{
    static tr_variant serialize(std::chrono::milliseconds const& src);
    static bool deserialize(tr_variant const& src, std::chrono::milliseconds* tgt);
};

template<>
struct Converter<tr_diffserv_t>
{
    static tr_variant serialize(tr_diffserv_t const& src);
    static bool deserialize(tr_variant const& src, tr_diffserv_t* tgt);
};

template<>
struct Converter<tr_encryption_mode>
{
    static tr_variant serialize(tr_encryption_mode const& src);
    static bool deserialize(tr_variant const& src, tr_encryption_mode* tgt);
};

template<>
struct Converter<tr_file_preallocation>
{
    static tr_variant serialize(tr_file_preallocation const& src);
    static bool deserialize(tr_variant const& src, tr_file_preallocation* tgt);
};

template<>
struct Converter<tr_log_level>
{
    static tr_variant serialize(tr_log_level const& src);
    static bool deserialize(tr_variant const& src, tr_log_level* tgt);
};

template<>
struct Converter<tr_mode_t>
{
    static tr_variant serialize(tr_mode_t const& src);
    static bool deserialize(tr_variant const& src, tr_mode_t* tgt);
};

template<>
struct Converter<tr_pex>
{
    static tr_variant serialize(tr_pex const& src);
    static bool deserialize(tr_variant const& src, tr_pex* tgt);
};

template<>
struct Converter<tr_port>
{
    static tr_variant serialize(tr_port const& src);
    static bool deserialize(tr_variant const& src, tr_port* tgt);
};

template<>
struct Converter<tr_sched_day>
{
    static tr_variant serialize(tr_sched_day const& src);
    static bool deserialize(tr_variant const& src, tr_sched_day* tgt);
};

template<>
struct Converter<tr_verify_added_mode>
{
    static tr_variant serialize(tr_verify_added_mode const& src);
    static bool deserialize(tr_variant const& src, tr_verify_added_mode* tgt);
};

// ---

/**
 * Helpers for converting structured types to/from a `tr_variant`.
 *
 * Types opt in by declaring a `fields` tuple, typically:
 * `static constexpr auto fields = std::tuple{ Field<&T::member>{key}, ... }`.
 */

/**
 * Describes a single serializable field in a struct.
 *
 * @tparam MemberPtr Pointer-to-member, e.g. `&MyStruct::my_field`
 * @tparam Key       Key type used for lookup in the variant map (default: tr_quark)
 *
 * Example:
 *   Field<&Settings::port>{ TR_KEY_peer_port }
 */
template<auto MemberPtr, typename Key = tr_quark>
struct Field;

template<typename Owner, typename T, T Owner::* MemberPtr, typename Key>
struct Field<MemberPtr, Key>
{
    using owner_type = Owner;
    using value_type = T;
    using key_type = Key;
    static constexpr auto MemberPointer = MemberPtr;

    Key const key;

    explicit constexpr Field(Key key_in) noexcept
        : key{ std::move(key_in) }
    {
    }

    template<typename Derived>
    void load(Derived* derived, tr_variant::Map const& map) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        if (auto const iter = map.find(key); iter != std::end(map))
        {
            (void)to_value(iter->second, &(static_cast<Owner*>(derived)->*MemberPtr));
        }
    }

    template<typename Derived>
    void save(Derived const* derived, tr_variant::Map& map) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        map.try_emplace(key, to_variant(static_cast<Owner const*>(derived)->*MemberPtr));
    }
};

namespace detail
{

// NOLINTBEGIN(readability-identifier-naming)
// use std-style naming for these traits

template<typename T>
inline constexpr bool is_field_v = false;

template<auto MemberPtr, typename Key>
inline constexpr bool is_field_v<Field<MemberPtr, Key>> = true;

template<typename Tuple, std::size_t... I>
consteval bool is_fields_tuple(std::index_sequence<I...> /*indices*/)
{
    return (is_field_v<std::tuple_element_t<I, Tuple>> && ...);
}

template<typename Tuple>
inline constexpr bool is_fields_tuple_v = is_fields_tuple<Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>{});

// NOLINTEND(readability-identifier-naming)

template<typename S, typename T, tr_quark Key, std::size_t... I>
consteval std::size_t field_index(std::index_sequence<I...> /*indices*/)
{
    using Fields = std::remove_cvref_t<decltype(S::Fields)>;
    constexpr auto Count = std::tuple_size_v<Fields>;
    auto idx = Count;

    ((std::get<I>(S::Fields).key == Key && std::is_same_v<T, typename std::tuple_element_t<I, Fields>::value_type> ? idx = I :
                                                                                                                     0),
     ...);

    return idx;
}

} // namespace detail

template<typename T>
concept Serializable = requires {
    { std::tuple_size_v<std::remove_cvref_t<decltype(T::Fields)>> } -> std::convertible_to<std::size_t>;
} && detail::is_fields_tuple_v<std::remove_cvref_t<decltype(T::Fields)>>;

template<Serializable S>
[[nodiscard]] std::optional<tr_variant> to_variant(S const& src, tr_quark key);

template<typename T, Serializable S>
[[nodiscard]] std::optional<T> get(S const& src, tr_quark key)
{
    if (auto value = to_variant(src, key))
    {
        return to_value<T>(*value);
    }

    return std::nullopt;
}

template<typename T, tr_quark Key, Serializable S>
[[nodiscard]] constexpr T get(S const& src)
{
    using Fields = std::remove_cvref_t<decltype(S::Fields)>;
    constexpr auto Count = std::tuple_size_v<Fields>;
    constexpr auto Index = detail::field_index<S, T, Key>(std::make_index_sequence<Count>{});

    static_assert(Index < Count, "No field with matching tr_quark and value type in Serializable::Fields");

    using FieldType = std::tuple_element_t<Index, Fields>;
    static_assert(std::is_base_of_v<typename FieldType::owner_type, S>);

    return src.*(FieldType::MemberPointer);
}

template<typename T, tr_quark Key, Serializable S>
constexpr bool set(S& tgt, T val)
{
    using Fields = std::remove_cvref_t<decltype(S::Fields)>;
    constexpr auto Count = std::tuple_size_v<Fields>;
    constexpr auto Index = detail::field_index<S, T, Key>(std::make_index_sequence<Count>{});

    static_assert(Index < Count, "No field with matching tr_quark and value type in Serializable::Fields");

    using FieldType = std::tuple_element_t<Index, Fields>;
    static_assert(std::is_base_of_v<typename FieldType::owner_type, S>);

    auto& target = tgt.*(FieldType::MemberPointer);
    if (target == val)
    {
        return false;
    }

    target = std::move(val);
    return true;
}

template<Serializable S>
[[nodiscard]] std::optional<tr_variant> to_variant(S const& src, tr_quark key)
{
    auto result = std::optional<tr_variant>{};
    auto try_field = [&](auto const& field)
    {
        if (result || field.key != key)
        {
            return;
        }

        auto map = tr_variant::Map{ 1U };
        field.save(&src, map);

        if (auto const iter = map.find(key); iter != std::end(map))
        {
            result = iter->second.clone();
        }
    };

    std::apply([&](auto const&... field) { (try_field(field), ...); }, S::Fields);
    return result;
}

template<Serializable S>
bool set_from_variant(S& tgt, tr_quark key, tr_variant const& value)
{
    auto found = false;
    auto changed = false;
    auto try_field = [&](auto const& field)
    {
        if (found || field.key != key)
        {
            return;
        }

        found = true;

        using FieldType = std::remove_cvref_t<decltype(field)>;
        if (auto value_cast = to_value<typename FieldType::value_type>(value))
        {
            auto& target = tgt.*(FieldType::MemberPointer);
            if (detail::values_differ(target, *value_cast))
            {
                target = std::move(*value_cast);
                changed = true;
            }
        }
    };

    std::apply([&](auto const&... field) { (try_field(field), ...); }, S::Fields);
    return changed;
}

template<typename T, Serializable S>
bool set(S& tgt, tr_quark key, T val)
{
    auto found = false;
    auto type_ok = true;
    auto changed = false;

    auto try_field = [&](auto const& field)
    {
        if (found || field.key != key)
        {
            return;
        }

        found = true;
        using FieldType = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::is_same_v<T, typename FieldType::value_type>)
        {
            auto& target = tgt.*(FieldType::MemberPointer);
            if (detail::values_differ(target, val))
            {
                target = std::move(val);
                changed = true;
            }
            else
            {
                changed = false;
            }
        }
        else
        {
            type_ok = false;
        }
    };

    std::apply([&](auto const&... field) { (try_field(field), ...); }, S::Fields);
    return found && type_ok && changed;
}

/**
 * Load fields from a variant map into a target object.
 * Missing keys are silently ignored (fields retain their existing values).
 * If `src` is not a map, this is a no-op.
 *
 * @param tgt    The object to populate
 * @param fields A tuple of Field<> descriptors
 * @param src    The source variant (expected to be a Map)
 */
template<typename T, typename Fields>
void load(T& tgt, Fields const& fields, tr_variant::Map const& src)
{
    std::apply([&tgt, &src](auto const&... field) { (field.load(&tgt, src), ...); }, fields);
}

template<typename T, typename Fields>
void load(T& tgt, Fields const& fields, tr_variant const& src)
{
    if (auto const* map = src.get_if<tr_variant::Map>())
    {
        load(tgt, fields, *map);
    }
}

/**
 * Save an object's fields to a variant map.
 *
 * @param src    The object to serialize
 * @param fields A tuple of Field<> descriptors
 * @return       A tr_variant::Map containing the serialized fields
 */
template<typename T, typename Fields>
[[nodiscard]] tr_variant::Map save(T const& src, Fields const& fields)
{
    auto map = tr_variant::Map{ std::tuple_size_v<std::remove_cvref_t<Fields>> };
    std::apply([&src, &map](auto const&... field) { (field.save(&src, map), ...); }, fields);
    return map;
}

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
    for (auto const& elem : src)
    {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_push_back_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    auto tmp = C{};
    reserve_if_possible(tmp, std::size(*vec));

    for (auto const& elem : *vec)
    {
        typename C::value_type value{};
        if (!to_value(elem, &value))
        {
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
    for (auto const& elem : src)
    {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_insert_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    auto tmp = C{};

    for (auto const& elem : *vec)
    {
        typename C::value_type value{};
        if (!to_value(elem, &value))
        {
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
    for (auto const& elem : src)
    {
        ret.emplace_back(to_variant(elem));
    }
    return ret;
}

template<typename C>
bool to_array(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    if (std::size(*vec) != std::size(*ptgt))
    {
        return false; // Array size mismatch
    }

    auto tmp = C{};
    for (std::size_t i = 0; i < std::size(*vec); ++i)
    {
        if (!to_value((*vec)[i], &tmp[i]))
        {
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
    if (src.index() == tr_variant::NullIndex)
    {
        ptgt->reset();
        return true;
    }

    if (auto val = to_value<T>(src))
    {
        *ptgt = std::move(val);
        return true;
    }

    return false;
}

} // namespace detail
} // namespace tr::serializer
