// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "Speed.h"

#include <QtCore/QString>

#include "TrFormat.h"

QString Speed::toUploadQstring() const
{
    return TR_FORMAT("{upload_speed} ▴", fmt::arg("upload_speed", toQstring()));
}

QString Speed::toDownloadQstring() const
{
    return TR_FORMAT("{download_speed} ▾", fmt::arg("download_speed", toQstring()));
}
