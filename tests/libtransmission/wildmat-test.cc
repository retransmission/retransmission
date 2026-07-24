// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include <gtest/gtest.h>

#include <wildmat.h>

#include <libtransmission/string-utils.h>

using namespace std::literals;

// the matcher is constexpr, so it also works at compile time
static_assert(wildmat::match(""sv, ""sv));
static_assert(wildmat::match(""sv, "*"sv));
static_assert(!wildmat::match(""sv, "?"sv));
static_assert(wildmat::match("foo.torrent"sv, "*.torrent"sv));
static_assert(!wildmat::match("foo.torrent"sv, "*.png"sv));
static_assert(wildmat::match("bat"sv, "b[a-c]t"sv));
static_assert(!wildmat::match("bat"sv, "b[^a-c]t"sv));
static_assert(wildmat::match("b*t"sv, "b\\*t"sv));

TEST(Wildmat, literals)
{
    EXPECT_TRUE(wildmat::match("foo", "foo"));
    EXPECT_FALSE(wildmat::match("foo", "fo"));
    EXPECT_FALSE(wildmat::match("fo", "foo"));
    EXPECT_FALSE(wildmat::match("Foo", "foo")); // case-sensitive
    EXPECT_TRUE(wildmat::match("", ""));
    EXPECT_FALSE(wildmat::match("foo", ""));
    EXPECT_FALSE(wildmat::match("", "foo"));
}

TEST(Wildmat, questionMark)
{
    EXPECT_TRUE(wildmat::match("mini", "min?"));
    EXPECT_TRUE(wildmat::match("f.o", "f?o"));
    EXPECT_FALSE(wildmat::match("fo", "f?o")); // '?' cannot match nothing
    EXPECT_FALSE(wildmat::match("", "?"));
}

TEST(Wildmat, star)
{
    EXPECT_TRUE(wildmat::match("", "*"));
    EXPECT_TRUE(wildmat::match("anything at all", "*"));
    EXPECT_TRUE(wildmat::match("foobar", "foo*"));
    EXPECT_TRUE(wildmat::match("foobar", "*bar"));
    EXPECT_TRUE(wildmat::match("foobar", "f*r"));
    EXPECT_TRUE(wildmat::match("foobar", "foobar*"));
    EXPECT_TRUE(wildmat::match("barfoobar", "*foo*"));
    EXPECT_TRUE(wildmat::match("foobar", "***f**b*r**"));
    EXPECT_FALSE(wildmat::match("foobar", "foo*x"));
    EXPECT_FALSE(wildmat::match("bat", "at*")); // pattern must cover all of text
    EXPECT_TRUE(wildmat::match("abcxxxxxd", "*a*b*c*d"));
    EXPECT_FALSE(wildmat::match("abcxxxxx", "*a*b*c*d"));

    // the pathological case from the original's header comment
    auto constexpr Pattern = "-*-*-*-*-*-*-12-*-*-*-m-*-*-*"sv;
    EXPECT_TRUE(wildmat::match("-adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1", Pattern));
    EXPECT_FALSE(wildmat::match("-adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1", Pattern));
}

TEST(Wildmat, characterSets)
{
    EXPECT_TRUE(wildmat::match("bar", "b[aei]r"));
    EXPECT_FALSE(wildmat::match("bor", "b[aei]r"));
    EXPECT_TRUE(wildmat::match("bar", "b[a-c]r"));
    EXPECT_FALSE(wildmat::match("bzr", "b[a-c]r"));
    EXPECT_TRUE(wildmat::match("byr", "b[a-cx-z]r")); // multiple ranges
    EXPECT_FALSE(wildmat::match("bar", "b[]r")); // an empty set matches nothing
    EXPECT_FALSE(wildmat::match("bar", "b[-a]r")); // a leading '-' starts a dead range: 'a' is its endpoint, not a member
    EXPECT_FALSE(wildmat::match("b-r", "b[-a]r")); // ...and the '-' itself is not a member either
    EXPECT_TRUE(wildmat::match("b*r", "b[*?]r")); // wildcards are literal inside a set
}

TEST(Wildmat, negatedSets)
{
    EXPECT_TRUE(wildmat::match("bor", "b[^aei]r"));
    EXPECT_FALSE(wildmat::match("bar", "b[^aei]r"));
    EXPECT_TRUE(wildmat::match("bZr", "b[^a-z]r"));
    EXPECT_FALSE(wildmat::match("bmr", "b[^a-z]r"));
    EXPECT_TRUE(wildmat::match("bxr", "b[^]r")); // an empty negated set matches any character
}

TEST(Wildmat, escapes)
{
    EXPECT_TRUE(wildmat::match("b*r", "b\\*r"));
    EXPECT_FALSE(wildmat::match("bar", "b\\*r"));
    EXPECT_TRUE(wildmat::match("b?r", "b\\?r"));
    EXPECT_FALSE(wildmat::match("bar", "b\\?r"));
    EXPECT_TRUE(wildmat::match("b[r", "b\\[r"));
    EXPECT_TRUE(wildmat::match("b\\r", "b\\\\r"));
    EXPECT_TRUE(wildmat::match("bar", "b\\ar")); // escaping an ordinary char is fine
}

TEST(Wildmat, malformedPatternsNeverMatch)
{
    // the original could read out of bounds on unterminated sets;
    // here, malformed patterns are handled safely and just never match
    EXPECT_FALSE(wildmat::match("fooa", "foo[a-"));
    EXPECT_FALSE(wildmat::match("fooa", "foo[a"));
    EXPECT_FALSE(wildmat::match("foox", "foo[^"));
    EXPECT_FALSE(wildmat::match("b-r", "b[x-]r")); // the '-' consumes the ']', so this set never closes
    EXPECT_FALSE(wildmat::match("foo\\", "foo\\")); // trailing backslash matches nothing
}

TEST(Wildmat, eightBitClean)
{
    // bytes compare as unsigned char, so high-bit bytes work in ranges
    EXPECT_TRUE(wildmat::match("\xE9", "[\xE0-\xEF]"));
    EXPECT_FALSE(wildmat::match("\xD9", "[\xE0-\xEF]"));
    EXPECT_TRUE(wildmat::match("caf\xC3\xA9", "caf??")); // a 2-byte UTF-8 char needs two '?'s
    EXPECT_TRUE(wildmat::match("caf\xC3\xA9", "caf*"));
}

TEST(Wildmat, trWildmat)
{
    EXPECT_TRUE(tr_wildmat("192.168.1.100"sv, "192.168.*.*"sv));
    EXPECT_FALSE(tr_wildmat("10.0.1.100"sv, "192.168.*.*"sv));
    EXPECT_TRUE(tr_wildmat("torrents.example.com"sv, "*.example.com"sv));
    EXPECT_FALSE(tr_wildmat("example.com.evil.org"sv, "*.example.com"sv));

    // string_views need not be zero-terminated
    auto constexpr Full = "foobar"sv;
    EXPECT_TRUE(tr_wildmat(Full.substr(0, 3), "foo"sv));
    EXPECT_FALSE(tr_wildmat(Full.substr(0, 3), "foobar"sv));
}
