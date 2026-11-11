// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <cstddef> // size_t
#include <string>
#include <string_view>
#include <tuple> // std::ignore
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/session-poller.h"
#include "libtransmission-app/session.h"
#include "libtransmission-app/torrent-keys.h"

#include "../libtransmission/fake-timer.h"
#include "mock-rpc-server.h"
#include "test-fixtures.h"

namespace
{

using namespace std::literals;
using tr::app::SessionPoller;
using tr::app::TorrentKeySet;
using tr::test::FakeTimerMaker;

// whether `body` is a request for `method`
[[nodiscard]] bool is_request(std::string_view const body, std::string_view const method)
{
    return body.find(fmt::format(R"("method":"{:s}")", method)) != std::string_view::npos;
}

[[nodiscard]] bool has_key(std::string_view const body, std::string_view const key)
{
    return body.find(fmt::format(R"("{:s}")", key)) != std::string_view::npos;
}

// the one body in `bodies` that requests `method`, or empty if there isn't exactly one
[[nodiscard]] std::string the_request(std::vector<std::string> const& bodies, std::string_view const method)
{
    auto const is_method = [method](std::string const& body) {
        return is_request(body, method);
    };
    if (std::ranges::count_if(bodies, is_method) != 1) {
        return {};
    }
    return *std::ranges::find_if(bodies, is_method);
}

[[nodiscard]] bool has_every_key(std::string_view const body, TorrentKeySet const keys)
{
    return std::ranges::all_of(tr::app::torrent_keys(keys), [body](tr_quark const key) {
        return has_key(body, tr_quark_get_string_view(key));
    });
}

// A Session whose client talks to a MockRpcServer that answers every request
// with an empty success, and a FakeTimerMaker for the poller under test.
class AppSessionPollerTest
    : public TransmissionTest
    , public SessionUnderTest<>
{
protected:
    void TearDown() override
    {
        loop_.drain();
    }

    // Starts the client on the mock and completes its CSRF handshake, so every
    // later request reaches the mock exactly once.
    void connect()
    {
        rpc_.start(server_.url());
        std::ignore = exec_and_wait(rpc_, loop_, TR_KEY_session_stats);
        seen_ = std::size(server_.request_bodies());
    }

    // Waits for `n` more requests to reach the mock and for their replies to land,
    // then returns every request body sent since the previous call.
    [[nodiscard]] std::vector<std::string> take_bodies(size_t const n)
    {
        EXPECT_TRUE(loop_.pump_until([this, n] { return std::size(server_.request_bodies()) >= seen_ + n; }));
        loop_.drain();

        auto bodies = server_.request_bodies();
        bodies.erase(std::begin(bodies), std::begin(bodies) + static_cast<std::ptrdiff_t>(seen_));
        seen_ += std::size(bodies);
        return bodies;
    }

    // after the client, a base: TearDown drains the client's requests before the server goes
    MockRpcServer server_{ /*advertise_tr5=*/true };
    FakeTimerMaker timer_maker_;
    size_t seen_ = 0;
};

// A stopped client would only answer each tick with a network error.
TEST_F(AppSessionPollerTest, aStoppedSessionPausesTheTimerUntilANewSource)
{
    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 60s } };
    connect();
    poller.start();
    auto const& timer = *timer_maker_.timers().front();
    ASSERT_TRUE(timer.isRunning());

    session_.stop();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_TRUE(poller.is_running());

    connect();
    session_.source_changed();
    EXPECT_TRUE(timer.isRunning());
    std::ignore = take_bodies(3U);
}

// Even with the timer off.
TEST_F(AppSessionPollerTest, aNewSourceFetchesEverything)
{
    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 60s } };
    connect();

    session_.source_changed();

    auto const bodies = take_bodies(3);
    EXPECT_TRUE(has_every_key(the_request(bodies, "torrent_get"), TorrentKeySet::MainAll));
    EXPECT_FALSE(std::empty(the_request(bodies, "session_stats")));
    EXPECT_FALSE(std::empty(the_request(bodies, "session_get")));
    EXPECT_FALSE(timer_maker_.timers().front()->isRunning());
}

TEST_F(AppSessionPollerTest, startAndStopDriveOneRepeatingTimer)
{
    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 60s } };
    ASSERT_EQ(1U, std::size(timer_maker_.timers()));
    auto const& timer = *timer_maker_.timers().front();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(poller.is_running());

    poller.start();
    EXPECT_TRUE(timer.isRunning());
    EXPECT_TRUE(poller.is_running());
    EXPECT_TRUE(timer.is_repeating());
    EXPECT_EQ(3s, timer.interval());

    poller.start();
    EXPECT_EQ(1, timer.startCount());

    poller.stop();
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(poller.is_running());
}

