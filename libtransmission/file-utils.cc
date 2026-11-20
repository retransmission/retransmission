// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/file-utils.h"

#include <cerrno> // EINVAL, EISDIR
#include <cstdint> // uint64_t
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h> /* umask() */
#endif

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/tr-strbuf.h"

using namespace std::literals;

bool tr_file_read(std::string_view filename, std::vector<char>& contents, tr_error* error)
{
    auto const szfilename = tr_pathbuf{ filename };

    /* try to stat the file */
    auto local_error = tr_error{};
    if (error == nullptr) {
        error = &local_error;
    }

    auto const info = tr_sys_path_get_info(szfilename, 0, error);
    if (!info) {
        tr_logAddError(
            fmt::format(
                "Couldn't read '{path}': {error} ({error_code})",
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        return false;
    }

    if (!info->isFile()) {
        tr_logAddError(fmt::format("Couldn't read '{path}': Not a regular file", fmt::arg("path", filename)));
        error->set(EISDIR, "Not a regular file"sv);
        return false;
    }

    /* Load the torrent file into our buffer */
    auto const fd = tr_sys_file_open(szfilename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, error);
    if (fd == TR_BAD_SYS_FILE) {
        tr_logAddError(
            fmt::format(
                "Couldn't read '{path}': {error} ({error_code})",
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        return false;
    }

    contents.resize(info->size);
    if (!tr_sys_file_read(fd, std::data(contents), info->size, nullptr, error)) {
        tr_logAddError(
            fmt::format(
                "Couldn't read '{path}': {error} ({error_code})",
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        tr_sys_file_close(fd);
        return false;
    }

    tr_sys_file_close(fd);
    return true;
}

bool tr_file_save(std::string_view filename, std::string_view contents, tr_error* error)
{
    // follow symlinks to find the "real" file, to make sure the temporary
    // we build with tr_sys_file_open_temp() is created on the right partition
    if (auto const realname = tr_sys_path_resolve(filename); !std::empty(realname) && realname != filename) {
        return tr_file_save(realname, contents, error);
    }

    // Write it to a temp file first.
    // This is a safeguard against edge cases, e.g. disk full, crash while writing, etc.
    auto tmp = tr_pathbuf{ filename, ".tmp.XXXXXX"sv };
    auto const fd = tr_sys_file_open_temp(std::data(tmp), error);
    if (fd == TR_BAD_SYS_FILE) {
        return false;
    }
#ifndef _WIN32
    // set file mode per settings umask()
    {
        auto const val = ::umask(0);
        ::umask(val);
        fchmod(fd, 0666 & ~val);
    }
#endif

    // Save the contents. This might take >1 pass.
    auto ok = true;
    while (!std::empty(contents)) {
        auto n_written = uint64_t{};
        if (!tr_sys_file_write(fd, std::data(contents), std::size(contents), &n_written, error)) {
            ok = false;
            break;
        }
        contents.remove_prefix(n_written);
    }

    // If we saved it to disk successfully, move it from '.tmp' to the correct filename
    if (!tr_sys_file_close(fd, error) || !ok || !tr_sys_path_rename(tmp, tr_pathbuf{ filename }, error)) {
        return false;
    }

    tr_logAddTrace(fmt::format("Saved '{}'", filename));
    return true;
}

namespace
{
// Checks that `oldpath` is a file, and creates the dir that `newpath` goes in.
[[nodiscard]] bool prepare_move(std::string_view const oldpath, std::string_view const newpath, tr_error* const error)
{
    auto const info = tr_sys_path_get_info(oldpath, 0, error);
    if (!info) {
        error->prefix_message("Unable to get information on old file: ");
        return false;
    }
    if (!info->isFile()) {
        error->set(EINVAL, "Old path does not point to a file."sv);
        return false;
    }

    if (!tr_sys_dir_create(tr_sys_path_dirname(newpath), TR_SYS_DIR_CREATE_PARENTS, 0777, error)) {
        error->prefix_message("Unable to create directory for new file: ");
        return false;
    }

    return true;
}
} // namespace

bool tr_file_move(std::string_view const oldpath, std::string_view const newpath, tr_error* error)
{
    auto local_error = tr_error{};
    if (error == nullptr) {
        error = &local_error;
    }

    if (!prepare_move(oldpath, newpath, error)) {
        return false;
    }

    if (!tr_sys_path_rename(oldpath, newpath, error)) {
        error->prefix_message("Unable to move file: ");
        return false;
    }

    return true;
}

namespace
{
[[nodiscard]] std::optional<tr_file_moved> rename_or_copy(
    std::string_view const oldpath,
    std::string_view const newpath,
    tr_error* const error,
    std::function<void(uint64_t bytes_copied)> const& on_copied)
{
    if (!prepare_move(oldpath, newpath, error)) {
        return {};
    }

    // Renaming fails across filesystems.
    if (tr_sys_path_rename(oldpath, newpath)) {
        return tr_file_moved::Renamed;
    }

    if (!tr_file_copy_atomic(oldpath, newpath, error, on_copied)) {
        error->prefix_message("Unable to copy: ");
        return {};
    }

    return tr_file_moved::Copied;
}
} // namespace

std::optional<tr_file_moved> tr_file_rename_or_copy(
    std::string_view const oldpath,
    std::string_view const newpath,
    tr_error* error,
    std::function<void(uint64_t bytes_copied)> const& on_copied)
{
    auto local_error = tr_error{};
    if (error == nullptr) {
        error = &local_error;
    }

    auto how = rename_or_copy(oldpath, newpath, error, on_copied);

    // Clearing another torrent's old location removes empty folders and copy
    // temp files, and it can catch ours partway through. Start over once.
    if (!how && error->is_enoent() && tr_sys_path_exists(oldpath)) {
        *error = {};
        how = rename_or_copy(oldpath, newpath, error, on_copied);
    }

    return how;
}

bool tr_file_copy_atomic(
    std::string_view const oldpath,
    std::string_view const newpath,
    tr_error* const error,
    std::function<void(uint64_t bytes_copied)> const& on_copied)
{
    auto const tmp = tr::detail::file_copy_temp_path(newpath);

    // A killed copy may have left it behind.
    tr_sys_path_remove(tmp);

    if (tr_sys_path_copy(oldpath, tmp, error, on_copied) && tr_sys_path_rename(tmp, newpath, error)) {
        return true;
    }

    tr_sys_path_remove(tmp);
    return false;
}

tr_pathbuf tr::detail::file_copy_temp_path(std::string_view const newpath)
{
    // Naming it after `newpath` lets a retry replace what a killed copy left behind.
    // Hashing the name keeps its length fixed, so it fits wherever `newpath` does.
    auto const hash = tr_sha1_string{ tr_sha1::digest(tr_sys_path_basename(newpath)) };
    return tr_pathbuf{ tr_sys_path_dirname(newpath), '/', TrFileCopyPrefix, hash.sv() };
}
