// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::copy_n()
#include <array>
#include <cstddef> // size_t, std::byte
#include <memory> // std::unique_ptr
#include <span>
#include <string_view>

#include "libtransmission/digest.h"
#include "libtransmission/tr-arc4.h"

// Spec: https://wiki.vuze.com/w/Message_Stream_Encryption
namespace tr_message_stream_encryption
{

/**
 * Holds state for the Diffie-Hellman key exchange that takes place
 * during encrypted peer handshakes
 */
class DH
{
public:
    // MSE spec: "Minimum length [for the private key] is 128 bit.
    // Anything beyond 180 bit is not believed to add any further
    // security and only increases the necessary calculation time.
    // You should use a length of 160bits whenever possible[.]"
    static auto constexpr PrivateKeySize = size_t{ 20 };

    // MSE spec: "P, S [the shared secret], Ya and Yb
    // [the public keys] are 768bits long[.]"
    static auto constexpr KeySize = size_t{ 96 };

    // big-endian byte arrays holding the keys and shared secret.
    // MSE spec: "The entire handshake is in big-endian."
    using private_key_bigend_t = std::array<std::byte, PrivateKeySize>;
    using key_bigend_t = std::array<std::byte, KeySize>;

    // In actual code, a private key is randomly generated.
    // Providing a predefined one is useful for reproducible unit tests.
    constexpr explicit DH(private_key_bigend_t const& private_key) noexcept
        : private_key_{ private_key }
    {
    }

    // Returns our own public key to be shared with a peer.
    [[nodiscard]] key_bigend_t publicKey() noexcept;

    // Compute the shared secret from our private key and the peer's public key.
    void setPeerPublicKey(key_bigend_t const& peer_public_key) noexcept;

    // Returns the shared secret.
    [[nodiscard]] constexpr auto const& secret() const noexcept
    {
        return secret_;
    }

    [[nodiscard]] static private_key_bigend_t randomPrivateKey() noexcept;

private:
    private_key_bigend_t private_key_ = {};
    key_bigend_t public_key_ = {};
    key_bigend_t secret_ = {};
};

// --- arc4 encryption for both incoming and outgoing stream
// Cipher state is allocated when the first direction is initialized,
// so plaintext peers do not carry the tables.
class Filter
{
public:
    Filter() = default;
    ~Filter() = default;

    // Not movable: a moved-from Filter would pass bytes through unencrypted.
    Filter(Filter&&) = delete;
    Filter(Filter const&) = delete;
    Filter& operator=(Filter&&) = delete;
    Filter& operator=(Filter const&) = delete;

    void decrypt_init(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash);
    constexpr void decrypt_disable() noexcept
    {
        disable(&Ciphers::dec);
    }

    template<typename T>
    constexpr void decrypt(T const* buf_in, size_t buf_len, T* buf_out) noexcept
    {
        process(buf_in, buf_len, buf_out, active_cipher(&Ciphers::dec));
    }

    void encrypt_init(bool is_incoming, DH const& dh, tr_sha1_digest_t const& info_hash);
    constexpr void encrypt_disable() noexcept
    {
        disable(&Ciphers::enc);
    }

    template<typename T>
    constexpr void encrypt(T const* buf_in, size_t buf_len, T* buf_out) noexcept
    {
        process(buf_in, buf_len, buf_out, active_cipher(&Ciphers::enc));
    }

    [[nodiscard]] constexpr auto is_active() const noexcept
    {
        return ciphers_ != nullptr;
    }

    // Encrypts src into dst with a cipher keyed as encrypt_init() would key it,
    // without allocating or keeping any state.
    static void encrypt_once(
        bool is_incoming,
        DH const& dh,
        tr_sha1_digest_t const& info_hash,
        std::span<std::byte const> src,
        std::span<std::byte> dst);

private:
    struct Direction {
        tr_arc4 cipher;
        bool active = false;
    };

    // Both directions share one allocation; it exists while either is active.
    struct Ciphers {
        Direction dec;
        Direction enc;
    };

    using DirectionMember = Direction Ciphers::*;

    void init(DirectionMember direction, std::string_view key, DH const& dh, tr_sha1_digest_t const& info_hash);

    constexpr void disable(DirectionMember direction) noexcept
    {
        if (ciphers_) {
            ((*ciphers_).*direction).active = false;
            if (!ciphers_->dec.active && !ciphers_->enc.active) {
                ciphers_.reset();
            }
        }
    }

    // null when this direction is not encrypted
    [[nodiscard]] constexpr tr_arc4* active_cipher(DirectionMember direction) noexcept
    {
        if (!ciphers_) {
            return nullptr;
        }
        auto& dir = (*ciphers_).*direction;
        return dir.active ? &dir.cipher : nullptr;
    }

    // arc4 is null when this direction is not encrypted
    template<typename T>
    static constexpr void process(T const* buf_in, size_t buf_len, T* buf_out, tr_arc4* arc4) noexcept
    {
        if (buf_in == nullptr || buf_out == nullptr) {
            if (arc4 != nullptr) {
                arc4->discard(buf_len);
            }
        } else if (arc4 != nullptr) {
            arc4->process(std::as_bytes(std::span{ buf_in, buf_len }), std::as_writable_bytes(std::span{ buf_out, buf_len }));
        } else if (buf_in != buf_out) {
            std::copy_n(buf_in, buf_len, buf_out);
        }
    }

    std::unique_ptr<Ciphers> ciphers_;
};

} // namespace tr_message_stream_encryption
