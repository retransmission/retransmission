// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

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
#include <utility>
#include <variant> // std::monostate
#include <vector>

#include <sigslot/signal.hpp>

#include <woke/woke.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/types.h" // tr_session_stats
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

    Session(Prefs& prefs, RpcClient& rpc);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    virtual ~Session() = default;

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
    void refresh_all_torrents(TorrentKeySet keys = TorrentKeySet::MainStats);

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

    // Fired after syncing a pref whose change invalidates other session info,
    // e.g. changing `download_dir` changes `session_get`'s freespace argument.
    sigslot::signal<> session_refresh_needed;

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

    void update_sleep_inhibit();
    void update_nap_inhibit();
    void sync_pref_to_session(tr_quark key);

    Prefs& prefs_;
    RpcClient& rpc_;
    TorrentList torrents_{ prefs_ }; // after prefs_: constructed from it
    tr_session* embedded_session_ = nullptr;
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
