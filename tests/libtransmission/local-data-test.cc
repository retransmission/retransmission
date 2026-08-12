// This file Copyright (C) 2026 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

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
#include <libtransmission/tr-strbuf.h>

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

    local_data.remove(12, {});
    EXPECT_TRUE(raw_backend->remove_called);

    local_data.close_file(13, 2);
    ASSERT_TRUE(raw_backend->closed_file.has_value());
    EXPECT_EQ(13, raw_backend->closed_file->first);
    EXPECT_EQ(2, raw_backend->closed_file->second);

    local_data.close_torrent(14);
    EXPECT_EQ(14, raw_backend->closed_torrent);

    local_data.close_all();
    EXPECT_TRUE(raw_backend->close_all_called);

    local_data.shutdown();
}

TEST(LocalData, ShuffledCompletionsWaitForPump)
{
    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Shuffled);

    auto n_called = 0;
    for (auto i = 0; i < 4; ++i) {
        local_data.read(7, { .begin = 0U, .end = 3U }, [&n_called](auto, auto, auto const&, auto) { ++n_called; });
    }

    EXPECT_EQ(0, n_called);
    local_data.pump();
    EXPECT_EQ(4, n_called);
}

TEST(LocalData, ShuffledCompletionsArriveOutOfOrder)
{
    // With this many ops and this many runs, staying in order every time
    // would be a wild coincidence.
    static auto constexpr NumOps = 8;
    static auto constexpr NumRuns = 16;

    auto local_data = tr::LocalData{ std::make_unique<StubBackend>() };
    local_data.set_completions(tr::LocalData::Completions::Shuffled);

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
    local_data.set_completions(tr::LocalData::Completions::Shuffled);

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
    local_data.set_completions(tr::LocalData::Completions::Shuffled);

    auto n_called = 0;
    auto data = std::make_unique<tr::LocalData::BlockData>();
    data->assign({ uint8_t{ 4U } });
    local_data.write(11, { .begin = 0U, .end = 1U }, std::move(data), [&n_called](auto, auto, auto const&) { ++n_called; });

    local_data.shutdown();
    EXPECT_EQ(1, n_called);
}
// ---

// Exercises the threaded backend against real files in a sandbox.
// The test thread doubles as the session thread. Marshaled functions
// queue up and run from pump_until().
class LocalDataWorkersTest : public tr::test::SandboxedTest
{
protected:
    static auto constexpr TorId = tr_torrent_id_t{ 7 };

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
    bool pump_until(Pred const& pred)
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

    [[nodiscard]] static bool matches(tr::LocalData::BlockData const& data, std::string_view const expected)
    {
        return std::size(data) == std::size(expected) &&
            std::equal(
                   std::begin(data),
                   std::end(data),
                   std::begin(expected),
                   std::end(expected),
                   [](uint8_t const x, char const y) { return x == static_cast<uint8_t>(y); });
    }

    // One file whose byte i holds a deterministic pattern.
    [[nodiscard]] static uint8_t pattern_byte(size_t const i) noexcept
    {
        return static_cast<uint8_t>((i * 31U + 7U) & 0xFFU);
    }

    [[nodiscard]] std::string make_pattern_file(std::string_view const subpath, size_t const size, size_t const salt = 0U)
    {
        auto contents = std::string{};
        contents.reserve(size);
        for (auto i = size_t{}; i < size; ++i) {
            contents.push_back(static_cast<char>(pattern_byte(i + salt)));
        }

        createFileWithContents(tr_pathbuf{ sandboxDir(), '/', subpath }, contents);
        return contents;
    }

    // A one- or multi-file descriptor whose files live in the sandbox.
    [[nodiscard]] std::shared_ptr<tr::StorageDescriptor const> make_descriptor(
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
        return std::make_shared<tr::StorageDescriptor const>(
            tr::StorageDescriptor{ 0U, block_info, std::move(tf), std::move(fpm), sandboxDir(), std::string{} });
    }

