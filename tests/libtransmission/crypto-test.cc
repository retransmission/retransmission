// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::ranges::equal
#include <array>
#include <cstddef> // std::byte, size_t
#include <cstdint> // uint8_t
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

#include <gtest/gtest.h>

#include <libtransmission/peer-mse.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/macros.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/tr-arc4.h>

using namespace std::literals;

namespace
{

auto constexpr SomeHash = tr_sha1_digest_t{
    std::byte{ 0 },  std::byte{ 1 },  std::byte{ 2 },  std::byte{ 3 },  std::byte{ 4 },  std::byte{ 5 },  std::byte{ 6 },
    std::byte{ 7 },  std::byte{ 8 },  std::byte{ 9 },  std::byte{ 10 }, std::byte{ 11 }, std::byte{ 12 }, std::byte{ 13 },
    std::byte{ 14 }, std::byte{ 15 }, std::byte{ 16 }, std::byte{ 17 }, std::byte{ 18 }, std::byte{ 19 },
};

template<size_t N>
[[nodiscard]] std::string toString(std::array<std::byte, N> const& array)
{
    auto ostr = std::ostringstream{};
    ostr << '[';
    for (auto const b : array) {
        ostr << static_cast<unsigned>(b) << ' ';
    }
    ostr << ']';
    return ostr.str();
}

using MseDh = tr_message_stream_encryption::DH;

// big-endian; short values get left-padded with zeroes
template<size_t N>
[[nodiscard]] constexpr std::array<std::byte, N> fromHex(std::string_view const hex)
{
    auto ret = std::array<std::byte, N>{};
    for (size_t i = 0; i < std::size(hex); ++i) {
        auto const ch = hex[std::size(hex) - 1 - i];
        auto const nibble = ch <= '9' ? ch - '0' : ch - 'a' + 10;
        ret[N - 1 - (i / 2)] |= std::byte(nibble << (i % 2 * 4));
    }
    return ret;
}

// the MSE spec's P and its neighbors
auto constexpr Prime =
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404ddef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a63a36210000000000090563"sv;
auto constexpr PrimeMinus1 =
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404ddef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a63a36210000000000090562"sv;
auto constexpr PrimePlus1 =
    "ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd129024e088a67cc74020bbea63b139b22514a08798e3404ddef9519b3cd3a431b302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a63a36210000000000090564"sv;

auto constexpr KeyMax =
    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"sv;
auto constexpr KeyTopBit =
    "800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"sv;
auto constexpr KeyA =
    "eafcb7a4f1fb1fd2ce8238033f05081f74becf0e7dae35f34c0461cf14f0ccb3fd6dfededce2213b4c0bb896a25305e86ba90b2b871070c20a846b4a2928534fcf1231e68e6c64ebcb47a64e0aee0f248b79399e0d6599336bd315387b19cbe7"sv;
auto constexpr KeyB =
    "a1bf7312296cb906cb211a9531294e528e7e224fa58c92cff3f5678b2f446ec86edc9d60fda4a69d7442eb4ed1b972c2d72283d9f4d5a26004c40d010af7076960acdc329c3a816cf77aeb224808066e23aea17aa643212b0f7bd1275421435c"sv;
auto constexpr KeyC =
    "d3b1e1151abcd5429b7f4a83b29d6b8158d54e36158746ec8aafa657a33b09ea3c721ab0eedcadc0c87b734a7524047bfb2d0e088c562d1526dcb5b3d63ee805f58c88b8776bd06801a0213ff92c8bce8d7d66bad181e8051fee429ff08e9cc2"sv;
auto constexpr KeyD =
    "b7bf9b7f71afa25a93e862d3d23455f802f955e126ed2e1acc4712fea4763584287aceaf19203a6f6cd0989e6b91550494899999fc929f75cb2d43ad8b86ec58ab8c7665ea67bc84be4e78eaf19fd84ef1f85275671df957589c726329870502"sv;

auto constexpr PrivateKeyMax = "ffffffffffffffffffffffffffffffffffffffff"sv;
auto constexpr PrivateKeyTopBit = "8000000000000000000000000000000000000000"sv;
auto constexpr PrivateKeyA = "9d62529daed0236d6b30b1275bee6d78d00131ab"sv;
auto constexpr PrivateKeyB = "9c1bf1fe4b2d5dc25c3e39db46f11ccc743721aa"sv;

} // namespace

