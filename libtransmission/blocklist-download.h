// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>

#include "libtransmission/transmission.h" // tr_blocklist_update_func

struct tr_session;

namespace tr
{
class Timer;
}

namespace tr::blocklist
{
// opaque per-request state; defined in blocklist-download.cc
struct Pending;

// Downloads the session's blocklist URL, decompresses it (gzip, tar, zip, or
// plain text -- see decompress()), installs it via tr_blocklistSetContent(),
// and reports the outcome. Also owns the periodic auto-update timer. One
// instance is owned by each tr_session.
class Updater
{
public:
    explicit Updater(tr_session* session);
    ~Updater();

    Updater(Updater const&) = delete;
    Updater(Updater&&) = delete;
    Updater& operator=(Updater const&) = delete;
    Updater& operator=(Updater&&) = delete;

    // Start a one-shot update. `on_done` is invoked exactly once, on the
    // session thread, when the update finishes -- unless cancel() is called
    // first, in which case it is not invoked at all.
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

    tr_session* const session_;
    std::unique_ptr<tr::Timer> timer_;
    std::weak_ptr<Pending> latest_;
};

} // namespace tr::blocklist
