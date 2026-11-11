// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
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
#include <thread>
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
        tr::LocalData::MoveTarget const& target,
        std::function<void()> const& on_placed) override
    {
        moved_to = target.parent;
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

    [[nodiscard]] tr_error rename(
        [[maybe_unused]] tr_torrent_id_t const id,
        std::string_view const oldpath,
        std::string_view const newname) override
    {
        renamed_from = std::string{ oldpath };
        renamed_to = std::string{ newname };
        return {};
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
        [] { return tr::LocalData::MoveTarget{ .parent = "/new" }; },
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
        [] { return tr::LocalData::MoveTarget{ .parent = "/newer" }; },
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
        [] { return tr::LocalData::MoveTarget{}; },
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
        [] { return tr_error{}; },
        [&rename_called](tr_torrent_id_t tor_id, tr_error const& error) {
            rename_called = true;
            EXPECT_EQ(8, tor_id);
            EXPECT_FALSE(error);
        });
    EXPECT_TRUE(rename_called);
    EXPECT_EQ("old", raw_backend->renamed_from);
    EXPECT_EQ("new", raw_backend->renamed_to);

    // a failed check keeps the rename off the disk
    raw_backend->renamed_from.clear();
    auto rename_rejected = false;
    local_data.rename(
        8,
        "old",
        "new",
        [] { return tr_error::from_errno(EINVAL); },
        [&rename_rejected](tr_torrent_id_t, tr_error const& error) {
            rename_rejected = true;
            EXPECT_TRUE(error);
        });
    EXPECT_TRUE(rename_rejected);
    EXPECT_EQ("", raw_backend->renamed_from);

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
        [] { return tr::LocalData::MoveTarget{ .parent = "/new" }; },
        [&errors](auto, auto, tr_error const& error) { errors.push_back(error); });
    local_data.remove(1, {}, [&errors](auto, tr_error const& error) { errors.push_back(error); });

    ASSERT_EQ(5U, std::size(errors));
    for (auto const& error : errors) {
        EXPECT_EQ(expected.message(), error.message());
        EXPECT_EQ(expected.code(), error.code());
    }

    local_data.shutdown();
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

    // Run the marshaled functions, and the ones those marshal, until none is left.
    void runMarshaled()
    {
        for (;;) {
            auto fn = std::function<void()>{};
            {
                auto const lock = std::scoped_lock{ marshal_mutex_ };
                if (std::empty(marshaled_)) {
                    return;
                }
                fn = std::move(marshaled_.front());
                marshaled_.pop_front();
            }
            fn();
        }
    }

    // Run marshaled functions until `pred()` holds. False on timeout.
    template<typename Pred>
    [[nodiscard]] bool pumpUntil(Pred const& pred)
    {
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };

        for (;;) {
            runMarshaled();

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

    // The move picks its dir when it starts.
    local_data->move(
        TorId,
        [this, &order] {
            order.emplace_back("parent");
            return tr::LocalData::MoveTarget{ .parent = pathOf("new") };
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

TEST_F(LocalDataWorkersTest, moveLeavesTheDiskWorkToAWorker)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    auto const session_thread = std::this_thread::get_id();

    local_data->set_workers_paused(true);
    auto events = std::vector<std::string>{};
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        [&events, session_thread](auto, auto, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            EXPECT_EQ(session_thread, std::this_thread::get_id());
            events.emplace_back("moved");
        },
        [this, &events, session_thread](auto, auto) {
            EXPECT_TRUE(tr_sys_path_exists(pathOf("new/data.bin")));
            EXPECT_EQ(session_thread, std::this_thread::get_id());
            events.emplace_back("placed");
        });

    // the call returned without touching the disk
    EXPECT_TRUE(std::empty(events));
    EXPECT_TRUE(tr_sys_path_exists(pathOf("data.bin")));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("new/data.bin")));

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 2U; }));
    EXPECT_EQ((std::vector<std::string>{ "placed", "moved" }), events);
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, aFailedMoveReportsItAndReleasesTheOpsBehindIt)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("blocker"), "not a directory"sv));
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));

    auto order = std::vector<std::string>{};
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("blocker/new") }; },
        [&order](auto, auto, tr_error const& error) {
            EXPECT_TRUE(error);
            order.emplace_back("moved");
        },
        [](auto, auto) { FAIL() << "a failed move places nothing"; });
    local_data->close_torrent(TorId, [&order](tr_torrent_id_t) { order.emplace_back("closed"); });

    EXPECT_TRUE(pumpUntil([&order]() { return std::size(order) == 2U; }));
    EXPECT_EQ((std::vector<std::string>{ "moved", "closed" }), order);
    EXPECT_TRUE(tr_sys_path_exists(pathOf("data.bin")));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, opsBehindAMoveFindTheNewLocation)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const before = makeDescriptor({ { "data.bin", BlockSize } }, BlockSize);
    auto after = std::make_shared<tr::StorageDescriptor>(*before);
    after->download_dir = pathOf("new");
    after->current_dir = pathOf("new");

    auto current = std::shared_ptr<tr::StorageDescriptor const>{ before };
    auto local_data = tr::LocalData{ torrents_, open_files_ };
    local_data.start_workers(2U, open_files_, marshal(), [&current](tr_torrent_id_t) { return current; });

    local_data.close_torrent(TorId);
    local_data.move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        {},
        [&current, &after](auto, auto) { current = after; });

    auto result = ReadResult{};
    readInto(local_data, 0U, BlockSize, result);

    EXPECT_TRUE(pumpUntil([&result]() { return result.done; }));
    EXPECT_FALSE(result.error) << result.error;
    EXPECT_EQ(patternString(0U, BlockSize), result.data);

    local_data.shutdown();
}

