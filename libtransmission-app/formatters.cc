// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <ctime> // time_t
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/types.h> // tr_tracker_view
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

// Escapes text for Pango markup and for HTML,
// and replaces control characters, which neither displays.
[[nodiscard]] std::string escape_markup(std::string_view const text)
{
    auto escaped = std::string{};
    escaped.reserve(std::size(text));

    for (auto const ch : text) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped += (static_cast<unsigned char>(ch) < 0x20U || ch == 0x7F) ? ' ' : ch;
            break;
        }
    }

    return escaped;
}

void append_announce_status(
    std::vector<std::string>& lines,
    tr_tracker_view const& tracker,
    time_t const now,
    TrackerStatusMarkup const& markup)
{
    if (tracker.hasAnnounced && tracker.announceState != TR_TRACKER_INACTIVE) {
        auto const time_span_ago = format_time_relative(tracker.lastAnnounceTime, now);

        if (tracker.lastAnnounceSucceeded) {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround the peer text
                    fmt::runtime(tr_ngettext(
                        "Got a list of {markup_begin}{peer_count} peer{markup_end} {time_span_ago}",
                        "Got a list of {markup_begin}{peer_count} peers{markup_end} {time_span_ago}",
                        tracker.lastAnnouncePeerCount)),
                    fmt::arg("markup_begin", markup.success_begin),
                    fmt::arg("peer_count", tracker.lastAnnouncePeerCount),
                    fmt::arg("markup_end", markup.success_end),
                    fmt::arg("time_span_ago", time_span_ago)));
        } else if (tracker.lastAnnounceTimedOut) {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround the time_span
                    fmt::runtime(_("Peer list request {markup_begin}timed out {time_span_ago}{markup_end}; will retry")),
                    fmt::arg("markup_begin", markup.timeout_begin),
                    fmt::arg("time_span_ago", time_span_ago),
                    fmt::arg("markup_end", markup.timeout_end)));
        } else {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround the error
                    fmt::runtime(_("Got an error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                    fmt::arg("markup_begin", markup.error_begin),
                    fmt::arg("error", escape_markup(std::data(tracker.lastAnnounceResult))),
                    fmt::arg("markup_end", markup.error_end),
                    fmt::arg("time_span_ago", time_span_ago)));
        }
    }

    switch (tracker.announceState) {
    case TR_TRACKER_INACTIVE:
        lines.emplace_back(_("No updates scheduled"));
        break;

    case TR_TRACKER_WAITING:
        lines.emplace_back(
            fmt::format(
                fmt::runtime(_("Asking for more peers {time_span_from_now}")),
                fmt::arg("time_span_from_now", format_time_relative(tracker.nextAnnounceTime, now))));
        break;

    case TR_TRACKER_QUEUED:
        lines.emplace_back(_("Queued to ask for more peers"));
        break;

    case TR_TRACKER_ACTIVE:
        lines.emplace_back(
            fmt::format(
                // {markup_begin} and {markup_end} should surround time_span_ago
                fmt::runtime(_("Asked for more peers {markup_begin}{time_span_ago}{markup_end}")),
                fmt::arg("markup_begin", "<small>"),
                fmt::arg("time_span_ago", format_time_relative(tracker.lastAnnounceStartTime, now)),
                fmt::arg("markup_end", "</small>")));
        break;

    default:
        break;
    }
}

void append_scrape_status(
    std::vector<std::string>& lines,
    tr_tracker_view const& tracker,
    time_t const now,
    TrackerStatusMarkup const& markup)
{
    if (tracker.hasScraped) {
        auto const time_span_ago = format_time_relative(tracker.lastScrapeTime, now);

        if (!tracker.lastScrapeSucceeded) {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround the error text
                    fmt::runtime(_("Got a scrape error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                    fmt::arg("error", escape_markup(std::data(tracker.lastScrapeResult))),
                    fmt::arg("time_span_ago", time_span_ago),
                    fmt::arg("markup_begin", markup.error_begin),
                    fmt::arg("markup_end", markup.error_end)));
        } else if (tracker.seederCount < 0 || tracker.leecherCount < 0) {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround "no information"
                    fmt::runtime(_("Tracker had {markup_begin}no information{markup_end} on peer counts {time_span_ago}")),
                    fmt::arg("markup_begin", markup.success_begin),
                    fmt::arg("markup_end", markup.success_end),
                    fmt::arg("time_span_ago", time_span_ago)));
        } else {
            lines.emplace_back(
                fmt::format(
                    // {markup_begin} and {markup_end} should surround the seeder/leecher text
                    fmt::runtime(_(
                        "Tracker had {markup_begin}{seeder_count} {seeder_or_seeders} and {leecher_count} {leecher_or_leechers}{markup_end} {time_span_ago}")),
                    fmt::arg("seeder_count", tracker.seederCount),
                    fmt::arg("seeder_or_seeders", tr_ngettext("seeder", "seeders", tracker.seederCount)),
                    fmt::arg("leecher_count", tracker.leecherCount),
                    fmt::arg("leecher_or_leechers", tr_ngettext("leecher", "leechers", tracker.leecherCount)),
                    fmt::arg("time_span_ago", time_span_ago),
                    fmt::arg("markup_begin", markup.success_begin),
                    fmt::arg("markup_end", markup.success_end)));
        }
    }

    switch (tracker.scrapeState) {
    case TR_TRACKER_WAITING:
        lines.emplace_back(
            fmt::format(
                fmt::runtime(_("Asking for peer counts {time_span_from_now}")),
                fmt::arg("time_span_from_now", format_time_relative(tracker.nextScrapeTime, now))));
        break;

    case TR_TRACKER_QUEUED:
        lines.emplace_back(_("Queued to ask for peer counts"));
        break;

    case TR_TRACKER_ACTIVE:
        lines.emplace_back(
            fmt::format(
                fmt::runtime(_("Asked for peer counts {markup_begin}{time_span_ago}{markup_end}")),
                fmt::arg("markup_begin", "<small>"),
                fmt::arg("time_span_ago", format_time_relative(tracker.lastScrapeStartTime, now)),
                fmt::arg("markup_end", "</small>")));
        break;

    default: // TR_TRACKER_INACTIVE
        break;
    }
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

std::vector<std::string> tracker_status_lines(
    tr_tracker_view const& tracker,
    time_t const now,
    bool const with_scrape,
    TrackerStatusMarkup const& markup)
{
    auto lines = std::vector<std::string>{};
    append_announce_status(lines, tracker, now, markup);

    if (with_scrape) {
        append_scrape_status(lines, tracker, now, markup);
    }

    return lines;
}

} // namespace tr::app
