// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstddef> // size_t
#include <functional> // std::hash
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/shared-string.h>

#include "test-fixtures.h"

using namespace std::literals;

using SharedStringTest = ::tr::test::TransmissionTest;

using tr::shared_string;

namespace
{
// Other tests in this binary may hold pooled strings of their own,
// so assertions about the pool are relative to whatever is in it now.
[[nodiscard]] auto poolSize()
{
    return shared_string::pool_size();
}
} // namespace

TEST_F(SharedStringTest, defaultIsEmpty)
{
    auto const str = shared_string{};
    EXPECT_TRUE(str.empty());
    EXPECT_EQ(""sv, str.sv());
    EXPECT_STREQ("", str.c_str());
}

TEST_F(SharedStringTest, emptyStringIsNotPooled)
{
    auto const baseline = poolSize();
    auto const from_sv = shared_string{ ""sv };
    auto const from_c_str = shared_string{ "" };
    auto const from_nullptr = shared_string{ static_cast<char const*>(nullptr) };

    EXPECT_EQ(baseline, poolSize());
    EXPECT_TRUE(from_sv.empty());
    EXPECT_EQ(shared_string{}, from_sv);
    EXPECT_EQ(from_sv, from_c_str);
    EXPECT_EQ(from_sv, from_nullptr);
}

TEST_F(SharedStringTest, equalTextSharesOneCopy)
{
    auto const baseline = poolSize();

    auto const a = shared_string{ "https://example.org/announce"sv };
    auto const b = shared_string{ "https://example.org/announce"sv };
    auto const c = shared_string{ "https://example.com/announce"sv };

    EXPECT_EQ(baseline + 2U, poolSize());
    EXPECT_EQ(a, b);
    EXPECT_EQ(std::data(a.sv()), std::data(b.sv())); // literally the same buffer
    EXPECT_NE(a, c);
}

TEST_F(SharedStringTest, poolDrainsWhenLastHandleGoesAway)
{
    auto const baseline = poolSize();

    {
        auto const outer = shared_string{ "keep-me"sv };
        EXPECT_EQ(baseline + 1U, poolSize());

        {
            auto const inner = shared_string{ "keep-me"sv };
            auto const other = shared_string{ "drop-me"sv };
            EXPECT_EQ(baseline + 2U, poolSize());
        }

        // `inner` shared `outer`'s copy, so only "drop-me" was freed
        EXPECT_EQ(baseline + 1U, poolSize());
    }

    EXPECT_EQ(baseline, poolSize());
}

TEST_F(SharedStringTest, copyAndMove)
{
    auto const baseline = poolSize();

    auto original = shared_string{ "value"sv };
    auto const copied = original; // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_EQ(baseline + 1U, poolSize());
    EXPECT_EQ(original, copied);

    // A move hands over the reference rather than taking another one
    auto const moved = std::move(original);
    EXPECT_EQ(baseline + 1U, poolSize());
    EXPECT_EQ(copied, moved);
}

TEST_F(SharedStringTest, assignmentReleasesPriorValue)
{
    auto const baseline = poolSize();

    auto str = shared_string{ "first"sv };
    EXPECT_EQ(baseline + 1U, poolSize());

    str = "second"sv;
    EXPECT_EQ(baseline + 1U, poolSize());
    EXPECT_EQ("second"sv, str.sv());

    str.clear();
    EXPECT_EQ(baseline, poolSize());
    EXPECT_TRUE(str.empty());
}

TEST_F(SharedStringTest, selfAssignmentKeepsValue)
{
    auto const baseline = poolSize();

    auto str = shared_string{ "value"sv };
    auto& alias = str;
    str = alias;

    EXPECT_EQ(baseline + 1U, poolSize());
    EXPECT_EQ("value"sv, str.sv());
}

TEST_F(SharedStringTest, orderingIsLexicographic)
{
    auto strs = std::vector<shared_string>{ shared_string{ "ccc"sv }, shared_string{ "aaa"sv }, shared_string{ "bbb"sv } };
    std::ranges::sort(strs);

    EXPECT_EQ("aaa"sv, strs[0].sv());
    EXPECT_EQ("bbb"sv, strs[1].sv());
    EXPECT_EQ("ccc"sv, strs[2].sv());

    EXPECT_LT(shared_string{ "aaa"sv }, shared_string{ "bbb"sv });
}

TEST_F(SharedStringTest, comparesWithStringView)
{
    auto const str = shared_string{ "value"sv };
    EXPECT_EQ("value"sv, str);
    EXPECT_NE("other"sv, str);
}

TEST_F(SharedStringTest, hashesAsItsText)
{
    static auto constexpr Text = "value"sv;

    // hashing the text, not the pooled address, is what lets a
    // shared_string key be looked up by an equal std::string_view
    EXPECT_EQ(std::hash<std::string_view>{}(Text), std::hash<shared_string>{}(shared_string{ Text }));
    EXPECT_EQ(std::hash<std::string_view>{}(""sv), std::hash<shared_string>{}(shared_string{}));

    auto set = std::unordered_set<shared_string>{};
    set.insert(shared_string{ Text });
    EXPECT_EQ(1U, set.count(shared_string{ Text }));
}

TEST_F(SharedStringTest, cStrIsNulTerminated)
{
    auto const str = shared_string{ "abc"sv };
    auto const* const c_str = str.c_str();
    EXPECT_STREQ("abc", c_str);
    EXPECT_EQ('\0', c_str[std::size(str.sv())]);
}

TEST_F(SharedStringTest, longStringRoundTrips)
{
    // Everything else in this suite interns short literals, so this is
    // the only test that sizes a node past a few dozen characters.
    auto const text = std::string(1000U, 'x') + "-end";

    auto const str = shared_string{ text };
    EXPECT_EQ(text, str.sv());
    EXPECT_STREQ(text.c_str(), str.c_str());
    EXPECT_EQ(str, shared_string{ text });
}

TEST_F(SharedStringTest, embeddedNulRoundTrips)
{
    auto const text = "abc\0def"sv;

    auto const str = shared_string{ text };
    EXPECT_EQ(text, str.sv());

    // pooling keys on the whole span, not the leading C string
    EXPECT_NE(str, shared_string{ "abc"sv });

    // c_str() reads as a C string, so it stops at the embedded NUL
    EXPECT_STREQ("abc", str.c_str());
}

TEST_F(SharedStringTest, concurrentInternAndRelease)
{
    static auto constexpr NumThreads = size_t{ 8U };
    static auto constexpr NumRounds = size_t{ 2000U };
    static auto constexpr NumKeys = size_t{ 4U };

    auto const baseline = poolSize();

    auto threads = std::vector<std::thread>{};
    threads.reserve(NumThreads);
    for (size_t i = 0U; i < NumThreads; ++i) {
        threads.emplace_back([i]() {
            for (size_t round = 0U; round < NumRounds; ++round) {
                // Contend on a small key set so that the last reference to
                // a value is repeatedly dropped while others are interning it.
                auto const str = shared_string{ fmt::format("key-{:d}", (i + round) % NumKeys) };
                EXPECT_FALSE(str.empty());
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(baseline, poolSize());
}