TEST_F(LocalDataWorkersTest, renameChecksFirstAndLeavesTheDiskWorkToAWorker)
{
    ASSERT_TRUE(tr_file_save(pathOf("old.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "old.bin", BlockSize } }, BlockSize));

    local_data->set_workers_paused(true);
    auto checked = false;
    auto renamed = false;
    local_data->rename(
        TorId,
        "old.bin",
        "new.bin",
        [&checked] {
            checked = true;
            return tr_error{};
        },
        [&renamed](tr_torrent_id_t, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            renamed = true;
        });

    EXPECT_TRUE(checked);
    EXPECT_FALSE(renamed);
    EXPECT_TRUE(tr_sys_path_exists(pathOf("old.bin")));

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&renamed]() { return renamed; }));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("old.bin")));
    EXPECT_TRUE(tr_sys_path_exists(pathOf("new.bin")));

    // a failed check never reaches the disk
    auto rejected = false;
    local_data->rename(
        TorId,
        "new.bin",
        "newer.bin",
        [] { return tr_error::from_errno(EINVAL); },
        [&rejected](tr_torrent_id_t, tr_error const& error) {
            EXPECT_TRUE(error);
            rejected = true;
        });
    EXPECT_TRUE(rejected);
    EXPECT_TRUE(tr_sys_path_exists(pathOf("new.bin")));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, closingATorrentLeavesTheClosesToALane)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    openInPool(0U, "data.bin");
    auto const file = std::weak_ptr{ open_files_.get(TorId, 0U, false) };
    ASSERT_FALSE(file.expired());

    // The barrier is done at once: the pool has let go of the file...
    local_data->set_workers_paused(true);
    auto closed = false;
    local_data->close_torrent(TorId, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_TRUE(closed);
    EXPECT_FALSE(open_files_.get(TorId, 0U, false));

    // ...which stays open until a lane gets to it
    EXPECT_FALSE(file.expired());

    // a removal enqueued later finds it closed
    auto was_closed = std::optional<bool>{};
    auto removed = false;
    local_data->remove(
        TorId,
        [&file, &was_closed](std::string_view const filename, tr_error* const error) {
            was_closed = file.expired();
            return tr_sys_path_remove(filename, error);
        },
        [&removed](tr_torrent_id_t, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            removed = true;
        });
    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&removed]() { return removed; }));
    EXPECT_EQ(std::optional{ true }, was_closed);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownClosesTheFilesLeftToALane)
{
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(BlockSize, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", BlockSize } }, BlockSize));
    openInPool(0U, "a.bin");
    openInPool(1U, "b.bin");
    auto const a = std::weak_ptr{ open_files_.get(TorId, 0U, false) };
    auto const b = std::weak_ptr{ open_files_.get(TorId, 1U, false) };

    // closing one file leaves the other in the pool
    local_data->set_workers_paused(true);
    auto closed = false;
    local_data->close_file(TorId, 0U, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_TRUE(closed);
    EXPECT_FALSE(open_files_.get(TorId, 0U, false));
    EXPECT_TRUE(open_files_.get(TorId, 1U, false));
    EXPECT_FALSE(a.expired());

    local_data->shutdown();
    EXPECT_TRUE(a.expired());
    EXPECT_FALSE(b.expired());
}

TEST_F(LocalDataWorkersTest, adminOpPendingCoversWholeTorrentBarriers)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    EXPECT_FALSE(local_data->admin_op_pending(TorId));

    // a write in flight makes the close of its file wait in the queue...
    local_data->set_workers_paused(true);
    writeBlocks(*local_data, 0U, BlockSize, [] {});
    auto closed = false;
    local_data->close_file(TorId, 0U, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_FALSE(closed);

    // ...and a queued close of one file doesn't count
    EXPECT_FALSE(local_data->admin_op_pending(TorId));

    auto moved = false;
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        [&moved](auto, auto, tr_error const&) { moved = true; });
    EXPECT_TRUE(local_data->admin_op_pending(TorId));

    local_data->set_workers_paused(false);
    EXPECT_TRUE(pumpUntil([&moved]() { return moved; }));
    EXPECT_FALSE(local_data->admin_op_pending(TorId));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownFinishesAMoveInFlight)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeLocalData(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));

    auto events = std::vector<std::string>{};
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        [&events](auto, auto, tr_error const&) { events.emplace_back("moved"); },
        [&events](auto, auto) { events.emplace_back("placed"); });

    local_data->shutdown();
    EXPECT_EQ((std::vector<std::string>{ "placed", "moved" }), events);
    EXPECT_TRUE(tr_sys_path_exists(pathOf("new/data.bin")));
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

