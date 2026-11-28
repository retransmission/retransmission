// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint16_t
#include <optional>

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <ws2tcpip.h>
#endif

struct tr_session;

void tr_utp_init(tr_session* session);

[[nodiscard]] bool tr_utp_packet(
    unsigned char const* buf,
    size_t buflen,
    struct sockaddr const* from,
    socklen_t fromlen,
    tr_session* ss);

void tr_utp_issue_deferred_acks(tr_session* ss);

// The UDP payload µTP starts with towards an address of `family` (AF_INET or AF_INET6) whose route has an
// MTU of `route_mtu` bytes, if known: libutp's default, lowered so that a datagram fits the route
// unfragmented, but never below 576, where libutp's MTU search stops.
[[nodiscard]] uint16_t tr_utp_udp_mtu(int family, std::optional<size_t> route_mtu) noexcept;

// The MTU of the IPv4 route from `from` (the address µTP's socket is bound to) to `to`, as the system
// reports it: the outgoing interface's, unless the route sets a lower one; on Linux and Windows also a
// lower path MTU the system has learnt. Unknown for IPv6, on systems without such a lookup, and where it
// fails.
[[nodiscard]] std::optional<size_t> tr_utp_route_mtu(
    struct sockaddr const* to,
    socklen_t tolen,
    struct sockaddr const* from,
    socklen_t fromlen);

void tr_utp_close(tr_session* session);
