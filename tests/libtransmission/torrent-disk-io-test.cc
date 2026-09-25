// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/local-data.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/torrent.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

// Covers the torrent call sites that read and write through tr::LocalData.
// The fixture parks completions instead of shuffling them, so each test
// decides when they arrive.
class TorrentDiskIoTest : public SessionTest
{
protected:
    void SetUp() override
    {
        SessionTest::SetUp();
        session_->local_data.set_completions(tr::LocalData::Completions::Deferred);
    }

    [[nodiscard]] static std::unique_ptr<tr::LocalData::BlockData> zeroBlock(tr_torrent const* tor, tr_block_index_t block)
    {
        auto data = std::make_unique<tr::LocalData::BlockData>();
        data->resize(tor->block_size(block));
        std::ranges::fill(*data, uint8_t{ 0U });
        return data;
    }

    // Two 24 KiB pieces of zeroes. The middle block ends piece 0 and
    // starts piece 1, so writing it last completes both.
    [[nodiscard]] tr_torrent* straddlingTorrentInit()
    {
        static auto constexpr PieceSize = size_t{ 24576U };
        auto const piece_hash = tr_sha1::digest(std::string(PieceSize, '\0'));
        auto pieces = std::string{};
        std::ranges::transform(piece_hash, std::back_inserter(pieces), [](std::byte const b) { return static_cast<char>(b); });
        pieces += pieces;

        auto const benc = "d4:infod6:lengthi" + std::to_string(2U * PieceSize) + "e4:name8:straddle12:piece lengthi" +
            std::to_string(PieceSize) + "e6:pieces" + std::to_string(std::size(pieces)) + ':' + pieces + "ee";
        auto builder = tr_torrent_builder{ session_ };
        EXPECT_TRUE(builder.set_metainfo(benc));
        builder.set_paused(true);
        return createTorrentAndWaitForVerifyDone(&builder);
    }

    // Writes the straddling torrent's blocks, the middle one last.
    void writeStraddlingBlocks(tr_torrent* const tor, bool const bad_piece_1)
    {
        for (auto const block : { tr_block_index_t{ 0U }, tr_block_index_t{ 2U }, tr_block_index_t{ 1U } }) {
            auto data = zeroBlock(tor, block);
            if (bad_piece_1 && block == 2U) {
                std::ranges::fill(*data, uint8_t{ 1U });
            }
            tor->save_block(block, std::move(data));
            session_->local_data.pump();
        }
    }
};

} // namespace

TEST_F(TorrentDiskIoTest, blockIsNotOursUntilItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    blockingRunInSessionThread([this, tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write hasn't finished, so the block isn't ours yet
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_TRUE(tor->has_block_or_pending(block));

        // and a second copy of it is refused while that write is in flight
        EXPECT_FALSE(tor->on_block_received(block));

        session_->local_data.pump();
        EXPECT_TRUE(tor->has_block(block));

        // now that we have it, another copy is still refused
        EXPECT_FALSE(tor->on_block_received(block));
    });
}

TEST_F(TorrentDiskIoTest, failedWriteStopsTorrent)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    blockingRunInSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        EXPECT_FALSE(tor->error().is_local_error());

        auto error = tr_error{};
        error.set_from_errno(ENOSPC);
        tor->on_block_written(block, error);

        EXPECT_TRUE(tor->error().is_local_error());
        EXPECT_FALSE(tor->is_running());

        // the block was not counted, and is no longer pending
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });
}

TEST_F(TorrentDiskIoTest, writtenPieceIsNotAdvertisedUntilItsHashCompletes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0U);
    blockingRunInSessionThread([this, tor, span]() {
        ASSERT_TRUE(tor->is_piece_checked(0U));
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }

        // every block is written, but the last one waits for the hash
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0U));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_FALSE(tor->has_all());
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });

        session_->local_data.pump();
        EXPECT_TRUE(tor->has_piece(0U));
        EXPECT_TRUE(tor->has_all());
        EXPECT_TRUE(tor->is_piece_checked(0U));
        EXPECT_EQ(std::byte{ 0x80 }, tor->create_piece_bitfield().front() & std::byte{ 0x80 });
    });
}

TEST_F(TorrentDiskIoTest, failedHashNeverMakesThePieceAvailable)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0U);
    blockingRunInSessionThread([this, tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            auto data = zeroBlock(tor, block);
            std::ranges::fill(*data, uint8_t{ 1U });
            tor->save_block(block, std::move(data));
        }
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0U));
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });

        // the piece is downloaded again, block by block
        session_->local_data.pump();
        for (auto block = span.begin; block < span.end; ++block) {
            EXPECT_FALSE(tor->has_block_or_pending(block));
        }
        EXPECT_FALSE(tor->has_piece(0U));
        EXPECT_EQ(std::byte{}, tor->create_piece_bitfield().front() & std::byte{ 0x80 });
    });
}

TEST_F(TorrentDiskIoTest, hashResultForInvalidatedPieceIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([this, tor, span, &n_completed]() {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }

        // deliver the writes, which leaves the piece's hash in flight
        session_->local_data.pump();
        EXPECT_EQ(1U, tor->count_missing_blocks_in_piece(0));
        EXPECT_FALSE(tor->has_piece(0));

        // Invalidate the piece while its hash is still in flight.
        // The hash is now about a version of the piece that no longer
        // exists, so delivering it must not mark the piece complete again.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        EXPECT_FALSE(tor->has_piece(0));
        EXPECT_EQ(0U, n_completed);
    });
}

TEST_F(TorrentDiskIoTest, blockSharedByTwoPiecesCountsOnceBothPass)
{
    auto* const tor = straddlingTorrentInit();
    ASSERT_NE(nullptr, tor);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([this, tor, &n_completed]() {
        // the middle block's write leaves both hashes in flight
        writeStraddlingBlocks(tor, false);
        EXPECT_FALSE(tor->has_block(1U));
        EXPECT_TRUE(tor->has_block_or_pending(1U));
        EXPECT_EQ(0U, n_completed);

        // whichever hash lands first
        session_->local_data.pump();
        EXPECT_EQ(2U, n_completed);
        EXPECT_TRUE(tor->has_all());
    });
}

TEST_F(TorrentDiskIoTest, blockSharedWithAFailedPieceIsDownloadedAgain)
{
    auto* const tor = straddlingTorrentInit();
    ASSERT_NE(nullptr, tor);
    auto n_completed = size_t{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    // Piece 0's hash passes before piece 1's starts. The block they share
    // still waits for piece 1, which fails and takes the block back.
    session_->local_data.set_completions(tr::LocalData::Completions::Inline);
    blockingRunInSessionThread([this, tor, &n_completed]() {
        writeStraddlingBlocks(tor, true);
        EXPECT_EQ(0U, n_completed);
        EXPECT_TRUE(tor->has_block(0U));
        EXPECT_FALSE(tor->has_block_or_pending(1U));
        EXPECT_FALSE(tor->has_block_or_pending(2U));
    });
}

TEST_F(TorrentDiskIoTest, blockArrivingDuringVerificationIsRefused)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    blockingRunInSessionThread([tor, block]() {
        tr_torrentVerify(tor);

        // Stopping the torrent doesn't stop a webseed fetch, so its block
        // can still arrive. The verify may have scanned the piece already.
        EXPECT_FALSE(tor->on_block_received(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });

    EXPECT_TRUE(waitFor([&verified]() { return verified.load(); }, MaxWaitMsec));
}

} // namespace tr::test
