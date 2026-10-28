// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/tr-assert.h" // TR_ASSERT

void tr_block_info::init_sizes(uint64_t const total_size_in, uint32_t const piece_size_in) noexcept
{
    TR_ASSERT(piece_size_in == 0 || piece_size_in >= TrBlockSize);
    if (piece_size_in == 0) {
        *this = {};
        return;
    }

    total_size_ = total_size_in;
    piece_size_ = piece_size_in;
    n_pieces_ = static_cast<tr_piece_index_t>((total_size_ + piece_size_ - 1) / piece_size_);
    n_blocks_ = static_cast<tr_block_index_t>((total_size_ + TrBlockSize - 1) / TrBlockSize);

    uint32_t remainder = total_size_ % piece_size_;
    final_piece_size_ = remainder != 0U ? remainder : piece_size_;

    remainder = total_size_ % TrBlockSize;
    final_block_size_ = remainder != 0U ? remainder : TrBlockSize;
}

uint64_t tr_block_info::size_of_pieces(tr_bitfield const& pieces) const noexcept
{
    // a bitfield of another size does not describe these pieces
    if (!is_initialized() || pieces.size() != piece_count()) {
        return 0U;
    }

    if (pieces.has_all()) {
        return total_size();
    }

    // only the last piece can be short
    auto size = uint64_t{ pieces.count() } * piece_size();
    if (auto const last = piece_count() - 1U; pieces.test(last)) {
        size -= piece_size() - piece_size(last);
    }

    return size;
}
