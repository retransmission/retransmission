// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::ranges::any_of()
#include <array>
#include <bit> // std::popcount()
#include <cstddef> // size_t
#include <cstdint>
#include <ctime> // time_t
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "libtransmission/digest.h" // tr_sha1_digest_t, tr_sha1_string
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/shared-string.h"
#include "libtransmission/types.h"
#include "libtransmission/values.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/converters.h"

namespace tr::app
{

class Prefs;

// One entry of a torrent's `files` and `file_stats` lists.
struct TorrentFile {
    std::string name;
    uint64_t size = 0; // TR_KEY_length
    uint64_t have = 0; // TR_KEY_bytes_completed
    tr_file_index_t index = 0; // position in the torrent's file list; set by Torrent, not by the session
    tr_priority_t priority = TR_PRI_NORMAL;
    bool wanted = true;

    [[nodiscard]] constexpr tr::Values::Percent progress() const noexcept
    {
        return { have, size };
    }

    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    static constexpr auto Fields = std::make_tuple(
        Field<&TorrentFile::have>{ TR_KEY_bytes_completed },
        Field<&TorrentFile::name>{ TR_KEY_name },
        Field<&TorrentFile::priority>{ TR_KEY_priority },
        Field<&TorrentFile::size>{ TR_KEY_length },
        Field<&TorrentFile::wanted>{ TR_KEY_wanted });
};

// One entry of a torrent's `peers` list.
struct Peer {
    std::string address;
    std::string client_name;
    std::string flags; // TR_KEY_flag_str
    tr::Values::Speed rate_to_client;
    tr::Values::Speed rate_to_peer;
    std::optional<uint64_t> have_total; // nullopt from older daemons
    double progress = 0.0; // see `Torrent::peer_progress()`
    size_t active_reqs_to_client = 0;
    size_t active_reqs_to_peer = 0;
    uint16_t port = 0;
    bool is_encrypted = false;

    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    static constexpr auto Fields = std::make_tuple(
        Field<&Peer::active_reqs_to_client>{ TR_KEY_active_reqs_to_client },
        Field<&Peer::active_reqs_to_peer>{ TR_KEY_active_reqs_to_peer },
        Field<&Peer::address>{ TR_KEY_address },
        Field<&Peer::client_name>{ TR_KEY_client_name },
        Field<&Peer::flags>{ TR_KEY_flag_str },
        Field<&Peer::have_total>{ TR_KEY_have_total },
        Field<&Peer::is_encrypted>{ TR_KEY_is_encrypted },
        Field<&Peer::port>{ TR_KEY_port },
        Field<&Peer::progress>{ TR_KEY_progress },
        Field<&Peer::rate_to_client>{ TR_KEY_rate_to_client },
        Field<&Peer::rate_to_peer>{ TR_KEY_rate_to_peer });
};

// One entry of a torrent's `trackers` and `tracker_stats` lists.
// `trackers` carries only the announce URL, id, sitename, and tier.
struct TrackerStat {
    tr::shared_string announce; // pooled: many torrents share a tracker
    std::string host; // "host:port", unique per tracker
    std::string last_announce_result;
    std::string last_scrape_result;
    std::string sitename;
    time_t last_announce_start_time = 0;
    time_t last_announce_time = 0;
    time_t last_scrape_start_time = 0;
    time_t last_scrape_time = 0;
    time_t next_announce_time = 0;
    time_t next_scrape_time = 0;
    int64_t last_announce_peer_count = 0;
    int64_t leecher_count = -1; // -1 if unknown
    int64_t seeder_count = -1; // -1 if unknown
    uint32_t id = 0;
    uint32_t tier = 0;
    tr_tracker_state announce_state = TR_TRACKER_INACTIVE;
    tr_tracker_state scrape_state = TR_TRACKER_INACTIVE;
    bool has_announced = false;
    bool has_scraped = false;
    bool is_backup = false;
    bool last_announce_succeeded = false;
    bool last_announce_timed_out = false;
    bool last_scrape_succeeded = false;
    bool last_scrape_timed_out = false;
    bool sitename_is_derived = false; // from `announce`; set by Torrent, not by the session

    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    static constexpr auto Fields = std::make_tuple(
        Field<&TrackerStat::announce>{ TR_KEY_announce },
        Field<&TrackerStat::announce_state>{ TR_KEY_announce_state },
        Field<&TrackerStat::has_announced>{ TR_KEY_has_announced },
        Field<&TrackerStat::has_scraped>{ TR_KEY_has_scraped },
        Field<&TrackerStat::host>{ TR_KEY_host },
        Field<&TrackerStat::id>{ TR_KEY_id },
        Field<&TrackerStat::is_backup>{ TR_KEY_is_backup },
        Field<&TrackerStat::last_announce_peer_count>{ TR_KEY_last_announce_peer_count },
        Field<&TrackerStat::last_announce_result>{ TR_KEY_last_announce_result },
        Field<&TrackerStat::last_announce_start_time>{ TR_KEY_last_announce_start_time },
        Field<&TrackerStat::last_announce_succeeded>{ TR_KEY_last_announce_succeeded },
        Field<&TrackerStat::last_announce_time>{ TR_KEY_last_announce_time },
        Field<&TrackerStat::last_announce_timed_out>{ TR_KEY_last_announce_timed_out },
        Field<&TrackerStat::last_scrape_result>{ TR_KEY_last_scrape_result },
        Field<&TrackerStat::last_scrape_start_time>{ TR_KEY_last_scrape_start_time },
        Field<&TrackerStat::last_scrape_succeeded>{ TR_KEY_last_scrape_succeeded },
        Field<&TrackerStat::last_scrape_time>{ TR_KEY_last_scrape_time },
        Field<&TrackerStat::last_scrape_timed_out>{ TR_KEY_last_scrape_timed_out },
        Field<&TrackerStat::leecher_count>{ TR_KEY_leecher_count },
        Field<&TrackerStat::next_announce_time>{ TR_KEY_next_announce_time },
        Field<&TrackerStat::next_scrape_time>{ TR_KEY_next_scrape_time },
        Field<&TrackerStat::scrape_state>{ TR_KEY_scrape_state },
        Field<&TrackerStat::seeder_count>{ TR_KEY_seeder_count },
        Field<&TrackerStat::sitename>{ TR_KEY_sitename },
        Field<&TrackerStat::tier>{ TR_KEY_tier });
};

// One entry of a torrent's `webseeds_ex` list.
struct Webseed {
    std::string url;
    tr::Values::Speed download_speed; // TR_KEY_download_bytes_per_second
    bool is_downloading = false;

    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    static constexpr auto Fields = std::make_tuple(
        Field<&Webseed::download_speed>{ TR_KEY_download_bytes_per_second },
        Field<&Webseed::is_downloading>{ TR_KEY_is_downloading },
        Field<&Webseed::url>{ TR_KEY_url });
};

class Torrent;

// A set of a `Torrent`'s fields, one bit per member: the fields an update
// changed, or a mask of the fields that a derived value or a view reads.
// `Torrent::fields()` builds one from `torrent_get` keys. Usable in constant
// expressions, so a view can keep its field-to-widget table `constexpr`.
// TODO: use std::bitset once libstdc++ 13 is the minimum; older ones lack its constexpr operations
class TorrentFieldSet
{
public:
    // one bit per member of `Torrent`, which checks that they fit
    static constexpr size_t Capacity = 64U;

