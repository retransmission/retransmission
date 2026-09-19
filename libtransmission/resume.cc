// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::min, std::ranges::adjacent_find, std::ranges::sort
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/api-compat.h"
#include "libtransmission/bitfield.h"
#include "libtransmission/converters.h"
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-mgr.h" /* pex */
#include "libtransmission/quark.h"
#include "libtransmission/resume.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;
using namespace tr::Values;

namespace tr_resume
{
namespace
{
constexpr auto MaxRememberedPeers = 200U;

[[nodiscard]] std::optional<std::string_view> nonempty(std::optional<std::string_view> const sv)
{
    return sv && !std::empty(*sv) ? sv : std::nullopt;
}

// ---

// Builds a list with one entry, `fn(file_index)`, per file.
template<typename Fn>
[[nodiscard]] tr_variant::Vector per_file_list(tr_torrent const* const tor, Fn const& fn)
{
    auto const n_files = tor->file_count();
    auto list = tr_variant::Vector{};
    list.reserve(n_files);
    for (tr_file_index_t i = 0; i < n_files; ++i) {
        list.emplace_back(fn(i));
    }
    return list;
}

// Calls `fn(file_index, entry)` for each of the torrent's files, in file order.
// `entry` points to the file's entry in the saved per-file `list`,
// or is nullptr if the list has no entry for that file.
// Returns false, without calling `fn`, if the list can't be paired up with the files.
template<typename Fn>
[[nodiscard]] bool for_each_file_entry(tr_torrent const* const tor, tr_variant::Vector const& list, Fn const& fn)
{
    auto const n_files = tor->file_count();
    auto const n_list = std::size(list);

    // Resume files written before zero-length files were part of a torrent's
    // file list are short by exactly those files, so each entry after an
    // omitted one sits at a lower position in the list than its file index.
    // Skipping the zero-length files while walking the list realigns them.
    auto const skip_empty_files = n_list != n_files;

    if (skip_empty_files) {
        auto n_empty_files = size_t{};
        for (tr_file_index_t i = 0; i < n_files; ++i) {
            if (tor->file_size(i) == 0U) {
                ++n_empty_files;
            }
        }

        // A list of any other length can't be paired up with the files, so none
        // of it can be applied: a partial mapping would give file indices other
        // files' settings.
        if (n_list + n_empty_files != n_files) {
            return false;
        }
    }

    auto pos = size_t{};
    for (tr_file_index_t i = 0; i < n_files; ++i) {
        auto const has_entry = !skip_empty_files || tor->file_size(i) != 0U;
        fn(i, has_entry ? &list[pos++] : nullptr);
    }

    return true;
}

// ---

void save_peers(tr_variant::Map& map, tr_torrent const* const tor)
{
    if (auto const pex = tr_peerMgrGetPeers(tor, TR_AF_INET, TR_PEERS_INTERESTING, MaxRememberedPeers); !std::empty(pex)) {
        map.insert_or_assign(TR_KEY_peers2, tr::serializer::to_variant(pex));
    }

    if (auto const pex = tr_peerMgrGetPeers(tor, TR_AF_INET6, TR_PEERS_INTERESTING, MaxRememberedPeers); !std::empty(pex)) {
        map.insert_or_assign(TR_KEY_peers2_6, tr::serializer::to_variant(pex));
    }
}

size_t add_peers(tr_torrent* const tor, tr_variant::Vector const& l)
{
    auto const n_pex = std::min(std::size(l), size_t{ MaxRememberedPeers });
    auto pex = std::vector<tr_pex>{};
    pex.reserve(n_pex);
    for (size_t i = 0; i < n_pex; ++i) {
        if (auto p = tr::serializer::to_value<tr_pex>(l[i])) {
            pex.emplace_back(std::move(*p));
        }
    }
    return tr_peerMgrAddPex(tor, TR_PEER_FROM_RESUME, pex);
}

[[nodiscard]] fields_t load_peers(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto ret = fields_t{};

    if (auto const* const l = map.find_if<tr_variant::Vector>(TR_KEY_peers2)) {
        auto const num_added = add_peers(tor, *l);
        tr_logAddTraceTor(tor, fmt::format("Loaded {} IPv4 peers from resume file", num_added));
        ret = Peers;
    }

    if (auto const* const l = map.find_if<tr_variant::Vector>(TR_KEY_peers2_6)) {
        auto const num_added = add_peers(tor, *l);
        tr_logAddTraceTor(tor, fmt::format("Loaded {} IPv6 peers from resume file", num_added));
        ret = Peers;
    }

    return ret;
}

// ---

void save_labels(tr_variant::Map& map, tr_torrent const* const tor)
{
    auto const& labels = tor->labels();
    auto list = tr_variant::Vector{};
    list.reserve(std::size(labels));
    for (auto const& label : labels) {
        list.emplace_back(tr_variant::unmanaged_string(label.sv()));
    }
    map.insert_or_assign(TR_KEY_labels, std::move(list));
}

[[nodiscard]] fields_t load_labels(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const list = map.find_if<tr_variant::Vector>(TR_KEY_labels);
    if (list == nullptr) {
        return {};
    }

    auto labels = tr_labels_t{};
    labels.reserve(std::size(*list));
    for (auto const& var : *list) {
        if (auto const sv = nonempty(var.value_if<std::string_view>())) {
            labels.emplace_back(*sv);
        }
    }

    tor->set_labels(labels);
    return Labels;
}

// ---

void save_group(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_group, tr_variant::unmanaged_string(tor->bandwidth_group().sv()));
}

[[nodiscard]] fields_t load_group(tr_variant::Map const& map, tr_torrent* const tor)
{
    if (auto const sv = nonempty(map.value_if<std::string_view>(TR_KEY_group))) {
        tor->set_bandwidth_group(*sv);
        return Group;
    }

    return {};
}

// ---

void save_dnd(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_dnd, per_file_list(tor, [tor](tr_file_index_t const i) { return !tor->file_is_wanted(i); }));
}

