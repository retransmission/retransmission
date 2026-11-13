// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // TCP_CONGESTION
#endif

#include <utility> // std::cmp_equal

#include <event2/event.h>

#include <fmt/format.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/peer-socket-tcp.h"
#include "libtransmission/net.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils-ev.h"

#define tr_logAddWarnSock(sock, msg) tr_logAddWarn(msg, (sock)->display_name())
#define tr_logAddDebugSock(sock, msg) tr_logAddDebug(msg, (sock)->display_name())
#define tr_logAddTraceSock(sock, msg) tr_logAddTrace(msg, (sock)->display_name())

namespace
{
[[nodiscard]] tr_socket_t create_socket(int const domain)
{
    auto const sockfd = socket(domain, SOCK_STREAM, 0);
    if (!is_valid_socket(sockfd)) {
        if (sockerrno != EAFNOSUPPORT) {
            tr_logAddWarn(
                fmt::format(
                    "Couldn't create socket: {error} ({error_code})",
                    fmt::arg("error", tr_net_strerror(sockerrno)),
                    fmt::arg("error_code", sockerrno)));
        }
        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sockfd)) == -1) {
        tr_net_close_socket(sockfd);
        return TR_BAD_SOCKET;
    }

    if (static bool buf_logged = false; !buf_logged) {
        int i = 0;
        socklen_t size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&i), &size) != -1) {
            tr_logAddTrace(fmt::format("SO_SNDBUF size is {}", i));
        }

        i = 0;
        size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&i), &size) != -1) {
            tr_logAddTrace(fmt::format("SO_RCVBUF size is {}", i));
        }

        buf_logged = true;
    }

    return sockfd;
}

[[nodiscard]] std::pair<tr_socket_t, bool> open_peer_socket(
    tr_session const& session,
    tr_socket_address const& socket_address,
    bool const client_is_seed)
{
    auto const& [addr, port] = socket_address;

    TR_ASSERT(addr.is_valid());

    if (!session.allowsTCP() || !socket_address.is_valid()) {
        return { TR_BAD_SOCKET, false };
    }

    auto const s = create_socket(tr_ip_protocol_to_af(addr.type));
    if (!is_valid_socket(s)) {
        return { TR_BAD_SOCKET, false };
    }

    if (!tr_netSetSocketInterface(s, addr.type, session.bind_interface())) {
        tr_net_close_socket(s);
        return TR_BAD_SOCKET;
    }

    // seeds don't need a big read buffer, so make it smaller
    if (client_is_seed) {
        constexpr int N = 8192;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&N), sizeof(N)) == -1) {
            tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF on socket {}: {}", s, tr_net_strerror(sockerrno)));
        }
    }

    auto const [sock, addrlen] = socket_address.to_sockaddr();

    // set source address
    auto const source_addr = session.bind_address(addr.type);
    auto const [source_sock, sourcelen] = tr_socket_address::to_sockaddr(source_addr, {});

    if (bind(s, reinterpret_cast<sockaddr const*>(&source_sock), sourcelen) == -1) {
        tr_logAddWarn(
            fmt::format(
                "Couldn't set source address {address} on {socket}: {error} ({error_code})",
                fmt::arg("address", source_addr.display_name()),
                fmt::arg("socket", s),
                fmt::arg("error", tr_net_strerror(sockerrno)),
                fmt::arg("error_code", sockerrno)));
        tr_net_close_socket(s);
        return { TR_BAD_SOCKET, false };
    }

    auto const is_connected = connect(s, reinterpret_cast<sockaddr const*>(&sock), addrlen) == 0;
    if (!is_connected &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS) {
        if (auto const tmperrno = sockerrno;
            (tmperrno != ECONNREFUSED && tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr.is_ipv4()) {
            tr_logAddWarn(
                fmt::format(
                    "Couldn't connect socket {socket} to {address}:{port}: {error} ({error_code})",
                    fmt::arg("socket", s),
                    fmt::arg("address", addr.display_name()),
                    fmt::arg("port", port.host()),
                    fmt::arg("error", tr_net_strerror(tmperrno)),
                    fmt::arg("error_code", tmperrno)));
        }

        tr_net_close_socket(s);
        return { TR_BAD_SOCKET, false };
    }

    tr_logAddTrace(fmt::format("New OUTGOING connection {} ({})", s, socket_address.display_name()));

    return { s, is_connected };
}

