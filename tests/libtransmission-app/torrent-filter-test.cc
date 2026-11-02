// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/types.h>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent-filter.h"
#include "libtransmission-app/torrent.h"

#include "test-fixtures.h"
#include "torrent-get-helpers.h"

using namespace std::literals;
using tr::app::should_show_torrent;
using tr::app::ShowMode;
using tr::app::Torrent;
using tr::app::TorrentFilter;

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

// stands in for the toolkit's Unicode fold
[[nodiscard]] std::string ascii_fold(std::string_view const text)
{
    return tr_strlower(text);
}

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

TEST_F(AppTorrentFilterTest, showModeFieldsOfOneModeNameOnlyWhatItReads)
{
    using tr::app::show_mode_fields;

    EXPECT_FALSE(show_mode_fields(ShowMode::ShowAll).any());
    EXPECT_EQ(Torrent::fields<TR_KEY_status>(), show_mode_fields(ShowMode::ShowDownloading));
    EXPECT_EQ(Torrent::fields<TR_KEY_status>(), show_mode_fields(ShowMode::ShowPaused));
    EXPECT_EQ(Torrent::fields<TR_KEY_status>(), show_mode_fields(ShowMode::ShowSeeding));
    EXPECT_EQ(Torrent::fields<TR_KEY_status>(), show_mode_fields(ShowMode::ShowVerifying));
    EXPECT_EQ(Torrent::fields<TR_KEY_error>(), show_mode_fields(ShowMode::ShowError));
    EXPECT_EQ(Torrent::fields<TR_KEY_is_finished>(), show_mode_fields(ShowMode::ShowFinished));
    EXPECT_EQ(
        (Torrent::fields<TR_KEY_peers_getting_from_us, TR_KEY_peers_sending_to_us, TR_KEY_status>()),
        show_mode_fields(ShowMode::ShowActive));
}

// --- TorrentFilter

TEST_F(AppTorrentFilterTest, matchesEverythingUntilAFilterIsSet)
{
    auto const filter = TorrentFilter{ prefs_, ascii_fold };
    EXPECT_TRUE(filter.matches_all());
    EXPECT_FALSE(filter.fields().any());
    for (auto const activity : Activities) {
        EXPECT_TRUE(filter.matches(make(activity)));
    }
}

TEST_F(AppTorrentFilterTest, filtersByShowMode)
{
    auto filter = TorrentFilter{ prefs_, ascii_fold };
    auto changes = 0;
    auto const tag = filter.changed.connect_scoped([&changes](TorrentFilter::Change /*change*/) { ++changes; });

    prefs_.set(TR_KEY_show_mode, ShowMode::ShowPaused);
    EXPECT_EQ(1, changes);
    EXPECT_FALSE(filter.matches_all());
    EXPECT_TRUE(filter.matches(make(TR_STATUS_STOPPED)));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_DOWNLOAD)));
    EXPECT_EQ(tr::app::show_mode_fields(ShowMode::ShowPaused), filter.fields());
}

TEST_F(AppTorrentFilterTest, filtersByTrackerSite)
{
    auto filter = TorrentFilter{ prefs_, ascii_fold };
    prefs_.set(TR_KEY_filter_trackers, "example"s);

    auto const listed = make(
        TR_STATUS_SEED,
        kv(TR_KEY_trackers, make_list(make_tracker("https://tracker.example.com/announce"sv))));
    auto const other = make(
        TR_STATUS_SEED,
        kv(TR_KEY_trackers, make_list(make_tracker("https://tracker.other.org/announce"sv))));
    EXPECT_TRUE(filter.matches(listed));
    EXPECT_FALSE(filter.matches(other));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_SEED)));
    EXPECT_EQ(Torrent::fields<TR_KEY_trackers>(), filter.fields());

    prefs_.set(TR_KEY_filter_trackers, ""s);
    EXPECT_TRUE(filter.matches(other));
    EXPECT_TRUE(filter.matches_all());
}

TEST_F(AppTorrentFilterTest, filtersByTextInTheNameOrTheHash)
{
    auto filter = TorrentFilter{ prefs_, ascii_fold };
    auto const tor = make(
        TR_STATUS_SEED,
        kv(TR_KEY_name, "Alpha Beta"sv),
        kv(TR_KEY_hash_string, "0123456789abcdef0123456789abcdef01234567"sv));

    prefs_.set(TR_KEY_filter_text, "beta"s);
    EXPECT_TRUE(filter.matches(tor));
    EXPECT_EQ((Torrent::fields<TR_KEY_name, TR_KEY_hash_string>()), filter.fields());

    prefs_.set(TR_KEY_filter_text, "  BETA "s); // folded and stripped
    EXPECT_TRUE(filter.matches(tor));

    prefs_.set(TR_KEY_filter_text, "89ABCDEF"s); // the hash too
    EXPECT_TRUE(filter.matches(tor));

    prefs_.set(TR_KEY_filter_text, "gamma"s);
    EXPECT_FALSE(filter.matches(tor));

    prefs_.set(TR_KEY_filter_text, "   "s); // blank is no filter
    EXPECT_TRUE(filter.matches(tor));
    EXPECT_TRUE(filter.matches_all());
}

