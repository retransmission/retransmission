// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/file.h> // tr_sys_path_*()
#include <libtransmission/local-data.h>
#include <libtransmission/quark.h>
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
    void SetUp() override
    {
        auto const download_dir = GetParam().second;
        auto const incomplete_dir = GetParam().first;

        auto& map = settings();
        map.insert_or_assign(TR_KEY_download_dir, download_dir);
        map.insert_or_assign(TR_KEY_incomplete_dir, incomplete_dir);
        map.insert_or_assign(TR_KEY_incomplete_dir_enabled, true);

        SessionTest::SetUp();
    }

    static auto constexpr MaxWaitMsec = 3000;
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

    struct TestIncompleteDirData {
        tr_session* session = {};
        tr_torrent* tor = {};
        tr_block_index_t block = {};
        tr_piece_index_t pieceIndex = {};
        std::vector<uint8_t> buf;
    };

    auto const test_incomplete_dir_threadfunc = [](TestIncompleteDirData* data) noexcept {
        if (data->tor->on_block_received(data->block)) {
            data->tor->save_block(data->block, std::make_unique<tr::LocalData::BlockData>(data->buf));
        }
    };

    // now finish writing it
    {
        auto data = TestIncompleteDirData{};
        data.session = session_;
        data.tor = tor;

        auto const [begin, end] = tor->block_span_for_piece(data.pieceIndex);

        for (tr_block_index_t block_index = begin; block_index < end; ++block_index) {
            data.buf.resize(tr_block_info::BlockSize);
            std::ranges::fill(data.buf, '\0');
            data.block = block_index;
            session_->run_in_session_thread(test_incomplete_dir_threadfunc, &data);

            // save_block() may return before the write finishes
            auto const test = [tor, block_index]() {
                return tor->has_block(block_index);
            };
            EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        }
    }

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

TEST_P(IncompleteDirTest, setLocationFindsFilesOutsideCurrentDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };

    // With file #0 absent, current_dir falls back to incompleteDir even when
    // the torrent's other files are present in downloadDir.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    EXPECT_EQ(std::string_view{ incomplete_dir }, tor->current_dir().sv());

    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 1; i < n; ++i) {
        auto const file = tr_torrentFile(tor, i);
        auto const filename = tr_pathbuf{ download_dir, '/', file.name };
        createFileWithContents(filename, std::string(static_cast<size_t>(file.length), '\0'));
    }

    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // The absent file remains absent, while every existing file is moved from
    // downloadDir even though it was not the torrent-wide current_dir.
    EXPECT_EQ(""s, tr_torrentFindFile(tor, 0));
    for (tr_file_index_t i = 1; i < n; ++i) {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    tr_torrentRemove(tor, true);
}

TEST_P(IncompleteDirTest, setLocationMovesFromIncompleteDirToDownloadDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);

    // The destination is also one of the source directories. Cleanup must not
    // remove files after moving them there from incompleteDir.
    auto state = -1;
    tr_torrentSetLocation(tor, download_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i) {
        auto const file = tr_torrentFile(tor, i);
        auto expected = tr_pathbuf{ download_dir, '/', file.name };
        if (file.have < file.length) {
            expected += tr_torrent_files::PartialFileSuffix;
        }

        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    tr_torrentRemove(tor, true);
}

TEST_P(IncompleteDirTest, setLocationLeavesDuplicateSourceFileInPlace)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);

    auto const file = tr_torrentFile(tor, 1);
    auto const download_file = tr_pathbuf{ download_dir, '/', file.name };
    auto const incomplete_file = tr_pathbuf{ incomplete_dir, '/', file.name };
    auto const contents = std::string(static_cast<size_t>(file.length), '\0');
    createFileWithContents(download_file, contents);
    createFileWithContents(incomplete_file, contents);

    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    auto const expected = tr_pathbuf{ target_dir, '/', file.name };
    EXPECT_EQ(expected, tr_torrentFindFile(tor, 1));
    EXPECT_TRUE(tr_sys_path_exists(incomplete_file));

    tr_sys_path_remove(incomplete_file);
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
