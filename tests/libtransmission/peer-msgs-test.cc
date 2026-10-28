// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <memory>
#include <string>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h>

#include "test-fixtures.h"

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

    tr_socket_t peer_socket_ = static_cast<tr_socket_t>(TR_BAD_SOCKET);
};
} // namespace

TEST_F(PeerMsgsTest, SmallProtocolMessagesFlushWithoutBandwidthPulse)
{
    auto sockets = std::array<evutil_socket_t, 2>{};
    ASSERT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, std::data(sockets)));
    peer_socket_ = static_cast<tr_socket_t>(sockets[1]);
    ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
    ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));

    auto io = std::shared_ptr<tr_peerIo>{};
    blockingRunInSessionThread([this, &io, fd = sockets[0]] {
        auto const address = tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_host(48000) };
        // Leave this I/O outside the bandwidth tree so its periodic flush
        // cannot conceal a stalled message.
        io = tr_peerIo::new_incoming(
            session_,
            nullptr,
            tr_peer_socket_tcp::create(*session_, address, static_cast<tr_socket_t>(fd)));
    });

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
                auto buf = std::array<char, 256>{};
                if (auto const n = recv(peer_socket_, std::data(buf), std::size(buf), 0); n > 0) {
                    received.append(std::data(buf), static_cast<size_t>(n));
                }
                return received.size() >= size;
            },
            100))
            << "message size: " << size;
        EXPECT_EQ(expected, received);
    }

    blockingRunInSessionThread([&io] { io.reset(); });
}
} // namespace tr::test
