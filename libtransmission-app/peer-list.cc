// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/peer-list.h"

#include <algorithm> // std::ranges::transform()
#include <compare>
#include <iterator> // std::back_inserter()
#include <string_view>
#include <utility> // std::move()

#include <libtransmission/types.h> // tr_torrent_id_t
#include <libtransmission/values.h>
#include <libtransmission/web-utils.h> // tr_addressSortKey()

#include "libtransmission-app/torrent.h"

namespace tr::app
{
namespace
{

// unlike `std::tolower()`, leaves UTF-8 bytes alone in every locale
[[nodiscard]] constexpr char to_ascii_lower(char const ch) noexcept
{
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

} // namespace

PeerAddressKey::PeerAddressKey(std::string_view const address)
{
    if (auto const ip = tr_addressSortKey(address)) {
        ip_ = *ip;
        return;
    }

    is_text_ = true;
    text_.reserve(std::size(address));
    std::ranges::transform(address, std::back_inserter(text_), to_ascii_lower);
}

// ---

PeerListItem::PeerListItem(tr_torrent_id_t const torrent_id, Peer peer, tr::Values::Percent const progress)
    : peer_{ std::move(peer) }
    , address_key_{ peer_.address }
    , progress_{ progress }
    , torrent_id_{ torrent_id }
{
}

PeerListItem::PeerListItem(Torrent const& tor, Peer const& peer)
    : PeerListItem{ tor.id(), peer, tor.peer_progress(peer) }
{
}

PeerKey PeerListItem::key() const
{
    return { .torrent_id = torrent_id_, .address = peer_.address, .port = peer_.port };
}

std::weak_ordering PeerListItem::compare(PeerListItem const& that, PeerColumn const column) const
{
    auto const& a = peer_;
    auto const& b = that.peer_;

    switch (column) {
    case PeerColumn::Encrypted:
        return static_cast<int>(b.is_encrypted) <=> static_cast<int>(a.is_encrypted); // encrypted first
    case PeerColumn::UploadSpeed:
        return a.rate_to_peer <=> b.rate_to_peer;
    case PeerColumn::UploadRequests:
        return a.active_reqs_to_client <=> b.active_reqs_to_client;
    case PeerColumn::DownloadSpeed:
        return a.rate_to_client <=> b.rate_to_client;
    case PeerColumn::DownloadRequests:
        return a.active_reqs_to_peer <=> b.active_reqs_to_peer;
    case PeerColumn::Progress:
        return progress_ <=> that.progress_;
    case PeerColumn::Flags:
        return a.flags <=> b.flags;
    case PeerColumn::Address:
        return address_key_ <=> that.address_key_;
    case PeerColumn::Client:
        return a.client_name <=> b.client_name;
    }

    return std::weak_ordering::equivalent;
}

} // namespace tr::app
