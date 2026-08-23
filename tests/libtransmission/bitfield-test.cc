// This file Copyright (C) 2010-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/bitfield.h>

TEST(Bitfield, count)
{
    auto constexpr IterCount = size_t{ 10000U };

    for (size_t i = 0; i < IterCount; ++i) {
        // generate a random bitfield
        auto const bit_count = 100U + tr_rand_int(1000U);
        auto bf = tr_bitfield{ bit_count };
        for (size_t idx = 0U; idx < bit_count; ++idx) {
            bf.set(idx, tr_rand_int(2U) != 0U);
        }

        // pick arbitrary endpoints in the 1st and 2nd half of the bitfield
        auto const midpt = bit_count / 2U;
        auto const begin = tr_rand_int(midpt);
        auto const end = midpt + tr_rand_int(midpt);

        // test the bitfield
        unsigned long count1 = {};
        for (auto j = begin; j < end; ++j) {
            if (bf.test(j)) {
                ++count1;
            }
        }

        auto const count2 = bf.count(begin, end);
        EXPECT_EQ(count1, count2);
    }

    auto bf = tr_bitfield{ 0 };
    EXPECT_TRUE(bf.has_none());
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(0U, bf.count(0, 1));

    bf.set_has_all();
    EXPECT_FALSE(bf.has_none());
    EXPECT_TRUE(bf.has_all());
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(10U, bf.count(0, 10));
    EXPECT_EQ(13U, bf.count(11, 24));

    bf = tr_bitfield{ 100 };
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(0U, bf.count(0, 100));
    bf.set_has_all();
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(1U, bf.count(0, 1));
    EXPECT_EQ(100U, bf.count(0, 100));
    EXPECT_EQ(100U, bf.count(0, 1000));
}

TEST(Bitfield, countBounds)
{
    auto constexpr Raw = std::to_array<std::byte>({ std::byte{ 0xa0 } });

    auto bf = tr_bitfield{ 17 };
    ASSERT_TRUE(bf.set_raw(Raw));
    EXPECT_TRUE(bf.is_valid());

    EXPECT_EQ(2U, bf.count(0U, bf.size()));
    EXPECT_EQ(2U, bf.count(0U, std::numeric_limits<size_t>::max()));
    EXPECT_EQ(1U, bf.count(0U, 1U));
    EXPECT_EQ(1U, bf.count(1U, 3U));
    EXPECT_EQ(0U, bf.count(8U, bf.size()));
    EXPECT_EQ(0U, bf.count(bf.size(), std::numeric_limits<size_t>::max()));
    EXPECT_EQ(0U, bf.count(5U, 4U));

    bf.set_has_all();
    EXPECT_EQ(bf.size(), bf.count(0U, std::numeric_limits<size_t>::max()));
    EXPECT_EQ(bf.size() - 1U, bf.count(1U, std::numeric_limits<size_t>::max()));
}

TEST(Bitfield, ctorFromFlagArray)
{
    auto constexpr Tests = std::to_array<std::array<bool, 10>>({
        { false, true, false, true, false, false, true, false, false, true }, // mixed
        { true, true, true, true, true, true, true, true, true, true }, // have all
        { false, false, false, false, false, false, false, false, false, false }, // have none
    });

    for (auto const& flags : Tests) {
        size_t const true_count = std::count(std::begin(flags), std::end(flags), true);
        size_t const n = std::size(flags);
        bool const have_all = true_count == n;
        bool const have_none = true_count == 0;

        auto bf = tr_bitfield(n);
        EXPECT_TRUE(bf.set_from_bools(flags));

        EXPECT_EQ(n, bf.size());
        EXPECT_EQ(have_all, bf.has_all());
        EXPECT_EQ(have_none, bf.has_none());
        EXPECT_EQ(true_count, bf.count());

        for (size_t i = 0; i < std::size(flags); ++i) {
            EXPECT_EQ(flags[i], bf.test(i));
        }
    }
}

TEST(Bitfield, setFromBoolsAfterHaveAll)
{
    auto constexpr Flags = std::to_array<bool>({ true, false, true, false, false, true, false, true, false, false });

    auto bf = tr_bitfield(std::size(Flags));
    bf.set_has_all();
    ASSERT_TRUE(bf.set_from_bools(Flags));

    EXPECT_FALSE(bf.has_all());
    EXPECT_FALSE(bf.has_none());
    EXPECT_EQ(4U, bf.count());

    for (size_t i = 0; i < std::size(Flags); ++i) {
        EXPECT_EQ(Flags[i], bf.test(i));
    }
}

