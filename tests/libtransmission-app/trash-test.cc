// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <unistd.h> // symlink()
#endif

#include <libtransmission/error.h>
#include <libtransmission/file-utils.h> // tr_file_save()
#include <libtransmission/file.h>

#include <libtransmission-app/trash.h>

#include "test-fixtures.h"

using namespace std::string_view_literals;

namespace
{

class TrashTest : public SandboxedTest
{
protected:
    // Paths the trash can was offered.
    std::vector<std::string> offered_;

    // A trash can that takes nothing, like one on a volume without a trash folder.
    [[nodiscard]] tr::app::TrashFunc refusingTrash()
    {
        return [this](std::string_view const path, std::string& why) {
            offered_.emplace_back(path);
            why = "no trash can here";
            return false;
        };
    }

    [[nodiscard]] std::string path(std::string_view const subpath) const
    {
        return fmt::format("{:s}/{:s}", sandbox_dir(), subpath);
    }

    std::string makeFile(std::string_view const subpath) const
    {
        auto file = path(subpath);
        tr_sys_dir_create(tr_sys_path_dirname(file), TR_SYS_DIR_CREATE_PARENTS, 0700);
        EXPECT_TRUE(tr_file_save(file, "x"sv));
        return file;
    }
};

} // namespace

TEST_F(TrashTest, leavesAnAcceptedPathToTheTrashCan)
{
    auto const file = makeFile("a.torrent");
    auto const trashed = path("trashed.torrent");
    auto const trash = [&trashed](std::string_view const from, std::string& /*why*/) {
        return tr_sys_path_rename(from, trashed);
    };

    auto error = tr_error{};
    EXPECT_TRUE(tr::app::trash_or_remove(file, &error, trash));
    EXPECT_FALSE(error);
    EXPECT_FALSE(tr_sys_path_exists(file));
    EXPECT_TRUE(tr_sys_path_exists(trashed));
}

TEST_F(TrashTest, deletesAFileTheTrashCanRefuses)
{
    auto const file = makeFile("a.torrent");

    auto error = tr_error{};
    EXPECT_TRUE(tr::app::trash_or_remove(file, &error, refusingTrash()));
    EXPECT_FALSE(error);
    EXPECT_FALSE(tr_sys_path_exists(file));
}

TEST_F(TrashTest, deletesARefusedFolderWithEverythingInside)
{
    auto const folder = path("Foo");
    makeFile("Foo/a");
    makeFile("Foo/.hidden");
    makeFile("Foo/sub/b");

    auto error = tr_error{};
    EXPECT_TRUE(tr::app::trash_or_remove(folder, &error, refusingTrash()));
    EXPECT_FALSE(error);
    EXPECT_EQ(std::vector{ folder }, offered_);
    EXPECT_FALSE(tr_sys_path_exists(folder));
}

#ifndef _WIN32
TEST_F(TrashTest, deletesASymlinkInARefusedFolderButNotItsTarget)
{
    auto const folder = path("Foo");
    auto const kept = makeFile("Keep/a");
    tr_sys_dir_create(folder, 0, 0700);
    ASSERT_EQ(0, symlink(path("Keep").c_str(), path("Foo/link").c_str()));

    EXPECT_TRUE(tr::app::trash_or_remove(folder, nullptr, refusingTrash()));
    EXPECT_FALSE(tr_sys_path_exists(folder));
    EXPECT_TRUE(tr_sys_path_exists(kept));
}
#endif

TEST_F(TrashTest, reportsAFailedDelete)
{
    auto error = tr_error{};
    EXPECT_FALSE(tr::app::trash_or_remove(path("missing"), &error, refusingTrash()));
    EXPECT_TRUE(error);
}
