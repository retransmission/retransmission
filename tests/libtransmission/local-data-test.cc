// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h> // chmod()
#include <unistd.h> // geteuid()
#endif

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/block-info.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file-piece-map.h>
#include <libtransmission/file.h>
#include <libtransmission/local-data.h>
#include <libtransmission/open-files.h>
#include <libtransmission/storage-descriptor.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrents.h>
#include <libtransmission/types.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

class StubBackend final : public tr::LocalData::Backend
{
public:
    [[nodiscard]] tr_error_code_t read(
        [[maybe_unused]] tr_torrent_id_t tor_id,
        tr_byte_span_t byte_span,
        tr::LocalData::BlockData& setme) override
    {
        read_span = byte_span;
        setme.assign({ uint8_t{ 1U }, uint8_t{ 2U }, uint8_t{ 3U } });
        return read_err;
    }

    [[nodiscard]] tr_error_code_t test_piece(
        [[maybe_unused]] tr_torrent_id_t tor_id,
        tr_piece_index_t piece,
        tr_sha1_digest_t& setme_hash) override
    {
        tested_piece = piece;
        setme_hash = hash;
        return test_err;
    }

    [[nodiscard]] tr_error_code_t write(
        [[maybe_unused]] tr_torrent_id_t tor_id,
        tr_byte_span_t byte_span,
        tr::LocalData::BlockData const& data) override
    {
        write_span = byte_span;
        last_write.assign(std::begin(data), std::end(data));
        return write_err;
    }

    [[nodiscard]] tr_error_code_t move(
        [[maybe_unused]] tr_torrent_id_t id,
        std::string_view old_parent,
        std::string_view parent,
        std::string_view parent_name) override
    {
        moved_from = std::string{ old_parent };
        moved_to = std::string{ parent };
        moved_name = std::string{ parent_name };
        return move_err;
    }

    [[nodiscard]] tr_error_code_t remove(
        [[maybe_unused]] tr_torrent_id_t id,
        [[maybe_unused]] tr_torrent_remove_func remove_func) override
    {
        remove_called = true;
        return remove_err;
    }

    void rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback)
        override
    {
        renamed_from = std::string{ oldpath };
        renamed_to = std::string{ newname };
        if (callback != nullptr) {
            callback(id, oldpath, newname, tr_error{});
        }
    }

    void close_all() override
    {
        close_all_called = true;
    }

    void close_torrent(tr_torrent_id_t tor_id) override
    {
        closed_torrent = tor_id;
    }

    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num) override
    {
        closed_file = std::pair{ tor_id, file_num };
    }

    tr_error_code_t read_err = 0;
    tr_error_code_t test_err = 0;
    tr_error_code_t write_err = 0;
    tr_error_code_t move_err = 0;
    tr_error_code_t remove_err = 0;
    bool remove_called = false;
    bool close_all_called = false;
    tr_byte_span_t read_span{};
    tr_byte_span_t write_span{};
    tr_piece_index_t tested_piece = 0;
    tr_sha1_digest_t hash = tr_sha1::digest("local-data-test"sv);
    std::vector<uint8_t> last_write;
    std::string moved_from;
    std::string moved_to;
    std::string moved_name;
    std::string renamed_from;
    std::string renamed_to;
    tr_torrent_id_t closed_torrent = -1;
    std::optional<std::pair<tr_torrent_id_t, tr_file_index_t>> closed_file;
};

} // namespace

TEST(LocalData, ReadRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto called = false;
    local_data.read(
        7,
        { .begin = 10U, .end = 13U },
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_byte_span_t byte_span, tr_error const& error, auto data) {
            called = true;
            EXPECT_EQ(7, tor_id);
            EXPECT_EQ(raw_backend->read_span.begin, byte_span.begin);
            EXPECT_EQ(raw_backend->read_span.end, byte_span.end);
            EXPECT_FALSE(error);
            ASSERT_NE(nullptr, data);
            EXPECT_EQ((std::vector<uint8_t>{ 1U, 2U, 3U }), std::vector<uint8_t>(std::begin(*data), std::end(*data)));
        });

    EXPECT_TRUE(called);
}

