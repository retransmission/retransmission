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
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "libtransmission/error.h"
#include "libtransmission/timer.h"
#include "libtransmission/watchdir.h"

namespace tr::impl
{
// base class for concrete tr_watchdirs
class BaseWatchdir : public Watchdir
{
public:
    BaseWatchdir(std::string_view dirname, Callback callback, TimerMaker& timer_maker)
        : dirname_{ dirname }
        , callback_{ std::move(callback) }
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

    constexpr void setRetryDuration(std::chrono::milliseconds retry_duration) noexcept
    {
        retry_duration_ = retry_duration;
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

    // how long a file may keep asking for a retry before it's given up on
    static constexpr auto TimeoutDuration = std::chrono::seconds{ 15 };

    struct Pending {
        Timestamp first_kick_at;
        Timestamp last_kick_at;
    };

    [[nodiscard]] std::optional<Timestamp> nextKickTime() const
    {
        if (std::empty(pending_)) {
            return {};
        }

        auto const last_kick_at = [](auto const& entry) {
            return entry.second.last_kick_at;
        };
        return std::ranges::min_element(pending_, {}, last_kick_at)->second.last_kick_at + retry_duration_;
    }

    void restartTimerIfPending()
    {
        if (auto next_kick_time = nextKickTime(); next_kick_time) {
            using namespace std::chrono;
            auto const now = steady_clock::now();
            auto const duration = duration_cast<milliseconds>(*next_kick_time - now);
            retry_timer_->start_single_shot(duration);
        }
    }

    void onRetryTimer()
    {
        using namespace std::chrono;
        auto const now = steady_clock::now();

        auto tmp = decltype(pending_){};
        std::swap(tmp, pending_);
        for (auto const& [basename, info] : tmp) {
            if (info.last_kick_at + retry_duration_ <= now) {
                processFile(basename);
            } else {
                pending_.try_emplace(basename, info);
            }
        }

        restartTimerIfPending();
    }

    std::string const dirname_;
    Callback const callback_;
    std::unique_ptr<Timer> const retry_timer_;

    std::map<std::string, Pending, std::less<>> pending_;
    std::set<std::string, std::less<>> handled_;
    tr_error::code_t scan_error_code_ = {};
    std::chrono::milliseconds retry_duration_ = std::chrono::seconds{ 5 };
};

} // namespace tr::impl
