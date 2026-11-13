// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/peer-log.h>
#include <libtransmission/quark.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

// 2023-11-14T22:13:20Z
auto constexpr Now = time_t{ 1'700'000'000 };

[[nodiscard]] std::vector<std::byte> message(uint8_t id, size_t payload_size)
{
    auto const len = static_cast<uint32_t>(payload_size + 1U);
    auto ret = std::vector<std::byte>{ std::byte(len >> 24U), std::byte(len >> 16U), std::byte(len >> 8U), std::byte(len) };
    ret.push_back(std::byte{ id });
    ret.resize(std::size(ret) + payload_size, std::byte{ 0xAB });
    return ret;
}

[[nodiscard]] PeerConnectionInfo makeConn()
{
    auto conn = PeerConnectionInfo{};
    conn.info_hash_string = "0123456789abcdef0123456789abcdef01234567";
    conn.torrent_name = "test torrent";
    conn.peer = *tr_socket_address::from_string("192.0.2.7:6881"sv);
    conn.local_port = 51413U;
    conn.is_utp = true;
    conn.client = "Test 1.0";
    conn.peer_id = { '-', 'T', 'T', '0', '0', '0', '1', '-' };
    return conn;
}

[[nodiscard]] std::string readFile(std::string_view path)
{
    auto contents = std::vector<char>{};
    EXPECT_TRUE(tr_file_read(path, contents));
    return { std::begin(contents), std::end(contents) };
}

TEST(PeerMessageRing, keepsPieceHeaderAndSomeBlockData)
{
    auto ring = PeerMessageRing{};
    auto const piece = message(7U, 8U + 16384U);
    ring.add(true, piece, {}, std::size(piece), 0U);

    ASSERT_EQ(1U, std::size(ring.messages()));
    auto const& seg = ring.messages().front();
    EXPECT_EQ(4U + 1U + 8U + PeerMessageRing::PieceDataKept, std::size(seg.bytes));
    EXPECT_EQ(std::size(piece), seg.original_size);
}

TEST(PeerMessageRing, joinsHeadAndTail)
{
    auto ring = PeerMessageRing{};
    auto const have = message(4U, 4U);
    auto const head = std::span{ have }.first(5U);
    auto const tail = std::span{ have }.subspan(5U);
    ring.add(true, head, tail, std::size(have), 0U);

    ASSERT_EQ(1U, std::size(ring.messages()));
    EXPECT_EQ(have, ring.messages().front().bytes);
}

TEST(PeerMessageRing, dropsTheOldestMessages)
{
    auto ring = PeerMessageRing{};
    for (size_t i = 0; i < PeerMessageRing::MaxMessages + 5U; ++i) {
        auto const have = message(4U, 4U);
        ring.add(i % 2U == 0U, have, {}, std::size(have), i);
    }
    EXPECT_EQ(PeerMessageRing::MaxMessages, std::size(ring.messages()));
    EXPECT_EQ(5U, ring.messages().front().time_usec);

    // a few large messages hit the byte limit before the count limit
    ring = PeerMessageRing{};
    for (size_t i = 0; i < 4U; ++i) {
        auto const big = message(20U, PeerMessageRing::MaxBytes / 3U);
        ring.add(true, big, {}, std::size(big), i);
    }
    EXPECT_EQ(2U, std::size(ring.messages()));
}

TEST(PeerLog, parsesCaptureMode)
{
    EXPECT_EQ(PeerCaptureMode::Off, peer_capture_mode_from_string("off"sv));
    EXPECT_EQ(PeerCaptureMode::Off, peer_capture_mode_from_string(""sv));
    EXPECT_EQ(PeerCaptureMode::Rejected, peer_capture_mode_from_string("rejected"sv));
    EXPECT_EQ(PeerCaptureMode::Anomalies, peer_capture_mode_from_string("anomalies"sv));
    EXPECT_FALSE(peer_capture_mode_from_string("all"sv));
}

class PeerLogTest : public SandboxedTest
{
};

TEST_F(PeerLogTest, writesOneJsonLinePerClose)
{
    auto log = PeerLog{};
    log.configure(true, sandboxDir(), PeerCaptureMode::Off);

    auto const conn = makeConn();
    log.log_close(
        conn,
        PeerCloseEvent{ .reason = PeerCloseReason::BadHave,
                        .detail = "got Have: 9 but torrent only has 4 pieces",
                        .connected_at = Now - 30,
                        .closed_at = Now,
                        .piece_bytes_up = 10U,
                        .piece_bytes_down = 20U,
                        .capture_file = "captures/x.pcapng" });
    log.log_close(
        conn,
        PeerCloseEvent{ .reason = PeerCloseReason::SocketError,
                        .detail = {},
                        .connected_at = 0,
                        .closed_at = Now,
                        .piece_bytes_up = 0U,
                        .piece_bytes_down = 0U,
                        .capture_file = {} });

    auto const contents = readFile(tr_pathbuf{ sandboxDir(), "/events-2023-11-14.jsonl"sv });
    auto const newline = contents.find('\n');
    ASSERT_NE(std::string::npos, newline);
    EXPECT_EQ(std::size(contents) - 1U, contents.find('\n', newline + 1U)); // two lines

    auto const var = tr_variant_serde::json().parse(std::string_view{ contents }.substr(0, newline));
    ASSERT_TRUE(var);
    auto const* const map = var->get_if<tr_variant::Map>();
    ASSERT_NE(nullptr, map);
    auto const str = [map](std::string_view key) {
        return map->value_if<std::string_view>(tr_quark_new(key)).value_or(""sv);
    };
    auto const num = [map](std::string_view key) {
        return map->value_if<int64_t>(tr_quark_new(key)).value_or(-1);
    };
    EXPECT_EQ("2023-11-14T22:13:20Z"sv, str("time"sv));
    EXPECT_EQ("close"sv, str("event"sv));
    EXPECT_EQ("bad-have"sv, str("reason"sv));
    EXPECT_EQ("got Have: 9 but torrent only has 4 pieces"sv, str("detail"sv));
    EXPECT_EQ("192.0.2.7"sv, str("address"sv));
    EXPECT_EQ(6881, num("port"sv));
    EXPECT_EQ("utp"sv, str("transport"sv));
    EXPECT_EQ("Test 1.0"sv, str("client"sv));
    EXPECT_EQ("2d5454303030312d000000000000000000000000"sv, str("peer_id"sv));
    EXPECT_EQ(30, num("duration_s"sv));
    EXPECT_EQ(10, num("piece_bytes_up"sv));
    EXPECT_EQ(20, num("piece_bytes_down"sv));
    EXPECT_EQ("captures/x.pcapng"sv, str("capture"sv));
}

TEST_F(PeerLogTest, writesNothingWhenDisabled)
{
    auto log = PeerLog{};
    log.configure(false, sandboxDir(), PeerCaptureMode::Off);
    log.log_close(
        makeConn(),
        PeerCloseEvent{ .reason = PeerCloseReason::None,
                        .detail = {},
                        .connected_at = 0,
                        .closed_at = Now,
                        .piece_bytes_up = 0U,
                        .piece_bytes_down = 0U,
                        .capture_file = {} });

    auto const recent = std::vector<pcapng::Segment>{
        pcapng::Segment{ .from_peer = true, .bytes = {}, .original_size = 0U, .time_usec = 0U, .comment = {} }
    };
    EXPECT_EQ(""sv, log.write_capture(makeConn(), recent, "bad-have"sv, {}, Now));
    EXPECT_FALSE(tr_sys_path_exists(tr_pathbuf{ sandboxDir(), "/events-2023-11-14.jsonl"sv }));
}

TEST_F(PeerLogTest, writesOneCapturePerAddressPerInterval)
{
    auto log = PeerLog{};
    log.configure(false, sandboxDir(), PeerCaptureMode::Rejected);

    auto const have = message(4U, 4U);
    auto const recent = std::vector<pcapng::Segment>{
        pcapng::Segment{ .from_peer = true,
                         .bytes = have,
                         .original_size = static_cast<uint32_t>(std::size(have)),
                         .time_usec = 0U,
                         .comment = {} },
    };

    auto const name = log.write_capture(makeConn(), recent, "bad-have"sv, "detail"sv, Now);
    EXPECT_EQ("captures/20231114T221320Z-192.0.2.7.6881-bad-have.pcapng"sv, name);
    auto const contents = readFile(tr_pathbuf{ sandboxDir(), '/', name });
    EXPECT_TRUE(contents.starts_with("\x0A\x0D\x0D\x0A"sv));
    EXPECT_NE(std::string::npos, contents.find("bad-have: detail"sv)); // the bad message's packet comment

    EXPECT_EQ(""sv, log.write_capture(makeConn(), recent, "bad-have"sv, {}, Now + PeerLog::CaptureIntervalSecs - 1));
    EXPECT_NE(""sv, log.write_capture(makeConn(), recent, "bad-have"sv, {}, Now + PeerLog::CaptureIntervalSecs));
}

} // namespace
} // namespace tr::test
