// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <glibmm/init.h>
#include <glibmm/main.h>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>

#include <libtransmission-app/rpc-client.h>

#include "Utils.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

// An embedded session in a sandbox, with peer discovery and port forwarding off.
class GtkRpcClientTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        Glib::init();

        auto settings = tr_sessionGetDefaultSettings();
        for (auto const key : { TR_KEY_dht_enabled, TR_KEY_lpd_enabled, TR_KEY_port_forwarding_enabled, TR_KEY_utp_enabled }) {
            settings.insert_or_assign(key, false);
        }

        session_ = tr_sessionInit(sandbox_dir(), false, settings);
    }

    void TearDown() override
    {
        tr_sessionClose(session_, 0.5);
        session_ = nullptr;
    }

    tr_session* session_ = nullptr;
};

} // namespace

// The marshaler may be called from any thread, e.g. libtransmission's,
// and the function still runs on the thread that runs the main loop.
TEST(GtkRunOnUiThreadTest, runsFunctionsOnTheMainLoopThread)
{
    Glib::init();

    auto const loop = Glib::MainLoop::create();
    auto ran_on = std::optional<std::thread::id>{};

    std::thread{ [&loop, &ran_on] {
        gtr_run_on_ui_thread([&loop, &ran_on] {
            ran_on = std::this_thread::get_id();
            loop->quit();
        });
    } }.join();

    // fail instead of hanging if the function never runs
    auto timeout = Glib::signal_timeout().connect_seconds(
        [&loop] {
            loop->quit();
            return false;
        },
        10);

    // the posting thread is done, and the function still hasn't run: it needs the loop
    EXPECT_FALSE(ran_on.has_value());

    loop->run();
    timeout.disconnect();

    ASSERT_TRUE(ran_on.has_value());
    EXPECT_EQ(std::this_thread::get_id(), *ran_on);
}

// The client hands RpcClient gtr_run_on_ui_thread() so that replies reach their callbacks
// on the GTK thread: a reply waits in the main loop's idle queue until the loop runs it.
TEST_F(GtkRpcClientTest, deliversRepliesOnTheMainLoop)
{
    auto rpc = tr::app::RpcClient{ &gtr_run_on_ui_thread };
    rpc.start(session_);

    auto const loop = Glib::MainLoop::create();
    auto reply_thread = std::optional<std::thread::id>{};
    auto version = std::string{};

    auto fields = tr_variant::Vector{};
    fields.emplace_back(tr_variant::unmanaged_string(TR_KEY_version));
    auto params = tr_variant::Map{ 1U };
    params.try_emplace(TR_KEY_fields, std::move(fields));

    rpc.exec(TR_KEY_session_get, std::move(params), [&reply_thread, &version, &loop](tr::app::RpcResponse const& response) {
        reply_thread = std::this_thread::get_id();

        if (auto const* const result = response.args ? response.args->get_if<tr_variant::Map>() : nullptr; result != nullptr) {
            version = result->value_if<std::string_view>(TR_KEY_version).value_or(""sv);
        }

        loop->quit();
    });

    // fail instead of hanging if the reply never comes
    auto timeout = Glib::signal_timeout().connect_seconds(
        [&loop] {
            loop->quit();
            return false;
        },
        10);

    // Even once the session has answered, the reply can't run until the loop does.
    EXPECT_FALSE(reply_thread.has_value());

    loop->run();
    timeout.disconnect();

    ASSERT_TRUE(reply_thread.has_value());
    EXPECT_EQ(std::this_thread::get_id(), *reply_thread);
    EXPECT_EQ(LONG_VERSION_STRING, version);

    rpc.stop();
}
