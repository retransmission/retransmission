// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <cstdint>
#include <ctime> // time_t
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include <sigslot/signal.hpp>

#include "libtransmission/digest.h" // tr_sha1_digest_t
#include "libtransmission/quark.h"
#include "libtransmission/types.h" // tr_torrent_id_t
#include "libtransmission/variant.h"

#include "libtransmission-app/torrent.h"

namespace tr::app
{

class Prefs;

/**
 * The client's copy of a session's torrents, sorted by id.
 *
 * `update()` applies one `torrent_get` response and reports every effect
 * through the signals below, synchronously, on the caller's thread, so a
 * view learns the same things whether a toolkit or a poll scheduler made
 * the call. A `Torrent` keeps its address until `rows_removing` names its
 * row, so a view may hold a `Torrent const*` between calls.
 */
class TorrentList
{
public:
    // Whether the request named no `ids`, so the response lists every torrent,
    // or named some, e.g. `recently_active`. Only a complete response can
    // report a torrent gone by its absence.
    enum class Coverage : uint8_t {
        Partial,
        Complete,
    };

    // ascending and unique, so membership is `std::ranges::binary_search()`
    using Ids = std::vector<tr_torrent_id_t>;

    // one torrent whose fields an update changed
    struct TorrentChange {
        tr_torrent_id_t id = 0;
        size_t row = 0; // its row when `changed` fires
        TorrentFieldSet fields; // never empty

        [[nodiscard]] bool operator==(TorrentChange const&) const noexcept = default;
    };

    // What one `update()` did. Every torrent named here is in the list when
    // `changed` fires, except the removed ones. The torrents it inserted
    // aren't changes: the row signals report them, and `inserted` says where
    // they ended up.
    struct Changes {
        std::vector<size_t> inserted; // the rows the update inserted, ascending
        std::vector<TorrentChange> changed; // in row order
        TorrentFieldSet changed_fields; // the union of `changed[i].fields`
        Ids added; // named torrents the session got within `AddedWindow` of `now`; each is reported once
        Ids completed; // `left_until_done` reached zero after downloading something
        Ids edited; // `edit_date` changed, so the session's copy of the metainfo did too
        Ids need_info; // first seen without a name, e.g. a new magnet or a stats-only row; fetch their info
        Ids removed; // the rows the response's `removed` named, plus those a complete response omitted

        [[nodiscard]] bool any() const noexcept;
    };

    // a torrent the session has had longer than this is not `added`, so a startup or reconnect list is quiet
    static constexpr auto AddedWindow = time_t{ 60 };

    explicit TorrentList(Prefs const& prefs) noexcept;
    ~TorrentList() = default;
    TorrentList(TorrentList&&) = delete;
    TorrentList(TorrentList const&) = delete;
    TorrentList& operator=(TorrentList&&) = delete;
    TorrentList& operator=(TorrentList const&) = delete;

    // Applies a `torrent_get` response's `arguments`: its `torrents`, in object
    // or table format, then its `removed` ids. A row without a usable `id` is
    // skipped, and one skipped row stops a complete response from removing by
    // absence. Keys `Torrent` doesn't track are ignored. `now` dates the
    // `added` window. A slot must not call `update()` or `clear()`.
    void update(tr_variant::Map const& args, Coverage coverage, time_t now);

    // Removes every row as one run. Fires the row signals only: the session keeps its torrents.
    void clear();

    // --- rows, in id order

    [[nodiscard]] size_t size() const noexcept
    {
        return std::size(entries_);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return std::empty(entries_);
    }

    // `row` must be less than `size()`
    [[nodiscard]] Torrent const& operator[](size_t const row) const noexcept
    {
        return *entries_[row].torrent;
    }

    // nullptr if no row has this id
    [[nodiscard]] Torrent const* find(tr_torrent_id_t id) const noexcept;

    // nullptr if no torrent has this info hash; a linear search, for duplicate checks when adding
    [[nodiscard]] Torrent const* find(tr_sha1_digest_t const& hash) const noexcept;

    // nullopt if no row has this id
    [[nodiscard]] std::optional<size_t> row_of(tr_torrent_id_t id) const noexcept;

    // every torrent, in row order
    [[nodiscard]] auto torrents() const
    {
        return entries_ | std::views::transform(&Entry::get);
    }

    // --- signals, in the order one `update()` fires them. Connect with `connect_scoped()`.
    // A row signal names one contiguous run of rows, once before the list changes and
    // once after: a run of inserts while `size()` still excludes it, a run of removes
    // while `operator[]` still reaches it. Inserts come in ascending row order, removes
    // in descending, so the rows a signal names are right when it fires. By then every
    // torrent already holds the update's values; `changed` only says which fields moved.

    sigslot::signal<size_t /*row*/, size_t /*count*/> rows_inserting;
    sigslot::signal<size_t /*row*/, size_t /*count*/> rows_inserted;
    sigslot::signal<size_t /*row*/, size_t /*count*/> rows_removing;
    sigslot::signal<size_t /*row*/, size_t /*count*/> rows_removed;

    // once per `update()` that did anything, after its row signals
    sigslot::signal<Changes const&> changed;

private:
    struct Entry {
        std::unique_ptr<Torrent> torrent; // heap: a `Torrent` isn't assignable, and views hold its address
        bool added_reported = false;

        [[nodiscard]] Torrent const& get() const noexcept
        {
            return *torrent;
        }

        [[nodiscard]] tr_torrent_id_t id() const noexcept
        {
            return torrent->id();
        }
    };

    // a `torrents` entry with a usable id: a Map in object format, a Vector in table format
    struct Row {
        tr_torrent_id_t id = 0;
        tr_variant const* values = nullptr;
    };

    // what the update did to one torrent, by id: rows move before `changed` fires
    struct Seen {
        tr_torrent_id_t id = 0;
        TorrentFieldSet fields;
        bool is_new = false;
    };

    // a response's `torrents` list, parsed
    struct Parsed {
        std::vector<Row> rows; // the rows with a usable id, sorted by id
        std::vector<tr_quark> keys; // the header's keys, in table format
        size_t n_skipped = 0; // the rows without a usable id
    };

    [[nodiscard]] static Parsed parse_rows(tr_variant::Vector const& torrents);

    // applies id-sorted `rows`; new torrents go to `newcomers`, not the list, so no row moves yet
    void apply_rows(
        std::span<Row const> rows,
        std::span<tr_quark const> keys,
        std::vector<Entry>& newcomers,
        std::vector<Seen>& seen);

    // merges id-sorted `newcomers` into the list, one run per gap, in ascending row order
    void insert_runs(std::vector<Entry>& newcomers);

    // erases the rows of ascending `ids`, one run at a time, in descending row order
    void erase_runs(Ids const& ids);

    // fires `changed` with what `seen` and `removed` amount to
    void report(std::span<Seen const> seen, Ids removed, time_t now);

    Prefs const& prefs_;
    std::vector<Entry> entries_; // sorted by id
    bool updating_ = false; // a slot that re-entered `update()` would corrupt the runs
};

} // namespace tr::app
