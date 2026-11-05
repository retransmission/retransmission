// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <sigslot/signal.hpp>

#include "BaseDialog.h"
#include "ui_StatsDialog.h"

class Session;

class StatsDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit StatsDialog(Session& session, QWidget* parent = nullptr);
    ~StatsDialog() override = default;
    StatsDialog(StatsDialog&&) = delete;
    StatsDialog(StatsDialog const&) = delete;
    StatsDialog& operator=(StatsDialog&&) = delete;
    StatsDialog& operator=(StatsDialog const&) = delete;

private:
    void updateStats();

    Session& session_;

    Ui::StatsDialog ui_ = {};

    sigslot::scoped_connection stats_connection_;
};
