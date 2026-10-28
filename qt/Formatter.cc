// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Formatter.h"

#include <libtransmission/values.h>

#include <libtransmission-app/formatters.h>

#include "TrFormat.h"

using namespace tr::Values;

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

QString Formatter::percentToString(Percent const percent)
{
    return TR_FORMAT("{percent}%", fmt::arg("percent", percent.to_whole_string()));
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

QString Formatter::timeToString(time_t const seconds)
{
    return QString::fromStdString(tr::app::format_time(seconds));
}

QString Formatter::timeLeftToString(time_t const seconds)
{
    return QString::fromStdString(tr::app::format_time_left(seconds));
}

QString Formatter::relativeTimeToString(time_t const then, time_t const now)
{
    return QString::fromStdString(tr::app::format_time_relative(then, now));
}
