// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/torrent-filter.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

// NB: `show_mode_fields()` lists every field read here; keep them in sync
bool should_show_torrent(Torrent const& tor, ShowMode const mode) noexcept
{
    switch (mode) {
    case ShowMode::ShowActive:
        return tor.peers_getting_from_us() > 0 || tor.peers_sending_to_us() > 0 || tor.is_verifying();

    case ShowMode::ShowDownloading:
        return tor.is_downloading() || tor.is_waiting_to_download();

    case ShowMode::ShowError:
        return tor.has_error();

    case ShowMode::ShowFinished:
        return tor.is_finished();

    case ShowMode::ShowPaused:
        return tor.is_paused();

    case ShowMode::ShowSeeding:
        return tor.is_seeding() || tor.is_waiting_to_seed();

    case ShowMode::ShowVerifying:
        return tor.is_verifying() || tor.is_waiting_to_verify();

    default:
        return true;
    }
}

} // namespace tr::app
