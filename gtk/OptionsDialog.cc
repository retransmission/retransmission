// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "OptionsDialog.h"

#include "FileList.h"
#include "FreeSpaceLabel.h"
#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission-app/add-data.h>
#include <libtransmission-app/session.h>

#include <libtransmission/file.h> /* tr_sys_path_is_same() */
#include <libtransmission/quark.h>
#include <libtransmission/types.h>

#include <gtkmm/checkbutton.h>
#include <gtkmm/combobox.h>
#include <gtkmm/filefilter.h>

#include <giomm/file.h>

#include <glibmm/i18n.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using namespace std::literals;

/****
*****
****/

namespace
{

auto const ShowOptionsDialogChoice = "show_options_dialog"sv; // TODO(C++20): Use ""s

} // namespace

/****
*****
****/

class OptionsDialog::Impl
{
public:
    Impl(
        OptionsDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        tr::app::AddData add,
        tr::app::AddOptions const& preset);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

private:
    void sourceChanged();
    void downloadDirChanged(PathButton* b);
    void updateFiles();

    void addResponseCB(int response);

private:
    OptionsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    tr::app::AddData add_;
    std::string downloadDir_;

    // the current source's `resolve_add()` token
    std::shared_ptr<std::monostate const> resolving_;

    PathButton* source_chooser_ = nullptr;
    FileList* file_list_ = nullptr;
    Gtk::CheckButton* run_check_ = nullptr;
    Gtk::CheckButton* trash_check_ = nullptr;
    Gtk::ComboBox* priority_combo_ = nullptr;
    FreeSpaceLabel* freespace_label_ = nullptr;
};

void OptionsDialog::Impl::addResponseCB(int response)
{
    resolving_.reset();

    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT) && add_.type != tr::app::AddData::Type::None) {
        auto options = tr::app::AddOptions{};
        options.download_dir = downloadDir_;
        options.paused = !run_check_->get_active();
        options.bandwidth_priority = static_cast<tr_priority_t>(gtr_combo_box_get_active_enum(*priority_combo_));
        options.set_file_choices(file_list_->preview_files());

        add_.trash_source = trash_check_->get_active();
        core_->add_torrent(std::move(add_), std::move(options));
    }

    dialog_.close();
}

void OptionsDialog::Impl::updateFiles()
{
    trash_check_->set_sensitive(add_.type == tr::app::AddData::Type::File);

    auto files = add_.files();
    file_list_->set_sensitive(!std::empty(files));
    file_list_->set_preview(std::move(files));
}

void OptionsDialog::Impl::sourceChanged()
{
    auto const filename = source_chooser_->get_filename();

    // empty when this dialog cleared the chooser after a dropped source
    if (filename.empty() || (add_.type == tr::app::AddData::Type::File && tr_sys_path_is_same(filename, add_.source))) {
        return;
    }

    // runs only while `resolving_` is this source's token, so `this` is alive
    resolving_ = std::make_shared<std::monostate const>();
    core_->resolve_add(tr::app::AddData{ filename }, resolving_, [this](std::optional<tr::app::AddData> resolved) {
        if (resolved) {
            add_ = std::move(*resolved);
        } else {
            // dropped: listed already, or not a torrent. The add report tells the user which.
            add_ = {};
            source_chooser_->set_filename({});
        }

        updateFiles();
    });
}

void OptionsDialog::Impl::downloadDirChanged(PathButton* b)
{
    auto const fname = b->get_filename();

    if (!fname.empty() && (downloadDir_.empty() || !tr_sys_path_is_same(fname, downloadDir_))) {
        downloadDir_ = fname;
        freespace_label_->set_dir(downloadDir_);
    }
}

namespace
{

template<typename FileChooserT>
void addTorrentFilters(FileChooserT* chooser)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("Torrent files"));
    filter->add_pattern("*.torrent");
    chooser->add_filter(filter);

    filter = Gtk::FileFilter::create();
    filter->set_name(_("All files"));
    filter->add_pattern("*");
    chooser->add_filter(filter);
}

} // namespace

/****
*****
****/

OptionsDialog::OptionsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::AddData add,
    tr::app::AddOptions const& preset)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, std::move(add), preset))
{
    set_transient_for(parent);
}

OptionsDialog::~OptionsDialog() = default;

std::unique_ptr<OptionsDialog> OptionsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::AddData add,
    tr::app::AddOptions const& preset)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("OptionsDialog.ui"));
    return std::unique_ptr<OptionsDialog>(
        gtr_get_widget_derived<OptionsDialog>(builder, "OptionsDialog", parent, core, std::move(add), preset));
}

