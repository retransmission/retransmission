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

    // Fills in the {appname} field of an already-translated string, such as one from a .ui file.
    [[nodiscard]] static QString withAppName(QString text);

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

    // Sets the spin box's prefix and suffix to the text around `field`
    // in the translated format's plural form for the spin box's value.
    // xgettext extracts `msgid` and `msgid_plural` from calls to this function.
    static void updateSpinBoxFormat(QSpinBox* spinBox, char const* msgid, char const* msgid_plural, std::string_view field);
};