[[nodiscard]] fields_t load_dnd(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const list = map.find_if<tr_variant::Vector>(TR_KEY_dnd);
    if (list == nullptr) {
        tr_logAddDebugTor(tor, "Couldn't load DND flags.");
        return {};
    }

    auto const n_files = tor->file_count();
    auto wanted = std::vector<tr_file_index_t>{};
    auto unwanted = std::vector<tr_file_index_t>{};
    wanted.reserve(n_files);
    unwanted.reserve(n_files);

    // A file with no entry is wanted: it is zero-length, so there is no
    // download to opt out of, and that is what a fresh torrent gives it.
    auto const sort_file = [&wanted, &unwanted](tr_file_index_t const i, tr_variant const* const entry) {
        auto const dnd = entry != nullptr && entry->value_if<bool>().value_or(false);
        (dnd ? unwanted : wanted).push_back(i);
    };

    if (!for_each_file_entry(tor, *list, sort_file)) {
        return {};
    }

    tor->init_files_wanted(unwanted, false);
    tor->init_files_wanted(wanted, true);

    return Dnd;
}

// ---

void save_file_priorities(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_priority, per_file_list(tor, [tor](tr_file_index_t const i) { return tor->file_priority(i); }));
}

[[nodiscard]] fields_t load_file_priorities(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const list = map.find_if<tr_variant::Vector>(TR_KEY_priority);
    if (list == nullptr) {
        return {};
    }

    // A file with no entry keeps the priority a fresh torrent gives it.
    auto const set_priority = [tor](tr_file_index_t const i, tr_variant const* const entry) {
        if (entry == nullptr) {
            return;
        }

        if (auto const priority = entry->value_if<int64_t>()) {
            tor->set_file_priority(i, static_cast<tr_priority_t>(*priority));
        }
    };

    return for_each_file_entry(tor, *list, set_priority) ? FilePriorities : fields_t{};
}

// ---

