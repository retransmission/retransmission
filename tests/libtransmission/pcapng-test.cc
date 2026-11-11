// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/pcapng.h>

using namespace std::literals;

namespace tr::test
{
namespace
{

using namespace tr::pcapng;

[[nodiscard]] uint32_t le32(std::span<std::byte const> b, size_t at)
{
    return static_cast<uint32_t>(b[at]) |
        (static_cast<uint32_t>(b[at + 1U]) << 8U) |
        (static_cast<uint32_t>(b[at + 2U]) << 16U) |
        (static_cast<uint32_t>(b[at + 3U]) << 24U);
}

[[nodiscard]] uint16_t le16(std::span<std::byte const> b, size_t at)
{
    return static_cast<uint16_t>(static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1U]) << 8U));
}

[[nodiscard]] uint16_t be16(std::span<std::byte const> b, size_t at)
{
    return static_cast<uint16_t>((static_cast<uint32_t>(b[at]) << 8U) | static_cast<uint32_t>(b[at + 1U]));
}

[[nodiscard]] uint32_t be32(std::span<std::byte const> b, size_t at)
{
    return (static_cast<uint32_t>(be16(b, at)) << 16U) | be16(b, at + 2U);
}

struct Block {
    uint32_t type = 0;
    std::span<std::byte const> body; // between the length fields
};

struct EnhancedPacket {
    std::span<std::byte const> data;
    uint32_t original_size = 0;
    std::string comment;
};

[[nodiscard]] std::vector<Block> parseBlocks(std::span<std::byte const> file)
{
    auto ret = std::vector<Block>{};
    auto pos = size_t{};
    while (pos < std::size(file)) {
        auto const type = le32(file, pos);
        auto const len = le32(file, pos + 4U);
        EXPECT_EQ(0U, len % 4U);
        EXPECT_LE(pos + len, std::size(file));
        EXPECT_EQ(len, le32(file, pos + len - 4U)); // trailing length repeats the leading one
        ret.push_back(Block{ .type = type, .body = file.subspan(pos + 8U, len - 12U) });
        pos += len;
    }
    EXPECT_EQ(std::size(file), pos);
    return ret;
}

[[nodiscard]] std::string findComment(std::span<std::byte const> options)
{
    auto pos = size_t{};
    while (pos + 4U <= std::size(options)) {
        auto const code = le16(options, pos);
        auto const len = le16(options, pos + 2U);
        if (code == 0U) {
            break;
        }
        if (code == 1U) {
            return std::string{ reinterpret_cast<char const*>(options.data() + pos + 4U), len };
        }
        pos += 4U + ((len + 3U) & ~3U);
    }
    return {};
}

[[nodiscard]] std::vector<EnhancedPacket> parsePackets(std::span<std::byte const> file)
{
    auto const blocks = parseBlocks(file);
    EXPECT_GE(std::size(blocks), 2U);
    EXPECT_EQ(0x0A0D0D0AU, blocks[0].type);
    EXPECT_EQ(0x1A2B3C4DU, le32(blocks[0].body, 0U));
    EXPECT_EQ(1U, blocks[1].type);
    EXPECT_EQ(LinktypeRaw, le16(blocks[1].body, 0U));

    auto ret = std::vector<EnhancedPacket>{};
    for (size_t i = 2; i < std::size(blocks); ++i) {
        auto const& body = blocks[i].body;
        EXPECT_EQ(6U, blocks[i].type);
        auto const captured = le32(body, 12U);
        auto pkt = EnhancedPacket{};
        pkt.data = body.subspan(20U, captured);
        pkt.original_size = le32(body, 16U);
        pkt.comment = findComment(body.subspan(20U + ((captured + 3U) & ~3U)));
        ret.push_back(pkt);
    }
    return ret;
}

[[nodiscard]] std::vector<std::byte> toBytes(std::string_view sv)
{
    auto const b = std::as_bytes(std::span{ sv });
    return { std::begin(b), std::end(b) };
}

[[nodiscard]] Endpoint ipv4(uint8_t last, uint16_t port)
{
    auto ret = Endpoint{};
    ret.address = { 192U, 0U, 2U, last };
    ret.port = port;
    return ret;
}

[[nodiscard]] Endpoint ipv6(uint8_t last, uint16_t port)
{
    auto ret = Endpoint{};
    ret.address = { 0x20U, 0x01U, 0x0dU, 0xb8U };
    ret.address[15] = last;
    ret.is_ipv6 = true;
    ret.port = port;
    return ret;
}

