// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <tuple>
#include <utility>

#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>

#include <libtransmission/constants.h>
#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/converters.h>
#include <libtransmission-app/display-modes.h>

#include "UserMetaType.h"
#include "VariantHelpers.h"

class Prefs : public QObject
{
    Q_OBJECT

public:
    Prefs() = default;
    explicit Prefs(tr::Settings const& settings);
    explicit Prefs(QString const& dir);
    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() override = default;

    [[nodiscard]] static bool isCore(tr_quark key);

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
            emit changed(key);
        }
    }

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        if (tr::serializer::set(*this, key, val))
        {
            emit changed(key);
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

    [[nodiscard]] tr::Settings current_settings() const;
    void save(QString const& filename) const;

signals:
    void changed(tr_quark key);

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    std::chrono::sys_seconds blocklist_date_ = {};
    QString blocklist_url_;
    QString default_trackers_;
    QString dir_watch_ = QString::fromStdString(tr_getDefaultDownloadDir());
    QString download_dir_ = QString::fromStdString(tr_getDefaultDownloadDir());
    QString filter_text_;
    QString filter_trackers_;
    QString incomplete_dir_;
    QString main_window_layout_order_ = QStringLiteral("menu,toolbar,filter,list,statusbar");
    QString open_dialog_folder_ = QDir::home().absolutePath();
    QString rpc_password_;
    QString rpc_username_;
    QString rpc_whitelist_;
    QString script_torrent_done_filename_;
    QString script_torrent_done_seeding_filename_;
    QString session_remote_host_ = QStringLiteral("localhost");
    QString session_remote_password_;
    QString session_remote_url_base_path_ = QStringLiteral("/transmission/");
    QString session_remote_username_;
    QString socket_diffserv_;
    QStringList complete_sound_command_;
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
