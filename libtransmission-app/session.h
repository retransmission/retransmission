// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator> // std::data(), std::size()
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant> // std::monostate
#include <vector>

#include <sigslot/signal.hpp>

#include <woke/woke.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/types.h" // tr_address_type, tr_session_stats
#include "libtransmission/variant.h"

#include "libtransmission-app/add-data.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/torrent-keys.h"
#include "libtransmission-app/torrent-list.h"

struct tr_session;

namespace tr::app
{

class Prefs;

// a `torrent_get` or `session_get` `fields` list: `keys` as unmanaged strings
[[nodiscard]] tr_variant::Vector make_fields(std::span<tr_quark const> keys);

[[nodiscard]] inline tr_variant::Vector make_fields(std::initializer_list<tr_quark> const keys)
{
    return make_fields(std::span{ std::data(keys), std::size(keys) });
}

// How long a client that has left the screen lets its shutdown run. Long enough for a slow
// close, and shorter than a waiting launch's patience, so that launch still gets to start.
inline constexpr auto ShutdownGracePeriod = std::chrono::seconds{ 60 };

// Ends the process after `delay`, whatever it's doing then. `tr_sessionClose()` limits its
// wait on trackers but not on a stuck disk, and a user with no window has no way to quit.
void exit_process_after(std::chrono::milliseconds delay = ShutdownGracePeriod);

class Session
{
public:
    // How the app reaches its `tr_session`:
    enum class Type : uint8_t {
        Embedded, // the session runs inside this process
        Local, // a separate daemon process on this machine
        Remote, // a daemon on another machine
    };

    // ascending unique torrent ids; a method that takes them sorts what it is given
    using Ids = TorrentList::Ids;

    // `start()` creates an embedded session in `config_dir`, which disposes of removed
    // torrents' files with `remove_func`. A subclass that creates its own `tr_session`,
    // e.g. a test's, reports it with `set_embedded_session()` instead of calling `start()`,
    // and closes it itself: `stop()` only forgets it.
    Session(std::string config_dir, Prefs& prefs, RpcClient& rpc, tr_torrent_remove_func remove_func = {});

    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;

    // Blocks until an embedded session has finished closing.
    virtual ~Session();

    // --- lifecycle

    // Connects to the session that prefs name: `remote_session_*`, or else an embedded one.
    // Fires `source_changed`, or `config_dir_contended` if the embedded session can't have
    // its config dir. An embedded session started during a close waits until `closed` has
    // fired, since the closing session holds that dir until then.
    // A started Session must `stop()` before it starts again.
    void start();

    // Starts an embedded session whatever prefs name, as `start()` would, for a client
    // that can't drive a remote session. Its saved torrents load paused when `paused`.
    void start_embedded(bool paused = false);

    // Disconnects from the session, forgets its info and torrents, and fires `stopped`.
    // `tr_sessionClose()` can wait on trackers for seconds, so an embedded session
    // closes on another thread: `is_closing()` until `closed` fires on the UI thread.
    // Cancels a start that is waiting on an earlier close.
    void stop();

    void restart();

    // For a client that has left the screen and is quitting: `save_prefs()`, then
    // `stop()`. Ends the process if it is still running after `ShutdownGracePeriod`,
    // since a stuck close would otherwise leave a process the user can't see or quit.
    void shut_down();

    [[nodiscard]] bool is_closing() const noexcept
    {
        return close_thread_.joinable();
    }

    // Blocks until a close has finished, then fires `closed`.
    // For a process that can't get back to its event loop, e.g. during an OS shutdown.
    void wait_for_close();

    sigslot::signal<> source_changed;
    sigslot::signal<> stopped;
    sigslot::signal<> closed;
    sigslot::signal<> config_dir_contended;

    // where an embedded session keeps its settings and torrents
    [[nodiscard]] constexpr std::string const& config_dir() const noexcept
    {
        return config_dir_;
    }

    // the remote session's url without credentials, or empty when not connected to one
    [[nodiscard]] constexpr std::string const& remote_url() const noexcept
    {
        return rpc_.url();
    }

    // where a browser finds the session's web client, with any credentials the session needs
    [[nodiscard]] std::string web_client_url() const;

    // --- torrents

