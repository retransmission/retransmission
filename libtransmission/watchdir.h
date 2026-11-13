// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

struct event_base;

namespace tr
{

class TimerMaker;

class Watchdir
{
public:
    // What a callback did with a file, and so when the watch dir offers it again:
    // Done: once a later file of that name arrives;
    // Retry: it couldn't use the file, so in a few seconds for 15 s, then at the next scan or change to it;
    // Wait: the file isn't ready, e.g. still being written, so in a few seconds, however long that takes.
    enum class Action : uint8_t { Done, Retry, Wait };

    using Callback = std::function<Action(std::string_view dirname, std::string_view basename)>;

    // Says which names to offer to the callback. A watch dir ignores the others: it doesn't stat them or remember them.
    using NameFilter = std::function<bool(std::string_view basename)>;

    Watchdir() = default;
    virtual ~Watchdir() = default;
    Watchdir(Watchdir&&) = delete;
    Watchdir(Watchdir const&) = delete;
    Watchdir& operator=(Watchdir&&) = delete;
    Watchdir& operator=(Watchdir const&) = delete;

    [[nodiscard]] virtual std::string_view dirname() const noexcept = 0;

    // Scans the directory now, e.g. because the caller's own monitor saw it change.
    virtual void rescan() = 0;

    static void set_generic_rescan_interval(std::chrono::milliseconds interval) noexcept
    {
        generic_rescan_interval_ = interval;
    }

    [[nodiscard]] static std::unique_ptr<Watchdir> create(
        std::string_view dirname,
        Callback callback,
        tr::TimerMaker& timer_maker,
        struct event_base* evbase);

    [[nodiscard]] static std::unique_ptr<Watchdir> create_generic(
        std::string_view dirname,
        Callback callback,
        tr::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval = generic_rescan_interval_,
        NameFilter filter = {});

private:
    // NOLINTNEXTLINE(readability-identifier-naming)
    static inline auto generic_rescan_interval_ = std::chrono::milliseconds{ 1000 };
};

} // namespace tr