TEST(Crypto, DH)
{
    auto a = tr_message_stream_encryption::DH{ tr_message_stream_encryption::DH::randomPrivateKey() };
    auto b = tr_message_stream_encryption::DH{ tr_message_stream_encryption::DH::randomPrivateKey() };

    a.setPeerPublicKey(b.publicKey());
    b.setPeerPublicKey(a.publicKey());
    EXPECT_EQ(toString(a.secret()), toString(b.secret()));
    EXPECT_EQ(a.secret(), b.secret());
    EXPECT_EQ(96U, std::size(a.secret()));

    auto c = tr_message_stream_encryption::DH{ tr_message_stream_encryption::DH::randomPrivateKey() };
    c.setPeerPublicKey(b.publicKey());
    EXPECT_NE(a.secret(), c.secret());
    EXPECT_NE(toString(a.secret()), toString(c.secret()));
}

TEST(Crypto, dhPublicKeyKnownAnswers)
{
    struct LocalTest {
        std::string_view private_key;
        std::string_view public_key;
    };

    // public_key is Python's pow(2, private_key, P)
    auto constexpr Tests = std::to_array<LocalTest>({
        { .private_key = "0"sv, .public_key = "1"sv },
        { .private_key = "1"sv, .public_key = "2"sv },
        { .private_key = "2"sv, .public_key = "4"sv },
        { .private_key = "3"sv, .public_key = "8"sv },
        // largest power of two below P
        { .private_key = "2ff"sv, .public_key = KeyTopBit },
        // smallest power of two above P
        { .private_key = "300"sv,
          .public_key =
              "36f0255dde973dcb3b399d747f23e32ed6fdb1f77598338bfdf44159c4ec64ddaeb5f78671cbfb22106ae64c32c5bce4cfd4f5920da0ebc8b01eca9292ae3dba1b7a4a899da181390bb3bd1659c5c9defffffffffff6fa9d"sv },
        { .private_key = PrivateKeyTopBit,
          .public_key =
              "1cecf622140e972f71f40f88de300149f19737f10dfa546c1548c66c9214f99ce7e43541e8a21b07b04a278840e319739f8da70c8b7d4c568c6ef9ce893cdb85deef6ce88ce49b8843b929e0ff9530cb876526d8390b6b68a05c618c17bb70b9"sv },
        { .private_key = PrivateKeyMax,
          .public_key =
              "fe9aac142f64ad4d5bca7e30bea17d62c709de9e4c5694e2b27e8c6ed82510bb57952fcce38f6a5084b9699ec34b033ed1a0dbd51d70274bff40ae493f0ce2cdbd3cbd6055c9677a7eb70d29f4d8ecfbc2625316a7ae1a0d3d6605f0406bad79"sv },
        // public key needs left-padding
        { .private_key = "a37ae097c712def9fce1edbf5879452652ff85cb"sv,
          .public_key =
              "55c6073493a8bf424e3d190572d80c9d33c37cf6ce4b81052b8bcb580767a3de6f7418aa3c431d6fb0d083e9032d5982f57425b2179b94beab8314c82825eeff11803e48bb1a8740d8db02f06988fc34e9bc7bc62a58b77e5c66629600c520"sv },
        { .private_key = PrivateKeyA,
          .public_key =
              "c8221aac9244d0ea4d0cf2b19b457bb76cc1138686552d9405f4a4619f358e3090906841f3bf4b19280000f79f1fedd3bb336a77e15ef74371abb7688965ad42ef8183ea8796d68a975492a05b8cd21e969979c49e205bec5b7cff64a05e6fab"sv },
        { .private_key = PrivateKeyB,
          .public_key =
              "66a7c92f8716f3e0ec3af586071dfcc36d97a00bde6360b9f1041b94785d3c6e956468413ce879f4b15c25c1c56c8ff4dfd36a9b37dfb6f52769023875254782e7a7984ef7272643a23e38adcbe9dd8d6a5b83c2ed5de6906b5ecace8c05d6d1"sv },
    });

    for (auto const& [private_key, public_key] : Tests) {
        auto dh = MseDh{ fromHex<MseDh::PrivateKeySize>(private_key) };
        EXPECT_EQ(tr_hash_string{ fromHex<MseDh::KeySize>(public_key) }, tr_hash_string{ dh.publicKey() })
            << "private_key " << private_key;
    }
}

