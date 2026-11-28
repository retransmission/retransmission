// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#ifdef __linux__
#include <netinet/in.h> // IP_MTU
#include <sys/socket.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <cstring> // std::memcpy
#include <net/if.h> // if_indextoname(), ifreq
#include <net/if_dl.h> // sockaddr_dl
#include <net/route.h> // rt_msghdr, RTM_GET
#include <netinet/in.h>
#include <sys/ioctl.h> // SIOCGIFMTU
#include <sys/socket.h>
#include <sys/time.h> // timeval
#include <unistd.h> // close(), getpid(), getrtable(), read(), write()
#endif

#include <fmt/format.h> // fmt::ptr

#ifdef WITH_UTP
#include <libutp/utp.h>
#endif

#include "libtransmission/crypto-utils.h" // tr_rand_int()
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-utp.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-utp.h"

using namespace std::literals;

uint16_t tr_utp_udp_mtu(int const family, std::optional<size_t> const route_mtu) noexcept
{
    // libutp's own defaults (utp_default_get_udp_mtu(), not in its public API): for IPv4, a
    // 1500-byte Ethernet MTU less IPv4, UDP, GRE, PPPoE and MPPE headers and 36 spare bytes;
    // for IPv6, Teredo's 1280 less IPv6 and UDP headers, which fits every IPv6 route: RFC 8200 requires
    // every IPv6 link to carry at least 1280 bytes.
    static auto constexpr DefaultIpv4 = size_t{ 1402 };
    static auto constexpr DefaultIpv6 = size_t{ 1232 };
    // libutp's MTU search never goes below 576 and asserts the starting size is not lower.
    static auto constexpr Floor = size_t{ 576 };

    auto const is_ipv6 = family == AF_INET6;
    auto mtu = is_ipv6 ? DefaultIpv6 : DefaultIpv4;
    if (auto const headers = size_t{ is_ipv6 ? 40U : 20U } + 8U; route_mtu && *route_mtu > headers) {
        mtu = std::min(mtu, *route_mtu - headers);
    }
    return static_cast<uint16_t>(std::max(mtu, Floor));
}

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
namespace
{
// The MTU of network interface `index`.
std::optional<size_t> interface_mtu(unsigned const index)
{
    auto ifr = ifreq{};
    if (if_indextoname(index, ifr.ifr_name) == nullptr) {
        return {};
    }

    auto const fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return {};
    }

    auto const ok = ioctl(fd, SIOCGIFMTU, &ifr) == 0;
    close(fd);
    return ok && ifr.ifr_mtu > 0 ? std::optional{ static_cast<size_t>(ifr.ifr_mtu) } : std::nullopt;
}
} // namespace
#endif

