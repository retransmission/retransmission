// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cmath>
#include <climits>
#include <cstdint>
#include <list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <libtransmission/net.h>
#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using SerializerTest = ::tr::test::TransmissionTest;
using namespace std::literals;
using tr::serializer::to_value;
using tr::serializer::to_variant;

namespace
{

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(Rect const& that) const noexcept
    {
        return x == that.x && y == that.y && width == that.width && height == that.height;
    }
};

} // namespace

// Custom `Converter<Rect>` specialization — illustrates how downstream code
// would add a converter for its own type.
// `to_variant`/`to_value` names are dictated by the Converter<T> interface,
// so suppress the ClassMethodCase check from .clang-tidy.
template<>
struct tr::serializer::Converter<Rect> {
    // NOLINTNEXTLINE(readability-identifier-naming)
    static tr_variant to_variant(Rect const& r)
    {
        auto v = tr_variant::Vector{};
        v.reserve(4U);
        v.emplace_back(int64_t{ r.x });
        v.emplace_back(int64_t{ r.y });
        v.emplace_back(int64_t{ r.width });
        v.emplace_back(int64_t{ r.height });
        return v;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    static bool to_value(tr_variant const& src, Rect* tgt)
    {
        auto const* const v = src.get_if<tr_variant::Vector>();
        if (v == nullptr || std::size(*v) != 4U) {
            return false;
        }

        auto const x = (*v)[0].value_if<int64_t>();
        auto const y = (*v)[1].value_if<int64_t>();
        auto const w = (*v)[2].value_if<int64_t>();
        auto const h = (*v)[3].value_if<int64_t>();

        if (!x || !y || !w || !h) {
            return false;
        }

        *tgt = Rect{
            .x = static_cast<int>(*x),
            .y = static_cast<int>(*y),
            .width = static_cast<int>(*w),
            .height = static_cast<int>(*h),
        };
        return true;
    }
};

namespace
{

TEST_F(SerializerTest, usesBuiltins)
{
    {
        auto const var = to_variant(true);
        EXPECT_TRUE(var.holds_alternative<bool>());

        auto out = false;
        EXPECT_TRUE(to_value(var, &out));
        EXPECT_EQ(out, true);
    }

    {
        auto const var = to_variant(3.5);
        EXPECT_TRUE(var.holds_alternative<double>());

        auto out = 0.0;
        EXPECT_TRUE(to_value(var, &out));
        EXPECT_DOUBLE_EQ(out, 3.5);
    }

    {
        auto const s = "hello"s;
        auto const var = to_variant(s);
        EXPECT_TRUE(var.holds_alternative<std::string_view>());
        EXPECT_EQ(var.value_if<std::string_view>().value_or(""sv), "hello"sv);

        auto out = std::string{};
        EXPECT_TRUE(to_value(var, &out));
        EXPECT_EQ(out, s);
    }

    {
        auto const s = std::optional<std::string>{ "opt"s };
        auto const var = to_variant(s);
        EXPECT_TRUE(var.holds_alternative<std::string_view>());
        EXPECT_EQ(var.value_if<std::string_view>().value_or(""sv), "opt"sv);

        auto out = std::optional<std::string>{};
        EXPECT_TRUE(to_value(var, &out));
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(*out, *s);
    }

    {
        auto const s = std::optional<std::string>{};
        auto const var = to_variant(s);
        EXPECT_TRUE(var.holds_alternative<std::nullptr_t>());

        auto out = std::optional<std::string>{ "will reset"s };
        EXPECT_TRUE(to_value(var, &out));
        EXPECT_FALSE(out.has_value());
    }

    {
        auto const expected = uint64_t{ 12345678901234ULL };
        auto const var = to_variant(expected);
        EXPECT_TRUE(var.holds_alternative<int64_t>());

        auto out = uint64_t{};
        EXPECT_TRUE(to_value(var, &out));
        EXPECT_EQ(out, expected);
    }
}

TEST_F(SerializerTest, usesTimeT)
{
    static auto constexpr Expected = time_t{ 1774486600 };
    auto const var = to_variant(Expected);
    EXPECT_TRUE(var.holds_alternative<int64_t>());
    EXPECT_EQ(var.value_if<time_t>(), Expected);

    auto actual = time_t{};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, Expected);
}

TEST_F(SerializerTest, usesTrPex)
{
    static auto constexpr CompactIp = std::array{ '\x7F', '\0', '\0', '\1', '\x73', '\x1A' }; // 127.0.0.1:6771
    static_assert(CompactIp.size() == tr_socket_address::CompactSockAddrBytes[TR_AF_INET]);

    auto const expected_flags = static_cast<uint8_t>(tr_rand_int(0x100U));
    auto const expected_sockaddr = tr_socket_address::from_compact_ipv4(reinterpret_cast<std::byte const*>(CompactIp.data()))
                                       .first;
    auto const var = to_variant(tr_pex{ expected_sockaddr, expected_flags });

    auto* const map = var.get_if<tr_variant::Map>();
    ASSERT_NE(map, nullptr);
    auto const compact_ip = map->value_if<std::string_view>(TR_KEY_socket_address);
    ASSERT_TRUE(compact_ip);
    EXPECT_EQ(
        std::lexicographical_compare_three_way(compact_ip->begin(), compact_ip->end(), CompactIp.begin(), CompactIp.end()),
        std::strong_ordering::equivalent);
    EXPECT_EQ(map->value_if<int64_t>(TR_KEY_flags), expected_flags);

    auto actual = tr_pex{};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual.socket_address, expected_sockaddr);
    EXPECT_EQ(actual.flags, expected_flags);
}

