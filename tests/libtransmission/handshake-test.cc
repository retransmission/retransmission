// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef> // size_t, std::byte
#include <cstdint> // uint8_t
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h> // write()
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_sha1, tr_sha1_from_string, tr_base...
#include <libtransmission/handshake.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-mse.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h> // tr_peerIdInit()
#include <libtransmission/string-utils.h>
#include <libtransmission/timer.h>
#include <libtransmission/tr-assert.h>
#include <libtransmission/macros.h>
#include <libtransmission/types.h>

#include "test-fixtures.h"

using namespace std::literals;

#define LOCAL_SOCKETPAIR_AF TR_IF_WIN32(AF_INET, AF_UNIX)

namespace tr::test
{

auto constexpr MaxWaitMsec = 5000;

class HandshakeTest : public SessionTest
{
public:
    class MediatorMock final : public tr_handshake::Mediator
    {
    public:
        explicit MediatorMock(tr_session* session)
            : session_{ session }
        {
        }

        [[nodiscard]] std::optional<TorrentInfo> torrent(tr_sha1_digest_t const& info_hash) const override
        {
            if (auto const iter = torrents.find(info_hash); iter != std::end(torrents)) {
                return iter->second;
            }

            return {};
        }

        [[nodiscard]] std::optional<TorrentInfo> torrent_from_obfuscated(tr_sha1_digest_t const& obfuscated) const override
        {
            for (auto const& [info_hash, info] : torrents) {
                if (obfuscated == tr_sha1::digest("req2"sv, info.info_hash)) {
                    return info;
                }
            }

            return {};
        }

        [[nodiscard]] tr::TimerMaker& timer_maker() override
        {
            return session_->timerMaker();
        }

        [[nodiscard]] bool allows_dht() const override
        {
            return false;
        }

        [[nodiscard]] size_t pad(void* setme, [[maybe_unused]] size_t maxlen) const override
        {
            TR_ASSERT(maxlen > 10);
            auto const len = size_t{ 10 };
            std::fill_n(static_cast<char*>(setme), 10, ' ');
            return len;
        }

        [[nodiscard]] tr_message_stream_encryption::DH::private_key_bigend_t private_key() const override
        {
            return private_key_;
        }

        void set_utp_failed(tr_sha1_digest_t const& /*info_hash*/, tr_socket_address const& /*socket_address*/) override
        {
        }

        void setPrivateKeyFromBase64(std::string_view b64)
        {
            auto const str = tr_base64_decode(b64);
            TR_ASSERT(std::size(str) == std::size(private_key_));
            std::copy_n(reinterpret_cast<std::byte const*>(std::data(str)), std::size(str), std::begin(private_key_));
        }

        tr_session* const session_;
        std::map<tr_sha1_digest_t, TorrentInfo> torrents;
        tr_message_stream_encryption::DH::private_key_bigend_t private_key_ = {};
    };

    template<typename Span>
    void sendToClient(evutil_socket_t sock, Span const& data)
    {
        auto const* walk = std::data(data);
        static_assert(sizeof(*walk) == 1);
        size_t len = std::size(data);

        while (len > 0) {
            auto const n = send(sock, reinterpret_cast<char const*>(walk), len, 0);
            TR_ASSERT(n >= 0);
            len -= n;
            walk += n;
        }
    }

    void sendB64ToClient(evutil_socket_t sock, std::string_view b64)
    {
        sendToClient(sock, tr_base64_decode(b64));
    }

    static auto constexpr ReservedBytesNoExtensions = std::to_array<uint8_t>({ 0, 0, 0, 0, 0, 0, 0, 0 });
    static auto constexpr PlaintextProtocolName = "\023BitTorrent protocol"sv;

