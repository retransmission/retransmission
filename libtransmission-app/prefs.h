// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "libtransmission/constants.h"
#include "libtransmission/converters.h"
#include "libtransmission/macros.h"
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/transmission.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/converters.h"
#include "libtransmission-app/display-modes.h"

namespace tr::app
{
/**
 * Local application preferences
 *
 * These belong to this client and are always meaningful,
 * whether we run an in-process session or are connected to a remote one.
 */
struct AppPrefs {
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    // sorted in descending-alignment order to minimize struct padding.
    std::chrono::sys_seconds blocklist_date_;
    std::string dir_watch_ = tr::platform::get_download_dir();
    std::string filter_text_;
    std::string filter_trackers_;
    std::string open_dialog_folder_ = tr::platform::get_home_dir();
    std::string session_remote_host_ = "localhost";
    std::string session_remote_password_;
    std::string session_remote_url_base_path_ = TR_PROJ_WEB_SERVER_BASE_PATH;
    std::string session_remote_username_;
    std::vector<std::string> complete_sound_command_;
    int details_window_height_ = 500;
    int details_window_width_ = 700;
    int main_window_height_ = 500;
    int main_window_width_ = 600;
    int main_window_x_ = 50;
    int main_window_y_ = 50;
    int session_remote_port_ = static_cast<int>(TrDefaultRpcPort);
    ShowMode show_mode_ = DefaultShowMode;
    SortMode sort_mode_ = DefaultSortMode;
    StatsMode statusbar_stats_ = DefaultStatsMode;
    bool blocklist_updates_enabled_ = true;
    bool compact_view_ = false;
    bool complete_sound_enabled_ = true;
    bool dir_watch_enabled_ = false;
    bool filterbar_ = true;
    bool inhibit_hibernation_ = false;
    bool main_window_is_maximized_ = false;
    bool options_prompt_ = true;
    bool read_clipboard_ = false;
    bool session_is_remote_ = false;
    bool session_remote_auth_ = false;
    bool session_remote_https_ = false;
    bool show_backup_trackers_ = false;
    bool show_extra_peer_details_ = false;
    bool show_notification_on_add_ = true;
    bool show_notification_on_complete_ = true;
    bool show_tracker_scrapes_ = false;
    bool show_tray_icon_ = false;
    bool sort_reversed_ = false;
    bool start_minimized_ = false;
    bool statusbar_ = true;
    bool toolbar_ = true;