TEST(Crypto, dhSecretKnownAnswers)
{
    struct LocalTest {
        std::string_view private_key;
        std::string_view peer_public_key;
        std::string_view secret;
    };

    // secret is Python's pow(peer_public_key, private_key, P)
    auto constexpr Tests = std::to_array<LocalTest>({
        { .private_key = PrivateKeyA, .peer_public_key = "0"sv, .secret = "0"sv },
        { .private_key = "0"sv, .peer_public_key = "0"sv, .secret = "1"sv },
        { .private_key = PrivateKeyA, .peer_public_key = "1"sv, .secret = "1"sv },
        { .private_key = "0"sv, .peer_public_key = KeyA, .secret = "1"sv },
        { .private_key = "1"sv, .peer_public_key = KeyA, .secret = KeyA },
        { .private_key = "1"sv,
          .peer_public_key = KeyMax,
          .secret =
              "36f0255dde973dcb3b399d747f23e32ed6fdb1f77598338bfdf44159c4ec64ddaeb5f78671cbfb22106ae64c32c5bce4cfd4f5920da0ebc8b01eca9292ae3dba1b7a4a899da181390bb3bd1659c5c9defffffffffff6fa9c"sv },
        { .private_key = "1"sv, .peer_public_key = Prime, .secret = "0"sv },
        { .private_key = "2"sv,
          .peer_public_key = KeyA,
          .secret =
              "eb37bb448783767b8c65a0d85c16e32f60f5b7b3cd999a34d1005d0c2cadfd665225cda76de3687f832a16d7209a3fd08e969c019bdc7349574c241dee8fb2c902d40cf3dfc6512780e4128b0ad7fd688f646c14bfd54fb29dfbad93313bbdc"sv },
        { .private_key = "2"sv,
          .peer_public_key = KeyMax,
          .secret =
              "a6130c9d54da3d531f1fd1fdcf40ad3132e9d3e2c71fcc0901c0a44669b7f7cb37793d3fb05d168fb3c0f6559f4d2105b43fe5a46f216d13947a01ca129a89c7748abc43843df9ab36aa8246586c6b11e7a5287724ad35c795f01940463a2555"sv },
        { .private_key = "2"sv, .peer_public_key = PrimeMinus1, .secret = "1"sv },
        { .private_key = "3"sv, .peer_public_key = PrimeMinus1, .secret = PrimeMinus1 },
        { .private_key = PrivateKeyA, .peer_public_key = PrimeMinus1, .secret = PrimeMinus1 },
        { .private_key = PrivateKeyA, .peer_public_key = Prime, .secret = "0"sv },
        { .private_key = PrivateKeyA, .peer_public_key = PrimePlus1, .secret = "1"sv },
        { .private_key = PrivateKeyA,
          .peer_public_key = KeyMax,
          .secret =
              "1dad01069ecf0d1fd4f1c87fe0ff0520aa5bdaa9ce4fcdd3a9f1308be96631aa5bb5e7ce0c5260eb7285ef4b841c77ec9069d5778f87b0f5ed09cd9df790732f832126891ebd940acc9f87626a3f2a9ab5cc89eb8ed7ed006d80d1d0e4e0e292"sv },
        { .private_key = PrivateKeyMax,
          .peer_public_key = KeyMax,
          .secret =
              "94ac2e467a831931b23abe2f13dbb8055ffa1f513758f457546d275e90caeae4cf07358ea7dcfe3efe3a504627aad77aa09860721335890554e33fbf530395fb54ad738679b467f4b10f26ecc006cf182bbc2692f2cc6395bcf2617cf079cac9"sv },
        // peer key needs left-padding
        { .private_key = PrivateKeyA,
          .peer_public_key = "82a21898a3524ebd"sv,
          .secret =
              "c2e2bb00bf140476dda1dbda9c0eb4889807a880c23ed8da822843473062b5ac9beb184c7b3ab1984c102cab402b71431158386fed9bfbe88450e006e6ad72fbd083f6b7764d8ede9d681da671418eec3199d163ef8c5fb0c2ac944cc9d53bd5"sv },
        { .private_key = PrivateKeyA,
          .peer_public_key = KeyTopBit,
          .secret =
              "df8c2f11ce87b8ca5689af36bdb5e02eedf360eb3c6dfe6d2c7d3da06d9a7f7fb94c424e6d00d1fabfe5eb3afc51ddfa68d4f13a5539965d457b307b2352f0a17b90d626becf32014d1e76a6fa6185544d527d286f62ded7d2d2c0ac1969b7a7"sv },
        // secret needs left-padding
        { .private_key = PrivateKeyA,
          .peer_public_key =
              "f2f42dc295187ea7157c19235add52a89c884f70e8cea4542c7c5263ff8e4ac058448b36b00500880ba39df914fe015051558d7bcd1304a279ed455f373ac8f49f74a2cea11caf4c2cfb6e11210d967f5ace8fb3780d1bcd11de75b986228579"sv,
          .secret =
              "d3b7f69eabeee3c9760d76698d95a354d1d6ffed2f257eb6cf1793ff10a010b87f09211ac4743ad8feddd9e56e16da79ed5c1bf8927107980ce5b5c89c3371e11dfc095654b0783b5e6f9d884f3ac989dc3acd7709b23dc38f5f938498f525"sv },
        { .private_key = PrivateKeyA,
          .peer_public_key = KeyB,
          .secret =
              "7b881e507c068c961f73ad95d39d2f8be97736b18ca4dea9972856ec11a4ca7fbfd7d7ba4e24cd67d50461e6ee9c12fe74c358f9a2151a6061cd63930dbab43127846a6eaa6fe1739b535ff7da811f1dc6e0d58c8671e1c2caa24be5534a5d30"sv },
        { .private_key = PrivateKeyB,
          .peer_public_key = KeyC,
          .secret =
              "850c4c3a861cfe5e1eec5feff4081f66b2555ded84af70e80e57228f0a4b64f86a3b87b8354750340be1f725109c66d0b85e03d8ac512eab01df404b6bfcf2b21de2d695e821e235bc8f1dd46a969ac2505f7b3b69fe6c94d86506453182044c"sv },
        { .private_key = PrivateKeyMax,
          .peer_public_key = KeyD,
          .secret =
              "cf4c5ffbf6879adeead1f3eaf157e57a7ed31c3e399205a42ae789e050995557ee9eee6f0d70f2db3313fae4a895a58d7f1d6dbcdf678f1cf417ae4e13619eec8e62fcd4dfd06b019980302f54bdc7fe3210b924269287d8f01c2c42b5543fbd"sv },
        { .private_key = PrivateKeyTopBit,
          .peer_public_key = KeyD,
          .secret =
              "4e5349121564181cc1ee3007f9421906d4cc06289e940868bbebfbd7d1e8b9223a9ba242f427b1505d5231eb3d7f402827e8899ab3e7cea6780661aa548c3a29857e54157a3f52463d17da578a301ec74e581e59b27430ceff625e63727d6b6b"sv },
    });

    for (auto const& [private_key, peer_public_key, secret] : Tests) {
        auto dh = MseDh{ fromHex<MseDh::PrivateKeySize>(private_key) };
        dh.setPeerPublicKey(fromHex<MseDh::KeySize>(peer_public_key));
        EXPECT_EQ(tr_hash_string{ fromHex<MseDh::KeySize>(secret) }, tr_hash_string{ dh.secret() })
            << "private_key " << private_key << " peer_public_key " << peer_public_key;
    }
}

