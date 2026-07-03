// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cassert>
#include <chrono>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <libtransmission/constants.h>
#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/converters.h>
#include <libtransmission/transmission.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/converters.h>
#include <libtransmission-app/display-modes.h>

namespace tr::app
{

/**
 * Customization point that supplies the toolkit-specific bits.
 *
 * Specializations should provide:
 *  - `static StringType from_utf8(std::string_view);`
 */
template<typename StringType>
struct PrefsStringTraits;

/**
 * Returns `true` iff `key` is a setting owned by the session core (as opposed
 * to a UI-only preference). Kept as a free function so the big `switch` lives
 * in one translation unit instead of being re-instantiated per `StringType`.
 */
[[nodiscard]] bool prefs_is_core(tr_quark key);

/**
 * Local application preferences
 *
 * These belong to this client and are always meaningful,
 * whether we run an in-process session or are connected to a remote one.
 */
template<typename StringType>
struct AppPrefs {
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    using Traits = PrefsStringTraits<StringType>;

    // sorted in descending-alignment order to minimize struct padding.
    std::chrono::sys_seconds blocklist_date_;
    StringType dir_watch_ = Traits::from_utf8(tr::platform::get_download_dir());
    StringType filter_text_;
    StringType filter_trackers_;
    StringType main_window_layout_order_ = Traits::from_utf8("menu,toolbar,filter,list,statusbar");
    StringType open_dialog_folder_ = Traits::from_utf8(tr::platform::get_home_dir());
    StringType session_remote_host_ = Traits::from_utf8("localhost");
    StringType session_remote_password_;
    StringType session_remote_url_base_path_ = Traits::from_utf8("/transmission/");
    StringType session_remote_username_;
    std::vector<StringType> complete_sound_command_;
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
    bool askquit_ = true;
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
    bool show_notification_on_add_ = true;
    bool show_notification_on_complete_ = true;
    bool show_tracker_scrapes_ = false;
    bool show_tray_icon_ = false;
    bool sort_reversed_ = false;
    bool start_minimized_ = false;
    bool statusbar_ = true;
    bool toolbar_ = true;

    static constexpr auto Fields = std::make_tuple(
        Field<&AppPrefs::askquit_>{ TR_KEY_prompt_before_exit },
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
        Field<&AppPrefs::main_window_layout_order_>{ TR_KEY_main_window_layout_order },
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
template<typename StringType>
struct SessionPrefs {
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    using Traits = PrefsStringTraits<StringType>;

    // sorted in descending-alignment order to minimize struct padding.
    StringType blocklist_url_;
    StringType default_trackers_;
    StringType download_dir_ = Traits::from_utf8(tr::platform::get_download_dir());
    StringType incomplete_dir_;
    StringType rpc_password_;
    StringType rpc_username_;
    StringType rpc_whitelist_;
    StringType script_torrent_done_filename_;
    StringType script_torrent_done_seeding_filename_;
    StringType socket_diffserv_;
    double ratio_ = 0.0;
    int alt_speed_limit_down_ = 0;
    int alt_speed_limit_time_begin_ = 0;
    int alt_speed_limit_time_day_ = 0;
    int alt_speed_limit_time_end_ = 0;
    int alt_speed_limit_up_ = 0;
    int download_queue_size_ = 0;
    int dspeed_ = 0;
    int idle_limit_ = 0;
    int msglevel_ = 0;
    int peer_limit_global_ = 0;
    int peer_limit_torrent_ = 0;
    int peer_port_ = 0;
    int peer_port_random_high_ = 0;
    int peer_port_random_low_ = 0;
    int preallocation_ = 0;
    int queue_stalled_minutes_ = 0;
    int rpc_port_ = 0;
    int upload_slots_per_torrent_ = 0;
    int uspeed_ = 0;
    tr_encryption_mode encryption_ = {};
    bool alt_speed_limit_enabled_ = false;
    bool alt_speed_limit_time_enabled_ = false;
    bool blocklist_enabled_ = false;
    bool dht_enabled_ = false;
    bool download_queue_enabled_ = false;
    bool dspeed_enabled_ = false;
    bool idle_limit_enabled_ = false;
    bool incomplete_dir_enabled_ = false;
    bool lpd_enabled_ = false;
    bool peer_port_random_on_start_ = false;
    bool pex_enabled_ = false;
    bool port_forwarding_ = false;
    bool ratio_enabled_ = false;
    bool rename_partial_files_ = false;
    bool rpc_auth_required_ = false;
    bool rpc_enabled_ = false;
    bool rpc_whitelist_enabled_ = false;
    bool script_torrent_done_enabled_ = false;
    bool script_torrent_done_seeding_enabled_ = false;
    bool start_ = false;
    bool trash_original_ = false;
    bool uspeed_enabled_ = false;
    bool utp_enabled_ = false;

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
        Field<&SessionPrefs::trash_original_>{ TR_KEY_trash_original_torrent_files },
        Field<&SessionPrefs::upload_slots_per_torrent_>{ TR_KEY_upload_slots_per_torrent },
        Field<&SessionPrefs::uspeed_>{ TR_KEY_speed_limit_up },
        Field<&SessionPrefs::uspeed_enabled_>{ TR_KEY_speed_limit_up_enabled },
        Field<&SessionPrefs::utp_enabled_>{ TR_KEY_utp_enabled });
};

[[nodiscard]] constexpr bool is_prefs_key(tr_quark const key)
{
    using SomeType = int; // not actually used; just needs to be a concrete type
    return tr::serializer::has_key<SessionPrefs<SomeType>, AppPrefs<SomeType>>(key);
}

template<typename StringType>
class Prefs
{
public:
    using Traits = PrefsStringTraits<StringType>;

    Prefs() = default;

    explicit Prefs(tr::Settings const& settings)
    {
        tr::serializer::load(settings, app_prefs_, session_prefs_);
    }

    explicit Prefs(StringType const& config_dir_in)
    {
        auto const config_dir = Traits::to_utf8(config_dir_in);
        tr::serializer::load(tr_sessionLoadSettings(config_dir), app_prefs_, session_prefs_);
    }

    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    virtual ~Prefs() = default;

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(tr_quark const key) const
    {
        if (auto val = tr::serializer::to_variant(key, app_prefs_, session_prefs_))
            return { key, std::move(*val) };

        return { key, tr_variant{} };
    }

    void set(tr_quark const key, tr_variant const& var)
    {
        if (tr::serializer::set_from_variant(key, var, app_prefs_, session_prefs_))
            on_changed(key);
    }

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        if (tr::serializer::set(key, val, app_prefs_, session_prefs_))
            on_changed(key);
    }

    void set(tr_quark /*key*/, char const* /*value*/) = delete;

    template<typename T>
    [[nodiscard]] T get(tr_quark const key) const
    {
        auto const val = tr::serializer::get<T>(key, app_prefs_, session_prefs_);
        assert(val.has_value());
        return val.value_or(T{});
    }

    [[nodiscard]] tr::Settings app_settings() const
    {
        return tr::serializer::save(app_prefs_);
    }

    [[nodiscard]] tr::Settings session_settings() const
    {
        return tr::serializer::save(session_prefs_);
    }

protected:
    virtual void on_changed(tr_quark const key) = 0;

private:
    AppPrefs<StringType> app_prefs_;
    SessionPrefs<StringType> session_prefs_;
};

} // namespace tr::app
