// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <string>
#include <string_view>

struct tr_error;

namespace tr::app
{

// Moves a file or folder to the trash can.
// Returns false when the trash can refuses, with the reason in `why`.
using TrashFunc = std::function<bool(std::string_view path, std::string& why)>;

// Moves `path` to the trash can with `trash`, or deletes it when the trash can refuses.
// It fits the remove func that tr_sessionInit() takes.
//
// A refused folder is deleted with everything inside it, without following symlinks.
// That way tr_torrent_files::remove()'s bottom-up pass finds nothing left inside it,
// so a refused folder costs one trash try and one warning.
//
// A failed delete goes into `error`, or into the log when `error` is null.
bool trash_or_remove(std::string_view path, tr_error* error, TrashFunc const& trash);

} // namespace tr::app
