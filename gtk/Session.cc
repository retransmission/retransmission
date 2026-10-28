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

#include <libtransmission/converters.h>
#include <libtransmission/macros.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_time()
#include <libtransmission/variant.h>
#include <libtransmission/web-utils.h> // tr_urlIsValid()

#include <giomm/asyncresult.h>
#include <giomm/fileinfo.h>
#include <giomm/filemonitor.h>
#include <giomm/liststore.h>

#include <glibmm/error.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/stringutils.h>

#include <sigslot/signal.hpp>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/sortlistmodel.h>
#else
#include <gtkmm/treemodelsort.h>
#endif

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstring> // strstr
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace std::literals;
using tr::app::FaviconCache;
using tr::app::Prefs;
using tr::app::RpcClient;
using tr::app::RpcResponse;
using tr::app::SortMode;

class Session::Impl
{
public:
    Impl(Session& core, tr_session* session, RpcClient& rpc);
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    ~Impl();

    [[nodiscard]] tr_session* close();

    [[nodiscard]] Glib::RefPtr<Gio::ListStore<Torrent>> get_raw_model() const;
    [[nodiscard]] Glib::RefPtr<SortListModel<Torrent>> get_model();
    [[nodiscard]] tr_session* get_session() const;

    [[nodiscard]] std::pair<Glib::RefPtr<Torrent>, guint> find_torrent_by_id(tr_torrent_id_t torrent_id) const;

    [[nodiscard]] size_t get_unpaused_torrent_count() const;

    [[nodiscard]] bool get_port_test_pending(tr_address_type ip_protocol) const noexcept;
    void set_port_test_pending(bool pending, tr_address_type ip_protocol);

    void update();
    void update_torrent(tr_torrent_id_t id);
    void torrents_added();

    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify);
    void add_builder(std::unique_ptr<tr_torrent_builder> builder, bool do_prompt, bool do_notify);
    void add_torrent(Glib::RefPtr<Torrent> const& torrent, bool do_notify);
    [[nodiscard]] bool add_from_url(Glib::ustring const& url);

    void remove_torrent(tr_torrent_id_t id, bool delete_files);

    [[nodiscard]] constexpr auto& rpc() noexcept
    {
        return rpc_;
    }

    [[nodiscard]] auto& signal_add_error()
    {
        return signal_add_error_;
    }

    [[nodiscard]] auto& signal_add_prompt()
    {
        return signal_add_prompt_;
    }

    [[nodiscard]] auto& signal_blocklist_info()
    {
        return signal_blocklist_info_;
    }

    [[nodiscard]] auto& signal_blocklist_updated()
    {
        return signal_blocklist_updated_;
    }

    [[nodiscard]] auto& signal_busy()
    {
        return signal_busy_;
    }

    [[nodiscard]] auto& signal_prefs_changed()
    {
        return signal_prefs_changed_;
    }

    [[nodiscard]] auto& signal_port_tested()
    {
        return signal_port_tested_;
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

    [[nodiscard]] bool is_busy() const;
    void add_to_busy(int addMe);
    void inc_busy();
    void dec_busy();

    bool add(Glib::ustring const& name_in, bool do_start, bool do_prompt, bool do_notify);
    void add_file_async_callback(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::AsyncResult>& result,
        std::unique_ptr<tr_torrent_builder> builder,
        bool do_prompt,
        bool do_notify);

    [[nodiscard]] Glib::RefPtr<Torrent> create_new_torrent(tr_torrent_builder* builder);

    void watchdir_update();
    void watchdir_scan();
    void watchdir_monitor_file(Glib::RefPtr<Gio::File> const& file);
    bool watchdir_idle();
    void on_file_changed_in_watchdir(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::File> const& other_type,
        IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) event_type);

    void on_pref_changed(tr_quark key);

    void on_torrent_completeness_changed(tr_torrent_id_t tor_id, tr_completeness completeness, bool was_running);
    void on_torrent_metadata_changed(tr_torrent_id_t tor_id);

