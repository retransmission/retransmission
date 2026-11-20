// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint>
#include <ctime>
#include <span>
#include <unordered_map>
#include <utility>

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <ws2tcpip.h>
#endif

#include "libtransmission/net.h"

struct tr_session;

// Ends the µTP connections a hard move to another network binding leaves behind.
// Their sockets live on in libutp, which closes them in its own time and keeps
// retransmitting their packets through the new binding's UDP socket meanwhile;
// those would let the peers link the two addresses. libutp hands each packet to
// the send callback without its socket, so connections are told apart by the
// destination and the connection ID in the µTP header.
class tr_utp_flow_filter
{
public:
    // false if `packet`, about to be sent to `to`, belongs to a silenced connection
    [[nodiscard]] bool allow(tr_socket_address const& to, std::span<uint8_t const> packet, time_t now);

    // every connection that has sent a packet since the last call sends no more
    void silence(time_t now);

private:
    // how long a connection stays silenced, or remembered without sending;
    // libutp gives up on an unanswered connection well before
    static auto constexpr Linger = time_t{ 600 };

    using Flow = std::pair<tr_socket_address, uint16_t>;

    struct FlowHash {
        [[nodiscard]] size_t operator()(Flow const& flow) const noexcept
        {
            return std::hash<tr_socket_address>{}(flow.first) ^ (size_t{ flow.second } << 1U);
        }
    };

    void prune(time_t now);

    std::unordered_map<Flow, time_t, FlowHash> seen_; // last send
    std::unordered_map<Flow, time_t, FlowHash> silenced_; // until
    // a run of packets to one connection costs one lookup
    std::pair<Flow, time_t> last_;
    time_t next_prune_ = 0;
};

void tr_utp_init(tr_session* session);

[[nodiscard]] bool tr_utp_packet(
    unsigned char const* buf,
    size_t buflen,
    struct sockaddr const* from,
    socklen_t fromlen,
    tr_session* ss);

void tr_utp_issue_deferred_acks(tr_session* ss);

// after a hard move, the µTP connections opened so far send nothing more
void tr_utp_silence_flows(tr_session* session);

void tr_utp_close(tr_session* session);