TEST(LocalData, TestPieceRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto called = false;
    local_data.test_piece(
        9,
        3,
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_piece_index_t piece, tr_error const& error, auto hash) {
            called = true;
            EXPECT_EQ(9, tor_id);
            EXPECT_EQ(raw_backend->tested_piece, piece);
            EXPECT_FALSE(error);
            ASSERT_TRUE(hash.has_value());
            EXPECT_EQ(raw_backend->hash, *hash);
        });

    EXPECT_TRUE(called);
}

TEST(LocalData, WriteRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto data = std::make_unique<tr::LocalData::BlockData>();
    data->assign({ uint8_t{ 4U }, uint8_t{ 5U }, uint8_t{ 6U } });

    auto called = false;
    local_data.write(
        11,
        { .begin = 20U, .end = 23U },
        std::move(data),
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_byte_span_t byte_span, tr_error const& error) {
            called = true;
            EXPECT_EQ(11, tor_id);
            EXPECT_EQ(raw_backend->write_span.begin, byte_span.begin);
            EXPECT_EQ(raw_backend->write_span.end, byte_span.end);
            EXPECT_FALSE(error);
            EXPECT_EQ((std::vector<uint8_t>{ 4U, 5U, 6U }), raw_backend->last_write);
        });

    EXPECT_TRUE(called);
    EXPECT_EQ(0U, local_data.enqueued_write_bytes());
}

TEST(LocalData, AdminOperationsDelegate)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto move_called = false;
    local_data.move(5, "/old", "/new", "name", [&move_called](tr_torrent_id_t tor_id, tr_error const& error) {
        move_called = true;
        EXPECT_EQ(5, tor_id);
        EXPECT_FALSE(error);
    });
    EXPECT_TRUE(move_called);
    EXPECT_EQ("/old", raw_backend->moved_from);
    EXPECT_EQ("/new", raw_backend->moved_to);
    EXPECT_EQ("name", raw_backend->moved_name);

    auto rename_called = false;
    local_data.rename(
        8,
        "old",
        "new",
        [&rename_called](tr_torrent_id_t tor_id, std::string_view oldpath, std::string_view newname, tr_error const& error) {
            rename_called = true;
            EXPECT_EQ(8, tor_id);
            EXPECT_EQ("old", oldpath);
            EXPECT_EQ("new", newname);
            EXPECT_FALSE(error);
        });
    EXPECT_TRUE(rename_called);

    auto remove_called = false;
    local_data.remove(12, {}, [&remove_called](tr_torrent_id_t tor_id, tr_error const& error) {
        remove_called = true;
        EXPECT_EQ(12, tor_id);
        EXPECT_FALSE(error);
    });
    EXPECT_TRUE(remove_called);
    EXPECT_TRUE(raw_backend->remove_called);

    auto close_file_called = false;
    local_data.close_file(13, 2, [&close_file_called](tr_torrent_id_t tor_id) {
        close_file_called = true;
        EXPECT_EQ(13, tor_id);
    });
    EXPECT_TRUE(close_file_called);
    ASSERT_TRUE(raw_backend->closed_file.has_value());
    EXPECT_EQ(13, raw_backend->closed_file->first);
    EXPECT_EQ(2, raw_backend->closed_file->second);

    auto close_torrent_called = false;
    local_data.close_torrent(14, [&close_torrent_called](tr_torrent_id_t tor_id) {
        close_torrent_called = true;
        EXPECT_EQ(14, tor_id);
    });
    EXPECT_TRUE(close_torrent_called);
    EXPECT_EQ(14, raw_backend->closed_torrent);

    local_data.close_all();
    EXPECT_TRUE(raw_backend->close_all_called);

    local_data.shutdown();
}

TEST(LocalData, DeferredCompletionsWaitForPump)
{
    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Deferred);

    auto n_called = 0;
    for (auto i = 0; i < 4; ++i) {
        local_data.read(7, { .begin = 0U, .end = 3U }, [&n_called](auto, auto, auto const&, auto) { ++n_called; });
    }

    EXPECT_EQ(0, n_called);
    local_data.pump();
    EXPECT_EQ(4, n_called);
}

