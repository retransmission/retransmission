// This file copyright Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cerrno>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/open-files.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;

using OpenFilesTest = tr::test::SessionTest;

static auto constexpr PreallocateFull = tr_file_preallocation::Full;

TEST_F(OpenFilesTest, getCachedFailsIfNotCached)
{
    auto const file = session_->openFiles().get(0, 0, false);
    EXPECT_FALSE(file);
}

TEST_F(OpenFilesTest, getOpensIfNotCached)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // confirm that it's not pre-cached
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));

    // confirm that we can cache the file
    auto const file = session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(file);
    EXPECT_NE(TR_BAD_SYS_FILE, file->fd());

    // test the file contents to confirm that the handle points to the right file
    auto buf = std::array<char, std::size(Contents) + 1>{};
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(file->fd(), std::data(buf), std::size(Contents), 0, &bytes_read));
    auto const contents = std::string_view{ std::data(buf), static_cast<size_t>(bytes_read) };
    EXPECT_EQ(Contents, contents);
}

TEST_F(OpenFilesTest, getCacheSucceedsIfCached)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents)));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));
}

TEST_F(OpenFilesTest, getCachedReturnsTheSameFd)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    auto const file1 = session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents));
    auto const file2 = session_->openFiles().get(0, 0, false);
    ASSERT_TRUE(file1);
    ASSERT_TRUE(file2);
    EXPECT_EQ(file1, file2);
    EXPECT_EQ(file1->fd(), file2->fd());
}

TEST_F(OpenFilesTest, getCachedFailsIfWrongPermissions)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache it in ro mode
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents)));

    // now try to get it in r/w mode
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));
    EXPECT_FALSE(session_->openFiles().get(0, 0, true));
}

TEST_F(OpenFilesTest, opensInReadOnlyUnlessWritableIsRequested)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache a file read-only mode
    auto const file = session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(file);

    // confirm that writing to it fails
    auto error = tr_error{};
    EXPECT_FALSE(tr_sys_file_write(file->fd(), std::data(Contents), std::size(Contents), nullptr, &error));
    EXPECT_TRUE(error);
}

TEST_F(OpenFilesTest, createsMissingFileIfWriteRequested)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    EXPECT_FALSE(tr_sys_path_exists(filename));

    auto file = session_->openFiles().get(0, 0, false);
    EXPECT_FALSE(file);
    EXPECT_FALSE(tr_sys_path_exists(filename));

    file = session_->openFiles().get(0, 0, true, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(file);
    EXPECT_NE(TR_BAD_SYS_FILE, file->fd());
    EXPECT_TRUE(tr_sys_path_exists(filename));
}

TEST_F(OpenFilesTest, closeFileClosesTheFile)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    // cache a file read-only mode
    EXPECT_TRUE(session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents)));
    EXPECT_TRUE(session_->openFiles().get(0, 0, false));

    // close the file
    session_->openFiles().close_file(0, 0);

    // confirm that its fd is no longer cached
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));
}

TEST_F(OpenFilesTest, closingAFileLeavesItReadableForItsHolders)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    auto const file = session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(file);

    // Uncache it while we're still holding it. An LRU eviction and a
    // torrent's files being closed both land here.
    session_->openFiles().close_file(0, 0);
    EXPECT_FALSE(session_->openFiles().get(0, 0, false));

    // our descriptor is still ours to read through
    auto buf = std::array<char, std::size(Contents) + 1>{};
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(file->fd(), std::data(buf), std::size(Contents), 0, &bytes_read));
    EXPECT_EQ(Contents, std::string_view(std::data(buf), static_cast<size_t>(bytes_read)));
}

TEST_F(OpenFilesTest, reopeningWritableLeavesTheReadOnlyHolderAlone)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    auto filename = tr_pathbuf{ sandboxDir(), "/test-file.txt" };
    createFileWithContents(filename, Contents);

    auto const ro = session_->openFiles().get(0, 0, false, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(ro);

    // asking for write access drops the read-only entry and opens a new one
    auto const rw = session_->openFiles().get(0, 0, true, filename, PreallocateFull, std::size(Contents));
    ASSERT_TRUE(rw);
    EXPECT_NE(ro, rw);

    // the read-only descriptor we were handed still works
    auto buf = std::array<char, std::size(Contents) + 1>{};
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(ro->fd(), std::data(buf), std::size(Contents), 0, &bytes_read));
    EXPECT_EQ(Contents, std::string_view(std::data(buf), static_cast<size_t>(bytes_read)));
}