[[nodiscard]] tr_variant::Map save_single_speed_limit(tr_torrent const* const tor, tr_direction const dir)
{
    auto map = tr_variant::Map{ 3 };
    map.try_emplace(TR_KEY_speed_Bps, tor->speed_limit(dir).base_quantity());
    map.try_emplace(TR_KEY_use_global_speed_limit, tor->uses_session_limits());
    map.try_emplace(TR_KEY_use_speed_limit, tor->uses_speed_limit(dir));
    return map;
}

void save_speed_limits(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_speed_limit_down, save_single_speed_limit(tor, tr_direction::Down));
    map.insert_or_assign(TR_KEY_speed_limit_up, save_single_speed_limit(tor, tr_direction::Up));
}

void save_ratio_limits(tr_variant::Map& map, tr_torrent const* const tor)
{
    auto d = tr_variant::Map{ 2 };
    d.try_emplace(TR_KEY_seed_ratio_limit, tor->seed_ratio());
    d.try_emplace(TR_KEY_ratio_mode, tor->seed_ratio_mode());
    map.insert_or_assign(TR_KEY_seed_ratio_limit, std::move(d));
}

void save_idle_limits(tr_variant::Map& map, tr_torrent const* const tor)
{
    auto d = tr_variant::Map{ 2 };
    d.try_emplace(TR_KEY_idle_limit, tor->idle_limit_minutes());
    d.try_emplace(TR_KEY_idle_mode, tor->idle_limit_mode());
    map.insert_or_assign(TR_KEY_idle_limit, std::move(d));
}

void load_single_speed_limit(tr_variant::Map const& map, tr_direction const dir, tr_torrent* const tor)
{
    if (auto const i = map.value_if<int64_t>(TR_KEY_speed_Bps)) {
        tor->set_speed_limit(dir, Speed{ *i, Speed::Units::Byps });
    } else if (auto const i2 = map.value_if<int64_t>(TR_KEY_speed)) {
        tor->set_speed_limit(dir, Speed{ *i2, Speed::Units::KByps });
    }

    if (auto const b = map.value_if<bool>(TR_KEY_use_speed_limit)) {
        tor->use_speed_limit(dir, *b);
    }

    if (auto const b = map.value_if<bool>(TR_KEY_use_global_speed_limit)) {
        tr_torrentUseSessionLimits(tor, *b);
    }
}

[[nodiscard]] fields_t load_speed_limits(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto ret = fields_t{};

    if (auto const* const child = map.find_if<tr_variant::Map>(TR_KEY_speed_limit_up)) {
        load_single_speed_limit(*child, tr_direction::Up, tor);
        ret = Speedlimit;
    }

    if (auto const* const child = map.find_if<tr_variant::Map>(TR_KEY_speed_limit_down)) {
        load_single_speed_limit(*child, tr_direction::Down, tor);
        ret = Speedlimit;
    }

    return ret;
}

[[nodiscard]] fields_t load_ratio_limits(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const d = map.find_if<tr_variant::Map>(TR_KEY_seed_ratio_limit);
    if (d == nullptr) {
        return {};
    }

    if (auto const dratio = d->value_if<double>(TR_KEY_seed_ratio_limit)) {
        tor->set_seed_ratio(*dratio);
    }

    if (auto const i = d->value_if<int64_t>(TR_KEY_ratio_mode)) {
        tor->set_seed_ratio_mode(static_cast<tr_ratiolimit>(*i));
    }

    return Ratiolimit;
}

[[nodiscard]] fields_t load_idle_limits(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const d = map.find_if<tr_variant::Map>(TR_KEY_idle_limit);
    if (d == nullptr) {
        return {};
    }

    if (auto const imin = d->value_if<int64_t>(TR_KEY_idle_limit)) {
        tor->set_idle_limit_minutes(*imin);
    }

    if (auto const i = d->value_if<int64_t>(TR_KEY_idle_mode)) {
        tor->set_idle_limit_mode(static_cast<tr_idlelimit>(*i));
    }

    return Idlelimit;
}

// ---

void save_name(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_name, tr_variant::unmanaged_string(tor->name()));
}

