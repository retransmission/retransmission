// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef> // size_t, std::byte
#include <cstdint> // int64_t, uint8_t
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
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
#include <libtransmission/peer-socket.h>
#include <libtransmission/session.h> // tr_peerIdInit()
#include <libtransmission/string-utils.h>
#include <libtransmission/timer.h>
#include <libtransmission/tr-assert.h>
#include <libtransmission/macros.h>
#include <libtransmission/types.h>

#include "fake-timer.h"
#include "test-fixtures.h"

using namespace std::literals;

#define LOCAL_SOCKETPAIR_AF TR_IF_WIN32(AF_INET, AF_UNIX)

namespace tr::test
{

auto constexpr MaxWaitMsec = 5000;

namespace
{

class RetrySocket final : public tr_peer_socket
{
public:
    RetrySocket(tr_socket_address const& address, tr_transport_protocol protocol, bool is_connected)
        : tr_peer_socket{ address }
        , protocol_{ protocol }
        , is_connected_{ is_connected }
    {
    }

    void set_read_enabled(bool enabled) override
    {
        read_enabled_ = enabled;
    }

    void set_write_enabled(bool enabled) override
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

    [[nodiscard]] constexpr bool is_connected() const noexcept override
    {
        return is_connected_;
    }

    [[nodiscard]] constexpr tr_transport_protocol type() const noexcept override
    {
        return protocol_;
    }

    void receive(std::span<std::byte const> bytes)
    {
        input_.assign(bytes.begin(), bytes.end());
        read_cb();
    }

    void fail(int code = ECONNRESET)
    {
        error_cb(tr_net_error(code));
    }

    std::string output;

protected:
    size_t try_read_impl(InBuf& buf, size_t max, tr_error* error) override
    {
        auto const n = std::min(max, input_.size());
        if (n == 0U) {
            if (error != nullptr) {
                *error = tr_net_error(EWOULDBLOCK);
            }
            return 0;
        }

        buf.add(input_.data(), n);
        input_.erase(input_.begin(), input_.begin() + n);
        return n;
    }

    size_t try_write_impl(OutBuf& buf, size_t max, tr_error* /*error*/) override
    {
        auto const n = std::min(max, buf.size());
        output.append(reinterpret_cast<char const*>(buf.data()), n);
        buf.drain(n);
        return n;
    }

private:
    tr_transport_protocol protocol_;
    bool is_connected_;
    bool read_enabled_ = false;
    bool write_enabled_ = false;
    std::vector<std::byte> input_;
};

} // namespace

class HandshakeTest : public SessionTest
{
public:
    class MediatorMock final : public tr_handshake::Mediator
    {
    public:
        explicit MediatorMock(tr_session* session, tr::TimerMaker* timer_maker = nullptr)
            : session_{ session }
            , timer_maker_{ timer_maker }
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
            return timer_maker_ != nullptr ? *timer_maker_ : session_->timerMaker();
        }

        [[nodiscard]] bool allows_dht() const override
        {
            return false;
        }

        [[nodiscard]] uint16_t pad(void* setme, [[maybe_unused]] uint16_t maxlen) const override
        {
            TR_ASSERT(maxlen > PadASize);
            std::fill_n(static_cast<char*>(setme), PadASize, ' ');
            return PadASize;
        }

        [[nodiscard]] tr_handshake::DH get_dh() const override
        {
            return tr_handshake::DH{ private_key_ };
        }

        void set_utp_failed(tr_sha1_digest_t const& info_hash, tr_socket_address const& socket_address) override
        {
            utp_failures.emplace_back(info_hash, socket_address);
        }

        void setPrivateKeyFromBase64(std::string_view b64)
        {
            auto const str = tr_base64_decode(b64);
            TR_ASSERT(std::size(str) == std::size(private_key_));
            std::copy_n(reinterpret_cast<std::byte const*>(std::data(str)), std::size(str), std::begin(private_key_));
        }

