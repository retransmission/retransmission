// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cmath> // sqrt()
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/utils.h>

#include "test-fixtures.h"

using UtilsTest = ::tr::test::TransmissionTest;
using namespace std::literals;

TEST_F(UtilsTest, truncd)
{
    EXPECT_EQ("100.00%"sv, fmt::format("{:.2f}%", 99.999));
    EXPECT_EQ("99.99%"sv, fmt::format("{:.2f}%", tr_truncd(99.999, 2)));
    EXPECT_EQ("403650.6562"sv, fmt::format("{:.4f}", tr_truncd(403650.656250, 4)));
    EXPECT_EQ("2.15"sv, fmt::format("{:.2f}", tr_truncd(2.15, 2)));
    EXPECT_EQ("2.05"sv, fmt::format("{:.2f}", tr_truncd(2.05, 2)));
    EXPECT_EQ("3.33"sv, fmt::format("{:.2f}", tr_truncd(3.333333, 2)));
    EXPECT_EQ("3"sv, fmt::format("{:.0f}", tr_truncd(3.333333, 0)));
    EXPECT_EQ("3"sv, fmt::format("{:.0f}", tr_truncd(3.9999, 0)));

#if !(defined(_MSC_VER) || (defined(__MINGW32__) && defined(__MSVCRT__)))
    /* FIXME: MSCVRT behaves differently in case of nan */
    auto const nan = sqrt(-1.0);
    auto const nanstr = fmt::format("{:.2f}", tr_truncd(nan, 2));
    EXPECT_TRUE(strstr(nanstr.c_str(), "nan") != nullptr || strstr(nanstr.c_str(), "NaN") != nullptr);
#endif
}

TEST_F(UtilsTest, mimeTypes)
{
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.flac"sv));
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.FLAC"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename(".avi"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename("/path/to/FILENAME.AVI"sv));
    EXPECT_EQ("application/octet-stream"sv, tr_get_mime_type_for_filename("music.ajoijfeisfe"sv));
}
