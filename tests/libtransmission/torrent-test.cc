// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/error-types.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/types.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using TorrentTest = tr::test::SessionTest;
using tr::Values::Percent;
using tr::Values::Ratio;

namespace
{
auto constexpr TorFilenames = std::array{
    "Android-x86 8.1 r6 iso.torrent"sv,
    "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
    "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
    "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
};

[[nodiscard]] std::future<std::vector<tr_stat>> statAsync(tr_session* const session)
{
    // shared, so that a callback that runs after the caller stops waiting still has its promise
    auto const promise = std::make_shared<std::promise<std::vector<tr_stat>>>();
    auto future = promise->get_future();
    tr_torrentStatAsync(session, [promise](std::vector<tr_stat> stats) { promise->set_value(std::move(stats)); });
    return future;
}

// Holds the session thread until the returned promise is set, so that later calls queue behind it.
[[nodiscard]] std::promise<void> holdSessionThread(tr_session* const session)
{
    auto release = std::promise<void>{};
    session->queue_session_thread([released = release.get_future().share()] { released.wait(); });
    return release;
}
} // namespace

TEST_F(TorrentTest, queueMoveUp)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 1, 3, 2 };
    auto builder = tr_torrent_builder{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(TorFilenames, torrents.begin(), [this](auto const filename) {
        return torrentInitFromFile(filename);
    });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i) {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    blockingRunInSessionThread([&] { tr_torrent::queue_move_up(move_torrents); });

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i) {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveDown)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 0, 2, 3 };
    auto builder = tr_torrent_builder{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(TorFilenames, torrents.begin(), [this](auto const filename) {
        return torrentInitFromFile(filename);
    });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i) {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    blockingRunInSessionThread([&] { tr_torrent::queue_move_down(move_torrents); });

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i) {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, useSessionLimitsAffectsBothDirections)
{
    auto* const tor = torrentInitFromFile(TorFilenames[0]);
    ASSERT_NE(nullptr, tor);

    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(tr_direction::Up));
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(tr_direction::Down));

    tr_torrentUseSessionLimits(tor, false);
    EXPECT_FALSE(tor->bandwidth().are_parent_limits_honored(tr_direction::Up));
    EXPECT_FALSE(tor->bandwidth().are_parent_limits_honored(tr_direction::Down));

    tr_torrentUseSessionLimits(tor, true);
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(tr_direction::Up));
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(tr_direction::Down));
}

TEST_F(TorrentTest, queueMoveTop)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 3, 1, 2 };
    auto builder = tr_torrent_builder{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(TorFilenames, torrents.begin(), [this](auto const filename) {
        return torrentInitFromFile(filename);
    });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i) {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    blockingRunInSessionThread([&] { tr_torrent::queue_move_top(move_torrents); });

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i) {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveBottom)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 2, 0, 3 };
    auto builder = tr_torrent_builder{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(TorFilenames, torrents.begin(), [this](auto const filename) {
        return torrentInitFromFile(filename);
    });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i) {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    blockingRunInSessionThread([&] { tr_torrent::queue_move_bottom(move_torrents); });

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i) {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, workQueuedBehindRemovalSkipsFreedTorrent)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    auto const name = tr_torrentName(tor);
    auto const target_dir = tr_pathbuf{ sandboxDir(), "/target"sv };

    // Hold the session thread, so that each call below queues its work.
    auto release = holdSessionThread(session_);

    // `tor` is valid for every call, but the first removal frees it before the rest of the work runs.
    // Only a sanitizer build checks the work that reports nothing.
    tr_torrentRemove(tor, false);
    tr_torrentStart(tor);
    tr_torrentStop(tor);
    tr_torrentVerify(tor);
    tr_torrentManualUpdate(tor);
    auto location_state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &location_state);
    auto rename_error = std::optional<tr_error_code_t>{};
    tr_torrentRenamePath(
        tor,
        name,
        "renamed"sv,
        [&rename_error](
            tr_torrent_id_t const /*tor_id*/,
            std::string_view const /*oldpath*/,
            std::string_view const /*newname*/,
            tr_error const& error) { rename_error = error.code(); });
    tr_torrentRemove(tor, false);

    release.set_value();
    blockingRunInSessionThread([] {}); // runs after the work queued above

    // The work that reports back reports an error.
    EXPECT_EQ(TR_LOC_ERROR, location_state);
    EXPECT_EQ(TR_ERROR_EINVAL, rename_error);
}

TEST_F(TorrentTest, setAnnounceListReportsSaveFailure)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);

    // the trackers can't be saved into a .torrent file that has no info dict
    auto error = tr_error{};
    EXPECT_TRUE(tr_file_save(tor->torrent_file(), "d7:comment3:fooe", &error)) << error;
    EXPECT_FALSE(tor->set_announce_list("https://a.example.com/announce"));
    EXPECT_TRUE(tor->error().is_local_error());
    EXPECT_NE(std::string::npos, tor->error().errmsg().find("missing 'info' dict"));
}

