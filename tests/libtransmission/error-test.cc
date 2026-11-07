// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#endif

#include <gtest/gtest.h>

#include <libtransmission/error.h>

#include "test-fixtures.h"

using namespace std::literals;

TEST(Error, errorSet)
{
    auto error = tr_error{};

    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(error.has_value()) << error;
    error.set(2, "oops"sv);
    EXPECT_TRUE(error);
    EXPECT_TRUE(error.has_value()) << error;
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("oops"sv, error.message());

    error.prefix_message("error: ");
    EXPECT_TRUE(error);
    EXPECT_TRUE(error.has_value()) << error;
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("error: oops"sv, error.message());

    error = {};
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(error.has_value()) << error;
}

TEST(Error, isOutOfSpace)
{
#ifdef _WIN32
    auto const out_of_space = { ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL, ERROR_DISK_QUOTA_EXCEEDED, ERROR_FILE_TOO_LARGE };
    auto const other = ERROR_ACCESS_DENIED;
#else
    auto const out_of_space = { ENOSPC, EDQUOT, EFBIG };
    auto const other = EACCES;
#endif

    for (auto const code : out_of_space) {
        EXPECT_TRUE(tr_error::is_out_of_space(code)) << code;
        EXPECT_TRUE((tr_error{ code, "oops" }).is_out_of_space()) << code;
    }

    EXPECT_FALSE(tr_error::is_out_of_space(other));
    EXPECT_FALSE(tr_error{}.is_out_of_space());
}
