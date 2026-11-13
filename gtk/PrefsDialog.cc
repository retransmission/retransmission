// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "PrefsDialog.h"

#include "FreeSpaceLabel.h"
#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h"
#include "Session.h"
#include "SystemTrayIcon.h"
#include "Utils.h"

#include <libtransmission/string-utils.h>
#include <libtransmission/transmission.h>
#include <libtransmission/types.h>
#include <libtransmission/web-utils.h>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/editable.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textview.h>
#include <gtkmm/treemodelcolumn.h>
#include <gtkmm/widget.h>

#include <glibmm/date.h>
#include <glibmm/datetime.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/timer.h>
#include <glibmm/ustring.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/eventcontrollerfocus.h>
#endif

#include <sigc++/adaptors/track_obj.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <sigslot/signal.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace tr::Values;

/**
***
**/

class PrefsDialog::Impl
{
public:
    Impl(
        PrefsDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

private:
    void response_cb(int response);

private:
    PrefsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
};

/**
***
**/

void PrefsDialog::Impl::response_cb(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(HELP)) {
        gtr_open_uri(gtr_get_help_uri() + "/html/preferences.html");
    }

    if (response == TR_GTK_RESPONSE_TYPE(CLOSE)) {
        dialog_.close();
    }
}

namespace
{

class PageBase : public Gtk::Box
{
public:
    PageBase(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    PageBase(PageBase&&) = delete;
    PageBase(PageBase const&) = delete;
    PageBase& operator=(PageBase&&) = delete;
    PageBase& operator=(PageBase const&) = delete;
    ~PageBase() override;

    Gtk::CheckButton* init_check_button(Glib::ustring const& name, tr_quark key);
    Gtk::Entry* init_entry(Glib::ustring const& name, tr_quark key);
    Gtk::TextView* init_text_view(Glib::ustring const& name, tr_quark key);
    PathButton* init_chooser_button(Glib::ustring const& name, tr_quark key);
    Gtk::ComboBox* init_encryption_combo(Glib::ustring const& name, tr_quark key);
    Gtk::ComboBox* init_time_combo(Glib::ustring const& name, tr_quark key);
    Gtk::ComboBox* init_week_combo(Glib::ustring const& name, tr_quark key);

    template<typename T>
    Gtk::SpinButton* init_spin_button(Glib::ustring const& name, tr_quark const key, T const low, T const high, T const step)
    {
        auto* button = get_widget<Gtk::SpinButton>(name);
        button->set_adjustment(
            Gtk::Adjustment::create(
                static_cast<double>(prefs_.get<T>(key)),
                static_cast<double>(low),
                static_cast<double>(high),
                static_cast<double>(step)));
        button->set_digits(std::is_floating_point_v<T> ? 2 : 0);
        button->signal_value_changed().connect([this, button, key] { spun_cb<T>(*button, key); });
        return button;
    }

    [[nodiscard]] tr::app::Prefs const& prefs() const noexcept
    {
        return prefs_;
    }

    template<typename T>
    T* get_widget(Glib::ustring const& name) const
    {
        return gtr_get_widget<T>(builder_, name);
    }

    template<typename T, typename... ArgTs>
    T* get_widget_derived(Glib::ustring const& name, ArgTs&&... args) const
    {
        return gtr_get_widget_derived<T>(builder_, name, std::forward<ArgTs>(args)...);
    }

    template<typename T, typename... ArgTs>
    static void localize_label(T& widget, ArgTs&&... args)
    {
        widget.set_label(fmt::format(fmt::runtime(widget.get_label().raw()), std::forward<ArgTs>(args)...));
    }

private:
    template<typename T>
    bool spun_cb_idle(Gtk::SpinButton const& spin, tr_quark const key)
    {
        // How long the spin button must be idle before we push the value to core_.
        // Prevents flooding the core with intermediate values while the user spins.
        static constexpr auto SpinIdleThresholdSec = 0.33;

        auto const last_change_it = spin_timers_.find(key);
        g_return_val_if_fail(last_change_it != spin_timers_.end(), false);

        // if the user is still making changes, then do nothing yet
        if (last_change_it->second.first->elapsed() < SpinIdleThresholdSec) {
            return true;
        }

        // update the core
        core_->set_pref(key, static_cast<T>(spin.get_value()));

        // cleanup
        spin_timers_.erase(last_change_it);
        return false;
    }

    template<typename T>
    void spun_cb(Gtk::SpinButton& w, tr_quark const key)
    {
        // user may be spinning through many values, so let's hold off
        // for a moment to keep from flooding the core with changes
        auto last_change_it = spin_timers_.find(key);
        if (last_change_it == spin_timers_.end()) {
            auto timeout_tag = Glib::signal_timeout().connect_seconds([this, &w, key] { return spun_cb_idle<T>(w, key); }, 1);
            last_change_it = spin_timers_.emplace(key, std::pair(std::make_unique<Glib::Timer>(), timeout_tag)).first;
        }

        last_change_it->second.first->start();
    }

    void entry_changed_cb(Gtk::Entry& w, tr_quark key);

    void chosen_cb(PathButton& w, tr_quark key);

    void onIntComboChanged(Gtk::ComboBox& combo_box, tr_quark key);

    [[nodiscard]] static auto get_weekday_string(Glib::Date::Weekday weekday);

private:
    Glib::RefPtr<Gtk::Builder> const builder_;
    Glib::RefPtr<Session> const core_;
    tr::app::Prefs const& prefs_;

    std::map<tr_quark, std::pair<std::unique_ptr<Glib::Timer>, sigc::connection>> spin_timers_;
};

PageBase::PageBase(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : Gtk::Box(cast_item)
    , builder_(builder)
    , core_(core)
    , prefs_(prefs)
{
}

PageBase::~PageBase()
{
    for (auto& [key, info] : spin_timers_) {
        info.second.disconnect();
    }
}

Gtk::CheckButton* PageBase::init_check_button(Glib::ustring const& name, tr_quark const key)
{
    auto* button = get_widget<Gtk::CheckButton>(name);
    button->set_active(prefs_.get<bool>(key));
    button->signal_toggled().connect([this, button, key] { core_->set_pref(key, button->get_active()); });
    return button;
}

void PageBase::entry_changed_cb(Gtk::Entry& w, tr_quark const key)
{
    core_->set_pref(key, w.get_text());
}

Gtk::Entry* PageBase::init_entry(Glib::ustring const& name, tr_quark const key)
{
    auto* const entry = get_widget<Gtk::Entry>(name);

    if (auto const value = prefs_.get<std::string>(key); !value.empty()) {
        entry->set_text(value);
    }

    entry->signal_changed().connect([this, entry, key] { entry_changed_cb(*entry, key); });
    return entry;
}

Gtk::TextView* PageBase::init_text_view(Glib::ustring const& name, tr_quark const key)
{
    auto* const view = get_widget<Gtk::TextView>(name);

    auto buffer = view->get_buffer();
    buffer->set_text(prefs_.get<std::string>(key));

    auto const save_buffer = [this, buffer, key] {
        core_->set_pref(key, buffer->get_text());
    };

#if GTKMM_CHECK_VERSION(4, 0, 0)
    auto focus_controller = Gtk::EventControllerFocus::create();
    focus_controller->signal_leave().connect(save_buffer);
    view->add_controller(focus_controller);
#else
    view->add_events(Gdk::FOCUS_CHANGE_MASK);
    view->signal_focus_out_event().connect_notify(sigc::hide<0>(save_buffer));
#endif

    return view;
}

void PageBase::chosen_cb(PathButton& w, tr_quark const key)
{
    core_->set_pref(key, w.get_filename());
}

PathButton* PageBase::init_chooser_button(Glib::ustring const& name, tr_quark const key)
{
    auto* const button = get_widget_derived<PathButton>(name);

    if (auto const path = prefs_.get<std::string>(key); !path.empty()) {
        button->set_filename(path);
    }

    button->signal_selection_changed().connect([this, button, key] { chosen_cb(*button, key); });
    return button;
}

void PageBase::onIntComboChanged(Gtk::ComboBox& combo_box, tr_quark const key)
{
    core_->set_pref(key, gtr_combo_box_get_active_enum(combo_box));
}

Gtk::ComboBox* PageBase::init_encryption_combo(Glib::ustring const& name, tr_quark const key)
{
    auto* const combo = get_widget<Gtk::ComboBox>(name);
    gtr_combo_box_set_enum(
        *combo,
        {
            { _("Allow encryption"), TR_CLEAR_PREFERRED },
            { _("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
            { _("Require encryption"), TR_ENCRYPTION_REQUIRED },
        });
    auto const mode = prefs_.get<tr_encryption_mode>(key);
    gtr_combo_box_set_active_enum(*combo, static_cast<int>(mode));
    combo->signal_changed().connect([this, combo, key] { onIntComboChanged(*combo, key); });
    return combo;
}

Gtk::ComboBox* PageBase::init_time_combo(Glib::ustring const& name, tr_quark const key)
{
    class TimeModelColumns : public Gtk::TreeModelColumnRecord
    {
    public:
        TimeModelColumns() noexcept
        {
            add(offset);
            add(title);
        }

        Gtk::TreeModelColumn<int> offset;
        Gtk::TreeModelColumn<Glib::ustring> title;
    };

    static TimeModelColumns const time_cols;

    /* build a store at 15 minute intervals */
    auto store = Gtk::ListStore::create(time_cols);

    for (int i = 0; i < 60 * 24; i += 15) {
        auto const iter = store->append();
        (*iter)[time_cols.offset] = i;
        (*iter)[time_cols.title] = fmt::format("{:02}:{:02}", i / 60, i % 60);
    }

    /* build the widget */
    auto* const combo = get_widget<Gtk::ComboBox>(name);
    combo->set_model(store);
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();
    combo->pack_start(*r, true);
    combo->add_attribute(r->property_text(), time_cols.title);
    combo->set_active(prefs_.get<int>(key) / 15);
    combo->signal_changed().connect([this, combo, key] {
        if (auto const iter = combo->get_active(); iter) {
            core_->set_pref(key, iter->get_value(time_cols.offset));
        }
    });
    return combo;
}

auto PageBase::get_weekday_string(Glib::Date::Weekday weekday)
{
    auto date = Glib::Date{};
    date.set_time_current();
    date.add_days(static_cast<int>(weekday) - static_cast<int>(date.get_weekday()));
    return date.format_string("%A");
}

Gtk::ComboBox* PageBase::init_week_combo(Glib::ustring const& name, tr_quark const key)
{
    auto* const combo = get_widget<Gtk::ComboBox>(name);
    gtr_combo_box_set_enum(
        *combo,
        {
            { _("Every Day"), TR_SCHED_ALL },
            { _("Weekdays"), TR_SCHED_WEEKDAY },
            { _("Weekends"), TR_SCHED_WEEKEND },
            { get_weekday_string(Glib::Date::Weekday::MONDAY), TR_SCHED_MON },
            { get_weekday_string(Glib::Date::Weekday::TUESDAY), TR_SCHED_TUES },
            { get_weekday_string(Glib::Date::Weekday::WEDNESDAY), TR_SCHED_WED },
            { get_weekday_string(Glib::Date::Weekday::THURSDAY), TR_SCHED_THURS },
            { get_weekday_string(Glib::Date::Weekday::FRIDAY), TR_SCHED_FRI },
            { get_weekday_string(Glib::Date::Weekday::SATURDAY), TR_SCHED_SAT },
            { get_weekday_string(Glib::Date::Weekday::SUNDAY), TR_SCHED_SUN },
        });
    gtr_combo_box_set_active_enum(*combo, prefs_.get<int>(key));
    combo->signal_changed().connect([this, combo, key] { onIntComboChanged(*combo, key); });
    return combo;
}

/****
*****  Download Tab
****/

class DownloadingPage : public PageBase
{
public:
    DownloadingPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    DownloadingPage(DownloadingPage&&) = delete;
    DownloadingPage(DownloadingPage const&) = delete;
    DownloadingPage& operator=(DownloadingPage&&) = delete;
    DownloadingPage& operator=(DownloadingPage const&) = delete;
    ~DownloadingPage() override;

private:
    void on_core_prefs_changed(tr_quark key);

private:
    Glib::RefPtr<Session> const core_;

    FreeSpaceLabel* freespace_label_ = nullptr;

    sigc::connection core_prefs_tag_;
};

void DownloadingPage::on_core_prefs_changed(tr_quark const key)
{
    if (key == TR_KEY_download_dir) {
        // Prefs, not tr_session: the client's copy of the setting, which exists for every kind of session.
        freespace_label_->set_dir(prefs().get<std::string>(TR_KEY_download_dir));
    }
}

DownloadingPage::~DownloadingPage()
{
    core_prefs_tag_.disconnect();
}

DownloadingPage::DownloadingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
    , core_(core)
    , freespace_label_(get_widget_derived<FreeSpaceLabel>("download_dir_stats_label", core))
{
    core_prefs_tag_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &DownloadingPage::on_core_prefs_changed));

    init_check_button("watch_dir_check", TR_KEY_watch_dir_enabled);
    init_chooser_button("watch_dir_chooser", TR_KEY_watch_dir);
    init_check_button("show_options_dialog_check", TR_KEY_show_options_window);
    init_check_button("start_on_add_check", TR_KEY_start_added_torrents);
    init_check_button("trash_on_add_check", TR_KEY_trash_original_torrent_files);
    init_chooser_button("download_dir_chooser", TR_KEY_download_dir);
    init_spin_button<uint16_t>(
        "max_active_downloads_spin",
        TR_KEY_download_queue_size,
        0,
        std::numeric_limits<uint16_t>::max(),
        1);
    init_spin_button<uint16_t>(
        "max_inactive_time_spin",
        TR_KEY_queue_stalled_minutes,
        1,
        std::numeric_limits<uint16_t>::max(),
        15);
    init_check_button("append_suffix_to_incomplete_check", TR_KEY_rename_partial_files);
    init_check_button("incomplete_dir_check", TR_KEY_incomplete_dir_enabled);
    init_chooser_button("incomplete_dir_chooser", TR_KEY_incomplete_dir);
    init_check_button("download_done_script_check", TR_KEY_script_torrent_done_enabled);
    init_chooser_button("download_done_script_chooser", TR_KEY_script_torrent_done_filename);
    init_check_button("download_done_verify_check", TR_KEY_torrent_complete_verify_enabled);

    on_core_prefs_changed(TR_KEY_download_dir);
}

/****
*****  Torrent Tab
****/

class SeedingPage : public PageBase
{
public:
    SeedingPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    SeedingPage(SeedingPage&&) = delete;
    SeedingPage(SeedingPage const&) = delete;
    SeedingPage& operator=(SeedingPage&&) = delete;
    SeedingPage& operator=(SeedingPage const&) = delete;
    ~SeedingPage() override = default;
};

SeedingPage::SeedingPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
{
    init_check_button("stop_seeding_ratio_check", TR_KEY_seed_ratio_limited);
    init_spin_button<double>("stop_seeding_ratio_spin", TR_KEY_seed_ratio_limit, 0, 1000, 0.05);
    init_check_button("stop_seeding_timeout_check", TR_KEY_idle_seeding_limit_enabled);
    init_spin_button<uint16_t>("stop_seeding_timeout_spin", TR_KEY_idle_seeding_limit, 1, 40320, 5);
    init_check_button("seeding_done_script_check", TR_KEY_script_torrent_done_seeding_enabled);
    init_chooser_button("seeding_done_script_chooser", TR_KEY_script_torrent_done_seeding_filename);
}

/****
*****  Desktop Tab
****/

class DesktopPage : public PageBase
{
public:
    DesktopPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    DesktopPage(DesktopPage&&) = delete;
    DesktopPage(DesktopPage const&) = delete;
    DesktopPage& operator=(DesktopPage&&) = delete;
    DesktopPage& operator=(DesktopPage const&) = delete;
    ~DesktopPage() override = default;
};

DesktopPage::DesktopPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
{
    init_check_button("inhibit_hibernation_check", TR_KEY_inhibit_desktop_hibernation);

    // Starting minimized means starting as a tray icon alone, so the
    // start-minimized control is only offered where a tray icon can exist.
    if (SystemTrayIcon::is_available()) {
        auto* const check = init_check_button("show_systray_icon_check", TR_KEY_show_notification_area_icon);
        check->set_label(gtr_with_app_name(check->get_label()));
        init_check_button("start_minimized_check", TR_KEY_start_minimized);
    } else {
        get_widget<Gtk::CheckButton>("show_systray_icon_check")->hide();
        get_widget<Gtk::CheckButton>("start_minimized_check")->hide();
    }

    init_check_button("notify_on_torrent_add_check", TR_KEY_torrent_added_notification_enabled);
    init_check_button("notify_on_torrent_finish_check", TR_KEY_torrent_complete_notification_enabled);
    init_check_button("ding_no_torrent_finish_check", TR_KEY_torrent_complete_sound_enabled);
}

/****
*****  Peer Tab
****/

class PrivacyPage : public PageBase
{
    static auto constexpr BlocklistUpdateResultDisplayTimeoutInSeconds = 3U;

public:
    PrivacyPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    PrivacyPage(PrivacyPage&&) = delete;
    PrivacyPage(PrivacyPage const&) = delete;
    PrivacyPage& operator=(PrivacyPage&&) = delete;
    PrivacyPage& operator=(PrivacyPage const&) = delete;
    ~PrivacyPage() override;

private:
    void onBlocklistInfo();
    void updateBlocklistText();
    void onBlocklistUpdated(bool success);
    void onBlocklistUpdate();
    void on_blocklist_url_changed(Gtk::Editable* e);

private:
    Glib::RefPtr<Session> core_;

    Gtk::Button* updateBlocklistButton_ = nullptr;
    Gtk::Label* label_ = nullptr;
    Gtk::Label* date_label_ = nullptr;
    Gtk::CheckButton* check_ = nullptr;

    size_t rule_count_ = 0;
    // True while label_ shows a manual update's progress or result instead of the rule count.
    bool showing_update_status_ = false;

    sigslot::scoped_connection blocklist_updated_tag_;
    sigslot::scoped_connection blocklist_update_failed_tag_;
    sigslot::scoped_connection session_updated_tag_;
    sigc::connection blocklist_update_result_tag_;
};

void PrivacyPage::onBlocklistInfo()
{
    rule_count_ = static_cast<size_t>(core_->blocklist_size().value_or(0));
    if (!showing_update_status_) {
        updateBlocklistText();
    }

    auto const updated_at = core_->blocklist_date().value_or(std::chrono::sys_seconds{}).time_since_epoch().count();
    gtr_label_set_text(
        *date_label_,
        updated_at == 0 ? _("Blocklist has never been updated") :
                          fmt::format(
                              fmt::runtime(_("Blocklist last updated {date}")),
                              fmt::arg("date", Glib::DateTime::create_now_local(updated_at).format("%c"))));
}

void PrivacyPage::updateBlocklistText()
{
    gtr_label_set_text(
        *label_,
        fmt::format(
            fmt::runtime(ngettext("Blocklist has {count:L} entry", "Blocklist has {count:L} entries", rule_count_)),
            fmt::arg("count", rule_count_)));
}

/* prefs dialog is being destroyed, so stop listening to blocklist updates */
PrivacyPage::~PrivacyPage()
{
    blocklist_update_result_tag_.disconnect();
}

/* core says the blocklist was updated */
void PrivacyPage::onBlocklistUpdated(bool success)
{
    updateBlocklistButton_->set_sensitive(true);
    showing_update_status_ = true;
    label_->set_text(success ? _("Blocklist updated!") : _("Couldn't update blocklist"));

    blocklist_update_result_tag_ = Glib::signal_timeout().connect_seconds(
        [this] {
            showing_update_status_ = false;
            updateBlocklistText();
            return false;
        },
        BlocklistUpdateResultDisplayTimeoutInSeconds);
}

/* user pushed a button to update the blocklist */
void PrivacyPage::onBlocklistUpdate()
{
    updateBlocklistButton_->set_sensitive(false);

    showing_update_status_ = true;
    label_->set_text(_("Getting new blocklist…"));
    blocklist_update_result_tag_.disconnect();

    core_->update_blocklist();
}

void PrivacyPage::on_blocklist_url_changed(Gtk::Editable* e)
{
    auto const url = e->get_chars(0, -1);
    updateBlocklistButton_->set_sensitive(tr_urlIsValid(url.c_str()));
}

PrivacyPage::PrivacyPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
    , core_(core)
    , updateBlocklistButton_(get_widget<Gtk::Button>("update_blocklist_button"))
    , label_(get_widget<Gtk::Label>("blocklist_stats_label"))
    , date_label_(get_widget<Gtk::Label>("blocklist_date_label"))
    , check_(init_check_button("blocklist_check", TR_KEY_blocklist_enabled))
{
    init_encryption_combo("encryption_mode_combo", TR_KEY_encryption);

    auto* const blocklist_url_entry = init_entry("blocklist_url_entry", TR_KEY_blocklist_url);

    updateBlocklistButton_->signal_clicked().connect([this] { onBlocklistUpdate(); });
    updateBlocklistButton_->set_sensitive(check_->get_active());
    blocklist_url_entry->signal_changed().connect(
        [this, blocklist_url_entry] { on_blocklist_url_changed(blocklist_url_entry); });
    on_blocklist_url_changed(blocklist_url_entry);

    init_check_button("blocklist_autoupdate_check", TR_KEY_blocklist_updates_enabled);

    blocklist_updated_tag_ = core_->blocklist_updated.connect_scoped(
        [this](int64_t const /*rule_count*/) { onBlocklistUpdated(true); });
    blocklist_update_failed_tag_ = core_->blocklist_update_failed.connect_scoped(
        [this](std::string_view const /*errmsg*/) { onBlocklistUpdated(false); });
    // every session poll brings the blocklist's size and date
    session_updated_tag_ = core_->session_updated.connect_scoped(&PrivacyPage::onBlocklistInfo, this);
    onBlocklistInfo();
}

/****
*****  Remote Tab
****/

class RemotePage : public PageBase
{
    class WhitelistModelColumns : public Gtk::TreeModelColumnRecord
    {
    public:
        WhitelistModelColumns() noexcept
        {
            add(address);
        }

        Gtk::TreeModelColumn<Glib::ustring> address;
    };

    static WhitelistModelColumns const whitelist_cols;

public:
    RemotePage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    RemotePage(RemotePage&&) = delete;
    RemotePage(RemotePage const&) = delete;
    RemotePage& operator=(RemotePage&&) = delete;
    RemotePage& operator=(RemotePage const&) = delete;
    ~RemotePage() override = default;

private:
    void refreshWhitelist();
    void onAddressEdited(Glib::ustring const& path, Glib::ustring const& address);
    void onAddWhitelistClicked();
    void onRemoveWhitelistClicked();
    void refreshRPCSensitivity();

    void onLaunchClutchCB() const;

    [[nodiscard]] static Glib::RefPtr<Gtk::ListStore> whitelist_tree_model_new(std::string const& whitelist);

private:
    Glib::RefPtr<Session> core_;

    Gtk::TreeView* view_;
    Gtk::Button* remove_button_;
    Gtk::CheckButton* rpc_tb_;
    Gtk::CheckButton* auth_tb_;
    Gtk::CheckButton* whitelist_tb_;

    Glib::RefPtr<Gtk::ListStore> store_;
    std::vector<Gtk::Widget*> auth_widgets_;
    std::vector<Gtk::Widget*> whitelist_widgets_;
};

RemotePage::WhitelistModelColumns const RemotePage::whitelist_cols;

Glib::RefPtr<Gtk::ListStore> RemotePage::whitelist_tree_model_new(std::string const& whitelist)
{
    auto store = Gtk::ListStore::create(whitelist_cols);

    std::istringstream stream(whitelist);
    std::string s;

    while (std::getline(stream, s, ',')) {
        s = gtr_str_strip(s);

        if (s.empty()) {
            continue;
        }

        auto const iter = store->append();
        (*iter)[whitelist_cols.address] = s;
    }

    return store;
}

void RemotePage::refreshWhitelist()
{
    std::ostringstream gstr;

    for (auto const& row : store_->children()) {
        gstr << row.get_value(whitelist_cols.address) << ",";
    }

    auto str = gstr.str();
    if (!str.empty()) {
        str.resize(str.size() - 1); /* remove the trailing comma */
    }

    core_->set_pref(TR_KEY_rpc_whitelist, str);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void RemotePage::onAddressEdited(Glib::ustring const& path, Glib::ustring const& address)
{
    if (auto const iter = store_->get_iter(path); iter) {
        (*iter)[whitelist_cols.address] = address;
    }

    refreshWhitelist();
}

void RemotePage::onAddWhitelistClicked()
{
    auto const iter = store_->append();
    (*iter)[whitelist_cols.address] = "0.0.0.0";

    view_->set_cursor(store_->get_path(iter), *view_->get_column(0), true);
}

void RemotePage::onRemoveWhitelistClicked()
{
    auto const sel = view_->get_selection();

    if (auto const iter = sel->get_selected(); iter) {
        store_->erase(iter);
        refreshWhitelist();
    }
}

void RemotePage::refreshRPCSensitivity()
{
    bool const rpc_active = rpc_tb_->get_active();
    bool const auth_active = auth_tb_->get_active();
    bool const whitelist_active = whitelist_tb_->get_active();
    auto const sel = view_->get_selection();
    auto const have_addr = sel->get_selected();
    auto const n_rules = store_->children().size();

    for (auto* const widget : auth_widgets_) {
        widget->set_sensitive(rpc_active && auth_active);
    }

    for (auto* const widget : whitelist_widgets_) {
        widget->set_sensitive(rpc_active && whitelist_active);
    }

    remove_button_->set_sensitive(rpc_active && whitelist_active && have_addr && n_rules > 1);
}

void RemotePage::onLaunchClutchCB() const
{
    gtr_open_uri(core_->web_client_url());
}

RemotePage::RemotePage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
    , core_(core)
    , view_(get_widget<Gtk::TreeView>("rpc_whitelist_view"))
    , remove_button_(get_widget<Gtk::Button>("remove_from_rpc_whistlist_button"))
    , rpc_tb_(init_check_button("enable_rpc_check", TR_KEY_rpc_enabled))
    , auth_tb_(init_check_button("enable_rpc_auth_check", TR_KEY_rpc_authentication_required))
    , whitelist_tb_(init_check_button("rpc_whitelist_check", TR_KEY_rpc_whitelist_enabled))
{
    rpc_tb_->signal_toggled().connect([this] { refreshRPCSensitivity(); });
    auto* const open_button = get_widget<Gtk::Button>("open_web_client_button");
    open_button->signal_clicked().connect([this] { onLaunchClutchCB(); });

    init_spin_button<uint16_t>("rpc_port_spin", TR_KEY_rpc_port, 0, std::numeric_limits<uint16_t>::max(), 1);

    auth_tb_->signal_toggled().connect([this] { refreshRPCSensitivity(); });

    auto* const username_entry = init_entry("rpc_username_entry", TR_KEY_rpc_username);
    auth_widgets_.push_back(username_entry);
    auth_widgets_.push_back(get_widget<Gtk::Label>("rpc_username_label"));

    auto* const password_entry = init_entry("rpc_password_entry", TR_KEY_rpc_password);
    auth_widgets_.push_back(password_entry);
    auth_widgets_.push_back(get_widget<Gtk::Label>("rpc_password_label"));

    whitelist_tb_->signal_toggled().connect([this] { refreshRPCSensitivity(); });

    {
        store_ = whitelist_tree_model_new(prefs.get<std::string>(TR_KEY_rpc_whitelist));

        view_->set_model(store_);
        setup_item_view_button_event_handling(*view_, {}, [this](double view_x, double view_y) {
            return on_item_view_button_released(*view_, view_x, view_y);
        });

        whitelist_widgets_.push_back(view_);
        auto const sel = view_->get_selection();
        sel->signal_changed().connect([this] { refreshRPCSensitivity(); });

        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->signal_edited().connect(sigc::mem_fun(*this, &RemotePage::onAddressEdited));
        r->property_editable() = true;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>("", *r);
        c->add_attribute(r->property_text(), whitelist_cols.address);
        c->set_expand(true);
        view_->append_column(*c);

        whitelist_widgets_.push_back(get_widget<Gtk::Label>("rpc_whitelist_label"));

        remove_button_->signal_clicked().connect([this] { onRemoveWhitelistClicked(); });
        refreshRPCSensitivity();
        auto* add_button = get_widget<Gtk::Button>("add_to_rpc_whitelist_button");
        whitelist_widgets_.push_back(add_button);
        add_button->signal_clicked().connect([this] { onAddWhitelistClicked(); });
    }

    refreshRPCSensitivity();
}

/****
*****  Bandwidth Tab
****/

class SpeedPage : public PageBase
{
public:
    SpeedPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    SpeedPage(SpeedPage&&) = delete;
    SpeedPage(SpeedPage const&) = delete;
    SpeedPage& operator=(SpeedPage&&) = delete;
    SpeedPage& operator=(SpeedPage const&) = delete;
    ~SpeedPage() override = default;
};

SpeedPage::SpeedPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
{
    auto const speed_units_kbyps_str = Speed::units().display_name(Speed::Units::KByps);

    localize_label(
        *init_check_button("upload_limit_check", TR_KEY_speed_limit_up_enabled),
        fmt::arg("speed_units", speed_units_kbyps_str));
    init_spin_button<uint32_t>("upload_limit_spin", TR_KEY_speed_limit_up, 0, std::numeric_limits<uint32_t>::max(), 5);

    localize_label(
        *init_check_button("download_limit_check", TR_KEY_speed_limit_down_enabled),
        fmt::arg("speed_units", speed_units_kbyps_str));
    init_spin_button<uint32_t>("download_limit_spin", TR_KEY_speed_limit_down, 0, std::numeric_limits<uint32_t>::max(), 5);

    localize_label(*get_widget<Gtk::Label>("alt_upload_limit_label"), fmt::arg("speed_units", speed_units_kbyps_str));
    init_spin_button<uint32_t>("alt_upload_limit_spin", TR_KEY_alt_speed_up, 0, std::numeric_limits<uint32_t>::max(), 5);

    localize_label(*get_widget<Gtk::Label>("alt_download_limit_label"), fmt::arg("speed_units", speed_units_kbyps_str));
    init_spin_button<uint32_t>("alt_download_limit_spin", TR_KEY_alt_speed_down, 0, std::numeric_limits<uint32_t>::max(), 5);

    init_time_combo("alt_speed_start_time_combo", TR_KEY_alt_speed_time_begin);
    init_time_combo("alt_speed_end_time_combo", TR_KEY_alt_speed_time_end);
    init_check_button("alt_schedule_time_check", TR_KEY_alt_speed_time_enabled);
    init_week_combo("alt_speed_days_combo", TR_KEY_alt_speed_time_day);
}

/****
*****  Network Tab
****/

class NetworkPage : public PageBase
{
public:
    NetworkPage(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::Prefs const& prefs);
    NetworkPage(NetworkPage&&) = delete;
    NetworkPage(NetworkPage const&) = delete;
    NetworkPage& operator=(NetworkPage&&) = delete;
    NetworkPage& operator=(NetworkPage const&) = delete;
    ~NetworkPage() override;

private:
    void portTestSetSensitive();
    void updatePortStatusText();

    [[nodiscard]] static std::string_view getPortStatusText(tr::app::Session::PortStatus status) noexcept;

    // --- interface binding

    enum class Feedback : uint8_t { None, Warning, Error };

    struct BindAddressRow {
        tr_quark key;
        tr_address_type type;
        Gtk::ComboBox* combo;
        Gtk::MenuButton* filter_button;
        Gtk::Label* feedback;
        Glib::RefPtr<Gtk::ListStore> store;
        // the saved address and the choices the combo was last built from
        std::string options_key;
        // every address saved while the page is open, so one that has gone stays selectable
        std::vector<std::string> saved_values;
    };

    void onCorePrefsChanged(tr_quark key);
    void initInterfaceBinding();
    void refreshNetworkBinding();
    void onNetworkBinding(Session::NetworkBinding const& binding);
    void rebuildInterfaceCombo();
    void onInterfaceChosen();
    void updateInterfaceDetails();
    void rebuildBindAddressCombo(BindAddressRow& row);
    void onBindAddressChosen(BindAddressRow const& row);

    // --- filters shared by the interface list and both address lists

    struct Filter {
        std::string key; // a FilterDef's key
        std::vector<std::string> values; // an entry passes if it has any
    };

    [[nodiscard]] bool counts(Filter const& filter) const;
    // the filters in use that apply to `type`'s addresses, or to the interfaces without
    [[nodiscard]] std::vector<Filter> activeFilters(std::optional<tr_address_type> type) const;
    // whether `filters` leave `iface` in the interface list
    [[nodiscard]] bool interfacePasses(std::vector<Filter> const& filters, Session::NetworkInterface const& iface) const;
    // every value a filter has, with its name
    [[nodiscard]] std::vector<std::pair<std::string, Glib::ustring>> allFilterChoices(std::string_view key) const;
    // the values a filter can still pick, given the other filters
    [[nodiscard]] std::vector<std::pair<std::string, Glib::ustring>> filterChoices(std::string_view key) const;
    void setFilters(std::vector<Filter> filters);
    void initFilterButton(Gtk::MenuButton& button, std::optional<tr_address_type> type);
    void showFilterList(Gtk::Popover& popover, std::optional<tr_address_type> type);
    void showFilterChoices(Gtk::Popover& popover, std::optional<tr_address_type> type, std::string_view key);
    void renderFilterButtons();

    // Pango markup
    [[nodiscard]] static Glib::ustring describeAddress(Session::NetworkAddress const& address);
    // Pango markup
    [[nodiscard]] static Glib::ustring describeTags(std::vector<Session::NetworkAddress const*> const& addresses);

    [[nodiscard]] std::string savedInterface() const;
    [[nodiscard]] std::string chosenInterface() const;
    [[nodiscard]] Session::NetworkInterface const* findInterface(std::string_view name) const;

    static void setFeedback(Gtk::Label& label, Feedback feedback, Glib::ustring const& text);

private:
    Glib::RefPtr<Session> core_;
    tr::app::Prefs const& prefs_;

    Gtk::Label* portLabel_ = nullptr;
    Gtk::Button* portButton_ = nullptr;
    Gtk::SpinButton* portSpin_ = nullptr;

    Gtk::ComboBox* interfaceCombo_ = nullptr;
    Gtk::MenuButton* interfaceFilterButton_ = nullptr;
    Glib::RefPtr<Gtk::ListStore> interfaceStore_;
    Gtk::Label* interfaceAddressesLabel_ = nullptr;
    Gtk::Label* interfaceStatusLabel_ = nullptr;
    Gtk::Entry* rpcInterfaceEntry_ = nullptr;
    std::array<BindAddressRow, 2> bindAddresses_ = {};

    // the last report from the session, which lists its interfaces only when asked
    Session::NetworkBinding binding_;
    // the saved interface and the interface names the combo was last built from
    std::string interfaceOptionsKey_;
    // every interface saved while the page is open, so one that has gone stays selectable
    std::vector<std::string> savedInterfaces_;
    // set while a combo is changed from code, so that isn't taken as the user's choice
    bool updatingCombos_ = false;

    // By default, the interfaces NetworkManager set up, where it reports, and
    // no link-local address, which no peer or tracker can reach.
    // Not saved: the dialog starts with these each time it opens.
    std::vector<Filter> filters_;

    sigslot::scoped_connection portTag_;
    sigc::connection prefsTag_;
    sigc::connection bindingRefreshTag_;
};

namespace
{
class InterfaceComboColumns : public Gtk::TreeModelColumnRecord
{
public:
    InterfaceComboColumns() noexcept
    {
        add(id);
        add(label);
        add(sensitive);
    }

    Gtk::TreeModelColumn<Glib::ustring> id; // the bind_interface or bind address value
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<bool> sensitive; // shown by the bind address combos only
};

// an IPv6 address's type, the first of session_get's tags
struct RangeName {
    std::string_view tag;
    char const* name;
    char const* about;
};

auto constexpr RangeNames = std::array<RangeName, 9U>{ {
    { .tag = "gua", .name = N_("GUA"), .about = N_("global, reachable from the internet") },
    { .tag = "ula", .name = N_("ULA"), .about = N_("unique local, reachable only within your site") },
    { .tag = "link-local",
      .name = N_("Link-local"),
      .about = N_(
          "this link only, which no peer or tracker is on. Local peer discovery reaches the link through the interface, without a bind address.") },
    { .tag = "nat64", .name = N_("NAT64"), .about = N_("an IPv4 address translated by NAT64") },
    { .tag = "teredo", .name = N_("Teredo"), .about = N_("a Teredo tunnel") },
    { .tag = "6to4", .name = N_("6to4"), .about = N_("a 6to4 tunnel (deprecated)") },
    { .tag = "documentation", .name = N_("Documentation"), .about = N_("for examples only, not routed") },
    { .tag = "site-local", .name = N_("Site-local"), .about = N_("deprecated") },
    { .tag = "loopback", .name = N_("Loopback"), .about = N_("this host only") },
} };

// session_get's other tags, an address's origin
struct OriginName {
    std::string_view tag;
    std::string_view name;
    char const* about;
};

auto constexpr OriginNames = std::array<OriginName, 3U>{ {
    { .tag = "slaac-stable", .name = "SLAAC-stable", .about = N_("autoconfigured, keeps its address") },
    { .tag = "slaac-privacy", .name = "SLAAC-privacy", .about = N_("autoconfigured, temporary, replaced regularly") },
    { .tag = "dhcp", .name = "DHCP", .about = N_("leased from a DHCPv6 server") },
} };

// session_get's managed_by values
struct ManagerName {
    std::string_view value;
    char const* name;
};

auto constexpr ManagerNames = std::array<ManagerName, 4U>{ {
    { .value = "networkmanager", .name = "NetworkManager" },
    { .value = "docker", .name = "Docker" },
    { .value = "libvirt", .name = "libvirt" },
    { .value = "other", .name = N_("Other") },
} };

// The filters over the interface and bind address lists. Type and Origin
// describe IPv6 addresses only.
struct FilterDef {
    std::string_view key;
    char const* label;
    bool ipv6_only;
};

auto constexpr FilterDefs = std::array<FilterDef, 4U>{ {
    { .key = "managed_by", .label = N_("Managed by"), .ipv6_only = false },
    { .key = "interface", .label = N_("Interface"), .ipv6_only = false },
    { .key = "type", .label = N_("Type"), .ipv6_only = true },
    { .key = "origin", .label = N_("Origin"), .ipv6_only = true },
} };

[[nodiscard]] FilterDef const& filter_def(std::string_view const key)
{
    return *std::ranges::find(FilterDefs, key, &FilterDef::key);
}

// what a filter's chosen values are matched against: an interface's, or with
// `address`, one of its addresses'
[[nodiscard]] std::vector<std::string> filter_values(
    std::string_view const key,
    Session::NetworkAddress const* const address,
    Session::NetworkInterface const& iface)
{
    if (key == "managed_by") {
        return std::empty(iface.managed_by) ? std::vector<std::string>{} : std::vector<std::string>{ iface.managed_by };
    }
    if (key == "interface") {
        return { iface.name };
    }
    if (address == nullptr || std::empty(address->tags)) {
        return {};
    }
    if (key == "type") {
        return { address->tags.front() };
    }
    return { std::next(std::begin(address->tags)), std::end(address->tags) };
}

// A tunnel passes the Managed by filter whatever manages it: a VPN that
// wg-quick or OpenVPN brought up is no manager's, yet it is what a user binds to.
[[nodiscard]] bool passes_filter(
    std::string_view const key,
    std::vector<std::string> const& values,
    Session::NetworkAddress const* const address,
    Session::NetworkInterface const& iface)
{
    if (key == "managed_by" && iface.point_to_point) {
        return true;
    }
    return std::ranges::any_of(filter_values(key, address, iface), [&values](auto const& value) {
        return std::ranges::find(values, value) != std::end(values);
    });
}

// a link-local address is only reachable on its own link, which no peer or tracker is on
[[nodiscard]] bool is_link_local(Session::NetworkAddress const& address)
{
    return !std::empty(address.tags) && address.tags.front() == "link-local";
}

InterfaceComboColumns const interface_combo_cols;

// how often the page asks for the interfaces and the binding status while it's open
auto constexpr NetworkBindingRefreshSeconds = 5U;
} // namespace

void NetworkPage::onCorePrefsChanged(tr_quark const key)
{
    // mapping the page again catches up on what changed meanwhile
    if (!get_mapped()) {
        return;
    }

    switch (key) {
    case TR_KEY_bind_interface:
        rebuildInterfaceCombo();
        updateInterfaceDetails();
        // the status describes the binding, which just changed
        refreshNetworkBinding();
        break;

    case TR_KEY_bind_address_ipv4:
    case TR_KEY_bind_address_ipv6:
        for (auto& row : bindAddresses_) {
            if (row.key == key) {
                rebuildBindAddressCombo(row);
            }
        }
        break;

    default:
        break;
    }
}

NetworkPage::~NetworkPage()
{
    bindingRefreshTag_.disconnect();
    prefsTag_.disconnect();
}

// --- interface binding

std::string NetworkPage::savedInterface() const
{
    // the session strips surrounding whitespace before binding
    return std::string{ tr_strv_strip(prefs_.get<std::string>(TR_KEY_bind_interface)) };
}

std::string NetworkPage::chosenInterface() const
{
    if (auto const iter = interfaceCombo_->get_active(); iter) {
        return iter->get_value(interface_combo_cols.id).raw();
    }

    return savedInterface();
}

Session::NetworkInterface const* NetworkPage::findInterface(std::string_view const name) const
{
    auto const iter = std::ranges::find(binding_.interfaces, name, &Session::NetworkInterface::name);
    return iter != std::end(binding_.interfaces) ? &*iter : nullptr;
}

void NetworkPage::setFeedback(Gtk::Label& label, Feedback const feedback, Glib::ustring const& text)
{
    gtr_widget_set_css_class(label, "tr-feedback-error", feedback == Feedback::Error);
    gtr_widget_set_css_class(label, "tr-feedback-warning", feedback == Feedback::Warning);
    label.set_text(text);
    label.set_visible(!text.empty());
}

void NetworkPage::refreshNetworkBinding()
{
    // a slot to a member of this page is dropped if the page is gone when the reply comes
    core_->fetch_network_binding(sigc::mem_fun(*this, &NetworkPage::onNetworkBinding));
}

void NetworkPage::onNetworkBinding(Session::NetworkBinding const& binding)
{
    binding_ = binding;
    if (!get_mapped()) {
        return;
    }

    rpcInterfaceEntry_->set_text(binding_.rpc_bind_interface);
    rebuildInterfaceCombo();
    updateInterfaceDetails();
}

// A configured interface that has gone away stays selectable,
// so the page shows what the session is actually using.
void NetworkPage::rebuildInterfaceCombo()
{
    auto const current = savedInterface();
    auto const is_blocked = current == TrBindInterfaceBlocked;
    if (!std::empty(current) && !is_blocked && std::ranges::find(savedInterfaces_, current) == std::end(savedInterfaces_)) {
        savedInterfaces_.push_back(current);
    }

    // the interface in use stays listed whatever the filters say
    auto names = std::vector<std::string>{};
    for (auto const& iface : binding_.interfaces) {
        if (iface.name == current || interfacePasses(filters_, iface)) {
            names.push_back(iface.name);
        }
    }
    auto gone = std::vector<std::string>{};
    for (auto const& name : savedInterfaces_) {
        if (findInterface(name) == nullptr) {
            gone.push_back(name);
        }
    }

    auto key = current;
    for (auto const& name : names) {
        key += '\n';
        key += name;
    }
    for (auto const& name : gone) {
        key += "\n\t";
        key += name;
    }

    updatingCombos_ = true;

    // rebuilt only on change: replacing the model closes an open popup
    if (key != interfaceOptionsKey_) {
        interfaceOptionsKey_ = key;

        auto const add = [this](Glib::ustring const& id, Glib::ustring const& label) {
            auto row = *interfaceStore_->append();
            row[interface_combo_cols.id] = id;
            row[interface_combo_cols.label] = label;
        };

        interfaceStore_->clear();
        add({}, _("Any (default route)"));
        for (auto const& name : names) {
            add(name, name);
        }
        for (auto const& name : gone) {
            add(name, fmt::format(fmt::runtime(_("{interface} (unavailable)")), fmt::arg("interface", name)));
        }
        add(std::string{ TrBindInterfaceBlocked }, _("None (block all traffic)"));
    }

    auto const active_id = is_blocked ? std::string{ TrBindInterfaceBlocked } : current;
    for (auto const& row : interfaceStore_->children()) {
        if (row.get_value(interface_combo_cols.id).raw() == active_id) {
            interfaceCombo_->set_active(TR_GTK_TREE_MODEL_CHILD_ITER(row));
            break;
        }
    }

    updatingCombos_ = false;

    // an empty list means the session's platform can't bind to an interface
    interfaceCombo_->set_sensitive(!std::empty(binding_.interfaces) || !std::empty(current));
    renderFilterButtons();
}

void NetworkPage::onInterfaceChosen()
{
    if (updatingCombos_) {
        return;
    }

    core_->set_pref(TR_KEY_bind_interface, chosenInterface());
    updateInterfaceDetails();
}

// shows the chosen interface's addresses, what the saved binding does to
// traffic, and whether the bind addresses belong to the chosen interface
void NetworkPage::updateInterfaceDetails()
{
    auto const name = chosenInterface();
    auto const* const iface = findInterface(name);

    if (iface == nullptr) {
        interfaceAddressesLabel_->set_visible(false);
    } else {
        auto text = Glib::ustring{};
        for (auto const& address : iface->addresses) {
            if (!text.empty()) {
                text += '\n';
            }
            // grayed out, as no peer or tracker can reach it
            auto const line = describeAddress(address).raw();
            text += is_link_local(address) ? fmt::format(R"(<span alpha="50%">{}</span>)", line) : line;
        }
        interfaceAddressesLabel_->set_markup(text.empty() ? Glib::Markup::escape_text(_("No addresses")) : text);
        auto shown = std::vector<Session::NetworkAddress const*>{};
        for (auto const& address : iface->addresses) {
            shown.push_back(&address);
        }
        interfaceAddressesLabel_->set_tooltip_markup(describeTags(shown));
        interfaceAddressesLabel_->set_visible(true);
    }

    // the status describes the saved binding, not a choice still being saved
    auto feedback = Feedback::None;
    auto text = Glib::ustring{};
    if (name == savedInterface()) {
        if (binding_.status == "missing") {
            feedback = Feedback::Warning;
            text = fmt::format(
                fmt::runtime(
                    _("Kill switch on: {interface} is missing, so peer and tracker traffic is stopped until it returns.")),
                fmt::arg("interface", name));
        } else if (binding_.status == "bind_failed") {
            feedback = Feedback::Error;
            text = fmt::format(
                fmt::runtime(
                    _("Kill switch on: binding to {interface} failed, so peer and tracker traffic is stopped. {error}")),
                fmt::arg("interface", name),
                fmt::arg("error", binding_.error));
        } else if (binding_.status == "blocked") {
            feedback = Feedback::Warning;
            text = _("All peer and tracker traffic is blocked.");
        } else if (binding_.status == "unsupported") {
            feedback = Feedback::Error;
            text = fmt::format(
                fmt::runtime(_("This platform can't bind to {interface}, so peer and tracker traffic is stopped.")),
                fmt::arg("interface", name));
        }
    }
    setFeedback(*interfaceStatusLabel_, feedback, text);

    for (auto& row : bindAddresses_) {
        rebuildBindAddressCombo(row);
    }
}

// one line of markup for an address: the address and its origin,
// e.g. "2001:db8::1 · SLAAC-stable"
Glib::ustring NetworkPage::describeAddress(Session::NetworkAddress const& address)
{
    auto text = Glib::Markup::escape_text(address.address).raw();
    if (std::empty(address.tags)) {
        return text;
    }

    for (auto const& tag : address.tags | std::views::drop(1)) {
        auto const* const origin = std::ranges::find(OriginNames, tag, &OriginName::tag);
        text += fmt::format(" · {}", origin != std::end(OriginNames) ? origin->name : std::string_view{ tag });
    }
    return text;
}

// markup explaining the types and origins among `addresses`, one per line
Glib::ustring NetworkPage::describeTags(std::vector<Session::NetworkAddress const*> const& addresses)
{
    auto const has_tag = [&addresses](std::string_view const tag) {
        return std::ranges::any_of(addresses, [tag](auto const* address) {
            return std::ranges::find(address->tags, tag) != std::end(address->tags);
        });
    };

    auto ranges = std::string{};
    for (auto const& range : RangeNames) {
        if (has_tag(range.tag)) {
            ranges += fmt::format(
                "{}{}: {}",
                std::empty(ranges) ? "" : "\n",
                Glib::Markup::escape_text(_(range.name)).raw(),
                Glib::Markup::escape_text(_(range.about)).raw());
        }
    }

    auto origins = std::string{};
    for (auto const& origin : OriginNames) {
        if (has_tag(origin.tag)) {
            origins += fmt::format(
                "{}{}: {}",
                std::empty(origins) ? "" : "\n",
                origin.name,
                Glib::Markup::escape_text(_(origin.about)).raw());
        }
    }

    return std::empty(ranges) || std::empty(origins) ? ranges + origins : fmt::format("{}\n\n{}", ranges, origins);
}

// Lists the local addresses of the row's family a bind address can be: the chosen
// interface's, or every interface's while the default route is chosen.
// A saved address that no interface has stays selectable, as with interfaces.
void NetworkPage::rebuildBindAddressCombo(BindAddressRow& row)
{
    // "0.0.0.0" and "::" are older settings' spelling of any address
    auto saved = std::string{ tr_strv_strip(prefs_.get<std::string>(row.key)) };
    if (saved == "0.0.0.0" || saved == "::") {
        saved.clear();
    }
    if (!std::empty(saved) && std::ranges::find(row.saved_values, saved) == std::end(row.saved_values)) {
        row.saved_values.push_back(saved);
    }
    auto const chosen = chosenInterface();

    auto interfaces = std::vector<Session::NetworkInterface const*>{};
    if (std::empty(chosen)) {
        for (auto const& iface : binding_.interfaces) {
            interfaces.push_back(&iface);
        }
    } else if (auto const* const iface = findInterface(chosen); iface != nullptr) {
        interfaces.push_back(iface);
    }

    // the address in use stays listed whatever the filters say
    auto const filters = activeFilters(row.type);
    auto const is_listed =
        [&row, &saved, &chosen, &filters](Session::NetworkAddress const& address, Session::NetworkInterface const& iface) {
            if (address.address.contains(':') != (row.type == TR_AF_INET6)) {
                return false;
            }
            return address.address == saved || std::ranges::all_of(filters, [&](Filter const& filter) {
                       // a chosen interface's addresses are listed whatever manages it
                       return (filter.key == "managed_by" && !std::empty(chosen)) ||
                           passes_filter(filter.key, filter.values, &address, iface);
                   });
        };

    struct Choice {
        std::string id;
        Glib::ustring label;
        bool sensitive = true;
    };
    auto choices = std::vector<Choice>{};
    choices.push_back(
        { .id = "",
          .label = Glib::Markup::escape_text(
              row.type == TR_AF_INET ? _("Any (0.0.0.0), recommended") : _("Any (::), recommended")) });
    auto listed = false;
    auto shown = std::vector<Session::NetworkAddress const*>{};
    for (auto const* const iface : interfaces) {
        auto has_heading = false;
        for (auto const& address : iface->addresses) {
            if (!is_listed(address, *iface)) {
                continue;
            }
            // A combo has no groups, so each interface gets a heading row that
            // can't be chosen, as the web UI's <optgroup> does, even when it's the
            // only one. Its id has a tab, which no address has, so no saved value matches it.
            if (!has_heading) {
                has_heading = true;
                choices.push_back(
                    { .id = fmt::format("\t{}", iface->name),
                      .label = fmt::format("<b>{}</b>", Glib::Markup::escape_text(iface->name).raw()),
                      .sensitive = false });
            }
            choices.push_back({ .id = address.address, .label = "    " + describeAddress(address) });
            listed = listed || address.address == saved;
            shown.push_back(&address);
        }
    }

    auto const advice = row.type == TR_AF_INET ?
        _("Recommended: Any. The chosen interface already keeps traffic on it.") :
        _("Recommended: Any. The chosen interface already keeps traffic on it, and IPv6 addresses change: SLAAC-privacy addresses are replaced regularly, and a new prefix from your provider replaces the rest. To pin one anyway, pick a SLAAC-stable or DHCP address.");
    auto tooltip = Glib::Markup::escape_text(advice).raw();
    if (auto const tags = describeTags(shown); !tags.empty()) {
        tooltip += "\n\n" + tags.raw();
    }
    row.combo->set_tooltip_markup(tooltip);
    for (auto const& address : row.saved_values) {
        if (std::ranges::none_of(shown, [&address](auto const* const s) { return s->address == address; })) {
            choices.push_back(
                { .id = address,
                  .label = Glib::Markup::escape_text(
                      fmt::format(fmt::runtime(_("{address} (unavailable)")), fmt::arg("address", address))) });
        }
    }

    auto key = std::string{};
    for (auto const& [id, label, sensitive] : choices) {
        key += id;
        key += '\t';
        key += label.raw();
        key += sensitive ? "\n" : "\t-\n";
    }

    updatingCombos_ = true;

    // rebuilt only on change: replacing the model closes an open popup
    if (key != row.options_key) {
        row.options_key = key;
        row.store->clear();
        for (auto const& [id, label, sensitive] : choices) {
            auto store_row = *row.store->append();
            store_row[interface_combo_cols.id] = id;
            store_row[interface_combo_cols.label] = label;
            store_row[interface_combo_cols.sensitive] = sensitive;
        }
    }

    for (auto const& store_row : row.store->children()) {
        if (store_row.get_value(interface_combo_cols.id).raw() == saved) {
            row.combo->set_active(TR_GTK_TREE_MODEL_CHILD_ITER(store_row));
            break;
        }
    }

    updatingCombos_ = false;

    renderFilterButtons();

    // binding an address the chosen interface doesn't have fails at runtime,
    // but the interface may be changed next, so this only warns;
    // an unavailable interface has no addresses to check against
    if (!std::empty(saved) && !listed && !std::empty(chosen) && chosen != TrBindInterfaceBlocked &&
        findInterface(chosen) != nullptr) {
        setFeedback(
            *row.feedback,
            Feedback::Warning,
            fmt::format(fmt::runtime(_("Not an address of {interface}")), fmt::arg("interface", chosen)));
    } else {
        setFeedback(*row.feedback, Feedback::None, {});
    }
}

void NetworkPage::onBindAddressChosen(BindAddressRow const& row)
{
    if (updatingCombos_) {
        return;
    }

    if (auto const iter = row.combo->get_active(); iter) {
        if (auto const value = iter->get_value(interface_combo_cols.id).raw(); value != prefs_.get<std::string>(row.key)) {
            core_->set_pref(row.key, value);
        }
    }
}

// --- filters

namespace
{
void box_append(Gtk::Box& box, Gtk::Widget& child)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    box.append(child);
#else
    box.pack_start(child, false, false, 0);
#endif
}

// a flat button whose label starts at its left edge, as a menu item's does
Gtk::Button& make_menu_item(Glib::ustring const& text)
{
    auto* const label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0);
    auto* const button = Gtk::make_managed<Gtk::Button>();
#if GTKMM_CHECK_VERSION(4, 0, 0)
    button->set_child(*label);
    button->set_has_frame(false);
#else
    button->add(*label);
    button->set_relief(Gtk::RELIEF_NONE);
#endif
    return *button;
}

void set_popover_content(Gtk::Popover& popover, Gtk::Box& box)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    popover.set_child(box);
#else
    if (popover.get_child() != nullptr) {
        popover.remove();
    }
    popover.add(box);
    box.show_all();
#endif
}
} // namespace

// A Managed by filter counts only while some interface has one of its managers,
// so the default one leaves the lists whole where nothing reports NetworkManager.
bool NetworkPage::counts(Filter const& filter) const
{
    return filter.key != "managed_by" || std::ranges::any_of(binding_.interfaces, [&filter](auto const& iface) {
               return std::ranges::find(filter.values, iface.managed_by) != std::end(filter.values);
           });
}

std::vector<NetworkPage::Filter> NetworkPage::activeFilters(std::optional<tr_address_type> const type) const
{
    auto ret = std::vector<Filter>{};
    for (auto const& filter : filters_) {
        if (counts(filter) && (!filter_def(filter.key).ipv6_only || type == TR_AF_INET6)) {
            ret.push_back(filter);
        }
    }
    return ret;
}

// An interface is listed when it passes the filters on interfaces and, while
// filters on IPv6 addresses are in use, still has an address to bind: an IPv4
// one, which they don't judge, or an IPv6 one they leave.
bool NetworkPage::interfacePasses(std::vector<Filter> const& filters, Session::NetworkInterface const& iface) const
{
    auto const passes = [&](bool const ipv6_only, Session::NetworkAddress const* const address) {
        return std::ranges::all_of(filters, [&](Filter const& filter) {
            return filter_def(filter.key).ipv6_only != ipv6_only || !counts(filter) ||
                passes_filter(filter.key, filter.values, address, iface);
        });
    };
    auto const any_address_filter = std::ranges::any_of(filters, [](Filter const& f) { return filter_def(f.key).ipv6_only; });

    return passes(false, nullptr) && (!any_address_filter || std::ranges::any_of(iface.addresses, [&](auto const& address) {
               return !address.address.contains(':') || passes(true, &address);
           }));
}

std::vector<std::pair<std::string, Glib::ustring>> NetworkPage::allFilterChoices(std::string_view const key) const
{
    auto ret = std::vector<std::pair<std::string, Glib::ustring>>{};
    if (key == "managed_by") {
        for (auto const& [value, name] : ManagerNames) {
            ret.emplace_back(value, _(name));
        }
    } else if (key == "interface") {
        for (auto const& iface : binding_.interfaces) {
            ret.emplace_back(iface.name, iface.name);
        }
    } else if (key == "type") {
        for (auto const& range : RangeNames) {
            ret.emplace_back(range.tag, _(range.name));
        }
    } else {
        for (auto const& origin : OriginNames) {
            ret.emplace_back(origin.tag, std::string{ origin.name });
        }
    }
    return ret;
}

// A value counts while something the other filters leave still has it, so
// each filter narrows the next.
std::vector<std::pair<std::string, Glib::ustring>> NetworkPage::filterChoices(std::string_view const key) const
{
    auto others = std::vector<Filter>{};
    for (auto const& filter : filters_) {
        if (filter.key != key && counts(filter)) {
            others.push_back(filter);
        }
    }
    auto const passes = [&others](bool const ipv6_only, Session::NetworkAddress const* address, auto const& iface) {
        return std::ranges::all_of(others, [&](Filter const& filter) {
            return filter_def(filter.key).ipv6_only != ipv6_only || passes_filter(filter.key, filter.values, address, iface);
        });
    };
    auto present = std::vector<std::string>{};
    auto const add = [&present](std::vector<std::string> const& values) {
        for (auto const& value : values) {
            if (std::ranges::find(present, value) == std::end(present)) {
                present.push_back(value);
            }
        }
    };
    for (auto const& iface : binding_.interfaces) {
        if (!filter_def(key).ipv6_only) {
            if (interfacePasses(others, iface)) {
                add(filter_values(key, nullptr, iface));
            }
            continue;
        }
        if (!passes(false, nullptr, iface)) {
            continue;
        }
        for (auto const& address : iface.addresses) {
            if (address.address.contains(':') && passes(true, &address, iface)) {
                add(filter_values(key, &address, iface));
            }
        }
    }

    auto ret = allFilterChoices(key);
    std::erase_if(ret, [&present](auto const& choice) {
        return std::ranges::find(present, choice.first) == std::end(present);
    });
    return ret;
}

void NetworkPage::setFilters(std::vector<Filter> filters)
{
    std::erase_if(filters, [](Filter const& filter) { return std::empty(filter.values); });
    filters_ = std::move(filters);
    rebuildInterfaceCombo();
    updateInterfaceDetails();
}

// Each ▽ button is dimmed until filters apply to its list, which its tooltip names.
void NetworkPage::renderFilterButtons()
{
    auto const render = [this](Gtk::MenuButton& button, std::optional<tr_address_type> const type) {
        auto text = std::string{};
        for (auto const& [key, values] : activeFilters(type)) {
            auto const choices = allFilterChoices(key);
            auto const name_of = [&choices](std::string const& value) {
                auto const iter = std::ranges::find(choices, value, &std::pair<std::string, Glib::ustring>::first);
                return iter != std::end(choices) ? iter->second.raw() : value;
            };
            auto names = std::vector<std::string>{};
            // a filter that keeps most values reads better as what it leaves out
            auto left_out = std::vector<std::string>{};
            for (auto const& [value, name] : choices) {
                if (std::ranges::find(values, value) == std::end(values)) {
                    left_out.push_back(name.raw());
                }
            }
            if (std::size(left_out) < std::size(values)) {
                names.push_back(fmt::format(fmt::runtime(_("not {values}")), fmt::arg("values", fmt::join(left_out, ", "))));
            } else {
                for (auto const& value : values) {
                    names.push_back(name_of(value));
                }
            }
            text += fmt::format("{}{}: {}", std::empty(text) ? "" : "; ", _(filter_def(key).label), fmt::join(names, ", "));
        }

        gtr_widget_set_css_class(button, "dim-label", std::empty(text));
        button.set_tooltip_text(
            std::empty(text) ? _("Filter the entries listed") :
                               fmt::format(fmt::runtime(_("Listing only what passes: {filters}")), fmt::arg("filters", text)));
    };

    render(*interfaceFilterButton_, {});
    for (auto const& row : bindAddresses_) {
        render(*row.filter_button, row.type);
    }
}

void NetworkPage::initFilterButton(Gtk::MenuButton& button, std::optional<tr_address_type> const type)
{
    auto* const popover = Gtk::make_managed<Gtk::Popover>();
    button.set_popover(*popover);
    popover->signal_show().connect([this, popover, type] { showFilterList(*popover, type); });
}

// The filters that apply to the button's list, each opening its choices.
// A filter with nothing left to choose isn't offered.
void NetworkPage::showFilterList(Gtk::Popover& popover, std::optional<tr_address_type> const type)
{
    // Managed by is offered only where something other than Other manages an interface
    auto const has_managers = std::ranges::any_of(binding_.interfaces, [](auto const& iface) {
        return !std::empty(iface.managed_by) && iface.managed_by != "other";
    });
    auto const active = activeFilters(type);
    auto* const box = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(VERTICAL));