TEST_F(SerializerTest, usesCustomTypes)
{
    static constexpr Rect Expected{ .x = 10, .y = 20, .width = 640, .height = 480 };
    auto const var = to_variant(Expected);

    auto actual = Rect{};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, Expected);
}

TEST_F(SerializerTest, usesLists)
{
    auto const expected = std::list<std::string>{ "apple", "ball", "cat" };
    auto const var = to_variant(expected);

    auto const* const l = var.get_if<tr_variant::Vector>();
    ASSERT_NE(l, nullptr);
    ASSERT_EQ(std::size(*l), 3U);
    EXPECT_EQ((*l)[0].value_if<std::string_view>().value_or(""sv), "apple"sv);
    EXPECT_EQ((*l)[1].value_if<std::string_view>().value_or(""sv), "ball"sv);
    EXPECT_EQ((*l)[2].value_if<std::string_view>().value_or(""sv), "cat"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesVectors)
{
    auto const expected = std::vector<std::string>{ "apple", "ball", "cat" };
    auto const var = to_variant(expected);

    auto const* const l = var.get_if<tr_variant::Vector>();
    ASSERT_NE(l, nullptr);
    ASSERT_EQ(std::size(*l), 3U);
    EXPECT_EQ((*l)[0].value_if<std::string_view>().value_or(""sv), "apple"sv);
    EXPECT_EQ((*l)[1].value_if<std::string_view>().value_or(""sv), "ball"sv);
    EXPECT_EQ((*l)[2].value_if<std::string_view>().value_or(""sv), "cat"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesVectorsOfCustom)
{
    auto const expected = std::vector<Rect>{
        { .x = 1, .y = 2, .width = 3, .height = 4 },
        { .x = 10, .y = 20, .width = 640, .height = 480 },
    };
    auto const var = to_variant(expected);

    auto actual = decltype(expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesNestedVectors)
{
    auto const expected = std::vector<std::vector<std::string>>{ { "a", "b" }, { "c" } };
    auto const var = to_variant(expected);

    auto const* const outer = var.get_if<tr_variant::Vector>();
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(std::size(*outer), 2U);

    auto const* const inner0 = (*outer)[0].get_if<tr_variant::Vector>();
    ASSERT_NE(inner0, nullptr);
    ASSERT_EQ(std::size(*inner0), 2U);
    EXPECT_EQ((*inner0)[0].value_if<std::string_view>().value_or(""sv), "a"sv);
    EXPECT_EQ((*inner0)[1].value_if<std::string_view>().value_or(""sv), "b"sv);

    auto const* const inner1 = (*outer)[1].get_if<tr_variant::Vector>();
    ASSERT_NE(inner1, nullptr);
    ASSERT_EQ(std::size(*inner1), 1U);
    EXPECT_EQ((*inner1)[0].value_if<std::string_view>().value_or(""sv), "c"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, vectorRejectsWrongType)
{
    auto const var = tr_variant{ true };
    auto out = std::vector<std::string>{ "keep" };
    EXPECT_FALSE(to_value(var, &out));
    EXPECT_EQ(out, (std::vector<std::string>{ "keep" }));
}

TEST_F(SerializerTest, vectorIsNondestructiveOnPartialFailure)
{
    auto list = tr_variant::Vector{};
    list.reserve(3U);
    list.emplace_back("ok"sv);
    list.emplace_back(nullptr);
    list.emplace_back("ok"sv);

    auto const var = tr_variant{ std::move(list) };
    auto out = std::vector<std::string>{ "keep" };
    EXPECT_FALSE(to_value(var, &out));
    EXPECT_EQ(out, (std::vector<std::string>{ "keep" }));
}

TEST_F(SerializerTest, usesOptional)
{
    auto const expected = std::optional{ "apple"s };
    auto const var = to_variant(expected);

    auto const sv = var.value_if<std::string_view>();
    ASSERT_EQ(sv, "apple"sv);

    auto actual = decltype(expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesNullOptional)
{
    auto const expected = std::optional<std::string>{};
    auto const var = to_variant(expected);

    auto const sv = var.value_if<std::string_view>();
    ASSERT_FALSE(sv);

    auto actual = decltype(expected){ "discard"s };
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, usesOptionalOfCustom)
{
    constexpr auto Expected = std::optional{ Rect{ .x = 1, .y = 2, .width = 3, .height = 4 } };
    auto const var = to_variant(Expected);

    auto actual = decltype(Expected){};
    EXPECT_TRUE(to_value(var, &actual));
    EXPECT_EQ(actual, Expected);
}

TEST_F(SerializerTest, optionalRejectsWrongType)
{
    auto const var = tr_variant{ true };
    auto out = std::optional{ "keep"s };
    EXPECT_FALSE(to_value(var, &out));
    EXPECT_EQ(out, "keep"s);
}

TEST_F(SerializerTest, mapKeyOutParamWritesValue)
{
    auto map = tr_variant::Map{};
    map.insert_or_assign(TR_KEY_seed_ratio_limit, to_variant(2.5));
    map.insert_or_assign(TR_KEY_address, to_variant("localhost"s));

    auto ratio = 0.0;
    EXPECT_TRUE(to_value(map, TR_KEY_seed_ratio_limit, &ratio));
    EXPECT_DOUBLE_EQ(ratio, 2.5);

    auto address = std::string{};
    EXPECT_TRUE(to_value(map, TR_KEY_address, &address));
    EXPECT_EQ(address, "localhost"s);
}

TEST_F(SerializerTest, mapKeyOutParamMissingKeyReturnsFalse)
{
    auto const map = tr_variant::Map{};

    auto ratio = 9.0;
    EXPECT_FALSE(to_value(map, TR_KEY_seed_ratio_limit, &ratio));
    EXPECT_DOUBLE_EQ(ratio, 9.0);
}

TEST_F(SerializerTest, mapKeyOutParamWrongTypeReturnsFalse)
{
    auto map = tr_variant::Map{};
    map.insert_or_assign(TR_KEY_dht_enabled, to_variant(true));

    auto out = std::string{ "keep" };
    EXPECT_FALSE(to_value(map, TR_KEY_dht_enabled, &out));
    EXPECT_EQ(out, "keep"s);
}

TEST_F(SerializerTest, mapKeyOptionalReturnsValue)
{
    auto map = tr_variant::Map{};
    map.insert_or_assign(TR_KEY_seed_ratio_limit, to_variant(2.5));

    auto const ratio = to_value<double>(map, TR_KEY_seed_ratio_limit);
    ASSERT_TRUE(ratio.has_value());
    EXPECT_DOUBLE_EQ(*ratio, 2.5);
}

TEST_F(SerializerTest, mapKeyOptionalMissingKeyReturnsNullopt)
{
    auto const map = tr_variant::Map{};

    EXPECT_FALSE(to_value<double>(map, TR_KEY_seed_ratio_limit).has_value());
}

TEST_F(SerializerTest, mapKeyOptionalWrongTypeReturnsNullopt)
{
    auto map = tr_variant::Map{};
    map.insert_or_assign(TR_KEY_dht_enabled, to_variant(true));

    EXPECT_FALSE(to_value<std::string>(map, TR_KEY_dht_enabled).has_value());
}

// ---

using tr::serializer::Field;
using tr::serializer::get;
using tr::serializer::load;
using tr::serializer::save;
using tr::serializer::set;
using tr::serializer::set_from_variant;

struct Endpoint {
    std::string address;
    tr_port port;

    static constexpr auto Fields = std::tuple{
        Field<&Endpoint::address>{ TR_KEY_address },
        Field<&Endpoint::port>{ TR_KEY_port },
    };

    [[nodiscard]] bool operator==(Endpoint const& that) const noexcept
    {
        return address == that.address && port == that.port;
    }

    // C++17 requires explicit operator!=; C++20 would auto-generate from operator==
    [[nodiscard]] bool operator!=(Endpoint const& that) const noexcept
    {
        return !(*this == that);
    }
};

struct Simple {
    int blocks = 0;
    bool enabled = false;

    static constexpr auto Fields = std::tuple{
        Field<&Simple::blocks>{ TR_KEY_blocks },
        Field<&Simple::enabled>{ TR_KEY_dht_enabled },
    };
};

struct Floating {
    double ratio = 0.0;

    static constexpr auto Fields = std::tuple{
        Field<&Floating::ratio>{ TR_KEY_seed_ratio_limit },
    };
};

TEST_F(SerializerTest, fieldSaveLoad)
{
    auto const expected = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };

    // Save to variant
    auto const expected_json = fmt::format(R"({{"address":"localhost","port":{}}})", TrDefaultPeerPort);
    auto const expected_json_sv = std::string_view{ expected_json };
    auto const var = tr_variant{ save(expected, Endpoint::Fields) };
    EXPECT_EQ(expected_json_sv, tr_variant_serde::json().compact().to_string(var));

    // Load back into a new instance
    auto actual = Endpoint{};
    EXPECT_NE(actual, expected);
    load(var, actual, Endpoint::Fields);
    EXPECT_EQ(actual, expected);
}

TEST_F(SerializerTest, fieldLoadIgnoresMissingKeys)
{
    auto endpoint = Endpoint{ .address = "default", .port = tr_port::from_host(9999) };
    auto const original = endpoint;

    load(tr_variant::make_map(), endpoint, Endpoint::Fields);

    // Should remain unchanged
    EXPECT_EQ(original, endpoint);
}

TEST_F(SerializerTest, fieldLoadIgnoresNonMap)
{
    auto endpoint = Endpoint{ .address = "default", .port = tr_port::from_host(9999) };
    auto const original = endpoint;

    load(tr_variant{ 42 }, endpoint, Endpoint::Fields);

    // Should remain unchanged
    EXPECT_EQ(original, endpoint);
}

TEST_F(SerializerTest, serializableGetByKey)
{
    auto const endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };

    auto const addr = get<std::string>(TR_KEY_address, endpoint);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(*addr, "localhost");

    auto const port = get<tr_port>(TR_KEY_port, endpoint);
    ASSERT_TRUE(port.has_value());
    EXPECT_EQ(*port, tr_port::from_host(TrDefaultPeerPort));

    auto const missing = get<std::string>(TR_KEY_comment, endpoint);
    EXPECT_FALSE(missing.has_value());

    auto const wrong_type = get<int>(TR_KEY_address, endpoint);
    EXPECT_FALSE(wrong_type.has_value());
}

TEST_F(SerializerTest, serializableSetByKey)
{
    auto endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };

    EXPECT_TRUE(set(TR_KEY_address, std::string{ "example.com" }, endpoint));
    EXPECT_EQ(endpoint.address, "example.com");

    EXPECT_FALSE(set(TR_KEY_address, std::string{ "example.com" }, endpoint));

    EXPECT_FALSE(set(TR_KEY_comment, std::string{ "no field" }, endpoint));

    EXPECT_FALSE(set(TR_KEY_address, 42, endpoint));

    EXPECT_TRUE(set(TR_KEY_port, tr_port::from_host(1234), endpoint));
    EXPECT_EQ(endpoint.port, tr_port::from_host(1234));
}

TEST_F(SerializerTest, boolToIntCoercionWarnsButSucceeds)
{
    // A boolean feeding an integer field is not a benc/json-expected
    // conversion. It still succeeds (legacy data must keep loading), but emits
    // a developer-facing stderr warning so the mismatch can be noticed.
    auto simple = Simple{ .blocks = 5, .enabled = false };

    testing::internal::CaptureStderr();
    EXPECT_TRUE(set(TR_KEY_blocks, true, simple)); // bool -> int: 5 -> 1
    auto const warning = testing::internal::GetCapturedStderr();
    EXPECT_EQ(1, simple.blocks);
    EXPECT_FALSE(warning.empty());

    // bool -> bool is expected: value applies with no warning.
    testing::internal::CaptureStderr();
    EXPECT_TRUE(set(TR_KEY_dht_enabled, true, simple));
    EXPECT_TRUE(testing::internal::GetCapturedStderr().empty());
    EXPECT_TRUE(simple.enabled);

    // ...and likewise at the scalar `to_value` boundary.
    testing::internal::CaptureStderr();
    auto const as_int = to_value<int>(to_variant(true));
    EXPECT_FALSE(testing::internal::GetCapturedStderr().empty());
    ASSERT_TRUE(as_int.has_value());
    EXPECT_EQ(1, *as_int);
}

TEST_F(SerializerTest, bencRoundTripPreservesBoolAndDouble)
{
    // benc has no boolean or floating-point token: bools serialize as i1e/i0e
    // and doubles as strings, then recover on read (integer->bool,
    // string->double). These are *expected* coercions, so the round trip must
    // preserve the values and emit no warning.
    auto const src_simple = Simple{ .blocks = 5, .enabled = true };
    auto const src_floating = Floating{ .ratio = 2.5 };

    auto const benc = tr_variant_serde::benc().to_string(
        tr_variant{ save(src_simple, Simple::Fields, src_floating, Floating::Fields) });

    auto serde = tr_variant_serde::benc();
    auto const parsed = serde.parse(benc);
    ASSERT_TRUE(parsed.has_value());

    auto dst_simple = Simple{};
    auto dst_floating = Floating{};
    testing::internal::CaptureStderr();
    load(*parsed, dst_simple, Simple::Fields, dst_floating, Floating::Fields);
    EXPECT_TRUE(testing::internal::GetCapturedStderr().empty());

    EXPECT_EQ(5, dst_simple.blocks);
    EXPECT_TRUE(dst_simple.enabled); // recovered from i1e (integer) -> bool
    EXPECT_DOUBLE_EQ(2.5, dst_floating.ratio); // recovered from string -> double
}

TEST_F(SerializerTest, serializableToVariantByKey)
{
    auto const endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };

    auto const address = to_variant(TR_KEY_address, endpoint);
    ASSERT_TRUE(address);
    EXPECT_EQ("localhost"sv, address->value_if<std::string_view>().value_or(""sv));

    auto const port = to_variant(TR_KEY_port, endpoint);
    ASSERT_TRUE(port);
    EXPECT_EQ(TrDefaultPeerPort, port->value_if<int64_t>().value_or(-1));

    EXPECT_FALSE(to_variant(TR_KEY_comment, endpoint));
}

TEST_F(SerializerTest, serializableSetFromVariantByKey)
{
    auto endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };

    EXPECT_TRUE(set_from_variant(TR_KEY_address, tr_variant{ "example.com"sv }, endpoint));
    EXPECT_EQ("example.com", endpoint.address);
    EXPECT_FALSE(set_from_variant(TR_KEY_address, tr_variant{ "example.com"sv }, endpoint));
    EXPECT_FALSE(set_from_variant(TR_KEY_address, tr_variant{ 123 }, endpoint));
    EXPECT_FALSE(set_from_variant(TR_KEY_comment, tr_variant{ "unused"sv }, endpoint));

    EXPECT_TRUE(set_from_variant(TR_KEY_port, tr_variant{ int64_t{ 1234 } }, endpoint));
    EXPECT_EQ(tr_port::from_host(1234), endpoint.port);
}