        tr_session* const session_;
        tr::TimerMaker* const timer_maker_;
        std::map<tr_sha1_digest_t, TorrentInfo> torrents;
        tr_message_stream_encryption::DH::private_key_bigend_t private_key_ = {};
        std::vector<std::pair<tr_sha1_digest_t, tr_socket_address>> utp_failures;
    };

    struct RetryScript {
        RetryScript()
        {
            TR_ASSERT(active_retry_script == nullptr);
            active_retry_script = this;
        }

        ~RetryScript()
        {
            active_retry_script = nullptr;
        }

        RetryScript(RetryScript const&) = delete;
        RetryScript(RetryScript&&) = delete;
        RetryScript& operator=(RetryScript const&) = delete;
        RetryScript& operator=(RetryScript&&) = delete;

        std::optional<tr_transport_protocol> unavailable;
        std::vector<tr_transport_protocol> attempts;
        std::vector<std::shared_ptr<RetrySocket>> sockets;
    };

    // Socket factories have no context argument; one script runs on the session thread.
    inline static RetryScript* active_retry_script = nullptr;

    [[nodiscard]] static std::shared_ptr<tr_peer_socket> makeRetrySocket(
        tr_socket_address const& address,
        tr_transport_protocol protocol)
    {
        auto& script = *active_retry_script;
        script.attempts.push_back(protocol);
        if (script.unavailable == protocol) {
            return {};
        }

        return script.sockets.emplace_back(std::make_shared<RetrySocket>(address, protocol, true));
    }

    [[nodiscard]] static std::shared_ptr<tr_peer_socket> retryUtp(
        tr_session& /*session*/,
        tr_socket_address const& address,
        bool /*client_is_seed*/)
    {
        return makeRetrySocket(address, tr_transport_protocol::UTP);
    }

    [[nodiscard]] static std::shared_ptr<tr_peer_socket> retryTcp(
        tr_session& /*session*/,
        tr_socket_address const& address,
        bool /*client_is_seed*/)
    {
        return makeRetrySocket(address, tr_transport_protocol::TCP);
    }

    [[nodiscard]] auto createRetryIo(
        RetryScript& script,
        std::span<tr_transport_protocol const> transports,
        bool connected = true,
        bool incoming = false)
    {
        TR_ASSERT(!transports.empty());
        auto factories = tr_peerIo::SocketFactories{};
        for (auto const protocol : std::views::reverse(transports)) {
            factories.emplace_back(protocol == tr_transport_protocol::UTP ? &retryUtp : &retryTcp);
        }

        auto socket = std::make_shared<RetrySocket>(DefaultPeerSockAddr, transports.front(), connected);
        script.sockets.push_back(socket);
        return tr_peerIo::create(
            session_,
            std::move(socket),
            std::move(factories),
            &session_->top_bandwidth_,
            &UbuntuTorrent.info_hash,
            incoming,
            false);
    }

    static void expectAttempt(tr_peerIo& io, RetrySocket const& socket, bool mse)
    {
        auto const expected_size = mse ? tr_handshake::DH::KeySize + PadASize : tr_handshake::HandshakeSize;
        EXPECT_EQ(expected_size, io.flush(tr_direction::Up, SIZE_MAX));
        EXPECT_EQ(expected_size, socket.output.size());
        EXPECT_EQ(!mse, std::string_view{ socket.output }.starts_with(PlaintextProtocolName));
    }

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
    static auto constexpr PadASize = uint16_t{ 10U };

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

    // A nonzero buf_size sets the buffers that hold bytes the peer has sent
    // but the client has not read. Platforms differ in which one limits that,
    // so it sets both the client's SO_RCVBUF and the peer's SO_SNDBUF.
    [[nodiscard]] auto createIncomingIo(tr_session* session, int const buf_size = 0)
    {
        auto sockpair = std::to_array<evutil_socket_t>({ -1, -1 });
        EXPECT_EQ(0, evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, std::data(sockpair))) << tr_strerror(errno);
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockpair[1]));
        if (buf_size != 0) {
            auto const* const optval = reinterpret_cast<char const*>(&buf_size);
            EXPECT_EQ(0, setsockopt(sockpair[0], SOL_SOCKET, SO_RCVBUF, optval, sizeof(buf_size)))
                << tr_net_strerror(sockerrno);
            EXPECT_EQ(0, setsockopt(sockpair[1], SOL_SOCKET, SO_SNDBUF, optval, sizeof(buf_size)))
                << tr_net_strerror(sockerrno);
        }
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
            {},
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

