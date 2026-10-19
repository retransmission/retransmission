// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/error-types.h>
#include <libtransmission/file.h>
#include <libtransmission/quark.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using TorrentTest = tr::test::SessionTest;

namespace
{
auto constexpr TorFilenames = std::array{
    "Android-x86 8.1 r6 iso.torrent"sv,
    "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
    "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
    "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
};
}

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

    tr_torrent::queue_move_up(move_torrents);

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

    tr_torrent::queue_move_down(move_torrents);

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

    tr_torrent::queue_move_top(move_torrents);

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

    tr_torrent::queue_move_bottom(move_torrents);

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
    auto release = std::promise<void>{};
    session_->queue_session_thread([released = release.get_future().share()]() { released.wait(); });

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
    blockingRunInSessionThread([]() {}); // runs after the work queued above

    // The work that reports back reports an error.
    EXPECT_EQ(TR_LOC_ERROR, location_state);
    EXPECT_EQ(TR_ERROR_EINVAL, rename_error);
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
        return waitForInSessionThread([this, id]() { return tr_torrentFindFromId(session_, id) == nullptr; }, 10'000);
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
