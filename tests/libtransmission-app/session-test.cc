// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint>
#include <ctime> // std::time()
#include <functional>
#include <future>
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

#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/session-id.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/transmission.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/session.h"
#include "libtransmission-app/torrent-keys.h"
#include "libtransmission-app/torrent-list.h"
#include "libtransmission-app/torrent.h"

#include "mock-rpc-server.h"
#include "test-fixtures.h"

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

TEST(AppSessionTest, inhibitsSleepOnlyWhenLocalActiveAndEnabled)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);

    auto session = TestSession{ prefs, rpc };
    EXPECT_FALSE(session.should_inhibit_sleep()); // unknown type, nothing active

    session.set_session_type(Session::Type::Local); // a local daemon
    EXPECT_FALSE(session.should_inhibit_sleep()); // still nothing active

    session.set_busy(true);
    EXPECT_TRUE(session.should_inhibit_sleep()); // local + busy + enabled

    // toggling the preference is honored immediately
    prefs.set(TR_KEY_inhibit_desktop_hibernation, false);
    EXPECT_FALSE(session.should_inhibit_sleep());
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);
    EXPECT_TRUE(session.should_inhibit_sleep());

    // a remote session never inhibits, even with active transfers
    session.set_session_type(Session::Type::Remote);
    EXPECT_FALSE(session.should_inhibit_sleep());

    // going non-local also forgets the busy state, so returning to a local
    // session starts clean instead of inheriting the earlier activity
    session.set_session_type(Session::Type::Local);
    EXPECT_FALSE(session.should_inhibit_sleep());
}

// App Nap should be inhibited only when the session runs in THIS process; a
// remote-control UI (local or remote daemon) can be throttled while idle.
TEST(AppSessionTest, inhibitsNapOnlyForEmbeddedSession)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    EXPECT_FALSE(session.should_inhibit_nap()); // unknown type

    session.set_session_type(Session::Type::Embedded);
    EXPECT_TRUE(session.should_inhibit_nap()); // we host the session

    session.set_session_type(Session::Type::Local);
    EXPECT_FALSE(session.should_inhibit_nap()); // local daemon, another process

    session.set_session_type(Session::Type::Remote);
    EXPECT_FALSE(session.should_inhibit_nap()); // remote daemon
}

// message_level applies to this process's logger rather than being
// sent over RPC, so it works without any session at all.
TEST(AppSessionTest, appliesMessageLevelToTheProcessLogger)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const old_level = tr_logGetLevel();
    auto const new_level = old_level == TR_LOG_DEBUG ? TR_LOG_INFO : TR_LOG_DEBUG;

    prefs.set(TR_KEY_message_level, new_level);
    EXPECT_EQ(new_level, tr_logGetLevel());

    tr_logSetLevel(old_level);
}

// Importing only covers the session's own settings; an app pref that
// happens to be in the imported map must be left alone.
TEST(AppSessionTest, importingSettingsIgnoresAppPrefs)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const old_statusbar = prefs.get<bool>(TR_KEY_show_statusbar);

    auto settings = tr_variant::Map{ 2U };
    settings.insert_or_assign(TR_KEY_show_statusbar, !old_statusbar);
    settings.insert_or_assign(TR_KEY_peer_limit_global, 123);
    session.import_session_settings(settings);

    EXPECT_EQ(old_statusbar, prefs.get<bool>(TR_KEY_show_statusbar));
    EXPECT_EQ(123U, prefs.get<size_t>(TR_KEY_peer_limit_global));
}