[[nodiscard]] fields_t load_name(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const o_name = map.value_if<std::string_view>(TR_KEY_name);
    if (!o_name) {
        return {};
    }

    auto const name = tr_strv_strip(*o_name);
    if (std::empty(name)) {
        return {};
    }

    tor->set_name(name);

    return Name;
}

// ---

void save_filenames(tr_variant::Map& map, tr_torrent const* const tor)
{
    map.insert_or_assign(TR_KEY_files, per_file_list(tor, [tor](tr_file_index_t const i) {
                             return tr_variant::unmanaged_string(tor->file_subpath(i));
                         }));
}

[[nodiscard]] fields_t load_filenames(tr_variant::Map const& map, tr_torrent* const tor)
{
    auto const* const list = map.find_if<tr_variant::Vector>(TR_KEY_files);
    if (list == nullptr) {
        return {};
    }

    // The saved pathnames that differ from the ones the files have now.
    // They're collected in full before any of them is applied,
    // since a duplicate can turn up at any position
    // and the entries before it would already be on their files.
    auto renames = std::vector<std::pair<tr_file_index_t, std::string_view>>{};

    // A file with no entry, or whose entry isn't a usable pathname,
    // keeps the pathname it has.
    auto const add_rename = [tor, &renames](tr_file_index_t const i, tr_variant const* const entry) {
        if (entry == nullptr) {
            return;
        }

        if (auto const sv = nonempty(entry->value_if<std::string_view>()); sv && *sv != tor->file_subpath(i)) {
            renames.emplace_back(i, *sv);
        }
    };

    if (!for_each_file_entry(tor, *list, add_rename)) {
        return {};
    }

    // The common case: nothing was renamed, so there's nothing to vet or apply.
    if (std::empty(renames)) {
        return Filenames;
    }

    // Two files sharing a pathname share one file on disk, but the open-file
    // cache keys on file index, so each gets its own descriptor and they
    // write over each other. A torrent's own file list can't name a file
    // twice, so a duplicate is the saved list's, and the rest of that list is
    // no more trustworthy than the part that collided.
    auto const n_files = tor->file_count();
    auto subpaths = std::vector<std::string_view>{};
    subpaths.reserve(n_files);
    for (tr_file_index_t i = 0; i < n_files; ++i) {
        subpaths.emplace_back(tor->file_subpath(i));
    }
    for (auto const& [i, subpath] : renames) {
        subpaths[i] = subpath;
    }
    std::ranges::sort(subpaths);
    if (auto const dupe = std::ranges::adjacent_find(subpaths); dupe != std::end(subpaths)) {
        tr_logAddWarnTor(
            tor,
            fmt::format(
                "Not using the filenames saved for this torrent: they give '{:s}' to more than one file. "
                "Using the filenames from the torrent instead; the saved ones are in '{:s}' until it is written over.",
                *dupe,
                tor->resume_file()));
        return {};
    }

    for (auto const& [i, subpath] : renames) {
        tor->set_file_subpath(i, subpath);
    }

    return Filenames;
}

// ---

[[nodiscard]] tr_variant bitfield_to_raw(tr_bitfield const& b)
{
    if (b.has_all()) {
        return tr_variant::unmanaged_string("all"sv);
    }

    if (b.has_none() || !b.is_size_known()) {
        return tr_variant::unmanaged_string("none"sv);
    }

    return tr_variant::make_raw(b.raw());
}

[[nodiscard]] bool raw_to_bitfield(tr_bitfield& bitfield, std::string_view const raw)
{
    if (std::empty(raw) || raw == "none"sv) {
        bitfield.set_has_none();
    } else if (raw == "all"sv) {
        bitfield.set_has_all();
    } else {
        return bitfield.set_raw(raw);
    }

    return true;
}

void save_progress(tr_variant::Map& map, tr_torrent::ResumeHelper const& helper)
{
    auto prog = tr_variant::Map{ 3 };
    prog.try_emplace(TR_KEY_mtimes, tr::serializer::to_variant(helper.file_mtimes()));
    prog.try_emplace(TR_KEY_pieces, bitfield_to_raw(helper.checked_pieces()));
    prog.try_emplace(TR_KEY_blocks, bitfield_to_raw(helper.blocks()));
    map.insert_or_assign(TR_KEY_progress, std::move(prog));
}

