// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
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

#include <libtransmission-app/display-modes.h>
#include <libtransmission-app/prefs.h>
#include <libtransmission-app/rpc-client.h>
#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>

#include "Session.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "TorrentSorter.h"
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

    // Adds a stopped torrent from the test assets to the embedded session, then fetches it into the list.
    [[nodiscard]] tr_torrent_id_t add_listed_torrent(char const* const filename)
    {
        auto builder = tr_torrent_builder{ session_ };
        EXPECT_TRUE(builder.set_metainfo_from_file(std::string{ LIBTRANSMISSION_TEST_ASSETS_DIR "/" } + filename));
        builder.set_download_dir(sandbox_dir());
        builder.set_paused(true);
        auto const* const tor = tr_torrentNew(&builder, nullptr);
        EXPECT_NE(nullptr, tor);
        auto const id = tr_torrentId(tor);

        core_->add_torrent(id, false);
        EXPECT_TRUE(wait_for([this, id] { return core_->torrents().find(id) != nullptr; }));
        return id;
    }

    // the list's object for `id`, or nullptr; the model mirrors the list's rows
    [[nodiscard]] Glib::RefPtr<Torrent> listed(tr_torrent_id_t const id) const
    {
        if (auto const row = core_->torrents().row_of(id); row) {
            return gtr_ptr_dynamic_cast<Torrent>(core_->get_model()->get_object(static_cast<guint>(*row)));
        }
        return {};
    }

    static constexpr auto const* Arch = "archlinux-2025.05.01-x86_64.iso.torrent";
    static constexpr auto const* Debian = "debian-11.2.0-amd64-DVD-1.iso.torrent";

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

// A torrent the session adds shows up in the list once it is fetched.
TEST_F(GtkSessionTest, listsAnAddedTorrentOnceFetched)
{
    EXPECT_EQ(0U, core_->get_model()->get_n_items());

    auto const id = add_listed_torrent(Debian);

    EXPECT_EQ(1U, core_->get_model()->get_n_items());
    auto const torrent = listed(id);
    ASSERT_TRUE(torrent);
    EXPECT_EQ("debian-11.2.0-amd64-DVD-1.iso", torrent->get_name());
}

// A view may still hold a removed torrent's object, which keeps the torrent's last values.
TEST_F(GtkSessionTest, aRemovedTorrentsObjectKeepsItsValues)
{
    auto const id = add_listed_torrent(Debian);
    auto const torrent = listed(id);
    ASSERT_TRUE(torrent);
    auto const progress = torrent->get_long_progress_text();

    core_->remove_torrent(id, false);

    EXPECT_EQ(0U, core_->get_model()->get_n_items());
    EXPECT_EQ(id, torrent->get_id());
    EXPECT_EQ("debian-11.2.0-amd64-DVD-1.iso", torrent->get_name());
    EXPECT_EQ(progress, torrent->get_long_progress_text());
}

// A change fetched from the session reaches the list's object and the change signal.
TEST_F(GtkSessionTest, aFetchedChangeReachesTheObject)
{
    static auto constexpr NameField = tr::app::Torrent::fields<TR_KEY_name>();

    auto const id = add_listed_torrent(Debian);
    auto const torrent = listed(id);
    ASSERT_TRUE(torrent);

    // a rename also edits the metainfo, which fetches the torrent again, so gather every change
    auto changed_ids = tr::app::TorrentList::Ids{};
    auto changed_fields = Torrent::Fields{};
    auto connection = sigc::connection{ core_->signal_torrents_changed().connect(
        [&changed_ids, &changed_fields](auto const& ids, auto const& fields) {
            changed_ids.insert(std::end(changed_ids), std::begin(ids), std::end(ids));
            changed_fields |= fields;
        }) };

    // the rename finishes on another thread, so fetch once it has
    auto renamed = std::atomic<bool>{ false };
    tr_torrentRenamePath(
        core_->find_torrent(id),
        "debian-11.2.0-amd64-DVD-1.iso",
        "renamed.iso",
        [&renamed](auto /*id*/, auto /*oldpath*/, auto /*newname*/, auto const& /*error*/) { renamed = true; });
    ASSERT_TRUE(wait_for([&renamed] { return renamed.load(); }));
    core_->torrent_changed(id);
    ASSERT_TRUE(wait_for([&changed_fields] { return (changed_fields & NameField).any(); }));

    tr::app::TorrentList::normalize(changed_ids);
    EXPECT_EQ(tr::app::TorrentList::Ids{ id }, changed_ids);
    EXPECT_EQ("renamed.iso", torrent->get_name());

    connection.disconnect();
}

// The sorter orders by the shared sort's keys, then by name.
TEST_F(GtkSessionTest, sortsBySortModeThenName)
{
    auto const arch = listed(add_listed_torrent(Arch));
    auto const debian = listed(add_listed_torrent(Debian));
    ASSERT_TRUE(arch && debian);

    auto const sorter = TorrentSorter::create();
    EXPECT_LT(sorter->compare(*arch, *debian), 0);

    sorter->set_reversed(true);
    EXPECT_GT(sorter->compare(*arch, *debian), 0);

    // the larger torrent first
    sorter->set_reversed(false);
    sorter->set_mode(SortMode::SortBySize);
    ASSERT_GT(debian->get_shared().size_when_done(), arch->get_shared().size_when_done());
    EXPECT_LT(sorter->compare(*debian, *arch), 0);
}

// The filter reads the search text from the prefs and matches names case-insensitively.
TEST_F(GtkSessionTest, filtersByTheSearchTextPref)
{
    auto const arch = listed(add_listed_torrent(Arch));
    auto const debian = listed(add_listed_torrent(Debian));
    ASSERT_TRUE(arch && debian);

    auto const filter = TorrentFilter::create(*prefs_);
    EXPECT_TRUE(filter->matches_all());

    prefs_->set(TR_KEY_filter_text, std::string{ "DEBIAN" });
    EXPECT_FALSE(filter->matches_all());
    EXPECT_TRUE(filter->match(*debian));
    EXPECT_FALSE(filter->match(*arch));
}