// A session_get response reports the blocklist info;
// a response without it keeps the info from before.
TEST(AppSessionTest, importingSettingsReadsBlocklistInfo)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    EXPECT_FALSE(session.blocklist_size().has_value());
    EXPECT_FALSE(session.blocklist_date().has_value());

    auto settings = tr_variant::Map{ 2U };
    settings.insert_or_assign(TR_KEY_blocklist_size, 42);
    settings.insert_or_assign(TR_KEY_blocklist_date, 1700000000);
    session.import_session_settings(settings);
    session.import_session_settings(tr_variant::Map{});

    EXPECT_EQ(session.blocklist_size(), 42);
    // Compare the counts, not the sys_seconds.
    // Printing a sys_seconds on failure needs std::to_chars for floating point, which old macOS lacks.
    auto const date = session.blocklist_date();
    ASSERT_TRUE(date.has_value());
    EXPECT_EQ(date->time_since_epoch().count(), 1700000000);

    session.clear_session_info();
    EXPECT_FALSE(session.blocklist_size().has_value());
    EXPECT_FALSE(session.blocklist_date().has_value());
}

[[nodiscard]] tr_variant::Map make_stats_map(uint64_t const uploaded, uint64_t const downloaded, uint64_t const session_count)
{
    auto map = tr_variant::Map{ 3U };
    map.insert_or_assign(TR_KEY_uploaded_bytes, uploaded);
    map.insert_or_assign(TR_KEY_downloaded_bytes, downloaded);
    map.insert_or_assign(TR_KEY_session_count, session_count);
    return map;
}

TEST(AppSessionTest, importingStatsReadsBothRunsAndBusy)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);
    auto session = TestSession{ prefs, rpc };
    session.set_session_type(Session::Type::Local);

    auto updates = 0;
    auto const tag = session.stats_updated.connect_scoped([&updates] { ++updates; });

    auto args = tr_variant::Map{ 3U };
    args.insert_or_assign(TR_KEY_current_stats, make_stats_map(300U, 100U, 1U));
    args.insert_or_assign(TR_KEY_cumulative_stats, make_stats_map(9000U, 3000U, 7U));
    args.insert_or_assign(TR_KEY_busy, true);
    session.import_session_stats(args);

    EXPECT_EQ(1, updates);
    EXPECT_EQ(300U, session.stats().uploadedBytes);
    EXPECT_EQ(100U, session.stats().downloadedBytes);
    EXPECT_EQ(1U, session.stats().sessionCount);
    EXPECT_EQ(3.0, session.stats().ratio.to_double());
    EXPECT_EQ(9000U, session.cumulative_stats().uploadedBytes);
    EXPECT_EQ(7U, session.cumulative_stats().sessionCount);
    EXPECT_EQ(3.0, session.cumulative_stats().ratio.to_double());
    EXPECT_TRUE(session.should_inhibit_sleep());

    // a reply without `busy` leaves it alone
    auto idle = tr_variant::Map{ 1U };
    idle.insert_or_assign(TR_KEY_current_stats, make_stats_map(301U, 100U, 1U));
    session.import_session_stats(idle);
    EXPECT_EQ(2, updates);
    EXPECT_EQ(301U, session.stats().uploadedBytes);
    EXPECT_EQ(7U, session.cumulative_stats().sessionCount);
    EXPECT_TRUE(session.should_inhibit_sleep());

    idle.insert_or_assign(TR_KEY_busy, false);
    session.import_session_stats(idle);
    EXPECT_FALSE(session.should_inhibit_sleep());
}