TEST(Crypto, encryptDecrypt)
{
    auto a_dh = tr_message_stream_encryption::DH{ tr_message_stream_encryption::DH::randomPrivateKey() };
    auto b_dh = tr_message_stream_encryption::DH{ tr_message_stream_encryption::DH::randomPrivateKey() };

    a_dh.setPeerPublicKey(b_dh.publicKey());
    b_dh.setPeerPublicKey(a_dh.publicKey());

    auto constexpr Input1 = "test1"sv;
    auto encrypted1 = std::array<char, 128>{};
    auto decrypted1 = std::array<char, 128>{};

    auto a = tr_message_stream_encryption::Filter{};
    a.encrypt_init(false, a_dh, SomeHash);
    a.encrypt(std::data(Input1), std::size(Input1), std::data(encrypted1));
    auto b = tr_message_stream_encryption::Filter{};
    b.decrypt_init(true, b_dh, SomeHash);
    b.decrypt(std::data(encrypted1), std::size(Input1), std::data(decrypted1));
    EXPECT_EQ(Input1, std::data(decrypted1)) << "Input1 " << Input1 << " decrypted1 " << std::data(decrypted1);

    auto constexpr Input2 = "@#)C$@)#(*%bvkdjfhwbc039bc4603756VB3)"sv;
    auto encrypted2 = std::array<char, 128>{};
    auto decrypted2 = std::array<char, 128>{};

    b.encrypt_init(true, b_dh, SomeHash);
    b.encrypt(std::data(Input2), std::size(Input2), std::data(encrypted2));
    a.decrypt_init(false, a_dh, SomeHash);
    a.decrypt(std::data(encrypted2), std::size(Input2), std::data(decrypted2));
    EXPECT_EQ(Input2, std::data(decrypted2)) << "Input2 " << Input2 << " decrypted2 " << std::data(decrypted2);
}