std::optional<size_t> tr_utp_route_mtu(
    sockaddr const* const to,
    [[maybe_unused]] socklen_t const tolen,
    [[maybe_unused]] sockaddr const* const from,
    [[maybe_unused]] socklen_t const fromlen)
{
    // IPv4 only. Every IPv6 link carries at least 1280 bytes (RFC 8200), so an IPv6 route's MTU, learnt
    // or not, never lowers libutp's IPv6 default.
    if (to->sa_family != AF_INET) {
        return {};
    }

#if (defined(__linux__) || defined(_WIN32)) && defined(IP_MTU)
    // Windows has IP_MTU since Windows 10 version 1703; before that getsockopt() fails.
    // Connecting a UDP socket sends nothing; it only looks up the route.
    auto const sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (!is_valid_socket(sock)) {
        return {};
    }

    // A DWORD on Windows and an int on Linux: 4 bytes either way.
    auto mtu = uint32_t{};
    auto mtu_len = socklen_t{ sizeof(mtu) };
    auto* const mtu_buf = reinterpret_cast<char*>(&mtu);
    // Bound like µTP's socket, so that source-based routing (Linux `ip rule from`) picks the same route.
    auto const ok = bind(sock, from, fromlen) == 0 && connect(sock, to, tolen) == 0 &&
        getsockopt(sock, IPPROTO_IP, IP_MTU, mtu_buf, &mtu_len) == 0;
    tr_net_close_socket(sock);
    return ok && mtu > 0U ? std::optional{ static_cast<size_t>(mtu) } : std::nullopt;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    // No IP_MTU here: ask the routing table, which holds an MTU per route (the interface's,
    // unless the route sets its own).
    if (tolen != sizeof(sockaddr_in)) {
        return {};
    }

    // Receives the system's IPv4 routing messages, the reply among them.
    auto const fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
    if (fd < 0) {
        return {};
    }

    // Give up on the reply after 100 ms, also while other routing messages keep arriving.
    static auto constexpr Timeout = 100ms;
    auto const deadline = std::chrono::steady_clock::now() + Timeout;
    auto const timeout = timeval{ .tv_sec = 0,
                                  .tv_usec = static_cast<suseconds_t>(std::chrono::microseconds{ Timeout }.count()) };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // The kernel pads each address in a routing message to this size.
#ifdef __APPLE__
    auto constexpr Align = sizeof(uint32_t);
#elif defined(__NetBSD__)
    auto constexpr Align = sizeof(uint64_t);
#else
    auto constexpr Align = sizeof(long);
#endif
    auto const padded = [](size_t len) {
        return (len + Align - 1U) / Align * Align;
    };

    // An RTM_GET request is an rt_msghdr followed by the destination and an empty interface
    // address. Asking for the interface is what makes NetBSD fill in rtm_index. sizeof(rt_msghdr)
    // need not be a multiple of the address's alignment, so the message is built in bytes.
    alignas(rt_msghdr) std::array<unsigned char, 2048> buf = {};
    auto ifp = sockaddr_dl{};
    ifp.sdl_len = sizeof(ifp);
    ifp.sdl_family = AF_LINK;
    auto request = rt_msghdr{};
    request.rtm_msglen = static_cast<u_short>(sizeof(request) + padded(tolen) + padded(sizeof(ifp)));
    request.rtm_version = RTM_VERSION;
    request.rtm_type = RTM_GET;
    request.rtm_addrs = RTA_DST | RTA_IFP;
    request.rtm_pid = getpid();
    request.rtm_seq = 1;
#ifdef __OpenBSD__
    // the process's routing table, which `route -T` or rcctl's rtable may have set
    request.rtm_tableid = static_cast<u_short>(getrtable());
#endif
    std::memcpy(std::data(buf), &request, sizeof(request));
    std::memcpy(std::data(buf) + sizeof(request), to, tolen);
    reinterpret_cast<sockaddr*>(std::data(buf) + sizeof(request))->sa_len = static_cast<uint8_t>(tolen);
    std::memcpy(std::data(buf) + sizeof(request) + padded(tolen), &ifp, sizeof(ifp));

    auto mtu = std::optional<size_t>{};
    if (write(fd, std::data(buf), request.rtm_msglen) == request.rtm_msglen) {
        for (;;) {
            auto const n_read = read(fd, std::data(buf), std::size(buf));
            if (n_read < static_cast<ssize_t>(sizeof(rt_msghdr))) {
                break;
            }

            auto reply = rt_msghdr{};
            std::memcpy(&reply, std::data(buf), sizeof(reply));
            if (reply.rtm_type != RTM_GET || reply.rtm_pid != request.rtm_pid || reply.rtm_seq != request.rtm_seq) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
                continue; // another process's routing message
            }

            if (reply.rtm_errno == 0) {
                mtu = reply.rtm_rmx.rmx_mtu > 0 ? std::optional{ static_cast<size_t>(reply.rtm_rmx.rmx_mtu) } :
                                                  interface_mtu(reply.rtm_index);
            }
            break;
        }
    }
    close(fd);
    return mtu;
#else
    return {};
#endif
}

#ifndef WITH_UTP

void tr_utp_init(tr_session* /*session*/)
{
}

bool tr_utp_packet(
    unsigned char const* /*buf*/,
    size_t /*buflen*/,
    struct sockaddr const* /*from*/,
    socklen_t /*fromlen*/,
    tr_session* /*ss*/)
{
    return false;
}

void tr_utp_issue_deferred_acks(tr_session* /*ss*/)
{
}

void tr_utp_close(tr_session* /*session*/)
{
}

#else