private:
    Session& core_;

    sigc::signal<void(ErrorCode, Glib::ustring const&)> signal_add_error_;
    sigc::signal<void(tr_torrent_builder*)> signal_add_prompt_;
    sigc::signal<void()> signal_blocklist_info_;
    sigc::signal<void(bool)> signal_blocklist_updated_;
    sigc::signal<void(bool)> signal_busy_;
    sigc::signal<void(tr_quark)> signal_prefs_changed_;
    sigc::signal<void(std::optional<bool>, tr_address_type)> signal_port_tested_;
    sigc::signal<void(std::unordered_set<tr_torrent_id_t> const&, Torrent::ChangeFlags)> signal_torrents_changed_;

    sigslot::scoped_connection prefs_changed_tag_;

    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection monitor_tag_;
    Glib::RefPtr<Gio::File> monitor_dir_;
    std::vector<Glib::RefPtr<Gio::File>> monitor_files_;
    sigc::connection monitor_idle_tag_;

    bool adding_from_watch_dir_ = false;
    std::array<bool, NUM_TR_AF_INET_TYPES> port_test_pending_ = {};

    gint busy_count_ = 0;
    Glib::RefPtr<Gio::ListStore<Torrent>> raw_model_;
    Glib::RefPtr<SortListModel<Torrent>> sorted_model_;
    Glib::RefPtr<TorrentSorter> sorter_ = TorrentSorter::create();
    tr_session* session_ = nullptr;
    RpcClient& rpc_;

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

tr_session* Session::get_session() const
{
    return impl_->get_session();
}

tr_session* Session::Impl::get_session() const
{
    return session_;
}

/***
****  BUSY
***/

bool Session::Impl::is_busy() const
{
    return busy_count_ > 0;
}

void Session::Impl::add_to_busy(int addMe)
{
    bool const wasBusy = is_busy();

    busy_count_ += addMe;

    if (wasBusy != is_busy()) {
        signal_busy_.emit(is_busy());
    }
}

void Session::Impl::inc_busy()
{
    add_to_busy(1);
}

void Session::Impl::dec_busy()
{
    add_to_busy(-1);
}

/***
****
****  WATCHDIR
****
***/

namespace
{

[[nodiscard]] time_t get_file_mtime(Glib::RefPtr<Gio::File> const& file)
{
    try {
        return static_cast<time_t>(
            file->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED)->get_attribute_uint64(G_FILE_ATTRIBUTE_TIME_MODIFIED));
    } catch (Glib::Error const&) {
        return 0;
    }
}

void rename_torrent(Glib::RefPtr<Gio::File> const& file)
{
    auto info = Glib::RefPtr<Gio::FileInfo>();

    try {
        info = file->query_info(G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME);
    } catch (Glib::Error const&) {
        return;
    }

    auto const old_name = info->get_attribute_as_string(G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME);
    auto const new_name = fmt::format("{}.added", old_name);

    try {
        file->set_display_name(new_name);
    } catch (Glib::Error const& e) {
        gtr_message(
            fmt::format(
                "Couldn't rename '{old_path}' as '{path}': {error} ({error_code})",
                fmt::arg("old_path", old_name),
                fmt::arg("path", new_name),
                fmt::arg("error", e.what()),
                fmt::arg("error_code", e.code())));
    }
}

} // namespace

bool Session::Impl::watchdir_idle()
{
    std::vector<Glib::RefPtr<Gio::File>> changing;
    std::vector<Glib::RefPtr<Gio::File>> unchanging;
    time_t const now = tr_time();

    /* separate the files into two lists: changing and unchanging */
    for (auto const& file : monitor_files_) {
        time_t const mtime = get_file_mtime(file);

        if (mtime + 2 >= now) {
            changing.push_back(file);
        } else {
            unchanging.push_back(file);
        }
    }

    /* add the files that have stopped changing */
    if (!unchanging.empty()) {
        bool const do_start = core_.prefs_.get<bool>(TR_KEY_start_added_torrents);
        bool const do_prompt = core_.prefs_.get<bool>(TR_KEY_show_options_window);

        adding_from_watch_dir_ = true;
        add_files(unchanging, do_start, do_prompt, true);
        std::ranges::for_each(unchanging, rename_torrent);
        adding_from_watch_dir_ = false;
    }

    /* keep monitoring the ones that are still changing */
    monitor_files_ = std::move(changing);

    /* if monitor_files is nonempty, keep checking every second */
    if (!monitor_files_.empty()) {
        return true;
    }

    monitor_idle_tag_.disconnect();
    return false;
}

