// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <string>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/quark.h>
#include <libtransmission/shared-string.h>
#include <libtransmission/torrent-builder.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{
auto constexpr TorrentFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/debian-11.2.0-amd64-DVD-1.iso.torrent"sv;
auto constexpr ResumeFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/debian-11.2.0-amd64-DVD-1.iso.resume"sv;
auto constexpr InfoHashStr = "c9a337562cb0360fd6f5ab40fd2b1b81d5325dbd"sv;

// The values `ResumeFile` records, for tests that pit the builder against it.
auto constexpr ResumePeerLimit = uint16_t{ 50 };
auto constexpr ResumeIsPaused = true;
auto constexpr ResumeIsSequential = false;

class TorrentBuilderTest : public SessionTest
{
protected:
    // Puts the torrent, and optionally its resume file, where tr_sessionLoadTorrents() looks.
    [[nodiscard]] bool installFixture(bool const with_resume) const
    {
        auto error = tr_error{};

        if (!tr_sys_path_copy(TorrentFile, tr_pathbuf{ session_->torrentDir(), '/', InfoHashStr, ".torrent"sv }, &error)) {
            return false;
        }

        return !with_resume ||
            tr_sys_path_copy(ResumeFile, tr_pathbuf{ session_->resumeDir(), '/', InfoHashStr, ".resume"sv }, &error);
    }
};

TEST_F(TorrentBuilderTest, settingsReachTheTorrent)
{
    auto constexpr PeerLimit = uint16_t{ 17 };
    auto constexpr FromPiece = tr_piece_index_t{ 3 };

    auto const download_dir = tr_pathbuf{ sandboxDir(), "/builder-download-dir"sv };
    auto const labels = tr_labels_t{ tr::shared_string{ "alpha"sv }, tr::shared_string{ "beta"sv } };

    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(TorrentFile));
    builder.set_paused(true);
    builder.set_peer_limit(PeerLimit);
    builder.set_download_dir(download_dir.sv());
    builder.set_labels(tr_labels_t{ labels });
    builder.set_bandwidth_priority(TR_PRI_HIGH);
    builder.set_sequential_download(true);
    builder.set_sequential_download_from_piece(FromPiece);

    // a fresh add is briefly TR_STATUS_CHECK_WAIT / TR_STATUS_CHECK while its
    // initial verify runs, so wait that out before checking the activity
    auto* const tor = createTorrentAndWaitForVerifyDone(&builder);
    ASSERT_NE(nullptr, tor);

    EXPECT_EQ(TR_STATUS_STOPPED, tr_torrentStat(tor).activity);
    EXPECT_EQ(PeerLimit, tor->peer_limit());
    EXPECT_EQ(download_dir.sv(), tor->download_dir().sv());
    EXPECT_EQ(labels, tor->labels());
    EXPECT_EQ(TR_PRI_HIGH, tor->get_priority());
    EXPECT_TRUE(tor->is_sequential_download());
    EXPECT_EQ(FromPiece, tor->sequential_download_from_piece());
}

TEST_F(TorrentBuilderTest, builderBeatsResumeFile)
{
    // Every value the builder sets below differs from the one ResumeFile records.
    auto constexpr PeerLimit = uint16_t{ ResumePeerLimit + 49 };
    static_assert(PeerLimit != ResumePeerLimit);

    if (!installFixture(true)) {
        GTEST_SKIP() << "Failed to set up the torrent and resume dirs";
    }

    auto builder = tr_torrent_builder{ session_ };
    builder.set_peer_limit(PeerLimit);
    builder.set_paused(!ResumeIsPaused);
    builder.set_sequential_download(!ResumeIsSequential);
    ASSERT_EQ(1U, tr_sessionLoadTorrents(session_, &builder));

    auto* const tor = session_->torrents().get(1U);
    ASSERT_NE(nullptr, tor);

    EXPECT_EQ(PeerLimit, tor->peer_limit());
    EXPECT_EQ(!ResumeIsSequential, tor->is_sequential_download());
    EXPECT_NE(TR_STATUS_STOPPED, tr_torrentStat(tor).activity);
}

TEST_F(TorrentBuilderTest, magnetLinkDropsTheSavedTorrentFile)
{
    // A magnet has no benc for save() to write, so the bytes of whatever
    // was set before it must not be left behind for save() to find.
    auto constexpr MagnetLink = "fa5794674a18241bec985ddc3390e3cb171345e4"sv;

    auto const target = tr_pathbuf{ sandboxDir(), "/saved.torrent"sv };

    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(TorrentFile));
    EXPECT_TRUE(builder.save(target));

    ASSERT_TRUE(builder.set_metainfo_from_magnet_link(MagnetLink));

    auto error = tr_error{};
    EXPECT_FALSE(builder.save(target, &error));
    EXPECT_TRUE(error);
    EXPECT_EQ(EINVAL, error.code());
}

