// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_sha1_from_string()
#include <libtransmission/quark.h>
#include <libtransmission/shared-string.h>
#include <libtransmission/types.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent.h"

#include "test-fixtures.h"

using namespace std::literals;

using tr::app::Torrent;
using Field = tr::app::TorrentField;
using FieldSet = tr::app::TorrentFieldSet;
using Percent = Torrent::Percent;
using Ratio = Torrent::Ratio;
using Speed = Torrent::Speed;
using Storage = Torrent::Storage;

namespace
{

auto constexpr HashString = "f0e1d2c3b4a5968778695a4b3c2d1e0f10203040"sv;

[[nodiscard]] tr_variant::Map make_file(std::string_view const name, int64_t const length)
{
    auto file = tr_variant::Map{ 2U };
    file.try_emplace(TR_KEY_name, name);
    file.try_emplace(TR_KEY_length, length);
    return file;
}

[[nodiscard]] tr_variant::Map make_file_stat(int64_t const bytes_completed, int64_t const priority, bool const wanted)
{
    auto stat = tr_variant::Map{ 3U };
    stat.try_emplace(TR_KEY_bytes_completed, bytes_completed);
    stat.try_emplace(TR_KEY_priority, priority);
    stat.try_emplace(TR_KEY_wanted, wanted);
    return stat;
}

[[nodiscard]] tr_variant::Map make_tracker(std::string_view const announce, std::string_view const sitename, int64_t const tier)
{
    auto tracker = tr_variant::Map{ 3U };
    tracker.try_emplace(TR_KEY_announce, announce);
    if (!std::empty(sitename)) {
        tracker.try_emplace(TR_KEY_sitename, sitename);
    }
    tracker.try_emplace(TR_KEY_tier, tier);
    return tracker;
}

[[nodiscard]] tr_variant::Map make_peer(std::string_view const address, int64_t const rate_to_client)
{
    auto peer = tr_variant::Map{ 2U };
    peer.try_emplace(TR_KEY_address, address);
    peer.try_emplace(TR_KEY_rate_to_client, rate_to_client);
    return peer;
}

[[nodiscard]] tr_variant::Map make_webseed(std::string_view const url, bool const is_downloading, int64_t const byps)
{
    auto webseed = tr_variant::Map{ 3U };
    webseed.try_emplace(TR_KEY_url, url);
    webseed.try_emplace(TR_KEY_is_downloading, is_downloading);
    webseed.try_emplace(TR_KEY_download_bytes_per_second, byps);
    return webseed;
}

template<typename... Maps>
[[nodiscard]] tr_variant::Vector make_list(Maps&&... maps)
{
    auto list = tr_variant::Vector{};
    list.reserve(sizeof...(Maps));
    (list.emplace_back(std::forward<Maps>(maps)), ...);
    return list;
}

// A row that sets every tracked field to a value other than its default.
[[nodiscard]] tr_variant::Map make_full_row()
{
    auto row = tr_variant::Map{};

    row.try_emplace(TR_KEY_id, 1); // immutable; ignored
    row.try_emplace(TR_KEY_activity_date, 1'700'000'001);
    row.try_emplace(TR_KEY_added_date, 1'700'000'002);
    row.try_emplace(TR_KEY_bandwidth_priority, static_cast<int64_t>(TR_PRI_HIGH));
    row.try_emplace(TR_KEY_checked_bytes, 8);
    row.try_emplace(TR_KEY_comment, "a comment"sv);
    row.try_emplace(TR_KEY_corrupt_ever, 11);
    row.try_emplace(TR_KEY_creator, "a creator"sv);
    row.try_emplace(TR_KEY_date_created, 1'700'000'003);
    row.try_emplace(TR_KEY_desired_available, 12);
    row.try_emplace(TR_KEY_download_dir, "/downloads"sv);
    row.try_emplace(TR_KEY_download_limit, 100); // KB/s
    row.try_emplace(TR_KEY_download_limited, true);
    row.try_emplace(TR_KEY_downloaded_ever, 13);
    row.try_emplace(TR_KEY_edit_date, 1'700'000'004);
    row.try_emplace(TR_KEY_error, static_cast<int64_t>(tr_stat::Error::TrackerWarning));
    row.try_emplace(TR_KEY_error_string, "a warning"sv);
    row.try_emplace(TR_KEY_eta, 120);
    row.try_emplace(TR_KEY_file_count, 2);
    row.try_emplace(TR_KEY_file_stats, make_list(make_file_stat(5, TR_PRI_HIGH, true), make_file_stat(0, TR_PRI_LOW, false)));
    row.try_emplace(TR_KEY_files, make_list(make_file("dir/a.txt"sv, 10), make_file("dir/b.txt"sv, 20)));
    row.try_emplace(TR_KEY_hash_string, HashString);
    row.try_emplace(TR_KEY_have_unchecked, 14);
    row.try_emplace(TR_KEY_have_valid, 15);
    row.try_emplace(TR_KEY_honors_session_limits, false);
    row.try_emplace(TR_KEY_is_finished, true);
    row.try_emplace(TR_KEY_is_folder, true);
    row.try_emplace(TR_KEY_is_private, true);
    row.try_emplace(TR_KEY_is_stalled, true);
    row.try_emplace(TR_KEY_labels, make_list(tr_variant{ "red"sv }, tr_variant{ "green"sv }));
    row.try_emplace(TR_KEY_left_until_done, 16);
    row.try_emplace(TR_KEY_manual_announce_time, 1'700'000'005);
    row.try_emplace(TR_KEY_metadata_have, 36);
    row.try_emplace(TR_KEY_metadata_percent_complete, 1.0);
    row.try_emplace(TR_KEY_metadata_size, 36);
    row.try_emplace(TR_KEY_name, "a name"sv);
    row.try_emplace(TR_KEY_peer_limit, 17);
    row.try_emplace(TR_KEY_peers, make_list(make_peer("127.0.0.1"sv, 1000)));
    row.try_emplace(TR_KEY_peers_connected, 18);
    row.try_emplace(TR_KEY_peers_getting_from_us, 19);
    row.try_emplace(TR_KEY_peers_sending_to_us, 20);
    row.try_emplace(TR_KEY_piece_count, 23);
    row.try_emplace(TR_KEY_piece_size, 24);
    row.try_emplace(TR_KEY_primary_mime_type, "text/plain"sv);
    row.try_emplace(TR_KEY_queue_position, 25);
    row.try_emplace(TR_KEY_rate_download, 26); // B/s
    row.try_emplace(TR_KEY_rate_upload, 27); // B/s
    row.try_emplace(TR_KEY_recheck_progress, 0.28);
    row.try_emplace(TR_KEY_seed_idle_limit, 29);
    row.try_emplace(TR_KEY_seed_idle_mode, static_cast<int64_t>(TR_IDLELIMIT_SINGLE));
    row.try_emplace(TR_KEY_seed_ratio_limit, 3.0);
    row.try_emplace(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_SINGLE));
    row.try_emplace(TR_KEY_size_when_done, 31);
    row.try_emplace(TR_KEY_start_date, 1'700'000'006);
    row.try_emplace(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD));
    row.try_emplace(TR_KEY_total_size, 32);
    row.try_emplace(TR_KEY_tracker_list, "https://tracker.example.com/announce\n"sv);
    row.try_emplace(TR_KEY_tracker_stats, make_list(make_tracker("https://tracker.example.com/announce"sv, "example"sv, 0)));
    row.try_emplace(TR_KEY_trackers, make_list(make_tracker("https://tracker.example.com/announce"sv, "example"sv, 0)));
    row.try_emplace(TR_KEY_upload_limit, 200); // KB/s
    row.try_emplace(TR_KEY_upload_limited, true);
    row.try_emplace(TR_KEY_upload_ratio, 0.5);
    row.try_emplace(TR_KEY_uploaded_ever, 33);
    row.try_emplace(TR_KEY_webseeds_ex, make_list(make_webseed("https://webseed.example.com/"sv, true, 34)));
    row.try_emplace(TR_KEY_webseeds_sending_to_us, 35);

    return row;
}

// Splits an object-format row into the keys and values of a table-format row.
[[nodiscard]] std::pair<std::vector<tr_quark>, tr_variant::Vector> to_table_row(tr_variant::Map const& row)
{
    auto keys = std::vector<tr_quark>{};
    auto values = tr_variant::Vector{};

    for (auto const& [key, value] : row) {
        keys.push_back(key);
        values.emplace_back(value.clone());
    }

    return { std::move(keys), std::move(values) };
}

[[nodiscard]] tr_variant::Map make_row(tr_quark const key, tr_variant value)
{
    auto row = tr_variant::Map{ 1U };
    row.try_emplace(key, std::move(value));
    return row;
}

[[nodiscard]] tr_variant::Map make_metadata_row(int64_t const have, int64_t const size)
{
    auto row = tr_variant::Map{ 2U };
    row.try_emplace(TR_KEY_metadata_have, have);
    row.try_emplace(TR_KEY_metadata_size, size);
    return row;
}

class AppTorrentTest : public TransmissionTest
{
protected:
    tr::app::Prefs prefs_;
};

} // namespace

TEST_F(AppTorrentTest, parsesObjectRow)
{
    auto tor = Torrent{ prefs_, 1 };
    auto const changed = tor.update(make_full_row());

    EXPECT_EQ(tor.id(), 1);
    EXPECT_EQ(tor.activity_date(), 1'700'000'001);
    EXPECT_EQ(tor.added_date(), 1'700'000'002);
    EXPECT_EQ(tor.bandwidth_priority(), TR_PRI_HIGH);
    EXPECT_EQ(tor.comment(), "a comment"sv);
    EXPECT_EQ(tor.corrupt_ever(), Storage(11, Storage::Units::Bytes));
    EXPECT_EQ(tor.creator(), "a creator"sv);
    EXPECT_EQ(tor.date_created(), 1'700'000'003);
    EXPECT_EQ(tor.desired_available(), Storage(12, Storage::Units::Bytes));
    EXPECT_EQ(tor.download_dir(), "/downloads"sv);
    EXPECT_EQ(tor.download_limit(), Speed(100, Speed::Units::KByps));
    EXPECT_TRUE(tor.download_limited());
    EXPECT_EQ(tor.downloaded_ever(), Storage(13, Storage::Units::Bytes));
    EXPECT_EQ(tor.edit_date(), 1'700'000'004);
    EXPECT_EQ(tor.error_code(), tr_stat::Error::TrackerWarning);
    EXPECT_TRUE(tor.has_error());
    EXPECT_EQ(tor.error_string(), "a warning"sv);
    EXPECT_EQ(tor.eta(), 120);
    EXPECT_TRUE(tor.has_eta());
    EXPECT_EQ(tor.file_count(), 2U);
    EXPECT_EQ(tor.hash(), *tr_sha1_from_string(HashString));
    EXPECT_EQ(tor.hash_string(), HashString);
    EXPECT_EQ(tor.have_unchecked(), Storage(14, Storage::Units::Bytes));
    EXPECT_EQ(tor.have_valid(), Storage(15, Storage::Units::Bytes));
    EXPECT_EQ(tor.have_total(), Storage(29, Storage::Units::Bytes));
    EXPECT_FALSE(tor.honors_session_limits());
    EXPECT_TRUE(tor.is_finished());
    EXPECT_TRUE(tor.is_folder());
    EXPECT_TRUE(tor.is_private());
    EXPECT_TRUE(tor.is_stalled());
    EXPECT_EQ(tor.labels(), (std::vector<std::string>{ "red", "green" }));
    EXPECT_EQ(tor.left_until_done(), Storage(16, Storage::Units::Bytes));
    EXPECT_FALSE(tor.is_done());
    EXPECT_EQ(tor.manual_announce_time(), 1'700'000'005);
    EXPECT_EQ(tor.metadata_percent_complete(), (Percent{ 36U, 36U }));
    EXPECT_TRUE(tor.has_metadata());
    EXPECT_EQ(tor.name(), "a name"sv);
    EXPECT_TRUE(tor.has_name());
    EXPECT_EQ(tor.peer_limit(), 17U);
    EXPECT_EQ(tor.peers_connected(), 18U);
    EXPECT_EQ(tor.peers_getting_from_us(), 19U);
    EXPECT_EQ(tor.peers_sending_to_us(), 20U);
    EXPECT_EQ(tor.percent_complete(), (Percent{ 29U, 32U }));
    EXPECT_EQ(tor.percent_done(), (Percent{ 15U, 31U }));
    EXPECT_EQ(tor.piece_count(), 23U);
    EXPECT_EQ(tor.piece_size(), 24U);
    EXPECT_EQ(tor.primary_mime_type(), "text/plain"sv);
    EXPECT_EQ(tor.queue_position(), 25U);
    EXPECT_EQ(tor.download_speed(), Speed(26, Speed::Units::Byps));
    EXPECT_EQ(tor.upload_speed(), Speed(27, Speed::Units::Byps));
    EXPECT_EQ(tor.recheck_progress(), (Percent{ 8U, 32U }));
    EXPECT_EQ(tor.seed_idle_limit(), 29U);
    EXPECT_EQ(tor.seed_idle_mode(), TR_IDLELIMIT_SINGLE);
    EXPECT_DOUBLE_EQ(tor.seed_ratio_limit(), 3.0);
    EXPECT_EQ(tor.seed_ratio_mode(), TR_RATIOLIMIT_SINGLE);
    EXPECT_EQ(tor.size_when_done(), Storage(31, Storage::Units::Bytes));
    EXPECT_EQ(tor.start_date(), 1'700'000'006);
    EXPECT_EQ(tor.activity(), TR_STATUS_DOWNLOAD);
    EXPECT_TRUE(tor.is_downloading());
    EXPECT_TRUE(tor.is_ready_to_transfer());
    EXPECT_FALSE(tor.is_paused());
    EXPECT_FALSE(tor.is_queued());
    EXPECT_EQ(tor.total_size(), Storage(32, Storage::Units::Bytes));
    EXPECT_FALSE(tor.is_seed());
    EXPECT_EQ(tor.tracker_list(), "https://tracker.example.com/announce\n"sv);
    EXPECT_EQ(tor.upload_limit(), Speed(200, Speed::Units::KByps));
    EXPECT_TRUE(tor.upload_limited());
    EXPECT_EQ(tor.upload_ratio(), (Ratio{ 1U, 2U }));
    EXPECT_EQ(tor.uploaded_ever(), Storage(33, Storage::Units::Bytes));
    EXPECT_EQ(tor.webseeds_sending_to_us(), 35U);
    EXPECT_EQ(tor.active_peers_down(), 55U);
    EXPECT_EQ(tor.active_peers_up(), 19U);
    EXPECT_EQ(tor.active_peer_count(), 74U);

    ASSERT_EQ(std::size(tor.files()), 2U);
    EXPECT_EQ(tor.files()[0].name, "dir/a.txt");
    EXPECT_EQ(tor.files()[0].size, 10U);
    EXPECT_EQ(tor.files()[0].have, 5U);
    EXPECT_EQ(tor.files()[0].priority, TR_PRI_HIGH);
    EXPECT_TRUE(tor.files()[0].wanted);
    EXPECT_EQ(tor.files()[0].index, 0U);
    EXPECT_EQ(tor.files()[1].name, "dir/b.txt");
    EXPECT_EQ(tor.files()[1].size, 20U);
    EXPECT_EQ(tor.files()[1].have, 0U);
    EXPECT_EQ(tor.files()[1].priority, TR_PRI_LOW);
    EXPECT_FALSE(tor.files()[1].wanted);
    EXPECT_EQ(tor.files()[1].index, 1U);

    ASSERT_EQ(std::size(tor.peers()), 1U);
    EXPECT_EQ(tor.peers()[0].address, "127.0.0.1");
    EXPECT_EQ(tor.peers()[0].rate_to_client, Speed(1000, Speed::Units::Byps));

    ASSERT_EQ(std::size(tor.tracker_stats()), 1U);
    EXPECT_EQ(tor.tracker_stats()[0].announce, "https://tracker.example.com/announce");
    EXPECT_EQ(tor.tracker_stats()[0].sitename, "example");
    EXPECT_EQ(tor.sitenames(), (std::vector<std::string>{ "example" }));
    EXPECT_TRUE(tor.includes_tracker("example"sv));
    EXPECT_FALSE(tor.includes_tracker("other"sv));

    ASSERT_EQ(std::size(tor.webseeds()), 1U);
    EXPECT_EQ(tor.webseeds()[0].url, "https://webseed.example.com/");
    EXPECT_TRUE(tor.webseeds()[0].is_downloading);
    EXPECT_EQ(tor.webseeds()[0].download_speed, Speed(34, Speed::Units::Byps));

    // every field changed from its default, and each one reported exactly one bit
    EXPECT_EQ(changed.count(), static_cast<size_t>(Field::N_FIELDS));
}

TEST_F(AppTorrentTest, everyFieldsKeyReportsABitAndEveryBitHasAKey)
{
    auto bits = FieldSet{};
    auto n_keys = size_t{};

    std::apply(
        [&bits, &n_keys](auto const&... field) {
            (
                [&] {
                    ++n_keys;
                    auto const bit = Torrent::field(field.key);
                    ASSERT_TRUE(bit.has_value()) << tr_quark_get_string_view(field.key);
                    bits.set(*bit);
                }(),
                ...);
        },
        Torrent::Fields);

    EXPECT_EQ(bits.count(), static_cast<size_t>(Field::N_FIELDS));
    EXPECT_EQ(n_keys, static_cast<size_t>(Field::N_FIELDS) + 2U); // files/file_stats, trackers/tracker_stats

    EXPECT_EQ(Torrent::field(TR_KEY_id), std::nullopt);
    EXPECT_EQ(Torrent::field(TR_KEY_magnet_link), std::nullopt);
}

TEST_F(AppTorrentTest, identicalUpdateChangesNothing)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_full_row()).any());

    auto const changed = tor.update(make_full_row());
    EXPECT_TRUE(changed.none());
    EXPECT_EQ(changed.count(), 0U);
    EXPECT_EQ(tor.name(), "a name"sv);
}

TEST_F(AppTorrentTest, tableRowMatchesObjectRow)
{
    auto const row = make_full_row();
    auto const [keys, values] = to_table_row(row);

    auto from_object = Torrent{ prefs_, 1 };
    auto const object_changed = from_object.update(row);

    auto from_table = Torrent{ prefs_, 1 };
    auto const table_changed = from_table.update(keys, values);

    EXPECT_EQ(object_changed, table_changed);
    EXPECT_EQ(table_changed.count(), static_cast<size_t>(Field::N_FIELDS));
    EXPECT_EQ(from_table.name(), from_object.name());
    EXPECT_EQ(from_table.hash(), from_object.hash());
    EXPECT_EQ(from_table.download_dir(), from_object.download_dir());
    EXPECT_EQ(from_table.total_size(), from_object.total_size());
    EXPECT_EQ(from_table.activity(), from_object.activity());
    EXPECT_EQ(std::size(from_table.files()), std::size(from_object.files()));
    EXPECT_EQ(from_table.files()[1].name, from_object.files()[1].name);
    EXPECT_EQ(from_table.sitenames(), from_object.sitenames());
}

TEST_F(AppTorrentTest, tableRowWithFewerValuesThanKeysAppliesThePairs)
{
    auto const keys = std::vector<tr_quark>{ TR_KEY_name, TR_KEY_total_size, TR_KEY_eta };
    auto values = tr_variant::Vector{};
    values.emplace_back("short row"sv);
    values.emplace_back(99);

    auto tor = Torrent{ prefs_, 1 };
    auto const changed = tor.update(keys, values);

    EXPECT_EQ(changed, Field::NAME | Field::TOTAL_SIZE);
    EXPECT_EQ(tor.name(), "short row"sv);
    EXPECT_EQ(tor.total_size(), Storage(99, Storage::Units::Bytes));
    EXPECT_EQ(tor.eta(), TR_ETA_NOT_AVAIL);
}

TEST_F(AppTorrentTest, ignoresUnknownKeysAndWrongTypes)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_full_row()).any());

    // untracked keys
    EXPECT_TRUE(tor.update(make_row(TR_KEY_id, tr_variant{ 2 })).none());
    EXPECT_TRUE(tor.update(make_row(TR_KEY_magnet_link, tr_variant{ "magnet:?xt=urn:btih:00"sv })).none());
    EXPECT_EQ(tor.id(), 1);

    // wrong types keep the old values
    EXPECT_TRUE(tor.update(make_row(TR_KEY_name, tr_variant{ 42 })).none());
    EXPECT_EQ(tor.name(), "a name"sv);
    EXPECT_TRUE(tor.update(make_row(TR_KEY_total_size, tr_variant{ "lots"sv })).none());
    EXPECT_EQ(tor.total_size(), Storage(32, Storage::Units::Bytes));
    EXPECT_TRUE(tor.update(make_row(TR_KEY_files, tr_variant{ "not a list"sv })).none());
    EXPECT_EQ(std::size(tor.files()), 2U);
    EXPECT_TRUE(tor.update(make_row(TR_KEY_hash_string, tr_variant{ "not a hash"sv })).none());
    EXPECT_EQ(tor.hash_string(), HashString);

    // enum values out of range
    EXPECT_TRUE(tor.update(make_row(TR_KEY_status, tr_variant{ 99 })).none());
    EXPECT_EQ(tor.activity(), TR_STATUS_DOWNLOAD);
    EXPECT_TRUE(tor.update(make_row(TR_KEY_error, tr_variant{ -1 })).none());
    EXPECT_EQ(tor.error_code(), tr_stat::Error::TrackerWarning);
    EXPECT_TRUE(tor.update(make_row(TR_KEY_bandwidth_priority, tr_variant{ 2 })).none());
    EXPECT_EQ(tor.bandwidth_priority(), TR_PRI_HIGH);
}

TEST_F(AppTorrentTest, partialRowLeavesOtherFieldsIntact)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_full_row()).any());

    auto const changed = tor.update(make_row(TR_KEY_status, tr_variant{ static_cast<int64_t>(TR_STATUS_SEED) }));

    EXPECT_EQ(changed, FieldSet{ Field::STATUS });
    EXPECT_TRUE(tor.is_seeding());
    EXPECT_EQ(tor.name(), "a name"sv);
    EXPECT_EQ(tor.total_size(), Storage(32, Storage::Units::Bytes));
    EXPECT_EQ(std::size(tor.files()), 2U);
}

TEST_F(AppTorrentTest, metadataPercentFromCounts)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_EQ(tor.update(make_metadata_row(500, 1000)), Field::METADATA_HAVE | Field::METADATA_SIZE);
    EXPECT_EQ(tor.metadata_percent_complete(), (Percent{ 500U, 1000U }));
    EXPECT_FALSE(tor.has_metadata());

