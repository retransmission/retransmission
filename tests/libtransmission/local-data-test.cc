// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno> // EINVAL, ENOSPC
#include <chrono>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple> // std::ignore
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/block-info.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file-piece-map.h>
#include <libtransmission/file-utils.h>
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
    [[nodiscard]] tr_error read(
        [[maybe_unused]] tr_torrent_id_t const tor_id,
        tr_byte_span_t const byte_span,
        tr::LocalData::BlockData& setme) override
    {
        read_span = byte_span;
        setme.assign({ uint8_t{ 1U }, uint8_t{ 2U }, uint8_t{ 3U } });
        return read_err;
    }

    [[nodiscard]] tr_error test_piece(
        [[maybe_unused]] tr_torrent_id_t const tor_id,
        tr_piece_index_t const piece,
        tr_sha1_digest_t& setme_hash) override
    {
        tested_piece = piece;
        setme_hash = hash;
        return test_err;
    }

    [[nodiscard]] tr_error write(
        [[maybe_unused]] tr_torrent_id_t const tor_id,
        tr_byte_span_t const byte_span,
        tr::LocalData::BlockData const& data) override
    {
        write_span = byte_span;
        last_write.assign(std::begin(data), std::end(data));
        return write_err;
    }

    [[nodiscard]] tr_error move(
        [[maybe_unused]] tr_torrent_id_t const id,
        std::string_view const parent,
        std::function<void()> const& on_placed) override
    {
        moved_to = std::string{ parent };
        if (!move_err) {
            on_placed();
        }
        return move_err;
    }

    [[nodiscard]] tr_error remove(
        [[maybe_unused]] tr_torrent_id_t const id,
        [[maybe_unused]] tr_torrent_remove_func const remove_func) override
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

    tr_error read_err;
    tr_error test_err;
    tr_error write_err;
    tr_error move_err;
    tr_error remove_err;
    bool remove_called = false;
    bool close_all_called = false;
    tr_byte_span_t read_span{};
    tr_byte_span_t write_span{};
    tr_piece_index_t tested_piece = 0;
    tr_sha1_digest_t hash = tr_sha1::digest("local-data-test"sv);
    std::vector<uint8_t> last_write;
    std::string moved_to;
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
    local_data.move(
        5,
        [] { return std::string{ "/new" }; },
        [&move_called](tr_torrent_id_t tor_id, std::string_view const parent, tr_error const& error) {
            move_called = true;
            EXPECT_EQ(5, tor_id);
            EXPECT_EQ("/new", parent);
            EXPECT_FALSE(error);
        });
    EXPECT_TRUE(move_called);
    EXPECT_EQ("/new", raw_backend->moved_to);

    // placing the files comes before the end of the move
    auto events = std::vector<std::string>{};
    local_data.move(
        5,
        [] { return std::string{ "/newer" }; },
        [&events](tr_torrent_id_t, std::string_view, tr_error const&) { events.emplace_back("moved"); },
        [&events](tr_torrent_id_t tor_id, std::string_view const parent) {
            EXPECT_EQ(5, tor_id);
            EXPECT_EQ("/newer", parent);
            events.emplace_back("placed");
        });
    EXPECT_EQ((std::vector<std::string>{ "placed", "moved" }), events);

    // an empty parent skips the move
    auto skip_called = false;
    local_data.move(
        5,
        [] { return std::string{}; },
        [&skip_called](tr_torrent_id_t, std::string_view const parent, tr_error const& error) {
            skip_called = true;
            EXPECT_EQ("", parent);
            EXPECT_FALSE(error);
        },
        [](tr_torrent_id_t, std::string_view) { FAIL() << "a skipped move places nothing"; });
    EXPECT_TRUE(skip_called);
    EXPECT_EQ("/newer", raw_backend->moved_to);

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

