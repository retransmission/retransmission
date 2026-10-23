// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <optional>

#include <fmt/format.h>

#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/session-settings.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/transmission.h"

#include "libtransmission-app/prefs.h"

namespace tr::app
{
namespace
{

// settings.json, with gaps filled from the app's defaults and then libtransmission's.
[[nodiscard]] auto get_fallback_settings(std::string_view const config_dir)
{
    return tr_sessionLoadSettings(config_dir, tr::serializer::save(AppPrefs{}, SessionPrefs{}));
}

void remove_transient_keys(tr::Settings& settings)
{
    for (auto const key : { TR_KEY_filter_text }) {
        settings.erase(key);
    }
}

} // namespace

SessionPrefs::SessionPrefs()
{
    tr::serializer::load(tr_sessionGetDefaultSettings(), *this);
}

Prefs::Prefs(tr::Settings const& settings)
{
    tr::serializer::load(settings, app_prefs_, session_prefs_);
}

Prefs::Prefs(std::string_view const config_dir)
    : Prefs{ get_fallback_settings(config_dir) }
{
}

void Prefs::save(std::string_view const config_dir, std::optional<tr::Settings> const& local_session_settings) const
{
    // save the app settings.
    auto settings = tr::serializer::save(app_prefs_);

    // if a local session exists, save its settings.
    if (local_session_settings) {
        settings.merge(*local_session_settings);
    }

    // ensure we have all the settings we need.
    // eg if we're connected to a remote session, `settings` doesn't have session settings yet.
    settings.merge(get_fallback_settings(config_dir));

    remove_transient_keys(settings);

    tr::settings::save(tr_sessionGetSettingsFilename(config_dir), settings);
}

bool Prefs::set(tr_quark const key, tr_variant const& var)
{
    warn_if_unregistered(key);

    auto const changed = tr::serializer::set_from_variant(key, var, app_prefs_, session_prefs_);
    if (changed) {
        changed_(key);
    }

    return changed;
}

void Prefs::warn_if_unregistered(tr_quark const key)
{
    if (tr::serializer::has_key<AppPrefs>(key) || tr::serializer::has_key<SessionPrefs>(key)) {
        return;
    }

    tr_logAddWarn(fmt::format("No pref is registered for key '{:s}'", tr_quark_get_string_view(key)));
    TR_ASSERT_MSG(false, "no pref is registered for this key");
}
} // namespace tr::app
