// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/filefilter.h>

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <memory>
#include <string>
#include <vector>

class PathButton : public Gtk::Button
{
    using BaseWidgetType = Gtk::Button;

public:
    PathButton();
    PathButton(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder);
    PathButton(PathButton&&) = delete;
    PathButton(PathButton const&) = delete;
    PathButton& operator=(PathButton&&) = delete;
    PathButton& operator=(PathButton const&) = delete;
    ~PathButton() override;

    void set_recent_paths(std::vector<Glib::ustring> const& value);

    std::string get_filename() const;
    void set_filename(std::string const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    sigc::signal<void()>& signal_selection_changed();

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