TEST(LocalData, ErrorsKeepTheBackendsMessage)
{
    auto const expected = tr_error{ ENOSPC, "injected backend error" };
    auto backend = std::make_unique<StubBackend>();
    backend->read_err = expected;
    backend->test_err = expected;
    backend->write_err = expected;
    backend->move_err = expected;
    backend->remove_err = expected;
    auto local_data = tr::LocalData{ std::move(backend) };

    auto errors = std::vector<tr_error>{};
    local_data.read(1, { .begin = 0U, .end = 3U }, [&errors](auto, auto, tr_error const& error, auto) {
        errors.push_back(error);
    });
    local_data.test_piece(1, 0, [&errors](auto, auto, tr_error const& error, auto) { errors.push_back(error); });
    local_data.write(
        1,
        { .begin = 0U, .end = 3U },
        std::make_unique<tr::LocalData::BlockData>(),
        [&errors](auto, auto, tr_error const& error) { errors.push_back(error); });
    local_data.move(
        1,
        [] { return std::string{ "/new" }; },
        [&errors](auto, auto, tr_error const& error) { errors.push_back(error); });
    local_data.remove(1, {}, [&errors](auto, tr_error const& error) { errors.push_back(error); });

    ASSERT_EQ(5U, std::size(errors));
    for (auto const& error : errors) {
        EXPECT_EQ(expected.message(), error.message());
        EXPECT_EQ(expected.code(), error.code());
    }

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
    local_data.move(
        7,
        [] { return std::string{ "/new" }; },
        [&read_finished, &move_finished](auto, auto, auto const&) {
            EXPECT_TRUE(read_finished);
            move_finished = true;
        });

    EXPECT_TRUE(move_finished);
    EXPECT_EQ("/new", raw_backend->moved_to);
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

    auto const run_once = [] {
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

// Stalls a worker where it creates a file, until the test lets it go on.
// Preallocation runs on the worker, outside every lock, so a test can
// enqueue more ops while the worker is known to be between two runs.
class PreallocationGate
{
public:
    [[nodiscard]] tr_open_files::Preallocate hook()
    {
        return [this](tr_sys_file_t, uint64_t, int, tr_error*) {
            auto lock = std::unique_lock{ mutex_ };
            ++n_entered_;
            cv_.notify_all();
            cv_.wait(lock, [this] { return n_released_ >= n_entered_; });
            return true;
        };
    }

    // Wait for a worker to stall creating the `n`th file. False on timeout.
    [[nodiscard]] bool waitForFile(size_t const n)
    {
        auto lock = std::unique_lock{ mutex_ };
        return cv_.wait_for(lock, std::chrono::seconds{ 5 }, [this, n] { return n_entered_ >= n; });
    }

    // Let the next `n` files through, starting with one that is stalled.
    void release(size_t const n = 1U)
    {
        {
            auto const lock = std::scoped_lock{ mutex_ };
            n_released_ += n;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t n_entered_ = 0U;
    size_t n_released_ = 0U;
};

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
    [[nodiscard]] bool pumpUntil(Pred const& pred)
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
            if (!marshal_cv_.wait_until(lock, deadline, [this] { return !std::empty(marshaled_); })) {
                return pred();
            }
        }
    }

    // Include every byte of the block index so misplaced blocks change the contents.
    [[nodiscard]] static uint8_t patternByte(size_t const byte) noexcept
    {
        auto const block = uint64_t{ byte / BlockSize };
        auto const shift = (byte % sizeof(block)) * 8U;
        return static_cast<uint8_t>(((byte * 31U) + 7U) ^ (block >> shift));
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
        auto contents = std::vector<char>{};
        EXPECT_TRUE(tr_file_read(pathOf(subpath), contents));
        return { std::data(contents), std::size(contents) };
    }

    // True if the whole file can be read from the page cache alone. Not on
    // every platform, or every filesystem, and only once the data is cached.
    [[nodiscard]] bool canReadCached(std::string_view const subpath) const
    {
        auto const fd = tr_sys_file_open(pathOf(subpath), TR_SYS_FILE_READ, 0);
        if (fd == TR_BAD_SYS_FILE) {
            return false;
        }

        auto const size = tr_sys_path_get_info(pathOf(subpath))->size;
        auto buf = std::vector<uint8_t>(static_cast<size_t>(size));
        auto const read = tr_sys_file_read_cached_at(fd, std::data(buf), size, 0U);
        tr_sys_file_close(fd);
        return read;
    }

    // Opens a torrent file into the pool, as a read through a worker would.
    // Cached reads only use files that are already open.
    void openInPool(tr_file_index_t const file_index, std::string_view const subpath)
    {
        auto error = tr_error{};
        auto const size = tr_sys_path_get_info(pathOf(subpath))->size;
        EXPECT_TRUE(open_files_.get(TorId, file_index, false, pathOf(subpath), tr_file_preallocation::None, size, error))
            << error;
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

    // The write budget also sizes the retained-block cache, at half the budget.
    [[nodiscard]] auto makeLocalData(
        std::shared_ptr<tr::StorageDescriptor const> desc,
        size_t const n_workers = 2U,
        std::optional<uint64_t> const write_budget = {})
    {
        auto local_data = std::make_unique<tr::LocalData>(torrents_, open_files_);
        if (write_budget) {
            local_data->set_write_budget(*write_budget);
        }
        // Analyzer false positive: the descriptor moved into the lambda is still owned.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        local_data->start_workers(n_workers, open_files_, marshal(), [desc = std::move(desc)](tr_torrent_id_t const id) {
            return id == TorId ? desc : nullptr;
        });
        return local_data;
    }

    struct ReadResult {
        bool done = false;
        tr_error error;
        std::string data;
    };

    // Read torrent bytes [begin, begin + len) into `result`.
    static void readInto(tr::LocalData& local_data, size_t const begin, size_t const len, ReadResult& result)
    {
        local_data.read(
            TorId,
            { .begin = begin, .end = begin + len },
            [&result](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto data) {
                result.done = true;
                result.error = error;
                if (data) {
                    result.data.assign(reinterpret_cast<char const*>(data->data()), data->size());
                }
            });
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

    // A descriptor whose new files are preallocated, for a PreallocationGate.
    [[nodiscard]] std::shared_ptr<tr::StorageDescriptor const> makePreallocatingDescriptor(
        std::vector<std::pair<std::string_view, uint64_t>> const& files) const
    {
        auto desc = std::make_shared<tr::StorageDescriptor>(*makeDescriptor(files, BlockSize));
        desc->preallocation = tr_file_preallocation::Full;
        return desc;
    }

    // Run what the session thread has been asked to run so far.
    void pumpOnce()
    {
        EXPECT_TRUE(pumpUntil([]() { return true; }));
    }

    tr_torrents torrents_;
    tr_open_files open_files_;

private:
    std::mutex marshal_mutex_;
    std::condition_variable marshal_cv_;
    std::deque<std::function<void()>> marshaled_;
};

} // namespace