TEST(AppSessionTest, importingInfoReadsTheVersionAndType)
{
#ifdef __sun
    // tr_session_id::is_local() relies on file locking that Solaris lacks
    GTEST_SKIP();
#endif

    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    EXPECT_TRUE(std::empty(session.session_version()));

    auto updates = 0;
    auto const tag = session.session_updated.connect_scoped([&updates] { ++updates; });

    // a session id is local while this machine holds its lock file
    auto const local_id = tr_session_id{ [] { return std::time(nullptr); } };
    auto args = tr_variant::Map{ 3U };
    args.insert_or_assign(TR_KEY_version, "4.2.0"sv);
    args.insert_or_assign(TR_KEY_session_id, local_id.sv());
    args.insert_or_assign(TR_KEY_peer_limit_global, 123);
    session.import_session_info(args);

    EXPECT_EQ(1, updates);
    EXPECT_EQ("4.2.0", session.session_version());
    EXPECT_TRUE(session.is_local_filesystem());
    EXPECT_FALSE(session.is_embedded());
    EXPECT_EQ(123U, prefs.get<size_t>(TR_KEY_peer_limit_global));

    args.insert_or_assign(TR_KEY_session_id, "not-a-local-session"sv);
    session.import_session_info(args);
    EXPECT_FALSE(session.is_local_filesystem());

    // without a session id the type is unknown, and a reply without a version keeps the old one
    session.set_session_type(Session::Type::Local);
    session.import_session_info(tr_variant::Map{});
    EXPECT_EQ(3, updates);
    EXPECT_FALSE(session.is_local_filesystem());
    EXPECT_FALSE(session.is_embedded());
    EXPECT_EQ("4.2.0", session.session_version());

    session.clear_session_info();
    EXPECT_TRUE(std::empty(session.session_version()));
}

// Sync tests against a real embedded session.
class AppSessionSyncTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        auto settings = tr_sessionGetDefaultSettings();
        for (auto const key : { TR_KEY_dht_enabled, TR_KEY_lpd_enabled, TR_KEY_port_forwarding_enabled, TR_KEY_utp_enabled }) {
            settings.insert_or_assign(key, false);
        }

        session_ = tr_sessionInit(sandbox_dir(), false, settings);
    }

    void TearDown() override
    {
        tr_sessionClose(session_, 0.5);
        session_ = nullptr;
    }

    tr_session* session_ = nullptr;
};

TEST_F(AppSessionSyncTest, syncsCorePrefsToTheSessionOverRpc)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const new_limit = size_t{ 111 };
    ASSERT_NE(new_limit, tr_sessionGetPeerLimit(session_));

    prefs.set(TR_KEY_peer_limit_global, new_limit);
    EXPECT_TRUE(loop.pump_until([this]() { return tr_sessionGetPeerLimit(session_) == new_limit; }));
}

// session_set ignores a `uint16_t` setting unless it arrives as an int.
TEST_F(AppSessionSyncTest, syncsUint16PrefsToTheSessionOverRpc)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const new_limit = uint16_t{ 45 };
    ASSERT_NE(new_limit, tr_sessionGetIdleLimit(session_));

    prefs.set(TR_KEY_idle_seeding_limit, new_limit);
    EXPECT_TRUE(loop.pump_until([this]() { return tr_sessionGetIdleLimit(session_) == new_limit; }));
}

TEST_F(AppSessionSyncTest, appliesRpcServerPrefsWithTheCApi)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    auto const new_port = 54321;
    ASSERT_NE(new_port, tr_sessionGetRPCPort(session_));
    prefs.set(TR_KEY_rpc_port, new_port);
    EXPECT_TRUE(loop.pump_until([this]() { return tr_sessionGetRPCPort(session_) == new_port; }));

    prefs.set(TR_KEY_rpc_username, "alice"s);
    EXPECT_TRUE(loop.pump_until([this]() { return tr_sessionGetRPCUsername(session_) == "alice"; }));
}

TEST_F(AppSessionSyncTest, importingSettingsDoesNotEchoBackToTheSession)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    auto const session_limit = uint16_t{ 99 };
    tr_sessionSetPeerLimit(session_, session_limit);
    ASSERT_TRUE(loop.pump_until([this]() { return tr_sessionGetPeerLimit(session_) == session_limit; }));

    // an imported value lands in prefs but must not be pushed to the session
    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_limit_global, 250);
    session.import_session_settings(settings);
    EXPECT_EQ(250U, prefs.get<size_t>(TR_KEY_peer_limit_global));

    // give any stray session_set echo a chance to land before checking
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(session_limit, tr_sessionGetPeerLimit(session_));

    // ...and syncing works again once the import is done
    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    EXPECT_TRUE(loop.pump_until([this]() { return tr_sessionGetPeerLimit(session_) == 111U; }));
}