/* If this file is a torrent, add it to our list */
void Session::Impl::watchdir_monitor_file(Glib::RefPtr<Gio::File> const& file)
{
    auto const filename = file->get_path();
    bool const is_torrent = Glib::str_has_suffix(filename, ".torrent");

    if (is_torrent) {
        /* if we're not already watching this file, start watching it now */
        bool const found = std::ranges::any_of(monitor_files_, [file](auto const& f) { return file->equal(f); });

        if (!found) {
            monitor_files_.push_back(file);

            if (!monitor_idle_tag_.connected()) {
                monitor_idle_tag_ = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &Impl::watchdir_idle), 1);
            }
        }
    }
}

/* GFileMonitor noticed a file was created */
void Session::Impl::on_file_changed_in_watchdir(
    Glib::RefPtr<Gio::File> const& file,
    Glib::RefPtr<Gio::File> const& /*other_type*/,
    IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) event_type)
{
    if (event_type == TR_GIO_FILE_MONITOR_EVENT(CREATED)) {
        watchdir_monitor_file(file);
    }
}

/* walk through the pre-existing files in the watchdir */
void Session::Impl::watchdir_scan()
{
    auto const dirname = core_.prefs_.get<std::string>(TR_KEY_watch_dir);

    try {
        for (auto const& name : Glib::Dir(dirname)) {
            watchdir_monitor_file(Gio::File::create_for_path(Glib::build_filename(dirname, name)));
        }
    } catch (Glib::FileError const& e) {
        gtr_warning(
            fmt::format(
                "Couldn't open watchdir '{dirname}': {error} ({error_code})",
                fmt::arg("dirname", dirname),
                fmt::arg("error", e.what()),
                fmt::arg("error_code", static_cast<int>(e.code()))));
    }
}

void Session::Impl::watchdir_update()
{
    bool const is_enabled = core_.prefs_.get<bool>(TR_KEY_watch_dir_enabled);
    auto const dir = Gio::File::create_for_path(core_.prefs_.get<std::string>(TR_KEY_watch_dir));

    if (monitor_ != nullptr && (!is_enabled || !dir->equal(monitor_dir_))) {
        monitor_tag_.disconnect();
        monitor_->cancel();

        monitor_dir_.reset();
        monitor_.reset();
    }

    if (!is_enabled || monitor_ != nullptr) {
        return;
    }

    auto monitor = Glib::RefPtr<Gio::FileMonitor>();

    try {
        monitor = dir->monitor_directory();
    } catch (Glib::Error const&) {
        return;
    }

    watchdir_scan();

    monitor_ = std::move(monitor);
    monitor_dir_ = dir;
    monitor_tag_ = monitor_->signal_changed().connect(sigc::mem_fun(*this, &Impl::on_file_changed_in_watchdir));
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

    case TR_KEY_watch_dir:
    case TR_KEY_watch_dir_enabled:
        watchdir_update();
        break;

    default:
        break;
    }
}

/**
***
**/

Glib::RefPtr<Session> Session::create(tr_session* const session, Prefs& prefs, RpcClient& rpc)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new Session(session, prefs, rpc));
}

Session::Session(tr_session* const session, Prefs& prefs, RpcClient& rpc)
    : Glib::ObjectBase(typeid(Session))
    , tr::app::Session(prefs, rpc)
    , prefs_(prefs)
    , config_dir_(tr_sessionGetConfigDir(session))
    , impl_(std::make_unique<Impl>(*this, session, rpc))
{
    set_embedded_session(session);
    set_session_type(Type::Embedded);
}

