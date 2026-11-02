// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <libtransmission/api-compat.h>
#include <libtransmission/macros.h>
#include <libtransmission/quark.h>
#include <libtransmission/transmission.h> // tr_sessionInit()
#include <libtransmission/utils.h> // tr_lib_init()
#include <libtransmission/variant.h>

#include <libtransmission-app/rpc-client.h>

#include <gtest/gtest.h>

#include "mock-rpc-server.h"
#include "test-fixtures.h"

namespace api_compat = tr::api_compat;
using tr::app::RpcClient;
using tr::app::RpcResponse;
using namespace std::literals;

namespace
{

class AppRpcClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tr_lib_init(); // start up Winsock on Windows before the mock server binds
        saved_style_ = api_compat::default_style();
    }

    void TearDown() override
    {
        api_compat::set_default_style(saved_style_);
    }

private:
    api_compat::Style saved_style_ = api_compat::Style::Tr4;
};

// A caller that counts replies, e.g. a dialog holding its widgets until a
// torrent_set completes, needs one reply per exec() whether or not the client
// is connected yet.
TEST_F(AppRpcClientTest, answersARequestMadeBeforeStartWithANetworkError)
{
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    auto network_responses = 0;
    auto const tag = client.network_response.connect_scoped(
        [&network_responses](bool /*is_network_error*/, std::string_view /*message*/) { ++network_responses; });

    auto fut = exec_async(client, TR_KEY_session_get);
    EXPECT_NE(fut.wait_for(0s), std::future_status::ready); // answered on the UI thread, not inside exec()

    auto const response = loop.pump_until_ready(fut);
    EXPECT_FALSE(response.success);
    EXPECT_TRUE(response.network_error);
    EXPECT_FALSE(std::empty(response.errmsg));
    EXPECT_EQ(0, network_responses);
}

TEST_F(AppRpcClientTest, firstPostUsesDefaultStyleTr4)
{
    api_compat::set_default_style(api_compat::Style::Tr4);

    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(200, response.http_status);
    EXPECT_FALSE(response.network_error);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.front().find(R"("method":"session-get")")) << bodies.front();
    EXPECT_EQ(std::string::npos, bodies.front().find("jsonrpc")) << bodies.front();

    // headers look right
    EXPECT_NE(std::string::npos, server.last_content_type().find("application/json"));
    EXPECT_EQ(0U, server.last_user_agent().rfind(TR_PROJ_APPNAME_CAPITALIZED "/", 0)); // starts with
}

TEST_F(AppRpcClientTest, firstPostUsesDefaultStyleTr5)
{
    api_compat::set_default_style(api_compat::Style::Tr5);

    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(200, response.http_status);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.front().find(R"("jsonrpc":"2.0")")) << bodies.front();
    EXPECT_NE(std::string::npos, bodies.front().find(R"("method":"session_get")")) << bodies.front();
}

TEST_F(AppRpcClientTest, switchesToTr5AfterA409ThatAdvertisesTheVersion)
{
    api_compat::set_default_style(api_compat::Style::Tr4);

    auto server = MockRpcServer{ /*advertise_tr5=*/true };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    // first request drives the 409 handshake; the server advertises Tr5 on it.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    // the subsequent request should now be serialized in Tr5.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.back().find(R"("jsonrpc":"2.0")")) << bodies.back();
    EXPECT_NE(std::string::npos, bodies.back().find(R"("method":"session_get")")) << bodies.back();
}

TEST_F(AppRpcClientTest, staysTr4AfterA409WithoutTheVersionHeader)
{
    api_compat::set_default_style(api_compat::Style::Tr4);

    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    // first request drives the 409 handshake; no version header is advertised.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    // the subsequent request should still be serialized in Tr4.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.back().find(R"("method":"session-get")")) << bodies.back();
    EXPECT_EQ(std::string::npos, bodies.back().find("jsonrpc")) << bodies.back();
}

TEST_F(AppRpcClientTest, reportsNetworkErrorWhenServerIsUnreachable)
{
    // bind a port, then shut the server down so connecting to it fails
    auto url_str = std::string{};
    {
        auto const server = MockRpcServer{ /*advertise_tr5=*/false };
        url_str = server.url();
    }

    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(url_str);

    auto network_error = std::optional<bool>{};
    auto network_response_message = std::string{};
    auto const tag = client.network_response.connect_scoped([&](bool const is_network_error, std::string_view const message) {
        network_error = is_network_error;
        network_response_message = std::string{ message };
    });

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_TRUE(response.network_error);
    EXPECT_FALSE(response.success);
    EXPECT_EQ(0, response.http_status);
    EXPECT_FALSE(std::empty(response.errmsg));

    EXPECT_TRUE(network_error.value_or(false));
    EXPECT_EQ(response.errmsg, network_response_message);
}

