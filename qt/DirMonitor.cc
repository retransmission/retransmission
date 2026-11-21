// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "DirMonitor.h"

#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include <QtCore/QFileSystemWatcher>
#include <QtCore/QObject>
#include <QtCore/QString>

#include "Utils.h"

namespace
{

class QtDirMonitor final : public tr::app::DirMonitor
{
public:
    explicit QtDirMonitor(std::function<void()> on_change)
    {
        QObject::connect(&watcher_, &QFileSystemWatcher::directoryChanged, &watcher_, std::move(on_change));
    }

    [[nodiscard]] bool add(QString const& dirname)
    {
        return watcher_.addPath(dirname);
    }

private:
    QFileSystemWatcher watcher_; // on Linux, an inotify instance; a user has 128 by default
};

} // namespace

std::unique_ptr<tr::app::DirMonitor> QtDirMonitorMaker::create(std::string_view const dirname, std::function<void()> on_change)
{
    auto monitor = std::make_unique<QtDirMonitor>(std::move(on_change));
    if (!monitor->add(Utils::qstringFromUtf8(dirname))) {
        return {};
    }

    return monitor;
}
