// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "DirMonitor.h"

#include "GtkCompat.h"

#include <giomm/file.h>
#include <giomm/filemonitor.h>

#include <glibmm/error.h>
#include <glibmm/refptr.h>

#include <sigc++/connection.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace
{

// the events that add or remove a name, and the end of a write that a scan may have caught midway
[[nodiscard]] constexpr bool changes_names(IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) const event) noexcept
{
    switch (event) {
    case TR_GIO_FILE_MONITOR_EVENT(CREATED):
    case TR_GIO_FILE_MONITOR_EVENT(DELETED):
    case TR_GIO_FILE_MONITOR_EVENT(MOVED):
    case TR_GIO_FILE_MONITOR_EVENT(MOVED_IN):
    case TR_GIO_FILE_MONITOR_EVENT(MOVED_OUT):
    case TR_GIO_FILE_MONITOR_EVENT(RENAMED):
    case TR_GIO_FILE_MONITOR_EVENT(CHANGES_DONE_HINT):
        return true;

    default:
        return false;
    }
}

class GioDirMonitor final : public tr::app::DirMonitor
{
public:
    GioDirMonitor(Glib::RefPtr<Gio::FileMonitor> monitor, std::function<void()> on_change)
        : monitor_{ std::move(monitor) }
        , tag_{ monitor_->signal_changed().connect([on_change = std::move(on_change)](
                                                       Glib::RefPtr<Gio::File> const& /*file*/,
                                                       Glib::RefPtr<Gio::File> const& /*other_file*/,
                                                       IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent)
                                                           const event) {
            if (changes_names(event)) {
                on_change();
            }
        }) }
    {
    }

    GioDirMonitor(GioDirMonitor&&) = delete;
    GioDirMonitor(GioDirMonitor const&) = delete;
    GioDirMonitor& operator=(GioDirMonitor&&) = delete;
    GioDirMonitor& operator=(GioDirMonitor const&) = delete;

    ~GioDirMonitor() override
    {
        tag_.disconnect();
        monitor_->cancel();
    }

private:
    Glib::RefPtr<Gio::FileMonitor> const monitor_;
    sigc::connection tag_;
};

} // namespace

std::unique_ptr<tr::app::DirMonitor> GioDirMonitorMaker::create(std::string_view const dirname, std::function<void()> on_change)
{
    try {
        auto monitor = Gio::File::create_for_path(std::string{ dirname })->monitor_directory();
        return std::make_unique<GioDirMonitor>(std::move(monitor), std::move(on_change));
    } catch (Glib::Error const&) {
        return {};
    }
}
