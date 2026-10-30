// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/torrent.h"

#include <algorithm>
#include <cstddef> // size_t
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/types.h"
#include "libtransmission/values.h"
#include "libtransmission/variant.h"
#include "libtransmission/web-utils.h" // tr_urlParse()

#include "libtransmission-app/prefs.h"

namespace tr::app
{

Torrent::Torrent(Prefs const& prefs, tr_torrent_id_t const id) noexcept
    : prefs_{ prefs }
    , id_{ id }
{
}

TorrentFieldSet Torrent::update(tr_variant::Map const& row)
{
    auto changed = TorrentFieldSet{};

    for (auto const& [key, value] : row) {
        set_field(key, value, changed);
    }

    finish_update(changed);
    return changed;
}

TorrentFieldSet Torrent::update(std::span<tr_quark const> const keys, std::span<tr_variant const> const values)
{
    auto changed = TorrentFieldSet{};

    for (size_t i = 0, n = std::min(std::size(keys), std::size(values)); i < n; ++i) {
        set_field(keys[i], values[i], changed);
    }

    finish_update(changed);
    return changed;
}

// One bit per member, and no key has two entries. Two pairs of keys share a
// member and its bit: `files`/`file_stats` and `trackers`/`tracker_stats`.
static_assert(Torrent::all_fields().count() + 2U == std::tuple_size_v<decltype(Torrent::Fields)>);
static_assert(Torrent::fields<TR_KEY_files>() == Torrent::fields<TR_KEY_file_stats>());
static_assert(Torrent::fields<TR_KEY_trackers>() == Torrent::fields<TR_KEY_tracker_stats>());
static_assert(tr::serializer::has_unique_keys<Torrent>());

namespace
{

// stores `value` in the member of `Fields` entry `I`; returns `true` iff the member changed
template<size_t I>
bool set_entry(Torrent& tor, tr_variant const& value)
{
    using Entry = std::tuple_element_t<I, std::remove_cvref_t<decltype(Torrent::Fields)>>;
    return tr::serializer::set(tor.*Entry::MemberPointer, value);
}

} // namespace

void Torrent::set_field(tr_quark const key, tr_variant const& value, TorrentFieldSet& changed)
{
    // Tables, not a search through `Fields`: `update()` sees every key of every torrent in a response.
    static constexpr auto Bits = member_bits();
    static constexpr auto NoEntry = uint8_t{ 255U };
    static_assert(std::size(Bits) < NoEntry);

    static constexpr auto EntryOfKey = []<size_t... I>(std::index_sequence<I...> /*indices*/) {
        auto entries = std::array<uint8_t, TR_N_KEYS>{};
        entries.fill(NoEntry);
        ((entries[std::get<I>(Fields).key] = static_cast<uint8_t>(I)), ...);
        return entries;
    }(std::make_index_sequence<std::size(Bits)>{});

    static constexpr auto Setters = []<size_t... I>(std::index_sequence<I...> /*indices*/) {
        return std::array{ &set_entry<I>... };
    }(std::make_index_sequence<std::size(Bits)>{});

    // keys interned at runtime, e.g. from a newer session, have no entry
    if (key >= std::size(EntryOfKey)) {
        return;
    }

    if (auto const entry = EntryOfKey[key]; entry != NoEntry && Setters[entry](*this, value)) {
        changed.set(Bits[entry]);
    }
}

void Torrent::finish_update(TorrentFieldSet const& changed)
{
    if ((changed & fields<TR_KEY_files>()).any()) {
        for (size_t i = 0, n = std::size(files_); i < n; ++i) {
            files_[i].index = i;
        }
    }

    if ((changed & fields<TR_KEY_tracker_stats>()).any()) {
        // Sessions older than 4.0 don't report sitenames, so derive those from the announce URL.
        // Rederive them on every change, since an entry loaded in place may hold a new URL.
        for (auto& tracker : tracker_stats_) {
            if (std::empty(tracker.sitename) || tracker.sitename_is_derived) {
                auto const parsed = tr_urlParse(tracker.announce.sv());
                tracker.sitename = parsed ? parsed->sitename : std::string_view{};
                tracker.sitename_is_derived = true;
            }
        }

        sitenames_.clear();
        sitenames_.reserve(std::size(tracker_stats_));
        for (auto const& tracker : tracker_stats_) {
            sitenames_.push_back(tracker.sitename);
        }
        std::ranges::sort(sitenames_);
        auto const [first, last] = std::ranges::unique(sitenames_);
        sitenames_.erase(first, last);
    }
}

bool Torrent::includes_tracker(std::string_view const sitename) const noexcept
{
    return std::ranges::binary_search(sitenames_, sitename);
}

std::optional<double> Torrent::effective_seed_ratio() const
{
    switch (seed_ratio_mode_) {
    case TR_RATIOLIMIT_SINGLE:
        return seed_ratio_limit_;

    case TR_RATIOLIMIT_GLOBAL:
        if (prefs_.get<bool>(TR_KEY_seed_ratio_limited)) {
            return prefs_.get<double>(TR_KEY_seed_ratio_limit);
        }
        return {};

    default:
        return {};
    }
}

tr::Values::Percent Torrent::metadata_percent_complete() const noexcept
{
    if (metadata_have_ && metadata_size_) {
        // the size is 0 until a peer reports it
        return *metadata_size_ != 0U ? Percent{ *metadata_have_, *metadata_size_ } : Percent{};
    }

    return Percent::from_double(metadata_percent_complete_);
}

// every size is 0 until the metadata arrives
tr::Values::Percent Torrent::progress_of(uint64_t const have, uint64_t const total) const noexcept
{
    return has_metadata() ? Percent{ have, total } : Percent{};
}

tr::Values::Percent Torrent::percent_complete() const noexcept
{
    return progress_of(have_total().base_quantity(), total_size_.base_quantity());
}

tr::Values::Percent Torrent::percent_done() const noexcept
{
    auto const size_when_done = size_when_done_.base_quantity();
    return progress_of(size_when_done - left_until_done_.base_quantity(), size_when_done);
}

tr::Values::Percent Torrent::recheck_progress() const noexcept
{
    if (checked_bytes_) {
        return progress_of(*checked_bytes_, total_size_.base_quantity());
    }

    return Percent::from_double(recheck_progress_);
}

tr::Values::Percent Torrent::peer_progress(Peer const& peer) const noexcept
{
    // Without metadata, every size is 0 and `progress` alone tells seeds apart.
    if (peer.have_total && has_metadata()) {
        return { *peer.have_total, total_size_.base_quantity() };
    }

    return Percent::from_double(peer.progress);
}

tr::Values::Percent Torrent::seed_ratio_percent_done() const
{
    auto const limit = effective_seed_ratio();
    auto const ratio = upload_ratio();

    // a ratio with no number, none or infinite, has nothing left to reach
    if (!limit || !(*limit > 0.0) || !is_done() || is_finished_ || ratio.is_none() || ratio.is_infinite()) {
        return { 1U, 1U };
    }

    // the exact ratio: `ratio`'s hundredths could round up to the limit and fill the bar early
    return Percent::from_double(upload_ratio_ / *limit);
}

tr::Values::Percent Torrent::activity_percent_done() const
{
    if (is_seeding() && effective_seed_ratio().has_value()) {
        return seed_ratio_percent_done();
    }

    if (!has_metadata()) {
        return metadata_percent_complete();
    }

    return percent_done();
}

} // namespace tr::app