TEST_F(SerializerTest, serializableSetFromVariantUsesFloatingChangePolicy)
{
    auto floating = Floating{ .ratio = std::numeric_limits<double>::quiet_NaN() };

    EXPECT_TRUE(set_from_variant(TR_KEY_seed_ratio_limit, tr_variant{ std::numeric_limits<double>::quiet_NaN() }, floating));
    EXPECT_TRUE(std::isnan(floating.ratio));

    EXPECT_TRUE(set_from_variant(TR_KEY_seed_ratio_limit, tr_variant{ 2.5 }, floating));
    EXPECT_DOUBLE_EQ(2.5, floating.ratio);
    EXPECT_FALSE(set_from_variant(TR_KEY_seed_ratio_limit, tr_variant{ 2.5 }, floating));
}

TEST_F(SerializerTest, serializableConstexprGetSet)
{
    constexpr auto KBlocks = TR_KEY_blocks;
    constexpr auto KEnabled = TR_KEY_dht_enabled;

    constexpr auto CompileTimeGet = [] {
        auto s = Simple{ .blocks = 5, .enabled = true };
        return get<int, KBlocks>(s) == 5 && get<bool, KEnabled>(s);
    }();

    static_assert(CompileTimeGet);

    constexpr auto CompileTimeSet = [] {
        auto s = Simple{ .blocks = 1, .enabled = false };
        auto changed1 = set<int, KBlocks>(s, 2);
        auto changed2 = set<bool, KEnabled>(s, true);
        auto changed3 = set<int, KBlocks>(s, 2);
        return changed1 && changed2 && !changed3 && s.blocks == 2 && s.enabled;
    }();

    static_assert(CompileTimeSet);
}