TEST(Bitfield, setRaw)
{
    auto constexpr TestByte = std::byte{ 10 };
    auto constexpr TestByteTrueBits = 2;

    auto raw = std::vector(100, TestByte);

    auto bf = tr_bitfield(std::size(raw) * 8);
    ASSERT_TRUE(bf.set_raw(raw));
    EXPECT_EQ(TestByteTrueBits * std::size(raw), bf.count());

    // The first byte of the bitfield corresponds to indices 0 - 7
    // from high bit to low bit, respectively. The next one 8-15, etc.
    // Spare bits at the end are set to zero.
    auto test = std::byte{};
    for (int i = 0; i < 8; ++i) {
        if (bf.test(i)) {
            test |= (std::byte{ 1 } << (7 - i));
        }
    }
    EXPECT_EQ(TestByte, test);
    EXPECT_EQ(raw, bf.raw());

    // check that has-all bitfield gets all-true
    bf = tr_bitfield(std::size(raw) * 8);
    bf.set_has_all();
    raw = bf.raw();
    EXPECT_EQ(std::size(bf) / 8, std::size(raw));
    EXPECT_EQ(std::byte{ 0xFF }, raw[0]);

    // check that the spare bits t the end are zero
    bf = tr_bitfield{ 1 };
    static constexpr auto By = std::byte{ 0xFF };
    ASSERT_TRUE(bf.set_raw({ &By, 1U }));
    EXPECT_TRUE(bf.has_all());
    EXPECT_FALSE(bf.has_none());
    EXPECT_EQ(1U, bf.count());
    raw = bf.raw();
    EXPECT_EQ(1U, std::size(raw));
    EXPECT_EQ(std::byte{ 1 } << 7, raw[0]);
}

TEST(Bitfield, finalBytePadding)
{
    auto const input = std::vector<std::byte>{ std::byte{}, std::byte{ 0xff } };

    auto byte_aligned = tr_bitfield{ std::size(input) * 8U };
    ASSERT_TRUE(byte_aligned.set_raw(input));
    EXPECT_TRUE(byte_aligned.is_valid());
    EXPECT_EQ(input, byte_aligned.raw());

    auto partial_byte = tr_bitfield{ 10 };
    ASSERT_TRUE(partial_byte.set_raw(input));
    EXPECT_TRUE(partial_byte.is_valid());
    EXPECT_EQ((std::vector<std::byte>{ std::byte{}, std::byte{ 0xc0 } }), partial_byte.raw());
}

TEST(Bitfield, acceptsPartialInput)
{
    auto constexpr Flags = std::to_array<bool>({ true, false, true });
    auto constexpr Raw = std::to_array<std::byte>({ std::byte{ 0xa0 } });
    auto constexpr ByteRaw = std::to_array<uint8_t>({ 0x80U });

    auto empty = tr_bitfield{ 10 };
    EXPECT_EQ((std::vector<std::byte>{ std::byte{}, std::byte{} }), empty.raw());

    auto from_bools = tr_bitfield{ 10 };
    ASSERT_TRUE(from_bools.set_from_bools(Flags));
    EXPECT_TRUE(from_bools.is_valid());
    EXPECT_EQ(2U, from_bools.count());
    EXPECT_EQ((std::vector<std::byte>{ std::byte{ 0xa0 }, std::byte{} }), from_bools.raw());

    auto from_raw = tr_bitfield{ 10 };
    ASSERT_TRUE(from_raw.set_raw(Raw));
    EXPECT_TRUE(from_raw.is_valid());
    EXPECT_EQ(2U, from_raw.count());
    EXPECT_EQ((std::vector<std::byte>{ std::byte{ 0xa0 }, std::byte{} }), from_raw.raw());

    auto from_byte_raw = tr_bitfield{ 8 };
    ASSERT_TRUE(from_byte_raw.set_raw(ByteRaw));
    EXPECT_TRUE(from_byte_raw.is_valid());
    EXPECT_TRUE(from_byte_raw.test(0U));
    EXPECT_EQ((std::vector<std::byte>{ std::byte{ 0x80 } }), from_byte_raw.raw());
}

