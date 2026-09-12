// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cassert>
#include <cstddef> // size_t
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrent-metainfo.h>
#include "libtransmission/macros.h"
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;

using TorrentFilesTest = ::tr::test::SandboxedTest;

namespace
{
class TorrentFilesMoveTest : public ::tr::test::SandboxedTest
{
protected:
    static void expectContents(std::string_view path, std::string_view expected)
    {
        auto contents = std::vector<char>{};
        ASSERT_TRUE(tr_file_read(path, contents));
        EXPECT_EQ(expected, (std::string_view{ contents.data(), contents.size() }));
    }
};
} // namespace

TEST_F(TorrentFilesMoveTest, consolidatesIntoSourceRoot)
{
    auto const download = tr_pathbuf{ sandboxDir(), "/download"sv };
    auto const incomplete = tr_pathbuf{ sandboxDir(), "/incomplete"sv };
    auto const roots = std::array<std::string_view, 2>{ download.sv(), incomplete.sv() };
    auto files = tr_torrent_files{};
    files.add("name/first", 1);
    files.add("name/second", 1);
    createFileWithContents(tr_pathbuf{ download, "/name/first"sv }, "first"sv);
    createFileWithContents(tr_pathbuf{ incomplete, "/name/second.part"sv }, "second"sv);
    // A normal source leaves a stale .part destination untouched.
    createFileWithContents(tr_pathbuf{ incomplete, "/name/first.part"sv }, "stale"sv);

    ASSERT_TRUE(files.move(roots, incomplete, "name"));
    expectContents(tr_pathbuf{ incomplete, "/name/first"sv }, "first"sv);
    expectContents(tr_pathbuf{ incomplete, "/name/first.part"sv }, "stale"sv);
    expectContents(tr_pathbuf{ incomplete, "/name/second.part"sv }, "second"sv);
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name"sv }));
}

TEST_F(TorrentFilesMoveTest, cleanupPreservesNestedRootAndDestination)
{
    for (auto const destination_is_nested : { false, true }) {
        SCOPED_TRACE(destination_is_nested);
        auto const base = tr_pathbuf{ sandboxDir(), destination_is_nested ? "/destination"sv : "/source"sv };
        auto const download = tr_pathbuf{ base, "/download"sv };
        auto const nested = tr_pathbuf{ download, "/name/nested"sv };
        auto const target = destination_is_nested ? nested : tr_pathbuf{ base, "/target"sv };
        auto const incomplete = destination_is_nested ? tr_pathbuf{ base, "/incomplete"sv } : nested;
        auto const roots = std::array<std::string_view, 2>{ download.sv(), incomplete.sv() };
        auto files = tr_torrent_files{};
        files.add("name/data", 1);
        createFileWithContents(tr_pathbuf{ download, "/name/data"sv }, "data"sv);
        // Even junk inside a protected root must survive.
        createFileWithContents(tr_pathbuf{ nested, "/desktop.ini"sv }, "protected"sv);

        ASSERT_TRUE(files.move(roots, target, "name"));
        expectContents(tr_pathbuf{ nested, "/desktop.ini"sv }, "protected"sv);
        expectContents(tr_pathbuf{ target, "/name/data"sv }, "data"sv);
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/data"sv }));
    }
}