// With a random port on start, the session's settings map still holds the configured port;
// the bound one is only available from the session itself. The map's port must not
// land in Prefs on the way, or every import would announce two port changes.
TEST_F(AppSessionSyncTest, importingSettingsReadsTheBoundPeerPortWhenEmbedded)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    auto const bound_port = uint16_t{ 51999 };
    tr_sessionSetPeerPort(session_, bound_port);
    ASSERT_TRUE(loop.pump_until([this]() { return tr_sessionGetPeerPort(session_) == bound_port; }));

    auto port_changes = 0;
    auto const tag = prefs.observe_changes([&port_changes](tr_quark const key) {
        if (key == TR_KEY_peer_port) {
            ++port_changes;
        }
    });

    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_port, 12345);
    session.import_session_settings(settings);
    EXPECT_EQ(bound_port, prefs.get<uint16_t>(TR_KEY_peer_port));
    EXPECT_EQ(1, port_changes);
}

TEST_F(AppSessionSyncTest, embeddedSettingsRequireAnEmbeddedSession)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    EXPECT_FALSE(session.embedded_settings().has_value());

    session.set_embedded_session(session_);
    auto const settings = session.embedded_settings();
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(settings->contains(TR_KEY_download_dir));
}

TEST_F(AppSessionSyncTest, importingSettingsReadsRpcServerSettingsWhenEmbedded)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    tr_sessionSetRPCUsername(session_, "alice");
    ASSERT_TRUE(loop.pump_until([this]() { return tr_sessionGetRPCUsername(session_) == "alice"; }));

    auto const rpc_port = uint16_t{ 54321 };
    tr_sessionSetRPCPort(session_, rpc_port);
    ASSERT_TRUE(loop.pump_until([this]() { return tr_sessionGetRPCPort(session_) == rpc_port; }));

    // session_get responses omit the RPC server's settings,
    // so import must read them from the session directly
    session.import_session_settings(tr_variant::Map{});
    EXPECT_EQ("alice", prefs.get<std::string>(TR_KEY_rpc_username));
    EXPECT_EQ(rpc_port, prefs.get<uint16_t>(TR_KEY_rpc_port));
}

// The recently-used paths come from the session on request,
// since adding a torrent changes them without a settings-changed notice.
TEST_F(AppSessionSyncTest, fetchesRecentPathsFromTheSession)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const download_dir = sandbox_dir() + "/dl";
    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(LIBTRANSMISSION_TEST_ASSETS_DIR "/debian-11.2.0-amd64-DVD-1.iso.torrent"));
    builder.set_download_dir(download_dir);
    builder.set_paused(true);
    ASSERT_NE(nullptr, tr_torrentNew(&builder, nullptr));

    auto paths = std::vector<std::string>{};
    auto replied = std::atomic<bool>{ false };
    session.fetch_recent_paths(TR_KEY_recent_download_paths, [&paths, &replied](std::vector<std::string> const& value) {
        paths = value;
        replied = true;
    });

    ASSERT_TRUE(loop.pump_until([&replied]() { return replied.load(); }));
    ASSERT_FALSE(std::empty(paths));
    EXPECT_EQ(download_dir, paths.front());
}

