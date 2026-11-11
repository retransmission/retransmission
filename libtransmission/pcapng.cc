// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "libtransmission/pcapng.h"

namespace tr::pcapng
{
namespace
{

// https://www.ietf.org/archive/id/draft-ietf-opsawg-pcapng-03.html
auto constexpr BlockSectionHeader = uint32_t{ 0x0A0D0D0AU };
auto constexpr BlockInterfaceDescription = uint32_t{ 0x00000001U };
auto constexpr BlockEnhancedPacket = uint32_t{ 0x00000006U };
auto constexpr ByteOrderMagic = uint32_t{ 0x1A2B3C4DU };
auto constexpr OptEndOfOpt = uint16_t{ 0U };
auto constexpr OptComment = uint16_t{ 1U };
auto constexpr OptShbUserAppl = uint16_t{ 4U };

auto constexpr IpProtoTcp = uint8_t{ 6U };
auto constexpr IpProtoUdp = uint8_t{ 17U };
auto constexpr Ipv4HeaderSize = uint32_t{ 20U };
auto constexpr Ipv6HeaderSize = uint32_t{ 40U };
auto constexpr TcpHeaderSize = uint32_t{ 20U };

auto constexpr TcpFin = uint8_t{ 0x01U };
auto constexpr TcpSyn = uint8_t{ 0x02U };
auto constexpr TcpPsh = uint8_t{ 0x08U };
auto constexpr TcpAck = uint8_t{ 0x10U };

// BEP 29 packet types
auto constexpr UtpData = uint8_t{ 0U };
auto constexpr UtpFin = uint8_t{ 1U };
auto constexpr UtpState = uint8_t{ 2U };
auto constexpr UtpSyn = uint8_t{ 4U };

// Little-endian writer for the pcapng blocks. The byte-order magic
// tells readers which order the file uses.
class BlockWriter
{
public:
    void u16(uint16_t v)
    {
        put(v, 2U);
    }

    void u32(uint32_t v)
    {
        put(v, 4U);
    }

    void bytes(std::span<std::byte const> b)
    {
        out_.insert(std::end(out_), std::begin(b), std::end(b));
    }

    void pad4()
    {
        while (std::size(out_) % 4U != 0U) {
            out_.push_back(std::byte{ 0 });
        }
    }

    void option(uint16_t code, std::string_view value)
    {
        if (std::empty(value)) {
            return;
        }

        u16(code);
        u16(static_cast<uint16_t>(std::min(std::size(value), size_t{ 0xFFFFU })));
        bytes(std::as_bytes(std::span{ std::data(value), std::min(std::size(value), size_t{ 0xFFFFU }) }));
        pad4();
    }

    // Starts a block; returns the offset of its length field.
    [[nodiscard]] size_t begin_block(uint32_t type)
    {
        u32(type);
        auto const len_offset = std::size(out_);
        u32(0U); // patched by end_block()
        return len_offset;
    }

    void end_block(size_t len_offset)
    {
        auto const block_len = static_cast<uint32_t>(std::size(out_) - (len_offset - 4U) + 4U);
        u32(block_len);
        for (size_t i = 0; i < 4U; ++i) {
            out_[len_offset + i] = static_cast<std::byte>((block_len >> (8U * i)) & 0xFFU);
        }
    }

    [[nodiscard]] std::vector<std::byte> take() noexcept
    {
        return std::move(out_);
    }

private:
    void put(uint64_t v, size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            out_.push_back(static_cast<std::byte>((v >> (8U * i)) & 0xFFU));
        }
    }

    std::vector<std::byte> out_;
};

// Big-endian writer for the packet headers.
class PacketWriter
{
public:
    void u8(uint8_t v)
    {
        out_.push_back(std::byte{ v });
    }

    void u16(uint16_t v)
    {
        u8(static_cast<uint8_t>(v >> 8U));
        u8(static_cast<uint8_t>(v));
    }

    void u32(uint32_t v)
    {
        u16(static_cast<uint16_t>(v >> 16U));
        u16(static_cast<uint16_t>(v));
    }

    void address(Endpoint const& e)
    {
        auto const n = e.is_ipv6 ? 16U : 4U;
        for (size_t i = 0; i < n; ++i) {
            u8(e.address[i]);
        }
    }

    void bytes(std::span<std::byte const> b)
    {
        out_.insert(std::end(out_), std::begin(b), std::end(b));
    }

    [[nodiscard]] auto size() const noexcept
    {
        return std::size(out_);
    }

    void patch_u16(size_t offset, uint16_t v)
    {
        out_[offset] = static_cast<std::byte>(v >> 8U);
        out_[offset + 1U] = static_cast<std::byte>(v & 0xFFU);
    }

