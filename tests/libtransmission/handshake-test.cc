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
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple> // std::tuple_size_v
#include <utility>

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
#include <libtransmission/error.h>
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
protected:
    // A handshake that reads nothing from its peer recycles its DH key into a process-wide pool.
    // Tests that expect their mediator's private key must not get another test's recycled key.
    void SetUp() override
    {
        SessionTest::SetUp();
        auto const lock = std::unique_lock{ tr_handshake::dh_pool_mutex };
        tr_handshake::dh_pool.clear();
    }

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
            ++n_utp_failed;
        }

        size_t n_utp_failed = 0U;

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

    // A µTP socket whose peer stays silent: it discards writes and reads nothing.
    // Its connect is pending until answer() is called.
    class SilentUtpSocket final
        : public tr_peer_socket
        , public std::enable_shared_from_this<SilentUtpSocket>
    {
    public:
        explicit SilentUtpSocket(tr_socket_address const& socket_address)
            : tr_peer_socket{ socket_address }
        {
        }

        void set_read_enabled(bool const enabled) override
        {
            read_enabled_ = enabled;
        }

        void set_write_enabled(bool const enabled) override
        {
            write_enabled_ = enabled;
        }

        [[nodiscard]] bool is_read_enabled() const override
        {
            return read_enabled_;
        }

        [[nodiscard]] bool is_write_enabled() const override
        {
            return write_enabled_;
        }

        [[nodiscard]] bool is_utp_connect_pending() const noexcept override
        {
            return connect_pending_;
        }

        void answer() noexcept
        {
            connect_pending_ = false;
        }

        // The error callback can redial over TCP, which destroys this socket.
        void reportError(int const error_code)
        {
            auto const keep_alive = shared_from_this();
            auto error = tr_error{};
            error.set_from_errno(error_code);
            error_cb(error);
        }

    protected:
        [[nodiscard]] constexpr Type type() const noexcept override
        {
            return Type::UTP;
        }

        [[nodiscard]] size_t try_read_impl(InBuf& /*buf*/, size_t /*n_bytes*/, tr_error* /*error*/) override
        {
            return {};
        }

        [[nodiscard]] size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* /*error*/) override
        {
            buf.drain(n_bytes);
            return n_bytes;
        }

    private:
        bool read_enabled_ = false;
        bool write_enabled_ = false;
        bool connect_pending_ = true;
    };

    // A socket closed on destruction, so a failed ASSERT doesn't leak it.
    class OwnedSocket
    {
    public:
        explicit OwnedSocket(tr_socket_t const sock = TR_BAD_SOCKET) noexcept
            : sock_{ sock }
        {
        }

        OwnedSocket(OwnedSocket&& that) noexcept
            : sock_{ std::exchange(that.sock_, TR_BAD_SOCKET) }
        {
        }

        OwnedSocket& operator=(OwnedSocket&& that) noexcept
        {
            std::swap(sock_, that.sock_);
            return *this;
        }

        OwnedSocket(OwnedSocket const&) = delete;
        OwnedSocket& operator=(OwnedSocket const&) = delete;

        ~OwnedSocket()
        {
            if (is_valid_socket(sock_)) {
                tr_net_close_socket(sock_);
            }
        }

        [[nodiscard]] constexpr tr_socket_t get() const noexcept
        {
            return sock_;
        }

    private:
        tr_socket_t sock_;
    };

    struct LoopbackListener {
        OwnedSocket sock;
        tr_socket_address address;
    };

    // a nonblocking loopback TCP listener on an ephemeral port, for peer IOs that redial over TCP
    [[nodiscard]] static std::optional<LoopbackListener> createLoopbackListener()
    {
        auto sock = OwnedSocket{ socket(AF_INET, SOCK_STREAM, 0) };
        if (!is_valid_socket(sock.get())) {
            return {};
        }

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        auto addrlen = socklen_t{ sizeof(addr) };
        if (bind(sock.get(), reinterpret_cast<sockaddr*>(&addr), addrlen) != 0 || listen(sock.get(), 1) != 0 ||
            getsockname(sock.get(), reinterpret_cast<sockaddr*>(&addr), &addrlen) != 0 ||
            evutil_make_socket_nonblocking(sock.get()) != 0) {
            return {};
        }

        auto const address = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&addr));
        if (!address) {
            return {};
        }

        return LoopbackListener{ .sock = std::move(sock), .address = *address };
    }

    // the first connection to `listener` within MaxWaitMsec, made nonblocking
    [[nodiscard]] static OwnedSocket acceptConnection(LoopbackListener const& listener)
    {
        auto sock = OwnedSocket{};
        waitFor(
            [&] {
                sock = OwnedSocket{ accept(listener.sock.get(), nullptr, nullptr) };
                return is_valid_socket(sock.get());
            },
            MaxWaitMsec);
        if (is_valid_socket(sock.get())) {
            EXPECT_EQ(0, evutil_make_socket_nonblocking(sock.get()));
        }
        return sock;
    }

    // at least `n_bytes` from `sock`, or whatever arrived within MaxWaitMsec
    [[nodiscard]] static std::string receive(OwnedSocket const& sock, size_t const n_bytes)
    {
        auto received = std::string{};
        waitFor(
            [&] {
                auto buf = std::array<char, 128>{};
                if (auto const n = recv(sock.get(), std::data(buf), std::size(buf), 0); n > 0) {
                    received.append(std::data(buf), n);
                }
                return std::size(received) >= n_bytes;
            },
            MaxWaitMsec);
        return received;
    }

    [[nodiscard]] static auto createOutgoingUtpIo(
        tr_session* session,
        tr_socket_address const& socket_address,
        tr_sha1_digest_t const& info_hash,
        bool const is_utp_only = false)
    {
        auto socket = std::make_shared<SilentUtpSocket>(socket_address);
        auto* const raw = socket.get();
        auto peer_io = tr_peerIo::create(
            session,
            std::move(socket),
            &session->top_bandwidth_,
            &info_hash,
            false /*incoming*/,
            false /*seed*/,
            is_utp_only);
        return std::pair{ std::move(peer_io), raw };
    }

    // a BEP 55 holepunch IO, as tr_peerIo::new_outgoing_utp() makes
    [[nodiscard]] static auto createHolepunchIo(
        tr_session* session,
        tr_socket_address const& socket_address,
        tr_sha1_digest_t const& info_hash)
    {
        return createOutgoingUtpIo(session, socket_address, info_hash, true /*is_utp_only*/);
    }

    // The peer IO's callbacks run on the session thread, and a TCP redial
    // swaps its socket, so µTP errors are injected there too.
    void reportUtpError(SilentUtpSocket* const socket, int const error_code)
    {
        blockingRunInSessionThread([socket, error_code] { socket->reportError(error_code); });
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

// In Allow mode our first message is the plaintext BitTorrent handshake.
TEST_F(HandshakeTest, outgoingPlaintextFallsBackFromUtpToTcp)
{
    auto const peer_id = makeRandomPeerId();
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

    auto const listener = createLoopbackListener();
    ASSERT_TRUE(listener);
    auto [io, utp_socket] = createOutgoingUtpIo(session_, listener->address, UbuntuTorrent.info_hash);

    auto res = std::optional<tr_handshake::Result>{};
    auto const handshake = startHandshake(res, &mediator, io, TR_CLEAR_PREFERRED);
    reportUtpError(utp_socket, ETIMEDOUT);

    auto const sock = acceptConnection(*listener);
    ASSERT_NE(TR_BAD_SOCKET, sock.get());
    EXPECT_TRUE(receive(sock, std::size(PlaintextProtocolName)).starts_with(PlaintextProtocolName));

    sendToClient(sock.get(), PlaintextProtocolName);
    sendToClient(sock.get(), ReservedBytesNoExtensions);
    sendToClient(sock.get(), UbuntuTorrent.info_hash);
    sendToClient(sock.get(), peer_id);
    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_connected);
    EXPECT_EQ(peer_id, res->peer_id);
    EXPECT_FALSE(io->is_utp());
    EXPECT_FALSE(io->is_encrypted());
    EXPECT_EQ(1U, mediator.n_utp_failed);
}

