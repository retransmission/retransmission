// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <libtransmission/bandwidth.h>
#include <libtransmission/error.h>
#include <libtransmission/macros.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

class PeerSocketTcpTest : public SessionTest
{
protected:
    void SetUp() override
    {
        SessionTest::SetUp();

        endpoint_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(is_valid_socket(endpoint_sock_));
        ASSERT_EQ(0, evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(endpoint_sock_)));

        auto [addr, addrlen] = tr_socket_address::to_sockaddr(*tr_address::from_string("127.0.0.1"sv), {});
        ASSERT_EQ(0, bind(endpoint_sock_, reinterpret_cast<sockaddr const*>(&addr), addrlen));
        ASSERT_EQ(0, getsockname(endpoint_sock_, reinterpret_cast<sockaddr*>(&addr), &addrlen));
        auto const endpoint = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&addr));
        ASSERT_TRUE(endpoint);
        endpoint_ = *endpoint;
    }

    void TearDown() override
    {
        blockingRunInSessionThread([this] {
            peer_io_.reset();
            peer_socket_.reset();
        });
        if (is_valid_socket(remote_sock_)) {
            tr_net_close_socket(remote_sock_);
        }
        if (is_valid_socket(endpoint_sock_)) {
            tr_net_close_socket(endpoint_sock_);
        }

        SessionTest::TearDown();
    }

    [[nodiscard]] bool acceptPeer()
    {
        return waitFor(
            [this] {
                remote_sock_ = accept(endpoint_sock_, nullptr, nullptr);
                if (!is_valid_socket(remote_sock_)) {
                    return false;
                }
                EXPECT_EQ(0, evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(remote_sock_)));
                return true;
            },
            5000);
    }

    tr_socket_t endpoint_sock_ = TR_BAD_SOCKET;
    tr_socket_t remote_sock_ = TR_BAD_SOCKET;
    tr_socket_address endpoint_;
    tr_bandwidth quota_;
    std::shared_ptr<tr_peerIo> peer_io_;
    std::shared_ptr<tr_peer_socket_tcp> peer_socket_;
    tr_error connect_error_;
    size_t error_count_ = 0;
};

} // namespace

TEST_F(PeerSocketTcpTest, pendingConnectionPreservesBuffersAndReportsFailure)
{
    auto const asynchronous = blockingRunInSessionThread([this] {
        peer_socket_ = tr_peer_socket_tcp::create(*session_, endpoint_, false);
        if (!peer_socket_) {
            return false;
        }

        peer_socket_->set_error_cb([this](tr_error const& error) {
            peer_socket_.reset();
            connect_error_ = error;
            ++error_count_;
        });

        auto outbuf = tr_peer_socket::PeerBuffer{ "outgoing"sv };
        auto inbuf = tr_peer_socket::PeerBuffer{ "buffered input"sv };
        auto error = tr_error{};
        EXPECT_EQ(0U, peer_socket_->try_write(outbuf, 100, &error));
        EXPECT_EQ(EWOULDBLOCK, error.code());
        EXPECT_TRUE(error.is_transient());
        EXPECT_EQ("outgoing"sv, outbuf.to_string_view());

        error = {};
        EXPECT_EQ(0U, peer_socket_->try_read(inbuf, 100, &error));
        EXPECT_EQ(EWOULDBLOCK, error.code());
        EXPECT_TRUE(error.is_transient());
        EXPECT_EQ("buffered input"sv, inbuf.to_string_view());

        EXPECT_EQ(0U, peer_socket_->try_write(outbuf, 100, nullptr));
        EXPECT_EQ(0U, peer_socket_->try_read(inbuf, 100, nullptr));
        EXPECT_EQ("outgoing"sv, outbuf.to_string_view());
        EXPECT_EQ("buffered input"sv, inbuf.to_string_view());

        peer_socket_->set_read_enabled(true);
        peer_socket_->set_write_enabled(true);
        peer_socket_->set_read_enabled(false);
        peer_socket_->set_write_enabled(false);

        tr_net_close_socket(std::exchange(endpoint_sock_, TR_BAD_SOCKET));
        return true;
    });

    if (!asynchronous) {
        GTEST_SKIP() << "connect() rejected the non-listening port synchronously";
    }

    ASSERT_TRUE(waitForInSessionThread([this] { return error_count_ != 0; }, 15000));
    blockingRunInSessionThread([this] {
        EXPECT_EQ(1U, error_count_);
        EXPECT_EQ(ECONNREFUSED, connect_error_.code());
        EXPECT_EQ(tr_net_strerror(ECONNREFUSED), connect_error_.message());
        EXPECT_FALSE(peer_socket_);
    });
}

