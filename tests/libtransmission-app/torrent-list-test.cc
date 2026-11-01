// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <cstdint>
#include <ctime> // time_t
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <sigslot/signal.hpp>

#include <libtransmission/crypto-utils.h> // tr_sha1_from_string()
#include <libtransmission/digest.h>
#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent-list.h"
#include "libtransmission-app/torrent.h"

#include "test-fixtures.h"
#include "torrent-get-helpers.h"

using namespace std::literals;

using tr::app::Torrent;
using tr::app::TorrentList;
using Changes = TorrentList::Changes;
using Coverage = TorrentList::Coverage;
using Ids = TorrentList::Ids;
using TorrentChange = TorrentList::TorrentChange;

namespace
{

// `added_date` is judged against this
auto constexpr Now = time_t{ 1'700'000'000 };

// the `arguments` of a `torrent_get` response; `removed` is omitted unless given
[[nodiscard]] tr_variant::Map make_args(tr_variant::Vector torrents, std::optional<Ids> const& removed = {})
{
    auto args = tr_variant::Map{ 2U };
    args.try_emplace(TR_KEY_torrents, std::move(torrents));

    if (removed) {
        auto ids = tr_variant::Vector{};
        for (auto const id : *removed) {
            ids.emplace_back(id);
        }
        args.try_emplace(TR_KEY_removed, std::move(ids));
    }

    return args;
}

// Rewrites object-format rows as one table: a key row, then each row's values.
// Every row must carry the first row's keys.
[[nodiscard]] tr_variant::Vector to_table(tr_variant::Vector const& rows)
{
    auto table = tr_variant::Vector{};
    if (std::empty(rows)) {
        return table;
    }

    auto const& first = *rows.front().get_if<tr_variant::Map>();
    auto header = tr_variant::Vector{};
    for (auto const& [key, value] : first) {
        header.emplace_back(tr_quark_get_string_view(key));
    }
    table.emplace_back(std::move(header));

    for (auto const& row : rows) {
        auto const& map = *row.get_if<tr_variant::Map>();
        auto values = tr_variant::Vector{};
        for (auto const& [key, value] : first) {
            auto const iter = map.find(key);
            EXPECT_NE(iter, std::end(map)) << tr_quark_get_string_view(key);
            values.emplace_back(iter != std::end(map) ? iter->second.clone() : tr_variant{});
        }
        table.emplace_back(std::move(values));
    }

    return table;
}

// Logs every signal as one line, with the list's size as the slot sees it and,
// where the rows exist, their ids, so a test asserts one whole sequence.
class Recorder
{
public:
    explicit Recorder(TorrentList& list)
        : list_{ list }
        , connections_{ list.rows_inserting.connect_scoped(
                            [this](size_t const row, size_t const count) { log("inserting", row, count, false); }),
                        list.rows_inserted.connect_scoped(
                            [this](size_t const row, size_t const count) { log("inserted", row, count, true); }),
                        list.rows_removing.connect_scoped(
                            [this](size_t const row, size_t const count) { log("removing", row, count, true); }),
                        list.rows_removed.connect_scoped(
                            [this](size_t const row, size_t const count) { log("removed", row, count, false); }),
                        list.changed.connect_scoped([this](Changes const& what) {
                            lines.emplace_back("changed");
                            changes.push_back(what);
                        }) }
    {
    }

    [[nodiscard]] Changes const& last_changes() const
    {
        if (std::empty(changes)) {
            ADD_FAILURE() << "`changed` didn't fire";
            static auto const None = Changes{};
            return None;
        }

        return changes.back();
    }

    void reset()
    {
        lines.clear();
        changes.clear();
    }

    std::vector<std::string> lines;
    std::vector<Changes> changes;

private:
    void log(std::string_view const name, size_t const row, size_t const count, bool const with_ids)
    {
        auto line = fmt::format("{:s} {:d} {:d} size {:d}", name, row, count, std::size(list_));
        if (with_ids) {
            line += " ids";
            for (auto i = row; i < row + count; ++i) {
                line += fmt::format(" {:d}", list_[i].id());
            }
        }
        lines.push_back(std::move(line));
    }

    TorrentList const& list_;
    std::array<sigslot::scoped_connection, 5> connections_;
};

[[nodiscard]] Ids ids_of(TorrentList const& list)
{
    auto ids = Ids{};
    for (auto const& tor : list.torrents()) {
        ids.push_back(tor.id());
    }
    return ids;
}

[[nodiscard]] Ids changed_ids(Changes const& changes)
{
    auto ids = Ids{};
    for (auto const& change : changes.changed) {
        ids.push_back(change.id);
    }
    return ids;
}

class AppTorrentListTest : public TransmissionTest
{
protected:
    // a complete response naming `ids`, each with a name and an old `added_date`
    void seed(std::initializer_list<tr_torrent_id_t> const ids)
    {
        auto rows = tr_variant::Vector{};
        for (auto const id : ids) {
            rows.emplace_back(
                make_row(id, kv(TR_KEY_name, fmt::format("torrent {:d}", id)), kv(TR_KEY_added_date, Now - 3600)));
        }
        list_.update(make_args(std::move(rows)), Coverage::Complete, Now);
        recorder_.reset();
    }

    tr::app::Prefs prefs_;
    TorrentList list_{ prefs_ };
    Recorder recorder_{ list_ };
};

} // namespace

// --- rows

TEST_F(AppTorrentListTest, insertsNewTorrentsInIdOrder)
{
    list_.update(make_args(make_list(make_row(3), make_row(1), make_row(2))), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2, 3 }));
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "inserting 0 3 size 0", "inserted 0 3 size 3 ids 1 2 3", "changed" }));
    EXPECT_EQ(recorder_.last_changes().inserted, (std::vector<size_t>{ 0U, 1U, 2U }));
}

