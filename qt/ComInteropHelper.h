// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include <QtAxContainer/QAxObject>

class QObject;
class QString;
class QVariant;

class ComInteropHelper
{
public:
    ComInteropHelper();

    bool isConnected() const;

    QVariant addMetainfo(QString const& metainfo) const;

    static void initialize();
    static void registerObject(QObject* parent);

private:
    std::unique_ptr<QAxObject> client_;
};
