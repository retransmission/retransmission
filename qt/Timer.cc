// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Timer.h"

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <QtCore/QObject>
#include <QtCore/QTimer>

QtTimer::QtTimer()
{
    timer_.setSingleShot(true); // QTimer repeats by default; tr::Timer fires once until `set_repeating()`

    QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
        if (callback_) {
            callback_();
        }
    });
}

void QtTimer::stop()
{
    timer_.stop();
}

void QtTimer::set_callback(std::function<void()> callback)
{
    callback_ = std::move(callback);
}

void QtTimer::set_repeating(bool const repeating)
{
    timer_.setSingleShot(!repeating);
}

void QtTimer::set_interval(std::chrono::milliseconds const interval)
{
    // QTimer::setInterval() restarts a running timer; as with libtransmission's
    // own timer, an unchanged interval leaves it alone
    if (timer_.intervalAsDuration() != interval) {
        timer_.setInterval(interval);
    }
}

void QtTimer::start()
{
    // QTimer::start() restarts a running timer
    if (!timer_.isActive()) {
        timer_.start();
    }
}

std::chrono::milliseconds QtTimer::interval() const noexcept
{
    return timer_.intervalAsDuration();
}

bool QtTimer::is_repeating() const noexcept
{
    return !timer_.isSingleShot();
}

std::unique_ptr<tr::Timer> QtTimerMaker::create()
{
    return std::make_unique<QtTimer>();
}
