// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <ctime> // time_t
#include <string>

#include <fmt/format.h>

#include <libtransmission/utils.h> // _(), tr_ngettext()

#include "libtransmission-app/formatters.h"

namespace tr::app
{
namespace
{
auto constexpr SecondsPerMinute = time_t{ 60 };
auto constexpr SecondsPerHour = time_t{ 3600 };
auto constexpr SecondsPerDay = time_t{ 86400 };

// Each phrase is a whole sentence for translators,
// since languages inflect a unit differently after "in", "ago" or "left".
[[nodiscard]] std::string format_time_from_now(time_t const seconds)
{
    if (auto const days_from_now = seconds / SecondsPerDay; days_from_now > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{days_from_now:L} day from now", "{days_from_now:L} days from now", days_from_now)),
            fmt::arg("days_from_now", days_from_now));
    }

    if (auto const hours_from_now = (seconds % SecondsPerDay) / SecondsPerHour; hours_from_now > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{hours_from_now:L} hour from now", "{hours_from_now:L} hours from now", hours_from_now)),
            fmt::arg("hours_from_now", hours_from_now));
    }

    if (auto const minutes_from_now = (seconds % SecondsPerHour) / SecondsPerMinute; minutes_from_now > 0) {
        return fmt::format(
            fmt::runtime(
                tr_ngettext("{minutes_from_now:L} minute from now", "{minutes_from_now:L} minutes from now", minutes_from_now)),
            fmt::arg("minutes_from_now", minutes_from_now));
    }

    if (auto const seconds_from_now = seconds % SecondsPerMinute; seconds_from_now > 0) {
        return fmt::format(
            fmt::runtime(
                tr_ngettext("{seconds_from_now:L} second from now", "{seconds_from_now:L} seconds from now", seconds_from_now)),
            fmt::arg("seconds_from_now", seconds_from_now));
    }

    return _("now");
}

[[nodiscard]] std::string format_time_ago(time_t const seconds)
{
    if (auto const days_ago = seconds / SecondsPerDay; days_ago > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{days_ago:L} day ago", "{days_ago:L} days ago", days_ago)),
            fmt::arg("days_ago", days_ago));
    }

    if (auto const hours_ago = (seconds % SecondsPerDay) / SecondsPerHour; hours_ago > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{hours_ago:L} hour ago", "{hours_ago:L} hours ago", hours_ago)),
            fmt::arg("hours_ago", hours_ago));
    }

    if (auto const minutes_ago = (seconds % SecondsPerHour) / SecondsPerMinute; minutes_ago > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{minutes_ago:L} minute ago", "{minutes_ago:L} minutes ago", minutes_ago)),
            fmt::arg("minutes_ago", minutes_ago));
    }

    if (auto const seconds_ago = seconds % SecondsPerMinute; seconds_ago > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{seconds_ago:L} second ago", "{seconds_ago:L} seconds ago", seconds_ago)),
            fmt::arg("seconds_ago", seconds_ago));
    }

    return _("now");
}
} // namespace

std::string format_time(time_t const seconds)
{
    if (auto const days = seconds / SecondsPerDay; days > 0) {
        return fmt::format(fmt::runtime(tr_ngettext("{days:L} day", "{days:L} days", days)), fmt::arg("days", days));
    }

    if (auto const hours = (seconds % SecondsPerDay) / SecondsPerHour; hours > 0) {
        return fmt::format(fmt::runtime(tr_ngettext("{hours:L} hour", "{hours:L} hours", hours)), fmt::arg("hours", hours));
    }

    if (auto const minutes = (seconds % SecondsPerHour) / SecondsPerMinute; minutes > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{minutes:L} minute", "{minutes:L} minutes", minutes)),
            fmt::arg("minutes", minutes));
    }

    if (auto const secs = seconds % SecondsPerMinute; secs > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{seconds:L} second", "{seconds:L} seconds", secs)),
            fmt::arg("seconds", secs));
    }

    return _("now");
}

std::string format_time_left(time_t const seconds)
{
    if (auto const days_left = seconds / SecondsPerDay; days_left > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{days_left:L} day left", "{days_left:L} days left", days_left)),
            fmt::arg("days_left", days_left));
    }

    if (auto const hours_left = (seconds % SecondsPerDay) / SecondsPerHour; hours_left > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{hours_left:L} hour left", "{hours_left:L} hours left", hours_left)),
            fmt::arg("hours_left", hours_left));
    }

    if (auto const minutes_left = (seconds % SecondsPerHour) / SecondsPerMinute; minutes_left > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{minutes_left:L} minute left", "{minutes_left:L} minutes left", minutes_left)),
            fmt::arg("minutes_left", minutes_left));
    }

    if (auto const seconds_left = seconds % SecondsPerMinute; seconds_left > 0) {
        return fmt::format(
            fmt::runtime(tr_ngettext("{seconds_left:L} second left", "{seconds_left:L} seconds left", seconds_left)),
            fmt::arg("seconds_left", seconds_left));
    }

    return _("now");
}

std::string format_time_relative(time_t const then, time_t const now)
{
    return then > now ? format_time_from_now(then - now) : format_time_ago(now - then);
}

} // namespace tr::app
