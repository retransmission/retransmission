// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <clocale>
#include <cstdint>
#include <limits>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include <libtransmission/types.h> // TR_RATIO_NA, TR_RATIO_INF
#include <libtransmission/utils.h> // tr_locale_set_global()
#include <libtransmission/values.h>

#include "test-fixtures.h"

using ValuesTest = ::tr::test::TransmissionTest;
using namespace tr::Values;

TEST_F(ValuesTest, baseQuantity)
{
    auto const val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ(1000000UL, val.base_quantity());
}

TEST_F(ValuesTest, count)
{
    auto const val = Speed{ 1, Speed::Units::MByps };
    EXPECT_NEAR(1000U, val.count(Speed::Units::KByps), 0.0001);
    EXPECT_NEAR(1U, val.count(Speed::Units::MByps), 0.0001);
    EXPECT_NEAR(0.001, val.count(Speed::Units::GByps), 0.0001);
}

TEST_F(ValuesTest, toString)
{
    auto val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ("1 MB/s", val.to_string());

    val = Speed{ 1, Speed::Units::Byps };
    EXPECT_EQ(1U, val.base_quantity());
    EXPECT_EQ("1 B/s", val.to_string());

    val = Speed{ 10, Speed::Units::KByps };
    EXPECT_EQ("10 kB/s", val.to_string());

    val = Speed{ 999, Speed::Units::KByps };
    EXPECT_EQ("999 kB/s", val.to_string());

    val = Speed{ 99.22222, Speed::Units::KByps };
    EXPECT_EQ("99.22 kB/s", val.to_string());

    val = Speed{ 999.22222, Speed::Units::KByps };
    EXPECT_EQ("999.2 kB/s", val.to_string());
}

TEST_F(ValuesTest, isZero)
{
    auto val = Speed{};
    EXPECT_TRUE(val.is_zero());

    val = Speed{ 0, Speed::Units::Byps };
    EXPECT_TRUE(val.is_zero());

    val = Speed{ 1, Speed::Units::Byps };
    EXPECT_FALSE(val.is_zero());
}

TEST_F(ValuesTest, displayNames)
{
    using Names = decltype(Config::memory.display_names());

    EXPECT_EQ((Names{ "B", "KiB", "MiB", "GiB", "TiB" }), Config::memory.display_names());
    EXPECT_EQ((Names{ "B/s", "kB/s", "MB/s", "GB/s", "TB/s" }), Config::speed.display_names());
    EXPECT_EQ((Names{ "B", "kB", "MB", "GB", "TB" }), Config::storage.display_names());
}

TEST_F(ValuesTest, percentIsBuiltFromCounts)
{
    auto const half = Percent{ 50U, 100U };
    EXPECT_FALSE(half.is_complete());
    EXPECT_EQ(50U, half.have());
    EXPECT_EQ(100U, half.total());
    EXPECT_DOUBLE_EQ(0.5, half.to_fraction());
    EXPECT_DOUBLE_EQ(0.5, half.quotient());

    EXPECT_TRUE((Percent{ 100U, 100U }).is_complete());
    EXPECT_TRUE((Percent{ 150U, 100U }).is_complete());
    EXPECT_DOUBLE_EQ(1.0, (Percent{ 150U, 100U }).to_fraction());
    EXPECT_DOUBLE_EQ(1.0, (Percent{ 150U, 100U }).quotient());

    // an empty torrent has nothing left to do
    EXPECT_TRUE((Percent{ 0U, 0U }).is_complete());

    // a default Percent is 0% and incomplete, e.g. metadata of unknown size
    EXPECT_FALSE(Percent{}.is_complete());
    EXPECT_DOUBLE_EQ(0.0, Percent{}.to_fraction());
    EXPECT_DOUBLE_EQ(0.0, Percent{}.quotient());
}

TEST_F(ValuesTest, percentNeverReadsCompleteEarly)
{
    // 99.995% rounds to the grid's top step but stays below completion
    auto const almost = Percent{ 99995U, 100000U };
    EXPECT_FALSE(almost.is_complete());
    EXPECT_DOUBLE_EQ(0.9999, almost.to_fraction());
    EXPECT_DOUBLE_EQ(0.99995, almost.quotient());
    EXPECT_EQ("99.9", almost.to_string());
    EXPECT_EQ("99", almost.to_whole_string());

    EXPECT_EQ("99.9", (Percent{ 9995U, 10000U }).to_string()); // would round to "100.0"
    EXPECT_EQ("99", (Percent{ 996U, 1000U }).to_whole_string()); // would round to "100"

    EXPECT_EQ("100", (Percent{ 1U, 1U }).to_string());
    EXPECT_EQ("100", (Percent{ 1U, 1U }).to_whole_string());
    EXPECT_EQ("100", (Percent{ 0U, 0U }).to_string());
}

