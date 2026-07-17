// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <ctime> // time_t
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "libtransmission/transmission.h" // tr_blocklist_update_func
#include "libtransmission/web.h" // tr_web::FetchOptions

namespace tr
{
class Timer;
class TimerMaker;
} // namespace tr

namespace tr::blocklist
{
// opaque per-request state; defined in blocklist-download.cc
struct Pending;

// Peel off any container/compressor a blocklist provider might wrap a list in
// (gzip, tar (possibly gzipped), zip) and return the first regular file's
// contents; a plain, unwrapped list is returned as-is. Exposed here so the
// format matrix can be tested directly, without a session or a download.
[[nodiscard]] std::string decompress(std::string_view body);

// Downloads the blocklist URL, decompresses it (see decompress()), installs the
// result, and reports the outcome. Also owns the periodic auto-update timer.
// One instance is owned by each tr_session.
class Updater
{
public:
    // Seam to the outside world. tr_session in production, a mock in tests.
    struct Mediator {
        virtual ~Mediator() = default;

        // --- downloading ---

        // The configured blocklist URL to fetch.
        [[nodiscard]] virtual std::string blocklist_url() const = 0;

        // Fetch over HTTP. The response callback is invoked on the session thread.
        virtual void fetch(tr_web::FetchOptions&& options) = 0;

        // --- installing ---

        // Install decompressed blocklist content and return the new rule count.
        // Returns positive on success, 0 for well-formed-but-empty content.
        // On error, return nullopt and set `error` to a readable string.
        [[nodiscard]] virtual std::optional<size_t> set_blocklist_content(std::string_view content, std::string& error) = 0;

        // --- auto-update timer inputs ---

        [[nodiscard]] virtual bool enabled() const noexcept = 0;
        [[nodiscard]] virtual bool updates_enabled() const noexcept = 0;
        [[nodiscard]] virtual time_t mtime() const = 0;

        // --- threading + timers ---

        [[nodiscard]] virtual tr::TimerMaker& timer_maker() noexcept = 0;
        virtual void run_in_session_thread(std::function<void()> func) = 0;
    };

    explicit Updater(Mediator& mediator);
    ~Updater();

    Updater(Updater const&) = delete;
    Updater(Updater&&) = delete;
    Updater& operator=(Updater const&) = delete;
    Updater& operator=(Updater&&) = delete;

    // Start a one-shot update. `on_done` is invoked exactly once, on the
    // session thread: with the update's outcome when it finishes, or with a
    // Superseded status if a newer update() takes over first. It is not invoked
    // at all if cancel() is called first.
    void update(tr_blocklist_update_func on_done);

    // Abandon the most recent in-flight update: its `on_done` will not fire.
    // (The underlying HTTP transfer may still run to completion internally,
    // since tr_web has no per-request abort.)
    void cancel();

    // (Re)arm or disarm the periodic auto-update timer to match the current
    // settings (blocklist enabled + URL set + updates enabled). Safe to call
    // from any thread.
    void restart_timer();

private:
    void arm_timer();
    void on_auto_update_timer();

    Mediator& mediator_;
    std::unique_ptr<tr::Timer> timer_;
    std::weak_ptr<Pending> latest_;
};

} // namespace tr::blocklist
