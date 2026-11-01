// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <ctime> // time_t
#include <string_view>
#include <utility>
#include <vector>

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QItemSelectionModel>
#include <QString>
#include <QStringList>
#include <QTest>

#include <libtransmission/quark.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>

#include "Prefs.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "TrQtInit.h"
#include "Typedefs.h"
#include "qt-test-fixtures.h"

using namespace std::literals;

using Coverage = tr::app::TorrentList::Coverage;

namespace
{

auto constexpr Now = time_t{ 1'700'000'000 };

// an object-format `torrent_get` row, dated long enough ago that it isn't "added"
[[nodiscard]] tr_variant::Map make_row(tr_torrent_id_t const id, std::string_view const name, int64_t const rate_download = 0)
{
    auto row = tr_variant::Map{ 4U };
    row.try_emplace(TR_KEY_id, id);
    row.try_emplace(TR_KEY_name, name);
    row.try_emplace(TR_KEY_rate_download, rate_download);
    row.try_emplace(TR_KEY_added_date, Now - 86400);
    return row;
}

template<typename... Rows>
[[nodiscard]] tr_variant::Map make_args(Rows&&... rows)
{
    auto torrents = tr_variant::Vector{};
    (torrents.emplace_back(std::forward<Rows>(rows)), ...);

    auto args = tr_variant::Map{ 2U };
    args.try_emplace(TR_KEY_torrents, std::move(torrents));
    return args;
}

[[nodiscard]] QStringList names_of(QAbstractItemModel const& model)
{
    auto names = QStringList{};
    for (int row = 0, n = model.rowCount(); row < n; ++row) {
        names << model.index(row, 0).data(Qt::DisplayRole).toString();
    }
    return names;
}

[[nodiscard]] tr_torrent_id_t id_of(QModelIndex const& index)
{
    return index.data(TorrentModel::TorrentRole).value<tr::app::Torrent const*>()->id();
}

using Spans = std::vector<std::pair<int, int>>;

// The list, the model over it, and the sort/filter proxy over that,
// with Qt's own model tester checking both models' signals.
struct Client {
    Prefs prefs;
    tr::app::TorrentList list{ prefs };
    TorrentModel model{ list };
    TorrentFilter filter{ prefs };
    QAbstractItemModelTester model_tester{ &model, QAbstractItemModelTester::FailureReportingMode::QtTest };
    QAbstractItemModelTester filter_tester{ &filter, QAbstractItemModelTester::FailureReportingMode::QtTest };

    Client()
    {
        filter.setSourceModel(&model);
    }
};

class TorrentModelTest : public QObject
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    static void showsTheListInIdOrder()
    {
        auto client = Client{};
        QCOMPARE(client.model.rowCount(), 0);

        client.list.update(
            make_args(make_row(5, "echo"sv), make_row(1, "alpha"sv), make_row(3, "charlie"sv)),
            Coverage::Complete,
            Now);

        QCOMPARE(client.model.rowCount(), 3);
        QCOMPARE(client.model.rowCount(client.model.index(0, 0)), 0);
        QCOMPARE(
            names_of(client.model),
            (QStringList{ QStringLiteral("alpha"), QStringLiteral("charlie"), QStringLiteral("echo") }));
        QCOMPARE(id_of(client.model.index(1, 0)), 3);
        QCOMPARE(
            client.model.index(1, 0).data(TorrentModel::TorrentRole).value<tr::app::Torrent const*>(),
            client.list.find(3));
    }

    static void insertsIntoTheGapsAndReportsChangesBySpan()
    {
        auto client = Client{};
        client.list.update(
            make_args(make_row(1, "alpha"sv), make_row(3, "charlie"sv), make_row(5, "echo"sv)),
            Coverage::Complete,
            Now);

        auto spans = Spans{};
        QObject::connect(
            &client.model,
            &QAbstractItemModel::dataChanged,
            [&spans](QModelIndex const& top_left, QModelIndex const& bottom_right) {
                spans.emplace_back(top_left.row(), bottom_right.row());
            });

        // two new rows and one changed one
        client.list.update(
            make_args(make_row(2, "bravo"sv), make_row(6, "foxtrot"sv), make_row(1, "alpha"sv, 100)),
            Coverage::Partial,
            Now);
        QCOMPARE(
            names_of(client.model),
            (QStringList{ QStringLiteral("alpha"),
                          QStringLiteral("bravo"),
                          QStringLiteral("charlie"),
                          QStringLiteral("echo"),
                          QStringLiteral("foxtrot") }));
        QVERIFY(spans == (Spans{ { 0, 0 } }));

        // adjacent changed rows share one dataChanged()
        spans.clear();
        client.list.update(
            make_args(make_row(1, "alpha"sv, 200), make_row(2, "bravo"sv, 200), make_row(6, "foxtrot"sv, 200)),
            Coverage::Partial,
            Now);
        QVERIFY(spans == (Spans{ { 0, 1 }, { 4, 4 } }));
    }

    static void showsARenamedTorrentUnderItsNewName()
    {
        auto client = Client{};
        client.list.update(make_args(make_row(1, "alpha"sv)), Coverage::Complete, Now);
        QCOMPARE(client.model.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("alpha"));

        client.list.update(make_args(make_row(1, "zulu"sv)), Coverage::Partial, Now);
        QCOMPARE(client.model.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("zulu"));
        QCOMPARE(client.filter.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("zulu"));
    }

    static void keepsTheProxySortedThroughARenameAndAnInsert()
    {
        auto client = Client{};
        client.list.update(
            make_args(make_row(1, "alpha"sv), make_row(2, "bravo"sv), make_row(3, "charlie"sv)),
            Coverage::Complete,
            Now);
        QCOMPARE(
            names_of(client.filter),
            (QStringList{ QStringLiteral("alpha"), QStringLiteral("bravo"), QStringLiteral("charlie") }));

        // the proxy places the new row by the name the same update just gave the old one
        client.list.update(make_args(make_row(1, "zulu"sv), make_row(4, "delta"sv)), Coverage::Partial, Now);
        QCOMPARE(
            names_of(client.filter),
            (QStringList{ QStringLiteral("bravo"),
                          QStringLiteral("charlie"),
                          QStringLiteral("delta"),
                          QStringLiteral("zulu") }));
    }

    static void keepsTheSelectionAcrossARemoval()
    {
        auto client = Client{};
        client.list.update(
            make_args(make_row(1, "alpha"sv), make_row(2, "bravo"sv), make_row(3, "charlie"sv), make_row(4, "delta"sv)),
            Coverage::Complete,
            Now);

        auto selection = QItemSelectionModel{ &client.filter };
        for (int row = 0; row < client.filter.rowCount(); ++row) {
            if (auto const index = client.filter.index(row, 0); id_of(index) != 1) {
                selection.select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            }
        }
        QCOMPARE(selection.selectedRows().size(), 3);

        // a complete list that omits 2, with `removed` naming 3
        auto args = make_args(make_row(1, "alpha"sv), make_row(4, "delta"sv));
        auto removed = tr_variant::Vector{};
        removed.emplace_back(3);
        args.try_emplace(TR_KEY_removed, std::move(removed));
        client.list.update(args, Coverage::Complete, Now);

        QCOMPARE(names_of(client.filter), (QStringList{ QStringLiteral("alpha"), QStringLiteral("delta") }));
        QCOMPARE(selection.selectedRows().size(), 1);
        QCOMPARE(id_of(selection.selectedRows().front()), 4);
    }

    static void clearRemovesEveryRow()
    {
        auto client = Client{};
        client.list.update(make_args(make_row(1, "alpha"sv), make_row(2, "bravo"sv)), Coverage::Complete, Now);

        auto selection = QItemSelectionModel{ &client.filter };
        selection.setCurrentIndex(client.filter.index(0, 0), QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);

        client.model.clear();
        QCOMPARE(client.model.rowCount(), 0);
        QCOMPARE(client.filter.rowCount(), 0);
        QVERIFY(selection.selectedRows().isEmpty());
        QVERIFY(!selection.currentIndex().isValid());

        client.model.clear(); // nothing left to do

        client.list.update(make_args(make_row(3, "charlie"sv)), Coverage::Complete, Now);
        QCOMPARE(names_of(client.filter), QStringList{ QStringLiteral("charlie") });
    }

    static void asksForInfoWhenANewTorrentHasNoName()
    {
        auto client = Client{};
        auto need_info = torrent_ids_t{};
        QObject::connect(&client.model, &TorrentModel::torrentsNeedInfo, [&need_info](torrent_ids_t const& ids) {
            need_info = ids;
        });

        // a stats-only row for a torrent we haven't seen
        auto row = tr_variant::Map{ 1U };
        row.try_emplace(TR_KEY_id, 7);
        client.list.update(make_args(std::move(row)), Coverage::Partial, Now);
        QVERIFY(need_info == (torrent_ids_t{ 7 }));
        QVERIFY(client.model.index(0, 0).data(Qt::DisplayRole).toString().isEmpty());

        client.list.update(make_args(make_row(7, "golf"sv)), Coverage::Partial, Now);
        QCOMPARE(client.model.index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("golf"));
    }

    static void rejectsIndexesOutsideTheList()
    {
        auto client = Client{};
        QVERIFY(!client.model.data(QModelIndex{}, Qt::DisplayRole).isValid());
        QVERIFY(!client.model.index(0, 0).isValid());
    }
};

} // namespace

int main(int argc, char** argv)
{
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = TorrentModelTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "torrent-model-test.moc"
