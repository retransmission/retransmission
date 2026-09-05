// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/constants.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h> // tr_sys_path_*()
#include <libtransmission/local-data.h>
#include <libtransmission/quark.h>
#include <libtransmission/session.h>
#include <libtransmission/shared-string.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{
auto constexpr MaxWaitMsec = 5000;

class IncompleteDirTest
    : public SessionTest
    , public ::testing::WithParamInterface<std::pair<std::string, std::string>>
{
protected:
    void completeBlockSpan(tr_torrent* const tor, tr_block_span_t const span)
    {
        auto const [begin, end] = span;
        for (auto block_index = begin; block_index < end; ++block_index) {
            auto const save_zeroes = [tor, block_index]() {
                if (tor->on_block_received(block_index)) {
                    auto const zeroes = std::vector<uint8_t>(TrBlockSize);
                    tor->save_block(block_index, std::make_unique<tr::LocalData::BlockData>(zeroes));
                }
            };
            session_->run_in_session_thread(save_zeroes);
            // save_block() may return before the write finishes
            auto const test = [tor, block_index]() {
                return tor->has_block(block_index);
            };
            EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        }
    }

    void SetUp() override
    {
        auto const& [incomplete_dir, download_dir] = GetParam();

        auto& map = settings();
        map.insert_or_assign(TR_KEY_download_dir, download_dir);
        map.insert_or_assign(TR_KEY_incomplete_dir, incomplete_dir);
        map.insert_or_assign(TR_KEY_incomplete_dir_enabled, true);

        SessionTest::SetUp();
    }

    static auto constexpr MaxWaitMsec = 3000;

    auto setLocation(tr_torrent* tor, std::string_view path, bool move = true)
    {
        return blockingRunInSessionThread([tor, path = std::string{ path }, move]() {
            auto state = -1;
            tr_torrentSetLocation(tor, path, move, &state);
            return state;
        });
    }

    void checkRemoveLocalData(bool via_backend)
    {
        std::string const download_dir = tr_sessionGetDownloadDir(session_);
        std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
        auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
        ASSERT_NE(nullptr, tor);
        ASSERT_EQ(incomplete_dir, tor->current_dir().sv());
        createFileWithContents(
            tr_pathbuf{ incomplete_dir, '/', tr_torrentFile(tor, 0).name, tr_torrent_files::PartialFileSuffix },
            "partial"sv);
        for (tr_file_index_t i = 1; i < tor->file_count(); ++i) {
            createFileWithContents(tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name }, "data"sv);
        }
        auto const download_tree = tr_pathbuf{ download_dir, '/', tor->name() };
        auto const incomplete_tree = tr_pathbuf{ incomplete_dir, '/', tor->name() };

        blockingRunInSessionThread([session = session_, tor, via_backend]() {
            if (via_backend) {
                session->local_data.remove(tor->id(), {});
            }
            tr_torrentRemove(tor, !via_backend);
        });

        EXPECT_FALSE(tr_sys_path_exists(download_tree));
        EXPECT_FALSE(tr_sys_path_exists(incomplete_tree));
    }

    void checkSetLocationToNewDirectory(bool partial_first_file, bool via_backend = false)
    {
        std::string const download_dir = tr_sessionGetDownloadDir(session_);
        std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
        auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };

        // File #0 starts absent, so current_dir falls back to incompleteDir.
        // Optionally add it there as .part; all other files are in downloadDir.
        auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
        ASSERT_NE(nullptr, tor);
        EXPECT_EQ(incomplete_dir, tor->current_dir().sv());
        if (partial_first_file) {
            createFileWithContents(
                tr_pathbuf{ incomplete_dir, '/', tr_torrentFile(tor, 0).name, tr_torrent_files::PartialFileSuffix },
                "partial data"sv);
        }
        for (tr_file_index_t i = 1; i < tor->file_count(); ++i) {
            auto const file = tr_torrentFile(tor, i);
            createFileWithContents(
                tr_pathbuf{ download_dir, '/', file.name },
                std::string(static_cast<size_t>(file.length), '\0'));
        }
        createFileWithContents(tr_pathbuf{ download_dir, '/', tor->name(), "/.DS_Store"sv }, "junk"sv);
        if (partial_first_file) {
            createFileWithContents(tr_pathbuf{ incomplete_dir, '/', tor->name(), "/desktop.ini"sv }, "junk"sv);
        }

        if (via_backend) {
            auto result = std::make_shared<std::promise<tr_error_code_t>>();
            auto ready = result->get_future();
            session_->run_in_session_thread([session = session_, tor, target = std::string{ target_dir.sv() }, result]() {
                session->local_data.move(tor->id(), target, [result](auto, tr_error const& error) {
                    result->set_value(error ? error.code() : 0);
                });
            });
            ASSERT_EQ(std::future_status::ready, ready.wait_for(5s));
            ASSERT_EQ(0, ready.get());
            // The backend only moves files; update Location without moving them again.
            ASSERT_EQ(TR_LOC_DONE, setLocation(tor, target_dir, false));
        } else {
            ASSERT_EQ(TR_LOC_DONE, setLocation(tor, target_dir));
        }
        if (!partial_first_file) {
            EXPECT_EQ(""s, tr_torrentFindFile(tor, 0));
        }
        for (tr_file_index_t i = partial_first_file ? 0U : 1U; i < tor->file_count(); ++i) {
            // Only file #0 was created with .part; moving must preserve that name.
            auto const suffix = i == 0 ? tr_torrent_files::PartialFileSuffix : ""sv;
            auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name, suffix };
            EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
        }
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download_dir, '/', tor->name() }));
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ incomplete_dir, '/', tor->name() }));
        if (!via_backend) {
            EXPECT_TRUE(tor->incomplete_dir().empty());
        }
        EXPECT_EQ(target_dir.sv(), tor->download_dir().sv());
        EXPECT_EQ(target_dir.sv(), tor->current_dir().sv());
        tr_torrentRemove(tor, true);
    }
};
} // namespace