TEST_F(ValuesTest, percentToStringTiers)
{
    EXPECT_EQ("0.00", (Percent{ 0U, 1U }).to_string());
    EXPECT_EQ("0.01", (Percent{ 1U, 10000U }).to_string());
    EXPECT_EQ("4.99", (Percent{ 499U, 10000U }).to_string());
    EXPECT_EQ("5.0", (Percent{ 4995U, 100000U }).to_string()); // 4.995% rounds into the next tier
    EXPECT_EQ("5.0", (Percent{ 500U, 10000U }).to_string());
    EXPECT_EQ("50.0", (Percent{ 1U, 2U }).to_string());
    EXPECT_EQ("99.9", (Percent{ 999U, 1000U }).to_string());

    EXPECT_EQ("0", (Percent{ 4U, 1000U }).to_whole_string());
    EXPECT_EQ("1", (Percent{ 5U, 1000U }).to_whole_string()); // half rounds up
    EXPECT_EQ("50", (Percent{ 1U, 2U }).to_whole_string());
}

TEST_F(ValuesTest, percentFromDouble)
{
    EXPECT_TRUE(Percent::from_double(1.0).is_complete());
    EXPECT_TRUE(Percent::from_double(1.5).is_complete());

    auto const almost = Percent::from_double(0.99996);
    EXPECT_FALSE(almost.is_complete());
    EXPECT_DOUBLE_EQ(0.9999, almost.to_fraction());
    EXPECT_EQ("99.9", almost.to_string());

    EXPECT_EQ(Percent::from_double(0.5), (Percent{ 1U, 2U }));
    EXPECT_EQ(Percent{}, Percent::from_double(0.0));
    EXPECT_EQ(Percent{}, Percent::from_double(-0.5));
    EXPECT_EQ(Percent{}, Percent::from_double(std::numeric_limits<double>::quiet_NaN()));
}

TEST_F(ValuesTest, percentComparesAtDisplayGranularity)
{
    EXPECT_EQ((Percent{ 50004U, 100000U }), (Percent{ 5U, 10U })); // 50.004% reads as 50.00%
    EXPECT_NE((Percent{ 5001U, 10000U }), (Percent{ 5U, 10U }));
    EXPECT_LT((Percent{ 5U, 10U }), (Percent{ 5001U, 10000U }));
    EXPECT_LT((Percent{ 9999U, 10000U }), (Percent{ 1U, 1U }));
    EXPECT_GT((Percent{ 1U, 1U }), Percent::from_double(0.99999));

    // the largest counts don't overflow, nor does the remainder's rounding term
    static auto constexpr Max = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ((Percent{ Max / 2U, Max }), (Percent{ 1U, 2U }));
    EXPECT_TRUE((Percent{ Max, Max }).is_complete());
    EXPECT_EQ((Percent{ (Max / 10000U) - 1U, Max / 10000U }), (Percent{ 9999U, 10000U }));
}

namespace
{

// de_DE.UTF-8, or nullopt where it is not installed
[[nodiscard]] std::optional<std::locale> germanLocale()
{
    auto loc = std::locale{};
    try {
        loc = std::locale{ "de_DE.UTF-8" };
    } catch (std::runtime_error const&) {
        return {};
    }

    // musl and OpenBSD accept any locale name but keep "C" numerics
    if (std::use_facet<std::numpunct<char>>(loc).decimal_point() != ',') {
        return {};
    }

    return loc;
}

// Passes de_DE.UTF-8 explicitly; the global locale stays classic.
class ValuesGivenLocaleTest : public ::tr::test::TransmissionTest
{
protected:
    void SetUp() override
    {
        TransmissionTest::SetUp();

        if (!de_) {
            GTEST_SKIP() << "de_DE.UTF-8 is not installed";
        }

        // so German separators in the output can only come from `de_`
        ASSERT_EQ("C", std::locale{}.name());
    }

