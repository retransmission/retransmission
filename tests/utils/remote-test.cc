// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <sys/wait.h> // WIFEXITED(), WEXITSTATUS()
#endif

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <fmt/format.h>

#include <libtransmission/macros.h>
#include <libtransmission/net.h> // sockaddr_storage, ntohs()
#include <libtransmission/utils.h> // tr_lib_init()

#include <gtest/gtest.h>

using namespace std::literals;

namespace
{

auto constexpr SessionId = "test-session-id"sv;

// A minimal in-process stand-in for transmission-daemon's RPC endpoint. It
// mimics just enough of the daemon to exercise transmission-remote end to end:
// the CSRF handshake (reply 409 with an TR_PROJ_RPC_SESSION_ID_HEADER the
// client must echo back) followed by a valid, empty success response.
// Everything stays on 127.0.0.1, so there's no external dependency.
class MockRpcServer
{
public:
    MockRpcServer()
        : base_{ event_base_new() }
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

    [[nodiscard]] int request_count() const noexcept
    {
        return request_count_.load();
    }

    [[nodiscard]] bool authenticated_request_seen() const noexcept
    {
        return authenticated_request_seen_.load();
    }

private:
    static void on_request(evhttp_request* req, void* vself)
    {
        static_cast<MockRpcServer*>(vself)->handle(req);
    }

    void handle(evhttp_request* req)
    {
        request_count_.fetch_add(1);

        auto* const in_headers = evhttp_request_get_input_headers(req);
        auto const* const session_id = evhttp_find_header(in_headers, TR_PROJ_RPC_SESSION_ID_HEADER);
        auto* const out_headers = evhttp_request_get_output_headers(req);
        auto* const out = evbuffer_new();

        if (session_id == nullptr) {
            // CSRF handshake: reject and hand back a session id to retry with.
            evhttp_add_header(out_headers, TR_PROJ_RPC_SESSION_ID_HEADER, std::string{ SessionId }.c_str());
            evhttp_send_reply(req, 409, "Conflict", out);
        } else {
            authenticated_request_seen_.store(std::string_view{ session_id } == SessionId);
            evhttp_add_header(out_headers, "Content-Type", "application/json");
            static auto constexpr Body = R"({"result":"success","arguments":{}})"sv;
            evbuffer_add(out, std::data(Body), std::size(Body));
            evhttp_send_reply(req, 200, "OK", out);
        }

        evbuffer_free(out);
    }

    event_base* base_;
    evhttp* http_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_ = false;
    std::atomic<int> request_count_ = 0;
    std::atomic<bool> authenticated_request_seen_ = false;
};

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

// Run a command, capturing its combined stdout+stderr and exit code.
CommandResult run(std::string const& command)
{
#ifdef _WIN32
    auto* const pipe = _popen(command.c_str(), "r");
#else
    auto* const pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return {};
    }

    auto output = std::string{};
    auto buffer = std::array<char, 4096>{};
    while (std::fgets(std::data(buffer), std::size(buffer), pipe) != nullptr) {
        output += std::data(buffer);
    }

#ifdef _WIN32
    return { _pclose(pipe), output };
#else
    auto const rc = pclose(pipe);
    return { WIFEXITED(rc) ? WEXITSTATUS(rc) : -1, output };
#endif
}

TEST(RemoteLoopback, performsSessionHandshakeAndSucceeds)
{
    tr_lib_init(); // start up Winsock on Windows before the mock server binds

    auto server = MockRpcServer{};

    auto const command = fmt::format(R"("{:s}" 127.0.0.1:{:d} -l 2>&1)", TR_REMOTE_EXE, server.port());
    auto const result = run(command);

    EXPECT_EQ(0, result.exit_code) << result.output;
    EXPECT_EQ(2, server.request_count()) << "expected a 409 handshake followed by an authenticated retry";
    EXPECT_TRUE(server.authenticated_request_seen());
}

} // namespace