    static constexpr auto Fields = std::make_tuple(
        Field<&AppPrefs::blocklist_date_>{ TR_KEY_blocklist_date },
        Field<&AppPrefs::blocklist_updates_enabled_>{ TR_KEY_blocklist_updates_enabled },
        Field<&AppPrefs::compact_view_>{ TR_KEY_compact_view },
        Field<&AppPrefs::complete_sound_command_>{ TR_KEY_torrent_complete_sound_command },
        Field<&AppPrefs::complete_sound_enabled_>{ TR_KEY_torrent_complete_sound_enabled },
        Field<&AppPrefs::details_window_height_>{ TR_KEY_details_window_height },
        Field<&AppPrefs::details_window_width_>{ TR_KEY_details_window_width },
        Field<&AppPrefs::dir_watch_>{ TR_KEY_watch_dir },
        Field<&AppPrefs::dir_watch_enabled_>{ TR_KEY_watch_dir_enabled },
        Field<&AppPrefs::show_mode_>{ TR_KEY_show_mode },
        Field<&AppPrefs::filter_text_>{ TR_KEY_filter_text },
        Field<&AppPrefs::filter_trackers_>{ TR_KEY_filter_trackers },
        Field<&AppPrefs::filterbar_>{ TR_KEY_show_filterbar },
        Field<&AppPrefs::inhibit_hibernation_>{ TR_KEY_inhibit_desktop_hibernation },
        Field<&AppPrefs::main_window_height_>{ TR_KEY_main_window_height },
        Field<&AppPrefs::main_window_is_maximized_>{ TR_KEY_main_window_is_maximized },
        Field<&AppPrefs::main_window_width_>{ TR_KEY_main_window_width },
        Field<&AppPrefs::main_window_x_>{ TR_KEY_main_window_x },
        Field<&AppPrefs::main_window_y_>{ TR_KEY_main_window_y },
        Field<&AppPrefs::open_dialog_folder_>{ TR_KEY_open_dialog_dir },
        Field<&AppPrefs::options_prompt_>{ TR_KEY_show_options_window },
        Field<&AppPrefs::read_clipboard_>{ TR_KEY_read_clipboard },
        Field<&AppPrefs::session_is_remote_>{ TR_KEY_remote_session_enabled },
        Field<&AppPrefs::session_remote_auth_>{ TR_KEY_remote_session_requires_authentication },
        Field<&AppPrefs::session_remote_host_>{ TR_KEY_remote_session_host },
        Field<&AppPrefs::session_remote_https_>{ TR_KEY_remote_session_https },
        Field<&AppPrefs::session_remote_password_>{ TR_KEY_remote_session_password },
        Field<&AppPrefs::session_remote_port_>{ TR_KEY_remote_session_port },
        Field<&AppPrefs::session_remote_url_base_path_>{ TR_KEY_remote_session_url_base_path },
        Field<&AppPrefs::session_remote_username_>{ TR_KEY_remote_session_username },
        Field<&AppPrefs::show_backup_trackers_>{ TR_KEY_show_backup_trackers },
        Field<&AppPrefs::show_extra_peer_details_>{ TR_KEY_show_extra_peer_details },
        Field<&AppPrefs::show_notification_on_add_>{ TR_KEY_torrent_added_notification_enabled },
        Field<&AppPrefs::show_notification_on_complete_>{ TR_KEY_torrent_complete_notification_enabled },
        Field<&AppPrefs::show_tracker_scrapes_>{ TR_KEY_show_tracker_scrapes },
        Field<&AppPrefs::show_tray_icon_>{ TR_KEY_show_notification_area_icon },
        Field<&AppPrefs::sort_mode_>{ TR_KEY_sort_mode },
        Field<&AppPrefs::sort_reversed_>{ TR_KEY_sort_reversed },
        Field<&AppPrefs::start_minimized_>{ TR_KEY_start_minimized },
        Field<&AppPrefs::statusbar_>{ TR_KEY_show_statusbar },
        Field<&AppPrefs::statusbar_stats_>{ TR_KEY_statusbar_stats },
        Field<&AppPrefs::toolbar_>{ TR_KEY_show_toolbar });
};

/**
 * Proxied Session Settings
 *
 * These mirror settings owned by the session.
 * They give us a place to hold the values when connected to a remote session.
 */
struct SessionPrefs {
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    SessionPrefs();

    // Initialize to empty defaults here to make the linter happy.
    // The real defaults are in the ctor, which gets libtransmission's defaults.
    std::string blocklist_url_;
    std::string default_trackers_;
    std::string download_dir_;
    std::string incomplete_dir_;
    std::string rpc_password_;
    std::string rpc_username_;
    std::string rpc_whitelist_;
    std::string script_torrent_done_filename_;
    std::string script_torrent_done_seeding_filename_;
    std::vector<std::string> recent_download_paths_;
    std::vector<std::string> recent_relocate_paths_;
    double ratio_ = {};
    size_t alt_speed_limit_down_ = {};
    size_t alt_speed_limit_time_begin_ = {};
    size_t alt_speed_limit_time_end_ = {};
    size_t alt_speed_limit_up_ = {};
    size_t download_queue_size_ = {};
    size_t dspeed_ = {};
    size_t peer_limit_global_ = {};
    size_t peer_limit_torrent_ = {};
    size_t queue_stalled_minutes_ = {};
    size_t upload_slots_per_torrent_ = {};
    size_t uspeed_ = {};
    tr_diffserv_t socket_diffserv_;
    tr_port peer_port_;
    tr_port peer_port_random_high_;
    tr_port peer_port_random_low_;
    tr_port rpc_port_;
    uint16_t idle_limit_ = {};
    tr_encryption_mode encryption_ = {};
    tr_file_preallocation preallocation_ = {};
    tr_log_level msglevel_ = {};
    tr_sched_day alt_speed_limit_time_day_ = TR_SCHED_ALL;
    bool alt_speed_limit_enabled_ = {};
    bool alt_speed_limit_time_enabled_ = {};
    bool blocklist_enabled_ = {};
    bool dht_enabled_ = {};
    bool download_queue_enabled_ = {};
    bool dspeed_enabled_ = {};
    bool idle_limit_enabled_ = {};
    bool incomplete_dir_enabled_ = {};
    bool lpd_enabled_ = {};
    bool peer_port_random_on_start_ = {};
    bool pex_enabled_ = {};
    bool port_forwarding_ = {};
    bool ratio_enabled_ = {};
    bool rename_partial_files_ = {};
    bool rpc_auth_required_ = {};
    bool rpc_enabled_ = {};
    bool rpc_whitelist_enabled_ = {};
    bool script_torrent_done_enabled_ = {};
    bool script_torrent_done_seeding_enabled_ = {};
    bool start_ = {};
    bool torrent_complete_verify_enabled_ = {};
    bool trash_original_ = {};
    bool uspeed_enabled_ = {};
    bool utp_enabled_ = {};