// In the encrypting modes our first message is the MSE public key (Ya).
TEST_F(HandshakeTest, outgoingEncryptedFallsBackFromUtpToTcp)
{
    static auto constexpr YaSize = std::tuple_size_v<tr_message_stream_encryption::DH::key_bigend_t>;

    for (auto const mode : { TR_ENCRYPTION_PREFERRED, TR_ENCRYPTION_REQUIRED }) {
        SCOPED_TRACE("encryption mode " + std::to_string(static_cast<int>(mode)));

        auto mediator = MediatorMock{ session_ };
        mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

        auto const listener = createLoopbackListener();
        ASSERT_TRUE(listener);
        auto [io, utp_socket] = createOutgoingUtpIo(session_, listener->address, UbuntuTorrent.info_hash);

        auto res = std::optional<tr_handshake::Result>{};
        auto const handshake = startHandshake(res, &mediator, io, mode);
        reportUtpError(utp_socket, ETIMEDOUT);

        auto const sock = acceptConnection(*listener);
        ASSERT_NE(TR_BAD_SOCKET, sock.get());
        auto const received = receive(sock, YaSize);
        EXPECT_GE(std::size(received), YaSize);
        EXPECT_FALSE(received.starts_with(PlaintextProtocolName));
        EXPECT_FALSE(io->is_utp());
        EXPECT_EQ(1U, mediator.n_utp_failed);

        // a junk Yb and PadB longer than PadbMaxlen end the handshake before the test does
        sendToClient(sock.get(), std::string(YaSize + (size_t{ 2U } * tr_handshake::PadbMaxlen), 'x'));
        waitFor([&res] { return res.has_value(); }, MaxWaitMsec);
        ASSERT_TRUE(res.has_value());
        EXPECT_FALSE(res->is_connected);
    }
}

