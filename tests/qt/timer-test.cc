// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>

#include <QObject>
#include <QTest>

#include "Timer.h"

using namespace std::literals;

namespace
{

class QtTimerTest : public QObject
{
    Q_OBJECT

private slots:
    // as with libtransmission's own timer, a new timer fires once
    static void firesOnceByDefault()
    {
        auto maker = QtTimerMaker{};
        auto fired = 0;
        auto const timer = maker.create([&fired] { ++fired; });
        QVERIFY(!timer->is_repeating());

        timer->start(5ms);
        QTRY_COMPARE(fired, 1);
        QTest::qWait(50);
        QCOMPARE(fired, 1);
    }

    static void repeatsWhenAsked()
    {
        auto maker = QtTimerMaker{};
        auto fired = 0;
        auto const timer = maker.create([&fired] { ++fired; });

        timer->start_repeating(5ms);
        QVERIFY(timer->is_repeating());
        QCOMPARE(timer->interval(), 5ms);
        QTRY_VERIFY(fired >= 3);

        timer->stop();
        auto const stopped_at = fired;
        QTest::qWait(50);
        QCOMPARE(fired, stopped_at);
    }

    // a running timer keeps its schedule: `start()` with the same interval leaves it alone
    static void startOnARunningTimerDoesNotRestartIt()
    {
        auto maker = QtTimerMaker{};
        auto fired = 0;
        auto const timer = maker.create([&fired] { ++fired; });

        timer->start_single_shot(300ms);
        QTest::qWait(150);
        timer->start(300ms); // a restart would push the shot to 300 ms from now
        QTest::qWait(240); // past the first shot, short of a restarted one
        QCOMPARE(fired, 1);
    }
};

} // namespace

QTEST_GUILESS_MAIN(QtTimerTest)

#include "timer-test.moc"
