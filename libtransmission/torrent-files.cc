// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::find()
#include <array>
#include <cstddef>
#include <cctype>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <small/map.hpp>

#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

using file_func_t = std::function<void(std::string_view filename)>;

[[nodiscard]] bool is_folder(std::string_view const path)
{
    auto const info = tr_sys_path_get_info(path);
    return info && info->isFolder();
}

[[nodiscard]] bool is_empty_folder(std::string_view const path)
{
    return is_folder(path) && std::empty(tr_sys_dir_get_files(path, tr_basename_accept_all));
}

void depth_first_walk(std::string_view const path, file_func_t const& func, std::optional<int> max_depth = {})
{
    if (is_folder(path) && (!max_depth || *max_depth > 0)) {
        for (auto const& name : tr_sys_dir_get_files(path, tr_basename_accept_all)) {
            depth_first_walk(tr_pathbuf{ path, '/', name }, func, max_depth ? *max_depth - 1 : max_depth);
        }
    }

    func(path);
}

bool is_junk_file(std::string_view filename)
{
    auto const base = tr_sys_path_basename(filename);

#ifdef __APPLE__
    // check for resource forks. <http://web.archive.org/web/20101010051608/http://support.apple.com/kb/TA20578>
    if (base.starts_with("._"sv)) {
        return true;
    }
#endif

    auto constexpr Files = std::to_array<std::string_view>({
        ".DS_Store"sv,
        "Thumbs.db"sv,
        "desktop.ini"sv,
    });

    return std::ranges::find(Files, base) != std::ranges::end(Files);
}

void remove_junk(std::string_view const filename)
{
    if (is_empty_folder(filename) || is_junk_file(filename)) {
        tr_sys_path_remove(filename);
    }
}

// Keep configured roots out of cleanup, including on case-insensitive
// filesystems. A tree inside a root can still be cleaned. Only confirmed
// absence rules out containment; other errors conservatively prevent cleanup.
bool tree_contains_path(std::string_view tree, std::string_view path, tr_error& error)
{
    auto const resolved_tree = tr_sys_path_resolve(tree, &error);
    if (std::empty(resolved_tree)) {
        return !tr_error_is_enoent(error.code());
    }
    auto const resolved_path = tr_sys_path_resolve(path, &error);
    if (std::empty(resolved_path)) {
        return !tr_error_is_enoent(error.code());
    }

    for (auto walk = std::string_view{ resolved_path };;) {
        if (tr_sys_path_is_same(resolved_tree, walk, &error) || error) {
            return true;
        }
        auto const parent = tr_sys_path_dirname(walk);
        if (parent == walk) {
            return false;
        }
        walk = parent;
    }
}

} // unnamed namespace

// ---

std::optional<tr_torrent_files::FoundFile> tr_torrent_files::find(
    tr_file_index_t const file_index,
    std::span<std::string_view const> const paths) const
{
    auto filename = tr_pathbuf{};
    auto const& subpath = path(file_index);

    for (auto const base : paths) {
        filename.assign(base, '/', subpath);
        if (auto const info = tr_sys_path_get_info(filename); info) {
            return FoundFile{ *info, base, subpath, false };
        }

        filename.append(PartialFileSuffix);
        if (auto const info = tr_sys_path_get_info(filename); info) {
            return FoundFile{ *info, base, subpath, true };
        }
    }

    return {};
}

bool tr_torrent_files::has_any_local_data(std::span<std::string_view const> const paths) const
{
    for (tr_file_index_t i = 0, n = file_count(); i < n; ++i) {
        if (find(i, paths)) {
            return true;
        }
    }

    return false;
}

std::string_view tr_torrent_files::primary_mime_type() const
{
    // count up how many bytes there are for each mime-type in the torrent
    auto size_per_mime_type = small::unordered_map<std::string_view, size_t, 256U>{};
    for (tr_file_index_t i = 0, n = file_count(); i < n; ++i) {
        auto const mime_type = tr_get_mime_type_for_filename(path(i));
        size_per_mime_type[mime_type] += file_size(i);
    }

    if (std::empty(size_per_mime_type)) {
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
        // application/octet-stream is the default value for all other cases.
        // An unknown file type should use this type.
        auto constexpr Fallback = "application/octet-stream"sv;
        return Fallback;
    }

    auto const it = std::ranges::max_element(size_per_mime_type, [](auto const& a, auto const& b) {
        return a.second < b.second;
    });
    return it->first;
}