TEST_F(LocalDataWorkersTest, failedPreallocationFailsTheWrite)
{
    auto const desc = std::make_shared<tr::StorageDescriptor>(*makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    desc->preallocation = tr_file_preallocation::Full;
    auto files = tr_open_files{ [](tr_sys_file_t, uint64_t, int, tr_error* const error) {
        return tr::test::failPreallocation(error, true);
    } };
    auto local_data = tr::LocalData{ torrents_, files };
    local_data.start_workers(2U, files, marshal(), [desc](tr_torrent_id_t) {
        return std::shared_ptr<tr::StorageDescriptor const>{ desc };
    });

    auto done = false;
    local_data.write(
        TorId,
        { .begin = 0U, .end = BlockSize },
        patternBlock(0U, BlockSize),
        [&done](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) {
            EXPECT_TRUE(error.is_out_of_space()) << error;
            EXPECT_TRUE(error.message().contains("injected preallocation error")) << error;
            EXPECT_TRUE(error.message().contains("data.bin")) << error;
            done = true;
        });

    EXPECT_TRUE(pumpUntil([&done]() { return done; }));
    EXPECT_EQ(0U, local_data.enqueued_write_bytes());
    local_data.shutdown();
}

TEST_F(LocalDataWorkersTest, failedReadsKeepTheSystemsMessage)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));
    ASSERT_TRUE(tr_sys_dir_create(pathOf("data.bin"), 0, 0700));

    // a directory fails the open on Windows and the read elsewhere
    auto const expected = [this] {
        auto error = tr_error{};
        if (auto const fd = tr_sys_file_open(pathOf("data.bin"), TR_SYS_FILE_READ, 0, &error); fd != TR_BAD_SYS_FILE) {
            auto byte = uint8_t{};
            std::ignore = tr_sys_file_read_at(fd, &byte, 1U, 0U, nullptr, &error);
            tr_sys_file_close(fd);
        }
        return error;
    }();
    if (!expected) {
        GTEST_SKIP() << "this filesystem can read a directory";
    }

    auto errors = std::vector<tr_error>{};
    local_data->read(TorId, { .begin = 0U, .end = BlockSize }, [&errors](auto, auto, tr_error const& error, auto) {
        errors.push_back(error);
    });
    local_data->test_piece(TorId, 0U, [&errors](auto, auto, tr_error const& error, auto) { errors.push_back(error); });

    EXPECT_TRUE(pumpUntil([&errors]() { return std::size(errors) == 2U; }));
    for (auto const& error : errors) {
        EXPECT_EQ(expected.code(), error.code()) << error;
        EXPECT_TRUE(error.message().contains(expected.message())) << error;
    }

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, blockPatternsAreDistinct)
{
    EXPECT_NE(patternString(0U, BlockSize), patternString(BlockSize, BlockSize));
    EXPECT_NE(patternString(0U, BlockSize), patternString(256U * BlockSize, BlockSize));
}

TEST_F(LocalDataWorkersTest, outOfOrderWritesPreserveBlockContents)
{
    static auto constexpr FileSize = (5U * BlockSize) + 127U;
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));
    auto const order = std::vector<size_t>{ 4U, 1U, 5U, 0U, 3U, 2U };
    auto n_done = size_t{};

    local_data->set_workers_paused(true);
    for (auto const block : order) {
        auto const begin = block * BlockSize;
        writeBlocks(*local_data, begin, std::min(begin + BlockSize, FileSize), [&n_done] { ++n_done; });
    }
    local_data->set_workers_paused(false);

    EXPECT_TRUE(pumpUntil([&n_done, &order]() { return n_done == std::size(order); }));
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));
    EXPECT_EQ(0U, local_data->enqueued_write_bytes());
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, writesLandOnDisk)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done] { ++n_done; });

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
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done] { ++n_done; });
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
    writeBlocks(*local_data, 0U, BlockSize, [&n_done] { ++n_done; });

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
    writeBlocks(*local_data, 0U, BlockSize, [&n_done] { ++n_done; });

    EXPECT_TRUE(pumpUntil([&n_done]() { return n_done == 1U; }));
    EXPECT_EQ(1U, open_files_.take_files_created());
    EXPECT_EQ(patternString(0U, BlockSize), readFile("sub/dir/data.bin"));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, pieceIsHashedFromBufferedBlocks)
{
    static auto constexpr FileSize = size_t{ 65536U };
    static auto constexpr PieceSize = uint32_t{ 32768U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, PieceSize));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, PieceSize, [&n_done] { ++n_done; });
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