    EXPECT_EQ(tor.update(make_row(TR_KEY_metadata_have, tr_variant{ 1000 })), FieldSet{ Field::METADATA_HAVE });
    EXPECT_TRUE(tor.has_metadata());
}

// no peer has reported the size yet
TEST_F(AppTorrentTest, metadataSizeZeroIsUnknown)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_metadata_row(0, 0)).any());
    EXPECT_EQ(tor.metadata_percent_complete(), Percent{});
    EXPECT_FALSE(tor.has_metadata());
}

// daemons that predate `metadata_have` and `metadata_size`
TEST_F(AppTorrentTest, metadataPercentFallsBackToTheDouble)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_row(TR_KEY_metadata_percent_complete, tr_variant{ 0.5 })).any());
    EXPECT_EQ(tor.metadata_percent_complete(), (Percent{ 1U, 2U }));
    EXPECT_FALSE(tor.has_metadata());

    EXPECT_TRUE(tor.update(make_row(TR_KEY_metadata_percent_complete, tr_variant{ 1.0 })).any());
    EXPECT_TRUE(tor.has_metadata());
}

TEST_F(AppTorrentTest, percentsFromByteCounts)
{
    auto row = make_metadata_row(1000, 1000);
    row.try_emplace(TR_KEY_size_when_done, 100);
    row.try_emplace(TR_KEY_left_until_done, 25);
    row.try_emplace(TR_KEY_have_valid, 60);
    row.try_emplace(TR_KEY_have_unchecked, 15);
    row.try_emplace(TR_KEY_total_size, 200);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());
    EXPECT_EQ(tor.percent_done(), (Percent{ 75U, 100U }));
    EXPECT_EQ(tor.percent_complete(), (Percent{ 75U, 200U }));
}