TEST_F(PeerSocketTcpTest, completesWithoutPayloadPollingAndAcceptedSocketIsReadyImmediately)
{
    ASSERT_EQ(0, listen(endpoint_sock_, 1));
    blockingRunInSessionThread([this] {
        peer_socket_ = tr_peer_socket_tcp::create(*session_, endpoint_, false);
        ASSERT_TRUE(peer_socket_);
        peer_socket_->set_read_enabled(false);
        peer_socket_->set_write_enabled(false);
    });
    ASSERT_TRUE(acceptPeer());

    static auto constexpr Outgoing = "outgoing"sv;
    static auto constexpr Incoming = "incoming"sv;
    auto outbuf = tr_peer_socket::PeerBuffer{ Outgoing };
    ASSERT_TRUE(waitForInSessionThread(
        [this, &outbuf] {
            auto error = tr_error{};
            auto const n_written = peer_socket_->try_write(outbuf, outbuf.size(), &error);
            EXPECT_TRUE(!error || error.is_transient()) << error;
            return n_written > 0 && outbuf.empty();
        },
        5000));

    auto peek = std::array<char, Outgoing.size()>{};
    ASSERT_TRUE(waitFor(
        [this, &peek] {
            return std::cmp_equal(
                recv(remote_sock_, peek.data(), TR_IF_WIN32(static_cast<int>(peek.size()), peek.size()), MSG_PEEK),
                peek.size());
        },
        5000));

    blockingRunInSessionThread([this] {
        auto const accepted = tr_peer_socket_tcp::create(*session_, endpoint_, std::exchange(remote_sock_, TR_BAD_SOCKET));
        auto inbuf = tr_peer_socket::PeerBuffer{};
        auto error = tr_error{};
        EXPECT_EQ(Outgoing.size(), accepted->try_read(inbuf, Outgoing.size(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_EQ(Outgoing, inbuf.to_string_view());

        auto reply = tr_peer_socket::PeerBuffer{ Incoming };
        EXPECT_EQ(Incoming.size(), accepted->try_write(reply, reply.size(), &error));
        EXPECT_FALSE(error) << error;
        EXPECT_TRUE(reply.empty());
    });

    auto inbuf = tr_peer_socket::PeerBuffer{};
    ASSERT_TRUE(waitForInSessionThread(
        [this, &inbuf] {
            auto error = tr_error{};
            auto const n_read = peer_socket_->try_read(inbuf, Incoming.size() - inbuf.size(), &error);
            EXPECT_TRUE(!error || error.is_transient()) << error;
            return n_read > 0 && inbuf.size() == Incoming.size();
        },
        5000));
    EXPECT_EQ(Incoming, inbuf.to_string_view());
}

TEST_F(PeerSocketTcpTest, completesWithExhaustedUploadQuota)
{
    ASSERT_EQ(0, listen(endpoint_sock_, 1));
    static auto constexpr Outgoing = "outgoing"sv;
    static auto constexpr Incoming = "incoming"sv;
    blockingRunInSessionThread([this] {
        quota_.set_limited(tr_direction::Up, true);
        quota_.set_desired_speed(tr_direction::Up, {});
        peer_io_ = tr_peerIo::new_outgoing(session_, &quota_, endpoint_, {}, false, false);
        ASSERT_TRUE(peer_io_);
        peer_io_->set_callbacks(
            nullptr,
            nullptr,
            [](tr_peerIo*, tr_error const& error, void* context) { *static_cast<tr_error*>(context) = error; },
            &connect_error_);
        peer_io_->write_bytes(Outgoing.data(), Outgoing.size(), false);
        EXPECT_EQ(0U, peer_io_->flush(tr_direction::Up, Outgoing.size()));
        EXPECT_FALSE(peer_io_->has_bandwidth_left(tr_direction::Up));
        peer_io_->set_enabled(tr_direction::Down, true);
    });
    ASSERT_TRUE(acceptPeer());
    ASSERT_EQ(
        Incoming.size(),
        send(remote_sock_, Incoming.data(), TR_IF_WIN32(static_cast<int>(Incoming.size()), Incoming.size()), 0));
    ASSERT_TRUE(
        waitForInSessionThread([this] { return connect_error_ || peer_io_->read_buffer_size() == Incoming.size(); }, 5000));
    blockingRunInSessionThread([this] {
        EXPECT_FALSE(connect_error_) << connect_error_;
        EXPECT_TRUE(peer_io_->read_buffer_starts_with(Incoming));
        EXPECT_FALSE(peer_io_->has_bandwidth_left(tr_direction::Up));
    });
}

TEST_F(PeerSocketTcpTest, retriesBufferedWritesAfterConnecting)
{
    ASSERT_EQ(0, listen(endpoint_sock_, 1));
    static auto constexpr Outgoing = "outgoing"sv;
    blockingRunInSessionThread([this] {
        peer_io_ = tr_peerIo::new_outgoing(session_, &quota_, endpoint_, {}, false, false);
        ASSERT_TRUE(peer_io_);
        peer_io_->write_bytes(Outgoing.data(), Outgoing.size(), false);
        peer_io_->flush(tr_direction::Up, Outgoing.size());
    });
    ASSERT_TRUE(acceptPeer());

    auto peek = std::array<char, Outgoing.size()>{};
    ASSERT_TRUE(waitFor(
        [this, &peek] {
            return std::cmp_equal(
                recv(remote_sock_, peek.data(), TR_IF_WIN32(static_cast<int>(peek.size()), peek.size()), MSG_PEEK),
                peek.size());
        },
        5000));
    EXPECT_EQ(Outgoing, (std::string_view{ peek.data(), peek.size() }));
}

} // namespace tr::test
