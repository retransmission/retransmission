// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <tuple>

#include <libtransmission/torrent.h>
#include <libtransmission/transmission.h>
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

TEST_F(TorrentTest, bindInterfaceMatchesSession)
{
    auto* const tor = torrentInitFromFile(TorFilenames[0]);

    // on the default route, a torrent inherits it unless it names something else
    EXPECT_EQ(""sv, tor->effective_bind_interface());
    EXPECT_TRUE(tor->bind_interface_matches_session());
    tr_torrentSetBindInterface(tor, "blocked"sv);
    EXPECT_FALSE(tor->bind_interface_matches_session());
    tr_torrentSetBindInterface(tor, ""sv);

    tr_sessionSetBindInterface(session_, "lo0"sv);
    ASSERT_TRUE(tr::test::waitFor([this] { return tr_sessionGetBindInterface(session_) == "lo0"sv; }, 5s));

    // torrent value, effective interface, matches session
    static auto constexpr Tests = std::to_array<std::tuple<std::string_view, std::string_view, bool>>({
        { ""sv, "lo0"sv, true },
        { "default"sv, ""sv, false },
        { "lo0"sv, "lo0"sv, true },
        { " lo0 "sv, "lo0"sv, true },
        { "en0"sv, "en0"sv, false },
        { "blocked"sv, "blocked"sv, false },
    });

    for (auto const& [value, effective, matches] : Tests) {
        tr_torrentSetBindInterface(tor, value);
        EXPECT_EQ(effective, tor->effective_bind_interface()) << '"' << value << '"';
        EXPECT_EQ(matches, tor->bind_interface_matches_session()) << '"' << value << '"';
    }
}
