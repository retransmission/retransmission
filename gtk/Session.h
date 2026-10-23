// This file Copyright © Retransmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#include "GtkCompat.h"
#include "Prefs.h"
#include "Torrent.h"

#include <libtransmission-app/favicon-cache.h>
#include <libtransmission-app/rpc-client.h>

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
#include <ctime>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

struct tr_session;
struct tr_torrent;
struct tr_torrent_builder;

class Session : public Glib::Object
{
public:
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;

    enum ErrorCode : uint16_t {
        ERR_ADD_TORRENT_ERR = 1,
        ERR_ADD_TORRENT_DUP = 2,
        ERR_NO_MORE_TORRENTS = 1000 /* finished adding a batch */
    };

    using Model = IF_GTKMM4(Gio::ListModel, Gtk::TreeModel);

public:
    ~Session() override;

    [[nodiscard]] static Glib::RefPtr<Session> create(tr_session* session, tr::app::Prefs& prefs, tr::app::RpcClient& rpc);

    [[nodiscard]] tr_session* close();

    [[nodiscard]] Glib::RefPtr<Gio::ListModel> get_model() const;
    [[nodiscard]] Glib::RefPtr<Model> get_sorted_model() const;

    void clear();

    [[nodiscard]] tr_session* get_session() const;

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
     * Load saved state and return number of torrents added.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void load(bool force_paused);

    /**
     * Add a list of torrents.
     * This function assumes ownership of torrentFiles
     *
     * May pop up dialogs for each torrent if that preference is enabled.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify);

    /** @brief Add a torrent from a URL */
    bool add_from_url(Glib::ustring const& url);

    /** @brief Add a torrent. */
    void add_builder(std::unique_ptr<tr_torrent_builder> builder);

    /** Add a torrent. */
    void add_torrent(Glib::RefPtr<Torrent> const& torrent, bool do_notify);

    /**
     * Notifies listeners that torrents have been added.
     * This should be called after one or more tr_core_add* () calls.
     */
    void torrents_added();

    void torrent_changed(tr_torrent_id_t id);

    /******
    *******
    ******/

    /* remove a torrent */
    void remove_torrent(tr_torrent_id_t id, bool delete_files);

    /* update the model with current torrent status */
    void update();

    /**
     * Attempts to start a torrent immediately.
     */
    void start_now(tr_torrent_id_t id);

    // Set a preference value, save the prefs file, and emit the "prefs-changed" signal
    template<typename T>
    void set_pref(tr_quark const key, T const& val)
    {
        if (prefs_.set(key, val)) {
            prefs_.save(tr_sessionGetConfigDir(get_session()), tr_sessionGetSettings(get_session()));
        }
    }

    // ---

    void port_test(tr_address_type ip_protocol);
    [[nodiscard]] bool port_test_pending(tr_address_type ip_protocol) const noexcept;

    // Recently-used directories, fetched from the session via a blocking RPC.
    [[nodiscard]] std::vector<Glib::ustring> get_recent_download_paths() const;
    [[nodiscard]] std::vector<Glib::ustring> get_recent_relocate_paths() const;

    void blocklist_update();

    // Ask the session for its blocklist's rule count and last update time;
    // signal_blocklist_info() delivers them.
    void refresh_blocklist_info();

    // Sends `method` to the session. `on_done`, if given, runs on the GTK thread with the reply.
    void exec(tr_quark method, tr_variant::Map params = {}, tr::app::RpcClient::ResponseFunc on_done = {});

    void open_folder(tr_torrent_id_t torrent_id) const;

    [[nodiscard]] sigc::signal<void(ErrorCode, Glib::ustring const&)>& signal_add_error();
    [[nodiscard]] sigc::signal<void(tr_torrent_builder*)>& signal_add_prompt();
    [[nodiscard]] sigc::signal<void(size_t, time_t)>& signal_blocklist_info();
    [[nodiscard]] sigc::signal<void(bool)>& signal_blocklist_updated();
    [[nodiscard]] sigc::signal<void(bool)>& signal_busy();
    [[nodiscard]] sigc::signal<void(tr_quark)>& signal_prefs_changed();
    [[nodiscard]] sigc::signal<void(std::optional<bool>, tr_address_type)>& signal_port_tested();
    [[nodiscard]] sigc::signal<void(std::unordered_set<tr_torrent_id_t> const&, Torrent::ChangeFlags)>&
    signal_torrents_changed();

protected:
    Session(tr_session* session, tr::app::Prefs& prefs, tr::app::RpcClient& rpc);

private:
    [[nodiscard]] std::vector<Glib::ustring> get_recent_dirs(tr_quark key) const;

    tr::app::Prefs& prefs_;

    class Impl;
    std::unique_ptr<Impl> const impl_;
};