TEST_P(IncompleteDirTest, incompleteDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);

    // init an incomplete torrent.
    // the test zero_torrent will be missing its first piece.
    tr_sessionSetIncompleteFileNamingEnabled(session_, true);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto path = tr_pathbuf{};

    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 0).name, tr_torrent_files::PartialFileSuffix);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 0));
    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 1).name);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 1));
    EXPECT_EQ(tor->piece_size(), tr_torrentStat(tor).left_until_done);

    auto completeness = TR_LEECH;
    tr_sessionSetCompletenessCallback(
        session_,
        [&completeness](tr_torrent_id_t const /*tor_id*/, tr_completeness const c, bool const /*was_running*/) noexcept {
            completeness = c;
        });

    // now finish writing it
    completeBlockSpan(tor, tor->block_span_for_piece(0));

    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto test = [&completeness]() {
        return completeness != TR_LEECH;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_SEED, completeness);

    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i) {
        auto const expected = tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // cleanup
    tr_torrentRemove(tor, true);
}

TEST_P(IncompleteDirTest, removeFindsFilesInBothRoots)
{
    checkRemoveLocalData(false);
}

TEST_P(IncompleteDirTest, backendRemoveFindsFilesInBothRoots)
{
    checkRemoveLocalData(true);
}

TEST_P(IncompleteDirTest, currentDirUsesFirstExistingFile)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);
    EXPECT_EQ(incomplete_dir, tor->current_dir().sv());

    // Earlier files are absent; only the last file exists, with a .part suffix.
    auto const last = tr_torrentFile(tor, tor->file_count() - 1);
    auto const path = tr_pathbuf{ download_dir, '/', last.name, tr_torrent_files::PartialFileSuffix };
    createFileWithContents(path, "partial"sv);
    {
        auto const lock = tor->unique_lock();
        // A supplied miss must not trigger a fresh search, even if a file has since appeared.
        tor->refresh_current_dir(tr::shared_string{});
        EXPECT_EQ(incomplete_dir, tor->current_dir().sv());
    }
    // Later location changes must search again, without retaining the initialization result.
    ASSERT_EQ(TR_LOC_DONE, setLocation(tor, download_dir, false));
    EXPECT_EQ(download_dir, tor->current_dir().sv());
    EXPECT_TRUE(tr_sys_path_exists(path));

    ASSERT_TRUE(tr_sys_path_remove(path));
    {
        auto const lock = tor->unique_lock();
        // A supplied hit is also reused rather than rechecked.
        tor->refresh_current_dir(tr::shared_string{ download_dir });
        EXPECT_EQ(download_dir, tor->current_dir().sv());
    }
    ASSERT_EQ(TR_LOC_DONE, setLocation(tor, download_dir, false));
    EXPECT_EQ(incomplete_dir, tor->current_dir().sv());
    tr_torrentRemove(tor, false);
}

