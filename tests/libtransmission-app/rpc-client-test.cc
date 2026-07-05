// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#define __TRANSMISSION__ // for libtransmission/net.h

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <fmt/format.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/constants.h> // TrRpcSessionIdHeader, TrRpcVersionHeader
#include <libtransmission/net.h> // sockaddr_storage, ntohs()
#include <libtransmission/quark.h>
#include <libtransmission/rpcimpl.h> // TrRpcVersionSemver
#include <libtransmission/utils.h> // tr_lib_init()

#include <libtransmission-app/rpc-client.h>

#include <gtest/gtest.h>

namespace api_compat = tr::api_compat;
using tr::app::RpcClient;
using tr::app::RpcResponse;
using namespace std::literals;

namespace
{

auto constexpr SessionId = "test-session-id"sv;

// A minimal in-process stand-in for transmission-daemon's RPC endpoint. It does
// just enough to exercise tr::app::RpcClient's remote transport end to end: the
// CSRF handshake (reply 409 with an X-Transmission-Session-Id that the client
// must echo back), optionally advertising the Tr5 RPC version, followed by a
// valid success response. It records every request body so tests can assert on
// the wire format. Everything stays on 127.0.0.1, so there's nothing to flake.
class MockRpcServer
{
public:
    explicit MockRpcServer(bool advertise_tr5)
        : advertise_tr5_{ advertise_tr5 }
        , base_{ event_base_new() }
        , http_{ evhttp_new(base_) }
    {
        evhttp_set_allowed_methods(http_, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD);

        auto* const handle = evhttp_bind_socket_with_handle(http_, "127.0.0.1", 0);
        auto ss = sockaddr_storage{};
        auto sslen = socklen_t{ sizeof(ss) };
        getsockname(evhttp_bound_socket_get_fd(handle), reinterpret_cast<sockaddr*>(&ss), &sslen);
        port_ = ntohs(reinterpret_cast<sockaddr_in const*>(&ss)->sin_port);

        evhttp_set_gencb(http_, &MockRpcServer::on_request, this);

        thread_ = std::thread{ [this]() {
            // Poll instead of a blocking dispatch so teardown doesn't depend on
            // libevent being built with cross-thread wakeup support.
            while (!stop_.load(std::memory_order_acquire)) {
                event_base_loop(base_, EVLOOP_NONBLOCK);
                std::this_thread::sleep_for(1ms);
            }
        } };
    }

    ~MockRpcServer()
    {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
        evhttp_free(http_);
        event_base_free(base_);
    }

    MockRpcServer(MockRpcServer const&) = delete;
    MockRpcServer& operator=(MockRpcServer const&) = delete;

    [[nodiscard]] uint16_t port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] std::vector<std::string> request_bodies() const
    {
        auto const lock = std::lock_guard{ mutex_ };
        return bodies_;
    }

    [[nodiscard]] std::string last_content_type() const
    {
        auto const lock = std::lock_guard{ mutex_ };
        return content_type_;
    }

    [[nodiscard]] std::string last_user_agent() const
    {
        auto const lock = std::lock_guard{ mutex_ };
        return user_agent_;
    }

private:
    static void on_request(evhttp_request* req, void* vself)
    {
        static_cast<MockRpcServer*>(vself)->handle(req);
    }

    void handle(evhttp_request* req)
    {
        auto* const in_headers = evhttp_request_get_input_headers(req);
        auto const* const session_id = evhttp_find_header(in_headers, std::data(TrRpcSessionIdHeader));

        record_request(req, in_headers);

        auto* const out_headers = evhttp_request_get_output_headers(req);
        auto* const out = evbuffer_new();

        if (session_id == nullptr) {
            // CSRF handshake: reject and hand back a session id to retry with.
            evhttp_add_header(out_headers, std::data(TrRpcSessionIdHeader), std::string{ SessionId }.c_str());
            if (advertise_tr5_) {
                evhttp_add_header(out_headers, std::data(TrRpcVersionHeader), std::data(TrRpcVersionSemver));
            }
            evhttp_send_reply(req, 409, "Conflict", out);
        } else {
            evhttp_add_header(out_headers, "Content-Type", "application/json");
            static auto constexpr Body = R"({"result":{}})"sv;
            evbuffer_add(out, std::data(Body), std::size(Body));
            evhttp_send_reply(req, 200, "OK", out);
        }

        evbuffer_free(out);
    }

