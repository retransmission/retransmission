// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission-app/prefs.h>

#include <libtransmission/converters.h>

#include <glibmm/miscutils.h>
#include <glibmm/ustring.h>

#include <string>
#include <string_view>

namespace tr::app
{
template<typename StringType>
struct PrefsStringTraits;

// Toolkit customization point used by tr::app::Prefs<Glib::ustring>.
template<>
struct PrefsStringTraits<Glib::ustring> {
    [[nodiscard]] static Glib::ustring from_utf8(std::string_view const str)
    {
        return Glib::ustring{ std::string{ str } };
    }

    [[nodiscard]] static Glib::ustring home_dir()
    {
        return Glib::get_home_dir();
    }
};
} // namespace tr::app

namespace tr::serializer
{
TR_DECLARE_CONVERTER(Glib::ustring)
} // namespace tr::serializer