TEST_P(IncompleteDirTest, setLocationFindsFilesOutsideCurrentDir)
{
    checkSetLocationToNewDirectory(false);
}

TEST_P(IncompleteDirTest, setLocationMovesFilesFromBothRoots)
{
    checkSetLocationToNewDirectory(true);
}

TEST_P(IncompleteDirTest, backendMovesFilesFromBothRoots)
{
    checkSetLocationToNewDirectory(true, true);
}

TEST_P(IncompleteDirTest, setLocationOverwritesExistingDestination)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    for (tr_file_index_t i = 1; i < tor->file_count(); ++i) {
        createFileWithContents(tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name }, "source"sv);
    }
    auto const last = tr_torrentFile(tor, tor->file_count() - 1);
    createFileWithContents(tr_pathbuf{ incomplete_dir, '/', last.name }, "destination"sv);

    ASSERT_EQ(TR_LOC_DONE, setLocation(tor, incomplete_dir));
    EXPECT_EQ(incomplete_dir, tor->download_dir().sv());
    EXPECT_TRUE(tor->incomplete_dir().empty());
    EXPECT_EQ(incomplete_dir, tor->current_dir().sv());
    for (tr_file_index_t i = 1; i < tor->file_count(); ++i) {
        auto const name = tr_torrentFile(tor, i).name;
        EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download_dir, '/', name }));
        auto contents = std::vector<char>{};
        ASSERT_TRUE(tr_file_read(tr_pathbuf{ incomplete_dir, '/', name }, contents));
        EXPECT_EQ("source"sv, (std::string_view{ contents.data(), contents.size() }));
    }
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ download_dir, '/', tor->name() }));
    tr_torrentRemove(tor, false);
}

TEST_P(IncompleteDirTest, completionMovesFilesOutsideCurrentDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    for (tr_file_index_t i = 0; i < tor->file_count(); ++i) {
        auto const file = tr_torrentFile(tor, i);
        // The first file makes current_dir point at downloadDir;
        // all the other files must still be consolidated when verification completes.
        auto const& base = i == 0 ? download_dir : incomplete_dir;
        createFileWithContents(tr_pathbuf{ base, '/', file.name }, std::string(static_cast<size_t>(file.length), '\0'));
    }
    ASSERT_EQ(TR_LOC_DONE, setLocation(tor, download_dir, false));
    ASSERT_EQ(download_dir, tor->current_dir().sv());
    ASSERT_FALSE(tor->is_done());
    createFileWithContents(tr_pathbuf{ incomplete_dir, '/', tor->name(), "/.DS_Store"sv }, "junk"sv);

    blockingTorrentVerify(tor);
    EXPECT_TRUE(tor->is_done());
    EXPECT_TRUE(tor->incomplete_dir().empty());
    for (tr_file_index_t i = 0; i < tor->file_count(); ++i) {
        auto const expected = tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ incomplete_dir, '/', tor->name() }));
    tr_torrentRemove(tor, true);
}

INSTANTIATE_TEST_SUITE_P(
    IncompleteDir,
    IncompleteDirTest,
    ::testing::Values(
        // what happens when incompleteDir is a subdir of downloadDir
        std::make_pair(std::string{ "Downloads/Incomplete" }, std::string{ "Downloads" }),
        // test what happens when downloadDir is a subdir of incompleteDir
        std::make_pair(std::string{ "Downloads" }, std::string{ "Downloads/Complete" }),
        // test what happens when downloadDir and incompleteDir are siblings
        std::make_pair(std::string{ "Incomplete" }, std::string{ "Downloads" })));

/***
****
***/

using MoveTest = SessionTest;

TEST_F(MoveTest, setLocation)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // now move it
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    auto test = [&state]() {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the torrent is still complete after being moved
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // confirm the files really got moved
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i) {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // cleanup
    tr_torrentRemove(tor, true);
}

} // namespace tr::test
