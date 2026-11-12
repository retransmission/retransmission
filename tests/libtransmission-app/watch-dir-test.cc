// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstddef> // size_t
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>

#include "libtransmission-app/add-data.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/session.h"
#include "libtransmission-app/watch-dir.h"

#include "../libtransmission/fake-timer.h"
#include "test-fixtures.h"

namespace
{

using namespace std::literals;
using tr::app::WatchDir;
using tr::test::FakeTimer;
using tr::test::FakeTimerMaker;

auto constexpr Arch = "archlinux-2025.05.01-x86_64.iso.torrent"sv;
auto constexpr Debian = "debian-11.2.0-amd64-DVD-1.iso.torrent"sv;

// Makes the monitors a toolkit would, one at a time, for the test to trigger.
class FakeDirMonitorMaker final : public tr::app::DirMonitorMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::app::DirMonitor> create(std::string_view const dirname, std::function<void()> on_change)
        override
    {
        return std::make_unique<Monitor>(*this, dirname, std::move(on_change));
    }

    // the live monitor's directory, or empty if there's none
    [[nodiscard]] std::string const& dirname() const noexcept
    {
        return dirname_;
    }

    // as the toolkit would when the live monitor's directory changes
    void notify() const
    {
        if (on_change_) {
            on_change_();
        }
    }

private:
    class Monitor final : public tr::app::DirMonitor
    {
    public:
        Monitor(FakeDirMonitorMaker& maker, std::string_view const dirname, std::function<void()> on_change)
            : maker_{ maker }
        {
            maker_.dirname_ = dirname;
            maker_.on_change_ = std::move(on_change);
        }

        Monitor(Monitor&&) = delete;
        Monitor(Monitor const&) = delete;
        Monitor& operator=(Monitor&&) = delete;
        Monitor& operator=(Monitor const&) = delete;

        ~Monitor() override
        {
            maker_.dirname_.clear();
            maker_.on_change_ = {};
        }

    private:
        FakeDirMonitorMaker& maker_;
    };

    std::string dirname_;
    std::function<void()> on_change_;
};

// A WatchDir on a real embedded session, with its directory enabled in the prefs.
// The session counts as started only once a test fires `source_changed`.
class AppWatchDirTest : public EmbeddedRpcSessionTest<>
{
protected:
    void SetUp() override
    {
        EmbeddedRpcSessionTest::SetUp();
        EXPECT_TRUE(tr_sys_dir_create(dir_, 0, 0700));
        prefs_.set(TR_KEY_show_options_window, false);
        prefs_.set(TR_KEY_trash_original_torrent_files, false);
        prefs_.set(TR_KEY_watch_dir, dir_);
        prefs_.set(TR_KEY_watch_dir_enabled, true);
    }

    // Copies a test asset into the watch dir as `name` and returns the copy's path.
    [[nodiscard]] std::string drop(std::string_view const asset, std::string_view const name) const
    {
        auto path = fmt::format("{:s}/{:s}", dir_, name);
        EXPECT_TRUE(tr_sys_path_copy(fmt::format("{:s}/{:s}", LIBTRANSMISSION_TEST_ASSETS_DIR, asset), path));
        return path;
    }

    // as the client's loop would on its next turn, by firing the WatchDir's own timer, the first one made
    void next_turn() const
    {
        timer_maker_.timers().front()->fire();
    }

    void start_session()
    {
        session_.source_changed();
        next_turn();
    }

    // as the rescan timer would; the newest watch dir makes it after its retry timer
    void rescan() const
    {
        timer_maker_.timers().back()->fire();
    }

    // the coalescing timer, the second one the watch dir makes, after its start timer
    [[nodiscard]] FakeTimer* coalesce_timer() const
    {
        return timer_maker_.timers().at(1);
    }

    // as that timer would, `CoalesceInterval` after the first monitor notice
    void coalesce() const
    {
        coalesce_timer()->fire();
    }

    // Waits until the session has renamed the watch dir's `path` as added.
    [[nodiscard]] bool wait_until_added(std::string const& path)
    {
        return loop_.pump_until([&path] { return !tr_sys_path_exists(path) && tr_sys_path_exists(path + ".added"); });
    }

    [[nodiscard]] size_t torrent_count() const
    {
        return std::size(tr_sessionGetAllTorrents(core_));
    }

    std::string const dir_ = sandbox_dir() + "/watch";
    FakeTimerMaker timer_maker_;
    FakeDirMonitorMaker monitor_maker_;
    WatchDir watch_dir_{ session_, prefs_, timer_maker_, monitor_maker_ };
};

TEST_F(AppWatchDirTest, watchesWhileEnabledAndTheSessionIsStarted)
{
    EXPECT_EQ(""sv, watch_dir_.dirname());

    session_.source_changed();
    EXPECT_EQ(""sv, watch_dir_.dirname());
    next_turn();
    EXPECT_EQ(dir_, watch_dir_.dirname());
    EXPECT_EQ(dir_, monitor_maker_.dirname());

    prefs_.set(TR_KEY_watch_dir_enabled, false);
    EXPECT_EQ(""sv, watch_dir_.dirname());
    EXPECT_EQ(""sv, monitor_maker_.dirname());

    prefs_.set(TR_KEY_watch_dir_enabled, true);
    EXPECT_EQ(dir_, watch_dir_.dirname());
    EXPECT_EQ(dir_, monitor_maker_.dirname());

    auto const other = sandbox_dir() + "/other";
    prefs_.set(TR_KEY_watch_dir, other);
    EXPECT_EQ(other, watch_dir_.dirname());
    EXPECT_EQ(other, monitor_maker_.dirname());

    session_.stop();
    EXPECT_EQ(""sv, watch_dir_.dirname());
    EXPECT_EQ(""sv, monitor_maker_.dirname());

    // a session that stops before the loop's next turn
    session_.source_changed();
    session_.stop();
    next_turn();
    EXPECT_EQ(""sv, watch_dir_.dirname());
}