// Only a timeout or refusal shows the peer lacks µTP; a reset still redials over TCP.
TEST_F(HandshakeTest, utpConnectResetFallsBackWithoutMarkingPeer)
{
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

    auto const listener = createLoopbackListener();
    ASSERT_TRUE(listener);
    auto [io, utp_socket] = createOutgoingUtpIo(session_, listener->address, UbuntuTorrent.info_hash);

    auto res = std::optional<tr_handshake::Result>{};
    auto const handshake = startHandshake(res, &mediator, io, TR_CLEAR_PREFERRED);
    reportUtpError(utp_socket, ECONNRESET);

    {
        auto const sock = acceptConnection(*listener);
        ASSERT_NE(TR_BAD_SOCKET, sock.get());
        EXPECT_FALSE(io->is_utp());
        EXPECT_EQ(0U, mediator.n_utp_failed);
    } // closing the TCP connection ends the handshake before the test does

    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->is_connected);
}

// A peer that answered the µTP connect speaks µTP; its error ends the handshake.
TEST_F(HandshakeTest, utpErrorAfterConnectDoesNotFallBackToTcp)
{
    for (auto const error_code : { ETIMEDOUT, ENOTCONN }) {
        SCOPED_TRACE("error code " + std::to_string(error_code));

        auto mediator = MediatorMock{ session_ };
        mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

        auto const listener = createLoopbackListener();
        ASSERT_TRUE(listener);
        auto [io, utp_socket] = createOutgoingUtpIo(session_, listener->address, UbuntuTorrent.info_hash);
        utp_socket->answer();

        auto res = std::optional<tr_handshake::Result>{};
        auto const handshake = startHandshake(res, &mediator, io, TR_CLEAR_PREFERRED);
        reportUtpError(utp_socket, error_code);
        waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

        ASSERT_TRUE(res.has_value());
        EXPECT_FALSE(res->is_connected);
        EXPECT_TRUE(io->is_utp());
        EXPECT_EQ(0U, mediator.n_utp_failed);
        EXPECT_EQ(TR_BAD_SOCKET, accept(listener->sock.get(), nullptr, nullptr));
    }
}