OptionsDialog::Impl::Impl(
    OptionsDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    tr::app::AddData add,
    tr::app::AddOptions const& preset)
    : dialog_(dialog)
    , core_(core)
    , add_{ std::move(add) }
    , downloadDir_{ preset.download_dir }
    , source_chooser_(gtr_get_widget_derived<PathButton>(builder, "source_button"))
    , file_list_(gtr_get_widget_derived<FileList>(builder, "files_view_scroll", "files_view", core_, 0))
    , run_check_(gtr_get_widget<Gtk::CheckButton>(builder, "start_check"))
    , trash_check_(gtr_get_widget<Gtk::CheckButton>(builder, "trash_check"))
    , priority_combo_(gtr_get_widget<Gtk::ComboBox>(builder, "priority_combo"))
    , freespace_label_(gtr_get_widget_derived<FreeSpaceLabel>(builder, "free_space_label", core_, downloadDir_))
{
    dialog_.set_default_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
    dialog.signal_response().connect(sigc::mem_fun(*this, &Impl::addResponseCB));

    gtr_priority_combo_init(*priority_combo_);
    gtr_combo_box_set_active_enum(*priority_combo_, preset.bandwidth_priority.value_or(TR_PRI_NORMAL));

    addTorrentFilters(source_chooser_);
    if (add_.type == tr::app::AddData::Type::File) {
        source_chooser_->set_filename(add_.source);
    }
    source_chooser_->signal_selection_changed().connect(sigc::mem_fun(*this, &Impl::sourceChanged));

    auto* destination_chooser = gtr_get_widget_derived<PathButton>(builder, "destination_button");
    destination_chooser->set_filename(downloadDir_);
    core_->fetch_recent_paths(TR_KEY_recent_download_paths, sigc::mem_fun(*destination_chooser, &PathButton::set_recent_paths));

    destination_chooser->signal_selection_changed().connect(
        [this, destination_chooser] { downloadDirChanged(destination_chooser); });

    run_check_->set_active(!preset.paused.value_or(false));
    trash_check_->set_active(add_.trash_source);

    updateFiles();

    dialog_.get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT))->grab_focus();
}

/****
*****
****/

void TorrentFileChooserDialog::onOpenDialogResponse(int response, Glib::RefPtr<Session> const& core)
{
    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT)) {
        bool const do_prompt = get_choice(std::string(ShowOptionsDialogChoice)) == "true";
        auto const prompt = do_prompt ? tr::app::Session::AddPrompt::Always : tr::app::Session::AddPrompt::Never;

        auto const files = IF_GTKMM4(get_files2, get_files)();
        g_assert(!files.empty());

        // Remember this folder the next time we use this dialog.
        // Read it from the selection: with the XDG portal, get_current_folder() returns the starting folder.
        if (auto const parent = files.front()->get_parent(); parent != nullptr) {
            if (auto const path = parent->get_path(); !path.empty()) {
                prefs_.set(TR_KEY_open_dialog_dir, path);
            }
        }

        for (auto const& file : files) {
            core->request_add(tr::app::AddData{ gtr_add_key(file) }, prompt);
        }
    }
}

std::unique_ptr<TorrentFileChooserDialog> TorrentFileChooserDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs& prefs)
{
    return std::unique_ptr<TorrentFileChooserDialog>(new TorrentFileChooserDialog(parent, core, prefs));
}

TorrentFileChooserDialog::TorrentFileChooserDialog(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    tr::app::Prefs& prefs)
    : Gtk::FileChooserNative(_("Open a Torrent"), parent, TR_GTK_FILE_CHOOSER_ACTION(OPEN), _("_Open"), _("_Cancel"))
    , prefs_(prefs)
{
    set_modal(true);

    set_select_multiple(true);
    addTorrentFilters(this);
    signal_response().connect([this, core](int response) { onOpenDialogResponse(response, core); });

    if (auto const folder = prefs_.get<std::string>(TR_KEY_open_dialog_dir); !folder.empty()) {
        IF_GTKMM4(set_current_folder, set_current_folder_file)(Gio::File::create_for_path(folder));
    }

    add_choice(std::string(ShowOptionsDialogChoice), _("Show options dialog"));
    set_choice(std::string(ShowOptionsDialogChoice), prefs_.get<bool>(TR_KEY_show_options_window) ? "true" : "false");
}

/***
****
***/

void TorrentUrlChooserDialog::onOpenURLResponse(int response, Gtk::Entry const& entry, Glib::RefPtr<Session> const& core)
{
    if (response == TR_GTK_RESPONSE_TYPE(CANCEL)) {
        close();
    } else if (response == TR_GTK_RESPONSE_TYPE(ACCEPT)) {
        auto const url = gtr_str_strip(entry.get_text());

        if (url.empty()) {
            return;
        }

        if (core->request_add(tr::app::AddData{ url.raw() })) {
            close();
        } else {
            gtr_unrecognized_url_dialog(*this, url);
        }
    }
}

std::unique_ptr<TorrentUrlChooserDialog> TorrentUrlChooserDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("TorrentUrlChooserDialog.ui"));
    return std::unique_ptr<TorrentUrlChooserDialog>(
        gtr_get_widget_derived<TorrentUrlChooserDialog>(builder, "TorrentUrlChooserDialog", parent, core));
}

TorrentUrlChooserDialog::TorrentUrlChooserDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
{
    set_transient_for(parent);

    auto* const e = gtr_get_widget<Gtk::Entry>(builder, "url_entry");
    auto* const accept = get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
    gtr_paste_clipboard_url_into_entry(*e);

#if GTKMM_CHECK_VERSION(4, 0, 0)
    set_default_widget(*accept);
#else
    set_default(*accept);
#endif

    signal_response().connect([this, e, core](int response) { onOpenURLResponse(response, *e, core); });
}