TEST(Bitfield, rejectsOversizedInput)
{
    auto constexpr Raw = std::to_array<std::byte>({ std::byte{ 0xa0 } });
    auto constexpr OversizedRaw = std::to_array<std::byte>({ std::byte{ 0xff }, std::byte{ 0xff }, std::byte{ 0xff } });
    auto constexpr OversizedFlags = std::to_array<bool>({ true, true, true, true, true, true, true, true, true, true, true });

    auto bf = tr_bitfield{ 10 };
    ASSERT_TRUE(bf.set_raw(Raw));
    auto const expected_raw = bf.raw();
    auto const expected_count = bf.count();

    EXPECT_FALSE(bf.set_raw(OversizedRaw));
    EXPECT_FALSE(bf.set_from_bools(OversizedFlags));
    EXPECT_TRUE(bf.is_valid());
    EXPECT_EQ(expected_count, bf.count());
    EXPECT_EQ(expected_raw, bf.raw());
}

TEST(Bitfield, mutationResultsAndBounds)
{
    auto bf = tr_bitfield{ 5 };

    EXPECT_FALSE(bf.set(0U, false));
    EXPECT_TRUE(bf.set(0U));
    EXPECT_FALSE(bf.set(0U));
    EXPECT_FALSE(bf.set(bf.size()));
    EXPECT_FALSE(bf.set(std::numeric_limits<size_t>::max()));

    EXPECT_TRUE(bf.set_span(1U, std::numeric_limits<size_t>::max()));
    EXPECT_TRUE(bf.has_all());
    EXPECT_FALSE(bf.set_span(1U, std::numeric_limits<size_t>::max()));
    EXPECT_TRUE(bf.set_span(1U, std::numeric_limits<size_t>::max(), false));
    EXPECT_EQ(1U, bf.count());
    EXPECT_FALSE(bf.set_span(1U, std::numeric_limits<size_t>::max(), false));
    EXPECT_TRUE(bf.set(0U, false));
    EXPECT_TRUE(bf.has_none());

    EXPECT_FALSE(bf.set_span(0U, 0U));
    EXPECT_FALSE(bf.set_span(5U, std::numeric_limits<size_t>::max()));
    EXPECT_FALSE(bf.set_span(4U, 2U));
    EXPECT_TRUE(bf.is_valid());
}

TEST(Bitfield, rejectsOutOfBoundsAtLargestSize)
{
    auto bf = tr_bitfield{ std::numeric_limits<size_t>::max() };

    EXPECT_TRUE(bf.is_size_known());
    EXPECT_TRUE(bf.has_none());
    EXPECT_TRUE(bf.is_valid());
    EXPECT_FALSE(bf.set(bf.size()));
    EXPECT_FALSE(bf.set_span(bf.size(), bf.size()));
    EXPECT_EQ(0U, bf.count(bf.size(), bf.size()));
}

