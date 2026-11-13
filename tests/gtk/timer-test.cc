// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>

#include <glibmm/init.h>

#include <gtest/gtest.h>

#include "Timer.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

class GlibTimerTest : public GtkTest
{
protected:
    void SetUp() override
    {
        GtkTest::SetUp();
        Glib::init();
    }

    GlibTimerMaker maker_;
};

} // namespace

// as with libtransmission's own timer, a new timer fires once
TEST_F(GlibTimerTest, firesOnceByDefault)
{
    auto fired = 0;
    auto const timer = maker_.create([&fired] { ++fired; });
    EXPECT_FALSE(timer->is_repeating());

    timer->start(5ms);
    ASSERT_TRUE(spin_until([&fired] { return fired == 1; }));
    spin_for(50ms);
    EXPECT_EQ(1, fired);
}

TEST_F(GlibTimerTest, repeatsWhenAsked)
{
    auto fired = 0;
    auto const timer = maker_.create([&fired] { ++fired; });

    timer->start_repeating(5ms);
    EXPECT_TRUE(timer->is_repeating());
    EXPECT_EQ(5ms, timer->interval());
    ASSERT_TRUE(spin_until([&fired] { return fired >= 3; }));

    timer->stop();
    auto const stopped_at = fired;
    spin_for(50ms);
    EXPECT_EQ(stopped_at, fired);
}

// a running timer keeps its schedule: `start()` with the same interval leaves it alone
TEST_F(GlibTimerTest, startOnARunningTimerDoesNotRestartIt)
{
    auto fired = 0;
    auto const timer = maker_.create([&fired] { ++fired; });

    timer->start_single_shot(300ms);
    spin_for(150ms);
    timer->start(300ms); // a restart would push the shot to 300 ms from now
    spin_for(240ms); // past the first shot, short of a restarted one
    EXPECT_EQ(1, fired);
}

// a single shot is over when its callback runs, so the callback can arm the next one
TEST_F(GlibTimerTest, aSingleShotCanStartItselfAgain)
{
    auto fired = 0;
    auto const timer = maker_.create();
    timer->set_callback([&fired, &timer] {
        if (++fired < 3) {
            timer->start();
        }
    });

    timer->start_single_shot(5ms);
    ASSERT_TRUE(spin_until([&fired] { return fired == 3; }));
    spin_for(50ms);
    EXPECT_EQ(3, fired);
}

// a repeating timer that its callback stops fires no more
TEST_F(GlibTimerTest, aRepeatingTimerCanStopItself)
{
    auto fired = 0;
    auto const timer = maker_.create();
    timer->set_callback([&fired, &timer] {
        if (++fired == 2) {
            timer->stop();
        }
    });

    timer->start_repeating(5ms);
    ASSERT_TRUE(spin_until([&fired] { return fired == 2; }));
    spin_for(50ms);
    EXPECT_EQ(2, fired);
}

// as with libtransmission's own timer, a callback may destroy its own timer
TEST_F(GlibTimerTest, aCallbackCanDestroyItsTimer)
{
    auto fired = 0;
    auto timer = maker_.create();
    timer->set_callback([&fired, &timer] {
        ++fired;
        timer.reset();
    });

    timer->start_repeating(5ms);
    ASSERT_TRUE(spin_until([&timer] { return timer == nullptr; }));
    spin_for(50ms);
    EXPECT_EQ(1, fired);
}
