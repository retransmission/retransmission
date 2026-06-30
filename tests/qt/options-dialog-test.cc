// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <vector>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QMenu>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTest>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "AddData.h"
#include "OptionsDialog.h"
#include "PathButton.h"
#include "Prefs.h"
#include "Session.h"
#include "qt-test-fixtures.h"
#include "rpc-test-fixtures.h"

namespace
{
auto const magnet = QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
auto const download_dir = QStringLiteral("/data/downloads");

void setRemotePrefs(Prefs& prefs, QString const& host)
{
    prefs.set(TR_KEY_remote_session_enabled, true);
    prefs.set(TR_KEY_remote_session_host, host);
    prefs.set(TR_KEY_remote_session_port, TrDefaultRpcPort);
    prefs.set(TR_KEY_download_dir, download_dir);
}
} // namespace

class OptionsDialogTest
    : public QObject
    , SandboxedTest
{
    Q_OBJECT

    [[nodiscard]] static std::vector<QString> sampleRecents()
    {
        return { QStringLiteral("/data/movies"), QStringLiteral("/data/music"), QStringLiteral("/data/books") };
    }

private slots:
    // A remote session can't browse the server's filesystem, so the dialog
    // offers an editable combo box seeded with the server's recent paths.
    void remote_session_uses_editable_combo()
    {
        auto const recents = sampleRecents();

        auto prefs = Prefs{};
        setRemotePrefs(prefs, QStringLiteral("example.invalid"));
        prefs.set(TR_KEY_recent_download_paths, recents);

        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(!session.isLocal());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };

        auto* const stack = dialog.findChild<QStackedWidget*>(QStringLiteral("destinationStack"));
        auto* const combo = dialog.findChild<QComboBox*>(QStringLiteral("destinationCombo"));
        QVERIFY(stack != nullptr);
        QVERIFY(combo != nullptr);
        if (combo == nullptr) {
            return;
        }

        // remote sessions show the editable combo, not the path button
        QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(combo));
        QVERIFY(combo->isEditable());

        // the combo is seeded with the recent paths and the current download dir
        QCOMPARE(combo->count(), static_cast<int>(recents.size()));
        for (auto i = 0U; i < recents.size(); ++i) {
            QCOMPARE(combo->itemText(static_cast<int>(i)), recents.at(i));
        }
        QCOMPARE(combo->currentText(), download_dir);
    }

    // A server without the recent-paths feature reports an empty list; the combo
    // is then empty but still type-able.
    void remote_session_without_recents_keeps_combo_editable()
    {
        auto prefs = Prefs{};
        setRemotePrefs(prefs, QStringLiteral("example.invalid"));
        prefs.set(TR_KEY_recent_download_paths, std::vector<QString>{});

        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(!session.isLocal());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };

        auto* const combo = dialog.findChild<QComboBox*>(QStringLiteral("destinationCombo"));
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 0);
        QVERIFY(combo->isEditable());
        QCOMPARE(combo->currentText(), download_dir);
    }

    // A local session can browse the filesystem, so it shows the path button,
    // and the recent paths become a drop-down menu on that button.
    void local_session_uses_path_button_with_recents_menu()
    {
        auto const recents = sampleRecents();

        auto prefs = Prefs{};
        setRemotePrefs(prefs, QStringLiteral("127.0.0.1")); // loopback => isLocal()
        prefs.set(TR_KEY_recent_download_paths, recents);

        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocal());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };

        auto* const stack = dialog.findChild<QStackedWidget*>(QStringLiteral("destinationStack"));
        auto* const button = dialog.findChild<PathButton*>(QStringLiteral("destinationButton"));
        QVERIFY(stack != nullptr);
        QVERIFY(button != nullptr);

        // local sessions show the path button, not the combo
        QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(button));
        QCOMPARE(button->path(), QDir::toNativeSeparators(download_dir));

        auto* const menu = button->menu();
        QVERIFY(menu != nullptr);

        // one action per recent path, then a separator, then "Other…"
        auto const actions = menu->actions();
        QCOMPARE(actions.size(), static_cast<int>(recents.size()) + 2);
        // PathButton normalizes paths to native separators, so do the same here.
        for (auto i = 0U; i < recents.size(); ++i) {
            QCOMPARE(actions.at(static_cast<int>(i))->toolTip(), QDir::toNativeSeparators(recents.at(i)));
        }
        QVERIFY(actions.at(static_cast<int>(recents.size()))->isSeparator());
    }

    // With an empty recents list the path button has no menu and behaves like a
    // plain chooser.
    void local_session_without_recents_has_no_menu()
    {
        auto prefs = Prefs{};
        setRemotePrefs(prefs, QStringLiteral("127.0.0.1")); // loopback => isLocal()
        prefs.set(TR_KEY_recent_download_paths, std::vector<QString>{});

        auto nam = FakeNetworkAccessManager{};
        auto rpc = RpcClient{ nam };
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocal());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };

        auto* const button = dialog.findChild<PathButton*>(QStringLiteral("destinationButton"));
        QVERIFY(button != nullptr);
        if (button == nullptr) {
            return;
        }
        QVERIFY(button->menu() == nullptr);
        QCOMPARE(button->path(), QDir::toNativeSeparators(download_dir));
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = OptionsDialogTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "options-dialog-test.moc"
