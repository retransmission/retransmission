// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include <QtCore/QTimer>

#include <libtransmission/timer.h>

// A `tr::Timer` over a `QTimer`, for the tr::app classes that take a `tr::TimerMaker`.
// As with libtransmission's own timer, a new timer is single-shot and `start()` on
// a running timer does nothing.
class QtTimer final : public tr::Timer
{
public:
    QtTimer();

    void stop() override;
    void set_callback(std::function<void()> callback) override;
    void set_repeating(bool repeating = true) override;
    void set_interval(std::chrono::milliseconds interval) override;
    void start() override;

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override;
    [[nodiscard]] bool is_repeating() const noexcept override;

private:
    QTimer timer_;
    std::function<void()> callback_;
};

class QtTimerMaker final : public tr::TimerMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::Timer> create() override;

    using tr::TimerMaker::create;
};