Session::~Session() = default;

Session::Impl::Impl(Session& core, tr_session* const session, RpcClient& rpc)
    : core_{ core }
    , session_{ session }
    , rpc_{ rpc }
{
    // Before anything can send: exec() drops requests made before start().
    rpc_.start(session);

    raw_model_ = Gio::ListStore<Torrent>::create();
    signal_torrents_changed_.connect(sigc::hide<0>(sigc::mem_fun(*sorter_, &TorrentSorter::update)));
    sorted_model_ = SortListModel<Torrent>::create(gtr_ptr_static_cast<Gio::ListModel>(raw_model_), sorter_);

    /* init from prefs & listen to pref changes */
    on_pref_changed(TR_KEY_sort_mode);
    on_pref_changed(TR_KEY_sort_reversed);
    on_pref_changed(TR_KEY_watch_dir_enabled);
    signal_prefs_changed_.connect([this](auto key) { on_pref_changed(key); });
    prefs_changed_tag_ = core_.prefs_.observe_changes([this](tr_quark const key) { signal_prefs_changed_.emit(key); });

    tr_sessionSetMetadataCallback(session, [this](tr_torrent_id_t const tor_id) { on_torrent_metadata_changed(tor_id); });

    tr_sessionSetCompletenessCallback(
        session,
        [this](tr_torrent_id_t const tor_id, tr_completeness const completeness, bool const was_running) {
            on_torrent_completeness_changed(tor_id, completeness, was_running);
        });

    // The scheduler turns alt speeds on and off without a settings-changed notice,
    // so read the new value back here. Runs on the session thread.
    tr_sessionSetAltSpeedFunc(session, [this](bool const /*is_enabled*/, bool const /*by_user*/) {
        gtr_run_on_ui_thread([core = get_core_ptr()] { core->import_embedded_settings(); });
    });
}

Session::Impl::~Impl()
{
    monitor_idle_tag_.disconnect();
}

tr_session* Session::close()
{
    auto* const session = impl_->close();

    if (session != nullptr) {
        save_prefs();
        set_embedded_session(nullptr);
    }

    return session;
}

tr_session* Session::Impl::close()
{
    auto* session = session_;

    if (session != nullptr) {
        session_ = nullptr;
        rpc_.stop();
    }

    return session;
}

void Session::import_embedded_settings()
{
    if (auto const settings = embedded_settings(); settings) {
        import_session_settings(*settings);
    }
}

void Session::save_prefs() const
{
    prefs_.save(config_dir_, embedded_settings());
}

/***
****  COMPLETENESS CALLBACK
***/

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before calling notify's dbus code... */
void Session::Impl::on_torrent_completeness_changed(
    tr_torrent_id_t const tor_id,
    tr_completeness const completeness,
    bool const was_running)
{
    if (auto* const tor = tr_torrentFindFromId(session_, tor_id);
        was_running && completeness != TR_LEECH && tor != nullptr && tr_torrentStat(tor).size_when_done != 0U) {
        Glib::signal_idle().connect([core = get_core_ptr(), &prefs = core_.prefs_, tor_id] {
            gtr_notify_torrent_completed(core, prefs, tor_id);
            return false;
        });
    }
}

/***
****  METADATA CALLBACK
***/

namespace
{

struct metadata_callback_data {
    Session* core;
    tr_torrent_id_t torrent_id;
};

} // namespace

std::pair<Glib::RefPtr<Torrent>, guint> Session::Impl::find_torrent_by_id(tr_torrent_id_t torrent_id) const
{
    auto begin_position = 0U;
    auto end_position = raw_model_->get_n_items();

    while (begin_position < end_position) {
        auto const position = begin_position + ((end_position - begin_position) / 2);
        auto const torrent = raw_model_->get_item(position);
        auto const current_torrent_id = torrent->get_id();

        if (current_torrent_id == torrent_id) {
            return { torrent, position };
        }

        if (current_torrent_id < torrent_id) {
            begin_position = position + 1;
        } else {
            end_position = position;
        }
    }

    return {};
}

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before changing our list store... */
void Session::Impl::on_torrent_metadata_changed(tr_torrent_id_t const tor_id)
{
    Glib::signal_idle().connect([this, core = get_core_ptr(), tor_id] {
        update_torrent(tor_id);
        return false;
    });
}

