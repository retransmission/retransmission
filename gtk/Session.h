// This file Copyright © Retransmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#include "GtkCompat.h"
#include "Prefs.h"
#include "Torrent.h"

#include <libtransmission-app/favicon-cache.h>
#include <libtransmission-app/rpc-client.h>
#include <libtransmission-app/session.h>
#include <libtransmission-app/torrent-list.h>

#include <libtransmission/transmission.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include <gdkmm/pixbuf.h>
#include <gtkmm/treemodel.h>

#include <giomm/file.h>
#include <giomm/listmodel.h>

#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct tr_session;
struct tr_torrent;
struct tr_torrent_builder;

class Session
    : public Glib::Object
    , public tr::app::Session
{
public:
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;

    enum ErrorCode : uint16_t {
        ERR_ADD_TORRENT_ERR = 1,
        ERR_ADD_TORRENT_DUP = 2,
        ERR_NO_MORE_TORRENTS = 1000, /* finished adding a batch */
    };

    using Model = IF_GTKMM4(Gio::ListModel, Gtk::TreeModel);

public:
    ~Session() override;

    // A Session for the embedded session that `start_embedded()` creates in `config_dir`.
    [[nodiscard]] static Glib::RefPtr<Session> create(std::string config_dir, tr::app::Prefs& prefs, tr::app::RpcClient& rpc);

    [[nodiscard]] Glib::RefPtr<Gio::ListModel> get_model() const;
    [[nodiscard]] Glib::RefPtr<Model> get_sorted_model() const;

    // the embedded session, or nullptr before `start_embedded()` or after `stop()`
    [[nodiscard]] tr_session* get_session() const noexcept
    {
        return embedded_session();
    }

    [[nodiscard]] size_t get_unpaused_torrent_count() const;

    [[nodiscard]] size_t get_torrent_count() const;

    [[nodiscard]] tr_torrent* find_torrent(tr_torrent_id_t id) const;

    // TODO(c++20) std::span
    [[nodiscard]] std::vector<tr_torrent*> find_torrents(std::vector<tr_torrent_id_t> const& ids) const;

    [[nodiscard]] tr::app::FaviconCache<Glib::RefPtr<Gdk::Pixbuf>>& favicon_cache() const;

    /******
    *******
    ******/

    /**
     * Add a list of torrents.
     * This function assumes ownership of torrentFiles
     *
     * May pop up dialogs for each torrent if that preference is enabled.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt);

    /** @brief Add a torrent from a URL */
    bool add_from_url(Glib::ustring const& url);

    /** @brief Add a torrent. */
    void add_builder(std::unique_ptr<tr_torrent_builder> builder);

    // Fetches a torrent that changed outside the session's RPC, e.g. one added or
    // renamed through the C API, so the list shows it before the next poll.
    void fetch_torrent(tr_torrent_id_t id);

    /******
    *******
    ******/

    /**
     * Attempts to start a torrent immediately.
     */
    void start_now(tr_torrent_id_t id);

    // Sets a pref and saves the prefs file. The session learns of a session pref
    // from Prefs, through the tr::app::Session base.
    template<typename T>
    void set_pref(tr_quark const key, T const& val)
    {
        if (prefs_.set(key, val)) {
            save_prefs();
        }
    }

    // Copies the embedded session's settings into Prefs without echoing them back,
    // e.g. after the session changed one itself.
    void import_embedded_settings();

    // ---

    // Asks the session for its recently-used paths, `TR_KEY_recent_download_paths` or
    // `TR_KEY_recent_relocate_paths`, and hands them to `on_paths` on the GTK thread.
    // A slot bound to a widget or other sigc::trackable is skipped once that object is gone.
    // Hides the base's std::function overload on purpose: with both visible, a lambda would be ambiguous.
    void fetch_recent_paths(tr_quark key, sigc::slot<void(std::vector<Glib::ustring> const&)> on_paths);

    // Asks the session for its blocklist's rule count and last update time.
    // signal_blocklist_info() fires once blocklist_size() and blocklist_date() hold them.
    void refresh_blocklist_info();

    // Sends `method` to the session. `on_done`, if given, runs on the GTK thread with the reply.
    void exec(tr_quark method, tr_variant::Map params = {}, tr::app::RpcClient::ResponseFunc on_done = {});

    void open_folder(tr_torrent_id_t torrent_id) const;

    [[nodiscard]] sigc::signal<void(ErrorCode, Glib::ustring const&)>& signal_add_error();
    [[nodiscard]] sigc::signal<void(tr_torrent_builder*)>& signal_add_prompt();
    [[nodiscard]] sigc::signal<void()>& signal_blocklist_info();
    [[nodiscard]] sigc::signal<void(bool)>& signal_busy();
    [[nodiscard]] sigc::signal<void(tr_quark)>& signal_prefs_changed();
    // the torrents an update changed, by id, and the union of the fields it changed
    [[nodiscard]] sigc::signal<void(tr::app::TorrentList::Ids const&, Torrent::Fields const&)>& signal_torrents_changed();

protected:
    Session(std::string config_dir, tr::app::Prefs& prefs, tr::app::RpcClient& rpc);

private:
    tr::app::Prefs& prefs_;

    class Impl;
    std::unique_ptr<Impl> const impl_;
};
