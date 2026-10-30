// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/error.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/types.h>
#include "libtransmission/macros.h"
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace
{
class TorrentFilesTest : public ::tr::test::SandboxedTest
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

TEST_F(TorrentFilesTest, consolidatesIntoSourceRoot)
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

TEST_F(TorrentFilesTest, cleanupPreservesNestedRootAndDestination)
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
        files.add("name/dir1/file", 1);
        createFileWithContents(tr_pathbuf{ download, "/name/data"sv }, "data"sv);
        createFileWithContents(tr_pathbuf{ download, "/name/dir1/file"sv }, "other"sv);
        // Even junk inside a protected root must survive.
        createFileWithContents(tr_pathbuf{ nested, "/desktop.ini"sv }, "protected"sv);
        // Junk in its parent must still be cleaned.
        createFileWithContents(tr_pathbuf{ download, "/name/desktop.ini"sv }, "junk"sv);

        ASSERT_TRUE(files.move(roots, target, "name"));
        expectContents(tr_pathbuf{ nested, "/desktop.ini"sv }, "protected"sv);
        expectContents(tr_pathbuf{ target, "/name/data"sv }, "data"sv);
        expectContents(tr_pathbuf{ target, "/name/dir1/file"sv }, "other"sv);
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/data"sv }));
        // A protected subtree must not prevent cleanup of its empty sibling.
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/dir1"sv }));
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download, "/name/desktop.ini"sv }));
    }
}

TEST_F(TorrentFilesTest, cleanupRemovesJunkAndPreservesOrdinaryEntries)
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

TEST_F(TorrentFilesTest, removePreservesRootsWithoutMatchingFiles)
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

    // A previously used root is also left alone after its data is deleted by hand.
    auto const data = tr_pathbuf{ used, "/name/data"sv };
    auto const leftover_junk = tr_pathbuf{ used, "/name/.DS_Store"sv };
    auto const leftover_empty = tr_pathbuf{ used, "/name/empty"sv };
    createFileWithContents(data, "data"sv);
    createFileWithContents(leftover_junk, "junk"sv);
    ASSERT_TRUE(tr_sys_dir_create(leftover_empty, TR_SYS_DIR_CREATE_PARENTS, 0700));
    ASSERT_TRUE(tr_sys_path_remove(data));

    files.remove(roots, "name", tr_sys_path_remove);

    expectContents(leftover_junk, "junk"sv);
    EXPECT_TRUE(tr_sys_path_exists(leftover_empty));
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

TEST_F(TorrentFilesTest, sortedByPath)
{
    auto files = tr_torrent_files{};
    files.add("name/c"sv, 3U);
    files.add("name/a"sv, 1U);
    files.add("name/b"sv, 2U);

    auto const expected = std::vector<std::pair<std::string, uint64_t>>{ { "name/a", 1U }, { "name/b", 2U }, { "name/c", 3U } };
    EXPECT_EQ(expected, files.sorted_by_path());
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
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(filename, found->filename());
    EXPECT_EQ(search_path_1, found->base);
    EXPECT_EQ("first_dir/hello.txt"sv, found->subpath);
    EXPECT_FALSE(found->is_partial);

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, search_path);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(filename, found->filename());

    // now make it an incomplete file
    auto const partial_filename = tr_pathbuf{ filename, tr_torrent_files::PartialFileSuffix };
    EXPECT_TRUE(tr_sys_path_rename(filename, partial_filename));
    search_path = std::vector<std::string_view>{ search_path_1.sv(), search_path_2.sv() };
    found = files.find(file_index, search_path);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(partial_filename, found->filename());
    EXPECT_EQ(search_path_1, found->base);
    EXPECT_EQ("first_dir/hello.txt"sv, found->subpath);
    EXPECT_TRUE(found->is_partial);

    // same search, but with the search paths reversed
    search_path = std::vector<std::string_view>{ search_path_2.sv(), search_path_1.sv() };
    found = files.find(file_index, search_path);
    ASSERT_TRUE(found.has_value());
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

TEST_F(TorrentFilesTest, sanitizeSubpathReservedCharacters)
{
    static auto constexpr Win32ReservedChars = "\"*:<>?\\/|"sv;
    static auto constexpr IsWin32 = TR_IF_WIN32(true, false);

    auto input = std::string{};
    auto portable_expected = std::string{};
    auto os_specific_expected = std::string{};
    for (auto byte = 0; byte <= 0xff; ++byte) {
        auto const ch = static_cast<char>(byte);
        auto const is_common_reserved = byte <= 31 || ch == '/';
        auto const is_win32_reserved = Win32ReservedChars.contains(ch);

        input.push_back(ch);
        portable_expected.push_back(is_common_reserved || is_win32_reserved ? '_' : ch);
        os_specific_expected.push_back(is_common_reserved || (IsWin32 && is_win32_reserved) ? '_' : ch);
    }

    EXPECT_EQ(portable_expected, tr_torrent_files::sanitize_subpath(input, false));
    EXPECT_EQ(os_specific_expected, tr_torrent_files::sanitize_subpath(input, true));
}