TEST_F(OpenFilesTest, closeTorrentClosesTheTorrentFiles)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    static auto constexpr TorId = tr_torrent_id_t{ 0 };

    auto filename = tr_pathbuf{ sandboxDir(), "/a.txt" };
    createFileWithContents(filename, Contents);
    EXPECT_TRUE(session_->openFiles().get(TorId, 1, false, filename, PreallocateFull, std::size(Contents)));

    filename.assign(sandboxDir(), "/b.txt");
    createFileWithContents(filename, Contents);
    EXPECT_TRUE(session_->openFiles().get(TorId, 3, false, filename, PreallocateFull, std::size(Contents)));

    // confirm that closing a different torrent does not affect these files
    session_->openFiles().close_torrent(TorId + 1);
    EXPECT_TRUE(session_->openFiles().get(TorId, 1, false));
    EXPECT_TRUE(session_->openFiles().get(TorId, 3, false));

    // confirm that closing this torrent closes and uncaches the files
    session_->openFiles().close_torrent(TorId);
    EXPECT_FALSE(session_->openFiles().get(TorId, 1, false));
    EXPECT_FALSE(session_->openFiles().get(TorId, 3, false));
}

TEST_F(OpenFilesTest, servesConcurrentCallersWhileFilesAreClosed)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    static auto constexpr TorId = tr_torrent_id_t{ 0 };
    static auto constexpr NumFiles = 8;
    static auto constexpr NumReaders = 4;
    static auto constexpr NumPasses = 200;

    for (auto i = 0; i < NumFiles; ++i) {
        createFileWithContents(tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, i) }, Contents);
    }

    // A handle keeps its file readable no matter what the pool does to
    // the entry it came from, so every read here has to see the contents.
    auto stop = std::atomic{ false };
    auto bad_reads = std::atomic{ 0 };

    auto readers = std::vector<std::thread>{};
    for (auto reader = 0; reader < NumReaders; ++reader) {
        readers.emplace_back([this, &bad_reads, reader]() {
            for (auto pass = 0; pass < NumPasses; ++pass) {
                auto const file_num = (reader + pass) % NumFiles;
                auto const filename = tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, file_num) };
                auto const file = session_->openFiles()
                                      .get(TorId, file_num, false, filename, PreallocateFull, std::size(Contents));
                if (!file) {
                    ++bad_reads;
                    continue;
                }

                auto buf = std::array<char, std::size(Contents) + 1>{};
                auto bytes_read = uint64_t{};
                if (!tr_sys_file_read_at(file->fd(), std::data(buf), std::size(Contents), 0, &bytes_read) ||
                    std::string_view(std::data(buf), static_cast<size_t>(bytes_read)) != Contents) {
                    ++bad_reads;
                }
            }
        });
    }

    auto closer = std::thread{ [this, &stop]() {
        while (!stop) {
            session_->openFiles().close_torrent(TorId);
        }
    } };

    for (auto& reader : readers) {
        reader.join();
    }
    stop = true;
    closer.join();

    EXPECT_EQ(0, bad_reads);
}

