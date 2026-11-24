// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdlib>
#include <string_view>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/platform.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;
using PlatformTest = ::tr::test::SessionTest;

#ifdef __linux__
namespace
{
// Lays out cgroup limit files under the sandbox, as if it were /sys/fs/cgroup.
class CgroupMemoryLimitTest : public ::tr::test::SandboxedTest
{
protected:
    void setLimit(std::string_view const dir, std::string_view const filename, std::string_view const text) const
    {
        auto const path = tr_pathbuf{ sandboxDir(), dir };
        ASSERT_TRUE(tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0700));
        ASSERT_TRUE(tr_file_save(tr_pathbuf{ path, '/', filename }, text));
    }

    [[nodiscard]] auto limit(std::string_view const proc_self_cgroup) const
    {
        return tr::platform::detail::cgroup_memory_limit(proc_self_cgroup, sandboxDir());
    }
};
} // namespace

TEST_F(CgroupMemoryLimitTest, v2ReadsMemoryMax)
{
    setLimit("", "memory.max", "536870912\n");

    EXPECT_EQ(536870912U, limit("0::/\n"));
}

TEST_F(CgroupMemoryLimitTest, v2MaxIsNoLimit)
{
    setLimit("", "memory.max", "max\n");

    EXPECT_FALSE(limit("0::/\n"));
}

TEST_F(CgroupMemoryLimitTest, lowestLimitAmongAncestorsWins)
{
    setLimit("/system.slice", "memory.max", "268435456\n");
    setLimit("/system.slice/app.service", "memory.max", "1073741824\n");

    EXPECT_EQ(268435456U, limit("0::/system.slice/app.service\n"));

    setLimit("/system.slice", "memory.max", "max\n");

    EXPECT_EQ(1073741824U, limit("0::/system.slice/app.service\n"));
    EXPECT_EQ(1073741824U, limit("0::/system.slice/app.service/\n"));
}

TEST_F(CgroupMemoryLimitTest, v1ReadsTheMemoryControllersHierarchy)
{
    // The container's own cgroup is mounted at the root,
    // and the path that names it from the host's root is not there.
    setLimit("/memory", "memory.limit_in_bytes", "134217728\n");

    EXPECT_EQ(134217728U, limit("12:memory:/docker/abc\n4:cpu,cpuacct:/docker/abc\n1:name=systemd:/docker/abc\n"));
    EXPECT_EQ(134217728U, limit("7:cpu,memory:/docker/abc\n"));

    // A hybrid host has a v2 hierarchy too, mounted elsewhere.
    EXPECT_EQ(134217728U, limit("12:memory:/docker/abc\n0::/docker/abc\n"));
}

TEST_F(CgroupMemoryLimitTest, otherControllersSetNoLimit)
{
    setLimit("", "memory.max", "1048576\n");
    setLimit("/memory", "memory.limit_in_bytes", "1048576\n");

    EXPECT_FALSE(limit(""));
    EXPECT_FALSE(limit("4:cpu,cpuacct:/\n1:name=systemd:/\n"));
}

TEST_F(CgroupMemoryLimitTest, pathOutsideTheMountIsSkipped)
{
    setLimit("", "memory.max", "1048576\n");

    EXPECT_FALSE(limit("0::/../other\n"));
    EXPECT_FALSE(limit("0::/..\n"));
}

TEST_F(CgroupMemoryLimitTest, malformedLinesAreSkipped)
{
    setLimit("", "memory.max", "1048576\n");
    setLimit("/memory", "memory.limit_in_bytes", "1048576\n");

    static auto constexpr Lines = std::array{
        "0"sv, "0:"sv, "0::"sv, "0::relative"sv, "5:memory"sv, "5:memory:"sv, "5:memory:relative"sv, "::/"sv, "\n\n"sv,
    };
    for (auto const line : Lines) {
        EXPECT_FALSE(limit(line)) << line;
    }
}
#endif

