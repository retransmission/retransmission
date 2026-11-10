// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentFilter.h"

#include <compare>
#include <string>
#include <string_view>

#include <QtCore/QString>

#include <libtransmission/quark.h>

#include <libtransmission-app/torrent-filter.h>
#include <libtransmission-app/torrent-sort.h>
#include <libtransmission-app/torrent.h>

#include "Prefs.h"
#include "TorrentModel.h"
#include "Utils.h"

using tr::app::Torrent;

TorrentFilter::TorrentFilter(Prefs const& prefs)
    : prefs_{ prefs }
    , filter_{ prefs, [](std::string_view const text) { return Utils::qstringFromUtf8(text).toCaseFolded().toStdString(); } }
{
    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &TorrentFilter::onPrefChanged);
    connect(&refilter_timer_, &QTimer::timeout, this, &TorrentFilter::refilter);

    setDynamicSortFilter(true);

    refilter();
}

/***
****
***/

void TorrentFilter::onPrefChanged(tr_quark const key)
{
    if (key == TR_KEY_sort_mode || key == TR_KEY_sort_reversed) {
        refilterSoon();
    }
}

void TorrentFilter::refilterSoon()
{
    // debounces batched prefs changes
    if (!refilter_timer_.isActive()) {
        refilter_timer_.setSingleShot(true);
        refilter_timer_.start(50);
    }
}

void TorrentFilter::refilter()
{
    sort_mode_ = prefs_.get<SortMode>(TR_KEY_sort_mode);
    invalidate();
    sort(0, prefs_.get<bool>(TR_KEY_sort_reversed) ? Qt::AscendingOrder : Qt::DescendingOrder);
}

/***
****
***/

bool TorrentFilter::lessThan(QModelIndex const& left, QModelIndex const& right) const
{
    auto const* const model = sourceModel();
    auto const& a = *model->data(left, TorrentModel::TorrentRole).value<Torrent const*>();
    auto const& b = *model->data(right, TorrentModel::TorrentRole).value<Torrent const*>();

    auto val = tr::app::compare_torrents(a, b, sort_mode_);

    if (val == 0) {
        // the model caches the names as QStrings
        auto const a_name = model->data(left, Qt::DisplayRole).toString();
        auto const b_name = model->data(right, Qt::DisplayRole).toString();
        val = 0 <=> QString::compare(a_name, b_name, Qt::CaseInsensitive);
    }

    if (val == 0) {
        val = a.hash() <=> b.hash();
    }

    return val < 0;
}

/***
****
***/

bool TorrentFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    auto const index = sourceModel()->index(source_row, 0, source_parent);
    return filter_.matches(*index.data(TorrentModel::TorrentRole).value<Torrent const*>());
}
