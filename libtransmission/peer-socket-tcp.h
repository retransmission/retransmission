// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>

#include "libtransmission/peer-socket.h"

struct tr_session;
struct tr_socket_address;

class tr_peer_socket_tcp : public tr_peer_socket
{
public:
    [[nodiscard]] static std::unique_ptr<tr_peer_socket_tcp> create(
        tr_session& session,
        tr_socket_address const& socket_address,
        bool client_is_seed);
    [[nodiscard]] static std::unique_ptr<tr_peer_socket_tcp> create(
        tr_session& session,
        tr_socket_address const& socket_address,
        tr_socket_t sock);

protected:
    explicit tr_peer_socket_tcp(tr_socket_address const& socket_address);
};
