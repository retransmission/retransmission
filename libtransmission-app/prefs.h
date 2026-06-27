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

#include <sigslot/signal.hpp>

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
 *  - `static StringType home_dir();`
 */
template<typename StringType>
struct PrefsStringTraits;

/**
 * Returns `true` iff `key` is a setting owned by the session core (as opposed
 * to a UI-only preference). Kept as a free function so the big `switch` lives
 * in one translation unit instead of being re-instantiated per `StringType`.
 */
[[nodiscard]] bool prefs_is_core(tr_quark key);

template<typename StringType>
class Prefs final
{
public:
    Prefs() = default;

    explicit Prefs(tr::Settings const& settings)
    {
        tr::serializer::load(*this, Fields, settings);
    }

    explicit Prefs(std::string_view config_dir)
    {
        tr::serializer::load(*this, Fields, tr_sessionLoadSettings(config_dir));
    }

    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() = default;

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(tr_quark const key) const
    {
        if (auto val = tr::serializer::to_variant(*this, key)) {
            return { key, std::move(*val) };
        }

        return { key, tr_variant{} };
    }

    void set(tr_quark const key, tr_variant const& var)
    {
        if (tr::serializer::set_from_variant(*this, key, var)) {
            on_changed(key);
        }
    }

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        if (tr::serializer::set(*this, key, val)) {
            on_changed(key);
        }
    }

    void set(tr_quark /*key*/, char const* /*value*/) = delete;

    template<typename T>
    [[nodiscard]] T get(tr_quark const key) const
    {
        auto const val = tr::serializer::get<T>(*this, key);
        assert(val.has_value());
        return val.value_or(T{});
    }

    [[nodiscard]] tr::Settings current_settings() const
    {
        auto map = tr::serializer::save(*this, Fields);
        map.erase(TR_KEY_filter_text);
        return map;
    }

    void save(std::string_view filename) const
    {
        auto settings = current_settings();
        settings.merge(tr::settings::load(filename));
        settings.erase(TR_KEY_filter_text);
        tr::settings::save(filename, settings);
    }

    template<typename Observer>
    [[nodiscard]] sigslot::scoped_connection observe_changed(Observer observer) const
    {
        return changed_.connect_scoped(std::move(observer));
    }

