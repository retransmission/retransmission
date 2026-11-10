// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint>
#include <cstdlib> // EXIT_FAILURE
#include <ctime> // std::time()
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple> // std::ignore
#include <type_traits> // std::invoke_result_t
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <sigslot/signal.hpp>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/session-id.h>
#include <libtransmission/session.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/transmission.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/add-data.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/session.h"
#include "libtransmission-app/torrent-keys.h"
#include "libtransmission-app/torrent-list.h"
#include "libtransmission-app/torrent.h"

#include "mock-rpc-server.h"
#include "test-fixtures.h"
#include "torrent-get-helpers.h"

namespace
{

using namespace std::literals;
using namespace tr::app;

// Run RpcClient continuations inline, for a Session whose client is never
// started: its only reply is the "not connected" error, which then runs inside
// exec() itself, on the test thread. A started client answers on the session
// thread, which UiThreadFunc forbids, so those tests pump a UiLoop instead.
// Not for set_torrents(): `torrent_set_done` would fire inside the call,
// before the caller holds the tag.
[[nodiscard]] RpcClient::UiThreadFunc inline_marshaler()
{
    return [](std::function<void()> fn) {
        fn();
    };
}

// exposes the protected setters so the test can drive the inputs directly
class TestSession : public Session
{
public:
    using Session::clear_session_info;
    using Session::pending_session_sets;
    using Session::Session;
    using Session::set_busy;
    using Session::set_embedded_session;
    using Session::set_session_type;
};

// A Session whose client is never started, so every reply is the inline "not connected" error.
class AppSessionTest : public TransmissionTest
{
protected:
    RpcClient rpc_{ inline_marshaler() };
    Prefs prefs_;
    TestSession session_{ std::string{}, prefs_, rpc_ };
};

TEST_F(AppSessionTest, inhibitsSleepOnlyWhenLocalActiveAndEnabled)
{
    prefs_.set(TR_KEY_inhibit_desktop_hibernation, true);

    EXPECT_FALSE(session_.should_inhibit_sleep()); // unknown type, nothing active

    session_.set_session_type(Session::Type::Local); // a local daemon
    EXPECT_FALSE(session_.should_inhibit_sleep()); // still nothing active

    session_.set_busy(true);
    EXPECT_TRUE(session_.should_inhibit_sleep()); // local + busy + enabled

    // toggling the preference is honored immediately
    prefs_.set(TR_KEY_inhibit_desktop_hibernation, false);
    EXPECT_FALSE(session_.should_inhibit_sleep());
    prefs_.set(TR_KEY_inhibit_desktop_hibernation, true);
    EXPECT_TRUE(session_.should_inhibit_sleep());

    // a remote session never inhibits, even with active transfers
    session_.set_session_type(Session::Type::Remote);
    EXPECT_FALSE(session_.should_inhibit_sleep());

    // going non-local also forgets the busy state, so returning to a local
    // session starts clean instead of inheriting the earlier activity
    session_.set_session_type(Session::Type::Local);
    EXPECT_FALSE(session_.should_inhibit_sleep());
}

// App Nap should be inhibited only when the session runs in THIS process; a
// remote-control UI (local or remote daemon) can be throttled while idle.
TEST_F(AppSessionTest, inhibitsNapOnlyForEmbeddedSession)
{
    EXPECT_FALSE(session_.should_inhibit_nap()); // unknown type

    session_.set_session_type(Session::Type::Embedded);
    EXPECT_TRUE(session_.should_inhibit_nap()); // we host the session

    session_.set_session_type(Session::Type::Local);
    EXPECT_FALSE(session_.should_inhibit_nap()); // local daemon, another process

    session_.set_session_type(Session::Type::Remote);
    EXPECT_FALSE(session_.should_inhibit_nap()); // remote daemon
}

// message_level applies to this process's logger rather than being
// sent over RPC, so it works without any session at all.
TEST_F(AppSessionTest, appliesMessageLevelToTheProcessLogger)
{
    auto const old_level = tr_logGetLevel();
    auto const new_level = old_level == TR_LOG_DEBUG ? TR_LOG_INFO : TR_LOG_DEBUG;

    prefs_.set(TR_KEY_message_level, new_level);
    EXPECT_EQ(new_level, tr_logGetLevel());

    tr_logSetLevel(old_level);
}

// Importing only covers the session's own settings; an app pref that
// happens to be in the imported map must be left alone.
TEST_F(AppSessionTest, importingSettingsIgnoresAppPrefs)
{
    auto const old_statusbar = prefs_.get<bool>(TR_KEY_show_statusbar);

    auto settings = tr_variant::Map{ 2U };
    settings.insert_or_assign(TR_KEY_show_statusbar, !old_statusbar);
    settings.insert_or_assign(TR_KEY_peer_limit_global, 123);
    session_.import_session_settings(settings);

    EXPECT_EQ(old_statusbar, prefs_.get<bool>(TR_KEY_show_statusbar));
    EXPECT_EQ(123U, prefs_.get<size_t>(TR_KEY_peer_limit_global));
}

// A session_get response reports the blocklist info;
// a response without it keeps the info from before.
TEST_F(AppSessionTest, importingSettingsReadsBlocklistInfo)
{
    EXPECT_FALSE(session_.blocklist_size().has_value());
    EXPECT_FALSE(session_.blocklist_date().has_value());

    auto settings = tr_variant::Map{ 2U };
    settings.insert_or_assign(TR_KEY_blocklist_size, 42);
    settings.insert_or_assign(TR_KEY_blocklist_date, 1700000000);
    session_.import_session_settings(settings);
    session_.import_session_settings(tr_variant::Map{});

    EXPECT_EQ(session_.blocklist_size(), 42);
    // Compare the counts, not the sys_seconds.
    // Printing a sys_seconds on failure needs std::to_chars for floating point, which old macOS lacks.
    auto const date = session_.blocklist_date();
    ASSERT_TRUE(date.has_value());
    EXPECT_EQ(date->time_since_epoch().count(), 1700000000);

    session_.clear_session_info();
    EXPECT_FALSE(session_.blocklist_size().has_value());
    EXPECT_FALSE(session_.blocklist_date().has_value());
}

[[nodiscard]] tr_variant::Map make_stats_map(uint64_t const uploaded, uint64_t const downloaded, uint64_t const session_count)
{
    auto map = tr_variant::Map{ 3U };
    map.insert_or_assign(TR_KEY_uploaded_bytes, uploaded);
    map.insert_or_assign(TR_KEY_downloaded_bytes, downloaded);
    map.insert_or_assign(TR_KEY_session_count, session_count);
    return map;
}

TEST_F(AppSessionTest, importingStatsReadsBothRunsAndBusy)
{
    prefs_.set(TR_KEY_inhibit_desktop_hibernation, true);
    session_.set_session_type(Session::Type::Local);

    auto updates = 0;
    auto const tag = session_.stats_updated.connect_scoped([&updates] { ++updates; });

    auto args = tr_variant::Map{ 3U };
    args.insert_or_assign(TR_KEY_current_stats, make_stats_map(300U, 100U, 1U));
    args.insert_or_assign(TR_KEY_cumulative_stats, make_stats_map(9000U, 3000U, 7U));
    args.insert_or_assign(TR_KEY_busy, true);
    session_.import_session_stats(args);

    EXPECT_EQ(1, updates);
    EXPECT_EQ(300U, session_.stats().uploadedBytes);
    EXPECT_EQ(100U, session_.stats().downloadedBytes);
    EXPECT_EQ(1U, session_.stats().sessionCount);
    EXPECT_EQ(3.0, session_.stats().ratio.to_double());
    EXPECT_EQ(9000U, session_.cumulative_stats().uploadedBytes);
    EXPECT_EQ(7U, session_.cumulative_stats().sessionCount);
    EXPECT_EQ(3.0, session_.cumulative_stats().ratio.to_double());
    EXPECT_TRUE(session_.should_inhibit_sleep());

    // a reply without `busy` leaves it alone
    auto idle = tr_variant::Map{ 1U };
    idle.insert_or_assign(TR_KEY_current_stats, make_stats_map(301U, 100U, 1U));
    session_.import_session_stats(idle);
    EXPECT_EQ(2, updates);
    EXPECT_EQ(301U, session_.stats().uploadedBytes);
    EXPECT_EQ(7U, session_.cumulative_stats().sessionCount);
    EXPECT_TRUE(session_.should_inhibit_sleep());

    idle.insert_or_assign(TR_KEY_busy, false);
    session_.import_session_stats(idle);
    EXPECT_FALSE(session_.should_inhibit_sleep());
}

TEST_F(AppSessionTest, importingInfoReadsTheVersionAndType)
{
#ifdef __sun
    // tr_session_id::is_local() relies on file locking that Solaris lacks
    GTEST_SKIP();
#endif

    EXPECT_TRUE(std::empty(session_.session_version()));

    auto updates = 0;
    auto const tag = session_.session_updated.connect_scoped([&updates] { ++updates; });

    // a session id is local while this machine holds its lock file
    auto const local_id = tr_session_id{ [] { return std::time(nullptr); } };
    auto args = tr_variant::Map{ 3U };
    args.insert_or_assign(TR_KEY_version, "4.2.0"sv);
    args.insert_or_assign(TR_KEY_session_id, local_id.sv());
    args.insert_or_assign(TR_KEY_peer_limit_global, 123);
    session_.import_session_info(args);

    EXPECT_EQ(1, updates);
    EXPECT_EQ("4.2.0", session_.session_version());
    EXPECT_TRUE(session_.is_local_filesystem());
    EXPECT_FALSE(session_.is_embedded());
    EXPECT_EQ(123U, prefs_.get<size_t>(TR_KEY_peer_limit_global));

    args.insert_or_assign(TR_KEY_session_id, "not-a-local-session"sv);
    session_.import_session_info(args);
    EXPECT_FALSE(session_.is_local_filesystem());

    // without a session id the type is unknown, and a reply without a version keeps the old one
    session_.set_session_type(Session::Type::Local);
    session_.import_session_info(tr_variant::Map{});
    EXPECT_EQ(3, updates);
    EXPECT_FALSE(session_.is_local_filesystem());
    EXPECT_FALSE(session_.is_embedded());
    EXPECT_EQ("4.2.0", session_.session_version());

    session_.clear_session_info();
    EXPECT_TRUE(std::empty(session_.session_version()));
}

// Session tests against a real embedded session, its replies pumped through a UiLoop.
auto constexpr Debian = "debian-11.2.0-amd64-DVD-1.iso.torrent"sv;
auto constexpr MagnetLink = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"sv;
auto constexpr Arch = "archlinux-2025.05.01-x86_64.iso.torrent"sv;

class AppSessionRpcTest
    : public EmbeddedSessionTest
    , public SessionUnderTest<TestSession>
{
protected:
    static constexpr auto Gimp = "gimp-2.10.32-1-arm64.dmg.torrent"sv;

    void SetUp() override
    {
        EmbeddedSessionTest::SetUp();
        rpc_.start(core_);
    }

    void TearDown() override
    {
        loop_.drain();
        rpc_.stop();
        EmbeddedSessionTest::TearDown();
    }

    // adds a paused torrent to the session directly, as another client would
    [[nodiscard]] tr_torrent_id_t add_torrent(std::string_view const filename, std::string_view const download_dir = {})
    {
        auto builder = tr_torrent_builder{ core_ };
        EXPECT_TRUE(builder.set_metainfo_from_file(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, filename)));
        if (!std::empty(download_dir)) {
            builder.set_download_dir(download_dir);
        }
        builder.set_paused(true);
        auto const* const tor = tr_torrentNew(&builder, nullptr);
        EXPECT_NE(nullptr, tor);

        // wait until the session thread has marked the verify that tr_torrentNew() queued
        auto done_promise = std::promise<void>{};
        auto done_future = done_promise.get_future();
        core_->run_in_session_thread([&done_promise] { done_promise.set_value(); });
        done_future.wait();

        return tor != nullptr ? tr_torrentId(tor) : tr_torrent_id_t{};
    }

