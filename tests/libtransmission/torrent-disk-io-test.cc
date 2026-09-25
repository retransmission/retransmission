// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/local-data.h>
#include <libtransmission/quark.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/torrent.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

// Helpers for the torrent call sites that read and write through tr::LocalData.
class TorrentDiskIoTestBase : public SessionTest
{
protected:
    [[nodiscard]] static std::unique_ptr<tr::LocalData::BlockData> zeroBlock(tr_torrent const* tor, tr_block_index_t block)
    {
        auto data = std::make_unique<tr::LocalData::BlockData>();
        data->resize(tor->block_size(block));
        std::ranges::fill(*data, uint8_t{ 0U });
        return data;
    }
};

// Covers the call sites on the synchronous backend. The fixture parks
// completions instead of shuffling them, so each test decides when
// they arrive.
class TorrentDiskIoTest : public TorrentDiskIoTestBase
{
protected:
    // These tests say exactly when each completion arrives, so they
    // stay on the synchronous backend even when TR_LOCAL_DATA_WORKERS
    // asks the fixture for the threaded one.
    [[nodiscard]] bool useLocalDataWorkersFromEnv() const override
    {
        return false;
    }

    void SetUp() override
    {
        TorrentDiskIoTestBase::SetUp();
        session_->local_data.set_completions(tr::LocalData::Completions::Deferred);
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

// Covers the same call sites on the threaded backend, where writes and
// piece hashes finish on worker threads.
class TorrentDiskIoWorkersTest : public TorrentDiskIoTestBase
{
protected:
    [[nodiscard]] bool useLocalDataWorkersFromEnv() const override
    {
        return false;
    }

    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_disk_io_workers, int64_t{ 2 });
        TorrentDiskIoTestBase::SetUp();
    }
};

class TorrentRemovalTest
    : public TorrentDiskIoWorkersTest
    , public ::testing::WithParamInterface<bool>
{
};

// Completes torrents whose files start in the incomplete dir.
class IncompleteDirWorkersTest : public TorrentDiskIoWorkersTest
{
protected:
    void SetUp() override
    {
        settings().insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
        TorrentDiskIoWorkersTest::SetUp();
    }

    // Completes `tor` from this thread by skipping the file that holds
    // its missing piece. A paused write holds back the disk barriers
    // until then. `before` runs on the session thread first. Returns the
    // torrent's current dir as the completeness callback saw it.
    [[nodiscard]] std::string completeBehindAWrite(tr_torrent* const tor, std::function<void()> const& before = {})
    {
        auto dir = std::string{};
        auto called = std::atomic<bool>{ false };
        tr_sessionSetCompletenessCallback(session_, [tor, &dir, &called](tr_torrent_id_t, tr_completeness, bool) {
            dir = tor->current_dir().sv();
            called = true;
        });

        session_->local_data.set_workers_paused(true);
        blockingRunInSessionThread([tor, &before]() {
            auto const block = tor->block_span_for_piece(0U).begin;
            tor->save_block(block, zeroBlock(tor, block));
            if (before) {
                before();
            }
        });
        auto const file = tr_file_index_t{ 0U };
        tr_torrentSetFileDLs(tor, std::span{ &file, 1U }, false);

        // the callback waits for the move out, which waits for the write
        blockingRunInSessionThread([&called]() { EXPECT_FALSE(called); });
        session_->local_data.set_workers_paused(false);
        EXPECT_TRUE(waitFor([&called]() { return called.load(); }, MaxWaitMsec));
        tr_sessionSetCompletenessCallback(session_, nullptr);
        return dir;
    }