TEST_F(LocalDataWorkersTest, retainedCacheIsHalfTheWriteBudgetUpTo32MiB)
{
    static auto constexpr MiB = size_t{ 1024U } * 1024U;
    auto local_data = tr::LocalData{ torrents_, open_files_ };

    // with no budget set, the cache takes the cap
    EXPECT_EQ(32U * MiB, local_data.retained_bytes());

    local_data.set_write_budget(0U);
    EXPECT_EQ(0U, local_data.retained_bytes());
    local_data.set_write_budget(MiB);
    EXPECT_EQ(MiB / 2U, local_data.retained_bytes());
    local_data.set_write_budget(uint64_t{ 128U } * MiB);
    EXPECT_EQ(32U * MiB, local_data.retained_bytes());
}

TEST_F(LocalDataWorkersTest, pieceLargerThanTheRetainedCacheIsReadBack)
{
    static auto constexpr PieceSize = uint32_t{ 32768U };
    // a cache of one block can't hold a whole piece, so it keeps none of it
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", PieceSize } }, PieceSize), 1U, 2U * BlockSize);
    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, PieceSize, [&n_done] { ++n_done; });
    ASSERT_TRUE(pumpUntil([&n_done, n_writes]() { return n_done == n_writes; }));

    auto hash = std::optional<tr_sha1_digest_t>{};
    local_data->test_piece(TorId, 0U, [&hash](tr_torrent_id_t, tr_piece_index_t, tr_error const& error, auto found) {
        EXPECT_FALSE(error);
        hash = found;
    });
    ASSERT_TRUE(pumpUntil([&hash]() { return hash.has_value(); }));
    EXPECT_EQ(tr_sha1::digest(patternString(0U, PieceSize)), *hash);
    EXPECT_EQ(0U, local_data->stats().hashes_from_buffers);
    EXPECT_EQ(1U, local_data->stats().hashes_from_disk);
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, resizingRetainedCapacityEvictsOldestPiecesAndAllowsGrowth)
{
    static auto constexpr PieceSize = size_t{ 32768U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", 2U * PieceSize } }, PieceSize), 1U);
    auto const write_pieces = [&] {
        auto n_done = size_t{};
        auto const n_writes = writeBlocks(*local_data, 0U, 2U * PieceSize, [&n_done] { ++n_done; });
        EXPECT_TRUE(pumpUntil([&n_done, n_writes] { return n_done == n_writes; }));
    };
    auto const hash_pieces = [&] {
        for (auto piece = tr_piece_index_t{}; piece < 2U; ++piece) {
            auto hash = std::optional<tr_sha1_digest_t>{};
            local_data->test_piece(TorId, piece, [&hash](tr_torrent_id_t, tr_piece_index_t, tr_error const& error, auto found) {
                EXPECT_FALSE(error);
                hash = found;
            });
            ASSERT_TRUE(pumpUntil([&hash]() { return hash.has_value(); }));
            EXPECT_EQ(tr_sha1::digest(patternString(piece * PieceSize, PieceSize)), *hash);
            EXPECT_EQ(1U, local_data->stats().hashes_from_disk);
        }
    };

    write_pieces();
    local_data->set_write_budget(2U * PieceSize);
    hash_pieces();
    EXPECT_EQ(1U, local_data->stats().hashes_from_buffers);

    local_data->set_write_budget(4U * PieceSize);
    write_pieces();
    hash_pieces();
    EXPECT_EQ(3U, local_data->stats().hashes_from_buffers);
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, pieceIsReadBackWhenItsBuffersAreGone)
{
    static auto constexpr FileSize = size_t{ 65536U };
    static auto constexpr PieceSize = uint32_t{ 32768U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, PieceSize));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, PieceSize, [&n_done] { ++n_done; });
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
    writeBlocks(*local_data, 0U, 2U * BlockSize, [&order] { order.emplace_back("write"); });

    // The move picks its dir when it starts. (The move itself fails
    // since there's no real torrent. Only the ordering matters here.)
    local_data->move(
        TorId,
        [&order] {
            order.emplace_back("parent");
            return std::string{ "/new" };
        },
        [&order](auto, auto, auto const&) { order.emplace_back("move"); });

    // ...and this read waits for the barrier
    local_data->read(TorId, { .begin = 0U, .end = BlockSize }, [&order](auto, auto, auto const&, auto) {
        order.emplace_back("read");
    });

    // nothing has finished while the workers are held
    EXPECT_TRUE(std::empty(order));
    EXPECT_EQ(2U * BlockSize, local_data->enqueued_write_bytes());

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&order]() { return std::size(order) == 5U; }));
    auto const expected = std::vector<std::string>{ "write", "write", "parent", "move", "read" };
    EXPECT_EQ(expected, order);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, closingAFileWaitsOnlyForOpsThatTouchIt)
{
    auto const local_data = makeLocalData(makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", BlockSize } }, BlockSize));

    // a write to a.bin is in flight...
    local_data->set_workers_paused(true);
    auto n_written = size_t{};
    writeBlocks(*local_data, 0U, BlockSize, [&n_written] { ++n_written; });

    // ...so closing b.bin runs at once, while closing a.bin waits
    auto b_closed = false;
    local_data->close_file(TorId, 1U, [&b_closed](tr_torrent_id_t) { b_closed = true; });
    EXPECT_TRUE(b_closed);
    auto a_closed = false;
    local_data->close_file(TorId, 0U, [&a_closed](tr_torrent_id_t) { a_closed = true; });
    EXPECT_FALSE(a_closed);

    // ...and the queued close of a.bin does not hold back a read of b.bin.
    // With b.bin open and cached, the read needs no worker and completes
    // at once, wherever the platform can read from the cache alone.
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(BlockSize, BlockSize)));
    auto const cached = canReadCached("b.bin");
    openInPool(1U, "b.bin");
    auto read_done = false;
    local_data->read(TorId, { .begin = BlockSize, .end = 2U * BlockSize }, [&read_done](auto, auto, auto const&, auto) {
        read_done = true;
    });
    EXPECT_EQ(cached, read_done);

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&a_closed, &read_done]() { return a_closed && read_done; }));
    EXPECT_EQ(1U, n_written);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, secondWriteOfABlockInFlightFails)
{
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", 2U * BlockSize } }, 32768U));

    local_data->set_workers_paused(true);
    auto n_written = size_t{};
    writeBlocks(*local_data, 0U, BlockSize, [&n_written] { ++n_written; });

    // the copy fails at once and does not join the queue
    auto code = std::optional<tr_error::code_t>{};
    local_data->write(
        TorId,
        { .begin = 0U, .end = BlockSize },
        patternBlock(0U),
        [&code](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) { code = error.code(); });
    EXPECT_EQ(EINVAL, code);
    EXPECT_EQ(BlockSize, local_data->enqueued_write_bytes());

    // the first write still lands, and the gate still drains
    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&n_written]() { return n_written == 1U; }));
    EXPECT_EQ(0U, local_data->enqueued_write_bytes());
    auto closed = false;
    local_data->close_torrent(TorId, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_TRUE(pumpUntil([&closed]() { return closed; }));
    EXPECT_EQ(patternString(0U, BlockSize), readFile("data.bin").substr(0U, BlockSize));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, combinedWritesDeliverEveryCallbackBeforeAQueuedBarrier)
{
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", 2U * BlockSize } }, 32768U), 1U);
    auto n_done = size_t{};
    auto closed = false;

    local_data->set_workers_paused(true);
    writeBlocks(*local_data, 0U, 2U * BlockSize, [&] {
        EXPECT_FALSE(closed);
        if (++n_done == 1U) {
            local_data->close_torrent(TorId, [&](tr_torrent_id_t) {
                EXPECT_EQ(2U, n_done);
                closed = true;
            });
        }
    });
    local_data->set_workers_paused(false);

    EXPECT_TRUE(pumpUntil([&closed]() { return closed; }));
    EXPECT_EQ(2U, n_done);
    EXPECT_EQ(1U, local_data->stats().write_runs);
    EXPECT_EQ(2U, local_data->stats().blocks_written);
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, combinedWritesDeliverEveryCallbackBeforeAQueuedFileClose)
{
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", 2U * BlockSize } }, 32768U), 1U);
    auto n_done = size_t{};
    auto closed = false;

    local_data->set_workers_paused(true);
    writeBlocks(*local_data, 0U, 2U * BlockSize, [&] {
        EXPECT_FALSE(closed);
        if (++n_done == 1U) {
            local_data->close_file(TorId, 0U, [&](tr_torrent_id_t) {
                EXPECT_EQ(2U, n_done);
                closed = true;
            });
        }
    });
    local_data->set_workers_paused(false);

    EXPECT_TRUE(pumpUntil([&closed]() { return closed; }));
    EXPECT_EQ(2U, n_done);
    EXPECT_EQ(1U, local_data->stats().write_runs);
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, emptyWriteSucceedsBesideAWriteInFlight)
{
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, 32768U), 1U);
    auto n_done = size_t{};

    local_data->set_workers_paused(true);
    writeBlocks(*local_data, 0U, BlockSize, [&n_done] { ++n_done; });

    // an empty span at the same offset has nothing to write
    auto empty_done = false;
    local_data->write(
        TorId,
        { .begin = 0U, .end = 0U },
        std::make_unique<tr::LocalData::BlockData>(),
        [&empty_done](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            empty_done = true;
        });
    EXPECT_TRUE(empty_done);
    local_data->set_workers_paused(false);

    EXPECT_TRUE(pumpUntil([&n_done]() { return n_done == 1U; }));
    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownDeliversEveryCallback)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done] { ++n_done; });

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