[[nodiscard]] std::vector<time_t> load_mtimes(tr_variant::Map const& prog, tr_torrent const* const tor)
{
    // A file with no usable entry gets 0, marking its pieces untested.
    auto mtimes = std::vector<time_t>(tor->file_count());

    // A file whose mtime we take from the entry saved for some other file
    // has its pieces dropped from the checked set, so a legacy-length list
    // costs a rehash of everything after its first zero-length file unless
    // its entries are paired with their own files.
    auto const set_mtime = [&mtimes](tr_file_index_t const i, tr_variant const* const entry) {
        if (entry != nullptr) {
            mtimes[i] = static_cast<time_t>(entry->value_if<int64_t>().value_or(0));
        }
    };

    auto const* const list = prog.find_if<tr_variant::Vector>(TR_KEY_mtimes);
    if (list == nullptr || !for_each_file_entry(tor, *list, set_mtime)) {
        auto const n_list = list != nullptr ? std::size(*list) : size_t{};
        tr_logAddDebugTor(tor, fmt::format("Couldn't load mtimes: expected {} got {}", std::size(mtimes), n_list));
    }

    return mtimes;
}

/*
 * 'progress' is a dict with three entries:
 * - 'blocks', a bitfield for whether we have each block.
 * - 'pieces', a bitfield for whether each piece has been checked.
 * - 'mtimes', an array of per-file timestamps
 * On startup, 'pieces' is loaded. Then we check to see if the disk
 * mtimes differ from the 'mtimes' list. Changed files have their
 * pieces cleared from the bitset.
 *
 * Resume files from 3.00 and earlier have no 'pieces' entry,
 * so all of their pieces load as unchecked.
 * Older ones still name the blocks bitfield 'bitfield'.
 */
[[nodiscard]] fields_t load_progress(tr_variant::Map const& map, tr_torrent* const tor, tr_torrent::ResumeHelper& helper)
{
    auto const* const prog = map.find_if<tr_variant::Map>(TR_KEY_progress);
    if (prog == nullptr) {
        return {};
    }

    /// CHECKED PIECES

    auto checked = tr_bitfield{ tor->piece_count() };
    if (auto const sv = prog->value_if<std::string_view>(TR_KEY_pieces); sv && !raw_to_bitfield(checked, *sv)) {
        tr_logAddDebugTor(tor, "Couldn't load checked pieces: invalid value for 'pieces'");
    }

    auto const mtimes = load_mtimes(*prog, tor);
    helper.load_checked_pieces(checked, std::data(mtimes));

    /// COMPLETION

    auto blocks = tr_bitfield{ tor->block_count() };
    char const* err = nullptr;
    if (auto const b = prog->find(TR_KEY_blocks); b != std::end(*prog)) {
        if (auto const sv = b->second.value_if<std::string_view>(); sv && !raw_to_bitfield(blocks, *sv)) {
            err = "Invalid value for 'blocks'";
        }
    } else if (auto const raw = prog->value_if<std::string_view>(TR_KEY_bitfield)) {
        if (!blocks.set_raw(*raw)) {
            err = "Invalid value for 'bitfield'";
        }
    } else {
        err = "Couldn't find 'blocks' or 'bitfield'";
    }

    if (err != nullptr) {
        tr_logAddDebugTor(tor, fmt::format("Torrent needs to be verified - {}", err));
    } else {
        helper.load_blocks(std::move(blocks));
    }

    return Progress;
}

} // namespace