// every size is 0 until the metadata arrives, which would read as complete
TEST_F(AppTorrentTest, percentsNeedMetadata)
{
    auto row = make_metadata_row(0, 0);
    row.try_emplace(TR_KEY_size_when_done, 0);
    row.try_emplace(TR_KEY_left_until_done, 0);
    row.try_emplace(TR_KEY_total_size, 0);
    row.try_emplace(TR_KEY_checked_bytes, 0);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());
    EXPECT_FALSE(tor.percent_done().is_complete());
    EXPECT_FALSE(tor.percent_complete().is_complete());
    EXPECT_FALSE(tor.recheck_progress().is_complete());
}

TEST_F(AppTorrentTest, recheckProgressFromCheckedBytes)
{
    auto row = make_metadata_row(1000, 1000);
    row.try_emplace(TR_KEY_total_size, 200);
    row.try_emplace(TR_KEY_checked_bytes, 50);
    row.try_emplace(TR_KEY_recheck_progress, 0.9);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());
    EXPECT_EQ(tor.recheck_progress(), (Percent{ 50U, 200U }));
}

// daemons that predate `checked_bytes`
TEST_F(AppTorrentTest, recheckProgressFallsBackToTheDouble)
{
    auto row = tr_variant::Map{ 2U };
    row.try_emplace(TR_KEY_metadata_percent_complete, 1.0);
    row.try_emplace(TR_KEY_recheck_progress, 0.25);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());
    EXPECT_EQ(tor.recheck_progress(), (Percent{ 1U, 4U }));
}

