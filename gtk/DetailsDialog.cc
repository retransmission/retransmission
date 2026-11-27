// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "DetailsDialog.h"

#include "Actions.h"
#include "FileList.h"
#include "GtkCompat.h"
#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_BIG, GUI_PAD_SMALL
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission-app/keyed-rows.h>
#include <libtransmission-app/option-ranges.h>
#include <libtransmission-app/peer-list.h>
#include <libtransmission-app/torrent-keys.h>
#include <libtransmission-app/torrent-list.h>
#include <libtransmission-app/torrent.h>
#include <libtransmission-app/tracker-status.h>
#include <libtransmission-app/uniform-value.h>

#include <libtransmission/string-utils.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>
#include <libtransmission/web-utils.h>

#include <gdkmm/pixbuf.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/cellrendererprogress.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>
#include <gtkmm/tooltip.h>
#include <gtkmm/treemodel.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/treemodelsort.h>
#include <gtkmm/treerowreference.h>
#include <gtkmm/treeview.h>

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/quark.h>
#include <glibmm/ustring.h>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <sigslot/signal.hpp>

#include <algorithm>
#include <array>
#include <cmath> // std::llround(), std::pow()
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib> // abort()
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple> // std::ignore
#include <unordered_map>
#include <unordered_set>

using namespace std::literals;

using namespace tr::Values;

class DetailsDialog::Impl
{
public:
    Impl(
        DetailsDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs& prefs);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    void set_torrents(std::vector<tr_torrent_id_t> const& torrent_ids);
    void refresh();

private:
    void info_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void peer_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void tracker_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void options_page_init(Glib::RefPtr<Gtk::Builder> const& builder);

    void on_details_window_size_allocated();

    bool onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip);
    void onMorePeerInfoToggled();

    bool trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter);
    void on_tracker_list_selection_changed();

    void on_tracker_list_add_button_clicked();
    void on_edit_trackers();
    void on_tracker_list_remove_button_clicked();
    void onScrapeToggled();
    void onBackupToggled();

    // Sets an option on every shown torrent. The Options tab holds still until
    // the session answers, so an older fetch can't seem to undo the edit.
    template<typename T>
    void set_option(tr_quark const key, T const& value)
    {
        if (auto const tag = core_->set_torrents(ids_, key, value); tag != 0) {
            pending_option_sets_.insert(tag);
        }
    }

    void on_set_done(Session::Tag tag, Session::SetResult const& result);

    void refreshInfo(std::vector<tr_torrent*> const& torrents);
    void refreshPeers();
    void refreshTracker();
    void refreshFiles(std::vector<tr_torrent*> const& torrents);
    void refreshOptions();

    void refreshPeerList(std::vector<tr::app::Torrent const*> const& torrents);
    void refreshWebseedList(std::vector<tr::app::Torrent const*> const& torrents);

    [[nodiscard]] tr_torrent_id_t tracker_list_get_current_torrent_id() const;
    [[nodiscard]] tr::app::Torrent const* tracker_list_get_current_torrent() const;

    [[nodiscard]] std::vector<tr_torrent*> getTorrents() const;
    [[nodiscard]] std::vector<tr::app::Torrent const*> app_torrents() const; // the shown torrents in `core_->torrents()`

private:
    DetailsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    tr::app::Prefs& prefs_;

    Gtk::CheckButton* honor_limits_check_ = nullptr;
    Gtk::CheckButton* up_limited_check_ = nullptr;
    Gtk::SpinButton* up_limit_sping_ = nullptr;
    Gtk::CheckButton* down_limited_check_ = nullptr;
    Gtk::SpinButton* down_limit_spin_ = nullptr;
    Gtk::ComboBox* bandwidth_combo_ = nullptr;

    Gtk::ComboBox* ratio_combo_ = nullptr;
    Gtk::SpinButton* ratio_spin_ = nullptr;
    Gtk::ComboBox* idle_combo_ = nullptr;
    Gtk::SpinButton* idle_spin_ = nullptr;
    Gtk::SpinButton* max_peers_spin_ = nullptr;

    sigc::connection honor_limits_check_tag_;
    sigc::connection up_limited_check_tag_;
    sigc::connection down_limited_check_tag_;
    sigc::connection down_limit_spin_tag_;
    sigc::connection up_limit_spin_tag_;
    sigc::connection bandwidth_combo_tag_;
    sigc::connection ratio_combo_tag_;
    sigc::connection ratio_spin_tag_;
    sigc::connection idle_combo_tag_;
    sigc::connection idle_spin_tag_;
    sigc::connection max_peers_spin_tag_;

    std::unordered_set<Session::Tag> pending_option_sets_;
    sigslot::scoped_connection set_done_tag_;

    Gtk::Label* added_lb_ = nullptr;
    Gtk::Label* size_lb_ = nullptr;
    Gtk::Label* state_lb_ = nullptr;
    Gtk::Label* have_lb_ = nullptr;
    Gtk::Label* dl_lb_ = nullptr;
    Gtk::Label* ul_lb_ = nullptr;
    Gtk::Label* error_lb_ = nullptr;
    Gtk::Label* date_started_lb_ = nullptr;
    Gtk::Label* eta_lb_ = nullptr;
    Gtk::Label* last_activity_lb_ = nullptr;

    Gtk::Label* hash_lb_ = nullptr;
    Gtk::Label* privacy_lb_ = nullptr;
    Gtk::Label* origin_lb_ = nullptr;
    Gtk::Label* destination_lb_ = nullptr;
    Glib::RefPtr<Gtk::TextBuffer> comment_buffer_;

    // a peer's row, and the item that its sort and tooltip read
    struct PeerRow {
        Gtk::TreeRowReference ref;
        std::unique_ptr<tr::app::PeerListItem> item;
    };

    tr::app::KeyedRows<tr::app::PeerKey, PeerRow> peer_rows_;
    tr::app::KeyedRows<std::pair<tr_torrent_id_t, std::string>, Gtk::TreeRowReference> webseed_rows_;
    Glib::RefPtr<Gtk::ListStore> peer_store_;
    Glib::RefPtr<Gtk::ListStore> webseed_store_;
    Gtk::ScrolledWindow* webseed_view_ = nullptr;
    Gtk::TreeView* peer_view_ = nullptr;
    Gtk::CheckButton* more_peer_details_check_ = nullptr;

    Glib::RefPtr<Gtk::ListStore> tracker_store_;
    std::unordered_map<std::string, Gtk::TreeRowReference> tracker_hash_;
    Glib::RefPtr<Gtk::TreeModelFilter> trackers_filtered_;
    Gtk::Button* add_tracker_button_ = nullptr;
    Gtk::Button* edit_trackers_button_ = nullptr;
    Gtk::Button* remove_tracker_button_ = nullptr;
    Gtk::TreeView* tracker_view_ = nullptr;
    Gtk::CheckButton* scrape_check_ = nullptr;
    Gtk::CheckButton* all_check_ = nullptr;

    FileList* file_list_ = nullptr;
    Gtk::Label* file_label_ = nullptr;

    std::vector<tr_torrent_id_t> ids_;
    sigc::connection periodic_refresh_tag_;
    sigc::connection torrents_changed_tag_;

    Glib::Quark const TORRENT_ID_KEY = Glib::Quark("tr-torrent-id-key");
    Glib::Quark const TEXT_BUFFER_KEY = Glib::Quark("tr-text-buffer-key");
    Glib::Quark const URL_ENTRY_KEY = Glib::Quark("tr-url-entry-key");

    static guint last_page_;
};

guint DetailsDialog::Impl::last_page_ = 0;

std::vector<tr_torrent*> DetailsDialog::Impl::getTorrents() const
{
    return core_->find_torrents(ids_);
}

/****
*****
*****  OPTIONS TAB
*****
****/