TEST_F(HandshakeTest, retriesBothModesOncePerTransport)
{
    blockingRunInSessionThread([this] {
        for (
            auto const& transports : {
                std::array{ tr_transport_protocol::UTP, tr_transport_protocol::TCP },
                std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP },
            }) {
            for (auto const mode : { TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED }) {
                SCOPED_TRACE(std::string{ tr_transport_protocol_to_sv(transports.front()) });
                SCOPED_TRACE(static_cast<int>(mode));

                auto script = RetryScript{};
                auto mediator = MediatorMock{ session_ };
                mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
                auto io = createRetryIo(script, transports);
                auto result = std::optional<tr_handshake::Result>{};
                auto handshake = startHandshake(result, &mediator, io, mode);
                auto const expected_protocols = std::array{ transports[0], transports[0], transports[1], transports[1] };
                auto const prefer_mse = mode == TR_ENCRYPTION_PREFERRED;
                auto const expected_modes = std::array{ prefer_mse, !prefer_mse, prefer_mse, !prefer_mse };

                for (auto i = size_t{}; i < expected_modes.size(); ++i) {
                    ASSERT_FALSE(result);
                    ASSERT_EQ(i + 1U, script.sockets.size());
                    auto const socket = script.sockets.back();
                    EXPECT_EQ(expected_protocols[i], socket->type());
                    expectAttempt(*io, *socket, expected_modes[i]);
                    socket->fail();
                }

                ASSERT_TRUE(result);
                EXPECT_FALSE(result->is_connected);
                EXPECT_EQ((std::vector{ transports[0], transports[1], transports[1] }), script.attempts);
            }
        }
    });
}

TEST_F(HandshakeTest, skipsAlternateModeWhenTransportDidNotConnect)
{
    blockingRunInSessionThread([this] {
        for (
            auto const& transports : {
                std::array{ tr_transport_protocol::UTP, tr_transport_protocol::TCP },
                std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP },
            }) {
            for (auto const mode : { TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED }) {
                SCOPED_TRACE(std::string{ tr_transport_protocol_to_sv(transports.front()) });
                SCOPED_TRACE(static_cast<int>(mode));

                auto script = RetryScript{};
                auto mediator = MediatorMock{ session_ };
                mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
                auto io = createRetryIo(script, transports, false);
                auto result = std::optional<tr_handshake::Result>{};
                auto handshake = startHandshake(result, &mediator, io, mode);

                script.sockets.front()->fail(ETIMEDOUT);
                ASSERT_FALSE(result);
                ASSERT_EQ(2U, script.sockets.size());
                EXPECT_EQ((std::vector{ transports[1] }), script.attempts);
                EXPECT_EQ(transports[1], script.sockets.back()->type());
                expectAttempt(*io, *script.sockets.back(), mode == TR_ENCRYPTION_PREFERRED);
            }
        }
    });
}

TEST_F(HandshakeTest, requiredEncryptionNeverRetriesPlaintext)
{
    blockingRunInSessionThread([this] {
        auto script = RetryScript{};
        auto mediator = MediatorMock{ session_ };
        mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
        constexpr auto Transports = std::array{ tr_transport_protocol::UTP, tr_transport_protocol::TCP };
        auto io = createRetryIo(script, Transports);
        auto result = std::optional<tr_handshake::Result>{};
        auto handshake = startHandshake(result, &mediator, io, TR_ENCRYPTION_REQUIRED);

        expectAttempt(*io, *script.sockets.back(), true);
        script.sockets.back()->fail();
        ASSERT_FALSE(result);
        ASSERT_EQ(2U, script.sockets.size());
        EXPECT_EQ((std::vector{ tr_transport_protocol::TCP }), script.attempts);
        expectAttempt(*io, *script.sockets.back(), true);
        script.sockets.back()->fail();
        ASSERT_TRUE(result);
        EXPECT_FALSE(result->is_connected);
    });
}

