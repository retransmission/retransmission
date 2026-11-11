// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "SorterBase.h"
#include "Torrent.h"

#include <libtransmission-app/display-modes.h>

#include <glibmm/refptr.h>

using tr::app::SortMode;

class TorrentSorter : public SorterBase<Torrent>
{
public:
    void set_mode(SortMode mode);
    void set_reversed(bool is_reversed);

    // SorterBase<Torrent>
    int compare(Torrent const& lhs, Torrent const& rhs) const override;

    // re-sorts if the changed `fields` can move a torrent
    void update(Torrent::Fields const& fields);

    [[nodiscard]] static Glib::RefPtr<TorrentSorter> create();

private:
    TorrentSorter();

    SortMode mode_ = tr::app::DefaultSortMode;
    bool is_reversed_ = false;
};
