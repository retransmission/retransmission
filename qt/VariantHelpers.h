// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <libtransmission/converters.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

class QString;

namespace tr::serializer
{

TR_DECLARE_CONVERTER(QString)

} // namespace tr::serializer

namespace trqt::variant_helpers
{

template<typename T>
[[nodiscard]] std::optional<T> dictFind(tr_variant const* dict, tr_quark key)
{
    if (dict) {
        if (auto const* const map = dict->get_if<tr_variant::Map>()) {
            return tr::serializer::to_value<T>(*map, key);
        }
    }

    return {};
}

} // namespace trqt::variant_helpers
