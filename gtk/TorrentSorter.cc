// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentSorter.h"

#include "SorterBase.hh"

#include <libtransmission-app/display-modes.h>
#include <libtransmission-app/torrent-sort.h>

#include <compare>

TorrentSorter::TorrentSorter()
    : Glib::ObjectBase(typeid(TorrentSorter))
{
}

void TorrentSorter::set_mode(SortMode const mode)
{
    if (mode_ == mode) {
        return;
    }

    mode_ = mode;
    changed(Change::DIFFERENT);
}

void TorrentSorter::set_reversed(bool is_reversed)
{
    if (is_reversed_ == is_reversed) {
        return;
    }

    is_reversed_ = is_reversed;
    changed(Change::INVERTED);
}

int TorrentSorter::compare(Torrent const& lhs, Torrent const& rhs) const
{
    // tr::app orders the torrents that a view lists last first, so ask about rhs
    auto val = tr::app::compare_torrents(rhs.get_shared(), lhs.get_shared(), mode_);

    if (val == 0) {
        // then the names, as the user's locale collates them
        val = lhs.get_name_collate_key() <=> rhs.get_name_collate_key();
    }

    if (val == 0) {
        val = lhs.get_shared().hash() <=> rhs.get_shared().hash();
    }

    // SorterBase wants exactly -1, 0 or 1
    auto const ret = static_cast<int>(val > 0) - static_cast<int>(val < 0);
    return is_reversed_ ? -ret : ret;
}

void TorrentSorter::update(Torrent::Fields const& fields)
{
    if ((fields & (tr::app::sort_mode_fields(mode_) | Torrent::NameFields)).any()) {
        changed(Change::DIFFERENT);
    }
}

Glib::RefPtr<TorrentSorter> TorrentSorter::create()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new TorrentSorter());
}