    // The session's torrents as last fetched. Views connect to its signals.
    // `stop()` clears it.
    [[nodiscard]] TorrentList& torrents() noexcept
    {
        return torrents_;
    }

    [[nodiscard]] TorrentList const& torrents() const noexcept
    {
        return torrents_;
    }

    // Fetches `keys` for every torrent into `torrents()`. A complete fetch, so the
    // torrents the reply omits leave the list. `MainStats` is the periodic poll;
    // `MainAll` rebuilds the list, e.g. after connecting.
    void refresh_all_torrents(TorrentKeySet keys);

    // Fetches `MainStats` for the torrents active in the last minute, plus the
    // session's `removed` list. The poll between full refreshes.
    void refresh_active_torrents();

    // Fetches `keys` for `ids`. Sends nothing when `ids` is empty.
    void refresh_torrents(Ids ids, TorrentKeySet keys);

    // --- verbs. Each sends its request, then re-fetches `MainStats` for the same
    // ids so `torrents()` shows the new state without waiting for a poll. A verb
    // given no ids sends nothing: "every torrent" is the explicit `_all` call.

    void start_torrents(Ids ids);
    void start_torrents_now(Ids ids);
    void stop_torrents(Ids ids);
    void start_all_torrents();
    void stop_all_torrents();
    void verify_torrents(Ids ids);
    void reannounce_torrents(Ids ids);
    void queue_move_top(Ids ids);
    void queue_move_up(Ids ids);
    void queue_move_down(Ids ids);
    void queue_move_bottom(Ids ids);

    // Removes `ids` from the session, with their data when `delete_local_data`. On
    // success the rows leave `torrents()` at once, as if a poll had named them
    // `removed`; nothing is fetched. After a failure the next poll decides.
    void remove_torrents(Ids ids, bool delete_local_data);

    // Moves `ids` to `location`, moving their files when `move`. No re-fetch: the
    // session moves asynchronously, so a reply-time fetch would still carry the old
    // `download_dir`. The move marks the torrent edited, and `edited` fetches `MainAll`.
    void set_torrent_location(Ids ids, std::string location, bool move);

    // Renames `old_path`, relative to the torrent root, to `new_name`, then
    // re-fetches `Rename`. On failure fires `torrent_rename_failed` instead.
    void rename_torrent_path(Ids ids, std::string old_path, std::string new_name);

    struct RenameError {
        Ids ids;
        std::string old_path;
        std::string new_name;
        std::string errmsg; // the session's message, or the transport's
        bool network_error = false; // the user can't fix a network error by editing the path
    };

    sigslot::signal<RenameError const&> torrent_rename_failed;

    // --- adding torrents

    // Whether to ask the user about a torrent before adding it.
    enum class AddPrompt : uint8_t {
        FromPrefs, // as `show_options_window` says
        Always,
        Never,
    };

    // Takes a request to add a torrent from anywhere in the client, and is the one place that
    // applies `show_options_window` and `trash_original_torrent_files`.
    //
    // With no prompt, calls `add_torrent()`. With one, calls `resolve_add()` and fires
    // `torrent_add_prompt` for the torrent it hands back, if any. The slot shows the toolkit's
    // dialog and calls `add_torrent()` if the user accepts.
    //
    // `options` go to `add_torrent()`, or to the prompt as the choices it starts with.
    //
    // Returns false if it ignored the request: a `Type::None` one, unless `AddPrompt::Always`
    // asks the user to fill it in.
    bool request_add(AddData add, AddPrompt prompt = AddPrompt::FromPrefs, AddOptions options = {});

    // the request's options, with `download_dir` and `paused` filled in from the prefs if it left them unset
    sigslot::signal<AddData const&, AddOptions const&> torrent_add_prompt;

