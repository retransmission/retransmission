// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t
#include <optional>
#include <vector>

#include <sigslot/signal.hpp>

#include <QtCore/QAbstractListModel>
#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <QtGui/QIcon>

#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>

#include "Typedefs.h"

// A `tr::app::TorrentList` as a Qt list model: one row per torrent, in id order.
// The list does the diffing; this brackets its row signals with Qt's and
// re-emits its `Changes` as Qt signals.
class TorrentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    // NOLINTNEXTLINE(performance-enum-size)
    enum Role { TorrentRole = Qt::UserRole }; // `tr::app::Torrent const*`

    explicit TorrentModel(tr::app::TorrentList& torrents);
    TorrentModel& operator=(TorrentModel&&) = delete;
    TorrentModel& operator=(TorrentModel const&) = delete;
    TorrentModel(TorrentModel&&) = delete;
    TorrentModel(TorrentModel const&) = delete;
    ~TorrentModel() override = default;

    [[nodiscard]] tr::app::TorrentList const& list() const noexcept
    {
        return torrents_;
    }

    // forgets every torrent, e.g. when the session changes
    void clear();

    // QAbstractItemModel
    [[nodiscard]] int rowCount(QModelIndex const& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

signals:
    void torrentsAdded(torrent_ids_t const&);
    void torrentsChanged(torrent_ids_t const&, tr::app::TorrentFieldSet const& fields);
    void torrentsCompleted(torrent_ids_t const&);
    void torrentsEdited(torrent_ids_t const&);
    void torrentsNeedInfo(torrent_ids_t const&);

private:
    // the conversions the delegate asks for on every paint, built on first use
    struct Row {
        std::optional<QString> name;
        std::optional<QIcon> icon;
    };

    void onRowsInserting(size_t row, size_t count);
    void onRowsInserted(size_t row, size_t count);
    void onRowsRemoving(size_t row, size_t count);
    void onRowsRemoved(size_t row, size_t count);
    void onChanged(tr::app::TorrentList::Changes const& changes);

    tr::app::TorrentList& torrents_;
    mutable std::vector<Row> rows_; // parallel to the list's rows
    std::array<sigslot::scoped_connection, 5> connections_;
};

Q_DECLARE_METATYPE(tr::app::Torrent const*)
