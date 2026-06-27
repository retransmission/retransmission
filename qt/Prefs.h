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

#include <sigslot/signal.hpp>

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

// Toolkit-neutral preferences live in tr::app::Prefs<QString>. This thin
// QObject adapter owns one and bridges its sigslot `changed` signal to a Qt
// signal, so the rest of the Qt code can `connect()` as before.
class Prefs final : public QObject
{
    Q_OBJECT

public:
    Prefs() = default;

    explicit Prefs(tr::Settings const& settings)
        : impl_{ settings }
    {
    }

    explicit Prefs(QString const& dir)
        : impl_{ dir.toStdString() }
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

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(tr_quark const key) const
    {
        return impl_.keyval(key);
    }

    void set(tr_quark const key, tr_variant const& var)
    {
        impl_.set(key, var);
    }

    template<typename T>
    void set(tr_quark const key, T const& val)
    {
        impl_.set(key, val);
    }

    void set(tr_quark /*key*/, char const* /*value*/) = delete;

    template<typename T>
    [[nodiscard]] T get(tr_quark const key) const
    {
        return impl_.get<T>(key);
    }

    [[nodiscard]] tr::Settings current_settings() const
    {
        return impl_.current_settings();
    }

    void save(QString const& filename) const
    {
        impl_.save(filename.toStdString());
    }

signals:
    void changed(tr_quark key);

private:
    tr::app::Prefs<QString> impl_;
    sigslot::scoped_connection changed_conn_{ impl_.observe_changed([this](tr_quark const key) { emit changed(key); }) };
};
