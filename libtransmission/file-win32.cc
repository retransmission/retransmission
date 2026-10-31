// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cctype> // for isalpha()
#include <cstring>
#include <ctime>
#include <iterator> // for std::back_inserter
#include <optional>
#include <string>
#include <string_view>

#include <shlobj.h> /* SHCreateDirectoryEx() */
#include <winioctl.h> /* FSCTL_SET_SPARSE */

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h" /* tr_rand_int() */
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

using namespace std::literals;

namespace
{
auto constexpr NativeLocalPathPrefix = L"\\\\?\\"sv;
auto constexpr NativeUncPathPrefix = L"\\\\?\\UNC\\"sv;
auto constexpr DeltaEpochInSecs = INT64_C(11644473600);

void set_system_error(tr_error* error, DWORD code)
{
    if (error != nullptr) {
        auto const message = tr_win32_format_message(code);
        error->set(code, !std::empty(message) ? message : fmt::format("Unknown error: {:#08x}", code));
    }
}

[[nodiscard]] constexpr time_t filetime_to_unix_time(FILETIME const& t) noexcept
{
    // FILETIME counts 100 ns ticks since 1601, so pre-1970 times are negative
    auto const ticks = static_cast<int64_t>((uint64_t{ t.dwHighDateTime } << 32U) | t.dwLowDateTime);
    return (ticks / 10'000'000) - DeltaEpochInSecs;
}

constexpr bool to_bool(BOOL value) noexcept
{
    return value != FALSE;
}

[[nodiscard]] constexpr auto stat_to_sys_path_info(
    DWORD const attributes,
    DWORD const size_low,
    DWORD const size_high,
    FILETIME const& mtime) noexcept
{
    auto info = tr_sys_path_info{};

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        info.type = TR_SYS_PATH_IS_DIRECTORY;
    } else if ((attributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_VIRTUAL)) == 0) {
        info.type = TR_SYS_PATH_IS_FILE;
    } else {
        info.type = TR_SYS_PATH_IS_OTHER;
    }

    info.size = size_high;
    info.size <<= 32;
    info.size |= size_low;

    info.last_modified_at = filetime_to_unix_time(mtime);

    return info;
}

auto constexpr Slashes = R"(\/)"sv;

constexpr bool is_slash(char c)
{
    return Slashes.contains(c);
}

constexpr bool is_unc_path(std::string_view path)
{
    return std::size(path) >= 2 && is_slash(path[0]) && path[1] == path[0];
}

bool is_valid_path(std::string_view path)
{
    if (is_unc_path(path)) {
        if (std::size(path) <= 2 || isalnum(static_cast<unsigned char>(path[2])) == 0) {
            return false;
        }
    } else if (auto const pos = path.find(':'); pos != std::string_view::npos) {
        if (pos != 1 || isalpha(static_cast<unsigned char>(path[0])) == 0) {
            return false;
        }

        path.remove_prefix(2);
    }

    return path.find_first_of(R"(<>:"|?*)"sv) == std::string_view::npos;
}

[[nodiscard]] auto path_to_fixed_native_path(std::string_view const path)
{
    auto wide_path = tr_win32_utf8_to_native(path);

    // convert '/' to '\'
    static auto constexpr Convert = [](wchar_t wch) {
        return wch == L'/' ? L'\\' : wch;
    };
    std::ranges::transform(wide_path, std::begin(wide_path), Convert);

    // squash multiple consecutive separators into one to avoid ERROR_INVALID_NAME
    static auto constexpr Equal = [](wchar_t a, wchar_t b) {
        return a == b && a == L'\\';
    };
    auto const tmp = wide_path;
    wide_path.clear();
    std::ranges::unique_copy(tmp, std::back_inserter(wide_path), Equal);

    return wide_path;
}

/* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
   https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx for more info */
[[nodiscard]] auto path_to_native_path(std::string_view path)
{
    if (is_unc_path(path)) {
        // UNC path: "\\server\share" -> "\\?\UNC\server\share"
        path.remove_prefix(2); // remove path's UNC prefix slashes
        auto wide_path = path_to_fixed_native_path(path);
        wide_path.insert(0, NativeUncPathPrefix);
        return wide_path;
    }

    if (!tr_sys_path_is_relative(path)) {
        // local path: "C:" -> "\\?\C:"
        auto wide_path = path_to_fixed_native_path(path);
        wide_path.insert(0, NativeLocalPathPrefix);
        return wide_path;
    }

    return path_to_fixed_native_path(path);
}