TEST_F(LocalDataWorkersTest, unwritableFileFailsTheWrite)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 32768U));

    // Nobody can open a directory for writing, on any platform or as root.
    ASSERT_TRUE(tr_sys_dir_create(pathOf("data.bin"), 0, 0700));

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

    // a failed write counts for nothing
    auto const stats = local_data->stats();
    EXPECT_EQ(0U, stats.write_runs);
    EXPECT_EQ(0U, stats.blocks_written);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, readMissCompletesLaterFromAWorker)
{
    static auto constexpr FileSize = size_t{ 4U * BlockSize };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 2U * BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, FileSize)));

    // data.bin isn't open yet, so the page cache can't answer
    auto result = ReadResult{};
    readInto(*local_data, BlockSize, BlockSize, result);
    EXPECT_FALSE(result.done);

    EXPECT_TRUE(pumpUntil([&result]() { return result.done; }));
    EXPECT_FALSE(result.error) << result.error;
    EXPECT_EQ(patternString(BlockSize, BlockSize), result.data);

    auto const stats = local_data->stats();
    EXPECT_EQ(0U, stats.cached_reads);
    EXPECT_EQ(1U, stats.read_runs);
    EXPECT_EQ(1U, stats.blocks_read);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, cachedReadCompletesInline)
{
    static auto constexpr FileSize = size_t{ 2U * BlockSize };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, FileSize)));
    if (!canReadCached("data.bin")) {
        GTEST_SKIP() << "this platform or filesystem can't read from the page cache alone";
    }

    // the first read opens the file on a worker...
    auto first = ReadResult{};
    readInto(*local_data, 0U, BlockSize, first);
    ASSERT_TRUE(pumpUntil([&first]() { return first.done; }));

    // ...so the next one is answered before read() returns
    auto second = ReadResult{};
    readInto(*local_data, BlockSize, BlockSize, second);
    EXPECT_TRUE(second.done);
    EXPECT_FALSE(second.error) << second.error;
    EXPECT_EQ(patternString(BlockSize, BlockSize), second.data);

    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.cached_reads);
    EXPECT_EQ(1U, stats.blocks_read);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, adjacentReadsAreCombined)
{
    static auto constexpr NBlocks = size_t{ 8U };
    static auto constexpr FileSize = NBlocks * BlockSize;
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, 4U * BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, FileSize)));

    // reads issued in one pass, out of order...
    auto results = std::vector<ReadResult>(NBlocks);
    for (auto const block : { 5U, 0U, 7U, 2U, 1U, 6U, 3U, 4U }) {
        readInto(*local_data, block * BlockSize, BlockSize, results[block]);
    }

    EXPECT_TRUE(pumpUntil([&results]() { return std::ranges::all_of(results, &ReadResult::done); }));
    for (auto block = size_t{}; block < NBlocks; ++block) {
        EXPECT_FALSE(results[block].error) << results[block].error;
        EXPECT_EQ(patternString(block * BlockSize, BlockSize), results[block].data) << block;
    }

    // ...come off the disk as one
    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.read_runs);
    EXPECT_EQ(NBlocks, stats.blocks_read);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, duplicateReadsShareOneDiskRead)
{
    static auto constexpr FileSize = size_t{ 2U * BlockSize };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, FileSize)));

    // two peers asking for the same block
    auto results = std::vector<ReadResult>(2U);
    for (auto& result : results) {
        readInto(*local_data, BlockSize, BlockSize, result);
    }

    EXPECT_TRUE(pumpUntil([&results]() { return std::ranges::all_of(results, &ReadResult::done); }));
    for (auto const& result : results) {
        EXPECT_FALSE(result.error) << result.error;
        EXPECT_EQ(patternString(BlockSize, BlockSize), result.data);
    }

    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.read_runs);
    EXPECT_EQ(2U, stats.blocks_read);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, failedReadDoesNotFailItsNeighbor)
{
    // a.bin is on disk, b.bin isn't
    auto const local_data = makeLocalData(makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", BlockSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, BlockSize)));

    // adjacent, so they're tried as one read first
    auto a = ReadResult{};
    auto b = ReadResult{};
    readInto(*local_data, 0U, BlockSize, a);
    readInto(*local_data, BlockSize, BlockSize, b);

    EXPECT_TRUE(pumpUntil([&a, &b]() { return a.done && b.done; }));
    EXPECT_FALSE(a.error) << a.error;
    EXPECT_EQ(patternString(0U, BlockSize), a.data);
    EXPECT_TRUE(b.error);
    EXPECT_TRUE(std::empty(b.data));

    auto const stats = local_data->stats();
    EXPECT_EQ(1U, stats.read_runs);
    EXPECT_EQ(1U, stats.blocks_read);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, barriersWaitForReadsAndHoldBackLaterOnes)
{
    static auto constexpr FileSize = size_t{ 2U * BlockSize };
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, FileSize)));

    auto events = std::vector<std::string>{};
    local_data->read(TorId, { .begin = 0U, .end = BlockSize }, [&events](auto, auto, tr_error const& error, auto) {
        EXPECT_FALSE(error) << error;
        events.emplace_back("read before");
    });
    local_data->close_torrent(TorId, [&events](tr_torrent_id_t) { events.emplace_back("closed"); });
    local_data->read(TorId, { .begin = BlockSize, .end = FileSize }, [&events](auto, auto, tr_error const& error, auto) {
        EXPECT_FALSE(error) << error;
        events.emplace_back("read after");
    });
    EXPECT_TRUE(std::empty(events));

    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 3U; }));
    EXPECT_EQ((std::vector<std::string>{ "read before", "closed", "read after" }), events);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownDeliversReadsThatCompletionsStart)
{
    // Neither file is open yet, so the page cache answers neither read.
    auto const local_data = makeLocalData(makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", BlockSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(BlockSize, BlockSize)));

    // a.bin's completion starts a read of b.bin, as a peer refilling its
    // output buffer does
    auto a = ReadResult{};
    auto b = ReadResult{};
    local_data->read(
        TorId,
        { .begin = 0U, .end = BlockSize },
        [&local_data, &a, &b](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto) {
            a.done = true;
            a.error = error;
            readInto(*local_data, BlockSize, BlockSize, b);
        });

    // ...and shutdown() delivers both, well inside its 10 s stall warning
    auto const started = std::chrono::steady_clock::now();
    local_data->shutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{ 5 });
    EXPECT_TRUE(a.done);
    EXPECT_FALSE(a.error) << a.error;
    EXPECT_TRUE(b.done);
    EXPECT_FALSE(b.error) << b.error;
    EXPECT_EQ(patternString(BlockSize, BlockSize), b.data);
}