TEST_F(AppTorrentListTest, newTorrentsHoldTheirFieldsWhenInserted)
{
    // a proxy model filters and sorts new rows as soon as they appear
    auto names = std::vector<std::string>{};
    auto const tag = list_.rows_inserted.connect_scoped([this, &names](size_t const row, size_t const count) {
        for (auto i = row; i < row + count; ++i) {
            names.emplace_back(list_[i].name());
        }
    });

    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv)))), Coverage::Complete, Now);
    EXPECT_EQ(names, (std::vector<std::string>{ "alpha" }));
}

TEST_F(AppTorrentListTest, insertsOneRunPerGapInAscendingOrder)
{
    seed({ 1, 10, 20 });

    list_.update(make_args(make_list(make_row(12), make_row(6), make_row(30), make_row(5))), Coverage::Partial, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 5, 6, 10, 12, 20, 30 }));
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "inserting 1 2 size 3",
                                   "inserted 1 2 size 5 ids 5 6",
                                   "inserting 4 1 size 5",
                                   "inserted 4 1 size 6 ids 12",
                                   "inserting 6 1 size 6",
                                   "inserted 6 1 size 7 ids 30",
                                   "changed" }));
}

TEST_F(AppTorrentListTest, removesOneRunPerGapInDescendingOrder)
{
    seed({ 1, 2, 3, 4, 5 });

    list_.update(make_args(make_list(make_row(3), make_row(4))), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 3, 4 }));
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "removing 4 1 size 5 ids 5",
                                   "removed 4 1 size 4",
                                   "removing 0 2 size 4 ids 1 2",
                                   "removed 0 2 size 2",
                                   "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 1, 2, 5 }));
}