// A session_get queued before our session_set carries the value being replaced;
// importing it would revert the pref until the next poll.
TEST_F(AppSessionSyncTest, importSkipsAKeyWithASessionSetInFlight)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto stale = tr_variant::Map{ 1U };
    stale.insert_or_assign(TR_KEY_peer_limit_global, 50);

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 }); // its reply is queued until pumped
    EXPECT_EQ(1U, session.pending_session_sets(TR_KEY_peer_limit_global));
    session.import_session_settings(stale);
    EXPECT_EQ(111U, prefs.get<size_t>(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop.pump_until([&session] { return session.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
    session.import_session_settings(stale);
    EXPECT_EQ(50U, prefs.get<size_t>(TR_KEY_peer_limit_global));
}

// A spin box stepped twice has two sets in flight; the key stays guarded until both are answered.
TEST_F(AppSessionSyncTest, importSkipsAKeyUntilEverySessionSetIsAnswered)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto stale = tr_variant::Map{ 1U };
    stale.insert_or_assign(TR_KEY_peer_limit_global, 50);

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    prefs.set(TR_KEY_peer_limit_global, size_t{ 112 });
    EXPECT_EQ(2U, session.pending_session_sets(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop.pump_until([&session] { return session.pending_session_sets(TR_KEY_peer_limit_global) == 1U; }));
    session.import_session_settings(stale);
    EXPECT_EQ(112U, prefs.get<size_t>(TR_KEY_peer_limit_global));

    ASSERT_TRUE(loop.pump_until([&session] { return session.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
    session.import_session_settings(stale);
    EXPECT_EQ(50U, prefs.get<size_t>(TR_KEY_peer_limit_global));
}

// A set in flight at stop() is answered with a network error, which still clears its guard.
TEST_F(AppSessionSyncTest, aSessionSetInFlightAtStopStillClearsItsGuard)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    rpc.stop();
    EXPECT_TRUE(loop.pump_until([&session] { return session.pending_session_sets(TR_KEY_peer_limit_global) == 0U; }));
}

// A set sent before the client starts is answered at once, so it can't pin its key.
TEST(AppSessionTest, aSessionSetBeforeStartDoesNotGuardItsKey)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    EXPECT_EQ(0U, session.pending_session_sets(TR_KEY_peer_limit_global));

    auto settings = tr_variant::Map{ 1U };
    settings.insert_or_assign(TR_KEY_peer_limit_global, 50);
    session.import_session_settings(settings);
    EXPECT_EQ(50U, prefs.get<size_t>(TR_KEY_peer_limit_global));
}

// A toolkit's event queue outlives the Session, so a reply may be delivered after it is gone.
TEST_F(AppSessionSyncTest, aReplyToADestroyedSessionDoesNothing)
{
    auto loop = UiLoop{};
    auto rpc = RpcClient{ loop.marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = std::make_unique<TestSession>(prefs, rpc);

    prefs.set(TR_KEY_peer_limit_global, size_t{ 111 });
    session.reset();

    // the reply runs against the dead Session's token
    EXPECT_TRUE(loop.pump_until([this] { return tr_sessionGetPeerLimit(session_) == 111U; }));
    std::ignore = loop.quiet_for(100ms);
}

// Session tests against a real embedded session, its replies pumped through a UiLoop.
class AppSessionRpcTest : public SandboxedTest
{
protected:
    static constexpr auto Debian = "debian-11.2.0-amd64-DVD-1.iso.torrent"sv;
    static constexpr auto Arch = "archlinux-2025.05.01-x86_64.iso.torrent"sv;

    void SetUp() override
    {
        auto settings = tr_sessionGetDefaultSettings();
        for (auto const key : { TR_KEY_dht_enabled, TR_KEY_lpd_enabled, TR_KEY_port_forwarding_enabled, TR_KEY_utp_enabled }) {
            settings.insert_or_assign(key, false);
        }
        settings.insert_or_assign(TR_KEY_download_dir, sandbox_dir());

        core_ = tr_sessionInit(sandbox_dir(), false, settings);
        rpc_.start(core_);
    }

    void TearDown() override
    {
        // let every reply land, including those to the requests the replies sent
        while (!loop_.quiet_for(50ms)) {
        }

        rpc_.stop();
        tr_sessionClose(core_, 0.5);
        core_ = nullptr;
    }

    // adds a paused torrent to the session directly, as another client would
    [[nodiscard]] tr_torrent_id_t add_torrent(std::string_view const filename)
    {
        auto builder = tr_torrent_builder{ core_ };
        EXPECT_TRUE(builder.set_metainfo_from_file(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, filename)));
        builder.set_paused(true);
        auto const* const tor = tr_torrentNew(&builder, nullptr);
        EXPECT_NE(nullptr, tor);
        return tor != nullptr ? tr_torrentId(tor) : tr_torrent_id_t{};
    }

    // Pumps until the session has answered every request sent so far.
    // It answers `torrent_get` and `session_stats` inside exec(), so their replies queue in order.
    void pump_replies()
    {
        auto answered = false;
        rpc_.exec(TR_KEY_session_stats, {}, [&answered](RpcResponse const& /*response*/) { answered = true; });
        ASSERT_TRUE(loop_.pump_until([&answered] { return answered; }));
    }

    // Asks the session for `id`'s `key` and returns `read(row)`, where `row` is
    // nullptr when the session lacks `id`. Some changes, e.g. tr_torrentRemove(),
    // finish on the session thread, so the session is the one to ask.
    template<typename Read>
    [[nodiscard]] auto read_session_row(tr_torrent_id_t const id, tr_quark const key, Read read)
    {
        auto ids = tr_variant::Vector{};
        ids.emplace_back(id);
        auto fields = tr_variant::Vector{};
        fields.emplace_back(tr_variant::unmanaged_string(key));
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_ids, std::move(ids));
        params.try_emplace(TR_KEY_fields, std::move(fields));

        auto promise = std::promise<std::invoke_result_t<Read, tr_variant::Map const*>>{};
        auto future = promise.get_future();
        rpc_.exec(TR_KEY_torrent_get, std::move(params), [&promise, &read](RpcResponse const& response) {
            auto const* const args = response.args ? response.args->get_if<tr_variant::Map>() : nullptr;
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

    [[nodiscard]] int64_t session_status(tr_torrent_id_t const id)
    {
        return read_session_row(id, TR_KEY_status, [](tr_variant::Map const* const row) {
            return row != nullptr ? row->value_if<int64_t>(TR_KEY_status).value_or(-1) : -1;
        });
    }

    // Gives the list a row for `id` that predates every edit. Seeded by hand: `edit_date`
    // has one-second resolution, so a fetch just before an edit could carry the same date.
    void seed_unedited_row(tr_torrent_id_t const id)
    {
        auto header = tr_variant::Vector{};
        header.emplace_back(tr_variant::unmanaged_string(TR_KEY_id));
        header.emplace_back(tr_variant::unmanaged_string(TR_KEY_edit_date));
        header.emplace_back(tr_variant::unmanaged_string(TR_KEY_name));
        auto row = tr_variant::Vector{};
        row.emplace_back(id);
        row.emplace_back(0);
        row.emplace_back("unedited"sv);
        auto table = tr_variant::Vector{};
        table.emplace_back(std::move(header));
        table.emplace_back(std::move(row));
        auto args = tr_variant::Map{ 1U };
        args.try_emplace(TR_KEY_torrents, std::move(table));
        session_.torrents().update(args, TorrentList::Coverage::Partial, std::time(nullptr));
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

    tr_session* core_ = nullptr;
    UiLoop loop_; // before rpc_, whose marshaler it is
    RpcClient rpc_{ loop_.marshaler() };
    Prefs prefs_;
    TestSession session_{ prefs_, rpc_ };
};

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

    tr_torrentRemove(tr_torrentFindFromId(core_, id2), false);
    ASSERT_TRUE(loop_.pump_until([this, id2] { return !session_has(id2); }));

    session_.refresh_active_torrents();
    ASSERT_TRUE(loop_.pump_until([this, id2] { return session_.torrents().find(id2) == nullptr; }));
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
    auto ids = tr_variant::Vector{};
    ids.emplace_back(id);
    auto labels = tr_variant::Vector{};
    labels.emplace_back("linux"sv);
    auto params = tr_variant::Map{ 2U };
    params.try_emplace(TR_KEY_ids, std::move(ids));
    params.try_emplace(TR_KEY_labels, std::move(labels));
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
using AppSessionErrorReplyTest = TransmissionTest;

// An error reply may carry a partial `data.result`; applied as a complete list, it would empty the list.
TEST_F(AppSessionErrorReplyTest, leavesTheListAlone)
{
    static auto constexpr ErrorBody =
        R"({"jsonrpc":"2.0","id":1,"error":{"code":-32602,"message":"bad fields","data":{"result":{"torrents":[]}}}})"sv;
    auto server = MockRpcServer{ /*advertise_tr5=*/true, /*require_auth=*/false, std::string{ ErrorBody } };
    auto loop = UiLoop{}; // before rpc: a late reply calls its marshaler
    auto rpc = RpcClient{ loop.marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    rpc.start(server.url());

    auto const row = make_row(1, kv(TR_KEY_name, "kept"sv));
    session.torrents().update(make_args(make_list(row.clone())), TorrentList::Coverage::Partial, std::time(nullptr));
    ASSERT_EQ(1U, session.torrents().size());

    session.refresh_all_torrents(TorrentKeySet::MainAll);
    std::ignore = exec_and_wait(rpc, loop, TR_KEY_session_stats); // the fetch's reply landed before this one
    loop.drain();
    EXPECT_EQ(1U, session.torrents().size());
}

// A toolkit's event queue outlives the Session, so a torrent_get reply may be delivered after it is gone.
TEST_F(AppSessionRpcTest, aTorrentReplyToADestroyedSessionDoesNothing)
{
    std::ignore = add_torrent(Debian);

    auto session = std::make_unique<TestSession>(prefs_, rpc_);
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
    // a new torrent may wait to verify; start from one that is plainly stopped
    auto const id = add_torrent(Debian);
    ASSERT_TRUE(loop_.pump_until([this, id] { return session_status(id) == TR_STATUS_STOPPED; }));
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this, id] { return has_name(id); }));
    ASSERT_EQ(TR_STATUS_STOPPED, activity(id));

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
    auto const id1 = add_torrent(Debian);
    auto const id2 = add_torrent(Arch);
    ASSERT_TRUE(loop_.pump_until(
        [this, id1, id2] { return session_status(id1) == TR_STATUS_STOPPED && session_status(id2) == TR_STATUS_STOPPED; }));
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));
    ASSERT_EQ(TR_STATUS_STOPPED, activity(id1));
    ASSERT_EQ(TR_STATUS_STOPPED, activity(id2));

    session_.start_all_torrents();
    EXPECT_TRUE(loop_.pump_until(
        [this, id1, id2] { return activity(id1) != TR_STATUS_STOPPED && activity(id2) != TR_STATUS_STOPPED; }));
}

// The `_all` verbs re-fetch every torrent, a complete fetch, so the list also learns of removals.
TEST_F(AppSessionRpcTest, theAllVerbsDropTheTorrentsTheSessionLacks)
{
    auto const kept = add_torrent(Debian);

    for (auto const verb : { &Session::start_all_torrents, &Session::stop_all_torrents }) {
        auto const gone = add_torrent(Arch);
        session_.refresh_all_torrents(TorrentKeySet::MainAll);
        ASSERT_TRUE(loop_.pump_until([this] { return session_.torrents().size() == 2U; }));

        tr_torrentRemove(tr_torrentFindFromId(core_, gone), false);
        ASSERT_TRUE(loop_.pump_until([this, gone] { return !session_has(gone); }));

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
    ASSERT_TRUE(loop_.pump_until([this, id2] { return !session_has(id2); }));

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
    auto files = tr_variant::Vector{};
    files.emplace_back("not-an-index"sv);
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_files_wanted, std::move(files));
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

} // namespace
