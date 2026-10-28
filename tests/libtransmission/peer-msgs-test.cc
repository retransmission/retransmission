// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h>
#include <libtransmission/torrent.h>

#include "test-fixtures.h"

namespace tr::test
{
class PeerMsgsTest : public SessionTest
{
protected:
    void TearDown() override
    {
        if (peer_socket_ != EVUTIL_INVALID_SOCKET) {
            tr_net_close_socket(peer_socket_);
        }
        SessionTest::TearDown();
    }

    void connectPeer(tr_torrent* tor, bool remote_is_seed)
    {
        tr_torrentStart(tor);
        ASSERT_TRUE(waitForInSessionThread([tor]() { return tor->is_running(); }, 2000));
        // Let the torrent-start rechoke run before a peer arrives.
        std::this_thread::sleep_for(300ms);

        auto sockets = std::to_array<evutil_socket_t>({ EVUTIL_INVALID_SOCKET, EVUTIL_INVALID_SOCKET });
        ASSERT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, std::data(sockets)));
        peer_socket_ = sockets[1];
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));

        auto wire = std::string{ "\023BitTorrent protocol" };
        wire.append(8, '\0');
        auto const& hash = tor->info_hash();
        wire.append(reinterpret_cast<char const*>(std::data(hash)), std::size(hash));
        wire.append("-TEST00-000000000000");

        auto pieces = tr_bitfield{ tor->piece_count() };
        if (remote_is_seed) {
            pieces.set_has_all();
        }
        auto const raw = pieces.raw();
        auto const length = static_cast<uint32_t>(std::size(raw) + 1U);
        for (auto shift : { 24U, 16U, 8U, 0U }) {
            wire.push_back(static_cast<char>(length >> shift));
        }
        wire.push_back('\5'); // Bitfield
        wire.append(reinterpret_cast<char const*>(std::data(raw)), std::size(raw));
        if (!remote_is_seed) {
            wire.append("\0\0\0\1\2", 5); // Interested
        }
        ASSERT_EQ(static_cast<int>(std::size(wire)), send(peer_socket_, std::data(wire), std::size(wire), 0));

        blockingRunInSessionThread([this, fd = sockets[0]]() {
            auto const address = tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_host(48000) };
            tr_peerMgrAddIncoming(peerManager(), tr_peer_socket_tcp::create(*session_, address, static_cast<tr_socket_t>(fd)));
        });
        ASSERT_TRUE(waitForInSessionThread([tor]() { return std::size(tr_peerMgrPeerStats(tor)) == 1U; }, 2000));
    }

    evutil_socket_t peer_socket_ = EVUTIL_INVALID_SOCKET;
};

TEST_F(PeerMsgsTest, NewSeedPromptsInterestBeforePeriodicRechoke)
{
    auto* tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);
    connectPeer(tor, true);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(waitForInSessionThread(
        [tor]() {
            auto const peers = tr_peerMgrPeerStats(tor);
            return std::size(peers) == 1U && peers.front().client_is_interested;
        },
        2000));
}

TEST_F(PeerMsgsTest, NewDownloaderIsUnchokedBeforePeriodicRechoke)
{
    auto* tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    connectPeer(tor, false);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(waitForInSessionThread(
        [tor]() {
            auto const peers = tr_peerMgrPeerStats(tor);
            return std::size(peers) == 1U && peers.front().peer_is_interested && !peers.front().peer_is_choked;
        },
        2000));
}

TEST_F(PeerMsgsTest, SmallProtocolMessagesFlushWithoutBandwidthPulse)
{
    auto sockets = std::to_array<evutil_socket_t>({ EVUTIL_INVALID_SOCKET, EVUTIL_INVALID_SOCKET });
    ASSERT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, std::data(sockets)));
    peer_socket_ = sockets[1];
    ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
    ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));

    auto io = std::shared_ptr<tr_peerIo>{};
    blockingRunInSessionThread([this, &io, fd = sockets[0]]() {
        auto const address = tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_host(48000) };
        // Leave this I/O outside the bandwidth tree so its periodic flush
        // cannot conceal a stalled message.
        io = tr_peerIo::new_incoming(
            session_,
            nullptr,
            tr_peer_socket_tcp::create(*session_, address, static_cast<tr_socket_t>(fd)));
    });

    // Interest, one request, seven requests, and batches at/above the old
    // flush threshold. Split writes model protocol headers and bodies.
    for (auto const size : { 5U, 17U, 119U, 128U, 256U }) {
        auto const expected = std::string(size, static_cast<char>(size));
        auto received = std::string{};
        blockingRunInSessionThread([&io, &expected]() {
            io->write_bytes(expected.data(), 4U, false);
            io->write_bytes(expected.data() + 4U, expected.size() - 4U, false);
        });
        EXPECT_TRUE(waitForInSessionThread(
            [this, &received, size]() {
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

    blockingRunInSessionThread([&io]() { io.reset(); });
}
} // namespace tr::test
