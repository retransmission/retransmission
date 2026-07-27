// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "PathButton.h"

#include "GtkCompat.h"
#include "Utils.h"

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/filechooser.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/separator.h>

#include <giomm/file.h>
#include <giomm/icon.h>

#include <glibmm/error.h>
#include <glibmm/i18n.h>
#include <glibmm/property.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <sigc++/signal.h>

#include <string>
#include <vector>

namespace
{

using TrFileChooserAction = IF_GTKMM4(Gtk::FileChooser::Action, Gtk::FileChooserAction);

// gtkmm4 uses Widget::set_child(); gtkmm3 (a GtkBin subclass) uses add().
template<typename WidgetT>
void tr_set_child(WidgetT& parent, Gtk::Widget& child)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    parent.set_child(child);
#else
    parent.add(child);
#endif
}

// A popover may have its child replaced repeatedly; gtkmm3 needs the old one removed first.
void tr_popover_set_child(Gtk::Popover& popover, Gtk::Widget& child)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    popover.set_child(child);
#else
    // Gtk::Bin::remove() takes no argument and removes the single child.
    if (popover.get_child() != nullptr) {
        popover.remove();
    }
    popover.add(child);
#endif
}

void tr_box_append(Gtk::Box& box, Gtk::Widget& child)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    box.append(child);
#else
    box.pack_start(child, child.get_hexpand() || child.get_vexpand(), true, 0);
#endif
}

void tr_button_set_flat(Gtk::Button& button)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    button.set_has_frame(false);
#else
    button.set_relief(Gtk::RELIEF_NONE);
#endif
}

void tr_popover_set_parent(Gtk::Popover& popover, Gtk::Widget& parent)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    popover.set_parent(parent);
#else
    popover.set_relative_to(parent);
#endif
}

void tr_popover_unparent([[maybe_unused]] Gtk::Popover& popover)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    if (popover.get_parent() != nullptr) {
        popover.unparent();
    }
#endif
}

void tr_popover_unparent_on_destroy([[maybe_unused]] Gtk::Widget& parent, [[maybe_unused]] Gtk::Popover& popover)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    parent.signal_destroy().connect([&popover]() { tr_popover_unparent(popover); });
#endif
}

void tr_image_set_from_icon_name(Gtk::Image& image, Glib::ustring const& icon_name)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    image.set_from_icon_name(icon_name);
#else
    image.set_from_icon_name(icon_name, Gtk::ICON_SIZE_BUTTON);
#endif
}

// The const qualifier picks the Gio::Icon overload; gtkmm3 also offers an IconSet one.
void tr_image_set_from_gicon(Gtk::Image& image, Glib::RefPtr<Gio::Icon const> const& icon)
{
#if GTKMM_CHECK_VERSION(4, 0, 0)
    image.set(icon);
#else
    image.set(icon, Gtk::ICON_SIZE_BUTTON);
#endif
}

// gtkmm4 widgets are visible by default; gtkmm3 needs children shown explicitly.
void tr_show_all([[maybe_unused]] Gtk::Widget& widget)
{
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    widget.show_all();
#endif
}

} // namespace

class PathButton::Impl
{
public:
    explicit Impl(PathButton& widget);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    std::string const& get_filename() const;
    void set_filename(std::string const& value);

    void set_recent_paths(std::vector<Glib::ustring> const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    sigc::signal<void()>& signal_selection_changed();

private:
    void on_clicked();
    void populate_menu();
    void show_dialog();

    void update();
    void update_mode();

private:
    PathButton& widget_;

    Glib::Property<TrFileChooserAction> action_;
    Glib::Property<Glib::ustring> title_;

    sigc::signal<void()> selection_changed_;

    Gtk::Image* const image_ = Gtk::make_managed<Gtk::Image>();
    Gtk::Label* const label_ = Gtk::make_managed<Gtk::Label>();
    Gtk::Image* const mode_ = Gtk::make_managed<Gtk::Image>();
    Gtk::Popover popover_;

    std::string current_file_;
    std::vector<Glib::ustring> recent_paths_;
    std::vector<Glib::RefPtr<Gtk::FileFilter>> filters_;
};

PathButton::Impl::Impl(PathButton& widget)
    : widget_{ widget }
    , action_{ widget, "action", TR_GTK_FILE_CHOOSER_ACTION(OPEN) }
    , title_{ widget, "title", {} }
{
    action_.get_proxy().signal_changed().connect([this]() { update_mode(); });

    label_->set_ellipsize(TR_PANGO_ELLIPSIZE_MODE(END));
    label_->set_hexpand(true);
    label_->set_halign(TR_GTK_ALIGN(START));

    auto* const layout = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(HORIZONTAL), 5);
    tr_box_append(*layout, *image_);
    tr_box_append(*layout, *label_);
    tr_box_append(*layout, *Gtk::make_managed<Gtk::Separator>(TR_GTK_ORIENTATION(VERTICAL)));
    tr_box_append(*layout, *mode_);
    tr_set_child(widget_, *layout);
    tr_show_all(*layout);

    tr_popover_set_parent(popover_, widget_);
    popover_.set_position(TR_GTK_POSITION_TYPE(BOTTOM));