// ---

bool tr_torrent_files::move(
    std::span<std::string_view const> old_parents,
    std::string_view parent_in,
    std::string_view parent_name,
    tr_error* error) const
{
    auto const parent = tr_pathbuf{ parent_in };
    tr_logAddTrace(fmt::format("Moving files from [{}] to '{:s}'", fmt::join(old_parents, ", "), parent.sv()), parent_name);

    if (std::ranges::all_of(old_parents, [&parent](auto old_parent) { return tr_sys_path_is_same(old_parent, parent); })) {
        return true;
    }

    auto local_error = tr_error{};
    error = error != nullptr ? error : &local_error;

    // Select sources before moving files; retain them to identify cleanup trees.
    auto moves = std::vector<std::pair<tr_file_index_t, FoundFile>>{};
    for (tr_file_index_t i = 0, n = file_count(); i < n; ++i) {
        auto const found = find(i, old_parents);
        if (!found) {
            continue;
        }

        auto const old_path = found->filename<tr_pathbuf>();
        auto const path = found->filename_under<tr_pathbuf>(parent);
        tr_logAddTrace(fmt::format("Found file #{:d} '{:s}'", i, old_path.sv()), parent_name);

        if (tr_sys_path_is_same(old_path, path)) {
            continue;
        }

        moves.emplace_back(i, *found);
    }

    if (!tr_sys_dir_create(parent, TR_SYS_DIR_CREATE_PARENTS, 0777, error)) {
        return false;
    }

    for (auto const& [i, found] : moves) {
        auto const old_path = found.filename<tr_pathbuf>();
        auto const path = found.filename_under<tr_pathbuf>(parent);
        if (tr_sys_path_exists(path)) {
            tr_logAddWarn(
                fmt::format(
                    fmt::runtime(_("Moving '{source}' will overwrite '{path}'")),
                    fmt::arg("source", old_path.sv()),
                    fmt::arg("path", path.sv())),
                parent_name);
        }
        tr_logAddTrace(fmt::format("Moving file #{:d} from '{:s}' to '{:s}'", i, old_path.sv(), path.sv()), parent_name);
        if (!tr_file_move(old_path, path, true, error)) {
            error->prefix_message(fmt::format("'{:s}': ", old_path.sv()));
            return false;
        }
    }

    for (auto const old_parent : old_parents) {
        if (tr_sys_path_is_same(old_parent, parent)) {
            continue;
        }

        // Derive each tree from a moved file, not the torrent name. Scan it
        // once and leave unused trees alone.
        auto trees = std::set<std::string_view>{};
        for (auto const& found : moves | std::views::values) {
            auto const slash = found.subpath.find('/');
            if (found.base == old_parent && slash != std::string_view::npos) {
                trees.emplace(found.subpath.substr(0, slash));
            }
        }
        for (auto const top : trees) {
            auto const tree = tr_pathbuf{ old_parent, '/', top };
            auto const contains_root = [&tree, parent_name](auto root) {
                auto path_error = tr_error{};
                if (!tree_contains_path(tree, root, path_error)) {
                    return false;
                }
                tr_logAddDebug(
                    path_error ? fmt::format("Skipping cleanup of '{:s}': {:s}", tree.sv(), path_error.message()) :
                                 fmt::format("Skipping cleanup of '{:s}': contains root '{:s}'", tree.sv(), root),
                    parent_name);
                return true;
            };
            // Do not enter a configured root. A tree inside another root is
            // allowed; cleanup only removes junk and empty directories.
            if (contains_root(parent.sv()) ||
                std::ranges::any_of(old_parents, [&](auto root) { return root != old_parent && contains_root(root); })) {
                continue;
            }
            depth_first_walk(tree, remove_junk);
        }
    }

    return true;
}

// ---

void tr_torrent_files::remove(
    std::span<std::string_view const> const parents,
    std::string_view const tmpdir_prefix,
    tr_torrent_remove_func const& func,
    tr_error* error) const
{
    auto local_error = tr_error{};
    error = error != nullptr ? error : &local_error;

    for (auto const parent : parents) {
        // The single-root remove() also cleans junk and empty directories.
        // Do not run that cleanup in a root with no matching torrent files.
        if (!has_any_local_data(std::span{ &parent, 1U })) {
            continue;
        }
        auto root_error = tr_error{};
        remove(parent, tmpdir_prefix, func, &root_error);
        // Process independent roots even when an earlier root reported an
        // error, and preserve the first error for the caller.
        if (root_error && !*error) {
            *error = std::move(root_error);
        }
    }
}

