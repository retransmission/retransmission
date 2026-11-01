// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <compare>
#include <cstdint>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/types.h>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent-sort.h"
#include "libtransmission-app/torrent.h"

#include "test-fixtures.h"
#include "torrent-get-helpers.h"

using namespace std::literals;

using tr::app::compare_torrents;
using tr::app::SortMode;
using tr::app::Torrent;

namespace
{

class AppTorrentSortTest : public TransmissionTest
{
protected:
    template<typename... KeyVals>
    [[nodiscard]] Torrent make(tr_torrent_id_t const id, KeyVals... keyvals)
    {
        auto tor = Torrent{ prefs_, id };
        tor.update(make_row(id, std::move(keyvals)...));
        return tor;
    }

    tr::app::Prefs prefs_;
};

} // namespace

TEST_F(AppTorrentSortTest, byNameIsLeftToTheCaller)
{
    auto const a = make(1, kv(TR_KEY_name, "alpha"sv));
    auto const b = make(2, kv(TR_KEY_name, "beta"sv));
    EXPECT_EQ(compare_torrents(a, b, SortMode::SortByName), std::partial_ordering::equivalent);
}

TEST_F(AppTorrentSortTest, byQueueOrdersLaterPositionsFirst)
{
    auto const a = make(1, kv(TR_KEY_queue_position, 1));
    auto const b = make(2, kv(TR_KEY_queue_position, 2));
    EXPECT_EQ(compare_torrents(a, b, SortMode::SortByQueue), std::partial_ordering::greater);
    EXPECT_EQ(compare_torrents(b, a, SortMode::SortByQueue), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(a, a, SortMode::SortByQueue), std::partial_ordering::equivalent);
}

TEST_F(AppTorrentSortTest, bySizeAgeAndIdAreAscending)
{
    auto const a = make(1, kv(TR_KEY_size_when_done, 100), kv(TR_KEY_added_date, 1000));
    auto const b = make(2, kv(TR_KEY_size_when_done, 200), kv(TR_KEY_added_date, 2000));
    EXPECT_EQ(compare_torrents(a, b, SortMode::SortBySize), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(a, b, SortMode::SortByAge), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(a, b, SortMode::SortById), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(b, a, SortMode::SortById), std::partial_ordering::greater);
}

TEST_F(AppTorrentSortTest, byActivityComparesSpeedsThenPeersThenState)
{
    // total speed first
    auto const fast = make(1, kv(TR_KEY_rate_download, 100), kv(TR_KEY_rate_upload, 0));
    auto const slow = make(2, kv(TR_KEY_rate_download, 50), kv(TR_KEY_rate_upload, 20));
    EXPECT_EQ(compare_torrents(fast, slow, SortMode::SortByActivity), std::partial_ordering::greater);

    // then the peers we upload to and the web seeds we download from
    auto const busy = make(3, kv(TR_KEY_peers_getting_from_us, 2));
    auto const idle = make(4, kv(TR_KEY_webseeds_sending_to_us, 1));
    EXPECT_EQ(compare_torrents(busy, idle, SortMode::SortByActivity), std::partial_ordering::greater);

    // then the state chain: a paused torrent sorts before a running one
    auto const paused = make(5, kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_STOPPED)));
    auto const running = make(6, kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD)));
    EXPECT_EQ(compare_torrents(paused, running, SortMode::SortByActivity), std::partial_ordering::less);
}

TEST_F(AppTorrentSortTest, byStateComparesPauseThenActivityThenQueueThenError)
{
    auto const paused = make(1, kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_STOPPED)));
    auto const seeding = make(2, kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_SEED)));
    auto const downloading = make(3, kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD)), kv(TR_KEY_queue_position, 1));
    EXPECT_EQ(compare_torrents(paused, seeding, SortMode::SortByState), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(downloading, seeding, SortMode::SortByState), std::partial_ordering::less);

    // same activity: later queue positions first, then torrents with an error
    auto const queued_later = make(
        4,
        kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD)),
        kv(TR_KEY_queue_position, 2),
        kv(TR_KEY_error, static_cast<int64_t>(tr_stat::Error::LocalError)));
    EXPECT_EQ(compare_torrents(downloading, queued_later, SortMode::SortByState), std::partial_ordering::greater);

    auto const same_queue_with_error = make(
        5,
        kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD)),
        kv(TR_KEY_queue_position, 1),
        kv(TR_KEY_error, static_cast<int64_t>(tr_stat::Error::LocalError)));
    EXPECT_EQ(compare_torrents(downloading, same_queue_with_error, SortMode::SortByState), std::partial_ordering::less);
}

