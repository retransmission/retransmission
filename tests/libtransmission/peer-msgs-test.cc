// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-msgs.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{
class PeerMsgsTest : public SessionTest
{
protected:
    void TearDown() override
    {
        if (is_valid_socket(peer_socket_)) {
            tr_net_close_socket(peer_socket_);
        }
        SessionTest::TearDown();
    }

    // An I/O over a socketpair, left outside the bandwidth tree
    // so its periodic flush cannot conceal a stalled message.
    [[nodiscard]] std::shared_ptr<tr_peerIo> makeIo()
    {
        auto sockets = std::array<evutil_socket_t, 2>{};
        EXPECT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, std::data(sockets)));
        peer_socket_ = static_cast<tr_socket_t>(sockets[1]);
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        EXPECT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));

        return blockingRunInSessionThread([this, fd = sockets[0]] {
            auto const address = tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_host(48000) };
            return tr_peerIo::new_incoming(
                session_,
                nullptr,
                tr_peer_socket_tcp::create(*session_, address, static_cast<tr_socket_t>(fd)));
        });
    }

    // Appends whatever the peer has received to `received`.
    void receive(std::string& received) const
    {
        auto buf = std::array<char, 256>{};
        if (auto const n = recv(peer_socket_, std::data(buf), std::size(buf), 0); n > 0) {
            received.append(std::data(buf), static_cast<size_t>(n));
        }
    }

    tr_socket_t peer_socket_ = static_cast<tr_socket_t>(TR_BAD_SOCKET);
};
} // namespace

TEST_F(PeerMsgsTest, smallProtocolMessagesFlushWithoutBandwidthPulse)
{
    auto io = makeIo();
    ASSERT_TRUE(io);

    // Interest, one request, seven requests, and larger batches.
    // Split writes model protocol headers and bodies.
    for (auto const size : { 5U, 17U, 119U, 128U, 256U }) {
        auto const expected = std::string(size, static_cast<char>(size));
        auto received = std::string{};
        blockingRunInSessionThread([&io, &expected] {
            io->write_bytes(expected.data(), 4U, false);
            io->write_bytes(expected.data() + 4U, expected.size() - 4U, false);
        });
        EXPECT_TRUE(waitForInSessionThread(
            [this, &received, size] {
                receive(received);
                return received.size() >= size;
            },
            5000))
            << "message size: " << size;
        EXPECT_EQ(expected, received);
    }

    blockingRunInSessionThread([&io] { io.reset(); });
}

TEST_F(PeerMsgsTest, deferredWriteWaitsForNextFlush)
{
    auto io = makeIo();
    ASSERT_TRUE(io);

    // A HAVE, then a request.
    auto const deferred = std::string(9U, 'h');
    auto const urgent = std::string(17U, 'r');

    blockingRunInSessionThread(
        [&io, &deferred] { io->write_bytes(deferred.data(), deferred.size(), false, tr_peerIo::Flush::WithNext); });

    // Long enough for an undeferred write to be sent.
    std::this_thread::sleep_for(50ms);
    auto received = std::string{};
    EXPECT_TRUE(blockingRunInSessionThread([this, &received] {
        receive(received);
        return received.empty();
    })) << "a deferred write was sent on its own";

    blockingRunInSessionThread([&io, &urgent] { io->write_bytes(urgent.data(), urgent.size(), false); });
    EXPECT_TRUE(waitForInSessionThread(
        [this, &received, n = deferred.size() + urgent.size()] {
            receive(received);
            return received.size() >= n;
        },
        5000));
    EXPECT_EQ(deferred + urgent, received);

    blockingRunInSessionThread([&io] { io.reset(); });
}
TEST_F(PeerMsgsTest, haveWaitsForInterestedMessage)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    ASSERT_NE(nullptr, tor);
    auto io = makeIo();
    ASSERT_TRUE(io);

    auto msgs = blockingRunInSessionThread([tor, &io] {
        auto info = std::make_shared<tr_peer_info>(*tr_address::from_string("127.0.0.1"), uint8_t{}, TR_PEER_FROM_INCOMING, [] {
            return tr_port{};
        });
        return tr_peerMsgs::create(*tor, std::move(info), io, tr_peer_id_t{}, [](auto*, auto const&, void*) {}, nullptr);
    });
    ASSERT_TRUE(msgs);

    // The bitfield sent on connect.
    auto received = std::string{};
    ASSERT_TRUE(waitForInSessionThread(
        [this, &received] {
            receive(received);
            return !received.empty();
        },
        5000));
    std::this_thread::sleep_for(50ms);
    blockingRunInSessionThread([this, &received] { receive(received); });
    received.clear();

    blockingRunInSessionThread([&msgs] { msgs->on_piece_completed(0U); });
    std::this_thread::sleep_for(50ms);
    EXPECT_TRUE(blockingRunInSessionThread([this, &received] {
        receive(received);
        return received.empty();
    })) << "a HAVE was sent on its own";

    blockingRunInSessionThread([&msgs] { msgs->set_interested(true); });
    static auto constexpr Have = "\x00\x00\x00\x05\x04\x00\x00\x00\x00"sv;
    static auto constexpr Interested = "\x00\x00\x00\x01\x02"sv;
    EXPECT_TRUE(waitForInSessionThread(
        [this, &received] {
            receive(received);
            return received.size() >= Have.size() + Interested.size();
        },
        5000));
    EXPECT_EQ(std::string{ Have } + std::string{ Interested }, received);

    blockingRunInSessionThread([&msgs, &io] {
        msgs.reset();
        io.reset();
    });
}
} // namespace tr::test