namespace
{

// shows `value`, or the mixed state when it is nullopt
void set_check(Gtk::CheckButton* const check, sigc::connection& tag, std::optional<bool> const value)
{
    tag.block();
    check->set_inconsistent(!value);
    check->set_active(value.value_or(false));
    tag.unblock();
}

// compares at the precision that the spin shows
void set_spin_if_different(Gtk::SpinButton* const spin, sigc::connection& tag, double const value)
{
    auto const scale = std::pow(10.0, spin->get_digits());

    if (std::llround(spin->get_value() * scale) != std::llround(value * scale)) {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

// shows `value`, or no choice when it is nullopt
void set_combo_enum(Gtk::ComboBox* const combobox, sigc::connection& tag, std::optional<int> const value)
{
    tag.block();
    if (value) {
        gtr_combo_box_set_active_enum(*combobox, *value);
    } else {
        combobox->set_active(-1);
    }
    tag.unblock();
}

template<typename T>
[[nodiscard]] Glib::RefPtr<Gtk::Adjustment> make_adjustment(tr::app::OptionRange<T> const& range, double const step)
{
    return Gtk::Adjustment::create(range.min, range.min, range.max, step);
}

// the fields that `refreshOptions()` shows
constexpr auto OptionFields = tr::app::Torrent::fields<
    TR_KEY_bandwidth_priority,
    TR_KEY_download_limit,
    TR_KEY_download_limited,
    TR_KEY_honors_session_limits,
    TR_KEY_peer_limit,
    TR_KEY_seed_idle_limit,
    TR_KEY_seed_idle_mode,
    TR_KEY_seed_ratio_limit,
    TR_KEY_seed_ratio_mode,
    TR_KEY_upload_limit,
    TR_KEY_upload_limited>();

} // namespace

void DetailsDialog::Impl::refreshOptions()
{
    // a pending set's re-fetch would show the edit; until then, the model has the old value
    if (!std::empty(pending_option_sets_)) {
        return;
    }

    auto const torrents = app_torrents();
    if (std::empty(torrents)) {
        return;
    }

    // When the torrents differ, a check shows the mixed state, a combo shows no choice,
    // and a spin shows the first torrent's value; an edit applies to them all.
    using tr::app::Torrent;
    using tr::app::uniform_value;
    auto const& first = *torrents.front();

    set_check(honor_limits_check_, honor_limits_check_tag_, uniform_value(torrents, &Torrent::honors_session_limits));

    set_check(down_limited_check_, down_limited_check_tag_, uniform_value(torrents, &Torrent::download_limited));
    set_spin_if_different(down_limit_spin_, down_limit_spin_tag_, first.download_limit().count(Speed::Units::KByps));

    set_check(up_limited_check_, up_limited_check_tag_, uniform_value(torrents, &Torrent::upload_limited));
    set_spin_if_different(up_limit_sping_, up_limit_spin_tag_, first.upload_limit().count(Speed::Units::KByps));

    set_combo_enum(bandwidth_combo_, bandwidth_combo_tag_, uniform_value(torrents, &Torrent::bandwidth_priority));

    auto const ratio_mode = uniform_value(torrents, &Torrent::seed_ratio_mode);
    set_combo_enum(ratio_combo_, ratio_combo_tag_, ratio_mode);
    gtr_widget_set_visible(*ratio_spin_, ratio_mode == TR_RATIOLIMIT_SINGLE);
    set_spin_if_different(ratio_spin_, ratio_spin_tag_, first.seed_ratio_limit());

    auto const idle_mode = uniform_value(torrents, &Torrent::seed_idle_mode);
    set_combo_enum(idle_combo_, idle_combo_tag_, idle_mode);
    gtr_widget_set_visible(*idle_spin_, idle_mode == TR_IDLELIMIT_SINGLE);
    set_spin_if_different(idle_spin_, idle_spin_tag_, first.seed_idle_limit());

    set_spin_if_different(max_peers_spin_, max_peers_spin_tag_, first.peer_limit());
}

void DetailsDialog::Impl::on_set_done(Session::Tag const tag, Session::SetResult const& /*result*/)
{
    // The session re-fetched the torrents before firing, or, if the set failed,
    // left them as they were, which puts the session's values back in the widgets.
    if (pending_option_sets_.erase(tag) > 0) {
        refreshOptions();
    }
}

void DetailsDialog::Impl::options_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    auto const speed_units_kbyps_str = Speed::units().display_name(Speed::Units::KByps);

    // a click on a mixed check sets it for every torrent
    auto const bind_check = [this](Gtk::CheckButton* const check, tr_quark const key) {
        return check->signal_toggled().connect([this, check, key] {
            check->set_inconsistent(false);
            set_option(key, check->get_active());
        });
    };

    honor_limits_check_tag_ = bind_check(honor_limits_check_, TR_KEY_honors_session_limits);

    down_limited_check_->set_label(
        fmt::format(fmt::runtime(down_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    down_limited_check_tag_ = bind_check(down_limited_check_, TR_KEY_download_limited);

    down_limit_spin_->set_adjustment(make_adjustment(tr::app::SpeedLimitRange, 5));
    down_limit_spin_tag_ = down_limit_spin_->signal_value_changed().connect(
        [this] { set_option(TR_KEY_download_limit, down_limit_spin_->get_value_as_int()); });

    up_limited_check_->set_label(
        fmt::format(fmt::runtime(up_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    up_limited_check_tag_ = bind_check(up_limited_check_, TR_KEY_upload_limited);

    up_limit_sping_->set_adjustment(make_adjustment(tr::app::SpeedLimitRange, 5));
    up_limit_spin_tag_ = up_limit_sping_->signal_value_changed().connect(
        [this] { set_option(TR_KEY_upload_limit, up_limit_sping_->get_value_as_int()); });

    gtr_priority_combo_init(*bandwidth_combo_);
    bandwidth_combo_tag_ = bandwidth_combo_->signal_changed().connect(
        [this] { set_option(TR_KEY_bandwidth_priority, gtr_combo_box_get_active_enum(*bandwidth_combo_)); });

    gtr_combo_box_set_enum(
        *ratio_combo_,
        {
            { _("Use global settings"), TR_RATIOLIMIT_GLOBAL },
            { _("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED },
            { _("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE },
        });
    ratio_combo_tag_ = ratio_combo_->signal_changed().connect([this] {
        auto const mode = gtr_combo_box_get_active_enum(*ratio_combo_);
        set_option(TR_KEY_seed_ratio_mode, mode);
        gtr_widget_set_visible(*ratio_spin_, mode == TR_RATIOLIMIT_SINGLE);
    });
    ratio_spin_->set_adjustment(make_adjustment(tr::app::SeedRatioLimitRange, .05));
    ratio_spin_->set_digits(2);
    ratio_spin_->set_width_chars(7);
    ratio_spin_tag_ = ratio_spin_->signal_value_changed().connect(
        [this] { set_option(TR_KEY_seed_ratio_limit, ratio_spin_->get_value()); });

    gtr_combo_box_set_enum(
        *idle_combo_,
        {
            { _("Use global settings"), TR_IDLELIMIT_GLOBAL },
            { _("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED },
            { _("Stop seeding if idle for N minutes:"), TR_IDLELIMIT_SINGLE },
        });
    idle_combo_tag_ = idle_combo_->signal_changed().connect([this] {
        auto const mode = gtr_combo_box_get_active_enum(*idle_combo_);
        set_option(TR_KEY_seed_idle_mode, mode);
        gtr_widget_set_visible(*idle_spin_, mode == TR_IDLELIMIT_SINGLE);
    });
    idle_spin_->set_adjustment(make_adjustment(tr::app::SeedIdleLimitRange, 5));
    idle_spin_tag_ = idle_spin_->signal_value_changed().connect(
        [this] { set_option(TR_KEY_seed_idle_limit, idle_spin_->get_value_as_int()); });

    max_peers_spin_->set_adjustment(make_adjustment(tr::app::PeerLimitRange, 5));
    max_peers_spin_tag_ = max_peers_spin_->signal_value_changed().connect(
        [this] { set_option(TR_KEY_peer_limit, max_peers_spin_->get_value_as_int()); });
}

/****
*****
*****  INFO TAB
*****
****/

namespace
{

[[nodiscard]] Glib::ustring activityString(int activity, bool finished)
{
    switch (activity) {
    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_CHECK:
        return _("Verifying local data");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_DOWNLOAD:
        return C_("Verb", "Downloading");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_SEED:
        return C_("Verb", "Seeding");

    case TR_STATUS_STOPPED:
        return finished ? _("Seeding complete") : _("Paused");

    default:
        g_assert_not_reached();
    }

    return {};
}

/* Only call gtk_text_buffer_set_text () if the new text differs from the old.
 * This way if the user has text selected, refreshing won't deselect it */
void gtr_text_buffer_set_text(Glib::RefPtr<Gtk::TextBuffer> const& b, Glib::ustring const& str)
{
    if (b->get_text() != str) {
        b->set_text(str);
    }
}

[[nodiscard]] std::string get_date_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%x}", *std::localtime(&t));
}

[[nodiscard]] std::string get_date_time_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%c}", *std::localtime(&t));
}

} // namespace

void DetailsDialog::Impl::refreshInfo(std::vector<tr_torrent*> const& torrents)
{
    auto const now = time(nullptr);
    Glib::ustring str;
    Glib::ustring const mixed = _("Mixed");
    Glib::ustring const no_torrent = _("No Torrents Selected");
    Glib::ustring stateString;
    uint64_t sizeWhenDone = 0;

    auto const stats = tr_torrentStat(torrents);

    std::vector<tr_torrent_view> infos;
    infos.reserve(torrents.size());
    for (auto const* const torrent : torrents) {
        infos.push_back(tr_torrentView(torrent));
    }

    /* privacy_lb */
    if (infos.empty()) {
        str = no_torrent;
    } else {
        bool const baseline = infos.front().is_private;
        bool const is_uniform = std::ranges::all_of(infos, [baseline](auto const& info) {
            return info.is_private == baseline;
        });

        if (is_uniform) {
            str = baseline ? _("Private to this tracker -- DHT and PEX disabled") : _("Public torrent");
        } else {
            str = mixed;
        }
    }

    privacy_lb_->set_text(str);

    /* added_lb */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const baseline = stats.front().added_date;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& stat) {
            return stat.added_date == baseline;
        });

        if (is_uniform) {
            str = get_date_time_string(baseline);
        } else {
            str = mixed;
        }
    }

    added_lb_->set_text(str);

    /* origin_lb */
    if (infos.empty()) {
        str = no_torrent;
    } else {
        auto const creator = tr_strv_strip(infos.front().creator != nullptr ? infos.front().creator : "");
        auto const date = infos.front().date_created;
        auto const datestr = get_date_string(date);
        bool const mixed_creator = std::ranges::any_of(infos, [&creator](auto const& info) {
            return creator != (info.creator != nullptr ? info.creator : "");
        });
        bool const mixed_date = std::ranges::any_of(infos, [date](auto const& info) { return date != info.date_created; });

        bool const empty_creator = std::empty(creator);
        bool const empty_date = date == 0;

        if (mixed_creator || mixed_date) {
            str = mixed;
        } else if (!empty_creator && !empty_date) {
            str = fmt::format(
                fmt::runtime(_("Created by {creator} on {date}")),
                fmt::arg("creator", creator),
                fmt::arg("date", datestr));
        } else if (!empty_creator) {
            str = fmt::format(fmt::runtime(_("Created by {creator}")), fmt::arg("creator", creator));
        } else if (!empty_date) {
            str = fmt::format(fmt::runtime(_("Created on {date}")), fmt::arg("date", datestr));
        } else {
            str = _("N/A");
        }
    }

    origin_lb_->set_text(str);

    /* comment_buffer */
    if (infos.empty()) {
        str.clear();
    } else {
        auto const baseline = Glib::ustring(infos.front().comment != nullptr ? infos.front().comment : "");
        bool const is_uniform = std::ranges::all_of(infos, [&baseline](auto const& info) {
            return baseline == (info.comment != nullptr ? info.comment : "");
        });

        str = is_uniform ? baseline : mixed;
    }

    gtr_text_buffer_set_text(comment_buffer_, str);

    /* destination_lb */
    if (torrents.empty()) {
        str = no_torrent;
    } else {
        auto const baseline = tr_torrentGetDownloadDir(torrents.front());
        bool const is_uniform = std::ranges::all_of(torrents, [&baseline](auto const* torrent) {
            return baseline == tr_torrentGetDownloadDir(torrent);
        });

        str = is_uniform ? Glib::ustring{ baseline } : mixed;
    }

    destination_lb_->set_text(str);

    /* state_lb */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const baseline = stats.front().activity;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.activity; });
        bool const all_finished = std::ranges::all_of(stats, [](auto const& st) { return st.finished; });

        str = is_uniform ? activityString(baseline, all_finished) : mixed;
    }

    stateString = str;
    state_lb_->set_text(str);

    /* date started */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        time_t const baseline = stats.front().start_date;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.start_date; });

        if (!is_uniform) {
            str = mixed;
        } else if (baseline <= 0 || stats[0].activity == TR_STATUS_STOPPED) {
            str = stateString;
        } else {
            str = tr_format_time(now - baseline);
        }
    }