    // Copies a test asset into the sandbox and returns the copy's path,
    // for a test that looks at what becomes of the file.
    [[nodiscard]] std::string copy_asset(std::string_view const asset, std::string_view const name = {}) const
    {
        auto path = fmt::format("{:s}/{:s}", sandbox_dir(), std::empty(name) ? asset : name);
        EXPECT_TRUE(tr_sys_path_copy(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, asset), path));
        return path;
    }

    // Pumps until the session has answered every request sent so far.
    // It answers `torrent_get` and `session_stats` inside exec(), so their replies queue in order.
    void pump_replies()
    {
        std::ignore = exec_and_wait(rpc_, loop_, TR_KEY_session_stats);
    }

    // Repeats `refresh` and waits for its reply until `pred` holds, for state the
    // session reaches on its own thread after a request returns, e.g. a new torrent's
    // verify finishing or a removal being marked. Returns false if `timeout` passes first.
    template<typename Refresh, typename Pred>
    [[nodiscard]] bool refresh_until(
        Refresh refresh,
        Pred pred,
        std::chrono::milliseconds const timeout = std::chrono::seconds{ 10 })
    {
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            refresh();
            pump_replies();
            std::ignore = loop_.quiet_for(std::chrono::milliseconds{ 20 }); // give the session thread a turn
        }

        return true;
    }

    // Asks the session for `id`'s `key` and returns `read(row)`, where `row` is
    // nullptr when the session lacks `id`. Some changes, e.g. tr_torrentRemove(),
    // finish on the session thread, so the session is the one to ask.
    template<typename Read>
    [[nodiscard]] auto read_session_row(tr_torrent_id_t const id, tr_quark const key, Read read)
    {
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_ids, make_list(id));
        params.try_emplace(TR_KEY_fields, make_fields({ key }));

        auto promise = std::promise<std::invoke_result_t<Read, tr_variant::Map const*>>{};
        auto future = promise.get_future();
        rpc_.exec(TR_KEY_torrent_get, std::move(params), [&promise, &read](RpcResponse const& response) {
            auto const* const args = response.args_map();
            auto const* const torrents = args != nullptr ? args->find_if<tr_variant::Vector>(TR_KEY_torrents) : nullptr;
            auto const* const row = torrents != nullptr && !std::empty(*torrents) ?
                torrents->front().get_if<tr_variant::Map>() :
                nullptr;
            promise.set_value(read(row));
        });
        return loop_.pump_until_ready(future);
    }

    [[nodiscard]] bool session_has(tr_torrent_id_t const id)
    {
        return read_session_row(id, TR_KEY_id, [](tr_variant::Map const* const row) { return row != nullptr; });
    }

    [[nodiscard]] std::string session_download_dir(tr_torrent_id_t const id)
    {
        return read_session_row(id, TR_KEY_download_dir, [](tr_variant::Map const* const row) {
            auto const dir = row != nullptr ? row->value_if<std::string_view>(TR_KEY_download_dir) : std::nullopt;
            return std::string{ dir.value_or(""sv) };
        });
    }

    // Gives the list a row for `id` that predates every edit. Seeded by hand: `edit_date`
    // has one-second resolution, so a fetch just before an edit could carry the same date.
    void seed_unedited_row(tr_torrent_id_t const id)
    {
        auto row = make_row(id, kv(TR_KEY_edit_date, 0), kv(TR_KEY_name, "unedited"sv));
        session_.torrents().update(make_args(make_list(std::move(row))), TorrentList::Coverage::Partial, std::time(nullptr));
    }

    [[nodiscard]] tr_torrent_activity activity(tr_torrent_id_t const id) const
    {
        auto const* const tor = session_.torrents().find(id);
        return tor != nullptr ? tor->activity() : TR_STATUS_STOPPED;
    }

    [[nodiscard]] bool has_name(tr_torrent_id_t const id) const
    {
        auto const* const tor = session_.torrents().find(id);
        return tor != nullptr && tor->has_name();
    }

    // adds `asset` and waits until the list has it with its name
    void add_and_list(std::string_view const asset)
    {
        auto const n_listed = session_.torrents().size() + 1U;
        session_.add_torrent(AddData{ copy_asset(asset) });
        ASSERT_TRUE(loop_.pump_until([this, n_listed] {
            return session_.torrents().size() == n_listed && session_.torrents()[n_listed - 1U].has_name();
        }));
    }

    std::vector<Session::AddReport> reports_;

private:
    sigslot::scoped_connection report_connection_ = session_.torrent_add_report.connect_scoped(
        [this](Session::AddReport const& report) { reports_.push_back(report); });
};

TEST_F(AppSessionRpcTest, syncsCorePrefsToTheSessionOverRpc)
{
    auto const new_limit = size_t{ 111 };
    ASSERT_NE(new_limit, tr_sessionGetPeerLimit(core_));

    prefs_.set(TR_KEY_peer_limit_global, new_limit);
    EXPECT_TRUE(loop_.pump_until([this]() { return tr_sessionGetPeerLimit(core_) == new_limit; }));
}

// session_set ignores a `uint16_t` setting unless it arrives as an int.
TEST_F(AppSessionRpcTest, syncsUint16PrefsToTheSessionOverRpc)
{
    auto const new_limit = uint16_t{ 45 };
    ASSERT_NE(new_limit, tr_sessionGetIdleLimit(core_));

    prefs_.set(TR_KEY_idle_seeding_limit, new_limit);
    EXPECT_TRUE(loop_.pump_until([this]() { return tr_sessionGetIdleLimit(core_) == new_limit; }));
}

TEST_F(AppSessionRpcTest, appliesRpcServerPrefsWithTheCApi)
{
    session_.set_embedded_session(core_);

    auto const new_port = 54321;
    ASSERT_NE(new_port, tr_sessionGetRPCPort(core_));
    prefs_.set(TR_KEY_rpc_port, new_port);
    EXPECT_TRUE(loop_.pump_until([this]() { return tr_sessionGetRPCPort(core_) == new_port; }));

    prefs_.set(TR_KEY_rpc_username, "alice"s);
    EXPECT_TRUE(loop_.pump_until([this]() { return tr_sessionGetRPCUsername(core_) == "alice"; }));
}

// The fixture owns `core_` and closes it; the Session just lets go of it.
TEST_F(AppSessionRpcTest, stopForgetsASessionItDidNotStart)
{
    session_.set_embedded_session(core_);
    ASSERT_TRUE(session_.embedded_settings());

    session_.stop();
    EXPECT_FALSE(session_.embedded_settings());
    EXPECT_FALSE(session_.is_closing());
}

TEST_F(AppSessionRpcTest, importingSettingsDoesNotEchoBackToTheSession)
{
    session_.set_embedded_session(core_);

    auto const session_limit = uint16_t{ 99 };
    tr_sessionSetPeerLimit(core_, session_limit);
    ASSERT_TRUE(loop_.pump_until([this]() { return tr_sessionGetPeerLimit(core_) == session_limit; }));

    // an imported value lands in prefs but must not be pushed to the session
    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_limit_global, 250);
    session_.import_session_settings(settings);
    EXPECT_EQ(250U, prefs_.get<size_t>(TR_KEY_peer_limit_global));

    // give any stray session_set echo a chance to land before checking
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(session_limit, tr_sessionGetPeerLimit(core_));

    // ...and syncing works again once the import is done
    prefs_.set(TR_KEY_peer_limit_global, size_t{ 111 });
    EXPECT_TRUE(loop_.pump_until([this]() { return tr_sessionGetPeerLimit(core_) == 111U; }));
}

// With a random port on start, the session's settings map still holds the configured port;
// the bound one is only available from the session itself. The map's port must not
// land in Prefs on the way, or every import would announce two port changes.
TEST_F(AppSessionRpcTest, importingSettingsReadsTheBoundPeerPortWhenEmbedded)
{
    session_.set_embedded_session(core_);

    auto const bound_port = uint16_t{ 51999 };
    tr_sessionSetPeerPort(core_, bound_port);
    ASSERT_TRUE(loop_.pump_until([this]() { return tr_sessionGetPeerPort(core_) == bound_port; }));

    auto port_changes = 0;
    auto const tag = prefs_.observe_changes([&port_changes](tr_quark const key) {
        if (key == TR_KEY_peer_port) {
            ++port_changes;
        }
    });

    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_port, 12345);
    session_.import_session_settings(settings);
    EXPECT_EQ(bound_port, prefs_.get<uint16_t>(TR_KEY_peer_port));
    EXPECT_EQ(1, port_changes);
}

TEST_F(AppSessionRpcTest, embeddedSettingsRequireAnEmbeddedSession)
{
    EXPECT_FALSE(session_.embedded_settings().has_value());

    session_.set_embedded_session(core_);
    auto const settings = session_.embedded_settings();
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(settings->contains(TR_KEY_download_dir));
}

TEST_F(AppSessionRpcTest, importingSettingsReadsRpcServerSettingsWhenEmbedded)
{
    session_.set_embedded_session(core_);

    tr_sessionSetRPCUsername(core_, "alice");
    ASSERT_TRUE(loop_.pump_until([this]() { return tr_sessionGetRPCUsername(core_) == "alice"; }));

    auto const rpc_port = uint16_t{ 54321 };
    tr_sessionSetRPCPort(core_, rpc_port);
    ASSERT_TRUE(loop_.pump_until([this]() { return tr_sessionGetRPCPort(core_) == rpc_port; }));

    // session_get responses omit the RPC server's settings,
    // so import must read them from the session directly
    session_.import_session_settings(tr_variant::Map{});
    EXPECT_EQ("alice", prefs_.get<std::string>(TR_KEY_rpc_username));
    EXPECT_EQ(rpc_port, prefs_.get<uint16_t>(TR_KEY_rpc_port));
}

// The recently-used paths come from the session on request,
// since adding a torrent changes them without a settings-changed notice.
TEST_F(AppSessionRpcTest, fetchesRecentPathsFromTheSession)
{
    auto const download_dir = sandbox_dir() + "/dl";
    std::ignore = add_torrent(Debian, download_dir);

    auto paths = std::vector<std::string>{};
    auto replied = std::atomic<bool>{ false };
    session_.fetch_recent_paths(TR_KEY_recent_download_paths, [&paths, &replied](std::vector<std::string> const& value) {
        paths = value;
        replied = true;
    });

    ASSERT_TRUE(loop_.pump_until([&replied]() { return replied.load(); }));
    ASSERT_FALSE(std::empty(paths));
    EXPECT_EQ(download_dir, paths.front());
}

TEST_F(AppSessionRpcTest, fetchesAMagnetLinkFromTheSession)
{
    auto const id = add_torrent(Debian);

    auto link = std::string{};
    session_.fetch_magnet_link(id, [&link](std::string_view const value) { link = value; });
    ASSERT_TRUE(loop_.pump_until([&link] { return !std::empty(link); }));
    EXPECT_TRUE(link.starts_with("magnet:?xt=urn:btih:")) << link;

    // a torrent the session lacks has no link to hand over
    auto called = false;
    session_.fetch_magnet_link(id + 1, [&called](std::string_view const /*value*/) { called = true; });
    pump_replies();
    EXPECT_FALSE(called);
}