// --- Group A: multiple coupled (object, Fields) pairs ---

static_assert(tr::serializer::has_unique_keys<Endpoint, Simple>());
static_assert(!tr::serializer::has_unique_keys<Endpoint, Endpoint>());

TEST_F(SerializerTest, multiPairSaveMergesIntoOneMap)
{
    auto const endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };
    auto const simple = Simple{ .blocks = 7, .enabled = true };

    auto const map = save(endpoint, Endpoint::Fields, simple, Simple::Fields);

    EXPECT_NE(std::end(map), map.find(TR_KEY_address));
    EXPECT_NE(std::end(map), map.find(TR_KEY_port));
    EXPECT_NE(std::end(map), map.find(TR_KEY_blocks));
    EXPECT_NE(std::end(map), map.find(TR_KEY_dht_enabled));
}

TEST_F(SerializerTest, multiPairLoadRoundTrip)
{
    auto const src_endpoint = Endpoint{ .address = "example.com", .port = tr_port::from_host(1234) };
    auto const src_simple = Simple{ .blocks = 9, .enabled = true };
    auto const map = save(src_endpoint, Endpoint::Fields, src_simple, Simple::Fields);

    auto dst_endpoint = Endpoint{};
    auto dst_simple = Simple{};
    auto const changed = load(map, dst_endpoint, Endpoint::Fields, dst_simple, Simple::Fields);

    EXPECT_EQ(src_endpoint, dst_endpoint);
    EXPECT_EQ(src_simple.blocks, dst_simple.blocks);
    EXPECT_EQ(src_simple.enabled, dst_simple.enabled);

    // changed_keys aggregates across both pairs and is sorted
    EXPECT_TRUE(std::ranges::is_sorted(changed));
    EXPECT_TRUE(std::ranges::binary_search(changed, TR_KEY_address));
    EXPECT_TRUE(std::ranges::binary_search(changed, TR_KEY_blocks));
}