    constexpr TorrentFieldSet() noexcept = default;

    [[nodiscard]] constexpr bool any() const noexcept
    {
        return std::ranges::any_of(words_, [](Word const word) { return word != 0U; });
    }

    [[nodiscard]] constexpr bool none() const noexcept
    {
        return !any();
    }

    [[nodiscard]] constexpr size_t count() const noexcept
    {
        auto n_set = size_t{};
        for (auto const word : words_) {
            n_set += static_cast<size_t>(std::popcount(word));
        }
        return n_set;
    }

    constexpr void reset() noexcept
    {
        words_ = {};
    }

    constexpr TorrentFieldSet& operator|=(TorrentFieldSet const& that) noexcept
    {
        for (size_t i = 0; i < WordCount; ++i) {
            words_[i] |= that.words_[i];
        }
        return *this;
    }

    constexpr TorrentFieldSet& operator&=(TorrentFieldSet const& that) noexcept
    {
        for (size_t i = 0; i < WordCount; ++i) {
            words_[i] &= that.words_[i];
        }
        return *this;
    }

    [[nodiscard]] friend constexpr TorrentFieldSet operator|(TorrentFieldSet lhs, TorrentFieldSet const& rhs) noexcept
    {
        return lhs |= rhs;
    }

    [[nodiscard]] friend constexpr TorrentFieldSet operator&(TorrentFieldSet lhs, TorrentFieldSet const& rhs) noexcept
    {
        return lhs &= rhs;
    }

    [[nodiscard]] constexpr bool operator==(TorrentFieldSet const& that) const noexcept = default;

private:
    friend class Torrent;

    using Word = uint64_t;

    static constexpr auto WordBits = static_cast<size_t>(std::numeric_limits<Word>::digits);
    static constexpr auto WordCount = Capacity / WordBits;