[[nodiscard]] tr_sys_file_t open_file(
    std::string_view const path,
    DWORD const access,
    DWORD const disposition,
    DWORD const flags,
    tr_error* error)
{
    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path)) {
        ret = CreateFileW(
            wide_path.c_str(),
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            disposition,
            flags,
            nullptr);
    }

    if (ret == TR_BAD_SYS_FILE) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

[[nodiscard]] bool create_dir(
    std::string_view const path,
    int const flags,
    int /*permissions*/,
    bool const okay_if_exists,
    tr_error* error)
{
    bool ret = false;
    DWORD error_code = ERROR_SUCCESS;
    auto const wide_path = path_to_native_path(path);

    // already exists (no-op)
    if (auto const info = tr_sys_path_get_info(path); info && info->isFolder()) {
        return true;
    }

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0) {
        error_code = SHCreateDirectoryExW(nullptr, wide_path.c_str(), nullptr);
        ret = error_code == ERROR_SUCCESS;
    } else {
        ret = to_bool(CreateDirectoryW(wide_path.c_str(), nullptr));

        if (!ret) {
            error_code = GetLastError();
        }
    }

    if (!ret && error_code == ERROR_ALREADY_EXISTS && okay_if_exists) {
        DWORD const attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            ret = true;
        }
    }

    if (!ret) {
        set_system_error(error, error_code);
    }

    return ret;
}

template<typename CallbackT>
void create_temp_path(char* path_template, CallbackT const& callback, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    auto path = std::string{ path_template };
    auto const path_size = std::size(path);

    TR_ASSERT(path_size > 0);

    auto local_error = tr_error{};

    for (int attempt = 0; attempt < 100; ++attempt) {
        size_t i = path_size;

        while (i > 0 && path_template[i - 1] == 'X') {
            static auto constexpr Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"sv;
            path[i - 1] = Chars[tr_rand_int(std::size(Chars))];
            --i;
        }

        TR_ASSERT(path_size >= i + 6);

        local_error = {};

        callback(path.c_str(), &local_error);

        if (!local_error) {
            break;
        }
    }

    if (!local_error) {
        std::copy_n(std::begin(path), path_size, path_template);
    } else if (error != nullptr) {
        *error = std::move(local_error);
    }
}

[[nodiscard]] std::optional<tr_sys_path_info> tr_sys_file_get_info_(tr_sys_file_t const handle, tr_error* const error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    auto attributes = BY_HANDLE_FILE_INFORMATION{};
    if (to_bool(GetFileInformationByHandle(handle, &attributes))) {
        return stat_to_sys_path_info(
            attributes.dwFileAttributes,
            attributes.nFileSizeLow,
            attributes.nFileSizeHigh,
            attributes.ftLastWriteTime);
    }

    set_system_error(error, GetLastError());
    return {};
}

// Symlinks and junctions are links; other reparse points, such as
// cloud-file placeholders, are not. An unreadable tag counts as a link,
// so a junction is never followed.
[[nodiscard]] bool is_link_reparse_point(wchar_t const* const wide_path)
{
    auto data = WIN32_FIND_DATAW{};
    auto const find = FindFirstFileW(wide_path, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return true;
    }

    FindClose(find);

    // dwReserved0 holds the reparse tag
    return data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
}
} // namespace

