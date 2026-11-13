// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "DirMonitor.h"

#include "qt-test-fixtures.h"

namespace
{

void drop_file(QTemporaryDir const& dir)
{
    auto file = QFile{ dir.filePath(QStringLiteral("new.torrent")) };
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("contents") > 0);
}

class QtDirMonitorTest : public QObject
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    static void reportsANewFile()
    {
        auto const dir = QTemporaryDir{};
        QVERIFY(dir.isValid());
        auto maker = QtDirMonitorMaker{};
        auto changes = 0;
        auto const monitor = maker.create(dir.path().toStdString(), [&changes] { ++changes; });
        QVERIFY(monitor != nullptr);

        drop_file(dir);
        QTRY_VERIFY(changes != 0);
    }

    static void staysQuietOnceDestroyed()
    {
        auto const dir = QTemporaryDir{};
        QVERIFY(dir.isValid());
        auto maker = QtDirMonitorMaker{};
        auto changes = 0;
        auto monitor = maker.create(dir.path().toStdString(), [&changes] { ++changes; });
        QVERIFY(monitor != nullptr);
        monitor.reset();

        drop_file(dir);
        QTest::qWait(300);
        QCOMPARE(changes, 0);
    }

    static void cannotMonitorAMissingDir()
    {
        auto maker = QtDirMonitorMaker{};
        QVERIFY(maker.create("/nonexistent/watch/dir", [] {}) == nullptr);
    }
};

} // namespace

QTEST_GUILESS_MAIN(QtDirMonitorTest)

#include "dir-monitor-test.moc"
