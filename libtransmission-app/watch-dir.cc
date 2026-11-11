// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/watch-dir.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

#include "libtransmission/file.h"
#include "libtransmission/quark.h"
#include "libtransmission/timer.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/watchdir.h"

#include "libtransmission-app/add-data.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/session.h"

using namespace std::literals;

namespace tr::app
{
namespace
{

// whether `name` ends in ".torrent", in any case
[[nodiscard]] constexpr bool is_torrent_name(std::string_view const name) noexcept
{
    auto constexpr Suffix = ".torrent"sv;
    auto const ascii_lower = [](char const ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    };
    return std::size(name) >= std::size(Suffix) &&
        std::ranges::equal(name.substr(std::size(name) - std::size(Suffix)), Suffix, {}, ascii_lower);
}

static_assert(is_torrent_name("a.TORRENT"sv));
static_assert(!is_torrent_name("a.torrent.added"sv));

// whether `path` changed so recently that it may still be being written
[[nodiscard]] bool is_settling(std::string_view const path)
{
    auto constexpr SettleSeconds = time_t{ 2 };
    auto const info = tr_sys_path_get_info(path);
    return info && info->last_modified_at + SettleSeconds >= std::time(nullptr);
}

} // namespace

WatchDir::WatchDir(Session& session, Prefs const& prefs, tr::TimerMaker& timer_maker, DirMonitorMaker& monitor_maker)
    : session_{ session }
    , prefs_{ prefs }
    , timer_maker_{ timer_maker }
    , monitor_maker_{ monitor_maker }
    , start_timer_{ timer_maker.create([this] { set_started(true); }) }
    , prefs_connection_{ prefs.observe_changes([this](tr_quark const key) {
        if (key == TR_KEY_watch_dir || key == TR_KEY_watch_dir_enabled) {
            update();
        }
    }) }
    , source_changed_connection_{ session.source_changed.connect_scoped(
          [this] { start_timer_->start_single_shot(std::chrono::milliseconds::zero()); }) }
    , stopped_connection_{ session.stopped.connect_scoped([this] {
        start_timer_->stop();
        set_started(false);
    }) }
{
}

std::string_view WatchDir::dirname() const noexcept
{
    return watchdir_ ? watchdir_->dirname() : std::string_view{};
}

void WatchDir::set_started(bool const started)
{
    started_ = started;
    update();
}

void WatchDir::update()
{
    auto const dir = prefs_.get<std::string>(TR_KEY_watch_dir);
    if (!started_ || !prefs_.get<bool>(TR_KEY_watch_dir_enabled) || std::empty(dir)) {
        monitor_.reset();
        watchdir_.reset();
        return;
    }

    if (dirname() != dir) {
        // the monitor first, so that a file landing during the first scan doesn't wait for the fallback scan;
        // its notices arrive on a later loop turn, once `watchdir_` is the new one.
        // One monitor at a time, since a monitor may hold a scarce OS handle.
        monitor_.reset();
        monitor_ = monitor_maker_.create(dir, [this] { watchdir_->rescan(); });
        watchdir_ = tr::Watchdir::create_generic(
            dir,
            [this](std::string_view const dirname, std::string_view const basename) { return on_file(dirname, basename); },
            timer_maker_,
            FallbackScanInterval,
            is_torrent_name);
    }
}

tr::Watchdir::Action WatchDir::on_file(std::string_view const dirname, std::string_view const basename) const
{
    auto const path = tr_pathbuf{ dirname, '/', basename };
    auto add = AddData{ path.sv() };
    if (!add.parsed()) {
        // not a torrent, unless it's still being written
        return is_settling(path.sv()) ? tr::Watchdir::Action::Wait : tr::Watchdir::Action::Done;
    }

    add.from_watch_dir = true;
    session_.request_add(std::move(add));
    return tr::Watchdir::Action::Done;
}

} // namespace tr::app