TEST_F(AppTorrentListTest, insertsBelowTheRowsRemovedInTheSameUpdate)
{
    seed({ 5, 6, 7 });

    // 1 takes row 0, so 5 and 7 are a row lower when they're removed
    list_.update(
        make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv)), make_row(6, kv(TR_KEY_rate_download, 100)))),
        Coverage::Complete,
        Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 6 }));
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "inserting 0 1 size 3",
                                   "inserted 0 1 size 4 ids 1",
                                   "removing 3 1 size 4 ids 7",
                                   "removed 3 1 size 3",
                                   "removing 1 1 size 3 ids 5",
                                   "removed 1 1 size 2",
                                   "changed" }));

    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(changes.changed, (std::vector<TorrentChange>{ { 6, 1U, Torrent::fields<TR_KEY_rate_download>() } }));
    EXPECT_EQ(changes.removed, (Ids{ 5, 7 }));
}

TEST_F(AppTorrentListTest, torrentAddressesAreStable)
{
    seed({ 10 });
    auto const* const tor = list_.find(10);
    ASSERT_NE(tor, nullptr);

    list_.update(make_args(make_list(make_row(1), make_row(5), make_row(15))), Coverage::Partial, Now);
    EXPECT_EQ(list_.find(10), tor);
    EXPECT_EQ(list_.row_of(10), 2U);

    list_.update(make_args(make_list(make_row(10), make_row(15))), Coverage::Complete, Now);
    EXPECT_EQ(list_.find(10), tor);
    EXPECT_EQ(list_.row_of(10), 0U);
}

TEST_F(AppTorrentListTest, lookupsAgree)
{
    EXPECT_TRUE(list_.empty());
    EXPECT_EQ(list_.find(1), nullptr);
    EXPECT_EQ(list_.row_of(1), std::nullopt);

    seed({ 2, 4, 6 });
    EXPECT_FALSE(list_.empty());
    EXPECT_EQ(list_.size(), 3U);

    for (auto const& [id, row] : { std::pair{ 2, 0U }, std::pair{ 4, 1U }, std::pair{ 6, 2U } }) {
        EXPECT_EQ(list_.row_of(id), row);
        EXPECT_EQ(list_.find(id), &list_[row]);
        EXPECT_EQ(list_[row].id(), id);
    }

    EXPECT_EQ(list_.find(3), nullptr);
    EXPECT_EQ(list_.row_of(3), std::nullopt);
    EXPECT_EQ(ids_of(list_), (Ids{ 2, 4, 6 }));
}

TEST_F(AppTorrentListTest, findsATorrentByItsHash)
{
    auto constexpr HashString = "f0e1d2c3b4a5968778695a4b3c2d1e0f10203040"sv;
    auto const hash = tr_sha1_from_string(HashString);
    ASSERT_TRUE(hash);

    EXPECT_EQ(list_.find(*hash), nullptr);

    list_.update(make_args(make_list(make_row(1), make_row(2, kv(TR_KEY_hash_string, HashString)))), Coverage::Complete, Now);
    EXPECT_EQ(list_.find(*hash), list_.find(2));
    EXPECT_EQ(list_.find(tr_sha1_digest_t{}), list_.find(1));
}

// --- formats

TEST_F(AppTorrentListTest, tableFormatMatchesObjectFormat)
{
    auto const make_rows = [](int const rate) {
        return make_list(
            make_row(
                2,
                kv(TR_KEY_name, "beta"sv),
                kv(TR_KEY_rate_download, rate * 2),
                kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_SEED))),
            make_row(
                1,
                kv(TR_KEY_name, "alpha"sv),
                kv(TR_KEY_rate_download, rate),
                kv(TR_KEY_status, static_cast<int64_t>(TR_STATUS_DOWNLOAD))));
    };

    // the same rows in each format, first as newcomers and then with new speeds
    auto table_list = TorrentList{ prefs_ };
    auto table_recorder = Recorder{ table_list };
    for (auto const rate : { 100, 200 }) {
        table_list.update(make_args(to_table(make_rows(rate))), Coverage::Complete, Now);
        list_.update(make_args(make_rows(rate)), Coverage::Complete, Now);
    }

    EXPECT_EQ(recorder_.lines, table_recorder.lines);
    EXPECT_EQ(recorder_.last_changes().changed, table_recorder.last_changes().changed);
    EXPECT_EQ(recorder_.last_changes().changed_fields, table_recorder.last_changes().changed_fields);

    ASSERT_EQ(table_list.size(), 2U);
    EXPECT_EQ(table_list[0].name(), "alpha");
    EXPECT_EQ(table_list[0].download_speed(), list_[0].download_speed());
    EXPECT_EQ(table_list[1].activity(), TR_STATUS_SEED);
}