TEST_F(AppWatchDirTest, addsTorrentsThereBeforeAndAfterItStarts)
{
    auto const before = drop(Arch, "before.torrent"sv);
    start_session();
    EXPECT_TRUE(wait_until_added(before));

    auto const after = drop(Debian, "after.TORRENT"sv);
    rescan();
    EXPECT_TRUE(wait_until_added(after));

    EXPECT_EQ(2U, torrent_count());
}

TEST_F(AppWatchDirTest, scansAfterAMonitorNoticeAndEveryMinute)
{
    start_session();
    EXPECT_EQ(WatchDir::FallbackScanInterval, timer_maker_.timers().back()->interval());

    auto const path = drop(Arch, "arch.torrent"sv);
    monitor_maker_.notify();
    coalesce();
    EXPECT_TRUE(wait_until_added(path));
}

// A monitor reports each name that changes and may report one twice, while a scan
// lists the whole directory, so a file arriving in a burst waits for the others.
TEST_F(AppWatchDirTest, scansOnceForABurstOfMonitorNotices)
{
    start_session();
    auto const path = drop(Arch, "arch.torrent"sv);

    monitor_maker_.notify();
    monitor_maker_.notify();
    monitor_maker_.notify();
    EXPECT_EQ(WatchDir::CoalesceInterval, coalesce_timer()->interval());
    EXPECT_FALSE(coalesce_timer()->is_repeating());

    // the notices themselves don't scan
    loop_.drain();
    EXPECT_FALSE(tr_sys_path_exists(path + ".added"));

    coalesce();
    EXPECT_TRUE(wait_until_added(path));
}

// `update()` drops the watch dir that a pending scan would have read.
TEST_F(AppWatchDirTest, forgetsAPendingScanWhenItStopsWatching)
{
    start_session();
    auto const path = drop(Arch, "arch.torrent"sv);
    monitor_maker_.notify();

    prefs_.set(TR_KEY_watch_dir_enabled, false);
    coalesce();
    loop_.drain();
    EXPECT_TRUE(tr_sys_path_exists(path)); // never added, so never renamed
    EXPECT_EQ(0U, torrent_count());
}

// A file that doesn't parse yet but has just changed may still be being written.
TEST_F(AppWatchDirTest, addsAFileOnceItsWriteFinishes)
{
    auto const path = fmt::format("{:s}/slow.torrent", dir_);
    EXPECT_TRUE(tr_file_save(path, "d8:announce"sv)); // the start of a torrent
    start_session();

    EXPECT_EQ(path, drop(Arch, "slow.torrent"sv));
    rescan();
    EXPECT_TRUE(wait_until_added(path));
}

TEST_F(AppWatchDirTest, leavesFilesItCanNotAdd)
{
    // a torrent without the suffix
    auto const txt = drop(Arch, "arch.txt"sv);

    // junk that has stopped changing, so it isn't a torrent still being written
    auto const junk = fmt::format("{:s}/junk.torrent", dir_);
    EXPECT_TRUE(tr_file_save(junk, "this is not a torrent"sv));
    std::filesystem::last_write_time(junk, std::filesystem::file_time_type::clock::now() - 1h);

    auto n_failed = size_t{};
    auto const tag = session_.torrent_add_report.connect_scoped(
        [&n_failed](tr::app::Session::AddReport const& report) { n_failed += std::size(report.failed); });

    auto const debian = drop(Debian, "debian.torrent"sv);
    start_session();
    EXPECT_TRUE(wait_until_added(debian)); // so the scan is done

    // neither is sent, or its failed add would be reported with the torrent's batch
    loop_.drain();
    EXPECT_EQ(0U, n_failed);
    EXPECT_TRUE(tr_sys_path_exists(txt));
    EXPECT_TRUE(tr_sys_path_exists(junk));
    EXPECT_EQ(1U, torrent_count());
}

// GTK connects its add prompt after it starts the session.
TEST_F(AppWatchDirTest, promptsThroughASlotConnectedAfterTheSessionStarts)
{
    prefs_.set(TR_KEY_show_options_window, true);
    auto const path = drop(Arch, "arch.torrent"sv);
    session_.source_changed();

    auto prompted = std::vector<std::string>{};
    auto const tag = session_.torrent_add_prompt.connect_scoped(
        [&prompted](tr::app::AddData const& add, tr::app::AddOptions const& /*options*/) { prompted.push_back(add.source); });
    next_turn();
    EXPECT_EQ(std::vector{ path }, prompted);
}

// The generic scan only sees the name leave if it looks between the two files.
TEST_F(AppWatchDirTest, addsANewFileWithTheNameOfOneItAdded)
{
    auto const path = drop(Arch, "same.torrent"sv);
    start_session();
    ASSERT_TRUE(wait_until_added(path));
    rescan();

    EXPECT_EQ(path, drop(Debian, "same.torrent"sv));
    rescan();
    EXPECT_TRUE(wait_until_added(path));
    EXPECT_EQ(2U, torrent_count());
}

} // namespace
