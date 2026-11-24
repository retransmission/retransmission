// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __HAIKU__
#include <limits.h> /* PATH_MAX */
#endif

#ifdef _WIN32
#include <process.h> /* _beginthreadex(), _endthreadex() */
#include <windows.h>
#include <shlobj.h> /* SHGetKnownFolderPath(), FOLDERID_... */
#ifdef small // workaround name collision between libsmall and rpcndr.h
#undef small
#endif
#else
#include <pwd.h>
#include <unistd.h> /* getuid() */
#endif

#ifdef BUILD_MAC_CLIENT
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef __HAIKU__
#include <FindDirectory.h>
#endif

#include <fmt/format.h>

#include "libtransmission/transmission.h"

#include "libtransmission/env.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/macros.h"
#include "libtransmission/platform.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{
[[nodiscard]] std::optional<uint64_t> physical_memory() noexcept
{
#ifdef _WIN32
    auto status = MEMORYSTATUSEX{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0) {
        return uint64_t{ status.ullTotalPhys };
    }
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    auto const n_pages = sysconf(_SC_PHYS_PAGES);
    auto const page_size = sysconf(_SC_PAGESIZE);
    if (n_pages > 0 && page_size > 0) {
        return static_cast<uint64_t>(n_pages) * static_cast<uint64_t>(page_size);
    }
#endif

    return {};
}

#ifdef __linux__
// The smaller of the values that are there.
[[nodiscard]] constexpr std::optional<uint64_t> smaller(
    std::optional<uint64_t> const a,
    std::optional<uint64_t> const b) noexcept
{
    if (!a || !b) {
        return a ? a : b;
    }

    return std::min(*a, *b);
}

// The number a cgroup limit file holds. No value if the file is missing
// or holds none.
[[nodiscard]] std::optional<uint64_t> read_cgroup_limit(std::string const& filename)
{
    auto in = std::ifstream{ filename };
    auto text = std::string{};
    std::getline(in, text);
    return tr_num_parse<uint64_t>(text);
}
#endif

#ifdef _WIN32
std::string win32_get_known_folder_ex(REFKNOWNFOLDERID folder_id, DWORD flags)
{
    if (PWSTR path = nullptr; SHGetKnownFolderPath(folder_id, flags | KF_FLAG_DONT_UNEXPAND, nullptr, &path) == S_OK) {
        auto ret = tr_win32_native_to_utf8(path);
        CoTaskMemFree(path);
        return ret;
    }

    return {};
}

auto win32_get_known_folder(REFKNOWNFOLDERID folder_id)
{
    return win32_get_known_folder_ex(folder_id, KF_FLAG_DONT_VERIFY);
}
#endif

[[nodiscard]] std::string xdgConfigHome()
{
    if (auto dir = tr_env_get_string("XDG_CONFIG_HOME"sv); !std::empty(dir)) {
        return dir;
    }

    return fmt::format("{:s}/.config"sv, tr::platform::get_home_dir());
}

[[nodiscard]] std::string getXdgEntryFromUserDirs(std::string_view key)
{
    auto content = std::vector<char>{};
    if (auto const filename = fmt::format("{:s}/{:s}"sv, xdgConfigHome(), "user-dirs.dirs"sv);
        !tr_sys_path_exists(filename) || !tr_file_read(filename, content) || std::empty(content)) {
        return {};
    }

    // search for key="val" and extract val
    auto const search = fmt::format("{:s}=\"", key);
    auto const [key_first, key_last] = std::ranges::search(content, search);
    if (key_first == key_last) {
        return {};
    }
    auto const end = std::find(key_last, content.end(), '"');
    if (end == content.end()) {
        return {};
    }
    auto val = std::string{ key_last, end };

    // if val contains "$HOME", replace that with tr::platform::get_home_dir()
    auto constexpr Home = "$HOME"sv;
    if (auto const [home_first, home_last] = std::ranges::search(val, Home); home_first != home_last) {
        val.replace(home_first, home_last, tr::platform::get_home_dir());
    }

    return val;
}

[[nodiscard]] bool isWebClientDir(std::string_view path)
{
    auto const filename = tr_pathbuf{ path, '/', "index.html"sv };
    bool const found = tr_sys_path_exists(filename);
    tr_logAddTrace(fmt::format("Searching for web interface file '{:s}'", filename));
    return found;
}
} // namespace