TEST(MemorySizeTest, isKnown)
{
    auto const memory = tr::platform::get_memory_size();

    ASSERT_TRUE(memory);
    EXPECT_GT(*memory, 0U);
}

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

TEST_F(PlatformTest, defaultDownloadDirXdg)
{
    setenv("HOME", sandboxDir().c_str(), 1);
    setenv("XDG_CONFIG_HOME", LIBTRANSMISSION_TEST_ASSETS_DIR, 1);

    auto const expected = fmt::format("{:s}/UserDirsDownloads"sv, sandboxDir());
    auto const actual = tr::platform::get_download_dir();
    EXPECT_EQ(expected, actual);

    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
}

#if !defined(_WIN32) && !defined(__HAIKU__)
TEST_F(PlatformTest, defaultDownloadDir)
{
    setenv("HOME", sandboxDir().c_str(), 1);

    auto const expected = fmt::format("{:s}/Downloads"sv, sandboxDir());
    auto const actual = tr::platform::get_download_dir();
    EXPECT_EQ(expected, actual);

    unsetenv("HOME");
}
#endif

TEST_F(PlatformTest, defaultConfigDirEnv)
{
    setenv("TRANSMISSION_HOME", sandboxDir().c_str(), 1);

    auto const expected = sandboxDir();
    auto const actual = tr::platform::get_default_config_dir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("TRANSMISSION_HOME");
}

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__HAIKU__)

TEST_F(PlatformTest, defaultConfigDirXdgConfig)
{
    setenv("XDG_CONFIG_HOME", sandboxDir().c_str(), 1);

    auto const expected = fmt::format("{:s}/appname", sandboxDir());
    auto const actual = tr::platform::get_default_config_dir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("XDG_CONFIG_HOME");
}

TEST_F(PlatformTest, defaultConfigDirXdgConfigHome)
{
    unsetenv("TRANSMISSION_HOME");
    unsetenv("XDG_CONFIG_HOME");
    auto const home = tr_pathbuf{ sandboxDir(), "/home/user" };
    setenv("HOME", home.c_str(), 1);

    auto const expected = fmt::format("{:s}/.config/appname", home.sv());
    auto const actual = tr::platform::get_default_config_dir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("HOME");
}

#endif

TEST_F(PlatformTest, webClientDirEnvClutch)
{
    setenv("CLUTCH_HOME", sandboxDir().c_str(), 1);

    EXPECT_EQ(sandboxDir(), tr_getWebClientDir(session_));

    unsetenv("CLUTCH_HOME");
}

TEST_F(PlatformTest, webClientDirEnvTr)
{
    setenv("TRANSMISSION_WEB_HOME", sandboxDir().c_str(), 1);

    EXPECT_EQ(sandboxDir(), tr_getWebClientDir(session_));

    unsetenv("TRANSMISSION_WEB_HOME");
}

#if !defined(BUILD_MAC_CLIENT) && !defined(_WIN32)
TEST_F(PlatformTest, webClientDirXdgDataHome)
{
    // Lay out the sandbox the way the build installs the web client under the data dir.
    // A web client installed outside the data dir is one the lookup can't find.
    auto constexpr InstallSubdir = std::string_view{ LIBTRANSMISSION_TEST_WEB_CLIENT_INSTALL_SUBDIR };
    ASSERT_FALSE(InstallSubdir.starts_with(".."sv));

    setenv("XDG_DATA_HOME", sandboxDir().c_str(), 1);

    auto const expected = tr_pathbuf{ sandboxDir(), '/', InstallSubdir };
    auto const index_html = tr_pathbuf{ expected, "/index.html"sv };
    EXPECT_TRUE(tr_sys_dir_create(expected, TR_SYS_DIR_CREATE_PARENTS, 0777));
    EXPECT_TRUE(tr_file_save(index_html, "<html></html>"sv));

    EXPECT_EQ(expected, tr_getWebClientDir(session_));

    unsetenv("XDG_DATA_HOME");
}
#endif
