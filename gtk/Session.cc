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
#include <string>
#include <string_view>
#include <utility>

using namespace std::literals;
using tr::app::FaviconCache;
using tr::app::Prefs;
using tr::app::RpcClient;
using tr::app::RpcResponse;
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
    ~Impl();

    [[nodiscard]] Glib::RefPtr<Gio::ListStore<Torrent>> get_raw_model() const;
    [[nodiscard]] Glib::RefPtr<SortListModel<Torrent>> get_model();

    void torrents_added();

    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt);
    void add_builder(std::unique_ptr<tr_torrent_builder> builder, bool do_prompt);
    [[nodiscard]] bool add_from_url(Glib::ustring const& url);

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

    [[nodiscard]] auto& signal_busy()
    {
        return signal_busy_;
    }

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

    [[nodiscard]] bool is_busy() const;
    void add_to_busy(int addMe);
    void inc_busy();
    void dec_busy();

    bool add(Glib::ustring const& name_in, bool do_start, bool do_prompt);
    void add_file_async_callback(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::AsyncResult>& result,
        std::unique_ptr<tr_torrent_builder> builder,
        bool do_prompt);

    [[nodiscard]] tr_torrent* create_new_torrent(tr_torrent_builder* builder);

    void watchdir_update();
    void watchdir_scan();
    void watchdir_monitor_file(Glib::RefPtr<Gio::File> const& file);
    bool watchdir_idle();
    void on_file_changed_in_watchdir(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::File> const& other_type,
        IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) event_type);

    void on_pref_changed(tr_quark key);

    // keep raw_model_ a mirror of the list's rows
    void on_rows_inserted(size_t row, size_t count);
    void on_rows_removing(size_t row, size_t count);
    void on_torrents_changed(TorrentList::Changes const& changes);

private:
    Session& core_;

    sigc::signal<void(ErrorCode, Glib::ustring const&)> signal_add_error_;
    sigc::signal<void(tr_torrent_builder*)> signal_add_prompt_;
    sigc::signal<void()> signal_blocklist_info_;
    sigc::signal<void(bool)> signal_busy_;
    sigc::signal<void(tr_quark)> signal_prefs_changed_;
    sigc::signal<void(TorrentList::Ids const&, Torrent::Fields const&)> signal_torrents_changed_;

    sigslot::scoped_connection prefs_changed_tag_;
    std::array<sigslot::scoped_connection, 3U> torrent_list_tags_;

    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection monitor_tag_;
    Glib::RefPtr<Gio::File> monitor_dir_;
    std::vector<Glib::RefPtr<Gio::File>> monitor_files_;
    sigc::connection monitor_idle_tag_;

    bool adding_from_watch_dir_ = false;

    gint busy_count_ = 0;
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
        add_files(unchanging, do_start, do_prompt);
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
    on_pref_changed(TR_KEY_watch_dir_enabled);
    signal_prefs_changed_.connect([this](auto key) { on_pref_changed(key); });
    prefs_changed_tag_ = core_.prefs_.observe_changes([this](tr_quark const key) { signal_prefs_changed_.emit(key); });
}