// A session_get queued before our session_set carries the value being replaced;
// importing it would revert the pref until the next poll.
TEST_F(AppSessionRpcTest, importSkipsAKeyWithASessionSetInFlight)
{
    auto stale = tr_variant::Map{ 1U };
    stale.insert_or_assign(TR_KEY_peer_limit_global, 50);

    prefs_.set(TR_KEY_peer_limit_global, size_t{ 111 }); // its reply is queued until pumped
    EXPECT_EQ(1U, session_.pending_session_sets(TR_KEY_peer_limit_global));
    session_.import_session_settings(stale);
    EXPECT_EQ(111U, prefs_.get<size_t>(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
    session_.import_session_settings(stale);
    EXPECT_EQ(50U, prefs_.get<size_t>(TR_KEY_peer_limit_global));
}

// A spin box stepped twice has two sets in flight; the key stays guarded until both are answered.
TEST_F(AppSessionRpcTest, importSkipsAKeyUntilEverySessionSetIsAnswered)
{
    auto stale = tr_variant::Map{ 1U };
    stale.insert_or_assign(TR_KEY_peer_limit_global, 50);

    prefs_.set(TR_KEY_peer_limit_global, size_t{ 111 });
    prefs_.set(TR_KEY_peer_limit_global, size_t{ 112 });
    EXPECT_EQ(2U, session_.pending_session_sets(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.pending_session_sets(TR_KEY_peer_limit_global) == 1U; }));
    session_.import_session_settings(stale);
    EXPECT_EQ(112U, prefs_.get<size_t>(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
    session_.import_session_settings(stale);
    EXPECT_EQ(50U, prefs_.get<size_t>(TR_KEY_peer_limit_global));
}

// A set in flight at stop() is answered with a network error, which still clears its guard.
TEST_F(AppSessionRpcTest, aSessionSetInFlightAtStopStillClearsItsGuard)
{
    prefs_.set(TR_KEY_peer_limit_global, size_t{ 111 });
    rpc_.stop();
    EXPECT_TRUE(loop_.pump_until([this] { return session_.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
}

// A set sent before the client starts is answered at once, so it can't pin its key.
TEST_F(AppSessionTest, aSessionSetBeforeStartDoesNotGuardItsKey)
{
    prefs_.set(TR_KEY_peer_limit_global, size_t{ 111 });
    EXPECT_EQ(0U, session_.pending_session_sets(TR_KEY_peer_limit_global));

    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_limit_global, 50);
    session_.import_session_settings(settings);
    EXPECT_EQ(50U, prefs_.get<size_t>(TR_KEY_peer_limit_global));
}

// A toolkit's event queue outlives the Session, so a reply may be delivered after it is gone.
TEST_F(AppSessionRpcTest, aReplyToADestroyedSessionDoesNothing)
{
    auto prefs = Prefs{};
    auto session = std::make_unique<TestSession>(std::string{}, prefs, rpc_);

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    session.reset();

    // the reply runs against the dead Session's token
    EXPECT_TRUE(loop_.pump_until([this] { return tr_sessionGetPeerLimit(core_) == 111U; }));
    std::ignore = loop_.quiet_for(100ms);
}

TEST_F(AppSessionRpcTest, refreshAllTorrentsFetchesEveryTorrent)
{
    auto const id1 = add_torrent(Debian);
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U; }));
    EXPECT_TRUE(has_name(id1));

    // a listed refresh only reaches the torrents it names
    auto const id2 = add_torrent(Arch);
    session_.refresh_torrents({ id1 }, TorrentKeySet::MainStats);
    pump_replies();
    EXPECT_EQ(1U, session_.torrents().size());

    session_.refresh_all_torrents(TorrentKeySet::MainStats);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));
    EXPECT_NE(nullptr, session_.torrents().find(id2));
}

TEST_F(AppSessionRpcTest, refreshActiveTorrentsIsPartialAndAppliesRemoved)
{
    auto const id1 = add_torrent(Debian);
    auto const id2 = add_torrent(Arch);
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));

    // the session marks the removal on its own thread, so one poll may be too early to carry it
    tr_torrentRemove(tr_torrentFindFromId(core_, id2), false);
    ASSERT_TRUE(refresh_until(
        [this] { session_.refresh_active_torrents(); },
        [this, id2] { return session_.torrents().find(id2) == nullptr; }));
    EXPECT_NE(nullptr, session_.torrents().find(id1));
}

TEST_F(AppSessionRpcTest, refreshTorrentsWithNoIdsSendsNothing)
{
    session_.refresh_torrents({}, TorrentKeySet::MainStats);
    EXPECT_TRUE(loop_.quiet_for(50ms));
}

// A stats-only row has no name, so the list asks for its info; the Session fetches it.
TEST_F(AppSessionRpcTest, aTorrentThatNeedsInfoGetsMainAll)
{
    auto need_info = TorrentList::Ids{};
    auto const tag = session_.torrents().changed.connect_scoped([&need_info](TorrentList::Changes const& changes) {
        need_info.insert(std::end(need_info), std::begin(changes.need_info), std::end(changes.need_info));
    });

    auto const id = add_torrent(Debian);
    session_.refresh_all_torrents(TorrentKeySet::MainStats);
    EXPECT_TRUE(loop_.pump_until([this, id] { return has_name(id); }));
    EXPECT_EQ(TorrentList::Ids{ id }, need_info);
}

// `labels` is in `MainAll` but not `MainStats`, so only the Session's reaction to `edited` can fetch it.
TEST_F(AppSessionRpcTest, anEditedTorrentGetsMainAll)
{
    auto const id = add_torrent(Debian);

    // another client labels the torrent
    auto params = tr_variant::Map{ 2U };
    params.try_emplace(TR_KEY_ids, make_list(id));
    params.try_emplace(TR_KEY_labels, make_list("linux"sv));
    rpc_.exec(TR_KEY_torrent_set, std::move(params), {});

    // the list's copy predates that edit
    seed_unedited_row(id);
    ASSERT_TRUE(has_name(id));
    ASSERT_TRUE(std::empty(session_.torrents().find(id)->labels()));

    session_.refresh_torrents({ id }, TorrentKeySet::MainStats);
    EXPECT_TRUE(loop_.pump_until([this, id] {
        auto const& fetched = session_.torrents().find(id)->labels();
        return std::size(fetched) == 1U && fetched.front() == "linux";
    }));
}

// TransmissionTest's suite setup brings up networking (Winsock on Windows) before the mock server binds.
class AppSessionErrorReplyTest
    : public TransmissionTest
    , public SessionUnderTest<TestSession>
{
};

// An error reply may carry a partial `data.result`; applied as a complete list, it would empty the list.
TEST_F(AppSessionErrorReplyTest, leavesTheListAlone)
{
    static auto constexpr ErrorBody =
        R"({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"bad fields","data":{"result":{"torrents":[]}}}})"sv;
    auto server = MockRpcServer{ /*advertise_tr5=*/true, /*require_auth=*/false, std::string{ ErrorBody } };
    rpc_.start(server.url());

    auto const row = make_row(1, kv(TR_KEY_name, "kept"sv));
    session_.torrents().update(make_args(make_list(row.clone())), TorrentList::Coverage::Partial, std::time(nullptr));
    ASSERT_EQ(1U, session_.torrents().size());

    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    std::ignore = exec_and_wait(rpc_, loop_, TR_KEY_session_stats); // the fetch's reply landed before this one
    loop_.drain();
    EXPECT_EQ(1U, session_.torrents().size());
}

// A toolkit's event queue outlives the Session, so a torrent_get reply may be delivered after it is gone.
TEST_F(AppSessionRpcTest, aTorrentReplyToADestroyedSessionDoesNothing)
{
    std::ignore = add_torrent(Debian);

    auto session = std::make_unique<TestSession>(std::string{}, prefs_, rpc_);
    session->refresh_all_torrents(TorrentKeySet::MainAll);
    session.reset();

    // the reply runs against the dead Session's token
    pump_replies();
}

TEST_F(AppSessionRpcTest, verbsWithNoIdsSendNothing)
{
    for (
        auto const verb : { &Session::start_torrents,
                            &Session::start_torrents_now,
                            &Session::stop_torrents,
                            &Session::verify_torrents,
                            &Session::reannounce_torrents,
                            &Session::queue_move_top,
                            &Session::queue_move_up,
                            &Session::queue_move_down,
                            &Session::queue_move_bottom }) {
        (session_.*verb)({});
    }
    session_.remove_torrents({}, false);
    session_.set_torrent_location({}, sandbox_dir(), false);
    session_.rename_torrent_path({}, "old", "new");

    auto done = size_t{};
    auto const tag = session_.torrent_set_done.connect_scoped([&done](Session::Tag /*tag*/) { ++done; });
    EXPECT_EQ(Session::Tag{}, session_.set_torrents({}, TR_KEY_peer_limit, 7));

    EXPECT_TRUE(loop_.quiet_for(50ms));
    EXPECT_EQ(0U, done);
}

// No poller exists, so only the verb's own re-fetch can update the list.
TEST_F(AppSessionRpcTest, startReFetchesItsTorrents)
{
    // a new torrent may verify first; start from one the list sees plainly stopped
    auto const id = add_torrent(Debian);
    ASSERT_TRUE(refresh_until(
        [this] { session_.refresh_all_torrents(TorrentKeySet::MainAll); },
        [this, id] { return has_name(id) && activity(id) == TR_STATUS_STOPPED; }));

    session_.start_torrents({ id });
    EXPECT_TRUE(loop_.pump_until([this, id] { return activity(id) != TR_STATUS_STOPPED; }));
}

// The list starts empty each time, so the row can only come from the verb's re-fetch.
TEST_F(AppSessionRpcTest, everyVerbReFetchesItsTorrents)
{
    auto const id = add_torrent(Debian);

    for (
        auto const verb : { &Session::stop_torrents,
                            &Session::verify_torrents,
                            &Session::reannounce_torrents,
                            &Session::queue_move_top,
                            &Session::queue_move_up,
                            &Session::queue_move_down,
                            &Session::queue_move_bottom,
                            &Session::start_torrents_now }) {
        session_.torrents().clear();
        (session_.*verb)({ id, id });
        // the stats-only row needs info, so a `MainAll` fetch follows; let it land before the next clear()
        EXPECT_TRUE(loop_.pump_until([this, id] { return has_name(id); }));
    }
}

TEST_F(AppSessionRpcTest, startAllReachesEveryTorrent)
{
    // new torrents may verify first; start from two the list sees plainly stopped
    auto const id1 = add_torrent(Debian);
    auto const id2 = add_torrent(Arch);
    ASSERT_TRUE(refresh_until(
        [this] { session_.refresh_all_torrents(TorrentKeySet::MainAll); },
        [this, id1, id2] {
            return session_.torrents().size() == 2U && activity(id1) == TR_STATUS_STOPPED && activity(id2) == TR_STATUS_STOPPED;
        }));

    // A torrent that is still verifying starts when its check ends, which can be after
    // the verb's own re-fetch, so ask again until both have started.
    session_.start_all_torrents();
    EXPECT_TRUE(refresh_until(
        [this] { session_.refresh_all_torrents(TorrentKeySet::MainStats); },
        [this, id1, id2] { return activity(id1) != TR_STATUS_STOPPED && activity(id2) != TR_STATUS_STOPPED; }));
}

// The `_all` verbs re-fetch every torrent, a complete fetch, so the list also learns of removals.
TEST_F(AppSessionRpcTest, theAllVerbsDropTheTorrentsTheSessionLacks)
{
    auto const kept = add_torrent(Debian);

    // a torrent per verb: the first `gone` may still be closing when the second is added
    for (
        auto const& [verb, filename] :
        { std::pair{ &Session::start_all_torrents, Arch }, std::pair{ &Session::stop_all_torrents, Gimp } }) {
        auto const gone = add_torrent(filename);
        session_.refresh_all_torrents(TorrentKeySet::MainAll);
        ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));

        tr_torrentRemove(tr_torrentFindFromId(core_, gone), false);

        (session_.*verb)();
        EXPECT_TRUE(loop_.pump_until([this, gone] { return session_.torrents().find(gone) == nullptr; }));
        EXPECT_NE(nullptr, session_.torrents().find(kept));
    }
}

// A complete re-fetch answered after stop() is a network error, so it removes nothing.
TEST_F(AppSessionRpcTest, startAllAfterStopRemovesNothing)
{
    auto const id2 = add_torrent(Arch);
    std::ignore = add_torrent(Debian);
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));

    tr_torrentRemove(tr_torrentFindFromId(core_, id2), false);

    // the start's reply is the only one in flight; running it sends the re-fetch
    session_.start_all_torrents();
    ASSERT_TRUE(loop_.pump_one(10s));
    rpc_.stop();

    std::ignore = loop_.quiet_for(50ms);
    EXPECT_EQ(2U, session_.torrents().size());
}

TEST_F(AppSessionRpcTest, queueMoveReFetchesThePosition)
{
    auto const id1 = add_torrent(Debian);
    auto const id2 = add_torrent(Arch);
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));
    ASSERT_LT(session_.torrents().find(id1)->queue_position(), session_.torrents().find(id2)->queue_position());

    session_.queue_move_top({ id2 });
    EXPECT_TRUE(loop_.pump_until([this, id2] { return session_.torrents().find(id2)->queue_position() == 0U; }));
}

TEST_F(AppSessionRpcTest, removeDropsTheRowsAtOnce)
{
    auto const id1 = add_torrent(Debian);
    auto const id2 = add_torrent(Arch);
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));

    session_.remove_torrents({ id2 }, false);
    EXPECT_TRUE(loop_.pump_until([this, id2] { return session_.torrents().find(id2) == nullptr; }));
    EXPECT_NE(nullptr, session_.torrents().find(id1));
    EXPECT_FALSE(session_has(id2));
}

