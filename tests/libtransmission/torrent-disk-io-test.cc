// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#ifndef _WIN32
#include <sys/stat.h> // chmod()
#include <unistd.h> // geteuid()
#endif

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/local-data.h>
#include <libtransmission/quark.h>
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

    // Runs `func` on the session thread and waits for it to finish.
    void inSessionThread(std::function<void()> const& func)
    {
        auto done = false;
        session_->run_in_session_thread([&func, &done]() {
            func();
            done = true;
        });
        ASSERT_TRUE(waitFor([&done]() { return done; }, MaxWaitMsec));
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

} // namespace

TEST_F(TorrentDiskIoTest, blockIsNotOursUntilItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    inSessionThread([this, tor, block]() {
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

    inSessionThread([tor, block]() {
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

TEST_F(TorrentDiskIoTest, hashResultForInvalidatedPieceIsDropped)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    inSessionThread([this, tor, span]() {
        for (auto block = span.begin; block < span.end; ++block) {
            ASSERT_TRUE(tor->on_block_received(block));
            tor->save_block(block, zeroBlock(tor, block));
        }

        // deliver the writes, which leaves the piece's hash in flight
        session_->local_data.pump();
        EXPECT_TRUE(tor->has_piece(0));

        // Invalidate the piece while its hash is still in flight.
        // The hash is now about a version of the piece that no longer
        // exists, so delivering it must not mark the piece complete again.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        EXPECT_FALSE(tor->has_piece(0));
    });
}

TEST_F(TorrentDiskIoTest, requestBudgetIsUnboundedOnTheSynchronousBackend)
{
    EXPECT_FALSE(session_->spare_request_blocks().has_value());
}

// ---

TEST_F(TorrentDiskIoWorkersTest, queuedWritesConsumeTheRequestBudget)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    auto budget = std::optional<size_t>{};
    inSessionThread([this, &budget]() { budget = session_->spare_request_blocks(); });
    ASSERT_TRUE(budget.has_value());
    EXPECT_GT(*budget, 0U);

    // a queued write takes its block out of the budget until it lands
    session_->local_data.set_workers_paused(true);
    inSessionThread([this, tor, block, &budget]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
        EXPECT_EQ(*budget - 1U, session_->spare_request_blocks());
    });

    session_->local_data.set_workers_paused(false);
    EXPECT_TRUE(waitFor([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    inSessionThread([this, &budget]() { EXPECT_EQ(budget, session_->spare_request_blocks()); });
}

TEST_F(TorrentDiskIoWorkersTest, blockCountsOnlyAfterItsWriteFinishes)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    inSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));

        // the write is on a worker, so the block is pending but not ours
        EXPECT_TRUE(tor->has_block_or_pending(block));
        EXPECT_FALSE(tor->on_block_received(block));
    });

    EXPECT_TRUE(waitFor([tor, block]() { return tor->has_block(block); }, MaxWaitMsec));
    EXPECT_EQ(0U, session_->local_data.enqueued_write_bytes());
}

TEST_F(TorrentDiskIoWorkersTest, completedPieceIsHashedFromBufferedBlocks)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const span = tor->block_span_for_piece(0);

    // The hash passing is what completes the piece.
    auto n_completed = std::atomic<size_t>{};
    auto const tag = tor->piece_completed_.connect_scoped([&n_completed](tr_torrent*, tr_piece_index_t) { ++n_completed; });

    inSessionThread([tor, span]() {
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

#ifndef _WIN32
TEST_F(TorrentDiskIoWorkersTest, failedWriteSetsLocalError)
{
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions";
    }

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto const block = tor->block_span_for_piece(0).begin;

    // the block's file can't be opened for writing
    auto const filename = tr_torrentFindFile(tor, 0);
    ASSERT_FALSE(std::empty(filename));
    ASSERT_EQ(0, chmod(filename.c_str(), 0444));

    inSessionThread([tor, block]() {
        ASSERT_TRUE(tor->on_block_received(block));
        tor->save_block(block, zeroBlock(tor, block));
    });

    EXPECT_TRUE(waitFor([tor]() { return tor->error().is_local_error(); }, MaxWaitMsec));

    // the block was not counted, and is no longer pending
    EXPECT_FALSE(tor->has_block(block));
    EXPECT_FALSE(tor->has_block_or_pending(block));

    chmod(filename.c_str(), 0644);
}
#endif

} // namespace tr::test
