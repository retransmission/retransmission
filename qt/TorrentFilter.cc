// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentFilter.h"

#include <array>
#include <compare>
#include <optional>
#include <string>

#include <QtCore/QString>

#include <libtransmission/quark.h>

#include <libtransmission-app/torrent-filter.h>
#include <libtransmission-app/torrent-sort.h>
#include <libtransmission-app/torrent.h>

#include "Prefs.h"
#include "TorrentModel.h"
#include "Utils.h"

using tr::app::should_show_torrent;
using tr::app::Torrent;

TorrentFilter::TorrentFilter(Prefs const& prefs)
    : prefs_{ prefs }
{
    connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), this, &TorrentFilter::onPrefChanged);
    connect(&refilter_timer_, &QTimer::timeout, this, &TorrentFilter::refilter);

    setDynamicSortFilter(true);

    refilter();
}

/***
****
***/

void TorrentFilter::onPrefChanged(tr_quark key)
{
    // For refiltering nearly immediately. Used to debounce batched prefs changes.
    static int const FastMSec = 50;

    // For waiting a little longer. Useful when user is typing the filter text.
    static int const SlowMSec = 500;

    std::optional<int> msec;
    switch (key) {
    case TR_KEY_filter_text:
        // special case for isEmpty: user probably hit the 'clear' button
        msec = prefs_.get<QString>(key).isEmpty() ? FastMSec : SlowMSec;
        break;

    case TR_KEY_filter_trackers:
    case TR_KEY_show_mode:
    case TR_KEY_sort_mode:
    case TR_KEY_sort_reversed:
        msec = FastMSec;
        break;

    default:
        break;
    }

    // if this pref change affects filtering, ensure that a refilter is queued
    if (msec && !refilter_timer_.isActive()) {
        refilter_timer_.setSingleShot(true);
        refilter_timer_.start(*msec);
    }
}

void TorrentFilter::refilter()
{
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

    auto val = tr::app::compare_torrents(a, b, prefs_.get<SortMode>(TR_KEY_sort_mode));

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
    auto const& tor = *index.data(TorrentModel::TorrentRole).value<Torrent const*>();

    if (!should_show_torrent(tor, prefs_.get<ShowMode>(TR_KEY_show_mode))) {
        return false;
    }

    if (auto const sitename = prefs_.get<std::string>(TR_KEY_filter_trackers);
        !std::empty(sitename) && !tor.includes_tracker(sitename)) {
        return false;
    }

    if (auto const text = prefs_.get<QString>(TR_KEY_filter_text); !text.isEmpty()) {
        return index.data(Qt::DisplayRole).toString().contains(text, Qt::CaseInsensitive) ||
            Utils::qstringFromUtf8(tor.hash_string()).contains(text, Qt::CaseInsensitive);
    }

    return true;
}

std::array<int, ShowModeCount> TorrentFilter::countTorrentsPerMode() const
{
    auto const* const torrent_model = dynamic_cast<TorrentModel const*>(sourceModel());
    if (torrent_model == nullptr) {
        return {};
    }

    auto torrent_counts = std::array<int, ShowModeCount>{};

    for (auto const& tor : torrent_model->list().torrents()) {
        for (unsigned int mode = 0; mode < ShowModeCount; ++mode) {
            if (should_show_torrent(tor, static_cast<ShowMode>(mode))) {
                ++torrent_counts[mode];
            }
        }
    }

    return torrent_counts;
}