TEST_F(AppTorrentListTest, tableFormatKeepsColumnsAlignedPastUnknownKeys)
{
    seed({ 1 });

    auto table = make_list(
        make_list(tr_variant{ "not_a_key"sv }, tr_variant{ 7 }, tr_variant{ "id"sv }, tr_variant{ "name"sv }),
        make_list(tr_variant{ "ignored"sv }, tr_variant{ "ignored"sv }, tr_variant{ 1 }, tr_variant{ "alpha"sv }));

    list_.update(make_args(std::move(table)), Coverage::Complete, Now);

    ASSERT_EQ(list_.size(), 1U);
    EXPECT_EQ(list_[0].id(), 1);
    EXPECT_EQ(list_[0].name(), "alpha");
    EXPECT_EQ(recorder_.last_changes().changed_fields, Torrent::fields<TR_KEY_name>());

    // the unknown key wasn't interned
    EXPECT_FALSE(tr_quark_lookup("not_a_key"sv));
}

TEST_F(AppTorrentListTest, tableWithoutIdColumnSkipsEveryRow)
{
    seed({ 1 });

    auto table = make_list(make_list(tr_variant{ "name"sv }), make_list(tr_variant{ "alpha"sv }));
    list_.update(make_args(std::move(table)), Coverage::Complete, Now);

    // the skipped row also blocks removal by absence
    EXPECT_EQ(ids_of(list_), (Ids{ 1 }));
    EXPECT_TRUE(std::empty(recorder_.lines));
}

TEST_F(AppTorrentListTest, skipsRowsWithoutUsableId)
{
    auto no_id = tr_variant::Map{};
    no_id.try_emplace(TR_KEY_name, "no id"sv);
    auto bad_id = tr_variant::Map{};
    bad_id.try_emplace(TR_KEY_id, "one"sv);

    list_.update(
        make_args(make_list(tr_variant{ "not a row"sv }, std::move(no_id), std::move(bad_id), make_row(1), make_row(2))),
        Coverage::Complete,
        Now);
    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2 }));

    // table rows that are too short for the id column, or aren't lists
    auto table = make_list(
        make_list(tr_variant{ "name"sv }, tr_variant{ "id"sv }),
        make_list(tr_variant{ "short"sv }),
        tr_variant{ "not a row"sv },
        make_list(tr_variant{ "gamma"sv }, tr_variant{ 3 }));
    list_.update(make_args(std::move(table)), Coverage::Complete, Now);
    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2, 3 }));
}

// --- removal

TEST_F(AppTorrentListTest, completeListRemovesAbsentTorrents)
{
    seed({ 1, 2, 3 });

    list_.update(make_args(make_list(make_row(1), make_row(3))), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 3 }));
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 1 1 size 3 ids 2", "removed 1 1 size 2", "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 2 }));
}

TEST_F(AppTorrentListTest, emptyCompleteListRemovesEverything)
{
    seed({ 1, 2, 3 });

    list_.update(make_args(tr_variant::Vector{}), Coverage::Complete, Now);
    EXPECT_TRUE(list_.empty());
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 0 3 size 3 ids 1 2 3", "removed 0 3 size 0", "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 1, 2, 3 }));

    // a table with only its key row is empty too
    seed({ 4 });
    list_.update(make_args(make_list(make_list(tr_variant{ "id"sv }))), Coverage::Complete, Now);
    EXPECT_TRUE(list_.empty());
}