    date_started_lb_->set_text(str);

    /* eta */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const baseline = stats.front().eta;
        auto const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.eta; });

        if (!is_uniform) {
            str = mixed;
        } else if (baseline < 0) {
            str = _("Unknown");
        } else {
            str = tr_format_time_left(baseline);
        }
    }

    eta_lb_->set_text(str);

    /* size_lb */
    {
        auto const piece_count = std::accumulate(
            std::begin(infos),
            std::end(infos),
            uint64_t{},
            [](auto sum, auto const& info) { return sum + info.n_pieces; });

        if (piece_count == 0) {
            str.clear();
        } else {
            auto const total_size = std::accumulate(
                std::begin(infos),
                std::end(infos),
                uint64_t{},
                [](auto sum, auto const& info) { return sum + info.total_size; });

            auto const file_count = std::accumulate(
                std::begin(torrents),
                std::end(torrents),
                std::size_t{},
                [](auto sum, auto const* tor) { return sum + tr_torrentFileCount(tor); });

            str = fmt::format(
                fmt::runtime(
                    ngettext("{total_size} in {file_count:L} file", "{total_size} in {file_count:L} files", file_count)),
                fmt::arg("total_size", tr_strlsize(total_size)),
                fmt::arg("file_count", file_count));

            auto const piece_size = std::empty(infos) ? uint32_t{} : infos.front().piece_size;
            auto const piece_size_is_uniform = std::ranges::all_of(infos, [piece_size](auto const& info) {
                return info.piece_size == piece_size;
            });

            if (piece_size_is_uniform) {
                str += ' ';
                str += fmt::format(
                    fmt::runtime(ngettext(
                        "({piece_count:L} BitTorrent piece @ {piece_size})",
                        "({piece_count:L} BitTorrent pieces @ {piece_size})",
                        piece_count)),
                    fmt::arg("piece_count", piece_count),
                    fmt::arg("piece_size", Memory{ piece_size, Memory::Units::Bytes }.to_string()));
            }
        }

        size_lb_->set_text(str);
    }

    /* have_lb */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        uint64_t leftUntilDone = 0;
        uint64_t haveUnchecked = 0;
        uint64_t haveValid = 0;
        uint64_t available = 0;

        for (auto const& st : stats) {
            haveUnchecked += st.have_unchecked;
            haveValid += st.have_valid;
            sizeWhenDone += st.size_when_done;
            leftUntilDone += st.left_until_done;
            available += st.size_when_done - st.left_until_done + st.desired_available;
        }

        {
            auto const avail = Percent{ available, sizeWhenDone }.to_string();
            auto const buf2 = Percent{ haveValid + haveUnchecked, sizeWhenDone }.to_string();
            auto const total = tr_strlsize(haveUnchecked + haveValid);
            auto const unver = tr_strlsize(haveUnchecked);

            if (haveUnchecked == 0 && leftUntilDone == 0) {
                str = fmt::format(
                    fmt::runtime(_("{current_size} ({percent_done}%)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2));
            } else if (haveUnchecked == 0) {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(_("{current_size} ({percent_done}% of {percent_available}% available)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail));
            } else {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(
                        _("{current_size} ({percent_done}% of {percent_available}% available; {unverified_size} unverified)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail),
                    fmt::arg("unverified_size", unver));
            }
        }
    }

    have_lb_->set_text(str);

    // dl_lb
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const downloaded_str = tr_strlsize(
            std::accumulate(std::begin(stats), std::end(stats), uint64_t{ 0 }, [](auto sum, auto const& st) {
                return sum + st.downloaded_ever;
            }));

        auto const failed = std::accumulate(std::begin(stats), std::end(stats), uint64_t{ 0 }, [](auto sum, auto const& st) {
            return sum + st.corrupt_ever;
        });

        if (failed != 0) {
            str = fmt::format(
                fmt::runtime(_("{downloaded_size} (+{discarded_size} discarded after failed checksum)")),
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", tr_strlsize(failed)));
        } else {
            str = downloaded_str;
        }
    }

    dl_lb_->set_text(str);

    /* ul_lb */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const uploaded = std::accumulate(std::begin(stats), std::end(stats), uint64_t{}, [](auto sum, auto const& st) {
            return sum + st.uploaded_ever;
        });
        auto const denominator = std::accumulate(std::begin(stats), std::end(stats), uint64_t{}, [](auto sum, auto const& st) {
            return sum + st.size_when_done;
        });
        str = fmt::format(
            fmt::runtime(_("{uploaded_size} (Ratio: {ratio})")),
            fmt::arg("uploaded_size", tr_strlsize(uploaded)),
            fmt::arg("ratio", Ratio{ uploaded, denominator }.to_string()));
    }

    ul_lb_->set_text(str);

    /* hash_lb */
    if (infos.empty()) {
        str = no_torrent;
    } else if (infos.size() == 1) {
        str = infos.front().hash_string;
    } else {
        str = mixed;
    }

    hash_lb_->set_text(str);

    /* error */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const& baseline = stats.front().error_string;
        bool const is_uniform = std::ranges::all_of(stats, [&baseline](auto const& st) { return baseline == st.error_string; });

        str = is_uniform ? Glib::ustring{ baseline } : mixed;
    }

    if (str.empty()) {
        str = _("No errors");
    }

    error_lb_->set_text(str);

    /* activity date */
    if (stats.empty()) {
        str = no_torrent;
    } else {
        auto const iter = std::ranges::max_element(stats, [](auto const& lhs, auto const& rhs) {
            return lhs.activity_date < rhs.activity_date;
        });
        time_t const latest = iter->activity_date;

        if (latest <= 0) {
            str = _("Never");
        } else if ((now - latest) < 5U) {
            str = _("Active now");
        } else {
            str = tr_format_time_relative(now, latest);
        }
    }

    last_activity_lb_->set_text(str);
}

void DetailsDialog::Impl::info_page_init(Glib::RefPtr<Gtk::Builder> const& builder)
{
    comment_buffer_ = Gtk::TextBuffer::create();
    auto* tw = gtr_get_widget<Gtk::TextView>(builder, "comment_value_view");
    tw->set_buffer(comment_buffer_);
}

/****
*****
*****  PEERS TAB
*****
****/

namespace
{

using tr::app::PeerColumn;
using tr::app::PeerFlag;

class WebseedModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    WebseedModelColumns() noexcept
    {
        add(url);
        add(download_rate_speed);
        add(download_rate_string);
    }

    Gtk::TreeModelColumn<Glib::ustring> url;
    Gtk::TreeModelColumn<Speed> download_rate_speed;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
};

WebseedModelColumns const webseed_cols;

class PeerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    PeerModelColumns() noexcept
    {
        add(item);
        add(address);
        add(download_rate_string);
        add(upload_rate_string);
        add(client);
        add(progress_string);
        add(upload_request_count_string);
        add(download_request_count_string);
        add(encryption_stock_id);
        add(flags);
    }

    // what the sort and the tooltip read; null only while a new row is being filled
    Gtk::TreeModelColumn<tr::app::PeerListItem const*> item;
    Gtk::TreeModelColumn<Glib::ustring> address;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
    Gtk::TreeModelColumn<Glib::ustring> upload_rate_string;
    Gtk::TreeModelColumn<Glib::ustring> client;
    Gtk::TreeModelColumn<Glib::ustring> progress_string;
    Gtk::TreeModelColumn<Glib::ustring> upload_request_count_string;
    Gtk::TreeModelColumn<Glib::ustring> download_request_count_string;
    Gtk::TreeModelColumn<Glib::ustring> encryption_stock_id;
    Gtk::TreeModelColumn<Glib::ustring> flags;
};

PeerModelColumns const peer_cols;

// the sort column id of `column`
[[nodiscard]] constexpr int col(PeerColumn const column) noexcept
{
    return static_cast<int>(column);
}

// a sort func's argument
using SortIter = IF_GTKMM4(Gtk::TreeModel::const_iterator, Gtk::TreeModel::iterator);

// a sort func's result
[[nodiscard]] constexpr int to_sort_result(std::weak_ordering const val) noexcept
{
    if (val < 0) {
        return -1;
    }
    return val > 0 ? 1 : 0;
}

// `item` must outlive the row: the row keeps a pointer to it
void set_peer_row(Gtk::TreeModel::iterator const& iter, tr::app::PeerListItem const& item)
{
    auto const& peer = item.peer();

    auto const count_string = [](size_t const count) {
        return count > 0U ? std::to_string(count) : std::string{};
    };
    auto const speed_string = [](Speed const& speed) {
        return speed.is_zero() ? std::string{} : speed.to_string();
    };

    (*iter)[peer_cols.item] = &item;
    (*iter)[peer_cols.address] = peer.address;
    (*iter)[peer_cols.client] = std::string{ peer.client_name.sv() };
    (*iter)[peer_cols.encryption_stock_id] = peer.is_encrypted ? "lock" : "";
    (*iter)[peer_cols.progress_string] = item.progress().to_whole_string();
    (*iter)[peer_cols.upload_request_count_string] = count_string(peer.active_reqs_to_client);
    (*iter)[peer_cols.download_request_count_string] = count_string(peer.active_reqs_to_peer);
    (*iter)[peer_cols.download_rate_string] = speed_string(peer.rate_to_client);
    (*iter)[peer_cols.upload_rate_string] = speed_string(peer.rate_to_peer);
    (*iter)[peer_cols.flags] = peer.flags;
}

[[nodiscard]] char const* peer_flag_description(PeerFlag const flag)
{
    switch (flag) {
    case PeerFlag::OptimisticUnchoke:
        return _("Optimistic unchoke");
    case PeerFlag::DownloadingFrom:
        return _("Downloading from this peer");
    case PeerFlag::WouldDownloadFrom:
        return _("We would download from this peer if they would let us");
    case PeerFlag::UploadingTo:
        return _("Uploading to peer");
    case PeerFlag::WouldUploadTo:
        return _("This peer would download from us if we would let them");
    case PeerFlag::UnchokedUsUninterested:
        return _("Peer has unchoked us, but we're not interested");
    case PeerFlag::UnchokedPeerUninterested:
        return _("We unchoked this peer, but they're not interested");
    case PeerFlag::Encrypted:
        return _("Encrypted connection");
    case PeerFlag::FromPex:
        return _("Peer was found through Peer Exchange (PEX)");
    case PeerFlag::FromDht:
        return _("Peer was found through DHT");
    case PeerFlag::Incoming:
        return _("Peer is an incoming connection");
    case PeerFlag::Utp:
        return _("Peer is connected over µTP");
    case PeerFlag::SupportsHolepunch:
        return _("Peer supports holepunch (BEP 55)");
    }

    return "";
}

} // namespace