TEST(LocalData, DeferredCompletionsArriveOutOfOrder)
{
    // A shuffle can leave one run in order by chance. It cannot do that
    // for this many runs in a row.
    static auto constexpr NumOps = 8;
    static auto constexpr NumRuns = 16;

    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Deferred);

    auto reordered = false;
    for (auto run = 0; !reordered && run < NumRuns; ++run) {
        auto order = std::vector<tr_piece_index_t>{};
        for (tr_piece_index_t piece = 0; piece < NumOps; ++piece) {
            local_data.test_piece(7, piece, [&order](auto, tr_piece_index_t const tested, auto const&, auto) {
                order.push_back(tested);
            });
        }

        local_data.pump();
        ASSERT_EQ(NumOps, std::size(order));
        reordered = !std::ranges::is_sorted(order);
    }

    EXPECT_TRUE(reordered);
}

TEST(LocalData, AdminOpsDrainParkedCompletions)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };
    local_data.set_completions(tr::LocalData::Completions::Deferred);

    auto read_finished = false;
    local_data.read(7, { .begin = 0U, .end = 3U }, [&read_finished](auto, auto, auto const&, auto) { read_finished = true; });

    // rule 3: a barrier waits for the ops enqueued before it
    auto move_finished = false;
    local_data.move(7, "/old", "/new", "name", [&read_finished, &move_finished](auto, auto const&) {
        EXPECT_TRUE(read_finished);
        move_finished = true;
    });

    EXPECT_TRUE(move_finished);
    EXPECT_EQ("/old", raw_backend->moved_from);
}

TEST(LocalData, ShutdownDeliversParkedCompletions)
{
    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Deferred);

    auto n_called = 0;
    auto data = std::make_unique<tr::LocalData::BlockData>();
    data->assign({ uint8_t{ 4U } });
    local_data.write(11, { .begin = 0U, .end = 1U }, std::move(data), [&n_called](auto, auto, auto const&) { ++n_called; });

    local_data.shutdown();
    EXPECT_EQ(1, n_called);
}

TEST(LocalData, ShuffledCompletionsMixInlineAndDeferred)
{
    static auto constexpr NumOps = 32;

    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Shuffled);

    auto n_called = 0;
    for (auto i = 0; i < NumOps; ++i) {
        local_data.read(7, { .begin = 0U, .end = 3U }, [&n_called](auto, auto, auto const&, auto) { ++n_called; });
    }

    // rule 4: some of these fired before the enqueue call returned,
    // and the rest are waiting for pump()
    auto const n_inline = n_called;
    EXPECT_GT(n_inline, 0);
    EXPECT_LT(n_inline, NumOps);

    local_data.pump();
    EXPECT_EQ(NumOps, n_called);
}

TEST(LocalData, ShuffledCompletionsReplayFromSeed)
{
    static auto constexpr NumOps = 16;
    static auto constexpr Seed = uint32_t{ 12345U };

    auto const run_once = []() {
        auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
        local_data.set_completions(tr::LocalData::Completions::Shuffled, Seed);

        auto order = std::vector<tr_piece_index_t>{};
        for (tr_piece_index_t piece = 0; piece < NumOps; ++piece) {
            local_data.test_piece(7, piece, [&order](auto, tr_piece_index_t const tested, auto const&, auto) {
                order.push_back(tested);
            });
        }
        local_data.pump();
        return order;
    };

    EXPECT_EQ(run_once(), run_once());
}

// ---

namespace
{

// Exercises the threaded backend against real files in a sandbox.
// The test thread doubles as the session thread: marshaled functions
// queue up and run from pumpUntil().
class LocalDataWorkersTest : public tr::test::SandboxedTest
{
protected:
    static auto constexpr TorId = tr_torrent_id_t{ 7 };
    static auto constexpr BlockSize = size_t{ TrBlockSize };

    [[nodiscard]] tr::LocalData::Marshal marshal()
    {
        return [this](std::function<void()> fn) {
            {
                auto const lock = std::scoped_lock{ marshal_mutex_ };
                marshaled_.emplace_back(std::move(fn));
            }
            marshal_cv_.notify_all();
        };
    }

