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

std::optional<TorrentField> Torrent::field(tr_quark const key) noexcept
{
    switch (key) {
    case TR_KEY_activity_date:
        return TorrentField::ACTIVITY_DATE;
    case TR_KEY_added_date:
        return TorrentField::ADDED_DATE;
    case TR_KEY_bandwidth_priority:
        return TorrentField::BANDWIDTH_PRIORITY;
    case TR_KEY_checked_bytes:
        return TorrentField::CHECKED_BYTES;
    case TR_KEY_comment:
        return TorrentField::COMMENT;
    case TR_KEY_corrupt_ever:
        return TorrentField::CORRUPT_EVER;
    case TR_KEY_creator:
        return TorrentField::CREATOR;
    case TR_KEY_date_created:
        return TorrentField::DATE_CREATED;
    case TR_KEY_desired_available:
        return TorrentField::DESIRED_AVAILABLE;
    case TR_KEY_download_dir:
        return TorrentField::DOWNLOAD_DIR;
    case TR_KEY_download_limit:
        return TorrentField::DOWNLOAD_LIMIT;
    case TR_KEY_download_limited:
        return TorrentField::DOWNLOAD_LIMITED;
    case TR_KEY_downloaded_ever:
        return TorrentField::DOWNLOADED_EVER;
    case TR_KEY_edit_date:
        return TorrentField::EDIT_DATE;
    case TR_KEY_error:
        return TorrentField::ERROR_CODE;
    case TR_KEY_error_string:
        return TorrentField::ERROR_STRING;
    case TR_KEY_eta:
        return TorrentField::ETA;
    case TR_KEY_file_count:
        return TorrentField::FILE_COUNT;
    case TR_KEY_file_stats:
    case TR_KEY_files:
        return TorrentField::FILES;
    case TR_KEY_hash_string:
        return TorrentField::HASH;
    case TR_KEY_have_unchecked:
        return TorrentField::HAVE_UNCHECKED;
    case TR_KEY_have_valid:
        return TorrentField::HAVE_VALID;
    case TR_KEY_honors_session_limits:
        return TorrentField::HONORS_SESSION_LIMITS;
    case TR_KEY_is_finished:
        return TorrentField::IS_FINISHED;
    case TR_KEY_is_folder:
        return TorrentField::IS_FOLDER;
    case TR_KEY_is_private:
        return TorrentField::IS_PRIVATE;
    case TR_KEY_is_stalled:
        return TorrentField::IS_STALLED;
    case TR_KEY_labels:
        return TorrentField::LABELS;
    case TR_KEY_left_until_done:
        return TorrentField::LEFT_UNTIL_DONE;
    case TR_KEY_manual_announce_time:
        return TorrentField::MANUAL_ANNOUNCE_TIME;
    case TR_KEY_metadata_have:
        return TorrentField::METADATA_HAVE;
    case TR_KEY_metadata_percent_complete:
        return TorrentField::METADATA_PERCENT_COMPLETE;
    case TR_KEY_metadata_size:
        return TorrentField::METADATA_SIZE;
    case TR_KEY_name:
        return TorrentField::NAME;
    case TR_KEY_peer_limit:
        return TorrentField::PEER_LIMIT;
    case TR_KEY_peers:
        return TorrentField::PEERS;
    case TR_KEY_peers_connected:
        return TorrentField::PEERS_CONNECTED;
    case TR_KEY_peers_getting_from_us:
        return TorrentField::PEERS_GETTING_FROM_US;
    case TR_KEY_peers_sending_to_us:
        return TorrentField::PEERS_SENDING_TO_US;
    case TR_KEY_piece_count:
        return TorrentField::PIECE_COUNT;
    case TR_KEY_piece_size:
        return TorrentField::PIECE_SIZE;
    case TR_KEY_primary_mime_type:
        return TorrentField::PRIMARY_MIME_TYPE;
    case TR_KEY_queue_position:
        return TorrentField::QUEUE_POSITION;
    case TR_KEY_rate_download:
        return TorrentField::DOWNLOAD_SPEED;
    case TR_KEY_rate_upload:
        return TorrentField::UPLOAD_SPEED;
    case TR_KEY_recheck_progress:
        return TorrentField::RECHECK_PROGRESS;
    case TR_KEY_seed_idle_limit:
        return TorrentField::SEED_IDLE_LIMIT;
    case TR_KEY_seed_idle_mode:
        return TorrentField::SEED_IDLE_MODE;
    case TR_KEY_seed_ratio_limit:
        return TorrentField::SEED_RATIO_LIMIT;
    case TR_KEY_seed_ratio_mode:
        return TorrentField::SEED_RATIO_MODE;
    case TR_KEY_size_when_done:
        return TorrentField::SIZE_WHEN_DONE;
    case TR_KEY_start_date:
        return TorrentField::START_DATE;
    case TR_KEY_status:
        return TorrentField::STATUS;
    case TR_KEY_total_size:
        return TorrentField::TOTAL_SIZE;
    case TR_KEY_tracker_list:
        return TorrentField::TRACKER_LIST;
    case TR_KEY_tracker_stats:
    case TR_KEY_trackers:
        return TorrentField::TRACKER_STATS;
    case TR_KEY_upload_limit:
        return TorrentField::UPLOAD_LIMIT;
    case TR_KEY_upload_limited:
        return TorrentField::UPLOAD_LIMITED;
    case TR_KEY_upload_ratio:
        return TorrentField::UPLOAD_RATIO;
    case TR_KEY_uploaded_ever:
        return TorrentField::UPLOADED_EVER;
    case TR_KEY_webseeds_ex:
        return TorrentField::WEBSEEDS;
    case TR_KEY_webseeds_sending_to_us:
        return TorrentField::WEBSEEDS_SENDING_TO_US;
    default:
        return {};
    }
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

void Torrent::set_field(tr_quark const key, tr_variant const& value, TorrentFieldSet& changed)
{
    // `field()` is a jump table, so it screens untracked keys before the
    // serializer's walk through `Fields`.
    if (auto const bit = field(key); bit && tr::serializer::set_from_variant(key, value, *this)) {
        changed.set(*bit);
    }
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
