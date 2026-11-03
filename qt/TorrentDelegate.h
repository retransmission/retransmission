// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <QtCore/QString>

#include <QtGui/QIcon>

#include <QtWidgets/QStyledItemDelegate>

#include "NativeIcon.h"

class QStyle;
class QStyleOptionProgressBar;

namespace tr::app
{
class Torrent;
}

class TorrentDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TorrentDelegate(QObject* parent = nullptr);
    ~TorrentDelegate() override = default;
    TorrentDelegate& operator=(TorrentDelegate&&) = delete;
    TorrentDelegate& operator=(TorrentDelegate const&) = delete;
    TorrentDelegate(TorrentDelegate&&) = delete;
    TorrentDelegate(TorrentDelegate const&) = delete;

    // QAbstractItemDelegate
    QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const override;
    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override;

    // The words for a torrent's state and error. Here until libtransmission-app can translate.
    [[nodiscard]] static QString activityString(tr::app::Torrent const& tor);
    [[nodiscard]] static QString errorString(tr::app::Torrent const& tor);

protected:
    // a row's torrent, with the model's cached conversions of it
    struct Item {
        tr::app::Torrent const& torrent;
        QString name;
        QIcon icon;
    };

    [[nodiscard]] static Item item(QModelIndex const& index);

    [[nodiscard]] QSize margin(QStyle const& style) const;
    void setProgressBarPercentDone(QStyleOptionViewItem const& option, tr::app::Torrent const& tor) const;
    [[nodiscard]] QIcon warningEmblem() const
    {
        return warning_emblem_;
    }

    // Our own overridables
    [[nodiscard]] virtual QSize itemSizeHint(QStyleOptionViewItem const& option, Item const& item) const;
    virtual void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Item const& item) const;

    [[nodiscard]] static QString statusString(tr::app::Torrent const& tor);
    [[nodiscard]] static QString progressString(tr::app::Torrent const& tor);
    [[nodiscard]] static QString shortStatusString(tr::app::Torrent const& tor);
    [[nodiscard]] static QString shortTransferString(tr::app::Torrent const& tor);

    static inline QColor const BlueBack{ "lightgrey" };
    static inline QColor const BlueBrush{ "steelblue" };
    static inline QColor const GreenBack{ "darkseagreen" };
    static inline QColor const GreenBrush{ "forestgreen" };
    static inline QColor const SilverBack{ "grey" };
    static inline QColor const SilverBrush{ "silver" };

    mutable QStyleOptionProgressBar progress_bar_style_;

private:
    QIcon const warning_emblem_ = icons::icon(icons::Type::TorrentErrorEmblem);
    mutable std::optional<int> height_hint_;
    mutable QFont height_font_;
};
