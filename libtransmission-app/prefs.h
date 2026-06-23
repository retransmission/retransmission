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
class BasePrefs
{
public:
    BasePrefs() = default;

    explicit BasePrefs(tr::Settings const& settings)
    {
        tr::serializer::load(*this, Fields, settings);
    }

    explicit BasePrefs(std::string_view config_dir)
    {
        tr::serializer::load(*this, Fields, tr_sessionLoadSettings(config_dir));
    }

    BasePrefs(BasePrefs&&) = delete;
    BasePrefs(BasePrefs const&) = delete;
    BasePrefs& operator=(BasePrefs&&) = delete;
    BasePrefs& operator=(BasePrefs const&) = delete;
    virtual ~BasePrefs() = default;

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(tr_quark const key) const
    {
        if (auto val = tr::serializer::to_variant(*this, key))
        {
            return { key, std::move(*val) };
        }

        return { key, tr_variant{} };
    }

    void set(tr_quark const key, tr_variant const& var)
    {
        if (tr::serializer::set_from_variant(*this, key, var))
        {
            on_changed(key);
        }
    }

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        if (tr::serializer::set(*this, key, val))
        {
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

protected:
    // Notification hook invoked whenever a `set()` actually changes a value.
    // The base implementation does nothing; subclasses override it to forward
    // the change (e.g. emit a `changed` signal).
    virtual void on_changed(tr_quark /*key*/)
    {
    }

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    using Traits = PrefsStringTraits<StringType>;

    std::chrono::sys_seconds blocklist_date_;
    StringType blocklist_url_;
    StringType default_trackers_;
    StringType dir_watch_ = Traits::from_utf8(tr_getDefaultDownloadDir());
    StringType download_dir_ = Traits::from_utf8(tr_getDefaultDownloadDir());
    StringType filter_text_;
    StringType filter_trackers_;
    StringType incomplete_dir_;
    StringType main_window_layout_order_ = Traits::from_utf8("menu,toolbar,filter,list,statusbar");
    StringType open_dialog_folder_ = Traits::home_dir();
    StringType rpc_password_;
    StringType rpc_username_;
    StringType rpc_whitelist_;
    StringType script_torrent_done_filename_;
    StringType script_torrent_done_seeding_filename_;
    StringType session_remote_host_ = Traits::from_utf8("localhost");
    StringType session_remote_password_;
    StringType session_remote_url_base_path_ = Traits::from_utf8("/transmission/");
    StringType session_remote_username_;
    StringType socket_diffserv_;
    std::vector<StringType> complete_sound_command_;
    double ratio_ = 0.0;
    int alt_speed_limit_down_ = 0;
    int alt_speed_limit_time_begin_ = 0;
    int alt_speed_limit_time_day_ = 0;
    int alt_speed_limit_time_end_ = 0;
    int alt_speed_limit_up_ = 0;
    int download_queue_size_ = 0;
    int dspeed_ = 0;
    int idle_limit_ = 0;
    int main_window_height_ = 500;
    int main_window_width_ = 600;
    int main_window_x_ = 50;
    int main_window_y_ = 50;
    int msglevel_ = 0;
    int peer_limit_global_ = 0;
    int peer_limit_torrent_ = 0;
    int peer_port_ = 0;
    int peer_port_random_high_ = 0;
    int peer_port_random_low_ = 0;
    int preallocation_ = 0;
    int queue_stalled_minutes_ = 0;
    int rpc_port_ = 0;
    int session_remote_port_ = static_cast<int>(TrDefaultRpcPort);
    int upload_slots_per_torrent_ = 0;
    int uspeed_ = 0;
    ShowMode filter_mode_ = DefaultShowMode;
    SortMode sort_mode_ = DefaultSortMode;
    StatsMode statusbar_stats_ = DefaultStatsMode;
    tr_encryption_mode encryption_ = {};
    bool alt_speed_limit_enabled_ = false;
    bool alt_speed_limit_time_enabled_ = false;
    bool askquit_ = true;
    bool blocklist_enabled_ = false;
    bool blocklist_updates_enabled_ = true;
    bool compact_view_ = false;
    bool complete_sound_enabled_ = true;
    bool dht_enabled_ = false;
    bool dir_watch_enabled_ = false;
    bool download_queue_enabled_ = false;
    bool dspeed_enabled_ = false;
    bool filterbar_ = true;
    bool idle_limit_enabled_ = false;
    bool incomplete_dir_enabled_ = false;
    bool inhibit_hibernation_ = false;
    bool lpd_enabled_ = false;
    bool options_prompt_ = true;
    bool peer_port_random_on_start_ = false;
    bool pex_enabled_ = false;
    bool port_forwarding_ = false;
    bool ratio_enabled_ = false;
    bool read_clipboard_ = false;
    bool rename_partial_files_ = false;
    bool rpc_auth_required_ = false;
    bool rpc_enabled_ = false;
    bool rpc_whitelist_enabled_ = false;
    bool script_torrent_done_enabled_ = false;
    bool script_torrent_done_seeding_enabled_ = false;
    bool session_is_remote_ = false;
    bool session_remote_auth_ = false;
    bool session_remote_https_ = false;
    bool show_backup_trackers_ = false;
    bool show_notification_on_add_ = true;
    bool show_notification_on_complete_ = true;
    bool show_tracker_scrapes_ = false;
    bool show_tray_icon_ = false;
    bool sort_reversed_ = false;
    bool start_ = false;
    bool start_minimized_ = false;
    bool statusbar_ = true;
    bool toolbar_ = true;
    bool trash_original_ = false;
    bool uspeed_enabled_ = false;
    bool utp_enabled_ = false;

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&BasePrefs::alt_speed_limit_down_>{ TR_KEY_alt_speed_down },
        Field<&BasePrefs::alt_speed_limit_enabled_>{ TR_KEY_alt_speed_enabled },
        Field<&BasePrefs::alt_speed_limit_time_begin_>{ TR_KEY_alt_speed_time_begin },
        Field<&BasePrefs::alt_speed_limit_time_day_>{ TR_KEY_alt_speed_time_day },
        Field<&BasePrefs::alt_speed_limit_time_enabled_>{ TR_KEY_alt_speed_time_enabled },
        Field<&BasePrefs::alt_speed_limit_time_end_>{ TR_KEY_alt_speed_time_end },
        Field<&BasePrefs::alt_speed_limit_up_>{ TR_KEY_alt_speed_up },
        Field<&BasePrefs::askquit_>{ TR_KEY_prompt_before_exit },
        Field<&BasePrefs::blocklist_date_>{ TR_KEY_blocklist_date },
        Field<&BasePrefs::blocklist_enabled_>{ TR_KEY_blocklist_enabled },
        Field<&BasePrefs::blocklist_updates_enabled_>{ TR_KEY_blocklist_updates_enabled },
        Field<&BasePrefs::blocklist_url_>{ TR_KEY_blocklist_url },
        Field<&BasePrefs::compact_view_>{ TR_KEY_compact_view },
        Field<&BasePrefs::complete_sound_command_>{ TR_KEY_torrent_complete_sound_command },
        Field<&BasePrefs::complete_sound_enabled_>{ TR_KEY_torrent_complete_sound_enabled },
        Field<&BasePrefs::default_trackers_>{ TR_KEY_default_trackers },
        Field<&BasePrefs::dht_enabled_>{ TR_KEY_dht_enabled },
        Field<&BasePrefs::dir_watch_>{ TR_KEY_watch_dir },
        Field<&BasePrefs::dir_watch_enabled_>{ TR_KEY_watch_dir_enabled },
        Field<&BasePrefs::download_dir_>{ TR_KEY_download_dir },
        Field<&BasePrefs::download_queue_enabled_>{ TR_KEY_download_queue_enabled },
        Field<&BasePrefs::download_queue_size_>{ TR_KEY_download_queue_size },
        Field<&BasePrefs::dspeed_>{ TR_KEY_speed_limit_down },
        Field<&BasePrefs::dspeed_enabled_>{ TR_KEY_speed_limit_down_enabled },
        Field<&BasePrefs::encryption_>{ TR_KEY_encryption },
        Field<&BasePrefs::filter_mode_>{ TR_KEY_filter_mode },
        Field<&BasePrefs::filter_text_>{ TR_KEY_filter_text },
        Field<&BasePrefs::filter_trackers_>{ TR_KEY_filter_trackers },
        Field<&BasePrefs::filterbar_>{ TR_KEY_show_filterbar },
        Field<&BasePrefs::idle_limit_>{ TR_KEY_idle_seeding_limit },
        Field<&BasePrefs::idle_limit_enabled_>{ TR_KEY_idle_seeding_limit_enabled },
        Field<&BasePrefs::incomplete_dir_>{ TR_KEY_incomplete_dir },
        Field<&BasePrefs::incomplete_dir_enabled_>{ TR_KEY_incomplete_dir_enabled },
        Field<&BasePrefs::inhibit_hibernation_>{ TR_KEY_inhibit_desktop_hibernation },
        Field<&BasePrefs::lpd_enabled_>{ TR_KEY_lpd_enabled },
        Field<&BasePrefs::main_window_height_>{ TR_KEY_main_window_height },
        Field<&BasePrefs::main_window_layout_order_>{ TR_KEY_main_window_layout_order },
        Field<&BasePrefs::main_window_width_>{ TR_KEY_main_window_width },
        Field<&BasePrefs::main_window_x_>{ TR_KEY_main_window_x },
        Field<&BasePrefs::main_window_y_>{ TR_KEY_main_window_y },
        Field<&BasePrefs::msglevel_>{ TR_KEY_message_level },
        Field<&BasePrefs::open_dialog_folder_>{ TR_KEY_open_dialog_dir },
        Field<&BasePrefs::options_prompt_>{ TR_KEY_show_options_window },
        Field<&BasePrefs::peer_limit_global_>{ TR_KEY_peer_limit_global },
        Field<&BasePrefs::peer_limit_torrent_>{ TR_KEY_peer_limit_per_torrent },
        Field<&BasePrefs::peer_port_>{ TR_KEY_peer_port },
        Field<&BasePrefs::peer_port_random_high_>{ TR_KEY_peer_port_random_high },
        Field<&BasePrefs::peer_port_random_low_>{ TR_KEY_peer_port_random_low },
        Field<&BasePrefs::peer_port_random_on_start_>{ TR_KEY_peer_port_random_on_start },
        Field<&BasePrefs::pex_enabled_>{ TR_KEY_pex_enabled },
        Field<&BasePrefs::port_forwarding_>{ TR_KEY_port_forwarding_enabled },
        Field<&BasePrefs::preallocation_>{ TR_KEY_preallocation },
        Field<&BasePrefs::queue_stalled_minutes_>{ TR_KEY_queue_stalled_minutes },
        Field<&BasePrefs::ratio_>{ TR_KEY_seed_ratio_limit },
        Field<&BasePrefs::ratio_enabled_>{ TR_KEY_seed_ratio_limited },
        Field<&BasePrefs::read_clipboard_>{ TR_KEY_read_clipboard },
        Field<&BasePrefs::rename_partial_files_>{ TR_KEY_rename_partial_files },
        Field<&BasePrefs::rpc_auth_required_>{ TR_KEY_rpc_authentication_required },
        Field<&BasePrefs::rpc_enabled_>{ TR_KEY_rpc_enabled },
        Field<&BasePrefs::rpc_password_>{ TR_KEY_rpc_password },
        Field<&BasePrefs::rpc_port_>{ TR_KEY_rpc_port },
        Field<&BasePrefs::rpc_username_>{ TR_KEY_rpc_username },
        Field<&BasePrefs::rpc_whitelist_>{ TR_KEY_rpc_whitelist },
        Field<&BasePrefs::rpc_whitelist_enabled_>{ TR_KEY_rpc_whitelist_enabled },
        Field<&BasePrefs::script_torrent_done_enabled_>{ TR_KEY_script_torrent_done_enabled },
        Field<&BasePrefs::script_torrent_done_filename_>{ TR_KEY_script_torrent_done_filename },
        Field<&BasePrefs::script_torrent_done_seeding_enabled_>{ TR_KEY_script_torrent_done_seeding_enabled },
        Field<&BasePrefs::script_torrent_done_seeding_filename_>{ TR_KEY_script_torrent_done_seeding_filename },
        Field<&BasePrefs::session_is_remote_>{ TR_KEY_remote_session_enabled },
        Field<&BasePrefs::session_remote_auth_>{ TR_KEY_remote_session_requires_authentication },
        Field<&BasePrefs::session_remote_host_>{ TR_KEY_remote_session_host },
        Field<&BasePrefs::session_remote_https_>{ TR_KEY_remote_session_https },
        Field<&BasePrefs::session_remote_password_>{ TR_KEY_remote_session_password },
        Field<&BasePrefs::session_remote_port_>{ TR_KEY_remote_session_port },
        Field<&BasePrefs::session_remote_url_base_path_>{ TR_KEY_remote_session_url_base_path },
        Field<&BasePrefs::session_remote_username_>{ TR_KEY_remote_session_username },
        Field<&BasePrefs::show_backup_trackers_>{ TR_KEY_show_backup_trackers },
        Field<&BasePrefs::show_notification_on_add_>{ TR_KEY_torrent_added_notification_enabled },
        Field<&BasePrefs::show_notification_on_complete_>{ TR_KEY_torrent_complete_notification_enabled },
        Field<&BasePrefs::show_tracker_scrapes_>{ TR_KEY_show_tracker_scrapes },
        Field<&BasePrefs::show_tray_icon_>{ TR_KEY_show_notification_area_icon },
        Field<&BasePrefs::socket_diffserv_>{ TR_KEY_peer_socket_diffserv },
        Field<&BasePrefs::sort_mode_>{ TR_KEY_sort_mode },
        Field<&BasePrefs::sort_reversed_>{ TR_KEY_sort_reversed },
        Field<&BasePrefs::start_>{ TR_KEY_start_added_torrents },
        Field<&BasePrefs::start_minimized_>{ TR_KEY_start_minimized },
        Field<&BasePrefs::statusbar_>{ TR_KEY_show_statusbar },
        Field<&BasePrefs::statusbar_stats_>{ TR_KEY_statusbar_stats },
        Field<&BasePrefs::toolbar_>{ TR_KEY_show_toolbar },
        Field<&BasePrefs::trash_original_>{ TR_KEY_trash_original_torrent_files },
        Field<&BasePrefs::upload_slots_per_torrent_>{ TR_KEY_upload_slots_per_torrent },
        Field<&BasePrefs::uspeed_>{ TR_KEY_speed_limit_up },
        Field<&BasePrefs::uspeed_enabled_>{ TR_KEY_speed_limit_up_enabled },
        Field<&BasePrefs::utp_enabled_>{ TR_KEY_utp_enabled });
};

} // namespace tr::app