    // Readies a torrent for a prompt, and again whenever the prompt's user names another source:
    // - A URL is fetched here so the prompt can show its files. If that fails, it stays a URL for
    //   the session to fetch, which may be able to reach what this client can't.
    // - A torrent already in `torrents()` is dropped. Its source file is disposed of as
    //   `add_torrent()` would, and `torrent_add_report` reports it unless it came from the watch dir.
    // - A local file that isn't a torrent is dropped too: there is nothing to ask about it.
    //   `torrent_add_report` reports it as a failed add.
    // `on_resolved` gets the readied torrent, or nullopt if it was dropped. It runs inside this
    // call unless `add` is a URL.
    // `wanted` belongs to whoever waits for the answer, e.g. the prompt. A fetch that lands after
    // it expires, after `stop()` or after this Session is destroyed does nothing with `add`: no
    // disposal, no report, no `on_resolved`. So the user can accept or cancel during a fetch.
    void resolve_add(AddData add, std::weak_ptr<void const> wanted, std::function<void(std::optional<AddData>)> on_resolved);

    // Whether an add is waiting on a URL fetch, e.g. for a busy cursor:
    // `resolve_add()`'s own, or the session's for a URL that `add_torrent()` sent it.
    [[nodiscard]] constexpr bool add_fetch_pending() const noexcept
    {
        return add_fetches_in_flight_ > 0U;
    }

    // when `add_fetch_pending()` changes
    sigslot::signal<> add_fetch_changed;

    // Sends `torrent_add` for `add`. Once the session has the torrent, or says it already has
    // one, disposes of `add`'s source file as `add` says, unless that file is the session's own
    // copy. A new torrent is fetched into `torrents()` at once.
    void add_torrent(AddData add, AddOptions options = {});

    // Adds a torrent this client just created from `top`, the file or folder it shares, to seed it.
    // No prompt, and `torrent_file` is kept whatever `trash_original_torrent_files` says.
    void add_created_torrent(std::string_view torrent_file, std::string_view top);

    // The adds of one batch that the user should hear about, ready to list:
    // each list is in name order, ignoring ASCII case, and holds no entry twice.
    struct AddReport {
        struct Failure {
            std::string name; // the add's `readable_name()`
            std::string errmsg; // the session's message, the transport's, or `resolve_add()`'s

            [[nodiscard]] auto operator<=>(Failure const&) const = default;
        };

        struct Duplicate {
            std::string name; // the add's `readable_short_name()`
            std::string hash_string; // the listed torrent's

            // the hash as a list shows it
            [[nodiscard]] constexpr std::string_view short_hash() const noexcept
            {
                return std::string_view{ hash_string }.substr(0U, 7U);
            }

            [[nodiscard]] auto operator<=>(Duplicate const&) const = default;
        };

        std::vector<Failure> failed;
        std::vector<Duplicate> duplicates; // never a watch dir's

        [[nodiscard]] bool empty() const noexcept
        {
            return std::empty(failed) && std::empty(duplicates);
        }
    };

    // Fires once no add is pending: no `torrent_add` awaits its reply and no `resolve_add()`
    // fetch is in flight. Never inside the call that ends the batch, so a burst of adds makes
    // one report. Nothing from before a `stop()` is reported, and an empty batch fires nothing.
    sigslot::signal<AddReport const&> torrent_add_report;

    // --- torrent_set

    // Identifies one `set_torrents()` call to `torrent_set_done`. A call that sent
    // nothing returns 0, which `torrent_set_done` never names: a set of pending tags
    // must not store it, or it waits forever.
    using Tag = uint64_t;

    // Sends `torrent_set` with `params` for `ids` (this adds `ids` to `params`),
    // then re-fetches `DetailStats` for them, then fires `torrent_set_done` with the
    // returned tag. A dialog that keeps the pending tags can hold its widgets steady
    // until then: by the time the tag fires, `torrents()` holds the set's result,
    // and a fetch that was in flight before the set can no longer appear to undo it.
    // The tag fires on failure and network error too, without the re-fetch, and
    // never inside this call: `RpcClient::UiThreadFunc` queues every reply.
    // Sends nothing when `ids` is empty.
    [[nodiscard]] Tag set_torrents(Ids ids, tr_variant::Map params);

    // one field; `val` goes through `tr::serializer::to_variant()`, so a toolkit
    // type works wherever its converter is visible
    template<typename Val>
    [[nodiscard]] Tag set_torrents(Ids ids, tr_quark const key, Val const& val)
    {
        auto params = tr_variant::Map{ 2U };
        params.insert_or_assign(key, tr::serializer::to_variant(val));
        return set_torrents(std::move(ids), std::move(params));
    }

    sigslot::signal<Tag> torrent_set_done;

