// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t

#include <QtCore/QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QtCore/QString>

#include <libtransmission/values.h>

class Formatter
{
    Q_DECLARE_TR_FUNCTIONS(Formatter)

public:
    Formatter() = delete;

    [[nodiscard]] static QString memoryToString(int64_t bytes);
    [[nodiscard]] static QString percentToString(tr::Values::Percent percent); // "50%"
    [[nodiscard]] static QString storageToString(int64_t bytes);
    [[nodiscard]] static QString storageToString(uint64_t bytes);
    [[nodiscard]] static QString timeToString(int seconds);
};
