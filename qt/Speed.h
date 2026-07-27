// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QtCore/QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QtCore/QString>

#include <libtransmission/values.h>

#include "Utils.h"

class Speed : public tr::Values::Speed
{
    Q_DECLARE_TR_FUNCTIONS(Speed)

public:
    Speed() = default;

    template<std::integral Number>
    constexpr Speed(Number value, Units multiple)
        : tr::Values::Speed{ value, multiple }
    {
    }

    template<std::floating_point Number>
    Speed(Number value, Units multiple)
        : tr::Values::Speed{ value, multiple }
    {
    }

    [[nodiscard]] auto to_qstring() const noexcept
    {
        return QString::fromStdString(to_string());
    }

    [[nodiscard]] auto to_upload_qstring() const
    {
        static auto constexpr UploadSymbol = QChar{ 0x25B4 };
        return tr("%1 %2").arg(to_qstring()).arg(UploadSymbol);
    }

    [[nodiscard]] auto to_download_qstring() const
    {
        static auto constexpr DownloadSymbol = QChar{ 0x25BE };
        return tr("%1 %2").arg(to_qstring()).arg(DownloadSymbol);
    }

    [[nodiscard]] static auto display_name(Speed::Units const units)
    {
        auto const speed_unit_sv = Speed::units().display_name(units);
        return Utils::qstringFromUtf8(speed_unit_sv);
    }
};
