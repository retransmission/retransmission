// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <string>
#include <string_view>

#include <glibmm/ustring.h>

#include <gtest/gtest.h>

#include <libtransmission/converters.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "Prefs.h"
#include "test-fixtures.h"

using namespace std::literals;
using tr::serializer::to_value;
using tr::serializer::to_variant;

using SerializerTest = GtkTest;

// `Glib::ustring` is a range of `gunichar`, so without its Converter the
// generic range fallback would serialize it as a vector of code points.
TEST_F(SerializerTest, ustringToVariantIsString)
{
    auto const var = to_variant(Glib::ustring{ "sort_by_name" });
    EXPECT_EQ(var.value_if<std::string_view>(), "sort_by_name"sv);
}

TEST_F(SerializerTest, ustringFromStringVariant)
{
    auto const val = to_value<Glib::ustring>(tr_variant{ "sort_by_name"s });
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "sort_by_name");
}

TEST_F(SerializerTest, ustringFromNonStringVariantFails)
{
    EXPECT_FALSE(to_value<Glib::ustring>(tr_variant{ int64_t{ 42 } }).has_value());
}

// Text entries hand the prefs a `Glib::ustring`; everything else in the
// client reads the same key back as a `std::string`.
TEST_F(SerializerTest, ustringPrefReadsBackAsString)
{
    auto constexpr Key = TR_KEY_rpc_username;

    gtr_pref_set(Key, Glib::ustring{ "alice" });

    EXPECT_EQ(gtr_pref_string_get(Key), "alice");
    EXPECT_EQ(gtr_pref_get<Glib::ustring>(Key), "alice");
}
