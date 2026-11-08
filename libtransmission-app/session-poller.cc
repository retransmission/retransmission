// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/session-poller.h"

#include <chrono>

#include "libtransmission/timer.h"

#include "libtransmission-app/session.h"
#include "libtransmission-app/torrent-keys.h"

namespace tr::app
{

SessionPoller::SessionPoller(Session& session, tr::TimerMaker& timer_maker, PollIntervals const intervals)
    : session_{ session }
    , intervals_{ intervals }
    , timer_{ timer_maker.create([this] { tick(); }) }
    , source_changed_connection_{ session.source_changed.connect_scoped(&SessionPoller::refresh_now, this) }
    , stopped_connection_{ session.stopped.connect_scoped([this] { timer_->stop(); }) }
{
}

void SessionPoller::tick()
{
    auto const now = std::chrono::steady_clock::now();
    if (!last_full_ || now - *last_full_ >= intervals_.full_list) {
        last_full_ = now;
        session_.refresh_all_torrents(TorrentKeySet::MainStats);
    } else {
        session_.refresh_active_torrents();
    }

    session_.refresh_session_stats();
    session_.refresh_session_info();
}

void SessionPoller::start()
{
    if (!running_) {
        running_ = true;
        timer_->start_repeating(intervals_.tick);
    }
}

void SessionPoller::stop()
{
    running_ = false;
    timer_->stop();
}

void SessionPoller::refresh_now()
{
    last_full_ = std::chrono::steady_clock::now();
    session_.refresh_all_torrents(TorrentKeySet::MainAll);
    session_.refresh_session_stats();
    session_.refresh_session_info();

    if (running_) {
        // tr::Timer::start() ignores a running timer, so a restart is stop then start
        timer_->stop();
        timer_->start();
    }
}

} // namespace tr::app