std::vector<tr::app::Torrent const*> DetailsDialog::Impl::app_torrents() const
{
    auto torrents = std::vector<tr::app::Torrent const*>{};
    torrents.reserve(std::size(ids_));
    for (auto const id : ids_) {
        if (auto const* const tor = core_->torrents().find(id); tor != nullptr) {
            torrents.push_back(tor);
        }
    }
    return torrents;
}

void DetailsDialog::Impl::refreshPeerList(std::vector<tr::app::Torrent const*> const& torrents)
{
    auto const& store = peer_store_;

    for (auto const* const tor : torrents) {
        for (auto const& peer : tor->peers()) {
            auto item = tr::app::PeerListItem{ *tor, peer };
            auto const key = item.key();
            peer_rows_.add_or_update(
                key,
                [&] {
                    auto row = PeerRow{ .ref = {}, .item = std::make_unique<tr::app::PeerListItem>(std::move(item)) };
                    auto const iter = store->append();
                    row.ref = Gtk::TreeRowReference{ store, store->get_path(iter) };
                    set_peer_row(iter, *row.item);
                    return row;
                },
                [&](PeerRow& row) {
                    *row.item = std::move(item);
                    set_peer_row(store->get_iter(row.ref.get_path()), *row.item);
                });
        }
    }

    // erases each row before its item goes
    peer_rows_.remove_unseen([&store](PeerRow const& row) { store->erase(store->get_iter(row.ref.get_path())); });
}

void DetailsDialog::Impl::refreshWebseedList(std::vector<tr::app::Torrent const*> const& torrents)
{
    auto has_any_webseeds = false;
    auto const& store = webseed_store_;

    for (auto const* const tor : torrents) {
        for (auto const& webseed : tor->webseeds()) {
            has_any_webseeds = true;

            auto const set_speed = [&webseed](Gtk::TreeModel::iterator const& iter) {
                (*iter)[webseed_cols.download_rate_speed] = webseed.download_speed;
                (*iter)[webseed_cols.download_rate_string] = webseed.is_downloading ? webseed.download_speed.to_string() :
                                                                                      std::string{};
            };

            webseed_rows_.add_or_update(
                { tor->id(), webseed.url },
                [&] {
                    auto const iter = store->append();
                    (*iter)[webseed_cols.url] = webseed.url;
                    set_speed(iter);
                    return Gtk::TreeRowReference{ store, store->get_path(iter) };
                },
                [&](Gtk::TreeRowReference const& ref) { set_speed(store->get_iter(ref.get_path())); });
        }
    }

    webseed_rows_.remove_unseen([&store](Gtk::TreeRowReference const& ref) { store->erase(store->get_iter(ref.get_path())); });

    /* most of the time there are no webseeds...
       don't waste space showing an empty list */
    webseed_view_->set_visible(has_any_webseeds);
}

