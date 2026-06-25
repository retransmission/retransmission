// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "VariantHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

#include <QString>
#include <QUrl>

#include <libtransmission/converters.h>
#include <libtransmission/web-utils.h>

#include "Application.h" // qApp
#include "QtCompat.h"
#include "Speed.h"
#include "Torrent.h"

namespace ser = tr::serializer;

namespace trqt::variant_helpers
{

bool change(Peer& setme, tr_variant const* value)
{
    return !ser::load(setme, Peer::Fields, *value).empty();
}

bool change(TorrentFile& setme, tr_variant const* value)
{
    return !ser::load(setme, TorrentFile::Fields, *value).empty();
}

bool change(TrackerStat& setme, tr_variant const* value)
{
    auto const changed_keys = ser::load(setme, TrackerStat::Fields, *value);
    auto const site_changed = std::ranges::binary_search(changed_keys, TR_KEY_announce) ||
        std::ranges::binary_search(changed_keys, TR_KEY_sitename);

    if (site_changed && !setme.announce.isEmpty() && trApp != nullptr) {
        if (setme.sitename.isEmpty()) {
            auto const announce_str = setme.announce.toStdString();
            if (auto const parsed = tr_urlParse(announce_str)) {
                auto const sitename = parsed->sitename;
                setme.sitename = QString::fromUtf8(
                    std::data(sitename),
                    static_cast<IF_QT6(qsizetype, int)>(std::size(sitename)));
            }
        }

        setme.announce = trApp->intern(setme.announce);
        trApp->load_favicon(setme.announce);
    }

    return !changed_keys.empty();
}

///

namespace
{
bool toQString(tr_variant const& src, QString* tgt)
{
    if (auto const val = src.value_if<std::string_view>()) {
        *tgt = QString::fromUtf8(std::data(*val), static_cast<IF_QT6(qsizetype, int)>(std::size(*val)));
        return true;
    }

    return false;
}

tr_variant fromQString(QString const& val)
{
    return val.toStdString();
}

// ---

bool toSpeed(tr_variant const& src, Speed* tgt)
{
    if (auto const val = ser::to_value<int64_t>(src)) {
        *tgt = Speed{ *val, Speed::Units::Byps };
        return true;
    }

    return false;
}

tr_variant fromSpeed(Speed const& src)
{
    return ser::to_variant(static_cast<int64_t>(src.base_quantity()));
}

// ---

bool toTorrentHash(tr_variant const& src, TorrentHash* tgt)
{
    if (auto const val = src.value_if<std::string_view>()) {
        *tgt = TorrentHash{ *val };
        return true;
    }

    return false;
}

tr_variant fromTorrentHash(TorrentHash const& src)
{
    return ser::to_variant(src.toString());
}
} // namespace

} // namespace trqt::variant_helpers

// ---
// `Converter<T>` out-of-line definitions for Qt-side types. Each forwards
// to the matching helper in the unnamed namespace above.

namespace tr::serializer
{
namespace vh = trqt::variant_helpers;

tr_variant Converter<QString>::to_variant(QString const& src)
{
    return vh::fromQString(src);
}
bool Converter<QString>::to_value(tr_variant const& src, QString* tgt)
{
    return vh::toQString(src, tgt);
}

tr_variant Converter<Speed>::to_variant(Speed const& src)
{
    return vh::fromSpeed(src);
}
bool Converter<Speed>::to_value(tr_variant const& src, Speed* tgt)
{
    return vh::toSpeed(src, tgt);
}

tr_variant Converter<TorrentHash>::to_variant(TorrentHash const& src)
{
    return vh::fromTorrentHash(src);
}
bool Converter<TorrentHash>::to_value(tr_variant const& src, TorrentHash* tgt)
{
    return vh::toTorrentHash(src, tgt);
}

} // namespace tr::serializer
