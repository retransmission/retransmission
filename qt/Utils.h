// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>
#include <utility>

#include <QtCore/QPointer>
#include <QtCore/QString>

class QAbstractItemView;
class QColor;
class QHeaderView;
class QIcon;
class QModelIndex;
class QRect;
class QSpinBox;

class Utils
{
public:
    static QIcon getIconFromIndex(QModelIndex const& index);

    [[nodiscard]] static QString qstringFromUtf8(std::string_view str);

    static QString removeTrailingDirSeparator(QString const& path);

    static void narrowRect(QRect& rect, int dx1, int dx2, Qt::LayoutDirection direction);

    static int measureViewItem(QAbstractItemView const* view, QString const& text);
    static int measureHeaderItem(QHeaderView const* view, QString const& text);

    static QColor getFadedColor(QColor const& color);

    template<typename DialogT, typename... ArgsT>
    static void openDialog(QPointer<DialogT>& dialog, ArgsT&&... args)
    {
        if (dialog.isNull()) {
            dialog = new DialogT{ std::forward<ArgsT>(args)... }; // NOLINT clang-analyzer-cplusplus.NewDelete
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        } else {
            dialog->raise();
            dialog->activateWindow();
        }
    }

    static void updateSpinBoxFormat(QSpinBox* spinBox, char const* context, char const* format, QString const& placeholder);
};