void DetailsDialog::Impl::refreshPeers()
{
    auto const torrents = app_torrents();
    refreshPeerList(torrents);
    refreshWebseedList(torrents);
}

bool DetailsDialog::Impl::onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip)
{
    Gtk::TreeModel::iterator iter;
    if (!peer_view_->get_tooltip_context_iter(x, y, keyboard_tip, iter)) {
        return false;
    }

    auto const* const item = iter->get_value(peer_cols.item);
    if (item == nullptr) {
        return false;
    }

    auto const& peer = item->peer();
    auto const* const tor = core_->torrents().find(item->torrent_id());
    auto const name = tor != nullptr ? std::string{ std::string_view{ tor->name() } } : std::string{};

    std::ostringstream gstr;
    gstr << "<b>" << Glib::Markup::escape_text(name) << "</b>\n" << Glib::Markup::escape_text(peer.address) << "\n \n";

    auto const flags = tr::app::PeerFlags::from_string(peer.flags);
    for (auto const& [flag, letter] : tr::app::PeerFlagLetters) {
        if (flags.contains(flag)) {
            gstr << letter << ": " << peer_flag_description(flag) << '\n';
        }
    }

    auto str = gstr.str();
    str.resize(str.size() - 1); // remove the last linefeed
    tooltip->set_markup(str);
    return true;
}

namespace
{

[[nodiscard]] Gtk::TreeViewColumn* make_text_column(
    Glib::ustring const& title,
    Gtk::TreeModelColumn<Glib::ustring> const& model_column,
    float const xalign = 0.0F)
{
    auto* const r = Gtk::make_managed<Gtk::CellRendererText>();
    r->property_xalign() = xalign;
    auto* const c = Gtk::make_managed<Gtk::TreeViewColumn>(title, *r);
    c->add_attribute(r->property_text(), model_column);
    return c;
}

[[nodiscard]] Gtk::TreeViewColumn* make_peer_view_column(PeerColumn const column)
{
    switch (column) {
    case PeerColumn::Encrypted:
        {
            auto* const r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
            r->property_xalign() = 0.0F;
            r->property_yalign() = 0.5F;
            auto* const c = Gtk::make_managed<Gtk::TreeViewColumn>(Glib::ustring(), *r);
            c->add_attribute(r->property_icon_name(), peer_cols.encryption_stock_id);
            c->set_sizing(TR_GTK_TREE_VIEW_COLUMN_SIZING(FIXED));
            c->set_fixed_width(20);
            return c;
        }

    case PeerColumn::UploadSpeed:
        return make_text_column(_("Up"), peer_cols.upload_rate_string, 1.0F);

    case PeerColumn::UploadRequests:
        return make_text_column(_("Up Reqs"), peer_cols.upload_request_count_string);

    case PeerColumn::DownloadSpeed:
        return make_text_column(_("Down"), peer_cols.download_rate_string, 1.0F);

    case PeerColumn::DownloadRequests:
        return make_text_column(_("Dn Reqs"), peer_cols.download_request_count_string);

    case PeerColumn::Progress:
        {
            auto* const r = Gtk::make_managed<Gtk::CellRendererProgress>();
            // % is percent done
            auto* const c = Gtk::make_managed<Gtk::TreeViewColumn>(_("%"), *r);
            c->add_attribute(r->property_text(), peer_cols.progress_string);
            return c;
        }

    case PeerColumn::Flags:
        return make_text_column(_("Flags"), peer_cols.flags);

    case PeerColumn::Address:
        return make_text_column(_("Address"), peer_cols.address);

    case PeerColumn::Client:
        return make_text_column(_("Client"), peer_cols.client);
    }

    std::abort();
}

void setPeerViewColumns(Gtk::TreeView* const peer_view, tr::app::Prefs const& prefs)
{
    bool const more = prefs.get<bool>(TR_KEY_show_extra_peer_details);

    /* remove any existing columns */
    peer_view->remove_all_columns();

    for (auto const column : tr::app::PeerColumns) {
        if (more || !tr::app::is_extra_peer_column(column)) {
            auto* const c = make_peer_view_column(column);
            c->set_resizable(false);
            c->set_sort_column(col(column));
            peer_view->append_column(*c);
        }
    }

    /* the 'expander' column has a 10-pixel margin on the left
       that doesn't look quite correct in any of these columns...
       so create a non-visible column and assign it as the
       'expander column. */
    auto* const c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_visible(false);
    peer_view->append_column(*c);
    peer_view->set_expander_column(*c);
}

} // namespace

void DetailsDialog::Impl::onMorePeerInfoToggled()
{
    tr_quark const key = TR_KEY_show_extra_peer_details;
    bool const value = more_peer_details_check_->get_active();
    core_->set_pref(key, value);
    setPeerViewColumns(peer_view_, prefs_);
}

void DetailsDialog::Impl::peer_page_init(Glib::RefPtr<Gtk::Builder> const& builder)
{
    /* webseeds */

    webseed_store_ = Gtk::ListStore::create(webseed_cols);
    // GTK cannot sort a column of a C++ type by itself
    webseed_store_->set_sort_func(webseed_cols.download_rate_speed, [](SortIter const& a, SortIter const& b) {
        return to_sort_result(
            a->get_value(webseed_cols.download_rate_speed) <=> b->get_value(webseed_cols.download_rate_speed));
    });
    auto* v = gtr_get_widget<Gtk::TreeView>(builder, "webseeds_view");
    v->set_model(webseed_store_);
    setup_item_view_button_event_handling(*v, {}, [v](double view_x, double view_y) {
        return on_item_view_button_released(*v, view_x, view_y);
    });

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Web Seeds"), *r);
        c->add_attribute(r->property_text(), webseed_cols.url);
        c->set_expand(true);
        c->set_sort_column(webseed_cols.url);
        v->append_column(*c);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Down"), *r);
        c->add_attribute(r->property_text(), webseed_cols.download_rate_string);
        c->set_sort_column(webseed_cols.download_rate_speed);
        v->append_column(*c);
    }

    /* peers */

    peer_store_ = Gtk::ListStore::create(peer_cols);
    auto const m = Gtk::TreeModelSort::create(peer_store_);
    for (auto const column : tr::app::PeerColumns) {
        m->set_sort_func(col(column), [column](SortIter const& a, SortIter const& b) {
            auto const* const lhs = a->get_value(peer_cols.item);
            auto const* const rhs = b->get_value(peer_cols.item);
            if (lhs == nullptr || rhs == nullptr) {
                return to_sort_result(static_cast<int>(lhs != nullptr) <=> static_cast<int>(rhs != nullptr));
            }
            return to_sort_result(lhs->compare(*rhs, column));
        });
    }
    m->set_sort_column(col(tr::app::DefaultPeerSortColumn), TR_GTK_SORT_TYPE(ASCENDING));

    peer_view_->set_model(m);
    peer_view_->set_has_tooltip(true);
    peer_view_->signal_query_tooltip().connect(sigc::mem_fun(*this, &Impl::onPeerViewQueryTooltip), false);
    setup_item_view_button_event_handling(*peer_view_, {}, [this](double view_x, double view_y) {
        return on_item_view_button_released(*peer_view_, view_x, view_y);
    });

    setPeerViewColumns(peer_view_, prefs_);

    more_peer_details_check_->set_active(prefs_.get<bool>(TR_KEY_show_extra_peer_details));
    more_peer_details_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onMorePeerInfoToggled));
}

/****
*****
*****  TRACKER
*****
****/

namespace
{

auto constexpr ErrMarkupBegin = "<span color='red'>"sv;
auto constexpr ErrMarkupEnd = "</span>"sv;
auto constexpr TimeoutMarkupBegin = "<span color='#246'>"sv;
auto constexpr TimeoutMarkupEnd = "</span>"sv;
auto constexpr SuccessMarkupBegin = "<span color='#080'>"sv;
auto constexpr SuccessMarkupEnd = "</span>"sv;

std::array<std::string_view, 3> const text_dir_mark = { ""sv, "\u200E"sv, "\u200F"sv };

[[nodiscard]] std::string get_status_line_markup(tr::app::TrackerStatusLine const& line, time_t const now)
{
    using Phrase = tr::app::TrackerStatusLine::Phrase;

    auto const time_span = [&line, now] {
        return tr_format_time_relative(now, line.time);
    };

    switch (line.phrase) {
    case Phrase::LastAnnounceGotPeers:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround the peer text
            fmt::runtime(ngettext(
                "Got a list of {markup_begin}{peer_count:L} peer{markup_end} {time_span_ago}",
                "Got a list of {markup_begin}{peer_count:L} peers{markup_end} {time_span_ago}",
                line.peer_count)),
            fmt::arg("markup_begin", SuccessMarkupBegin),
            fmt::arg("peer_count", line.peer_count),
            fmt::arg("markup_end", SuccessMarkupEnd),
            fmt::arg("time_span_ago", time_span()));

    case Phrase::LastAnnounceTimedOut:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround the time_span
            fmt::runtime(_("Peer list request {markup_begin}timed out {time_span_ago}{markup_end}; will retry")),
            fmt::arg("markup_begin", TimeoutMarkupBegin),
            fmt::arg("time_span_ago", time_span()),
            fmt::arg("markup_end", TimeoutMarkupEnd));