    // gtkmm4 parents the popover to the button; unparent it while the button is
    // being disposed (::destroy), before it is finalized. On gtkmm3 this is a no-op.
    tr_popover_unparent_on_destroy(widget_, popover_);

    widget_.signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_clicked));

    update();
    update_mode();
}

PathButton::Impl::~Impl()
{
    tr_popover_unparent(popover_);
}

std::string const& PathButton::Impl::get_filename() const
{
    return current_file_;
}

void PathButton::Impl::set_filename(std::string const& value)
{
    current_file_ = value;
    update();
    selection_changed_.emit();
}

void PathButton::Impl::set_recent_paths(std::vector<Glib::ustring> const& value)
{
    recent_paths_ = value;
}

void PathButton::Impl::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    filters_.push_back(value);
}

sigc::signal<void()>& PathButton::Impl::signal_selection_changed()
{
    return selection_changed_;
}

void PathButton::Impl::on_clicked()
{
    // When there are no recent paths to offer (e.g. file pickers), fall back to
    // opening the file chooser directly so the button behaves like a plain one.
    if (recent_paths_.empty()) {
        show_dialog();
        return;
    }

    populate_menu();
    popover_.popup();
}

void PathButton::Impl::populate_menu()
{
    auto* const box = Gtk::make_managed<Gtk::Box>(TR_GTK_ORIENTATION(VERTICAL), 0);

    for (auto const& path : recent_paths_) {
        auto* const path_label = Gtk::make_managed<Gtk::Label>(path);
        path_label->set_halign(TR_GTK_ALIGN(START));
        path_label->set_ellipsize(TR_PANGO_ELLIPSIZE_MODE(START));
        path_label->set_max_width_chars(40);

        auto* const row = Gtk::make_managed<Gtk::Button>();
        tr_set_child(*row, *path_label);
        tr_button_set_flat(*row);
        row->set_tooltip_text(path);
        row->signal_clicked().connect([this, path = path.raw()]() {
            popover_.popdown();
            set_filename(path);
        });
        tr_box_append(*box, *row);
    }

    tr_box_append(*box, *Gtk::make_managed<Gtk::Separator>(TR_GTK_ORIENTATION(HORIZONTAL)));

    auto* const other_label = Gtk::make_managed<Gtk::Label>(_("Other…"));
    other_label->set_halign(TR_GTK_ALIGN(START));

    auto* const other = Gtk::make_managed<Gtk::Button>();
    tr_set_child(*other, *other_label);
    tr_button_set_flat(*other);
    other->signal_clicked().connect([this]() {
        popover_.popdown();
        show_dialog();
    });
    tr_box_append(*box, *other);

    tr_popover_set_child(popover_, *box);
    tr_show_all(*box);
}

void PathButton::Impl::show_dialog()
{
    auto const title = title_.get_value();

    auto dialog = Gtk::FileChooserNative::create(
        !title.empty() ? title : _("Select a File"),
        action_.get_value(),
        _("_Open"),
        _("_Cancel"));
    dialog->set_transient_for(gtr_widget_get_window(widget_));
    dialog->set_modal(true);

    if (!current_file_.empty()) {
        dialog->set_file(Gio::File::create_for_path(current_file_));
    }

    for (auto const& filter : filters_) {
        dialog->add_filter(filter);
    }

    dialog->signal_response().connect([this, dialog](int response) mutable {
        if (response == TR_GTK_RESPONSE_TYPE(ACCEPT)) {
            set_filename(dialog->get_file()->get_path());
            selection_changed_.emit();
        }

        dialog.reset();
    });

    dialog->show();
}

void PathButton::Impl::update()
{
    if (!current_file_.empty()) {
        auto const file = Gio::File::create_for_path(current_file_);

        try {
            tr_image_set_from_gicon(*image_, file->query_info()->get_icon());
        } catch (Glib::Error const&) {
            tr_image_set_from_icon_name(*image_, "image-missing");
        }

        label_->set_text(file->get_basename());
    } else {
        tr_image_set_from_icon_name(*image_, "image-missing");
        label_->set_text(_("(None)"));
    }

    widget_.set_tooltip_text(current_file_);
}

void PathButton::Impl::update_mode()
{
    tr_image_set_from_icon_name(
        *mode_,
        action_.get_value() == TR_GTK_FILE_CHOOSER_ACTION(SELECT_FOLDER) ? "folder-open-symbolic" : "document-open-symbolic");
}

PathButton::PathButton()
    : Glib::ObjectBase(typeid(PathButton))
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::PathButton(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& /*builder*/)
    : Glib::ObjectBase(typeid(PathButton))
    , BaseWidgetType(cast_item)
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::~PathButton() = default;

void PathButton::set_recent_paths(std::vector<Glib::ustring> const& value)
{
    impl_->set_recent_paths(value);
}

std::string PathButton::get_filename() const
{
    return impl_->get_filename();
}

void PathButton::set_filename(std::string const& value)
{
    impl_->set_filename(value);
}

void PathButton::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    impl_->add_filter(value);
}

sigc::signal<void()>& PathButton::signal_selection_changed()
{
    return impl_->signal_selection_changed();
}