private:
    void on_changed(tr_quark const key)
    {
        changed_(key);
    }

    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    using Traits = PrefsStringTraits<StringType>;

    mutable sigslot::signal<tr_quark> changed_;

    // --- Category 1: local application preferences ---
    // These belong to this client and are always meaningful,
    // whether we run an in-process session or are connected to a remote one.
    // Members are kept in descending-alignment order to minimize struct padding.
    std::chrono::sys_seconds blocklist_date_;
    StringType dir_watch_ = Traits::from_utf8(tr_getDefaultDownloadDir());
    StringType filter_text_;
    StringType filter_trackers_;
    StringType main_window_layout_order_ = Traits::from_utf8("menu,toolbar,filter,list,statusbar");
    StringType open_dialog_folder_ = Traits::home_dir();
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
    ShowMode filter_mode_ = DefaultShowMode;
    SortMode sort_mode_ = DefaultSortMode;
    StatsMode statusbar_stats_ = DefaultStatsMode;
    bool askquit_ = true;
    bool blocklist_updates_enabled_ = true;
    bool compact_view_ = false;
    bool complete_sound_enabled_ = true;
    bool dir_watch_enabled_ = false;
    bool filterbar_ = true;
    bool inhibit_hibernation_ = false;
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

    // --- Category 2: session-setting proxies ---
    // These mirror settings owned by the session.
    // They give us a place to hold the values when connected to a remote session.
    // Kept in descending-alignment order to minimize struct padding.
    StringType blocklist_url_;
    StringType default_trackers_;
    StringType download_dir_ = Traits::from_utf8(tr_getDefaultDownloadDir());
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

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&Prefs::alt_speed_limit_down_>{ TR_KEY_alt_speed_down },
        Field<&Prefs::alt_speed_limit_enabled_>{ TR_KEY_alt_speed_enabled },
        Field<&Prefs::alt_speed_limit_time_begin_>{ TR_KEY_alt_speed_time_begin },
        Field<&Prefs::alt_speed_limit_time_day_>{ TR_KEY_alt_speed_time_day },
        Field<&Prefs::alt_speed_limit_time_enabled_>{ TR_KEY_alt_speed_time_enabled },
        Field<&Prefs::alt_speed_limit_time_end_>{ TR_KEY_alt_speed_time_end },
        Field<&Prefs::alt_speed_limit_up_>{ TR_KEY_alt_speed_up },
        Field<&Prefs::askquit_>{ TR_KEY_prompt_before_exit },
        Field<&Prefs::blocklist_date_>{ TR_KEY_blocklist_date },
        Field<&Prefs::blocklist_enabled_>{ TR_KEY_blocklist_enabled },
        Field<&Prefs::blocklist_updates_enabled_>{ TR_KEY_blocklist_updates_enabled },
        Field<&Prefs::blocklist_url_>{ TR_KEY_blocklist_url },
        Field<&Prefs::compact_view_>{ TR_KEY_compact_view },
        Field<&Prefs::complete_sound_command_>{ TR_KEY_torrent_complete_sound_command },
        Field<&Prefs::complete_sound_enabled_>{ TR_KEY_torrent_complete_sound_enabled },
        Field<&Prefs::default_trackers_>{ TR_KEY_default_trackers },
        Field<&Prefs::details_window_height_>{ TR_KEY_details_window_height },
        Field<&Prefs::details_window_width_>{ TR_KEY_details_window_width },
        Field<&Prefs::dht_enabled_>{ TR_KEY_dht_enabled },
        Field<&Prefs::dir_watch_>{ TR_KEY_watch_dir },
        Field<&Prefs::dir_watch_enabled_>{ TR_KEY_watch_dir_enabled },
        Field<&Prefs::download_dir_>{ TR_KEY_download_dir },
        Field<&Prefs::download_queue_enabled_>{ TR_KEY_download_queue_enabled },
        Field<&Prefs::download_queue_size_>{ TR_KEY_download_queue_size },
        Field<&Prefs::dspeed_>{ TR_KEY_speed_limit_down },
        Field<&Prefs::dspeed_enabled_>{ TR_KEY_speed_limit_down_enabled },
        Field<&Prefs::encryption_>{ TR_KEY_encryption },
        Field<&Prefs::filter_mode_>{ TR_KEY_filter_mode },
        Field<&Prefs::filter_text_>{ TR_KEY_filter_text },
        Field<&Prefs::filter_trackers_>{ TR_KEY_filter_trackers },
        Field<&Prefs::filterbar_>{ TR_KEY_show_filterbar },
        Field<&Prefs::idle_limit_>{ TR_KEY_idle_seeding_limit },
        Field<&Prefs::idle_limit_enabled_>{ TR_KEY_idle_seeding_limit_enabled },
        Field<&Prefs::incomplete_dir_>{ TR_KEY_incomplete_dir },
        Field<&Prefs::incomplete_dir_enabled_>{ TR_KEY_incomplete_dir_enabled },
        Field<&Prefs::inhibit_hibernation_>{ TR_KEY_inhibit_desktop_hibernation },
        Field<&Prefs::lpd_enabled_>{ TR_KEY_lpd_enabled },
        Field<&Prefs::main_window_height_>{ TR_KEY_main_window_height },
        Field<&Prefs::main_window_layout_order_>{ TR_KEY_main_window_layout_order },
        Field<&Prefs::main_window_width_>{ TR_KEY_main_window_width },
        Field<&Prefs::main_window_x_>{ TR_KEY_main_window_x },
        Field<&Prefs::main_window_y_>{ TR_KEY_main_window_y },
        Field<&Prefs::msglevel_>{ TR_KEY_message_level },
        Field<&Prefs::open_dialog_folder_>{ TR_KEY_open_dialog_dir },
        Field<&Prefs::options_prompt_>{ TR_KEY_show_options_window },
        Field<&Prefs::peer_limit_global_>{ TR_KEY_peer_limit_global },
        Field<&Prefs::peer_limit_torrent_>{ TR_KEY_peer_limit_per_torrent },
        Field<&Prefs::peer_port_>{ TR_KEY_peer_port },
        Field<&Prefs::peer_port_random_high_>{ TR_KEY_peer_port_random_high },
        Field<&Prefs::peer_port_random_low_>{ TR_KEY_peer_port_random_low },
        Field<&Prefs::peer_port_random_on_start_>{ TR_KEY_peer_port_random_on_start },
        Field<&Prefs::pex_enabled_>{ TR_KEY_pex_enabled },
        Field<&Prefs::port_forwarding_>{ TR_KEY_port_forwarding_enabled },
        Field<&Prefs::preallocation_>{ TR_KEY_preallocation },
        Field<&Prefs::queue_stalled_minutes_>{ TR_KEY_queue_stalled_minutes },
        Field<&Prefs::ratio_>{ TR_KEY_seed_ratio_limit },
        Field<&Prefs::ratio_enabled_>{ TR_KEY_seed_ratio_limited },
        Field<&Prefs::read_clipboard_>{ TR_KEY_read_clipboard },
        Field<&Prefs::rename_partial_files_>{ TR_KEY_rename_partial_files },
        Field<&Prefs::rpc_auth_required_>{ TR_KEY_rpc_authentication_required },
        Field<&Prefs::rpc_enabled_>{ TR_KEY_rpc_enabled },
        Field<&Prefs::rpc_password_>{ TR_KEY_rpc_password },
        Field<&Prefs::rpc_port_>{ TR_KEY_rpc_port },
        Field<&Prefs::rpc_username_>{ TR_KEY_rpc_username },
        Field<&Prefs::rpc_whitelist_>{ TR_KEY_rpc_whitelist },
        Field<&Prefs::rpc_whitelist_enabled_>{ TR_KEY_rpc_whitelist_enabled },
        Field<&Prefs::script_torrent_done_enabled_>{ TR_KEY_script_torrent_done_enabled },
        Field<&Prefs::script_torrent_done_filename_>{ TR_KEY_script_torrent_done_filename },
        Field<&Prefs::script_torrent_done_seeding_enabled_>{ TR_KEY_script_torrent_done_seeding_enabled },
        Field<&Prefs::script_torrent_done_seeding_filename_>{ TR_KEY_script_torrent_done_seeding_filename },
        Field<&Prefs::session_is_remote_>{ TR_KEY_remote_session_enabled },
        Field<&Prefs::session_remote_auth_>{ TR_KEY_remote_session_requires_authentication },
        Field<&Prefs::session_remote_host_>{ TR_KEY_remote_session_host },
        Field<&Prefs::session_remote_https_>{ TR_KEY_remote_session_https },
        Field<&Prefs::session_remote_password_>{ TR_KEY_remote_session_password },
        Field<&Prefs::session_remote_port_>{ TR_KEY_remote_session_port },
        Field<&Prefs::session_remote_url_base_path_>{ TR_KEY_remote_session_url_base_path },
        Field<&Prefs::session_remote_username_>{ TR_KEY_remote_session_username },
        Field<&Prefs::show_backup_trackers_>{ TR_KEY_show_backup_trackers },
        Field<&Prefs::show_notification_on_add_>{ TR_KEY_torrent_added_notification_enabled },
        Field<&Prefs::show_notification_on_complete_>{ TR_KEY_torrent_complete_notification_enabled },
        Field<&Prefs::show_tracker_scrapes_>{ TR_KEY_show_tracker_scrapes },
        Field<&Prefs::show_tray_icon_>{ TR_KEY_show_notification_area_icon },
        Field<&Prefs::socket_diffserv_>{ TR_KEY_peer_socket_diffserv },
        Field<&Prefs::sort_mode_>{ TR_KEY_sort_mode },
        Field<&Prefs::sort_reversed_>{ TR_KEY_sort_reversed },
        Field<&Prefs::start_>{ TR_KEY_start_added_torrents },
        Field<&Prefs::start_minimized_>{ TR_KEY_start_minimized },
        Field<&Prefs::statusbar_>{ TR_KEY_show_statusbar },
        Field<&Prefs::statusbar_stats_>{ TR_KEY_statusbar_stats },
        Field<&Prefs::toolbar_>{ TR_KEY_show_toolbar },
        Field<&Prefs::trash_original_>{ TR_KEY_trash_original_torrent_files },
        Field<&Prefs::upload_slots_per_torrent_>{ TR_KEY_upload_slots_per_torrent },
        Field<&Prefs::uspeed_>{ TR_KEY_speed_limit_up },
        Field<&Prefs::uspeed_enabled_>{ TR_KEY_speed_limit_up_enabled },
        Field<&Prefs::utp_enabled_>{ TR_KEY_utp_enabled });
};

} // namespace tr::app
