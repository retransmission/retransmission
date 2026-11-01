// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

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

#include <libtransmission/constants.h> // TrRpcSessionIdHeader, TrRpcVersionHeader
#include <libtransmission/net.h> // sockaddr_storage, ntohs()
#include <libtransmission/rpcimpl.h> // TrRpcVersionSemver

auto constexpr SessionId = std::string_view{ "test-session-id" };

// A minimal in-process stand-in for transmission-daemon's RPC endpoint. It does
// just enough to exercise tr::app::RpcClient's remote transport end to end: the
// CSRF handshake (reply 409 with an TR_PROJ_SHARED_RPC_SESSION_ID_HEADER that the
// client must echo back), optionally advertising the Tr5 RPC version, followed
// by a valid success response. It records every request body so tests can assert
// on the wire format. Everything stays on 127.0.0.1, so there's nothing to flake.
class MockRpcServer
{
public:
    explicit MockRpcServer(bool advertise_tr5, bool require_auth = false, std::string body_override = {})
        : advertise_tr5_{ advertise_tr5 }
        , require_auth_{ require_auth }
        , body_override_{ std::move(body_override) }
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

    [[nodiscard]] uint16_t port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] std::vector<std::string> request_bodies() const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return bodies_;
    }

    [[nodiscard]] std::string last_content_type() const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return content_type_;
    }

    [[nodiscard]] std::string last_user_agent() const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return user_agent_;
    }

    [[nodiscard]] bool saw_authorization() const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return saw_authorization_;
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

        if (require_auth_ && evhttp_find_header(in_headers, "Authorization") == nullptr) {
            // challenge with Basic so libcurl knows how to retry with credentials
            evhttp_add_header(out_headers, "WWW-Authenticate", "Basic realm=\"transmission\"");
            evhttp_send_reply(req, 401, "Unauthorized", out);
        } else if (session_id == nullptr) {
            // CSRF handshake: reject and hand back a session id to retry with.
            evhttp_add_header(out_headers, std::data(TrRpcSessionIdHeader), std::string{ SessionId }.c_str());
            if (advertise_tr5_) {
                evhttp_add_header(out_headers, std::data(TrRpcVersionHeader), std::data(TrRpcVersionSemver));
            }
            evhttp_send_reply(req, 409, "Conflict", out);
        } else {
            evhttp_add_header(out_headers, "Content-Type", "application/json");
            static auto constexpr Body = std::string_view{ R"({"jsonrpc":"2.0","id":1,"result":{}})" };
            auto const& body = std::empty(body_override_) ? std::string{ Body } : body_override_;
            evbuffer_add(out, std::data(body), std::size(body));
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
        auto const* const authorization = evhttp_find_header(in_headers, "Authorization");

        auto const lock = std::scoped_lock{ mutex_ };
        bodies_.push_back(std::move(body));
        if (content_type != nullptr) {
            content_type_ = content_type;
        }
        if (user_agent != nullptr) {
            user_agent_ = user_agent;
        }
        if (authorization != nullptr) {
            saw_authorization_ = true;
        }
    }

    bool advertise_tr5_;
    bool require_auth_;
    std::string body_override_;

    mutable std::mutex mutex_;
    bool saw_authorization_ = false;
    std::vector<std::string> bodies_;
    std::string content_type_;
    std::string user_agent_;

    event_base* base_;
    evhttp* http_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_ = false;
};
