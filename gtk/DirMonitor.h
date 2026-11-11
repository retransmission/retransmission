// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission-app/watch-dir.h>

#include <functional>
#include <memory>
#include <string_view>

// Monitors a directory with a `Gio::FileMonitor`, for `tr::app::WatchDir`.
class GioDirMonitorMaker final : public tr::app::DirMonitorMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::app::DirMonitor> create(std::string_view dirname, std::function<void()> on_change)
        override;
};
