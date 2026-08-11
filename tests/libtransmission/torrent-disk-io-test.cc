// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include <libtransmission/error.h>
#include <libtransmission/local-data.h>
#include <libtransmission/torrent.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

// Exercises the call sites that changed when torrent IO moved behind
// tr::LocalData. Completions are parked rather than shuffled so that
// each test says exactly when they arrive.
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

        // Invalidate the piece while its hash is still being computed.
        // The hash is now about a piece that no longer exists, so
        // delivering it must not mark the piece complete again.
        tor->set_has_piece(0, false);
        session_->local_data.pump();
        EXPECT_FALSE(tor->has_piece(0));
    });
}

} // namespace tr::test