TEST_F(TorrentFilesMoveTest, cleanupRemovesJunkAndPreservesOrdinaryEntries)
{
    auto const download = tr_pathbuf{ sandboxDir(), "/download"sv };
    auto const target = tr_pathbuf{ sandboxDir(), "/target"sv };
    auto const unused = tr_pathbuf{ sandboxDir(), "/unused"sv };
    auto const roots = std::array<std::string_view, 2>{ download.sv(), unused.sv() };
    auto files = tr_torrent_files{};
    files.add("name/data", 1);
    files.add("name/nested/data", 1);
    createFileWithContents(tr_pathbuf{ download, "/name/data"sv }, "first"sv);
    createFileWithContents(tr_pathbuf{ download, "/name/nested/data"sv }, "second"sv);
    // The nested tree becomes empty; notes.txt keeps its parent alive.
    createFileWithContents(tr_pathbuf{ download, "/name/nested/.DS_Store"sv }, "junk"sv);
    createFileWithContents(tr_pathbuf{ download, "/name/notes.txt"sv }, "notes"sv);
    createFileWithContents(tr_pathbuf{ unused, "/name/desktop.ini"sv }, "unused root"sv);
    ASSERT_TRUE(files.move(roots, target, "name"));
    expectContents(tr_pathbuf{ target, "/name/data"sv }, "first"sv);
    expectContents(tr_pathbuf{ target, "/name/nested/data"sv }, "second"sv);
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/data"sv }));
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/nested"sv }));
    expectContents(tr_pathbuf{ download, "/name/notes.txt"sv }, "notes"sv);
    expectContents(tr_pathbuf{ unused, "/name/desktop.ini"sv }, "unused root"sv);
}

TEST_F(TorrentFilesTest, removePreservesUnusedRoot)
{
    auto const used = tr_pathbuf{ sandboxDir(), "/used"sv };
    auto const unused = tr_pathbuf{ sandboxDir(), "/unused"sv };
    auto const roots = std::array<std::string_view, 2>{ used.sv(), unused.sv() };
    auto files = tr_torrent_files{};
    files.add("name/data", 1);
    createFileWithContents(tr_pathbuf{ used, "/name/data"sv }, "data"sv);
    auto const junk = tr_pathbuf{ unused, "/name/desktop.ini"sv };
    auto const empty = tr_pathbuf{ unused, "/name/empty"sv };
    createFileWithContents(junk, "foreign"sv);
    ASSERT_TRUE(tr_sys_dir_create(empty, TR_SYS_DIR_CREATE_PARENTS, 0700));

    files.remove(roots, "name", tr_sys_path_remove);

    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ used, "/name"sv }));
    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(junk, contents));
    EXPECT_EQ("foreign"sv, (std::string_view{ contents.data(), contents.size() }));
    EXPECT_TRUE(tr_sys_path_exists(empty));
}

TEST_F(TorrentFilesTest, add)
{
    auto constexpr Path = "/hello/world"sv;
    auto constexpr Size = size_t{ 1024 };

    auto files = tr_torrent_files{};
    EXPECT_EQ(size_t{ 0U }, files.file_count());
    EXPECT_TRUE(std::empty(files));

    auto const file_index = files.add(Path, Size);
    EXPECT_EQ(tr_file_index_t{ 0U }, file_index);
    EXPECT_EQ(size_t{ 1U }, files.file_count());
    EXPECT_EQ(Size, files.file_size(file_index));
    EXPECT_EQ(Path, files.path(file_index));
    EXPECT_FALSE(std::empty(files));
}

TEST_F(TorrentFilesTest, setPath)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_torrent_files{};
    auto const file_index = files.add(Path1, Size);
    EXPECT_EQ(Path1, files.path(file_index));
    EXPECT_EQ(Size, files.file_size(file_index));

    files.set_path(file_index, Path2);
    EXPECT_EQ(Path2, files.path(file_index));
    EXPECT_EQ(Size, files.file_size(file_index));
}

TEST_F(TorrentFilesTest, clear)
{
    auto constexpr Path1 = "/hello/world"sv;
    auto constexpr Path2 = "/hello/there"sv;
    auto constexpr Size = size_t{ 2048 };

    auto files = tr_torrent_files{};
    files.add(Path1, Size);
    EXPECT_EQ(size_t{ 1U }, files.file_count());
    files.add(Path2, Size);
    EXPECT_EQ(size_t{ 2U }, files.file_count());

    files.clear();
    EXPECT_TRUE(std::empty(files));
    EXPECT_EQ(size_t{ 0U }, files.file_count());
}