    case Phrase::LastAnnounceError:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround the error
            fmt::runtime(_("Got an error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
            fmt::arg("markup_begin", ErrMarkupBegin),
            fmt::arg("error", Glib::Markup::escape_text(std::string{ line.message })),
            fmt::arg("markup_end", ErrMarkupEnd),
            fmt::arg("time_span_ago", time_span()));

    case Phrase::AnnounceInactive:
        return _("No updates scheduled");

    case Phrase::AnnounceWaiting:
        return fmt::format(
            fmt::runtime(_("Asking for more peers {time_span_from_now}")),
            fmt::arg("time_span_from_now", time_span()));

    case Phrase::AnnounceQueued:
        return _("Queued to ask for more peers");

    case Phrase::AnnounceActive:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround time_span_ago
            fmt::runtime(_("Asked for more peers {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", time_span()),
            fmt::arg("markup_end", "</small>"));

    case Phrase::LastScrapeGotCounts:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround the seeder/leecher text
            fmt::runtime(_(
                "Tracker had {markup_begin}{seeder_count:L} {seeder_or_seeders} and {leecher_count:L} {leecher_or_leechers}{markup_end} {time_span_ago}")),
            fmt::arg("seeder_count", line.seeder_count),
            fmt::arg("seeder_or_seeders", ngettext("seeder", "seeders", line.seeder_count)),
            fmt::arg("leecher_count", line.leecher_count),
            fmt::arg("leecher_or_leechers", ngettext("leecher", "leechers", line.leecher_count)),
            fmt::arg("time_span_ago", time_span()),
            fmt::arg("markup_begin", SuccessMarkupBegin),
            fmt::arg("markup_end", SuccessMarkupEnd));

    case Phrase::LastScrapeNoCounts:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround "no information"
            fmt::runtime(_("Tracker had {markup_begin}no information{markup_end} on peer counts {time_span_ago}")),
            fmt::arg("markup_begin", SuccessMarkupBegin),
            fmt::arg("markup_end", SuccessMarkupEnd),
            fmt::arg("time_span_ago", time_span()));

    case Phrase::LastScrapeError:
        return fmt::format(
            // {markup_begin} and {markup_end} should surround the error text
            fmt::runtime(_("Got a scrape error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
            fmt::arg("error", Glib::Markup::escape_text(std::string{ line.message })),
            fmt::arg("time_span_ago", time_span()),
            fmt::arg("markup_begin", ErrMarkupBegin),
            fmt::arg("markup_end", ErrMarkupEnd));

    case Phrase::ScrapeWaiting:
        return fmt::format(
            fmt::runtime(_("Asking for peer counts {time_span_from_now}")),
            fmt::arg("time_span_from_now", time_span()));

    case Phrase::ScrapeQueued:
        return _("Queued to ask for peer counts");

    case Phrase::ScrapeActive:
        return fmt::format(
            fmt::runtime(_("Asked for peer counts {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", time_span()),
            fmt::arg("markup_end", "</small>"));
    }

    return {};
}

void buildTrackerSummary(
    std::ostream& gstr,
    std::string_view const key,
    tr::app::TrackerStat const& tracker,
    bool const show_scrape,
    Gtk::TextDirection const direction)
{
    auto const dir_mark = text_dir_mark.at(static_cast<int>(direction));
    auto const host = tracker.host.sv();

    // hostname
    gstr << dir_mark;
    gstr << (tracker.is_backup ? "<i>" : "<b>");
    gstr << Glib::Markup::escape_text(!key.empty() ? fmt::format("{:s} - {:s}", host, key) : std::string{ host });
    gstr << (tracker.is_backup ? "</i>" : "</b>");

    auto const now = time(nullptr);
    for (auto const& line : tr::app::tracker_status_lines(tracker, show_scrape)) {
        gstr << '\n' << dir_mark << get_status_line_markup(line, now);
    }
}

class TrackerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TrackerModelColumns() noexcept
    {
        add(torrent_id);
        add(text);
        add(is_backup);
        add(tracker_id);
        add(favicon);
        add(was_updated);
        add(key);
    }

    Gtk::TreeModelColumn<tr_torrent_id_t> torrent_id;
    Gtk::TreeModelColumn<Glib::ustring> text;
    Gtk::TreeModelColumn<bool> is_backup;
    Gtk::TreeModelColumn<int> tracker_id;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> favicon;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<std::string> key;
};

TrackerModelColumns const tracker_cols;

} // namespace

bool DetailsDialog::Impl::trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter)
{
    /* show all */
    if (all_check_->get_active()) {
        return true;
    }

    /* don't show the backups... */
    return !iter->get_value(tracker_cols.is_backup);
}

tr_torrent_id_t DetailsDialog::Impl::tracker_list_get_current_torrent_id() const
{
    // if there's only one torrent in the dialog, always use it
    if (ids_.size() == 1) {
        return ids_.front();
    }

    // otherwise, use the selected tracker's torrent
    auto const sel = tracker_view_->get_selection();
    if (auto const iter = sel->get_selected(); iter) {
        return iter->get_value(tracker_cols.torrent_id);
    }

    return -1;
}

tr::app::Torrent const* DetailsDialog::Impl::tracker_list_get_current_torrent() const
{
    return core_->torrents().find(tracker_list_get_current_torrent_id());
}

namespace
{

void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, Gtk::TreeRowReference& reference)
{
    if (pixbuf != nullptr && *pixbuf != nullptr) {
        auto const path = reference.get_path();
        auto const model = reference.get_model();

        if (auto const iter = model->get_iter(path); iter) {
            (*iter)[tracker_cols.favicon] = *pixbuf;
        }
    }
}

} // namespace