// ---

namespace tr::platform
{
#ifdef __linux__
std::optional<uint64_t> detail::cgroup_memory_limit(std::string_view proc_self_cgroup, std::string_view const cgroup_root)
{
    auto limit = std::optional<uint64_t>{};

    // Each line is "hierarchy-id:controllers:path".
    auto line = std::string_view{};
    while (tr_strv_sep(&proc_self_cgroup, &line, '\n')) {
        auto const id = tr_strv_sep(&line, ':');
        auto const controllers = tr_strv_sep(&line, ':');
        auto path = line;

        // v2 has one hierarchy, with id 0 and no controllers named.
        // v1 mounts the memory controller's hierarchy in a dir of its own.
        auto const is_v2 = id == "0"sv && std::empty(controllers);
        if (!is_v2 && !fmt::format(",{:s},", controllers).contains(",memory,"sv)) {
            continue;
        }
        auto const dir = is_v2 ? ""sv : "/memory"sv;
        auto const filename = is_v2 ? "memory.max"sv : "memory.limit_in_bytes"sv;

        // A path that climbs out of the mount names cgroups we can't see.
        if (!path.starts_with('/') || path.contains("/../"sv) || path.ends_with("/.."sv)) {
            continue;
        }

        while (path.ends_with('/')) {
            path.remove_suffix(1U);
        }

        // An ancestor's limit binds too. A container can see its own
        // cgroup mounted at the root while `path` still names it from the
        // host's root, so dirs that don't exist are skipped.
        for (;;) {
            limit = smaller(limit, read_cgroup_limit(fmt::format("{:s}{:s}{:s}/{:s}", cgroup_root, dir, path, filename)));
            if (std::empty(path)) {
                break;
            }
            path = path.substr(0, path.rfind('/'));
        }
    }

    return limit;
}
#endif

std::optional<uint64_t> get_memory_size()
{
#ifdef __linux__
    auto in = std::ifstream{ "/proc/self/cgroup" };
    auto const text = std::string{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
    return smaller(physical_memory(), detail::cgroup_memory_limit(text, "/sys/fs/cgroup"sv));
#else
    return physical_memory();
#endif
}

std::string get_default_config_dir(std::string_view appname)
{
    if (std::empty(appname)) {
        appname = TR_PROJ_APPNAME_CAPITALIZED;
    }

    if (auto dir = tr_env_get_string("TRANSMISSION_HOME"sv); !std::empty(dir)) {
        return dir;
    }

#ifdef __APPLE__

    return fmt::format("{:s}/Library/Application Support/{:s}"sv, tr::platform::get_home_dir(), appname);

#elif defined(_WIN32)

    auto const appdata = win32_get_known_folder(FOLDERID_LocalAppData);
    return fmt::format("{:s}/{:s}"sv, appdata, appname);

#elif defined(__HAIKU__)

    char buf[PATH_MAX];
    find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, buf, sizeof(buf));
    return fmt::format("{:s}/{:s}"sv, buf, appname);

#else

    return fmt::format("{:s}/{:s}"sv, xdgConfigHome(), appname);

#endif
}

std::string get_download_dir()
{
    if (auto dir = getXdgEntryFromUserDirs("XDG_DOWNLOAD_DIR"sv); !std::empty(dir)) {
        return dir;
    }

#ifdef _WIN32
    if (auto dir = win32_get_known_folder(FOLDERID_Downloads); !std::empty(dir)) {
        return dir;
    }
#endif

#ifdef __HAIKU__
    return fmt::format("{:s}/Desktop"sv, tr::platform::get_home_dir());
#endif

    return fmt::format("{:s}/Downloads"sv, tr::platform::get_home_dir());
}

std::string get_home_dir()
{
    if (auto dir = tr_env_get_string("HOME"sv); !std::empty(dir)) {
        return dir;
    }

#ifdef _WIN32

    if (auto dir = win32_get_known_folder(FOLDERID_Profile); !std::empty(dir)) {
        return dir;
    }

#else

    struct passwd pwent = {};
    struct passwd* pw = nullptr;
    auto buf = std::array<char, 4096>{};
    getpwuid_r(getuid(), &pwent, std::data(buf), std::size(buf), &pw);
    if (pw != nullptr) {
        return pw->pw_dir;
    }

#endif

    return {};
}
} // namespace tr::platform

// ---

