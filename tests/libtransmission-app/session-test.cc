// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/session.h"

#include "test-fixtures.h"

namespace
{

using namespace std::literals;
using namespace tr::app;

// Run RpcClient continuations inline. These tests poll for observable
// side effects, so they don't need a UI-thread event loop.
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
    using Session::Session;
    using Session::set_embedded_session;
    using Session::set_has_busy_torrents;
    using Session::set_session_type;
};

// Poll for a condition; RPC dispatch and session setters land asynchronously
// on the session thread.
template<typename Pred>
[[nodiscard]] bool wait_for(Pred pred, std::chrono::milliseconds const timeout = 10'000ms)
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        if (pred()) {
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }

        std::this_thread::sleep_for(5ms);
    }
}

TEST(AppSessionTest, inhibitsSleepOnlyWhenLocalActiveAndEnabled)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);

    auto session = TestSession{ prefs, rpc };
    EXPECT_FALSE(session.should_inhibit_sleep()); // unknown type, nothing active

    session.set_session_type(Session::Type::Local); // a local daemon
    EXPECT_FALSE(session.should_inhibit_sleep()); // still nothing active

    session.set_has_busy_torrents(true);
    EXPECT_TRUE(session.should_inhibit_sleep()); // local + active + enabled

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

// Changing a pref that invalidates cached session info, e.g. download_dir
// changing session_get's freespace argument, should request a refresh.
TEST(AppSessionTest, changingDownloadDirRequestsSessionRefresh)
{
    auto rpc = RpcClient{ inline_marshaler() };
    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto refreshes = 0;
    auto const tag = session.session_refresh_needed.connect_scoped([&refreshes]() { ++refreshes; });

    prefs.set(TR_KEY_download_dir, "/some/new/dir"s);
    EXPECT_EQ(1, refreshes);

    prefs.set(TR_KEY_compact_view, true); // an app pref; not session-facing
    EXPECT_EQ(1, refreshes);
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
    auto rpc = RpcClient{ inline_marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };

    auto const new_limit = size_t{ 111 };
    ASSERT_NE(new_limit, tr_sessionGetPeerLimit(session_));

    prefs.set(TR_KEY_peer_limit_global, new_limit);
    EXPECT_TRUE(wait_for([this]() { return tr_sessionGetPeerLimit(session_) == new_limit; }));
}

TEST_F(AppSessionSyncTest, appliesRpcServerPrefsWithTheCApi)
{
    auto rpc = RpcClient{ inline_marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    auto const new_port = 54321;
    ASSERT_NE(new_port, tr_sessionGetRPCPort(session_));
    prefs.set(TR_KEY_rpc_port, new_port);
    EXPECT_TRUE(wait_for([this]() { return tr_sessionGetRPCPort(session_) == new_port; }));

    prefs.set(TR_KEY_rpc_username, "alice"s);
    EXPECT_TRUE(wait_for([this]() { return tr_sessionGetRPCUsername(session_) == "alice"; }));
}

TEST_F(AppSessionSyncTest, importingSettingsDoesNotEchoBackToTheSession)
{
    auto rpc = RpcClient{ inline_marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    auto const session_limit = uint16_t{ 99 };
    tr_sessionSetPeerLimit(session_, session_limit);
    ASSERT_TRUE(wait_for([this]() { return tr_sessionGetPeerLimit(session_) == session_limit; }));

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
    EXPECT_TRUE(wait_for([this]() { return tr_sessionGetPeerLimit(session_) == 111U; }));
}

TEST_F(AppSessionSyncTest, embeddedSettingsRequireAnEmbeddedSession)
{
    auto rpc = RpcClient{ inline_marshaler() };
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
    auto rpc = RpcClient{ inline_marshaler() };
    rpc.start(session_);

    auto prefs = Prefs{};
    auto session = TestSession{ prefs, rpc };
    session.set_embedded_session(session_);

    tr_sessionSetRPCUsername(session_, "alice");
    ASSERT_TRUE(wait_for([this]() { return tr_sessionGetRPCUsername(session_) == "alice"; }));

    // session_get responses omit the RPC server's settings,
    // so import must read them from the session directly
    session.import_session_settings(tr_variant::Map{});
    EXPECT_EQ("alice", prefs.get<std::string>(TR_KEY_rpc_username));
}

} // namespace
