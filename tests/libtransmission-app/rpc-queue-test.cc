// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/variant.h>

#include "libtransmission-app/rpc-client.h"
#include "libtransmission-app/rpc-queue.h"

namespace
{

using tr::app::RpcClient;
using tr::app::RpcQueue;
using tr::app::RpcResponse;

[[nodiscard]] RpcResponse ok_response()
{
    auto ret = RpcResponse{};
    ret.success = true;
    return ret;
}

[[nodiscard]] RpcResponse error_response(std::string errmsg, bool const network_error = false)
{
    auto ret = RpcResponse{};
    ret.success = false;
    ret.network_error = network_error;
    ret.errmsg = std::move(errmsg);
    return ret;
}

TEST(AppRpcQueueTest, runsStepsInOrderAndForwardsResponses)
{
    auto events = std::vector<std::string>{};

    RpcQueue::create()
        .add([&events](RpcClient::ResponseFunc done) {
            events.emplace_back("first");
            auto response = ok_response();
            response.args = std::make_shared<tr_variant>(int64_t{ 42 });
            done(std::move(response));
        })
        .add([&events](RpcResponse const& prev, RpcClient::ResponseFunc done) {
            events.emplace_back("second");
            EXPECT_TRUE(prev.success);
            ASSERT_NE(nullptr, prev.args);
            EXPECT_EQ(prev.args->value_if<int64_t>(), 42);
            done(ok_response());
        })
        .finally([&events]() { events.emplace_back("finally"); })
        .run();

    EXPECT_EQ((std::vector<std::string>{ "first", "second", "finally" }), events);
}

TEST(AppRpcQueueTest, acceptsAllFourStepShapes)
{
    auto events = std::vector<std::string>{};

    // an auxiliary step (no `done` parameter) reports success implicitly,
    // so the chain keeps running after it
    RpcQueue::create()
        .add([&events](RpcResponse const& /*prev*/, RpcClient::ResponseFunc done) {
            events.emplace_back("request-with-prev");
            done(ok_response());
        })
        .add([&events](RpcClient::ResponseFunc done) {
            events.emplace_back("request");
            done(ok_response());
        })
        .add([&events](RpcResponse const& /*prev*/) { events.emplace_back("aux-with-prev"); })
        .add([&events]() { events.emplace_back("aux"); })
        .finally([&events]() { events.emplace_back("finally"); })
        .run();

    EXPECT_EQ((std::vector<std::string>{ "request-with-prev", "request", "aux-with-prev", "aux", "finally" }), events);
}

TEST(AppRpcQueueTest, stepFailureRunsItsErrorHandlerAndStopsTheChain)
{
    auto events = std::vector<std::string>{};

    RpcQueue::create()
        .add(
            [&events](RpcClient::ResponseFunc done) {
                events.emplace_back("first");
                done(error_response("boom"));
            },
            [&events](RpcResponse const& response) { events.emplace_back("handler:" + response.errmsg); })
        .add([&events]() { events.emplace_back("second"); })
        .finally([&events]() { events.emplace_back("finally"); })
        .run();

    EXPECT_EQ((std::vector<std::string>{ "first", "handler:boom", "finally" }), events);
}

TEST(AppRpcQueueTest, errorHandlerMayTakeNoArguments)
{
    auto handled = false;

    RpcQueue::create()
        .add([](RpcClient::ResponseFunc done) { done(error_response("boom")); }, [&handled]() { handled = true; })
        .run();

    EXPECT_TRUE(handled);
}

// A network error can't be handled by a step's error handler,
// so it aborts the chain without calling one.
TEST(AppRpcQueueTest, networkErrorSkipsTheErrorHandler)
{
    auto events = std::vector<std::string>{};

    RpcQueue::create()
        .add(
            [&events](RpcClient::ResponseFunc done) {
                events.emplace_back("first");
                done(error_response("lost", /*network_error=*/true));
            },
            [&events](RpcResponse const& /*response*/) { events.emplace_back("handler"); })
        .add([&events]() { events.emplace_back("second"); })
        .finally([&events]() { events.emplace_back("finally"); })
        .run();

    EXPECT_EQ((std::vector<std::string>{ "first", "finally" }), events);
}

TEST(AppRpcQueueTest, finallyRunsOnAnEmptyQueue)
{
    auto ran = false;
    RpcQueue::create().finally([&ran]() { ran = true; }).run();
    EXPECT_TRUE(ran);
}

// The queue owns itself while running: a step may park its `done` continuation
// and invoke it after run() has returned, e.g. when an RPC response arrives.
TEST(AppRpcQueueTest, staysAliveUntilAParkedStepCompletes)
{
    auto parked = RpcClient::ResponseFunc{};
    auto events = std::vector<std::string>{};

    RpcQueue::create()
        .add([&parked](RpcClient::ResponseFunc done) { parked = std::move(done); })
        .add([&events]() { events.emplace_back("second"); })
        .finally([&events]() { events.emplace_back("finally"); })
        .run();

    EXPECT_TRUE(std::empty(events)); // parked: the chain is waiting on `done`

    parked(ok_response());
    EXPECT_EQ((std::vector<std::string>{ "second", "finally" }), events);
}

} // namespace
