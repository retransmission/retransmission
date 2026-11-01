// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/types.h>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent-filter.h"
#include "libtransmission-app/torrent.h"

#include "test-fixtures.h"
#include "torrent-get-helpers.h"

using tr::app::should_show_torrent;
using tr::app::ShowMode;
using tr::app::Torrent;

namespace
{

auto constexpr Activities = std::array{ TR_STATUS_STOPPED,  TR_STATUS_CHECK_WAIT, TR_STATUS_CHECK, TR_STATUS_DOWNLOAD_WAIT,
                                        TR_STATUS_DOWNLOAD, TR_STATUS_SEED_WAIT,  TR_STATUS_SEED };

class AppTorrentFilterTest : public TransmissionTest
{
protected:
    // a torrent in `activity`, plus `keyvals`
    template<typename... KeyVals>
    [[nodiscard]] Torrent make(tr_torrent_activity const activity, KeyVals... keyvals)
    {
        auto tor = Torrent{ prefs_, 1 };
        tor.update(make_row(1, kv(TR_KEY_status, static_cast<int64_t>(activity)), std::move(keyvals)...));
        return tor;
    }

    tr::app::Prefs prefs_;
};

} // namespace

TEST_F(AppTorrentFilterTest, showAllShowsEverything)
{
    for (auto const activity : Activities) {
        EXPECT_TRUE(should_show_torrent(make(activity), ShowMode::ShowAll));
    }
}

TEST_F(AppTorrentFilterTest, showActiveNeedsATransferOrAVerify)
{
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_DOWNLOAD), ShowMode::ShowActive));
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_SEED), ShowMode::ShowActive));
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_STOPPED), ShowMode::ShowActive));

    EXPECT_TRUE(should_show_torrent(make(TR_STATUS_DOWNLOAD, kv(TR_KEY_peers_sending_to_us, 1)), ShowMode::ShowActive));
    EXPECT_TRUE(should_show_torrent(make(TR_STATUS_SEED, kv(TR_KEY_peers_getting_from_us, 1)), ShowMode::ShowActive));
    EXPECT_TRUE(should_show_torrent(make(TR_STATUS_CHECK), ShowMode::ShowActive));

    // a web seed alone doesn't count
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_DOWNLOAD, kv(TR_KEY_webseeds_sending_to_us, 1)), ShowMode::ShowActive));
}

TEST_F(AppTorrentFilterTest, activityModesFollowTheStatus)
{
    for (auto const activity : Activities) {
        auto const tor = make(activity);

        EXPECT_EQ(
            should_show_torrent(tor, ShowMode::ShowDownloading),
            activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_DOWNLOAD_WAIT);
        EXPECT_EQ(
            should_show_torrent(tor, ShowMode::ShowSeeding),
            activity == TR_STATUS_SEED || activity == TR_STATUS_SEED_WAIT);
        EXPECT_EQ(
            should_show_torrent(tor, ShowMode::ShowVerifying),
            activity == TR_STATUS_CHECK || activity == TR_STATUS_CHECK_WAIT);
        EXPECT_EQ(should_show_torrent(tor, ShowMode::ShowPaused), activity == TR_STATUS_STOPPED);
    }
}

TEST_F(AppTorrentFilterTest, showFinishedNeedsTheFlag)
{
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_STOPPED), ShowMode::ShowFinished));
    EXPECT_TRUE(should_show_torrent(make(TR_STATUS_STOPPED, kv(TR_KEY_is_finished, true)), ShowMode::ShowFinished));
}

TEST_F(AppTorrentFilterTest, showErrorNeedsAnError)
{
    EXPECT_FALSE(should_show_torrent(make(TR_STATUS_DOWNLOAD), ShowMode::ShowError));
    EXPECT_TRUE(should_show_torrent(
        make(TR_STATUS_DOWNLOAD, kv(TR_KEY_error, static_cast<int64_t>(tr_stat::Error::TrackerWarning))),
        ShowMode::ShowError));
}

TEST_F(AppTorrentFilterTest, showModeFieldsNameWhatThePredicateReads)
{
    auto const fields = tr::app::show_mode_fields();
    EXPECT_EQ(fields.count(), 5U);
    EXPECT_TRUE((fields & Torrent::fields<TR_KEY_status>()).any());
    EXPECT_TRUE((fields & Torrent::fields<TR_KEY_error>()).any());
    EXPECT_TRUE((fields & Torrent::fields<TR_KEY_is_finished>()).any());
    EXPECT_TRUE((fields & Torrent::fields<TR_KEY_peers_getting_from_us>()).any());
    EXPECT_TRUE((fields & Torrent::fields<TR_KEY_peers_sending_to_us>()).any());
}
