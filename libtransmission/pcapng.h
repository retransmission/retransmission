// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // std::byte
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Builds pcapng files that Wireshark opens as a TCP or µTP connection
// carrying a recorded byte stream.
//
// The packets are synthesized from the application-level stream: their
// addresses, ports and payload are real, but segmentation, timing of the
// setup packets, sequence numbers and connection ids are made up, and
// retransmits or acks of the real connection are not in the file.
namespace tr::pcapng
{

struct Endpoint {
    // network byte order; an IPv4 address uses the first 4 bytes
    std::array<uint8_t, 16> address = {};
    bool is_ipv6 = false;
    uint16_t port = 0; // host byte order
};

enum class Transport : uint8_t {
    Tcp,
    Utp, // µTP (BEP 29) over UDP
};

struct StreamInfo {
    Endpoint local;
    Endpoint peer;
    Transport transport = Transport::Tcp;
    bool peer_initiated = false;
    uint64_t start_usec = 0; // timestamp of the connection setup packets, in µs since the epoch
    std::string section_comment;
};

// One write by one side of the stream.
struct Segment {
    bool from_peer = false;
    std::vector<std::byte> bytes; // the bytes kept; a prefix of what was sent
    uint32_t original_size = 0; // the bytes sent; at least `bytes.size()`
    uint64_t time_usec = 0; // µs since the epoch
    std::string comment; // shown by Wireshark as the packet comment
};

// The link type of the packets: raw IPv4 or IPv6, with no link-layer header.
inline constexpr uint16_t LinktypeRaw = 101U;

// The size limit of one synthesized IP packet.
inline constexpr uint32_t MaxPacketSize = 65535U;

// Returns a pcapng file holding one connection: its setup, one packet per
// segment, and a close sent by the local side.
[[nodiscard]] std::vector<std::byte> synthesize_stream(StreamInfo const& info, std::span<Segment const> segments);

} // namespace tr::pcapng
