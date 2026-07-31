// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TorrentModel.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <ctime>
#include <iterator> // for std::back_inserter
#include <ranges>
#include <string_view>
#include <vector>

#include <QtCore/QDebug>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "Torrent.h"
#include "TorrentDelegate.h"
#include "VariantHelpers.h"

/***
****
***/

namespace
{

constexpr struct {
    constexpr bool operator()(Torrent const* left, Torrent const* right) const noexcept
    {
        return left->id() < right->id();
    }

    constexpr bool operator()(int left_id, Torrent const* right) const noexcept
    {
        return left_id < right->id();
    }

    constexpr bool operator()(Torrent const* left, int right_id) const noexcept
    {
        return left->id() < right_id;
    }

    constexpr bool operator()(int left_id, int right_id) const noexcept
    {
        return left_id < right_id;
    }
} TorrentIdLessThan;

template<typename Iter>
auto getIds(Iter it, Iter const& end)
{
    torrent_ids_t ids;

    for (; it != end; ++it) {
        ids.insert((*it)->id());
    }

    return ids;
}

} // namespace

/***
****
***/

TorrentModel::TorrentModel(Prefs const& prefs)
    : prefs_{ prefs }
{
}

TorrentModel::~TorrentModel()
{
    clear();
}

void TorrentModel::clear()
{
    beginResetModel();
    qDeleteAll(torrents_);
    torrents_.clear();
    endResetModel();
}

int TorrentModel::rowCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent)

    return static_cast<int>(torrents_.size());
}

QVariant TorrentModel::data(QModelIndex const& index, int role) const
{
    auto const* const t = (index.isValid() && index.row() < rowCount()) ? torrents_.at(index.row()) : nullptr;

    if (t != nullptr) {
        switch (role) {
        case Qt::DisplayRole:
            return t->name();

        case Qt::DecorationRole:
            return t->getMimeTypeIcon();

        case TorrentRole:
            return QVariant::fromValue(t);

        default:
            break;
        }
    }

    return {};
}

/***
****
***/

void TorrentModel::removeTorrents(tr_variant* torrent_list)
{
    auto const* const ids = torrent_list->get_if<tr_variant::Vector>();
    if (!ids || std::empty(*ids)) {
        return;
    }

    auto torrents = torrents_t{};
    torrents.reserve(std::size(*ids));

    for (tr_variant const& child : *ids) {
        if (auto const id = tr::serializer::to_value<int64_t>(child)) {
            if (auto* const torrent = getTorrentFromId(static_cast<int>(*id))) {
                torrents.push_back(torrent);
            }
        }
    }

    if (!torrents.empty()) {
        rowsRemove(torrents);
    }
}

void TorrentModel::updateTorrents(tr_variant* torrent_list, bool is_complete_list)
{
    auto const old = is_complete_list ? torrents_ : torrents_t{};
    auto added = torrent_ids_t{};
    auto changed = torrent_ids_t{};
    auto completed = torrent_ids_t{};
    auto edited = torrent_ids_t{};
    auto instantiated = torrents_t{};
    auto needinfo = torrent_ids_t{};
    auto processed = torrents_t{};
    auto changed_fields = Torrent::fields_t{};

    auto const now = time(nullptr);
    auto const recently_added = [&now](auto const& tor) {
        static auto constexpr MaxAge = 60;
        auto const date = tor->dateAdded();
        return (date != 0) && (difftime(now, date) < MaxAge);
    };

    auto* const list = torrent_list->get_if<tr_variant::Vector>();
    if (list == nullptr) {
        return;
    }

    auto* const first_child = !list->empty() ? &list->front() : nullptr;

    // In 'table' format, the first entry in 'torrents' is an array of keys.
    // All the other entries are an array of the values for one torrent.
    bool const table = first_child != nullptr && first_child->holds_alternative<tr_variant::Vector>();

    std::vector<tr_quark> keys;
    if (table) {
        auto const* const key_vec = first_child->get_if<tr_variant::Vector>();
        keys.reserve(key_vec->size());
        for (auto const& key_var : *key_vec) {
            if (auto const sv = key_var.value_if<std::string_view>()) {
                keys.push_back(tr_quark_new(*sv));
            }
        }
    }

    // Loop through the torrent records...
    std::vector<Torrent::keyval_t> keyvals;
    keyvals.reserve(keys.size());
    processed.reserve(list->size());
    auto n_skipped = size_t{};
    for (auto it = std::next(std::begin(*list), table ? 1 : 0); it != std::end(*list); ++it) {
        tr_variant& v = *it;

        // Pair up this torrent's keys and values
        keyvals.clear();
        if (table) {
            // In table mode, v is a list of values parallel to `keys`
            auto const* const row_vec = v.get_if<tr_variant::Vector>();
            if (row_vec == nullptr) {
                ++n_skipped;
                continue;
            }
            auto const n = std::min(std::size(keys), std::size(*row_vec));
            for (size_t i = 0; i < n; ++i) {
                keyvals.emplace_back(keys[i], &(*row_vec)[i]);
            }
        } else {
            // In object mode, v is an object of torrent property key/vals
            auto const* const row_map = v.get_if<tr_variant::Map>();
            if (row_map == nullptr) {
                ++n_skipped;
                continue;
            }
            for (auto const& [key, val] : *row_map) {
                keyvals.emplace_back(key, &val);
            }
        }

        // Find the torrent id
        auto const id_it = std::ranges::find(keyvals, TR_KEY_id, &Torrent::keyval_t::first);
        if (id_it == std::ranges::end(keyvals)) {
            ++n_skipped;
            continue;
        }
        auto const id_val = id_it->second->value_if<int64_t>();
        if (!id_val) {
            ++n_skipped;
            continue;
        }
        auto const id = static_cast<int>(*id_val);

        Torrent* tor = getTorrentFromId(id);
        bool is_new = false;

        if (tor == nullptr) {
            tor = new Torrent{ prefs_, id };
            instantiated.push_back(tor);
            is_new = true;
        }

        auto const fields = tor->update(keyvals);

        if (fields.any()) {
            changed_fields |= fields;
            changed.insert(id);
        }

        if (fields.test(Torrent::EDIT_DATE)) {
            edited.insert(id);
        }

        if (is_new && !tor->hasName()) {
            needinfo.insert(id);
        }

        if (recently_added(tor) && tor->hasName() && !already_added_.contains(id)) {
            added.insert(id);
            already_added_.insert(id);
        }

        if (fields.test(Torrent::LEFT_UNTIL_DONE) && (tor->leftUntilDone() == 0) && (tor->downloadedEver() > 0)) {
            completed.insert(id);
        }

        processed.push_back(tor);
    }

    if (n_skipped != 0U) {
        qWarning() << "Ignored" << n_skipped << "torrent record(s) that had no usable id";
    }

    // model upkeep

    if (!instantiated.empty()) {
        rowsAdd(instantiated);
    }

    if (!edited.empty()) {
        emit torrentsEdited(edited);
    }

    if (!changed.empty()) {
        rowsEmitChanged(changed);
    }

    // emit signals

    if (!added.empty()) {
        emit torrentsAdded(added);
    }

    if (!needinfo.empty()) {
        emit torrentsNeedInfo(needinfo);
    }

    if (!changed.empty()) {
        emit torrentsChanged(changed, changed_fields);
    }

    if (!completed.empty()) {
        emit torrentsCompleted(completed);
    }

    // model upkeep

    // Torrents absent from a complete list are gone from the session. A skipped
    // record defeats that reasoning: it's a torrent we failed to identify, and
    // removing it here would evict a torrent the session still has.
    if (is_complete_list && n_skipped == 0U) {
        std::ranges::sort(processed, TorrentIdLessThan);
        torrents_t removed;
        removed.reserve(old.size());
        std::ranges::set_difference(old, processed, std::back_inserter(removed), TorrentIdLessThan);
        rowsRemove(removed);
    }
}