    // --- session stats

    // Asks the session for `session_stats`, then `import_session_stats()`.
    void refresh_session_stats();

    // Applies a `session_stats` reply's arguments: `stats()`, `cumulative_stats()` and
    // the `busy` flag behind `should_inhibit_sleep()`. Fires `stats_updated`.
    void import_session_stats(tr_variant::Map const& args);

    // this run of the session, as of the last import
    [[nodiscard]] constexpr tr_session_stats const& stats() const noexcept
    {
        return stats_;
    }

    // every run of the session, as of the last import
    [[nodiscard]] constexpr tr_session_stats const& cumulative_stats() const noexcept
    {
        return cumulative_stats_;
    }

    sigslot::signal<> stats_updated;

    // --- session info

    // Asks the session for every `session_get` field, then `import_session_info()`.
    void refresh_session_info();

    // Applies a full `session_get` reply: `import_session_settings()`, the session's
    // `version`, and its type from `session_id` (Embedded while an embedded session is
    // set, else Local or Remote by `tr_session_id::is_local()`, else unknown).
    // Fires `session_updated`. A reply to a partial `session_get`, e.g. the blocklist
    // fields alone, lacks `session_id`, so give that to `import_session_settings()`.
    void import_session_info(tr_variant::Map const& args);

    // the session's version string, empty until a `session_get` reports it
    [[nodiscard]] constexpr std::string const& session_version() const noexcept
    {
        return session_version_;
    }

    // after `import_session_info()`, and after a derived class imports settings another way
    sigslot::signal<> session_updated;

    // --- blocklist

    // Asks the session to download its blocklist. One of the two signals below answers
    // each call. A success also refreshes the session info, for the new size and date.
    void update_blocklist();

    sigslot::signal<int64_t /*rule_count*/> blocklist_updated;
    sigslot::signal<std::string_view /*errmsg*/> blocklist_update_failed;

    // --- port test

    enum class PortStatus : uint8_t {
        Unknown,
        Checking,
        Open,
        Closed,
        Error,
    };

    // Asks the session whether its peer port is reachable over IPv4 and over IPv6.
    // Sends nothing while a test is pending.
    void test_port();

    // `Unknown` until a test, and again after the peer port changes, `stop()`
    // or `forget_port_status()`.
    [[nodiscard]] constexpr PortStatus port_status(tr_address_type const ip_protocol) const noexcept
    {
        return port_status_[ip_protocol];
    }

    // Back to `Unknown`. An old result says little about the port now, so a UI
    // calls this when it opens, to show only what it tested itself.
    void forget_port_status();

    // Whether a test awaits its replies. It can outlast a `Checking` status, since
    // the peer port can change in the meantime. A UI holds the port's widgets on this.
    [[nodiscard]] constexpr bool port_test_pending() const noexcept
    {
        return port_test_pending_[TR_AF_INET] || port_test_pending_[TR_AF_INET6];
    }

    // when `port_status()` or `port_test_pending()` changes
    sigslot::signal<> port_test_changed;

    // --- session

    // keep the machine awake: a local-filesystem session with active transfers
    [[nodiscard]] bool should_inhibit_sleep() const;

    // returns true iff the session runs inside this process
    [[nodiscard]] constexpr bool is_embedded() const noexcept
    {
        return session_type_ == Type::Embedded;
    }

    // keep this process un-throttled: only when it hosts the session itself
    [[nodiscard]] constexpr bool should_inhibit_nap() const noexcept
    {
        return is_embedded();
    }

    // returns true iff the session runs on this machine
    [[nodiscard]] constexpr bool is_local_filesystem() const noexcept
    {
        return session_type_.value_or(Type::Remote) != Type::Remote;
    }

    // Issues `method` and runs `on_done` with the reply unless this Session has
    // been destroyed by then. Every request goes through here, this class's own
    // and a client's, so no reply ever reaches a dead Session.
    void send(tr_quark method, tr_variant::Map params = {}, RpcClient::ResponseFunc on_done = {});

    // Copy the session's settings, e.g. a `session_get` response, into Prefs without echoing them back to the session.
    // Leaves alone a key with a `session_set` of ours in flight: a `session_get`
    // queued before that set carries the value it is replacing.
    // Also refreshes the blocklist info that `settings` reports.
    // Also refreshes the RPC server settings when the session is embedded.
    // For security reasons, an embedded session is the only place to read them.
    void import_session_settings(tr_variant::Map const& settings);

