// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#ifdef WITH_UTP

#include <array>
#include <cerrno> // ENOBUFS
#include <cstddef> // size_t, std::byte
#include <cstdint> // SIZE_MAX
#include <deque>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <gtest/gtest.h>

#include <libutp/utp.h>

#include <libtransmission/error.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-socket-utp.h>
#include <libtransmission/peer-socket.h>

#include "fake-timer.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

// Two libutp contexts joined by an in-memory network: the sender is a bare libutp socket,
// the receiver a tr_peer_socket_utp. Nothing runs unless the test calls it, and libutp's
// clock moves only when the test advances it.
class PeerSocketUtpTest : public ::testing::Test
{
protected:
    struct Endpoint {
        utp_context* ctx = nullptr;
        tr_socket_address address;
        Endpoint* peer = nullptr;
        PeerSocketUtpTest* test = nullptr;
        std::deque<std::vector<std::byte>> inbox;
        size_t n_received = 0;
    };

    void SetUp() override
    {
        sender_.address = { *tr_address::from_string("10.0.0.1"sv), tr_port::from_host(6881) };
        receiver_.address = { *tr_address::from_string("10.0.0.2"sv), tr_port::from_host(6882) };
        sender_.peer = &receiver_;
        receiver_.peer = &sender_;

        for (auto* const endpoint : { &sender_, &receiver_ }) {
            endpoint->test = this;
            endpoint->ctx = utp_init(2);
            utp_context_set_userdata(endpoint->ctx, endpoint);
            utp_set_callback(endpoint->ctx, UTP_SENDTO, onSendto);
            utp_set_callback(endpoint->ctx, UTP_GET_MILLISECONDS, onGetMilliseconds);
            utp_set_callback(endpoint->ctx, UTP_GET_MICROSECONDS, onGetMicroseconds);
        }

        tr_peer_socket_utp::init(receiver_.ctx);
        utp_context_set_option(receiver_.ctx, UTP_RCVBUF, ReceiveWindow);
        utp_set_callback(receiver_.ctx, UTP_ON_ACCEPT, onAccept);
    }

    void TearDown() override
    {
        accepted_.reset();
        if (sock_ != nullptr) {
            utp_close(sock_);
        }
        utp_destroy(sender_.ctx);
        utp_destroy(receiver_.ctx);
    }

    // Delivers packets until none are in flight, 1 ms per hop so that libutp measures a
    // delay and grows its congestion window. Like tr-udp, each endpoint issues its
    // deferred ACKs after a batch of incoming packets and at no other time.
    void deliverAll()
    {
        while (!std::empty(sender_.inbox) || !std::empty(receiver_.inbox)) {
            for (auto* const endpoint : { &sender_, &receiver_ }) {
                if (std::empty(endpoint->inbox)) {
                    continue;
                }

                ++now_msec_;
                auto const [from, fromlen] = endpoint->peer->address.to_sockaddr();
                for (auto batch = std::exchange(endpoint->inbox, {}); auto const& packet : batch) {
                    ++endpoint->n_received;
                    utp_process_udp(
                        endpoint->ctx,
                        reinterpret_cast<byte const*>(std::data(packet)),
                        std::size(packet),
                        reinterpret_cast<sockaddr const*>(&from),
                        fromlen);
                }
                utp_issue_deferred_acks(endpoint->ctx);
            }
        }
    }

    void connect()
    {
        sock_ = utp_create_socket(sender_.ctx);
        auto const [to, tolen] = receiver_.address.to_sockaddr();
        ASSERT_EQ(0, utp_connect(sock_, reinterpret_cast<sockaddr const*>(&to), tolen));
        deliverAll();
        ASSERT_NE(nullptr, accepted_);
    }

    // Writes twice the receive window, so the window fills with data still queued behind it.
    // Writing more risks filling libutp's outgoing packet buffer with unsent packets, which
    // blocks the socket for good.
    void writeTwiceTheWindow()
    {
        for (auto total = size_t{}; total < size_t{ 2U } * ReceiveWindow;) {
            auto const n = utp_write(sock_, std::data(chunk_), std::size(chunk_));
            ASSERT_GT(n, 0);
            total += static_cast<size_t>(n);
            deliverAll();
        }
    }

