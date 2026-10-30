// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "Flags.h"
#include "IconCache.h"

#include <libtransmission/types.h>
#include <libtransmission/values.h>

#include <gtkmm/treemodelcolumn.h>

#include <glibmm/extraclassinit.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <cstdint>
#include <memory>

struct tr_torrent;

class Torrent
    : public Glib::ExtraClassInit
    , public Glib::Object
{
public:
    class Columns : public Gtk::TreeModelColumnRecord
    {
    public:
        Columns();

        Gtk::TreeModelColumn<Torrent*> self;
        Gtk::TreeModelColumn<Glib::ustring> name_collated;
    };

    enum class ChangeFlag : uint8_t {
        ACTIVE_PEER_COUNT,
        ACTIVE_PEERS_DOWN,
        ACTIVE_PEERS_UP,
        ACTIVE,
        ACTIVITY,
        ADDED_DATE,
        ERROR_CODE,
        ERROR_MESSAGE,
        ETA,
        FINISHED,
        HAS_METADATA,
        IS_FOLDER,
        LONG_PROGRESS,
        LONG_STATUS,
        NAME,
        PERCENT_COMPLETE,
        PERCENT_DONE,
        PRIMARY_MIME_TYPE,
        PRIORITY,
        QUEUE_POSITION,
        RATIO,
        RECHECK_PROGRESS,
        SEED_RATIO_PERCENT_DONE,
        SPEED_DOWN,
        SPEED_UP,
        STALLED,
        TOTAL_SIZE,
        TRACKERS,
        N_FLAGS,
    };

    using ChangeFlags = Flags<ChangeFlag>;

    [[nodiscard]] friend constexpr ChangeFlags operator|(ChangeFlag const lhs, ChangeFlag const rhs) noexcept
    {
        return ChangeFlags{ lhs } | rhs;
    }

public:
    using Percent = tr::Values::Percent;
    using Ratio = tr::Values::Ratio;
    using Speed = tr::Values::Speed;
    using Storage = tr::Values::Storage;

    [[nodiscard]] int get_active_peer_count() const;
    [[nodiscard]] int get_active_peers_down() const;
    [[nodiscard]] int get_active_peers_up() const;
    [[nodiscard]] bool get_active() const;
    [[nodiscard]] tr_torrent_activity get_activity() const;
    [[nodiscard]] time_t get_added_date() const;
    [[nodiscard]] tr_stat::Error get_error_code() const;
    [[nodiscard]] Glib::ustring const& get_error_message() const;
    [[nodiscard]] time_t get_eta() const;
    [[nodiscard]] bool get_finished() const;
    [[nodiscard]] tr_torrent_id_t get_id() const;
    [[nodiscard]] Glib::ustring const& get_name_collated() const;
    [[nodiscard]] Glib::ustring get_name() const;
    [[nodiscard]] Percent get_percent_complete() const;
    [[nodiscard]] Percent get_percent_done() const;
    [[nodiscard]] float get_percent_done_fraction() const;
    [[nodiscard]] Glib::ustring get_percent_done_text() const;
    [[nodiscard]] tr_priority_t get_priority() const;
    [[nodiscard]] size_t get_queue_position() const;
    [[nodiscard]] Ratio get_ratio() const;
    [[nodiscard]] Percent get_recheck_progress() const;
    [[nodiscard]] Percent get_seed_ratio_percent_done() const;
    [[nodiscard]] Speed get_speed_down() const;
    [[nodiscard]] Speed get_speed_up() const;
    [[nodiscard]] tr_torrent& get_underlying() const;
    [[nodiscard]] Storage get_total_size() const;
    [[nodiscard]] size_t get_trackers() const;

    [[nodiscard]] Glib::RefPtr<TorrentIconType> get_icon() const;
    [[nodiscard]] Glib::ustring get_short_status_text() const;
    [[nodiscard]] Glib::ustring get_long_progress_text() const;
    [[nodiscard]] Glib::ustring get_long_status_text() const;
    [[nodiscard]] bool get_sensitive() const;
    [[nodiscard]] std::vector<Glib::ustring> get_css_classes() const;

    [[nodiscard]] ChangeFlags update();

    [[nodiscard]] static Glib::RefPtr<Torrent> create(tr_torrent* torrent);

    [[nodiscard]] static Columns const& get_columns();

    [[nodiscard]] static int get_item_id(Glib::RefPtr<Glib::ObjectBase const> const& item);
    static void get_item_value(Glib::RefPtr<Glib::ObjectBase const> const& item, int column, Glib::ValueBase& value);

    [[nodiscard]] static int compare_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs);
    [[nodiscard]] static bool less_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs);

private:
    Torrent();
    explicit Torrent(tr_torrent* torrent);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