    constexpr void set(size_t const bit) noexcept
    {
        words_[bit / WordBits] |= Word{ 1U } << (bit % WordBits);
    }

    std::array<Word, WordCount> words_ = {};
};

/**
 * A client-side copy of one torrent's `torrent_get` fields.
 *
 * `update()` applies one row of a `torrent_get` response and reports which
 * fields changed, one bit per field. A field keeps its previous value until a
 * row carries its key, so a row from a small key set leaves the rest intact.
 *
 * Toolkit adapters hold one of these per torrent and convert at the view edge,
 * e.g. to a `QString` or `Glib::ustring` when `fields<TR_KEY_name>()` changes.
 */
class Torrent
{
    // The bit of each `Fields` entry: one per member, numbered in `Fields` order.
    // Defined ahead of `fields()` and `all_fields()`, which need its deduced return type.
    [[nodiscard]] static consteval auto member_bits() noexcept
    {
        using Entries = std::remove_cvref_t<decltype(Fields)>;
        constexpr auto N = std::tuple_size_v<Entries>;

        // the index of the first entry that feeds the member of entry `I`;
        // a `Field`'s type names its member, so the entries of one member share a type
        auto const first_entry = []<size_t I>(std::integral_constant<size_t, I> /*index*/) {
            auto const same = []<size_t... J>(std::index_sequence<J...> /*earlier*/) {
                return std::array<bool, I>{
                    std::is_same_v<std::tuple_element_t<J, Entries>, std::tuple_element_t<I, Entries>>...
                };
            }(std::make_index_sequence<I>{});
            return static_cast<size_t>(std::ranges::find(same, true) - std::begin(same));
        };

        auto const firsts = [&first_entry]<size_t... I>(std::index_sequence<I...> /*indices*/) {
            return std::array{ first_entry(std::integral_constant<size_t, I>{})... };
        }(std::make_index_sequence<N>{});

        auto bits = std::array<size_t, N>{};
        auto n_bits = size_t{};
        for (size_t i = 0; i < N; ++i) {
            bits[i] = firsts[i] == i ? n_bits++ : bits[firsts[i]];
        }
        return bits;
    }

public:
    using Percent = tr::Values::Percent;
    using Ratio = tr::Values::Ratio;
    using Speed = tr::Values::Speed;
    using Storage = tr::Values::Storage;

    // `prefs` supplies the session-wide seed ratio limit that
    // `effective_seed_ratio()` falls back to; see `SeedRatioPrefKeys`.
    Torrent(Prefs const& prefs, tr_torrent_id_t id) noexcept;

    // Applies one `torrent_get` row in object format and returns the fields it changed.
    // Keys this class doesn't track, and values of the wrong type, are ignored.
    TorrentFieldSet update(tr_variant::Map const& row);

    // Applies one `torrent_get` row in table format: `keys` is the response's
    // first row and `values` the torrent's own. Returns the fields it changed.
    TorrentFieldSet update(std::span<tr_quark const> keys, std::span<tr_variant const> values);

    // The fields that `Keys` feed, one bit per member: `files` and `file_stats`
    // both name the bit of `files()`, and `trackers` and `tracker_stats` both
    // name the bit of `tracker_stats()`.
    template<tr_quark... Keys>
    [[nodiscard]] static consteval TorrentFieldSet fields() noexcept
    {
        static_assert((tr::serializer::has_key<Torrent>(Keys) && ...), "a key has no entry in Torrent::Fields");

        constexpr auto Bits = member_bits();
        constexpr auto EntryKeys = std::apply([](auto const&... entry) { return std::array{ entry.key... }; }, Fields);

        auto mask = TorrentFieldSet{};
        for (size_t i = 0; i < std::size(EntryKeys); ++i) {
            if (((EntryKeys[i] == Keys) || ...)) {
                mask.set(Bits[i]);
            }
        }
        return mask;
    }

    [[nodiscard]] static consteval TorrentFieldSet all_fields() noexcept
    {
        auto mask = TorrentFieldSet{};
        for (auto const bit : member_bits()) {
            mask.set(bit);
        }
        return mask;
    }

    // --- identity

    [[nodiscard]] constexpr auto id() const noexcept
    {
        return id_;
    }

    [[nodiscard]] constexpr auto const& hash() const noexcept
    {
        return hash_;
    }

    [[nodiscard]] constexpr auto hash_string() const noexcept
    {
        return tr_sha1_string{ hash_ };
    }

    [[nodiscard]] constexpr auto const& name() const noexcept
    {
        return name_;
    }