/***
****
****  ADDING TORRENTS
****
***/

void Session::add_torrent(Glib::RefPtr<Torrent> const& torrent, bool do_notify)
{
    impl_->add_torrent(torrent, do_notify);
}

void Session::Impl::add_torrent(Glib::RefPtr<Torrent> const& torrent, bool do_notify)
{
    if (torrent != nullptr) {
        raw_model_->insert_sorted(torrent, &Torrent::compare_by_id);

        if (do_notify) {
            gtr_notify_torrent_added(get_core_ptr(), core_.prefs_, torrent->get_id());
        }
    }
}

Glib::RefPtr<Torrent> Session::Impl::create_new_torrent(tr_torrent_builder* builder)
{
    // let the gtk client handle the removal, since libT
    // doesn't have any concept of the glib trash API.
    // Prefs, not tr_session: the client's copy of the setting, which exists for every kind of session.
    auto const do_trash = core_.prefs_.get<bool>(TR_KEY_trash_original_torrent_files);
    tr_torrent* const tor = tr_torrentNew(builder, nullptr);

    if (tor != nullptr && do_trash) {
        if (auto const& source = builder->source_filename(); !std::empty(source)) {
            // #1294: don't delete the .torrent file if it's our internal copy
            std::string const config_dir = tr_sessionGetConfigDir(session_);
            bool const is_internal = source.starts_with(config_dir);
            if (!is_internal) {
                gtr_file_trash_or_remove(source);
            }
        }
    }

    return Torrent::create(tor);
}

void Session::Impl::add_builder(std::unique_ptr<tr_torrent_builder> builder, bool const do_prompt, bool const do_notify)
{
    auto const& metainfo = builder->metainfo();
    if (std::empty(metainfo.info_hash_string())) {
        return;
    }

    if (tr_torrentFindFromMetainfo(get_session(), &metainfo) != nullptr) {
        /* don't complain about torrent files in the watch directory
         * that have already been added... that gets annoying and we
         * don't want to be nagging users to clean up their watch dirs */
        if (std::empty(builder->source_filename()) || !adding_from_watch_dir_) {
            signal_add_error_.emit(ERR_ADD_TORRENT_DUP, metainfo.name().c_str());
        }

        return;
    }

    if (!do_prompt) {
        add_torrent(create_new_torrent(builder.get()), do_notify);
        return;
    }

    // the receiver wraps the pointer back up; see Application::Impl::on_add_torrent()
    signal_add_prompt_.emit(builder.release());
}

namespace
{

void core_apply_defaults(tr_torrent_builder* const builder, Prefs const& prefs)
{
    if (!builder->paused()) {
        builder->set_paused(!prefs.get<bool>(TR_KEY_start_added_torrents));
    }

    if (!builder->peer_limit()) {
        builder->set_peer_limit(prefs.get<size_t>(TR_KEY_peer_limit_per_torrent));
    }

    if (std::empty(builder->download_dir())) {
        builder->set_download_dir(prefs.get<std::string>(TR_KEY_download_dir));
    }
}

} // namespace

void Session::add_builder(std::unique_ptr<tr_torrent_builder> builder)
{
    bool const do_notify = false;
    bool const do_prompt = prefs_.get<bool>(TR_KEY_show_options_window);
    core_apply_defaults(builder.get(), prefs_);
    impl_->add_builder(std::move(builder), do_prompt, do_notify);

    // This is a batch of one, so end it here.
    // Otherwise what add_builder() reported, usually a duplicate, never reaches the user.
    torrents_added();
}

/***
****
***/