namespace
{
/* Greg says 50ms works for them. */
auto constexpr UtpInterval = 50ms;

void utp_on_accept(tr_session* const session, UTPSocket* const utp_sock)
{
    auto from_storage = sockaddr_storage{};
    auto* const from = reinterpret_cast<sockaddr*>(&from_storage);
    socklen_t fromlen = sizeof(from_storage);
    utp_getpeername(utp_sock, from, &fromlen);

    if (auto addrport = tr_socket_address::from_sockaddr(reinterpret_cast<struct sockaddr*>(&from_storage)); addrport) {
        session->addIncoming(tr_peer_socket_utp::create(*addrport, utp_sock, session->timerMaker()));
    } else {
        tr_logAddWarn("Unknown socket family");
        utp_close(utp_sock);
    }
}

void utp_send_to(
    tr_session const* const ss,
    uint8_t const* const buf,
    size_t const buflen,
    struct sockaddr const* const to,
    socklen_t const tolen)
{
    ss->udp_core_->sendto(buf, buflen, to, tolen);
}

uint64 utp_callback(utp_callback_arguments* args)
{
    auto* const session = static_cast<tr_session*>(utp_context_get_userdata(args->context));

    TR_ASSERT(session != nullptr);
    TR_ASSERT(session->utp_context == args->context);

    switch (args->callback_type) {
#ifdef TR_UTP_TRACE
    case UTP_LOG:
        tr_logAddTrace(fmt::format("[µTP] {}", reinterpret_cast<char const*>(args->buf)));
        break;
#endif

    case UTP_ON_FIREWALL:
        // Refused here, before libutp creates a socket and asks for its MTU.
        return !session->allowsUTP() || tr_peer_socket::limit_reached(session) ? 1 : 0;

    case UTP_ON_ACCEPT:
        utp_on_accept(session, args->socket);
        break;

    case UTP_SENDTO:
        utp_send_to(session, args->buf, args->len, args->address, args->address_len);
        break;

    case UTP_GET_UDP_MTU:
        {
            auto const family = args->address->sa_family;
            auto const [from, fromlen] = tr_socket_address::to_sockaddr(
                session->bind_address(tr_af_to_ip_protocol(family)),
                tr_port{});
            return tr_utp_udp_mtu(
                family,
                tr_utp_route_mtu(args->address, args->address_len, reinterpret_cast<sockaddr const*>(&from), fromlen));
        }

    default:
        break;
    }

    return 0;
}

void restart_timer(tr_session* session)
{
    auto interval = std::chrono::milliseconds{};
    auto const random_percent = tr_rand_int(1000U) / 1000.0;

    if (session->allowsUTP()) {
        static auto constexpr MinInterval = UtpInterval * 0.5;
        static auto constexpr MaxInterval = UtpInterval * 1.5;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    } else {
        // If somebody has disabled µTP, then we still want to run
        // utp_check_timeouts, in order to let closed sockets finish
        // gracefully and so on.  However, since we're not particularly
        // interested in that happening in a timely manner, we might as
        // well use a large timeout.
        static auto constexpr MinInterval = 2s;
        static auto constexpr MaxInterval = 3s;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    }

    session->utp_timer->start_single_shot(interval);
}

void timer_callback(void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    utp_check_timeouts(session->utp_context);
    restart_timer(session);
}
} // namespace

void tr_utp_init(tr_session* session)
{
    if (session->utp_context != nullptr) {
        return;
    }

    auto* const ctx = utp_init(2);

    if (ctx == nullptr) {
        return;
    }

    utp_context_set_userdata(ctx, session);
    utp_set_callback(ctx, UTP_ON_FIREWALL, &utp_callback);
    utp_set_callback(ctx, UTP_ON_ACCEPT, &utp_callback);
    utp_set_callback(ctx, UTP_SENDTO, &utp_callback);
    utp_set_callback(ctx, UTP_GET_UDP_MTU, &utp_callback);
    tr_peer_socket_utp::init(ctx);

#ifdef TR_UTP_TRACE
    utp_set_callback(ctx, UTP_LOG, &utp_callback);
    utp_context_set_option(ctx, UTP_LOG_NORMAL, 1);
    utp_context_set_option(ctx, UTP_LOG_MTU, 1);
    utp_context_set_option(ctx, UTP_LOG_DEBUG, 1);
#endif

    session->utp_context = ctx;
    session->utp_timer = session->timerMaker().create(timer_callback, session);
    restart_timer(session);
}

// NOLINTNEXTLINE(misc-const-correctness)
bool tr_utp_packet(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss)
{
    auto const ret = utp_process_udp(ss->utp_context, buf, buflen, from, fromlen);

    return ret != 0;
}

// NOLINTNEXTLINE(misc-const-correctness)
void tr_utp_issue_deferred_acks(tr_session* ss)
{
    utp_issue_deferred_acks(ss->utp_context);
}

void tr_utp_close(tr_session* session)
{
    session->utp_timer.reset();

    if (session->utp_context != nullptr) {
        utp_destroy(session->utp_context);
        session->utp_context = nullptr;
    }
}

#endif /* #ifndef WITH_UTP ... else */