TEST_F(LocalDataWorkersTest, queuedHashWaitsForAWriteThatOverlapsIt)
{
    // Piece 0 is blocks 0-2: a.bin, then the start of b.bin. One worker, so
    // the write of block 0 is delivered before the write of block 2.
    auto const local_data = makeLocalData(
        makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", 2U * BlockSize } }, 3U * BlockSize),
        1U);

    auto events = std::vector<std::string>{};
    auto const on_write = [&events](std::string_view const event) {
        return [&events, event](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            events.emplace_back(event);
        };
    };

    // Closing a.bin waits for block 0's write, and holds back the hash of
    // piece 0. The write of block 2 is in b.bin, so it goes ahead.
    local_data->set_workers_paused(true);
    local_data->write(TorId, { .begin = 0U, .end = BlockSize }, patternBlock(0U), on_write("wrote block 0"));
    local_data->close_file(TorId, 0U, [&events](tr_torrent_id_t) { events.emplace_back("closed a.bin"); });
    local_data->test_piece(TorId, 0U, [&events](auto, auto, auto const&, auto) { events.emplace_back("hashed"); });
    local_data->write(
        TorId,
        { .begin = 2U * BlockSize, .end = 3U * BlockSize },
        patternBlock(2U * BlockSize),
        on_write("wrote block 2"));
    local_data->set_workers_paused(false);

    // The close brings the hash up while block 2 is still being written.
    // The hash waits for that write instead of reading under it.
    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 4U; }));
    EXPECT_EQ((std::vector<std::string>{ "wrote block 0", "closed a.bin", "wrote block 2", "hashed" }), events);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, writesThatArriveDuringASweepWaitForTheNext)
{
    // Block 0 is first.bin, blocks 1-6 are data.bin. One worker, so the
    // callbacks arrive in the order the writes reached the disk.
    auto const desc = makePreallocatingDescriptor({ { "first.bin", BlockSize }, { "data.bin", 6U * BlockSize } });
    auto gate = PreallocationGate{};
    auto files = tr_open_files{ gate.hook() };
    auto local_data = tr::LocalData{ torrents_, files };
    local_data.start_workers(1U, files, marshal(), [desc](tr_torrent_id_t) {
        return std::shared_ptr<tr::StorageDescriptor const>{ desc };
    });

    auto order = std::vector<size_t>{};
    auto const write_block = [&local_data, &order](size_t const block) {
        writeBlocks(local_data, block * BlockSize, (block + 1U) * BlockSize, [&order, block] { order.push_back(block); });
    };

    // the sweep is blocks 0, 2 and 6, and the worker stalls on its first
    local_data.set_workers_paused(true);
    write_block(0U);
    write_block(2U);
    write_block(6U);
    local_data.set_workers_paused(false);
    ASSERT_TRUE(gate.waitForFile(1U));

    // block 4 arrives ahead of where the sweep has got to...
    write_block(4U);
    gate.release(2U);

    // ...and is still written after the rest of the sweep
    EXPECT_TRUE(pumpUntil([&order]() { return std::size(order) == 4U; }));
    EXPECT_EQ((std::vector<size_t>{ 0U, 2U, 6U, 4U }), order);

    local_data.shutdown();
}