    // Reads everything the receiver holds, as a peer IO does when its reads are enabled.
    size_t readAll()
    {
        auto n_read = size_t{};
        accepted_->set_read_cb([&] {
            auto inbuf = tr_peer_socket::PeerBuffer{};
            auto error = tr_error{};
            n_read = accepted_->try_read(inbuf, SIZE_MAX, &error);
        });
        accepted_->set_read_enabled(true);
        timer_maker_.timers().front()->fire();
        return n_read;
    }

    static constexpr auto ReceiveWindow = 64 * 1024;

    Endpoint sender_;
    Endpoint receiver_;
    UTPSocket* sock_ = nullptr; // the sender's
    std::shared_ptr<tr_peer_socket_utp> accepted_; // the receiver's
    UTPSocket* accepted_sock_ = nullptr; // accepted_'s libutp socket
    FakeTimerMaker timer_maker_;
    std::array<std::byte, 1024> chunk_ = {};
    uint64_t now_msec_ = 1000;

private:
    static uint64 onSendto(utp_callback_arguments* const args)
    {
        auto const* const self = static_cast<Endpoint*>(utp_context_get_userdata(args->context));
        auto const* const bytes = reinterpret_cast<std::byte const*>(args->buf);
        self->peer->inbox.emplace_back(bytes, bytes + args->len);
        return 0;
    }

    static uint64 onGetMilliseconds(utp_callback_arguments* const args)
    {
        return static_cast<Endpoint*>(utp_context_get_userdata(args->context))->test->now_msec_;
    }

    static uint64 onGetMicroseconds(utp_callback_arguments* const args)
    {
        return onGetMilliseconds(args) * 1000U;
    }

    static uint64 onAccept(utp_callback_arguments* const args)
    {
        auto const* const self = static_cast<Endpoint*>(utp_context_get_userdata(args->context));
        auto* const test = self->test;
        test->accepted_sock_ = args->socket;
        test->accepted_ = tr_peer_socket_utp::create(self->peer->address, args->socket, test->timer_maker_);
        return 0;
    }
};

// A peer IO that stops reading (before its first bandwidth allocation, under a download
// limit, or with a full buffer) lets the receive window fill. Reading again must tell the
// sender the window reopened, because a blocked sender sends nothing to prompt an ACK
// until its keepalive, 29 s later. The sender then sends its queued data on its next
// timeout check, which libutp runs every 500 ms.
TEST_F(PeerSocketUtpTest, readingReopensFullReceiveWindow)
{
    ASSERT_NO_FATAL_FAILURE(connect());
    ASSERT_FALSE(accepted_->is_read_enabled());

    ASSERT_NO_FATAL_FAILURE(writeTwiceTheWindow());
    auto const n_buffered = readAll();
    EXPECT_GT(n_buffered, size_t{ ReceiveWindow } / 2U);
    EXPECT_LE(n_buffered, size_t{ ReceiveWindow });

    deliverAll();
    now_msec_ += 500U;
    utp_check_timeouts(sender_.ctx);
    deliverAll();
    EXPECT_GT(readAll(), size_t{});
}

// A peer IO without bandwidth reads nothing. The socket must then wait for the peer IO
// to enable reads again, even with more buffered than the window, not retry at once.
TEST_F(PeerSocketUtpTest, fullBufferWaitsForReadsEnabled)
{
    connect();
    writeTwiceTheWindow();
    utp_setsockopt(accepted_sock_, UTP_RCVBUF, ReceiveWindow / 2);

    auto n_reads = 0;
    accepted_->set_read_cb([&] { ++n_reads; });
    auto n_errors = 0;
    accepted_->set_error_cb([&](tr_error const& /*error*/) { ++n_errors; });
    accepted_->set_read_enabled(true);
    auto const* const read_timer = timer_maker_.timers().front();
    auto const n_starts = read_timer->startCount();
    read_timer->fire();

    EXPECT_EQ(1, n_reads);
    EXPECT_EQ(0, n_errors);
    EXPECT_EQ(n_starts, read_timer->startCount()); // not rescheduled
}