    tr_socket_address const DefaultPeerSockAddr{ *tr_address::from_string("127.0.0.1"sv), tr_port::from_host(8080) };
    tr_handshake::Mediator::TorrentInfo const TorrentWeAreSeeding{
        .info_hash = tr_sha1::digest("abcde"sv),
        .client_peer_id = tr_peerIdInit(),
        .id = tr_torrent_id_t{ 100 },
        .is_done = true,
        .is_running = true,
    };
    tr_handshake::Mediator::TorrentInfo const UbuntuTorrent{
        .info_hash = *tr_sha1_from_string("2c6b6858d61da9543d4231a71db4b1c9264b0685"sv),
        .client_peer_id = tr_peerIdInit(),
        .id = tr_torrent_id_t{ 101 },
        .is_done = false,
        .is_running = true,
    };

    [[nodiscard]] auto createIncomingIo(tr_session* session)
    {
        auto sockpair = std::to_array<evutil_socket_t>({ -1, -1 });
        EXPECT_EQ(0, evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, std::data(sockpair))) << tr_strerror(errno);
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[1]));
        return std::pair{
            tr_peerIo::new_incoming(
                session,
                &session->top_bandwidth_,
                tr_peer_socket_tcp::create(*session, DefaultPeerSockAddr, static_cast<tr_socket_t>(sockpair[0]))),
            sockpair[1],
        };
    }

    [[nodiscard]] auto createOutgoingIo(tr_session* session, tr_sha1_digest_t const& info_hash)
    {
        auto sockpair = std::to_array<evutil_socket_t>({ -1, -1 });
        EXPECT_EQ(0, evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, std::data(sockpair))) << tr_strerror(errno);
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[1]));
        auto peer_io = tr_peerIo::create(
            session,
            tr_peer_socket_tcp::create(*session, DefaultPeerSockAddr, static_cast<tr_socket_t>(sockpair[0])),
            &session->top_bandwidth_,
            &info_hash,
            false /*incoming*/,
            false /*seed*/);
        return std::pair{ std::move(peer_io), sockpair[1] };
    }

    [[nodiscard]] static constexpr auto makePeerId(std::string_view sv)
    {
        auto peer_id = tr_peer_id_t{};
        for (size_t i = 0, n = std::size(sv); i < n; ++i) {
            peer_id[i] = sv[i];
        }
        return peer_id;
    }

    [[nodiscard]] static auto makeRandomPeerId()
    {
        auto peer_id = tr_rand_obj<tr_peer_id_t>();
        static auto constexpr PeerIdPrefix = "-UW110Q-"sv;
        std::ranges::copy(PeerIdPrefix, std::begin(peer_id));
        return peer_id;
    }

    [[nodiscard]] static auto startHandshake(
        std::optional<tr_handshake::Result>& result,
        tr_handshake::Mediator* mediator,
        std::shared_ptr<tr_peerIo> const& peer_io,
        tr_encryption_mode encryption_mode = TR_CLEAR_PREFERRED)
    {
        return std::make_unique<tr_handshake>(mediator, peer_io, encryption_mode, [&result](auto const& resin) {
            result = resin;
            return true;
        });
    }
};

