// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/session.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint>
#include <cstdio> // stderr
#include <cstdlib> // std::_Exit(), EXIT_FAILURE
#include <ctime> // std::time()
#include <functional>
#include <iterator> // std::back_inserter(), std::end()
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/constants.h" // TrHttpServerRpcRelativePath, TrHttpServerWebRelativePath
#include "libtransmission/converters.h"
#include "libtransmission/crypto-utils.h" // tr_base64_encode()
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/macros.h"
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/session-id.h"
#include "libtransmission/torrent-builder.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/transmission.h"
#include "libtransmission/types.h"
#include "libtransmission/values.h"
#include "libtransmission/variant.h"
#include "libtransmission/web-utils.h"
#include "libtransmission/web.h" // tr_web::FetchResponse

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

void exit_process_after(std::chrono::milliseconds const delay)
{
    std::thread{ [delay] {
        std::this_thread::sleep_for(delay);
        fmt::print(stderr, "Still shutting down after {:d} ms; exiting anyway.\n", delay.count());
        std::_Exit(EXIT_FAILURE);
    } }.detach();
}

// ---

namespace
{

struct Credentials {
    std::string username;
    std::string password;
};

// what the remote session requires, if anything
[[nodiscard]] std::optional<Credentials> remote_credentials(Prefs const& prefs)
{
    auto username = prefs.get<std::string>(TR_KEY_remote_session_username);
    if (!prefs.get<bool>(TR_KEY_remote_session_requires_authentication) || std::empty(username)) {
        return {};
    }

    return Credentials{ .username = std::move(username), .password = prefs.get<std::string>(TR_KEY_remote_session_password) };
}

// `relative_path` on the remote session. `userinfo` is empty or ends with '@'.
[[nodiscard]] std::string remote_url_for(
    Prefs const& prefs,
    std::string_view const relative_path,
    std::string_view const userinfo = {})
{
    auto host = prefs.get<std::string>(TR_KEY_remote_session_host);
    if (host.contains(':') && !host.starts_with('[')) {
        host = fmt::format("[{:s}]", host); // a bare IPv6 literal
    }

    return fmt::format(
        "{:s}://{:s}{:s}:{:d}{:s}{:s}",
        prefs.get<bool>(TR_KEY_remote_session_https) ? "https"sv : "http"sv,
        userinfo,
        host,
        prefs.get<int>(TR_KEY_remote_session_port),
        prefs.get<std::string>(TR_KEY_remote_session_url_base_path),
        relative_path);
}

} // namespace

Session::Session(std::string config_dir, Prefs& prefs, RpcClient& rpc, tr_torrent_remove_func remove_func)
    : config_dir_{ std::move(config_dir) }
    , remove_func_{ remove_func ? std::move(remove_func) : tr_sys_path_remove }
    , prefs_{ prefs }
    , rpc_{ rpc }
{
    torrents_connection_ = torrents_.changed.connect_scoped(&Session::on_torrents_changed, this);

    prefs_connection_ = prefs_.observe_changes([this](tr_quark const key) {
        if (key == TR_KEY_inhibit_desktop_hibernation) {
            update_sleep_inhibit();
        }

        if (key == TR_KEY_peer_port) {
            forget_port_status();
        }

        if (prefs_is_core(key) && !importing_settings_) {
            sync_pref_to_session(key);
        }
    });
}

// NOLINTNEXTLINE(bugprone-exception-escape): stop() allocates
Session::~Session()
{
    if (started_session_ != nullptr) {
        stop();
    }

    if (is_closing()) {
        close_thread_.join();
    }
}

// --- lifecycle

void Session::start()
{
    if (prefs_.get<bool>(TR_KEY_remote_session_enabled)) {
        start_remote();
    } else {
        start_embedded();
    }
}

void Session::start_remote()
{
    TR_ASSERT(started_session_ == nullptr && std::empty(rpc_.url()));
    start_after_close_.reset(); // the latest start wins

    auto const url = remote_url_for(prefs_, TrHttpServerRpcRelativePath);
    if (auto credentials = remote_credentials(prefs_)) {
        rpc_.start(url, std::move(credentials->username), std::move(credentials->password));
    } else {
        rpc_.start(url);
    }

    source_changed();
}

