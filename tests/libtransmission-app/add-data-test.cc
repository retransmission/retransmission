// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/digest.h>
#include <libtransmission/file-utils.h> // tr_file_save()
#include <libtransmission/torrent-metainfo.h>

#include <libtransmission-app/add-data.h>
#include <libtransmission-app/interop.h>

#include "test-fixtures.h"

using namespace std::string_view_literals;
using tr::app::AddData;
using Type = tr::app::AddData::Type;

namespace
{

auto constexpr Magnet = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"sv;
auto constexpr Url = "https://example.com/some%20dir/x%20y.torrent?a=b"sv;

// The smallest thing tr_torrent_metainfo will accept: one 1-byte file named `a`, one 20-byte piece hash.
[[nodiscard]] std::string minimal_torrent()
{
    return fmt::format("d4:infod6:lengthi1e4:name1:a12:piece lengthi16384e6:pieces20:{:s}ee", std::string(20U, '\x01'));
}

class AddDataTest : public SandboxedTest
{
protected:
    [[nodiscard]] std::string save(std::string_view const name, std::string_view const contents) const
    {
        auto path = fmt::format("{:s}/{:s}", sandbox_dir(), name);
        EXPECT_TRUE(tr_file_save(path, contents));
        return path;
    }
};

TEST_F(AddDataTest, classifiesALinkAsItself)
{
    auto const url = AddData{ Url };
    EXPECT_EQ(Type::Url, url.type);
    EXPECT_EQ(Url, url.source);

    auto const magnet = AddData{ Magnet };
    EXPECT_EQ(Type::Magnet, magnet.type);
    EXPECT_EQ(Magnet, magnet.source);
}

TEST_F(AddDataTest, readsALocalFile)
{
    auto const path = save("ok.torrent"sv, minimal_torrent());

    auto const add = AddData{ path };
    EXPECT_EQ(Type::File, add.type);
    EXPECT_EQ(path, add.source);
    EXPECT_EQ(minimal_torrent(), add.metainfo);
}

#ifndef _WIN32
TEST_F(AddDataTest, readsAFileUri)
{
    auto const path = save("ok.torrent"sv, minimal_torrent());

    auto const add = AddData{ fmt::format("file://{:s}", path) };
    EXPECT_EQ(Type::File, add.type);
    EXPECT_EQ(path, add.source);
}
#endif

TEST_F(AddDataTest, resolvesARelativePath)
{
    auto const path = save("ok.torrent"sv, minimal_torrent());
    auto const relative = std::filesystem::relative(path).generic_string();
    if (std::empty(relative)) {
        GTEST_SKIP() << "the sandbox has no path relative to the working directory, e.g. it's on another drive";
    }

    auto const add = AddData{ relative };
    EXPECT_EQ(Type::File, add.type);
    EXPECT_TRUE(std::filesystem::equivalent(path, add.source));
    EXPECT_TRUE(std::filesystem::path{ add.source }.is_absolute());
}

// So that adding it reports the failure, rather than doing nothing.
TEST_F(AddDataTest, aFileThatCannotBeReadIsStillAFile)
{
    auto const add = AddData{ sandbox_dir() };
    EXPECT_EQ(Type::File, add.type);
    EXPECT_TRUE(std::empty(add.metainfo));
}

TEST_F(AddDataTest, readsBase64Metainfo)
{
    auto const add = AddData{ tr_base64_encode(minimal_torrent()) };
    EXPECT_EQ(Type::Metainfo, add.type);
    EXPECT_EQ(minimal_torrent(), add.metainfo);
}

TEST_F(AddDataTest, refusesWhatItCannotClassify)
{
    for (auto const key : { ""sv, "!!! not base64 !!!"sv, "/no/such/file.torrent"sv, "aGVsbG8="sv }) {
        EXPECT_EQ(Type::None, AddData{ key }.type) << key;
    }
}

// The round trip both ends of the wire perform. The launch encodes the file it read,
// and the client that took the launch reads the contents back out.
TEST_F(AddDataTest, wireFormReadsATorrentFileAsItsContents)
{
    auto const encoded = tr::interop::encode_metainfo_arg(save("ok.torrent"sv, minimal_torrent()));
    ASSERT_TRUE(encoded.has_value());

    auto const add = AddData::from_wire_metainfo(*encoded);
    EXPECT_EQ(Type::Metainfo, add.type);
    EXPECT_EQ(minimal_torrent(), add.metainfo);
}

TEST_F(AddDataTest, wireFormReadsALinkAsItself)
{
    EXPECT_EQ(Type::Url, AddData::from_wire_metainfo(Url).type);
    EXPECT_EQ(Type::Magnet, AddData::from_wire_metainfo(Magnet).type);
}

// A bare path is not a form AddMetainfo defines. Reading one would open a file the sender
// named and this process can reach. Across a bus, the sender need not even own it.
TEST_F(AddDataTest, wireFormRefusesAPathNamingAFileOnThisHost)
{
    EXPECT_EQ(Type::None, AddData::from_wire_metainfo(save("secret.torrent"sv, minimal_torrent())).type);
}

TEST_F(AddDataTest, wireFormRefusesWhatIsNeitherFormAtAll)
{
    EXPECT_EQ(Type::None, AddData::from_wire_metainfo(""sv).type);
    EXPECT_EQ(Type::None, AddData::from_wire_metainfo("!!! not base64 !!!"sv).type);
}

TEST_F(AddDataTest, namesAFile)
{
    auto const path = save("some.name.torrent"sv, minimal_torrent());

    auto const add = AddData{ path };
    EXPECT_EQ(path, add.readable_name());
    EXPECT_EQ("some.name"sv, add.readable_short_name());
}

TEST_F(AddDataTest, namesAUrl)
{
    auto const add = AddData{ Url };
    EXPECT_EQ(Url, add.readable_name());
    EXPECT_EQ("x y.torrent"sv, add.readable_short_name());
}

TEST_F(AddDataTest, namesAMagnetByItsNameOrElseItsHash)
{
    EXPECT_EQ("0123456789abcdef0123456789abcdef01234567"sv, AddData{ Magnet }.readable_name());

    auto const named = AddData{ fmt::format("{:s}&dn=Hello", Magnet) };
    EXPECT_EQ("Hello"sv, named.readable_name());
    EXPECT_EQ("Hello"sv, named.readable_short_name());
}

TEST_F(AddDataTest, namesMetainfoByItsTorrent)
{
    EXPECT_EQ("a"sv, AddData{ tr_base64_encode(minimal_torrent()) }.readable_name());
}

TEST_F(AddDataTest, parsesWhatItHasTheContentsOf)
{
    auto metainfo = tr_torrent_metainfo{};
    ASSERT_TRUE(metainfo.parse_benc(minimal_torrent()));

    EXPECT_EQ(metainfo.info_hash(), AddData{ save("ok.torrent"sv, minimal_torrent()) }.parsed()->info_hash());
    EXPECT_EQ(metainfo.info_hash(), AddData{ tr_base64_encode(minimal_torrent()) }.parsed()->info_hash());
    EXPECT_EQ(tr_sha1_from_string("0123456789abcdef0123456789abcdef01234567"sv), AddData{ Magnet }.parsed()->info_hash());

    EXPECT_FALSE(AddData{ Url }.parsed().has_value());
    EXPECT_FALSE(AddData{}.parsed().has_value());
    EXPECT_FALSE(AddData{ save("corrupt.torrent"sv, "not a torrent"sv) }.parsed().has_value());
}

} // namespace
