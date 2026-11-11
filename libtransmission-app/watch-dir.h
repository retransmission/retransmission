// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

#include <sigslot/signal.hpp>

#include "libtransmission/watchdir.h"

namespace tr
{
class Timer;
class TimerMaker;
} // namespace tr

namespace tr::app
{

class Prefs;
class Session;

// A toolkit's directory monitor, e.g. a QFileSystemWatcher or a Gio::FileMonitor.
// Destroying it stops the notices.
class DirMonitor
{
public:
    DirMonitor() = default;
    virtual ~DirMonitor() = default;
    DirMonitor(DirMonitor&&) = delete;
    DirMonitor(DirMonitor const&) = delete;
    DirMonitor& operator=(DirMonitor&&) = delete;
    DirMonitor& operator=(DirMonitor const&) = delete;
};

class DirMonitorMaker
{
public:
    DirMonitorMaker() = default;
    virtual ~DirMonitorMaker() = default;
    DirMonitorMaker(DirMonitorMaker&&) = delete;
    DirMonitorMaker(DirMonitorMaker const&) = delete;
    DirMonitorMaker& operator=(DirMonitorMaker&&) = delete;
    DirMonitorMaker& operator=(DirMonitorMaker const&) = delete;

    // Calls `on_change` on the UI thread when `dirname`'s entries change, and may also call it
    // when a write to one finishes. It never calls it before `create()` returns.
    // Returns nullptr if the toolkit can't monitor `dirname`.
    [[nodiscard]] virtual std::unique_ptr<DirMonitor> create(std::string_view dirname, std::function<void()> on_change) = 0;
};

// Adds each .torrent file that appears in the `watch_dir` pref's directory through
// `Session::request_add()`, which renames or trashes the file once the session has it.
// Watches while `watch_dir_enabled` is set and the session is started.
// It scans when the client's monitor reports a change, and every `FallbackScanInterval`
// for the changes a monitor misses, e.g. every change on most network shares.
// Construct it before the session starts. It starts watching on the loop turn after
// `source_changed`, so a client can connect its add prompt after starting the session.
class WatchDir
{
public:
    static constexpr auto FallbackScanInterval = std::chrono::minutes{ 1 };

    // `prefs`, `timer_maker` and `monitor_maker` must outlive the watch dir.
    WatchDir(Session& session, Prefs const& prefs, tr::TimerMaker& timer_maker, DirMonitorMaker& monitor_maker);
    ~WatchDir() = default;
    WatchDir(WatchDir&&) = delete;
    WatchDir(WatchDir const&) = delete;
    WatchDir& operator=(WatchDir&&) = delete;
    WatchDir& operator=(WatchDir const&) = delete;

    // the directory being watched, or empty when not watching
    [[nodiscard]] std::string_view dirname() const noexcept;

private:
    void set_started(bool started);
    void update();
    [[nodiscard]] tr::Watchdir::Action on_file(std::string_view dirname, std::string_view basename) const;

    Session& session_;
    Prefs const& prefs_;
    tr::TimerMaker& timer_maker_;
    DirMonitorMaker& monitor_maker_;
    std::unique_ptr<tr::Timer> const start_timer_;
    std::unique_ptr<tr::Watchdir> watchdir_;
    std::unique_ptr<DirMonitor> monitor_; // after watchdir_, which it calls into
    bool started_ = false;

    sigslot::scoped_connection prefs_connection_;
    sigslot::scoped_connection source_changed_connection_;
    sigslot::scoped_connection stopped_connection_;
};

} // namespace tr::app
