// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
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
    // torrents' files with `remove_func`. A client that creates its own `tr_session`
    // reports it with `set_embedded_session()` instead, and doesn't call `start()` or `stop()`.
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

    // the remote session's url without credentials, or empty when not connected to one
    [[nodiscard]] constexpr std::string const& remote_url() const noexcept
    {
        return rpc_.url();
    }

    // where a browser finds the session's web client, with any credentials the session needs
    [[nodiscard]] std::string web_client_url() const;

    // --- torrents

    // The session's torrents as last fetched. Views connect to its signals.
    // `clear()` it when the session source changes.
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

    // The embedded session, or nullptr when there isn't one. Setting one makes
    // the type Embedded; clearing it leaves the type to `set_session_type()`.
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

    // Reply closures hold a weak_ptr to this token, so a reply the UI thread
    // delivers after this Session is destroyed does nothing.
    std::shared_ptr<std::monostate const> const alive_ = std::make_shared<std::monostate const>();

    sigslot::scoped_connection prefs_connection_;
    sigslot::scoped_connection torrents_connection_;
};

} // namespace tr::app