    // Run marshaled functions until `pred()` holds. False on timeout.
    template<typename Pred>
    bool pumpUntil(Pred const& pred)
    {
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };

        for (;;) {
            for (;;) {
                auto fn = std::function<void()>{};
                {
                    auto const lock = std::scoped_lock{ marshal_mutex_ };
                    if (std::empty(marshaled_)) {
                        break;
                    }
                    fn = std::move(marshaled_.front());
                    marshaled_.pop_front();
                }
                fn();
            }

            if (pred()) {
                return true;
            }

            auto lock = std::unique_lock{ marshal_mutex_ };
            if (!marshal_cv_.wait_until(lock, deadline, [this]() { return !std::empty(marshaled_); })) {
                return pred();
            }
        }
    }

    // Byte `i` of the torrent holds a deterministic pattern.
    [[nodiscard]] static uint8_t patternByte(size_t const i) noexcept
    {
        return static_cast<uint8_t>(((i * 31U) + 7U) & 0xFFU);
    }

    [[nodiscard]] static std::string patternString(size_t const begin, size_t const len)
    {
        auto str = std::string{};
        str.reserve(len);
        for (auto i = size_t{}; i < len; ++i) {
            str.push_back(static_cast<char>(patternByte(begin + i)));
        }
        return str;
    }

    // The block of pattern bytes that belongs at torrent byte `begin`.
    [[nodiscard]] static std::unique_ptr<tr::LocalData::BlockData> patternBlock(
        size_t const begin,
        size_t const len = BlockSize)
    {
        auto data = std::make_unique<tr::LocalData::BlockData>();
        data->resize(len);
        for (auto i = size_t{}; i < len; ++i) {
            data->data()[i] = patternByte(begin + i);
        }
        return data;
    }

    [[nodiscard]] std::string pathOf(std::string_view const subpath) const
    {
        return std::string{ sandboxDir() } + '/' + std::string{ subpath };
    }

    [[nodiscard]] std::string readFile(std::string_view const subpath) const
    {
        auto in = std::ifstream{ pathOf(subpath), std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{} };
    }

    // A descriptor whose files live in the sandbox. The files
    // themselves are created by whoever writes to them.
    [[nodiscard]] std::shared_ptr<tr::StorageDescriptor const> makeDescriptor(
        std::vector<std::pair<std::string_view, uint64_t>> const& files,
        uint32_t const piece_size) const
    {
        auto tf = tr_torrent_files{};
        auto sizes = std::vector<uint64_t>{};
        auto total = uint64_t{};
        for (auto const& [subpath, size] : files) {
            tf.add(subpath, size);
            sizes.push_back(size);
            total += size;
        }

        auto const block_info = tr_block_info{ total, piece_size };
        auto fpm = tr_file_piece_map{ block_info, sizes };
        auto wanted = tr_bitfield{ std::size(files) };
        wanted.set_has_all();

        return std::make_shared<tr::StorageDescriptor const>(
            tr::StorageDescriptor{ .id = TorId,
                                   .block_info = block_info,
                                   .files = std::move(tf),
                                   .fpm = std::move(fpm),
                                   .files_wanted = std::move(wanted),
                                   .name = "test",
                                   .download_dir = sandboxDir(),
                                   .incomplete_dir = {},
                                   .current_dir = sandboxDir(),
                                   .preallocation = tr_file_preallocation::None,
                                   .partial_file_naming = false });
    }

    [[nodiscard]] auto makeLocalData(std::shared_ptr<tr::StorageDescriptor const> desc, size_t const n_workers = 2U)
    {
        auto local_data = std::make_unique<tr::LocalData>(torrents_, open_files_);
        local_data->start_workers(
            n_workers,
            marshal(),
            [desc = std::move(desc)](tr_torrent_id_t const id) { return id == TorId ? desc : nullptr; },
            [this](tr_torrent_id_t const id, size_t const n_files) {
                if (id == TorId) {
                    files_created_ += n_files;
                }
            });
        return local_data;
    }