TEST_F(AppTorrentTest, peerProgressFromHaveTotal)
{
    auto row = make_metadata_row(1000, 1000);
    row.try_emplace(TR_KEY_total_size, 200);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());

    auto peer = tr::app::Peer{};
    peer.have_total = 50U;
    peer.progress = 0.9;
    EXPECT_EQ(tor.peer_progress(peer), (Percent{ 50U, 200U }));
}

// `have_total` and `total_size` are both 0 until the metadata arrives
TEST_F(AppTorrentTest, peerProgressBeforeMetadataUsesTheDouble)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_metadata_row(0, 0)).any());

    auto peer = tr::app::Peer{};
    peer.have_total = 0U;
    peer.progress = 1.0;
    EXPECT_TRUE(tor.peer_progress(peer).is_complete());
}

// daemons that predate `have_total`
TEST_F(AppTorrentTest, peerProgressFallsBackToTheDouble)
{
    auto row = tr_variant::Map{ 2U };
    row.try_emplace(TR_KEY_metadata_percent_complete, 1.0);
    row.try_emplace(TR_KEY_total_size, 200);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());

    auto peer = tr::app::Peer{};
    peer.progress = 0.25;
    EXPECT_EQ(tor.peer_progress(peer), (Percent{ 1U, 4U }));
}

