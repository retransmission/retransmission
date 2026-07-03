// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <vector>

#include <QApplication>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <libtransmission/quark.h>

#include "Prefs.h"
#include "TrQtInit.h"
#include "qt-test-fixtures.h"

Q_DECLARE_METATYPE(tr_quark)

namespace
{
class PrefsTest : public QObject
{
    Q_OBJECT

    template<typename T>
    void verify_get_set_by_property(Prefs& prefs, tr_quark const key, T const& val1, T const& val2)
    {
        TRCOMPARE_NE(val1, val2);

        prefs.set(key, val1);
        TRCOMPARE_EQ(prefs.get<T>(key), val1);
        TRCOMPARE_NE(prefs.get<T>(key), val2);

        prefs.set(key, val2);
        TRCOMPARE_NE(prefs.get<T>(key), val1);
        TRCOMPARE_EQ(prefs.get<T>(key), val2);
    }

private slots:
    // QString round-trips exercise the Qt-only Converter in VariantHelpers.
    void handles_qstring()
    {
        auto constexpr Key = TR_KEY_download_dir;
        auto const val_a = QStringLiteral("/tmp/transmission-test-download-dir");
        auto const val_b = QStringLiteral("/tmp/transmission-test-download-dir-b");

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, val_a, val_b);
    }

    // QStringList round-trips exercise the Qt-only Converter in VariantHelpers.
    void handles_qstringlist()
    {
        auto constexpr Key = TR_KEY_torrent_complete_sound_command;
        auto const val_a = std::vector<QString>{ QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three") };
        auto const val_b = std::vector<QString>{ QStringLiteral("alpha"), QStringLiteral("beta") };

        auto prefs = Prefs{};
        verify_get_set_by_property(prefs, Key, val_a, val_b);
    }

    // ---

    // The Qt `changed(tr_quark)` signal is Prefs's Qt-specific `on_changed()`
    // override; the base notification hook is covered by the app-level tests.
    static void changed_signal_emits_when_change()
    {
        static auto constexpr Key = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const old_value = prefs.get<bool>(Key);
        auto const new_value = !old_value;

        // a real change emits exactly one `changed(key)`
        prefs.set(Key, new_value);
        TRCOMPARE_EQ(prefs.get<bool>(Key), new_value);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<tr_quark>(), Key);

        // re-setting the same value does not emit again
        prefs.set(Key, new_value);
        QCOMPARE(spy.count(), 1);
    }

    static void changed_signal_does_not_emit_when_unchanged()
    {
        static auto constexpr Key = TR_KEY_sort_reversed;

        auto prefs = Prefs{};
        auto const spy = QSignalSpy{ &prefs, qOverload<tr_quark>(&Prefs::changed) };

        auto const current_value = prefs.get<bool>(Key);
        prefs.set(Key, current_value);
        QCOMPARE(spy.count(), 0);
    }
};
} // namespace

int main(int argc, char** argv)
{
    qRegisterMetaType<tr_quark>("tr_quark");
    trqt::trqt_init();
    auto const app = QApplication{ argc, argv };
    auto test = PrefsTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "prefs-test.moc"
