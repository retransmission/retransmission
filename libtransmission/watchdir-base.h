// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_WATCHDIR_MODULE
#error only the wathcdir module should #include this header.
#endif

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libtransmission/error.h"
#include "libtransmission/timer.h"
#include "libtransmission/watchdir.h"

namespace tr::impl
{
// base class for concrete tr_watchdirs
class BaseWatchdir : public Watchdir
{
public:
    BaseWatchdir(std::string_view dirname, Callback callback, TimerMaker& timer_maker, NameFilter filter = {})
        : dirname_{ dirname }
        , callback_{ std::move(callback) }
        , filter_{ std::move(filter) }
        , retry_timer_{ timer_maker.create([this] { onRetryTimer(); }) }
    {
    }

    ~BaseWatchdir() override = default;

    BaseWatchdir(BaseWatchdir&&) = delete;
    BaseWatchdir(BaseWatchdir const&) = delete;
    BaseWatchdir& operator=(BaseWatchdir&&) = delete;
    BaseWatchdir& operator=(BaseWatchdir const&) = delete;

    [[nodiscard]] std::string_view dirname() const noexcept override
    {
        return dirname_;
    }

    void rescan() override
    {
        scan();
    }

protected:
    // Offers each file to the callback, and forgets the files that have left.
    void scan();
    void processFile(std::string_view basename);

    // For a file that has left the directory, so that a later file of that name is offered again.
    void forgetFile(std::string_view basename);

    // For the `errno` of a failed call that sets up a native watch.
    void logWatchError(int error_code) const;

private:
    using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;

    static constexpr auto RetryDuration = std::chrono::seconds{ 5 };

    // how long a file may keep asking for a retry before it's given up on
    static constexpr auto TimeoutDuration = std::chrono::seconds{ 15 };

    struct Pending {
        Timestamp first_kick_at;
        Timestamp last_kick_at;
    };

    void restartTimerIfPending()
    {
        if (std::empty(pending_)) {
            return;
        }

        using namespace std::chrono;
        auto const next_kick_at = std::ranges::min(pending_ | std::views::values, {}, &Pending::last_kick_at).last_kick_at +
            RetryDuration;
        // never negative: a GlibTimer reads a negative interval as weeks
        auto const delay = std::max(milliseconds{}, duration_cast<milliseconds>(next_kick_at - steady_clock::now()));
        retry_timer_->start_single_shot(delay);
    }

    void onRetryTimer()
    {
        auto const now = std::chrono::steady_clock::now();

        // processFile() keeps a retried file's entry, and with it the file's first try
        auto due = std::vector<std::string>{};
        for (auto const& [basename, info] : pending_) {
            if (info.last_kick_at + RetryDuration <= now) {
                due.push_back(basename);
            }
        }

        for (auto const& basename : due) {
            processFile(basename);
        }

        restartTimerIfPending();
    }

    std::string const dirname_;
    Callback const callback_;
    NameFilter const filter_;
    std::unique_ptr<Timer> const retry_timer_;

    std::map<std::string, Pending, std::less<>> pending_;
    std::set<std::string, std::less<>> handled_;
    tr_error::code_t scan_error_code_ = {};
};

} // namespace tr::impl