TEST_F(AppTorrentTest, fileProgressFromByteCounts)
{
    auto file = tr::app::TorrentFile{};
    file.size = 20U;
    file.have = 5U;
    EXPECT_EQ(file.progress(), (Percent{ 5U, 20U }));

    // an empty file is complete
    EXPECT_TRUE(tr::app::TorrentFile{}.progress().is_complete());
}

TEST_F(AppTorrentTest, uploadRatioDecodesTheWireStates)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_EQ(tor.update(make_row(TR_KEY_upload_ratio, tr_variant{ 0.5 })), FieldSet{ Field::UPLOAD_RATIO });
    EXPECT_EQ(tor.upload_ratio(), (Ratio{ 1U, 2U }));

    // -1 is none and -2 is infinite
    EXPECT_EQ(tor.update(make_row(TR_KEY_upload_ratio, tr_variant{ -1.0 })), FieldSet{ Field::UPLOAD_RATIO });
    EXPECT_TRUE(tor.upload_ratio().is_none());
    EXPECT_EQ(tor.update(make_row(TR_KEY_upload_ratio, tr_variant{ -2 })), FieldSet{ Field::UPLOAD_RATIO });
    EXPECT_TRUE(tor.upload_ratio().is_infinite());
}

TEST_F(AppTorrentTest, filesAndFileStatsShareOneBit)
{
    auto tor = Torrent{ prefs_, 1 };

    // file_stats can arrive before files, e.g. from a stats poll
    auto changed = tor.update(make_row(TR_KEY_file_stats, make_list(make_file_stat(5, TR_PRI_NORMAL, true))));
    EXPECT_EQ(changed, FieldSet{ Field::FILES });
    ASSERT_EQ(std::size(tor.files()), 1U);
    EXPECT_EQ(tor.files()[0].have, 5U);
    EXPECT_TRUE(std::empty(tor.files()[0].name));

    changed = tor.update(make_row(TR_KEY_files, make_list(make_file("a.txt"sv, 10))));
    EXPECT_EQ(changed, FieldSet{ Field::FILES });
    ASSERT_EQ(std::size(tor.files()), 1U);
    EXPECT_EQ(tor.files()[0].name, "a.txt");
    EXPECT_EQ(tor.files()[0].size, 10U);
    EXPECT_EQ(tor.files()[0].have, 5U);

    // an unchanged list is no change
    EXPECT_TRUE(tor.update(make_row(TR_KEY_files, make_list(make_file("a.txt"sv, 10)))).none());

    // a changed entry is
    changed = tor.update(make_row(TR_KEY_file_stats, make_list(make_file_stat(10, TR_PRI_NORMAL, true))));
    EXPECT_EQ(changed, FieldSet{ Field::FILES });
    EXPECT_EQ(tor.files()[0].have, 10U);

    // and so is a changed length, which renumbers the entries
    changed = tor.update(make_row(TR_KEY_files, make_list(make_file("a.txt"sv, 10), make_file("b.txt"sv, 20))));
    EXPECT_EQ(changed, FieldSet{ Field::FILES });
    ASSERT_EQ(std::size(tor.files()), 2U);
    EXPECT_EQ(tor.files()[1].index, 1U);
}

