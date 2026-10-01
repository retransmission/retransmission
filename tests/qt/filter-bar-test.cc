// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <utility>

#include <QApplication>
#include <QComboBox>
#include <QString>
#include <QTest>

#include <libtransmission/quark.h>

#include "FilterBar.h"
#include "Prefs.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "TrQtInit.h"
#include "UserMetaType.h"
#include "qt-test-fixtures.h"

namespace
{
// The activity combo and the show-mode pref mirror each other:
// picking a row sets the pref, and a pref change selects its row.
class FilterBarTest : public QObject
{
    Q_OBJECT

    // The labels are the untranslated source strings; no translator is loaded here.
    static auto constexpr Rows = std::array<std::pair<char const*, ShowMode>, ShowModeCount>{ {
        { "All", ShowMode::ShowAll },
        { "Active", ShowMode::ShowActive },
        { "Seeding", ShowMode::ShowSeeding },
        { "Downloading", ShowMode::ShowDownloading },
        { "Paused", ShowMode::ShowPaused },
        { "Finished", ShowMode::ShowFinished },
        { "Verifying", ShowMode::ShowVerifying },
        { "Error", ShowMode::ShowError },
    } };

    struct Client {
        Prefs prefs;
        TorrentModel model{ prefs };
        TorrentFilter filter{ prefs };
        FilterBar bar{ prefs, model, filter };
    };

    // The tracker combo has no "Active" row.
    [[nodiscard]] static QComboBox* activityCombo(FilterBar const& bar)
    {
        for (auto* const combo : bar.findChildren<QComboBox*>()) {
            if (combo->findText(QStringLiteral("Active")) != -1) {
                return combo;
            }
        }

        return nullptr;
    }

private slots:
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    // Picking a row feeds back through the pref's change signal,
    // so a mismatched lookup there selects some other row.
    static void keepsThePickedActivity()
    {
        auto client = Client{};
        auto* const combo = activityCombo(client.bar);
        QVERIFY(combo != nullptr);

        for (auto const& [label, mode] : Rows) {
            auto const row = combo->findText(QString::fromUtf8(label));
            QVERIFY2(row != -1, label);

            combo->setCurrentIndex(row);

            QVERIFY2(combo->currentIndex() == row, label);
            QVERIFY2(client.prefs.get<ShowMode>(TR_KEY_show_mode) == mode, label);
        }
    }

    static void selectsTheRowOfTheShowModePref()
    {
        auto client = Client{};
        auto* const combo = activityCombo(client.bar);
        QVERIFY(combo != nullptr);

        for (auto const& [label, mode] : Rows) {
            client.prefs.set(TR_KEY_show_mode, mode);

            QVERIFY2(combo->currentText() == QString::fromUtf8(label), label);
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = FilterBarTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "filter-bar-test.moc"