std::string tr_getWebClientDir([[maybe_unused]] tr_session const* session)
{
    if (auto dir = tr_env_get_string("CLUTCH_HOME"sv); !std::empty(dir)) {
        return dir;
    }

    if (auto dir = tr_env_get_string("TRANSMISSION_WEB_HOME"sv); !std::empty(dir)) {
        return dir;
    }

#ifdef BUILD_MAC_CLIENT

    // look in the Application Support folder
    if (auto path = tr_pathbuf{ session->configDir(), "/public_html"sv }; isWebClientDir(path)) {
        return std::string{ path };
    }

    // look in the resource bundle
    auto app_url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    auto app_ref = CFURLCopyFileSystemPath(app_url, kCFURLPOSIXPathStyle);
    auto const buflen = CFStringGetMaximumSizeOfFileSystemRepresentation(app_ref);
    auto buf = std::vector<char>(buflen, '\0');
    bool const success = CFStringGetFileSystemRepresentation(app_ref, std::data(buf), std::size(buf));
    TR_ASSERT(success);
    CFRelease(app_url);
    CFRelease(app_ref);
    if (auto const path = tr_pathbuf{ std::string_view{ std::data(buf) }, "/Contents/Resources/public_html"sv };
        isWebClientDir(path)) {
        return std::string{ path };
    }

#elif defined(_WIN32)

    /* Generally, Web interface should be stored in a Web subdir of
     * calling executable dir. */

    static auto constexpr KnownFolderIds = std::to_array<KNOWNFOLDERID const* const>({
        &FOLDERID_LocalAppData,
        &FOLDERID_RoamingAppData,
        &FOLDERID_ProgramData,
    });

    for (auto const* const folder_id : KnownFolderIds) {
        auto const dir = win32_get_known_folder(*folder_id);

        if (auto const path = tr_pathbuf{ dir, "/" TR_PROJ_APPNAME_CAPITALIZED "/public_html"sv }; isWebClientDir(path)) {
            return std::string{ path };
        }
    }

    /* check calling module place */
    auto wide_module_path = std::array<wchar_t, MAX_PATH>{};
    GetModuleFileNameW(nullptr, std::data(wide_module_path), std::size(wide_module_path));
    auto const module_path = tr_win32_native_to_utf8({ std::data(wide_module_path) });
    if (auto const dir = tr_sys_path_dirname(module_path); !std::empty(dir)) {
        if (auto const path = tr_pathbuf{ dir, "/public_html"sv }; isWebClientDir(path)) {
            return std::string{ path };
        }
    }

#else // everyone else, follow the XDG spec

    auto candidates = std::list<std::string>{};

    /* XDG_DATA_HOME should be the first in the list of candidates */
    if (auto tmp = tr_env_get_string("XDG_DATA_HOME"sv); !std::empty(tmp)) {
        candidates.emplace_back(std::move(tmp));
    } else {
        candidates.emplace_back(fmt::format("{:s}/.local/share"sv, tr::platform::get_home_dir()));
    }

    /* XDG_DATA_DIRS are the backup directories */
    {
        char const* const pkg = PACKAGE_DATA_DIR;
        auto const xdg = tr_env_get_string("XDG_DATA_DIRS"sv);
        auto const buf = fmt::format("{:s}:{:s}:/usr/local/share:/usr/share", pkg, xdg);

        auto sv = std::string_view{ buf };
        auto token = std::string_view{};
        while (tr_strv_sep(&sv, &token, ':')) {
            token = tr_strv_strip(token);
            if (!std::empty(token)) {
                candidates.emplace_back(token);
            }
        }
    }

    /* walk through the candidates & look for a match */
    for (auto const& dir : candidates) {
        if (auto const path = tr_pathbuf{ dir, "/" TR_PROJ_APPNAME "/public_html"sv }; isWebClientDir(path)) {
            return std::string{ path };
        }
    }

#endif

    return {};
}

std::string tr_getSessionIdDir()
{
#ifndef _WIN32

    return std::string{ "/tmp"sv };

#else

    auto const program_data_dir = win32_get_known_folder_ex(FOLDERID_ProgramData, KF_FLAG_CREATE);
    auto result = fmt::format("{:s}/{:s}"sv, program_data_dir, TR_PROJ_APPNAME_CAPITALIZED);
    tr_sys_dir_create(result, 0, 0);
    return result;

#endif
}
