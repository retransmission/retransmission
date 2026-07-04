// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <fstream>
#include <algorithm>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/torrent-queue.h>
#include <libtransmission/torrent.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace
{
struct TorrentQueueTest : public tr::test::SandboxedTest {
    class MockMediator final : public tr_torrent_queue::Mediator
    {
    public:
        explicit MockMediator(TorrentQueueTest const& test)
            : test_{ test }
        {
        }

        [[nodiscard]] std::string config_dir() const override
        {
            return test_.sandboxDir();
        }

        [[nodiscard]] std::string store_filename(tr_torrent_id_t id) const override
        {
            if (auto it = test_.torrents_.find(id); it != std::end(test_.torrents_)) {
                return it->second.store_filename();
            }
            return {};
        }

    private:
        TorrentQueueTest const& test_;
    };

    std::map<tr_torrent_id_t, tr_torrent const&> torrents_;

    MockMediator mediator_{ *this };

    static auto constexpr TorFilenames = std::array{
        "Android-x86 8.1 r6 iso.torrent"sv,
        "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
        "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
        "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
    };
};
} // namespace

TEST_F(TorrentQueueTest, addRemoveToFromQueue)
{
    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames) {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    for (size_t i = 0; i < std::size(owned); ++i) {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }

    queue.remove(owned[1]->id());
    queue.remove(owned[2]->id());
    owned.erase(std::begin(owned) + 1, std::begin(owned) + 3);
    for (size_t i = 0; i < std::size(owned); ++i) {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }
}

TEST_F(TorrentQueueTest, setQueuePos)
{
    static auto constexpr QueuePos = std::array{ 1U, 3U, 0U, 2U };
    static auto const ExpectedChangedIds = std::array<std::vector<tr_torrent_id_t>, std::size(QueuePos)>{ {
        // Queue order: 1, 2, 3, 4
        { 1, 2 },
        // Queue order: 2, 1, 3, 4
        { 1, 2, 3, 4 },
        // Queue order: 1, 3, 4, 2
        { 1, 3 },
        // Queue order: 3, 1, 4, 2
        {},
        // Queue order: 3, 1, 4, 2
    } };

    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames) {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    for (size_t i = 0; i < std::size(owned); ++i) {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }

    for (size_t i = 0; i < std::size(owned); ++i) {
        auto const id = owned[i]->id();
        auto const pos = QueuePos[i];
        auto changed_ids = queue.set_pos(id, pos);
        std::ranges::sort(changed_ids);
        EXPECT_EQ(queue.get_pos(id), pos);
        EXPECT_EQ(changed_ids, ExpectedChangedIds[i]);
        EXPECT_EQ(std::ranges::adjacent_find(changed_ids), std::ranges::end(changed_ids)); // check if unique
    }

    for (size_t i = 0; i < std::size(owned); ++i) {
        EXPECT_EQ(queue.get_pos(owned[i]->id()), QueuePos[i]);
    }
}

TEST_F(TorrentQueueTest, toFromFile)
{
    static auto constexpr ExpectedContents =
        "[\n"
        "    \"70341e8e1fe8778af23f6318ca75a22f8b1f1c05.torrent\",\n"
        "    \"c9a337562cb0360fd6f5ab40fd2b1b81d5325dbd.torrent\",\n"
        "    \"bc26c6bc83d0ca1a7bf9875df1ffc3fed81ff555.torrent\",\n"
        "    \"f09c8d0884590088f4004e010a928f8b6178c2fd.torrent\"\n"
        "]"sv;

    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames) {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    queue.to_file();

    auto f = std::ifstream{ sandboxDir() + "/queue.json" };
    auto const contents = std::string{ std::istreambuf_iterator{ f }, std::istreambuf_iterator<decltype(f)::char_type>{} };
    EXPECT_EQ(contents, ExpectedContents);
    f.close();

    auto const filenames = queue.from_file();
    ASSERT_EQ(std::size(filenames), std::size(owned));
    for (size_t i = 0; i < std::size(filenames); ++i) {
        EXPECT_EQ(filenames[i], owned[i]->store_filename());
    }
}

// The queue is pure id/position bookkeeping, so the tests below exercise the
// data structure directly with raw ids (no tr_torrent objects needed). This
// keeps the edge-case coverage focused on the queue's invariants.