    for (auto const& def : FilterDefs) {
        if ((def.key == "managed_by" && !has_managers) || (def.ipv6_only && type != TR_AF_INET6) ||
            std::empty(filterChoices(def.key))) {
            continue;
        }
        auto const is_active = std::ranges::find(active, def.key, &Filter::key) != std::end(active);
        auto& item = make_menu_item(fmt::format("{}{} ▸", _(def.label), is_active ? " ✓" : ""));
        // replaced on idle: the click is still being handled by the item the new view replaces
        item.signal_clicked().connect([this, &popover, type, key = def.key] {
            // tracked, so the view isn't replaced in a page or popover closed meanwhile
            Glib::signal_idle().connect_once(IF_GTKMM4(
                sigc::track_object,
                sigc::track_obj)([this, &popover, type, key] { showFilterChoices(popover, type, key); }, *this, popover));
        });
        box_append(*box, item);
    }

    if (!std::empty(active)) {
        auto& clear = make_menu_item(_("Clear filters"));
        // only this list's: an IPv6 Type filter outlives the IPv4 list's Clear
        clear.signal_clicked().connect([this, &popover, type] {
            auto const cleared = activeFilters(type);
            auto filters = filters_;
            std::erase_if(filters, [&cleared](Filter const& filter) {
                return std::ranges::find(cleared, filter.key, &Filter::key) != std::end(cleared);
            });
            popover.popdown();
            setFilters(std::move(filters));
        });
        box_append(*box, clear);
    }

