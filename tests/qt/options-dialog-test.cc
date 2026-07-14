// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

// Required by libtransmission public headers in this test TU.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
//#define __TRANSMISSION__ // for libtransmission/net.h (via rpc-test-fixtures.h)

#include <cstdint>
#include <string_view>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <QTest>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

#include "AddData.h"
#include "FreeSpaceLabel.h"
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

// Point prefs at a remote RPC server on the loopback `port`. The session stays
// "remote" (no server-provided local session id), so the dialog shows the combo.
void setRemotePrefs(Prefs& prefs, std::uint16_t const port)
{
    prefs.set(TR_KEY_remote_session_enabled, true);
    prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
    prefs.set(TR_KEY_remote_session_port, static_cast<int>(port));
    prefs.set(TR_KEY_download_dir, download_dir);
}

// Point prefs at an in-process session. Such a session reports
// isLocalFilesystem(), which is what makes the dialog show the path button.
void setLocalPrefs(Prefs& prefs, QString const& dl_dir)
{
    prefs.set(TR_KEY_remote_session_enabled, false);
    prefs.set(TR_KEY_download_dir, dl_dir);
}

// Keep an in-process session off the network so the test stays fast and
// deterministic (mirrors the libtransmission session fixture defaults).
void writeQuietSettings(QString const& config_dir)
{
    static constexpr auto Json = std::string_view{ R"({"dht-enabled":false,"lpd-enabled":false,"pex-enabled":false,)"
                                                   R"("port-forwarding-enabled":false,"utp-enabled":false})" };

    auto file = QFile{ QDir{ config_dir }.filePath(QStringLiteral("settings.json")) };
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(std::data(Json), static_cast<qint64>(std::size(Json)));
    }
}

// Pump the Qt event loop until `pred` is true or we time out. Returning control
// to the loop lets RpcClient deliver its queued-connection continuations.
template<typename Predicate>
[[nodiscard]] bool waitUntil(Predicate const& pred, int const timeout_ms = 10000)
{
    auto timer = QElapsedTimer{};
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeout_ms) {
            return false;
        }
        QTest::qWait(20);
    }
    return true;
}

// Building the dialog kicks off FreeSpaceLabel's async free-space request, which
// runs on a tr::app::RpcQueue. That queue keeps a reference to itself until it
// finishes, so it must run to completion before teardown or the never-finished
// queue leaks under LSan. A successful reply replaces the label's "Calculating…"
// placeholder, proving the queue reached its final step and released itself.
[[nodiscard]] bool drainFreeSpace(OptionsDialog& dialog)
{
    auto* const label = dialog.findChild<FreeSpaceLabel*>();
    if (label == nullptr) {
        return false;
    }

    auto const placeholder = label->text();
    return waitUntil([label, placeholder]() { return label->text() != placeholder; });
}

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
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    // A remote session can't browse the server's filesystem, so the dialog
    // offers an editable combo box seeded with the server's recent paths.
    void remote_session_uses_editable_combo()
    {
        auto const recents = sampleRecents();

        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server.port());
        prefs.set(TR_KEY_recent_download_paths, recents);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(!session.isLocalFilesystem());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };
        QVERIFY(drainFreeSpace(dialog));

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
        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server.port());
        prefs.set(TR_KEY_recent_download_paths, std::vector<QString>{});

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(!session.isLocalFilesystem());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };
        QVERIFY(drainFreeSpace(dialog));

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

        auto const dl_dir = QDir{ sandboxDir() }.filePath(QStringLiteral("downloads"));
        QVERIFY(QDir{}.mkpath(dl_dir));

        auto prefs = Prefs{};
        writeQuietSettings(sandboxDir());
        setLocalPrefs(prefs, dl_dir);
        prefs.set(TR_KEY_recent_download_paths, recents);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocalFilesystem());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };
        QVERIFY(drainFreeSpace(dialog));

        auto* const stack = dialog.findChild<QStackedWidget*>(QStringLiteral("destinationStack"));
        auto* const button = dialog.findChild<PathButton*>(QStringLiteral("destinationButton"));
        QVERIFY(stack != nullptr);
        QVERIFY(button != nullptr);

        // local sessions show the path button, not the combo
        QCOMPARE(stack->currentWidget(), static_cast<QWidget*>(button));
        QCOMPARE(button->path(), QDir::toNativeSeparators(dl_dir));

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
        auto const dl_dir = QDir{ sandboxDir() }.filePath(QStringLiteral("downloads"));
        QVERIFY(QDir{}.mkpath(dl_dir));

        auto prefs = Prefs{};
        writeQuietSettings(sandboxDir());
        setLocalPrefs(prefs, dl_dir);
        prefs.set(TR_KEY_recent_download_paths, std::vector<QString>{});

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocalFilesystem());

        auto dialog = OptionsDialog{ session, prefs, AddData{ magnet } };
        QVERIFY(drainFreeSpace(dialog));

        auto* const button = dialog.findChild<PathButton*>(QStringLiteral("destinationButton"));
        QVERIFY(button != nullptr);
        if (button == nullptr) {
            return;
        }
        QVERIFY(button->menu() == nullptr);
        QCOMPARE(button->path(), QDir::toNativeSeparators(dl_dir));
    }
};
} // namespace

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = OptionsDialogTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "options-dialog-test.moc"
