// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "ShutdownStatus.h"

#include "Utils.h" // tr_format_time_left(), tr_strlsize()

#include <libtransmission/transmission.h>

#include <glibmm/i18n.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using Op = tr_disk_activity::Op;
using Kind = Op::Kind;

// moves take longest, so they lead
[[nodiscard]] constexpr int rank(Kind const kind) noexcept
{
    switch (kind) {
    case Kind::Move:
        return 0;
    case Kind::Remove:
        return 1;
    case Kind::Write:
        return 2;
    case Kind::Rename:
        return 3;
    case Kind::Close:
        return 4;
    }
    return 5;
}

[[nodiscard]] constexpr uint64_t remaining(Op const& op) noexcept
{
    return op.total - std::min(op.done, op.total);
}

// the op that will take longest: the slowest kind, then the most left to do
[[nodiscard]] Op const* pick(std::vector<Op> const& ops)
{
    auto const it = std::ranges::min_element(ops, [](Op const& a, Op const& b) {
        return std::tuple{ rank(a.kind), remaining(b) } < std::tuple{ rank(b.kind), remaining(a) };
    });
    return it != std::end(ops) ? &*it : nullptr;
}

[[nodiscard]] std::string headline_for(Op const* const op)
{
    if (op != nullptr && op->kind == Kind::Move) {
        return _("Moving files…");
    }

    if (op != nullptr && op->kind == Kind::Remove) {
        return _("Deleting files…");
    }

    return _("Finishing disk writes…");
}

// Empty unless it has a number that moves.
[[nodiscard]] std::string describe(Op const& op, tr_disk_activity const& activity)
{
    switch (op.kind) {
    case Kind::Write:
        if (activity.queued_write_bytes == 0U) {
            return {};
        }
        if (auto const n_others = static_cast<size_t>(std::ranges::count(activity.in_flight, Kind::Write, &Op::kind)) - 1U;
            n_others > 0U) {
            return fmt::format(
                fmt::runtime(ngettext(
                    "Writing '{name}' and {count:L} more — {size} remaining",
                    "Writing '{name}' and {count:L} more — {size} remaining",
                    static_cast<unsigned long>(n_others))),
                fmt::arg("name", op.torrent_name),
                fmt::arg("count", n_others),
                fmt::arg("size", tr_strlsize(activity.queued_write_bytes)));
        }
        return fmt::format(
            fmt::runtime(_("Writing '{name}' — {size} remaining")),
            fmt::arg("name", op.torrent_name),
            fmt::arg("size", tr_strlsize(activity.queued_write_bytes)));

    case Kind::Move:
        if (op.total == 0U) {
            return {};
        }
        if (op.file_count > 1U) {
            return fmt::format(
                fmt::runtime(_("'{name}' to '{path}' — file {file:L} of {files:L}, {done} of {total}")),
                fmt::arg("name", op.torrent_name),
                fmt::arg("path", op.destination),
                fmt::arg("file", op.file_index + 1U),
                fmt::arg("files", op.file_count),
                fmt::arg("done", tr_strlsize(op.done)),
                fmt::arg("total", tr_strlsize(op.total)));
        }
        return fmt::format(
            fmt::runtime(_("'{name}' to '{path}' — {done} of {total}")),
            fmt::arg("name", op.torrent_name),
            fmt::arg("path", op.destination),
            fmt::arg("done", tr_strlsize(op.done)),
            fmt::arg("total", tr_strlsize(op.total)));

    case Kind::Remove:
        if (op.total == 0U) {
            return {};
        }
        return fmt::format(
            fmt::runtime(ngettext(
                "'{name}' — {done:L} of {total:L} file",
                "'{name}' — {done:L} of {total:L} files",
                static_cast<unsigned long>(op.total))),
            fmt::arg("name", op.torrent_name),
            fmt::arg("done", op.done),
            fmt::arg("total", op.total));

    case Kind::Rename:
    case Kind::Close:
        break;
    }

    return {};
}
} // namespace

ShutdownStatus make_shutdown_status(tr_disk_activity const& activity)
{
    auto const* const op = pick(activity.in_flight);
    auto const n_waiting = static_cast<unsigned long>(activity.queued_admin_count);

    if (op != nullptr || n_waiting != 0U || activity.phase == tr_close_phase::DrainingDisk) {
        auto status = ShutdownStatus{ .headline = headline_for(op), .detail = op != nullptr ? describe(*op, activity) : "" };
        if (n_waiting != 0U && std::empty(status.detail)) {
            status.detail = fmt::format(
                fmt::runtime(ngettext("{count:L} more torrent waiting", "{count:L} more torrents waiting", n_waiting)),
                fmt::arg("count", n_waiting));
        } else if (n_waiting != 0U) {
            status.detail = fmt::format(
                fmt::runtime(ngettext(
                    "{status}, and {count:L} more torrent waiting",
                    "{status}, and {count:L} more torrents waiting",
                    n_waiting)),
                fmt::arg("status", status.detail),
                fmt::arg("count", n_waiting));
        }
        return status;
    }

    if (activity.phase == tr_close_phase::WaitingTrackers && activity.trackers_total != 0U) {
        return { .headline = _("Closing connections…"),
                 .detail = fmt::format(
                     fmt::runtime(
                         _("Sending upload/download totals to trackers… {replied:L} of {total:L} replied, {time_left}")),
                     fmt::arg("replied", activity.trackers_total - activity.trackers_pending),
                     fmt::arg("total", activity.trackers_total),
                     fmt::arg("time_left", tr_format_time_left(activity.trackers_time_left.count()))) };
    }

    return { .headline = _("Shutting down…"), .detail = {} };
}
