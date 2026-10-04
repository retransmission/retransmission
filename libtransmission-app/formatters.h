// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <ctime> // time_t
#include <string>
#include <string_view>
#include <vector>

struct tr_tracker_view;

// Translated text that the clients share, so that translators translate it once.
// Numbers follow the C++ global locale, which each client sets.
namespace tr::app
{

// A duration in its largest unit, e.g. "5 minutes", or "now" for none.
[[nodiscard]] std::string format_time(time_t seconds);

// A duration that remains, e.g. "5 minutes left".
[[nodiscard]] std::string format_time_left(time_t seconds);

// When `then` is, seen from `now`, e.g. "5 minutes ago" or "5 minutes from now".
[[nodiscard]] std::string format_time_relative(time_t then, time_t now);

// The markup that a client wraps around parts of a tracker's status,
// e.g. Pango's <span color='red'> or HTML's <span style="color:red">.
struct TrackerStatusMarkup {
    std::string_view success_begin;
    std::string_view success_end;
    std::string_view timeout_begin;
    std::string_view timeout_end;
    std::string_view error_begin;
    std::string_view error_end;

    // Around the time of a request that is under way.
    std::string_view pending_begin = "<small>";
    std::string_view pending_end = "</small>";

    // Whether to escape the tracker's error messages, which markup needs since they come from the tracker.
    // A client that shows the lines as plain text leaves every field above empty and sets this to false.
    bool escape = true;
};

// A tracker's status as lines of Pango or HTML markup, or of plain text:
// what its last announce got and when it asks for peers again,
// then the same for scrapes if `with_scrape`.
[[nodiscard]] std::vector<std::string> tracker_status_lines(
    tr_tracker_view const& tracker,
    time_t now,
    bool with_scrape,
    TrackerStatusMarkup const& markup);

} // namespace tr::app