TEST_F(TorrentFilesTest, find)
{
    static auto constexpr Contents = "hello"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/first_dir/hello.txt"sv };
    createFileWithContents(std::string{ filename }, std::data(Contents), std::size(Contents));

    auto files = tr_torrent_files{};
    auto const file_index = files.add("first_dir/hello.txt", 1024);

    auto const search_path_1 = tr_pathbuf{ sandboxDir() };
    auto const search_path_2 = tr_pathbuf{ "/tmp"sv };

    auto search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    auto found = files.find(file_index, search_path);
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(filename, found->filename());
    EXPECT_EQ(search_path_1, found->base);
    EXPECT_EQ("first_dir/hello.txt"sv, found->subpath);
    EXPECT_FALSE(found->is_partial);

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, search_path);
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(filename, found->filename());

    // now make it an incomplete file
    auto const partial_filename = tr_pathbuf{ filename, tr_torrent_files::PartialFileSuffix };
    EXPECT_TRUE(tr_sys_path_rename(filename, partial_filename));
    search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    found = files.find(file_index, search_path);
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(partial_filename, found->filename());
    EXPECT_EQ(search_path_1, found->base);
    EXPECT_EQ("first_dir/hello.txt"sv, found->subpath);
    EXPECT_TRUE(found->is_partial);

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, search_path);
    EXPECT_TRUE(found.has_value());
    assert(found.has_value());
    EXPECT_EQ(partial_filename, found->filename());

    // what about if we look for a file that does not exist
    EXPECT_TRUE(tr_sys_path_remove(partial_filename));
    EXPECT_FALSE(files.find(file_index, search_path));
}

TEST_F(TorrentFilesTest, hasAnyLocalData)
{
    static auto constexpr Contents = "hello"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/first_dir/hello.txt"sv };
    createFileWithContents(std::string{ filename }, std::data(Contents), std::size(Contents));

    auto files = tr_torrent_files{};
    files.add("first_dir/hello.txt", 1024);

    auto const search_path_1 = tr_pathbuf{ sandboxDir() };
    auto const search_path_2 = tr_pathbuf{ "/tmp"sv };

    auto const search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    auto const span = std::span{ search_path };
    EXPECT_TRUE(files.has_any_local_data(span));
    EXPECT_TRUE(files.has_any_local_data(span.first(1U)));
    EXPECT_FALSE(files.has_any_local_data(span.subspan(1U)));
    EXPECT_FALSE(files.has_any_local_data({}));
}

TEST_F(TorrentFilesTest, mimeType)
{
    auto const filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/alice_in_wonderland_librivox_archive.torrent"sv };
    auto metainfo = tr_torrent_metainfo{};
    EXPECT_TRUE(metainfo.parse_torrent_file(filename));
    EXPECT_EQ("audio/mpeg"sv, metainfo.files().primary_mime_type());
}

TEST_F(TorrentFilesTest, mimeTypeVideoMp4)
{
    auto files = tr_torrent_files{};
    files.add("name/name.mp4"sv, 4'500'000'000U);
    files.add("name/name.info"sv, 2048U);
    files.add("name/SHA512sum"sv, 139U);
    EXPECT_EQ("video/mp4"sv, files.primary_mime_type());
}

TEST_F(TorrentFilesTest, isSubpathPortable)
{
    static auto constexpr NotWin32 = TR_IF_WIN32(false, true);

    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 18>{ {
        // never portable
        { ".", false },
        { "..", false },

        // don't end with periods
        { "foo.", NotWin32 },
        { "foo..", NotWin32 },

        // don't begin or end with whitespace
        { " foo ", NotWin32 },
        { " foo", NotWin32 },
        { "foo ", NotWin32 },

        // reserved names
        { "COM1", NotWin32 },
        { "COM1.txt", NotWin32 },
        { "Com1", NotWin32 },
        { "com1", NotWin32 },

        // reserved characters
        { "hell:o.txt", NotWin32 },
        { "hell\to.txt", false },

        // everything else
        { ".foo", true },
        { "com99.txt", true },
        { "foo", true },
        { "hello.txt", true },
        { "hello#.txt", true },
    } };

    for (auto const& [subpath, expected] : Tests) {
        EXPECT_EQ(expected, tr_torrent_files::is_subpath_sanitized(subpath)) << " subpath " << subpath;
    }
}
