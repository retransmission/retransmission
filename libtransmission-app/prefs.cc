// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <optional>
#include <utility>

#include <fmt/format.h>

#include "libtransmission/quark.h"
#include "libtransmission/session-settings.h"

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

namespace
{

[[nodiscard]] constexpr bool is_prefs_key(tr_quark const key)
{
    return tr::serializer::has_key<SessionPrefs, AppPrefs>(key);
}

[[nodiscard]] std::string get_settings_filename(std::string_view const& config_dir)
{
    return fmt::format("{:s}/settings.json", config_dir);
}

[[nodiscard]] auto get_fallback_settings(std::string_view const settings_filename)
{
    // get pre-existing settings from the settings config file
    auto fallbacks = tr::settings::load(settings_filename);

    // fill in any missing values with defaults (eg missing/incomplete config file)
    fallbacks.merge(tr_sessionGetDefaultSettings());
    fallbacks.merge(tr::serializer::save(AppPrefs{}, SessionPrefs{}));

    return fallbacks;
}

void remove_transient_keys(tr::Settings& settings)
{
    // remove transient keys
    for (auto const key : { TR_KEY_filter_text }) {
        settings.erase(key);
    }
}

void remove_unrecognized_keys(tr::Settings& settings)
{
    settings.erase_if([](auto const& item) {
        auto const& [key, value] = item;
        return !is_prefs_key(key) && !tr::is_settings_key(key);
    });
}

} // namespace

Prefs::Prefs(tr::Settings const& settings)
{
    tr::serializer::load(settings, app_prefs_, session_prefs_);
}

Prefs::Prefs(std::string_view const config_dir)
    : Prefs{ get_fallback_settings(get_settings_filename(config_dir)) }
{
}

void Prefs::save(std::string_view const config_dir, std::optional<tr::Settings> const& local_session_settings) const
{
    auto const settings_filename = get_settings_filename(config_dir);

    // Get live app settings
    auto settings = tr::serializer::save(app_prefs_);

    // If it's a local session, save its settings.
    // Note we DON'T want to pollute local session settings with a remote session's values
    if (local_session_settings) {
        settings.merge(*local_session_settings);
    }

    // Ensure we have all the settings we need.
    // eg if we're connected to a remote session, `settings` doesn't have session settings yet.
    settings.merge(get_fallback_settings(settings_filename));

    remove_transient_keys(settings);
    remove_unrecognized_keys(settings);

    tr::settings::save(settings_filename, settings);
}

void Prefs::set(tr_quark const key, tr_variant const& var)
{
    if (tr::serializer::set_from_variant(key, var, app_prefs_, session_prefs_)) {
        on_changed(key);
    }
}
} // namespace tr::app
