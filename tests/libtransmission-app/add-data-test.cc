// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/digest.h>
#include <libtransmission/file-utils.h> // tr_file_save()
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/types.h> // tr_file_index_t, tr_priority_t

#include <libtransmission-app/add-data.h>
#include <libtransmission-app/interop.h>
#include <libtransmission-app/torrent.h>

#include "test-fixtures.h"

using namespace std::string_view_literals;
using tr::app::AddData;
using tr::app::AddOptions;
using tr::app::TorrentFile;
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

// two files in a folder: `d/a`, 1 byte, and `d/b`, 2 bytes
[[nodiscard]] std::string two_file_torrent()
{
    return fmt::format(
        "d4:infod5:filesld6:lengthi1e4:pathl1:aeed6:lengthi2e4:pathl1:beee4:name1:d12:piece lengthi16384e6:pieces20:{:s}ee",
        std::string(20U, '\x01'));
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

TEST_F(AddDataTest, listsTheFilesOfWhatItHasTheContentsOf)
{
    auto const files = AddData{ save("ok.torrent"sv, two_file_torrent()) }.files();
    ASSERT_EQ(2U, std::size(files));
    EXPECT_EQ("d/a"sv, files[0].name);
    EXPECT_EQ(1U, files[0].size);
    EXPECT_EQ(0U, files[0].index);
    EXPECT_EQ("d/b"sv, files[1].name);
    EXPECT_EQ(2U, files[1].size);
    EXPECT_EQ(1U, files[1].index);
    for (auto const& file : files) {
        EXPECT_EQ(0U, file.have);
        EXPECT_EQ(TR_PRI_NORMAL, file.priority);
        EXPECT_TRUE(file.wanted);
    }

    EXPECT_EQ(1U, std::size(AddData{ tr_base64_encode(minimal_torrent()) }.files()));

    EXPECT_TRUE(std::empty(AddData{ Magnet }.files()));
    EXPECT_TRUE(std::empty(AddData{ Url }.files()));
    EXPECT_TRUE(std::empty(AddData{}.files()));
    EXPECT_TRUE(std::empty(AddData{ save("corrupt.torrent"sv, "not a torrent"sv) }.files()));
}

TEST_F(AddDataTest, setsTheFileChoicesFromAFileList)
{
    auto const make_file = [](tr_file_index_t const index, tr_priority_t const priority, bool const wanted) {
        auto file = TorrentFile{};
        file.index = index;
        file.priority = priority;
        file.wanted = wanted;
        return file;
    };

    auto const files = std::vector{
        make_file(0U, TR_PRI_NORMAL, true),
        make_file(1U, TR_PRI_LOW, false),
        make_file(2U, TR_PRI_HIGH, true),
        make_file(3U, TR_PRI_LOW, true),
    };

    auto options = AddOptions{};
    options.files_unwanted = { 7 }; // replaced, not appended to
    options.set_file_choices(files);
    EXPECT_EQ(std::vector{ 1 }, options.files_unwanted);
    EXPECT_EQ((std::vector{ 1, 3 }), options.priority_low);
    EXPECT_EQ(std::vector{ 2 }, options.priority_high);
}

} // namespace
