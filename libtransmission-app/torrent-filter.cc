// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/torrent-filter.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "libtransmission/quark.h"
#include "libtransmission/string-utils.h" // tr_strv_strip()

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/prefs.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

// NB: `show_mode_fields(mode)` lists every field read here; keep them in sync
bool should_show_torrent(Torrent const& tor, ShowMode const mode) noexcept
{
    switch (mode) {
    case ShowMode::ShowActive:
        return tor.peers_getting_from_us() > 0 || tor.peers_sending_to_us() > 0 || tor.is_verifying();

    case ShowMode::ShowDownloading:
        return tor.is_downloading() || tor.is_waiting_to_download();

    case ShowMode::ShowError:
        return tor.has_error();

    case ShowMode::ShowFinished:
        return tor.is_finished();

    case ShowMode::ShowPaused:
        return tor.is_paused();

    case ShowMode::ShowSeeding:
        return tor.is_seeding() || tor.is_waiting_to_seed();

    case ShowMode::ShowVerifying:
        return tor.is_verifying() || tor.is_waiting_to_verify();

    default:
        return true;
    }
}

// ---

TorrentFilter::TorrentFilter(Prefs const& prefs, CaseFold case_fold)
    : prefs_{ prefs }
    , case_fold_{ std::move(case_fold) }
{
    for (auto const key : { TR_KEY_show_mode, TR_KEY_filter_trackers, TR_KEY_filter_text }) {
        load(key);
    }

    prefs_connection_ = prefs_.observe_changes([this](tr_quark const key) {
        if (auto const change = load(key)) {
            changed(*change);
        }
    });
}

std::optional<TorrentFilter::Change> TorrentFilter::load(tr_quark const key)
{
    auto const classify = [](bool const more_strict, bool const less_strict) {
        if (more_strict) {
            return Change::MoreStrict;
        }
        return less_strict ? Change::LessStrict : Change::Different;
    };

    switch (key) {
    case TR_KEY_show_mode:
        if (auto const was = std::exchange(show_mode_, prefs_.get<ShowMode>(key)); was != show_mode_) {
            return classify(was == ShowMode::ShowAll, show_mode_ == ShowMode::ShowAll);
        }
        return {};

    case TR_KEY_filter_trackers:
        if (auto const was = std::exchange(sitename_, prefs_.get<std::string>(key)); was != sitename_) {
            return classify(std::empty(was), std::empty(sitename_));
        }
        return {};

    case TR_KEY_filter_text:
        // text that contains the old text matches a subset of what the old text matched
        if (auto const was = std::exchange(text_, case_fold_(tr_strv_strip(prefs_.get<std::string>(key)))); was != text_) {
            return classify(text_.contains(was), was.contains(text_));
        }
        return {};

    default:
        return {};
    }
}

bool TorrentFilter::matches(Torrent const& tor) const
{
    if (!should_show_torrent(tor, show_mode_)) {
        return false;
    }

    if (!std::empty(sitename_) && !tor.includes_tracker(sitename_)) {
        return false;
    }

    if (!std::empty(text_)) {
        // the hash is lowercase hex, which a fold leaves alone
        return case_fold_(tor.name()).contains(text_) || tor.hash_string().sv().contains(text_);
    }

    return true;
}

bool TorrentFilter::matches_all() const noexcept
{
    return show_mode_ == ShowMode::ShowAll && std::empty(sitename_) && std::empty(text_);
}

TorrentFieldSet TorrentFilter::fields() const noexcept
{
    auto ret = show_mode_fields(show_mode_);

    if (!std::empty(sitename_)) {
        ret |= Torrent::fields<TR_KEY_trackers>(); // the sitenames come from the trackers
    }

    if (!std::empty(text_)) {
        ret |= Torrent::fields<TR_KEY_name, TR_KEY_hash_string>();
    }

    return ret;
}

} // namespace tr::app