    // Write the blocks covering torrent bytes [begin, end).
    // Returns how many writes were issued.
    static size_t writeBlocks(
        tr::LocalData& local_data,
        size_t const begin,
        size_t const end,
        std::function<void()> const& on_done)
    {
        auto n_writes = size_t{};
        for (auto byte = begin; byte < end; byte += BlockSize) {
            auto const len = std::min(BlockSize, end - byte);
            local_data.write(
                TorId,
                { .begin = byte, .end = byte + len },
                patternBlock(byte, len),
                [on_done](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) {
                    EXPECT_FALSE(error) << error;
                    on_done();
                });
            ++n_writes;
        }
        return n_writes;
    }

    size_t files_created_ = 0U;
    tr_torrents torrents_;
    tr_open_files open_files_;

private:
    std::mutex marshal_mutex_;
    std::condition_variable marshal_cv_;
    std::deque<std::function<void()>> marshaled_;
};

} // namespace

TEST_F(LocalDataWorkersTest, writesLandOnDisk)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done]() { ++n_done; });

    EXPECT_TRUE(pumpUntil([&n_done, n_writes]() { return n_done == n_writes; }));
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));
    EXPECT_EQ(0U, local_data->enqueued_write_bytes());
    EXPECT_EQ(n_writes, local_data->stats().blocks_written);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, adjacentWritesAreCombined)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    // Hold the workers so that every block is queued before any is taken.
    local_data->set_workers_paused(true);
    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done]() { ++n_done; });
    EXPECT_EQ(FileSize, local_data->enqueued_write_bytes());

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&n_done, n_writes]() { return n_done == n_writes; }));

    // one write covered them all
    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.write_runs);
    EXPECT_EQ(n_writes, stats.blocks_written);
    EXPECT_EQ(0U, local_data->enqueued_write_bytes());
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, writesCrossFileBoundaries)
{
    static auto constexpr FileASize = size_t{ 10000U };
    static auto constexpr FileBSize = size_t{ 55536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "a.bin", FileASize }, { "b.bin", FileBSize } }, 32768U));

    // the first block straddles the two files
    auto n_done = size_t{};
    writeBlocks(*local_data, 0U, BlockSize, [&n_done]() { ++n_done; });

    EXPECT_TRUE(pumpUntil([&n_done]() { return n_done == 1U; }));
    EXPECT_EQ(patternString(0U, FileASize), readFile("a.bin"));
    EXPECT_EQ(patternString(FileASize, BlockSize - FileASize), readFile("b.bin"));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, writeCreatesTheFileAndItsDirs)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "sub/dir/data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    writeBlocks(*local_data, 0U, BlockSize, [&n_done]() { ++n_done; });

    EXPECT_TRUE(pumpUntil([&n_done]() { return n_done == 1U; }));
    EXPECT_EQ(1U, files_created_);
    EXPECT_EQ(patternString(0U, BlockSize), readFile("sub/dir/data.bin"));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, pieceIsHashedFromBufferedBlocks)
{
    static auto constexpr FileSize = size_t{ 65536U };
    static auto constexpr PieceSize = uint32_t{ 32768U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, PieceSize));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, PieceSize, [&n_done]() { ++n_done; });
    EXPECT_TRUE(pumpUntil([&n_done, n_writes]() { return n_done == n_writes; }));

    // Corrupt the piece on disk. A hash that read it back would notice.
    auto const zeroes = std::string(PieceSize, '\0');
    createFileWithContents(pathOf("data.bin"), zeroes);

    auto hash = std::optional<tr_sha1_digest_t>{};
    local_data->test_piece(TorId, 0U, [&hash](tr_torrent_id_t, tr_piece_index_t, tr_error const&, auto found) {
        hash = found;
    });

    EXPECT_TRUE(pumpUntil([&hash]() { return hash.has_value(); }));
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(tr_sha1::digest(patternString(0U, PieceSize)), *hash);

    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.hashes_from_buffers);
    EXPECT_EQ(0U, stats.hashes_from_disk);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, pieceIsReadBackWhenItsBuffersAreGone)
{
    static auto constexpr FileSize = size_t{ 65536U };
    static auto constexpr PieceSize = uint32_t{ 32768U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, PieceSize));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, PieceSize, [&n_done]() { ++n_done; });
    EXPECT_TRUE(pumpUntil([&n_done, n_writes]() { return n_done == n_writes; }));

    // closing the torrent drops its buffered blocks
    auto closed = false;
    local_data->close_torrent(TorId, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_TRUE(pumpUntil([&closed]() { return closed; }));

    auto hash = std::optional<tr_sha1_digest_t>{};
    local_data->test_piece(TorId, 0U, [&hash](tr_torrent_id_t, tr_piece_index_t, tr_error const&, auto found) {
        hash = found;
    });

    EXPECT_TRUE(pumpUntil([&hash]() { return hash.has_value(); }));
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(tr_sha1::digest(patternString(0U, PieceSize)), *hash);

    auto const stats = local_data->stats();
    EXPECT_EQ(0U, stats.hashes_from_buffers);
    EXPECT_EQ(1U, stats.hashes_from_disk);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, barriersWaitForWritesAndBlockLaterOps)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    // rule 3: the barrier waits for these...
    local_data->set_workers_paused(true);
    auto order = std::vector<std::string>{};
    writeBlocks(*local_data, 0U, 2U * BlockSize, [&order]() { order.emplace_back("write"); });

    // (the move itself fails since there's no real torrent, and only
    // the ordering of the completions matters here)
    local_data->move(TorId, "/old", "/new", "name", [&order](auto, auto const&) { order.emplace_back("move"); });

    // ...and this read waits for the barrier
    local_data->read(TorId, { .begin = 0U, .end = BlockSize }, [&order](auto, auto, auto const&, auto) {
        order.emplace_back("read");
    });

    // nothing has finished while the workers are held
    EXPECT_TRUE(std::empty(order));
    EXPECT_EQ(2U * BlockSize, local_data->enqueued_write_bytes());

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&order]() { return std::size(order) == 4U; }));
    auto const expected = std::vector<std::string>{ "write", "write", "move", "read" };
    EXPECT_EQ(expected, order);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownDeliversEveryCallback)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done]() { ++n_done; });

    // shutdown() delivers the callbacks inline, without a pump
    local_data->shutdown();
    EXPECT_EQ(n_writes, n_done);
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));
}