std::optional<tr_sys_path_info> tr_sys_path_get_info(std::string_view const path, int const flags, tr_error* const error)
{
    auto const wide_path = path_to_native_path(path);

    auto attributes = WIN32_FILE_ATTRIBUTE_DATA{};
    if (std::empty(wide_path) || !to_bool(GetFileAttributesExW(wide_path.c_str(), GetFileExInfoStandard, &attributes))) {
        set_system_error(error, GetLastError());
        return {};
    }

    auto const is_reparse_point = (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    if (!is_reparse_point || (flags & TR_SYS_PATH_NO_FOLLOW) != 0) {
        auto info = stat_to_sys_path_info(
            attributes.dwFileAttributes,
            attributes.nFileSizeLow,
            attributes.nFileSizeHigh,
            attributes.ftLastWriteTime);

        // A link reads as OTHER, as lstat() reports a symlink,
        // so a junction never reads as a folder.
        if (is_reparse_point && is_link_reparse_point(wide_path.c_str())) {
            info.type = TR_SYS_PATH_IS_OTHER;
        }

        return info;
    }

    // Opening a link opens its target.
    if (auto const handle = CreateFileW(wide_path.c_str(), 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        handle != INVALID_HANDLE_VALUE) {
        auto ret = tr_sys_file_get_info_(handle, error);
        CloseHandle(handle);
        return ret;
    }

    set_system_error(error, GetLastError());
    return {};
}

std::string_view tr_sys_path_basename(std::string_view path, tr_error* error)
{
    if (std::empty(path)) {
        return "."sv;
    }

    if (!is_valid_path(path)) {
        set_system_error(error, ERROR_PATH_NOT_FOUND);
        return {};
    }

    // Remove all trailing slashes.
    // If nothing is left, return "/"
    if (auto const pos = path.find_last_not_of(Slashes); pos != std::string_view::npos) {
        path = path.substr(0, pos + 1);
    } else // all slashes
    {
        return "/"sv;
    }

    if (auto const pos = path.find_last_of("\\/:"); pos != std::string_view::npos) {
        path.remove_prefix(pos + 1);
    }

    return !std::empty(path) ? path : "/"sv;
}

namespace
{
[[nodiscard]] bool isWindowsDeviceRoot(char ch) noexcept
{
    return isalpha(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] constexpr bool isPathSeparator(char ch) noexcept
{
    return ch == '/' || ch == '\\';
}
} // namespace

// This function is adapted from Node.js's path.win32.dirname() function,
// which is copyrighted by Joyent, Inc. and other Node contributors
// and is distributed under MIT (SPDX:MIT) license.
std::string_view tr_sys_path_dirname(std::string_view path)
{
    auto const len = std::size(path);

    if (len == 0) {
        return "."sv;
    }

    if (len == 1) {
        return isPathSeparator(path[0]) ? path : "."sv;
    }

    auto root_end = std::string_view::npos;
    auto offset = std::string_view::size_type{ 0 };

    // Try to match a root
    if (isPathSeparator(path[0])) {
        // Possible UNC root

        root_end = offset = 1;

        if (isPathSeparator(path[1])) {
            // Matched double path separator at beginning
            std::string_view::size_type j = 2;
            std::string_view::size_type last = j;
            // Match 1 or more non-path separators
            while (j < len && !isPathSeparator(path[j])) {
                j++;
            }
            if (j < len && j != last) {
                // Matched!
                last = j;
                // Match 1 or more path separators
                while (j < len && isPathSeparator(path[j])) {
                    j++;
                }
                if (j < len && j != last) {
                    // Matched!
                    last = j;
                    // Match 1 or more non-path separators
                    while (j < len && !isPathSeparator(path[j])) {
                        j++;
                    }
                    if (j == len) {
                        // We matched a UNC root only
                        return path;
                    }
                    if (j != last) {
                        // We matched a UNC root with leftovers

                        // Offset by 1 to include the separator after the UNC root to
                        // treat it as a "normal root" on top of a (UNC) root
                        root_end = offset = j + 1;
                    }
                }
            }
        }
        // Possible device root
    } else if (isWindowsDeviceRoot(path[0]) && path[1] == ':') {
        root_end = len > 2 && isPathSeparator(path[2]) ? 3 : 2;
        offset = root_end;
    }

    auto end = std::string_view::npos;
    auto matched_slash = true;
    for (std::string_view::size_type i = len - 1; i >= offset; --i) {
        if (isPathSeparator(path[i])) {
            if (!matched_slash) {
                end = i;
                break;
            }
        } else {
            // We saw the first non-path separator
            matched_slash = false;
        }
        if (i <= offset) {
            break;
        }
    }

    if (end == std::string_view::npos) {
        if (root_end == std::string_view::npos) {
            return "."sv;
        }

        end = root_end;
    }
    return path.substr(0, end);
}

bool tr_sys_path_rename(std::string_view const src_path, std::string_view const dst_path, tr_error* error)
{
    bool ret = false;
    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);

    if (!std::empty(wide_src_path) && !std::empty(wide_dst_path)) {
        DWORD flags = MOVEFILE_REPLACE_EXISTING;

        if (auto const src_attributes = GetFileAttributesW(wide_src_path.c_str());
            src_attributes != INVALID_FILE_ATTRIBUTES && (src_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            flags = 0;
        } else if (
            auto const dst_attributes = GetFileAttributesW(wide_dst_path.c_str());
            dst_attributes != INVALID_FILE_ATTRIBUTES && (dst_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            flags = 0;
        }

        ret = to_bool(MoveFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), flags));
    }

    if (!ret) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_path_copy(std::string_view const src_path, std::string_view const dst_path, tr_error* error)
{
    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);
    if (std::empty(wide_src_path) || std::empty(wide_dst_path)) {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    auto cancel = BOOL{ FALSE };
    DWORD const flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION | COPY_FILE_FAIL_IF_EXISTS;
    if (!to_bool(CopyFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), nullptr, nullptr, &cancel, flags))) {
        set_system_error(error, GetLastError());
        return false;
    }

    return true;
}

bool tr_sys_path_remove(std::string_view const path, tr_error* error)
{
    bool ret = false;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path)) {
        DWORD const attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                ret = to_bool(RemoveDirectoryW(wide_path.c_str()));
            } else {
                ret = to_bool(DeleteFileW(wide_path.c_str()));
            }
        }
    }

    if (!ret) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

