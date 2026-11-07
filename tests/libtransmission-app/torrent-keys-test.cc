// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iterator> // std::back_inserter()
#include <span>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>

#include "libtransmission-app/torrent-keys.h"

using namespace tr::app;

namespace
{

[[nodiscard]] bool has(TorrentKeySet const set, tr_quark const key)
{
    return std::ranges::binary_search(torrent_keys(set), key);
}

} // namespace

// Sorted, unique, carrying `id` and feeding only `Torrent` fields are the header's static_asserts.

TEST(TorrentKeysTest, theAllSetsAreTheUnionOfTheirInfoAndStatsSets)
{
    for (
        auto const [all, info, stats] :
        { std::tuple{ TorrentKeySet::MainAll, TorrentKeySet::MainInfo, TorrentKeySet::MainStats },
          std::tuple{ TorrentKeySet::DetailAll, TorrentKeySet::DetailInfo, TorrentKeySet::DetailStats } }) {
        auto expected = std::vector<tr_quark>{};
        std::ranges::set_union(torrent_keys(info), torrent_keys(stats), std::back_inserter(expected));
        EXPECT_TRUE(std::ranges::equal(torrent_keys(all), expected)) << static_cast<int>(all);
    }
}

// A poll refreshes only what changes on its own; a stats set that carried `name`
// would ask the session to send every name every few seconds.
TEST(TorrentKeysTest, statsSetsOmitTheInfoOnlyFields)
{
    for (auto const set : { TorrentKeySet::MainStats, TorrentKeySet::DetailStats }) {
        EXPECT_FALSE(has(set, TR_KEY_name));
        EXPECT_FALSE(has(set, TR_KEY_hash_string));
        EXPECT_FALSE(has(set, TR_KEY_added_date));
    }
}

// The GTK client shows "Stalled" and a webseed tab, and keys its trackers
// change mask on `tracker_list`, so those keys travel with the main sets.
TEST(TorrentKeysTest, carriesTheFieldsBothClientsShow)
{
    EXPECT_TRUE(has(TorrentKeySet::MainStats, TR_KEY_is_stalled));
    EXPECT_TRUE(has(TorrentKeySet::MainInfo, TR_KEY_tracker_list));
    EXPECT_TRUE(has(TorrentKeySet::DetailInfo, TR_KEY_webseeds_ex));
}

// `edit_date` tells a `torrent_set` re-fetch that the metainfo changed; `labels`
// and `tracker_list` show an edit even when the date did not move; the seed ratio
// keys let the dialog's ratio widgets settle on that same re-fetch.
TEST(TorrentKeysTest, detailStatsSettleATorrentSet)
{
    EXPECT_TRUE(has(TorrentKeySet::DetailStats, TR_KEY_edit_date));
    EXPECT_TRUE(has(TorrentKeySet::DetailStats, TR_KEY_labels));
    EXPECT_TRUE(has(TorrentKeySet::DetailStats, TR_KEY_tracker_list));
    EXPECT_TRUE(has(TorrentKeySet::DetailStats, TR_KEY_seed_ratio_limit));
    EXPECT_TRUE(has(TorrentKeySet::DetailStats, TR_KEY_seed_ratio_mode));
    EXPECT_TRUE(has(TorrentKeySet::MainStats, TR_KEY_edit_date));
}
