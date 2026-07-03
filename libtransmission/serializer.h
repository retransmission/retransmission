// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <array>
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

/**
 * Compile-time check that no serialization key is shared between the
 * given Serializable types' `Fields`. Example:
 * static_assert(has_unique_keys<SessionSettings, RpcServerSettings>());
 */
template<Serializable... S>
[[nodiscard]] constexpr bool has_unique_keys()
{
    constexpr auto Total = (std::size_t{ 0 } + ... + std::tuple_size_v<std::remove_cvref_t<decltype(S::Fields)>>);

    auto keys = std::array<tr_quark, Total>{};
    auto idx = std::size_t{ 0 };
    auto append_keys = [&keys, &idx](auto const& fields) {
        std::apply([&keys, &idx](auto const&... field) { ((keys[idx++] = field.key), ...); }, fields);
    };
    (append_keys(S::Fields), ...);

    std::ranges::sort(keys);
    return std::ranges::adjacent_find(keys) == std::end(keys);
}

/**
 * Returns `true` iff any of the given Serializables has a field with `key`.
 * Example: has_key(TR_KEY_peer_port, session_settings, rpc_server_settings)
 */
template<Serializable... S>
[[nodiscard]] bool has_key(tr_quark key, S const&... /*objs*/)
{
    auto fields_contain = [key](auto const& fields) {
        auto found = false;
        std::apply([key, &found](auto const&... field) { ((found = found || field.key == key), ...); }, fields);
        return found;
    };
    return (fields_contain(S::Fields) || ...);
}

/**
 * Compile-time check whether any of the given Serializable types has a
 * field with `key`. Takes the Serializables as template arguments so it
 * can be used in constant expressions. Example:
 * static_assert(has_key<SessionSettings, RpcServerSettings>(TR_KEY_utp_enabled));
 */
template<Serializable... S>
[[nodiscard]] constexpr bool has_key(tr_quark key)
{
    auto fields_contain = [key](auto const& fields) {
        auto found = false;
        std::apply([key, &found](auto const&... field) { ((found = found || field.key == key), ...); }, fields);
        return found;
    };
    return (fields_contain(S::Fields) || ...);
}

namespace detail
{

// --- Group B workers: operate on a single Serializable, keyed by `tr_quark` ---

template<Serializable S>
[[nodiscard]] std::optional<tr_variant> to_variant_one(S const& src, tr_quark key)
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

// returns `true` iff `key` names a field in `S`; sets `changed` iff its value changed
template<Serializable S>
bool set_from_variant_one(S& tgt, tr_quark key, tr_variant const& value, bool& changed)
{
    auto matched = false;
    auto try_field = [&](auto const& field) {
        if (field.key != key) {
            return false;
        }

        matched = true;
        using FieldType = std::remove_cvref_t<decltype(field)>;
        changed = set(tgt.*FieldType::MemberPointer, value);
        return true;
    };

    std::apply([&](auto const&... field) { (try_field(field) || ...); }, S::Fields);
    return matched;
}

// --- Group A workers: operate on a coupled (object, Fields) pair ---

template<typename T, typename Fields>
void save_one(T const& src, Fields const& fields, tr_variant::Map& map)
{
    std::apply([&src, &map](auto const&... field) { (field.save(&src, map), ...); }, fields);
}

template<typename T, typename Fields, typename ChangedKeys>
void load_one(T& tgt, Fields const& fields, tr_variant::Map const& src, ChangedKeys& changed_keys)
{
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
}

// Sum of `tuple_size` over the `Fields` (odd-indexed) elements of a flattened
// pack of (object, Fields) pairs.
template<typename ArgTuple, std::size_t... PairIndex>
[[nodiscard]] consteval std::size_t pair_fields_capacity(std::index_sequence<PairIndex...> /*pairs*/)
{
    return (
        std::size_t{ 0 } + ... + std::tuple_size_v<std::remove_cvref_t<std::tuple_element_t<(2 * PairIndex) + 1, ArgTuple>>>);
}

} // namespace detail