fields_t load(tr_torrent* const tor, tr_torrent::ResumeHelper& helper, fields_t const fields_to_load)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_torrent_metainfo::migrate_file(tor->session->resumeDir(), tor->name(), tor->info_hash_string(), ".resume"sv);

    auto const filename = tor->resume_file();
    auto benc = std::vector<char>{};
    if (!tr_sys_path_exists(filename) || !tr_file_read(filename, benc)) {
        return {};
    }

    auto serde = tr_variant_serde::benc();
    auto otop = serde.inplace().parse(benc);
    if (!otop) {
        tr_logAddDebugTor(tor, fmt::format("Couldn't read '{}': {}", filename, serde.error_.message()));
        return {};
    }

    tr::api_compat::convert_incoming_data(*otop);
    auto const* const p_map = otop->get_if<tr_variant::Map>();
    if (p_map == nullptr) {
        tr_logAddDebugTor(tor, fmt::format("Resume file '{}' does not contain a benc dict", filename));
        return {};
    }
    auto const& map = *p_map;

    tr_logAddDebugTor(tor, fmt::format("Read resume file '{}'", filename));
    auto fields_loaded = fields_t{};

    if ((fields_to_load & Corrupt) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_corrupt)) {
            tor->bytes_corrupt_.set_prev(*i);
            fields_loaded |= Corrupt;
        }
    }

    if ((fields_to_load & (Progress | DownloadDir)) != 0) {
        if (auto const sv = nonempty(map.value_if<std::string_view>(TR_KEY_destination))) {
            helper.load_download_dir(*sv);
            fields_loaded |= DownloadDir;
        }
    }

    if ((fields_to_load & (Progress | IncompleteDir)) != 0) {
        if (auto const sv = nonempty(map.value_if<std::string_view>(TR_KEY_incomplete_dir))) {
            helper.load_incomplete_dir(*sv);
            fields_loaded |= IncompleteDir;
        }
    }

    if ((fields_to_load & Downloaded) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_downloaded)) {
            tor->bytes_downloaded_.set_prev(*i);
            fields_loaded |= Downloaded;
        }
    }

    if ((fields_to_load & Uploaded) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_uploaded)) {
            tor->bytes_uploaded_.set_prev(*i);
            fields_loaded |= Uploaded;
        }
    }

    if ((fields_to_load & MaxPeers) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_max_peers)) {
            tor->set_peer_limit(static_cast<uint16_t>(*i));
            fields_loaded |= MaxPeers;
        }
    }

    if ((fields_to_load & Run) != 0) {
        if (auto const b = map.value_if<bool>(TR_KEY_paused)) {
            helper.load_start_when_stable(!*b);
            fields_loaded |= Run;
        }
    }

    if ((fields_to_load & AddedDate) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_added_date)) {
            helper.load_date_added(static_cast<time_t>(*i));
            fields_loaded |= AddedDate;
        }
    }

    if ((fields_to_load & DoneDate) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_done_date)) {
            helper.load_date_done(static_cast<time_t>(*i));
            fields_loaded |= DoneDate;
        }
    }

    if ((fields_to_load & ActivityDate) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_activity_date)) {
            tor->set_date_active(*i);
            fields_loaded |= ActivityDate;
        }
    }

    if ((fields_to_load & TimeSeeding) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_seeding_time_seconds)) {
            helper.load_seconds_seeding_before_current_start(*i);
            fields_loaded |= TimeSeeding;
        }
    }

    if ((fields_to_load & TimeDownloading) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_downloading_time_seconds)) {
            helper.load_seconds_downloading_before_current_start(*i);
            fields_loaded |= TimeDownloading;
        }
    }

    if ((fields_to_load & BandwidthPriority) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_bandwidth_priority);
            i && tr_isPriority(static_cast<tr_priority_t>(*i))) {
            tr_torrentSetPriority(tor, static_cast<tr_priority_t>(*i));
            fields_loaded |= BandwidthPriority;
        }
    }

    if ((fields_to_load & SequentialDownload) != 0) {
        if (auto const b = map.value_if<bool>(TR_KEY_sequential_download)) {
            tor->set_sequential_download(*b);
            fields_loaded |= SequentialDownload;
        }
    }

    if ((fields_to_load & SequentialDownloadFromPiece) != 0) {
        if (auto const i = map.value_if<int64_t>(TR_KEY_sequential_download_from_piece)) {
            tor->set_sequential_download_from_piece(*i);
            fields_loaded |= SequentialDownloadFromPiece;
        }
    }

    if ((fields_to_load & Peers) != 0) {
        fields_loaded |= load_peers(map, tor);
    }

    // Note: load_filenames() must come before load_progress()
    // so that load_progress() -> helper.load_checked_pieces() -> tor_.find_file()
    // will know where to look
    if ((fields_to_load & Filenames) != 0) {
        fields_loaded |= load_filenames(map, tor);
    }

    // Note: load_progress() should come before load_file_priorities()
    // so that we can skip loading priorities iff the torrent is a
    // seed or a partial seed.
    if ((fields_to_load & Progress) != 0) {
        fields_loaded |= load_progress(map, tor, helper);
    }

    if (!tor->is_done() && (fields_to_load & FilePriorities) != 0) {
        fields_loaded |= load_file_priorities(map, tor);
    }

    if ((fields_to_load & Dnd) != 0) {
        fields_loaded |= load_dnd(map, tor);
    }

    if ((fields_to_load & Speedlimit) != 0) {
        fields_loaded |= load_speed_limits(map, tor);
    }

    if ((fields_to_load & Ratiolimit) != 0) {
        fields_loaded |= load_ratio_limits(map, tor);
    }

    if ((fields_to_load & Idlelimit) != 0) {
        fields_loaded |= load_idle_limits(map, tor);
    }

    if ((fields_to_load & Name) != 0) {
        fields_loaded |= load_name(map, tor);
    }

    if ((fields_to_load & Labels) != 0) {
        fields_loaded |= load_labels(map, tor);
    }

    if ((fields_to_load & Group) != 0) {
        fields_loaded |= load_group(map, tor);
    }

    return fields_loaded;
}