    // Asks the session for its recently-used paths, `TR_KEY_recent_download_paths` or
    // `TR_KEY_recent_relocate_paths`, and hands them to `on_paths` on the UI thread.
    // Imported settings carry these lists too, but only as of the last import:
    // libtransmission adds to them without reporting a settings change.
    void fetch_recent_paths(tr_quark key, std::function<void(std::vector<std::string> const&)> on_paths);

    // Asks the session for `id`'s magnet link and hands it to `on_link` on the UI thread.
    // Calls nothing when the reply has no link, e.g. once the torrent is gone.
    void fetch_magnet_link(tr_torrent_id_t id, std::function<void(std::string_view)> on_link);

    // The blocklist's rule count, or nullopt until the session reports it.
    [[nodiscard]] constexpr std::optional<int64_t> blocklist_size() const noexcept
    {
        return blocklist_size_;
    }

    // When the session's blocklist was last updated, or the epoch if never.
    // nullopt until the session reports it, which daemons older than 4.2 never do.
    [[nodiscard]] constexpr std::optional<std::chrono::sys_seconds> blocklist_date() const noexcept
    {
        return blocklist_date_;
    }

    // The embedded session's settings, e.g. for `Prefs::save()`;
    // nullopt when there is no embedded session.
    [[nodiscard]] std::optional<tr::Settings> embedded_settings() const;

    // Writes settings.json: the app prefs plus the embedded session's own settings.
    // Comes before `stop()`, which ends the embedded session.
    void save_prefs() const;

protected:
    void set_session_type(std::optional<Type> type);
    void set_busy(bool busy);

    // The embedded session, or nullptr when there isn't one, e.g. after `stop()`.
    // For a client's features that still use the C API instead of RPC.
    [[nodiscard]] constexpr tr_session* embedded_session() const noexcept
    {
        return embedded_session_;
    }

    // Setting an embedded session makes the type Embedded;
    // clearing it leaves the type to `set_session_type()`.
    // Some settings, e.g. the RPC server's, can only be applied to an embedded session.
    void set_embedded_session(tr_session* const session)
    {
        embedded_session_ = session;
        if (session != nullptr) {
            set_session_type(Type::Embedded);
        }
    }

    // Forget the previous session's blocklist info and version, e.g. after disconnecting from it.
    constexpr void clear_session_info() noexcept
    {
        blocklist_size_.reset();
        blocklist_date_.reset();
        session_version_.clear();
        probed_id_.reset();
    }

    // how many `session_set` requests for `key` await their reply
    [[nodiscard]] size_t pending_session_sets(tr_quark key) const noexcept;

private:
    // Which torrents a `torrent_get` names. Owned, so it can ride in a reply closure.
    // Only `All` lets the reply remove by absence.
    struct TorrentScope {
        enum class Kind : uint8_t {
            All,
            RecentlyActive,
            Listed,
        };

        Kind kind = Kind::All;
        Ids ids; // `Listed` only; never empty

        [[nodiscard]] static TorrentScope all() noexcept;
        [[nodiscard]] static TorrentScope recently_active() noexcept;

        // `Listed` for `ids` as a toolkit hands them over, in selection order and
        // maybe with repeats; nullopt when none are left, so there is nothing to send
        [[nodiscard]] static std::optional<TorrentScope> of(Ids ids) noexcept;

        [[nodiscard]] constexpr TorrentList::Coverage coverage() const noexcept
        {
            return kind == Kind::All ? TorrentList::Coverage::Complete : TorrentList::Coverage::Partial;
        }

        // sets `ids`: omitted for `All`, "recently_active", or the list
        void add_to(tr_variant::Map& params) const;
    };

    // `torrent_get` params: `format: "table"`, the `fields` of `keys`, and `scope`'s ids
    [[nodiscard]] static tr_variant::Map make_torrent_get_params(TorrentKeySet keys, TorrentScope const& scope);

    // `torrent_get` for `scope`, applied to `torrents_`; `on_done` runs after the apply
    void fetch_torrents(TorrentKeySet keys, TorrentScope const& scope, RpcClient::ResponseFunc on_done = {});

