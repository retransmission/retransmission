// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "LicenseDialog.h"

#include <QtWidgets/QWidget>

#include "BaseDialog.h"

LicenseDialog::LicenseDialog(QWidget* parent)
    : BaseDialog{ parent }
{
    ui_.setupUi(this);
}