void set_congestion_control([[maybe_unused]] tr_socket_t const s, [[maybe_unused]] char const* const algorithm)
{
#ifdef TCP_CONGESTION
    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, algorithm, strlen(algorithm) + 1) == -1) {
        tr_logAddDebug(fmt::format("Can't set congestion control algorithm '{}': {}", algorithm, tr_net_strerror(sockerrno)));
    }
#endif
}

class tr_peer_socket_tcp_impl final
    : public tr_peer_socket_tcp
    , public std::enable_shared_from_this<tr_peer_socket_tcp_impl>
{
public:
    tr_peer_socket_tcp_impl(
        tr_session& session,
        tr_socket_address const& socket_address,
        tr_socket_t const sock,
        bool const is_connected)
        : tr_peer_socket_tcp{ socket_address }
        , sock_{ sock }
        , connect_error_{ is_connected ? 0 : EWOULDBLOCK }
    {
        TR_ASSERT(is_valid_socket(sock_));

        session.setSocketDiffServ(sock, address().type);

        if (auto const& algo = session.peerCongestionAlgorithm(); !std::empty(algo)) {
            set_congestion_control(sock, algo.c_str());
        }

        event_read_.reset(
            tr::evhelpers::event_new_pri2(
                session.event_base(),
                static_cast<evutil_socket_t>(sock_),
                EV_READ,
                event_read_cb,
                this));

        if (is_connected) {
            install_write_cb(session.event_base());
        } else {
            event_write_.reset(
                tr::evhelpers::event_new_pri2(
                    session.event_base(),
                    static_cast<evutil_socket_t>(sock_),
                    EV_WRITE | EV_PERSIST,
                    event_connect_cb,
                    this));
            event_add(event_write_.get(), nullptr);
        }

        tr_logAddTraceSock(this, fmt::format("socket (tcp) is {}", sock_));
    }

    tr_peer_socket_tcp_impl(tr_peer_socket_tcp_impl const&) = delete;
    tr_peer_socket_tcp_impl& operator=(tr_peer_socket_tcp_impl const&) = delete;
    tr_peer_socket_tcp_impl(tr_peer_socket_tcp_impl&&) = delete;
    tr_peer_socket_tcp_impl& operator=(tr_peer_socket_tcp_impl&&) = delete;

    ~tr_peer_socket_tcp_impl() override
    {
        event_read_.reset();
        event_write_.reset();
        tr_net_close_socket(sock_);
    }

    void set_read_enabled(bool const enabled) override
    {
        if (is_read_enabled_ == enabled) {
            return;
        }

        is_read_enabled_ = enabled;
        if (enabled) {
            if (connect_error_ == 0) {
                tr_logAddTraceSock(this, "enabling ready-to-read polling");
                event_add(event_read_.get(), nullptr);
            }
        } else {
            tr_logAddTraceSock(this, "disabling ready-to-read polling");
            event_del(event_read_.get());
        }
    }

    void set_write_enabled(bool const enabled) override
    {
        if (is_write_enabled_ == enabled) {
            return;
        }

        is_write_enabled_ = enabled;

        // Before the TCP handshake is complete, the write event is repurposed to wait for the handshake to complete,
        // and it is always pending or active.
        if (connect_error_ == 0) {
            if (enabled) {
                tr_logAddTraceSock(this, "enabling ready-to-write polling");
                event_add(event_write_.get(), nullptr);
            } else {
                tr_logAddTraceSock(this, "disabling ready-to-write polling");
                event_del(event_write_.get());
            }
        }
    }

    [[nodiscard]] bool is_read_enabled() const override
    {
        return is_read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const override
    {
        return is_write_enabled_;
    }

protected:
    [[nodiscard]] constexpr Type type() const noexcept override
    {
        return Type::TCP;
    }

    size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* error) override
    {
        if (connect_error_ != 0) {
            if (error != nullptr) {
                *error = tr_net_error(connect_error_);
            }
            return {};
        }

        auto const [bufptr, buflen] = buf.reserve_space(n_bytes);
        n_bytes = std::min(n_bytes, buflen);
        TR_ASSERT(n_bytes > 0U);
        auto const n_read = recv(sock_, reinterpret_cast<char*>(bufptr), TR_IF_WIN32(static_cast<int>(n_bytes), n_bytes), 0);
        auto const err = sockerrno;

        if (n_read > 0) {
            buf.commit_space(n_read);
            return static_cast<size_t>(n_read);
        }

        // When a stream socket peer has performed an orderly shutdown,
        // the return value will be 0 (the traditional "end-of-file" return).
        if (error != nullptr) {
            *error = tr_net_error(n_read == 0 ? ENOTCONN : err);
        }

        return {};
    }

    size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* error) override
    {
        if (connect_error_ != 0) {
            if (error != nullptr) {
                *error = tr_net_error(connect_error_);
            }
            return {};
        }

        n_bytes = std::min(n_bytes, std::size(buf));
        if (n_bytes == 0U) {
            return {};
        }

        if (auto const n_sent = send(
                sock_,
                reinterpret_cast<char const*>(std::data(buf)),
                TR_IF_WIN32(static_cast<int>(n_bytes), n_bytes),
                0);
            n_sent >= 0) {
            buf.drain(n_sent);
            return n_sent;
        }

        if (error != nullptr) {
            *error = tr_net_error(sockerrno);
        }

        return {};
    }