void save(tr_torrent* const tor, tr_torrent::ResumeHelper const& helper)
{
    if (!tr_isTorrent(tor)) {
        return;
    }

    auto map = tr_variant::Map{ 50 }; // arbitrary "big enough" number
    auto const now = tr_time();
    map.try_emplace(TR_KEY_seeding_time_seconds, helper.seconds_seeding(now));
    map.try_emplace(TR_KEY_downloading_time_seconds, helper.seconds_downloading(now));
    map.try_emplace(TR_KEY_activity_date, helper.date_active());
    map.try_emplace(TR_KEY_added_date, helper.date_added());
    map.try_emplace(TR_KEY_corrupt, tor->bytes_corrupt_.ever());
    map.try_emplace(TR_KEY_done_date, helper.date_done());
    map.try_emplace(TR_KEY_destination, tr_variant::unmanaged_string(tor->download_dir().sv()));

    if (!std::empty(tor->incomplete_dir())) {
        map.try_emplace(TR_KEY_incomplete_dir, tr_variant::unmanaged_string(tor->incomplete_dir().sv()));
    }

    map.try_emplace(TR_KEY_downloaded, tor->bytes_downloaded_.ever());
    map.try_emplace(TR_KEY_uploaded, tor->bytes_uploaded_.ever());
    map.try_emplace(TR_KEY_max_peers, tor->peer_limit());
    map.try_emplace(TR_KEY_bandwidth_priority, tor->get_priority());
    map.try_emplace(TR_KEY_paused, !helper.start_when_stable());
    map.try_emplace(TR_KEY_sequential_download, tor->is_sequential_download());
    map.try_emplace(TR_KEY_sequential_download_from_piece, tor->sequential_download_from_piece());
    save_peers(map, tor);

    if (tor->has_metainfo()) {
        save_file_priorities(map, tor);
        save_dnd(map, tor);
        save_progress(map, helper);
    }

    save_speed_limits(map, tor);
    save_ratio_limits(map, tor);
    save_idle_limits(map, tor);
    save_filenames(map, tor);
    save_name(map, tor);
    save_labels(map, tor);
    save_group(map, tor);

    auto out = tr_variant{ std::move(map) };
    tr::api_compat::convert_outgoing_data(out);
    auto serde = tr_variant_serde::benc();
    if (!serde.to_file(out, tor->resume_file())) {
        tor->error().set_local_error(fmt::format("Unable to save resume file: {:s}", serde.error_.message()));
    }
}

} // namespace tr_resume
