// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <string>

// What a slow quit is waiting for, in two lines. `detail` is empty
// unless it has a number that moves.
struct ShutdownStatus {
    std::string headline;
    std::string detail;
};

[[nodiscard]] ShutdownStatus make_shutdown_status(tr_disk_activity const& activity);
