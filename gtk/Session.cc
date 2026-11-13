// This file Copyright © Retransmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#include "Session.h"

#include "Notify.h"
#include "PrefsDialog.h"
#include "SortListModel.hh"
#include "Torrent.h"
#include "TorrentSorter.h"
#include "Utils.h"

#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>

#include <libtransmission/converters.h>
#include <libtransmission/macros.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include <giomm/liststore.h>

#include <glibmm/i18n.h>

#include <sigslot/signal.hpp>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/sortlistmodel.h>
#else
#include <gtkmm/treemodelsort.h>
#endif

#include <algorithm>
#include <array>
#include <cstring> // strstr
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

using tr::app::FaviconCache;
using tr::app::Prefs;
using tr::app::RpcClient;
using tr::app::SortMode;
using tr::app::TorrentList;

class Session::Impl
{
public:
    explicit Impl(Session& core);
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    ~Impl() = default;

    [[nodiscard]] Glib::RefPtr<Gio::ListStore<Torrent>> get_raw_model() const;
    [[nodiscard]] Glib::RefPtr<SortListModel<Torrent>> get_model();

    [[nodiscard]] auto& signal_prefs_changed()
    {
        return signal_prefs_changed_;
    }

    [[nodiscard]] auto& signal_torrents_changed()
    {
        return signal_torrents_changed_;
    }

    [[nodiscard]] constexpr auto& favicon_cache()
    {
        return favicon_cache_;
    }

private:
    [[nodiscard]] Glib::RefPtr<Session> get_core_ptr() const;

    void on_pref_changed(tr_quark key);

    // keep raw_model_ a mirror of the list's rows
    void on_rows_inserted(size_t row, size_t count);
    void on_rows_removing(size_t row, size_t count);
    void on_torrents_changed(TorrentList::Changes const& changes);

private:
    Session& core_;

    sigc::signal<void(tr_quark)> signal_prefs_changed_;
    sigc::signal<void(TorrentList::Ids const&, Torrent::Fields const&)> signal_torrents_changed_;

    sigslot::scoped_connection prefs_changed_tag_;
    std::array<sigslot::scoped_connection, 3U> torrent_list_tags_;

    Glib::RefPtr<Gio::ListStore<Torrent>> raw_model_;
    Glib::RefPtr<SortListModel<Torrent>> sorted_model_;
    Glib::RefPtr<TorrentSorter> sorter_ = TorrentSorter::create();

    FaviconCache<Glib::RefPtr<Gdk::Pixbuf>> favicon_cache_;
};

Glib::RefPtr<Session> Session::Impl::get_core_ptr() const
{
    core_.reference();
    return Glib::make_refptr_for_instance(&core_);
}

/***
****
***/

Glib::RefPtr<Gio::ListStore<Torrent>> Session::Impl::get_raw_model() const
{
    return raw_model_;
}

Glib::RefPtr<Gio::ListModel> Session::get_model() const
{
    return impl_->get_raw_model();
}

Glib::RefPtr<Session::Model> Session::get_sorted_model() const
{
    return impl_->get_model();
}

Glib::RefPtr<SortListModel<Torrent>> Session::Impl::get_model()
{
    return sorted_model_;
}

/***
****
***/

void Session::Impl::on_pref_changed(tr_quark const key)
{
    switch (key) {
    case TR_KEY_sort_mode:
        sorter_->set_mode(core_.prefs_.get<SortMode>(TR_KEY_sort_mode));
        break;

    case TR_KEY_sort_reversed:
        sorter_->set_reversed(core_.prefs_.get<bool>(TR_KEY_sort_reversed));
        break;

    default:
        break;
    }
}

/**
***
**/

Glib::RefPtr<Session> Session::create(std::string config_dir, Prefs& prefs, RpcClient& rpc)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new Session(std::move(config_dir), prefs, rpc));
}

Session::Session(std::string config_dir, Prefs& prefs, RpcClient& rpc)
    : Glib::ObjectBase(typeid(Session))
    , tr::app::Session(std::move(config_dir), prefs, rpc, gtr_file_trash_or_remove)
    , prefs_(prefs)
    , impl_(std::make_unique<Impl>(*this))
{
}

Session::~Session() = default;

Session::Impl::Impl(Session& core)
    : core_{ core }
{
    raw_model_ = Gio::ListStore<Torrent>::create();
    auto& torrents = core_.torrents();
    torrent_list_tags_ = { torrents.rows_inserted.connect_scoped(&Impl::on_rows_inserted, this),
                           torrents.rows_removing.connect_scoped(&Impl::on_rows_removing, this),
                           torrents.changed.connect_scoped(&Impl::on_torrents_changed, this) };
    signal_torrents_changed_.connect(sigc::hide<0>(sigc::mem_fun(*sorter_, &TorrentSorter::update)));
    sorted_model_ = SortListModel<Torrent>::create(gtr_ptr_static_cast<Gio::ListModel>(raw_model_), sorter_);

    /* init from prefs & listen to pref changes */
    on_pref_changed(TR_KEY_sort_mode);
    on_pref_changed(TR_KEY_sort_reversed);
    signal_prefs_changed_.connect([this](auto key) { on_pref_changed(key); });
    prefs_changed_tag_ = core_.prefs_.observe_changes([this](tr_quark const key) { signal_prefs_changed_.emit(key); });
}

