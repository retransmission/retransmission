// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include <QDir>
#include <QObject>
#include <QString>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/prefs.h>

#include "QtCompat.h"
#include "UserMetaType.h"
#include "VariantHelpers.h"

namespace tr::app
{
template<>
struct PrefsStringTraits<QString> {
    [[nodiscard]] static QString from_utf8(std::string_view const str)
    {
        return QString::fromUtf8(std::data(str), static_cast<IF_QT6(qsizetype, int)>(std::size(str)));
    }

    [[nodiscard]] static QString home_dir()
    {
        return QDir::home().absolutePath();
    }
};
} // namespace tr::app

class Prefs final
    : public QObject
    , public tr::app::BasePrefs<Prefs, QString>
{
    Q_OBJECT

    friend class tr::app::BasePrefs<Prefs, QString>;

public:
    Prefs() = default;

    explicit Prefs(tr::Settings const& settings)
        : BasePrefs{ settings }
    {
    }

    explicit Prefs(QString const& dir)
        : BasePrefs{ dir.toStdString() }
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

    using BasePrefs::save;

    void save(QString const& filename) const
    {
        BasePrefs::save(filename.toStdString());
    }

signals:
    void changed(tr_quark key);

private:
    void on_changed(tr_quark const key)
    {
        emit changed(key);
    }
};
