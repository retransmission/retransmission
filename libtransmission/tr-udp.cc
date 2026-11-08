// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>
#include <utility> // std::cmp_less

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h> // setsockopt, SOL_SOCKET, bind
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h" // tr_base64_encode()
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/macros.h"
#include "libtransmission/tr-utp.h"

namespace
{
[[nodiscard]] bool is_loopback(sockaddr const* const sa)
{
    auto const addrport = tr_socket_address::from_sockaddr(sa);
    return addrport && (addrport->address().is_ipv4_loopback() || addrport->address().is_ipv6_loopback());
}

// Since we use a single UDP socket in order to implement multiple
// µTP sockets, try to set up huge buffers.
void set_socket_buffers(tr_socket_t fd, bool large)
{
    static auto constexpr RecvBufferSize = 4 * 1024 * 1024;
    static auto constexpr SendBufferSize = 1 * 1024 * 1024;
    static auto constexpr SmallBufferSize = 32 * 1024;

    int rbuf = 0;
    int sbuf = 0;
    socklen_t rbuf_len = sizeof(rbuf);
    socklen_t sbuf_len = sizeof(sbuf);

    int size = large ? RecvBufferSize : SmallBufferSize;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0) {
        tr_logAddDebug(fmt::format("Couldn't set receive buffer: {}", tr_net_strerror(sockerrno)));
    }

