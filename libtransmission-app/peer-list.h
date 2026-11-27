// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <compare>
#include <cstdint> // uint16_t, uint8_t
#include <string>
#include <string_view>
#include <utility> // std::pair

#include <libtransmission/types.h> // tr_torrent_id_t
#include <libtransmission/values.h>
#include <libtransmission/web-utils.h> // tr_address_sort_key

#include "libtransmission-app/torrent.h"

namespace tr::app
{

// --- flags

// The states that a peer's `flag_str` names, one letter each
enum class PeerFlag : uint8_t {
    Utp, // T: connected over µTP
    OptimisticUnchoke, // O
    DownloadingFrom, // D
    WouldDownloadFrom, // d: we would download from the peer if it unchoked us
    UploadingTo, // U
    WouldUploadTo, // u: the peer would download from us if we unchoked it
    UnchokedUsUninterested, // K: the peer unchoked us, but we're not interested
    UnchokedPeerUninterested, // ?: we unchoked the peer, but it's not interested
    Encrypted, // E
    SupportsHolepunch, // h: BEP 55
    FromDht, // H
    FromPex, // X
    Incoming, // I
};

// Each flag and its letter, in the order the session writes them
inline constexpr auto PeerFlagLetters = std::to_array<std::pair<PeerFlag, char>>({
    { PeerFlag::Utp, 'T' },
    { PeerFlag::OptimisticUnchoke, 'O' },
    { PeerFlag::DownloadingFrom, 'D' },
    { PeerFlag::WouldDownloadFrom, 'd' },
    { PeerFlag::UploadingTo, 'U' },
    { PeerFlag::WouldUploadTo, 'u' },
    { PeerFlag::UnchokedUsUninterested, 'K' },
    { PeerFlag::UnchokedPeerUninterested, '?' },
    { PeerFlag::Encrypted, 'E' },
    { PeerFlag::SupportsHolepunch, 'h' },
    { PeerFlag::FromDht, 'H' },
    { PeerFlag::FromPex, 'X' },
    { PeerFlag::Incoming, 'I' },
});

// The flags that a peer's `flag_str` names; letters this version does not know are skipped
class PeerFlags
{
public:
    constexpr PeerFlags() noexcept = default;

    [[nodiscard]] static constexpr PeerFlags from_string(std::string_view const flag_str) noexcept
    {
        auto flags = PeerFlags{};
        for (auto const ch : flag_str) {
            for (auto const& [flag, letter] : PeerFlagLetters) {
                if (letter == ch) {
                    flags.insert(flag);
                }
            }
        }
        return flags;
    }

    constexpr void insert(PeerFlag const flag) noexcept
    {
        bits_ |= bit(flag);
    }

    [[nodiscard]] constexpr bool contains(PeerFlag const flag) const noexcept
    {
        return (bits_ & bit(flag)) != 0U;
    }

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return bits_ == 0U;
    }

    [[nodiscard]] constexpr bool operator==(PeerFlags const&) const noexcept = default;

private:
    [[nodiscard]] static constexpr uint16_t bit(PeerFlag const flag) noexcept
    {
        return static_cast<uint16_t>(1U << static_cast<unsigned>(flag));
    }

    uint16_t bits_ = 0;
};

// --- addresses

// Sorts peer addresses: IPv4, then IPv6, each numerically,
// then any other address as text, ignoring ASCII case.
class PeerAddressKey
{
public:
    PeerAddressKey() = default;
    explicit PeerAddressKey(std::string_view address);

    [[nodiscard]] std::strong_ordering operator<=>(PeerAddressKey const&) const = default;
    [[nodiscard]] bool operator==(PeerAddressKey const&) const = default;

private:
    // declared in sort order
    bool is_text_ = false; // false sorts IP addresses first
    tr_address_sort_key ip_ = {};
    std::string text_; // lowercase
};

// --- columns

// A peer list's columns, in display order
enum class PeerColumn : uint8_t {
    Encrypted,
    UploadSpeed,
    UploadRequests,
    DownloadSpeed,
    DownloadRequests,
    Progress,
    Flags,
    Address,
    Client,
};

inline constexpr auto PeerColumns = std::to_array<PeerColumn>({
    PeerColumn::Encrypted,
    PeerColumn::UploadSpeed,
    PeerColumn::UploadRequests,
    PeerColumn::DownloadSpeed,
    PeerColumn::DownloadRequests,
    PeerColumn::Progress,
    PeerColumn::Flags,
    PeerColumn::Address,
    PeerColumn::Client,
});

// Whether `column` is shown only when `show_extra_peer_details` is set
[[nodiscard]] constexpr bool is_extra_peer_column(PeerColumn const column) noexcept
{
    return column == PeerColumn::UploadRequests || column == PeerColumn::DownloadRequests;
}

// A peer list starts out sorted by this column, ascending
inline constexpr auto DefaultPeerSortColumn = PeerColumn::Address;

// --- items

// What identifies a peer list's row across refreshes.
// The port matters: peers behind one NAT share an address.
struct PeerKey {
    tr_torrent_id_t torrent_id = {};
    std::string address;
    uint16_t port = 0;

    [[nodiscard]] std::strong_ordering operator<=>(PeerKey const&) const = default;
    [[nodiscard]] bool operator==(PeerKey const&) const = default;
};

// One row of a peer list: a torrent's peer, with what the list sorts it by
class PeerListItem
{
public:
    PeerListItem(tr_torrent_id_t torrent_id, Peer peer, tr::Values::Percent progress);
    PeerListItem(Torrent const& tor, Peer const& peer);

    [[nodiscard]] constexpr auto torrent_id() const noexcept
    {
        return torrent_id_;
    }

    [[nodiscard]] constexpr auto const& peer() const noexcept
    {
        return peer_;
    }

    // see `Torrent::peer_progress()`
    [[nodiscard]] constexpr auto progress() const noexcept
    {
        return progress_;
    }

    [[nodiscard]] PeerKey key() const;

    // Ascending order by `column`, then by address, port, and torrent.
    // Client names compare ignoring ASCII case.
    [[nodiscard]] std::weak_ordering compare(PeerListItem const& that, PeerColumn column) const;

private:
    [[nodiscard]] std::weak_ordering compare_column(PeerListItem const& that, PeerColumn column) const;

    Peer peer_;
    PeerAddressKey address_key_;
    tr::Values::Percent progress_;
    tr_torrent_id_t torrent_id_ = {};
};

} // namespace tr::app