void Session::start_embedded(bool const paused)
{
    TR_ASSERT(started_session_ == nullptr && std::empty(rpc_.url()));

    if (is_closing()) {
        start_after_close_ = PendingStart{ .paused = paused };
        return;
    }

    TR_ASSERT(!std::empty(config_dir_));

    auto* const session = tr_sessionInit(config_dir_, true, tr_sessionLoadSettings(config_dir_), remove_func_);

    if (tr_sessionConfigDirIsContended(session)) {
        tr_sessionClose(session, 1); // it has loaded no torrents, so no tracker to wait on
        config_dir_contended();
        return;
    }

    started_session_ = session;
    set_embedded_session(session);
    rpc_.start(session);

    auto builder = tr_torrent_builder{ session };
    if (paused) {
        builder.set_paused(true);
    }
    tr_sessionLoadTorrents(session, &builder);

    source_changed();
}

void Session::stop()
{
    start_after_close_.reset();
    rpc_.stop();
    set_embedded_session(nullptr);

    adds_alive_ = std::make_shared<std::monostate const>();
    add_report_ = {};
    adds_in_flight_ = 0U;
    set_add_fetches_in_flight(0U); // nothing from before the stop will end one

    if (auto* const session = std::exchange(started_session_, nullptr); session != nullptr) {
        close_thread_ = std::thread{ [this, session] {
            tr_sessionClose(session);

            rpc_.run_on_ui_thread([this, alive = std::weak_ptr{ alive_ }] {
                if (alive.lock()) {
                    wait_for_close();
                }
            });
        } };
    }

    torrents_.clear();
    clear_session_info();
    set_session_type({});
    forget_port_status();
    stopped();
}

void Session::restart()
{
    stop();
    start();
}

void Session::shut_down()
{
    exit_process_after();
    save_prefs();
    stop();
}

void Session::wait_for_close()
{
    if (!is_closing()) {
        return;
    }

    close_thread_.join();
    closed();

    if (auto const pending = std::exchange(start_after_close_, {})) {
        start_embedded(pending->paused);
    }
}

std::string Session::web_client_url() const
{
    if (is_embedded()) {
        return fmt::format("http://localhost:{:d}", prefs_.get<int>(TR_KEY_rpc_port));
    }

    auto userinfo = std::string{};
    if (auto const credentials = remote_credentials(prefs_)) {
        tr_urlPercentEncode(userinfo, credentials->username);
        userinfo += ':';
        tr_urlPercentEncode(userinfo, credentials->password);
        userinfo += '@';
    }

    return remote_url_for(prefs_, TrHttpServerWebRelativePath, userinfo);
}