// ---

namespace
{

// Exercises the threaded backend with its threads' work done on the test
// thread, so that a test can say which thread goes next, or let a seed say.
class LocalDataThreadlessTest : public LocalDataWorkersTest
{
protected:
    using DiskThread = tr::detail::DiskThread;

    [[nodiscard]] auto makeThreadless(
        tr::LocalData::DescriptorProvider provider,
        size_t const n_workers = 2U,
        uint32_t const seed = 1U)
    {
        auto local_data = std::make_unique<tr::LocalData>(torrents_, open_files_);
        tr::detail::start_threadless(*local_data, n_workers, seed, open_files_, marshal(), std::move(provider));
        return local_data;
    }

    [[nodiscard]] auto makeThreadless(
        std::shared_ptr<tr::StorageDescriptor const> desc,
        size_t const n_workers = 2U,
        uint32_t const seed = 1U)
    {
        return makeThreadless(
            [desc = std::move(desc)](tr_torrent_id_t const id) { return id == TorId ? desc : nullptr; },
            n_workers,
            seed);
    }

    // A copy of `desc` for another torrent, with its files under `subdir`.
    [[nodiscard]] std::shared_ptr<tr::StorageDescriptor const> inDir(
        tr::StorageDescriptor const& desc,
        tr_torrent_id_t const id,
        std::string_view const subdir) const
    {
        auto copy = std::make_shared<tr::StorageDescriptor>(desc);
        copy->id = id;
        copy->download_dir = pathOf(subdir);
        copy->current_dir = pathOf(subdir);
        return copy;
    }

    // Like tr_file_save(), but makes the file's dir too.
    void saveFile(std::string_view const subpath, std::string_view const contents) const
    {
        auto const path = pathOf(subpath);
        ASSERT_TRUE(tr_sys_dir_create(tr_sys_path_dirname(path), TR_SYS_DIR_CREATE_PARENTS, 0700));
        ASSERT_TRUE(tr_file_save(path, contents));
    }

    // Take steps until no thread of `which` has one left. Returns how many.
    static size_t stepAll(tr::LocalData& local_data, DiskThread const which = DiskThread::Any)
    {
        auto n_steps = size_t{};
        while (tr::detail::step(local_data, which)) {
            ++n_steps;
        }
        return n_steps;
    }

    // Do all the work that is waiting, then deliver its callbacks.
    static void runAll(tr::LocalData& local_data)
    {
        stepAll(local_data);
        tr::detail::deliver(local_data);
    }
};

} // namespace