    // false until a row carries `name`; the session names a magnet by its `dn` or info hash
    [[nodiscard]] constexpr bool has_name() const noexcept
    {
        return !std::empty(name_);
    }

    [[nodiscard]] constexpr auto const& comment() const noexcept
    {
        return comment_;
    }

    [[nodiscard]] constexpr auto const& creator() const noexcept
    {
        return creator_;
    }

    [[nodiscard]] constexpr auto date_created() const noexcept
    {
        return date_created_;
    }

    [[nodiscard]] constexpr auto const& download_dir() const noexcept
    {
        return download_dir_;
    }

    [[nodiscard]] constexpr auto const& labels() const noexcept
    {
        return labels_;
    }

    [[nodiscard]] constexpr auto const& primary_mime_type() const noexcept
    {
        return primary_mime_type_;
    }

    [[nodiscard]] constexpr bool is_folder() const noexcept
    {
        return is_folder_;
    }

    [[nodiscard]] constexpr bool is_private() const noexcept
    {
        return is_private_;
    }

    // --- contents

    [[nodiscard]] constexpr auto const& files() const noexcept
    {
        return files_;
    }

    [[nodiscard]] constexpr auto file_count() const noexcept
    {
        return file_count_;
    }

    [[nodiscard]] constexpr auto piece_count() const noexcept
    {
        return piece_count_;
    }

    [[nodiscard]] constexpr auto piece_size() const noexcept
    {
        return piece_size_;
    }

    [[nodiscard]] constexpr auto total_size() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] constexpr auto size_when_done() const noexcept
    {
        return size_when_done_;
    }

    [[nodiscard]] constexpr auto left_until_done() const noexcept
    {
        return left_until_done_;
    }

    [[nodiscard]] constexpr bool is_done() const noexcept
    {
        return left_until_done_.is_zero();
    }

    [[nodiscard]] constexpr auto have_valid() const noexcept
    {
        return have_valid_;
    }

    [[nodiscard]] constexpr auto have_unchecked() const noexcept
    {
        return have_unchecked_;
    }

    [[nodiscard]] static consteval TorrentFieldSet have_total_fields() noexcept
    {
        return fields<TR_KEY_have_valid, TR_KEY_have_unchecked>();
    }

    [[nodiscard]] constexpr auto have_total() const noexcept
    {
        return have_valid_ + have_unchecked_;
    }

    [[nodiscard]] static consteval TorrentFieldSet is_seed_fields() noexcept
    {
        return fields<TR_KEY_have_valid, TR_KEY_total_size>();
    }

    // true iff every piece is present, not just the wanted ones
    [[nodiscard]] constexpr bool is_seed() const noexcept
    {
        return have_valid_ >= total_size_;
    }

    [[nodiscard]] constexpr auto desired_available() const noexcept
    {
        return desired_available_;
    }

    [[nodiscard]] constexpr auto downloaded_ever() const noexcept
    {
        return downloaded_ever_;
    }

    [[nodiscard]] constexpr auto uploaded_ever() const noexcept
    {
        return uploaded_ever_;
    }

    [[nodiscard]] constexpr auto corrupt_ever() const noexcept
    {
        return corrupt_ever_;
    }

    // --- progress

    [[nodiscard]] static consteval TorrentFieldSet metadata_percent_complete_fields() noexcept
    {
        return fields<TR_KEY_metadata_have, TR_KEY_metadata_percent_complete, TR_KEY_metadata_size>();
    }

    // `Percent{}` until a peer reports the metadata's size
    [[nodiscard]] Percent metadata_percent_complete() const noexcept;

    [[nodiscard]] bool has_metadata() const noexcept
    {
        return metadata_percent_complete().is_complete();
    }

    // The percents built from byte counts are `Percent{}` until the torrent has metadata.

    [[nodiscard]] static consteval TorrentFieldSet percent_complete_fields() noexcept
    {
        return metadata_percent_complete_fields() | have_total_fields() | fields<TR_KEY_total_size>();
    }

    [[nodiscard]] Percent percent_complete() const noexcept;

    [[nodiscard]] static consteval TorrentFieldSet percent_done_fields() noexcept
    {
        return metadata_percent_complete_fields() | fields<TR_KEY_left_until_done, TR_KEY_size_when_done>();
    }

    [[nodiscard]] Percent percent_done() const noexcept;

    [[nodiscard]] static consteval TorrentFieldSet recheck_progress_fields() noexcept
    {
        return metadata_percent_complete_fields() | fields<TR_KEY_checked_bytes, TR_KEY_recheck_progress, TR_KEY_total_size>();
    }

    [[nodiscard]] Percent recheck_progress() const noexcept;