TEST_F(HandshakeTest, handlesFailedReconnectSocketCreation)
{
    blockingRunInSessionThread([this] {
        for (auto const mode : { TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED }) {
            SCOPED_TRACE(static_cast<int>(mode));

            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
            auto io = createRetryIo(script, Transports);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, mode);
            script.unavailable = Transports.front();

            script.sockets.front()->fail();
            ASSERT_FALSE(result);
            ASSERT_EQ(2U, script.sockets.size());
            EXPECT_EQ((std::vector{ Transports[0], Transports[1] }), script.attempts);
            EXPECT_EQ(Transports[1], script.sockets.back()->type());
            expectAttempt(*io, *script.sockets.back(), mode != TR_CLEAR_PREFERRED);
        }

        for (auto const mode : { TR_ENCRYPTION_REQUIRED, TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED }) {
            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
            auto io = createRetryIo(script, Transports, false);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, mode);
            script.unavailable = Transports.back();

            script.sockets.front()->fail();
            ASSERT_TRUE(result);
            EXPECT_FALSE(result->is_connected);
            EXPECT_EQ((std::vector{ Transports.back() }), script.attempts);
        }
    });
}

TEST_F(HandshakeTest, incomingAndUnknownTorrentErrorsFailWithoutRetry)
{
    blockingRunInSessionThread([this] {
        for (auto const incoming : { false, true }) {
            SCOPED_TRACE(incoming);

            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            if (incoming) {
                mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            }
            constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
            auto io = createRetryIo(script, Transports, true, incoming);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, TR_ENCRYPTION_PREFERRED);

            script.sockets.front()->fail();
            ASSERT_TRUE(result);
            EXPECT_FALSE(result->is_connected);
            EXPECT_TRUE(script.attempts.empty());
        }
    });
}

TEST_F(HandshakeTest, stoppedTorrentErrorsFailWithoutRetry)
{
    blockingRunInSessionThread([this] {
        for (auto const mode : { TR_ENCRYPTION_PREFERRED, TR_ENCRYPTION_REQUIRED }) {
            SCOPED_TRACE(static_cast<int>(mode));

            auto stopped = UbuntuTorrent;
            stopped.is_running = false;
            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(stopped.info_hash, stopped);
            constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
            auto io = createRetryIo(script, Transports);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, mode);

            // A stopped torrent can't send the BitTorrent handshake that every retry ends in,
            // so it must not dial the peer again.
            script.sockets.front()->fail();
            ASSERT_TRUE(result);
            EXPECT_FALSE(result->is_connected);
            EXPECT_TRUE(script.attempts.empty());
        }
    });
}

TEST_F(HandshakeTest, partialPlaintextReplyDoesNotRepeatMse)
{
    blockingRunInSessionThread([this] {
        for (auto const mode : { TR_CLEAR_PREFERRED, TR_ENCRYPTION_PREFERRED }) {
            for (auto const reply_size : { tr_handshake::HandshakeName.size(), tr_handshake::IncomingHandshakeLen - 1U }) {
                SCOPED_TRACE(static_cast<int>(mode));
                SCOPED_TRACE(reply_size);

                auto script = RetryScript{};
                auto mediator = MediatorMock{ session_ };
                mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
                constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
                auto io = createRetryIo(script, Transports);
                auto result = std::optional<tr_handshake::Result>{};
                auto handshake = startHandshake(result, &mediator, io, mode);

                if (mode == TR_CLEAR_PREFERRED) {
                    expectAttempt(*io, *script.sockets.back(), false);
                    script.sockets.back()->fail();
                    ASSERT_EQ(2U, script.sockets.size());
                }
                expectAttempt(*io, *script.sockets.back(), true);

                auto reply = std::array<std::byte, tr_handshake::IncomingHandshakeLen - 1U>{};
                std::ranges::copy(tr_handshake::HandshakeName, reply.begin());
                script.sockets.back()->receive(std::span{ reply }.first(reply_size));
                ASSERT_EQ(reply_size, io->read_buffer_size());
                auto const attempts = script.attempts;
                script.sockets.back()->fail();
                ASSERT_TRUE(result);
                EXPECT_FALSE(result->is_connected);
                EXPECT_EQ(attempts, script.attempts);
            }
        }
    });
}