TEST_F(LocalDataWorkersTest, readsThatArriveDuringASweepWaitForTheNext)
{
    // Blocks 0 and 7 are files to create. Blocks 1, 3 and 5 each start a
    // file of their own, so none is open yet and the page cache answers none.
    // One worker, so the callbacks arrive in the order the disk was used.
    auto const desc = makePreallocatingDescriptor(
        { { "first.bin", BlockSize },
          { "a.bin", 2U * BlockSize },
          { "b.bin", 2U * BlockSize },
          { "c.bin", 2U * BlockSize },
          { "last.bin", BlockSize } });
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(BlockSize, 2U * BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(3U * BlockSize, 2U * BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("c.bin"), patternString(5U * BlockSize, 2U * BlockSize)));
    auto gate = PreallocationGate{};
    auto files = tr_open_files{ gate.hook() };
    auto local_data = tr::LocalData{ torrents_, files };
    local_data.start_workers(1U, files, marshal(), [desc](tr_torrent_id_t) {
        return std::shared_ptr<tr::StorageDescriptor const>{ desc };
    });

    auto events = std::vector<std::string>{};
    auto const write_block = [&local_data, &events](size_t const block) {
        writeBlocks(local_data, block * BlockSize, (block + 1U) * BlockSize, [&events, block] {
            events.emplace_back(fmt::format("wrote {}", block));
        });
    };
    auto const read_block = [&local_data, &events](size_t const block) {
        local_data.read(
            TorId,
            { .begin = block * BlockSize, .end = (block + 1U) * BlockSize },
            [&events, block](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto data) {
                EXPECT_FALSE(error) << error;
                ASSERT_NE(nullptr, data);
                EXPECT_EQ(
                    patternString(block * BlockSize, BlockSize),
                    (std::string{ reinterpret_cast<char const*>(data->data()), data->size() }));
                events.emplace_back(fmt::format("read {}", block));
            });
    };

    // stall the worker creating first.bin, and queue the rest behind it
    write_block(0U);
    ASSERT_TRUE(gate.waitForFile(1U));
    read_block(1U);
    read_block(5U);
    pumpOnce();
    write_block(7U);

    // the read sweep is blocks 1 and 5. The worker reads block 1, then
    // takes its turn at writing and stalls creating last.bin.
    gate.release();
    ASSERT_TRUE(gate.waitForFile(2U));

    // block 3 arrives ahead of where the sweep has got to...
    read_block(3U);
    pumpOnce();
    gate.release();

    // ...and is still read after the rest of the sweep
    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 5U; }));
    EXPECT_EQ((std::vector<std::string>{ "wrote 0", "read 1", "wrote 7", "read 5", "read 3" }), events);

    local_data.shutdown();
}