    std::optional<std::locale> de_ = germanLocale();
};

// Sets the global locale to de_DE.UTF-8.
class ValuesLocaleTest : public ::tr::test::TransmissionTest
{
protected:
    void SetUp() override
    {
        TransmissionTest::SetUp();

        auto const de = germanLocale();
        if (!de) {
            GTEST_SKIP() << "de_DE.UTF-8 is not installed";
        }

        old_locale_ = tr_locale_set_global(*de);
    }

    void TearDown() override
    {
        if (old_locale_) {
            tr_locale_set_global(*old_locale_);
        }

        TransmissionTest::TearDown();
    }

private:
    std::optional<std::locale> old_locale_;
};

// `digits` in Arabic-Indic digits, e.g. "٤٦" for "46"
[[nodiscard]] std::string arabicIndic(std::string_view const digits)
{
    auto ret = std::string{};
    for (auto const ch : digits) {
        ret += '\xD9';
        ret += static_cast<char>(0xA0 + (ch - '0'));
    }
    return ret;
}

// Writes unsigned integers in Arabic-Indic digits, as a client's {fmt} facet can,
// so the output shows which numbers were formatted with the locale.
class ArabicIndicDigits final : public fmt::format_facet<std::locale>
{
protected:
    [[nodiscard]] bool do_put(fmt::appender out, fmt::loc_value val, fmt::format_specs const& /*specs*/) const override
    {
        return val.visit([&](auto const value) -> bool {
            using T = std::remove_cv_t<decltype(value)>;

            if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T, unsigned long long>) {
                for (auto const ch : arabicIndic(fmt::format("{}", value))) {
                    *out++ = ch;
                }
                return true;
            } else {
                return false;
            }
        });
    }
};

} // namespace

TEST_F(ValuesLocaleTest, percentUsesTheGlobalLocale)
{
    EXPECT_EQ("4,99", (Percent{ 499U, 10000U }).to_string());
    EXPECT_EQ("99,9", (Percent{ 999U, 1000U }).to_string());
    EXPECT_EQ("50", (Percent{ 1U, 2U }).to_whole_string());
}

TEST_F(ValuesGivenLocaleTest, percentUsesTheGivenLocale)
{
    EXPECT_EQ("4,99", (Percent{ 499U, 10000U }).to_string(*de_));
}

TEST_F(ValuesTest, percentDigitsComeFromTheGivenLocale)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,clang-analyzer-cplusplus.NewDeleteLeaks): the locale owns the facet
    auto const loc = std::locale{ std::locale::classic(), new ArabicIndicDigits{} };
    EXPECT_EQ(arabicIndic("100"), (Percent{ 1U, 1U }).to_string(loc));
    EXPECT_EQ(arabicIndic("100"), (Percent{ 1U, 1U }).to_whole_string(loc));
    EXPECT_EQ(arabicIndic("99"), (Percent{ 999U, 1000U }).to_whole_string(loc));
    EXPECT_EQ(arabicIndic("46"), (Percent{ 4567U, 10000U }).to_whole_string(loc));
    EXPECT_EQ(arabicIndic("0"), (Percent{ 0U, 1U }).to_whole_string(loc));
}

TEST_F(ValuesTest, ratioStates)
{
    EXPECT_TRUE(Ratio{}.is_none());
    EXPECT_TRUE((Ratio{ 0U, 0U }).is_none());
    EXPECT_TRUE((Ratio{ 5U, 0U }).is_infinite());
    EXPECT_FALSE((Ratio{ 5U, 1U }).is_none());
    EXPECT_FALSE((Ratio{ 5U, 1U }).is_infinite());

    EXPECT_EQ(std::nullopt, Ratio{}.to_double());
    EXPECT_EQ(std::nullopt, (Ratio{ 5U, 0U }).to_double());
    EXPECT_EQ(std::nullopt, (Ratio{ 5U, 0U }).quotient());
    EXPECT_EQ(5.0, (Ratio{ 5U, 1U }).to_double());
    EXPECT_EQ(5U, (Ratio{ 5U, 1U }).numerator());
    EXPECT_EQ(1U, (Ratio{ 5U, 1U }).denominator());
}

TEST_F(ValuesTest, ratioFromDouble)
{
    EXPECT_TRUE(Ratio::from_double(TR_RATIO_NA).is_none());
    EXPECT_TRUE(Ratio::from_double(TR_RATIO_INF).is_infinite());
    EXPECT_TRUE(Ratio::from_double(std::numeric_limits<double>::quiet_NaN()).is_none());
    EXPECT_EQ(2.0, Ratio::from_double(2.0).to_double());
    EXPECT_EQ(Ratio::from_double(0.5), (Ratio{ 1U, 2U }));
    EXPECT_EQ("2.00", Ratio::from_double(2.0).to_string());
    EXPECT_EQ("30000000", Ratio::from_double(3.0e7).to_string()); // in hundredths, past a 32-bit long
}