    set_popover_content(popover, *box);
}

void NetworkPage::showFilterChoices(
    Gtk::Popover& popover,
    std::optional<tr_address_type> const type,
    std::string_view const key)
{
    auto* const box = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(VERTICAL));

    auto& back = make_menu_item(fmt::format("‹ {}", _(filter_def(key).label)));
    back.signal_clicked().connect([this, &popover, type] {
        Glib::signal_idle().connect_once(IF_GTKMM4(
            sigc::track_object,
            sigc::track_obj)([this, &popover, type] { showFilterList(popover, type); }, *this, popover));
    });
    box_append(*box, back);

    auto const choices = filterChoices(key);
    auto const iter = std::ranges::find(filters_, key, &Filter::key);
    auto const chosen = iter != std::end(filters_) ? iter->values : std::vector<std::string>{};
    for (auto const& [value, name] : choices) {
        auto* const check = Gtk::make_managed<Gtk::CheckButton>(name);
        check->set_active(std::ranges::find(chosen, value) != std::end(chosen));
        check->signal_toggled().connect([this, check, key = std::string{ key }, value, choices] {
            auto filters = filters_;
            auto it = std::ranges::find(filters, key, &Filter::key);
            if (it == std::end(filters)) {
                // a new filter goes last
                it = filters.insert(std::end(filters), Filter{ .key = key, .values = {} });
            }
            auto& values = it->values;
            std::erase(values, value);
            if (check->get_active()) {
                values.push_back(value);
            }
            // values keep the menu's order
            std::erase_if(values, [&choices](std::string const& v) {
                return std::ranges::find(choices, v, &std::pair<std::string, Glib::ustring>::first) == std::end(choices);
            });
            std::ranges::sort(values, {}, [&choices](std::string const& v) {
                return std::ranges::find(choices, v, &std::pair<std::string, Glib::ustring>::first) - std::begin(choices);
            });
            setFilters(std::move(filters));
        });
        box_append(*box, *check);
    }

    set_popover_content(popover, *box);
}