TEST(Pcapng, tcpStreamOverIpv4)
{
    auto info = StreamInfo{};
    info.local = ipv4(1U, 51413U);
    info.peer = ipv4(2U, 6881U);
    info.peer_initiated = true;
    info.start_usec = 1'700'000'000'000'000U;
    info.section_comment = "section";

    auto segments = std::vector<Segment>{};
    segments.push_back(
        { .from_peer = true, .bytes = toBytes("hello"sv), .original_size = 5U, .time_usec = info.start_usec, .comment = {} });
    segments.push_back(
        { .from_peer = false,
          .bytes = toBytes("abc"sv),
          .original_size = 100U,
          .time_usec = info.start_usec + 1U,
          .comment = "cut" });

    auto const file = synthesize_stream(info, segments);
    auto const blocks = parseBlocks(file);
    EXPECT_EQ("section", findComment(blocks[0].body.subspan(16U)));

    // SYN, SYN/ACK, ACK, 2 data, FIN
    auto const packets = parsePackets(file);
    ASSERT_EQ(6U, std::size(packets));

    // the peer opened the connection
    auto const& syn = packets[0].data;
    EXPECT_EQ(0x45U, static_cast<uint8_t>(syn[0]));
    EXPECT_EQ(6U, static_cast<uint8_t>(syn[9])); // TCP
    EXPECT_EQ(2U, static_cast<uint8_t>(syn[15])); // source is the peer
    EXPECT_EQ(0x02U, static_cast<uint8_t>(syn[20 + 13])); // SYN

    // the first data packet follows the handshake's sequence numbers
    auto const& data = packets[3].data;
    EXPECT_EQ(20U + 20U + 5U, std::size(data));
    EXPECT_EQ(be32(syn, 24U) + 1U, be32(data, 24U));
    EXPECT_EQ("hello"sv, (std::string_view{ reinterpret_cast<char const*>(data.data() + 40U), 5U }));

    // a cut-short segment keeps its original size in the IP header and the packet block
    auto const& cut = packets[4];
    EXPECT_EQ(20U + 20U + 3U, std::size(cut.data));
    EXPECT_EQ(20U + 20U + 100U, cut.original_size);
    EXPECT_EQ(20U + 20U + 100U, be16(cut.data, 2U));
    EXPECT_EQ("cut", cut.comment);

    // the local side closes; the FIN's sequence number counts the original 100 bytes
    auto const& fin = packets[5].data;
    EXPECT_EQ(1U, static_cast<uint8_t>(fin[15]));
    EXPECT_EQ(0x11U, static_cast<uint8_t>(fin[20 + 13])); // FIN|ACK
    EXPECT_EQ(be32(cut.data, 24U) + 100U, be32(fin, 24U));
}

TEST(Pcapng, utpStreamOverIpv6)
{
    auto info = StreamInfo{};
    info.local = ipv6(1U, 51413U);
    info.peer = ipv6(2U, 6881U);
    info.transport = Transport::Utp;
    info.peer_initiated = false;

    auto segments = std::vector<Segment>{};
    segments.push_back({ .from_peer = false, .bytes = toBytes("ours"sv), .original_size = 4U, .time_usec = 0U, .comment = {} });
    segments.push_back(
        { .from_peer = true, .bytes = toBytes("theirs"sv), .original_size = 6U, .time_usec = 0U, .comment = {} });

    // SYN, STATE, 2 data, FIN
    auto const packets = parsePackets(synthesize_stream(info, segments));
    ASSERT_EQ(5U, std::size(packets));

    auto constexpr Ip6 = size_t{ 40U };
    auto constexpr Udp = size_t{ 8U };
    auto const type = [&](size_t i) {
        return static_cast<uint8_t>(packets[i].data[Ip6 + Udp]);
    };
    auto const conn_id = [&](size_t i) {
        return be16(packets[i].data, Ip6 + Udp + 2U);
    };
    auto const seq_nr = [&](size_t i) {
        return be16(packets[i].data, Ip6 + Udp + 16U);
    };
    auto const ack_nr = [&](size_t i) {
        return be16(packets[i].data, Ip6 + Udp + 18U);
    };

    EXPECT_EQ(0x60U, static_cast<uint8_t>(packets[0].data[0]) & 0xF0U);
    EXPECT_EQ(17U, static_cast<uint8_t>(packets[0].data[6])); // UDP

    // type in the high nibble, version 1 in the low one
    EXPECT_EQ(0x41U, type(0)); // ST_SYN from us
    EXPECT_EQ(0x21U, type(1)); // ST_STATE from the peer
    EXPECT_EQ(0x01U, type(2)); // ST_DATA from us
    EXPECT_EQ(0x01U, type(3)); // ST_DATA from the peer
    EXPECT_EQ(0x11U, type(4)); // ST_FIN from us

    // BEP 29: the initiator sends with the SYN's id + 1; the other side with the SYN's id
    EXPECT_EQ(conn_id(0), conn_id(1));
    EXPECT_EQ(conn_id(0) + 1U, conn_id(2));
    EXPECT_EQ(conn_id(0), conn_id(3));

    EXPECT_EQ(seq_nr(0), ack_nr(1));
    EXPECT_EQ(seq_nr(0) + 1U, seq_nr(2));
    EXPECT_EQ(seq_nr(2), ack_nr(3));

    // UDP length covers the µTP header and payload
    EXPECT_EQ(Udp + 20U + 6U, be16(packets[3].data, Ip6 + 4U));
    EXPECT_EQ("theirs"sv, (std::string_view{ reinterpret_cast<char const*>(packets[3].data.data() + Ip6 + Udp + 20U), 6U }));
}

TEST(Pcapng, oversizedSegmentIsClampedToOnePacket)
{
    auto info = StreamInfo{};
    info.local = ipv4(1U, 1U);
    info.peer = ipv4(2U, 2U);

    auto segments = std::vector<Segment>{};
    segments.push_back(
        { .from_peer = true, .bytes = toBytes("x"sv), .original_size = 1'000'000U, .time_usec = 0U, .comment = {} });

    auto const packets = parsePackets(synthesize_stream(info, segments));
    ASSERT_EQ(5U, std::size(packets));
    EXPECT_EQ(MaxPacketSize, packets[3].original_size);
    EXPECT_EQ(MaxPacketSize, be16(packets[3].data, 2U));
}

} // namespace
} // namespace tr::test