TEST(Bitfield, bitfields)
{
    unsigned int const bitcount = 500;
    tr_bitfield field(bitcount);

    // test tr_bitfield::set()
    for (unsigned int i = 0; i < bitcount; i++) {
        if (i % 7 == 0) {
            field.set(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++) {
        EXPECT_EQ(field.test(i), (i % 7 == 0));
    }

    /* test tr_bitfield::setSpan */
    field.set_span(0, bitcount);

    for (unsigned int i = 0; i < bitcount; i++) {
        EXPECT_TRUE(field.test(i));
    }

    /* test tr_bitfield::clearBit */
    for (unsigned int i = 0; i < bitcount; i++) {
        if (i % 7 != 0) {
            field.unset(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++) {
        EXPECT_EQ(field.test(i), (i % 7 == 0));
    }

    /* test tr_bitfield::clearBitRange in the middle of a boundary */
    field.set_span(0, 64);
    field.unset_span(4, 21);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 21));
    }

    /* test tr_bitfield::clearBitRange on the boundaries */
    field.set_span(0, 64);
    field.unset_span(8, 24);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (i < 8 || i >= 24));
    }

    /* test tr_bitfield::clearBitRange when begin & end is on the same word */
    field.set_span(0, 64);
    field.unset_span(4, 5);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 5));
    }

    /* test tr_bitfield::setSpan */
    field.unset_span(0, 64);
    field.set_span(4, 21);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (4 <= i && i < 21));
    }

    /* test tr_bitfield::setSpan on the boundaries */
    field.unset_span(0, 64);
    field.set_span(8, 24);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (8 <= i && i < 24));
    }

    /* test tr_bitfield::setSpan when begin & end is on the same word */
    field.unset_span(0, 64);
    field.set_span(4, 5);

    for (unsigned int i = 0; i < 64; i++) {
        EXPECT_EQ(field.test(i), (4 <= i && i < 5));
    }

    /* test tr_bitfield::setSpan when end runs beyond the end of the bitfield */
    field.set_has_none();
    field.set_span(100, 1000);
    EXPECT_FALSE(field.has_none());
    EXPECT_FALSE(field.has_all());
    EXPECT_EQ(std::size(field) - 100, field.count());

    /* test tr_bitfield::unsetSpan when it changes nothing */
    field.set_has_none();
    field.unset_span(0, 100);
    EXPECT_TRUE(field.has_none());
    EXPECT_FALSE(field.has_all());
    EXPECT_EQ(0U, field.count());

    /* test tr_bitfield::setSpan when it changes nothing */
    field.set_has_all();
    field.set_span(0, 100);
    EXPECT_FALSE(field.has_none());
    EXPECT_TRUE(field.has_all());
    EXPECT_EQ(std::size(field), field.count());

    /* test tr_bitfield::setSpan with an invalid span doesn't crash */
    field.set_has_all();
    field.set_span(0, 0);
    EXPECT_TRUE(field.has_all());
}

TEST(Bitfield, hasAllNone)
{
    {
        tr_bitfield field(3);

        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(field.has_none());

        field.set(0);
        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(!field.has_none());

        field.unset(0);
        field.set(1);
        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(!field.has_none());

        field.unset(1);
        field.set(2);
        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(!field.has_none());

        field.set(0);
        field.set(1);
        EXPECT_TRUE(field.has_all());
        EXPECT_TRUE(!field.has_none());

        field.set_has_none();
        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(field.has_none());

        field.set_has_all();
        EXPECT_TRUE(field.has_all());
        EXPECT_TRUE(!field.has_none());
    }

    {
        tr_bitfield field(0);

        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(field.has_none());

        field.set_has_none();
        EXPECT_TRUE(!field.has_all());
        EXPECT_TRUE(field.has_none());

        field.set_has_all();
        EXPECT_TRUE(field.has_all());
        EXPECT_TRUE(!field.has_none());
    }
}

TEST(Bitfield, deferredSize)
{
    auto constexpr Raw = std::to_array<std::byte>({ std::byte{ 0x80 } });
    auto constexpr Flags = std::to_array<bool>({ true });

    auto have_none = tr_bitfield{ 0 };
    EXPECT_FALSE(have_none.is_size_known());
    EXPECT_TRUE(have_none.has_none());
    EXPECT_FALSE(have_none.has_all());
    EXPECT_TRUE(have_none.is_valid());
    EXPECT_EQ(0U, have_none.count(0U, 1U));
    EXPECT_FLOAT_EQ(0.0F, have_none.percent());
    EXPECT_FALSE(have_none.set(0U));
    EXPECT_FALSE(have_none.set_span(0U, 1U));
    EXPECT_FALSE(have_none.set_raw(Raw));
    EXPECT_FALSE(have_none.set_from_bools(Flags));

    EXPECT_FALSE(have_none.init_size(0U));
    EXPECT_FALSE(have_none.is_size_known());
    EXPECT_TRUE(have_none.init_size(10U));
    EXPECT_TRUE(have_none.is_size_known());
    EXPECT_TRUE(have_none.has_none());
    EXPECT_EQ(10U, have_none.size());
    EXPECT_EQ(0U, have_none.count());
    EXPECT_TRUE(have_none.is_valid());

    auto have_all = tr_bitfield{ 0 };
    have_all.set_has_all();
    EXPECT_FALSE(have_all.is_size_known());
    EXPECT_TRUE(have_all.has_all());
    EXPECT_FALSE(have_all.has_none());
    EXPECT_TRUE(have_all.is_valid());
    EXPECT_EQ(10U, have_all.count(0U, 10U));
    EXPECT_EQ(15U, have_all.count(5U, 20U));
    EXPECT_FLOAT_EQ(1.0F, have_all.percent());

    EXPECT_TRUE(have_all.init_size(10U));
    EXPECT_TRUE(have_all.is_size_known());
    EXPECT_TRUE(have_all.has_all());
    EXPECT_EQ(10U, have_all.count());
    EXPECT_EQ(5U, have_all.count(5U, 20U));
    EXPECT_EQ((std::vector<std::byte>{ std::byte{ 0xff }, std::byte{ 0xc0 } }), have_all.raw());
    for (size_t i = 0U; i < have_all.size(); ++i) {
        EXPECT_TRUE(have_all.test(i));
    }

    EXPECT_FALSE(have_all.init_size(20U));
    EXPECT_EQ(10U, have_all.size());
    EXPECT_TRUE(have_all.is_valid());
}