void DetailsDialog::Impl::refreshTracker()
{
    std::ostringstream gstr;
    auto& hash = tracker_hash_;
    auto const& store = tracker_store_;
    bool const showScrape = scrape_check_->get_active();

    /* step 1: get all the torrents */
    auto const torrents = app_torrents();

    // the key to find a tracker's row
    auto const row_key = [](tr::app::Torrent const& tor, tr::app::TrackerStat const& tracker) {
        return fmt::format("{:d}\t{:d}\t{:s}", tor.id(), tracker.tier, tracker.announce.sv());
    };

    /* step 2: mark all the trackers in the list as not-updated */
    for (auto& row : store->children()) {
        row[tracker_cols.was_updated] = false;
    }

    /* step 3: add / update trackers */
    for (auto const* const tor : torrents) {
        for (auto const& tracker : tor->tracker_stats()) {
            auto const key = row_key(*tor, tracker);
            if (!hash.contains(key)) {
                // if we didn't have that row, add it
                auto const iter = store->append();
                (*iter)[tracker_cols.torrent_id] = tor->id();
                (*iter)[tracker_cols.tracker_id] = static_cast<int>(tracker.id);
                (*iter)[tracker_cols.key] = key;

                auto const p = store->get_path(iter);
                hash.try_emplace(key, Gtk::TreeRowReference(store, p));
                core_->favicon_cache().load(
                    tracker.announce.sv(),
                    [ref = Gtk::TreeRowReference(store, p)](auto const* pixbuf_refptr) mutable {
                        favicon_ready_cb(pixbuf_refptr, ref);
                    });
            }
        }
    }

    /* step 4: update the rows */
    // with several torrents, each row names its own
    auto const name_rows = std::size(torrents) > 1;
    for (auto const* const tor : torrents) {
        auto const summary_name = name_rows ? std::string_view{ tor->name() } : ""sv;
        for (auto const& tracker : tor->tracker_stats()) {
            auto const iter = store->get_iter(hash.at(row_key(*tor, tracker)).get_path());

            // update the row
            gstr.str({});
            buildTrackerSummary(gstr, summary_name, tracker, showScrape, dialog_.get_direction());
            (*iter)[tracker_cols.text] = gstr.str();
            (*iter)[tracker_cols.is_backup] = tracker.is_backup;
            (*iter)[tracker_cols.tracker_id] = static_cast<int>(tracker.id);
            (*iter)[tracker_cols.was_updated] = true;
        }
    }

    /* step 5: remove trackers that have disappeared */
    if (auto iter = store->children().begin(); iter) {
        while (iter) {
            if (iter->get_value(tracker_cols.was_updated)) {
                ++iter;
            } else {
                auto const key = iter->get_value(tracker_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    edit_trackers_button_->set_sensitive(tracker_list_get_current_torrent_id() > 0);
}

void DetailsDialog::Impl::refreshFiles(std::vector<tr_torrent*> const& torrents)
{
    if (torrents.size() == 1) {
        file_list_->set_torrent(tr_torrentId(torrents.front()));
        file_list_->show();
        file_label_->hide();
    } else {
        file_list_->clear();
        file_list_->hide();
        file_label_->show();
    }
}

void DetailsDialog::Impl::onScrapeToggled()
{
    tr_quark const key = TR_KEY_show_tracker_scrapes;
    bool const value = scrape_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

void DetailsDialog::Impl::onBackupToggled()
{
    tr_quark const key = TR_KEY_show_backup_trackers;
    bool const value = all_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

namespace
{

class EditTrackersDialog : public Gtk::Dialog
{
public:
    EditTrackersDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr::app::Torrent const& torrent);
    EditTrackersDialog(EditTrackersDialog&&) = delete;
    EditTrackersDialog(EditTrackersDialog const&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog&&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog const&) = delete;
    ~EditTrackersDialog() override = default;

    [[nodiscard]] static std::unique_ptr<EditTrackersDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr::app::Torrent const& torrent);

protected:
    void on_response(int response) override;

private:
    void on_set_done(Session::Tag tag, Session::SetResult const& result);

    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    Gtk::TextView* const urls_view_;
    Session::Tag pending_set_ = {};
    sigslot::scoped_connection set_done_tag_;
};

EditTrackersDialog::EditTrackersDialog(
    BaseObjectType* const cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Torrent const& torrent)
    : Gtk::Dialog(cast_item)
    , core_(core)
    , torrent_id_(torrent.id())
    , urls_view_(gtr_get_widget<Gtk::TextView>(builder, "urls_view"))
    , set_done_tag_(core->torrent_set_done.connect_scoped(&EditTrackersDialog::on_set_done, this))
{
    set_title(fmt::format(fmt::runtime(_("{torrent_name} - Edit Trackers")), fmt::arg("torrent_name", torrent.name())));
    set_transient_for(parent);

    urls_view_->get_buffer()->set_text(std::string{ torrent.tracker_list().sv() });
}

std::unique_ptr<EditTrackersDialog> EditTrackersDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Torrent const& torrent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("EditTrackersDialog.ui"));
    return std::unique_ptr<EditTrackersDialog>(
        gtr_get_widget_derived<EditTrackersDialog>(builder, "EditTrackersDialog", parent, core, torrent));
}

void EditTrackersDialog::on_response(int const response)
{
    if (response != TR_GTK_RESPONSE_TYPE(ACCEPT)) {
        close();
        return;
    }

    // stays open until the session answers, so a rejected list can be fixed
    pending_set_ = core_->set_torrents({ torrent_id_ }, TR_KEY_tracker_list, urls_view_->get_buffer()->get_text(false).raw());
    set_sensitive(false);
}

void EditTrackersDialog::on_set_done(Session::Tag const tag, Session::SetResult const& result)
{
    if (tag != pending_set_) {
        return;
    }

    pending_set_ = {};
    set_sensitive(true);

    if (result.success) {
        close();
        return;
    }

    auto w = std::make_shared<Gtk::MessageDialog>(
        *this,
        _("List contains invalid URLs"),
        false,
        TR_GTK_MESSAGE_TYPE(ERROR),
        TR_GTK_BUTTONS_TYPE(CLOSE),
        true);
    w->set_secondary_text(_("Please correct the errors and try again."));
    w->signal_response().connect([w](int /*response*/) mutable { w.reset(); });
    w->show();
}

} // namespace

void DetailsDialog::Impl::on_edit_trackers()
{
    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr) {
        auto d = std::shared_ptr<EditTrackersDialog>(EditTrackersDialog::create(dialog_, core_, *tor));
        gtr_window_on_close(*d, [d] mutable { d.reset(); });
        d->show();
    }
}

void DetailsDialog::Impl::on_tracker_list_selection_changed()
{
    int const n = tracker_view_->get_selection()->count_selected_rows();
    auto const* const tor = tracker_list_get_current_torrent();

    remove_tracker_button_->set_sensitive(n > 0);
    add_tracker_button_->set_sensitive(tor != nullptr);
    edit_trackers_button_->set_sensitive(tor != nullptr);
}

namespace
{

class AddTrackerDialog : public Gtk::Dialog
{
public:
    AddTrackerDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr::app::Torrent const& torrent);
    AddTrackerDialog(AddTrackerDialog&&) = delete;
    AddTrackerDialog(AddTrackerDialog const&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog&&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog const&) = delete;
    ~AddTrackerDialog() override = default;

    [[nodiscard]] static std::unique_ptr<AddTrackerDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr::app::Torrent const& torrent);

protected:
    void on_response(int response) override;

private:
    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    Gtk::Entry* const url_entry_;
};

AddTrackerDialog::AddTrackerDialog(
    BaseObjectType* const cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Torrent const& torrent)
    : Gtk::Dialog(cast_item)
    , core_(core)
    , torrent_id_(torrent.id())
    , url_entry_(gtr_get_widget<Gtk::Entry>(builder, "url_entry"))
{
    set_title(fmt::format(fmt::runtime(_("{torrent_name} - Add Tracker")), fmt::arg("torrent_name", torrent.name())));
    set_transient_for(parent);

    auto* const accept = get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
#if GTKMM_CHECK_VERSION(4, 0, 0)
    set_default_widget(*accept);
#else
    set_default(*accept);
#endif

    gtr_paste_clipboard_url_into_entry(*url_entry_);
}

std::unique_ptr<AddTrackerDialog> AddTrackerDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Torrent const& torrent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("AddTrackerDialog.ui"));
    return std::unique_ptr<AddTrackerDialog>(
        gtr_get_widget_derived<AddTrackerDialog>(builder, "AddTrackerDialog", parent, core, torrent));
}

void AddTrackerDialog::on_response(int response)
{
    bool destroy = true;

    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT)) {
        auto const url = gtr_str_strip(url_entry_->get_text());

        if (!url.empty()) {
            if (tr_urlIsValidTracker(url.c_str())) {
                // TODO(ckerr) migrate to `TR_KEY_tracker_list`
                std::ignore = core_->set_torrents({ torrent_id_ }, TR_KEY_tracker_add, std::span{ &url.raw(), 1U });
            } else {
                gtr_unrecognized_url_dialog(*this, url);
                destroy = false;
            }
        }
    }

    if (destroy) {
        close();
    }
}

} // namespace

void DetailsDialog::Impl::on_tracker_list_add_button_clicked()
{
    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr) {
        auto d = std::shared_ptr<AddTrackerDialog>(AddTrackerDialog::create(dialog_, core_, *tor));
        gtr_window_on_close(*d, [d] mutable { d.reset(); });
        d->show();
    }
}

void DetailsDialog::Impl::on_tracker_list_remove_button_clicked()
{
    auto* v = tracker_view_;
    auto const sel = v->get_selection();

    if (auto const iter = sel->get_selected(); iter) {
        auto const torrent_id = iter->get_value(tracker_cols.torrent_id);
        auto const tracker_id = iter->get_value(tracker_cols.tracker_id);

        // TODO(ckerr): migrate to `TR_KEY_tracker_list`
        std::ignore = core_->set_torrents({ torrent_id }, TR_KEY_tracker_remove, std::array{ tracker_id });
    }
}

