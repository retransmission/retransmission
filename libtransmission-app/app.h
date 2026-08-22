// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <ctime>

namespace tr::app
{

// should be called once when starting the app
void init();

// How recently a torrent must have been active for the session to count as busy.
// Sleep inhibition only asks "did anything just happen", so this is far shorter
// than the queue's stalled-torrent threshold.
inline constexpr time_t ActivityWindow = 120;

// Whether `tr_sessionActivityDate()`'s answer is recent enough to count as busy.
[[nodiscard]] constexpr bool is_recently_active(time_t const activity_date, time_t const now) noexcept
{
    return activity_date != 0 && now - activity_date < ActivityWindow;
}

} // namespace tr::app
