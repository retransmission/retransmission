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
#include <map>
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

using path_func_t = std::function<void(std::string_view path)>;
using directory_filter_t = std::function<bool(std::string_view directory)>;

[[nodiscard]] bool is_folder(std::string_view const path)
{
    auto const info = tr_sys_path_get_info(path);
    return info && info->isFolder();
}

[[nodiscard]] bool is_empty_folder(std::string_view const path)
{
    return is_folder(path) && std::empty(tr_sys_dir_get_files(path, tr_basename_accept_all));
}

void depth_first_walk(
    std::string_view const path,
    path_func_t const& func,
    std::optional<int> max_depth = {},
    directory_filter_t const& skip_directory = {})
{
    auto const can_descend = !max_depth || *max_depth > 0;
    if ((can_descend || skip_directory) && is_folder(path)) {
        // A skipped directory is neither traversed nor passed to func.
        if (skip_directory && skip_directory(path)) {
            return;
        }
        if (can_descend) {
            for (auto const& name : tr_sys_dir_get_files(path, tr_basename_accept_all)) {
                depth_first_walk(tr_pathbuf{ path, '/', name }, func, max_depth ? *max_depth - 1 : max_depth, skip_directory);
            }
        }
    }

    func(path);
}

[[nodiscard]] bool is_junk_file(std::string_view filename)
{
    auto const base = tr_sys_path_basename(filename);

#ifdef __APPLE__
    // check for resource forks. <http://web.archive.org/web/20101010051608/http://support.apple.com/kb/TA20578>
    if (base.starts_with("._"sv)) {
        return true;
    }
#endif

    // a copy that a crash interrupted mid-move
    if (base.starts_with(TrFileCopyPrefix)) {
        return true;
    }

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
        if (auto error = tr_error{}; !tr_sys_path_remove(filename, &error)) {
            tr_logAddWarn(
                fmt::format(
                    "Couldn't remove '{path}': {error} ({error_code})",
                    fmt::arg("path", filename),
                    fmt::arg("error", error.message()),
                    fmt::arg("error_code", error.code())));
        }
    }
}