TEST_F(LocalDataThreadlessTest, workWaitsForTheSessionThread)
{
    static auto constexpr FileSize = 2U * BlockSize;
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));

    // the first write's callback enqueues the second write
    auto events = std::vector<std::string>{};
    writeBlocks(*local_data, 0U, BlockSize, [&local_data, &events] {
        events.emplace_back("first");
        writeBlocks(*local_data, BlockSize, FileSize, [&events] { events.emplace_back("second"); });

        // which doesn't run inside this callback...
        EXPECT_EQ(1U, std::size(events));
        EXPECT_EQ(BlockSize, local_data->enqueued_write_bytes());
    });

    // ...as the first didn't run inside the call that enqueued it
    EXPECT_TRUE(std::empty(events));
    EXPECT_EQ(BlockSize, local_data->enqueued_write_bytes());
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    runMarshaled();
    EXPECT_EQ((std::vector<std::string>{ "first", "second" }), events);
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));
    EXPECT_EQ(0U, local_data->enqueued_write_bytes());

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, aSeedReplaysItsSchedule)
{
    static auto constexpr OtherId = tr_torrent_id_t{ TorId + 1 };
    static auto constexpr NWritten = size_t{ 8U };
    static auto constexpr NRead = size_t{ 3U };

    // Eight blocks to write in two files, then three files to read. Each
    // is read once and never written, so no file is open when its read
    // arrives and the page cache answers none of them.
    auto const layout = makeDescriptor(
        { { "w0.bin", 4U * BlockSize },
          { "w1.bin", 4U * BlockSize },
          { "r0.bin", BlockSize },
          { "r1.bin", BlockSize },
          { "r2.bin", BlockSize } },
        BlockSize);

    // Writes, hashes, reads, a file close, and a move on one torrent, with
    // another torrent's move and removal on the lanes. Returns the
    // callbacks in the order they arrived.
    auto n_runs = size_t{};
    auto const run = [this, &layout, &n_runs](uint32_t const seed) {
        auto const dir = fmt::format("run-{}", n_runs++);
        auto const mine = inDir(*layout, TorId, dir + "/mine");
        auto const theirs = inDir(*layout, OtherId, dir + "/theirs");
        for (auto file = size_t{}; file < NRead; ++file) {
            auto const begin = (NWritten + file) * BlockSize;
            saveFile(fmt::format("{}/mine/r{}.bin", dir, file), patternString(begin, BlockSize));
            saveFile(fmt::format("{}/theirs/r{}.bin", dir, file), patternString(begin, BlockSize));
        }

        auto files = tr_open_files{};
        auto local_data = tr::LocalData{ torrents_, files };
        tr::detail::start_threadless(local_data, 3U, seed, files, marshal(), [mine, theirs](tr_torrent_id_t const id) {
            return id == TorId ? mine : theirs;
        });

        auto events = std::vector<std::string>{};

        // each block is hashed once it is written, as a torrent's pieces are
        for (auto const block : { 5U, 0U, 7U, 2U, 1U, 6U, 3U, 4U }) {
            local_data.write(
                TorId,
                { .begin = block * BlockSize, .end = (block + 1U) * BlockSize },
                patternBlock(block * BlockSize),
                [&local_data, &events, block](tr_torrent_id_t, tr_byte_span_t, tr_error const& error) {
                    EXPECT_FALSE(error) << error;
                    events.emplace_back(fmt::format("wrote {}", block));
                    local_data.test_piece(TorId, block, [&events, block](auto, auto, tr_error const& hash_error, auto hash) {
                        EXPECT_FALSE(hash_error) << hash_error;
                        EXPECT_EQ(tr_sha1::digest(patternString(block * BlockSize, BlockSize)), hash);
                        events.emplace_back(fmt::format("hashed {}", block));
                    });
                });
        }

        for (auto file = size_t{}; file < NRead; ++file) {
            auto const begin = (NWritten + file) * BlockSize;
            local_data.read(
                TorId,
                { .begin = begin, .end = begin + BlockSize },
                [&events, file](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto) {
                    EXPECT_FALSE(error) << error;
                    events.emplace_back(fmt::format("read {}", file));
                });
        }

        local_data.close_file(TorId, 1U, [&events](tr_torrent_id_t) { events.emplace_back("closed w1.bin"); });

        local_data.move(
            OtherId,
            [this, &dir] { return tr::LocalData::MoveTarget{ .parent = pathOf(dir + "/theirs-moved") }; },
            [&local_data, &events](tr_torrent_id_t, std::string_view, tr_error const& error) {
                EXPECT_FALSE(error) << error;
                events.emplace_back("moved theirs");
                local_data.remove(OtherId, {}, [&events](tr_torrent_id_t, tr_error const&) {
                    events.emplace_back("removed theirs");
                });
            });

        runMarshaled();

        // the move waits for everything before it on its torrent
        local_data.move(
            TorId,
            [this, &dir] { return tr::LocalData::MoveTarget{ .parent = pathOf(dir + "/mine-moved") }; },
            [&events](tr_torrent_id_t, std::string_view, tr_error const& error) {
                EXPECT_FALSE(error) << error;
                events.emplace_back("moved mine");
            });
        runMarshaled();

        EXPECT_EQ((2U * NWritten) + NRead + 4U, std::size(events));
        EXPECT_EQ(patternString(0U, 4U * BlockSize), readFile(dir + "/mine-moved/w0.bin"));
        EXPECT_EQ(patternString(4U * BlockSize, 4U * BlockSize), readFile(dir + "/mine-moved/w1.bin"));
        local_data.shutdown();
        return events;
    };

    // one seed, one schedule
    auto const first = run(7U);
    EXPECT_EQ(first, run(7U));

    // other seeds, other schedules
    auto differs = false;
    for (auto seed = uint32_t{ 8U }; !differs && seed < 16U; ++seed) {
        differs = run(seed) != first;
    }
    EXPECT_TRUE(differs);
}