    static constexpr auto Fields = std::make_tuple(
        Field<&SessionPrefs::alt_speed_limit_down_>{ TR_KEY_alt_speed_down },
        Field<&SessionPrefs::alt_speed_limit_enabled_>{ TR_KEY_alt_speed_enabled },
        Field<&SessionPrefs::alt_speed_limit_time_begin_>{ TR_KEY_alt_speed_time_begin },
        Field<&SessionPrefs::alt_speed_limit_time_day_>{ TR_KEY_alt_speed_time_day },
        Field<&SessionPrefs::alt_speed_limit_time_enabled_>{ TR_KEY_alt_speed_time_enabled },
        Field<&SessionPrefs::alt_speed_limit_time_end_>{ TR_KEY_alt_speed_time_end },
        Field<&SessionPrefs::alt_speed_limit_up_>{ TR_KEY_alt_speed_up },
        Field<&SessionPrefs::blocklist_enabled_>{ TR_KEY_blocklist_enabled },
        Field<&SessionPrefs::blocklist_url_>{ TR_KEY_blocklist_url },
        Field<&SessionPrefs::default_trackers_>{ TR_KEY_default_trackers },
        Field<&SessionPrefs::dht_enabled_>{ TR_KEY_dht_enabled },
        Field<&SessionPrefs::download_dir_>{ TR_KEY_download_dir },
        Field<&SessionPrefs::download_queue_enabled_>{ TR_KEY_download_queue_enabled },
        Field<&SessionPrefs::download_queue_size_>{ TR_KEY_download_queue_size },
        Field<&SessionPrefs::dspeed_>{ TR_KEY_speed_limit_down },
        Field<&SessionPrefs::dspeed_enabled_>{ TR_KEY_speed_limit_down_enabled },
        Field<&SessionPrefs::encryption_>{ TR_KEY_encryption },
        Field<&SessionPrefs::idle_limit_>{ TR_KEY_idle_seeding_limit },
        Field<&SessionPrefs::idle_limit_enabled_>{ TR_KEY_idle_seeding_limit_enabled },
        Field<&SessionPrefs::incomplete_dir_>{ TR_KEY_incomplete_dir },
        Field<&SessionPrefs::incomplete_dir_enabled_>{ TR_KEY_incomplete_dir_enabled },
        Field<&SessionPrefs::lpd_enabled_>{ TR_KEY_lpd_enabled },
        Field<&SessionPrefs::msglevel_>{ TR_KEY_message_level },
        Field<&SessionPrefs::peer_limit_global_>{ TR_KEY_peer_limit_global },
        Field<&SessionPrefs::peer_limit_torrent_>{ TR_KEY_peer_limit_per_torrent },
        Field<&SessionPrefs::peer_port_>{ TR_KEY_peer_port },
        Field<&SessionPrefs::peer_port_random_high_>{ TR_KEY_peer_port_random_high },
        Field<&SessionPrefs::peer_port_random_low_>{ TR_KEY_peer_port_random_low },
        Field<&SessionPrefs::peer_port_random_on_start_>{ TR_KEY_peer_port_random_on_start },
        Field<&SessionPrefs::pex_enabled_>{ TR_KEY_pex_enabled },
        Field<&SessionPrefs::port_forwarding_>{ TR_KEY_port_forwarding_enabled },
        Field<&SessionPrefs::preallocation_>{ TR_KEY_preallocation },
        Field<&SessionPrefs::queue_stalled_minutes_>{ TR_KEY_queue_stalled_minutes },
        Field<&SessionPrefs::ratio_>{ TR_KEY_seed_ratio_limit },
        Field<&SessionPrefs::ratio_enabled_>{ TR_KEY_seed_ratio_limited },
        Field<&SessionPrefs::recent_download_paths_>{ TR_KEY_recent_download_paths },
        Field<&SessionPrefs::recent_relocate_paths_>{ TR_KEY_recent_relocate_paths },
        Field<&SessionPrefs::rename_partial_files_>{ TR_KEY_rename_partial_files },
        Field<&SessionPrefs::rpc_auth_required_>{ TR_KEY_rpc_authentication_required },
        Field<&SessionPrefs::rpc_enabled_>{ TR_KEY_rpc_enabled },
        Field<&SessionPrefs::rpc_password_>{ TR_KEY_rpc_password },
        Field<&SessionPrefs::rpc_port_>{ TR_KEY_rpc_port },
        Field<&SessionPrefs::rpc_username_>{ TR_KEY_rpc_username },
        Field<&SessionPrefs::rpc_whitelist_>{ TR_KEY_rpc_whitelist },
        Field<&SessionPrefs::rpc_whitelist_enabled_>{ TR_KEY_rpc_whitelist_enabled },
        Field<&SessionPrefs::script_torrent_done_enabled_>{ TR_KEY_script_torrent_done_enabled },
        Field<&SessionPrefs::script_torrent_done_filename_>{ TR_KEY_script_torrent_done_filename },
        Field<&SessionPrefs::script_torrent_done_seeding_enabled_>{ TR_KEY_script_torrent_done_seeding_enabled },
        Field<&SessionPrefs::script_torrent_done_seeding_filename_>{ TR_KEY_script_torrent_done_seeding_filename },
        Field<&SessionPrefs::socket_diffserv_>{ TR_KEY_peer_socket_diffserv },
        Field<&SessionPrefs::start_>{ TR_KEY_start_added_torrents },
        Field<&SessionPrefs::torrent_complete_verify_enabled_>{ TR_KEY_torrent_complete_verify_enabled },
        Field<&SessionPrefs::trash_original_>{ TR_KEY_trash_original_torrent_files },
        Field<&SessionPrefs::upload_slots_per_torrent_>{ TR_KEY_upload_slots_per_torrent },
        Field<&SessionPrefs::uspeed_>{ TR_KEY_speed_limit_up },
        Field<&SessionPrefs::uspeed_enabled_>{ TR_KEY_speed_limit_up_enabled },
        Field<&SessionPrefs::utp_enabled_>{ TR_KEY_utp_enabled });
};

[[nodiscard]] constexpr bool prefs_is_core(tr_quark const key)
{
    return tr::serializer::has_key<SessionPrefs>(key);
}

class Prefs
{
public:
    Prefs() = default;
    explicit Prefs(std::string_view config_dir);
    explicit Prefs(tr::Settings const& settings);
    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    virtual ~Prefs() = default;

    void set(tr_quark key, tr_variant const& var);
    void set(tr_quark /*key*/, char const* /*value*/) = delete;

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        if (tr::serializer::set(key, val, app_prefs_, session_prefs_)) {
            on_changed(key);
        }
    }

    template<typename T>
    [[nodiscard]] T get(tr_quark const key) const
    {
        if constexpr (std::is_same_v<T, tr_variant>) {
            return tr::serializer::to_variant(key, app_prefs_, session_prefs_).value_or(T{});
        } else {
            return tr::serializer::get<T>(key, app_prefs_, session_prefs_).value_or(T{});
        }
    }

    void save(std::string_view config_dir, std::optional<tr::Settings> const& local_session_settings) const;

protected:
    virtual void on_changed(tr_quark key) = 0;

private:
    AppPrefs app_prefs_;
    SessionPrefs session_prefs_;
};

} // namespace tr::app