void Session::import_embedded_settings()
{
    if (auto const settings = embedded_settings(); settings) {
        import_session_settings(*settings);
    }
}

/***
****  MIRRORING THE TORRENT LIST
***/

void Session::Impl::on_rows_inserted(size_t const row, size_t const count)
{
    auto const& torrents = core_.torrents();

    auto items = std::vector<Glib::RefPtr<Torrent>>{};
    items.reserve(count);
    for (auto i = row; i < row + count; ++i) {
        items.emplace_back(Torrent::create(torrents[i]));
    }

    raw_model_->splice(static_cast<guint>(row), 0U, items);
}

void Session::Impl::on_rows_removing(size_t const row, size_t const count)
{
    // A view may still hold one of these, so give it a copy of the torrent the list is about to destroy.
    for (auto i = row; i < row + count; ++i) {
        raw_model_->get_item(static_cast<guint>(i))->detach();
    }

    raw_model_->splice(static_cast<guint>(row), static_cast<guint>(count), {});
}

void Session::Impl::on_torrents_changed(TorrentList::Changes const& changes)
{
    for (auto const id : changes.added) {
        gtr_notify_torrent_added(get_core_ptr(), core_.prefs_, id);
    }

    for (auto const id : changes.completed) {
        gtr_notify_torrent_completed(get_core_ptr(), core_.prefs_, id);
    }

    if (std::empty(changes.changed)) {
        return;
    }

    auto ids = TorrentList::Ids{};
    ids.reserve(std::size(changes.changed));
    for (auto const& change : changes.changed) {
        raw_model_->get_item(static_cast<guint>(change.row))->update(change.fields);
        ids.push_back(change.id);
    }

    signal_torrents_changed_.emit(ids, changes.changed_fields);
}

void Session::fetch_torrent(tr_torrent_id_t const id)
{
    refresh_torrents({ id }, tr::app::TorrentKeySet::MainAll);
}

// ---

void Session::fetch_recent_paths(tr_quark const key, sigc::slot<void(std::vector<Glib::ustring> const&)> on_paths)
{
    tr::app::Session::fetch_recent_paths(key, [on_paths = std::move(on_paths)](std::vector<std::string> const& paths) {
        on_paths(std::vector<Glib::ustring>{ std::begin(paths), std::end(paths) });
    });
}

/***
****
***/

size_t Session::get_torrent_count() const
{
    return std::size(torrents());
}

size_t Session::get_unpaused_torrent_count() const
{
    return static_cast<size_t>(std::ranges::count_if(torrents().torrents(), [](auto const& tor) { return !tor.is_paused(); }));
}

std::vector<tr_torrent*> Session::find_torrents(std::vector<tr_torrent_id_t> const& ids) const
{
    auto ret = std::vector<tr_torrent*>{};

    if (auto* const session = get_session()) {
        ret.reserve(std::size(ids));

        for (auto const& id : ids) {
            if (auto* const tor = tr_torrentFindFromId(session, id)) {
                ret.emplace_back(tor);
            }
        }
    }

    return ret;
}

tr_torrent* Session::find_torrent(tr_torrent_id_t id) const
{
    tr_torrent* tor = nullptr;

    if (auto* const session = get_session(); session != nullptr) {
        tor = tr_torrentFindFromId(session, id);
    }

    return tor;
}

FaviconCache<Glib::RefPtr<Gdk::Pixbuf>>& Session::favicon_cache() const
{
    return impl_->favicon_cache();
}

void Session::open_folder(tr_torrent_id_t torrent_id) const
{
    if (auto const* tor = find_torrent(torrent_id); tor != nullptr) {
        auto const current_dir = tr_torrentGetCurrentDir(tor);

        if (tr_torrentFileCount(tor) == 1) {
            auto const subpath = std::string_view{ tr_torrentFile(tor, 0).name };
            if (auto const slash = subpath.find('/'); slash != std::string_view::npos) {
                gtr_open_file(current_dir, subpath.substr(0, slash));
            } else {
                gtr_open_file(current_dir);
            }
        } else {
            gtr_open_file(current_dir, tr_torrentName(tor));
        }
    }
}

sigc::signal<void(tr_quark)>& Session::signal_prefs_changed()
{
    return impl_->signal_prefs_changed();
}

sigc::signal<void(TorrentList::Ids const&, Torrent::Fields const&)>& Session::signal_torrents_changed()
{
    return impl_->signal_torrents_changed();
}