TEST_F(LocalDataWorkersTest, hashesTakeTurnsWithReadsAndWrites)
{
    // Pieces 0-2 are on disk in their own files, none of them open yet, so
    // they are hashed from the disk and the page cache can't answer a read.
    // Piece 3 is written. One worker, so the callbacks arrive in the order
    // the worker took the ops.
    auto const local_data = makeLocalData(
        makeDescriptor(
            { { "a.bin", BlockSize }, { "b.bin", BlockSize }, { "c.bin", BlockSize }, { "d.bin", BlockSize } },
            BlockSize),
        1U);
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(BlockSize, BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("c.bin"), patternString(2U * BlockSize, BlockSize)));

    auto events = std::vector<std::string>{};
    auto const hash_piece = [&local_data, &events](tr_piece_index_t const piece) {
        local_data->test_piece(TorId, piece, [&events](auto, tr_piece_index_t const hashed, tr_error const& error, auto) {
            EXPECT_FALSE(error) << error;
            events.emplace_back(fmt::format("hashed {}", hashed));
        });
    };

    // two hashes waiting, then a read and a write
    local_data->set_workers_paused(true);
    hash_piece(0U);
    hash_piece(1U);
    local_data->read(TorId, { .begin = 2U * BlockSize, .end = 3U * BlockSize }, [&events](auto, auto, auto const& error, auto) {
        EXPECT_FALSE(error) << error;
        events.emplace_back("read");
    });
    pumpOnce();
    writeBlocks(*local_data, 3U * BlockSize, 4U * BlockSize, [&events] { events.emplace_back("wrote"); });
    local_data->set_workers_paused(false);

    // the second hash waits for its next turn
    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 4U; }));
    EXPECT_EQ((std::vector<std::string>{ "hashed 0", "read", "wrote", "hashed 1" }), events);

    local_data->shutdown();
}