TEST_F(SerializerTest, multiPairLoadIgnoresNonMap)
{
    auto endpoint = Endpoint{ .address = "keep", .port = tr_port::from_host(42) };
    auto simple = Simple{ .blocks = 3, .enabled = true };
    auto const original_endpoint = endpoint;

    auto const changed = load(tr_variant{ 7 }, endpoint, Endpoint::Fields, simple, Simple::Fields);

    EXPECT_TRUE(changed.empty());
    EXPECT_EQ(original_endpoint, endpoint);
    EXPECT_EQ(3, simple.blocks);
}

// --- Group B: one key across multiple Serializables ---

TEST_F(SerializerTest, multiObjectGetAndToVariantByKey)
{
    auto const endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };
    auto const simple = Simple{ .blocks = 11, .enabled = true };

    // key owned by the first object
    EXPECT_EQ("localhost", get<std::string>(TR_KEY_address, endpoint, simple).value_or(""));

    // key owned by the second object
    EXPECT_EQ(11, get<int>(TR_KEY_blocks, endpoint, simple).value_or(0));

    // to_variant finds the owner regardless of argument position
    EXPECT_TRUE(to_variant(TR_KEY_dht_enabled, endpoint, simple));

    // key owned by neither
    EXPECT_FALSE(to_variant(TR_KEY_comment, endpoint, simple));
    EXPECT_FALSE(get<int>(TR_KEY_comment, endpoint, simple).has_value());
}

