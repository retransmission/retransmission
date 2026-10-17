// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Prefs.h"

#include <libtransmission/converters.h>
#include <libtransmission/variant.h>

#include <glibmm/ustring.h>

#include <string>
#include <string_view>

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
