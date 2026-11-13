// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/watchdir-base.h"

using namespace std::literals;

namespace tr
{
namespace
{

[[nodiscard]] constexpr std::string_view actionToString(Watchdir::Action action)
{
    switch (action) {
    case Watchdir::Action::Retry:
        return "retry";

    case Watchdir::Action::Done:
        return "done";

    case Watchdir::Action::Wait:
        return "wait";
    }

    return "???";
}

[[nodiscard]] bool isRegularFile(std::string_view dir, std::string_view name)
{
    auto const path = tr_pathbuf{ dir, '/', name };

    auto error = tr_error{};
    auto const info = tr_sys_path_get_info(path, 0, &error);
    if (error && !error.is_enoent()) {
        tr_logAddWarn(
            fmt::format(
                "Skipping '{path}': {error} ({error_code})",
                fmt::arg("path", path),
                fmt::arg("error", error.message()),
                fmt::arg("error_code", error.code())));
    }

    return info && info->isFile();
}

} // namespace

namespace impl
{

void BaseWatchdir::processFile(std::string_view const basename)
{
    if (handled_.contains(basename)) {
        return;
    }

    if (!isRegularFile(dirname_, basename)) {
        forgetFile(basename);
        return;
    }

    auto const action = callback_(dirname_, basename);
    tr_logAddDebug(fmt::format("Callback decided to {:s} file '{:s}'", actionToString(action), basename));
    if (action == Action::Done) {
        forgetFile(basename);
        handled_.emplace(basename);
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    auto const [iter, added] = pending_.try_emplace(std::string{ basename });
    auto& info = iter->second;
    if (added) {
        info.first_kick_at = now;
    }
    info.last_kick_at = now;

    if (action == Action::Retry && now - info.first_kick_at > TimeoutDuration) {
        tr_logAddWarn(fmt::format("Couldn't add torrent file '{path}'", fmt::arg("path", basename)));
        pending_.erase(iter);
    } else {
        restartTimerIfPending();
    }
}

void BaseWatchdir::forgetFile(std::string_view const basename)
{
    if (auto const iter = handled_.find(basename); iter != std::end(handled_)) {
        handled_.erase(iter);
    }

    if (auto const iter = pending_.find(basename); iter != std::end(pending_)) {
        pending_.erase(iter);
    }
}

void BaseWatchdir::logWatchError(int const error_code) const
{
    tr_logAddError(
        fmt::format(
            "Couldn't watch '{path}': {error} ({error_code})",
            fmt::arg("path", dirname()),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
}

void BaseWatchdir::scan()
{
    // tr_sys_dir_get_files() lists a missing dir as empty, which would hide a mistyped path
    // and forget every handled name while a share whose mount point goes away is unmounted
    auto error = tr_error{};
    auto files = std::vector<std::string>{};
    if (auto const info = tr_sys_path_get_info(dirname_, 0, &error); info && info->isFolder()) {
        files = tr_sys_dir_get_files(
            dirname_,
            [this](std::string_view const name) { return tr_basename_is_not_dotfile(name) && (!filter_ || filter_(name)); },
            &error);
    } else if (info) {
        error.set_from_errno(ENOTDIR);
    }

    if (error) {
        // once per failure, since a generic watch dir rescans on a timer
        if (error.code() != scan_error_code_) {
            tr_logAddWarn(
                fmt::format(
                    "Couldn't read '{path}': {error} ({error_code})",
                    fmt::arg("path", dirname()),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
        }
    } else {
        // only a complete listing shows which files have left
        std::ranges::sort(files);
        auto const is_gone = [&files](std::string_view const name) {
            return !std::ranges::binary_search(files, name);
        };
        std::erase_if(handled_, is_gone);
        std::erase_if(pending_, [&is_gone](auto const& entry) { return is_gone(entry.first); });
    }
    scan_error_code_ = error.code();

    for (auto const& file : files) {
        processFile(file);
    }
}

} // namespace impl
} // namespace tr
