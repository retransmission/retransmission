// This file Copyright (C) 2015-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/time.h> // timeval
#endif

#include <event2/event.h>

#include <gtest/gtest.h>

#include <libtransmission/file.h>
#include <libtransmission/timer.h>
#include <libtransmission/timer-ev.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/watchdir.h>

#include "test-fixtures.h"

using namespace std::literals;

static auto constexpr GenericRescanInterval = 100ms;

// should be at least 2x the watchdir-generic size to ensure that
// we have time to pump all events at least once in processEvents()
static auto constexpr ProcessEventsTimeout = 300ms;
static_assert(ProcessEventsTimeout > GenericRescanInterval);

namespace tr::test
{
namespace
{
enum class WatchMode : uint8_t { NATIVE, GENERIC };

class WatchDirTest
    : public SandboxedTest
    , public ::testing::WithParamInterface<WatchMode>
{
private:
    std::shared_ptr<struct event_base> ev_base_;
    std::unique_ptr<tr::TimerMaker> timer_maker_;

protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        ev_base_.reset(event_base_new(), event_base_free);
        timer_maker_ = std::make_unique<tr::EvTimerMaker>(ev_base_.get());
        Watchdir::set_generic_rescan_interval(GenericRescanInterval);
    }

    void TearDown() override
    {
        ev_base_.reset();

        SandboxedTest::TearDown();
    }

    // `filter` applies only to a generic watch dir
    [[nodiscard]] auto createWatchDir(std::string_view path, Watchdir::Callback callback, Watchdir::NameFilter filter = {})
    {
        auto const force_generic = GetParam() == WatchMode::GENERIC;
        return force_generic ?
            Watchdir::create_generic(path, std::move(callback), *timer_maker_, GenericRescanInterval, std::move(filter)) :
            Watchdir::create(path, std::move(callback), *timer_maker_, ev_base_.get());
    }

    // a watch dir that records each name it offers in `names_` and is done with it
    [[nodiscard]] auto createRecordingWatchDir(std::string_view path, Watchdir::NameFilter filter = {})
    {
        return createWatchDir(
            path,
            [this](std::string_view /*dirname*/, std::string_view basename) {
                names_.emplace_back(basename);
                return Watchdir::Action::Done;
            },
            std::move(filter));
    }

    static void createFile(std::string_view dirname, std::string_view basename, std::string_view contents = ""sv)
    {
        createFileWithContents(tr_pathbuf{ dirname, '/', basename }, contents);
    }

    static std::string createDir(std::string_view dirname, std::string_view basename)
    {
        auto path = fmt::format("{:s}/{:s}", dirname, basename);
        tr_sys_dir_create(path, 0, 0700);
        return path;
    }

    void processEvents(std::chrono::milliseconds wait_interval = ProcessEventsTimeout)
    {
        auto tv = timeval{};
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(wait_interval);
        tv.tv_sec = static_cast<decltype(tv.tv_sec)>(seconds.count());

        wait_interval -= seconds;
        auto const usec = std::chrono::duration_cast<std::chrono::microseconds>(wait_interval);
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(usec.count());

        event_base_loopexit(ev_base_.get(), &tv);
        event_base_dispatch(ev_base_.get());
    }

    std::vector<std::string> names_;
};
} // namespace

TEST_P(WatchDirTest, construct)
{
    auto const path = sandboxDir();

    auto callback = [](std::string_view /*dirname*/, std::string_view /*basename*/) {
        return Watchdir::Action::Done;
    };
    auto watchdir = createWatchDir(path, callback);
    EXPECT_TRUE(watchdir);
    EXPECT_EQ(path, watchdir->dirname());

    processEvents();
}

TEST_P(WatchDirTest, initialScan)
{
    auto const path = sandboxDir();

    // setup: start with an empty directory.
    // this block confirms that it's empty
    {
        auto called = false;
        auto callback = [&called](std::string_view /*dirname*/, std::string_view /*basename*/) {
            called = true;
            return Watchdir::Action::Done;
        };
        auto watchdir = createWatchDir(path, callback);
        EXPECT_TRUE(watchdir);
        processEvents();
        EXPECT_FALSE(called);
    }

    // add a file
    auto const base_name = "test.txt"sv;
    createFile(path, base_name);

    // confirm that a wd will pick up the file that
    // was created before the wd was instantiated
    {
        auto names = std::set<std::string>{};
        auto callback = [&names](std::string_view /*dirname*/, std::string_view basename) {
            names.insert(std::string{ basename });
            return Watchdir::Action::Done;
        };
        auto watchdir = createWatchDir(path, callback);
        EXPECT_TRUE(watchdir);
        processEvents();
        EXPECT_EQ(1U, std::size(names));
        EXPECT_EQ(base_name, *names.begin());
    }
}