TEST(Crypto, arc4Rfc6229)
{
    // Independent keystream vectors, including MSE's 1024-byte discard:
    // https://www.rfc-editor.org/rfc/rfc6229#section-2
    auto constexpr Key = std::array{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }, std::byte{ 5 } };
    struct Vector {
        size_t offset;
        std::string_view bytes;
    };
    auto constexpr Vectors = std::array{
        Vector{ .offset = 0U, .bytes = "\xb2\x39\x63\x05\xf0\x3d\xc0\x27\xcc\xc3\x52\x4a\x0a\x11\x18\xa8"sv },
        Vector{ .offset = 256U, .bytes = "\x1c\xfc\xf6\x2b\x03\xed\xdb\x64\x1d\x77\xdf\xcf\x7f\x8d\x8c\x93"sv },
        Vector{ .offset = 1024U, .bytes = "\x30\xab\xbc\xc7\xc2\x0b\x01\x60\x9f\x23\xee\x2d\x5f\x6b\xb7\xdf"sv },
        Vector{ .offset = 4096U, .bytes = "\xff\x25\xb5\x89\x95\x99\x67\x07\xe5\x1f\xbd\xf0\x8b\x34\xd8\x75"sv },
    };
    for (auto const& [offset, expected] : Vectors) {
        auto cipher = tr_arc4{};
        cipher.init(Key);
        cipher.discard(offset);
        auto output = std::array<char, 16>{};
        cipher.process(std::as_bytes(std::span{ output }), std::as_writable_bytes(std::span{ output }));
        EXPECT_EQ(expected, std::string_view(std::data(output), std::size(output))) << offset;
    }

    // The same vectors from a single process() call, whose 8-bit index wraps many times
    auto stream = std::array<char, 4096U + 16U>{};
    auto cipher = tr_arc4{};
    cipher.init(Key);
    cipher.process(std::as_bytes(std::span{ stream }), std::as_writable_bytes(std::span{ stream }));
    auto const stream_view = std::string_view{ std::data(stream), std::size(stream) };
    for (auto const& [offset, expected] : Vectors) {
        EXPECT_EQ(expected, stream_view.substr(offset, std::size(expected))) << offset;
    }
}

