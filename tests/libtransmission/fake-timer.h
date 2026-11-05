// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <libtransmission/timer.h>

namespace tr::test
{

// A `tr::Timer` that never elapses on its own: the test calls `fire()`.
// As with libtransmission's own timer, `start()` on a running timer does nothing.
class FakeTimer final : public tr::Timer
{
public:
    void stop() override
    {
        ++stop_count_;
        is_running_ = false;
    }

    void set_callback(std::function<void()> callback) override
    {
        callback_ = std::move(callback);
    }

    void set_repeating(bool const repeating = true) override
    {
        is_repeating_ = repeating;
    }

    void set_interval(std::chrono::milliseconds const interval) override
    {
        interval_ = interval;
    }

    void start() override
    {
        ++start_count_;
        is_running_ = true;
    }

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
    {
        return interval_;
    }

    [[nodiscard]] bool is_repeating() const noexcept override
    {
        return is_repeating_;
    }

    [[nodiscard]] constexpr bool isRunning() const noexcept
    {
        return is_running_;
    }

    // every call, including the ones a running timer ignores
    [[nodiscard]] constexpr int startCount() const noexcept
    {
        return start_count_;
    }

    [[nodiscard]] constexpr int stopCount() const noexcept
    {
        return stop_count_;
    }

    // runs the callback as if the interval had elapsed; does nothing while stopped
    void fire() const
    {
        if (is_running_ && callback_) {
            callback_();
        }
    }

private:
    std::function<void()> callback_;
    std::chrono::milliseconds interval_ = {};
    int start_count_ = 0;
    int stop_count_ = 0;
    bool is_repeating_ = false;
    bool is_running_ = false;
};

// Makes `FakeTimer`s and keeps a pointer to each, so a test can fire the timers
// of the object under test. The pointers dangle once that object destroys them.
class FakeTimerMaker final : public tr::TimerMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::Timer> create() override
    {
        auto timer = std::make_unique<FakeTimer>();
        timers_.push_back(timer.get());
        return timer;
    }

    using tr::TimerMaker::create;

    // in creation order
    [[nodiscard]] std::vector<FakeTimer*> const& timers() const noexcept
    {
        return timers_;
    }

private:
    std::vector<FakeTimer*> timers_;
};

} // namespace tr::test
