// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/session.h"

#include <algorithm>
#include <cstddef> // size_t
#include <ctime> // std::time()
#include <functional>
#include <iterator> // std::back_inserter(), std::end()
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libtransmission/converters.h"
#include "libtransmission/log.h"
#include "libtransmission/macros.h"
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/transmission.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/rpc-queue.h"
#include "libtransmission-app/torrent-keys.h"
#include "libtransmission-app/torrent-list.h"

using namespace std::string_view_literals;

namespace tr::app
{

tr_variant::Vector make_fields(std::span<tr_quark const> const keys)
{
    auto fields = tr_variant::Vector{};
    fields.reserve(std::size(keys));
    std::ranges::transform(keys, std::back_inserter(fields), [](tr_quark const key) {
        return tr_variant::unmanaged_string(key);
    });
    return fields;
}

// ---

Session::Session(Prefs& prefs, RpcClient& rpc)
    : prefs_{ prefs }
    , rpc_{ rpc }
{
    torrents_connection_ = torrents_.changed.connect_scoped(&Session::on_torrents_changed, this);

    prefs_connection_ = prefs_.observe_changes([this](tr_quark const key) {
        if (key == TR_KEY_inhibit_desktop_hibernation) {
            update_sleep_inhibit();
        }

        if (prefs_is_core(key) && !importing_settings_) {
            sync_pref_to_session(key);
        }
    });
}

void Session::send(tr_quark const method, tr_variant::Map params, RpcClient::ResponseFunc on_done)
{
    rpc_.exec(method, std::move(params), [alive = std::weak_ptr{ alive_ }, on_done = std::move(on_done)](RpcResponse response) {
        if (on_done && alive.lock()) {
            on_done(std::move(response));
        }
    });
}

// --- torrents

Session::TorrentScope Session::TorrentScope::all() noexcept
{
    return {};
}

Session::TorrentScope Session::TorrentScope::recently_active() noexcept
{
    return { .kind = Kind::RecentlyActive, .ids = {} };
}

std::optional<Session::TorrentScope> Session::TorrentScope::of(Ids ids) noexcept
{
    TorrentList::normalize(ids);
    if (std::empty(ids)) {
        return {};
    }

    return TorrentScope{ .kind = Kind::Listed, .ids = std::move(ids) };
}

void Session::TorrentScope::add_to(tr_variant::Map& params) const
{
    switch (kind) {
    case Kind::All:
        break;

    case Kind::RecentlyActive:
        params.insert_or_assign(TR_KEY_ids, tr_variant::unmanaged_string(TR_KEY_recently_active));
        break;

    case Kind::Listed:
        params.insert_or_assign(TR_KEY_ids, tr::serializer::to_variant(ids));
        break;
    }
}

tr_variant::Map Session::make_torrent_get_params(TorrentKeySet const keys, TorrentScope const& scope)
{
    auto params = tr_variant::Map{ 3U };
    params.try_emplace(TR_KEY_format, tr_variant::unmanaged_string("table"sv));
    params.try_emplace(TR_KEY_fields, make_fields(torrent_keys(keys)));
    scope.add_to(params);
    return params;
}

void Session::fetch_torrents(TorrentKeySet const keys, TorrentScope const& scope, RpcClient::ResponseFunc on_done)
{
    auto const coverage = scope.coverage();
    send(
        TR_KEY_torrent_get,
        make_torrent_get_params(keys, scope),
        [this, coverage, on_done = std::move(on_done)](RpcResponse response) {
            // An error reply may still carry `args`, its `data.result`, and a stale
            // reply is a network error without any: only a success reaches the list.
            if (auto const* const args = response.success ? response.args_map() : nullptr) {
                torrents_.update(*args, coverage, std::time(nullptr));
            }

            if (on_done) {
                on_done(std::move(response));
            }
        });
}

void Session::refresh_all_torrents(TorrentKeySet const keys)
{
    fetch_torrents(keys, TorrentScope::all());
}

void Session::refresh_active_torrents()
{
    fetch_torrents(TorrentKeySet::MainStats, TorrentScope::recently_active());
}

void Session::refresh_torrents(Ids ids, TorrentKeySet const keys)
{
    if (auto const scope = TorrentScope::of(std::move(ids))) {
        fetch_torrents(keys, *scope);
    }
}

// A slot of `changed` must not call `update()`. This one only sends a request,
// whose reply RpcClient delivers on a later UI-thread turn.
void Session::on_torrents_changed(TorrentList::Changes const& changes)
{
    auto ids = Ids{};
    std::ranges::set_union(changes.need_info, changes.edited, std::back_inserter(ids));
    if (auto const scope = TorrentScope::of(std::move(ids))) {
        fetch_torrents(TorrentKeySet::MainAll, *scope);
    }
}

// --- verbs

void Session::send_torrent_verb(tr_quark const method, TorrentScope scope)
{
    auto params = tr_variant::Map{ 1U };
    scope.add_to(params);

    RpcQueue::create()
        .add([this, method, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
            send(method, std::move(params), std::move(done));
        })
        .add([this, scope = std::move(scope)](RpcClient::ResponseFunc done) {
            fetch_torrents(TorrentKeySet::MainStats, scope, std::move(done));
        })
        .run();
}

void Session::send_torrent_verb(tr_quark const method, Ids ids)
{
    if (auto scope = TorrentScope::of(std::move(ids))) {
        send_torrent_verb(method, std::move(*scope));
    }
}

void Session::start_torrents(Ids ids)
{
    send_torrent_verb(TR_KEY_torrent_start, std::move(ids));
}

void Session::start_torrents_now(Ids ids)
{
    send_torrent_verb(TR_KEY_torrent_start_now, std::move(ids));
}

void Session::stop_torrents(Ids ids)
{
    send_torrent_verb(TR_KEY_torrent_stop, std::move(ids));
}

void Session::start_all_torrents()
{
    send_torrent_verb(TR_KEY_torrent_start, TorrentScope::all());
}

void Session::stop_all_torrents()
{
    send_torrent_verb(TR_KEY_torrent_stop, TorrentScope::all());
}

void Session::verify_torrents(Ids ids)
{
    send_torrent_verb(TR_KEY_torrent_verify, std::move(ids));
}

void Session::reannounce_torrents(Ids ids)
{
    send_torrent_verb(TR_KEY_torrent_reannounce, std::move(ids));
}

void Session::queue_move_top(Ids ids)
{
    send_torrent_verb(TR_KEY_queue_move_top, std::move(ids));
}

void Session::queue_move_up(Ids ids)
{
    send_torrent_verb(TR_KEY_queue_move_up, std::move(ids));
}

void Session::queue_move_down(Ids ids)
{
    send_torrent_verb(TR_KEY_queue_move_down, std::move(ids));
}

void Session::queue_move_bottom(Ids ids)
{
    send_torrent_verb(TR_KEY_queue_move_bottom, std::move(ids));
}

void Session::remove_torrents(Ids ids, bool const delete_local_data)
{
    auto scope = TorrentScope::of(std::move(ids));
    if (!scope) {
        return;
    }

    auto params = tr_variant::Map{ 2U };
    scope->add_to(params);
    params.try_emplace(TR_KEY_delete_local_data, delete_local_data);
    send(TR_KEY_torrent_remove, std::move(params), [this, ids = std::move(scope->ids)](RpcResponse const& response) {
        if (response.success) {
            torrents_.remove(ids); // the rows go now rather than on the next poll
        }
    });
}

void Session::set_torrent_location(Ids ids, std::string location, bool const move)
{
    auto const scope = TorrentScope::of(std::move(ids));
    if (!scope) {
        return;
    }

    auto params = tr_variant::Map{ 3U };
    scope->add_to(params);
    params.try_emplace(TR_KEY_location, std::move(location));
    params.try_emplace(TR_KEY_move, move);
    send(TR_KEY_torrent_set_location, std::move(params));
}

void Session::rename_torrent_path(Ids ids, std::string old_path, std::string new_name)
{
    auto scope = TorrentScope::of(std::move(ids));
    if (!scope) {
        return;
    }

    auto params = tr_variant::Map{ 3U };
    scope->add_to(params);
    params.try_emplace(TR_KEY_path, old_path);
    params.try_emplace(TR_KEY_name, new_name);
    auto failure = RenameError{
        .ids = scope->ids,
        .old_path = std::move(old_path),
        .new_name = std::move(new_name),
        .errmsg = {},
        .network_error = false,
    };

    RpcQueue::create()
        .add(
            [this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
                send(TR_KEY_torrent_rename_path, std::move(params), std::move(done));
            },
            [this, failure = std::move(failure)](RpcResponse const& response) mutable {
                failure.errmsg = response.errmsg;
                failure.network_error = response.network_error;
                torrent_rename_failed(failure);
            })
        .add([this, scope = std::move(*scope)](RpcClient::ResponseFunc done) {
            fetch_torrents(TorrentKeySet::Rename, scope, std::move(done));
        })
        .run();
}

// --- session

size_t Session::pending_session_sets(tr_quark const key) const noexcept
{
    return static_cast<size_t>(std::ranges::count(session_sets_in_flight_, key));
}

void Session::sync_pref_to_session(tr_quark const key)
{
    auto* const embedded = embedded_session_;

    switch (key) {
    // For security reasons, the RPC server's own settings can't be changed
    // via RPC, so apply them directly when the session is embedded.
    case TR_KEY_rpc_authentication_required:
        if (embedded != nullptr) {
            tr_sessionSetRPCPasswordEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    case TR_KEY_rpc_enabled:
        if (embedded != nullptr) {
            tr_sessionSetRPCEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    case TR_KEY_rpc_password:
        if (embedded != nullptr) {
            tr_sessionSetRPCPassword(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_port:
        if (embedded != nullptr) {
            tr_sessionSetRPCPort(embedded, static_cast<uint16_t>(prefs_.get<int>(key)));
        }
        break;

    case TR_KEY_rpc_username:
        if (embedded != nullptr) {
            tr_sessionSetRPCUsername(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_whitelist:
        if (embedded != nullptr) {
            tr_sessionSetRPCWhitelist(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_whitelist_enabled:
        if (embedded != nullptr) {
            tr_sessionSetRPCWhitelistEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    // The log level applies to this process's logger, not the session,
    // so there is nothing to send over RPC.
    case TR_KEY_message_level:
        tr_logSetLevel(prefs_.get<tr_log_level>(key));
        break;

    default:
        {
            auto params = tr_variant::Map{ 1U };
            params.insert_or_assign(key, prefs_.get<tr_variant>(key));
            session_sets_in_flight_.push_back(key);
            send(TR_KEY_session_set, std::move(params), [this, key](RpcResponse const& /*response*/) {
                // one entry per request: another set of the same key may still be in flight
                if (auto const iter = std::ranges::find(session_sets_in_flight_, key);
                    iter != std::end(session_sets_in_flight_)) {
                    session_sets_in_flight_.erase(iter);
                }
            });

            if (key == TR_KEY_download_dir) {
                session_refresh_needed();
            }
        }
        break;
    }
}

void Session::import_session_settings(tr_variant::Map const& settings)
{
    importing_settings_ = true;

    auto const* const embedded = embedded_session_;

    for (auto const& [key, value] : settings) {
        // An embedded session's port comes from the session itself, below:
        // tr_sessionGetSettings() carries the configured port, and session_get
        // the advertised one, which port forwarding can remap.
        if (key == TR_KEY_peer_port && embedded != nullptr) {
            continue;
        }

        // a reply from before our own set carries the value that set replaced
        if (prefs_is_core(key) && pending_session_sets(key) == 0U) {
            prefs_.set(key, value);
        }
    }

    if (auto const size = settings.value_if<int64_t>(TR_KEY_blocklist_size)) {
        blocklist_size_ = size;
    }

    if (auto const date = settings.value_if<int64_t>(TR_KEY_blocklist_date)) {
        blocklist_date_ = std::chrono::sys_seconds{ std::chrono::seconds{ *date } };
    }

    // For security reasons, the RPC server's own settings aren't in
    // `session_get` responses; read them directly when the session is embedded.
    if (embedded != nullptr) {
        // the port the session listens on, which neither settings source above reports
        if (pending_session_sets(TR_KEY_peer_port) == 0U) {
            prefs_.set(TR_KEY_peer_port, tr_sessionGetPeerPort(embedded));
        }

        prefs_.set(TR_KEY_rpc_authentication_required, tr_sessionIsRPCPasswordEnabled(embedded));
        prefs_.set(TR_KEY_rpc_enabled, tr_sessionIsRPCEnabled(embedded));
        prefs_.set(TR_KEY_rpc_password, tr_sessionGetRPCPassword(embedded));
        prefs_.set(TR_KEY_rpc_port, tr_sessionGetRPCPort(embedded));
        prefs_.set(TR_KEY_rpc_username, tr_sessionGetRPCUsername(embedded));
        prefs_.set(TR_KEY_rpc_whitelist, tr_sessionGetRPCWhitelist(embedded));
        prefs_.set(TR_KEY_rpc_whitelist_enabled, tr_sessionGetRPCWhitelistEnabled(embedded));
    }

    importing_settings_ = false;
}

void Session::fetch_recent_paths(tr_quark const key, std::function<void(std::vector<std::string> const&)> on_paths)
{
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_fields, make_fields({ key }));

    send(TR_KEY_session_get, std::move(params), [key, on_paths = std::move(on_paths)](RpcResponse const& response) {
        auto paths = std::vector<std::string>{};

        if (auto const* const result = response.args_map(); result != nullptr) {
            if (auto const iter = result->find(key); iter != std::end(*result)) {
                paths = tr::serializer::to_value<std::vector<std::string>>(iter->second).value_or(std::vector<std::string>{});
            }
        }

        on_paths(paths);
    });
}

std::optional<tr::Settings> Session::embedded_settings() const
{
    if (embedded_session_ != nullptr) {
        return tr_sessionGetSettings(embedded_session_);
    }

    return {};
}

void Session::set_session_type(std::optional<Type> const type)
{
    if (session_type_ != type) {
        session_type_ = type;
        update_sleep_inhibit();
        update_nap_inhibit();
    }

    // should_inhibit_sleep() ignores busyness once we're non-local,
    // so a non-local session's remembered busyness is meaningless --
    // drop it here so a later switch back to a local session
    // starts from a clean slate.
    if (!is_local_filesystem()) {
        busy_ = false;
    }
}

void Session::set_busy(bool const busy)
{
    if (busy_ != busy) {
        busy_ = busy;
        update_sleep_inhibit();
    }
}

bool Session::should_inhibit_sleep() const
{
    return is_local_filesystem() && busy_ && prefs_.get<bool>(TR_KEY_inhibit_desktop_hibernation);
}

void Session::update_sleep_inhibit()
{
    if (should_inhibit_sleep()) {
        sleep_inhibitor_.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Torrents are active");
    } else {
        sleep_inhibitor_.uninhibit();
    }
}

void Session::update_nap_inhibit()
{
    if (should_inhibit_nap()) {
        nap_inhibitor_.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Application is running");
    } else {
        nap_inhibitor_.uninhibit();
    }
}

} // namespace tr::app
