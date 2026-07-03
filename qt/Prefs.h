// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>
#include <utility>

#include <QDir>
#include <QObject>
#include <QString>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/prefs.h>

#include "UserMetaType.h"
#include "Utils.h"
#include "VariantHelpers.h"

class Prefs final
    : public QObject
    , public tr::app::Prefs
{
    Q_OBJECT

public:
    Prefs() = default;

    explicit Prefs(tr::Settings const& settings)
        : tr::app::Prefs{ settings }
    {
    }

    explicit Prefs(QString const& config_dir)
        : tr::app::Prefs{ config_dir.toStdString() }
    {
    }

    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() override = default;

    [[nodiscard]] static bool isCore(tr_quark const key)
    {
        return tr::app::prefs_is_core(key);
    }

signals:
    void changed(tr_quark key);

protected:
    void on_changed(tr_quark const key) override
    {
        emit changed(key);
    }
};