private:
    static void event_connect_cb([[maybe_unused]] evutil_socket_t const fd, short /*event*/, void* const vs)
    {
        // The socket can destruct inside the callbacks, so keep it alive for the time being.
        auto const s = static_cast<tr_peer_socket_tcp_impl*>(vs)->shared_from_this();
        tr_logAddTraceSock(s, "libevent says we can check TCP handshake status");
        TR_ASSERT(std::cmp_equal(s->sock_, fd));

        int err = {};
        socklen_t error_len = sizeof(err);
        if (getsockopt(s->sock_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &error_len) == -1) {
            err = sockerrno;
            if (tr_error::is_transient(err)) {
                return;
            }
            tr_logAddWarnSock(s, fmt::format("failed to get socket error: {} ({})", tr_net_strerror(err), err));
        } else if (err != 0) {
            tr_logAddDebugSock(s, fmt::format("socket error in TCP handshake: {} ({})", tr_net_strerror(err), err));
        }

        s->connect_error_ = err;
        s->install_write_cb(event_get_base(s->event_write_.get()));

        if (err != 0) {
            // The callback can destroy this socket.
            s->error_cb(tr_net_error(err));
            return;
        }

        if (s->is_read_enabled_) {
            event_add(s->event_read_.get(), nullptr);
        }
        if (s->is_write_enabled_) {
            event_add(s->event_write_.get(), nullptr);
        }
    }

    static void event_read_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vs)
    {
        // The socket can destruct inside read_cb(), so keep it alive
        // for the duration of this code block. This can happen when
        // a BT handshake did not complete successfully for example.
        auto const s = static_cast<tr_peer_socket_tcp_impl*>(vs)->shared_from_this();
        tr_logAddTraceSock(s, "libevent says this peer socket is ready for reading");

        TR_ASSERT(std::cmp_equal(s->sock_, fd));

        s->is_read_enabled_ = false;
        s->read_cb();
    }

    static void event_write_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vs)
    {
        // The socket can destruct inside write_cb(), so keep it alive
        // for the duration of this code block. This can happen because
        // of a socket error for example.
        auto const s = static_cast<tr_peer_socket_tcp_impl*>(vs)->shared_from_this();
        tr_logAddTraceSock(s, "libevent says this peer socket is ready for writing");

        TR_ASSERT(std::cmp_equal(s->sock_, fd));

        s->is_write_enabled_ = false;
        s->write_cb();
    }

    void install_write_cb(struct event_base* const evbase)
    {
        event_write_.reset(
            tr::evhelpers::event_new_pri2(evbase, static_cast<evutil_socket_t>(sock_), EV_WRITE, event_write_cb, this));
    }

    tr::evhelpers::event_unique_ptr event_read_;
    tr::evhelpers::event_unique_ptr event_write_;

    tr_socket_t sock_;
    int connect_error_;

    bool is_read_enabled_ = false;
    bool is_write_enabled_ = false;
};
} // namespace

tr_peer_socket_tcp::tr_peer_socket_tcp(tr_socket_address const& socket_address)
    : tr_peer_socket{ socket_address }
{
}

std::shared_ptr<tr_peer_socket_tcp> tr_peer_socket_tcp::create(
    tr_session& session,
    tr_socket_address const& socket_address,
    bool const client_is_seed)
{
    if (auto const [sock, is_connected] = open_peer_socket(session, socket_address, client_is_seed); is_valid_socket(sock)) {
        return std::make_shared<tr_peer_socket_tcp_impl>(session, socket_address, sock, is_connected);
    }
    return {};
}

std::shared_ptr<tr_peer_socket_tcp> tr_peer_socket_tcp::create(
    tr_session& session,
    tr_socket_address const& socket_address,
    tr_socket_t sock)
{
    return std::make_shared<tr_peer_socket_tcp_impl>(session, socket_address, sock, true);
}
