// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>

#include <giomm/file.h>
#include <giomm/init.h>

#include <glibmm/init.h>

#include <gtest/gtest.h>

#include <libtransmission/magnet-metainfo.h>

#include "Utils.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

class GtkUtilsTest : public GtkTest
{
protected:
    void SetUp() override
    {
        GtkTest::SetUp();
        Glib::init();
        Gio::init();
    }
};

TEST_F(GtkUtilsTest, addKeyNamesALocalFileByItsPath)
{
    EXPECT_EQ("/some/dir/a.torrent"sv, gtr_add_key(Gio::File::create_for_path("/some/dir/a.torrent")));
    EXPECT_EQ("/some/dir/a.torrent"sv, gtr_add_key(Gio::File::create_for_uri("file:///some/dir/a.torrent")));
}

TEST_F(GtkUtilsTest, addKeyNamesAUrlByItself)
{
    EXPECT_EQ("https://example.com/a.torrent"sv, gtr_add_key(Gio::File::create_for_uri("https://example.com/a.torrent")));
}

// How a `Gio::File` spells a magnet link depends on the GVfs backend that made it.
TEST_F(GtkUtilsTest, addKeyReturnsAMagnetLinkThatStillParses)
{
    static auto constexpr Link = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=name"sv;

    auto expected = tr_magnet_metainfo{};
    ASSERT_TRUE(expected.parseMagnet(Link));

    auto actual = tr_magnet_metainfo{};
    EXPECT_TRUE(actual.parseMagnet(gtr_add_key(Gio::File::create_for_commandline_arg(std::string{ Link }))));
    EXPECT_EQ(expected.info_hash(), actual.info_hash());
    EXPECT_EQ(expected.name(), actual.name());
}

} // namespace