TEST_F(AppTorrentListTest, partialListKeepsAbsentTorrents)
{
    seed({ 1, 2, 3 });

    list_.update(make_args(make_list(make_row(2, kv(TR_KEY_rate_download, 100)))), Coverage::Partial, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2, 3 }));
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "changed" }));
    EXPECT_TRUE(std::empty(recorder_.last_changes().removed));
}

TEST_F(AppTorrentListTest, skippedRowBlocksRemovalByAbsenceButNotTheRemovedList)
{
    seed({ 1, 2, 3 });

    auto no_id = tr_variant::Map{};
    no_id.try_emplace(TR_KEY_name, "no id"sv);
    list_.update(make_args(make_list(make_row(1), std::move(no_id)), Ids{ 3 }), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2 }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 3 }));
}

TEST_F(AppTorrentListTest, removedListRemovesKnownIdsAndIgnoresUnknownOnes)
{
    seed({ 1, 2, 3 });

    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_rate_upload, 10))), Ids{ 2, 99 }), Coverage::Partial, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 3 }));
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 1 1 size 3 ids 2", "removed 1 1 size 2", "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 2 }));
    EXPECT_EQ(changed_ids(recorder_.last_changes()), (Ids{ 1 }));
}

TEST_F(AppTorrentListTest, removedListIsHonoredWithoutATorrentsKey)
{
    seed({ 1, 2 });

    auto args = tr_variant::Map{ 1U };
    args.try_emplace(TR_KEY_removed, make_list(tr_variant{ 2 }));
    list_.update(args, Coverage::Complete, Now);

    // and without `torrents`, nothing is removed by absence
    EXPECT_EQ(ids_of(list_), (Ids{ 1 }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 2 }));
}

TEST_F(AppTorrentListTest, responseWithoutTorrentsOrRemovedFiresNothing)
{
    seed({ 1, 2 });

    list_.update(tr_variant::Map{}, Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2 }));
    EXPECT_TRUE(std::empty(recorder_.lines));
}

TEST_F(AppTorrentListTest, removedAndAbsentCoalesceIntoOneRun)
{
    seed({ 1, 2, 3, 4, 5 });

    list_.update(make_args(make_list(make_row(1), make_row(2), make_row(5)), Ids{ 4 }), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2, 5 }));
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 2 2 size 5 ids 3 4", "removed 2 2 size 3", "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 3, 4 }));
}

TEST_F(AppTorrentListTest, anIdBothRemovedAndAbsentGoesOnce)
{
    seed({ 1, 2, 3 });

    list_.update(make_args(make_list(make_row(1)), Ids{ 2 }), Coverage::Complete, Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1 }));
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 1 2 size 3 ids 2 3", "removed 1 2 size 1", "changed" }));
    EXPECT_EQ(recorder_.last_changes().removed, (Ids{ 2, 3 }));
}

// --- changes

TEST_F(AppTorrentListTest, changedCarriesEachTorrentsOwnFieldsAndTheUnion)
{
    seed({ 1, 2, 3 });

    list_.update(
        make_args(
            make_list(make_row(3, kv(TR_KEY_rate_download, 300)), make_row(1, kv(TR_KEY_name, "renamed"sv)), make_row(2))),
        Coverage::Complete,
        Now);

    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(
        changes.changed,
        (std::vector<TorrentChange>{ { 1, 0U, Torrent::fields<TR_KEY_name>() },
                                     { 3, 2U, Torrent::fields<TR_KEY_rate_download>() } }));
    EXPECT_EQ(changes.changed_fields, (Torrent::fields<TR_KEY_name, TR_KEY_rate_download>()));
}

TEST_F(AppTorrentListTest, changedRowsAreFinalRows)
{
    seed({ 1, 2, 3 });

    // 2 goes, so 3 moves up a row before `changed` fires
    list_.update(make_args(make_list(make_row(1), make_row(3, kv(TR_KEY_rate_download, 300)))), Coverage::Complete, Now);

    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(changes.changed, (std::vector<TorrentChange>{ { 3, 1U, Torrent::fields<TR_KEY_rate_download>() } }));
    EXPECT_EQ(&list_[1], list_.find(3));
}