TEST_F(LocalDataThreadlessTest, writesFinishOutOfOrder)
{
    static auto constexpr NWrites = size_t{ 8U };
    auto const desc = makeDescriptor({ { "data.bin", 2U * NWrites * BlockSize } }, BlockSize);

    // Workers take the writes in disk order, and whichever finishes first
    // is delivered first. One seed can leave them in order by chance. It
    // can't do that for every seed.
    auto reordered = false;
    for (auto seed = uint32_t{ 1U }; !reordered && seed <= 16U; ++seed) {
        auto const local_data = makeThreadless(desc, 4U, seed);

        // every other block, so that no two writes are combined
        auto order = std::vector<size_t>{};
        for (auto block = size_t{}; block < 2U * NWrites; block += 2U) {
            writeBlocks(*local_data, block * BlockSize, (block + 1U) * BlockSize, [&order, block] { order.push_back(block); });
        }

        runMarshaled();
        ASSERT_EQ(NWrites, std::size(order));
        reordered = !std::ranges::is_sorted(order);
        local_data->shutdown();
    }

    EXPECT_TRUE(reordered);
}

TEST_F(LocalDataThreadlessTest, aLaneRunsItsJobsInOrder)
{
    static auto constexpr NRemoved = size_t{ 4U };
    auto const layout = makeDescriptor({ { "data.bin", 4U * BlockSize } }, BlockSize);

    // Whatever the seed, and whatever the workers are doing meanwhile...
    for (auto seed = uint32_t{ 1U }; seed <= 16U; ++seed) {
        auto descs = std::vector<std::shared_ptr<tr::StorageDescriptor const>>{};
        for (auto i = size_t{}; i <= NRemoved; ++i) {
            descs.emplace_back(inDir(*layout, static_cast<tr_torrent_id_t>(TorId + i), fmt::format("{}-{}", seed, i)));
            saveFile(fmt::format("{}-{}/data.bin", seed, i), patternString(0U, BlockSize));
        }
        auto const local_data = makeThreadless(
            [&descs](tr_torrent_id_t const id) { return descs[static_cast<size_t>(id - TorId)]; },
            3U,
            seed);

        auto n_written = size_t{};
        writeBlocks(*local_data, 0U, 4U * BlockSize, [&n_written] { ++n_written; });

        // ...the short lane removes the torrents in the order they were enqueued
        auto removing = std::vector<tr_torrent_id_t>{};
        auto removed = std::vector<tr_torrent_id_t>{};
        auto expected = std::vector<tr_torrent_id_t>{};
        for (auto i = size_t{ 1U }; i <= NRemoved; ++i) {
            auto const id = static_cast<tr_torrent_id_t>(TorId + i);
            expected.emplace_back(id);
            local_data->remove(
                id,
                [&removing, id](std::string_view const filename, tr_error* const error) {
                    if (filename.ends_with("data.bin")) {
                        removing.emplace_back(id);
                    }
                    return tr_sys_path_remove(filename, error);
                },
                [&removed](tr_torrent_id_t const removed_id, tr_error const& error) {
                    EXPECT_FALSE(error) << error;
                    removed.emplace_back(removed_id);
                });
        }

        runMarshaled();
        EXPECT_EQ(4U, n_written) << seed;
        EXPECT_EQ(expected, removing) << seed;
        EXPECT_EQ(expected, removed) << seed;
        local_data->shutdown();
    }
}

