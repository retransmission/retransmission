// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Timer.h"

#include <glibmm/main.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

GlibTimer::~GlibTimer()
{
    connection_.disconnect();
}

void GlibTimer::stop()
{
    is_running_ = false;
    ++source_token_;
    connection_.disconnect();
}

void GlibTimer::set_callback(std::function<void()> callback)
{
    callback_ = std::move(callback);
}

void GlibTimer::set_repeating(bool const repeating)
{
    is_repeating_ = repeating;
}

void GlibTimer::set_interval(std::chrono::milliseconds const interval)
{
    // As with libtransmission's own timer, a new interval restarts a running timer
    // and an unchanged one leaves it alone.
    if (interval_ == interval) {
        return;
    }

    interval_ = interval;

    if (is_running_) {
        stop();
        start();
    }
}

void GlibTimer::start()
{
    if (is_running_) {
        return;
    }

    is_running_ = true;
    connection_ = Glib::signal_timeout().connect(
        [this, token = ++source_token_] { return on_timeout(token); },
        static_cast<unsigned int>(interval_.count()));
}

std::chrono::milliseconds GlibTimer::interval() const noexcept
{
    return interval_;
}

bool GlibTimer::is_repeating() const noexcept
{
    return is_repeating_;
}

bool GlibTimer::on_timeout(uint64_t const source_token)
{
    // A single shot is over before its callback runs, so the callback can start() it again.
    if (!is_repeating_) {
        is_running_ = false;
        ++source_token_;
    }

    if (callback_) {
        callback_();
    }

    // false once the shot is over, or once the callback stopped or restarted the timer
    return source_token == source_token_;
}

std::unique_ptr<tr::Timer> GlibTimerMaker::create()
{
    return std::make_unique<GlibTimer>();
}
