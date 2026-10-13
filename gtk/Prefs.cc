// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Prefs.h"

#include <libtransmission-app/prefs.h>

#include <libtransmission/converters.h>
#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include <glibmm/ustring.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
std::string gl_confdir;
} // namespace

void gtr_pref_init(std::string_view config_dir)
{
    gl_confdir = config_dir;
}

tr::app::Prefs& gtr_prefs()
{
    static auto prefs = tr::app::Prefs{ gl_confdir };
    return prefs;
}

bool gtr_pref_has_key(tr_quark const key)
{
    return tr::serializer::has_key<tr::app::AppPrefs, tr::app::SessionPrefs>(key);
}

double gtr_pref_double_get(tr_quark const key)
{
    return gtr_pref_lookup<double>(key).value_or(0.0);
}

void gtr_pref_double_set(tr_quark const key, double const value)
{
    gtr_pref_set<double>(key, value);
}

bool gtr_pref_flag_get(tr_quark const key)
{
    return gtr_pref_lookup<bool>(key).value_or(false);
}

void gtr_pref_flag_set(tr_quark const key, bool const value)
{
    gtr_pref_set<bool>(key, value);
}

std::string gtr_pref_string_get(tr_quark const key)
{
    return gtr_pref_lookup<std::string>(key).value_or(std::string{});
}

void gtr_pref_string_set(tr_quark const key, std::string_view const value)
{
    gtr_pref_set<std::string>(key, std::string{ value });
}

void gtr_pref_save(tr_session* session)
{
    gtr_prefs().save(gl_confdir, tr_sessionGetSettings(session));
}

// ---

namespace tr::serializer
{

tr_variant Converter<Glib::ustring>::to_variant(Glib::ustring const& src)
{
    return src.raw();
}

bool Converter<Glib::ustring>::to_value(tr_variant const& src, Glib::ustring* const tgt)
{
    if (auto const val = src.value_if<std::string_view>()) {
        *tgt = Glib::ustring{ std::string{ *val } };
        return true;
    }

    return false;
}

} // namespace tr::serializer
