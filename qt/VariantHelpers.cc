// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "VariantHelpers.h"

#include <string_view>

#include <QtCore/QString>

#include <libtransmission/converters.h>
#include <libtransmission/variant.h>

#include "Utils.h"

namespace tr::serializer
{

tr_variant Converter<QString>::to_variant(QString const& src)
{
    return src.toStdString();
}

bool Converter<QString>::to_value(tr_variant const& src, QString* tgt)
{
    if (auto const val = src.value_if<std::string_view>()) {
        *tgt = Utils::qstringFromUtf8(*val);
        return true;
    }

    return false;
}

} // namespace tr::serializer
