// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "TrackerModel.h"

#include <algorithm> // std::sort()
#include <utility> // std::move()

#include <QtCore/QUrl>

#include "Application.h" // Application
#include "TorrentModel.h"
#include "Utils.h"

int TrackerModel::rowCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent)

    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TrackerModel::data(QModelIndex const& index, int role) const
{
    QVariant var;

    if (index.isValid() && index.row() < rowCount()) {
        TrackerInfo const& tracker_info = rows_.at(index.row());

        switch (role) {
        case Qt::DisplayRole:
            var = Utils::qstringFromUtf8(tracker_info.st.announce.sv());
            break;

        case Qt::DecorationRole:
            var = QIcon{ trApp->find_favicon(tracker_info.st.sitename) };
            break;

        case TrackerRole:
            var = QVariant::fromValue(tracker_info);
            break;

        default:
            break;
        }
    }

    return var;
}

/***
****
***/

void TrackerModel::refresh(TorrentModel const& torrent_model, torrent_ids_t const& ids)
{
    struct CompareTrackers {
        bool operator()(TrackerInfo const& a, TrackerInfo const& b) const
        {
            if (a.torrent_id != b.torrent_id) {
                return a.torrent_id < b.torrent_id;
            }

            if (a.st.tier != b.st.tier) {
                return a.st.tier < b.st.tier;
            }

            if (a.st.is_backup != b.st.is_backup) {
                return !a.st.is_backup;
            }

            return a.st.announce < b.st.announce;
        }
    };

    // build a list of the TrackerInfos
    auto trackers = std::vector<TrackerInfo>{};

    for (int const id : ids) {
        auto const* const tor = torrent_model.list().find(id);

        if (tor != nullptr) {
            for (auto const& st : tor->tracker_stats()) {
                trackers.push_back({ .st = st, .torrent_id = id });
            }
        }
    }

    // sort 'em
    static auto constexpr Comp = CompareTrackers{};
    std::ranges::sort(trackers, Comp);

    // merge 'em with the existing list
    int old_index = 0;
    int new_index = 0;

    while (std::cmp_less(old_index, rows_.size()) || std::cmp_less(new_index, trackers.size())) {
        auto const is_end_of_old = std::cmp_equal(old_index, rows_.size());
        auto const is_end_of_new = std::cmp_equal(new_index, trackers.size());

        if (is_end_of_old || (!is_end_of_new && Comp(trackers.at(new_index), rows_.at(old_index)))) {
            // add this new row
            beginInsertRows(QModelIndex{}, old_index, old_index);
            rows_.insert(rows_.begin() + old_index, std::move(trackers.at(new_index)));
            endInsertRows();
            ++old_index;
            ++new_index;
        } else if (is_end_of_new || Comp(rows_.at(old_index), trackers.at(new_index))) {
            // remove this old row
            beginRemoveRows(QModelIndex{}, old_index, old_index);
            rows_.erase(rows_.begin() + old_index);
            endRemoveRows();
        } else // update existing row
        {
            rows_[old_index].st = std::move(trackers.at(new_index).st);
            emit dataChanged(index(old_index, 0), index(old_index, 0));
            ++old_index;
            ++new_index;
        }
    }
}

int TrackerModel::find(int torrent_id, QString const& url) const
{
    auto const url_str = url.toStdString();

    for (size_t i = 0, n = rows_.size(); i < n; ++i) {
        TrackerInfo const& inf = rows_.at(i);

        if (inf.torrent_id == torrent_id && inf.st.announce == url_str) {
            return static_cast<int>(i);
        }
    }

    return -1;
}
