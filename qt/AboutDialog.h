// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "BaseDialog.h"
#include "ui_AboutDialog.h"

class Session;

class AboutDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(Session& session, QWidget* parent = nullptr);
    ~AboutDialog() override = default;
    AboutDialog(AboutDialog&&) = delete;
    AboutDialog(AboutDialog const&) = delete;
    AboutDialog& operator=(AboutDialog&&) = delete;
    AboutDialog& operator=(AboutDialog const&) = delete;

private slots:
    void showCredits();

private:
    Ui::AboutDialog ui_{};
};
