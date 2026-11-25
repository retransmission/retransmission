// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::ranges::count_if()
#include <array>
#include <compare>
#include <cstddef> // size_t
#include <iterator> // std::size()
#include <string_view>
#include <utility> // std::move()

#include <gtest/gtest.h>

#include <libtransmission/values.h>

#include "libtransmission-app/peer-list.h"
#include "libtransmission-app/torrent.h"

using namespace std::literals;
using tr::app::Peer;
using tr::app::PeerAddressKey;
using tr::app::PeerColumn;
using tr::app::PeerFlag;
using tr::app::PeerFlagLetters;
using tr::app::PeerFlags;
using tr::app::PeerListItem;
using Percent = tr::Values::Percent;
using Speed = tr::Values::Speed;

namespace
{

[[nodiscard]] Peer make_peer(std::string_view const address)
{
    auto peer = Peer{};
    peer.address = address;
    return peer;
}

[[nodiscard]] PeerListItem make_item(Peer peer, Percent const progress = {})
{
    return { 1, std::move(peer), progress };
}

// `lo` sorts before `hi` by `column`
void expect_before(PeerListItem const& lo, PeerListItem const& hi, PeerColumn const column)
{
    EXPECT_EQ(lo.compare(hi, column), std::weak_ordering::less);
    EXPECT_EQ(hi.compare(lo, column), std::weak_ordering::greater);
}

} // namespace

TEST(PeerFlagsTest, decodesEachLetter)
{
    for (auto const& [flag, letter] : PeerFlagLetters) {
        auto const flags = PeerFlags::from_string(std::string_view{ &letter, 1U });
        EXPECT_TRUE(flags.contains(flag)) << letter;
        EXPECT_EQ(std::ranges::count_if(PeerFlagLetters, [&](auto const& entry) { return flags.contains(entry.first); }), 1)
            << letter;
    }
}

TEST(PeerFlagsTest, lettersAreInFlagOrder)
{
    for (size_t i = 0; i < std::size(PeerFlagLetters); ++i) {
        EXPECT_EQ(static_cast<size_t>(PeerFlagLetters[i].first), i);
    }
}

TEST(PeerFlagsTest, skipsUnknownLetters)
{
    EXPECT_TRUE(PeerFlags::from_string(""sv).empty());
    EXPECT_TRUE(PeerFlags::from_string("Z!"sv).empty());

    auto expected = PeerFlags{};
    expected.insert(PeerFlag::DownloadingFrom);
    expected.insert(PeerFlag::Encrypted);
    EXPECT_EQ(PeerFlags::from_string("DZE"sv), expected);
}

TEST(PeerAddressKeyTest, sortsIpv4ThenIpv6ThenText)
{
    static auto constexpr Sorted = std::array{
        "9.0.0.1"sv, "10.0.0.1"sv, "10.0.0.20"sv, "::1"sv, "2001:db8::1"sv, "2001:db8::a"sv, "a.example"sv, "B.example"sv,
    };

    for (size_t i = 1; i < std::size(Sorted); ++i) {
        EXPECT_LT(PeerAddressKey{ Sorted[i - 1] }, PeerAddressKey{ Sorted[i] }) << Sorted[i - 1] << " vs " << Sorted[i];
    }
}

TEST(PeerAddressKeyTest, textIgnoresAsciiCase)
{
    EXPECT_EQ(PeerAddressKey{ "Peer.Example"sv }, PeerAddressKey{ "peer.example"sv });
}

TEST(PeerColumnsTest, extraDetailsAddTheRequestColumns)
{
    for (size_t i = 0; i < std::size(tr::app::PeerColumns); ++i) {
        EXPECT_EQ(static_cast<size_t>(tr::app::PeerColumns[i]), i);
    }

    for (auto const column : tr::app::PeerColumns) {
        auto const is_requests = column == PeerColumn::UploadRequests || column == PeerColumn::DownloadRequests;
        EXPECT_EQ(tr::app::is_extra_peer_column(column), is_requests) << static_cast<int>(column);
    }
}

TEST(PeerListItemTest, keyIsTorrentAddressAndPort)
{
    auto peer = make_peer("10.0.0.1"sv);
    peer.port = 51413U;
    auto const item = PeerListItem{ 7, std::move(peer), Percent{} };
    EXPECT_EQ(item.key(), (tr::app::PeerKey{ .torrent_id = 7, .address = "10.0.0.1", .port = 51413U }));
}

TEST(PeerListItemTest, comparesEachColumn)
{
    // `lo` has the later address, so only the column can put it first
    auto lo = make_peer("10.0.0.1"sv);
    auto hi = make_peer("9.0.0.1"sv);
    expect_before(make_item(hi), make_item(lo), PeerColumn::Address);

    lo.rate_to_peer = Speed{ 1U, Speed::Units::Byps };
    hi.rate_to_peer = Speed{ 2U, Speed::Units::Byps };
    expect_before(make_item(lo), make_item(hi), PeerColumn::UploadSpeed);

    lo.rate_to_client = Speed{ 1U, Speed::Units::Byps };
    hi.rate_to_client = Speed{ 2U, Speed::Units::Byps };
    expect_before(make_item(lo), make_item(hi), PeerColumn::DownloadSpeed);

    lo.active_reqs_to_client = 1U;
    hi.active_reqs_to_client = 2U;
    expect_before(make_item(lo), make_item(hi), PeerColumn::UploadRequests);

    lo.active_reqs_to_peer = 1U;
    hi.active_reqs_to_peer = 2U;
    expect_before(make_item(lo), make_item(hi), PeerColumn::DownloadRequests);

    lo.flags = "D";
    hi.flags = "U";
    expect_before(make_item(lo), make_item(hi), PeerColumn::Flags);

    lo.client_name = "Alpha";
    hi.client_name = "Beta";
    expect_before(make_item(lo), make_item(hi), PeerColumn::Client);

    expect_before(make_item(lo, Percent{ 1U, 4U }), make_item(hi, Percent{ 3U, 4U }), PeerColumn::Progress);

    // encrypted first
    lo.is_encrypted = true;
    hi.is_encrypted = false;
    expect_before(make_item(lo), make_item(hi), PeerColumn::Encrypted);
}

TEST(PeerListItemTest, clientNamesIgnoreCase)
{
    auto lo = make_peer("10.0.0.2"sv);
    auto hi = make_peer("10.0.0.1"sv);
    lo.client_name = "libtorrent";
    hi.client_name = "Transmission";
    expect_before(make_item(lo), make_item(hi), PeerColumn::Client);
}

TEST(PeerListItemTest, tiesGoByAddressPortAndTorrent)
{
    // equal by every column but the address
    expect_before(make_item(make_peer("9.0.0.1"sv)), make_item(make_peer("10.0.0.1"sv)), PeerColumn::DownloadSpeed);

    auto lo = make_peer("10.0.0.1"sv);
    auto hi = lo;
    lo.port = 1U;
    hi.port = 2U;
    expect_before(make_item(lo), make_item(hi), PeerColumn::DownloadSpeed);

    expect_before(PeerListItem{ 1, lo, Percent{} }, PeerListItem{ 2, lo, Percent{} }, PeerColumn::DownloadSpeed);
    EXPECT_EQ(make_item(lo).compare(make_item(lo), PeerColumn::DownloadSpeed), std::weak_ordering::equivalent);
}