    void expectFilesIn(tr_torrent* const tor, std::string_view const dir)
    {
        blockingRunInSessionThread([tor, dir]() {
            EXPECT_EQ(dir, tor->current_dir().sv());
            EXPECT_TRUE(std::empty(tor->incomplete_dir()));
            for (auto file = tr_file_index_t{ 1U }; file < tor->file_count(); ++file) {
                auto const found = tor->find_file(file);
                ASSERT_TRUE(found);
                EXPECT_EQ(dir, found->base);
            }
        });
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

TEST_P(TorrentRemovalTest, waitsForWritesAndHashesBeforeUnregistering)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const id = tor->id();
    auto const filename = tr_torrentFindFile(tor, 0U);
    auto hash_completed = false;

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, id, &hash_completed]() {
        tor->save_block(0U, zeroBlock(tor, 0U));
        session_->local_data.test_piece(
            id,
            1U,
            [&hash_completed](tr_torrent_id_t, tr_piece_index_t, tr_error const& error, auto hash) {
                EXPECT_FALSE(error);
                EXPECT_TRUE(hash);
                hash_completed = true;
            });
        tr_torrentRemove(tor, GetParam(), {});
        EXPECT_EQ(tor, session_->torrents().get(id));
        EXPECT_FALSE(tor->is_running());
        EXPECT_FALSE(tor->on_block_received(1U));
    });
    session_->local_data.set_workers_paused(false);

    EXPECT_TRUE(waitForInSessionThread([this, id]() { return session_->torrents().get(id) == nullptr; }, MaxWaitMsec));
    blockingRunInSessionThread([this, &hash_completed]() {
        EXPECT_TRUE(hash_completed);
        EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
    });
    EXPECT_EQ(!GetParam(), tr_sys_path_exists(filename));
}

INSTANTIATE_TEST_SUITE_P(KeepOrDeleteData, TorrentRemovalTest, ::testing::Bool());

TEST_F(TorrentDiskIoWorkersTest, blockCountsOnlyAfterItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    blockingRunInSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write is on a worker, so the block is pending but not ours
        EXPECT_TRUE(tor->has_block_or_pending(block));
        EXPECT_FALSE(tor->on_block_received(block));
    });

    EXPECT_TRUE(waitForInSessionThread([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
}

TEST_F(TorrentDiskIoWorkersTest, queuedMovesUseTheLatestSourceDirectory)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto const first_dir = tr_pathbuf{ sandboxDir(), "/first" };
    auto const last_dir = tr_pathbuf{ sandboxDir(), "/last" };
    auto first_state = -1;
    auto last_state = -1;
    auto closed = std::atomic<bool>{ false };

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, block, &first_dir, &last_dir, &first_state, &last_state, &closed]() {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentSetLocation(tor, first_dir, true, &first_state);
        tr_torrentSetLocation(tor, last_dir, true, &last_state);
        EXPECT_EQ(TR_LOC_MOVING, first_state);
        EXPECT_EQ(TR_LOC_MOVING, last_state);
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed]() { return closed.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([tor, &last_dir, &first_state, &last_state]() {
        EXPECT_EQ(TR_LOC_DONE, first_state);
        EXPECT_EQ(TR_LOC_DONE, last_state);
        EXPECT_EQ(last_dir.sv(), tor->download_dir().sv());
        for (auto file = tr_file_index_t{}; file < tor->file_count(); ++file) {
            auto const found = tor->find_file(file);
            ASSERT_TRUE(found);
            EXPECT_EQ(last_dir.sv(), found->base);
        }
    });
}

TEST_F(TorrentDiskIoWorkersTest, verificationWaitsForPendingWrites)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            tor->save_block(block, zeroBlock(tor, block));
        }
        tr_torrentVerify(tor);
        EXPECT_EQ(TR_STATUS_CHECK_WAIT, tor->activity());
    });
    EXPECT_FALSE(verified);

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&verified]() { return verified.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([this, tor]() {
        EXPECT_TRUE(tor->has_piece(0));
        EXPECT_EQ(0U, session_->local_data.stats().hashes_from_buffers);
        EXPECT_EQ(0U, session_->local_data.stats().hashes_from_disk);
    });
}

TEST_F(TorrentDiskIoWorkersTest, stoppingCancelsVerificationWaitingForWrites)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto closed = std::atomic<bool>{ false };
    auto verified = std::atomic<bool>{ false };
    auto const tag = session_->verify_done_.connect_scoped([&verified](tr_torrent_id_t) { verified = true; });

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([this, tor, block, &closed]() {
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);
        tr_torrentStop(tor);
        EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
        session_->local_data.close_torrent(tor->id(), [&closed](tr_torrent_id_t) { closed = true; });
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&closed]() { return closed.load(); }, MaxWaitMsec));
    blockingRunInSessionThread([tor, &verified]() {
        EXPECT_EQ(TR_STATUS_STOPPED, tor->activity());
        EXPECT_FALSE(verified);
    });
}