TEST_F(TorrentQueueTest, getPosOfAbsentIdReturnsMax)
{
    static auto constexpr Max = tr_torrent_queue::MaxQueuePosition;

    auto queue = tr_torrent_queue{ mediator_ };

    // nothing is queued yet
    EXPECT_EQ(Max, queue.get_pos(1));

    queue.add(10);
    queue.add(20);
    queue.add(30);
    EXPECT_EQ(0U, queue.get_pos(10));
    EXPECT_EQ(1U, queue.get_pos(20));
    EXPECT_EQ(2U, queue.get_pos(30));

    // ids that were never added: in a gap, past the end, and negative
    EXPECT_EQ(Max, queue.get_pos(5));
    EXPECT_EQ(Max, queue.get_pos(15));
    EXPECT_EQ(Max, queue.get_pos(999));
    EXPECT_EQ(Max, queue.get_pos(-1));

    // adding a much larger id must backfill the gap with the "not queued"
    // sentinel rather than leaving it pointing at a bogus position
    queue.add(100);
    EXPECT_EQ(3U, queue.get_pos(100));
    EXPECT_EQ(Max, queue.get_pos(50));
}

TEST_F(TorrentQueueTest, removeAbsentIdIsNoop)
{
    static auto constexpr Max = tr_torrent_queue::MaxQueuePosition;

    auto queue = tr_torrent_queue{ mediator_ };
    queue.add(10);
    queue.add(20);
    queue.add(30);

    // removing ids that aren't queued must not disturb the queue
    queue.remove(999);
    queue.remove(15);
    EXPECT_EQ(3U, queue.size());
    EXPECT_EQ(0U, queue.get_pos(10));
    EXPECT_EQ(1U, queue.get_pos(20));
    EXPECT_EQ(2U, queue.get_pos(30));

    // remove from the middle: the tail shifts down and stays queryable in O(1)
    queue.remove(20);
    EXPECT_EQ(2U, queue.size());
    EXPECT_EQ(0U, queue.get_pos(10));
    EXPECT_EQ(1U, queue.get_pos(30));
    EXPECT_EQ(Max, queue.get_pos(20));

    // removing the same id again is a no-op
    queue.remove(20);
    EXPECT_EQ(2U, queue.size());
    EXPECT_EQ(0U, queue.get_pos(10));
    EXPECT_EQ(1U, queue.get_pos(30));
}

TEST_F(TorrentQueueTest, setPosClampsAndIgnoresAbsent)
{
    auto queue = tr_torrent_queue{ mediator_ };
    for (auto const id : { 10, 20, 30, 40 }) {
        queue.add(id);
    }

    // absent id -> no change
    EXPECT_TRUE(queue.set_pos(999, 0U).empty());
    EXPECT_EQ(4U, queue.size());

    // new_pos past the end clamps to the back (queue_move_bottom relies on this)
    auto changed = queue.set_pos(10, tr_torrent_queue::MaxQueuePosition);
    std::ranges::sort(changed);
    EXPECT_EQ((std::vector<tr_torrent_id_t>{ 10, 20, 30, 40 }), changed);
    EXPECT_EQ(3U, queue.get_pos(10));
    EXPECT_EQ(0U, queue.get_pos(20));
    EXPECT_EQ(1U, queue.get_pos(30));
    EXPECT_EQ(2U, queue.get_pos(40));

    // moving to the position it already holds -> no change
    EXPECT_TRUE(queue.set_pos(10, 3U).empty());
}

TEST_F(TorrentQueueTest, queueConsistencyAfterMutations)
{
    auto queue = tr_torrent_queue{ mediator_ };

    auto live = std::vector<tr_torrent_id_t>{};
    auto next_id = tr_torrent_id_t{ 1 };

    // pos_of_ must be a bijection between live ids and [0, size)
    auto const check_dense = [&queue, &live]() {
        auto seen = std::vector<bool>(std::size(live), false);
        ASSERT_EQ(std::size(live), queue.size());
        for (auto const id : live) {
            auto const pos = queue.get_pos(id);
            ASSERT_LT(pos, std::size(live));
            ASSERT_FALSE(seen[pos]); // every position is used exactly once
            seen[pos] = true;
        }
    };

    // grow the queue past a couple of pos_of_ reallocations
    for (auto i = 0; i < 64; ++i) {
        auto const id = next_id++;
        queue.add(id);
        live.push_back(id);
    }
    check_dense();

    // a batch of reorders spanning the whole queue
    (void)queue.set_pos(live[10], 0U);
    (void)queue.set_pos(live[0], tr_torrent_queue::MaxQueuePosition);
    (void)queue.set_pos(live[30], 5U);
    check_dense();

    // remove several ids from various positions (incl. front and back)
    for (auto const id : { live[5], live[0], live[63], live[20] }) {
        queue.remove(id);
        std::erase(live, id);
    }
    check_dense();

    // keep assigning monotonically increasing ids, then reorder again
    for (auto i = 0; i < 8; ++i) {
        auto const id = next_id++;
        queue.add(id);
        live.push_back(id);
    }
    (void)queue.set_pos(live.back(), 0U);
    (void)queue.set_pos(live.front(), 3U);
    check_dense();
}