// An add dialog's pending torrent stays out of the list until the user adds it.
TEST_F(AppSessionRpcTest, aWithheldTorrentIsListedOnceReleased)
{
    auto const listed = add_torrent(Debian);
    auto const pending = add_torrent(Arch);
    session_.withhold_torrent(pending);

    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this, listed] { return session_.torrents().find(listed) != nullptr; }));
    EXPECT_EQ(nullptr, session_.torrents().find(pending));

    session_.release_torrent(pending);
    EXPECT_TRUE(loop_.pump_until([this, pending] { return session_.torrents().find(pending) != nullptr; }));
}

// The session moves asynchronously and marks the torrent edited when done; `edited` fetches `MainAll`.
TEST_F(AppSessionRpcTest, setLocationReachesTheListThroughEdited)
{
    auto const id = add_torrent(Debian);
    seed_unedited_row(id);

    auto const location = fmt::format("{:s}/b", sandbox_dir());
    session_.set_torrent_location({ id }, location, false);
    ASSERT_TRUE(loop_.pump_until([this, id, &location] { return session_download_dir(id) == location; }));
    EXPECT_NE(location, session_.torrents().find(id)->download_dir().sv());

    // `download_dir` is in `MainAll`, not `MainStats`
    session_.refresh_torrents({ id }, TorrentKeySet::MainStats);
    EXPECT_TRUE(loop_.pump_until([this, id, &location] { return session_.torrents().find(id)->download_dir() == location; }));
}

TEST_F(AppSessionRpcTest, renameFailureFiresTheSignal)
{
    auto const id = add_torrent(Debian);

    auto failures = std::vector<Session::RenameError>{};
    auto const tag = session_.torrent_rename_failed.connect_scoped(
        [&failures](Session::RenameError const& error) { failures.push_back(error); });

    session_.rename_torrent_path({ id }, "no-such", "x");
    ASSERT_TRUE(loop_.pump_until([&failures] { return !std::empty(failures); }));
    EXPECT_TRUE(loop_.quiet_for(50ms));
    ASSERT_EQ(1U, std::size(failures));
    auto const& failure = failures.front();
    EXPECT_EQ(Session::Ids{ id }, failure.ids);
    EXPECT_EQ("no-such", failure.old_path);
    EXPECT_EQ("x", failure.new_name);
    EXPECT_FALSE(std::empty(failure.errmsg));
    EXPECT_FALSE(failure.network_error);
    EXPECT_EQ(nullptr, session_.torrents().find(id));
}

// `name` is in `Rename` but not `MainStats`, and the list's row is seeded, so only the re-fetch updates it.
TEST_F(AppSessionRpcTest, renameReFetchesTheTorrent)
{
    auto const id = add_torrent(Debian);
    seed_unedited_row(id);

    auto failures = size_t{};
    auto const tag = session_.torrent_rename_failed.connect_scoped(
        [&failures](Session::RenameError const& /*error*/) { ++failures; });

    session_.rename_torrent_path({ id }, "debian-11.2.0-amd64-DVD-1.iso", "renamed.iso");
    EXPECT_TRUE(loop_.pump_until([this, id] { return session_.torrents().find(id)->name() == "renamed.iso"; }));
    EXPECT_EQ(0U, failures);
}

TEST_F(AppSessionRpcTest, addTorrentListsTheNewTorrent)
{
    session_.add_torrent(AddData{ copy_asset(Arch) });
    EXPECT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U && session_.torrents()[0].has_name(); }));
}

TEST_F(AppSessionRpcTest, addTorrentDisposesOfTheSourceFileOnceAdded)
{
    struct LocalTest {
        std::string_view asset;
        bool trash_source;
        bool from_watch_dir;
        bool source_stays;
        bool renamed;
    };

    auto const tests = std::array{
        LocalTest{ Debian, false, false, true, false },
        LocalTest{ Arch, true, false, false, false },
        LocalTest{ Gimp, false, true, false, true },
        LocalTest{ "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv, true, true, false, false },
    };

    auto n_listed = size_t{};
    for (auto const& test : tests) {
        auto add = AddData{ copy_asset(test.asset) };
        add.trash_source = test.trash_source;
        add.from_watch_dir = test.from_watch_dir;
        auto const source = add.source;

        session_.add_torrent(std::move(add));
        ++n_listed;
        ASSERT_TRUE(loop_.pump_until([this, n_listed] { return session_.torrents().size() == n_listed; }));

        EXPECT_EQ(test.source_stays, tr_sys_path_exists(source)) << test.asset;
        EXPECT_EQ(test.renamed, tr_sys_path_exists(fmt::format("{:s}.added", source))) << test.asset;
    }
}

TEST_F(AppSessionRpcTest, aCreatedTorrentKeepsItsFileAndSeedsItsData)
{
    prefs_.set(TR_KEY_trash_original_torrent_files, true);
    prefs_.set(TR_KEY_show_options_window, true);

    auto prompts = 0;
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    auto const source = copy_asset(Arch);
    auto const data_dir = fmt::format("{:s}/data", sandbox_dir());
    session_.add_created_torrent(source, fmt::format("{:s}/what-it-shares", data_dir));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U && session_.torrents()[0].has_name(); }));
    EXPECT_EQ(data_dir, session_.torrents()[0].download_dir());
    EXPECT_TRUE(tr_sys_path_exists(source));
    EXPECT_EQ(0, prompts);
}

TEST_F(AppSessionRpcTest, addTorrentReportsADuplicateAndDisposesOfItsSourceFile)
{
    add_and_list(Arch);
    auto const hash_string = std::string{ session_.torrents()[0].hash_string().sv() };

    auto add = AddData{ copy_asset(Arch, "again.torrent"sv) };
    add.trash_source = true;
    auto const source = add.source;
    session_.add_torrent(std::move(add));

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    ASSERT_EQ(1U, std::size(reports_));
    auto const& duplicates = reports_.front().duplicates;
    ASSERT_EQ(1U, std::size(duplicates));
    EXPECT_EQ(hash_string, duplicates.front().hash_string);
    EXPECT_EQ(std::string_view{ hash_string }.substr(0U, 7U), duplicates.front().short_hash());
    EXPECT_EQ("again"sv, duplicates.front().name);
    EXPECT_TRUE(std::empty(reports_.front().failed));
    EXPECT_FALSE(tr_sys_path_exists(source));
}

TEST_F(AppSessionRpcTest, aWatchDirDuplicateIsNotReported)
{
    add_and_list(Arch);

    auto add = AddData{ copy_asset(Arch, "again.torrent"sv) };
    add.from_watch_dir = true;
    auto const renamed = fmt::format("{:s}.added", add.source);
    session_.add_torrent(std::move(add));

    ASSERT_TRUE(loop_.pump_until([&renamed] { return tr_sys_path_exists(renamed); }));
    loop_.drain();
    EXPECT_TRUE(std::empty(reports_));
}

TEST_F(AppSessionRpcTest, aPromptedDuplicateIsReportedInsteadOfPrompting)
{
    add_and_list(Arch);

    auto prompts = 0;
    auto const prompt_tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    prefs_.set(TR_KEY_trash_original_torrent_files, true);
    auto const source = copy_asset(Arch, "again.torrent"sv);
    session_.request_add(AddData{ source }, Session::AddPrompt::Always);

    EXPECT_EQ(0, prompts);
    EXPECT_FALSE(tr_sys_path_exists(source));
    EXPECT_TRUE(std::empty(reports_)); // not inside the call

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    EXPECT_EQ(1U, std::size(reports_.front().duplicates));
}

// e.g. a user who opens several listed torrents at once
TEST_F(AppSessionRpcTest, aBurstOfPromptedDuplicatesMakesOneReport)
{
    add_and_list(Arch);
    add_and_list(Debian);

    session_.request_add(AddData{ copy_asset(Arch, "a.torrent"sv) }, Session::AddPrompt::Always);
    session_.request_add(AddData{ copy_asset(Debian, "b.torrent"sv) }, Session::AddPrompt::Always);
    loop_.drain();

    ASSERT_EQ(1U, std::size(reports_));
    EXPECT_EQ(2U, std::size(reports_.front().duplicates));
}

TEST_F(AppSessionRpcTest, aBatchOfRepliesMakesOneReport)
{
    add_and_list(Arch);
    add_and_list(Debian);

    session_.add_torrent(AddData{ copy_asset(Arch, "a.torrent"sv) });
    session_.add_torrent(AddData{ copy_asset(Debian, "b.torrent"sv) });
    session_.add_torrent(AddData{});
    loop_.drain();

    ASSERT_EQ(1U, std::size(reports_));
    EXPECT_EQ(2U, std::size(reports_.front().duplicates));
    EXPECT_EQ(1U, std::size(reports_.front().failed));
}

// e.g. a folder of files that was dropped twice
TEST_F(AppSessionRpcTest, aReportListsEachEntryOnceInNameOrder)
{
    add_and_list(Arch);
    add_and_list(Debian);

    // names that only sort this way if case is ignored, and one that differs from another only by case
    auto const zeta = copy_asset(Arch, "Zeta.torrent"sv);
    auto const zeta_lower = copy_asset(Arch, "zeta.torrent"sv);
    auto const alpha = copy_asset(Debian, "alpha.torrent"sv);
    auto const junk = [this](std::string_view const name) {
        auto path = fmt::format("{:s}/{:s}", sandbox_dir(), name);
        EXPECT_TRUE(tr_file_save(path, "this is not a torrent"sv));
        return path;
    };
    auto const upper = junk("B.torrent"sv);
    auto const lower = junk("a.torrent"sv);

    for (auto const& source : { zeta, upper, alpha, zeta_lower, lower, zeta, upper }) {
        session_.add_torrent(AddData{ source });
    }
    loop_.drain();

    ASSERT_EQ(1U, std::size(reports_));
    auto const& report = reports_.front();
    ASSERT_EQ(3U, std::size(report.duplicates));
    EXPECT_EQ("alpha"sv, report.duplicates[0].name);
    EXPECT_EQ("Zeta"sv, report.duplicates[1].name);
    EXPECT_EQ("zeta"sv, report.duplicates[2].name);
    ASSERT_EQ(2U, std::size(report.failed));
    EXPECT_EQ(lower, report.failed[0].name);
    EXPECT_EQ(upper, report.failed[1].name);
}

TEST_F(AppSessionRpcTest, aBatchOfWatchDirDuplicatesReportsNothing)
{
    add_and_list(Arch);
    add_and_list(Debian);

    for (auto const asset : { Arch, Debian }) {
        auto add = AddData{ copy_asset(asset, fmt::format("again-{:s}", asset)) };
        add.from_watch_dir = true;
        session_.add_torrent(std::move(add));
    }
    loop_.drain();

    EXPECT_TRUE(std::empty(reports_));
}

TEST_F(AppSessionRpcTest, anAddFromBeforeAStopIsNotReported)
{
    add_and_list(Arch);

    session_.request_add(AddData{ copy_asset(Arch, "a.torrent"sv) }, Session::AddPrompt::Always); // a duplicate, dropped now
    session_.add_torrent(AddData{}); // a failure, replied to after the stop
    session_.stop();
    loop_.drain();

    EXPECT_TRUE(std::empty(reports_));

    // nor does it hold up or join the next batch: with no session, this add fails at once
    session_.add_torrent(AddData{});
    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    ASSERT_EQ(1U, std::size(reports_));
    EXPECT_EQ(1U, std::size(reports_.front().failed));
    EXPECT_TRUE(std::empty(reports_.front().duplicates));
}

TEST_F(AppSessionRpcTest, aPromptedWatchDirDuplicateIsNeitherReportedNorPrompted)
{
    add_and_list(Arch);

    auto prompts = 0;
    auto const prompt_tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    prefs_.set(TR_KEY_trash_original_torrent_files, false);
    auto add = AddData{ copy_asset(Arch, "again.torrent"sv) };
    add.from_watch_dir = true;
    auto const source = add.source;
    session_.request_add(std::move(add), Session::AddPrompt::Always);
    loop_.drain();

    EXPECT_EQ(0, prompts);
    EXPECT_TRUE(std::empty(reports_));
    EXPECT_FALSE(tr_sys_path_exists(source));
    EXPECT_TRUE(tr_sys_path_exists(fmt::format("{:s}.added", source)));
}