TEST(Crypto, arc4DiscardBetweenProcessCalls)
{
    auto input = std::array<std::byte, 600>{};
    for (size_t i = 0; i < std::size(input); ++i) {
        input[i] = static_cast<std::byte>(i % 256U);
    }
    auto whole = tr_arc4{};
    auto split = tr_arc4{};
    for (auto* const cipher : { &whole, &split }) {
        cipher->init(SomeHash);
    }

    auto expected = input;
    whole.process(expected, expected);

    // process, skip some bytes, process the rest
    auto constexpr Head = size_t{ 251U };
    auto constexpr Skipped = size_t{ 101U };
    auto actual = input;
    auto const actual_span = std::span<std::byte>{ actual };
    split.process(actual_span.first(Head), actual_span.first(Head));
    split.discard(Skipped);
    split.process(actual_span.subspan(Head + Skipped), actual_span.subspan(Head + Skipped));

    auto const expected_span = std::span<std::byte const>{ expected };
    EXPECT_TRUE(std::ranges::equal(expected_span.first(Head), actual_span.first(Head)));
    EXPECT_TRUE(std::ranges::equal(expected_span.subspan(Head + Skipped), actual_span.subspan(Head + Skipped)));
}

TEST(Crypto, arc4ChunkedAndInPlace)
{
    auto input = std::array<std::byte, 4097>{};
    for (size_t i = 0; i < std::size(input); ++i) {
        input[i] = static_cast<std::byte>(i % 256U);
    }
    auto whole = tr_arc4{};
    auto chunked = tr_arc4{};
    auto decrypt = tr_arc4{};
    for (auto* const cipher : { &whole, &chunked, &decrypt }) {
        cipher->init(SomeHash);
        cipher->discard(1024U);
    }
    auto encrypted = decltype(input){};
    whole.process(input, encrypted);

    auto in_place = input;
    auto remaining = std::span<std::byte>{ in_place };
    for (auto const length : { 0U, 1U, 255U, 256U, 257U, 1024U, 2304U }) {
        auto const part = remaining.first(length);
        chunked.process(part, part);
        remaining = remaining.subspan(length);
    }
    ASSERT_TRUE(std::empty(remaining));
    EXPECT_EQ(encrypted, in_place);
    decrypt.process(in_place, in_place);
    EXPECT_EQ(input, in_place);
}

TEST(Crypto, sha1)
{
    auto hash1 = tr_sha1::digest("test"sv);
    EXPECT_EQ(
        0,
        memcmp(
            std::data(hash1),
            "\xa9\x4a\x8f\xe5\xcc\xb1\x9b\xa6\x1c\x4c\x08\x73\xd3\x91\xe9\x87\x98\x2f\xbb\xd3",
            std::size(hash1)));

    auto hash2 = tr_sha1::digest("test"sv);
    EXPECT_EQ(hash1, hash2);

    hash1 = tr_sha1::digest("1"sv, "22"sv, "333"sv);
    hash2 = tr_sha1::digest("1"sv, "22"sv, "333"sv);
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(
        0,
        memcmp(
            std::data(hash1),
            "\x1f\x74\x64\x8e\x50\xa6\xa6\x70\x8e\xc5\x4a\xb3\x27\xa1\x63\xd5\x53\x6b\x7c\xed",
            std::size(hash1)));

    auto const hash3 = tr_sha1::digest("test"sv);
    EXPECT_EQ("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"sv, tr_sha1_string{ hash3 });

    auto const hash4 = tr_sha1::digest("te"sv, "st"sv);
    EXPECT_EQ("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"sv, tr_sha1_string{ hash4 });

    auto const hash5 = tr_sha1::digest("t"sv, "e"sv, std::string{ "s" }, std::to_array<char>({ 't' }));
    EXPECT_EQ("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"sv, tr_sha1_string{ hash5 });
}