char* tr_sys_path_native_separators(char* path)
{
    if (path == nullptr) {
        return nullptr;
    }

    for (char* slash = strchr(path, '/'); slash != nullptr; slash = strchr(slash, '/')) {
        *slash = '\\';
    }

    return path;
}

tr_sys_file_t tr_sys_file_open(std::string_view const path, int const flags, int /*permissions*/, tr_error* error)
{
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    DWORD native_access = 0;
    DWORD native_disposition = OPEN_EXISTING;
    DWORD native_flags = FILE_ATTRIBUTE_NORMAL;

    if ((flags & TR_SYS_FILE_READ) != 0) {
        native_access |= GENERIC_READ;
    }

    if ((flags & TR_SYS_FILE_WRITE) != 0) {
        native_access |= GENERIC_WRITE;
    }

    if ((flags & TR_SYS_FILE_CREATE) != 0) {
        native_disposition = (flags & TR_SYS_FILE_TRUNCATE) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
    } else if ((flags & TR_SYS_FILE_TRUNCATE) != 0) {
        native_disposition = TRUNCATE_EXISTING;
    }

    if ((flags & TR_SYS_FILE_SEQUENTIAL) != 0) {
        native_flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }

    return open_file(path, native_access, native_disposition, native_flags, error);
}

tr_sys_file_t tr_sys_file_open_temp(char* path_template, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    create_temp_path(
        path_template,
        [&ret](char const* path, tr_error* error) {
            ret = open_file(path, GENERIC_READ | GENERIC_WRITE, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, error);
        },
        error);

    return ret;
}

bool tr_sys_file_close(tr_sys_file_t handle, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool const ret = to_bool(CloseHandle(handle));

    if (!ret) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_read(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t* bytes_read, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD) {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_read = 0;

    if (to_bool(ReadFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_read, nullptr))) {
        if (bytes_read != nullptr) {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    } else {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_read_at(
    tr_sys_file_t handle,
    void* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_read,
    tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD) {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    auto overlapped = OVERLAPPED{};
    DWORD my_bytes_read = 0;

    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.hEvent = nullptr;

    if (to_bool(ReadFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_read, &overlapped))) {
        if (bytes_read != nullptr) {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    } else {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_write(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t* bytes_written, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD) {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_written = 0;

    if (to_bool(WriteFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_written, nullptr))) {
        if (bytes_written != nullptr) {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    } else {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_write_at(
    tr_sys_file_t handle,
    void const* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_written,
    tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD) {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    auto overlapped = OVERLAPPED{};
    DWORD my_bytes_written = 0;

    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.hEvent = nullptr;

    if (to_bool(WriteFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_written, &overlapped))) {
        if (bytes_written != nullptr) {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    } else {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_truncate(tr_sys_file_t handle, uint64_t size, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    FILE_END_OF_FILE_INFO info;
    info.EndOfFile.QuadPart = static_cast<LONGLONG>(size);

    bool const ret = to_bool(SetFileInformationByHandle(handle, FileEndOfFileInfo, &info, sizeof(info)));

    if (!ret) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_preallocate(tr_sys_file_t handle, uint64_t size, int flags, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) != 0) {
        DWORD tmp = 0;

        if (!to_bool(DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr))) {
            set_system_error(error, GetLastError());
            return false;
        }
    }

    return tr_sys_file_truncate(handle, size, error);
}

bool tr_sys_file_lock(tr_sys_file_t handle, int operation, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT((operation & ~(TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB)) == 0);
    TR_ASSERT(!!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) == 1);

    auto overlapped = OVERLAPPED{};

    DWORD native_flags = 0;

    if ((operation & TR_SYS_FILE_LOCK_EX) != 0) {
        native_flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }

    if ((operation & TR_SYS_FILE_LOCK_NB) != 0) {
        native_flags |= LOCKFILE_FAIL_IMMEDIATELY;
    }

    bool const ret = LockFileEx(handle, native_flags, 0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;

    if (!ret) {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_lock_error_is_contended(tr_error const& error) noexcept
{
    return error.code() == ERROR_LOCK_VIOLATION;
}

bool tr_sys_dir_create_temp(char* path_template, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    bool ret = false;

    create_temp_path(
        path_template,
        [&ret](char const* path, tr_error* error) { ret = create_dir(path, 0, 0, false, error); },
        error);

    return ret;
}