TEST_F(HandshakeTest, incomingPlaintext)
{
    auto const peer_id = makeRandomPeerId();
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(TorrentWeAreSeeding.info_hash, TorrentWeAreSeeding);

    // The simplest handshake there is. "The handshake starts with character
    // nineteen (decimal) followed by the string 'BitTorrent protocol'.
    // The leading character is a length prefix[.]. After the fixed headers
    // come eight reserved bytes, which are all zero in all current
    // implementations[.] Next comes the 20 byte sha1 hash of the bencoded
    // form of the info value from the metainfo file[.] After the download
    // hash comes the 20-byte peer id which is reported in tracker requests
    // and contained in peer lists in tracker responses."
    // https://www.bittorrent.org/beps/bep_0052.html
    auto [io, sock] = createIncomingIo(session_);
    sendToClient(sock, PlaintextProtocolName);
    sendToClient(sock, ReservedBytesNoExtensions);
    sendToClient(sock, TorrentWeAreSeeding.info_hash);
    sendToClient(sock, peer_id);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(peer_id, res->peer_id);
    EXPECT_EQ(TorrentWeAreSeeding.info_hash, io->torrent_hash());

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

// The datastream is identical to HandshakeTest.incomingPlaintext,
// but this time we don't recognize the infohash sent by the peer.
TEST_F(HandshakeTest, incomingPlaintextUnknownInfoHash)
{
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(TorrentWeAreSeeding.info_hash, TorrentWeAreSeeding);

    auto [io, sock] = createIncomingIo(session_);
    sendToClient(sock, PlaintextProtocolName);
    sendToClient(sock, ReservedBytesNoExtensions);
    sendToClient(sock, tr_sha1::digest("some other torrent unknown to us"sv));
    sendToClient(sock, makeRandomPeerId());

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(io, res->io);
    EXPECT_FALSE(res->peer_id);
    EXPECT_EQ(tr_sha1_digest_t{}, io->torrent_hash());

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

TEST_F(HandshakeTest, outgoingPlaintext)
{
    auto const peer_id = makeRandomPeerId();
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, TorrentWeAreSeeding);

    auto [io, sock] = createOutgoingIo(session_, UbuntuTorrent.info_hash);
    sendToClient(sock, PlaintextProtocolName);
    sendToClient(sock, ReservedBytesNoExtensions);
    sendToClient(sock, UbuntuTorrent.info_hash);
    sendToClient(sock, peer_id);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(peer_id, res->peer_id);
    EXPECT_EQ(UbuntuTorrent.info_hash, io->torrent_hash());
    EXPECT_EQ(tr_sha1_string{ UbuntuTorrent.info_hash }, tr_sha1_string{ io->torrent_hash() });

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

TEST_F(HandshakeTest, incomingEncrypted)
{
    static auto constexpr ExpectedPeerId = makePeerId("-TR300Z-w4bd4mkebkbi"sv);

    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
    mediator.setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    auto [io, sock] = createIncomingIo(session_);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);

    // Peer->Client data from a successful encrypted handshake recorded
    // in the wild for replay here. This test will play as the peer.
    // 1. Peer sends Ya.
    sendB64ToClient(
        sock,
        "svkySIFcCsrDTeHjPt516UFbsoR+5vfbe5/m6stE7u5JLZ10kJ19NmP64E10qI"
        "nn78sCrJgjw1yEHHwrzOcKiRlYvcMotzJMe+SjrFUnaw3KBfn2bcKBhxb/sfM9"
        "J7nJ"sv);

    // 2. Wait for client to reply with Yb.
    ASSERT_TRUE(waitFor(
        [&s = std::as_const(sock), buf = std::array<char, 16>{}, n_read = size_t{}]() mutable {
            if (auto ret = recv(s, std::data(buf), std::size(buf), 0); ret > 0) {
                n_read += ret;
            }
            return n_read >= tr_handshake::DH::KeySize;
        },
        MaxWaitMsec));

    // 3. Peer sends the rest of the handshake.
    sendB64ToClient(
        sock,
        "ICAgICAgICAgIKdr4jIBZ4xFfO4xNiRV7Gl2azTSuTFuu06NU1WyRPif018JYe"
        "VGwrTPstEPu3V5lmzjtMGVLaL5EErlpJ93Xrz+ea6EIQEUZA+D4jKaV/to9NVi"
        "04/1W1A2PHgg+I9puac/i9BsFPcjdQeoVtU73lNCbTDQgTieyjDWmwo="sv);

    // 4. Wait for handshake to complete.
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(ExpectedPeerId, res->peer_id);
    EXPECT_EQ(UbuntuTorrent.info_hash, io->torrent_hash());
    EXPECT_EQ(tr_sha1_string{ UbuntuTorrent.info_hash }, tr_sha1_string{ io->torrent_hash() });

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

// The datastream is identical to HandshakeTest.incomingEncrypted,
// but this time we don't recognize the infohash sent by the peer.
TEST_F(HandshakeTest, incomingEncryptedUnknownInfoHash)
{
    auto mediator = MediatorMock{ session_ };
    mediator.setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    auto [io, sock] = createIncomingIo(session_);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);

    // Peer->Client data from a successful encrypted handshake recorded
    // in the wild for replay here. This test will play as the peer.
    // 1. Peer sends Ya.
    sendB64ToClient(
        sock,
        "svkySIFcCsrDTeHjPt516UFbsoR+5vfbe5/m6stE7u5JLZ10kJ19NmP64E10qI"
        "nn78sCrJgjw1yEHHwrzOcKiRlYvcMotzJMe+SjrFUnaw3KBfn2bcKBhxb/sfM9"
        "J7nJ"sv);

    // 2. Wait for client to reply with Yb.
    ASSERT_TRUE(waitFor(
        [&s = std::as_const(sock), buf = std::array<char, 16>{}, n_read = size_t{}]() mutable {
            if (auto ret = recv(s, std::data(buf), std::size(buf), 0); ret >= 0) {
                n_read += ret;
            }
            return n_read >= tr_handshake::DH::KeySize;
        },
        MaxWaitMsec));

    // 3. Peer sends the rest of the handshake.
    sendB64ToClient(
        sock,
        "ICAgICAgICAgIKdr4jIBZ4xFfO4xNiRV7Gl2azTSuTFuu06NU1WyRPif018JYe"
        "VGwrTPstEPu3V5lmzjtMGVLaL5EErlpJ93Xrz+ea6EIQEUZA+D4jKaV/to9NVi"
        "04/1W1A2PHgg+I9puac/i9BsFPcjdQeoVtU73lNCbTDQgTieyjDWmwo="sv);

    // 4. Wait for handshake to complete.
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(tr_sha1_digest_t{}, io->torrent_hash());

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

TEST_F(HandshakeTest, outgoingEncrypted)
{
    static auto constexpr ExpectedPeerId = makePeerId("-qB4250-scysDI_JuVN3"sv);

    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
    mediator.setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    auto [io, sock] = createOutgoingIo(session_, UbuntuTorrent.info_hash);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io, TR_ENCRYPTION_PREFERRED);

    // Peer->Client data from a successful encrypted handshake recorded
    // in the wild for replay here. This test will play as the peer.
    // 1. Wait for client to send Ya.
    ASSERT_TRUE(waitFor(
        [&s = std::as_const(sock), buf = std::array<char, 16>{}, n_read = size_t{}]() mutable {
            if (auto ret = recv(s, std::data(buf), std::size(buf), 0); ret >= 0) {
                n_read += ret;
            }
            return n_read >= tr_handshake::DH::KeySize;
        },
        MaxWaitMsec));

    // 2. Peer replies with Yb.
    sendB64ToClient(
        sock,
        "Sfgoq/nrQfD4Iwirfk+uhOmQMOC/QwK/vYiOact1NF9TpWXms3cvlKEKxs0VU"
        "mnmytRh9bh4Lcs1bswlC6R05XrJGzLhZqAqcLUUAR1VTLA5oKSjR1038zFbhn"
        "c71jql"sv);

    // 3. Wait for client to send HASH('req1', S).
    static auto constexpr WantedLen = tr_handshake::PadbMaxlen + std::tuple_size_v<tr_sha1_digest_t>;
    static auto constexpr NeedleBase64 = "mbpZFBwdi4U1snVvboN3sMEpNmU="sv;
    auto const needle = tr_base64_decode(NeedleBase64);
    auto buf = tr::StackBuffer<WantedLen, char>{};
    ASSERT_TRUE(waitFor(
        [&s = sock, &buf, &needle, n_read = size_t{}]() mutable {
            static auto constexpr StepSize = 14U;
            static_assert(WantedLen % StepSize == 0U);
            while (n_read < WantedLen) {
                auto const [cur, curlen] = buf.reserve_space(StepSize);
                auto const ret = recv(s, cur, curlen, 0);
                if (ret <= 0) {
                    return false;
                }
                buf.commit_space(ret);
                n_read += ret;
                if (auto const range = std::ranges::search(buf, needle); !range.empty()) {
                    return true;
                }
            }
            return false;
        },
        MaxWaitMsec));

    // 4. Peer sends the rest of the handshake.
    sendB64ToClient(
        sock,
        "paZ3steQoTE58dNkLfvPJdGfTli1FCa1FcvjqCGkzEigwxVTo7o9MviInBNGZ"
        "zetXtP0f4PgD0SubXoMNEi2dza/PsuA5e+7GcnNeNT0fcbw49HWtXgL3wb18s"
        "g3puL1I6oi1DQhFfCg1xDDwj2AEKA2cy7zMjhLSTP4n9KNFDfPElrbdZI0Q/F"
        "Db1JVyHzNGLJyYSgOnye/+IHiu/lqNmonH2ixTshuSBCW04PTkGGwxxWqFcw9"
        "MWNA8fidJaKjPgs6z4/FKXlyHGw+6Sa091zxAmxUNdnAmOS1uUcplSMfEz9Go"
        "xok8aLtS+w2xg+eEmIFf+KhW2n0bFqfQDvmAyXKLhXf44lTfq95aMb309APR/"
        "Q0QDEdK2SqUKV2dWQIQLgDlxhxSL7cPMHenT8m3rufi6EdkvdPyKcB1iI2zW8"
        "qUGbgPYZ+CXxTFjz/UxvxNdZaAePjJKz9tlO9ns4y+YqXe+ABBD9P7vMBo9bd"
        "pF4TQ/TjX2rAPqWNucvBjNODNAe/tMSprBz+7Z7cECwBmQEx/N6zsWkjt/qP0"
        "XYikCQLBsTCFNPLHAXOVTpqwJo3mKyzaRZ6V1bmrx+Yn++PdMzArtwxJxvYaW"
        "2G4l/eU/hFHEWzOqUgr6908KtU"sv);

    // 5. Wait for handshake to complete.
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    // check the results
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_connected);
    EXPECT_TRUE(res->read_anything_from_peer);
    EXPECT_EQ(io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(ExpectedPeerId, res->peer_id);
    EXPECT_EQ(UbuntuTorrent.info_hash, io->torrent_hash());
    EXPECT_EQ(tr_sha1_string{ UbuntuTorrent.info_hash }, tr_sha1_string{ io->torrent_hash() });

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

// --- Encryption mode matrix
//
// These tests run an outgoing tr_handshake against a remote peer behind a
// loopback TCP listener. A listener, unlike a socketpair, accepts the new
// connection that on_error() opens when it falls back between plaintext and MSE.

// The remote peer closes its first `drops` connections unanswered, then
// answers each later one with an incoming tr_handshake in `mode`.
// Without a `mode` it closes every connection.
struct RemotePeer {
    size_t drops = 0U;
    std::optional<tr_encryption_mode> mode;
};

struct ModeCase {
    std::string_view name;
    tr_encryption_mode mode;
    RemotePeer peer;
    bool connected;
    bool encrypted;
    size_t connections;
};

class HandshakeModeTest
    : public HandshakeTest
    , public ::testing::WithParamInterface<ModeCase>
{
protected:
    class LoopbackListener
    {
    public:
        LoopbackListener()
            : sock_{ socket(AF_INET, SOCK_STREAM, 0) }
        {
            auto addr = sockaddr_in{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            auto addrlen = socklen_t{ sizeof(addr) };
            EXPECT_NE(TR_BAD_SOCKET, sock_);
            EXPECT_EQ(0, bind(sock_, reinterpret_cast<sockaddr*>(&addr), addrlen));
            EXPECT_EQ(0, listen(sock_, 8));
            EXPECT_EQ(0, getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &addrlen));
            EXPECT_EQ(0, evutil_make_socket_nonblocking(sock_));
            port_ = tr_port::from_network(addr.sin_port);
        }

        LoopbackListener(LoopbackListener const&) = delete;
        LoopbackListener& operator=(LoopbackListener const&) = delete;
        LoopbackListener(LoopbackListener&&) = delete;
        LoopbackListener& operator=(LoopbackListener&&) = delete;

        ~LoopbackListener()
        {
            tr_net_close_socket(sock_);
        }

        [[nodiscard]] tr_socket_address address() const
        {
            return { *tr_address::from_string("127.0.0.1"sv), port_ };
        }

        [[nodiscard]] tr_socket_t accept() const
        {
            return ::accept(sock_, nullptr, nullptr);
        }

    private:
        tr_socket_t const sock_;
        tr_port port_;
    };

    // the result of one handshake
    struct Side {
        std::optional<bool> connected;
        bool encrypted = false;
        std::optional<tr_peer_id_t> peer_id;
    };

    struct Outcome {
        Side local;
        Side remote; // the remote's last handshake
        size_t connections = 0U;
    };

    [[nodiscard]] MediatorMock makeMediator(tr_peer_id_t const& peer_id)
    {
        auto torrent = UbuntuTorrent;
        torrent.client_peer_id = peer_id;
        auto mediator = MediatorMock{ session_ };
        mediator.torrents.emplace(torrent.info_hash, torrent);
        mediator.private_key_ = tr_handshake::DH::randomPrivateKey();
        return mediator;
    }

    [[nodiscard]] std::shared_ptr<tr_peerIo> dial(tr_socket_address const& address)
    {
        return tr_peerIo::new_outgoing(
            session_,
            &session_->top_bandwidth_,
            address,
            UbuntuTorrent.info_hash,
            false /*seed*/,
            false /*utp*/);
    }

    [[nodiscard]] std::shared_ptr<tr_peerIo> adopt(tr_socket_t sock)
    {
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sock));
        return tr_peerIo::new_incoming(
            session_,
            &session_->top_bandwidth_,
            tr_peer_socket_tcp::create(*session_, DefaultPeerSockAddr, sock));
    }

    static void addHandshake(
        std::vector<std::unique_ptr<tr_handshake>>& handshakes,
        MediatorMock& mediator,
        std::shared_ptr<tr_peerIo> const& io,
        tr_encryption_mode mode,
        Side& side)
    {
        handshakes.emplace_back(std::make_unique<tr_handshake>(&mediator, io, mode, [&side](auto const& res) {
            side.connected = res.is_connected;
            side.encrypted = res.io->is_encrypted();
            side.peer_id = res.peer_id;
            if (!res.is_connected) {
                res.io->clear();
            }
            return true;
        }));
    }

    Outcome connectTo(tr_encryption_mode mode, RemotePeer const& peer)
    {
        auto const listener = LoopbackListener{};
        auto const remote_peer_id = makeRandomPeerId();
        auto local = makeMediator(UbuntuTorrent.client_peer_id);
        auto remote = makeMediator(remote_peer_id);

        auto out = Outcome{};
        auto handshakes = std::vector<std::unique_ptr<tr_handshake>>{};

        blockingRunInSessionThread([&] { addHandshake(handshakes, local, dial(listener.address()), mode, out.local); });

        auto const answer_connections = [&] {
            for (auto sock = listener.accept(); sock != TR_BAD_SOCKET; sock = listener.accept()) {
                if (++out.connections <= peer.drops || !peer.mode) {
                    tr_net_close_socket(sock);
                    continue;
                }

                blockingRunInSessionThread([&] {
                    out.remote = Side{};
                    addHandshake(handshakes, remote, adopt(sock), *peer.mode, out.remote);
                });
            }
        };

        // No dial follows the local result, so every connection is counted
        // by then. A connected local side can finish before the remote.
        EXPECT_TRUE(waitFor(
            [&] {
                answer_connections();
                return blockingRunInSessionThread([&] {
                    return out.local.connected.has_value() && (!*out.local.connected || out.remote.connected.has_value());
                });
            },
            MaxWaitMsec));

        blockingRunInSessionThread([&] { handshakes.clear(); });

        if (out.local.connected.value_or(false)) {
            EXPECT_EQ(remote_peer_id, out.local.peer_id);
        }

        return out;
    }
};

