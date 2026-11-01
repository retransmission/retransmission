// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/torrent-list.h"

#include <algorithm>
#include <cstddef> // size_t, ptrdiff_t
#include <ctime> // time_t
#include <iterator> // std::back_inserter(), std::distance()
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/digest.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/torrent.h"

namespace tr::app
{

bool TorrentList::Changes::any() const noexcept
{
    return !std::empty(changed) || !std::empty(added) || !std::empty(completed) || !std::empty(edited) ||
        !std::empty(need_info) || !std::empty(removed);
}

TorrentList::TorrentList(Prefs const& prefs) noexcept
    : prefs_{ prefs }
{
}

// --- lookup

std::optional<size_t> TorrentList::row_of(tr_torrent_id_t const id) const noexcept
{
    auto const iter = std::ranges::lower_bound(entries_, id, {}, &Entry::id);
    if (iter != std::end(entries_) && iter->id() == id) {
        return static_cast<size_t>(std::distance(std::begin(entries_), iter));
    }

    return {};
}

Torrent const* TorrentList::find(tr_torrent_id_t const id) const noexcept
{
    auto const row = row_of(id);
    return row ? entries_[*row].torrent.get() : nullptr;
}

Torrent const* TorrentList::find(tr_sha1_digest_t const& hash) const noexcept
{
    auto const iter = std::ranges::find(entries_, hash, [](Entry const& entry) -> auto const& {
        return entry.torrent->hash();
    });
    return iter != std::end(entries_) ? iter->torrent.get() : nullptr;
}

// --- update

TorrentList::Parsed TorrentList::parse_rows(tr_variant::Vector const& torrents)
{
    auto parsed = Parsed{};
    auto& [rows, keys, n_skipped] = parsed;
    rows.reserve(std::size(torrents));

    // in table format, the first entry names the keys and each later one holds a torrent's values
    auto const* const header = !std::empty(torrents) ? torrents.front().get_if<tr_variant::Vector>() : nullptr;
    auto id_column = std::optional<size_t>{};
    if (header != nullptr) {
        keys.reserve(std::size(*header));
        for (auto const& name : *header) {
            // a key `Torrent` doesn't know holds its column with `TR_KEY_NONE`, which `Torrent::update()` ignores
            auto const key = name.value_if<std::string_view>().and_then(tr_quark_lookup).value_or(TR_KEY_NONE);
            if (key == TR_KEY_id) {
                id_column = std::size(keys);
            }
            keys.push_back(key);
        }
    }

    for (auto const& item : std::span{ torrents }.subspan(header != nullptr ? 1U : 0U)) {
        auto id = std::optional<tr_torrent_id_t>{};

        if (header != nullptr) {
            auto const* const values = item.get_if<tr_variant::Vector>();
            if (values != nullptr && id_column && *id_column < std::size(*values)) {
                id = (*values)[*id_column].value_if<tr_torrent_id_t>();
            }
        } else if (auto const* const map = item.get_if<tr_variant::Map>(); map != nullptr) {
            id = map->value_if<tr_torrent_id_t>(TR_KEY_id);
        }

        if (id) {
            rows.push_back({ .id = *id, .values = &item });
        } else {
            ++n_skipped;
        }
    }

    // duplicates become adjacent, still in response order, so the later row wins
    std::ranges::stable_sort(rows, {}, &Row::id);
    return parsed;
}

void TorrentList::apply_rows(
    std::span<Row const> const rows,
    std::span<tr_quark const> const keys,
    std::vector<Entry>& newcomers,
    std::vector<Seen>& seen)
{
    auto iter = std::begin(entries_);

    for (auto const& row : rows) {
        // both ascend, so the cursor only moves forward
        iter = std::ranges::lower_bound(iter, std::end(entries_), row.id, {}, &Entry::id);

        auto* tor = static_cast<Torrent*>(nullptr);
        auto is_new = false;
        if (iter != std::end(entries_) && iter->id() == row.id) {
            tor = iter->torrent.get();
        } else if (!std::empty(newcomers) && newcomers.back().id() == row.id) {
            tor = newcomers.back().torrent.get();
            is_new = true;
        } else {
            auto& entry = newcomers.emplace_back();
            entry.torrent = std::make_unique<Torrent>(prefs_, row.id);
            tor = entry.torrent.get();
            is_new = true;
        }

        auto const* const map = row.values->get_if<tr_variant::Map>();
        auto const fields = map != nullptr ? tor->update(*map) : tor->update(keys, *row.values->get_if<tr_variant::Vector>());

        if (!std::empty(seen) && seen.back().id == row.id) {
            seen.back().fields |= fields;
        } else {
            seen.push_back({ .id = row.id, .fields = fields, .is_new = is_new });
        }
    }
}

void TorrentList::insert_runs(std::vector<Entry>& newcomers)
{
    auto first = std::begin(newcomers);
    auto const end = std::end(newcomers);
    auto search_from = size_t{};

    while (first != end) {
        auto const row = static_cast<size_t>(std::distance(
            std::begin(entries_),
            std::ranges::lower_bound(
                std::next(std::begin(entries_), static_cast<std::ptrdiff_t>(search_from)),
                std::end(entries_),
                first->id(),
                {},
                &Entry::id)));

        // the run is every newcomer that sorts before the entry now at `row`
        auto last = end;
        if (row < std::size(entries_)) {
            last = std::ranges::lower_bound(first, end, entries_[row].id(), {}, &Entry::id);
        }

        auto const count = static_cast<size_t>(std::distance(first, last));
        rows_inserting(row, count);
        entries_.insert(
            std::next(std::begin(entries_), static_cast<std::ptrdiff_t>(row)),
            std::make_move_iterator(first),
            std::make_move_iterator(last));
        rows_inserted(row, count);

        search_from = row + count;
        first = last;
    }
}

void TorrentList::erase_runs(Ids const& ids)
{
    // ascending ids, ascending rows
    auto rows = std::vector<size_t>{};
    rows.reserve(std::size(ids));
    for (auto const id : ids) {
        if (auto const row = row_of(id)) {
            rows.push_back(*row);
        }
    }

    // highest run first, so the rows below it keep their numbers
    for (auto end = std::size(rows); end > 0U;) {
        auto begin = end - 1U;
        while (begin > 0U && rows[begin - 1U] + 1U == rows[begin]) {
            --begin;
        }

        auto const row = rows[begin];
        auto const count = end - begin;
        rows_removing(row, count);
        auto const iter = std::next(std::begin(entries_), static_cast<std::ptrdiff_t>(row));
        entries_.erase(iter, std::next(iter, static_cast<std::ptrdiff_t>(count)));
        rows_removed(row, count);

        end = begin;
    }
}

void TorrentList::report(std::span<Seen const> const seen, Ids removed, time_t const now)
{
    auto changes = Changes{};
    changes.removed = std::move(removed);

    auto iter = std::begin(entries_);
    for (auto const& item : seen) {
        iter = std::ranges::lower_bound(iter, std::end(entries_), item.id, {}, &Entry::id);
        if (iter == std::end(entries_) || iter->id() != item.id) {
            continue; // removed by this same update
        }

        auto& entry = *iter;
        auto const& tor = *entry.torrent;

        if (item.is_new) {
            // the row signals report the torrent itself; its first row's values aren't changes
            if (!tor.has_name()) {
                changes.need_info.push_back(item.id);
            }
        } else {
            if (item.fields.any()) {
                auto const row = static_cast<size_t>(std::distance(std::begin(entries_), iter));
                changes.changed.push_back({ .id = item.id, .row = row, .fields = item.fields });
                changes.changed_fields |= item.fields;
            }

            if ((item.fields & Torrent::fields<TR_KEY_edit_date>()).any()) {
                changes.edited.push_back(item.id);
            }

            if ((item.fields & Torrent::fields<TR_KEY_left_until_done>()).any() && tor.is_done() &&
                !tor.downloaded_ever().is_zero()) {
                changes.completed.push_back(item.id);
            }
        }

        // the notification shows the name, so a nameless magnet waits for its name
        if (!entry.added_reported && tor.has_name() && tor.added_date() != 0 && now - tor.added_date() < AddedWindow) {
            changes.added.push_back(item.id);
            entry.added_reported = true;
        }
    }

    if (changes.any()) {
        changed(changes);
    }
}

void TorrentList::update(tr_variant::Map const& args, Coverage const coverage, time_t const now)
{
    TR_ASSERT(!updating_);

    auto const* const torrents = args.find_if<tr_variant::Vector>(TR_KEY_torrents);
    auto const* const removed = args.find_if<tr_variant::Vector>(TR_KEY_removed);
    if (torrents == nullptr && removed == nullptr) {
        return;
    }

    updating_ = true;

    auto const parsed = torrents != nullptr ? parse_rows(*torrents) : Parsed{};
    if (parsed.n_skipped != 0U) {
        tr_logAddWarn(fmt::format("Ignored {:d} torrent record(s) that had no usable id", parsed.n_skipped));
    }

    auto newcomers = std::vector<Entry>{};
    auto seen = std::vector<Seen>{};
    seen.reserve(std::size(parsed.rows));
    apply_rows(parsed.rows, parsed.keys, newcomers, seen);
    insert_runs(newcomers);

    auto gone = Ids{};
    if (removed != nullptr) {
        for (auto const& item : *removed) {
            if (auto const id = item.value_if<tr_torrent_id_t>(); id && find(*id) != nullptr) {
                gone.push_back(*id);
            }
        }
    }

    // A skipped row is a torrent we couldn't identify, so removing by
    // absence could evict a torrent the session still has.
    if (coverage == Coverage::Complete && torrents != nullptr && parsed.n_skipped == 0U) {
        std::ranges::set_difference(
            entries_ | std::views::transform(&Entry::id),
            seen | std::views::transform(&Seen::id),
            std::back_inserter(gone));
    }

    std::ranges::sort(gone);
    auto const [dupes_begin, dupes_end] = std::ranges::unique(gone);
    gone.erase(dupes_begin, dupes_end);

    erase_runs(gone);
    report(seen, std::move(gone), now);

    updating_ = false;
}

void TorrentList::clear()
{
    TR_ASSERT(!updating_);

    if (std::empty(entries_)) {
        return;
    }

    updating_ = true;

    auto const count = std::size(entries_);
    rows_removing(0U, count);
    entries_.clear();
    rows_removed(0U, count);

    updating_ = false;
}

} // namespace tr::app
