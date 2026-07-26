// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/utils.h" // tr_lib_init()

#include "libtransmission-app/app.h"

namespace tr::app
{
void init()
{
    tr_lib_init();
    tr_locale_set_global("");
}
} // namespace tr::app