TEST_F(HandshakeTest, errorsAfterInitialHandshakeStagesFailWithoutRetry)
{
    blockingRunInSessionThread([this] {
        for (auto const mse : { false, true }) {
            SCOPED_TRACE(mse);

            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
            auto io = createRetryIo(script, Transports);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, mse ? TR_ENCRYPTION_PREFERRED : TR_CLEAR_PREFERRED);
            expectAttempt(*io, *script.sockets.back(), mse);

            if (mse) {
                auto yb = tr_handshake::DH::key_bigend_t{};
                yb.back() = std::byte{ 2 };
                script.sockets.back()->receive(yb);
            } else {
                auto reply = std::array<std::byte, tr_handshake::IncomingHandshakeLen>{};
                std::ranges::copy(tr_handshake::HandshakeName, reply.begin());
                std::ranges::copy(
                    UbuntuTorrent.info_hash,
                    reply.begin() + tr_handshake::HandshakeName.size() + tr_handshake::HandshakeFlagsBytes);
                script.sockets.back()->receive(reply);
            }

            ASSERT_FALSE(result);
            EXPECT_EQ(0U, io->read_buffer_size());
            script.sockets.back()->fail();
            ASSERT_TRUE(result);
            EXPECT_FALSE(result->is_connected);
            EXPECT_TRUE(script.attempts.empty());
        }
    });
}

TEST_F(HandshakeTest, recordsUnconnectedUtpFailureBeforeRetry)
{
    blockingRunInSessionThread([this] {
        for (auto const fallback : { false, true }) {
            SCOPED_TRACE(fallback);

            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            constexpr auto Transports = std::array{ tr_transport_protocol::UTP, tr_transport_protocol::TCP };
            auto io = createRetryIo(script, std::span{ Transports }.first(fallback ? 2U : 1U), false);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, TR_ENCRYPTION_PREFERRED);

            script.sockets.front()->fail(ETIMEDOUT);
            ASSERT_EQ(1U, mediator.utp_failures.size());
            EXPECT_EQ(UbuntuTorrent.info_hash, mediator.utp_failures.front().first);
            EXPECT_EQ(DefaultPeerSockAddr, mediator.utp_failures.front().second);
            EXPECT_EQ(!fallback, result.has_value());
            if (fallback) {
                EXPECT_EQ((std::vector{ tr_transport_protocol::TCP }), script.attempts);
            }
        }

        for (auto const protocol : { tr_transport_protocol::UTP, tr_transport_protocol::TCP }) {
            SCOPED_TRACE(std::string{ tr_transport_protocol_to_sv(protocol) });

            auto script = RetryScript{};
            auto mediator = MediatorMock{ session_ };
            mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
            auto const transports = std::array{ protocol };
            auto io = createRetryIo(script, transports, protocol == tr_transport_protocol::UTP);
            auto result = std::optional<tr_handshake::Result>{};
            auto handshake = startHandshake(result, &mediator, io, TR_ENCRYPTION_PREFERRED);

            script.sockets.front()->fail(ETIMEDOUT);
            EXPECT_TRUE(mediator.utp_failures.empty());
        }
    });
}

TEST_F(HandshakeTest, retriesDoNotRestartHandshakeTimeout)
{
    blockingRunInSessionThread([this] {
        auto script = RetryScript{};
        auto timer_maker = FakeTimerMaker{};
        auto mediator = MediatorMock{ session_, &timer_maker };
        mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
        constexpr auto Transports = std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP };
        auto io = createRetryIo(script, Transports);
        auto result = std::optional<tr_handshake::Result>{};
        auto handshake = startHandshake(result, &mediator, io, TR_ENCRYPTION_PREFERRED);

        ASSERT_EQ(1U, timer_maker.timers().size());
        auto* const timer = timer_maker.timers().front();
        EXPECT_EQ(1, timer->startCount());
        script.sockets.back()->fail();
        script.sockets.back()->fail();
        ASSERT_FALSE(result);
        EXPECT_EQ(1, timer->startCount());

        timer->fire();
        ASSERT_TRUE(result);
        EXPECT_FALSE(result->is_connected);
        auto const attempts = script.attempts;
        script.sockets.back()->fail();
        EXPECT_EQ(attempts, script.attempts);
    });
}

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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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