void Session::Impl::add_file_async_callback(
    Glib::RefPtr<Gio::File> const& file,
    Glib::RefPtr<Gio::AsyncResult>& result,
    std::unique_ptr<tr_torrent_builder> builder,
    bool const do_prompt,
    bool const do_notify)
{
    try {
        gsize length = 0;
        char* contents = nullptr;

        if (!file->load_contents_finish(result, contents, length)) {
            gtr_message(fmt::format("Couldn't read '{path}'", fmt::arg("path", file->get_parse_name())));
        } else if (builder->set_metainfo(contents != nullptr ? std::string_view{ contents, length } : std::string_view{})) {
            add_builder(std::move(builder), do_prompt, do_notify);
        }
    } catch (Glib::Error const& e) {
        gtr_message(
            fmt::format(
                "Couldn't read '{path}': {error} ({error_code})",
                fmt::arg("path", file->get_parse_name()),
                fmt::arg("error", e.what()),
                fmt::arg("error_code", e.code())));
    }

    // add_from_url() returned, and ended its batch, long before this download finished.
    // This late arrival needs a batch end of its own.
    torrents_added();

    dec_busy();
}

// Add `name,` which might be a local filename, a magnet link, or a URI.
bool Session::Impl::add(Glib::ustring const& name_in, bool const do_start, bool const do_prompt, bool const do_notify)
{
    auto name = name_in;

    // `gio::File` doesn't seem to know how to stringify magnet links correctly.
    // Unfortunately there are some code paths that unavoidably use `gio::File`
    // e.g. Gtk::Application::on_open() so we have to do this:
    if (auto constexpr BrokenMagnetLinkPrefix = "magnet:///?"sv; name.raw().starts_with(BrokenMagnetLinkPrefix)) {
        name.replace(0, std::size(BrokenMagnetLinkPrefix), "magnet:?");
    }

    auto* const session = get_session();
    if (session == nullptr) {
        return false;
    }

    bool handled = false;
    auto builder = std::make_unique<tr_torrent_builder>(session);
    core_apply_defaults(builder.get(), core_.prefs_);
    builder->set_paused(!do_start);

    bool loaded = false;
    auto file = Gio::File::create_for_parse_name(name);
    if (auto const path = file->get_path(); !std::empty(path)) {
        // try to treat it as a file...
        loaded = builder->set_metainfo_from_file(path);
    }

    if (!loaded) {
        // try to treat it as a magnet link...
        loaded = builder->set_metainfo_from_magnet_link(name.raw());
    }

    // if we could make sense of it, add it
    if (loaded) {
        handled = true;
        add_builder(std::move(builder), do_prompt, do_notify);
    } else if (tr_urlIsValid(file->get_uri())) {
        handled = true;
        inc_busy();
        // released because the slot must be copyable; the callback re-owns it
        file->load_contents_async([this, file, builder = builder.release(), do_prompt, do_notify](auto& result) {
            add_file_async_callback(file, result, std::unique_ptr<tr_torrent_builder>{ builder }, do_prompt, do_notify);
        });
    } else {
        std::cerr << fmt::format("Couldn't add torrent file '{path}'", fmt::arg("path", file->get_parse_name())) << '\n';
    }

    return handled;
}

bool Session::add_from_url(Glib::ustring const& url)
{
    return impl_->add_from_url(url);
}

bool Session::Impl::add_from_url(Glib::ustring const& url)
{
    auto const do_start = core_.prefs_.get<bool>(TR_KEY_start_added_torrents);
    auto const do_prompt = core_.prefs_.get<bool>(TR_KEY_show_options_window);
    auto const do_notify = false;

    auto const handled = add(url, do_start, do_prompt, do_notify);
    torrents_added();
    return handled;
}

void Session::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify)
{
    impl_->add_files(files, do_start, do_prompt, do_notify);
}

void Session::Impl::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify)
{
    for (auto const& file : files) {
        add(file->get_parse_name(), do_start, do_prompt, do_notify);
    }

    torrents_added();
}

void Session::torrents_added()
{
    impl_->torrents_added();
}

void Session::Impl::torrents_added()
{
    update();
    signal_add_error_.emit(ERR_NO_MORE_TORRENTS, {});
}