// A peer that ignores the receive window cannot grow the buffer without bound.
TEST_F(PeerSocketUtpTest, overrunWindowClosesConnection)
{
    connect();
    writeTwiceTheWindow();
    utp_setsockopt(accepted_sock_, UTP_RCVBUF, ReceiveWindow / 4);

    auto n_reads = 0;
    accepted_->set_read_cb([&] { ++n_reads; });
    auto error_code = 0;
    accepted_->set_error_cb([&](tr_error const& error) { error_code = error.code(); });
    accepted_->set_read_enabled(true);
    timer_maker_.timers().front()->fire();

    EXPECT_EQ(0, n_reads);
    EXPECT_EQ(ENOBUFS, error_code);
}

// Enabling reads again after the report does not report the overrun again.
TEST_F(PeerSocketUtpTest, overrunIsReportedOnce)
{
    connect();
    writeTwiceTheWindow();
    utp_setsockopt(accepted_sock_, UTP_RCVBUF, ReceiveWindow / 4);

    auto n_reads = 0;
    accepted_->set_read_cb([&] { ++n_reads; });
    auto n_errors = 0;
    accepted_->set_error_cb([&](tr_error const& /*error*/) { ++n_errors; });
    accepted_->set_read_enabled(true);
    auto const* const read_timer = timer_maker_.timers().front();
    read_timer->fire();
    ASSERT_EQ(1, n_errors);

    auto const n_starts = read_timer->startCount();
    accepted_->set_read_enabled(true);
    EXPECT_EQ(n_starts, read_timer->startCount()); // the timer that reports it is not armed again
    EXPECT_EQ(0, n_reads);
}

// The overrun is reported even when the peer IO stops reading before the report goes out.
TEST_F(PeerSocketUtpTest, overrunIsReportedAfterReadsStop)
{
    connect();
    writeTwiceTheWindow();
    utp_setsockopt(accepted_sock_, UTP_RCVBUF, ReceiveWindow / 4);

    auto error_code = 0;
    accepted_->set_error_cb([&](tr_error const& error) { error_code = error.code(); });
    accepted_->set_read_enabled(true);
    accepted_->set_read_enabled(false);
    auto const* const read_timer = timer_maker_.timers().front();
    ASSERT_TRUE(read_timer->isRunning());
    read_timer->fire();

    EXPECT_EQ(ENOBUFS, error_code);
}

// A peer IO queues its first message (the handshake) while the connection is still
// being set up, and does not enable writes for a short one. It must go out when the
// connection comes up, not on the next bandwidth pulse.
TEST_F(PeerSocketUtpTest, connectingWritesQueuedData)
{
    tr_peer_socket_utp::init(sender_.ctx);
    auto const outgoing = tr_peer_socket_utp::create(receiver_.address, sender_.ctx, timer_maker_);
    ASSERT_NE(nullptr, outgoing);

    auto n_writes = 0;
    outgoing->set_write_cb([&] { ++n_writes; });
    ASSERT_FALSE(outgoing->is_write_enabled());

    deliverAll();
    EXPECT_NE(nullptr, accepted_);
    EXPECT_EQ(1, n_writes);
}

// While the window has room for a packet, the sender keeps sending, so the window update
// from a read waits for the ACK of the sender's next batch instead of costing its own packet.
TEST_F(PeerSocketUtpTest, readingWithRoomLeftSendsNoExtraAck)
{
    ASSERT_NO_FATAL_FAILURE(connect());
    auto const n_written = utp_write(sock_, std::data(chunk_), std::size(chunk_));
    ASSERT_GT(n_written, 0);
    deliverAll();

    EXPECT_EQ(static_cast<size_t>(n_written), readAll());
    EXPECT_TRUE(std::empty(sender_.inbox));
}

} // namespace
} // namespace tr::test

#endif // WITH_UTP