TEST_F(AppSessionRpcTest, addTorrentReportsWhatCanNotBeAdded)
{
    session_.add_torrent(AddData{});

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    ASSERT_EQ(1U, std::size(reports_.front().failed));
    EXPECT_FALSE(std::empty(reports_.front().failed.front().errmsg));
}

TEST_F(AppSessionRpcTest, aPromptedUrlIsFetchedByAnEmbeddedSession)
{
    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Arch), contents));
    auto const benc = std::string{ std::data(contents), std::size(contents) };

    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get(benc);

    auto prompted = std::optional<AddData>{};
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompted](AddData const& add, AddOptions const& /*options*/) { prompted = add; });

    session_.request_add(AddData{ fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port()) }, Session::AddPrompt::Always);

    ASSERT_TRUE(loop_.pump_until([&prompted] { return prompted.has_value(); }));
    EXPECT_EQ(AddData::Type::Metainfo, prompted->type);
    EXPECT_EQ(benc, prompted->metainfo);
}

TEST_F(AppSessionRpcTest, resolveAddReturnsAFileUnchangedInsideTheCall)
{
    auto const add = AddData{ copy_asset(Arch) };
    auto const wanted = std::make_shared<int const>();

    auto resolved = std::optional<std::optional<AddData>>{};
    session_.resolve_add(add, wanted, [&resolved](std::optional<AddData> result) { resolved = std::move(result); });

    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->has_value());
    EXPECT_EQ(AddData::Type::File, (*resolved)->type);
    EXPECT_EQ(add.source, (*resolved)->source);
    EXPECT_EQ(add.metainfo, (*resolved)->metainfo);
}

TEST_F(AppSessionRpcTest, resolveAddFetchesAUrl)
{
    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Arch), contents));
    auto const benc = std::string{ std::data(contents), std::size(contents) };

    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get(benc);
    auto const url = fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port());
    auto const wanted = std::make_shared<int const>();

    auto resolved = std::optional<std::optional<AddData>>{};
    session_.resolve_add(AddData{ url }, wanted, [&resolved](std::optional<AddData> result) { resolved = std::move(result); });

    ASSERT_TRUE(loop_.pump_until([&resolved] { return resolved.has_value(); }));
    ASSERT_TRUE(resolved->has_value());
    EXPECT_EQ(AddData::Type::Metainfo, (*resolved)->type);
    EXPECT_EQ(benc, (*resolved)->metainfo);
    EXPECT_EQ(url, (*resolved)->source);
}

TEST_F(AppSessionRpcTest, resolveAddDropsAListedTorrent)
{
    add_and_list(Arch);

    auto const wanted = std::make_shared<int const>();
    auto resolved = std::optional<std::optional<AddData>>{};
    session_.resolve_add(AddData{ copy_asset(Arch, "again.torrent"sv) }, wanted, [&resolved](std::optional<AddData> result) {
        resolved = std::move(result);
    });

    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(resolved->has_value());
    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    EXPECT_EQ(1U, std::size(reports_.front().duplicates));
}

// e.g. a prompted URL that turns out to be a torrent the session has, opened along with a listed .torrent
TEST_F(AppSessionRpcTest, resolveAddReportsAFetchedTorrentThatIsListed)
{
    add_and_list(Arch);
    add_and_list(Debian);

    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Arch), contents));
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get(std::string{ std::data(contents), std::size(contents) });

    // dropped inside the call; its report must wait for the fetch that starts next
    session_.request_add(AddData{ copy_asset(Debian, "b.torrent"sv) }, Session::AddPrompt::Always);

    auto const wanted = std::make_shared<int const>();
    auto resolved = std::optional<std::optional<AddData>>{};
    session_.resolve_add(
        AddData{ fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port()) },
        wanted,
        [&resolved](std::optional<AddData> result) { resolved = std::move(result); });

    ASSERT_TRUE(loop_.pump_until([&resolved] { return resolved.has_value(); }));
    EXPECT_FALSE(resolved->has_value());
    loop_.drain();
    ASSERT_EQ(1U, std::size(reports_));
    EXPECT_EQ(2U, std::size(reports_.front().duplicates));
}

// e.g. a user who accepts the prompt while its URL is fetched, and whose add lists the torrent first
TEST_F(AppSessionRpcTest, resolveAddDropsAFetchNobodyWantsAnyMore)
{
    add_and_list(Arch);

    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Arch), contents));
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get(std::string{ std::data(contents), std::size(contents) });

    auto wanted = std::make_shared<int const>();
    auto resolved = false;
    session_.resolve_add(
        AddData{ fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port()) },
        wanted,
        [&resolved](std::optional<AddData> const& /*result*/) { resolved = true; });
    wanted.reset();

    ASSERT_TRUE(loop_.pump_until([&server] { return !std::empty(server.request_bodies()); }));
    loop_.drain(std::chrono::milliseconds{ 200 });
    EXPECT_FALSE(resolved);
    EXPECT_TRUE(std::empty(reports_));
}

// The user will want the file to try again.
TEST_F(AppSessionRpcTest, aRejectedAddReportsTheErrorAndKeepsTheSourceFile)
{
    auto const source = fmt::format("{:s}/corrupt.torrent", sandbox_dir());
    ASSERT_TRUE(tr_file_save(source, "this is not a torrent"sv));

    auto add = AddData{ source };
    add.trash_source = true;
    session_.add_torrent(std::move(add));

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    ASSERT_EQ(1U, std::size(reports_.front().failed));
    EXPECT_FALSE(std::empty(reports_.front().failed.front().errmsg));
    EXPECT_TRUE(tr_sys_path_exists(source));
}

TEST_F(AppSessionRpcTest, aFileThatIsNotATorrentIsReportedInsteadOfPrompting)
{
    auto const source = fmt::format("{:s}/corrupt.torrent", sandbox_dir());
    ASSERT_TRUE(tr_file_save(source, "this is not a torrent"sv));

    auto prompts = 0;
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    EXPECT_TRUE(session_.request_add(AddData{ source }, Session::AddPrompt::Always));

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    EXPECT_EQ(1U, std::size(reports_.front().failed));
    EXPECT_EQ(0, prompts);
    EXPECT_TRUE(tr_sys_path_exists(source));
}

// e.g. the file a prompt's user names as another source
TEST_F(AppSessionRpcTest, resolveAddDropsAFileThatIsNotATorrent)
{
    auto const source = fmt::format("{:s}/corrupt.torrent", sandbox_dir());
    ASSERT_TRUE(tr_file_save(source, "this is not a torrent"sv));

    auto const wanted = std::make_shared<int const>();
    auto resolved = std::optional<std::optional<AddData>>{};
    session_.resolve_add(AddData{ source }, wanted, [&resolved](std::optional<AddData> result) {
        resolved = std::move(result);
    });

    ASSERT_TRUE(resolved.has_value());
    EXPECT_FALSE(resolved->has_value());
    EXPECT_TRUE(std::empty(reports_)); // not inside the call

    ASSERT_TRUE(loop_.pump_until([this] { return !std::empty(reports_); }));
    ASSERT_EQ(1U, std::size(reports_.front().failed));
    EXPECT_EQ(source, reports_.front().failed.front().name);
    EXPECT_FALSE(std::empty(reports_.front().failed.front().errmsg));
}

// `peer_limit` is in `DetailStats` but not `MainAll`, so only the set's re-fetch can bring it.
TEST_F(AppSessionRpcTest, setTorrentsFiresItsTagAfterTheReFetchLands)
{
    auto const id = add_torrent(Debian);

    auto done = std::vector<Session::Tag>{};
    auto peer_limit_at_done = std::optional<uint16_t>{};
    auto const tag = session_.torrent_set_done.connect_scoped([this, id, &done, &peer_limit_at_done](Session::Tag const set) {
        done.push_back(set);
        if (auto const* const tor = session_.torrents().find(id); tor != nullptr) {
            peer_limit_at_done = tor->peer_limit();
        }
    });

    auto const set = session_.set_torrents({ id, id }, TR_KEY_peer_limit, 7);
    EXPECT_NE(Session::Tag{}, set);
    ASSERT_TRUE(loop_.pump_until([&done] { return !std::empty(done); }));
    EXPECT_EQ(std::vector{ set }, done);
    EXPECT_EQ(7U, peer_limit_at_done);
}

TEST_F(AppSessionRpcTest, setTorrentsIssuesADistinctTagPerCall)
{
    auto const id = add_torrent(Debian);

    auto done = std::vector<Session::Tag>{};
    auto const tag = session_.torrent_set_done.connect_scoped([&done](Session::Tag const set) { done.push_back(set); });

    auto const first = session_.set_torrents({ id }, TR_KEY_peer_limit, 7);
    auto const second = session_.set_torrents({ id }, TR_KEY_peer_limit, 8);
    EXPECT_NE(first, second);
    EXPECT_NE(Session::Tag{}, first);
    EXPECT_NE(Session::Tag{}, second);

    ASSERT_TRUE(loop_.pump_until([&done] { return std::size(done) == 2U; }));
    std::ignore = loop_.quiet_for(50ms); // the new row's `MainAll` fetch lands here
    std::ranges::sort(done);
    EXPECT_EQ((std::vector{ std::min(first, second), std::max(first, second) }), done);
}

// A failed set ends the chain before the re-fetch, so the list stays empty.
TEST_F(AppSessionRpcTest, setTorrentsFiresItsTagOnFailure)
{
    auto const id = add_torrent(Debian);

    auto done = std::vector<Session::Tag>{};
    auto const tag = session_.torrent_set_done.connect_scoped([&done](Session::Tag const set) { done.push_back(set); });

    // file indices must be integers
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_files_wanted, make_list("not-an-index"sv));
    auto const set = session_.set_torrents({ id }, std::move(params));

    ASSERT_TRUE(loop_.pump_until([&done] { return !std::empty(done); }));
    EXPECT_EQ(std::vector{ set }, done);
    EXPECT_TRUE(loop_.quiet_for(50ms));
    EXPECT_EQ(0U, session_.torrents().size());
}

TEST_F(AppSessionRpcTest, setTorrentsAfterStopFiresItsTag)
{
    auto const id = add_torrent(Debian);

    auto done = std::vector<Session::Tag>{};
    auto const tag = session_.torrent_set_done.connect_scoped([&done](Session::Tag const set) { done.push_back(set); });

    auto const set = session_.set_torrents({ id }, TR_KEY_peer_limit, 7);
    rpc_.stop();

    ASSERT_TRUE(loop_.pump_until([&done] { return !std::empty(done); }));
    EXPECT_EQ(std::vector{ set }, done);
    EXPECT_TRUE(loop_.quiet_for(50ms));
    EXPECT_EQ(0U, session_.torrents().size());
}

TEST_F(AppSessionRpcTest, refreshSessionStatsImportsTheReply)
{
    auto updates = 0;
    auto const tag = session_.stats_updated.connect_scoped([&updates] { ++updates; });

    session_.refresh_session_stats();
    ASSERT_TRUE(loop_.pump_until([&updates] { return updates > 0; }));
    EXPECT_EQ(1, updates);
    EXPECT_GE(session_.stats().sessionCount, 1U);
    EXPECT_GE(session_.cumulative_stats().sessionCount, 1U);
}

TEST_F(AppSessionRpcTest, refreshSessionInfoImportsTheReply)
{
    auto const session_limit = uint16_t{ 99 };
    ASSERT_NE(session_limit, prefs_.get<size_t>(TR_KEY_peer_limit_global));
    tr_sessionSetPeerLimit(core_, session_limit);
    ASSERT_TRUE(loop_.pump_until([this] { return tr_sessionGetPeerLimit(core_) == session_limit; }));

    auto updates = 0;
    auto const tag = session_.session_updated.connect_scoped([&updates] { ++updates; });

    session_.refresh_session_info();
    ASSERT_TRUE(loop_.pump_until([&updates] { return updates > 0; }));
    EXPECT_EQ(1, updates);
    EXPECT_EQ(session_limit, prefs_.get<size_t>(TR_KEY_peer_limit_global));
    EXPECT_FALSE(std::empty(session_.session_version()));
    EXPECT_FALSE(session_.is_embedded());
}

// The session id can't make an embedded session look like a daemon.
TEST_F(AppSessionRpcTest, importingInfoKeepsAnEmbeddedSessionEmbedded)
{
    session_.set_embedded_session(core_);

    auto args = tr_variant::Map{ 1U };
    args.insert_or_assign(TR_KEY_session_id, "not-a-local-session"sv);
    session_.import_session_info(args);
    EXPECT_TRUE(session_.is_embedded());

    session_.import_session_info(tr_variant::Map{});
    EXPECT_TRUE(session_.is_embedded());
}