// A µTP peer that connected and then dropped our public key may not do MSE,
// so it gets the plaintext retry; reconnect() sends that over TCP.
TEST_F(HandshakeTest, utpErrorAfterConnectAwaitingYbRetriesPlaintext)
{
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

    auto const listener = createLoopbackListener();
    ASSERT_TRUE(listener);
    auto [io, utp_socket] = createOutgoingUtpIo(session_, listener->address, UbuntuTorrent.info_hash);
    utp_socket->answer();

    auto res = std::optional<tr_handshake::Result>{};
    auto const handshake = startHandshake(res, &mediator, io, TR_ENCRYPTION_PREFERRED);
    reportUtpError(utp_socket, ECONNRESET);

    {
        auto const sock = acceptConnection(*listener);
        ASSERT_NE(TR_BAD_SOCKET, sock.get());
        EXPECT_TRUE(receive(sock, std::size(PlaintextProtocolName)).starts_with(PlaintextProtocolName));
        EXPECT_FALSE(io->is_utp());
        EXPECT_EQ(0U, mediator.n_utp_failed);
    } // closing the TCP connection ends the handshake before the test does

    waitFor([&res] { return res.has_value(); }, MaxWaitMsec);
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(res->is_connected);
}

// Only the IOs tr_peerIo::new_outgoing_utp() makes for a BEP 55 holepunch are µTP-only.
TEST_F(HandshakeTest, onlyHolepunchIosAreUtpOnly)
{
    if (!session_->allowsUTP()) {
        GTEST_SKIP() << "µTP is unavailable";
    }

    auto const listener = createLoopbackListener();
    ASSERT_TRUE(listener);

    // libutp runs on the session thread, so the µTP sockets are made there
    auto const flags = blockingRunInSessionThread([this, &listener] {
        auto const& info_hash = UbuntuTorrent.info_hash;
        auto* const bandwidth = &session_->top_bandwidth_;
        auto const holepunch = tr_peerIo::new_outgoing_utp(session_, bandwidth, listener->address, info_hash, false);
        auto const dialled = tr_peerIo::new_outgoing(session_, bandwidth, listener->address, info_hash, false, true);
        return std::array{
            holepunch && holepunch->is_utp(),
            holepunch && holepunch->is_utp_only(),
            dialled && dialled->is_utp(),
            dialled && dialled->is_utp_only(),
        };
    });

    EXPECT_TRUE(flags[0]) << "holepunch IO is µTP";
    EXPECT_TRUE(flags[1]) << "holepunch IO is µTP-only";
    EXPECT_TRUE(flags[2]) << "dialled IO is µTP";
    EXPECT_FALSE(flags[3]) << "dialled IO is not µTP-only";
}

// A holepunched peer is reachable only over the punched µTP path, so a failed
// holepunch handshake ends without a TCP redial, whatever the encryption mode.
TEST_F(HandshakeTest, failedHolepunchDoesNotFallBackToTcp)
{
    for (auto const mode : { TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED, TR_ENCRYPTION_REQUIRED }) {
        SCOPED_TRACE("encryption mode " + std::to_string(static_cast<int>(mode)));

        auto mediator = MediatorMock{ session_ };
        mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);

        auto const listener = createLoopbackListener();
        ASSERT_TRUE(listener);
        auto [io, utp_socket] = createHolepunchIo(session_, listener->address, UbuntuTorrent.info_hash);

        auto res = std::optional<tr_handshake::Result>{};
        auto const handshake = startHandshake(res, &mediator, io, mode);
        reportUtpError(utp_socket, ETIMEDOUT);
        waitFor([&res] { return res.has_value(); }, MaxWaitMsec);

        ASSERT_TRUE(res.has_value());
        EXPECT_FALSE(res->is_connected);
        EXPECT_TRUE(io->is_utp());
        EXPECT_EQ(0U, mediator.n_utp_failed);
        EXPECT_EQ(TR_BAD_SOCKET, accept(listener->sock.get(), nullptr, nullptr));
    }
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

} // namespace tr::test