TEST_P(WatchDirTest, watch)
{
    auto const dirname = sandboxDir();

    // create a new watchdir and confirm it's empty
    auto watchdir = createRecordingWatchDir(dirname);
    processEvents();
    EXPECT_TRUE(watchdir);
    EXPECT_TRUE(std::empty(names_));

    // test that a new file in an empty directory shows up
    auto const file1 = "test1"sv;
    createFile(dirname, file1);
    processEvents();
    EXPECT_EQ(std::vector<std::string>{ std::string{ file1 } }, names_);

    // test that a new file in a nonempty directory shows up
    names_.clear();
    auto const file2 = "test2"sv;
    createFile(dirname, file2);
    processEvents();
    processEvents();
    EXPECT_EQ(std::vector<std::string>{ std::string{ file2 } }, names_);

    // test that folders don't trigger the callback
    names_.clear();
    createDir(dirname, "test3"sv);
    processEvents();
    EXPECT_TRUE(std::empty(names_));
}

// before any event or timer has had a chance to run
TEST_P(WatchDirTest, rescanOffersANewFileAtOnce)
{
    auto const dirname = sandboxDir();
    auto watchdir = createRecordingWatchDir(dirname);

    auto const name = "test.torrent"sv;
    createFile(dirname, name);
    watchdir->rescan();
    EXPECT_EQ(std::vector<std::string>{ std::string{ name } }, names_);
}

TEST_P(WatchDirTest, offersANameAgainAfterItsFileLeaves)
{
    auto const dirname = sandboxDir();
    auto watchdir = createRecordingWatchDir(dirname);
    processEvents();

    auto const name = "test.torrent"sv;
    auto const path = tr_pathbuf{ dirname, '/', name };
    createFile(dirname, name);
    processEvents();
    EXPECT_EQ(std::vector<std::string>{ std::string{ name } }, names_);

    // renamed away, as the daemon and the clients do once they've added it
    EXPECT_TRUE(tr_sys_path_rename(path, tr_pathbuf{ path, ".added"sv }));
    processEvents();
    names_.clear();
    createFile(dirname, name);
    processEvents();
    EXPECT_EQ(std::vector<std::string>{ std::string{ name } }, names_);

    // deleted
    EXPECT_TRUE(tr_sys_path_remove(path));
    processEvents();
    names_.clear();
    createFile(dirname, name);
    processEvents();
    EXPECT_EQ(std::vector<std::string>{ std::string{ name } }, names_);
}

// e.g. a share whose mount point goes away while it's unmounted
TEST_P(WatchDirTest, keepsItsNamesWhileTheDirIsMissing)
{
    auto const dirname = createDir(sandboxDir(), "watch"sv);
    auto const elsewhere = tr_pathbuf{ sandboxDir(), "/elsewhere"sv };
    createFile(dirname, "test.torrent"sv);
    auto watchdir = createRecordingWatchDir(dirname);
    EXPECT_EQ(1U, std::size(names_));

    EXPECT_TRUE(tr_sys_path_rename(dirname, elsewhere));
    watchdir->rescan();
    EXPECT_TRUE(tr_sys_path_rename(elsewhere, dirname));
    watchdir->rescan();
    EXPECT_EQ(1U, std::size(names_));
}

TEST_P(WatchDirTest, offersOnlyTheNamesItsFilterAccepts)
{
    if (GetParam() == WatchMode::NATIVE) {
        GTEST_SKIP() << "only a generic watch dir takes a filter";
    }

    auto const dirname = sandboxDir();
    createFile(dirname, "a.torrent"sv);
    createFile(dirname, "a.torrent.added"sv);
    auto watchdir = createRecordingWatchDir(dirname, [](std::string_view const basename) {
        return basename.ends_with(".torrent"sv);
    });
    EXPECT_EQ(std::vector<std::string>{ "a.torrent" }, names_);
}

INSTANTIATE_TEST_SUITE_P( //
    WatchDir,
    WatchDirTest,
    ::testing::Values( //
        WatchMode::NATIVE,
        WatchMode::GENERIC));

} // namespace tr::test
