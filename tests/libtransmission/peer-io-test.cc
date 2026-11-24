// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#ifdef WITH_UTP

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <libtransmission/bandwidth.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/session.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

class PeerIoTest : public SessionTest
{
protected:
    void TearDown() override
    {
        blockingRunInSessionThread([this] { peer_io_.reset(); });
        if (is_valid_socket(endpoint_sock_)) {
            tr_net_close_socket(endpoint_sock_);
        }
        if (is_valid_socket(udp_endpoint_sock_)) {
            tr_net_close_socket(udp_endpoint_sock_);
        }

        SessionTest::TearDown();
    }

    tr_socket_t endpoint_sock_ = TR_BAD_SOCKET;
    tr_socket_t udp_endpoint_sock_ = TR_BAD_SOCKET;
    tr_bandwidth quota_;
    std::shared_ptr<tr_peerIo> peer_io_;
};

} // namespace

TEST_F(PeerIoTest, reconnectsInPreferredTransportOrder)
{
    endpoint_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(is_valid_socket(endpoint_sock_)) << tr_net_error(sockerrno);
    ASSERT_EQ(0, evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(endpoint_sock_))) << tr_net_error(sockerrno);
    auto [addr, addrlen] = tr_socket_address::to_sockaddr(*tr_address::from_string("127.0.0.1"sv), {});
    ASSERT_EQ(0, bind(endpoint_sock_, reinterpret_cast<sockaddr const*>(&addr), addrlen)) << tr_net_error(sockerrno);
    ASSERT_EQ(0, getsockname(endpoint_sock_, reinterpret_cast<sockaddr*>(&addr), &addrlen)) << tr_net_error(sockerrno);
    auto const endpoint = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&addr));
    ASSERT_TRUE(endpoint);

    ASSERT_EQ(0, listen(endpoint_sock_, 8)) << tr_net_error(sockerrno);
    udp_endpoint_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_TRUE(is_valid_socket(udp_endpoint_sock_)) << tr_net_error(sockerrno);
    ASSERT_EQ(0, bind(udp_endpoint_sock_, reinterpret_cast<sockaddr const*>(&addr), addrlen)) << tr_net_error(sockerrno);

    blockingRunInSessionThread([this, endpoint = *endpoint] {
        for (
            auto const transports : {
                std::array{ tr_transport_protocol::UTP, tr_transport_protocol::TCP },
                std::array{ tr_transport_protocol::TCP, tr_transport_protocol::UTP },
            }) {
            SCOPED_TRACE(std::string{ tr_transport_protocol_to_sv(transports.front()) });
            auto settings = tr_variant::Vector{};
            for (auto const protocol : transports) {
                settings.emplace_back(protocol == tr_transport_protocol::UTP ? "utp"sv : "tcp"sv);
            }
            ASSERT_TRUE(session_->load_preferred_transports(tr_variant{ std::move(settings) }));

            peer_io_ = tr_peerIo::new_outgoing(session_, &quota_, endpoint, {}, false, true);
            ASSERT_TRUE(peer_io_);
            EXPECT_EQ(transports[0], peer_io_->transport_protocol());
            ASSERT_TRUE(peer_io_->reconnect(true));
            EXPECT_EQ(transports[0], peer_io_->transport_protocol());
            ASSERT_TRUE(peer_io_->reconnect(false));
            EXPECT_EQ(transports[1], peer_io_->transport_protocol());
            EXPECT_FALSE(peer_io_->reconnect(false));

            peer_io_ = tr_peerIo::new_outgoing(session_, &quota_, endpoint, {}, false, false);
            ASSERT_TRUE(peer_io_);
            EXPECT_EQ(tr_transport_protocol::TCP, peer_io_->transport_protocol());
            ASSERT_TRUE(peer_io_->reconnect(true));
            EXPECT_EQ(tr_transport_protocol::TCP, peer_io_->transport_protocol());
            EXPECT_FALSE(peer_io_->reconnect(false));
            peer_io_.reset();
        }
    });
}

} // namespace tr::test

#endif // WITH_UTP