void DetailsDialog::Impl::tracker_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    int const pad = (GUI_PAD + GUI_PAD_BIG) / 2;

    tracker_store_ = Gtk::ListStore::create(tracker_cols);

    trackers_filtered_ = Gtk::TreeModelFilter::create(tracker_store_);
    trackers_filtered_->set_visible_func(sigc::mem_fun(*this, &Impl::trackerVisibleFunc));

    tracker_view_->set_model(trackers_filtered_);
    setup_item_view_button_event_handling(
        *tracker_view_,
        [this](guint /*button*/, TrGdkModifierType /*state*/, double view_x, double view_y, bool context_menu_requested) {
            return on_item_view_button_pressed(*tracker_view_, view_x, view_y, context_menu_requested);
        },
        [this](double view_x, double view_y) { return on_item_view_button_released(*tracker_view_, view_x, view_y); });

    auto const sel = tracker_view_->get_selection();
    sel->signal_changed().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_selection_changed));

    auto* c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_title(_("Trackers"));
    tracker_view_->append_column(*c);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        r->property_width() = 20 + (GUI_PAD_SMALL * 2);
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        r->property_yalign() = 0.0F;
        c->pack_start(*r, false);
        c->add_attribute(r->property_pixbuf(), tracker_cols.favicon);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        c->pack_start(*r, true);
        c->add_attribute(r->property_markup(), tracker_cols.text);
    }

    add_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_add_button_clicked));
    edit_trackers_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_edit_trackers));
    remove_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_remove_button_clicked));

    scrape_check_->set_active(prefs_.get<bool>(TR_KEY_show_tracker_scrapes));
    scrape_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onScrapeToggled));

    all_check_->set_active(prefs_.get<bool>(TR_KEY_show_backup_trackers));
    all_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onBackupToggled));
}

/****
*****  DIALOG
****/

void DetailsDialog::Impl::refresh()
{
    auto const torrents = getTorrents();

    refreshInfo(torrents);
    refreshPeers();
    refreshTracker();
    refreshFiles(torrents);
    refreshOptions();

    if (torrents.empty()) {
        dialog_.response(TR_GTK_RESPONSE_TYPE(CLOSE));
    }
}

void DetailsDialog::Impl::on_details_window_size_allocated()
{
    int w = 0;
    int h = 0;
#if GTKMM_CHECK_VERSION(4, 0, 0)
    dialog_.get_default_size(w, h);
#else
    dialog_.get_size(w, h);
#endif
    prefs_.set(TR_KEY_details_window_width, w);
    prefs_.set(TR_KEY_details_window_height, h);
}

DetailsDialog::Impl::~Impl()
{
    torrents_changed_tag_.disconnect();
    periodic_refresh_tag_.disconnect();
}

std::unique_ptr<DetailsDialog> DetailsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs& prefs)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("DetailsDialog.ui"));
    return std::unique_ptr<DetailsDialog>(gtr_get_widget_derived<DetailsDialog>(builder, "DetailsDialog", parent, core, prefs));
}

DetailsDialog::DetailsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs& prefs)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, prefs))
{
    set_transient_for(parent);
}

DetailsDialog::~DetailsDialog() = default;

DetailsDialog::Impl::Impl(
    DetailsDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs& prefs)
    : dialog_(dialog)
    , core_(core)
    , prefs_(prefs)
    , honor_limits_check_(gtr_get_widget<Gtk::CheckButton>(builder, "honor_limits_check"))
    , up_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "upload_limit_check"))
    , up_limit_sping_(gtr_get_widget<Gtk::SpinButton>(builder, "upload_limit_spin"))
    , down_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "download_limit_check"))
    , down_limit_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "download_limit_spin"))
    , bandwidth_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "priority_combo"))
    , ratio_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "ratio_limit_combo"))
    , ratio_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "ratio_limit_spin"))
    , idle_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "idle_limit_combo"))
    , idle_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "idle_limit_spin"))
    , max_peers_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "max_peers_spin"))
    , added_lb_(gtr_get_widget<Gtk::Label>(builder, "added_value_label"))
    , size_lb_(gtr_get_widget<Gtk::Label>(builder, "torrent_size_value_label"))
    , state_lb_(gtr_get_widget<Gtk::Label>(builder, "state_value_label"))
    , have_lb_(gtr_get_widget<Gtk::Label>(builder, "have_value_label"))
    , dl_lb_(gtr_get_widget<Gtk::Label>(builder, "downloaded_value_label"))
    , ul_lb_(gtr_get_widget<Gtk::Label>(builder, "uploaded_value_label"))
    , error_lb_(gtr_get_widget<Gtk::Label>(builder, "error_value_label"))
    , date_started_lb_(gtr_get_widget<Gtk::Label>(builder, "running_time_value_label"))
    , eta_lb_(gtr_get_widget<Gtk::Label>(builder, "remaining_time_value_label"))
    , last_activity_lb_(gtr_get_widget<Gtk::Label>(builder, "last_activity_value_label"))
    , hash_lb_(gtr_get_widget<Gtk::Label>(builder, "hash_value_label"))
    , privacy_lb_(gtr_get_widget<Gtk::Label>(builder, "privacy_value_label"))
    , origin_lb_(gtr_get_widget<Gtk::Label>(builder, "origin_value_label"))
    , destination_lb_(gtr_get_widget<Gtk::Label>(builder, "location_value_label"))
    , webseed_view_(gtr_get_widget<Gtk::ScrolledWindow>(builder, "webseeds_view_scroll"))
    , peer_view_(gtr_get_widget<Gtk::TreeView>(builder, "peers_view"))
    , more_peer_details_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_peer_details_check"))
    , add_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "add_tracker_button"))
    , edit_trackers_button_(gtr_get_widget<Gtk::Button>(builder, "edit_tracker_button"))
    , remove_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "remove_tracker_button"))
    , tracker_view_(gtr_get_widget<Gtk::TreeView>(builder, "trackers_view"))
    , scrape_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_tracker_details_check"))
    , all_check_(gtr_get_widget<Gtk::CheckButton>(builder, "backup_trackers_check"))
    , file_list_(gtr_get_widget_derived<FileList>(builder, "files_view_scroll", "files_view", core, 0))
    , file_label_(gtr_get_widget<Gtk::Label>(builder, "files_label"))
{
    /* return saved window size */
    auto const width = prefs_.get<int>(TR_KEY_details_window_width);
    auto const height = prefs_.get<int>(TR_KEY_details_window_height);
#if GTKMM_CHECK_VERSION(4, 0, 0)
    dialog_.set_default_size(width, height);
    dialog_.property_default_width().signal_changed().connect(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated));
    dialog_.property_default_height().signal_changed().connect(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated));
#else
    dialog_.resize(width, height);
    dialog_.signal_size_allocate().connect(sigc::hide<0>(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated)));
#endif

    dialog_.signal_response().connect(sigc::hide<0>(sigc::mem_fun(dialog_, &DetailsDialog::close)));

    info_page_init(builder);
    peer_page_init(builder);
    tracker_page_init(builder);
    options_page_init(builder);

    set_done_tag_ = core_->torrent_set_done.connect_scoped(&Impl::on_set_done, this);

    periodic_refresh_tag_ = Glib::signal_timeout().connect_seconds(
        [this] {
            core_->refresh_torrents(ids_, tr::app::TorrentKeySet::DetailStats);
            return refresh(), true;
        },
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);

    torrents_changed_tag_ = core_->signal_torrents_changed().connect(
        [this](tr::app::TorrentList::Ids const& ids, tr::app::TorrentFieldSet const& fields) {
            auto const shown = [this](tr_torrent_id_t const id) {
                return std::ranges::find(ids_, id) != std::end(ids_);
            };
            if (!std::ranges::any_of(ids, shown)) {
                return;
            }
            if ((fields & tr::app::Torrent::fields<TR_KEY_tracker_stats>()).any()) {
                refreshTracker();
            }
            if ((fields & tr::app::Torrent::fields<TR_KEY_peers, TR_KEY_webseeds_ex>()).any()) {
                refreshPeers();
            }
            if ((fields & OptionFields).any()) {
                refreshOptions();
            }
        });

    auto* const n = gtr_get_widget<Gtk::Notebook>(builder, "dialog_pages");
    n->set_current_page(static_cast<int>(last_page_));
    n->signal_switch_page().connect([](Gtk::Widget* /*page*/, guint page_number) { last_page_ = page_number; });
}

void DetailsDialog::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    impl_->set_torrents(ids);
}

void DetailsDialog::refresh()
{
    impl_->refresh();
}

void DetailsDialog::Impl::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    Glib::ustring title;
    auto const len = ids.size();

    ids_ = ids;
    core_->refresh_torrents(ids_, tr::app::TorrentKeySet::DetailAll);

    if (len == 1) {
        int const id = ids.front();
        auto const* tor = core_->find_torrent(id);
        title = fmt::format(fmt::runtime(_("{torrent_name} Properties")), fmt::arg("torrent_name", tr_torrentName(tor)));
    } else {
        title = fmt::format(
            fmt::runtime(ngettext("Properties - {torrent_count:L} Torrent", "Properties - {torrent_count:L} Torrents", len)),
            fmt::arg("torrent_count", len));
    }

    dialog_.set_title(title);

    refresh();
}
