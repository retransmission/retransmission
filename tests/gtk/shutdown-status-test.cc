// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include "ShutdownStatus.h"

using namespace std::literals;
using Kind = tr_disk_activity::Op::Kind;

namespace
{
[[nodiscard]] tr_disk_activity::Op make_op(
    Kind const kind,
    std::string_view const name,
    uint64_t const done,
    uint64_t const total)
{
    auto op = tr_disk_activity::Op{};
    op.kind = kind;
    op.torrent_name = name;
    op.done = done;
    op.total = total;
    return op;
}

[[nodiscard]] tr_disk_activity draining()
{
    auto activity = tr_disk_activity{};
    activity.phase = tr_close_phase::DrainingDisk;
    return activity;
}
} // namespace

TEST(ShutdownStatusTest, aMoveLeadsAndShowsItsFiles)
{
    auto activity = draining();
    activity.queued_write_bytes = 1000U;
    activity.in_flight.emplace_back(make_op(Kind::Write, "written", 0U, 1000U));
    auto move = make_op(Kind::Move, "Fedora", 1'200'000'000U, 4'700'000'000U);
    move.destination = "/media";
    move.file_index = 2U;
    move.file_count = 12U;
    activity.in_flight.emplace_back(move);

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Moving files…", status.headline);
    EXPECT_EQ("'Fedora' to '/media' — file 3 of 12, 1.20 GB of 4.70 GB", status.detail);
}

TEST(ShutdownStatusTest, writesCountTheOthersAndTheBytesLeft)
{
    auto activity = draining();
    activity.queued_write_bytes = 120'000'000U;
    activity.in_flight.emplace_back(make_op(Kind::Write, "small", 0U, 16'384U));
    activity.in_flight.emplace_back(make_op(Kind::Write, "big", 0U, 1'048'576U));
    activity.in_flight.emplace_back(make_op(Kind::Write, "medium", 0U, 65'536U));

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Finishing disk writes…", status.headline);
    EXPECT_EQ("Writing 'big' and 2 more — 120 MB remaining", status.detail);
}

TEST(ShutdownStatusTest, aRemovalCountsFilesAndTheTorrentsWaiting)
{
    auto activity = draining();
    activity.queued_admin_count = 2U;
    activity.in_flight.emplace_back(make_op(Kind::Remove, "Linux ISOs", 1204U, 3000U));

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Deleting files…", status.headline);
    // the test runs in the C locale, which groups no digits
    EXPECT_EQ("'Linux ISOs' — 1204 of 3000 files, and 2 more torrents waiting", status.detail);
}

TEST(ShutdownStatusTest, aRenameHasNoDetail)
{
    auto activity = draining();
    activity.in_flight.emplace_back(make_op(Kind::Rename, "name", 0U, 0U));

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Finishing disk writes…", status.headline);
    EXPECT_EQ("", status.detail);
}

TEST(ShutdownStatusTest, trackersCountDown)
{
    auto activity = tr_disk_activity{};
    activity.phase = tr_close_phase::WaitingTrackers;
    activity.trackers_total = 12U;
    activity.trackers_pending = 9U;
    activity.trackers_time_left = 6s;

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Closing connections…", status.headline);
    EXPECT_EQ("Sending upload/download totals to trackers… 3 of 12 replied, 6 seconds left", status.detail);
}

TEST(ShutdownStatusTest, otherwiseItsJustShuttingDown)
{
    auto activity = tr_disk_activity{};
    activity.phase = tr_close_phase::Finalizing;

    auto const status = make_shutdown_status(activity);
    EXPECT_EQ("Shutting down…", status.headline);
    EXPECT_EQ("", status.detail);
}