void NetworkPage::initInterfaceBinding()
{
    auto type_values = std::vector<std::string>{};
    for (auto const& range : RangeNames) {
        if (range.tag != "link-local") {
            type_values.emplace_back(range.tag);
        }
    }
    filters_ = {
        { .key = "managed_by", .values = { "networkmanager" } },
        { .key = "type", .values = std::move(type_values) },
    };
    initFilterButton(*interfaceFilterButton_, {});
    for (auto const& row : bindAddresses_) {
        initFilterButton(*row.filter_button, row.type);
    }

    interfaceStore_ = Gtk::ListStore::create(interface_combo_cols);
    interfaceCombo_->set_model(interfaceStore_);
    auto* const renderer = Gtk::make_managed<Gtk::CellRendererText>();
    interfaceCombo_->pack_start(*renderer, true);
    interfaceCombo_->add_attribute(renderer->property_text(), interface_combo_cols.label);
    interfaceCombo_->signal_changed().connect([this] { onInterfaceChosen(); });

    for (auto& row : bindAddresses_) {
        row.store = Gtk::ListStore::create(interface_combo_cols);
        row.combo->set_model(row.store);
        auto* const address_renderer = Gtk::make_managed<Gtk::CellRendererText>();
        row.combo->pack_start(*address_renderer, true);
        row.combo->add_attribute(address_renderer->property_markup(), interface_combo_cols.label);
        row.combo->add_attribute(address_renderer->property_sensitive(), interface_combo_cols.sensitive);
        row.combo->signal_changed().connect([this, &row] { onBindAddressChosen(row); });
    }

    rebuildInterfaceCombo();
    updateInterfaceDetails();

    // Polls only while the page is shown: a closed dialog's widgets are torn down,
    // but this page object outlives them, and other tabs don't show the binding.
    signal_map().connect([this] {
        rebuildInterfaceCombo();
        updateInterfaceDetails();
        refreshNetworkBinding();
        bindingRefreshTag_ = Glib::signal_timeout().connect_seconds(
            [this] {
                refreshNetworkBinding();
                return true;
            },
            NetworkBindingRefreshSeconds);
    });
    signal_unmap().connect([this] { bindingRefreshTag_.disconnect(); });
}

