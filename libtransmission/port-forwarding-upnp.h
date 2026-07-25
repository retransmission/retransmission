// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PORT_FORWARDING_MODULE
#error only the libtransmission port forwarding module should #include this header.
#endif

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

#include <string>

#include "libtransmission/types.h" // for tr_port_forwarding_state

class tr_port;
struct tr_upnp;

tr_upnp* tr_upnpInit();

void tr_upnpClose(tr_upnp* handle);

tr_port_forwarding_state tr_upnpPulse(
    tr_upnp* handle,
    tr_port advertised_port,
    tr_port local_port,
    bool is_enabled,
    bool do_port_check,
    std::string bindaddr);

/* @} */
