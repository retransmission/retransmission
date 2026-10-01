// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <ctime> // time_t
#include <string>

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

} // namespace tr::app
