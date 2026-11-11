// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // std::byte, size_t
#include <cstdint>
#include <limits> // std::numeric_limits
#include <memory> // std::make_unique
#include <string_view>

#include "libtransmission/crypto-utils.h" // tr_sha1
#include "libtransmission/digest.h" // tr_sha1_digest_t
#include "libtransmission/peer-mse.h"
#include "libtransmission/tr-arc4.h"

using namespace std::literals;

namespace tr_message_stream_encryption
{
namespace
{

// --- Montgomery arithmetic mod P

#ifdef __SIZEOF_INT128__
using limb_t = uint64_t;
using wide_limb_t = unsigned __int128;
#else
using limb_t = uint32_t;
using wide_limb_t = uint64_t;
#endif

auto constexpr LimbBits = std::numeric_limits<limb_t>::digits;

// least significant limb first
using limbs_t = std::array<limb_t, DH::KeySize / sizeof(limb_t)>;

[[nodiscard]] consteval limbs_t from_hex(std::string_view const hex)
{
    auto constexpr NibblesPerLimb = 2U * sizeof(limb_t);

    auto ret = limbs_t{};
    for (size_t i = 0; i < std::size(hex); ++i) {
        auto const ch = hex[std::size(hex) - 1U - i];
        auto const nibble = static_cast<limb_t>(ch <= '9' ? ch - '0' : ch - 'A' + 10);
        ret[i / NibblesPerLimb] |= nibble << ((i % NibblesPerLimb) * 4U);
    }
    return ret;
}

// -1/n mod 2^LimbBits, by Newton's method
[[nodiscard]] consteval limb_t negative_inverse(limb_t const n)
{
    auto inverse = n; // correct to 3 bits
    for (auto i = 0; i < 5; ++i) {
        inverse *= limb_t{ 2 } - (n * inverse); // doubles the correct bits
    }
    return limb_t{} - inverse;
}

auto constexpr One = limbs_t{ 1U };
auto constexpr Generator = limbs_t{ 2U };
auto constexpr Prime = from_hex(
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A63A36210000000000090563"sv);
auto constexpr NegativePrimeInverse = negative_inverse(Prime[0]);

// 2^(2 * 768) mod P
auto constexpr RSquared = from_hex(
    "A6130C9D54DA3D538D001CB98C6F28C7A95D0ECBC5679266AFBC083554E85EE33361BFF33A35E04B112CE56282E51749D515B23CD4ACE6DD3423ECEE2DDC6158D4C85168A99A751F6D9F175993AF6D83FF0CA2A3D838C98595F0194046281A8E"sv);

// a * b / 2^768 mod P, if a or b is below P
// FIOS method from https://www.microsoft.com/en-us/research/wp-content/uploads/1996/01/j37acmon.pdf,
// with the a * b and m * P carries in separate variables instead of folding one into t with ADD()
[[nodiscard]] constexpr limbs_t multiply(limbs_t const& a, limbs_t const& b) noexcept
{
    auto t = limbs_t{};
    auto t_top = limb_t{};

    for (auto const a_limb : a) {
        // add a_limb * b and the multiple of P that zeroes the low limb, then drop that limb
        limb_t const m = (t[0] + (a_limb * b[0])) * NegativePrimeInverse;
        auto carry_ab = limb_t{};
        auto carry_mp = limb_t{};
        for (size_t j = 0; j < std::size(t); ++j) {
            auto const ab = (wide_limb_t{ a_limb } * b[j]) + t[j] + carry_ab;
            auto const mp = (wide_limb_t{ m } * Prime[j]) + static_cast<limb_t>(ab) + carry_mp;
            carry_ab = static_cast<limb_t>(ab >> LimbBits);
            carry_mp = static_cast<limb_t>(mp >> LimbBits);
            if (j > 0U) {
                t[j - 1U] = static_cast<limb_t>(mp);
            }
        }
        auto const top = wide_limb_t{ t_top } + carry_ab + carry_mp;
        t.back() = static_cast<limb_t>(top);
        t_top = static_cast<limb_t>(top >> LimbBits);
    }

    // t is below 2P, so one subtraction is enough
    auto reduced = limbs_t{};
    auto borrow = limb_t{};
    for (size_t j = 0; j < std::size(t); ++j) {
        auto const diff = wide_limb_t{ t[j] } - Prime[j] - borrow;
        reduced[j] = static_cast<limb_t>(diff);
        borrow = static_cast<limb_t>(diff >> LimbBits) & 1U;
    }
    return t_top != 0U || borrow == 0U ? reduced : t;
}

auto constexpr MontgomeryOne = multiply(One, RSquared);

// variable-time is fine: each private key gets at most two exponentiations, too few samples for a timing attack
// (publicKey() caches its result; tr_handshake recycles a DH only before setPeerPublicKey() runs)
[[nodiscard]] constexpr limbs_t powm(limbs_t const& base, DH::private_key_bigend_t const& exponent) noexcept
{
    auto const montgomery_base = multiply(base, RSquared);
    auto ret = MontgomeryOne;

    for (auto const byte : exponent) {
        for (auto mask = 0x80U; mask != 0U; mask >>= 1U) {
            ret = multiply(ret, ret);
            if ((std::to_integer<unsigned>(byte) & mask) != 0U) {
                ret = multiply(ret, montgomery_base);
            }
        }
    }

    return multiply(ret, One);
}

[[nodiscard]] constexpr limbs_t import_key(DH::key_bigend_t const& bigend) noexcept
{
    auto ret = limbs_t{};
    for (size_t i = 0; i < std::size(bigend); ++i) {
        auto const byte = std::to_integer<limb_t>(bigend[std::size(bigend) - 1U - i]);
        ret[i / sizeof(limb_t)] |= byte << ((i % sizeof(limb_t)) * 8U);
    }
    return ret;
}

[[nodiscard]] constexpr DH::key_bigend_t export_key(limbs_t const& limbs) noexcept
{
    auto ret = DH::key_bigend_t{};
    for (size_t i = 0; i < std::size(ret); ++i) {
        auto const limb = limbs[i / sizeof(limb_t)] >> ((i % sizeof(limb_t)) * 8U);
        ret[std::size(ret) - 1U - i] = std::byte{ static_cast<uint8_t>(limb) };
    }
    return ret;
}

} // namespace

// --- DH

[[nodiscard]] DH::private_key_bigend_t DH::randomPrivateKey() noexcept
{
    return tr_rand_obj<DH::private_key_bigend_t>();
}

DH::key_bigend_t DH::publicKey() noexcept
{
    if (public_key_ == key_bigend_t{}) {
        public_key_ = export_key(powm(Generator, private_key_));
    }

    return public_key_;
}

void DH::setPeerPublicKey(key_bigend_t const& peer_public_key) noexcept
{
    secret_ = export_key(powm(import_key(peer_public_key), private_key_));
}

// --- Filter

namespace
{
void init_cipher(tr_arc4& cipher, std::string_view key, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    cipher.init(tr_sha1::digest(key, dh.secret(), info_hash));
    cipher.discard(1024);
}

[[nodiscard]] constexpr auto decrypt_key(bool is_incoming) noexcept
{
    return is_incoming ? "keyA"sv : "keyB"sv;
}

[[nodiscard]] constexpr auto encrypt_key(bool is_incoming) noexcept
{
    return decrypt_key(!is_incoming);
}
} // namespace

void Filter::init(DirectionMember direction, std::string_view key, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    if (!ciphers_) {
        ciphers_ = std::make_unique<Ciphers>();
    }
    auto& dir = (*ciphers_).*direction;
    init_cipher(dir.cipher, key, dh, info_hash);
    dir.active = true;
}

void Filter::decrypt_init(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    init(&Ciphers::dec, decrypt_key(is_incoming), dh, info_hash);
}

void Filter::encrypt_init(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash)
{
    init(&Ciphers::enc, encrypt_key(is_incoming), dh, info_hash);
}

void Filter::encrypt_once(
    bool const is_incoming,
    DH const& dh,
    tr_sha1_digest_t const& info_hash,
    std::span<std::byte const> const src,
    std::span<std::byte> const dst)
{
    auto cipher = tr_arc4{};
    init_cipher(cipher, encrypt_key(is_incoming), dh, info_hash);
    cipher.process(src, dst);
}

} // namespace tr_message_stream_encryption