TEST_F(TorrentBuilderTest, resumeFileBeatsBuilderForDownloadDir)
{
    // The resume file records where the data actually is, so it has to win:
    // tr_resume::load() reads the destination whenever it loads progress,
    // even though the builder claimed the DownloadDir field.
    if (!installFixture(false)) {
        GTEST_SKIP() << "Failed to set up the torrent dir";
    }

    auto const resumed_dir = std::string{ tr_pathbuf{ sandboxDir(), "/where-the-data-is"sv }.sv() };
    auto map = tr_variant::Map{ 1U };
    map.try_emplace(TR_KEY_destination, resumed_dir);
    auto out = tr_variant{ std::move(map) };
    auto serde = tr_variant_serde::benc();
    ASSERT_TRUE(serde.to_file(out, tr_pathbuf{ session_->resumeDir(), '/', InfoHashStr, ".resume"sv }));

    auto builder = tr_torrent_builder{ session_ };
    builder.set_paused(true);
    builder.set_download_dir(tr_pathbuf{ sandboxDir(), "/where-the-builder-wanted-it"sv }.sv());
    ASSERT_EQ(1U, tr_sessionLoadTorrents(session_, &builder));

    auto* const tor = session_->torrents().get(1U);
    ASSERT_NE(nullptr, tor);
    EXPECT_EQ(resumed_dir, tor->download_dir().sv());
}

TEST_F(TorrentBuilderTest, sessionDefaultsWhenBuilderIsSilent)
{
    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(TorrentFile));

    auto* const tor = tr_torrentNew(&builder, nullptr);
    ASSERT_NE(nullptr, tor);

    // The paused default is left out: a torrent that is not paused only starts
    // once its initial verify finishes, so its activity is not settled here.
    EXPECT_EQ(tr_sessionGetDownloadDir(session_), tor->download_dir().sv());
    EXPECT_EQ(tr_sessionGetPeerLimitPerTorrent(session_), tor->peer_limit());
}

TEST_F(TorrentBuilderTest, filePrioritiesAndWantedReachTheTorrent)
{
    auto constexpr MultifileTorrent = LIBTRANSMISSION_TEST_ASSETS_DIR "/alice_in_wonderland_librivox_archive.torrent"sv;
    auto constexpr High = std::array{ tr_file_index_t{ 0 } };
    auto constexpr Low = std::array{ tr_file_index_t{ 1 } };
    auto constexpr Unwanted = std::array{ tr_file_index_t{ 2 } };

    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(MultifileTorrent));
    builder.set_paused(true);
    builder.set_file_priorities(High, TR_PRI_HIGH);
    builder.set_file_priorities(Low, TR_PRI_LOW);
    builder.set_files_wanted(Unwanted, false);

    auto* const tor = tr_torrentNew(&builder, nullptr);
    ASSERT_NE(nullptr, tor);
    ASSERT_LE(3U, tor->file_count());

    EXPECT_EQ(TR_PRI_HIGH, tr_torrentFile(tor, 0).priority);
    EXPECT_EQ(TR_PRI_LOW, tr_torrentFile(tor, 1).priority);
    EXPECT_FALSE(tr_torrentFile(tor, 2).wanted);
    EXPECT_TRUE(tr_torrentFile(tor, 0).wanted);
}

TEST_F(TorrentBuilderTest, sourceFilenameIsSetOnlyByTheFileSetter)
{
    auto constexpr MagnetLink = "fa5794674a18241bec985ddc3390e3cb171345e4"sv;

    auto builder = tr_torrent_builder{ session_ };
    EXPECT_TRUE(std::empty(builder.source_filename()));

    ASSERT_TRUE(builder.set_metainfo_from_file(TorrentFile));
    EXPECT_EQ(TorrentFile, builder.source_filename());

    ASSERT_TRUE(builder.set_metainfo_from_magnet_link(MagnetLink));
    EXPECT_TRUE(std::empty(builder.source_filename()));
}

TEST_F(TorrentBuilderTest, bandwidthPriorityIgnoresOutOfRangeValues)
{
    auto builder = tr_torrent_builder{ session_ };
    ASSERT_TRUE(builder.set_metainfo_from_file(TorrentFile));
    builder.set_paused(true);
    builder.set_bandwidth_priority(TR_PRI_HIGH);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) the out-of-range value is the point of the test
    builder.set_bandwidth_priority(static_cast<tr_priority_t>(42));

    auto* const tor = tr_torrentNew(&builder, nullptr);
    ASSERT_NE(nullptr, tor);
    EXPECT_EQ(TR_PRI_HIGH, tor->get_priority());
}

} // namespace
} // namespace tr::test
