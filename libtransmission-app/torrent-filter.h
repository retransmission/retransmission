// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "libtransmission/quark.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

// whether the main window's `mode` filter shows `tor`
[[nodiscard]] bool should_show_torrent(Torrent const& tor, ShowMode mode) noexcept;

// the fields `should_show_torrent()` reads, so a change to one of them may hide or show a torrent
[[nodiscard]] consteval TorrentFieldSet show_mode_fields() noexcept
{
    return Torrent::
        fields<TR_KEY_error, TR_KEY_is_finished, TR_KEY_peers_getting_from_us, TR_KEY_peers_sending_to_us, TR_KEY_status>();
}

} // namespace tr::app
