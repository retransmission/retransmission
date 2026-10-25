// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <giomm/init.h>
#include <glibmm/init.h>
#include <glibmm/main.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/transmission.h>

#include <libtransmission-app/prefs.h>
#include <libtransmission-app/rpc-client.h>

#include "Session.h"
#include "Utils.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

// The client's Session on an embedded session in a sandbox,
// with peer discovery and port forwarding off.
class GtkSessionTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        Glib::init();
        Gio::init();

        auto settings = tr_sessionGetDefaultSettings();
        for (auto const key : { TR_KEY_dht_enabled, TR_KEY_lpd_enabled, TR_KEY_port_forwarding_enabled, TR_KEY_utp_enabled }) {
            settings.insert_or_assign(key, false);
        }

        session_ = tr_sessionInit(sandbox_dir(), false, settings);
        prefs_ = std::make_unique<tr::app::Prefs>(sandbox_dir());
        rpc_ = std::make_unique<tr::app::RpcClient>(&gtr_run_on_ui_thread);
        core_ = Session::create(session_, *prefs_, *rpc_);
    }

    void TearDown() override
    {
        if (core_->get_session() != nullptr) {
            std::ignore = core_->close();
        }

        // The session's callbacks reach into core_, so close it first, as the client does.
        tr_sessionClose(session_, 0.5);
        session_ = nullptr;
        core_.reset();
    }

    // Spins the main loop, which delivers RPC replies, until `pred` holds or `timeout` passes.
    template<typename Pred>
    [[nodiscard]] static bool wait_for(Pred pred, std::chrono::milliseconds const timeout = 10'000ms)
    {
        auto const context = Glib::MainContext::get_default();
        auto const deadline = std::chrono::steady_clock::now() + timeout;

        while (!pred()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            if (!context->iteration(false)) {
                std::this_thread::sleep_for(5ms);
            }
        }

        return true;
    }

    tr_session* session_ = nullptr;
    std::unique_ptr<tr::app::Prefs> prefs_;
    std::unique_ptr<tr::app::RpcClient> rpc_;
    Glib::RefPtr<Session> core_;
};

} // namespace

// close() returns the session for the caller to tr_sessionClose(), which can take seconds,
// so the nap inhibitor outlives it: the session stays embedded until the Session is destroyed.
TEST_F(GtkSessionTest, staysEmbeddedAfterClose)
{
    EXPECT_TRUE(core_->is_embedded());
    EXPECT_TRUE(core_->should_inhibit_nap());

    EXPECT_EQ(session_, core_->close());
    EXPECT_EQ(nullptr, core_->get_session());
    EXPECT_TRUE(core_->is_embedded());
    EXPECT_TRUE(core_->should_inhibit_nap());
}

// A session pref set through the client reaches the embedded session over RPC.
TEST_F(GtkSessionTest, setPrefSyncsSessionPrefsToTheSession)
{
    auto const new_limit = size_t{ 111 };
    ASSERT_NE(new_limit, tr_sessionGetPeerLimit(session_));

    core_->set_pref(TR_KEY_peer_limit_global, new_limit);
    EXPECT_TRUE(wait_for([this]() { return tr_sessionGetPeerLimit(session_) == new_limit; }));
}

// set_pref() saves settings.json, and the saved copy holds the app pref that was set.
TEST_F(GtkSessionTest, setPrefSavesSettings)
{
    auto const old_value = prefs_->get<bool>(TR_KEY_compact_view);
    core_->set_pref(TR_KEY_compact_view, !old_value);

    auto const reloaded = tr::app::Prefs{ sandbox_dir() };
    EXPECT_EQ(!old_value, reloaded.get<bool>(TR_KEY_compact_view));
}

// Importing copies the session's own values into Prefs without echoing them back.
TEST_F(GtkSessionTest, importEmbeddedSettingsReadsTheSession)
{
    auto const session_limit = uint16_t{ 99 };
    tr_sessionSetPeerLimit(session_, session_limit);
    ASSERT_TRUE(wait_for([this]() { return tr_sessionGetPeerLimit(session_) == session_limit; }));
    ASSERT_NE(session_limit, prefs_->get<size_t>(TR_KEY_peer_limit_global));

    core_->import_embedded_settings();
    EXPECT_EQ(session_limit, prefs_->get<size_t>(TR_KEY_peer_limit_global));
}

// Recent paths arrive on the main loop, and a torrent's download dir is among them.
TEST_F(GtkSessionTest, fetchesRecentPathsOnTheMainLoop)
{
    auto const download_dir = sandbox_dir() + "/dl";
    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(LIBTRANSMISSION_TEST_ASSETS_DIR "/debian-11.2.0-amd64-DVD-1.iso.torrent"));
    builder.set_download_dir(download_dir);
    builder.set_paused(true);
    ASSERT_NE(nullptr, tr_torrentNew(&builder, nullptr));

    auto paths = std::vector<Glib::ustring>{};
    auto replies = 0;
    core_->fetch_recent_paths(TR_KEY_recent_download_paths, [&paths, &replies](std::vector<Glib::ustring> const& value) {
        paths = value;
        ++replies;
    });
    EXPECT_EQ(0, replies); // not before the loop runs

    ASSERT_TRUE(wait_for([&replies]() { return replies > 0; }));
    EXPECT_EQ(1, replies);
    ASSERT_FALSE(paths.empty());
    EXPECT_EQ(download_dir, paths.front().raw());
}