TEST_P(HandshakeModeTest, connectsAsExpected)
{
    auto const& param = GetParam();

    auto const out = connectTo(param.mode, param.peer);

    ASSERT_TRUE(out.local.connected.has_value());
    EXPECT_EQ(param.connected, *out.local.connected);
    EXPECT_EQ(param.connections, out.connections);
    if (param.connected) {
        EXPECT_EQ(param.encrypted, out.local.encrypted);
        EXPECT_EQ(std::optional{ true }, out.remote.connected);
        EXPECT_EQ(param.encrypted, out.remote.encrypted);
    }
}

namespace
{
auto constexpr Allow = TR_CLEAR_PREFERRED;
auto constexpr Prefer = TR_ENCRYPTION_PREFERRED;
auto constexpr Require = TR_ENCRYPTION_REQUIRED;

auto constexpr ModeCases = std::array{
    // Every mode against every mode. The connecting side picks plaintext
    // (Allow) or MSE with RC4 only (Prefer, Require); only Require refuses
    // an incoming plaintext handshake.
    //
    //   dialer \ remote   Allow   Prefer   Require
    //   Allow             plain   plain    RC4, on the redial
    //   Prefer            RC4     RC4      RC4
    //   Require           RC4     RC4      RC4
    ModeCase{ .name = "AllowToAllow",
              .mode = Allow,
              .peer = { .mode = Allow },
              .connected = true,
              .encrypted = false,
              .connections = 1U },
    ModeCase{ .name = "AllowToPrefer",
              .mode = Allow,
              .peer = { .mode = Prefer },
              .connected = true,
              .encrypted = false,
              .connections = 1U },
    // the remote drops the plaintext handshake, so Allow redials with MSE
    ModeCase{ .name = "AllowToRequire",
              .mode = Allow,
              .peer = { .mode = Require },
              .connected = true,
              .encrypted = true,
              .connections = 2U },
    ModeCase{ .name = "PreferToAllow",
              .mode = Prefer,
              .peer = { .mode = Allow },
              .connected = true,
              .encrypted = true,
              .connections = 1U },
    ModeCase{ .name = "PreferToPrefer",
              .mode = Prefer,
              .peer = { .mode = Prefer },
              .connected = true,
              .encrypted = true,
              .connections = 1U },
    ModeCase{ .name = "PreferToRequire",
              .mode = Prefer,
              .peer = { .mode = Require },
              .connected = true,
              .encrypted = true,
              .connections = 1U },
    ModeCase{ .name = "RequireToAllow",
              .mode = Require,
              .peer = { .mode = Allow },
              .connected = true,
              .encrypted = true,
              .connections = 1U },
    ModeCase{ .name = "RequireToPrefer",
              .mode = Require,
              .peer = { .mode = Prefer },
              .connected = true,
              .encrypted = true,
              .connections = 1U },
    ModeCase{ .name = "RequireToRequire",
              .mode = Require,
              .peer = { .mode = Require },
              .connected = true,
              .encrypted = true,
              .connections = 1U },

    // Allow's MSE redial offers RC4 and plaintext: a remote that allows
    // plaintext selects it, so only the handshake is obfuscated.
    ModeCase{ .name = "AllowRedialToAllow",
              .mode = Allow,
              .peer = { .drops = 1U, .mode = Allow },
              .connected = true,
              .encrypted = false,
              .connections = 2U },
    ModeCase{ .name = "AllowRedialToPrefer",
              .mode = Allow,
              .peer = { .drops = 1U, .mode = Prefer },
              .connected = true,
              .encrypted = true,
              .connections = 2U },
    ModeCase{ .name = "AllowRedialToRequire",
              .mode = Allow,
              .peer = { .drops = 1U, .mode = Require },
              .connected = true,
              .encrypted = true,
              .connections = 2U },

    // Prefer redials in plaintext when its MSE handshake is dropped.
    ModeCase{ .name = "PreferRedialToAllow",
              .mode = Prefer,
              .peer = { .drops = 1U, .mode = Allow },
              .connected = true,
              .encrypted = false,
              .connections = 2U },
    ModeCase{ .name = "PreferRedialToPrefer",
              .mode = Prefer,
              .peer = { .drops = 1U, .mode = Prefer },
              .connected = true,
              .encrypted = false,
              .connections = 2U },
    ModeCase{ .name = "PreferRedialToRequire",
              .mode = Prefer,
              .peer = { .drops = 1U, .mode = Require },
              .connected = false,
              .encrypted = false,
              .connections = 2U },

    // Require never redials.
    ModeCase{ .name = "RequireDropped",
              .mode = Require,
              .peer = { .drops = 1U, .mode = Require },
              .connected = false,
              .encrypted = false,
              .connections = 1U },

    // A remote that drops everything gets one redial at most.
    ModeCase{ .name = "AllowDroppedTwice",
              .mode = Allow,
              .peer = {},
              .connected = false,
              .encrypted = false,
              .connections = 2U },
    ModeCase{ .name = "PreferDroppedTwice",
              .mode = Prefer,
              .peer = {},
              .connected = false,
              .encrypted = false,
              .connections = 2U },
    ModeCase{ .name = "RequireDroppedOnce",
              .mode = Require,
              .peer = {},
              .connected = false,
              .encrypted = false,
              .connections = 1U },
};
} // namespace