// Derive each tree from a moved file, not the torrent name.
// That sweeps each tree once and leaves unused trees alone.
void add_tree(std::set<std::string>& trees, std::string_view const base, std::string_view const subpath)
{
    if (auto const slash = subpath.find('/'); slash != std::string_view::npos) {
        trees.emplace(tr_pathbuf{ base, '/', subpath.substr(0, slash) }.sv());
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

std::optional<tr_torrent_files::MoveLeftovers> tr_torrent_files::move(
    std::span<std::string_view const> const old_parents,
    std::string_view const parent_in,
    std::string_view const parent_name,
    tr_error* error,
    bool* const needs_copy) const
{
    auto const parent = tr_pathbuf{ parent_in };
    tr_logAddTrace(fmt::format("Moving files from [{}] to '{:s}'", fmt::join(old_parents, ", "), parent.sv()), parent_name);

    auto leftovers = MoveLeftovers{};
    leftovers.parent_name = parent_name;
    leftovers.roots.emplace_back(parent.sv());
    leftovers.roots.insert(std::end(leftovers.roots), std::begin(old_parents), std::end(old_parents));

    if (std::ranges::all_of(old_parents, [&parent](auto old_parent) { return tr_sys_path_is_same(old_parent, parent); })) {
        return leftovers;
    }

    auto local_error = tr_error{};
    error = error != nullptr ? error : &local_error;

    // Select sources before moving files; retain them to identify cleanup trees.
    auto const n = file_count();
    auto moves = std::vector<std::pair<tr_file_index_t, FoundFile>>{};
    moves.reserve(n);
    for (tr_file_index_t i = 0; i < n; ++i) {
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

    if (std::empty(moves)) {
        return leftovers;
    }

    if (!tr_sys_dir_create(parent, TR_SYS_DIR_CREATE_PARENTS, 0777, error)) {
        return {};
    }

    // Copies can take minutes and renames don't, so copies go first.
    // A kill during a copy then finds nothing renamed yet.
    // This only sets the order: a rename that fails still falls back to a copy.
    auto is_copied_from = std::map<std::string_view, bool>{};
    for (auto const old_parent : old_parents) {
        is_copied_from[old_parent] = !tr_sys_path_is_same_filesystem(old_parent, parent);
    }
    auto const is_copied = [&is_copied_from](auto const& entry) {
        return is_copied_from.at(entry.second.base);
    };
    if (needs_copy != nullptr && std::ranges::any_of(moves, is_copied)) {
        *needs_copy = true;
        return {};
    }
    std::ranges::stable_partition(moves, is_copied);

    // Copied sources stay until every file is in place.
    // A kill mid-copy then leaves each copied file's source where the resume file says it is.
    auto moved = std::vector<std::pair<FoundFile, tr_file_moved>>{};
    moved.reserve(std::size(moves));

    // Puts back what already moved, so a failed move leaves the old dirs whole.
    // It's best effort, since the move has already failed.
    auto const undo = [&moved, &parent] {
        for (auto const& [found, how] : moved) {
            auto const path = found.filename_under<tr_pathbuf>(parent);
            if (how == tr_file_moved::Renamed) {
                tr_sys_path_rename(path, found.filename<tr_pathbuf>());
            } else {
                tr_sys_path_remove(path);
            }
        }
    };

    for (auto const& [i, found] : moves) {
        auto const old_path = found.filename<tr_pathbuf>();
        auto const path = found.filename_under<tr_pathbuf>(parent);
        if (tr_sys_path_exists(path)) {
            tr_logAddWarn(
                fmt::format(
                    "Moving '{source}' will overwrite '{path}'",
                    fmt::arg("source", old_path.sv()),
                    fmt::arg("path", path.sv())),
                parent_name);
        }
        tr_logAddTrace(fmt::format("Moving file #{:d} from '{:s}' to '{:s}'", i, old_path.sv(), path.sv()), parent_name);
        auto const how = tr_file_rename_or_copy(old_path, path, error);
        if (!how) {
            error->prefix_message(fmt::format("'{:s}': ", old_path.sv()));
            undo();
            return {};
        }
        moved.emplace_back(found, *how);

        if (*how == tr_file_moved::Copied) {
            leftovers.sources.emplace(old_path.sv());
        }
        add_tree(leftovers.trees, found.base, found.subpath);
    }

    return leftovers;
}

void tr_torrent_files::add_copies_left_by_killed_move(
    MoveLeftovers& leftovers,
    std::span<std::string_view const> const old_parents,
    std::string_view const parent_in) const
{
    for (tr_file_index_t i = 0, n = file_count(); i < n; ++i) {
        auto const found = find(i, std::span{ &parent_in, 1U });
        if (!found) {
            continue;
        }

        auto const path = found->filename<tr_pathbuf>();
        for (auto const old_parent : old_parents) {
            auto const old_path = found->filename_under<tr_pathbuf>(old_parent);
            auto const info = tr_sys_path_get_info(old_path);
            if (info && info->size == found->size && !tr_sys_path_is_same(old_path, path)) {
                leftovers.sources.emplace(old_path.sv());
                add_tree(leftovers.trees, old_parent, found->subpath);
            }
        }
    }
}

void tr_torrent_files::MoveLeftovers::remove() const
{
    for (auto const& source : sources) {
        if (auto log_error = tr_error{}; !tr_sys_path_remove(source, &log_error)) {
            tr_logAddError(
                fmt::format(
                    "Couldn't remove '{path}': {error} ({error_code})",
                    fmt::arg("path", source),
                    fmt::arg("error", log_error.message()),
                    fmt::arg("error_code", log_error.code())),
                parent_name);
        }
    }

    // Protect the roots themselves while still cleaning their siblings and ancestors.
    // If identity cannot be checked, leave the directory alone.
    auto const skip_directory = [this](std::string_view directory) {
        return std::ranges::any_of(roots, [this, directory](std::string_view const root) {
            auto path_error = tr_error{};
            auto const same = tr_sys_path_is_same(directory, root, &path_error);

            // If identity cannot be checked, preserve the directory and report why.
            if (path_error) {
                tr_logAddWarn(
                    fmt::format(
                        "Couldn't compare '{path}' with '{root}': {error} ({error_code}); leaving it alone",
                        fmt::arg("path", directory),
                        fmt::arg("root", root),
                        fmt::arg("error", path_error.message()),
                        fmt::arg("error_code", path_error.code())),
                    parent_name);
                return true;
            }

            if (!same) {
                return false;
            }

            tr_logAddDebug(fmt::format("Skipping cleanup of '{:s}': configured root '{:s}'", directory, root), parent_name);
            return true;
        });
    };

    for (auto const& tree : trees) {
        depth_first_walk(tree, remove_junk, {}, skip_directory);
    }
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
        // Only run that cleanup when matching torrent files are found.
        // This deliberately leaves empty directories and junk after manual data deletion;
        // without matching files, we cannot distinguish them from unrelated directories.
        if (!has_any_local_data(std::span{ &parent, 1U })) {
            continue;
        }
        auto root_error = tr_error{};
        remove(parent, tmpdir_prefix, func, &root_error);
        // Process independent roots even when an earlier root reported an error,
        // and preserve the first error for the caller.
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
            if (!tr_file_move(from, to, error)) {
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

[[nodiscard, maybe_unused]] bool is_win32_reserved_file(std::string_view in) noexcept
{
    if (std::empty(in)) {
        return false;
    }

    // Shortcut to avoid extra work below.
    // All the paths below involve filenames that begin with one of these chars
    static auto constexpr ReservedFilesBeginWithOneOf = "ACLNP"sv;
    if (!ReservedFilesBeginWithOneOf.contains(static_cast<char>(toupper(static_cast<unsigned char>(in.front()))))) {
        return false;
    }

    auto in_upper = tr_pathbuf{ in };
    std::ranges::for_each(in_upper, [](auto& ch) { ch = static_cast<char>(toupper(static_cast<unsigned char>(ch))); });
    auto const in_upper_sv = in_upper.sv();

    static auto constexpr ReservedNames = std::to_array<std::string_view>({
        // clang-format off: related names on the same line
        // https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
        // CON, PRN, AUX, NUL, COM1, COM2, COM3, COM4, COM5, COM6, COM7, COM8, COM9,
        // COM¹, COM², COM³, LPT1, LPT2, LPT3, LPT4, LPT5, LPT6, LPT7, LPT8, LPT9, LPT¹, LPT², and LPT³.
        // Also avoid these names followed immediately by an extension;
        // for example, NUL.txt and NUL.tar.gz are both equivalent to NUL.
        "AUX"sv,  "CON"sv,  "NUL"sv,  "PRN"sv,
        "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv, "COM8"sv, "COM9"sv,
        "COM¹"sv, "COM²"sv, "COM³"sv, "LPT¹"sv, "LPT²"sv, "LPT³"sv,
        "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
        // https://learn.microsoft.com/en-us/windows/console/console-handles
        // To open a handle to a console's input buffer, specify the CONIN$ value in a call to CreateFile.
        // Specify the CONOUT$ value in a call to CreateFile to open a handle to a console's active screen buffer.
        "CONIN$"sv, "CONOUT$"sv,
        // clang-format on
    });
    return std::ranges::any_of(ReservedNames, [in_upper_sv](auto const& name) {
        return in_upper_sv.starts_with(name) && (in_upper_sv.size() == name.size() || in_upper_sv[name.size()] == '.');
    });
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

} // namespace

// https://en.wikipedia.org/wiki/Filename#Comparison_of_filename_limitations
void tr_torrent_files::sanitize_subpath(std::string_view path, std::string& append_me, bool os_specific)
{
#ifndef _WIN32
    if (!os_specific)
#endif
    {
        // strip surrounding spaces and trailing periods
        auto const it = std::ranges::find_if_not(path, [](unsigned char ch) { return std::isspace(ch); });
        path.remove_prefix(std::ranges::distance(std::ranges::begin(path), it));

        auto const rit = std::ranges::find_if_not(std::ranges::rbegin(path), std::ranges::rend(path), [](unsigned char ch) {
            return ch == '.' || std::isspace(ch);
        });
        path.remove_suffix(std::ranges::distance(std::ranges::rbegin(path), rit));
    }

    // replace reserved filenames with an underscore
    if (is_reserved_file(path, os_specific)) {
        append_me += '_';
    }

    // replace reserved characters with an underscore
    auto const add_char = [os_specific](auto ch) {
        return is_reserved_char(ch, os_specific) ? '_' : ch;
    };
    std::ranges::transform(path, std::back_inserter(append_me), add_char);
}
