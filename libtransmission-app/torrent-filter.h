// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <sigslot/signal.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/shared-string.h"

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/torrent.h"

namespace tr::app
{

class Prefs;
class TorrentList;

// whether the main window's `mode` filter shows `tor`
[[nodiscard]] bool should_show_torrent(Torrent const& tor, ShowMode mode) noexcept;

// the fields `should_show_torrent()` reads for `mode`, so a change to one of them may hide or show a torrent
[[nodiscard]] constexpr TorrentFieldSet show_mode_fields(ShowMode const mode) noexcept
{
    switch (mode) {
    case ShowMode::ShowActive:
        return Torrent::active_peer_count_fields() | Torrent::fields<TR_KEY_status>();

    case ShowMode::ShowDownloading:
    case ShowMode::ShowPaused:
    case ShowMode::ShowSeeding:
    case ShowMode::ShowVerifying:
        return Torrent::fields<TR_KEY_status>();

    case ShowMode::ShowError:
        return Torrent::fields<TR_KEY_error>();

    case ShowMode::ShowFinished:
        return Torrent::fields<TR_KEY_is_finished>();

    default:
        return {};
    }
}

// the fields `should_show_torrent()` reads for any mode, so a change to one of them may change a mode's count
[[nodiscard]] consteval TorrentFieldSet show_mode_fields() noexcept
{
    auto ret = TorrentFieldSet{};
    for (auto mode = 0U; mode < ShowModeCount; ++mode) {
        ret |= show_mode_fields(static_cast<ShowMode>(mode));
    }
    return ret;
}

// how many of `torrents` each show mode shows, indexed by `ShowMode`
[[nodiscard]] std::array<int, ShowModeCount> count_torrents_per_mode(TorrentList const& torrents) noexcept;

// how many of `torrents` each tracker site has, by sitename, for a tracker filter's menu
[[nodiscard]] std::map<tr::shared_string, int> count_torrents_per_sitename(TorrentList const& torrents);

/**
 * The main window's torrent filters as the prefs hold them: a show mode
 * (`show_mode`), a tracker site (`filter_trackers`) and a search text
 * (`filter_text`). A view asks `matches()` per row, looks at every row again
 * when `changed` fires, and at a torrent again when its change bits meet
 * `fields()`.
 */
class TorrentFilter
{
public:
    // Folds a UTF-8 string for case-insensitive matching, e.g. `g_utf8_casefold()`
    // or `QString::toCaseFolded()`. The toolkit supplies it: libtransmission has
    // no Unicode case tables of its own.
    using CaseFold = std::function<std::string(std::string_view)>;

    // How `matches()` compares with before a change. A view that can skip rows
    // uses this, e.g. `Gtk::Filter::Change`: `MoreStrict` can only hide torrents,
    // `LessStrict` can only show them, and `Different` can do either.
    enum class Change : uint8_t {
        Different,
        LessStrict,
        MoreStrict,
    };

    TorrentFilter(Prefs const& prefs, CaseFold case_fold);
    ~TorrentFilter() = default;
    TorrentFilter(TorrentFilter&&) = delete;
    TorrentFilter(TorrentFilter const&) = delete;
    TorrentFilter& operator=(TorrentFilter&&) = delete;
    TorrentFilter& operator=(TorrentFilter const&) = delete;

    // The show mode, then the tracker site, then the text, which matches the
    // name or the hex info hash as a case-insensitive substring.
    [[nodiscard]] bool matches(Torrent const& tor) const;

    // true when no filter is set, so every torrent matches
    [[nodiscard]] bool matches_all() const noexcept;

    // the fields to watch: a change to any other can't change `matches()`, given the filters set now
    [[nodiscard]] TorrentFieldSet fields() const noexcept;

    // `matches()` may answer differently
    sigslot::signal<Change> changed;

private:
    // reads `key` from the prefs; returns how the filter changed, if it did
    std::optional<Change> load(tr_quark key);

    Prefs const& prefs_;
    CaseFold const case_fold_;
    ShowMode show_mode_ = ShowMode::ShowAll;
    std::string sitename_; // empty: any tracker
    std::string text_; // folded and stripped; empty: any torrent
    sigslot::scoped_connection prefs_connection_;
};

} // namespace tr::app
