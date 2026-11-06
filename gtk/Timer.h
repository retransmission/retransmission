// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/timer.h>

#include <sigc++/connection.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

// A `tr::Timer` on the GLib main loop, for the tr::app classes that take a `tr::TimerMaker`.
// As with libtransmission's own timer, a new timer is single-shot and `start()` on
// a running timer does nothing.
class GlibTimer final : public tr::Timer
{
public:
    GlibTimer() = default;
    ~GlibTimer() override;
    GlibTimer(GlibTimer&&) = delete;
    GlibTimer(GlibTimer const&) = delete;
    GlibTimer& operator=(GlibTimer&&) = delete;
    GlibTimer& operator=(GlibTimer const&) = delete;

    void stop() override;
    void set_callback(std::function<void()> callback) override;
    void set_repeating(bool repeating = true) override;
    void set_interval(std::chrono::milliseconds interval) override;
    void start() override;

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override;
    [[nodiscard]] bool is_repeating() const noexcept override;

private:
    // Runs the callback. Returns whether the GLib source that called it keeps going.
    bool on_timeout(uint64_t source_token);

    std::function<void()> callback_;
    sigc::connection connection_;
    std::chrono::milliseconds interval_ = {};

    // names the GLib source that start() armed last; a source holding another token is stale
    uint64_t source_token_ = 0;

    bool is_repeating_ = false;
    bool is_running_ = false;
};

class GlibTimerMaker final : public tr::TimerMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::Timer> create() override;

    using tr::TimerMaker::create;
};