TEST_F(LocalDataThreadlessTest, aPausedSessionThreadTakesNoStepsOfItsOwn)
{
    static auto constexpr FileSize = 2U * BlockSize;
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));

    local_data->set_workers_paused(true);
    auto n_done = size_t{};
    writeBlocks(*local_data, 0U, BlockSize, [&n_done] { ++n_done; });
    runMarshaled();
    EXPECT_EQ(0U, n_done);
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    // a test can still take them: a worker takes the write, then does it
    EXPECT_TRUE(tr::detail::step(*local_data));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));
    EXPECT_TRUE(tr::detail::step(*local_data));
    EXPECT_TRUE(tr_sys_path_exists(pathOf("data.bin")));
    EXPECT_FALSE(tr::detail::step(*local_data));

    // the write is done, and its callback waits to be delivered
    EXPECT_EQ(0U, n_done);
    tr::detail::deliver(*local_data);
    EXPECT_EQ(1U, n_done);

    // once it is no longer paused, the session thread does the rest
    writeBlocks(*local_data, BlockSize, FileSize, [&n_done] { ++n_done; });
    runMarshaled();
    EXPECT_EQ(1U, n_done);
    local_data->set_workers_paused(false);
    runMarshaled();
    EXPECT_EQ(2U, n_done);
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, aCachedReadStillCompletesInline)
{
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    if (!canReadCached("data.bin")) {
        GTEST_SKIP() << "this platform or filesystem can't read from the page cache alone";
    }
    openInPool(0U, "data.bin");

    // rule 4: answered before read() returns, with no step taken
    auto result = ReadResult{};
    readInto(*local_data, 0U, BlockSize, result);
    EXPECT_TRUE(result.done);
    EXPECT_FALSE(result.error) << result.error;
    EXPECT_EQ(patternString(0U, BlockSize), result.data);
    EXPECT_EQ(1U, local_data->stats().cached_reads);

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, aCloseReachesTheShortLaneAheadOfALaterRemoval)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    local_data->set_workers_paused(true);
    openInPool(0U, "data.bin");
    auto const file = std::weak_ptr{ open_files_.get(TorId, 0U, false) };
    ASSERT_FALSE(file.expired());

    // The close is done at once: the pool has let go of the file, which
    // stays open until the lane gets to it.
    auto closed = false;
    local_data->close_torrent(TorId, [&closed](tr_torrent_id_t) { closed = true; });
    EXPECT_TRUE(closed);
    EXPECT_FALSE(open_files_.get(TorId, 0U, false));
    EXPECT_FALSE(file.expired());

    auto removed = false;
    local_data->remove(TorId, {}, [&removed](tr_torrent_id_t, tr_error const& error) {
        EXPECT_FALSE(error) << error;
        removed = true;
    });

    // the lane's first job closes the file...
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    EXPECT_TRUE(file.expired());
    EXPECT_TRUE(tr_sys_path_exists(pathOf("data.bin")));

    // ...and its second removes it
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));
    EXPECT_FALSE(tr::detail::step(*local_data));

    EXPECT_FALSE(removed);
    tr::detail::deliver(*local_data);
    EXPECT_TRUE(removed);

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, shutdownDoesTheWorkThatIsWaiting)
{
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeThreadless(makeDescriptor({ { "a.bin", BlockSize }, { "b.bin", 2U * BlockSize } }, BlockSize));
    local_data->set_workers_paused(true);
    openInPool(0U, "a.bin");
    auto const a = std::weak_ptr{ open_files_.get(TorId, 0U, false) };

    // a file for a lane to close, writes for the workers, and a move behind them
    auto events = std::vector<std::string>{};
    local_data->close_file(TorId, 0U);
    writeBlocks(*local_data, BlockSize, 3U * BlockSize, [&events] { events.emplace_back("wrote"); });
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        [&events](auto, auto, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            events.emplace_back("moved");
        });
    EXPECT_TRUE(std::empty(events));
    EXPECT_FALSE(a.expired());

    // no thread will do any of it, so shutdown() does
    local_data->shutdown();
    EXPECT_EQ((std::vector<std::string>{ "wrote", "wrote", "moved" }), events);
    EXPECT_TRUE(a.expired());
    EXPECT_EQ(patternString(BlockSize, 2U * BlockSize), readFile("new/b.bin"));
}