// --- tr_sessionInit()'s remove func

namespace
{

class TorrentRemoveTest : public TorrentTest
{
protected:
    // Paths the session's remove func was asked to remove.
    // Removals run on the session thread,
    // so read this only after waiting for the torrent to be gone,
    // since that wait runs through the same thread.
    std::vector<std::string> removed_;

    void SetUp() override
    {
        session_remove_func_ = [this](std::string_view const filename, tr_error* const error) {
            removed_.emplace_back(filename);
            return tr_sys_path_remove(filename, error);
        };
        TorrentTest::SetUp();
    }

    // adds a complete zero torrent and returns it with its data folder, which must exist
    [[nodiscard]] std::pair<tr_torrent*, tr_pathbuf> addCompleteTorrent()
    {
        auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
        auto dir = tr_pathbuf{ tr_torrentGetCurrentDir(tor), '/', tr_torrentName(tor) };
        EXPECT_TRUE(tr_sys_path_exists(dir));
        return { tor, std::move(dir) };
    }

    [[nodiscard]] bool waitUntilRemoved(tr_torrent_id_t const id)
    {
        return waitForInSessionThread([this, id] { return tr_torrentFindFromId(session_, id) == nullptr; }, 10'000);
    }
};

} // namespace

TEST_F(TorrentRemoveTest, deletesLocalDataWithTheSessionRemoveFunc)
{
    auto const [tor, dir] = addCompleteTorrent();
    auto const id = tr_torrentId(tor);
    auto const name = std::string{ tr_torrentName(tor) };
    tr_torrentRemove(tor, true);
    ASSERT_TRUE(waitUntilRemoved(id));

    // The top-level folder comes first, so a trash can keep the hierarchy.
    ASSERT_FALSE(std::empty(removed_));
    EXPECT_EQ(name, tr_sys_path_basename(removed_.front()));
    EXPECT_FALSE(tr_sys_path_exists(dir));
}

// Clients that talk RPC, such as the Qt client, reach the remove func this way.
TEST_F(TorrentRemoveTest, rpcRemovalDeletesLocalDataWithTheSessionRemoveFunc)
{
    auto const [tor, dir] = addCompleteTorrent();
    auto const id = tr_torrentId(tor);

    auto params = tr_variant::Map{ 2U };
    params.try_emplace(TR_KEY_ids, id);
    params.try_emplace(TR_KEY_delete_local_data, true);
    auto request = tr_variant::Map{ 4U };
    request.try_emplace(TR_KEY_jsonrpc, JsonRpc::Version);
    request.try_emplace(TR_KEY_method, tr_variant::unmanaged_string(TR_KEY_torrent_remove));
    request.try_emplace(TR_KEY_id, 1);
    request.try_emplace(TR_KEY_params, std::move(params));
    tr_rpc_request_exec(session_, std::move(request));
    ASSERT_TRUE(waitUntilRemoved(id));

    EXPECT_FALSE(std::empty(removed_));
    EXPECT_FALSE(tr_sys_path_exists(dir));
}

TEST_F(TorrentRemoveTest, leavesLocalDataAloneWhenNotDeleting)
{
    auto const [tor, dir] = addCompleteTorrent();
    auto const id = tr_torrentId(tor);
    tr_torrentRemove(tor, false);
    ASSERT_TRUE(waitUntilRemoved(id));

    EXPECT_TRUE(std::empty(removed_));
    EXPECT_TRUE(tr_sys_path_exists(dir));
}

TEST_F(TorrentTest, statBuildsProgressFromCounts)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    ASSERT_NE(nullptr, tor);

    auto const st = tr_torrentStat(tor);
    auto const have = st.have_valid + st.have_unchecked;
    EXPECT_FALSE(st.percent_done.is_complete());
    EXPECT_EQ((Percent{ have, st.size_when_done }), st.percent_done);
    EXPECT_EQ((Percent{ have, tor->total_size() }), st.percent_complete);
    EXPECT_EQ((Ratio{ st.uploaded_ever, st.size_when_done }), st.upload_ratio);
    EXPECT_TRUE(st.metadata_percent_complete.is_complete());
    EXPECT_TRUE(st.seed_ratio_percent_done.is_complete()); // no seed ratio applies
    EXPECT_EQ(Percent{}, st.recheck_progress); // not verifying

    auto const file = tr_torrentFile(tor, 0);
    EXPECT_FALSE(file.progress.is_complete());
    EXPECT_EQ((Percent{ file.have, file.length }), file.progress);

    tr_torrentRemove(tor, false);
}