// --- rpc

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
            if (auto const* const args = response.result_map()) {
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

void Session::withhold_torrent(tr_torrent_id_t const id)
{
    torrents_.withhold(id);
}

void Session::release_torrent(tr_torrent_id_t const id)
{
    torrents_.release(id);
    refresh_torrents({ id }, TorrentKeySet::MainAll);
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

// --- adding torrents

namespace
{

// Whether `a` comes before `b` in a list a user reads.
// No toolkit's collation is at hand here, so only ASCII case is ignored.
[[nodiscard]] bool name_precedes(std::string_view const a, std::string_view const b) noexcept
{
    static auto constexpr Fold = [](char const ch) constexpr noexcept {
        return static_cast<unsigned char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    };

    return std::ranges::lexicographical_compare(a, b, {}, Fold, Fold);
}

// puts a report's entries in name order and drops the repeats
template<typename Entry>
void sort_and_dedupe(std::vector<Entry>& entries)
{
    std::ranges::sort(entries, [](Entry const& a, Entry const& b) {
        if (name_precedes(a.name, b.name)) {
            return true;
        }

        if (name_precedes(b.name, a.name)) {
            return false;
        }

        return a < b; // so that equal entries meet
    });

    auto const repeats = std::ranges::unique(entries);
    entries.erase(std::begin(repeats), std::end(repeats));
}

} // namespace

bool Session::request_add(AddData add, AddPrompt const prompt, AddOptions options)
{
    if (add.type == AddData::Type::None && prompt != AddPrompt::Always) {
        return false;
    }

    add.trash_source = prefs_.get<bool>(TR_KEY_trash_original_torrent_files);

    if (prompt == AddPrompt::Never || (prompt == AddPrompt::FromPrefs && !prefs_.get<bool>(TR_KEY_show_options_window))) {
        add_torrent(std::move(add), std::move(options));
        return true;
    }

    // the prompt shows both, so it needs the defaults that `add_torrent()` and the session would apply
    if (std::empty(options.download_dir)) {
        options.download_dir = prefs_.get<std::string>(TR_KEY_download_dir);
    }
    if (!options.paused) {
        options.paused = !prefs_.get<bool>(TR_KEY_start_added_torrents);
    }

    resolve_add(std::move(add), alive_, [this, options = std::move(options)](std::optional<AddData> const& resolved) {
        if (resolved) {
            torrent_add_prompt(*resolved, options);
        }
    });
    return true;
}

void Session::resolve_add(
    AddData add,
    std::weak_ptr<void const> wanted,
    std::function<void(std::optional<AddData>)> on_resolved)
{
    if (add.type != AddData::Type::Url) {
        on_resolved(drop_if_listed(std::move(add)));
        schedule_add_report();
        return;
    }

    // The user is waiting, with nothing on screen yet. If time runs out, the session gets to try.
    static auto constexpr Timeout = std::chrono::seconds{ 30 };

    auto url = add.source;

    set_add_fetches_in_flight(add_fetches_in_flight_ + 1U);
    rpc_.fetch(
        std::move(url),
        Timeout,
        [this,
         alive = std::weak_ptr{ adds_alive_ },
         wanted = std::move(wanted),
         add = std::move(add),
         on_resolved = std::move(on_resolved)](tr_web::FetchResponse const& response) mutable {
            if (alive.expired()) {
                return;
            }

            set_add_fetches_in_flight(add_fetches_in_flight_ - 1U);

            if (!wanted.expired()) {
                if (response.status == 200 && tr_torrent_metainfo{}.parse_benc(response.body)) {
                    add.type = AddData::Type::Metainfo;
                    add.metainfo = response.body;
                }

                on_resolved(drop_if_listed(std::move(add)));
            }

            schedule_add_report();
        });
}

std::optional<AddData> Session::drop_if_listed(AddData add)
{
    if (auto const tm = add.parsed()) {
        if (auto const* const tor = torrents_.find(tm->info_hash()); tor != nullptr) {
            drop_duplicate(add, tor->hash_string().sv());
            return {};
        }
    }

    return add;
}

void Session::add_torrent(AddData add, AddOptions options)
{
    auto args = tr_variant::Map{ 8U };

    switch (add.type) {
    case AddData::Type::Magnet:
    case AddData::Type::Url:
        args.insert_or_assign(TR_KEY_filename, add.source);
        break;

    case AddData::Type::File:
    case AddData::Type::Metainfo:
        args.insert_or_assign(TR_KEY_metainfo, tr_base64_encode(add.metainfo));
        break;

    case AddData::Type::None:
        // the session's error tells the user this can't be added
        break;
    }

    args.try_emplace(TR_KEY_paused, options.paused.value_or(!prefs_.get<bool>(TR_KEY_start_added_torrents)));

    if (!std::empty(options.download_dir)) {
        args.try_emplace(TR_KEY_download_dir, std::move(options.download_dir));
    }

    if (options.bandwidth_priority) {
        args.try_emplace(TR_KEY_bandwidth_priority, tr::serializer::to_variant(*options.bandwidth_priority));
    }

    auto const add_file_indices = [&args](tr_quark const key, std::vector<int> const& indices) {
        if (!std::empty(indices)) {
            args.try_emplace(key, tr::serializer::to_variant(indices));
        }
    };
    add_file_indices(TR_KEY_files_unwanted, options.files_unwanted);
    add_file_indices(TR_KEY_priority_low, options.priority_low);
    add_file_indices(TR_KEY_priority_high, options.priority_high);

    // the session fetches a URL before it replies
    if (add.type == AddData::Type::Url) {
        set_add_fetches_in_flight(add_fetches_in_flight_ + 1U);
    }

    ++adds_in_flight_;
    send(
        TR_KEY_torrent_add,
        std::move(args),
        [this, alive = std::weak_ptr{ adds_alive_ }, add = std::move(add)](RpcResponse const& response) {
            if (alive.expired()) {
                return;
            }

            --adds_in_flight_;
            if (add.type == AddData::Type::Url) {
                set_add_fetches_in_flight(add_fetches_in_flight_ - 1U);
            }

            on_torrent_add_reply(add, response);
            schedule_add_report();
        });
}

void Session::add_created_torrent(std::string_view const torrent_file, std::string_view const top)
{
    auto options = AddOptions{};
    options.download_dir = tr_sys_path_dirname(top);
    add_torrent(AddData{ torrent_file }, std::move(options));
}

void Session::on_torrent_add_reply(AddData const& add, RpcResponse const& response)
{
    if (!response.success) {
        add_report_.failed.emplace_back(add.readable_name(), response.errmsg);
        return;
    }

    auto const* const result = response.result_map();
    if (result == nullptr) {
        return;
    }

    if (auto const* const added = result->find_if<tr_variant::Map>(TR_KEY_torrent_added)) {
        dispose_source_file(add);

        if (auto const id = added->value_if<int64_t>(TR_KEY_id)) {
            refresh_torrents({ static_cast<tr_torrent_id_t>(*id) }, TorrentKeySet::MainAll);
        }
    } else if (auto const* const duplicate = result->find_if<tr_variant::Map>(TR_KEY_torrent_duplicate)) {
        drop_duplicate(add, duplicate->value_if<std::string_view>(TR_KEY_hash_string).value_or(std::string_view{}));
    }
}

void Session::drop_duplicate(AddData const& add, std::string_view const hash_string)
{
    dispose_source_file(add);

    if (!add.from_watch_dir) {
        add_report_.duplicates.emplace_back(add.readable_short_name(), std::string{ hash_string });
    }
}

void Session::set_add_fetches_in_flight(size_t const n)
{
    auto const was_pending = add_fetch_pending();
    add_fetches_in_flight_ = n;

    if (was_pending != add_fetch_pending()) {
        add_fetch_changed();
    }
}

void Session::schedule_add_report()
{
    if (adds_pending() || add_report_.empty()) {
        return;
    }

    rpc_.run_on_ui_thread([this, alive = std::weak_ptr{ adds_alive_ }] {
        // Nothing to report if an add started since then, which ends the batch later,
        // or if another of these already fired the report.
        if (!alive.expired() && !adds_pending() && !add_report_.empty()) {
            auto report = std::exchange(add_report_, {});
            sort_and_dedupe(report.failed);
            sort_and_dedupe(report.duplicates);
            torrent_add_report(report);
        }
    });
}

void Session::dispose_source_file(AddData const& add) const
{
    if (add.type != AddData::Type::File || (!add.trash_source && !add.from_watch_dir) || !tr_sys_path_exists(add.source)) {
        return;
    }

    // The session keeps its own copy of each torrent in a directory directly under its config dir.
    // A watch dir's file is never that copy, even if the watch dir is there too.
    if (!add.from_watch_dir && tr_sys_path_is_same(tr_sys_path_dirname(tr_sys_path_dirname(add.source)), config_dir_)) {
        return;
    }

    if (add.trash_source) {
        remove_func_(add.source, nullptr);
    } else if (add.from_watch_dir) {
        tr_sys_path_rename(add.source, fmt::format("{:s}.added", add.source));
    }
}

// --- torrent_set

Session::Tag Session::set_torrents(Ids ids, tr_variant::Map params)
{
    auto scope = TorrentScope::of(std::move(ids));
    if (!scope) {
        return {};
    }

    scope->add_to(params);
    auto const tag = next_tag_++;

    // the re-fetch step takes the chain's `done`, so `finally` runs after its reply is applied
    RpcQueue::create()
        .add([this, params = std::move(params)](RpcClient::ResponseFunc done) mutable {
            send(TR_KEY_torrent_set, std::move(params), std::move(done));
        })
        .add([this, scope = std::move(*scope)](RpcClient::ResponseFunc done) {
            fetch_torrents(TorrentKeySet::DetailStats, scope, std::move(done));
        })
        .finally([this, tag] { torrent_set_done(tag); })
        .run();

    return tag;
}

// --- session stats

void Session::load_stats(tr_variant::Map const& map, tr_session_stats& stats)
{
    static constexpr auto Fields = std::tuple{
        tr::serializer::Field<&tr_session_stats::downloadedBytes>{ TR_KEY_downloaded_bytes },
        tr::serializer::Field<&tr_session_stats::filesAdded>{ TR_KEY_files_added },
        tr::serializer::Field<&tr_session_stats::secondsActive>{ TR_KEY_seconds_active },
        tr::serializer::Field<&tr_session_stats::sessionCount>{ TR_KEY_session_count },
        tr::serializer::Field<&tr_session_stats::uploadedBytes>{ TR_KEY_uploaded_bytes },
    };
    tr::serializer::load(map, stats, Fields);

    stats.ratio = tr::Values::Ratio{ stats.uploadedBytes, stats.downloadedBytes };
}

void Session::refresh_session_stats()
{
    send(TR_KEY_session_stats, {}, [this](RpcResponse const& response) {
        if (auto const* const args = response.result_map()) {
            import_session_stats(*args);
        }
    });
}

void Session::import_session_stats(tr_variant::Map const& args)
{
    if (auto const* const current = args.find_if<tr_variant::Map>(TR_KEY_current_stats)) {
        load_stats(*current, stats_);
    }

    if (auto const* const cumulative = args.find_if<tr_variant::Map>(TR_KEY_cumulative_stats)) {
        load_stats(*cumulative, cumulative_stats_);
    }

    if (auto const busy = args.value_if<bool>(TR_KEY_busy)) {
        set_busy(*busy);
    }

    stats_updated();
}

// --- session info

void Session::refresh_session_info()
{
    send(TR_KEY_session_get, {}, [this](RpcResponse const& response) {
        if (auto const* const args = response.result_map()) {
            import_session_info(*args);
        }
    });
}

void Session::import_session_info(tr_variant::Map const& args)
{
    import_session_settings(args);

    if (auto const version = args.value_if<std::string_view>(TR_KEY_version)) {
        session_version_ = *version;
    }

    // an embedded session is Embedded whatever its id says: `set_embedded_session()` set that
    if (embedded_session_ == nullptr) {
        set_session_type(args.value_if<std::string_view>(TR_KEY_session_id).transform([this](std::string_view const id) {
            return probe_session_type(id);
        }));
    }

    session_updated();
}

Session::Type Session::probe_session_type(std::string_view const id)
{
    if (!probed_id_ || probed_id_->id != id) {
        probed_id_ = ProbedId{ .id = std::string{ id }, .type = tr_session_id::is_local(id) ? Type::Local : Type::Remote };
    }

    return probed_id_->type;
}

// --- blocklist

void Session::update_blocklist()
{
    send(TR_KEY_blocklist_update, {}, [this](RpcResponse const& response) {
        auto const* const args = response.result_map();
        auto const rule_count = args != nullptr ? args->value_if<int64_t>(TR_KEY_blocklist_size) : std::nullopt;
        if (!rule_count) {
            blocklist_update_failed(response.errmsg);
            return;
        }

        refresh_session_info();
        blocklist_updated(*rule_count);
    });
}

// --- port test

void Session::test_port()
{
    static auto constexpr IpStr = std::array{ "ipv4"sv, "ipv6"sv };

    if (port_test_pending()) {
        return;
    }

    port_status_.fill(PortStatus::Checking);
    port_test_pending_.fill(true);
    port_test_changed();

    for (auto const ip_protocol : { TR_AF_INET, TR_AF_INET6 }) {
        auto params = tr_variant::Map{ 1U };
        params.try_emplace(TR_KEY_ip_protocol, tr_variant::unmanaged_string(IpStr[ip_protocol]));
        send(TR_KEY_port_test, std::move(params), [this, ip_protocol](RpcResponse const& response) {
            on_port_tested(ip_protocol, response);
        });
    }
}

void Session::forget_port_status()
{
    port_status_.fill(PortStatus::Unknown);
    port_test_changed();
}

void Session::on_port_tested(tr_address_type const ip_protocol, RpcResponse const& response)
{
    port_test_pending_[ip_protocol] = false;

    // Otherwise the peer port changed during the test, so its answer is about an old port.
    if (auto& status = port_status_[ip_protocol]; status == PortStatus::Checking) {
        auto const* const args = response.result_map();
        auto const is_open = args != nullptr ? args->value_if<bool>(TR_KEY_port_is_open) : std::nullopt;
        if (!is_open) {
            status = PortStatus::Error;
        } else {
            status = *is_open ? PortStatus::Open : PortStatus::Closed;
        }
    }

    port_test_changed();
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

void Session::fetch_magnet_link(tr_torrent_id_t const id, std::function<void(std::string_view)> on_link)
{
    auto params = tr_variant::Map{ 2U };
    params.try_emplace(TR_KEY_ids, tr::serializer::to_variant(Ids{ id }));
    params.try_emplace(TR_KEY_fields, make_fields({ TR_KEY_magnet_link }));

    send(TR_KEY_torrent_get, std::move(params), [on_link = std::move(on_link)](RpcResponse const& response) {
        auto const* const args = response.result_map();
        auto const* const torrents = args != nullptr ? args->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
        auto const* const row = torrents != nullptr && !std::empty(*torrents) ? torrents->front().get_if<tr_variant::Map>() :
                                                                                nullptr;
        if (auto const link = row != nullptr ? row->value_if<std::string_view>(TR_KEY_magnet_link) : std::nullopt) {
            on_link(*link);
        }
    });
}

std::optional<tr::Settings> Session::embedded_settings() const
{
    if (embedded_session_ != nullptr) {
        return tr_sessionGetSettings(embedded_session_);
    }

    return {};
}

void Session::save_prefs() const
{
    prefs_.save(config_dir_, embedded_settings());
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