TEST_F(LocalDataThreadlessTest, closeAllDoesTheWorkThatIsWaiting)
{
    static auto constexpr FileSize = 2U * BlockSize;
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));

    auto n_done = size_t{};
    auto const n_writes = writeBlocks(*local_data, 0U, FileSize, [&n_done] { ++n_done; });
    EXPECT_EQ(0U, n_done);

    local_data->close_all();
    EXPECT_EQ(n_writes, n_done);
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));
    EXPECT_FALSE(open_files_.get(TorId, 0U, false));

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, writesThatArriveDuringASweepWaitForTheNext)
{
    // Block 0 is first.bin, blocks 1-6 are data.bin. One worker, so the
    // callbacks arrive in the order the writes reached the disk.
    auto const local_data = makeThreadless(
        makeDescriptor({ { "first.bin", BlockSize }, { "data.bin", 6U * BlockSize } }, BlockSize),
        1U);
    local_data->set_workers_paused(true);

    auto order = std::vector<size_t>{};
    auto const write_block = [&local_data, &order](size_t const block) {
        writeBlocks(*local_data, block * BlockSize, (block + 1U) * BlockSize, [&order, block] { order.push_back(block); });
    };

    // the sweep is blocks 0, 2 and 6, and the worker takes its first
    write_block(0U);
    write_block(2U);
    write_block(6U);
    ASSERT_TRUE(tr::detail::step(*local_data));

    // block 4 arrives ahead of where the sweep has got to...
    write_block(4U);

    // ...and is still written after the rest of the sweep
    runAll(*local_data);
    EXPECT_EQ((std::vector<size_t>{ 0U, 2U, 6U, 4U }), order);

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, readsThatArriveDuringASweepWaitForTheNext)
{
    // Blocks 0, 2 and 4 each start a file of their own, so none is open
    // yet and the page cache answers none. One worker, so the callbacks
    // arrive in the order the disk was used.
    auto const local_data = makeThreadless(
        makeDescriptor({ { "a.bin", 2U * BlockSize }, { "b.bin", 2U * BlockSize }, { "c.bin", 2U * BlockSize } }, BlockSize),
        1U);
    ASSERT_TRUE(tr_file_save(pathOf("a.bin"), patternString(0U, 2U * BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("b.bin"), patternString(2U * BlockSize, 2U * BlockSize)));
    ASSERT_TRUE(tr_file_save(pathOf("c.bin"), patternString(4U * BlockSize, 2U * BlockSize)));
    local_data->set_workers_paused(true);

    auto order = std::vector<size_t>{};
    auto const read_block = [&local_data, &order](size_t const block) {
        local_data->read(
            TorId,
            { .begin = block * BlockSize, .end = (block + 1U) * BlockSize },
            [&order, block](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto data) {
                EXPECT_FALSE(error) << error;
                ASSERT_NE(nullptr, data);
                EXPECT_EQ(
                    patternString(block * BlockSize, BlockSize),
                    (std::string{ reinterpret_cast<char const*>(data->data()), data->size() }));
                order.push_back(block);
            });
    };

    // the sweep is blocks 0 and 4, and the worker takes its first
    read_block(0U);
    read_block(4U);
    tr::detail::deliver(*local_data);
    ASSERT_TRUE(tr::detail::step(*local_data));

    // block 2 arrives ahead of where the sweep has got to...
    read_block(2U);
    tr::detail::deliver(*local_data);

    // ...and is still read after the rest of the sweep
    runAll(*local_data);
    EXPECT_EQ((std::vector<size_t>{ 0U, 4U, 2U }), order);

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, aMoveThatOnlyRenamesStaysOffTheLongLane)
{
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", BlockSize } }, BlockSize));
    local_data->set_workers_paused(true);

    auto moved = false;
    local_data->move(
        TorId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("new") }; },
        [&moved](auto, auto, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            moved = true;
        });

    // a move within one filesystem places its files on the short lane...
    EXPECT_FALSE(tr::detail::step(*local_data, DiskThread::LongLane));
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    EXPECT_TRUE(tr_sys_path_exists(pathOf("new/data.bin")));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    // ...and clears its old location there
    tr::detail::deliver(*local_data);
    EXPECT_FALSE(moved);
    EXPECT_FALSE(tr::detail::step(*local_data, DiskThread::LongLane));
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    tr::detail::deliver(*local_data);
    EXPECT_TRUE(moved);
    EXPECT_FALSE(tr::detail::step(*local_data));

    local_data->shutdown();
}