TEST_F(AppTorrentListTest, changedExcludesTorrentsRemovedInTheSameUpdate)
{
    seed({ 1, 2 });

    // a torrent in both lists is gone, so its change isn't reported
    list_.update(make_args(make_list(make_row(2, kv(TR_KEY_rate_download, 200))), Ids{ 2 }), Coverage::Partial, Now);

    auto const& changes = recorder_.last_changes();
    EXPECT_TRUE(std::empty(changes.changed));
    EXPECT_EQ(changes.removed, (Ids{ 2 }));
}

TEST_F(AppTorrentListTest, newTorrentsAreInsertedNotChanged)
{
    // a named torrent with an old date is news only as a row
    list_.update(
        make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv), kv(TR_KEY_added_date, Now - 3600)))),
        Coverage::Complete,
        Now);

    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "inserting 0 1 size 0", "inserted 0 1 size 1 ids 1", "changed" }));
    EXPECT_EQ(recorder_.last_changes().inserted, (std::vector<size_t>{ 0U }));
    EXPECT_TRUE(std::empty(recorder_.last_changes().changed));
}

TEST_F(AppTorrentListTest, identicalUpdateFiresNothing)
{
    auto const make = [] {
        return make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv)), make_row(2, kv(TR_KEY_name, "beta"sv))));
    };

    list_.update(make(), Coverage::Complete, Now);
    recorder_.reset();

    list_.update(make(), Coverage::Complete, Now);
    EXPECT_TRUE(std::empty(recorder_.lines));
}

TEST_F(AppTorrentListTest, duplicateRowsForOneIdApplyInOrderAndReportOnce)
{
    seed({ 1 });

    // a known id and a new one, each twice
    list_.update(
        make_args(make_list(
            make_row(1, kv(TR_KEY_rate_download, 100)),
            make_row(2, kv(TR_KEY_name, "first"sv)),
            make_row(1, kv(TR_KEY_rate_upload, 10)),
            make_row(2, kv(TR_KEY_name, "second"sv)))),
        Coverage::Complete,
        Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 2 }));
    EXPECT_EQ(list_[0].download_speed().base_quantity(), 100U);
    EXPECT_EQ(list_[0].upload_speed().base_quantity(), 10U);
    EXPECT_EQ(list_[1].name(), "second");

    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(
        changes.changed,
        (std::vector<TorrentChange>{ { 1, 0U, Torrent::fields<TR_KEY_rate_download, TR_KEY_rate_upload>() } }));
    EXPECT_EQ(changes.need_info, (Ids{}));
}

TEST_F(AppTorrentListTest, signalOrderIsInsertsRemovesChanged)
{
    seed({ 1, 2, 3 });

    // one response that inserts 4, changes 1, drops 2, and removes 3
    list_.update(
        make_args(make_list(make_row(4, kv(TR_KEY_name, "delta"sv)), make_row(1, kv(TR_KEY_rate_download, 100))), Ids{ 3 }),
        Coverage::Complete,
        Now);

    EXPECT_EQ(ids_of(list_), (Ids{ 1, 4 }));
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "inserting 3 1 size 3",
                                   "inserted 3 1 size 4 ids 4",
                                   "removing 1 2 size 4 ids 2 3",
                                   "removed 1 2 size 2",
                                   "changed" }));

    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(changes.inserted, (std::vector<size_t>{ 1U }));
    EXPECT_EQ(changes.changed, (std::vector<TorrentChange>{ { 1, 0U, Torrent::fields<TR_KEY_rate_download>() } }));
    EXPECT_EQ(changes.removed, (Ids{ 2, 3 }));
}

// --- events