TEST_F(LocalDataWorkersTest, missingTorrentWriteFailsCleanly)
{
    auto const local_data = makeLocalData(nullptr);

    auto done = false;
    auto failed = false;
    local_data
        ->write(3, { .begin = 0U, .end = BlockSize }, patternBlock(0U), [&done, &failed](auto, auto, tr_error const& error) {
            done = true;
            failed = !!error;
        });

    EXPECT_TRUE(pumpUntil([&done]() { return done; }));
    EXPECT_TRUE(failed);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, writePastTheTorrentFailsCleanly)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    auto done = false;
    auto failed = false;
    local_data->write(
        TorId,
        { .begin = FileSize - 100U, .end = FileSize - 100U + BlockSize },
        patternBlock(FileSize - 100U),
        [&done, &failed](auto, auto, tr_error const& error) {
            done = true;
            failed = !!error;
        });

    EXPECT_TRUE(pumpUntil([&done]() { return done; }));
    EXPECT_TRUE(failed);
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    local_data->shutdown();
}

#ifndef _WIN32
TEST_F(LocalDataWorkersTest, unwritableFileFailsTheWrite)
{
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores file permissions";
    }

    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    createFileWithContents(pathOf("data.bin"), std::string(FileSize, '\0'));
    ASSERT_EQ(0, chmod(pathOf("data.bin").c_str(), 0444));

    auto done = false;
    auto failed = false;
    local_data->write(
        TorId,
        { .begin = 0U, .end = BlockSize },
        patternBlock(0U),
        [&done, &failed](auto, auto, tr_error const& error) {
            done = true;
            failed = !!error;
        });

    EXPECT_TRUE(pumpUntil([&done]() { return done; }));
    EXPECT_TRUE(failed);

    local_data->shutdown();
    chmod(pathOf("data.bin").c_str(), 0644);
}
#endif