    [[nodiscard]] static consteval TorrentFieldSet peer_progress_fields() noexcept
    {
        return metadata_percent_complete_fields() | fields<TR_KEY_peers, TR_KEY_total_size>();
    }

    [[nodiscard]] Percent peer_progress(Peer const& peer) const noexcept;

    // The daemon's own ratio.
    // 2.x and 3.x daemons divide by bytes downloaded, later ones by `size_when_done`.
    [[nodiscard]] Ratio upload_ratio() const noexcept
    {
        return Ratio::from_double(upload_ratio_);
    }

    // --- activity

    [[nodiscard]] constexpr auto activity() const noexcept
    {
        return activity_;
    }

    [[nodiscard]] constexpr bool is_paused() const noexcept
    {
        return activity_ == TR_STATUS_STOPPED;
    }

    [[nodiscard]] constexpr bool is_waiting_to_verify() const noexcept
    {
        return activity_ == TR_STATUS_CHECK_WAIT;
    }

    [[nodiscard]] constexpr bool is_verifying() const noexcept
    {
        return activity_ == TR_STATUS_CHECK;
    }

    [[nodiscard]] constexpr bool is_waiting_to_download() const noexcept
    {
        return activity_ == TR_STATUS_DOWNLOAD_WAIT;
    }

    [[nodiscard]] constexpr bool is_downloading() const noexcept
    {
        return activity_ == TR_STATUS_DOWNLOAD;
    }

    [[nodiscard]] constexpr bool is_waiting_to_seed() const noexcept
    {
        return activity_ == TR_STATUS_SEED_WAIT;
    }

    [[nodiscard]] constexpr bool is_seeding() const noexcept
    {
        return activity_ == TR_STATUS_SEED;
    }

    [[nodiscard]] constexpr bool is_queued() const noexcept
    {
        return is_waiting_to_download() || is_waiting_to_seed();
    }

    [[nodiscard]] constexpr bool is_ready_to_transfer() const noexcept
    {
        return is_downloading() || is_seeding();
    }

    // true once the torrent has met its seed ratio or idle limit
    [[nodiscard]] constexpr bool is_finished() const noexcept
    {
        return is_finished_;
    }

    [[nodiscard]] constexpr bool is_stalled() const noexcept
    {
        return is_stalled_;
    }

    [[nodiscard]] constexpr auto error_code() const noexcept
    {
        return error_code_;
    }

    [[nodiscard]] constexpr bool has_error() const noexcept
    {
        return error_code_ != tr_stat::Error::Ok;
    }

    [[nodiscard]] constexpr auto const& error_string() const noexcept
    {
        return error_string_;
    }

    // seconds remaining, or TR_ETA_NOT_AVAIL / TR_ETA_UNKNOWN
    [[nodiscard]] constexpr auto eta() const noexcept
    {
        return eta_;
    }

    [[nodiscard]] constexpr bool has_eta() const noexcept
    {
        return eta_ >= 0;
    }

    [[nodiscard]] constexpr auto queue_position() const noexcept
    {
        return queue_position_;
    }

    [[nodiscard]] constexpr auto bandwidth_priority() const noexcept
    {
        return bandwidth_priority_;
    }

    // --- dates

    [[nodiscard]] constexpr auto activity_date() const noexcept
    {
        return activity_date_;
    }

    [[nodiscard]] constexpr auto added_date() const noexcept
    {
        return added_date_;
    }

    [[nodiscard]] constexpr auto edit_date() const noexcept
    {
        return edit_date_;
    }

    [[nodiscard]] constexpr auto start_date() const noexcept
    {
        return start_date_;
    }

    [[nodiscard]] constexpr auto manual_announce_time() const noexcept
    {
        return manual_announce_time_;
    }

    [[nodiscard]] static consteval TorrentFieldSet can_manual_announce_fields() noexcept
    {
        return fields<TR_KEY_status, TR_KEY_manual_announce_time>();
    }

    [[nodiscard]] constexpr bool can_manual_announce_at(time_t const now) const noexcept
    {
        return is_ready_to_transfer() && manual_announce_time_ <= now;
    }

    // --- peers

    [[nodiscard]] constexpr auto const& peers() const noexcept
    {
        return peers_;
    }

    [[nodiscard]] constexpr auto peers_connected() const noexcept
    {
        return peers_connected_;
    }

    [[nodiscard]] constexpr auto peers_getting_from_us() const noexcept
    {
        return peers_getting_from_us_;
    }

    [[nodiscard]] constexpr auto peers_sending_to_us() const noexcept
    {
        return peers_sending_to_us_;
    }