INSTANTIATE_TEST_SUITE_P(
    Modes,
    HandshakeModeTest,
    ::testing::ValuesIn(ModeCases),
    [](::testing::TestParamInfo<ModeCase> const& case_info) { return std::string{ case_info.param.name }; });

// Two peers dial each other at once: Transmission in Prefer dials with MSE
// while the remote, in Allow, dials back in plaintext. Nothing at the
// handshake level ties the two connections together, so both complete, one
// encrypted and one not. Keeping one of them is up to the peer manager.
TEST_F(HandshakeModeTest, simultaneousDialsBothConnect)
{
    auto const local_listener = LoopbackListener{};
    auto const remote_listener = LoopbackListener{};
    auto const remote_peer_id = makeRandomPeerId();
    auto local = makeMediator(UbuntuTorrent.client_peer_id);
    auto remote = makeMediator(remote_peer_id);

    auto local_dial = Side{};
    auto local_answer = Side{};
    auto remote_dial = Side{};
    auto remote_answer = Side{};
    auto handshakes = std::vector<std::unique_ptr<tr_handshake>>{};

    blockingRunInSessionThread([&] {
        addHandshake(handshakes, local, dial(remote_listener.address()), Prefer, local_dial);
        addHandshake(handshakes, remote, dial(local_listener.address()), Allow, remote_dial);
    });

    auto const answer = [&](LoopbackListener const& listener, MediatorMock& mediator, tr_encryption_mode mode, Side& side) {
        if (auto const sock = listener.accept(); sock != TR_BAD_SOCKET) {
            blockingRunInSessionThread([&] { addHandshake(handshakes, mediator, adopt(sock), mode, side); });
        }
    };

    EXPECT_TRUE(waitFor(
        [&] {
            answer(remote_listener, remote, Allow, remote_answer);
            answer(local_listener, local, Prefer, local_answer);
            return blockingRunInSessionThread(
                [&] { return local_dial.connected.has_value() && local_answer.connected.has_value(); });
        },
        MaxWaitMsec));

    blockingRunInSessionThread([&] { handshakes.clear(); });

    // the connection Transmission dialled is MSE with RC4
    EXPECT_EQ(std::optional{ true }, local_dial.connected);
    EXPECT_TRUE(local_dial.encrypted);
    EXPECT_EQ(remote_peer_id, local_dial.peer_id);

    // the connection the remote dialled is plaintext, from the same peer
    EXPECT_EQ(std::optional{ true }, local_answer.connected);
    EXPECT_FALSE(local_answer.encrypted);
    EXPECT_EQ(remote_peer_id, local_answer.peer_id);
}

} // namespace tr::test
