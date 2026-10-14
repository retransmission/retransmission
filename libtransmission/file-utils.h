// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <iterator> // for std::data(), std::size()
#include <string_view>
#include <vector>

#include "libtransmission/tr-strbuf.h"

struct tr_error;

/**
 * @addtogroup utils Utilities
 * @{
 */

bool tr_file_read(std::string_view filename, std::vector<char>& contents, tr_error* error = nullptr);

/**
 * Tries to move a file by renaming, and [optionally] if that fails, by copying.
 * A copy goes through tr_file_copy_atomic(), so `newpath` never holds a partial file.
 *
 * Creates the destination directory if it doesn't exist.
 */
bool tr_file_move(std::string_view oldpath, std::string_view newpath, bool allow_copy, tr_error* error = nullptr);

/**
 * Copies a file to a temporary name in `newpath`'s dir, flushes it to disk, then renames it into place.
 * `newpath` keeps its old state until the copy is whole,
 * even if the process is killed or the machine loses power mid-copy.
 */
[[nodiscard]] bool tr_file_copy_atomic(std::string_view oldpath, std::string_view newpath, tr_error* error = nullptr);

// Every tr_file_copy_atomic() temporary name starts with this.
// An interrupted copy leaves such a file behind until the same copy is retried.
inline constexpr auto TrFileCopyPrefix = std::string_view{ ".tr-copy-" };

bool tr_file_save(std::string_view filename, std::string_view contents, tr_error* error = nullptr);

template<typename ContiguousRange>
constexpr auto tr_file_save(std::string_view filename, ContiguousRange const& x, tr_error* error = nullptr)
{
    return tr_file_save(filename, std::string_view{ std::data(x), std::size(x) }, error);
}

namespace tr::detail
{
// Where tr_file_copy_atomic() stages its copy of `newpath`.
// Public for tests; everything else calls tr_file_copy_atomic().
[[nodiscard]] tr_pathbuf file_copy_temp_path(std::string_view newpath);
} // namespace tr::detail