TEST(Bitfield, shrinkTo)
{
    // shrinking an "unknown size" bitfield is never allowed
    auto unknown_size = tr_bitfield{ 0 };
    EXPECT_FALSE(unknown_size.shrink_to(0U));
    EXPECT_FALSE(unknown_size.shrink_to(1U));

    auto bf = tr_bitfield{ 20 };
    ASSERT_TRUE(bf.set_span(4U, 16U)); // bits [4,16) true, count == 12

    // growing is not allowed
    EXPECT_FALSE(bf.shrink_to(21U));
    EXPECT_EQ(20U, bf.size());

    // 0 is reserved for "unknown size", so shrinking to it fails even for a
    // bitfield with a known size and some true bits
    EXPECT_FALSE(bf.shrink_to(0U));
    EXPECT_EQ(20U, bf.size());
    EXPECT_TRUE(bf.is_valid());

    // shrinking to the current size is a no-op success
    EXPECT_TRUE(bf.shrink_to(20U));
    EXPECT_EQ(20U, bf.size());
    EXPECT_EQ(12U, bf.count());

    // shrinking drops bits beyond the new size and keeps the rest
    EXPECT_TRUE(bf.shrink_to(10U));
    EXPECT_EQ(10U, bf.size());
    EXPECT_EQ(6U, bf.count()); // bits [4,10) remain true; [10,16) are dropped
    for (size_t i = 0U; i < 10U; ++i) {
        EXPECT_EQ(i >= 4U, bf.test(i));
    }
    EXPECT_TRUE(bf.is_valid());

    // shrinking a have-all bitfield stays have-all at the new size
    auto have_all = tr_bitfield{ 20 };
    have_all.set_has_all();
    EXPECT_TRUE(have_all.shrink_to(10U));
    EXPECT_EQ(10U, have_all.size());
    EXPECT_TRUE(have_all.has_all());
    EXPECT_EQ(10U, have_all.count());
    EXPECT_TRUE(have_all.is_valid());

    // shrinking a have-none bitfield stays have-none at the new size
    auto have_none = tr_bitfield{ 20 };
    have_none.set_has_none();
    EXPECT_TRUE(have_none.shrink_to(10U));
    EXPECT_EQ(10U, have_none.size());
    EXPECT_TRUE(have_none.has_none());
    EXPECT_EQ(0U, have_none.count());
    EXPECT_TRUE(have_none.is_valid());

    // shrinking a partially set bitfield to a size that includes all the true bits makes it have-all
    auto promoted = tr_bitfield{ 20 };
    ASSERT_TRUE(promoted.set_span(0U, 15U));
    EXPECT_EQ(15U, promoted.count());
    EXPECT_FALSE(promoted.has_all());
    EXPECT_TRUE(promoted.shrink_to(15U));
    EXPECT_EQ(15U, promoted.size());
    EXPECT_EQ(15U, promoted.count());
    EXPECT_TRUE(promoted.has_all());
    EXPECT_EQ((std::vector<std::byte>{ std::byte{ 0xff }, std::byte{ 0xfe } }), promoted.raw());
    EXPECT_TRUE(promoted.is_valid());
}