    // sends `method` for `scope`, then fetches `MainStats` for it
    void send_torrent_verb(tr_quark method, TorrentScope scope);

    // sends `method` for `ids`, as above; sends nothing when `ids` is empty
    void send_torrent_verb(tr_quark method, Ids ids);

    // `add` if a prompt can ask about it. Else nullopt, after `drop_duplicate()` for a torrent that
    // `torrents()` already has, or a failure in `add_report_` for a local file that isn't a torrent.
    [[nodiscard]] std::optional<AddData> drop_unless_promptable(AddData add);
    void on_torrent_add_reply(AddData const& add, RpcResponse const& response);
    void drop_duplicate(AddData const& add, std::string_view hash_string);
    void dispose_source_file(AddData const& add) const;

    [[nodiscard]] constexpr bool adds_pending() const noexcept
    {
        return adds_in_flight_ > 0U || add_fetches_in_flight_ > 0U;
    }

    // queues `torrent_add_report` for the UI loop's next turn, if the batch has ended
    void schedule_add_report();

    void set_add_fetches_in_flight(size_t n);

    // the list's own needs: `MainAll` for the torrents that lack info or were edited
    void on_torrents_changed(TorrentList::Changes const& changes);

    // reads one `session_stats` map, e.g. `current_stats`, into `stats`
    static void load_stats(tr_variant::Map const& map, tr_session_stats& stats);

    // Local or Remote by `tr_session_id::is_local()`, which opens the daemon's
    // lock file; every poll repeats the id, so it is probed once
    [[nodiscard]] Type probe_session_type(std::string_view id);

    void on_port_tested(tr_address_type ip_protocol, RpcResponse const& response);
    void update_sleep_inhibit();
    void update_nap_inhibit();
    void sync_pref_to_session(tr_quark key);
    void start_remote();

    std::string const config_dir_;
    tr_torrent_remove_func const remove_func_;
    Prefs& prefs_;
    RpcClient& rpc_;
    TorrentList torrents_{ prefs_ }; // after prefs_: constructed from it
    tr_session* embedded_session_ = nullptr;
    tr_session* started_session_ = nullptr; // the embedded session, when this class started it
    std::thread close_thread_;

    // an embedded start that waits for `closed`
    struct PendingStart {
        bool paused = false;
    };
    std::optional<PendingStart> start_after_close_;
    woke::SleepInhibitor sleep_inhibitor_;
    woke::NapInhibitor nap_inhibitor_;
    std::optional<std::chrono::sys_seconds> blocklist_date_;
    std::optional<int64_t> blocklist_size_;
    std::optional<Type> session_type_;

    // the last `session_id` probed and its answer
    struct ProbedId {
        std::string id;
        Type type = Type::Remote;
    };
    std::optional<ProbedId> probed_id_;

    std::array<PortStatus, NUM_TR_AF_INET_TYPES> port_status_ = {};
    std::array<bool, NUM_TR_AF_INET_TYPES> port_test_pending_ = {};
    tr_session_stats stats_ = {};
    tr_session_stats cumulative_stats_ = {};
    std::string session_version_;
    Tag next_tag_ = 1; // 0 is never issued
    bool busy_ = false;
    bool importing_settings_ = false;

    // the core prefs with a `session_set` in flight, one entry per request
    std::vector<tr_quark> session_sets_in_flight_;

    AddReport add_report_;
    size_t adds_in_flight_ = 0U; // `torrent_add` requests
    size_t add_fetches_in_flight_ = 0U; // URL fetches: `resolve_add()`'s, and the session's for a URL sent to it

    // Like `alive_`, and replaced by `stop()`: an add from before a stop is forgotten.
    std::shared_ptr<std::monostate const> adds_alive_ = std::make_shared<std::monostate const>();

    // Reply closures hold a weak_ptr to this token, so a reply the UI thread
    // delivers after this Session is destroyed does nothing.
    std::shared_ptr<std::monostate const> const alive_ = std::make_shared<std::monostate const>();

    sigslot::scoped_connection prefs_connection_;
    sigslot::scoped_connection torrents_connection_;
};

} // namespace tr::app
