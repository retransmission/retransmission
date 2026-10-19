// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "AboutDialog.h"

#include "GtkCompat.h"
#include "Utils.h"

#include <libtransmission/macros.h>
#include <libtransmission/version.h>

#include <gtkmm/image.h>
#include <gtkmm/label.h>

#include <memory>

namespace
{

auto constexpr CreditsResponse = 1;

} // namespace

AboutDialog::AboutDialog(BaseObjectType* const cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Gtk::Window& parent)
    : Gtk::Dialog{ cast_item }
{
    set_transient_for(parent);
    set_title(gtr_with_app_name(get_title()));

    auto* const icon = gtr_get_widget<Gtk::Image>(builder, "icon_image");
#if GTKMM_CHECK_VERSION(4, 0, 0)
    icon->set_from_icon_name(TR_GTK_ICON_NAME);
#else
    icon->set_from_icon_name(TR_GTK_ICON_NAME, Gtk::ICON_SIZE_DIALOG);
#endif

    gtr_get_widget<Gtk::Label>(builder, "title_label")->set_text(TR_PROJ_APPNAME_CAPITALIZED " " LONG_VERSION_STRING);
    auto* const copyright = gtr_get_widget<Gtk::Label>(builder, "copyright_label");
    copyright->set_text(gtr_with_app_name(copyright->get_text()));
    gtr_get_widget<Gtk::Label>(builder, "homepage_label")
        ->set_markup("<a href=\"" TR_PROJ_URL_HOMEPAGE "\">" TR_PROJ_URL_HOMEPAGE "</a>");

    set_default_response(TR_GTK_RESPONSE_TYPE(CLOSE));
    signal_response().connect([this](int const response) {
        if (response == CreditsResponse) {
            gtr_open_uri(TR_PROJ_URL_CREDITS);
        } else if (response == TR_GTK_RESPONSE_TYPE(CLOSE)) {
            close();
        }
    });
}

std::unique_ptr<AboutDialog> AboutDialog::create(Gtk::Window& parent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("AboutDialog.ui"));
    return std::unique_ptr<AboutDialog>{ gtr_get_widget_derived<AboutDialog>(builder, "AboutDialog", parent) };
}
