// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint8_t
#include <iterator> // for std::data(), std::size()
#include <optional>
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
 * Moves a file by renaming it.
 *
 * Creates the destination directory if it doesn't exist.
 */
[[nodiscard]] bool tr_file_move(std::string_view oldpath, std::string_view newpath, tr_error* error = nullptr);

enum class tr_file_moved : uint8_t { Renamed, Copied };

/**
 * Moves a file by renaming it, or copies it with tr_file_copy_atomic() if renaming fails.
 * A copy leaves `oldpath` in place for the caller to remove.
 *
 * Creates the destination directory if it doesn't exist.
 */
[[nodiscard]] std::optional<tr_file_moved> tr_file_rename_or_copy(
    std::string_view oldpath,
    std::string_view newpath,
    tr_error* error = nullptr);

/**
 * Copies a file to a temporary name in `newpath`'s dir, then renames it into place.
 * `newpath` keeps its old state until the copy is whole, even if the process is killed mid-copy.
 */
[[nodiscard]] bool tr_file_copy_atomic(std::string_view oldpath, std::string_view newpath, tr_error* error = nullptr);

// Every tr_file_copy_atomic() temporary name starts with this,
// so cleanup can recognize one that an interrupted copy left behind.
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