TEST(Bitfield, shrinkToRecomputesTrueCount)
{
    // The true count after shrink_to() must reflect only the bits that
    // remain in the new, smaller range -- even when the pre-shrink count
    // happens to equal the new size, as it does here (count == 8, new
    // size == 8) while none of the surviving bits [0,8) are actually set.
    auto bf = tr_bitfield{ 16 };
    ASSERT_TRUE(bf.set_span(8U, 16U)); // bits [8,16) true, count == 8
    ASSERT_EQ(8U, bf.count());

    ASSERT_TRUE(bf.shrink_to(8U));
    EXPECT_EQ(8U, bf.size());
    EXPECT_FALSE(bf.has_all());
    EXPECT_TRUE(bf.has_none());
    EXPECT_EQ(0U, bf.count());
    for (size_t i = 0U; i < 8U; ++i) {
        EXPECT_FALSE(bf.test(i));
    }
    EXPECT_TRUE(bf.is_valid());
}

TEST(Bitfield, shrinkToAfterIndividualUnset)
{
    // has_none() can become true either via set_has_none() or by set()
    // clearing the last true bit. shrink_to() must produce a correctly
    // sized buffer either way.
    auto bf = tr_bitfield{ 16 };
    ASSERT_TRUE(bf.set(15U));
    ASSERT_TRUE(bf.set(15U, false));
    ASSERT_TRUE(bf.has_none());

    ASSERT_TRUE(bf.shrink_to(8U));
    EXPECT_EQ(8U, bf.size());
    EXPECT_TRUE(bf.has_none());
    EXPECT_EQ(0U, bf.count());
    EXPECT_TRUE(bf.is_valid());

    auto const raw = bf.raw();
    EXPECT_EQ(1U, std::size(raw)); // sized for the new bit count, not the old one
    EXPECT_EQ(std::byte{}, raw[0]);
}

TEST(Bitfield, percent)
{
    auto field = tr_bitfield{ 100 };
    field.set_has_all();
    EXPECT_NEAR(1.0F, field.percent(), 0.01);

    field.set_has_none();
    EXPECT_NEAR(0.0F, field.percent(), 0.01);

    field.set_span(0, std::size(field) / 2U);
    EXPECT_NEAR(0.5F, field.percent(), 0.01);

    field.set_has_none();
    field.set_span(0, std::size(field) / 4U);
    EXPECT_NEAR(0.25F, field.percent(), 0.01);
}

TEST(Bitfield, bitwiseOr)
{
    auto a = tr_bitfield{ 100 };
    auto b = tr_bitfield{ 100 };

    a.set_has_all();
    b.set_has_none();
    a |= b;
    EXPECT_TRUE(a.has_all());

    a.set_has_none();
    b.set_has_all();
    a |= b;
    EXPECT_TRUE(a.has_all());

    a.set_has_none();
    b.set_has_none();
    a |= b;
    EXPECT_TRUE(a.has_none());

    a.set_has_none();
    b.set_has_none();
    a.set_span(0, std::size(a) / 2U);
    b.set_span(std::size(a) / 2U, std::size(a));
    EXPECT_EQ(0.5, a.percent());
    EXPECT_EQ(0.5, b.percent());
    a |= b;
    EXPECT_EQ(1.0, a.percent());
    EXPECT_TRUE(a.has_all());

    a.set_has_none();
    b.set_has_none();
    for (size_t i = 0; i < std::size(a); ++i) {
        if ((i % 2U) != 0U) {
            a.set(i);
        } else {
            b.set(i);
        }
    }
    EXPECT_NEAR(0.5F, a.percent(), 0.01);
    EXPECT_NEAR(0.5F, b.percent(), 0.01);
    a |= b;
    EXPECT_TRUE(a.has_all());
}

TEST(Bitfield, bitwiseOperationsWithDifferentSizes)
{
    auto smaller = tr_bitfield{ 4 };
    auto larger = tr_bitfield{ 12 };

    ASSERT_TRUE(smaller.set(0U));
    ASSERT_TRUE(larger.set(4U));
    ASSERT_TRUE(larger.set(11U));
    smaller |= larger;

    EXPECT_EQ(12U, smaller.size());
    EXPECT_EQ(3U, smaller.count());
    EXPECT_TRUE(smaller.test(0U));
    EXPECT_TRUE(smaller.test(4U));
    EXPECT_TRUE(smaller.test(11U));
    EXPECT_TRUE(smaller.is_valid());
}
