// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <type_traits>
#include <utility>

#include "libtransmission-app/rpc-client.h"

namespace tr::app
{

// Executes RPC steps in order.
//
// A step is one of:
//   * request, with prior response: [](RpcResponse const&, RpcClient::ResponseFunc done){ rpc.exec(..., done); }
//   * request, first step:          [](RpcClient::ResponseFunc done){ rpc.exec(..., done); }
//   * auxiliary, with response:     [](RpcResponse const& r){ use(r); }
//   * auxiliary, no argument:       []{ ... }
//
// Usage: create().add(step)[.add(step)][.finally(step)].run()
class RpcQueue
{
public:
    RpcQueue(RpcQueue&&) = delete;
    RpcQueue(RpcQueue const&) = delete;
    RpcQueue& operator=(RpcQueue&&) = delete;
    RpcQueue& operator=(RpcQueue const&) = delete;

    [[nodiscard]] static RpcQueue& create()
    {
        auto self = std::shared_ptr<RpcQueue>{ new RpcQueue() };
        self->keepalive_ = self;
        return *self;
    }

    template<typename Func>
    [[nodiscard]] RpcQueue& add(Func&& func)
    {
        queue_.emplace(normalize_func(std::forward<Func>(func)), ErrorHandlerFunction{});
        return *this;
    }

    template<typename Func, typename ErrorHandler>
    [[nodiscard]] RpcQueue& add(Func&& func, ErrorHandler&& error_handler)
    {
        queue_.emplace(
            normalize_func(std::forward<Func>(func)),
            normalize_error_handler(std::forward<ErrorHandler>(error_handler)));
        return *this;
    }

    // `func` runs once when the chain ends, whether or not the chain succeeded
    template<typename Func>
    [[nodiscard]] RpcQueue& finally(Func&& func)
    {
        finally_ = make_copyable(std::forward<Func>(func));
        return *this;
    }

    void run()
    {
        // hold a reference so an immediate finish() can't destroy us mid-call
        auto const self = keepalive_;

        if (std::empty(queue_)) {
            // nothing to do: complete immediately (still runs finally_)
            finish();
            return;
        }

        run_next(RpcResponse{});
    }

private:
    using Continue = RpcClient::ResponseFunc; // std::function<void(RpcResponse)>

    // Internally queued step: takes the previous response and a continuation
    // that must be invoked (once) with this step's result.
    using QueuedFunction = std::function<void(RpcResponse const&, Continue)>;

    // Internally stored error handler: takes the failing response, returns nothing.
    using ErrorHandlerFunction = std::function<void(RpcResponse const&)>;

    RpcQueue() = default;

    [[nodiscard]] static RpcResponse make_ok_response()
    {
        auto ret = RpcResponse{};
        ret.success = true;
        return ret;
    }

    void run_next(RpcResponse const& prev)
    {
        auto [func, error_handler] = std::move(queue_.front());
        queue_.pop();

        next_error_handler_ = std::move(error_handler);

        auto self = keepalive_; // keep alive across the (possibly async) step
        func(prev, [self](RpcResponse result) { self->step_finished(std::move(result)); });
    }

    void step_finished(RpcResponse result)
    {
        // we can't handle network errors: abort the queue and pass the error upward.
        if (result.network_error) {
            finish();
            return;
        }

        // call the user error handler for ordinary (non-network) errors.
        if (!result.success && next_error_handler_) {
            next_error_handler_(result);
        }

        // run the next step if there is one and we succeeded.
        if (result.success && !std::empty(queue_)) {
            run_next(result);
            return;
        }

        finish();
    }

    void finish()
    {
        if (finally_) {
            finally_();
        }
        keepalive_.reset();
    }

    template<typename Func>
    static auto make_copyable(Func&& func)
    {
        using FuncType = std::remove_cvref_t<Func>;
        auto shared_func = std::make_shared<FuncType>(std::forward<Func>(func));
        return [shared_func](auto&&... args) -> decltype(auto) {
            return std::invoke(*shared_func, std::forward<decltype(args)>(args)...);
        };
    }

    // Adapts any of the four step shapes to a uniform QueuedFunction.
    // Precedence (most specific first): (prev, done) | (done) | (prev) | ().
    template<typename Func>
    static QueuedFunction normalize_func(Func&& func)
    {
        using F = std::remove_cvref_t<Func>;
        return [func = make_copyable(std::forward<Func>(func))](RpcResponse const& prev, Continue done) mutable {
            if constexpr (std::invocable<F, RpcResponse const&, Continue>) {
                std::invoke(func, prev, std::move(done));
            } else if constexpr (std::invocable<F, Continue>) {
                std::invoke(func, std::move(done));
            } else if constexpr (std::invocable<F, RpcResponse const&>) {
                std::invoke(func, prev);
                done(make_ok_response());
            } else {
                static_assert(std::invocable<F>, "step must take (prev, done), (done), (prev), or ()");
                std::invoke(func);
                done(make_ok_response());
            }
        };
    }

    // Adapts an error handler that takes either the failing response or nothing.
    template<typename Func>
    static ErrorHandlerFunction normalize_error_handler(Func&& func)
    {
        using F = std::remove_cvref_t<Func>;
        return [func = make_copyable(std::forward<Func>(func))](RpcResponse const& r) mutable {
            if constexpr (std::invocable<F, RpcResponse const&>) {
                std::invoke(func, r);
            } else {
                static_assert(std::invocable<F>, "error handler must take (RpcResponse const&) or ()");
                std::invoke(func);
            }
        };
    }

    std::shared_ptr<RpcQueue> keepalive_;
    std::queue<std::pair<QueuedFunction, ErrorHandlerFunction>> queue_;
    ErrorHandlerFunction next_error_handler_;
    std::function<void()> finally_;
};

} // namespace tr::app