TEST_F(AppTorrentTest, trackersAndTrackerStatsShareOneBit)
{
    auto tor = Torrent{ prefs_, 1 };

    auto changed = tor.update(make_row(TR_KEY_trackers, make_list(make_tracker("https://a.example.com/"sv, "a"sv, 0))));
    EXPECT_EQ(changed, FieldSet{ Field::TRACKER_STATS });
    ASSERT_EQ(std::size(tor.tracker_stats()), 1U);
    EXPECT_EQ(tor.tracker_stats()[0].sitename, "a");

    EXPECT_TRUE(tor.update(make_row(TR_KEY_trackers, make_list(make_tracker("https://a.example.com/"sv, "a"sv, 0)))).none());

    auto stats = make_tracker("https://a.example.com/"sv, "a"sv, 0);
    stats.try_emplace(TR_KEY_seeder_count, 7);
    stats.try_emplace(TR_KEY_announce_state, static_cast<int64_t>(TR_TRACKER_ACTIVE));
    changed = tor.update(make_row(TR_KEY_tracker_stats, make_list(std::move(stats))));
    EXPECT_EQ(changed, FieldSet{ Field::TRACKER_STATS });
    EXPECT_EQ(tor.tracker_stats()[0].seeder_count, 7);
    EXPECT_EQ(tor.tracker_stats()[0].announce_state, TR_TRACKER_ACTIVE);
}

