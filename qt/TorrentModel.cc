// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentModel.h"

#include <cstddef> // size_t, ptrdiff_t
#include <iterator> // std::next()
#include <utility>

#include <QtCore/QVariant>

#include <libtransmission/quark.h>

#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>

#include "IconCache.h"
#include "Utils.h"

using tr::app::Torrent;
using tr::app::TorrentList;

namespace
{

// the fields behind the cached conversions
auto constexpr NameFields = Torrent::fields<TR_KEY_name>();
auto constexpr IconFields = Torrent::fields<TR_KEY_file_count, TR_KEY_is_folder, TR_KEY_primary_mime_type>();

[[nodiscard]] torrent_ids_t to_ids(TorrentList::Ids const& ids)
{
    return { std::begin(ids), std::end(ids) };
}

} // namespace

TorrentModel::TorrentModel(TorrentList& torrents)
    : torrents_{ torrents }
    , rows_(std::size(torrents))
    , connections_{ torrents.rows_inserting.connect_scoped(&TorrentModel::onRowsInserting, this),
                    torrents.rows_inserted.connect_scoped(&TorrentModel::onRowsInserted, this),
                    torrents.rows_removing.connect_scoped(&TorrentModel::onRowsRemoving, this),
                    torrents.rows_removed.connect_scoped(&TorrentModel::onRowsRemoved, this),
                    torrents.changed.connect_scoped(&TorrentModel::onChanged, this) }
{
}

void TorrentModel::clear()
{
    torrents_.clear();
}

int TorrentModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(std::size(torrents_));
}

QVariant TorrentModel::data(QModelIndex const& index, int const role) const
{
    if (!index.isValid() || std::cmp_greater_equal(index.row(), std::size(torrents_))) {
        return {};
    }

    auto const row = static_cast<size_t>(index.row());
    auto const& tor = torrents_[row];
    auto& cache = rows_[row];

    switch (role) {
    case Qt::DisplayRole:
        if (!cache.name) {
            cache.name = Utils::qstringFromUtf8(tor.name());
        }
        return *cache.name;

    case Qt::DecorationRole:
        if (!cache.icon) {
            // a magnet has no files until its metadata arrives
            cache.icon = tor.file_count() == 0 ?
                IconCache::get().magnetIcon() :
                IconCache::get().getMimeTypeIcon(Utils::qstringFromUtf8(tor.primary_mime_type()), tor.is_folder());
        }
        return *cache.icon;

    case TorrentRole:
        return QVariant::fromValue(&tor);

    default:
        return {};
    }
}

// ---

void TorrentModel::onRowsInserting(size_t const row, size_t const count)
{
    beginInsertRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row + count - 1U));
}

void TorrentModel::onRowsInserted(size_t const row, size_t const count)
{
    rows_.insert(std::next(std::begin(rows_), static_cast<std::ptrdiff_t>(row)), count, Row{});
    endInsertRows();
}

void TorrentModel::onRowsRemoving(size_t const row, size_t const count)
{
    beginRemoveRows(QModelIndex{}, static_cast<int>(row), static_cast<int>(row + count - 1U));
}

void TorrentModel::onRowsRemoved(size_t const row, size_t const count)
{
    auto const first = std::next(std::begin(rows_), static_cast<std::ptrdiff_t>(row));
    rows_.erase(first, std::next(first, static_cast<std::ptrdiff_t>(count)));
    endRemoveRows();
}

void TorrentModel::onChanged(TorrentList::Changes const& changes)
{
    // Drop every stale conversion before the first dataChanged(): a sort
    // triggered by one span compares its rows against the others.
    auto ids = torrent_ids_t{};
    for (auto const& change : changes.changed) {
        ids.insert(change.id);

        auto& cache = rows_[change.row];
        if ((change.fields & NameFields).any()) {
            cache.name.reset();
        }
        if ((change.fields & IconFields).any()) {
            cache.icon.reset();
        }
    }

    // one dataChanged() per span of adjacent rows
    auto const emit_spans = [this](auto const& rows, auto const row_of) {
        for (auto first = std::begin(rows), end = std::end(rows); first != end;) {
            auto last = first;
            while (std::next(last) != end && row_of(*std::next(last)) == row_of(*last) + 1U) {
                ++last;
            }
            emit dataChanged(index(static_cast<int>(row_of(*first))), index(static_cast<int>(row_of(*last))));
            first = std::next(last);
        }
    };
    emit_spans(changes.changed, [](TorrentList::TorrentChange const& change) { return change.row; });

    // A sorting proxy placed each new row among the old rows before those
    // moved, so have it place the new rows again now that they have.
    emit_spans(changes.inserted, [](size_t const row) { return row; });

    if (!std::empty(changes.edited)) {
        emit torrentsEdited(to_ids(changes.edited));
    }

    if (!std::empty(changes.added)) {
        emit torrentsAdded(to_ids(changes.added));
    }

    if (!std::empty(changes.need_info)) {
        emit torrentsNeedInfo(to_ids(changes.need_info));
    }

    if (!std::empty(ids)) {
        emit torrentsChanged(ids, changes.changed_fields);
    }

    if (!std::empty(changes.completed)) {
        emit torrentsCompleted(to_ids(changes.completed));
    }
}
