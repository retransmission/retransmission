// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::sort()
#include <cstddef>
#include <cstdint> // uint64_t
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "libtransmission/file.h"
#include "libtransmission/types.h"

struct tr_error;

/**
 * A simple collection of files & utils for finding them, moving them, etc.
 */
struct tr_torrent_files {
public:
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return std::empty(files_);
    }

    [[nodiscard]] constexpr size_t file_count() const noexcept
    {
        return std::size(files_);
    }

    [[nodiscard]] constexpr uint64_t file_size(tr_file_index_t file_index) const
    {
        return files_.at(file_index).size_;
    }

    [[nodiscard]] constexpr auto total_size() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] constexpr std::string const& path(tr_file_index_t file_index) const
    {
        return files_.at(file_index).path_;
    }

    void set_path(tr_file_index_t file_index, std::string_view path)
    {
        files_.at(file_index).set_path(path);
    }

    void insert_subpath_prefix(std::string_view path)
    {
        auto prefix = std::string{ path };
        prefix += '/';

        for (auto& file : files_) {
            file.path_.insert(0, prefix);
            file.path_.shrink_to_fit();
        }
    }

    constexpr void reserve(size_t n_files)
    {
        files_.reserve(n_files);
    }

    constexpr void shrink_to_fit()
    {
        files_.shrink_to_fit();
    }

    constexpr void clear() noexcept
    {
        files_.clear();
        total_size_ = uint64_t{};
    }

    [[nodiscard]] auto sorted_by_path() const
    {
        auto ret = std::vector<std::pair<std::string /*path*/, uint64_t /*size*/>>{};
        ret.reserve(std::size(files_));
        std::ranges::transform(files_, std::back_inserter(ret), [](auto const& in) {
            return std::make_pair(in.path_, in.size_);
        });

        std::ranges::sort(std::views::keys(ret));

        return ret;
    }

    constexpr tr_file_index_t add(std::string_view path, uint64_t file_size)
    {
        auto const ret = static_cast<tr_file_index_t>(std::size(files_));
        files_.emplace_back(path, file_size);
        total_size_ += file_size;
        return ret;
    }

    bool move(
        std::span<std::string_view const> old_parents,
        std::string_view parent_in,
        std::string_view parent_name = "",
        tr_error* error = nullptr) const;

    void remove(
        std::string_view parent_in,
        std::string_view tmpdir_prefix,
        tr_torrent_remove_func const& func,
        tr_error* error = nullptr) const;

    static constexpr std::string_view PartialFileSuffix = ".part";

    /**
     * A file located on disk.
     *
     * `base` views the same storage as the matching entry of the `paths` span
     * passed into `find()`; `subpath` views the `tr_torrent_files`.
     * A FoundFile is valid only as long as both of those outlive it.
     *
     * filename() builds the name in whichever buffer the caller names, so
     * callers on hot paths can use a stack buffer, e.g. filename<tr_pathbuf>().
     */
    struct FoundFile : public tr_sys_path_info {
        // "/home/foo/Downloads"
        std::string_view base;

        // "torrent/01-file-one.txt"
        std::string_view subpath;

        // whether the file is an incomplete download
        bool is_partial = false;

        // what `subpath` needs appended to name the file on disk
        [[nodiscard]] constexpr std::string_view suffix() const noexcept
        {
            return is_partial ? PartialFileSuffix : std::string_view{};
        }

        // "/home/foo/Downloads/torrent/01-file-one.txt"
        template<typename Buf = std::string>
        [[nodiscard]] Buf filename() const
        {
            return filename_under<Buf>(base);
        }

        // what this file would be named if it lived under `parent` instead of `base`
        template<typename Buf = std::string>
        [[nodiscard]] Buf filename_under(std::string_view parent) const
        {
            auto buf = Buf{};
            buf += parent;
            buf += '/';
            buf += subpath;
            buf += suffix();
            return buf;
        }
    };

    [[nodiscard]] std::optional<FoundFile> find(tr_file_index_t file_index, std::span<std::string_view const> paths) const;
    [[nodiscard]] bool has_any_local_data(std::span<std::string_view const> paths) const;
    [[nodiscard]] std::string_view primary_mime_type() const;

    static void sanitize_subpath(std::string_view path, std::string& append_me, bool os_specific = true);

    [[nodiscard]] static std::string sanitize_subpath(std::string_view path, bool os_specific = true)
    {
        auto ret = std::string{};
        ret.reserve(std::size(path));
        sanitize_subpath(path, ret, os_specific);
        return ret;
    }

    [[nodiscard]] static bool is_subpath_sanitized(std::string_view path, bool os_specific = true)
    {
        return sanitize_subpath(path, os_specific) == path;
    }

private:
    struct file_t {
    public:
        constexpr void set_path(std::string_view subpath)
        {
            if (path_ != subpath) {
                path_ = subpath;
                path_.shrink_to_fit();
            }
        }

        constexpr file_t(std::string_view path, uint64_t size)
            : path_{ path }
            , size_{ size }
        {
        }

        std::string path_;
        uint64_t size_ = 0;
    };

    std::vector<file_t> files_;
    uint64_t total_size_ = 0;
};