TEST(AppExitTest, exitProcessAfterEndsTheProcess)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_EXIT(
        {
            exit_process_after(10ms);
            std::this_thread::sleep_for(1min);
        },
        ::testing::ExitedWithCode(EXIT_FAILURE),
        "exiting anyway");
}

// A Session that runs its own session, kept off the network if it's an embedded one
// in the sandbox, and what its signals report.
class AppSessionLifecycleTest : public SandboxedTest
{
protected:
    static constexpr auto ErrorBody = R"({"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"nope"}})"sv;

    void SetUp() override
    {
        tr::settings::save(tr_sessionGetSettingsFilename(sandbox_dir()), offline_settings(sandbox_dir()));
    }

    void set_remote_prefs(std::string_view const host, int const port)
    {
        prefs_.set(TR_KEY_remote_session_enabled, true);
        prefs_.set(TR_KEY_remote_session_host, std::string{ host });
        prefs_.set(TR_KEY_remote_session_port, port);
    }

    void start_on(MockRpcServer const& server)
    {
        set_remote_prefs("127.0.0.1"sv, server.port());
        session_.start();
    }

    // Starts on `server` and finishes the CSRF handshake, so that each later request reaches it once.
    void start_negotiated(MockRpcServer const& server)
    {
        start_on(server);
        ASSERT_TRUE(exec_and_wait(rpc_, loop_, TR_KEY_session_stats).success);
    }

    // The next `torrent_add` that `server` receives, as sent. For use after `start_negotiated()`.
    [[nodiscard]] std::string next_add_body(MockRpcServer const& server)
    {
        auto adds = std::vector<std::string>{};
        auto const found = loop_.pump_until([this, &server, &adds] {
            adds.clear();
            std::ranges::copy_if(server.request_bodies(), std::back_inserter(adds), [](auto const& body) {
                return body.contains("torrent_add") || body.contains("torrent-add");
            });
            return std::size(adds) > n_adds_seen_;
        });
        EXPECT_TRUE(found);
        n_adds_seen_ = std::size(adds);
        return found ? adds.back() : std::string{};
    }

    // What `torrent_add_prompt` fires with once `url` has been requested with a prompt.
    [[nodiscard]] std::optional<AddData> prompted_add_for(std::string const& url)
    {
        auto prompted = std::optional<AddData>{};
        auto const tag = session_.torrent_add_prompt.connect_scoped(
            [&prompted](AddData const& add, AddOptions const& /*options*/) { prompted = add; });

        session_.request_add(AddData{ url }, Session::AddPrompt::Always);
        EXPECT_TRUE(loop_.pump_until([&prompted] { return prompted.has_value(); }));
        return prompted;
    }

    [[nodiscard]] bool pump_until_closed()
    {
        return loop_.pump_until([this] { return !session_.is_closing(); });
    }

    [[nodiscard]] bool pump_until_port_tested()
    {
        return loop_.pump_until([this] { return !session_.port_test_pending(); });
    }

    // adds the test asset `filename` over RPC; true once the session has replied
    [[nodiscard]] bool add_over_rpc(std::string_view const filename, bool const paused)
    {
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_filename, fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, filename));
        params.try_emplace(TR_KEY_paused, paused);
        auto added = false;
        session_.send(TR_KEY_torrent_add, std::move(params), [&added](RpcResponse const& response) {
            EXPECT_TRUE(response.success);
            added = true;
        });
        return loop_.pump_until([&added] { return added; });
    }

    UiLoop loop_;
    RpcClient rpc_{ loop_.marshaler() };
    Prefs prefs_;
    Session session_{ sandbox_dir(), prefs_, rpc_ };

    size_t n_adds_seen_ = 0U;
    int source_changes_ = 0;
    int stops_ = 0;
    int closes_ = 0;
    int contentions_ = 0;
    std::vector<int64_t> updated_;
    std::vector<std::string> failed_;

private:
    sigslot::scoped_connection source_changed_connection_ = session_.source_changed.connect_scoped(
        [this] { ++source_changes_; });
    sigslot::scoped_connection stopped_connection_ = session_.stopped.connect_scoped([this] { ++stops_; });
    sigslot::scoped_connection closed_connection_ = session_.closed.connect_scoped([this] { ++closes_; });
    sigslot::scoped_connection contended_connection_ = session_.config_dir_contended.connect_scoped([this] { ++contentions_; });
    sigslot::scoped_connection updated_connection_ = session_.blocklist_updated.connect_scoped(
        [this](int64_t const n) { updated_.push_back(n); });
    sigslot::scoped_connection failed_connection_ = session_.blocklist_update_failed.connect_scoped(
        [this](std::string_view const errmsg) { failed_.emplace_back(errmsg); });
};

TEST_F(AppSessionLifecycleTest, startsAnEmbeddedSessionWhenPrefsNameNoRemote)
{
    session_.start();

    EXPECT_TRUE(session_.is_embedded());
    EXPECT_TRUE(std::empty(session_.remote_url()));
    EXPECT_EQ(1, source_changes_);
    EXPECT_TRUE(exec_and_wait(rpc_, loop_, TR_KEY_session_stats).success);
}

TEST_F(AppSessionLifecycleTest, anEmbeddedSessionLoadsItsTorrents)
{
    session_.start();
    ASSERT_TRUE(add_over_rpc(Debian, true));

    session_.restart();
    ASSERT_TRUE(pump_until_closed());
    ASSERT_TRUE(session_.is_embedded());

    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    EXPECT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U; }));
}

TEST_F(AppSessionLifecycleTest, startsARemoteSessionAtTheUrlInPrefs)
{
    struct LocalTest {
        std::string_view host;
        bool https = false;
        std::string_view base_path;
        std::string_view expected;
    };

    static auto constexpr Tests = std::array{
        LocalTest{ "example.com"sv, false, "/transmission/"sv, "http://example.com:9091/transmission/rpc"sv },
        LocalTest{ "example.com"sv, true, "/custom/"sv, "https://example.com:9091/custom/rpc"sv },
        LocalTest{ "::1"sv, false, "/transmission/"sv, "http://[::1]:9091/transmission/rpc"sv },
        LocalTest{ "[::1]"sv, false, "/transmission/"sv, "http://[::1]:9091/transmission/rpc"sv },
    };

    prefs_.set(TR_KEY_remote_session_username, "alice"s);
    prefs_.set(TR_KEY_remote_session_requires_authentication, true);

    for (auto const& test : Tests) {
        set_remote_prefs(test.host, 9091);
        prefs_.set(TR_KEY_remote_session_https, test.https);
        prefs_.set(TR_KEY_remote_session_url_base_path, std::string{ test.base_path });

        auto const changes = source_changes_;
        session_.start();
        EXPECT_EQ(test.expected, session_.remote_url());
        EXPECT_FALSE(session_.is_embedded());
        EXPECT_EQ(changes + 1, source_changes_);

        session_.stop();
        EXPECT_TRUE(std::empty(session_.remote_url()));
    }
}

TEST_F(AppSessionLifecycleTest, sendsCredentialsOnlyWhenPrefsRequireThem)
{
    for (auto const requires_auth : { false, true }) {
        auto server = MockRpcServer{ /*advertise_tr5=*/true, /*require_auth=*/requires_auth };
        set_remote_prefs("127.0.0.1"sv, server.port());
        prefs_.set(TR_KEY_remote_session_requires_authentication, requires_auth);
        prefs_.set(TR_KEY_remote_session_username, "alice"s);
        prefs_.set(TR_KEY_remote_session_password, "secret"s);

        session_.start();
        EXPECT_TRUE(exec_and_wait(rpc_, loop_, TR_KEY_session_stats).success);
        EXPECT_EQ(requires_auth, server.saw_authorization());

        session_.stop();
        loop_.drain();
    }
}

TEST_F(AppSessionLifecycleTest, webClientUrlFollowsTheSource)
{
    prefs_.set(TR_KEY_rpc_port, 12345);
    session_.start();
    EXPECT_EQ("http://localhost:12345"sv, session_.web_client_url());
    session_.stop();
    ASSERT_TRUE(pump_until_closed());

    set_remote_prefs("example.com"sv, 9091);
    session_.start();
    EXPECT_EQ("http://example.com:9091/transmission/web/"sv, session_.web_client_url());

    prefs_.set(TR_KEY_remote_session_requires_authentication, true);
    prefs_.set(TR_KEY_remote_session_username, "a b"s);
    prefs_.set(TR_KEY_remote_session_password, "p@ss"s);
    EXPECT_EQ("http://a%20b:p%40ss@example.com:9091/transmission/web/"sv, session_.web_client_url());
}

TEST_F(AppSessionLifecycleTest, aContendedConfigDirStartsNothing)
{
    auto* const holder = tr_sessionInit(sandbox_dir(), false, offline_settings(sandbox_dir()));

    session_.start();
    EXPECT_EQ(1, contentions_);
    EXPECT_EQ(0, source_changes_);
    EXPECT_FALSE(session_.is_embedded());

    tr_sessionClose(holder, 0.5);

    session_.start();
    EXPECT_EQ(1, contentions_);
    EXPECT_EQ(1, source_changes_);
    EXPECT_TRUE(session_.is_embedded());
}

// The close happens on another thread, so stop() returns while it's under way.
TEST_F(AppSessionLifecycleTest, stopForgetsTheSessionNowAndClosesItLater)
{
    session_.start();
    session_.stop();

    EXPECT_FALSE(session_.is_embedded());
    EXPECT_EQ(1, stops_);
    EXPECT_TRUE(session_.is_closing());
    EXPECT_EQ(0, closes_);

    EXPECT_TRUE(pump_until_closed());
    EXPECT_EQ(1, closes_);
}

TEST_F(AppSessionLifecycleTest, stoppingWithoutAnEmbeddedSessionHasNothingToClose)
{
    session_.stop();
    EXPECT_EQ(1, stops_);
    EXPECT_FALSE(session_.is_closing());

    set_remote_prefs("example.com"sv, 9091);
    session_.start();
    session_.stop();
    EXPECT_EQ(2, stops_);
    EXPECT_FALSE(session_.is_closing());

    loop_.drain();
    EXPECT_EQ(0, closes_);
}

// The closing session holds the config dir, so a start must wait or it would find the dir contended.
TEST_F(AppSessionLifecycleTest, aStartDuringACloseWaitsForIt)
{
    session_.start();
    session_.restart();
    EXPECT_FALSE(session_.is_embedded());
    EXPECT_EQ(1, source_changes_);

    EXPECT_TRUE(loop_.pump_until([this] { return source_changes_ == 2; }));
    EXPECT_TRUE(session_.is_embedded());
    EXPECT_EQ(1, closes_);
    EXPECT_EQ(0, contentions_);
}

// A remote session doesn't need the config dir that the closing session holds.
TEST_F(AppSessionLifecycleTest, aRemoteStartDuringACloseDoesNotWait)
{
    session_.start();
    set_remote_prefs("example.com"sv, 9091);
    session_.restart();

    EXPECT_TRUE(session_.is_closing());
    EXPECT_EQ(2, source_changes_);
    EXPECT_FALSE(std::empty(session_.remote_url()));

    EXPECT_TRUE(pump_until_closed());
    EXPECT_EQ(2, source_changes_);
}

TEST_F(AppSessionLifecycleTest, aStopCancelsAStartWaitingOnAClose)
{
    session_.start();
    session_.restart();
    session_.stop();

    EXPECT_TRUE(pump_until_closed());
    loop_.drain();
    EXPECT_EQ(1, source_changes_);
    EXPECT_FALSE(session_.is_embedded());
}

TEST_F(AppSessionLifecycleTest, addTorrentNeverDisposesOfTheSessionsOwnCopy)
{
    session_.start_embedded();

    auto const torrents_dir = fmt::format("{:s}/torrents", session_.config_dir());
    ASSERT_TRUE(tr_sys_dir_create(torrents_dir, TR_SYS_DIR_CREATE_PARENTS, 0777));
    auto const source = fmt::format("{:s}/own.torrent", torrents_dir);
    ASSERT_TRUE(tr_sys_path_copy(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Arch), source));

    auto add = AddData{ source };
    add.trash_source = true;
    session_.add_torrent(std::move(add));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U; }));
    EXPECT_TRUE(tr_sys_path_exists(source));
}