std::string_view NetworkPage::getPortStatusText(tr::app::Session::PortStatus const status) noexcept
{
    switch (status) {
    case tr::app::Session::PortStatus::Unknown:
        return C_("Port test status", "unknown");
    case tr::app::Session::PortStatus::Checking:
        return C_("Port test status", "checking…");
    case tr::app::Session::PortStatus::Open:
        return C_("Port test status", "open");
    case tr::app::Session::PortStatus::Closed:
        return C_("Port test status", "closed");
    case tr::app::Session::PortStatus::Error:
        return C_("Port test status", "error");
    default:
        return {};
    }
}

void NetworkPage::updatePortStatusText()
{
    auto const status_ipv4 = getPortStatusText(core_->port_status(TR_AF_INET));
    auto const status_ipv6 = getPortStatusText(core_->port_status(TR_AF_INET6));

    portLabel_->set_markup(
        core_->port_status(TR_AF_INET) == core_->port_status(TR_AF_INET6) ?
            fmt::format(fmt::runtime(_("Status: <b>{status}</b>")), fmt::arg("status", status_ipv4)) :
            fmt::format(
                fmt::runtime(_("Status: <b>{status_ipv4}</b> (IPv4), <b>{status_ipv6}</b> (IPv6)")),
                fmt::arg("status_ipv4", status_ipv4),
                fmt::arg("status_ipv6", status_ipv6)));
}