TEST_F(SerializerTest, multiObjectSetRoutesToOwner)
{
    auto endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };
    auto simple = Simple{ .blocks = 1, .enabled = false };

    // routes to the second object, leaving the first untouched
    EXPECT_TRUE(set(TR_KEY_blocks, 5, endpoint, simple));
    EXPECT_EQ(5, simple.blocks);
    EXPECT_EQ("localhost", endpoint.address);

    // routes to the first object
    EXPECT_TRUE(set(TR_KEY_address, std::string{ "example.com" }, endpoint, simple));
    EXPECT_EQ("example.com", endpoint.address);

    // owned by neither
    EXPECT_FALSE(set(TR_KEY_comment, 99, endpoint, simple));

    // via variant, routes to the owning object
    EXPECT_TRUE(set_from_variant(TR_KEY_dht_enabled, tr_variant{ true }, endpoint, simple));
    EXPECT_TRUE(simple.enabled);
    EXPECT_FALSE(set_from_variant(TR_KEY_comment, tr_variant{ true }, endpoint, simple));
}

TEST_F(SerializerTest, hasKeyAcrossMultipleObjects)
{
    auto const endpoint = Endpoint{ .address = "localhost", .port = tr_port::from_host(TrDefaultPeerPort) };
    auto const simple = Simple{ .blocks = 1, .enabled = false };

    using tr::serializer::has_key;

    // single object: key present / absent
    EXPECT_TRUE(has_key(TR_KEY_address, endpoint));
    EXPECT_FALSE(has_key(TR_KEY_blocks, endpoint));

    // key owned by the first object
    EXPECT_TRUE(has_key(TR_KEY_port, endpoint, simple));

    // key owned by the second object
    EXPECT_TRUE(has_key(TR_KEY_blocks, endpoint, simple));

    // owned by neither
    EXPECT_FALSE(has_key(TR_KEY_comment, endpoint, simple));
}

} // namespace
