// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "IconToolButton.h"

#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QStyleOptionToolButton>
#include <QtWidgets/QStylePainter>

IconToolButton::IconToolButton(QWidget* parent)
    : QToolButton{ parent }
{
}

QSize IconToolButton::sizeHint() const
{
    auto option = QStyleOptionToolButton{};
    initStyleOption(&option);
    option.features = QStyleOptionToolButton::None;
    option.toolButtonStyle = Qt::ToolButtonIconOnly;
    QSize const size = style()->sizeFromContents(QStyle::CT_ToolButton, &option, iconSize(), this);

    return size.expandedTo(iconSize() + QSize{ 4, 4 });
}

void IconToolButton::paintEvent(QPaintEvent* /*event*/)
{
    auto painter = QStylePainter{ this };
    auto option = QStyleOptionToolButton{};
    initStyleOption(&option);
    option.features = QStyleOptionToolButton::None;
    option.toolButtonStyle = Qt::ToolButtonIconOnly;
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