void NetworkPage::portTestSetSensitive()
{
    auto const sensitive = !core_->port_test_pending();
    portButton_->set_sensitive(sensitive);
    portSpin_->set_sensitive(sensitive);
}

NetworkPage::NetworkPage(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : PageBase(cast_item, builder, core, prefs)
    , core_(core)
    , prefs_(prefs)
    , portLabel_(get_widget<Gtk::Label>("listening_port_status_label"))
    , portButton_(get_widget<Gtk::Button>("test_listening_port_button"))
    , portSpin_(init_spin_button<uint16_t>("listening_port_spin", TR_KEY_peer_port, 1, std::numeric_limits<uint16_t>::max(), 1))
    , interfaceCombo_(get_widget<Gtk::ComboBox>("network_interface_combo"))
    , interfaceFilterButton_(get_widget<Gtk::MenuButton>("network_interface_filter_button"))
    , interfaceAddressesLabel_(get_widget<Gtk::Label>("network_interface_addresses_label"))
    , interfaceStatusLabel_(get_widget<Gtk::Label>("network_interface_status_label"))
    , rpcInterfaceEntry_(get_widget<Gtk::Entry>("rpc_bind_interface_entry"))
    , bindAddresses_{ {
          { .key = TR_KEY_bind_address_ipv4,
            .type = TR_AF_INET,
            .combo = get_widget<Gtk::ComboBox>("bind_address_ipv4_combo"),
            .filter_button = get_widget<Gtk::MenuButton>("bind_address_ipv4_filter_button"),
            .feedback = get_widget<Gtk::Label>("bind_address_ipv4_feedback_label"),
            .store = {},
            .options_key = {},
            .saved_values = {} },
          { .key = TR_KEY_bind_address_ipv6,
            .type = TR_AF_INET6,
            .combo = get_widget<Gtk::ComboBox>("bind_address_ipv6_combo"),
            .filter_button = get_widget<Gtk::MenuButton>("bind_address_ipv6_filter_button"),
            .feedback = get_widget<Gtk::Label>("bind_address_ipv6_feedback_label"),
            .store = {},
            .options_key = {},
            .saved_values = {} },
      } }
{
    initInterfaceBinding();

    portButton_->signal_clicked().connect([this] { core_->test_port(); });
    portTag_ = core_->port_test_changed.connect_scoped([this] {
        updatePortStatusText();
        portTestSetSensitive();
    });
    core_->forget_port_status();

    prefsTag_ = core_->signal_prefs_changed().connect([this](auto key) { onCorePrefsChanged(key); });

    init_check_button("pick_random_listening_port_at_start_check", TR_KEY_peer_port_random_on_start);
    init_check_button("enable_listening_port_forwarding_check", TR_KEY_port_forwarding_enabled);
    init_spin_button<uint16_t>(
        "max_torrent_peers_spin",
        TR_KEY_peer_limit_per_torrent,
        1,
        std::numeric_limits<uint16_t>::max(),
        5);
    init_spin_button<uint16_t>("max_total_peers_spin", TR_KEY_peer_limit_global, 1, std::numeric_limits<uint16_t>::max(), 5);

#ifdef WITH_UTP
    init_check_button("enable_utp_check", TR_KEY_utp_enabled);
#else
    get_widget<Gtk::CheckButton>("enable_utp_check")->hide();
#endif

    init_check_button("enable_pex_check", TR_KEY_pex_enabled);
    init_check_button("enable_dht_check", TR_KEY_dht_enabled);
    init_check_button("enable_lpd_check", TR_KEY_lpd_enabled);
    init_text_view("default_trackers_view", TR_KEY_default_trackers);
}

} // namespace