TEST_F(AppTorrentListTest, addedNeedsANameAndARecentDate)
{
    list_.update(
        make_args(make_list(
            make_row(1, kv(TR_KEY_name, "recent"sv), kv(TR_KEY_added_date, Now - 10)),
            make_row(2, kv(TR_KEY_name, "just inside"sv), kv(TR_KEY_added_date, Now - (TorrentList::AddedWindow - 1))),
            make_row(3, kv(TR_KEY_name, "just outside"sv), kv(TR_KEY_added_date, Now - TorrentList::AddedWindow)),
            make_row(4, kv(TR_KEY_added_date, Now - 10)),
            make_row(5, kv(TR_KEY_name, "undated"sv)),
            make_row(6, kv(TR_KEY_name, "old"sv), kv(TR_KEY_added_date, Now - 3600)))),
        Coverage::Complete,
        Now);

    EXPECT_EQ(recorder_.last_changes().added, (Ids{ 1, 2 }));
}

TEST_F(AppTorrentListTest, addedIsReportedOnce)
{
    auto const make = [] {
        return make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv), kv(TR_KEY_added_date, Now - 10))));
    };

    list_.update(make(), Coverage::Partial, Now);
    EXPECT_EQ(recorder_.last_changes().added, (Ids{ 1 }));
    recorder_.reset();

    // the same torrent on the next poll, still inside the window
    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_rate_download, 100)))), Coverage::Partial, Now + 5);
    EXPECT_TRUE(std::empty(recorder_.last_changes().added));
}

TEST_F(AppTorrentListTest, namelessTorrentIsAddedOnceNamed)
{
    // a stats-only row for a new torrent: no name yet
    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_added_date, Now - 10)))), Coverage::Partial, Now);
    EXPECT_TRUE(std::empty(recorder_.last_changes().added));
    EXPECT_EQ(recorder_.last_changes().need_info, (Ids{ 1 }));
    recorder_.reset();

    // its info arrives
    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv)))), Coverage::Partial, Now + 1);
    EXPECT_EQ(recorder_.last_changes().added, (Ids{ 1 }));
    EXPECT_TRUE(std::empty(recorder_.last_changes().need_info));
}

TEST_F(AppTorrentListTest, startupListIsQuiet)
{
    auto rows = tr_variant::Vector{};
    for (auto id = 1; id <= 10; ++id) {
        rows.emplace_back(make_row(
            id,
            kv(TR_KEY_name, fmt::format("torrent {:d}", id)),
            kv(TR_KEY_added_date, Now - 86400),
            kv(TR_KEY_edit_date, Now - 3600),
            kv(TR_KEY_left_until_done, 0),
            kv(TR_KEY_downloaded_ever, 1000)));
    }
    list_.update(make_args(std::move(rows)), Coverage::Complete, Now);

    EXPECT_EQ(list_.size(), 10U);
    EXPECT_EQ(
        recorder_.lines,
        (std::vector<std::string>{ "inserting 0 10 size 0", "inserted 0 10 size 10 ids 1 2 3 4 5 6 7 8 9 10", "changed" }));
    auto const& changes = recorder_.last_changes();
    EXPECT_EQ(std::size(changes.inserted), 10U);
    EXPECT_TRUE(std::empty(changes.changed));
    EXPECT_TRUE(std::empty(changes.added));
    EXPECT_TRUE(std::empty(changes.completed));
    EXPECT_TRUE(std::empty(changes.edited));
    EXPECT_TRUE(std::empty(changes.need_info));
    EXPECT_TRUE(std::empty(changes.removed));
}

TEST_F(AppTorrentListTest, needInfoOnlyForNewNamelessTorrents)
{
    seed({ 1 });

    // a known torrent's stats row carries no name, and that's not news
    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_rate_download, 100)), make_row(2))), Coverage::Partial, Now);
    EXPECT_EQ(recorder_.last_changes().need_info, (Ids{ 2 }));
    recorder_.reset();

    // nor is a second nameless row for the new one
    list_.update(make_args(make_list(make_row(2, kv(TR_KEY_rate_download, 100)))), Coverage::Partial, Now);
    EXPECT_TRUE(std::empty(recorder_.last_changes().need_info));
}

