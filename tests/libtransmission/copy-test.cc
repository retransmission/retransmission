// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdint> // uint64_t
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

namespace tr::test
{
namespace
{
class CopyTest : public SandboxedTest
{
protected:
    void testImpl(char const* filename1, char const* filename2, size_t const file_length)
    {
        auto const path1 = tr_pathbuf{ sandboxDir(), '/', filename1 };

        /* Create a file. */
        auto contents = std::vector<char>{};
        contents.resize(file_length);
        tr_rand_buffer(std::data(contents), std::size(contents));
        createFileWithContents(path1, std::data(contents), std::size(contents));

        auto const path2 = tr_pathbuf{ sandboxDir(), '/', filename2 };

        /* Copy it. */
        auto error = tr_error{};
        EXPECT_TRUE(tr_sys_path_copy(path1, path2, &error));
        EXPECT_FALSE(error) << error;

        EXPECT_TRUE(filesAreIdentical(path1, path2));

        /* Dispose of those files that we created. */
        tr_sys_path_remove(path1);
        tr_sys_path_remove(path2);
    }

    static bool filesAreIdentical(std::string_view filename1, std::string_view filename2)
    {
        auto contents1 = std::vector<char>{};
        auto contents2 = std::vector<char>{};
        return tr_file_read(filename1, contents1) && tr_file_read(filename2, contents2) && contents1 == contents2;
    }

    static bool hasTemporaryCopy(std::string_view const dir)
    {
        auto const names = tr_sys_dir_get_files(dir, tr_basename_accept_all);
        return std::ranges::any_of(names, [](std::string_view name) { return name.starts_with(TrFileCopyPrefix); });
    }

private:
    static uint64_t fillBufferFromFd(tr_sys_file_t fd, uint64_t bytes_remaining, char* buf, size_t buf_len)
    {
        memset(buf, 0, buf_len);

        size_t buf_pos = 0;
        while (buf_pos < buf_len && bytes_remaining > 0) {
            uint64_t const chunk_size = std::min(uint64_t{ buf_len - buf_pos }, bytes_remaining);
            uint64_t bytes_read = 0;

            tr_sys_file_read(fd, buf + buf_pos, chunk_size, &bytes_read);

            EXPECT_LE(buf_pos + bytes_read, buf_len);
            EXPECT_LE(bytes_read, bytes_remaining);
            buf_pos += bytes_read;
            bytes_remaining -= bytes_read;
        }

        return bytes_remaining;
    }
};
} // namespace

TEST_F(CopyTest, copy)
{
    char const* filename1 = "orig-blob.txt";
    char const* filename2 = "copy-blob.txt";
    auto const random_file_length = 1024 * 1024 * 10;

    testImpl(filename1, filename2, random_file_length);
}

TEST_F(CopyTest, atomicCopyReplacesTheTarget)
{
    auto const source = tr_pathbuf{ sandboxDir(), "/source.bin"sv };
    auto const target = tr_pathbuf{ sandboxDir(), "/target.bin"sv };
    createFileWithContents(source, "new contents"sv);
    createFileWithContents(target, "old contents"sv);

    auto error = tr_error{};
    EXPECT_TRUE(tr_file_copy_atomic(source, target, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(filesAreIdentical(source, target));
    EXPECT_FALSE(hasTemporaryCopy(sandboxDir()));
}

TEST_F(CopyTest, atomicCopyFitsANameAtTheLengthLimit)
{
    // Most filesystems cap a name at 255 bytes.
    auto const source = tr_pathbuf{ sandboxDir(), "/source.bin"sv };
    auto const target = tr_pathbuf{ sandboxDir(), '/', std::string(255U, 'x') };
    createFileWithContents(source, "contents"sv);

    auto error = tr_error{};
    EXPECT_TRUE(tr_file_copy_atomic(source, target, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(filesAreIdentical(source, target));
}

TEST_F(CopyTest, atomicCopyReplacesWhatAKilledCopyLeft)
{
    auto const source = tr_pathbuf{ sandboxDir(), "/source.bin"sv };
    auto const target = tr_pathbuf{ sandboxDir(), "/target.bin"sv };
    auto const leftover = tr::detail::file_copy_temp_path(target);
    createFileWithContents(source, "new contents"sv);
    // Some platforms copy a read-only source's state, so the leftover may be read-only.
    createFileWithContents(leftover, "partial"sv);
    setReadOnly(leftover, true);

    auto error = tr_error{};
    EXPECT_TRUE(tr_file_copy_atomic(source, target, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(filesAreIdentical(source, target));
    EXPECT_FALSE(hasTemporaryCopy(sandboxDir()));
}

TEST_F(CopyTest, atomicCopyOfAReadOnlySource)
{
    auto const source = tr_pathbuf{ sandboxDir(), "/source.bin"sv };
    auto const target = tr_pathbuf{ sandboxDir(), "/target.bin"sv };
    createFileWithContents(source, "contents"sv);
    setReadOnly(source, true);

    auto error = tr_error{};
    EXPECT_TRUE(tr_file_copy_atomic(source, target, &error));
    EXPECT_FALSE(error) << error;
    EXPECT_TRUE(filesAreIdentical(source, target));
    EXPECT_FALSE(hasTemporaryCopy(sandboxDir()));

    setReadOnly(source, false);
    if (tr_sys_path_exists(target)) {
        setReadOnly(target, false);
    }
}

TEST_F(CopyTest, failedAtomicCopyLeavesTheTargetAlone)
{
    auto const source = tr_pathbuf{ sandboxDir(), "/missing.bin"sv };
    auto const target = tr_pathbuf{ sandboxDir(), "/target.bin"sv };
    createFileWithContents(target, "old contents"sv);

    auto error = tr_error{};
    EXPECT_FALSE(tr_file_copy_atomic(source, target, &error));
    EXPECT_TRUE(error);
    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(target, contents));
    EXPECT_EQ("old contents"sv, (std::string_view{ std::data(contents), std::size(contents) }));
    EXPECT_FALSE(hasTemporaryCopy(sandboxDir()));
}

} // namespace tr::test