TEST_F(LocalDataThreadlessTest, aMoveThatCopiesDoesntHoldUpOneThatOnlyRenames)
{
    if (!tr_sys_path_exists("/dev/shm"sv)) {
        GTEST_SKIP() << "no /dev/shm";
    }
    auto const other_filesystem = tr::test::Sandbox{ "/dev/shm" };
    if (tr_sys_path_is_same_filesystem(sandboxDir(), other_filesystem.path())) {
        GTEST_SKIP() << "the sandbox is on /dev/shm's filesystem";
    }

    static auto constexpr OtherId = tr_torrent_id_t{ TorId + 1 };
    ASSERT_TRUE(tr_file_save(pathOf("data.bin"), patternString(0U, BlockSize)));
    createFileWithContents(pathOf("other/data.bin"), patternString(0U, BlockSize));

    auto const mine = makeDescriptor({ { "data.bin", BlockSize } }, BlockSize);
    auto const theirs = inDir(*mine, OtherId, "other");
    auto const local_data = makeThreadless([&mine, &theirs](tr_torrent_id_t const id) { return id == TorId ? mine : theirs; });
    local_data->set_workers_paused(true);

    // my move has to copy to another filesystem, so it goes to the long lane
    auto const copy_target = tr_pathbuf{ other_filesystem.path(), "/target"sv };
    auto events = std::vector<std::string>{};
    local_data->move(
        TorId,
        [&copy_target] { return tr::LocalData::MoveTarget{ .parent = std::string{ copy_target.sv() } }; },
        [&events](auto, auto, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            events.emplace_back("copied");
        },
        [&events](auto, auto) { events.emplace_back("copy placed"); });
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    EXPECT_FALSE(tr::detail::step(*local_data, DiskThread::ShortLane));

    // their move, queued later, only renames, and doesn't wait for mine
    local_data->move(
        OtherId,
        [this] { return tr::LocalData::MoveTarget{ .parent = pathOf("renamed") }; },
        [&events](auto, auto, tr_error const& error) {
            EXPECT_FALSE(error) << error;
            events.emplace_back("renamed");
        });
    while (tr::detail::step(*local_data, DiskThread::ShortLane)) {
        tr::detail::deliver(*local_data);
    }
    EXPECT_EQ((std::vector<std::string>{ "renamed" }), events);
    EXPECT_TRUE(tr_sys_path_exists(pathOf("renamed/data.bin")));

    // mine has moved nothing yet, and its barrier is still up
    EXPECT_TRUE(tr_sys_path_exists(pathOf("data.bin")));
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ copy_target, "/data.bin"sv }));
    EXPECT_TRUE(local_data->admin_op_pending(TorId));

    // the long lane copies, and the short lane clears the old location
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::LongLane));
    EXPECT_FALSE(tr::detail::step(*local_data));
    tr::detail::deliver(*local_data);
    EXPECT_FALSE(tr::detail::step(*local_data, DiskThread::LongLane));
    EXPECT_TRUE(tr::detail::step(*local_data, DiskThread::ShortLane));
    tr::detail::deliver(*local_data);
    EXPECT_EQ((std::vector<std::string>{ "renamed", "copy placed", "copied" }), events);
    EXPECT_TRUE(tr_sys_path_exists(tr_pathbuf{ copy_target, "/data.bin"sv }));
    EXPECT_FALSE(tr_sys_path_exists(pathOf("data.bin")));

    local_data->shutdown();
}

// A closing session cancels the reads that no worker has started,
// and waits for everything else.
TEST_F(LocalDataThreadlessTest, shutdownCancelsWaitingReadsAndFinishesTheRest)
{
    static auto constexpr FileSize = 4U * BlockSize;
    saveFile("data.bin", patternString(0U, FileSize));
    auto const local_data = makeThreadless(makeDescriptor({ { "data.bin", FileSize } }, BlockSize));
    local_data->set_workers_paused(true);

    // one read handed to the workers, another not yet
    auto handed = ReadResult{};
    readInto(*local_data, 2U * BlockSize, BlockSize, handed);
    runMarshaled();
    auto events = std::vector<std::string>{};
    writeBlocks(*local_data, 0U, BlockSize, [&events] { events.emplace_back("wrote"); });
    auto staged = ReadResult{};
    readInto(*local_data, 3U * BlockSize, BlockSize, staged);
    local_data->close_file(TorId, 0U, [&events](tr_torrent_id_t) { events.emplace_back("closed"); });

    local_data->begin_shutdown();
    for (auto const* const read : { &handed, &staged }) {
        EXPECT_TRUE(read->done);
        EXPECT_EQ(ECANCELED, read->error.code()) << read->error;
    }
    EXPECT_TRUE(std::empty(events));
    EXPECT_FALSE(local_data->try_finish_shutdown());

    // a read enqueued now fails at once
    auto late = ReadResult{};
    readInto(*local_data, 0U, BlockSize, late);
    EXPECT_TRUE(late.done);
    EXPECT_EQ(ECANCELED, late.error.code()) << late.error;

    // the workers resumed
    EXPECT_TRUE(pumpUntil([&events]() { return std::size(events) == 2U; }));
    EXPECT_EQ((std::vector<std::string>{ "wrote", "closed" }), events);
    EXPECT_EQ(patternString(0U, FileSize), readFile("data.bin"));

    // the file close went to an admin lane, which may not have run yet
    EXPECT_TRUE(pumpUntil([&local_data]() { return local_data->try_finish_shutdown(); }));

    // later ops are synchronous
    auto after = ReadResult{};
    readInto(*local_data, 0U, BlockSize, after);
    EXPECT_TRUE(after.done);
}
