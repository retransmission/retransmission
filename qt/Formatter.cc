// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Formatter.h"

#include <algorithm>

#include <libtransmission/utils.h>
#include <libtransmission/values.h>

#include "TrFormat.h"

using namespace std::literals;
using namespace tr::Values;

// static
QString Formatter::percentToString(double const x)
{
    return QString::fromStdString(tr_strpercent(x));
}

QString Formatter::memoryToString(int64_t const bytes)
{
    if (bytes < 0) {
        return tr("Unknown");
    }

    if (bytes == 0) {
        return tr("None");
    }

    return QString::fromStdString(Memory{ bytes, Memory::Units::Bytes }.to_string());
}

QString Formatter::storageToString(uint64_t const bytes)
{
    if (bytes == 0) {
        return tr("None");
    }

    return QString::fromStdString(Storage{ bytes, Storage::Units::Bytes }.to_string());
}

QString Formatter::storageToString(int64_t const bytes)
{
    if (bytes < 0) {
        return tr("Unknown");
    }

    return storageToString(static_cast<uint64_t>(bytes));
}

QString Formatter::ratioToString(double ratio)
{
    static auto constexpr Infinity = "\xE2\x88\x9E"sv;
    static auto const None = tr("None").toStdString();

    return QString::fromStdString(tr_strratio(ratio, None, Infinity));
}

QString Formatter::timeToString(int seconds)
{
    seconds = std::max(seconds, 0);

    if (seconds < 60) {
        return TR_FORMAT_N("{seconds:L} second(s)", seconds, fmt::arg("seconds", seconds));
    }

    auto const minutes = seconds / 60;

    if (minutes < 60) {
        return TR_FORMAT_N("{minutes:L} minute(s)", minutes, fmt::arg("minutes", minutes));
    }

    auto const hours = minutes / 60;

    if (hours < 24) {
        return TR_FORMAT_N("{hours:L} hour(s)", hours, fmt::arg("hours", hours));
    }

    auto const days = hours / 24;

    return TR_FORMAT_N("{days:L} day(s)", days, fmt::arg("days", days));
}
