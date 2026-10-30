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

// Every field has an entry, and no key has two. Two pairs of keys share a
// member and its field: `files`/`file_stats` and `trackers`/`tracker_stats`.
static_assert(
    std::apply([](auto const&... entry) { return (TorrentFieldSet{} | ... | entry.field); }, Torrent::Fields).count() ==
    static_cast<size_t>(TorrentField::N_FIELDS));
static_assert(std::tuple_size_v<decltype(Torrent::Fields)> == static_cast<size_t>(TorrentField::N_FIELDS) + 2U);
static_assert(tr::serializer::has_unique_keys<Torrent>());

void Torrent::set_field(tr_quark const key, tr_variant const& value, TorrentFieldSet& changed)
{
    auto const try_entry = [this, key, &value, &changed](auto const& entry) {
        if (entry.key != key) {
            return false;
        }

        using Entry = std::remove_cvref_t<decltype(entry)>;
        if (tr::serializer::set(this->*Entry::MemberPointer, value)) {
            changed.set(entry.field);
        }

        return true;
    };

    std::apply([&try_entry](auto const&... entry) { (try_entry(entry) || ...); }, Fields);
}

void Torrent::finish_update(TorrentFieldSet const& changed)
{
    if (changed.test(TorrentField::FILES)) {
        for (size_t i = 0, n = std::size(files_); i < n; ++i) {
            files_[i].index = i;
        }
    }

    if (changed.test(TorrentField::TRACKER_STATS)) {
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
    auto const seed_ratio = effective_seed_ratio();

    // the goal counts as reached when the ratio doesn't apply
    if (!seed_ratio || !is_done() || is_finished_) {
        return { 1U, 1U };
    }

    auto const goal = static_cast<uint64_t>(static_cast<double>(size_when_done_.base_quantity()) * *seed_ratio);
    return { std::min(uploaded_ever_.base_quantity(), goal), goal };
}

tr::Values::Percent Torrent::activity_percent_done() const
{
    if (is_seeding() && effective_seed_ratio().has_value()) {
        return seed_ratio_percent_done();
    }

    return percent_done();
}

} // namespace tr::app
