// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <optional>

#include <fmt/format.h>

#include "libtransmission/quark.h"
#include "libtransmission/session-settings.h"
#include "libtransmission/transmission.h"

#include "libtransmission-app/prefs.h"

namespace tr::app
{
namespace
{

[[nodiscard]] std::string get_settings_filename(std::string_view const& config_dir)
{
    return fmt::format("{:s}/settings.json", config_dir);
}

[[nodiscard]] auto get_fallback_settings(std::string_view const settings_filename)
{
    // get pre-existing settings from the settings config file
    auto fallbacks = tr::settings::load(settings_filename);

    // fill in any missing values with the app's defaults,
    // then with libtransmission's defaults (eg missing/incomplete config file)
    fallbacks.merge(tr::serializer::save(AppPrefs{}, SessionPrefs{}));
    fallbacks.merge(tr_sessionGetDefaultSettings());

    return fallbacks;
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
    : Prefs{ get_fallback_settings(get_settings_filename(config_dir)) }
{
}

void Prefs::save(std::string_view const config_dir, std::optional<tr::Settings> const& local_session_settings) const
{
    auto const settings_filename = get_settings_filename(config_dir);

    // save the app settings.
    auto settings = tr::serializer::save(app_prefs_);

    // if a local session exists, save its settings.
    if (local_session_settings) {
        settings.merge(*local_session_settings);
    }

    // ensure we have all the settings we need.
    // eg if we're connected to a remote session, `settings` doesn't have session settings yet.
    settings.merge(get_fallback_settings(settings_filename));

    remove_transient_keys(settings);

    tr::settings::save(settings_filename, settings);
}

void Prefs::set(tr_quark const key, tr_variant const& var)
{
    if (tr::serializer::set_from_variant(key, var, app_prefs_, session_prefs_)) {
        changed_(key);
    }
}
} // namespace tr::app