TEST_F(AppTorrentTest, sitenamesAreSortedUniqueAndDerivedWhenMissing)
{
    auto tor = Torrent{ prefs_, 1 };

    // the third tracker has no sitename, as sessions older than 4.0 report
    auto const changed = tor.update(make_row(
        TR_KEY_trackers,
        make_list(
            make_tracker("https://zed.example.com/"sv, "zed"sv, 0),
            make_tracker("https://alpha.example.com/"sv, "alpha"sv, 1),
            make_tracker("https://announce.mid.org/"sv, ""sv, 1),
            make_tracker("https://mid.example.com/"sv, "mid"sv, 2))));
    EXPECT_EQ(changed, FieldSet{ Field::TRACKER_STATS });

    ASSERT_EQ(std::size(tor.tracker_stats()), 4U);
    EXPECT_EQ(tor.tracker_stats()[2].sitename, "mid");
    EXPECT_EQ(tor.sitenames(), (std::vector<std::string>{ "alpha", "mid", "zed" }));
    EXPECT_TRUE(tor.includes_tracker("mid"sv));
    EXPECT_FALSE(tor.includes_tracker("example"sv));
}

TEST_F(AppTorrentTest, derivedSitenamesFollowTheirAnnounceUrls)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_row(
                               TR_KEY_trackers,
                               make_list(
                                   make_tracker("https://tracker.alpha.org/"sv, ""sv, 0),
                                   make_tracker("https://tracker.beta.org/"sv, ""sv, 1))))
                    .any());
    EXPECT_EQ(tor.sitenames(), (std::vector<std::string>{ "alpha", "beta" }));

    // removing the first tracker loads the second into its entry
    EXPECT_TRUE(tor.update(make_row(TR_KEY_trackers, make_list(make_tracker("https://tracker.beta.org/"sv, ""sv, 1)))).any());
    ASSERT_EQ(std::size(tor.tracker_stats()), 1U);
    EXPECT_EQ(tor.tracker_stats()[0].sitename, "beta");
    EXPECT_EQ(tor.sitenames(), (std::vector<std::string>{ "beta" }));
}

TEST_F(AppTorrentTest, peersAndWebseedsReportListChanges)
{
    auto tor = Torrent{ prefs_, 1 };

    EXPECT_EQ(tor.update(make_row(TR_KEY_peers, make_list(make_peer("10.0.0.1"sv, 100)))), FieldSet{ Field::PEERS });
    EXPECT_TRUE(tor.update(make_row(TR_KEY_peers, make_list(make_peer("10.0.0.1"sv, 100)))).none());
    EXPECT_EQ(tor.update(make_row(TR_KEY_peers, make_list(make_peer("10.0.0.1"sv, 200)))), FieldSet{ Field::PEERS });
    EXPECT_EQ(tor.peers()[0].rate_to_client, Speed(200, Speed::Units::Byps));
    EXPECT_EQ(tor.update(make_row(TR_KEY_peers, tr_variant::Vector{})), FieldSet{ Field::PEERS });
    EXPECT_TRUE(std::empty(tor.peers()));

    EXPECT_EQ(
        tor.update(make_row(TR_KEY_webseeds_ex, make_list(make_webseed("https://w.example.com/"sv, false, 0)))),
        FieldSet{ Field::WEBSEEDS });
    EXPECT_TRUE(tor.update(make_row(TR_KEY_webseeds_ex, make_list(make_webseed("https://w.example.com/"sv, false, 0)))).none());
    EXPECT_EQ(
        tor.update(make_row(TR_KEY_webseeds_ex, make_list(make_webseed("https://w.example.com/"sv, true, 50)))),
        FieldSet{ Field::WEBSEEDS });
    EXPECT_TRUE(tor.webseeds()[0].is_downloading);
    EXPECT_EQ(tor.webseeds()[0].download_speed, Speed(50, Speed::Units::Byps));
}

TEST_F(AppTorrentTest, sharedStringsArePooled)
{
    auto const before = tr::shared_string::pool_size();

    auto a = Torrent{ prefs_, 1 };
    auto b = Torrent{ prefs_, 2 };
    EXPECT_EQ(a.update(make_row(TR_KEY_download_dir, tr_variant{ "/same/dir"sv })), FieldSet{ Field::DOWNLOAD_DIR });
    EXPECT_EQ(b.update(make_row(TR_KEY_download_dir, tr_variant{ "/same/dir"sv })), FieldSet{ Field::DOWNLOAD_DIR });

    EXPECT_EQ(a.download_dir(), b.download_dir());
    EXPECT_EQ(tr::shared_string::pool_size(), before + 1U);
}

TEST_F(AppTorrentTest, effectiveSeedRatioFollowsModeAndPrefs)
{
    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(make_row(TR_KEY_seed_ratio_limit, tr_variant{ 1.5 })).any());

    // global mode defers to the session prefs
    prefs_.set(TR_KEY_seed_ratio_limited, false);
    EXPECT_EQ(tor.effective_seed_ratio(), std::nullopt);
    prefs_.set(TR_KEY_seed_ratio_limited, true);
    prefs_.set(TR_KEY_seed_ratio_limit, 2.0);
    EXPECT_EQ(tor.effective_seed_ratio(), 2.0);

    // single mode uses the torrent's own limit
    EXPECT_TRUE(tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_SINGLE) })).any());
    EXPECT_EQ(tor.effective_seed_ratio(), 1.5);

    // unlimited mode has none
    EXPECT_TRUE(
        tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_UNLIMITED) })).any());
    EXPECT_EQ(tor.effective_seed_ratio(), std::nullopt);
}