TEST_F(AppSessionLifecycleTest, addTorrentRenamesAWatchDirFileUnderTheConfigDir)
{
    session_.start_embedded();

    auto const watch_dir = fmt::format("{:s}/watch", session_.config_dir());
    ASSERT_TRUE(tr_sys_dir_create(watch_dir, TR_SYS_DIR_CREATE_PARENTS, 0777));
    auto const source = fmt::format("{:s}/watched.torrent", watch_dir);
    ASSERT_TRUE(tr_sys_path_copy(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Debian), source));

    auto add = AddData{ source };
    add.from_watch_dir = true;
    session_.add_torrent(std::move(add));

    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 1U; }));
    EXPECT_FALSE(tr_sys_path_exists(source));
    EXPECT_TRUE(tr_sys_path_exists(fmt::format("{:s}.added", source)));
}

TEST_F(AppSessionLifecycleTest, addTorrentSendsLinksByNameAndFilesByContents)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    session_.add_torrent(AddData{ MagnetLink });
    auto const magnet_body = next_add_body(server);
    EXPECT_TRUE(magnet_body.contains(R"("filename":"magnet:)")) << magnet_body;
    EXPECT_FALSE(magnet_body.contains(R"("metainfo")")) << magnet_body;

    session_.add_torrent(AddData{ fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Debian) });
    auto const file_body = next_add_body(server);
    EXPECT_TRUE(file_body.contains(R"("metainfo")")) << file_body;
    EXPECT_FALSE(file_body.contains(R"("filename")")) << file_body;
}

TEST_F(AppSessionLifecycleTest, addTorrentDefaultsPausedFromThePrefUnlessTold)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    prefs_.set(TR_KEY_start_added_torrents, false);
    session_.add_torrent(AddData{ MagnetLink });
    EXPECT_TRUE(next_add_body(server).contains(R"("paused":true)"));

    prefs_.set(TR_KEY_start_added_torrents, true);
    session_.add_torrent(AddData{ MagnetLink });
    EXPECT_TRUE(next_add_body(server).contains(R"("paused":false)"));

    auto options = AddOptions{};
    options.paused = true;
    session_.add_torrent(AddData{ MagnetLink }, std::move(options));
    EXPECT_TRUE(next_add_body(server).contains(R"("paused":true)"));
}

TEST_F(AppSessionLifecycleTest, addTorrentSendsOnlyTheOptionsThatAreSet)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    session_.add_torrent(AddData{ MagnetLink });
    auto const bare = next_add_body(server);
    for (auto const* const key : { "download_dir", "bandwidth_priority", "files_unwanted", "priority_low", "priority_high" }) {
        EXPECT_FALSE(bare.contains(key)) << key;
    }

    auto options = AddOptions{};
    options.download_dir = "/some/dir";
    options.bandwidth_priority = TR_PRI_HIGH;
    options.files_unwanted = { 1, 2 };
    options.priority_low = { 3 };
    options.priority_high = { 4 };
    session_.add_torrent(AddData{ MagnetLink }, std::move(options));
    auto const full = next_add_body(server);
    EXPECT_TRUE(full.contains(R"("download_dir":"/some/dir")")) << full;
    EXPECT_TRUE(full.contains(R"("bandwidth_priority":1)")) << full;
    EXPECT_TRUE(full.contains(R"("files_unwanted":[1,2])")) << full;
    EXPECT_TRUE(full.contains(R"("priority_low":[3])")) << full;
    EXPECT_TRUE(full.contains(R"("priority_high":[4])")) << full;
}

TEST_F(AppSessionLifecycleTest, requestAddPromptsOrAddsAsItIsTold)
{
    struct LocalTest {
        bool show_options_window;
        Session::AddPrompt prompt;
        bool expect_prompt;
    };

    static auto constexpr Tests = std::array{
        LocalTest{ false, Session::AddPrompt::FromPrefs, false },
        LocalTest{ true, Session::AddPrompt::FromPrefs, true },
        LocalTest{ false, Session::AddPrompt::Always, true },
        LocalTest{ true, Session::AddPrompt::Never, false },
    };

    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    auto prompts = 0;
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    for (auto const& test : Tests) {
        prompts = 0;
        prefs_.set(TR_KEY_show_options_window, test.show_options_window);
        EXPECT_TRUE(session_.request_add(AddData{ MagnetLink }, test.prompt));

        EXPECT_EQ(test.expect_prompt ? 1 : 0, prompts);
        if (!test.expect_prompt) {
            EXPECT_TRUE(next_add_body(server).contains("magnet:"));
        }
    }
}

TEST_F(AppSessionLifecycleTest, requestAddAppliesTheTrashPref)
{
    auto trash_source = std::optional<bool>{};
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&trash_source](AddData const& add, AddOptions const& /*options*/) { trash_source = add.trash_source; });

    for (auto const trash_pref : { false, true }) {
        trash_source.reset();
        prefs_.set(TR_KEY_trash_original_torrent_files, trash_pref);
        session_.request_add(AddData{ MagnetLink }, Session::AddPrompt::Always);
        EXPECT_EQ(trash_pref, trash_source);
    }
}

TEST_F(AppSessionLifecycleTest, requestAddPassesItsOptionsOn)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    auto preset = std::optional<AddOptions>{};
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&preset](AddData const& /*add*/, AddOptions const& options) { preset = options; });

    prefs_.set(TR_KEY_start_added_torrents, true);
    auto options = AddOptions{};
    options.paused = true;
    options.download_dir = "/somewhere/else";

    session_.request_add(AddData{ MagnetLink }, Session::AddPrompt::Always, options);
    ASSERT_TRUE(preset.has_value());
    EXPECT_EQ(true, preset->paused);
    EXPECT_EQ("/somewhere/else", preset->download_dir);

    // a prompt gets what the request left unset from the prefs
    preset.reset();
    session_.request_add(AddData{ MagnetLink }, Session::AddPrompt::Always);
    ASSERT_TRUE(preset.has_value());
    EXPECT_EQ(false, preset->paused);
    EXPECT_FALSE(std::empty(preset->download_dir));
    EXPECT_EQ(prefs_.get<std::string>(TR_KEY_download_dir), preset->download_dir);

    session_.request_add(AddData{ MagnetLink }, Session::AddPrompt::Never, options);
    auto const body = next_add_body(server);
    EXPECT_TRUE(body.contains("/somewhere/else"));
    EXPECT_TRUE(body.contains(R"("paused":true)"));
}

TEST_F(AppSessionLifecycleTest, requestAddOnlyPromptsForNothingWhenAskedTo)
{
    auto prompts = 0;
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    prefs_.set(TR_KEY_show_options_window, true);
    EXPECT_FALSE(session_.request_add(AddData{}));
    EXPECT_EQ(0, prompts);

    EXPECT_TRUE(session_.request_add(AddData{}, Session::AddPrompt::Always));
    EXPECT_EQ(1, prompts);
}

TEST_F(AppSessionLifecycleTest, aPromptedUrlBringsItsMetainfo)
{
    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, Debian), contents));
    auto const benc = std::string{ std::data(contents), std::size(contents) };

    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get(benc);

    auto const url = fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port());
    auto const prompted = prompted_add_for(url);

    ASSERT_TRUE(prompted.has_value());
    EXPECT_EQ(AddData::Type::Metainfo, prompted->type);
    EXPECT_EQ(benc, prompted->metainfo);
    EXPECT_EQ(url, prompted->source);
}

// Resolving adds nothing, and a remote session would be sent the whole file.
TEST_F(AppSessionLifecycleTest, resolveAddSendsNoFileThatIsNotATorrent)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    auto const source = fmt::format("{:s}/corrupt.torrent", sandbox_dir());
    ASSERT_TRUE(tr_file_save(source, "this is not a torrent"sv));

    auto n_failed = size_t{};
    auto const tag = session_.torrent_add_report.connect_scoped(
        [&n_failed](Session::AddReport const& report) { n_failed += std::size(report.failed); });

    auto const wanted = std::make_shared<int const>();
    session_.resolve_add(AddData{ source }, wanted, [](std::optional<AddData> const& /*result*/) {});

    ASSERT_TRUE(loop_.pump_until([&n_failed] { return n_failed != 0U; }));
    loop_.drain();
    EXPECT_EQ(1U, n_failed);
    EXPECT_TRUE(std::ranges::none_of(server.request_bodies(), [](auto const& body) {
        return body.contains("torrent_add") || body.contains("torrent-add");
    }));
}

// The session may be able to reach what this client can't.
TEST_F(AppSessionLifecycleTest, aPromptedUrlThatCantBeFetchedStaysAUrl)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };

    auto const url = fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port());
    auto const prompted = prompted_add_for(url);

    ASSERT_TRUE(prompted.has_value());
    EXPECT_EQ(AddData::Type::Url, prompted->type);
    EXPECT_EQ(url, prompted->source);
}

TEST_F(AppSessionLifecycleTest, aUrlFetchedAcrossAStopPromptsNothing)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get("not a torrent");

    auto prompts = 0;
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompts](AddData const& /*add*/, AddOptions const& /*options*/) { ++prompts; });

    auto const url = fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port());
    session_.request_add(AddData{ url }, Session::AddPrompt::Always);
    session_.stop();

    auto done = false;
    rpc_.fetch(url, std::chrono::seconds{ 30 }, [&done](tr_web::FetchResponse const& /*response*/) { done = true; });
    ASSERT_TRUE(loop_.pump_until([&done] { return done; }));
    loop_.drain();

    EXPECT_EQ(0, prompts);
}

TEST_F(AppSessionLifecycleTest, aFetchIsPendingUntilItLands)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get("not a torrent");

    auto changes = 0;
    auto const tag = session_.add_fetch_changed.connect_scoped([&changes] { ++changes; });

    auto const wanted = std::make_shared<int const>();
    auto resolved = false;
    session_.resolve_add(
        AddData{ fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port()) },
        wanted,
        [&resolved](std::optional<AddData> const& /*result*/) { resolved = true; });
    EXPECT_TRUE(session_.add_fetch_pending());
    EXPECT_EQ(1, changes);

    ASSERT_TRUE(loop_.pump_until([&resolved] { return resolved; }));
    EXPECT_FALSE(session_.add_fetch_pending());
    EXPECT_EQ(2, changes);
}

// the session fetches a URL it is sent before it replies
TEST_F(AppSessionLifecycleTest, aUrlSentToTheSessionIsPendingUntilItReplies)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_negotiated(server);

    auto changes = 0;
    auto const tag = session_.add_fetch_changed.connect_scoped([&changes] { ++changes; });

    session_.add_torrent(AddData{ MagnetLink });
    EXPECT_FALSE(session_.add_fetch_pending());

    session_.add_torrent(AddData{ "https://example.com/x.torrent"sv });
    session_.add_torrent(AddData{ "https://example.com/y.torrent"sv }); // a second fetch changes nothing
    EXPECT_TRUE(session_.add_fetch_pending());
    EXPECT_EQ(1, changes);

    EXPECT_TRUE(loop_.pump_until([this] { return !session_.add_fetch_pending(); }));
    EXPECT_EQ(2, changes);
}

TEST_F(AppSessionLifecycleTest, aFetchNobodyWantsStillEndsItsPending)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get("not a torrent");

    auto wanted = std::make_shared<int const>();
    session_.resolve_add(
        AddData{ fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port()) },
        wanted,
        [](std::optional<AddData> const& /*result*/) {});
    wanted.reset();
    EXPECT_TRUE(session_.add_fetch_pending());

    EXPECT_TRUE(loop_.pump_until([this] { return !session_.add_fetch_pending(); }));
}

TEST_F(AppSessionLifecycleTest, aStopEndsAPendingFetch)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    server.serve_on_get("not a torrent");

    auto changes = 0;
    auto const tag = session_.add_fetch_changed.connect_scoped([&changes] { ++changes; });

    auto const url = fmt::format("http://127.0.0.1:{:d}/x.torrent", server.port());
    session_.request_add(AddData{ url }, Session::AddPrompt::Always);
    ASSERT_TRUE(session_.add_fetch_pending());

    session_.stop();
    EXPECT_FALSE(session_.add_fetch_pending());
    EXPECT_EQ(2, changes);

    // a fetch sent after the stop lands after the one sent before it
    auto done = false;
    rpc_.fetch(url, std::chrono::seconds{ 30 }, [&done](tr_web::FetchResponse const& /*response*/) { done = true; });
    ASSERT_TRUE(loop_.pump_until([&done] { return done; }));
    loop_.drain();
    EXPECT_FALSE(session_.add_fetch_pending());
    EXPECT_EQ(2, changes);
}

