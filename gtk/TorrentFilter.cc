// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentFilter.h"

#include "FilterBase.hh"

#include <libtransmission-app/prefs.h>
#include <libtransmission-app/torrent-filter.h>

#include <glibmm/utility.h>

#include <string>
#include <string_view>

#include <glib.h>

namespace
{

[[nodiscard]] std::string case_fold(std::string_view const text)
{
    return std::string{
        Glib::make_unique_ptr_gfree(g_utf8_casefold(std::data(text), static_cast<gssize>(std::size(text)))).get()
    };
}

} // namespace

TorrentFilter::TorrentFilter(tr::app::Prefs const& prefs)
    : Glib::ObjectBase(typeid(TorrentFilter))
    , filter_{ prefs, &case_fold }
{
    changed_connection_ = filter_.changed.connect_scoped([this](tr::app::TorrentFilter::Change const change) {
        switch (change) {
        case tr::app::TorrentFilter::Change::LessStrict:
            changed(Change::LESS_STRICT);
            break;

        case tr::app::TorrentFilter::Change::MoreStrict:
            changed(Change::MORE_STRICT);
            break;

        default:
            changed(Change::DIFFERENT);
            break;
        }
    });
}

bool TorrentFilter::match(Torrent const& torrent) const
{
    return filter_.matches(torrent.get_shared());
}

bool TorrentFilter::matches_all() const
{
    return filter_.matches_all();
}

void TorrentFilter::update(Torrent::Fields const& fields)
{
    if ((fields & filter_.fields()).any()) {
        changed(Change::DIFFERENT);
    }
}

Glib::RefPtr<TorrentFilter> TorrentFilter::create(tr::app::Prefs const& prefs)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new TorrentFilter(prefs));
}