TEST_F(ValuesTest, ratioToStringTiers)
{
    EXPECT_EQ("0.00", (Ratio{ 0U, 1U }).to_string());
    EXPECT_EQ("0.50", (Ratio{ 1U, 2U }).to_string());
    EXPECT_EQ("4.99", (Ratio{ 499U, 100U }).to_string());
    EXPECT_EQ("5.0", (Ratio{ 4996U, 1000U }).to_string()); // 4.996 rounds into the next tier
    EXPECT_EQ("7.5", (Ratio{ 75U, 10U }).to_string());
    EXPECT_EQ("100", (Ratio{ 9995U, 100U }).to_string()); // 99.95 rounds into the next tier
    EXPECT_EQ("123", (Ratio{ 1234U, 10U }).to_string());

    EXPECT_EQ("None", Ratio{}.to_string());
    EXPECT_EQ("\xE2\x88\x9E", (Ratio{ 1U, 0U }).to_string());
}

TEST_F(ValuesTest, ratioStringsComeFromConfig)
{
    auto const saved = Config::ratio;
    Config::ratio = { .none = "n/a", .infinity = "inf" };
    EXPECT_EQ("n/a", Ratio{}.to_string());
    EXPECT_EQ("inf", (Ratio{ 1U, 0U }).to_string());
    Config::ratio = saved;
}

TEST_F(ValuesTest, ratioComparesAtDisplayGranularity)
{
    EXPECT_EQ((Ratio{ 1004U, 1000U }), (Ratio{ 1U, 1U })); // 1.004 reads as 1.00
    EXPECT_NE((Ratio{ 101U, 100U }), (Ratio{ 1U, 1U }));
    EXPECT_EQ(Ratio{}, (Ratio{ 0U, 0U }));
    EXPECT_NE(Ratio{}, (Ratio{ 1U, 0U }));
    EXPECT_NE((Ratio{ 1U, 0U }), (Ratio{ 1U, 1U }));

    // the largest counts don't overflow, nor does the remainder's rounding term
    static auto constexpr Max = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ((Ratio{ Max, Max }), (Ratio{ 1U, 1U }));
    EXPECT_EQ("1.00", (Ratio{ Max, Max }).to_string());
    EXPECT_EQ((Ratio{ (Max / 100U) - 1U, Max / 100U }), (Ratio{ 1U, 1U }));

    // a quotient past UINT64_MAX hundredths saturates and still reads as a number
    EXPECT_FALSE((Ratio{ Max, 1U }).is_infinite());
    EXPECT_EQ(std::to_string(Max / 100U), (Ratio{ Max, 1U }).to_string());
}

TEST_F(ValuesLocaleTest, ratioUsesTheGlobalLocale)
{
    EXPECT_EQ("0,50", (Ratio{ 1U, 2U }).to_string());
    EXPECT_EQ("7,5", (Ratio{ 75U, 10U }).to_string());
}

TEST_F(ValuesGivenLocaleTest, ratioUsesTheGivenLocale)
{
    EXPECT_EQ("0,67", (Ratio{ 2U, 3U }).to_string(*de_));
    EXPECT_EQ("1.234.567", (Ratio{ 1234567U, 1U }).to_string(*de_));
}

TEST_F(ValuesLocaleTest, givenLocaleOverridesTheGlobal)
{
    EXPECT_EQ("4.99", (Percent{ 499U, 10000U }).to_string(std::locale::classic()));
    EXPECT_EQ("0.67", (Ratio{ 2U, 3U }).to_string(std::locale::classic()));
}

TEST_F(ValuesGivenLocaleTest, givenLocaleLeavesTheCLocaleAlone)
{
    auto const numeric = std::string{ std::setlocale(LC_NUMERIC, nullptr) };
    EXPECT_EQ("4,99", (Percent{ 499U, 10000U }).to_string(*de_));
    EXPECT_EQ("0,67", (Ratio{ 2U, 3U }).to_string(*de_));
    EXPECT_EQ(numeric, std::setlocale(LC_NUMERIC, nullptr));
}
