// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "Torrent.h"
#include "UserMetaType.h"

// The Torrent properties that can affect ShowMode filtering.
// When one of these changes, it's time to refilter.
extern Torrent::fields_t const ShowModeFields;

bool should_show_torrent(Torrent const& torrent, ShowMode mode);
