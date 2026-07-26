// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QtGui/QIcon>

#include <QtWidgets/QStyle>

class QPainter;
class QStyleOptionProgressBar;

class StyleHelper
{
public:
    static QIcon::Mode getIconMode(QStyle::State const& state);

    static void drawProgressBar(QPainter& painter, QStyleOptionProgressBar const& option);
};
