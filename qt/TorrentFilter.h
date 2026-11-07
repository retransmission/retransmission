// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <sigslot/signal.hpp>

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QTimer>

#include <libtransmission-app/torrent-filter.h>

#include "Prefs.h"
#include "UserMetaType.h"

class TorrentFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TorrentFilter(Prefs const& prefs);
    ~TorrentFilter() override = default;
    TorrentFilter(TorrentFilter&&) = delete;
    TorrentFilter(TorrentFilter const&) = delete;
    TorrentFilter& operator=(TorrentFilter&&) = delete;
    TorrentFilter& operator=(TorrentFilter const&) = delete;

protected:
    // QSortFilterProxyModel
    [[nodiscard]] bool filterAcceptsRow(int source_row, QModelIndex const& source_parent) const override;
    [[nodiscard]] bool lessThan(QModelIndex const& source_left, QModelIndex const& source_right) const override;

private slots:
    void onPrefChanged(tr_quark key);
    void refilter();

private:
    void refilterSoon();

    QTimer refilter_timer_;
    Prefs const& prefs_;
    tr::app::TorrentFilter filter_;
    sigslot::scoped_connection filter_changed_ = filter_.changed.connect_scoped(
        [this](tr::app::TorrentFilter::Change /*change*/) { refilterSoon(); });
};
