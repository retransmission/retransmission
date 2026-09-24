// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstdint> // for uint64_t
#include <cstddef> // for size_t
#include <span>
#include <utility>
#include <vector>

#include "libtransmission/bitfield.h"
#include "libtransmission/types.h"

struct tr_block_info;
struct tr_torrent_metainfo;

class tr_file_piece_map
{
public:
    template<typename T>
    struct index_span_t {
        T begin;
        T end;
    };
    using file_span_t = index_span_t<tr_file_index_t>;
    using piece_span_t = index_span_t<tr_piece_index_t>;

    template<typename T>
    struct offset_t {
        T index;
        uint64_t offset;
    };

    using file_offset_t = offset_t<tr_file_index_t>;

private:
    template<typename T>
    struct CompareToSpan {
        using span_t = index_span_t<T>;

        [[nodiscard]] static constexpr int compare(T const item, span_t const span) // <=>
        {
            if (item < span.begin) {
                return -1;
            }

            if (item >= span.end) {
                return 1;
            }

            return 0;
        }

        [[nodiscard]] constexpr bool operator()(T const item, span_t const span) const // <
        {
            return compare(item, span) < 0;
        }

        [[nodiscard]] static constexpr int compare(span_t const span, T const item) // <=>
        {
            return -compare(item, span);
        }

        [[nodiscard]] constexpr bool operator()(span_t const span, T const item) const // <
        {
            return compare(span, item) < 0;
        }
    };

public:
    explicit tr_file_piece_map(tr_torrent_metainfo const& tm);
    tr_file_piece_map(tr_block_info const& block_info, std::span<uint64_t const> file_sizes);

    [[nodiscard]] constexpr piece_span_t piece_span_for_file(tr_file_index_t const file) const noexcept
    {
        return file_pieces_[file];
    }

    [[nodiscard]] constexpr file_span_t file_span_for_piece(tr_piece_index_t const piece) const
    {
        constexpr auto Compare = CompareToSpan<tr_piece_index_t>{};
        auto const begin = std::begin(file_pieces_);
        auto const [equal_begin, equal_end] = std::equal_range(begin, std::end(file_pieces_), piece, Compare);
        return {
            .begin = static_cast<tr_file_index_t>(equal_begin - begin),
            .end = static_cast<tr_file_index_t>(equal_end - begin),
        };
    }

    [[nodiscard]] file_offset_t file_offset(uint64_t offset) const;

    [[nodiscard]] constexpr size_t file_count() const noexcept
    {
        return std::size(file_pieces_);
    }

    [[nodiscard]] constexpr auto byte_span_for_file(tr_file_index_t const file) const noexcept
    {
        auto const& span = file_bytes_[file];
        return tr_byte_span_t{ .begin = span.begin, .end = span.end };
    }

    [[nodiscard]] constexpr bool is_edge_piece(tr_piece_index_t const piece) const
    {
        return std::ranges::binary_search(edge_pieces_, piece);
    }

private:
    using byte_span_t = index_span_t<uint64_t>;

    void reset(tr_torrent_metainfo const& tm);
    void reset(tr_block_info const& block_info, std::span<uint64_t const> file_sizes);

    std::vector<byte_span_t> file_bytes_;
    std::vector<piece_span_t> file_pieces_;
    std::vector<tr_piece_index_t> edge_pieces_;
};

class tr_file_priorities
{
public:
    constexpr explicit tr_file_priorities(tr_file_piece_map const* fpm) noexcept
        : fpm_{ fpm }
    {
    }

    // returns true if any file's priority changed.
    [[nodiscard]] constexpr bool set(tr_file_index_t const file, tr_priority_t const priority) noexcept
    {
        if (file >= fpm_->file_count()) {
            return false;
        }

        if (std::empty(priorities_)) {
            if (priority == TR_PRI_NORMAL) {
                return false;
            }

            priorities_.assign(fpm_->file_count(), TR_PRI_NORMAL);
            priorities_.shrink_to_fit();
        }

        return std::exchange(priorities_[file], priority) != priority;
    }

    [[nodiscard]] constexpr bool set(std::span<tr_file_index_t const> const files, tr_priority_t const priority)
    {
        if (std::ranges::any_of(files, [n_files = fpm_->file_count()](tr_file_index_t file) { return file >= n_files; })) {
            return false;
        }

        auto ret = false;
        for (auto const file : files) {
            ret |= set(file, priority);
        }
        return ret;
    }

    [[nodiscard]] constexpr tr_priority_t file_priority(tr_file_index_t const file) const noexcept
    {
        if (file >= priorities_.size()) {
            return TR_PRI_NORMAL;
        }

        return priorities_[file];
    }

    [[nodiscard]] tr_priority_t piece_priority(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const* fpm_;
    std::vector<tr_priority_t> priorities_;
};

class tr_files_wanted
{
public:
    explicit tr_files_wanted(tr_file_piece_map const* fpm);

    bool set(tr_file_index_t file, bool wanted);
    bool set(std::span<tr_file_index_t const> files, bool wanted);

    [[nodiscard]] constexpr bool file_wanted(tr_file_index_t file) const
    {
        return wanted_.test(file);
    }

    [[nodiscard]] bool piece_wanted(tr_piece_index_t piece) const;

private:
    tr_file_piece_map const* fpm_;
    tr_bitfield wanted_;
};