TEST_F(AppTorrentSortTest, byProgressComparesMetadataThenCompletionThenSeedProgress)
{
    // a magnet still fetching its metadata sorts first
    auto const magnet = make(1, kv(TR_KEY_metadata_percent_complete, 0.5));
    auto const complete = make(
        2,
        kv(TR_KEY_metadata_percent_complete, 1.0),
        kv(TR_KEY_total_size, 100),
        kv(TR_KEY_have_valid, 50));
    EXPECT_EQ(compare_torrents(magnet, complete, SortMode::SortByProgress), std::partial_ordering::less);

    // then how much of the torrent is here
    auto const half = make(3, kv(TR_KEY_metadata_percent_complete, 1.0), kv(TR_KEY_total_size, 100), kv(TR_KEY_have_valid, 50));
    auto const whole = make(
        4,
        kv(TR_KEY_metadata_percent_complete, 1.0),
        kv(TR_KEY_total_size, 100),
        kv(TR_KEY_have_valid, 100));
    EXPECT_EQ(compare_torrents(half, whole, SortMode::SortByProgress), std::partial_ordering::less);

    // then progress toward the seed ratio: 1.0 of 2.0 is behind 0.9 of 1.0
    auto const seed_a = make(
        5,
        kv(TR_KEY_metadata_percent_complete, 1.0),
        kv(TR_KEY_total_size, 100),
        kv(TR_KEY_have_valid, 100),
        kv(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_SINGLE)),
        kv(TR_KEY_seed_ratio_limit, 2.0),
        kv(TR_KEY_upload_ratio, 1.0));
    auto const seed_b = make(
        6,
        kv(TR_KEY_metadata_percent_complete, 1.0),
        kv(TR_KEY_total_size, 100),
        kv(TR_KEY_have_valid, 100),
        kv(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_SINGLE)),
        kv(TR_KEY_seed_ratio_limit, 1.0),
        kv(TR_KEY_upload_ratio, 0.9));
    EXPECT_EQ(compare_torrents(seed_a, seed_b, SortMode::SortByProgress), std::partial_ordering::less);

    // a torrent with no ratio to reach sorts after one still working toward its own
    auto const unlimited = make(
        7,
        kv(TR_KEY_metadata_percent_complete, 1.0),
        kv(TR_KEY_total_size, 100),
        kv(TR_KEY_have_valid, 100),
        kv(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_UNLIMITED)),
        kv(TR_KEY_upload_ratio, 5.0));
    EXPECT_EQ(compare_torrents(unlimited, seed_b, SortMode::SortByProgress), std::partial_ordering::greater);
}

TEST_F(AppTorrentSortTest, byRatioOrdersNoneBelowNumbersBelowInfinite)
{
    auto const none = make(1, kv(TR_KEY_upload_ratio, -1.0));
    auto const low = make(2, kv(TR_KEY_upload_ratio, 0.5));
    auto const high = make(3, kv(TR_KEY_upload_ratio, 2.0));
    auto const infinite = make(4, kv(TR_KEY_upload_ratio, -2.0));

    EXPECT_EQ(compare_torrents(none, low, SortMode::SortByRatio), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(low, high, SortMode::SortByRatio), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(high, infinite, SortMode::SortByRatio), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(infinite, none, SortMode::SortByRatio), std::partial_ordering::greater);
}

TEST_F(AppTorrentSortTest, byEtaPutsTorrentsWithoutOneFirst)
{
    auto const unknown = make(1, kv(TR_KEY_eta, static_cast<int64_t>(TR_ETA_NOT_AVAIL)));
    auto const soon = make(2, kv(TR_KEY_eta, 100));
    auto const later = make(3, kv(TR_KEY_eta, 200));

    EXPECT_EQ(compare_torrents(unknown, soon, SortMode::SortByEta), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(soon, later, SortMode::SortByEta), std::partial_ordering::less);
    EXPECT_EQ(compare_torrents(later, unknown, SortMode::SortByEta), std::partial_ordering::greater);
    EXPECT_EQ(compare_torrents(unknown, unknown, SortMode::SortByEta), std::partial_ordering::equivalent);
}
