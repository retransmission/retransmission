// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "qt-test-fixtures.h"
#include "rpc-test-fixtures.h"

// QSignalSpy needs tr_quark registered as a metatype to capture the argument of
// Prefs::changed(tr_quark).
Q_DECLARE_METATYPE(tr_quark)

namespace
{
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
    auto settings = tr::Settings{};
    settings.insert_or_assign(TR_KEY_dht_enabled, false);
    settings.insert_or_assign(TR_KEY_lpd_enabled, false);
    settings.insert_or_assign(TR_KEY_pex_enabled, false);
    settings.insert_or_assign(TR_KEY_port_forwarding_enabled, false);
    settings.insert_or_assign(TR_KEY_utp_enabled, false);

    auto const filename = QDir{ config_dir }.filePath(QStringLiteral("settings.json")).toStdString();
    tr::settings::save(filename, settings);
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
[[nodiscard]] bool drainFreeSpace(PrefsDialog& dialog)
{
    auto* const label = dialog.findChild<FreeSpaceLabel*>();
    if (label == nullptr) {
        return false;
    }

    auto const placeholder = label->text();
    return waitUntil([label, placeholder]() { return label->text() != placeholder; });
}

class PrefsDialogTest
    : public QObject
    , SandboxedTest
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    // Toggling the "verify data when download completes" checkbox must flow
    // through PrefsDialog::set() into Prefs, which emits changed(tr_quark). This
    // is the wiring the GUI relies on to persist the preference.
    void toggling_verify_check_emits_prefs_changed()
    {
        auto const dl_dir = QDir{ sandboxDir() }.filePath(QStringLiteral("downloads"));
        QVERIFY(QDir{}.mkpath(dl_dir));

        auto prefs = Prefs{};
        writeQuietSettings(sandboxDir());
        setLocalPrefs(prefs, dl_dir);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocalFilesystem());

        auto dialog = PrefsDialog{ session, prefs };
        QVERIFY(drainFreeSpace(dialog));

        auto* const check = dialog.findChild<QCheckBox*>(QStringLiteral("downloadDoneVerifyCheck"));
        QVERIFY(check != nullptr);
        if (check == nullptr) {
            return;
        }

        // Spy after construction: the initial setChecked() runs under a
        // QSignalBlocker, so only a genuine user toggle reaches Prefs.
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const was_checked = check->isChecked();
        check->click();
        QCOMPARE(check->isChecked(), !was_checked);

        // exactly one changed(key) for the verify-on-completion preference
        auto constexpr Key = TR_KEY_torrent_complete_verify_enabled;
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<tr_quark>(), Key);
        QCOMPARE(prefs.get<bool>(Key), !was_checked);
    }

    // The "automatic updates" checkbox drives blocklist_updates_enabled, which is
    // now a session-owned pref -- toggling it is how a client asks the session to
    // turn its periodic blocklist auto-update on or off.
    void toggling_auto_update_check_emits_prefs_changed()
    {
        auto const dl_dir = QDir{ sandboxDir() }.filePath(QStringLiteral("downloads"));
        QVERIFY(QDir{}.mkpath(dl_dir));

        auto prefs = Prefs{};
        writeQuietSettings(sandboxDir());
        setLocalPrefs(prefs, dl_dir);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.isLocalFilesystem());

        auto dialog = PrefsDialog{ session, prefs };
        QVERIFY(drainFreeSpace(dialog));

        // the auto-update checkbox is only enabled while the blocklist is enabled
        auto* const enable_check = dialog.findChild<QCheckBox*>(QStringLiteral("blocklistCheck"));
        QVERIFY(enable_check != nullptr);
        if (enable_check == nullptr) {
            return;
        }
        if (!enable_check->isChecked()) {
            enable_check->click();
        }

        auto* const check = dialog.findChild<QCheckBox*>(QStringLiteral("autoUpdateBlocklistCheck"));
        QVERIFY(check != nullptr);
        if (check == nullptr) {
            return;
        }
        QVERIFY(check->isEnabled());

        // Spy after construction: the initial setChecked() runs under a
        // QSignalBlocker, so only a genuine user toggle reaches Prefs.
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const was_checked = check->isChecked();
        check->click();
        QCOMPARE(check->isChecked(), !was_checked);

        auto constexpr Key = TR_KEY_blocklist_updates_enabled;
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<tr_quark>(), Key);
        QCOMPARE(prefs.get<bool>(Key), !was_checked);
    }
};
} // namespace

int main(int argc, char** argv)
{
    qRegisterMetaType<tr_quark>("tr_quark");
    auto const app = QApplication{ argc, argv };

    auto test = PrefsDialogTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "prefs-dialog-test.moc"
