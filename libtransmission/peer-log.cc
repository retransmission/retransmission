// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/pcapng.h"
#include "libtransmission/peer-log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/variant.h"
#include "libtransmission/version.h"

using namespace std::literals;

namespace tr
{
namespace
{

// BitTorrent message id of a Piece message, the 5th byte after the length prefix
auto constexpr BtPiece = std::byte{ 7 };
auto constexpr BtPieceHeaderSize = size_t{ 4U + 1U + 4U + 4U };

// The directory and its files hold peer addresses, so only the owner may read them.
auto constexpr DirPermissions = 0700;

[[nodiscard]] std::string to_hex(std::span<std::byte const> bytes)
{
    auto ret = std::string{};
    ret.reserve(std::size(bytes) * 2U);
    for (auto const b : bytes) {
        fmt::format_to(std::back_inserter(ret), "{:02x}", static_cast<unsigned>(b));
    }
    return ret;
}

[[nodiscard]] std::string to_hex(tr_peer_id_t const& peer_id)
{
    return to_hex(std::as_bytes(std::span{ peer_id }));
}

[[nodiscard]] pcapng::Endpoint to_endpoint(tr_socket_address const& addr)
{
    auto ret = pcapng::Endpoint{};
    ret.is_ipv6 = addr.address().is_ipv6();
    auto bytes = std::array<std::byte, 16>{};
    addr.address().to_compact(std::begin(bytes));
    std::ranges::transform(bytes, std::begin(ret.address), [](std::byte b) { return static_cast<uint8_t>(b); });
    ret.port = addr.port().host();
    return ret;
}

// The BitTorrent handshake: <pstrlen><pstr><reserved><info_hash><peer_id>.
[[nodiscard]] std::vector<std::byte> make_handshake(
    std::array<uint8_t, 8> const& reserved,
    tr_sha1_digest_t const& info_hash,
    tr_peer_id_t const& peer_id)
{
    static auto constexpr Pstr = "BitTorrent protocol"sv;

    auto ret = std::vector<std::byte>{};
    ret.reserve(68U);
    ret.push_back(static_cast<std::byte>(std::size(Pstr)));
    for (auto const ch : Pstr) {
        ret.push_back(static_cast<std::byte>(ch));
    }
    for (auto const b : reserved) {
        ret.push_back(static_cast<std::byte>(b));
    }
    ret.insert(std::end(ret), std::begin(info_hash), std::end(info_hash));
    for (auto const ch : peer_id) {
        ret.push_back(static_cast<std::byte>(ch));
    }
    return ret;
}

// A file name that is safe on every platform: IPv6 colons become dashes.
[[nodiscard]] std::string file_name_part(std::string_view str)
{
    auto ret = std::string{ str };
    std::ranges::replace_if(ret, [](char ch) { return ch == ':' || ch == '[' || ch == ']' || ch == '/'; }, '-');
    return ret;
}

void add(tr_variant::Map& map, std::string_view key, tr_variant val)
{
    map.try_emplace(tr_quark_new(key), std::move(val));
}

} // namespace

std::string_view to_string(PeerCloseReason const reason) noexcept
{
    switch (reason) {
    case PeerCloseReason::None:
        return "closed-by-us"sv;
    case PeerCloseReason::SocketError:
        return "socket-error"sv;
    case PeerCloseReason::BadMessageLength:
        return "bad-message-length"sv;
    case PeerCloseReason::BadHave:
        return "bad-have"sv;
    case PeerCloseReason::BadBitfield:
        return "bad-bitfield"sv;
    case PeerCloseReason::FastExtensionNotNegotiated:
        return "fast-extension-not-negotiated"sv;
    case PeerCloseReason::Banned:
        return "banned"sv;
    case PeerCloseReason::Duplicate:
        return "duplicate"sv;
    case PeerCloseReason::Blocklisted:
        return "blocklisted"sv;
    }
    return "unknown"sv;
}

bool is_protocol_violation(PeerCloseReason const reason) noexcept
{
    switch (reason) {
    case PeerCloseReason::BadMessageLength:
    case PeerCloseReason::BadHave:
    case PeerCloseReason::BadBitfield:
    case PeerCloseReason::FastExtensionNotNegotiated:
        return true;
    default:
        return false;
    }
}

std::string_view to_string(PeerAnomaly const anomaly) noexcept
{
    switch (anomaly) {
    case PeerAnomaly::UnknownMessage:
        return "unknown-message"sv;
    case PeerAnomaly::MalformedHolepunch:
        return "malformed-holepunch"sv;
    case PeerAnomaly::UnalignedBlock:
        return "unaligned-block"sv;
    case PeerAnomaly::UnwantedBlock:
        return "unwanted-block"sv;
    }
    return "unknown"sv;
}

std::optional<PeerCaptureMode> peer_capture_mode_from_string(std::string_view const str) noexcept
{
    if (str == "off"sv || std::empty(str)) {
        return PeerCaptureMode::Off;
    }
    if (str == "rejected"sv) {
        return PeerCaptureMode::Rejected;
    }
    if (str == "anomalies"sv) {
        return PeerCaptureMode::Anomalies;
    }
    return std::nullopt;
}

// ---

void PeerMessageRing::add(
    bool const from_peer,
    std::span<std::byte const> const head,
    std::span<std::byte const> const tail,
    uint64_t const original_size,
    uint64_t const time_usec)
{
    // The message id follows the 4-byte length prefix, in `head` or at the start of `tail`.
    auto id = std::optional<std::byte>{};
    if (std::size(head) > 4U) {
        id = head[4];
    } else if (std::size(head) + std::size(tail) > 4U) {
        id = tail[4U - std::size(head)];
    }
    auto const is_piece = id == BtPiece;
    auto const keep = is_piece ? BtPieceHeaderSize + PieceDataKept : original_size;

    auto seg = pcapng::Segment{};
    seg.from_peer = from_peer;
    seg.original_size = static_cast<uint32_t>(std::min(original_size, uint64_t{ UINT32_MAX }));
    seg.time_usec = time_usec;
    for (auto const part : { head, tail }) {
        auto const n = std::min(
            std::size(part),
            static_cast<size_t>(keep) - std::min(std::size(seg.bytes), static_cast<size_t>(keep)));
        seg.bytes.insert(std::end(seg.bytes), std::begin(part), std::begin(part) + static_cast<std::ptrdiff_t>(n));
    }

    bytes_ += std::size(seg.bytes);
    messages_.push_back(std::move(seg));

    while (std::size(messages_) > MaxMessages || (bytes_ > MaxBytes && std::size(messages_) > 1U)) {
        bytes_ -= std::size(messages_.front().bytes);
        messages_.pop_front();
    }
}

// ---

void PeerLog::configure(bool const log_enabled, std::string dir, PeerCaptureMode const capture_mode)
{
    log_enabled_ = log_enabled;
    dir_ = std::move(dir);
    capture_mode_ = capture_mode;
}

void PeerLog::append_line(time_t const now, std::string_view const line) const
{
    auto error = tr_error{};
    if (!tr_sys_dir_create(dir_, TR_SYS_DIR_CREATE_PARENTS, DirPermissions, &error)) {
        tr_logAddWarn(fmt::format("Couldn't create peer log directory '{}': {}", dir_, error.message()));
        return;
    }

    auto const filename = tr_pathbuf{ dir_, "/events-"sv, fmt::format("{:%F}", fmt::gmtime(now)), ".jsonl"sv };
    auto out = std::ofstream{ filename.c_str(), std::ios::binary | std::ios::app };
    out << line << '\n';
    if (!out) {
        tr_logAddWarn(fmt::format("Couldn't write peer log '{}'", filename.sv()));
    }
}

void PeerLog::log_close(PeerConnectionInfo const& conn, PeerCloseEvent const& event)
{
    if (!log_enabled_ || std::empty(dir_)) {
        return;
    }

    auto map = tr_variant::Map{ 18U };
    add(map, "time"sv, fmt::format("{:%FT%TZ}", fmt::gmtime(event.closed_at)));
    add(map, "event"sv, "close"sv);
    add(map, "reason"sv, to_string(event.reason));
    if (!std::empty(event.detail)) {
        add(map, "detail"sv, event.detail);
    }
    add(map, "info_hash"sv, conn.info_hash_string);
    add(map, "torrent"sv, conn.torrent_name);
    add(map, "address"sv, conn.peer.address().display_name());
    add(map, "port"sv, conn.peer.port().host());
    add(map, "transport"sv, conn.is_utp ? "utp"sv : "tcp"sv);
    add(map, "encrypted"sv, conn.is_encrypted);
    add(map, "incoming"sv, conn.is_incoming);
    add(map, "client"sv, conn.client);
    add(map, "peer_id"sv, to_hex(conn.peer_id));
    add(map, "duration_s"sv, event.connected_at > 0 ? std::max(time_t{}, event.closed_at - event.connected_at) : 0);
    add(map, "piece_bytes_up"sv, event.piece_bytes_up);
    add(map, "piece_bytes_down"sv, event.piece_bytes_down);
    if (!std::empty(event.capture_file)) {
        add(map, "capture"sv, event.capture_file);
    }

    append_line(event.closed_at, tr_variant_serde::json().compact().to_string(tr_variant{ std::move(map) }));
}

std::string PeerLog::write_capture(
    PeerConnectionInfo const& conn,
    std::span<pcapng::Segment const> const recent,
    std::string_view const reason,
    std::string_view const detail,
    time_t const now)
{
    if (capture_mode_ == PeerCaptureMode::Off || std::empty(dir_) || std::empty(recent)) {
        return {};
    }

    // Keep a misbehaving swarm from filling the disk.
    auto const addr = conn.peer.address().display_name();
    if (auto const it = last_capture_at_.find(addr);
        it != std::end(last_capture_at_) && now - it->second < CaptureIntervalSecs) {
        return {};
    }
    if (std::size(last_capture_at_) > 4096U) {
        std::erase_if(last_capture_at_, [now](auto const& item) { return now - item.second >= CaptureIntervalSecs; });
    }
    last_capture_at_.insert_or_assign(addr, now);

    auto const captures_dir = tr_pathbuf{ dir_, "/captures"sv };
    auto error = tr_error{};
    if (!tr_sys_dir_create(captures_dir, TR_SYS_DIR_CREATE_PARENTS, DirPermissions, &error)) {
        tr_logAddWarn(fmt::format("Couldn't create peer capture directory '{}': {}", captures_dir.sv(), error.message()));
        return {};
    }

    auto info = pcapng::StreamInfo{};
    info.peer = to_endpoint(conn.peer);
    info.local.is_ipv6 = info.peer.is_ipv6; // the local address is not recorded
    info.local.port = conn.local_port;
    info.transport = conn.is_utp ? pcapng::Transport::Utp : pcapng::Transport::Tcp;
    info.peer_initiated = conn.is_incoming;
    info.start_usec = recent.front().time_usec;
    info.section_comment = fmt::format(
        "{} {}\n"
        "torrent: {}\n"
        "info hash: {}\n"
        "peer: {} ({}, {}, {})\n"
        "peer id: {}\n"
        "reason: {}{}{}\n"
        "Packets are synthesized from the BitTorrent stream {}: handshakes, setup and close packets, "
        "sequence numbers and the local address are made up. Piece messages keep only {} bytes of block data.{}",
        LONG_VERSION_STRING,
        "peer capture",
        conn.torrent_name,
        conn.info_hash_string,
        conn.peer.display_name(),
        std::empty(conn.client) ? "unknown client"sv : std::string_view{ conn.client },
        conn.is_utp ? "µTP"sv : "TCP"sv,
        conn.is_encrypted ? "encrypted; shown decrypted"sv : "plaintext"sv,
        to_hex(conn.peer_id),
        reason,
        std::empty(detail) ? ""sv : ": "sv,
        detail,
        conn.is_encrypted ? "after decryption" : "as received",
        PeerMessageRing::PieceDataKept,
        conn.is_utp ? "\nWireshark decodes µTP only with the bt_utp_udp heuristic enabled "
                      "(Analyze > Enabled Protocols, or tshark --enable-heuristic bt_utp_udp)."sv :
                      ""sv);

    // Both handshakes, then the recorded messages; the last received message is the bad one.
    auto segments = std::vector<pcapng::Segment>{};
    segments.reserve(std::size(recent) + 2U);
    auto const handshake = [&](bool from_peer) {
        auto seg = pcapng::Segment{};
        seg.from_peer = from_peer;
        seg.bytes = from_peer ? make_handshake(conn.peer_reserved, conn.info_hash, conn.peer_id) :
                                make_handshake(conn.client_reserved, conn.info_hash, conn.client_peer_id);
        seg.original_size = static_cast<uint32_t>(std::size(seg.bytes));
        seg.time_usec = info.start_usec;
        segments.push_back(std::move(seg));
    };
    handshake(conn.is_incoming);
    handshake(!conn.is_incoming);
    segments.insert(std::end(segments), std::begin(recent), std::end(recent));
    if (auto const it = std::ranges::find_if(segments | std::views::reverse, &pcapng::Segment::from_peer);
        it != std::rend(segments)) {
        it->comment = fmt::format("{}{}{}", reason, std::empty(detail) ? ""sv : ": "sv, detail);
    }

    auto const name = fmt::format(
        "{:%Y%m%dT%H%M%SZ}-{}.{}-{}.pcapng", // address.port, as tcpdump prints it
        fmt::gmtime(now),
        file_name_part(addr),
        conn.peer.port().host(),
        file_name_part(reason));
    auto const path = tr_pathbuf{ captures_dir, '/', name };
    auto const file = pcapng::synthesize_stream(info, segments);
    if (!tr_file_save(path, std::string_view{ reinterpret_cast<char const*>(std::data(file)), std::size(file) }, &error)) {
        tr_logAddWarn(fmt::format("Couldn't save peer capture '{}': {}", path.sv(), error.message()));
        return {};
    }

    return fmt::format("captures/{}", name);
}

} // namespace tr