TEST_F(AppSessionLifecycleTest, startEmbeddedIgnoresTheRemotePrefs)
{
    set_remote_prefs("example.com"sv, 9091);
    session_.start_embedded();

    EXPECT_TRUE(session_.is_embedded());
    EXPECT_TRUE(std::empty(session_.remote_url()));
    EXPECT_EQ(1, source_changes_);
}

// Arch has no trackers or webseeds, so running it stays off the network.
TEST_F(AppSessionLifecycleTest, startEmbeddedCanLoadTheTorrentsPaused)
{
    // whether the session's one torrent is paused, once a fetch brings it back
    auto const fetch_paused = [this]() -> std::optional<bool> {
        session_.refresh_all_torrents(TorrentKeySet::MainAll);
        if (!loop_.pump_until([this] { return session_.torrents().size() == 1U; })) {
            return {};
        }
        return session_.torrents()[0].is_paused();
    };

    session_.start();
    ASSERT_TRUE(add_over_rpc(Arch, false));

    session_.stop();
    ASSERT_TRUE(pump_until_closed());
    session_.start_embedded(true);
    EXPECT_EQ(std::optional{ true }, fetch_paused());

    session_.start_torrents({ session_.torrents()[0].id() });
    ASSERT_TRUE(loop_.pump_until([this] { return !session_.torrents()[0].is_paused(); }));

    // these starts wait for the close
    session_.stop();
    session_.start_embedded(false);
    ASSERT_TRUE(loop_.pump_until([this] { return source_changes_ == 3; }));
    EXPECT_EQ(std::optional{ false }, fetch_paused());

    session_.stop();
    session_.start_embedded(true);
    ASSERT_TRUE(loop_.pump_until([this] { return source_changes_ == 4; }));
    EXPECT_EQ(std::optional{ true }, fetch_paused());
}

TEST_F(AppSessionLifecycleTest, waitForCloseFinishesTheCloseOnce)
{
    session_.start();
    session_.stop();

    session_.wait_for_close();
    EXPECT_FALSE(session_.is_closing());
    EXPECT_EQ(1, closes_);

    loop_.drain(); // the thread's own report of the finished close
    EXPECT_EQ(1, closes_);
}

// The config dir is free again as soon as the destructor returns.
TEST_F(AppSessionLifecycleTest, destroyingASessionFinishesItsClose)
{
    {
        auto started = Session{ sandbox_dir(), prefs_, rpc_ };
        started.start();
        ASSERT_TRUE(started.is_embedded());
    }

    {
        auto closing = Session{ sandbox_dir(), prefs_, rpc_ };
        closing.start();
        ASSERT_TRUE(closing.is_embedded());
        closing.stop();
        ASSERT_TRUE(closing.is_closing());
    }

    loop_.drain();
    session_.start();
    EXPECT_EQ(0, contentions_);
    EXPECT_TRUE(session_.is_embedded());
}

TEST_F(AppSessionLifecycleTest, aBlocklistUpdateReportsTheRuleCountAndRefreshesTheInfo)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true,
                                 /*require_auth=*/false,
                                 R"({"jsonrpc":"2.0","id":1,"result":{"blocklist_size":42}})" };
    start_on(server);

    auto refreshed = false;
    auto const refreshed_connection = session_.session_updated.connect_scoped([&refreshed] { refreshed = true; });
    session_.update_blocklist();

    EXPECT_TRUE(loop_.pump_until([&refreshed] { return refreshed; }));
    EXPECT_EQ(std::vector<int64_t>{ 42 }, updated_);
    EXPECT_TRUE(std::empty(failed_));
    EXPECT_EQ(42, session_.blocklist_size());
}

TEST_F(AppSessionLifecycleTest, aBlocklistUpdateErrorReportsItsMessage)
{
    struct LocalTest {
        bool advertise_tr5 = false;
        std::string_view body;
        std::string_view expected;
    };

    static auto constexpr Tests = std::array{
        LocalTest{ true, ErrorBody, "nope"sv },
        LocalTest{ false, R"({"result":"Not Found","arguments":{}})"sv, "Not Found"sv },
    };

    for (auto const& test : Tests) {
        auto server = MockRpcServer{ test.advertise_tr5, /*require_auth=*/false, std::string{ test.body } };
        start_on(server);
        failed_.clear();

        session_.update_blocklist();

        EXPECT_TRUE(loop_.pump_until([this] { return !std::empty(failed_); }));
        EXPECT_EQ(std::vector<std::string>{ std::string{ test.expected } }, failed_);
        EXPECT_TRUE(std::empty(updated_));

        session_.stop();
        loop_.drain();
    }
}

TEST_F(AppSessionLifecycleTest, aBlocklistUpdateWithoutARuleCountFails)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_on(server);

    session_.update_blocklist();

    EXPECT_TRUE(loop_.pump_until([this] { return !std::empty(failed_); }));
    EXPECT_TRUE(std::empty(updated_));
}

// Its progress UI would otherwise never resolve.
TEST_F(AppSessionLifecycleTest, aStopFailsAPendingBlocklistUpdate)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_on(server);

    session_.update_blocklist();
    session_.stop();

    EXPECT_TRUE(loop_.pump_until([this] { return !std::empty(failed_); }));
    EXPECT_EQ(1U, std::size(failed_));
    EXPECT_TRUE(std::empty(updated_));
}

// Nothing listens on port 1, so the download fails without leaving this machine.
// That failure reaches the session thread only after the close that restart() queues.
TEST_F(AppSessionLifecycleTest, aRestartFailsAPendingEmbeddedBlocklistUpdate)
{
    auto settings = offline_settings(sandbox_dir());
    settings.insert_or_assign(TR_KEY_blocklist_url, "http://127.0.0.1:1/blocklist"sv);
    tr::settings::save(tr_sessionGetSettingsFilename(sandbox_dir()), settings);

    session_.start();
    ASSERT_TRUE(session_.is_embedded());

    session_.update_blocklist();
    session_.restart();

    EXPECT_TRUE(loop_.pump_until([this] { return !std::empty(failed_); }));
    EXPECT_EQ(1U, std::size(failed_));
    EXPECT_TRUE(std::empty(updated_));
}

// The next session never shows the previous one's values, and numbers its torrents from 1 too.
TEST_F(AppSessionLifecycleTest, stopForgetsTheSessionsInfoAndTorrents)
{
    auto server = MockRpcServer{
        /*advertise_tr5=*/true,
        /*require_auth=*/false,
        R"({"jsonrpc":"2.0","id":1,"result":{"blocklist_date":1700000000,"blocklist_size":42,"version":"9.9.9"}})"
    };
    start_on(server);

    auto updates = 0;
    auto const updated_connection = session_.session_updated.connect_scoped([&updates] { ++updates; });
    session_.refresh_session_info();
    ASSERT_TRUE(loop_.pump_until([&updates] { return updates > 0; }));
    EXPECT_EQ(42, session_.blocklist_size());
    EXPECT_TRUE(session_.blocklist_date().has_value());
    EXPECT_EQ("9.9.9"sv, session_.session_version());

    auto row = make_row(1, kv(TR_KEY_name, "kept"sv));
    session_.torrents().update(make_args(make_list(std::move(row))), TorrentList::Coverage::Partial, std::time(nullptr));
    ASSERT_EQ(1U, session_.torrents().size());

    session_.stop();
    EXPECT_EQ(1, stops_);
    EXPECT_FALSE(session_.blocklist_size().has_value());
    EXPECT_FALSE(session_.blocklist_date().has_value());
    EXPECT_TRUE(std::empty(session_.session_version()));
    EXPECT_EQ(0U, session_.torrents().size());
}

TEST_F(AppSessionLifecycleTest, aPortTestReportsEachProtocol)
{
    using Status = Session::PortStatus;

    auto server = MockRpcServer{ /*advertise_tr5=*/true,
                                 /*require_auth=*/false,
                                 R"({"jsonrpc":"2.0","id":1,"result":{"port_is_open":true}})" };
    start_on(server);
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));

    auto changes = 0;
    auto const changed_connection = session_.port_test_changed.connect_scoped([&changes] { ++changes; });
    session_.test_port();
    EXPECT_EQ(1, changes);
    EXPECT_EQ(Status::Checking, session_.port_status(TR_AF_INET));
    EXPECT_EQ(Status::Checking, session_.port_status(TR_AF_INET6));
    EXPECT_TRUE(session_.port_test_pending());

    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(3, changes);
    EXPECT_EQ(Status::Open, session_.port_status(TR_AF_INET));
    EXPECT_EQ(Status::Open, session_.port_status(TR_AF_INET6));
}

TEST_F(AppSessionLifecycleTest, aClosedPortAndAnErrorEachHaveAStatus)
{
    using Status = Session::PortStatus;

    {
        auto server = MockRpcServer{ /*advertise_tr5=*/true,
                                     /*require_auth=*/false,
                                     R"({"jsonrpc":"2.0","id":1,"result":{"port_is_open":false}})" };
        start_on(server);
        session_.test_port();
        EXPECT_TRUE(pump_until_port_tested());
        EXPECT_EQ(Status::Closed, session_.port_status(TR_AF_INET));
        session_.stop();
        loop_.drain();
    }

    auto server = MockRpcServer{ /*advertise_tr5=*/true, /*require_auth=*/false, std::string{ ErrorBody } };
    start_on(server);
    session_.test_port();
    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Status::Error, session_.port_status(TR_AF_INET));
}

TEST_F(AppSessionLifecycleTest, aPortTestWhileOneIsPendingSendsNothing)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_on(server);
    std::ignore = exec_and_wait(rpc_, loop_, TR_KEY_session_stats); // the CSRF handshake, so a request reaches the server once

    session_.test_port();
    session_.test_port();

    EXPECT_TRUE(pump_until_port_tested());
    loop_.drain();

    auto const bodies = server.request_bodies();
    auto const is_port_test = [](std::string const& body) {
        return body.find(R"("port_test")") != std::string::npos;
    };
    EXPECT_EQ(2, std::ranges::count_if(bodies, is_port_test));
}

// An answer that arrives after the change is about the old port.
TEST_F(AppSessionLifecycleTest, aPeerPortChangeForgetsTheStatus)
{
    using Status = Session::PortStatus;

    auto server = MockRpcServer{ /*advertise_tr5=*/true,
                                 /*require_auth=*/false,
                                 R"({"jsonrpc":"2.0","id":1,"result":{"port_is_open":true}})" };
    start_on(server);

    session_.test_port();
    prefs_.set(TR_KEY_peer_port, 12345);
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));
    EXPECT_TRUE(session_.port_test_pending());

    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));

    session_.test_port();
    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Status::Open, session_.port_status(TR_AF_INET));

    prefs_.set(TR_KEY_peer_port, 12346);
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));
}

TEST_F(AppSessionLifecycleTest, forgettingThePortStatusLeavesAPendingTestPending)
{
    using Status = Session::PortStatus;

    auto server = MockRpcServer{ /*advertise_tr5=*/true,
                                 /*require_auth=*/false,
                                 R"({"jsonrpc":"2.0","id":1,"result":{"port_is_open":true}})" };
    start_on(server);

    session_.test_port();
    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Status::Open, session_.port_status(TR_AF_INET));

    auto changes = 0;
    auto const changed_connection = session_.port_test_changed.connect_scoped([&changes] { ++changes; });
    session_.forget_port_status();
    EXPECT_EQ(1, changes);
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));

    session_.test_port();
    session_.forget_port_status();
    EXPECT_TRUE(session_.port_test_pending());
    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Status::Unknown, session_.port_status(TR_AF_INET));
}

TEST_F(AppSessionLifecycleTest, aStopEndsAPendingPortTest)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    start_on(server);

    session_.test_port();
    session_.stop();

    EXPECT_EQ(Session::PortStatus::Unknown, session_.port_status(TR_AF_INET));
    EXPECT_TRUE(pump_until_port_tested());
    EXPECT_EQ(Session::PortStatus::Unknown, session_.port_status(TR_AF_INET));
}

} // namespace
