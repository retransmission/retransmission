// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <cstddef> // size_t
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/constants.h> // TrDefaultHttpServerBasePath, TrHttpServerRpcRelativePath
#include <libtransmission/quark.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/session-poller.h"
#include "libtransmission-app/session.h"
#include "libtransmission-app/torrent-keys.h"

#include "mock-rpc-server.h"
#include "test-fixtures.h"

namespace
{

using namespace std::literals;
using tr::app::Prefs;
using tr::app::RpcClient;
using tr::app::RpcResponse;
using tr::app::Session;
using tr::app::SessionPoller;
using tr::app::TorrentKeySet;

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
class AppSessionPollerTest : public TransmissionTest
{
protected:
    void TearDown() override
    {
        drain();
    }

    // Starts the client on the mock and completes its CSRF handshake, so every
    // later request reaches the mock exactly once.
    void connect()
    {
        rpc_.start(
            fmt::format(
                "http://127.0.0.1:{:d}{:s}{:s}",
                server_.port(),
                TrDefaultHttpServerBasePath,
                TrHttpServerRpcRelativePath));

        auto answered = false;
        rpc_.exec(TR_KEY_session_stats, {}, [&answered](RpcResponse const& /*response*/) { answered = true; });
        ASSERT_TRUE(loop_.pump_until([&answered] { return answered; }));
        seen_ = std::size(server_.request_bodies());
    }

    // lets every reply land, including those to the requests the replies sent
    void drain()
    {
        while (!loop_.quiet_for(50ms)) {
        }
    }

    // Waits for `n` more requests to reach the mock and for their replies to land,
    // then returns every request body sent since the previous call.
    [[nodiscard]] std::vector<std::string> take_bodies(size_t const n)
    {
        EXPECT_TRUE(loop_.pump_until([this, n] { return std::size(server_.request_bodies()) >= seen_ + n; }));
        drain();

        auto bodies = server_.request_bodies();
        bodies.erase(std::begin(bodies), std::begin(bodies) + static_cast<std::ptrdiff_t>(seen_));
        seen_ += std::size(bodies);
        return bodies;
    }

    MockRpcServer server_{ /*advertise_tr5=*/true }; // first: outlives the client's requests
    UiLoop loop_; // before rpc_: a late reply calls its marshaler
    RpcClient rpc_{ loop_.marshaler() };
    Prefs prefs_;
    Session session_{ prefs_, rpc_ };
    FakeTimerMaker timer_maker_;
    size_t seen_ = 0;
};

TEST_F(AppSessionPollerTest, startAndStopDriveOneRepeatingTimer)
{
    auto poller = SessionPoller{ session_, timer_maker_, { .tick = 3s, .full_list = 60s } };
    ASSERT_EQ(1U, std::size(timer_maker_.timers()));
    auto const& timer = *timer_maker_.timers().front();
    EXPECT_FALSE(timer.is_running());
    EXPECT_FALSE(poller.is_running());

    poller.start();
    EXPECT_TRUE(timer.is_running());
    EXPECT_TRUE(poller.is_running());
    EXPECT_TRUE(timer.is_repeating());
    EXPECT_EQ(3s, timer.interval());

    poller.start();
    EXPECT_EQ(1, timer.start_count());

    poller.stop();
    EXPECT_FALSE(timer.is_running());
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
    auto const starts = timer.start_count();
    auto const stops = timer.stop_count();

    poller.refresh_now();
    auto bodies = take_bodies(3U);
    EXPECT_EQ(3U, std::size(bodies));
    auto const torrent_get = the_request(bodies, "torrent_get");
    EXPECT_FALSE(has_key(torrent_get, "ids")) << torrent_get;
    EXPECT_TRUE(has_every_key(torrent_get, TorrentKeySet::MainAll)) << torrent_get;
    EXPECT_FALSE(std::empty(the_request(bodies, "session_stats")));
    EXPECT_FALSE(std::empty(the_request(bodies, "session_get")));

    // stopped then started: a start then a stop would leave it stopped
    EXPECT_EQ(starts + 1, timer.start_count());
    EXPECT_EQ(stops + 1, timer.stop_count());
    EXPECT_TRUE(timer.is_running());

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
    EXPECT_EQ(0, timer.start_count());
    EXPECT_EQ(0, timer.stop_count());
    EXPECT_FALSE(timer.is_running());
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
    drain();

    EXPECT_TRUE(std::empty(server_.request_bodies()));
    EXPECT_EQ(0U, std::size(session_.torrents()));
    EXPECT_EQ(0, updates);
}

} // namespace
