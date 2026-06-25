// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/quark.h>

#include "libtransmission-app/prefs.h"

namespace tr::app
{

bool prefs_is_core(tr_quark const key)
{
    switch (key) {
    case TR_KEY_alt_speed_up:
    case TR_KEY_alt_speed_down:
    case TR_KEY_alt_speed_enabled:
    case TR_KEY_alt_speed_time_begin:
    case TR_KEY_alt_speed_time_end:
    case TR_KEY_alt_speed_time_enabled:
    case TR_KEY_alt_speed_time_day:
    case TR_KEY_blocklist_enabled:
    case TR_KEY_blocklist_url:
    case TR_KEY_default_trackers:
    case TR_KEY_speed_limit_down:
    case TR_KEY_speed_limit_down_enabled:
    case TR_KEY_download_dir:
    case TR_KEY_download_queue_enabled:
    case TR_KEY_download_queue_size:
    case TR_KEY_encryption:
    case TR_KEY_idle_seeding_limit:
    case TR_KEY_idle_seeding_limit_enabled:
    case TR_KEY_incomplete_dir:
    case TR_KEY_incomplete_dir_enabled:
    case TR_KEY_message_level:
    case TR_KEY_peer_limit_global:
    case TR_KEY_peer_limit_per_torrent:
    case TR_KEY_peer_port:
    case TR_KEY_peer_port_random_on_start:
    case TR_KEY_peer_port_random_low:
    case TR_KEY_peer_port_random_high:
    case TR_KEY_queue_stalled_minutes:
    case TR_KEY_script_torrent_done_enabled:
    case TR_KEY_script_torrent_done_filename:
    case TR_KEY_script_torrent_done_seeding_enabled:
    case TR_KEY_script_torrent_done_seeding_filename:
    case TR_KEY_peer_socket_diffserv:
    case TR_KEY_start_added_torrents:
    case TR_KEY_trash_original_torrent_files:
    case TR_KEY_pex_enabled:
    case TR_KEY_dht_enabled:
    case TR_KEY_utp_enabled:
    case TR_KEY_lpd_enabled:
    case TR_KEY_port_forwarding_enabled:
    case TR_KEY_preallocation:
    case TR_KEY_seed_ratio_limit:
    case TR_KEY_seed_ratio_limited:
    case TR_KEY_rename_partial_files:
    case TR_KEY_rpc_authentication_required:
    case TR_KEY_rpc_enabled:
    case TR_KEY_rpc_password:
    case TR_KEY_rpc_port:
    case TR_KEY_rpc_username:
    case TR_KEY_rpc_whitelist_enabled:
    case TR_KEY_rpc_whitelist:
    case TR_KEY_speed_limit_up_enabled:
    case TR_KEY_speed_limit_up:
    case TR_KEY_upload_slots_per_torrent:
        return true;

    default:
        return false;
    }
}

} // namespace tr::app