TEST_F(TorrentTest, statOfACompleteTorrentIsComplete)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);

    auto const st = tr_torrentStat(tor);
    EXPECT_TRUE(st.percent_done.is_complete());
    EXPECT_TRUE(st.percent_complete.is_complete());
    EXPECT_TRUE(tr_torrentFile(tor, 0).progress.is_complete());

    tr_torrentRemove(tor, false);
}

TEST_F(TorrentTest, statOfAMagnetHasUnknownTotals)
{
    auto* const tor = zeroTorrentMagnetInit();
    ASSERT_NE(nullptr, tor);

    auto const st = tr_torrentStat(tor);
    EXPECT_EQ(Percent{}, st.percent_done);
    EXPECT_EQ(Percent{}, st.percent_complete);
    EXPECT_EQ(Percent{}, st.metadata_percent_complete);
    EXPECT_TRUE(st.upload_ratio.is_none());

    tr_torrentRemove(tor, false);
}

TEST_F(TorrentTest, statAsyncCallsBackOnTheSessionThread)
{
    auto const on_session_thread = std::make_shared<std::promise<bool>>();
    auto future = on_session_thread->get_future();
    tr_torrentStatAsync(session_, [session = session_, on_session_thread](std::vector<tr_stat> const& /*stats*/) {
        on_session_thread->set_value(session->am_in_session_thread());
    });

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());
}

TEST_F(TorrentTest, statAsyncRunsAfterQueuedWork)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    tr_torrentStartNow(tor);
    ASSERT_EQ(TR_STATUS_SEED, tr_torrentStat(tor).activity);

    // Hold the session thread, so that the stop is still queued when the stats are requested.
    auto release = holdSessionThread(session_);
    tr_torrentStop(tor);
    EXPECT_EQ(TR_STATUS_SEED, tr_torrentStat(tor).activity); // a synchronous stat runs ahead of the stop
    auto future = statAsync(session_);
    release.set_value();

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto const stats = future.get();
    ASSERT_EQ(1U, std::size(stats));
    EXPECT_EQ(TR_STATUS_STOPPED, stats.front().activity);

    tr_torrentRemove(tor, false);
}

TEST_F(TorrentTest, statAsyncReportsEveryTorrentById)
{
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(TorFilenames, torrents.begin(), [this](auto const filename) {
        return torrentInitFromFile(filename);
    });

    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto stats = future.get();
    ASSERT_EQ(std::size(torrents), std::size(stats));
    for (auto const* const tor : torrents) {
        auto const iter = std::ranges::find(stats, tor->id(), &tr_stat::id);
        ASSERT_NE(std::end(stats), iter);
        EXPECT_EQ(tor->total_size(), iter->size_when_done); // each torrent has a different size
    }

    // the request queues behind the removal
    auto const removed_id = torrents.front()->id();
    tr_torrentRemove(torrents.front(), false);
    future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    stats = future.get();
    EXPECT_EQ(std::size(torrents) - 1U, std::size(stats));
    EXPECT_EQ(std::end(stats), std::ranges::find(stats, removed_id, &tr_stat::id));
}

TEST_F(TorrentTest, statAsyncIgnoresAnEmptyCallback)
{
    auto const old_level = tr_logGetLevel();
    tr_logClearQueue();
    tr_logSetLevel(TR_LOG_WARN);
    tr_logSetQueueEnabled(true);

    tr_torrentStatAsync(session_, {});
    auto const msgs = tr_logGetQueue();
    EXPECT_NE(std::end(msgs), std::ranges::find(msgs, "callback != nullptr"sv, &tr_log_message::message));

    // the session thread survives to answer the next request
    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(std::empty(future.get()));

    tr_logSetQueueEnabled(false);
    tr_logSetLevel(old_level);
}

TEST_F(TorrentTest, peersAsyncCallsBackOnTheSessionThread)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);

    auto const on_session_thread = std::make_shared<std::promise<bool>>();
    auto future = on_session_thread->get_future();
    tr_torrentPeersAsync(tor, [session = session_, on_session_thread](std::vector<tr_peer_stat> const& peers) {
        EXPECT_TRUE(std::empty(peers)); // nothing is connected
        on_session_thread->set_value(session->am_in_session_thread());
    });
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());

    // A torrent removed ahead of the request still gets a (now empty) answer.
    auto release = holdSessionThread(session_);
    tr_torrentRemove(tor, false);
    auto const answered = std::make_shared<std::promise<void>>();
    auto answered_future = answered->get_future();
    tr_torrentPeersAsync(tor, [answered](std::vector<tr_peer_stat> const& peers) {
        EXPECT_TRUE(std::empty(peers));
        answered->set_value();
    });
    release.set_value();
    ASSERT_EQ(std::future_status::ready, answered_future.wait_for(5s));
}