TEST(Crypto, ssha1)
{
    struct LocalTest {
        std::string_view plain_text;
        std::string_view ssha1;
    };

    static auto constexpr Tests = std::to_array<LocalTest>({
        { .plain_text = "test"sv, .ssha1 = "{15ad0621b259a84d24dcd4e75b09004e98a3627bAMbyRHJy"sv },
        { .plain_text = "QNY)(*#$B)!_X$B !_B#($^!)*&$%CV!#)&$C!@$(P*)"sv,
          .ssha1 = "{10e2d7acbb104d970514a147cd16d51dfa40fb3c0OSwJtOL"sv },
    });

    static auto constexpr HashCount = size_t{ 4U } * 1024U;

    for (auto const& [plain_text, ssha1] : Tests) {
        auto hashes = std::unordered_set<std::string>{};
        hashes.reserve(HashCount);

        EXPECT_TRUE(tr_ssha1_matches(ssha1, plain_text));

        for (size_t j = 0; j < HashCount; ++j) {
            auto const hash = tr_ssha1(plain_text);
            EXPECT_TRUE(tr_ssha1_matches(hash, plain_text));
            hashes.insert(hash);
        }

        // confirm all hashes are different
        EXPECT_EQ(HashCount, hashes.size());

        /* exchange two first chars */
        auto phrase = std::string{ plain_text };
        std::swap(phrase[0], phrase[1]);

        for (auto const& hash : hashes) {
            /* changed phrase doesn't match the hashes */
            EXPECT_FALSE(tr_ssha1_matches(hash, phrase));
        }
    }

    /* should work with different salt lengths as well */
    EXPECT_TRUE(tr_ssha1_matches("{a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", "test"));
    EXPECT_TRUE(tr_ssha1_matches("{d209a21d3bc4f8fc4f8faf347e69f3def597eb170pySy4ai1ZPMjeU1", "test"));
}

TEST(Crypto, hashString)
{
    // a default-constructed instance is the empty "no digest yet" state
    auto const empty = tr_sha1_string{};
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(0U, std::size(empty));
    EXPECT_EQ(""sv, empty.sv());
    EXPECT_EQ('\0', *empty.c_str());
    EXPECT_EQ(empty, tr_sha1_string{});

    auto const full = tr_sha1_string{ tr_sha1::digest("test"sv) };
    EXPECT_FALSE(full.empty());
    EXPECT_EQ(tr_sha1_string::Strlen, std::size(full));
    EXPECT_EQ("a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"sv, full);

    // an empty instance sorts before any complete one, as an empty string would
    EXPECT_LT(empty, full);
}

TEST(Crypto, sha1FromString)
{
    // bad lengths
    EXPECT_FALSE(tr_sha1_from_string(""));
    EXPECT_FALSE(tr_sha1_from_string("a94a8fe5ccb19ba61c4c0873d391e987982fbbd"sv));
    EXPECT_FALSE(tr_sha1_from_string("a94a8fe5ccb19ba61c4c0873d391e987982fbbd33"sv));
    // nonhex
    EXPECT_FALSE(tr_sha1_from_string("a94a8fe5ccb19ba61c4cz873d391e987982fbbd3"sv));
    EXPECT_FALSE(tr_sha1_from_string("a94a8fe5ccb19  61c4c0873d391e987982fbbd3"sv));

    // lowercase hex
    auto const baseline = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"sv;
    auto const lc = tr_sha1_from_string(baseline);
    ASSERT_TRUE(lc.has_value());
    EXPECT_EQ(baseline, tr_sha1_string{ *lc });

    // uppercase hex should yield the same result
    auto const uc = tr_sha1_from_string(tr_strupper(baseline));
    ASSERT_TRUE(uc.has_value());
    EXPECT_EQ(*lc, *uc);
}