    [[nodiscard]] std::vector<std::byte>& data() noexcept
    {
        return out_;
    }

private:
    std::vector<std::byte> out_;
};

[[nodiscard]] uint16_t ipv4_checksum(std::span<std::byte const> header)
{
    auto sum = uint32_t{};
    for (size_t i = 0; i + 1U < std::size(header); i += 2U) {
        sum += (static_cast<uint32_t>(header[i]) << 8U) | static_cast<uint32_t>(header[i + 1U]);
    }
    while ((sum >> 16U) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }
    return static_cast<uint16_t>(~sum & 0xFFFFU);
}

struct Packet {
    std::vector<std::byte> bytes;
    uint32_t original_size = 0;
    uint64_t time_usec = 0;
    std::string_view comment;
};

class StreamSynthesizer
{
public:
    explicit StreamSynthesizer(StreamInfo const& info)
        : info_{ info }
    {
    }

    [[nodiscard]] std::vector<Packet> run(std::span<Segment const> segments)
    {
        if (info_.transport == Transport::Tcp) {
            tcp(segments);
        } else {
            utp(segments);
        }
        return std::move(packets_);
    }

private:
    [[nodiscard]] Endpoint const& src(bool from_peer) const noexcept
    {
        return from_peer ? info_.peer : info_.local;
    }

    [[nodiscard]] Endpoint const& dst(bool from_peer) const noexcept
    {
        return from_peer ? info_.local : info_.peer;
    }

    [[nodiscard]] bool initiator_is_peer() const noexcept
    {
        return info_.peer_initiated;
    }

    // Wraps a transport header and payload in an IP header and stores the packet.
    // `payload_original` may exceed `payload.size()` when the payload was cut short;
    // the IP length fields describe the original packet.
    void add_packet(
        bool from_peer,
        uint8_t proto,
        PacketWriter& transport,
        size_t transport_len_offset,
        std::span<std::byte const> payload,
        uint32_t payload_original,
        uint64_t time_usec,
        std::string_view comment)
    {
        auto const& s = src(from_peer);
        auto const& d = dst(from_peer);
        auto const ip_header_size = s.is_ipv6 ? Ipv6HeaderSize : Ipv4HeaderSize;
        auto const transport_header_size = static_cast<uint32_t>(transport.size());
        auto const max_payload = MaxPacketSize - ip_header_size - transport_header_size;
        payload_original = std::min(payload_original, max_payload);
        payload = payload.first(std::min(std::size(payload), size_t{ payload_original }));

        auto const transport_original = transport_header_size + payload_original;
        if (proto == IpProtoUdp) {
            transport.patch_u16(transport_len_offset, static_cast<uint16_t>(transport_original));
        }

        auto packet = PacketWriter{};
        if (s.is_ipv6) {
            packet.u32(0x60000000U); // version 6, no traffic class or flow label
            packet.u16(static_cast<uint16_t>(transport_original));
            packet.u8(proto);
            packet.u8(64U); // hop limit
            packet.address(s);
            packet.address(d);
        } else {
            packet.u8(0x45U); // version 4, 5-word header
            packet.u8(0U);
            packet.u16(static_cast<uint16_t>(Ipv4HeaderSize + transport_original));
            packet.u16(static_cast<uint16_t>(std::size(packets_)));
            packet.u16(0x4000U); // don't fragment
            packet.u8(64U); // TTL
            packet.u8(proto);
            packet.u16(0U); // checksum, patched below
            packet.address(s);
            packet.address(d);
            packet.patch_u16(10U, ipv4_checksum(std::span{ packet.data() }.first(Ipv4HeaderSize)));
        }

        // TCP and UDP checksums stay zero; Wireshark does not validate them by default.
        packet.bytes(transport.data());
        packet.bytes(payload);

        packets_.push_back(
            Packet{ .bytes = std::move(packet.data()),
                    .original_size = ip_header_size + transport_original,
                    .time_usec = time_usec,
                    .comment = comment });
    }

    void tcp_packet(
        bool from_peer,
        uint8_t flags,
        std::span<std::byte const> payload,
        uint32_t payload_original,
        uint64_t time_usec,
        std::string_view comment)
    {
        auto& self = from_peer ? peer_seq_ : local_seq_;
        auto const& other = from_peer ? local_seq_ : peer_seq_;

        auto header = PacketWriter{};
        header.u16(src(from_peer).port);
        header.u16(dst(from_peer).port);
        header.u32(self);
        header.u32((flags & TcpAck) != 0U ? other : 0U);
        header.u8(static_cast<uint8_t>((TcpHeaderSize / 4U) << 4U));
        header.u8(flags);
        header.u16(0xFFFFU); // window
        header.u16(0U); // checksum
        header.u16(0U); // urgent pointer
        add_packet(from_peer, IpProtoTcp, header, 0U, payload, payload_original, time_usec, comment);

        self += payload_original;
        if ((flags & (TcpSyn | TcpFin)) != 0U) {
            self += 1U;
        }
    }

