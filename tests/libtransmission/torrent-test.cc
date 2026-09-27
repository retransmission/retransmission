// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <future>
#include <optional>
#include <ranges>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/error-types.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/types.h>

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