TEST_F(AppTorrentFilterTest, filtersCombine)
{
    auto filter = TorrentFilter{ prefs_, ascii_fold };
    prefs_.set(TR_KEY_show_mode, ShowMode::ShowSeeding);
    prefs_.set(TR_KEY_filter_text, "alpha"s);

    EXPECT_TRUE(filter.matches(make(TR_STATUS_SEED, kv(TR_KEY_name, "alpha"sv))));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_DOWNLOAD, kv(TR_KEY_name, "alpha"sv))));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_SEED, kv(TR_KEY_name, "beta"sv))));
    EXPECT_EQ(
        (tr::app::show_mode_fields(ShowMode::ShowSeeding) | Torrent::fields<TR_KEY_name, TR_KEY_hash_string>()),
        filter.fields());
}

TEST_F(AppTorrentFilterTest, readsThePrefsWhenConstructed)
{
    prefs_.set(TR_KEY_show_mode, ShowMode::ShowSeeding);
    prefs_.set(TR_KEY_filter_trackers, "example"s);
    prefs_.set(TR_KEY_filter_text, "ALPHA"s);

    auto const filter = TorrentFilter{ prefs_, ascii_fold };

    auto const trackers = [] {
        return kv(TR_KEY_trackers, make_list(make_tracker("https://tracker.example.com/announce"sv)));
    };
    EXPECT_TRUE(filter.matches(make(TR_STATUS_SEED, trackers(), kv(TR_KEY_name, "alpha"sv))));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_DOWNLOAD, trackers(), kv(TR_KEY_name, "alpha"sv))));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_SEED, kv(TR_KEY_name, "alpha"sv))));
    EXPECT_FALSE(filter.matches(make(TR_STATUS_SEED, trackers(), kv(TR_KEY_name, "beta"sv))));
}

TEST_F(AppTorrentFilterTest, announcesHowStrictEachChangeIs)
{
    using Change = TorrentFilter::Change;
    using Changes = std::vector<Change>;

    auto filter = TorrentFilter{ prefs_, ascii_fold };
    auto changes = Changes{};
    auto const tag = filter.changed.connect_scoped([&changes](Change const change) { changes.push_back(change); });

    prefs_.set(TR_KEY_show_mode, ShowMode::ShowSeeding);
    prefs_.set(TR_KEY_show_mode, ShowMode::ShowPaused);
    prefs_.set(TR_KEY_show_mode, ShowMode::ShowAll);
    EXPECT_EQ((Changes{ Change::MoreStrict, Change::Different, Change::LessStrict }), changes);
    changes.clear();

    prefs_.set(TR_KEY_filter_trackers, "example"s);
    prefs_.set(TR_KEY_filter_trackers, "other"s);
    prefs_.set(TR_KEY_filter_trackers, ""s);
    EXPECT_EQ((Changes{ Change::MoreStrict, Change::Different, Change::LessStrict }), changes);
    changes.clear();

    prefs_.set(TR_KEY_filter_text, "alp"s);
    prefs_.set(TR_KEY_filter_text, "alpha"s);
    prefs_.set(TR_KEY_filter_text, "alp"s);
    prefs_.set(TR_KEY_filter_text, "beta"s);
    prefs_.set(TR_KEY_filter_text, "BETA"s); // folds to the same text: no change
    prefs_.set(TR_KEY_filter_text, ""s);
    EXPECT_EQ(
        (Changes{ Change::MoreStrict, Change::MoreStrict, Change::LessStrict, Change::Different, Change::LessStrict }),
        changes);
}

TEST_F(AppTorrentFilterTest, ignoresOtherPrefs)
{
    auto filter = TorrentFilter{ prefs_, ascii_fold };
    auto changes = 0;
    auto const tag = filter.changed.connect_scoped([&changes](TorrentFilter::Change /*change*/) { ++changes; });

    prefs_.set(TR_KEY_sort_reversed, !prefs_.get<bool>(TR_KEY_sort_reversed));
    EXPECT_EQ(0, changes);
    EXPECT_TRUE(filter.matches_all());
}
