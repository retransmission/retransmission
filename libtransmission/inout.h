// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t, uint64_t
#include <optional>
#include <span>

#include "libtransmission/block-info.h"
#include "libtransmission/crypto-utils.h" // tr_sha1
#include "libtransmission/digest.h"
#include "libtransmission/error-types.h"
#include "libtransmission/open-files.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

struct tr_torrent;

namespace tr
{
struct StorageDescriptor;
}

/**
 * Reads `std::size(setme)` bytes of torrent data, starting at byte `begin`.
 *
 * Safe to call from any thread.
 *
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] tr_error_code_t tr_ioRead(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t begin,
    std::span<uint8_t> setme,
    tr_open_files::Waiter* waiter = nullptr);

struct tr_io_write_result {
    // 0 on success, or an errno value on failure.
    tr_error_code_t error = 0;

    // How many files the write created on disk.
    size_t n_files_created = 0;
};

/**
 * Writes torrent data starting at byte `begin`.
 *
 * Creates, preallocates, and truncates files as needed.
 * Safe to call from any thread.
 *
 * Does no error handling of its own. The caller decides what to do with
 * the returned code.
 */
[[nodiscard]] tr_io_write_result tr_ioWrite(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    uint64_t begin,
    std::span<uint8_t const> writeme,
    tr_open_files::Waiter* waiter = nullptr);

/**
 * Hashes one piece from whatever `get_block` returns for each of its blocks.
 *
 * `get_block(block)` returns the block's bytes, or an empty span if they
 * can't be had. Blocks at the piece's edges are sliced down to the piece.
 *
 * @return the hash, or no value if any block couldn't be had.
 */
template<typename GetBlock> // std::span<uint8_t const>(tr_block_index_t)
[[nodiscard]] std::optional<tr_sha1_digest_t> tr_ioHashPiece(
    tr_block_info const& block_info,
    tr_piece_index_t const piece,
    GetBlock get_block)
{
    TR_ASSERT(piece < block_info.piece_count());

    auto sha = tr_sha1{};

    auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
    auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block) {
        auto span = std::span<uint8_t const>{ get_block(block) };
        if (std::empty(span)) {
            return {};
        }

        auto const byte_span = block_info.byte_span_for_block(block);
        TR_ASSERT(std::size(span) == byte_span.size());

        if (block + 1U == end_block) {
            span = span.first(static_cast<size_t>(end_byte - byte_span.begin));
        }
        if (block == begin_block) {
            span = span.subspan(static_cast<size_t>(begin_byte - byte_span.begin));
        }

        sha.add(span);
        n_bytes_checked += std::size(span);
    }

    TR_ASSERT(block_info.piece_size(piece) == n_bytes_checked);
    return sha.finish();
}

/**
 * Recalculates a piece's hash from the data on disk.
 *
 * Safe to call from any thread.
 *
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] tr_error_code_t tr_ioRecalculateHash(
    tr::StorageDescriptor const& desc,
    tr_open_files& open_files,
    tr_piece_index_t piece,
    tr_sha1_digest_t& setme,
    tr_open_files::Waiter* waiter = nullptr);

/**
 * @brief Test to see if the piece matches its metainfo's SHA1 checksum.
 */
[[nodiscard]] bool tr_ioTestPiece(tr_torrent const& tor, tr_piece_index_t piece);
