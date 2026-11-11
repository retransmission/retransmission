// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "IconCache.h"

#include <libtransmission-app/torrent.h>

#include <libtransmission/types.h>
#include <libtransmission/values.h>

#include <gtkmm/treemodelcolumn.h>

#include <glibmm/extraclassinit.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <memory>
#include <string>
#include <vector>

// One row of the session's `tr::app::TorrentList`, as a GLib object for the torrent views.
// Its properties are what a row shows; `update()` tells the views which of them changed.
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

    using Fields = tr::app::TorrentFieldSet;
    using Percent = tr::Values::Percent;

    // the fields behind `get_name()`, `get_name_collated()` and `get_name_collate_key()`
    static constexpr auto NameFields = tr::app::Torrent::fields<TR_KEY_hash_string, TR_KEY_name>();

    [[nodiscard]] tr::app::Torrent const& get_shared() const noexcept;

    [[nodiscard]] tr_torrent_id_t get_id() const noexcept;
    [[nodiscard]] Glib::ustring get_name() const;
    // the lowercase name, a tab and the info hash, for the type-ahead search
    [[nodiscard]] Glib::ustring const& get_name_collated() const noexcept;
    // the lowercase name's collation key: two keys compare as `std::string`s
    // the way their names compare as `Glib::ustring`s, without collating again
    [[nodiscard]] std::string const& get_name_collate_key() const noexcept;

    [[nodiscard]] Percent get_percent_done() const;
    [[nodiscard]] float get_percent_done_fraction() const;
    [[nodiscard]] Glib::ustring get_percent_done_text() const;

    [[nodiscard]] Glib::RefPtr<TorrentIconType> get_icon() const;
    [[nodiscard]] Glib::ustring get_short_status_text() const;
    [[nodiscard]] Glib::ustring get_long_progress_text() const;
    [[nodiscard]] Glib::ustring get_long_status_text() const;
    [[nodiscard]] bool get_sensitive() const;
    [[nodiscard]] std::vector<Glib::ustring> get_css_classes() const;

    // Notifies the views of the properties that `fields`, the shared torrent's changed fields, affect.
    void update(Fields const& fields);

    // Keeps a copy of the shared torrent, which its list is about to destroy,
    // so a view still holding this object reads the last values.
    void detach();

    // `torrent` must outlive the object, or be detached from it first.
    [[nodiscard]] static Glib::RefPtr<Torrent> create(tr::app::Torrent const& torrent);

    [[nodiscard]] static Columns const& get_columns();

    [[nodiscard]] static int get_item_id(Glib::RefPtr<Glib::ObjectBase const> const& item);
    static void get_item_value(Glib::RefPtr<Glib::ObjectBase const> const& item, int column, Glib::ValueBase& value);

private:
    Torrent();
    explicit Torrent(tr::app::Torrent const& torrent);

    class Impl;
    std::unique_ptr<Impl> const impl_;
};