TEST(Crypto, sha256FromString)
{
    // bad lengths
    EXPECT_FALSE(tr_sha256_from_string(""));
    EXPECT_FALSE(tr_sha256_from_string("a94a8fe5ccb19ba61c4c0873d391e987982fbbd"sv));
    EXPECT_FALSE(tr_sha256_from_string("a94a8fe5ccb19ba61c4c0873d391e987982fbbd33"sv));
    EXPECT_FALSE(tr_sha256_from_string("05d58dfd14ed21d33add137eb7a2c5d4ef5aaa4a945e654363d32b7c4bf5c92"sv));
    EXPECT_FALSE(tr_sha256_from_string("05d58dfd14ed21d33add137eb7a2c5d4ef5aaa4a945e654363d32b7c4bf5c9299"sv));
    // nonhex
    EXPECT_FALSE(tr_sha256_from_string("a94a8fe5ccb19ba61c4cz873d391e987982fbbd3aaaaaaaaaaaaaaaaaaaaaaa"sv));
    EXPECT_FALSE(tr_sha256_from_string("05  8dfd14ed21d33add137eb7a2c5d4ef5aaa4a945e654363d32b7c4bf5c92"sv));

    // lowercase hex
    auto const baseline = "05d58dfd14ed21d33add137eb7a2c5d4ef5aaa4a945e654363d32b7c4bf5c929"sv;
    auto const lc = tr_sha256_from_string(baseline);
    ASSERT_TRUE(lc.has_value());
    EXPECT_EQ(baseline, tr_sha256_string{ *lc });

    // uppercase hex should yield the same result
    auto const uc = tr_sha256_from_string(tr_strupper(baseline));
    ASSERT_TRUE(uc.has_value());
    EXPECT_EQ(*lc, *uc);
}

TEST(Crypto, random)
{
    /* test that tr_rand_int() stays in-bounds */
    for (int i = 0; i < 100000; ++i) {
        auto const val = tr_rand_int(100U);
        EXPECT_LE(0U, val);
        EXPECT_LT(val, 100U);
    }
}

using CryptoRandBufferTest = ::testing::TestWithParam<size_t>;

TEST_P(CryptoRandBufferTest, randBuf)
{
    static auto constexpr Iterations = 1000U;

    auto const width = GetParam();
    auto const empty = std::vector<std::byte>(width, std::byte{});

    auto buf = empty;

    for (size_t i = 0; i < Iterations; ++i) {
        auto tmp = buf;
        tr_rand_buffer(tmp);
        EXPECT_NE(tmp, empty);
        EXPECT_NE(tmp, buf);
        buf = tmp;
    }

    for (size_t i = 0; i < Iterations; ++i) {
        auto tmp = buf;
        EXPECT_TRUE(tr_rand_buffer_crypto(tmp));
        EXPECT_NE(tmp, empty);
        EXPECT_NE(tmp, buf);
        buf = tmp;
    }

    for (size_t i = 0; i < Iterations; ++i) {
        auto tmp = buf;
        tr_rand_buffer_std(tmp);
        EXPECT_NE(tmp, empty);
        EXPECT_NE(tmp, buf);
        buf = tmp;
    }
}

INSTANTIATE_TEST_SUITE_P(
    Crypto,
    CryptoRandBufferTest,
    ::testing::Values(32, 100, 1024, 3000),
    ::testing::PrintToStringParamName{});

TEST(Crypto, base64)
{
    auto raw = std::string_view{ "YOYO!"sv };
    auto encoded = tr_base64_encode(raw);
    EXPECT_EQ("WU9ZTyE="sv, encoded);
    EXPECT_EQ(raw, tr_base64_decode(encoded));

    EXPECT_EQ(""sv, tr_base64_encode(""sv));
    EXPECT_EQ(""sv, tr_base64_decode(""sv));

    static auto constexpr MaxBufSize = size_t{ 1024 };
    for (size_t i = 1; i <= MaxBufSize; ++i) {
        auto buf = std::string{};
        for (size_t j = 0; j < i; ++j) {
            buf += static_cast<char>(tr_rand_int(256U));
        }
        EXPECT_EQ(buf, tr_base64_decode(tr_base64_encode(buf)));

        buf = std::string{};
        for (size_t j = 0; j < i; ++j) {
            buf += static_cast<char>(1U + tr_rand_int(255U));
        }
        EXPECT_EQ(buf, tr_base64_decode(tr_base64_encode(buf)));
    }
}