    void record_request(evhttp_request* req, evkeyvalq* in_headers)
    {
        auto* const in_buf = evhttp_request_get_input_buffer(req);
        auto const len = evbuffer_get_length(in_buf);
        auto body = std::string{};
        if (len > 0) {
            body.assign(reinterpret_cast<char const*>(evbuffer_pullup(in_buf, -1)), len);
        }

        auto const* const content_type = evhttp_find_header(in_headers, "Content-Type");
        auto const* const user_agent = evhttp_find_header(in_headers, "User-Agent");

        auto const lock = std::lock_guard{ mutex_ };
        bodies_.push_back(std::move(body));
        if (content_type != nullptr) {
            content_type_ = content_type;
        }
        if (user_agent != nullptr) {
            user_agent_ = user_agent;
        }
    }

    bool advertise_tr5_;

    mutable std::mutex mutex_;
    std::vector<std::string> bodies_;
    std::string content_type_;
    std::string user_agent_;

    event_base* base_;
    evhttp* http_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_ = false;
};

// Stands in for the toolkit's UI thread. RpcClient marshals every response and
// signal through this hook; the test drives it like an event loop from the main
// thread, so continuations (including the 409 resend) run where a real toolkit
// would run them.
class UiLoop
{
public:
    [[nodiscard]] RpcClient::UiThreadFunc marshaler()
    {
        return [this](std::function<void()> fn) {
            auto const lock = std::lock_guard{ mutex_ };
            queue_.push_back(std::move(fn));
            cv_.notify_one();
        };
    }

    // Pump queued UI work until `fut` is ready, then return its value.
    template<typename T>
    [[nodiscard]] T pump_until_ready(std::future<T>& fut)
    {
        auto const deadline = std::chrono::steady_clock::now() + 10s;
        for (;;) {
            auto fn = std::function<void()>{};
            {
                auto lock = std::unique_lock{ mutex_ };
                cv_.wait_for(lock, 10ms, [this] { return !queue_.empty(); });
                if (!queue_.empty()) {
                    fn = std::move(queue_.front());
                    queue_.pop_front();
                }
            }

            if (fn) {
                fn();
            }

            if (fut.wait_for(0s) == std::future_status::ready) {
                return fut.get();
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error{ "timed out waiting for RPC response" };
            }
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
};

[[nodiscard]] RpcResponse exec_and_wait(RpcClient& client, UiLoop& loop, tr_quark const method)
{
    auto promise = std::make_shared<std::promise<RpcResponse>>();
    auto fut = promise->get_future();
    client.exec(method, static_cast<tr_variant*>(nullptr), [promise](RpcResponse response) {
        promise->set_value(std::move(response));
    });
    return loop.pump_until_ready(fut);
}

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

    [[nodiscard]] static std::string url(MockRpcServer const& server)
    {
        return fmt::format("http://127.0.0.1:{:d}/transmission/rpc", server.port());
    }

private:
    api_compat::Style saved_style_ = api_compat::Style::Tr4;
};

TEST_F(AppRpcClientTest, firstPostUsesDefaultStyleTr4)
{
    api_compat::set_default_style(api_compat::Style::Tr4);

    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(url(server));

    auto const response = exec_and_wait(client, loop, TR_KEY_session_get);
    EXPECT_EQ(200, response.http_status);
    EXPECT_FALSE(response.network_error);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.front().find(R"("method":"session-get")")) << bodies.front();
    EXPECT_EQ(std::string::npos, bodies.front().find("jsonrpc")) << bodies.front();

    // headers look right
    EXPECT_NE(std::string::npos, server.last_content_type().find("application/json"));
    EXPECT_EQ(0U, server.last_user_agent().rfind("Transmission/", 0)); // starts with
}

TEST_F(AppRpcClientTest, firstPostUsesDefaultStyleTr5)
{
    api_compat::set_default_style(api_compat::Style::Tr5);

    auto server = MockRpcServer{ /*advertise_tr5=*/false };
    auto loop = UiLoop{};
    auto client = RpcClient{ loop.marshaler() };
    client.start(url(server));

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
    client.start(url(server));

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
    client.start(url(server));

    // first request drives the 409 handshake; no version header is advertised.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    // the subsequent request should still be serialized in Tr4.
    EXPECT_EQ(200, exec_and_wait(client, loop, TR_KEY_session_get).http_status);

    auto const bodies = server.request_bodies();
    ASSERT_FALSE(std::empty(bodies));
    EXPECT_NE(std::string::npos, bodies.back().find(R"("method":"session-get")")) << bodies.back();
    EXPECT_EQ(std::string::npos, bodies.back().find("jsonrpc")) << bodies.back();
}

} // namespace