/**
 * Get the value of `key` as a variant, searching one or more Serializables in
 * order. The first object whose `Fields` contains `key` wins; use
 * `has_unique_keys()` to guarantee at most one owner.
 */
template<Serializable S, Serializable... Ss>
[[nodiscard]] std::optional<tr_variant> to_variant(tr_quark const key, S const& src, Ss const&... srcs)
{
    auto result = detail::to_variant_one(src, key);
    if constexpr (sizeof...(Ss) != 0U) {
        auto try_next = [&result, key](auto const& obj) {
            if (!result) {
                result = detail::to_variant_one(obj, key);
            }
        };
        (try_next(srcs), ...);
    }
    return result;
}

template<typename T, Serializable S, Serializable... Ss>
[[nodiscard]] std::optional<T> get(tr_quark const key, S const& src, Ss const&... srcs)
{
    if (auto value = to_variant(key, src, srcs...)) {
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

/**
 * Apply a variant `value` to `key` in whichever of the given Serializables owns
 * it. Returns `true` iff a matching field was found and its value changed.
 */
template<Serializable S, Serializable... Ss>
bool set_from_variant(tr_quark const key, tr_variant const& value, S& tgt, Ss&... tgts)
{
    auto changed = false;
    auto matched = false;
    auto try_next = [&](auto& obj) {
        if (!matched) {
            auto obj_changed = false;
            if (detail::set_from_variant_one(obj, key, value, obj_changed)) {
                matched = true;
                changed = obj_changed;
            }
        }
    };
    try_next(tgt);
    (try_next(tgts), ...);
    return changed;
}

/**
 * Set `key` to `val` in whichever of the given Serializables owns it.
 *
 * `val` is routed through a `tr_variant`, so any type with a registered
 * `Converter` (or a supported container) can target a field of a different but
 * compatible type -- e.g. `QString`/`Glib::ustring` -> `std::string`. A value
 * type with no converter is a compile-time error (see `to_variant`), never a
 * silent no-op. Returns `true` iff a matching field was found and changed.
 */
template<typename T, Serializable S, Serializable... Ss>
bool set(tr_quark const key, T const& val, S& tgt, Ss&... tgts)
{
    return set_from_variant(key, to_variant(val), tgt, tgts...);
}

/**
 * Load a source variant map into one or more coupled (target, Fields) pairs.
 * Missing keys are silently ignored (fields retain their existing values).
 *
 * @param src    The source variant map
 * @param args   One or more (target object, Fields tuple) pairs
 * @return       sorted keys of fields that changed, across all pairs
 *
 * Example: load(src, session_settings, SessionSettings::Fields, rpc, RpcServerSettings::Fields)
 */
template<typename... Args>
auto load(tr_variant::Map const& src, Args&&... args)
{
    static_assert(
        sizeof...(Args) != 0U && sizeof...(Args) % 2U == 0U,
        "load() expects a source followed by one or more (target, Fields) pairs");

    constexpr auto PairCount = sizeof...(Args) / 2U;
    auto arg_tuple = std::forward_as_tuple(std::forward<Args>(args)...);
    using ArgTuple = std::remove_cvref_t<decltype(arg_tuple)>;
    constexpr auto Capacity = detail::pair_fields_capacity<ArgTuple>(std::make_index_sequence<PairCount>{});

    auto changed_keys = small::vector<tr_quark, Capacity>{};
    [&]<std::size_t... PairIndex>(std::index_sequence<PairIndex...> /*pairs*/) {
        (detail::load_one(std::get<2U * PairIndex>(arg_tuple), std::get<(2U * PairIndex) + 1U>(arg_tuple), src, changed_keys),
         ...);
    }(std::make_index_sequence<PairCount>{});

    std::ranges::sort(changed_keys);
    return changed_keys;
}

template<typename... Args>
auto load(tr_variant const& src, Args&&... args)
{
    static_assert(
        sizeof...(Args) != 0U && sizeof...(Args) % 2U == 0U,
        "load() expects a source followed by one or more (target, Fields) pairs");

    constexpr auto PairCount = sizeof...(Args) / 2U;
    using ArgTuple = std::tuple<Args&&...>;
    constexpr auto Capacity = detail::pair_fields_capacity<ArgTuple>(std::make_index_sequence<PairCount>{});

    if (auto const* const map = src.get_if<tr_variant::Map>()) {
        return load(*map, std::forward<Args>(args)...);
    }

    return small::vector<tr_quark, Capacity>{};
}

/**
 * Load a source into one or more Serializables, inferring each target's
 * `Fields`. Delegates to the (target, Fields)-pair overload above.
 *
 * Example: load(src, session_settings, alt_speed_settings, rpc_server_settings)
 */
template<Serializable S, Serializable... Ss>
auto load(tr_variant::Map const& src, S& tgt, Ss&... tgts)
{
    static_assert(has_unique_keys<S, Ss...>(), "load() requires the objects to have globally unique keys");
    return std::apply(
        [&src](auto&&... args) { return load(src, args...); },
        std::tuple_cat(std::forward_as_tuple(tgt, S::Fields), std::forward_as_tuple(tgts, Ss::Fields)...));
}

template<Serializable S, Serializable... Ss>
auto load(tr_variant const& src, S& tgt, Ss&... tgts)
{
    static_assert(has_unique_keys<S, Ss...>(), "load() requires the objects to have globally unique keys");
    return std::apply(
        [&src](auto&&... args) { return load(src, args...); },
        std::tuple_cat(std::forward_as_tuple(tgt, S::Fields), std::forward_as_tuple(tgts, Ss::Fields)...));
}

/**
 * Save one or more coupled (object, Fields) pairs into a single variant map.
 *
 * @param args One or more (source object, Fields tuple) pairs
 * @return     A tr_variant::Map containing the serialized fields of every pair
 *
 * Example: save(session_settings, SessionSettings::Fields, rpc, RpcServerSettings::Fields)
 */
template<typename... Args>
[[nodiscard]] tr_variant::Map save(Args const&... args)
{
    static_assert(sizeof...(Args) != 0U && sizeof...(Args) % 2U == 0U, "save() expects one or more (object, Fields) pairs");

    constexpr auto PairCount = sizeof...(Args) / 2U;
    auto const arg_tuple = std::forward_as_tuple(args...);
    using ArgTuple = std::remove_cvref_t<decltype(arg_tuple)>;

    auto map = tr_variant::Map{ detail::pair_fields_capacity<ArgTuple>(std::make_index_sequence<PairCount>{}) };
    [&]<std::size_t... PairIndex>(std::index_sequence<PairIndex...> /*pairs*/) {
        (detail::save_one(std::get<2U * PairIndex>(arg_tuple), std::get<(2U * PairIndex) + 1U>(arg_tuple), map), ...);
    }(std::make_index_sequence<PairCount>{});
    return map;
}

/**
 * Save one or more Serializables into a single variant map, inferring each
 * object's `Fields`. Delegates to the (object, Fields)-pair overload above.
 *
 * Example: save(session_settings, alt_speed_settings, rpc_server_settings)
 */
template<Serializable S, Serializable... Ss>
[[nodiscard]] tr_variant::Map save(S const& obj, Ss const&... objs)
{
    static_assert(has_unique_keys<S, Ss...>(), "save() requires the objects to have globally unique keys");
    return std::apply(
        [](auto const&... args) { return save(args...); },
        std::tuple_cat(std::forward_as_tuple(obj, S::Fields), std::forward_as_tuple(objs, Ss::Fields)...));
}

} // namespace tr::serializer