/****
*****
****/

std::unique_ptr<PrefsDialog> PrefsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("PrefsDialog.ui"));
    return std::unique_ptr<PrefsDialog>(gtr_get_widget_derived<PrefsDialog>(builder, "PrefsDialog", parent, core, prefs));
}

PrefsDialog::PrefsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, prefs))
{
    set_title(gtr_with_app_name(get_title()));
    set_transient_for(parent);
}

PrefsDialog::~PrefsDialog() = default;

PrefsDialog::Impl::Impl(
    PrefsDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs const& prefs)
    : dialog_(dialog)
    , core_(core)
{
    gtr_get_widget_derived<SpeedPage>(builder, "speed_page_layout", core_, prefs);
    gtr_get_widget_derived<DownloadingPage>(builder, "downloading_page_layout", core_, prefs);
    gtr_get_widget_derived<SeedingPage>(builder, "seeding_page_layout", core_, prefs);
    gtr_get_widget_derived<PrivacyPage>(builder, "privacy_page_layout", core_, prefs);
    gtr_get_widget_derived<NetworkPage>(builder, "network_page_layout", core_, prefs);
    gtr_get_widget_derived<DesktopPage>(builder, "desktop_page_layout", core_, prefs);
    gtr_get_widget_derived<RemotePage>(builder, "remote_page_layout", core_, prefs);

    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::response_cb));
}