    [[nodiscard]] auto make_local_data(std::shared_ptr<tr::StorageDescriptor const> desc, size_t const n_workers = 2U)
    {
        auto local_data = std::make_unique<tr::LocalData>(torrents_, open_files_);
        local_data->start_workers(n_workers, marshal(), [desc = std::move(desc)](tr_torrent_id_t const id) {
            return id == TorId ? desc : nullptr;
        });
        return local_data;
    }

    tr_torrents torrents_;
    tr_open_files open_files_;

private:
    std::mutex marshal_mutex_;
    std::condition_variable marshal_cv_;
    std::deque<std::function<void()>> marshaled_;
};

TEST_F(LocalDataWorkersTest, readsCompleteWithTheRightData)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const contents = make_pattern_file("data.bin", FileSize);
    auto const local_data = make_local_data(make_descriptor({ { "data.bin", FileSize } }, 32768U));

    // read every 16 KiB block. Contiguous spans coalesce into runs.
    static auto constexpr NumBlocks = size_t{ 4U };
    auto n_done = size_t{};
    auto ok = std::array<bool, NumBlocks>{};
    for (auto block = size_t{}; block < NumBlocks; ++block) {
        auto const begin = uint64_t{ block * TrBlockSize };
        local_data->read(
            TorId,
            { .begin = begin, .end = begin + TrBlockSize },
            [&n_done, &ok, &contents, block](tr_torrent_id_t, tr_byte_span_t const span, tr_error const& error, auto data) {
                ++n_done;
                if (!error && data != nullptr) {
                    auto const expected = std::string_view{ contents }.substr(static_cast<size_t>(span.begin), TrBlockSize);
                    ok[block] = matches(*data, expected);
                }
            });
    }

    EXPECT_TRUE(pump_until([&n_done]() { return n_done == NumBlocks; }));
    EXPECT_TRUE(std::ranges::all_of(ok, [](bool const b) { return b; }));

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, readsCrossFileBoundaries)
{
    static auto constexpr FileASize = size_t{ 10000U };
    static auto constexpr FileBSize = size_t{ 55536U };
    auto const a = make_pattern_file("a.bin", FileASize);
    auto const b = make_pattern_file("b.bin", FileBSize, 1U);
    auto const local_data = make_local_data(make_descriptor({ { "a.bin", FileASize }, { "b.bin", FileBSize } }, 32768U));

    // a 16 KiB read straddling the file boundary
    auto done = false;
    auto matched = false;
    auto const expected = a.substr(FileASize - 5000U) + b.substr(0U, TrBlockSize - 5000U);
    local_data->read(
        TorId,
        { .begin = FileASize - 5000U, .end = FileASize - 5000U + TrBlockSize },
        [&done, &matched, &expected](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto data) {
            done = true;
            matched = !error && data != nullptr && matches(*data, expected);
        });

    EXPECT_TRUE(pump_until([&done]() { return done; }));
    EXPECT_TRUE(matched);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, testPieceHashesFromWorkers)
{
    static auto constexpr FileSize = size_t{ 65536U };
    static auto constexpr PieceSize = uint32_t{ 32768U };
    auto const contents = make_pattern_file("data.bin", FileSize);
    auto const local_data = make_local_data(make_descriptor({ { "data.bin", FileSize } }, PieceSize));

    auto done = false;
    auto hash = std::optional<tr_sha1_digest_t>{};
    local_data->test_piece(TorId, 1U, [&done, &hash](tr_torrent_id_t, tr_piece_index_t, tr_error const&, auto found) {
        done = true;
        hash = found;
    });

    EXPECT_TRUE(pump_until([&done]() { return done; }));
    ASSERT_TRUE(hash.has_value());
    EXPECT_EQ(tr_sha1::digest(std::string_view{ contents }.substr(PieceSize)), *hash);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, barriersWaitForReadsAndBlockLaterOps)
{
    static auto constexpr FileSize = size_t{ 65536U };
    make_pattern_file("data.bin", FileSize);
    auto const local_data = make_local_data(make_descriptor({ { "data.bin", FileSize } }, 32768U));

    // rule 3: the barrier waits for these...
    auto order = std::vector<std::string>{};
    for (auto block = size_t{}; block < 2U; ++block) {
        auto const begin = uint64_t{ block * TrBlockSize };
        local_data->read(TorId, { .begin = begin, .end = begin + TrBlockSize }, [&order](auto, auto, auto const&, auto) {
            order.emplace_back("read-before");
        });
    }

    // (the move itself fails since there's no real torrent, and only
    // the ordering of the completions matters here)
    local_data->move(TorId, "/old", "/new", "name", [&order](auto, auto const&) { order.emplace_back("move"); });

    // ...and this read waits for the barrier
    local_data->read(TorId, { .begin = 0U, .end = TrBlockSize }, [&order](auto, auto, auto const&, auto) {
        order.emplace_back("read-after");
    });

    EXPECT_TRUE(pump_until([&order]() { return std::size(order) == 4U; }));
    auto const expected = std::vector<std::string>{ "read-before", "read-before", "move", "read-after" };
    EXPECT_EQ(expected, order);

    local_data->shutdown();
}