    [[nodiscard]] constexpr auto webseeds_sending_to_us() const noexcept
    {
        return webseeds_sending_to_us_;
    }

    [[nodiscard]] static consteval TorrentFieldSet active_peers_down_fields() noexcept
    {
        return fields<TR_KEY_peers_sending_to_us, TR_KEY_webseeds_sending_to_us>();
    }

    // peers and webseeds we're downloading from
    [[nodiscard]] constexpr unsigned active_peers_down() const noexcept
    {
        return peers_sending_to_us_ + webseeds_sending_to_us_;
    }

    [[nodiscard]] static consteval TorrentFieldSet active_peer_count_fields() noexcept
    {
        return active_peers_down_fields() | fields<TR_KEY_peers_getting_from_us>();
    }

    [[nodiscard]] constexpr unsigned active_peer_count() const noexcept
    {
        return active_peers_down() + peers_getting_from_us_;
    }

    [[nodiscard]] constexpr auto peer_limit() const noexcept
    {
        return peer_limit_;
    }

    [[nodiscard]] constexpr auto const& webseeds() const noexcept
    {
        return webseeds_;
    }

    // --- trackers

    [[nodiscard]] constexpr auto const& tracker_stats() const noexcept
    {
        return tracker_stats_;
    }

    // the announce list in `tracker_list` format: one URL per line, tiers separated by blank lines
    [[nodiscard]] constexpr auto const& tracker_list() const noexcept
    {
        return tracker_list_;
    }

    // the distinct sitenames of `tracker_stats()`, sorted
    [[nodiscard]] constexpr auto const& sitenames() const noexcept
    {
        return sitenames_;
    }

    [[nodiscard]] bool includes_tracker(std::string_view sitename) const noexcept;

    // --- speeds and limits

    [[nodiscard]] constexpr auto download_speed() const noexcept
    {
        return download_speed_;
    }

    [[nodiscard]] constexpr auto upload_speed() const noexcept
    {
        return upload_speed_;
    }

    [[nodiscard]] constexpr auto download_limit() const noexcept
    {
        return Speed{ download_limit_kbyps_, Speed::Units::KByps };
    }

    [[nodiscard]] constexpr auto upload_limit() const noexcept
    {
        return Speed{ upload_limit_kbyps_, Speed::Units::KByps };
    }

    [[nodiscard]] constexpr bool download_limited() const noexcept
    {
        return download_limited_;
    }

    [[nodiscard]] constexpr bool upload_limited() const noexcept
    {
        return upload_limited_;
    }

    [[nodiscard]] constexpr bool honors_session_limits() const noexcept
    {
        return honors_session_limits_;
    }

    // --- seeding limits

    [[nodiscard]] constexpr auto seed_idle_mode() const noexcept
    {
        return seed_idle_mode_;
    }

    // minutes; applies when `seed_idle_mode()` is TR_IDLELIMIT_SINGLE
    [[nodiscard]] constexpr auto seed_idle_limit() const noexcept
    {
        return seed_idle_limit_;
    }

    [[nodiscard]] constexpr auto seed_ratio_mode() const noexcept
    {
        return seed_ratio_mode_;
    }

    // the torrent's own limit; applies when `seed_ratio_mode()` is TR_RATIOLIMIT_SINGLE
    [[nodiscard]] constexpr auto seed_ratio_limit() const noexcept
    {
        return seed_ratio_limit_;
    }

    // The prefs that `effective_seed_ratio()` and the values built on it read.
    // A change to one of these is not reported through `update()`.
    static constexpr auto SeedRatioPrefKeys = std::array{ TR_KEY_seed_ratio_limited, TR_KEY_seed_ratio_limit };

    [[nodiscard]] static consteval TorrentFieldSet effective_seed_ratio_fields() noexcept
    {
        return fields<TR_KEY_seed_ratio_mode, TR_KEY_seed_ratio_limit>();
    }

    // The seed ratio that applies to this torrent: its own, the session's, or none.
    [[nodiscard]] std::optional<double> effective_seed_ratio() const;

    [[nodiscard]] static consteval TorrentFieldSet seed_ratio_percent_done_fields() noexcept
    {
        return effective_seed_ratio_fields() | fields<TR_KEY_is_finished, TR_KEY_left_until_done, TR_KEY_upload_ratio>();
    }

    // Progress toward the effective seed ratio: `upload_ratio` over the limit,
    // the ratio the session itself stops at. It's complete unless the torrent is
    // done, unfinished, and has a nonzero ratio to meet. `torrent_get` doesn't
    // report this, so it's computed here.
    [[nodiscard]] Percent seed_ratio_percent_done() const;