TEST_F(AppSessionPollerTest, theFirstTickIsAFullPoll)
{
    connect();

    // `full_list` longer than any host's uptime: only the first tick can be full
    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 1h } };
    poller.start();
    auto const& timer = *timer_maker_.timers().front();

    timer.fire();
    auto bodies = take_bodies(3U);
    EXPECT_EQ(3U, std::size(bodies));
    auto const torrent_get = the_request(bodies, "torrent_get");
    EXPECT_FALSE(has_key(torrent_get, "ids")) << torrent_get;
    EXPECT_TRUE(has_every_key(torrent_get, TorrentKeySet::MainStats)) << torrent_get;
    EXPECT_FALSE(has_key(torrent_get, "name")) << torrent_get; // not a MainStats key
    EXPECT_FALSE(std::empty(the_request(bodies, "session_stats")));
    auto const session_get = the_request(bodies, "session_get");
    EXPECT_FALSE(std::empty(session_get));
    EXPECT_FALSE(has_key(session_get, "fields")) << session_get;

    timer.fire();
    bodies = take_bodies(3U);
    EXPECT_EQ(3U, std::size(bodies));
    auto const active_get = the_request(bodies, "torrent_get");
    EXPECT_TRUE(active_get.contains(R"("ids":"recently_active")")) << active_get;
    EXPECT_TRUE(has_every_key(active_get, TorrentKeySet::MainStats)) << active_get;
    EXPECT_FALSE(std::empty(the_request(bodies, "session_stats")));
    EXPECT_FALSE(std::empty(the_request(bodies, "session_get")));
}

TEST_F(AppSessionPollerTest, aZeroFullListMakesEveryTickFull)
{
    connect();

    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 0ms } };
    poller.start();
    auto const& timer = *timer_maker_.timers().front();

    for (int i = 0; i < 3; ++i) {
        timer.fire();
        auto const bodies = take_bodies(3U);
        EXPECT_EQ(3U, std::size(bodies));
        auto const torrent_get = the_request(bodies, "torrent_get");
        EXPECT_FALSE(std::empty(torrent_get));
        EXPECT_FALSE(has_key(torrent_get, "ids")) << torrent_get;
    }
}

TEST_F(AppSessionPollerTest, refreshNowFetchesEverythingAndRestartsTheTick)
{
    connect();

    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 1h } };
    poller.start();
    auto const& timer = *timer_maker_.timers().front();
    auto const starts = timer.startCount();
    auto const stops = timer.stopCount();

    poller.refresh_now();
    auto bodies = take_bodies(3U);
    EXPECT_EQ(3U, std::size(bodies));
    auto const torrent_get = the_request(bodies, "torrent_get");
    EXPECT_FALSE(has_key(torrent_get, "ids")) << torrent_get;
    EXPECT_TRUE(has_every_key(torrent_get, TorrentKeySet::MainAll)) << torrent_get;
    EXPECT_FALSE(std::empty(the_request(bodies, "session_stats")));
    EXPECT_FALSE(std::empty(the_request(bodies, "session_get")));

    // stopped then started: a start then a stop would leave it stopped
    EXPECT_EQ(starts + 1, timer.startCount());
    EXPECT_EQ(stops + 1, timer.stopCount());
    EXPECT_TRUE(timer.isRunning());

    // refresh_now() was the full poll
    timer.fire();
    bodies = take_bodies(3U);
    auto const active_get = the_request(bodies, "torrent_get");
    EXPECT_TRUE(active_get.contains(R"("ids":"recently_active")")) << active_get;
}

TEST_F(AppSessionPollerTest, refreshNowWhileStoppedLeavesTheTimerAlone)
{
    connect();

    auto poller = SessionPoller{ session_, timer_maker_ };
    auto const& timer = *timer_maker_.timers().front();

    poller.refresh_now();
    EXPECT_EQ(3U, std::size(take_bodies(3U)));
    EXPECT_EQ(0, timer.startCount());
    EXPECT_EQ(0, timer.stopCount());
    EXPECT_FALSE(timer.isRunning());
    EXPECT_FALSE(poller.is_running());
}

TEST_F(AppSessionPollerTest, ticksBeforeTheClientStartsSendNothing)
{
    auto poller = SessionPoller{ session_, timer_maker_ };
    poller.start();
    auto updates = 0;
    auto const stats_connection = session_.stats_updated.connect_scoped([&updates] { ++updates; });
    auto const info_connection = session_.session_updated.connect_scoped([&updates] { ++updates; });

    // the client answers each request with a network error, which ends its chain
    timer_maker_.timers().front()->fire();
    loop_.drain();

    EXPECT_TRUE(std::empty(server_.request_bodies()));
    EXPECT_EQ(0U, std::size(session_.torrents()));
    EXPECT_EQ(0, updates);
}

} // namespace