    void tcp(std::span<Segment const> segments)
    {
        auto const initiator = initiator_is_peer();
        tcp_packet(initiator, TcpSyn, {}, 0U, info_.start_usec, {});
        tcp_packet(!initiator, TcpSyn | TcpAck, {}, 0U, info_.start_usec, {});
        tcp_packet(initiator, TcpAck, {}, 0U, info_.start_usec, {});

        auto last_usec = info_.start_usec;
        for (auto const& seg : segments) {
            tcp_packet(seg.from_peer, TcpPsh | TcpAck, seg.bytes, seg.original_size, seg.time_usec, seg.comment);
            last_usec = std::max(last_usec, seg.time_usec);
        }

        tcp_packet(false, TcpFin | TcpAck, {}, 0U, last_usec, {});
    }

    void utp_packet(
        bool from_peer,
        uint8_t type,
        std::span<std::byte const> payload,
        uint32_t payload_original,
        uint64_t time_usec,
        std::string_view comment)
    {
        // BEP 29: the initiator's SYN carries its receive id; after that it sends
        // with its receive id + 1, and the other side sends with the SYN's id.
        auto const is_initiator = from_peer == initiator_is_peer();
        auto const conn_id = static_cast<uint16_t>(is_initiator && type != UtpSyn ? UtpConnId + 1U : UtpConnId);
        auto& self = from_peer ? peer_utp_seq_ : local_utp_seq_;
        auto const other = from_peer ? local_utp_seq_ : peer_utp_seq_;

        auto header = PacketWriter{};
        header.u16(src(from_peer).port);
        header.u16(dst(from_peer).port);
        auto const udp_len_offset = header.size();
        header.u16(0U); // UDP length, patched by add_packet()
        header.u16(0U); // UDP checksum
        header.u8(static_cast<uint8_t>((type << 4U) | 1U)); // version 1
        header.u8(0U); // no extension
        header.u16(conn_id);
        header.u32(static_cast<uint32_t>(time_usec));
        header.u32(0U); // timestamp difference
        header.u32(1024U * 1024U); // window
        header.u16(self);
        header.u16(static_cast<uint16_t>(other - 1U)); // ack of the other side's last packet
        add_packet(from_peer, IpProtoUdp, header, udp_len_offset, payload, payload_original, time_usec, comment);

        if (type != UtpState) {
            ++self;
        }
    }

    void utp(std::span<Segment const> segments)
    {
        auto const initiator = initiator_is_peer();
        utp_packet(initiator, UtpSyn, {}, 0U, info_.start_usec, {});
        utp_packet(!initiator, UtpState, {}, 0U, info_.start_usec, {});

        auto last_usec = info_.start_usec;
        for (auto const& seg : segments) {
            utp_packet(seg.from_peer, UtpData, seg.bytes, seg.original_size, seg.time_usec, seg.comment);
            last_usec = std::max(last_usec, seg.time_usec);
        }

        utp_packet(false, UtpFin, {}, 0U, last_usec, {});
    }

    static auto constexpr UtpConnId = uint16_t{ 0x5254U };

    StreamInfo const& info_;
    std::vector<Packet> packets_;

    uint32_t local_seq_ = 0x10000000U;
    uint32_t peer_seq_ = 0x20000000U;
    uint16_t local_utp_seq_ = 1U;
    uint16_t peer_utp_seq_ = 1U;
};

} // namespace

std::vector<std::byte> synthesize_stream(StreamInfo const& info, std::span<Segment const> segments)
{
    auto w = BlockWriter{};

    // Section Header Block
    auto len_offset = w.begin_block(BlockSectionHeader);
    w.u32(ByteOrderMagic);
    w.u16(1U); // major version
    w.u16(0U); // minor version
    w.u32(0xFFFFFFFFU); // section length: unspecified
    w.u32(0xFFFFFFFFU);
    w.option(OptComment, info.section_comment);
    w.option(OptShbUserAppl, "retransmission");
    w.u16(OptEndOfOpt);
    w.u16(0U);
    w.end_block(len_offset);

    // Interface Description Block; the default timestamp resolution is µs
    len_offset = w.begin_block(BlockInterfaceDescription);
    w.u16(LinktypeRaw);
    w.u16(0U); // reserved
    w.u32(0U); // snap length: unlimited
    w.end_block(len_offset);

    for (auto const& packet : StreamSynthesizer{ info }.run(segments)) {
        len_offset = w.begin_block(BlockEnhancedPacket);
        w.u32(0U); // interface id
        w.u32(static_cast<uint32_t>(packet.time_usec >> 32U));
        w.u32(static_cast<uint32_t>(packet.time_usec & 0xFFFFFFFFU));
        w.u32(static_cast<uint32_t>(std::size(packet.bytes)));
        w.u32(packet.original_size);
        w.bytes(packet.bytes);
        w.pad4();
        if (!std::empty(packet.comment)) {
            w.option(OptComment, packet.comment);
            w.u16(OptEndOfOpt);
            w.u16(0U);
        }
        w.end_block(len_offset);
    }

    return w.take();
}

} // namespace tr::pcapng
