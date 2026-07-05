// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#define __TRANSMISSION__ // for libtransmission/net.h
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <libtransmission/constants.h> // TrRpcSessionIdHeader
#include <libtransmission/net.h> // sockaddr_storage, ntohs()

// A minimal in-process stand-in for transmission-daemon's RPC endpoint. It does
// just enough to exercise Session's remote transport end to end: the CSRF
// handshake (reply 409 with an X-Transmission-Session-Id that the client must
// echo back) followed by a valid, empty success response. Every request body is
// recorded so tests can assert on the wire format. It never advertises an RPC
// version, so the client keeps whatever api_compat style it started with.
// Everything stays on 127.0.0.1, so there's no external dependency to flake.
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
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
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

    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] std::vector<std::string> request_bodies() const
    {
        auto const lock = std::lock_guard{ mutex_ };
        return bodies_;
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

        record_request_body(req);

        auto* const out_headers = evhttp_request_get_output_headers(req);
        auto* const out = evbuffer_new();

        if (session_id == nullptr) {
            // CSRF handshake: reject and hand back a session id to retry with.
            evhttp_add_header(out_headers, std::data(TrRpcSessionIdHeader), std::string{ SessionId }.c_str());
            evhttp_send_reply(req, 409, "Conflict", out);
        } else {
            evhttp_add_header(out_headers, "Content-Type", "application/json");
            static auto constexpr Body = std::string_view{ R"({"result":{}})" };
            evbuffer_add(out, std::data(Body), std::size(Body));
            evhttp_send_reply(req, 200, "OK", out);
        }

        evbuffer_free(out);
    }

    void record_request_body(evhttp_request* req)
    {
        auto* const in_buf = evhttp_request_get_input_buffer(req);
        auto const len = evbuffer_get_length(in_buf);
        auto body = std::string{};
        if (len > 0) {
            body.assign(reinterpret_cast<char const*>(evbuffer_pullup(in_buf, -1)), len);
        }

        auto const lock = std::lock_guard{ mutex_ };
        bodies_.push_back(std::move(body));
    }

    static constexpr auto SessionId = std::string_view{ "test-session-id" };

    mutable std::mutex mutex_;
    std::vector<std::string> bodies_;

    event_base* base_;
    evhttp* http_;
    std::uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_ = false;
};
