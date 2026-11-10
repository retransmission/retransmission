// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/error.h>
#include <libtransmission/net.h>
#include <libtransmission/peer-io.h>
#include <libtransmission/session.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

// Long enough for a loopback handshake that the listener accepts,
// short against the 1 s SYN retransmit of one it drops.
auto constexpr ConnectProbeMsec = 100;

[[nodiscard]] bool is_writable(tr_socket_t const sock, int const timeout_msec)
{
    auto fds = fd_set{};
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    auto timeout = timeval{ .tv_sec = 0, .tv_usec = timeout_msec * 1000 };
    return select(static_cast<int>(sock) + 1, nullptr, &fds, nullptr, &timeout) > 0;
}

class PeerSocketTcpTest : public SessionTest
{
protected:
    void TearDown() override
    {
        for (auto const sock : sockets_) {
            tr_net_close_socket(sock);
        }
        SessionTest::TearDown();
    }

    [[nodiscard]] tr_socket_t track(tr_socket_t const sock)
    {
        if (is_valid_socket(sock)) {
            sockets_.push_back(sock);
        }
        return sock;
    }

    // A loopback listener whose accept queue is full, so the kernel drops
    // every new SYN and the next connect() stays in progress.
    [[nodiscard]] std::optional<tr_socket_address> make_full_listener()
    {
        listener_ = track(socket(AF_INET, SOCK_STREAM, 0));
        if (!is_valid_socket(listener_)) {
            return {};
        }

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        auto addrlen = socklen_t{ sizeof(addr) };
        if (bind(listener_, reinterpret_cast<sockaddr const*>(&addr), addrlen) != 0 || listen(listener_, 0) != 0 ||
            getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &addrlen) != 0 ||
            evutil_make_socket_nonblocking(listener_) != 0) {
            return {};
        }

        for (int i = 0; i < 16; ++i) {
            auto const filler = socket(AF_INET, SOCK_STREAM, 0);
            if (!is_valid_socket(filler) || evutil_make_socket_nonblocking(filler) != 0) {
                return {};
            }

            [[maybe_unused]] auto const ret = connect(filler, reinterpret_cast<sockaddr const*>(&addr), addrlen);
            if (!is_writable(filler, ConnectProbeMsec)) {
                // This SYN was dropped: the queue is full.
                tr_net_close_socket(filler);
                return tr_socket_address{ *tr_address::from_string("127.0.0.1"), tr_port::from_network(addr.sin_port) };
            }

            fillers_.push_back(track(filler));
        }

        return {};
    }

    // Accept every queued connection and return the last one.
    [[nodiscard]] tr_socket_t drain_accept_queue()
    {
        auto last = static_cast<tr_socket_t>(TR_BAD_SOCKET);
        for (;;) {
            auto const sock = accept(listener_, nullptr, nullptr);
            if (!is_valid_socket(sock)) {
                return last;
            }
            last = track(sock);
            evutil_make_socket_nonblocking(last);
        }
    }

    tr_socket_t listener_ = static_cast<tr_socket_t>(TR_BAD_SOCKET);
    std::vector<tr_socket_t> fillers_;
    std::vector<tr_socket_t> sockets_;
};
} // namespace

// The plaintext BitTorrent handshake is 68 bytes. Sending it before connect()
// finishes must leave the connection usable: macOS returns ENOTCONN there,
// where Linux returns EAGAIN.
TEST_F(PeerSocketTcpTest, writeBeforeConnectFinishesIsSentAfterConnect)
{
    auto const address = make_full_listener();
    ASSERT_TRUE(address) << "could not fill the listener's accept queue";

    auto const expected = std::string(68, 'h');
    auto io = std::shared_ptr<tr_peerIo>{};
    auto error = std::optional<tr_error>{};
    auto const callbacks_ok = blockingRunInSessionThread([&] {
        io = tr_peerIo::new_outgoing(session_, nullptr, *address, tr_sha1_digest_t{}, false, false);
        if (!io) {
            return false;
        }
        io->set_callbacks(
            nullptr,
            nullptr,
            [](tr_peerIo* /*io*/, tr_error const& err, void* vdata) { *static_cast<std::optional<tr_error>*>(vdata) = err; },
            &error);
        io->write_bytes(std::data(expected), std::size(expected), false);
        io->flush(tr_direction::Up, std::size(expected));
        return true;
    });
    ASSERT_TRUE(callbacks_ok) << "could not open the outgoing peer socket";

    auto const early_error = blockingRunInSessionThread([&] { return error; });
    EXPECT_FALSE(early_error) << "write before connect finished failed: " << early_error->code() << " ("
                              << early_error->message() << ")";

    // Make room so the retransmitted SYN is accepted.
    std::ignore = drain_accept_queue();

    auto received = std::string{};
    auto peer = static_cast<tr_socket_t>(TR_BAD_SOCKET);
    EXPECT_TRUE(waitFor(
        [&] {
            if (!is_valid_socket(peer)) {
                peer = drain_accept_queue();
                if (!is_valid_socket(peer)) {
                    return false;
                }
            }
            auto buf = std::array<char, 128>{};
            if (auto const n = recv(peer, std::data(buf), std::size(buf), 0); n > 0) {
                received.append(std::data(buf), static_cast<size_t>(n));
            }
            return std::size(received) >= std::size(expected);
        },
        MaxWaitMsec));
    EXPECT_EQ(expected, received);

    auto const late_error = blockingRunInSessionThread([&] { return error; });
    EXPECT_FALSE(late_error) << "connection failed: " << late_error->code() << " (" << late_error->message() << ")";

    blockingRunInSessionThread([&io] { io.reset(); });
}
} // namespace tr::test