/***
****
***/

std::optional<int> TorrentModel::getRow(int id) const
{
    std::optional<int> row;

    if (auto const [begin, end] = std::ranges::equal_range(torrents_, id, TorrentIdLessThan); begin != end) {
        row = std::distance(torrents_.begin(), begin);
        assert(torrents_[*row]->id() == id);
    }

    return row;
}

Torrent* TorrentModel::getTorrentFromId(int id)
{
    auto const row = getRow(id);
    return row ? torrents_[*row] : nullptr;
}

Torrent const* TorrentModel::getTorrentFromId(int id) const
{
    auto const row = getRow(id);
    return row ? torrents_[*row] : nullptr;
}

/***
****
***/

std::vector<TorrentModel::span_t> TorrentModel::getSpans(torrent_ids_t const& ids) const
{
    // ids -> rows
    std::vector<int> rows;
    rows.reserve(ids.size());
    for (auto const& id : ids) {
        auto const row = getRow(id);
        if (row) {
            rows.push_back(*row);
        }
    }

    std::ranges::sort(rows);

    // rows -> spans
    std::vector<span_t> spans;
    spans.reserve(rows.size());
    span_t span;
    bool in_span = false;
    for (auto const& row : rows) {
        if (in_span) {
            if (span.second + 1 == row) {
                span.second = row;
            } else {
                spans.push_back(span);
                in_span = false;
            }
        }

        if (!in_span) {
            span.first = span.second = row;
            in_span = true;
        }
    }

    if (in_span) {
        spans.push_back(span);
    }

    return spans;
}

/***
****
***/

void TorrentModel::rowsEmitChanged(torrent_ids_t const& ids)
{
    for (auto const& [first, last] : getSpans(ids)) {
        emit dataChanged(index(first), index(last));
    }
}

void TorrentModel::rowsAdd(torrents_t const& torrents)
{
    if (torrents_.empty()) {
        beginInsertRows(QModelIndex{}, 0, static_cast<int>(torrents.size() - 1));
        torrents_ = torrents;
        std::ranges::sort(torrents_, TorrentIdLessThan);
        endInsertRows();
    } else {
        for (auto const& tor : torrents) {
            auto const it = std::ranges::lower_bound(torrents_, tor, TorrentIdLessThan);
            auto const row = static_cast<int>(std::distance(torrents_.begin(), it));

            beginInsertRows(QModelIndex{}, row, row);
            torrents_.insert(it, tor);
            endInsertRows();
        }
    }
}

void TorrentModel::rowsRemove(torrents_t const& torrents)
{
    // must walk in reverse to avoid invalidating row numbers
    auto const& spans = getSpans(getIds(torrents.begin(), torrents.end()));
    for (auto const& [first, last] : std::views::reverse(spans)) {
        beginRemoveRows(QModelIndex{}, first, last);
        torrents_.erase(torrents_.begin() + first, torrents_.begin() + last + 1);
        endRemoveRows();
    }

    qDeleteAll(torrents);
}

/***
****
***/

bool TorrentModel::hasTorrent(TorrentHash const& hash) const
{
    auto test = [hash](auto const& tor) {
        return tor->hash() == hash;
    };
    return std::ranges::any_of(torrents_, test);
}