    [[nodiscard]] static consteval TorrentFieldSet activity_percent_done_fields() noexcept
    {
        return seed_ratio_percent_done_fields() | percent_done_fields() | fields<TR_KEY_status>();
    }

    // Progress of the current activity: `seed_ratio_percent_done()` while seeding
    // toward a ratio, `metadata_percent_complete()` until a magnet has its
    // metadata, else `percent_done()`.
    [[nodiscard]] Percent activity_percent_done() const;

private:
    [[nodiscard]] Percent progress_of(uint64_t have, uint64_t total) const noexcept;

    void set_field(tr_quark key, tr_variant const& value, TorrentFieldSet& changed);
    void finish_update(TorrentFieldSet const& changed);

    Prefs const& prefs_;
    tr_torrent_id_t const id_;

    std::vector<Peer> peers_;
    std::vector<TorrentFile> files_;
    std::vector<TrackerStat> tracker_stats_;
    std::vector<Webseed> webseeds_;
    std::vector<std::string> labels_;
    std::vector<std::string> sitenames_;

    std::string name_;
    std::string primary_mime_type_;
    std::string tracker_list_;

    // pooled: many torrents share the same value
    tr::shared_string comment_;
    tr::shared_string creator_;
    tr::shared_string download_dir_;
    tr::shared_string error_string_;

    tr_sha1_digest_t hash_ = {};

    Storage corrupt_ever_;
    Storage desired_available_;
    Storage downloaded_ever_;
    Storage have_unchecked_;
    Storage have_valid_;
    Storage left_until_done_;
    Storage size_when_done_;
    Storage total_size_;
    Storage uploaded_ever_;

    Speed download_speed_;
    Speed upload_speed_;

    // `torrent_get` reports the limits in KB/s; the getters build the Speeds
    uint64_t download_limit_kbyps_ = 0;
    uint64_t upload_limit_kbyps_ = 0;

    // daemons without these keys send only `metadata_percent_complete` and `recheck_progress`
    std::optional<uint64_t> checked_bytes_;
    std::optional<uint64_t> metadata_have_;
    std::optional<uint64_t> metadata_size_;

    double metadata_percent_complete_ = 0.0;
    double recheck_progress_ = 0.0;
    double seed_ratio_limit_ = 0.0;
    double upload_ratio_ = 0.0;

    time_t activity_date_ = 0;
    time_t added_date_ = 0;
    time_t date_created_ = 0;
    time_t edit_date_ = 0;
    time_t eta_ = TR_ETA_NOT_AVAIL;
    time_t manual_announce_time_ = 0;
    time_t start_date_ = 0;

    size_t queue_position_ = 0;
    tr_file_index_t file_count_ = 0;
    tr_piece_index_t piece_count_ = 0;
    uint32_t piece_size_ = 0;

    uint16_t peer_limit_ = 0;
    uint16_t peers_connected_ = 0;
    uint16_t peers_getting_from_us_ = 0;
    uint16_t peers_sending_to_us_ = 0;
    uint16_t seed_idle_limit_ = 0;
    uint16_t webseeds_sending_to_us_ = 0;

    tr_torrent_activity activity_ = TR_STATUS_STOPPED;
    tr_stat::Error error_code_ = tr_stat::Error::Ok;
    tr_idlelimit seed_idle_mode_ = TR_IDLELIMIT_GLOBAL;
    tr_priority_t bandwidth_priority_ = TR_PRI_NORMAL;
    tr_ratiolimit seed_ratio_mode_ = TR_RATIOLIMIT_GLOBAL;

