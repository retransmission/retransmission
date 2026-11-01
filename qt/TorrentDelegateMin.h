// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "TorrentDelegate.h"

class TorrentDelegateMin : public TorrentDelegate
{
    Q_OBJECT

public:
    explicit TorrentDelegateMin(QObject* parent = nullptr)
        : TorrentDelegate{ parent }
    {
    }
    ~TorrentDelegateMin() override = default;
    TorrentDelegateMin(TorrentDelegateMin&&) = delete;
    TorrentDelegateMin(TorrentDelegateMin const&) = delete;
    TorrentDelegateMin& operator=(TorrentDelegateMin&&) = delete;
    TorrentDelegateMin& operator=(TorrentDelegateMin const&) = delete;

protected:
    // TorrentDelegate
    QSize itemSizeHint(QStyleOptionViewItem const& option, Item const& item) const override;
    void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Item const& item) const override;
};