TEST_F(TorrentDiskIoWorkersTest, cancelledVerificationRestoresDeferredPieceHashes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;
    auto hashed = std::atomic<bool>{ false };
    auto const tag = tor->piece_completed_.connect_scoped([&hashed](tr_torrent*, tr_piece_index_t const piece) {
        if (piece == 1U) {
            hashed = true;
        }
    });

    session_->local_data.set_workers_paused(true);
    blockingRunInSessionThread([tor, block]() {
        // the paused write holds the verify back...
        tor->save_block(block, zeroBlock(tor, block));
        tr_torrentVerify(tor);

        // ...while piece 1's writes land, which defers its hash
        tor->set_has_piece(1U, false);
        auto const [begin, end] = tor->block_span_for_piece(1U);
        for (auto written = begin; written < end; ++written) {
            tor->on_block_written(written, {});
        }
        tr_torrentStop(tor);
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([&hashed]() { return hashed.load(); }, MaxWaitMsec));
}

TEST_F(TorrentDiskIoWorkersTest, completedPieceIsHashedFromBufferedBlocks)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    // The hash passing is what completes the piece.
    auto n_completed = std::atomic<size_t>{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    blockingRunInSessionThread([tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }
    });

    EXPECT_TRUE(waitFor([&n_completed]() { return n_completed > 0U; }, MaxWaitMsec));

    // and the hash came from the blocks still in memory
    auto const stats = session_->local_data.stats();
    EXPECT_EQ(1U, stats.hashes_from_buffers);
    EXPECT_EQ(0U, stats.hashes_from_disk);
}

TEST_F(TorrentDiskIoWorkersTest, failedWriteSetsLocalError)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    // Replace the block's file with a directory. Nobody can open that
    // for writing, on any platform or as root.
    auto const filename = tr_torrentFindFile(tor, 0);
    ASSERT_FALSE(std::empty(filename));
    ASSERT_TRUE(tr_sys_path_remove(filename));
    ASSERT_TRUE(tr_sys_dir_create(filename, 0, 0700));

    blockingRunInSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
    });

    EXPECT_TRUE(waitForInSessionThread([tor]() { return tor->error().is_local_error(); }, MaxWaitMsec));

    // the block was not counted, and is no longer pending
    blockingRunInSessionThread([tor, block]() {
        EXPECT_FALSE(tor->has_block(block));
        EXPECT_FALSE(tor->has_block_or_pending(block));
    });
}

TEST_F(IncompleteDirWorkersTest, doneCallbackWaitsForTheMoveOut)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const download_dir = std::string{ tor->download_dir().sv() };

    EXPECT_EQ(download_dir, completeBehindAWrite(tor));
    expectFilesIn(tor, download_dir);
}

TEST_F(IncompleteDirWorkersTest, setLocationQueuedBeforeCompletionPicksTheDir)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const target = tr_pathbuf{ sandboxDir(), "/target" };
    auto state = -1;

    EXPECT_EQ(target.sv(), completeBehindAWrite(tor, [tor, &target, &state]() {
                  tr_torrentSetLocation(tor, target, true, &state);
              }));
    EXPECT_EQ(TR_LOC_DONE, state);
    expectFilesIn(tor, target);
}

TEST_F(IncompleteDirWorkersTest, failedSetLocationStillLeavesTheIncompleteDir)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const download_dir = std::string{ tor->download_dir().sv() };

    // nothing can be created under a regular file
    auto const blocker = tr_pathbuf{ sandboxDir(), "/blocker" };
    createFileWithContents(blocker, std::string_view{ "x" });
    auto const target = tr_pathbuf{ blocker, "/target" };
    auto state = -1;

    EXPECT_EQ(download_dir, completeBehindAWrite(tor, [tor, &target, &state]() {
                  tr_torrentSetLocation(tor, target, true, &state);
              }));
    EXPECT_EQ(TR_LOC_ERROR, state);
    expectFilesIn(tor, download_dir);
}

} // namespace tr::test