// A reply to a request sent before stop() reaches its caller only as a network error.
TEST_F(AppRpcClientTest, stopDropsRepliesToEarlierRequests)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    auto network_responses = 0;
    auto const tag = client.network_response.connect_scoped(
        [&network_responses](bool /*is_network_error*/, std::string_view /*message*/) { ++network_responses; });

    auto fut = exec_async(client, TR_KEY_session_get);
    client.stop();
    client.start(server.url());

    // The server answered with the 409 handshake, which the client must not resend.
    auto const stale = loop.pump_until_ready(fut);
    EXPECT_TRUE(stale.network_error);
    EXPECT_FALSE(stale.success);
    EXPECT_EQ(0, network_responses);
    EXPECT_EQ(1U, std::size(server.request_bodies()));

    // The stale 409 didn't hand its session id to the restarted client,
    // so the next request repeats the handshake.
    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_TRUE(response.success) << response.errmsg;
    EXPECT_EQ(3U, std::size(server.request_bodies()));
}

// A reply to a request sent before stop() doesn't set the restarted client's RPC style.
TEST_F(AppRpcClientTest, stopDropsTheRpcStyleOfEarlierReplies)
{
    api_compat::set_default_style(api_compat::Style::Tr4);

    auto tr5_server = MockRpcServer{ /*advertise_tr5=*/true };
    auto tr4_server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(tr5_server.url());

    // The Tr5 server answers with a 409 that advertises its RPC version.
    auto fut = exec_async(client, TR_KEY_session_get);
    client.stop();
    client.start(tr4_server.url());
    EXPECT_TRUE(loop.pump_until_ready(fut).network_error);

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_TRUE(response.success) << response.errmsg;
    auto const bodies = tr4_server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    for (auto const& body : bodies) {
        EXPECT_NE(std::string::npos, body.find(R"("method":"session-get")")) << body;
        EXPECT_EQ(std::string::npos, body.find("jsonrpc")) << body;
    }
}

TEST_F(AppRpcClientTest, signalsAuthRequiredOn401)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/false, /*require_auth=*/true };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    auto auth_signals = 0;
    auto const tag = client.auth_required.connect_scoped([&auth_signals]() { ++auth_signals; });

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(401, response.http_status);
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.network_error);
    EXPECT_EQ(1, auth_signals);
}

TEST_F(AppRpcClientTest, authenticatesWithSuppliedCredentials)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/false, /*require_auth=*/true };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url(), "alice", "sekrit");

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(200, response.http_status);
    EXPECT_TRUE(response.success) << response.errmsg;
    EXPECT_TRUE(server.saw_authorization());
}

TEST_F(AppRpcClientTest, reportsAnUnparseableBodyAsAnError)
{
    auto server = MockRpcServer{ /*advertise_tr5=*/false, /*require_auth=*/false, "this is not json" };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(server.url());

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(200, response.http_status);
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.network_error);
    EXPECT_FALSE(std::empty(response.errmsg));
}

// exec against a real tr_session, exercising the tr_rpc_request_exec dispatch
using AppRpcClientSessionTest = EmbeddedSessionTest;

TEST_F(AppRpcClientSessionTest, execParsesSuccessResponse)
{
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(core_);

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_TRUE(response.success);
    EXPECT_FALSE(response.network_error);
    EXPECT_EQ(200, response.http_status);
    EXPECT_TRUE(std::empty(response.errmsg));

    auto const* const args = response.args_map();
    ASSERT_NE(nullptr, args);
    EXPECT_TRUE(args->contains(TR_KEY_version));
}

TEST_F(AppRpcClientSessionTest, stopDropsRepliesToEarlierRequests)
{
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(core_);

    auto fut = exec_async(client, TR_KEY_session_get);
    client.stop();
    client.start(core_);

    auto const response = loop.pump_until_ready(fut);
    EXPECT_TRUE(response.network_error);
    EXPECT_FALSE(response.success);
    EXPECT_EQ(nullptr, response.args);
}

TEST_F(AppRpcClientSessionTest, execReportsAnUnknownMethodAsAnError)
{
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(core_);

    // any quark that isn't an RPC method name will do
    auto const response = exec_and_wait(client, loop, TR_KEY_compact_view);
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.network_error);
    EXPECT_FALSE(std::empty(response.errmsg));
}

} // namespace