TEST_F(AppTorrentListTest, editedFollowsEditDateChangesOfKnownTorrents)
{
    // a first row sets `edit_date` from its default, which is no edit
    list_.update(
        make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv), kv(TR_KEY_edit_date, Now - 100)))),
        Coverage::Complete,
        Now);
    EXPECT_TRUE(std::empty(recorder_.last_changes().edited));
    recorder_.reset();

    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_edit_date, Now - 50)))), Coverage::Complete, Now);
    EXPECT_EQ(recorder_.last_changes().edited, (Ids{ 1 }));
    recorder_.reset();

    list_.update(make_args(make_list(make_row(1, kv(TR_KEY_edit_date, Now - 50)))), Coverage::Complete, Now);
    EXPECT_TRUE(std::empty(recorder_.lines));
}

TEST_F(AppTorrentListTest, completedNeedsLeftUntilDoneToReachZeroAfterDownloading)
{
    // 1 downloads to completion; 2 finishes a verify with nothing downloaded; 3 is seen done; 4 is seen downloading
    list_.update(
        make_args(make_list(
            make_row(1, kv(TR_KEY_left_until_done, 100), kv(TR_KEY_downloaded_ever, 0)),
            make_row(2, kv(TR_KEY_left_until_done, 100), kv(TR_KEY_downloaded_ever, 0)),
            make_row(3, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 1000)),
            make_row(4, kv(TR_KEY_left_until_done, 100), kv(TR_KEY_downloaded_ever, 50)))),
        Coverage::Complete,
        Now);
    EXPECT_TRUE(std::empty(recorder_.last_changes().completed));
    recorder_.reset();

    list_.update(
        make_args(make_list(
            make_row(1, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 100)),
            make_row(2, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 0)),
            make_row(3, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 1000)),
            make_row(4, kv(TR_KEY_left_until_done, 50), kv(TR_KEY_downloaded_ever, 100)))),
        Coverage::Complete,
        Now);
    EXPECT_EQ(recorder_.last_changes().completed, (Ids{ 1 }));
    recorder_.reset();

    // deselecting the rest of 4's files is still a completion; staying done is not
    list_.update(
        make_args(make_list(
            make_row(1, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 100)),
            make_row(4, kv(TR_KEY_left_until_done, 0), kv(TR_KEY_downloaded_ever, 100)))),
        Coverage::Partial,
        Now);
    EXPECT_EQ(recorder_.last_changes().completed, (Ids{ 4 }));
}

// --- clear

TEST_F(AppTorrentListTest, clearRemovesEveryRowInOneRun)
{
    seed({ 1, 2, 3 });

    list_.clear();

    EXPECT_TRUE(list_.empty());
    EXPECT_EQ(recorder_.lines, (std::vector<std::string>{ "removing 0 3 size 3 ids 1 2 3", "removed 0 3 size 0" }));
}

TEST_F(AppTorrentListTest, clearOnAnEmptyListFiresNothing)
{
    list_.clear();
    EXPECT_TRUE(std::empty(recorder_.lines));
}

TEST_F(AppTorrentListTest, clearForgetsAddedReports)
{
    auto const make = [] {
        return make_args(make_list(make_row(1, kv(TR_KEY_name, "alpha"sv), kv(TR_KEY_added_date, Now - 10))));
    };

    list_.update(make(), Coverage::Complete, Now);
    EXPECT_EQ(recorder_.last_changes().added, (Ids{ 1 }));

    // another session may reuse the id
    list_.clear();
    recorder_.reset();
    list_.update(make(), Coverage::Complete, Now);
    EXPECT_EQ(recorder_.last_changes().added, (Ids{ 1 }));
}
