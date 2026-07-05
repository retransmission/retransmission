// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sigslot/signal.hpp>

#include <libtransmission/api-compat.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>
#include <libtransmission/web.h>

struct tr_session;

namespace tr::app
{

struct RpcResponse {
    std::string errmsg;
    std::shared_ptr<tr_variant> args;
    bool success = false;
    bool network_error = false; // true if no valid HTTP response
    long http_status = 0; // HTTP status, or 0 for no response, or 200 for local
};

// RPC client which speaks either to in-process session (tr_rpc_request_exec)
// or to a remote (tr_web). Every response callback and signal is delivered
// on the UI thread by way of the injected `run_on_ui_thread` hook,
// so callers never see the libtransmission or curl worker threads.
class RpcClient
{
public:
    // Invoked with the response of a single request, always on the UI thread.
    using ResponseFunc = std::function<void(RpcResponse)>;

    // Marshals a function onto the UI thread. Toolkits inject their own:
    // Qt -> QMetaObject::invokeMethod(..., Qt::QueuedConnection),
    // GTK -> Glib::signal_idle().connect_once(...).
    using UiThreadFunc = std::function<void(std::function<void()>)>;

    explicit RpcClient(UiThreadFunc run_on_ui_thread);
    ~RpcClient();
    RpcClient(RpcClient&&) = delete;
    RpcClient(RpcClient const&) = delete;
    RpcClient& operator=(RpcClient&&) = delete;
    RpcClient& operator=(RpcClient const&) = delete;

    // Use an in-process session
    void start(tr_session* session);

    // Use the remote server at `url` (scheme://host:port/path).
    void start(std::string url, std::optional<std::string> username = {}, std::optional<std::string> password = {});

    void stop();

    [[nodiscard]] bool is_local() const noexcept
    {
        return session_ != nullptr || url_is_loopback_;
    }

    [[nodiscard]] std::string const& url() const noexcept
    {
        return url_;
    }

    // Issue a request. `on_done` runs on the UI thread when it completes.
    void exec(tr_quark method, tr_variant::Map args, ResponseFunc on_done);
    void exec(tr_quark method, tr_variant* args, ResponseFunc on_done);

    [[nodiscard]] sigslot::scoped_connection observe_network_response(
        std::function<void(long status, std::string const& message)> observer) const
    {
        return network_response_.connect_scoped(std::move(observer));
    }

    [[nodiscard]] sigslot::scoped_connection observe_auth_required(std::function<void()> observer) const
    {
        return auth_required_.connect_scoped(std::move(observer));
    }

    [[nodiscard]] sigslot::scoped_connection observe_data_read_progress(std::function<void()> observer) const
    {
        return data_read_progress_.connect_scoped(std::move(observer));
    }

    [[nodiscard]] sigslot::scoped_connection observe_data_send_progress(std::function<void()> observer) const
    {
        return data_send_progress_.connect_scoped(std::move(observer));
    }

private:
    void send_local_request(tr_variant&& req, ResponseFunc on_done);
    void send_remote_request(std::string body, ResponseFunc on_done);

    [[nodiscard]] tr_web& web();

    [[nodiscard]] static RpcResponse parse_response_data(tr_variant& response);

    tr::api_compat::Style network_style_ = tr::api_compat::default_style();
    tr_session* session_ = nullptr;
    std::string session_id_;
    std::string url_;
    std::optional<std::string> username_;
    std::optional<std::string> password_;
    bool url_is_loopback_ = false;
    bool const verbose_;

    UiThreadFunc run_on_ui_thread_;

    mutable sigslot::signal<long, std::string const&> network_response_;
    mutable sigslot::signal<> auth_required_;
    mutable sigslot::signal<> data_read_progress_;
    mutable sigslot::signal<> data_send_progress_;

    // tr_web and its mediator are declared after the callbacks so they're
    // destroyed first, stopping the curl thread before the callbacks.
    tr_web::Mediator web_mediator_;
    std::unique_ptr<tr_web> web_;
};

} // namespace tr::app