void Session::torrent_changed(tr_torrent_id_t const id)
{
    impl_->update_torrent(id);
}

void Session::remove_torrent(tr_torrent_id_t id, bool delete_files)
{
    impl_->remove_torrent(id, delete_files);
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Session::Impl::remove_torrent(tr_torrent_id_t const id, bool const delete_files)
{
    if (auto const& [torrent, position] = find_torrent_by_id(id); torrent) {
        get_raw_model()->remove(position);

        tr_torrentRemove(&torrent->get_underlying(), delete_files);
    }
}

void Session::load(bool force_paused)
{
    auto builder = tr_torrent_builder{ impl_->get_session() };

    if (force_paused) {
        builder.set_paused(true);
    }

    auto* session = impl_->get_session();
    tr_sessionLoadTorrents(session, &builder);

    auto const raw_torrents = tr_sessionGetAllTorrents(session);

    auto torrents = std::vector<Glib::RefPtr<Torrent>>();
    torrents.reserve(raw_torrents.size());
    std::ranges::transform(raw_torrents, std::back_inserter(torrents), &Torrent::create);
    std::ranges::sort(torrents, &Torrent::less_by_id);

    auto const model = impl_->get_raw_model();
    model->splice(0, model->get_n_items(), torrents);
}

void Session::clear()
{
    impl_->get_raw_model()->remove_all();
}

/***
****
***/

void Session::update()
{
    // Feeds the sleep inhibitor until the session's stats are polled over RPC.
    if (auto* const session = get_session(); session != nullptr) {
        set_busy(tr_sessionIsBusy(session));
    }

    impl_->update();
}

void Session::start_now(tr_torrent_id_t const id)
{
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_ids, tr::serializer::to_variant(std::array{ id }));
    exec(TR_KEY_torrent_start_now, std::move(params));
}

void Session::Impl::update()
{
    auto torrent_ids = std::unordered_set<tr_torrent_id_t>();
    auto changes = Torrent::ChangeFlags();

    /* update the model */
    for (auto i = 0U, count = raw_model_->get_n_items(); i < count; ++i) {
        auto const torrent = raw_model_->get_item(i);
        if (auto const torrent_changes = torrent->update(); torrent_changes.any()) {
            torrent_ids.insert(torrent->get_id());
            changes |= torrent_changes;
        }
    }

    if (changes.any()) {
        signal_torrents_changed_.emit(torrent_ids, changes);
    }
}

void Session::Impl::update_torrent(tr_torrent_id_t const id)
{
    if (auto const& [torrent, position] = find_torrent_by_id(id); torrent) {
        if (auto const changes = torrent->update(); changes.any()) {
            signal_torrents_changed_.emit({ id }, changes);
        }
    }
}

/***
****
****  RPC Interface
****
***/

namespace
{

[[nodiscard]] tr_variant::Map const* get_result_map(RpcResponse const& response) noexcept
{
    return response.args ? response.args->get_if<tr_variant::Map>() : nullptr;
}

// params for a `session_get` that asks only for `keys`
[[nodiscard]] tr_variant::Map make_session_get_params(std::initializer_list<tr_quark> const keys)
{
    auto fields = tr_variant::Vector{};
    fields.reserve(std::size(keys));
    for (auto const key : keys) {
        fields.emplace_back(tr_variant::unmanaged_string(key));
    }

    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_fields, std::move(fields));
    return params;
}

} // namespace

void Session::exec(tr_quark const method, tr_variant::Map params, RpcClient::ResponseFunc on_done)
{
    impl_->rpc().exec(method, std::move(params), std::move(on_done));
}

/***
****  Sending a test-port request via RPC
***/

