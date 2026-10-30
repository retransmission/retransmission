// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#ifndef _WIN32
#include <csignal>
#endif
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <fmt/format.h>

#include <libtransmission/net.h> // sockaddr_in, getsockname(), ntohs()
#include <libtransmission/string-utils.h> // tr_strlower()

namespace tr::test
{

// A minimal in-process HTTP server bound to an ephemeral loopback port.
// It records what each request contained and lets a per-test handler shape
// the reply. Everything stays on loopback, so there's no external network
// dependency and nothing to flake in CI.
class LoopbackServer
{
public:
    // What the server saw for the most recent request.
    struct Request {
        std::string method;
        std::string uri;
        std::string body;
        std::map<std::string, std::string, std::less<>> headers; // names lowercased
        std::string peer_address;
        uint16_t peer_port = 0;
    };

    // Handler that produces the reply. If none is set, the server replies 200.
    using Handler = std::function<void(evhttp_request*)>;

    [[nodiscard]] static std::unique_ptr<LoopbackServer> create(tr_address_type const type = TR_AF_INET)
    {
        if (type != TR_AF_INET && type != TR_AF_INET6) {
            throw std::invalid_argument{ "Unsupported loopback server address type" };
        }

        try {
            return std::unique_ptr<LoopbackServer>{ new LoopbackServer{ type } };
        } catch (std::system_error const& error) {
            std::cerr << error.what() << '\n';
            return {};
        }
    }

    ~LoopbackServer()
    {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    LoopbackServer(LoopbackServer const&) = delete;
    LoopbackServer(LoopbackServer&&) = delete;
    LoopbackServer& operator=(LoopbackServer const&) = delete;
    LoopbackServer& operator=(LoopbackServer&&) = delete;

    [[nodiscard]] std::string url(std::string_view path = "/") const
    {
        return fmt::format("http://{:s}:{:d}{:s}", host_, port_, path);
    }

    void setHandler(Handler handler)
    {
        auto const lock = std::scoped_lock{ mutex_ };
        handler_ = std::move(handler);
    }

    [[nodiscard]] Request lastRequest() const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return request_;
    }

    // Convenience: send a reply with the given status, reason, body, and headers.
    static void reply(
        evhttp_request* const req,
        int const code,
        char const* const reason,
        std::string_view const body,
        std::span<std::pair<std::string, std::string> const> headers = {})
    {
        auto* const out = evbuffer_new();
        evbuffer_add(out, std::data(body), std::size(body));
        if (!headers.empty()) {
            auto* const output_headers = evhttp_request_get_output_headers(req);
            for (auto const& [key, value] : headers) {
                evhttp_add_header(output_headers, key.c_str(), value.c_str());
            }
        }
        evhttp_send_reply(req, code, reason, out);
        evbuffer_free(out);
    }

private:
    explicit LoopbackServer(tr_address_type const type)
        : host_{ type == TR_AF_INET ? "127.0.0.1" : "[::1]" }
    {
#ifndef _WIN32
        // The client may close a response-limited connection while libevent is still writing.
        (void)signal(SIGPIPE, SIG_IGN);
#endif

        evhttp_set_allowed_methods(
            http_.get(),
            EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_HEAD | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_OPTIONS);

        auto* const bound = evhttp_bind_socket_with_handle(http_.get(), type == TR_AF_INET ? "127.0.0.1" : "::1", 0);
        if (bound == nullptr) {
            throw std::system_error{
                sockerrno,
                std::system_category(),
                fmt::format("Could not bind loopback server to {}", host_),
            };
        }
        auto ss = sockaddr_storage{};
        auto sslen = socklen_t{ sizeof(ss) };
        if (getsockname(evhttp_bound_socket_get_fd(bound), reinterpret_cast<sockaddr*>(&ss), &sslen) != 0) {
            throw std::system_error{ sockerrno, std::system_category(), "Could not inspect loopback server socket" };
        }
        port_ = ntohs(
            ss.ss_family == AF_INET ? reinterpret_cast<sockaddr_in const*>(&ss)->sin_port :
                                      reinterpret_cast<sockaddr_in6 const*>(&ss)->sin6_port);

        evhttp_set_gencb(http_.get(), &LoopbackServer::onRequest, this);

        thread_ = std::thread{ [this] {
            // Poll instead of a blocking dispatch so teardown doesn't depend
            // on libevent being built with cross-thread wakeup support.
            using namespace std::chrono_literals;
            while (!stop_.load(std::memory_order_acquire)) {
                event_base_loop(base_.get(), EVLOOP_NONBLOCK);
                std::this_thread::sleep_for(1ms);
            }
        } };
    }
    static void onRequest(evhttp_request* req, void* vself)
    {
        static_cast<LoopbackServer*>(vself)->handle(req);
    }

    void handle(evhttp_request* req)
    {
        auto request = Request{};
        request.method = methodName(evhttp_request_get_command(req));
        request.uri = evhttp_request_get_uri(req);
#if LIBEVENT_VERSION_NUMBER >= 0x02020001
        char const* peer_address = nullptr;
#else
        char* peer_address = nullptr;
#endif
        evhttp_connection_get_peer(evhttp_request_get_connection(req), &peer_address, &request.peer_port);
        request.peer_address = peer_address;

        if (auto* const in = evhttp_request_get_input_buffer(req); in != nullptr) {
            if (auto const len = evbuffer_get_length(in); len > 0U) {
                request.body.assign(reinterpret_cast<char const*>(evbuffer_pullup(in, -1)), len);
            }
        }

        for (auto const* h = evhttp_request_get_input_headers(req)->tqh_first; h != nullptr; h = h->next.tqe_next) {
            request.headers.insert_or_assign(tr_strlower(h->key), std::string{ h->value });
        }

        auto handler = Handler{};
        {
            auto const lock = std::scoped_lock{ mutex_ };
            request_ = std::move(request);
            handler = handler_;
        }

        if (handler) {
            handler(req);
        } else {
            reply(req, HTTP_OK, "OK", std::string_view{});
        }
    }

    [[nodiscard]] static char const* methodName(evhttp_cmd_type cmd)
    {
        switch (cmd) {
        case EVHTTP_REQ_GET:
            return "GET";
        case EVHTTP_REQ_POST:
            return "POST";
        case EVHTTP_REQ_HEAD:
            return "HEAD";
        case EVHTTP_REQ_PUT:
            return "PUT";
        case EVHTTP_REQ_DELETE:
            return "DELETE";
        case EVHTTP_REQ_OPTIONS:
            return "OPTIONS";
        default:
            return "OTHER";
        }
    }

    std::unique_ptr<event_base, decltype(&event_base_free)> base_{ event_base_new(), &event_base_free };
    std::unique_ptr<evhttp, decltype(&evhttp_free)> http_{ evhttp_new(base_.get()), &evhttp_free };
    std::string_view const host_;
    uint16_t port_ = 0;

    std::thread thread_;
    std::atomic<bool> stop_ = false;

    mutable std::mutex mutex_;
    Handler handler_;
    Request request_;
};

} // namespace tr::test
