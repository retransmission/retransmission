// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/tr-strbuf.h>

#include "libtransmission-app/trash.h"

namespace tr::app
{
namespace
{

// Deletes `path` and, if it is a folder, everything inside it.
// Symlinks are deleted, not followed.
// It deletes all it can and reports the first failure.
bool remove_tree(std::string_view const path, tr_error* const error)
{
    auto ok = true;

    if (auto const info = tr_sys_path_get_info(path, TR_SYS_PATH_NO_FOLLOW); info && info->isFolder()) {
        for (auto const& name : tr_sys_dir_get_files(path, tr_basename_accept_all)) {
            ok = remove_tree(tr_pathbuf{ path, '/', name }, ok ? error : nullptr) && ok;
        }
    }

    return ok && tr_sys_path_remove(path, error);
}

} // namespace

bool trash_or_remove(std::string_view const path, tr_error* const error, TrashFunc const& trash)
{
    auto why = std::string{};
    if (trash(path, why)) {
        return true;
    }

    tr_logAddWarn(
        fmt::format(
            "Couldn't move '{path}' to the trash can, so deleting it: {error}",
            fmt::arg("path", path),
            fmt::arg("error", why)));

    auto failure = tr_error{};
    if (remove_tree(path, &failure)) {
        return true;
    }

    if (error != nullptr) {
        *error = std::move(failure);
    } else {
        tr_logAddWarn(
            fmt::format(
                "Couldn't remove '{path}': {error} ({error_code})",
                fmt::arg("path", path),
                fmt::arg("error", failure.message()),
                fmt::arg("error_code", failure.code())));
    }

    return false;
}

} // namespace tr::app
