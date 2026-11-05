// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <sigslot/signal.hpp>

#include "libtransmission/timer.h"

namespace tr::app
{

class Session;

// how often `SessionPoller` refreshes, and how often that refresh asks for every torrent
struct PollIntervals {
    std::chrono::milliseconds tick = std::chrono::seconds{ 3 };
    std::chrono::milliseconds full_list = std::chrono::seconds{ 60 };
};

// Keeps a `Session`'s torrents, stats and info fresh: every `tick` refreshes the
// recently active torrents, the stats and the info; once per `full_list` the
// torrent poll asks for every torrent instead, so the list also learns of
// removals and of torrents that never showed activity. A stopped session pauses the
// timer, and a new session source gets `refresh_now()`. Apart from `Session`,
// which then needs no timer: a client may poll another way, and a test drives
// the Session's refreshes directly.
class SessionPoller
{
public:
    // `timer_maker` must outlive the poller.
    SessionPoller(Session& session, tr::TimerMaker& timer_maker, PollIntervals intervals = {});
    ~SessionPoller() = default;
    SessionPoller(SessionPoller&&) = delete;
    SessionPoller(SessionPoller const&) = delete;
    SessionPoller& operator=(SessionPoller&&) = delete;
    SessionPoller& operator=(SessionPoller const&) = delete;

    // Arms the repeating timer; the first tick is one interval away and is a full
    // poll. Idempotent. Ticks before the session is reachable are answered as
    // network errors and change nothing.
    void start();

    // Disarms the timer. Requests already in flight still land in the session.
    void stop();

    [[nodiscard]] constexpr bool is_running() const noexcept
    {
        return running_;
    }

    // Fetches everything now: every torrent with `MainAll`, the stats, the info.
    // Counts as the full poll, and while running restarts the tick so the next one
    // is a whole interval away.
    void refresh_now();

private:
    void tick();

    Session& session_;
    PollIntervals const intervals_;
    std::unique_ptr<tr::Timer> const timer_;

    // when the list was last fetched whole; empty until then, so the first tick is a full poll
    std::optional<std::chrono::steady_clock::time_point> last_full_;
    bool running_ = false;

    sigslot::scoped_connection source_changed_connection_;
    sigslot::scoped_connection stopped_connection_;
};

} // namespace tr::app