    size = large ? SendBufferSize : SmallBufferSize;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0) {
        tr_logAddDebug(fmt::format("Couldn't set send buffer: {}", tr_net_strerror(sockerrno)));
    }

    if (large) {
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rbuf), &rbuf_len) < 0) {
            rbuf = 0;
        }

        if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sbuf), &sbuf_len) < 0) {
            sbuf = 0;
        }

        if (rbuf < RecvBufferSize) {
            tr_logAddDebug(fmt::format("Couldn't set receive buffer: requested {}, got {}", RecvBufferSize, rbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.rmem_max = {}' to /etc/sysctl.conf", RecvBufferSize));
#endif
        }

        if (sbuf < SendBufferSize) {
            tr_logAddDebug(fmt::format("Couldn't set send buffer: requested {}, got {}", SendBufferSize, sbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.wmem_max = {}' to /etc/sysctl.conf", SendBufferSize));
#endif
        }
    }
}

// Hands one datagram to its protocol. Returns true for a µTP packet.
// buf must have room for one more byte after buflen.
bool handle_packet(
    tr_session* const session,
    unsigned char* const buf,
    size_t const buflen,
    sockaddr* const from_sa,
    socklen_t const fromlen)
{
    auto const from_str = [from_sa] {
        return tr_socket_address::from_sockaddr(from_sa).value_or(tr_socket_address{}).display_name();
    };

    // Since most packets we receive here are µTP, make quick inline
    // checks for the other protocols. The logic is as follows:
    // - all DHT packets start with 'd' (100)
    // - all UDP tracker packets start with a 32-bit (!) "action", which
    //   is between 0 and 3
    // - the above cannot be µTP packets, since these start with a 4-bit
    //   "type" between 0 and 4, followed by a 4-bit version number (1)
    if (buf[0] == 'd') {
        if (session->dht_) {
            buf[buflen] = '\0'; // libdht requires zero-terminated messages
            session->dht_->handle_message(buf, buflen, from_sa, fromlen);
        }
    } else if (buflen >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3) {
        if (!session->announcer_udp_->handle_message(buf, buflen, from_sa, fromlen)) {
            tr_logAddTrace(fmt::format("{} Couldn't parse UDP tracker packet.", from_str()));
        }
    } else if (session->allowsUTP() && session->utp_context != nullptr) {
        if (tr_utp_packet(buf, buflen, from_sa, fromlen, session)) {
            return true;
        }

        tr_logAddTrace(
            fmt::format(
                "{} Unexpected UDP packet... len {} [{}]",
                from_str(),
                buflen,
                tr_base64_encode({ reinterpret_cast<char const*>(buf), buflen })));
    }

    return false;
}

auto constexpr DatagramBufferSize = size_t{ 8192U };

#ifdef HAVE_RECVMMSG
// Datagrams read per recvmmsg() call. µTP at full speed delivers tens of thousands of
// packets per second, so one syscall per datagram is a large part of its cost.
auto constexpr DatagramsPerRead = size_t{ 16U };

// Reads every pending datagram; returns true if any was a µTP packet.
bool read_datagrams(evutil_socket_t const s, tr_session* const session)
{
    struct Datagram {
        std::array<unsigned char, DatagramBufferSize> buf;
        sockaddr_storage from;
        iovec iov;
    };
    // Per thread, so that a read neither shares nor value-initialises 128 KiB.
    thread_local auto datagrams = std::array<Datagram, DatagramsPerRead>{};
    thread_local auto headers = std::array<mmsghdr, DatagramsPerRead>{};

    auto got_utp_packet = false;
    for (;;) {
        for (size_t i = 0; i < DatagramsPerRead; ++i) {
            auto& datagram = datagrams[i];
            datagram.iov = { .iov_base = std::data(datagram.buf), .iov_len = std::size(datagram.buf) - 1U };
            headers[i] = {};
            headers[i].msg_hdr.msg_name = &datagram.from;
            headers[i].msg_hdr.msg_namelen = sizeof(datagram.from);
            headers[i].msg_hdr.msg_iov = &datagram.iov;
            headers[i].msg_hdr.msg_iovlen = 1;
        }

        auto const n_read = recvmmsg(s, std::data(headers), DatagramsPerRead, 0, nullptr);
        if (n_read <= 0) {
            return got_utp_packet;
        }

        for (int i = 0; i < n_read; ++i) {
            auto& datagram = datagrams[i];
            auto const& header = headers[i];
            // An empty datagram leaves the buffer's old bytes in place, and a truncated one
            // is not the datagram its sender wrote; no protocol here sends either.
            if (header.msg_len == 0U || (header.msg_hdr.msg_flags & MSG_TRUNC) != 0) {
                continue;
            }
            got_utp_packet |= handle_packet(
                session,
                std::data(datagram.buf),
                header.msg_len,
                reinterpret_cast<sockaddr*>(&datagram.from),
                header.msg_hdr.msg_namelen);
        }

        // a short batch means the socket is drained
        if (std::cmp_less(n_read, DatagramsPerRead)) {
            return got_utp_packet;
        }
    }
}
#else
// Reads every pending datagram; returns true if any was a µTP packet.
bool read_datagrams(evutil_socket_t const s, tr_session* const session)
{
    thread_local auto buf = std::array<unsigned char, DatagramBufferSize>{};
    auto got_utp_packet = false;

    for (;;) {
        auto from = sockaddr_storage{};
        auto fromlen = socklen_t{ sizeof(from) };
        auto* const from_sa = reinterpret_cast<sockaddr*>(&from);
        auto const n_read = recvfrom(s, reinterpret_cast<char*>(std::data(buf)), std::size(buf) - 1, 0, from_sa, &fromlen);
        if (n_read <= 0) {
            return got_utp_packet;
        }

        got_utp_packet |= handle_packet(session, std::data(buf), static_cast<size_t>(n_read), from_sa, fromlen);
    }
}
#endif

void event_callback(evutil_socket_t s, [[maybe_unused]] short type, void* vsession)
{
    TR_ASSERT(vsession != nullptr);
    TR_ASSERT(type == EV_READ);

    auto* const session = static_cast<tr_session*>(vsession);
    if (read_datagrams(s, session)) {
        // To reduce protocol overhead, we wait until we've read all UDP packets
        // we can, then send one ACK for each µTP socket that received packet(s).
        tr_utp_issue_deferred_acks(session);
    }
}
} // namespace

// BEP-32 explains why we need to bind to one IPv6 address

tr_session::tr_udp_core::tr_udp_core(tr_session& session, tr_port udp_port)
    : udp_port_{ udp_port }
    , session_{ session }
{
    if (std::empty(udp_port_)) {
        return;
    }

    if (!session.has_ip_protocol(TR_AF_INET)) {
        // no IPv4; do nothing
    } else if (tr_socket_t const sock = socket(PF_INET, SOCK_DGRAM, 0); is_valid_socket(sock)) {
        (void)evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(sock));

        auto const addr = session_.bind_address(TR_AF_INET);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sock)) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't make IPv4 socket non-blocking {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't bind IPv4 socket {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else {
            tr_logAddInfo(fmt::format("Bound UDP IPv4 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketDiffServ(sock, TR_AF_INET);
            set_socket_buffers(sock, session_.allowsUTP());
            udp4_socket_ = sock;
            udp4_event_.reset(
                tr::evhelpers::event_new_pri2(
                    session_.event_base(),
                    static_cast<evutil_socket_t>(udp4_socket_),
                    EV_READ | EV_PERSIST,
                    event_callback,
                    &session_));
            event_add(udp4_event_.get(), nullptr);
        }
    }

    if (!session.has_ip_protocol(TR_AF_INET6)) {
        // no IPv6; do nothing
    } else if (tr_socket_t const sock = socket(PF_INET6, SOCK_DGRAM, 0); is_valid_socket(sock)) {
        (void)evutil_make_listen_socket_reuseable(static_cast<evutil_socket_t>(sock));
        (void)tr_make_listen_socket_ipv6only(static_cast<evutil_socket_t>(sock));

        auto const addr = session_.bind_address(TR_AF_INET6);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(static_cast<evutil_socket_t>(sock)) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't make IPv6 socket non-blocking {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0) {
            auto const error_code = errno;
            tr_logAddWarn(
                fmt::format(
                    "Couldn't bind IPv6 socket {address}: {error} ({error_code})",
                    fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        } else {
            tr_logAddInfo(fmt::format("Bound UDP IPv6 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketDiffServ(sock, TR_AF_INET6);
            set_socket_buffers(sock, session_.allowsUTP());
            udp6_socket_ = sock;
            udp6_event_.reset(
                tr::evhelpers::event_new_pri2(
                    session_.event_base(),
                    static_cast<evutil_socket_t>(udp6_socket_),
                    EV_READ | EV_PERSIST,
                    event_callback,
                    &session_));
            event_add(udp6_event_.get(), nullptr);
        }
    }

    if (!is_valid_socket(udp4_socket_) && !is_valid_socket(udp6_socket_)) {
        tr_logAddError("Couldn't create any UDP sockets.");
    }
}

tr_session::tr_udp_core::~tr_udp_core()
{
    udp6_event_.reset();

    if (is_valid_socket(udp6_socket_)) {
        tr_net_close_socket(udp6_socket_);
        udp6_socket_ = TR_BAD_SOCKET;
    }

    udp4_event_.reset();

    if (is_valid_socket(udp4_socket_)) {
        tr_net_close_socket(udp4_socket_);
        udp4_socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::tr_udp_core::sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen) const
{
    if (to->sa_family != AF_INET && to->sa_family != AF_INET6) {
        errno = EAFNOSUPPORT;
    } else if (auto const sock = to->sa_family == AF_INET ? udp4_socket_ : udp6_socket_; !is_valid_socket(sock)) {
        // don't warn on bad sockets; the system may not support IPv6
        return;
    } else if (!session_.has_source_address(tr_af_to_ip_protocol(to->sa_family)) && !is_loopback(to)) {
        // don't try to send if we don't have a route in this IP protocol
        return;
    }
    // NOLINTNEXTLINE(readability-redundant-casting)
    else if (::sendto(sock, static_cast<char const*>(buf), static_cast<TR_IF_WIN32(int, size_t)>(buflen), 0, to, tolen) != -1) {
        return;
    }

    auto display_name = std::string{};
    if (auto const addrport = tr_socket_address::from_sockaddr(to)) {
        display_name = addrport->display_name();
    }

    tr_logAddWarn(
        fmt::format(
            "Couldn't send to {address}: {errno} ({error})",
            fmt::arg("address", display_name),
            fmt::arg("errno", errno),
            fmt::arg("error", tr_strerror(errno))));
}