    bool download_limited_ = false;
    bool honors_session_limits_ = true;
    bool is_finished_ = false;
    bool is_folder_ = false;
    bool is_private_ = false;
    bool is_stalled_ = false;
    bool upload_limited_ = false;

public:
    // The `torrent_get` key that feeds each member, for `tr::serializer`.
    // Two keys may feed one member; see `fields()`.
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    static constexpr auto Fields = std::make_tuple(
        Field<&Torrent::activity_date_>{ TR_KEY_activity_date },
        Field<&Torrent::added_date_>{ TR_KEY_added_date },
        Field<&Torrent::bandwidth_priority_>{ TR_KEY_bandwidth_priority },
        Field<&Torrent::checked_bytes_>{ TR_KEY_checked_bytes },
        Field<&Torrent::comment_>{ TR_KEY_comment },
        Field<&Torrent::corrupt_ever_>{ TR_KEY_corrupt_ever },
        Field<&Torrent::creator_>{ TR_KEY_creator },
        Field<&Torrent::date_created_>{ TR_KEY_date_created },
        Field<&Torrent::desired_available_>{ TR_KEY_desired_available },
        Field<&Torrent::download_dir_>{ TR_KEY_download_dir },
        Field<&Torrent::download_limit_kbyps_>{ TR_KEY_download_limit },
        Field<&Torrent::download_limited_>{ TR_KEY_download_limited },
        Field<&Torrent::downloaded_ever_>{ TR_KEY_downloaded_ever },
        Field<&Torrent::edit_date_>{ TR_KEY_edit_date },
        Field<&Torrent::error_code_>{ TR_KEY_error },
        Field<&Torrent::error_string_>{ TR_KEY_error_string },
        Field<&Torrent::eta_>{ TR_KEY_eta },
        Field<&Torrent::file_count_>{ TR_KEY_file_count },
        Field<&Torrent::files_>{ TR_KEY_file_stats },
        Field<&Torrent::files_>{ TR_KEY_files },
        Field<&Torrent::hash_>{ TR_KEY_hash_string },
        Field<&Torrent::have_unchecked_>{ TR_KEY_have_unchecked },
        Field<&Torrent::have_valid_>{ TR_KEY_have_valid },
        Field<&Torrent::honors_session_limits_>{ TR_KEY_honors_session_limits },
        Field<&Torrent::is_finished_>{ TR_KEY_is_finished },
        Field<&Torrent::is_folder_>{ TR_KEY_is_folder },
        Field<&Torrent::is_private_>{ TR_KEY_is_private },
        Field<&Torrent::is_stalled_>{ TR_KEY_is_stalled },
        Field<&Torrent::labels_>{ TR_KEY_labels },
        Field<&Torrent::left_until_done_>{ TR_KEY_left_until_done },
        Field<&Torrent::manual_announce_time_>{ TR_KEY_manual_announce_time },
        Field<&Torrent::metadata_have_>{ TR_KEY_metadata_have },
        Field<&Torrent::metadata_percent_complete_>{ TR_KEY_metadata_percent_complete },
        Field<&Torrent::metadata_size_>{ TR_KEY_metadata_size },
        Field<&Torrent::name_>{ TR_KEY_name },
        Field<&Torrent::peer_limit_>{ TR_KEY_peer_limit },
        Field<&Torrent::peers_>{ TR_KEY_peers },
        Field<&Torrent::peers_connected_>{ TR_KEY_peers_connected },
        Field<&Torrent::peers_getting_from_us_>{ TR_KEY_peers_getting_from_us },
        Field<&Torrent::peers_sending_to_us_>{ TR_KEY_peers_sending_to_us },
        Field<&Torrent::piece_count_>{ TR_KEY_piece_count },
        Field<&Torrent::piece_size_>{ TR_KEY_piece_size },
        Field<&Torrent::primary_mime_type_>{ TR_KEY_primary_mime_type },
        Field<&Torrent::queue_position_>{ TR_KEY_queue_position },
        Field<&Torrent::download_speed_>{ TR_KEY_rate_download },
        Field<&Torrent::upload_speed_>{ TR_KEY_rate_upload },
        Field<&Torrent::recheck_progress_>{ TR_KEY_recheck_progress },
        Field<&Torrent::seed_idle_limit_>{ TR_KEY_seed_idle_limit },
        Field<&Torrent::seed_idle_mode_>{ TR_KEY_seed_idle_mode },
        Field<&Torrent::seed_ratio_limit_>{ TR_KEY_seed_ratio_limit },
        Field<&Torrent::seed_ratio_mode_>{ TR_KEY_seed_ratio_mode },
        Field<&Torrent::size_when_done_>{ TR_KEY_size_when_done },
        Field<&Torrent::start_date_>{ TR_KEY_start_date },
        Field<&Torrent::activity_>{ TR_KEY_status },
        Field<&Torrent::total_size_>{ TR_KEY_total_size },
        Field<&Torrent::tracker_list_>{ TR_KEY_tracker_list },
        Field<&Torrent::tracker_stats_>{ TR_KEY_tracker_stats },
        Field<&Torrent::tracker_stats_>{ TR_KEY_trackers },
        Field<&Torrent::upload_limit_kbyps_>{ TR_KEY_upload_limit },
        Field<&Torrent::upload_limited_>{ TR_KEY_upload_limited },
        Field<&Torrent::upload_ratio_>{ TR_KEY_upload_ratio },
        Field<&Torrent::uploaded_ever_>{ TR_KEY_uploaded_ever },
        Field<&Torrent::webseeds_>{ TR_KEY_webseeds_ex },
        Field<&Torrent::webseeds_sending_to_us_>{ TR_KEY_webseeds_sending_to_us });
};

} // namespace tr::app
