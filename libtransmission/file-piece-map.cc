// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility> // std::exchange()
#include <vector>

#include <small/set.hpp>

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/file-piece-map.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

tr_file_piece_map::tr_file_piece_map(tr_torrent_metainfo const& tm)
{
    reset(tm);
}

tr_file_piece_map::tr_file_piece_map(tr_block_info const& block_info, std::span<uint64_t const> const file_sizes)
{
    reset(block_info, file_sizes);
}

void tr_file_piece_map::reset(tr_block_info const& block_info, std::span<uint64_t const> const file_sizes)
{
    file_bytes_.resize(file_sizes.size());
    file_bytes_.shrink_to_fit();

    file_pieces_.resize(file_sizes.size());
    file_pieces_.shrink_to_fit();

    auto edge_pieces = small::set<tr_piece_index_t, 1024U>{};
    edge_pieces.reserve(file_sizes.size() * 2U);

    uint64_t offset = 0U;
    for (tr_file_index_t i = 0U; i < file_sizes.size(); ++i) {
        auto const file_size = file_sizes[i];

        auto const begin_byte = offset;
        auto end_byte = tr_byte_index_t{};

        // N.B. If the last file in the torrent is 0 bytes, and the torrent size is a multiple of piece size,
        // then the computed piece index will be past-the-end. We handle this with std::min.
        auto const begin_piece = std::min(block_info.byte_loc(begin_byte).piece, block_info.piece_count() - 1U);
        auto end_piece = tr_piece_index_t{};

        edge_pieces.insert(begin_piece);

        if (file_size != 0U) {
            end_byte = begin_byte + file_size;
            auto const final_byte = end_byte - 1U;
            auto const final_piece = block_info.byte_loc(final_byte).piece;
            end_piece = final_piece + 1U;

            edge_pieces.insert(final_piece);
        } else {
            end_byte = begin_byte;
            end_piece = begin_piece + 1U;
        }
        file_bytes_[i] = byte_span_t{ .begin = begin_byte, .end = end_byte };
        file_pieces_[i] = piece_span_t{ .begin = begin_piece, .end = end_piece };
        offset += file_size;
    }

    edge_pieces_.assign(std::begin(edge_pieces), std::end(edge_pieces));
}

void tr_file_piece_map::reset(tr_torrent_metainfo const& tm)
{
    auto const n = tm.file_count();
    auto file_sizes = std::vector<uint64_t>(n);
    for (tr_file_index_t i = 0U; i < n; ++i) {
        file_sizes[i] = tm.file_size(i);
    }
    reset(tm.block_info(), file_sizes);
}

// ---

tr_files_wanted::tr_files_wanted(tr_file_piece_map const* const fpm)
    : fpm_{ fpm }
    , wanted_{ fpm->file_count() }
{
    wanted_.set_has_all(); // by default we want all files
}

bool tr_files_wanted::set(tr_file_index_t const file, bool const wanted)
{
    return wanted_.set(file, wanted);
}

bool tr_files_wanted::set(std::span<tr_file_index_t const> const files, bool const wanted)
{
    if (std::ranges::any_of(files, [this](tr_file_index_t const file) { return file >= fpm_->file_count(); })) {
        return false;
    }

    auto ret = false;
    for (auto const file : files) {
        ret |= set(file, wanted);
    }
    return ret;
}

bool tr_files_wanted::piece_wanted(tr_piece_index_t const piece) const
{
    if (wanted_.has_all()) {
        return true;
    }

    auto const [begin, end] = fpm_->file_span_for_piece(piece);
    return wanted_.count(begin, end) != 0U;
}
