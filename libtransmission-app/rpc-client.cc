// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/rpc-client.h"

#include <cstdint> // int64_t
#include <cstdlib> // getenv
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <libtransmission/constants.h> // TrRpcSessionIdHeader, TrRpcVersionHeader
#include <libtransmission/rpcimpl.h> // JsonRpc::Version, tr_rpc_request_exec
#include <libtransmission/variant.h>
#include <libtransmission/version.h> // SHORT_VERSION_STRING
#include <libtransmission/web-utils.h> // tr_urlParse

namespace api_compat = tr::api_compat;

namespace tr::app
{
namespace
{

[[nodiscard]] int64_t next_id()
{
    static int64_t id = {};
    return id++;
}

[[nodiscard]] tr_variant build_request(tr_quark const method, tr_variant::Map params)
{
    auto req = tr_variant::Map{ 4U };
    req.try_emplace(TR_KEY_jsonrpc, tr_variant::unmanaged_string(JsonRpc::Version));
    req.try_emplace(TR_KEY_method, tr_variant::unmanaged_string(method));
    req.try_emplace(TR_KEY_id, next_id());
    if (!std::empty(params)) {
        req.try_emplace(TR_KEY_params, std::move(params));
    }

    return tr_variant{ std::move(req) };
}

} // namespace

RpcClient::RpcClient(UiThreadFunc run_on_ui_thread)
    : verbose_{ ::getenv("TR_RPC_VERBOSE") != nullptr }
    , run_on_ui_thread_{ std::move(run_on_ui_thread) }
{
}

RpcClient::~RpcClient() = default;

void RpcClient::start(tr_session* session)
{
    session_ = session;
}

void RpcClient::start(std::string url, std::optional<std::string> username, std::optional<std::string> password)
{
    url_ = std::move(url);
    username_ = std::move(username);
    password_ = std::move(password);

    url_is_loopback_ = false;
    if (auto const parsed = tr_urlParse(url_); parsed) {
        url_is_loopback_ = parsed->host == "localhost" || parsed->host == "127.0.0.1" || parsed->host == "::1";
    }
}

void RpcClient::stop()
{
    session_ = nullptr;
    session_id_.clear();
    url_.clear();
    username_.reset();
    password_.reset();
    url_is_loopback_ = false;
    network_style_ = api_compat::default_style();
}

tr_web& RpcClient::web()
{
    if (!web_) {
        web_ = tr_web::create(web_mediator_);
    }

    return *web_;
}

void RpcClient::exec(tr_quark const method, tr_variant::Map args, ResponseFunc on_done)
{
    auto req = build_request(method, std::move(args));

    if (session_ != nullptr) {
        send_local_request(std::move(req), std::move(on_done));
    } else if (!std::empty(url_)) {
        api_compat::convert(req, network_style_);
        auto body = tr_variant_serde::json().compact().to_string(req);
        send_remote_request(std::move(body), std::move(on_done));
    }
}

void RpcClient::exec(tr_quark const method, tr_variant* args, ResponseFunc on_done)
{
    if (args != nullptr) {
        if (auto* const args_map = args->get_if<tr_variant::Map>()) {
            exec(method, args_map->clone(), std::move(on_done));
            return;
        }
    }

    exec(method, tr_variant::Map{}, std::move(on_done));
}

void RpcClient::send_local_request(tr_variant&& req, ResponseFunc on_done)
{
    if (verbose_) {
        fmt::print("{:s}:{:d} sending req:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(req));
    }

    tr_rpc_request_exec(session_, std::move(req), [this, on_done = std::move(on_done)](tr_variant&& response) mutable {
        api_compat::convert_incoming_data(response);

        // this callback runs on the libtransmission thread; do the parse
        // here and hop the result over to the UI thread.
        auto shared = std::make_shared<tr_variant>(std::move(response));
        run_on_ui_thread_([shared, on_done = std::move(on_done)]() mutable {
            auto result = parse_response_data(*shared);
            result.http_status = 200;
            if (on_done) {
                on_done(std::move(result));
            }
        });
    });
}

void RpcClient::send_remote_request(std::string body, ResponseFunc on_done)
{
    data_send_progress();

    auto options = tr_web::FetchOptions{
        url_,
        [this, body, on_done = std::move(on_done)](tr_web::FetchResponse const& response) mutable {
            // this runs on tr_web's worker thread. Parse here (per design), then
            // hop all shared-state handling and the callback to the UI thread.
            auto const status = response.status;

            auto new_session_id = std::optional<std::string>{};
            if (auto const id = response.header(TrRpcSessionIdHeader); id) {
                new_session_id = std::string{ *id };
            }

            auto const is_tr5 = response.header(TrRpcVersionHeader).has_value();

            auto parsed = std::shared_ptr<tr_variant>{};
            if (status == 200) {
                if (auto var = tr_variant_serde::json().parse(response.body); var) {
                    api_compat::convert_incoming_data(*var);
                    parsed = std::make_shared<tr_variant>(std::move(*var));
                }
            }

            run_on_ui_thread_(
                [this, body = std::move(body), on_done = std::move(on_done), new_session_id, is_tr5, status, parsed]() mutable {
                    if (new_session_id) {
                        session_id_ = *new_session_id;
                    }

                    if (is_tr5) {
                        network_style_ = api_compat::Style::Tr5;
                    }

                    if (status == 409 && new_session_id) {
                        // session id expired; we've captured the new one, so resend.
                        send_remote_request(std::move(body), std::move(on_done));
                        return;
                    }

                    data_read_progress();

                    if (status == 401) {
                        auth_required();
                    }

                    auto result = RpcResponse{};
                    result.http_status = status;

                    if (status == 0) {
                        result.network_error = true;
                        result.errmsg = "network error";
                        network_response(status, result.errmsg);
                    } else if (parsed) {
                        result = parse_response_data(*parsed);
                        result.http_status = status;
                        network_response(status, std::string{});
                    } else {
                        result.errmsg = fmt::format("unexpected response (HTTP {:d})", status);
                        network_response(status, result.errmsg);
                    }

                    if (on_done) {
                        on_done(std::move(result));
                    }
                });
        },
        nullptr,
    };

    options.body = body;
    options.auth_scheme = tr_web::FetchOptions::AuthScheme::Any;
    options.headers.insert_or_assign("Content-Type", "application/json; charset=UTF-8");
    options.headers.insert_or_assign("User-Agent", "Transmission/" SHORT_VERSION_STRING);

    if (!std::empty(session_id_)) {
        options.headers.insert_or_assign(std::string{ TrRpcSessionIdHeader }, session_id_);
    }

    if (username_) {
        options.username = *username_;
        options.password = password_;
    }

    if (verbose_) {
        fmt::print("{:s}:{:d} POST {:s}\n{:s}\n", __FILE__, __LINE__, url_, body);
    }

    web().fetch(std::move(options));
}

RpcResponse RpcClient::parse_response_data(tr_variant& response)
{
    auto ret = RpcResponse{};

    ret.success = false;
    ret.errmsg = "unknown error";

    if (auto* const response_map = response.get_if<tr_variant::Map>()) {
        if (auto* const result = response_map->find_if<tr_variant::Map>(TR_KEY_result)) {
            ret.success = true;
            ret.errmsg.clear();
            ret.args = std::make_shared<tr_variant>(std::move(*result));
        }

        if (auto* const error_map = response_map->find_if<tr_variant::Map>(TR_KEY_error)) {
            if (auto const errmsg = error_map->value_if<std::string_view>(TR_KEY_message); errmsg) {
                ret.errmsg = std::string{ *errmsg };
            }

            if (auto* const data = error_map->find_if<tr_variant::Map>(TR_KEY_data)) {
                if (auto const errstr = data->value_if<std::string_view>(TR_KEY_error_string); errstr) {
                    ret.errmsg = std::string{ *errstr };
                }

                if (auto* const result = data->find_if<tr_variant::Map>(TR_KEY_result)) {
                    ret.args = std::make_shared<tr_variant>(std::move(*result));
                }
            }
        }
    }

    return ret;
}

} // namespace tr::app