TEST_F(TorrentFilesTest, sanitizeSubpathReservedNames)
{
    static auto constexpr ReservedNames = std::to_array<std::string_view>({
        "AUX"sv,  "CON"sv,  "NUL"sv,  "PRN"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    });
    static auto constexpr NotReserved = std::to_array<std::string_view>({
        ""sv,
        ".foo"sv,
        "AUXILIARY"sv,
        "COM0"sv,
        "COM10"sv,
        "COM10.txt"sv,
        "CONSOLE"sv,
        "LPT0"sv,
        "LPT10"sv,
        "NULx.txt"sv,
        "PRN_suffix"sv,
    });
    static auto constexpr IsWin32 = TR_IF_WIN32(true, false);

    auto const expect_reserved = [](std::string_view path) {
        auto expected = std::string{ "_" };
        expected.append(path);
        EXPECT_EQ(expected, tr_torrent_files::sanitize_subpath(path, false)) << path;
    };

    for (auto const name : ReservedNames) {
        for (auto const suffix : { ""sv, ".txt"sv }) {
            auto input = std::string{ name };
            input.append(suffix);

            expect_reserved(input);
            auto expected = IsWin32 ? std::string{ "_" } : std::string{};
            expected += input;
            EXPECT_EQ(expected, tr_torrent_files::sanitize_subpath(input)) << input;

            for (auto& ch : input) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            expect_reserved(input);
            expected = IsWin32 ? std::string{ "_" } : std::string{};
            expected += input;
            EXPECT_EQ(expected, tr_torrent_files::sanitize_subpath(input)) << input;
        }
    }

    for (auto const path : NotReserved) {
        EXPECT_EQ(path, tr_torrent_files::sanitize_subpath(path, false)) << path;
        EXPECT_EQ(path, tr_torrent_files::sanitize_subpath(path)) << path;
    }

    EXPECT_EQ(IsWin32 ? ""sv : "_."sv, tr_torrent_files::sanitize_subpath("."sv, false));
    EXPECT_EQ(IsWin32 ? ""sv : "_.."sv, tr_torrent_files::sanitize_subpath(".."sv, false));
    EXPECT_EQ(IsWin32 ? ""sv : "_."sv, tr_torrent_files::sanitize_subpath("."sv));
    EXPECT_EQ(IsWin32 ? ""sv : "_.."sv, tr_torrent_files::sanitize_subpath(".."sv));
}

TEST_F(TorrentFilesTest, sanitizeSubpathWin32TrailingCharacters)
{
    static auto constexpr IsWin32 = TR_IF_WIN32(true, false);
    static auto constexpr Tests = std::to_array<std::pair<std::string_view, std::string_view>>({
        { " name "sv, "name"sv },
        { "name..."sv, "name"sv },
        { "name. "sv, "name"sv },
        { "name . "sv, "name"sv },
        { " NUL. "sv, "_NUL"sv },
    });

    for (auto const& [path, win32_expected] : Tests) {
        auto const expected = IsWin32 ? win32_expected : path;
        EXPECT_EQ(expected, tr_torrent_files::sanitize_subpath(path, true)) << path;
        EXPECT_EQ(expected, tr_torrent_files::sanitize_subpath(path, false)) << path;
    }

    auto constexpr TabbedPath = "\tname\n"sv;
    auto constexpr TabbedExpected = TR_IF_WIN32("name"sv, "_name_"sv);
    EXPECT_EQ(TabbedExpected, tr_torrent_files::sanitize_subpath(TabbedPath, true));
    EXPECT_EQ(TabbedExpected, tr_torrent_files::sanitize_subpath(TabbedPath, false));

    auto constexpr WhitespaceOnly = "\t"sv;
    auto constexpr WhitespaceOnlyExpected = TR_IF_WIN32(""sv, "_"sv);
    EXPECT_EQ(WhitespaceOnlyExpected, tr_torrent_files::sanitize_subpath(WhitespaceOnly, true));
    EXPECT_EQ(WhitespaceOnlyExpected, tr_torrent_files::sanitize_subpath(WhitespaceOnly, false));
}

TEST_F(TorrentFilesTest, sanitizeSubpathAppends)
{
    auto output = std::string{ "prefix" };
    tr_torrent_files::sanitize_subpath("NUL.txt"sv, output, false);
    EXPECT_EQ("prefix_NUL.txt"sv, output);

    tr_torrent_files::sanitize_subpath("A/B"sv, output, false);
    EXPECT_EQ("prefix_NUL.txtA_B"sv, output);

    tr_torrent_files::sanitize_subpath(""sv, output, false);
    EXPECT_EQ("prefix_NUL.txtA_B"sv, output);
}
