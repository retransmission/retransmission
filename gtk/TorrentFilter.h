// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"
#include "Torrent.h"

#include <libtransmission-app/torrent-filter.h>

#include <glibmm/refptr.h>

#include <sigslot/signal.hpp>

namespace tr::app
{
class Prefs;
} // namespace tr::app

// The main window's filter: `tr::app::TorrentFilter`, which reads the
// show mode, the tracker and the search text from the prefs.
class TorrentFilter : public FilterBase<Torrent>
{
public:
    // FilterBase<Torrent>
    bool match(Torrent const& torrent) const override;
    bool matches_all() const override;

    // refilters if the changed `fields` can change a match
    void update(Torrent::Fields const& fields);

    [[nodiscard]] static Glib::RefPtr<TorrentFilter> create(tr::app::Prefs const& prefs);

private:
    explicit TorrentFilter(tr::app::Prefs const& prefs);

    tr::app::TorrentFilter filter_;
    sigslot::scoped_connection changed_connection_;
};