// PadA is longer than 64 KiB, but its length modulo 64 KiB is within
// PadaMaxlen, so a 16-bit length would pass the limit check.
TEST_F(HandshakeTest, incomingEncryptedRejectsPadALongerThan64KiB)
{
    static auto constexpr PadALen = size_t{ 65536U } + 10U;
    static_assert(PadALen % 65536U <= tr_handshake::PadaMaxlen);

    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(UbuntuTorrent.info_hash, UbuntuTorrent);
    mediator.setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    // 128 KiB holds Ya and PadA, and stays under NetBSD's default socket buffer limit.
    static auto constexpr SockBufSize = 128 * 1024;
    auto [io, sock] = createIncomingIo(session_, SockBufSize);

    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = startHandshake(res, &mediator, io);

    // Ya is the one recorded for HandshakeTest.incomingEncrypted.
    auto payload = tr_base64_decode(
        "svkySIFcCsrDTeHjPt516UFbsoR+5vfbe5/m6stE7u5JLZ10kJ19NmP64E10qI"
        "nn78sCrJgjw1yEHHwrzOcKiRlYvcMotzJMe+SjrFUnaw3KBfn2bcKBhxb/sfM9"
        "J7nJ"sv);
    payload.append(PadALen, ' ');

    // Send too little of Ya to tell it from a plaintext handshake, and wait
    // for the client to read it. The client is then waiting on the socket.
    static auto constexpr FirstLen = size_t{ 10U };
    sendToClient(sock, std::string_view{ payload }.substr(0, FirstLen));
    ASSERT_TRUE(waitForInSessionThread([&] { return io->read_buffer_size() == FirstLen; }, MaxWaitMsec));

    // Queue the rest in one send while the session thread is busy, so that the
    // client's next read gets all of it at once.
    auto const rest = std::string_view{ payload }.substr(FirstLen);
    auto n_sent = int64_t{};
    auto send_err = 0;
    blockingRunInSessionThread([&] {
        n_sent = send(sock, std::data(rest), std::size(rest), 0);
        send_err = sockerrno;
    });
    ASSERT_EQ(static_cast<int64_t>(std::size(rest)), n_sent) << tr_net_strerror(send_err);

    // The handshake must fail before the client replies with Yb.
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));
    EXPECT_FALSE(res->is_connected);
    auto buf = std::array<char, 16>{};
    EXPECT_LE(recv(sock, std::data(buf), std::size(buf), 0), 0);

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
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));

    // check the results
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

TEST_F(HandshakeTest, outgoingPlaintextForStoppedTorrentFails)
{
    auto stopped = UbuntuTorrent;
    stopped.is_running = false;
    auto mediator = MediatorMock{ session_ };
    mediator.torrents.emplace(stopped.info_hash, stopped);

    auto [io, sock] = createOutgoingIo(session_, UbuntuTorrent.info_hash);

    // The handshake can't be sent, so the handshake must fail now, not after HandshakeTimeoutSec.
    auto res = std::optional<tr_handshake::Result>{};
    auto handshake = std::unique_ptr<tr_handshake>{};
    blockingRunInSessionThread([&] {
        handshake = startHandshake(res, &mediator, io);
        EXPECT_FALSE(res.has_value());
    });
    ASSERT_TRUE(waitForInSessionThread([&res] { return res.has_value(); }, MaxWaitMsec));
    EXPECT_FALSE(res->is_connected);

    tr_net_close_socket(sock);
    blockingRunInSessionThread([&] {
        handshake.reset();
        res.reset();
        io.reset();
    });
}

} // namespace tr::test
