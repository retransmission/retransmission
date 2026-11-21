// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <functional>
#include <memory>
#include <utility>

struct event_base;

class tr_session_thread
{
protected:
    using callback_t = std::function<void()>;

public:
    static void tr_evthread_init();

    [[nodiscard]] static std::unique_ptr<tr_session_thread> create();
    virtual ~tr_session_thread() = default;

    [[nodiscard]] virtual struct event_base* event_base() noexcept = 0;

    [[nodiscard]] virtual bool am_in_session_thread() const noexcept = 0;

    virtual void queue(callback_t&& func) = 0;

    virtual void run(callback_t&& func) = 0;

    template<typename Func, typename... Args>
    void queue(Func&& func, Args&&... args)
    {
        queue(make_callback(std::forward<Func>(func), std::forward<Args>(args)...));
    }

    template<typename Func, typename... Args>
    void run(Func&& func, Args&&... args)
    {
        run(make_callback(std::forward<Func>(func), std::forward<Args>(args)...));
    }

private:
    // A lambda, not std::bind_front: with libstdc++ 14.2 headers,
    // clang finds std::bind_front's result not invocable.
    template<typename Func, typename... Args>
    [[nodiscard]] static callback_t make_callback(Func&& func, Args&&... args)
    {
        return [func = std::forward<Func>(func), ... args = std::forward<Args>(args)] mutable {
            std::invoke(func, args...);
        };
    }
};
