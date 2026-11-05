// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <gtkmm/window.h>

#include <glibmm/refptr.h>

#include <memory>
#include <string>

class Session;

class SystemTrayIcon
{
public:
    SystemTrayIcon(Gtk::Window& main_window, Glib::RefPtr<Session> const& core);
    SystemTrayIcon(SystemTrayIcon&&) = delete;
    SystemTrayIcon(SystemTrayIcon const&) = delete;
    SystemTrayIcon& operator=(SystemTrayIcon&&) = delete;
    SystemTrayIcon& operator=(SystemTrayIcon const&) = delete;
    ~SystemTrayIcon();

    void refresh();

    // While the session closes: says `message` where the tray can, and offers nothing to do.
    void set_shutting_down(std::string const& message);

    [[nodiscard]] static bool is_available();
    [[nodiscard]] static std::unique_ptr<SystemTrayIcon> create(Gtk::Window& main_window, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
