// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-socket-tcp.h>
#include <libtransmission/session.h>
#include <libtransmission/torrent.h>

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
        for (auto const sock : peer_sockets_) {
            tr_net_close_socket(sock);
        }
        SessionTest::TearDown();
    }

    // Each peer needs its own port, or the peer manager treats it as already connected.
    void connectPeer(tr_torrent* const tor, bool const remote_is_seed, uint16_t const port = 48000U)
    {
        if (!tor->is_running()) {
            tr_torrentStart(tor);
            ASSERT_TRUE(waitForInSessionThread([tor] { return tor->is_running(); }, 2000));
        }
        auto const peer_count = std::size(tr_peerMgrPeerStats(tor)) + 1U;

        auto sockets = std::array<evutil_socket_t, 2>{};
        ASSERT_EQ(0, evutil_socketpair(TR_IF_WIN32(AF_INET, AF_UNIX), SOCK_STREAM, 0, std::data(sockets)));
        auto const peer_socket = static_cast<tr_socket_t>(sockets[1]);
        peer_sockets_.push_back(peer_socket);
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[0]));
        ASSERT_EQ(0, evutil_make_socket_nonblocking(sockets[1]));

        auto wire = std::string{ "\023BitTorrent protocol" };
        wire.append(8, '\0');
        auto const& hash = tor->info_hash();
        wire.append(reinterpret_cast<char const*>(std::data(hash)), std::size(hash));
        wire.append("-TEST00-000000000000");
        ASSERT_EQ(static_cast<int>(std::size(wire)), send(peer_socket, std::data(wire), std::size(wire), 0));

        blockingRunInSessionThread([this, fd = sockets[0], port] {
            auto const address = tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_host(port) };
            tr_peerMgrAddIncoming(peerManager(), tr_peer_socket_tcp::create(*session_, address, static_cast<tr_socket_t>(fd)));
        });
        ASSERT_TRUE(
            waitForInSessionThread([tor, peer_count] { return std::size(tr_peerMgrPeerStats(tor)) == peer_count; }, 2000));

        // Real peers send their bitfield a round trip after the handshake.
        std::this_thread::sleep_for(300ms);

        wire.clear();
        auto pieces = tr_bitfield{ tor->piece_count() };
        if (remote_is_seed) {
            pieces.set_has_all();
        }
        auto const raw = pieces.raw();
        auto const length = static_cast<uint32_t>(std::size(raw) + 1U);
        for (auto const shift : { 24U, 16U, 8U, 0U }) {
            wire.push_back(static_cast<char>(length >> shift));
        }
        wire.push_back('\5'); // Bitfield
        wire.append(reinterpret_cast<char const*>(std::data(raw)), std::size(raw));
        if (!remote_is_seed) {
            wire.append("\0\0\0\1\2", 5); // Interested
        }
        ASSERT_EQ(static_cast<int>(std::size(wire)), send(peer_socket, std::data(wire), std::size(wire), 0));
    }

    std::vector<tr_socket_t> peer_sockets_;
};

class PeerMsgsOneSlotTest : public PeerMsgsTest
{
protected:
    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_upload_slots_per_torrent, 1);
        PeerMsgsTest::SetUp();
    }
};
} // namespace

TEST_F(PeerMsgsTest, NewSeedPromptsInterestBeforePeriodicRechoke)
{
    auto* tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);
    connectPeer(tor, true);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(waitForInSessionThread(
        [tor] {
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
        [tor] {
            auto const peers = tr_peerMgrPeerStats(tor);
            return std::size(peers) == 1U && peers.front().peer_is_interested && !peers.front().peer_is_choked;
        },
        2000));
}

TEST_F(PeerMsgsOneSlotTest, DownloaderBeyondUploadSlotsStaysChoked)
{
    auto* tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);

    connectPeer(tor, false, 48000U);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(waitForInSessionThread(
        [tor] {
            auto const peers = tr_peerMgrPeerStats(tor);
            return std::size(peers) == 1U && !peers.front().peer_is_choked;
        },
        2000));

    // The unchoke decision is made when Interested arrives, so once both peers are
    // interested the second one has already been refused the slot.
    connectPeer(tor, false, 48001U);
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(waitForInSessionThread(
        [tor] {
            auto const peers = tr_peerMgrPeerStats(tor);
            return std::ranges::count_if(peers, [](auto const& peer) { return peer.peer_is_interested; }) == 2;
        },
        2000));

    auto const unchoked = blockingRunInSessionThread([tor] {
        auto const peers = tr_peerMgrPeerStats(tor);
        return std::ranges::count_if(peers, [](auto const& peer) { return !peer.peer_is_choked; });
    });
    EXPECT_EQ(1, unchoked);
}
} // namespace tr::test