/**
 * This convoluted code does something (seemingly) simple:
 * remove the torrent's local files.
 *
 * Fun complications:
 * 1. Try to preserve the directory hierarchy in the recycle bin.
 * 2. If there are nontorrent files, don't delete them...
 * 3. ...unless the other files are "junk", such as .DS_Store
 */
void tr_torrent_files::remove(
    std::string_view parent_in,
    std::string_view tmpdir_prefix,
    tr_torrent_remove_func const& func,
    tr_error* error) const
{
    auto const parent = tr_pathbuf{ parent_in };

    // don't try to delete local data if the directory's gone missing
    if (!tr_sys_path_exists(parent)) {
        return;
    }

    // try to make a tmpdir
    auto tmpdir = tr_pathbuf{ parent, '/', tmpdir_prefix, "__XXXXXX"sv };
    if (!tr_sys_dir_create_temp(std::data(tmpdir), error)) {
        return;
    }

    // move the local data to the tmpdir
    auto const paths = std::to_array<std::string_view>({ parent.sv() });
    for (tr_file_index_t idx = 0, n_files = file_count(); idx < n_files; ++idx) {
        if (auto const found = find(idx, paths); found) {
            // if moving a file fails, give up and let the error propagate
            auto const from = found->filename<tr_pathbuf>();
            auto const to = found->filename_under<tr_pathbuf>(tmpdir);
            if (!tr_file_move(from, to, false, error)) {
                return;
            }
        }
    }

    // Make a list of the top-level torrent files & folders
    // because we'll need it below in the 'remove junk' phase
    auto const path = tr_pathbuf{ parent, '/', tmpdir_prefix };
    auto top_files = std::set<std::string>{ std::string{ path } };
    depth_first_walk(
        tmpdir,
        [&parent, &tmpdir, &top_files](std::string_view const filename) {
            if (tmpdir != filename) {
                top_files.emplace(tr_pathbuf{ parent, '/', tr_sys_path_basename(filename) });
            }
        },
        1);

    auto const func_wrapper = [&error, &func, &tmpdir](std::string_view const filename) {
        if (tmpdir != filename) {
            func(filename, error);
        }
    };

    // Remove the tmpdir.
    // Since `func` might send files to a recycle bin, try to preserve
    // the folder hierarchy by removing top-level files & folders first.
    // But that can fail -- e.g. `func` might refuse to remove nonempty
    // directories -- so plan B is to remove everything bottom-up.
    depth_first_walk(tmpdir, func_wrapper, 1);
    depth_first_walk(tmpdir, func_wrapper);
    tr_sys_path_remove(tmpdir);

    // OK we've removed the local data.
    // What's left are empty folders, junk, and user-generated files.
    // Remove the first two categories and leave the third alone.
    for (auto const& filename : top_files) {
        depth_first_walk(filename, remove_junk);
    }
}