TEST_F(OpenFilesTest, concurrentWritersShareTheInitializedFile)
{
    static auto constexpr NumWriters = size_t{ 8U };
    static auto constexpr FileSize = uint64_t{ 1024U * 1024U };
    auto const filename = tr_pathbuf{ sandboxDir(), "/concurrent.bin" };
    auto ready = std::barrier{ static_cast<std::ptrdiff_t>(NumWriters) };
    auto handles = std::array<tr_open_files::Handle, NumWriters>{};
    auto writers = std::vector<std::thread>{};

    for (auto index = size_t{}; index < NumWriters; ++index) {
        writers.emplace_back([this, &filename, &ready, &handles, index]() {
            ready.arrive_and_wait();
            handles[index] = session_->openFiles().get(0, 0, true, filename, PreallocateFull, FileSize);
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    ASSERT_TRUE(handles.front());
    for (auto const& handle : handles) {
        EXPECT_EQ(handles.front(), handle);
    }
    auto const info = tr_sys_path_get_info(filename);
    ASSERT_TRUE(info);
    EXPECT_EQ(FileSize, info->size);
}

class OpenFilesPreallocationTest
    : public tr::test::SandboxedTest
    , public ::testing::WithParamInterface<bool>
{
};

TEST_P(OpenFilesPreallocationTest, serializesWritersUntilInitializationFinishes)
{
    static auto constexpr FileSize = uint64_t{ 2U * 1024U * 1024U + 17U };
    static auto constexpr Offset = uint64_t{ 1024U * 1024U + 13U };
    static auto constexpr Payload = "second writer's data"sv;
    auto const fail = GetParam();
    auto const filename = tr_pathbuf{ sandboxDir(), "/preallocated.bin" };
    auto const other_filename = tr_pathbuf{ sandboxDir(), "/independent.bin" };
    auto entered = std::promise<void>{};
    auto resume = std::promise<void>{};
    auto const resumed = resume.get_future().share();
    auto calls = std::atomic<size_t>{};
    auto files = tr_open_files{ [&](tr_sys_file_t, uint64_t const size, int const flags, tr_error* const error) {
        ++calls;
        EXPECT_EQ(FileSize, size);
        EXPECT_EQ(0, flags);
        entered.set_value();
        EXPECT_EQ(std::future_status::ready, resumed.wait_for(5s));
#ifdef _WIN32
        error->set(fail ? ERROR_DISK_FULL : ERROR_NOT_SUPPORTED, "injected preallocation error");
#else
        error->set(fail ? ENOSPC : ENOSYS, "injected preallocation error");
#endif
        return false;
    } };

    auto first = std::async(std::launch::async, [&]() { return files.get(0, 0, true, filename, PreallocateFull, FileSize); });
    EXPECT_EQ(std::future_status::ready, entered.get_future().wait_for(5s));
    EXPECT_FALSE(files.get(0, 0, true));

    auto lookup_ready = std::promise<void>{};
    auto open_ready = std::promise<void>{};
    auto lookup_waiter = tr_open_files::Waiter{ .on_ready = [&]() { lookup_ready.set_value(); } };
    auto open_waiter = tr_open_files::Waiter{ .on_ready = [&]() { open_ready.set_value(); } };
    EXPECT_FALSE(files.get(0, 0, true, &lookup_waiter));
    EXPECT_FALSE(files.get(0, 0, true, filename, PreallocateFull, FileSize, &open_waiter));
    EXPECT_TRUE(lookup_waiter.blocked);
    EXPECT_TRUE(open_waiter.blocked);

    auto writer_started = std::promise<void>{};
    auto second = std::async(std::launch::async, [&]() {
        writer_started.set_value();
        auto file = files.get(0, 0, true, filename, PreallocateFull, FileSize);
        if (file) {
            auto const lock = file->io_lock();
            auto written = uint64_t{};
            EXPECT_TRUE(tr_sys_file_write_at(file->fd(), Payload.data(), Payload.size(), Offset, &written));
            EXPECT_EQ(Payload.size(), written);
        }
        return file;
    });
    EXPECT_EQ(std::future_status::ready, writer_started.get_future().wait_for(5s));
    EXPECT_EQ(std::future_status::timeout, second.wait_for(100ms));

    auto independent = std::async(std::launch::async, [&]() {
        return files.get(0, 1, true, other_filename, tr_file_preallocation::None, 0U);
    });
    EXPECT_EQ(std::future_status::ready, independent.wait_for(1s));
    resume.set_value();

    auto const first_file = first.get();
    auto const second_file = second.get();
    EXPECT_EQ(std::future_status::ready, lookup_ready.get_future().wait_for(0s));
    EXPECT_EQ(std::future_status::ready, open_ready.get_future().wait_for(0s));
    EXPECT_TRUE(independent.get());
    EXPECT_EQ(!fail, static_cast<bool>(first_file));
    ASSERT_TRUE(second_file);
    EXPECT_EQ(second_file, files.get(0, 0, true));
    EXPECT_EQ(1U, calls.load());
    if (!fail) {
        EXPECT_EQ(first_file, second_file);
    }

    auto expected = std::string(static_cast<size_t>(fail ? Offset + Payload.size() : FileSize), '\0');
    expected.replace(Offset, Payload.size(), Payload);
    auto actual = std::string(expected.size(), '\0');
    auto bytes_read = uint64_t{};
    EXPECT_TRUE(tr_sys_file_read_at(second_file->fd(), actual.data(), actual.size(), 0U, &bytes_read));
    EXPECT_EQ(expected.size(), bytes_read);
    EXPECT_EQ(expected, actual);
    auto const info = tr_sys_path_get_info(filename);
    ASSERT_TRUE(info);
    EXPECT_EQ(expected.size(), info->size);
}

INSTANTIATE_TEST_SUITE_P(FullAllocation, OpenFilesPreallocationTest, ::testing::Bool());

TEST_F(OpenFilesTest, closesLeastRecentlyUsedFile)
{
    static auto constexpr Contents = "Hello, World!\n"sv;
    static auto constexpr TorId = tr_torrent_id_t{ 0 };
    static auto constexpr LargerThanCacheLimit = 100;

    // Walk through a number of files. Confirm that they all succeed
    // even when the number exhausts the cache size, and newer files
    // supplant older ones.
    for (int i = 0; i < LargerThanCacheLimit; ++i) {
        auto filename = tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, i) };
        EXPECT_TRUE(session_->openFiles().get(TorId, i, true, filename, PreallocateFull, std::size(Contents)));
    }

    // Do a lookup-only for the files again *in the same order*. By following the
    // order, the first files we check will be the oldest from the last pass and
    // should have aged out. So we should have a nonzero number of failures; but
    // once we get a success, all the remaining should also succeed.
    auto results = std::array<bool, LargerThanCacheLimit>{};
    auto sorted = std::array<bool, LargerThanCacheLimit>{};
    for (int i = 0; i < LargerThanCacheLimit; ++i) {
        auto filename = tr_pathbuf{ sandboxDir(), fmt::format("/file-{:d}.txt"sv, i) };
        results[i] = static_cast<bool>(session_->openFiles().get(TorId, i, false));
    }
    sorted = results;
    std::ranges::sort(sorted);
    EXPECT_EQ(sorted, results);
    EXPECT_GT(std::ranges::count(results, true), 0);
}