TEST_F(AppTorrentTest, seedRatioPercentDoneMatchesLibtransmission)
{
    auto row = tr_variant::Map{};
    row.try_emplace(TR_KEY_left_until_done, 0);
    row.try_emplace(TR_KEY_size_when_done, 100);
    row.try_emplace(TR_KEY_uploaded_ever, 50);
    row.try_emplace(TR_KEY_is_finished, false);
    row.try_emplace(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_SINGLE));
    row.try_emplace(TR_KEY_seed_ratio_limit, 2.0);

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());

    // done, unfinished, and 50 of the 200-byte goal uploaded
    EXPECT_EQ(tor.seed_ratio_percent_done(), (Percent{ 50U, 200U }));

    // uploading past the goal caps at the goal
    EXPECT_TRUE(tor.update(make_row(TR_KEY_uploaded_ever, tr_variant{ 300 })).any());
    EXPECT_EQ(tor.seed_ratio_percent_done(), (Percent{ 200U, 200U }));
    EXPECT_TRUE(tor.update(make_row(TR_KEY_uploaded_ever, tr_variant{ 50 })).any());

    // not done yet: the ratio doesn't apply
    EXPECT_TRUE(tor.update(make_row(TR_KEY_left_until_done, tr_variant{ 10 })).any());
    EXPECT_TRUE(tor.seed_ratio_percent_done().is_complete());
    EXPECT_TRUE(tor.update(make_row(TR_KEY_left_until_done, tr_variant{ 0 })).any());

    // finished: the ratio's been met
    EXPECT_TRUE(tor.update(make_row(TR_KEY_is_finished, tr_variant{ true })).any());
    EXPECT_TRUE(tor.seed_ratio_percent_done().is_complete());
    EXPECT_TRUE(tor.update(make_row(TR_KEY_is_finished, tr_variant{ false })).any());

    // no ratio applies
    EXPECT_TRUE(
        tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_UNLIMITED) })).any());
    EXPECT_TRUE(tor.seed_ratio_percent_done().is_complete());

    // the session's ratio applies in global mode
    EXPECT_TRUE(tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_GLOBAL) })).any());
    prefs_.set(TR_KEY_seed_ratio_limited, true);
    prefs_.set(TR_KEY_seed_ratio_limit, 1.0);
    EXPECT_EQ(tor.seed_ratio_percent_done(), (Percent{ 50U, 100U }));

    // a zero goal is always met
    prefs_.set(TR_KEY_seed_ratio_limit, 0.0);
    EXPECT_TRUE(tor.seed_ratio_percent_done().is_complete());
}

TEST_F(AppTorrentTest, activityPercentDoneFollowsTheActivity)
{
    auto row = make_metadata_row(1000, 1000);
    row.try_emplace(TR_KEY_left_until_done, 0);
    row.try_emplace(TR_KEY_size_when_done, 100);
    row.try_emplace(TR_KEY_uploaded_ever, 50);
    row.try_emplace(TR_KEY_seed_ratio_mode, static_cast<int64_t>(TR_RATIOLIMIT_SINGLE));
    row.try_emplace(TR_KEY_seed_ratio_limit, 2.0);
    row.try_emplace(TR_KEY_status, static_cast<int64_t>(TR_STATUS_SEED));

    auto tor = Torrent{ prefs_, 1 };
    EXPECT_TRUE(tor.update(row).any());

    // seeding toward a ratio shows the ratio's progress
    EXPECT_EQ(tor.activity_percent_done(), (Percent{ 50U, 200U }));

    // seeding without one shows the download's
    EXPECT_TRUE(
        tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_UNLIMITED) })).any());
    EXPECT_TRUE(tor.activity_percent_done().is_complete());

    // and so does downloading, whatever the ratio
    EXPECT_TRUE(tor.update(make_row(TR_KEY_seed_ratio_mode, tr_variant{ static_cast<int64_t>(TR_RATIOLIMIT_SINGLE) })).any());
    EXPECT_TRUE(tor.update(make_row(TR_KEY_status, tr_variant{ static_cast<int64_t>(TR_STATUS_DOWNLOAD) })).any());
    EXPECT_TRUE(tor.update(make_row(TR_KEY_left_until_done, tr_variant{ 25 })).any());
    EXPECT_EQ(tor.activity_percent_done(), (Percent{ 75U, 100U }));
}

TEST_F(AppTorrentTest, fieldMasksCombine)
{
    constexpr auto Mask = Field::NAME | Field::STATUS;
    static_assert(Mask.test(Field::NAME));
    static_assert(Mask.test(Field::STATUS));
    static_assert(!Mask.test(Field::ETA));
    static_assert(Mask.count() == 2U);

    EXPECT_TRUE((Mask & FieldSet{ Field::STATUS }).any());
    EXPECT_TRUE((Mask & FieldSet{ Field::ETA }).none());
    EXPECT_EQ((Mask | Field::ETA).count(), 3U);

    static_assert(Torrent::ActivePeerCountFields.test(Field::WEBSEEDS_SENDING_TO_US));
    static_assert(Torrent::ActivityPercentDoneFields.test(Field::SEED_RATIO_MODE));
    static_assert(Torrent::ActivityPercentDoneFields.test(Field::METADATA_SIZE));
    static_assert(!Torrent::SeedRatioPercentDoneFields.test(Field::METADATA_SIZE));
    static_assert(Torrent::PercentCompleteFields.test(Field::HAVE_UNCHECKED));
}
