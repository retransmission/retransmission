// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Speed.h"

#include <string_view>

#include <QtCore/QString>

#include "TrFormat.h"

QString Speed::toUploadQstring() const
{
    static auto constexpr UploadSymbol = std::string_view{ "▴" };
    return TR_FORMAT("{speed} {arrow}", fmt::arg("speed", toQstring()), fmt::arg("arrow", UploadSymbol));
}

QString Speed::toDownloadQstring() const
{
    static auto constexpr DownloadSymbol = std::string_view{ "▾" };
    return TR_FORMAT("{speed} {arrow}", fmt::arg("speed", toQstring()), fmt::arg("arrow", DownloadSymbol));
}
