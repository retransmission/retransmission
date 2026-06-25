// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <small/vector.hpp>

#include "libtransmission/converters.h"
#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace tr::serializer
{

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
struct Field<MemberPtr, Key> {
    using owner_type = Owner;
    using value_type = T;
    using key_type = Key;
    static constexpr auto MemberPointer = MemberPtr;

    Key const key;

    explicit constexpr Field(Key key_in) noexcept
        : key{ std::move(key_in) }
    {
    }

    // returns `true` iff the value was changed
    template<typename Derived>
    [[nodiscard]] bool load(Derived* derived, tr_variant::Map const& map) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        auto const iter = map.find(key);
        return iter != std::end(map) && set(static_cast<Owner*>(derived)->*MemberPtr, iter->second);
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
    if (auto value = to_variant(src, key)) {
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
    if (target == val) {
        return false;
    }

    target = std::move(val);
    return true;
}

template<Serializable S>
[[nodiscard]] std::optional<tr_variant> to_variant(S const& src, tr_quark key)
{
    auto result = std::optional<tr_variant>{};
    auto try_field = [&](auto const& field) {
        if (field.key != key) {
            return false;
        }

        auto map = tr_variant::Map{ 1U };
        field.save(&src, map);

        if (auto const iter = map.find(key); iter != std::end(map)) {
            result = iter->second.clone();
        }

        return true;
    };

    std::apply([&](auto const&... field) { (try_field(field) || ...); }, S::Fields);
    return result;
}

template<Serializable S>
bool set_from_variant(S& tgt, tr_quark key, tr_variant const& value)
{
    auto changed = false;
    auto try_field = [&](auto const& field) {
        if (field.key != key) {
            return false;
        }

        using FieldType = std::remove_cvref_t<decltype(field)>;
        changed = set(tgt.*(FieldType::MemberPointer), value);
        return true;
    };

    std::apply([&](auto const&... field) { (try_field(field) || ...); }, S::Fields);
    return changed;
}

template<typename T, Serializable S>
bool set(S& tgt, tr_quark key, T val)
{
    auto type_ok = true;
    auto changed = false;

    auto try_field = [&](auto const& field) {
        if (field.key != key) {
            return false;
        }

        using FieldType = std::remove_cvref_t<decltype(field)>;
        if constexpr (std::is_same_v<T, typename FieldType::value_type>) {
            changed = set(tgt.*(FieldType::MemberPointer), std::move(val));
        } else {
            type_ok = false;
        }

        return true;
    };

    std::apply([&](auto const&... field) { (try_field(field) || ...); }, S::Fields);
    return type_ok && changed;
}

/**
 * Load fields from a variant map into a target object.
 * Missing keys are silently ignored (fields retain their existing values).
 *
 * @param tgt The object to populate
 * @param fields A tuple of Field<> descriptors
 * @param src The source variant (expected to be a Map)
 * @return changed_keys sorted keys of fields that changed
 */
template<typename T, typename Fields>
auto load(T& tgt, Fields const& fields, tr_variant::Map const& src)
{
    auto changed_keys = small::vector<tr_quark, std::tuple_size_v<Fields>>{};
    std::apply(
        [&tgt, &src, &changed_keys](auto const&... field) {
            (
                [&] {
                    if (field.load(&tgt, src)) {
                        changed_keys.emplace_back(field.key);
                    }
                }(),
                ...);
        },
        fields);
    std::ranges::sort(changed_keys);
    return changed_keys;
}

template<typename T, typename Fields>
auto load(T& tgt, Fields const& fields, tr_variant const& src)
{
    if (auto const* const map = src.get_if<tr_variant::Map>()) {
        return load(tgt, fields, *map);
    }

    return small::vector<tr_quark, std::tuple_size_v<Fields>>{};
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

} // namespace tr::serializer