namespace
{

// `is_unix_reserved_file` and `is_win32_reserved_file` kept as `maybe_unused`
// for potential support of different filesystems on the same OS
[[nodiscard, maybe_unused]] bool is_unix_reserved_file(std::string_view in) noexcept
{
    static auto constexpr ReservedNames = std::to_array<std::string_view>({
        "."sv,
        ".."sv,
    });
    return (std::ranges::find(ReservedNames, in) != std::ranges::end(ReservedNames));
}

// https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
// Do not use the following reserved names for the name of a file:
// CON, PRN, AUX, NUL, COM1, COM2, COM3, COM4, COM5, COM6, COM7, COM8,
// COM9, LPT1, LPT2, LPT3, LPT4, LPT5, LPT6, LPT7, LPT8, and LPT9.
// Also avoid these names followed immediately by an extension;
// for example, NUL.txt is not recommended.
[[nodiscard, maybe_unused]] bool is_win32_reserved_file(std::string_view in) noexcept
{
    if (std::empty(in)) {
        return false;
    }

    // Shortcut to avoid extra work below.
    // All the paths below involve filenames that begin with one of these chars
    static auto constexpr ReservedFilesBeginWithOneOf = "ACLNP"sv;
    if (ReservedFilesBeginWithOneOf.find(static_cast<char>(toupper(in.front()))) == std::string_view::npos) {
        return false;
    }

    auto in_upper = tr_pathbuf{ in };
    std::ranges::for_each(in_upper, [](auto& ch) { ch = static_cast<char>(toupper(ch)); });
    auto const in_upper_sv = in_upper.sv();

    static auto constexpr ReservedNames = std::to_array<std::string_view>({
        // clang-format off: related names on the same line
        "AUX"sv,  "CON"sv,  "NUL"sv,  "PRN"sv,
        "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv, "COM8"sv, "COM9"sv,
        "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
        // clang-format on
    });
    if (std::ranges::find(ReservedNames, in_upper_sv) != std::ranges::end(ReservedNames)) {
        return true;
    }

    static auto constexpr ReservedPrefixes = std::to_array<std::string_view>({
        // clang-format off: related names on the same line
        "AUX."sv,  "CON."sv,  "NUL."sv,  "PRN."sv,
        "COM1."sv, "COM2."sv, "COM3."sv, "COM4."sv, "COM5."sv, "COM6."sv, "COM7."sv, "COM8."sv, "COM9."sv,
        "LPT1."sv, "LPT2."sv, "LPT3."sv, "LPT4."sv, "LPT5."sv, "LPT6."sv, "LPT7."sv, "LPT8."sv, "LPT9."sv,
        // clang-format on
    });
    return std::ranges::any_of(ReservedPrefixes, [in_upper_sv](auto const& prefix) { return in_upper_sv.starts_with(prefix); });
}

[[nodiscard]] bool is_reserved_file(std::string_view in, bool os_specific) noexcept
{
    if (!os_specific) {
        return is_unix_reserved_file(in) || is_win32_reserved_file(in);
    }
#ifdef _WIN32
    return is_win32_reserved_file(in);
#else
    return is_unix_reserved_file(in);
#endif
}

// `is_unix_reserved_char` and `is_win32_reserved_char` kept as `maybe_unused`
// for potential support of different filesystems on the same OS
[[nodiscard, maybe_unused]] auto constexpr is_unix_reserved_char([[maybe_unused]] unsigned char ch) noexcept
{
    // TODO: keep this here for future uses
    return false;
}

// https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
// Use any character in the current code page for a name, including Unicode
// characters and characters in the extended character set (128–255),
// except for the following:
[[nodiscard, maybe_unused]] auto constexpr is_win32_reserved_char(unsigned char ch) noexcept
{
    switch (ch) {
    case '"':
    case '*':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] auto constexpr is_reserved_char(unsigned char ch, bool os_specific) noexcept
{
    if (ch <= 31 || ch == '/') {
        return true;
    }

    if (!os_specific) {
        return is_unix_reserved_char(ch) || is_win32_reserved_char(ch);
    }
#ifdef _WIN32
    return is_win32_reserved_char(ch);
#else
    return is_unix_reserved_char(ch);
#endif
}

// https://en.wikipedia.org/wiki/Filename#Comparison_of_filename_limitations
void append_sanitized_component(std::string_view in, std::string& out, bool os_specific)
{
#ifdef _WIN32
    // remove leading and trailing spaces
    in = tr_strv_strip(in);

    // remove trailing periods
    while (in.ends_with('.')) {
        in.remove_suffix(1);
    }
#endif

    // replace reserved filenames with an underscore
    if (is_reserved_file(in, os_specific)) {
        out += '_';
    }

    // replace reserved characters with an underscore
    auto const add_char = [os_specific](auto ch) {
        return is_reserved_char(ch, os_specific) ? '_' : ch;
    };
    std::ranges::transform(in, std::back_inserter(out), add_char);
}

} // namespace

void tr_torrent_files::sanitize_subpath(std::string_view path, std::string& append_me, bool os_specific)
{
    auto segment = std::string_view{};
    while (tr_strv_sep(&path, &segment, '/')) {
        append_sanitized_component(segment, append_me, os_specific);
        append_me += '/';
    }

    if (auto const n = std::size(append_me); n > 0) {
        append_me.resize(n - 1); // remove trailing slash
    }
}