void Session::port_test(tr_address_type const ip_protocol)
{
    static auto constexpr IpStr = std::array{ "ipv4"sv, "ipv6"sv };

    if (port_test_pending(ip_protocol)) {
        return;
    }
    impl_->set_port_test_pending(true, ip_protocol);

    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_ip_protocol, tr_variant::unmanaged_string(IpStr[ip_protocol]));

    exec(TR_KEY_port_test, std::move(params), [this, ip_protocol](RpcResponse const& response) {
        impl_->set_port_test_pending(false, ip_protocol);

        // An empty status, e.g. after an error reply, shows as the "error" state in the UI
        auto is_open = std::optional<bool>{};
        if (auto const* const result = get_result_map(response); result != nullptr) {
            is_open = result->value_if<bool>(TR_KEY_port_is_open);
        }

        impl_->signal_port_tested().emit(is_open, ip_protocol);
    });
}

bool Session::port_test_pending(tr_address_type const ip_protocol) const noexcept
{
    return impl_->get_port_test_pending(ip_protocol);
}

bool Session::Impl::get_port_test_pending(tr_address_type const ip_protocol) const noexcept
{
    return port_test_pending_[ip_protocol];
}

void Session::Impl::set_port_test_pending(bool const pending, tr_address_type const ip_protocol)
{
    port_test_pending_[ip_protocol] = pending;
}

/***
****  Updating a blocklist via RPC
***/

void Session::blocklist_update()
{
    exec(TR_KEY_blocklist_update, {}, [this](RpcResponse const& response) {
        // success iff the reply reports the new rule count
        auto n_rules = std::optional<int64_t>{};
        if (auto const* const result = get_result_map(response); result != nullptr) {
            n_rules = result->value_if<int64_t>(TR_KEY_blocklist_size);
        }

        impl_->signal_blocklist_updated().emit(n_rules.has_value());
    });
}

void Session::refresh_blocklist_info()
{
    // session_get reads the blocklist under the session lock,
    // which tr_blocklistGetRuleCount() does not take.
    exec(
        TR_KEY_session_get,
        make_session_get_params({ TR_KEY_blocklist_date, TR_KEY_blocklist_size }),
        [this](RpcResponse const& response) {
            if (auto const* const result = get_result_map(response); result != nullptr) {
                import_session_settings(*result);
                impl_->signal_blocklist_info().emit();
            }
        });
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
    return impl_->get_raw_model()->get_n_items();
}

size_t Session::get_unpaused_torrent_count() const
{
    return impl_->get_unpaused_torrent_count();
}

size_t Session::Impl::get_unpaused_torrent_count() const
{
    size_t activeCount = 0;

    for (auto i = 0U, count = raw_model_->get_n_items(); i < count; ++i) {
        if (raw_model_->get_item(i)->get_activity() != TR_STATUS_STOPPED) {
            ++activeCount;
        }
    }

    return activeCount;
}

std::vector<tr_torrent*> Session::find_torrents(std::vector<tr_torrent_id_t> const& ids) const
{
    auto ret = std::vector<tr_torrent*>{};

    if (auto* const session = impl_->get_session()) {
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

    if (auto* const session = impl_->get_session(); session != nullptr) {
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

sigc::signal<void(Session::ErrorCode, Glib::ustring const&)>& Session::signal_add_error()
{
    return impl_->signal_add_error();
}

sigc::signal<void(tr_torrent_builder*)>& Session::signal_add_prompt()
{
    return impl_->signal_add_prompt();
}

sigc::signal<void()>& Session::signal_blocklist_info()
{
    return impl_->signal_blocklist_info();
}

sigc::signal<void(bool)>& Session::signal_blocklist_updated()
{
    return impl_->signal_blocklist_updated();
}

sigc::signal<void(bool)>& Session::signal_busy()
{
    return impl_->signal_busy();
}

sigc::signal<void(tr_quark)>& Session::signal_prefs_changed()
{
    return impl_->signal_prefs_changed();
}

sigc::signal<void(std::optional<bool>, tr_address_type)>& Session::signal_port_tested()
{
    return impl_->signal_port_tested();
}

sigc::signal<void(std::unordered_set<tr_torrent_id_t> const&, Torrent::ChangeFlags)>& Session::signal_torrents_changed()
{
    return impl_->signal_torrents_changed();
}
