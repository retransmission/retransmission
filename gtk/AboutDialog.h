// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <gtkmm/builder.h>
#include <gtkmm/dialog.h>
#include <gtkmm/window.h>

#include <glibmm/refptr.h>

#include <memory>

class AboutDialog : public Gtk::Dialog
{
public:
    AboutDialog(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, Gtk::Window& parent);
    AboutDialog(AboutDialog&&) = delete;
    AboutDialog(AboutDialog const&) = delete;
    AboutDialog& operator=(AboutDialog&&) = delete;
    AboutDialog& operator=(AboutDialog const&) = delete;
    ~AboutDialog() override = default;

    [[nodiscard]] static std::unique_ptr<AboutDialog> create(Gtk::Window& parent);
};
