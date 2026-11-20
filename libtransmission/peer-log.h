// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t, std::byte
#include <cstdint>
#include <ctime> // time_t
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "libtransmission/digest.h" // tr_sha1_digest_t
#include "libtransmission/net.h" // tr_socket_address
#include "libtransmission/pcapng.h"
#include "libtransmission/types.h" // tr_peer_id_t

namespace tr
{

// Why a peer connection was closed.
enum class PeerCloseReason : uint8_t {
    None, // we closed it: pruned, torrent stopped or session closing
    SocketError, // a read or write failed, or the peer hung up
    BadMessageLength,
    BadHave,
    BadBitfield,
    FastExtensionNotNegotiated,
    Banned,
    Duplicate,
    Blocklisted,
};

[[nodiscard]] std::string_view to_string(PeerCloseReason reason) noexcept;

// True when the peer broke the protocol, as opposed to a network error or our choice.
[[nodiscard]] bool is_protocol_violation(PeerCloseReason reason) noexcept;

// A message we tolerate but that a well-behaved peer would not send.
enum class PeerAnomaly : uint8_t {
    UnknownMessage,
    MalformedHolepunch,
    UnalignedBlock,
    UnwantedBlock,
};

inline constexpr size_t PeerAnomalyCount = 4U;

[[nodiscard]] std::string_view to_string(PeerAnomaly anomaly) noexcept;

enum class PeerCaptureMode : uint8_t {
    Off,
    Rejected, // capture messages that close the connection
    Anomalies, // also capture messages we tolerate
};

[[nodiscard]] std::optional<PeerCaptureMode> peer_capture_mode_from_string(std::string_view str) noexcept;

// The last few BitTorrent messages sent and received on one connection,
// kept so that a capture can show what led up to a bad message.
class PeerMessageRing
{
public:
    static constexpr size_t MaxMessages = 32U;
    static constexpr size_t MaxBytes = 16U * 1024U;

    // Piece messages keep their header and this much block data.
    static constexpr size_t PieceDataKept = 16U;

    // `head` and `tail` together are one message as sent on the wire, starting
    // with its length prefix; `tail` may be empty. `original_size` is the size
    // the message has or declares, which may exceed the bytes passed in.
    void add(
        bool from_peer,
        std::span<std::byte const> head,
        std::span<std::byte const> tail,
        uint64_t original_size,
        uint64_t time_usec);

    [[nodiscard]] auto const& messages() const noexcept
    {
        return messages_;
    }

private:
    std::deque<pcapng::Segment> messages_;
    size_t bytes_ = 0U;
};

// What the event log and a capture say about one peer connection.
struct PeerConnectionInfo {
    tr_sha1_digest_t info_hash = {};
    std::string info_hash_string;
    std::string torrent_name;
    tr_socket_address peer;
    uint16_t local_port = 0U;
    bool is_utp = false;
    bool is_encrypted = false;
    bool is_incoming = false;
    std::string client;
    tr_peer_id_t client_peer_id = {}; // ours
    tr_peer_id_t peer_id = {}; // theirs
    std::array<uint8_t, 8> client_reserved = {}; // the reserved bytes of our handshake
    std::array<uint8_t, 8> peer_reserved = {}; // the reserved bytes of theirs
};

struct PeerCloseEvent {
    PeerCloseReason reason = PeerCloseReason::None;
    std::string_view detail;
    time_t connected_at = 0;
    time_t closed_at = 0;
    uint64_t piece_bytes_up = 0U;
    uint64_t piece_bytes_down = 0U;
    std::string_view capture_file;
};

// Writes the peer event log and bad-message captures to a directory:
//
//   <dir>/events-YYYY-MM-DD.jsonl   one JSON object per line, per UTC day
//   <dir>/captures/*.pcapng         one file per captured message
class PeerLog
{
public:
    // At most one capture per peer address in this many seconds.
    static constexpr time_t CaptureIntervalSecs = 3600;

    void configure(bool log_enabled, std::string dir, PeerCaptureMode capture_mode);

    [[nodiscard]] constexpr bool log_enabled() const noexcept
    {
        return log_enabled_;
    }

    [[nodiscard]] constexpr PeerCaptureMode capture_mode() const noexcept
    {
        return capture_mode_;
    }

    [[nodiscard]] constexpr auto const& dir() const noexcept
    {
        return dir_;
    }

    void log_close(PeerConnectionInfo const& conn, PeerCloseEvent const& event);

    // Writes a capture of a connection whose last received message is bad.
    // Returns the capture's file name, relative to dir(), or an empty string
    // if none was written.
    [[nodiscard]] std::string write_capture(
        PeerConnectionInfo const& conn,
        std::span<pcapng::Segment const> recent,
        std::string_view reason,
        std::string_view detail,
        time_t now);

private:
    void append_line(time_t now, std::string_view line) const;

    std::string dir_;
    bool log_enabled_ = false;
    PeerCaptureMode capture_mode_ = PeerCaptureMode::Off;
    std::unordered_map<std::string, time_t> last_capture_at_;
};

} // namespace tr