TEST_F(LocalDataWorkersTest, shutdownDeliversEveryCallback)
{
    static auto constexpr FileSize = size_t{ 65536U };
    make_pattern_file("data.bin", FileSize);
    auto const local_data = make_local_data(make_descriptor({ { "data.bin", FileSize } }, 32768U));

    auto n_done = size_t{};
    for (auto block = size_t{}; block < 4U; ++block) {
        auto const begin = uint64_t{ block * TrBlockSize };
        local_data->read(TorId, { .begin = begin, .end = begin + TrBlockSize }, [&n_done](auto, auto, auto const&, auto) {
            ++n_done;
        });
    }

    // shutdown() delivers the callbacks inline, without a pump
    local_data->shutdown();
    EXPECT_EQ(4U, n_done);
}

TEST_F(LocalDataWorkersTest, missingTorrentReadFailsCleanly)
{
    auto const local_data = make_local_data(nullptr);

    auto done = false;
    auto failed = false;
    local_data->read(3, { .begin = 0U, .end = TrBlockSize }, [&done, &failed](auto, auto, tr_error const& error, auto data) {
        done = true;
        failed = error && data == nullptr;
    });

    EXPECT_TRUE(pump_until([&done]() { return done; }));
    EXPECT_TRUE(failed);

    local_data->shutdown();
}

#ifdef __linux__
TEST_F(LocalDataWorkersTest, cachedReadsCompleteInline)
{
    static auto constexpr FileSize = size_t{ 65536U };
    auto const contents = make_pattern_file("data.bin", FileSize);
    auto const local_data = make_local_data(make_descriptor({ { "data.bin", FileSize } }, 32768U));

    // warm the fd pool. The just-written data is in the page cache.
    auto const filename = tr_pathbuf{ sandboxDir(), "/data.bin" };
    auto const pin = open_files_.get(TorId, 0U, false, filename, tr_file_preallocation::None, FileSize);
    ASSERT_TRUE(pin);

    // some filesystems (e.g. tmpfs) don't support nonblocking reads at
    // all. The engine falls back to the cold path there, and the cold
    // path has no inline completion to observe.
    auto probe = std::array<uint8_t, 16U>{};
    if (!tr_sys_file_read_at_nowait(*pin, std::data(probe), std::size(probe), 0U)) {
        GTEST_SKIP() << "nonblocking reads unsupported on this filesystem";
    }

    auto done = false;
    auto matched = false;
    local_data->read(
        TorId,
        { .begin = 0U, .end = TrBlockSize },
        [&done, &matched, &contents](tr_torrent_id_t, tr_byte_span_t, tr_error const& error, auto data) {
            done = true;
            matched = !error && data != nullptr && matches(*data, std::string_view{ contents }.substr(0U, TrBlockSize));
        });

    // no pump is needed. The hot path serves page-cache hits inline.
    EXPECT_TRUE(done);
    EXPECT_TRUE(matched);

    local_data->shutdown();
}
#endif