Session::Impl::~Impl()
{
    monitor_idle_tag_.disconnect();
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

/***
****
****  ADDING TORRENTS
****
***/

void Session::fetch_torrent(tr_torrent_id_t const id)
{
    refresh_torrents({ id }, tr::app::TorrentKeySet::MainAll);
}

tr_torrent* Session::Impl::create_new_torrent(tr_torrent_builder* builder)
{
    // let the gtk client handle the removal, since libT
    // doesn't have any concept of the glib trash API.
    // Prefs, not tr_session: the client's copy of the setting, which exists for every kind of session.
    auto const do_trash = core_.prefs_.get<bool>(TR_KEY_trash_original_torrent_files);
    tr_torrent* const tor = tr_torrentNew(builder, nullptr);

    if (tor != nullptr && do_trash) {
        if (auto const& source = builder->source_filename(); !std::empty(source)) {
            // #1294: don't delete the .torrent file if it's our internal copy
            bool const is_internal = source.starts_with(core_.config_dir());
            if (!is_internal) {
                gtr_file_trash_or_remove(source);
            }
        }
    }

    return tor;
}

void Session::Impl::add_builder(std::unique_ptr<tr_torrent_builder> builder, bool const do_prompt)
{
    auto const& metainfo = builder->metainfo();
    if (std::empty(metainfo.info_hash_string())) {
        return;
    }

    if (tr_torrentFindFromMetainfo(core_.get_session(), &metainfo) != nullptr) {
        /* don't complain about torrent files in the watch directory
         * that have already been added... that gets annoying and we
         * don't want to be nagging users to clean up their watch dirs */
        if (std::empty(builder->source_filename()) || !adding_from_watch_dir_) {
            signal_add_error_.emit(ERR_ADD_TORRENT_DUP, metainfo.name().c_str());
        }

        return;
    }

    if (!do_prompt) {
        if (auto const* const tor = create_new_torrent(builder.get()); tor != nullptr) {
            core_.fetch_torrent(tr_torrentId(tor));
        }
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
    bool const do_prompt = prefs_.get<bool>(TR_KEY_show_options_window);
    core_apply_defaults(builder.get(), prefs_);
    impl_->add_builder(std::move(builder), do_prompt);

    // This is a batch of one, so end it here.
    // Otherwise what add_builder() reported, usually a duplicate, never reaches the user.
    impl_->torrents_added();
}

/***
****
***/

void Session::Impl::add_file_async_callback(
    Glib::RefPtr<Gio::File> const& file,
    Glib::RefPtr<Gio::AsyncResult>& result,
    std::unique_ptr<tr_torrent_builder> builder,
    bool const do_prompt)
{
    try {
        gsize length = 0;
        char* contents = nullptr;

        if (!file->load_contents_finish(result, contents, length)) {
            gtr_message(fmt::format("Couldn't read '{path}'", fmt::arg("path", file->get_parse_name())));
        } else if (builder->set_metainfo(contents != nullptr ? std::string_view{ contents, length } : std::string_view{})) {
            add_builder(std::move(builder), do_prompt);
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
bool Session::Impl::add(Glib::ustring const& name_in, bool const do_start, bool const do_prompt)
{
    auto name = name_in;

    // `gio::File` doesn't seem to know how to stringify magnet links correctly.
    // Unfortunately there are some code paths that unavoidably use `gio::File`
    // e.g. Gtk::Application::on_open() so we have to do this:
    if (auto constexpr BrokenMagnetLinkPrefix = "magnet:///?"sv; name.raw().starts_with(BrokenMagnetLinkPrefix)) {
        name.replace(0, std::size(BrokenMagnetLinkPrefix), "magnet:?");
    }

    auto* const session = core_.get_session();
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
        add_builder(std::move(builder), do_prompt);
    } else if (tr_urlIsValid(file->get_uri())) {
        handled = true;
        inc_busy();
        // released because the slot must be copyable; the callback re-owns it
        file->load_contents_async([this, file, builder = builder.release(), do_prompt](auto& result) {
            add_file_async_callback(file, result, std::unique_ptr<tr_torrent_builder>{ builder }, do_prompt);
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

    auto const handled = add(url, do_start, do_prompt);
    torrents_added();
    return handled;
}

void Session::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt)
{
    impl_->add_files(files, do_start, do_prompt);
}

void Session::Impl::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt)
{
    for (auto const& file : files) {
        add(file->get_parse_name(), do_start, do_prompt);
    }

    torrents_added();
}

void Session::Impl::torrents_added()
{
    signal_add_error_.emit(ERR_NO_MORE_TORRENTS, {});
}

void Session::start_now(tr_torrent_id_t const id)
{
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_ids, tr::serializer::to_variant(std::array{ id }));
    send(TR_KEY_torrent_start_now, std::move(params));
}

/***
****  Reading the blocklist's info via RPC
***/

void Session::refresh_blocklist_info()
{
    // session_get reads the blocklist under the session lock,
    // which tr_blocklistGetRuleCount() does not take.
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_fields, tr::app::make_fields({ TR_KEY_blocklist_date, TR_KEY_blocklist_size }));
    send(TR_KEY_session_get, std::move(params), [this](RpcResponse const& response) {
        if (auto const* const args = response.args_map()) {
            import_session_settings(*args);
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

sigc::signal<void(bool)>& Session::signal_busy()
{
    return impl_->signal_busy();
}

sigc::signal<void(tr_quark)>& Session::signal_prefs_changed()
{
    return impl_->signal_prefs_changed();
}

sigc::signal<void(TorrentList::Ids const&, Torrent::Fields const&)>& Session::signal_torrents_changed()
{
    return impl_->signal_torrents_changed();
}
