// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>

#include <giomm/init.h>

#include <gtest/gtest.h>

#include <libtransmission/file-utils.h>

#include "DirMonitor.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

class GioDirMonitorTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        SandboxedTest::SetUp();
        Gio::init();
    }

    void drop_file() const
    {
        EXPECT_TRUE(tr_file_save(sandbox_dir() + "/new.torrent", "contents"sv));
    }

    GioDirMonitorMaker maker_;
};

} // namespace

TEST_F(GioDirMonitorTest, reportsANewFile)
{
    auto changes = 0;
    auto const monitor = maker_.create(sandbox_dir(), [&changes] { ++changes; });
    ASSERT_NE(nullptr, monitor);

    drop_file();
    EXPECT_TRUE(spin_until([&changes] { return changes != 0; }));
}

TEST_F(GioDirMonitorTest, staysQuietOnceDestroyed)
{
    auto changes = 0;
    auto monitor = maker_.create(sandbox_dir(), [&changes] { ++changes; });
    ASSERT_NE(nullptr, monitor);
    monitor.reset();

    drop_file();
    spin_for(300ms);
    EXPECT_EQ(0, changes);
}
