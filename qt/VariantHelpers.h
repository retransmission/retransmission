// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <ctime>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include <QString>

#include <libtransmission/converters.h>
#include <libtransmission/variant.h>

#include "QtCompat.h"

class QByteArray;
class QDateTime;

class Speed;
class TorrentHash;
struct Peer;
struct TorrentFile;
struct TrackerStat;

namespace tr::serializer
{

TR_DECLARE_CONVERTER(QDateTime)
TR_DECLARE_CONVERTER(QString)
TR_DECLARE_CONVERTER(Speed)
TR_DECLARE_CONVERTER(TorrentHash)

} // namespace tr::serializer

namespace trqt::variant_helpers
{
template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_same_v<T, bool>
{
    std::optional<T> ret;

    if (auto value = T{}; tr_variantGetBool(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, int> || std::is_same_v<T, time_t>
{
    std::optional<T> ret;

    if (auto value = int64_t{}; tr_variantGetInt(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_same_v<T, double>
{
    std::optional<T> ret;

    if (auto value = T{}; tr_variantGetReal(variant, &value))
    {
        ret = value;
    }

    return ret;
}

template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_same_v<T, QString>
{
    std::optional<T> ret;

    if (auto sv = std::string_view{}; tr_variantGetStrView(variant, &sv))
    {
        ret = QString::fromUtf8(std::data(sv), static_cast<IF_QT6(qsizetype, int)>(std::size(sv)));
    }

    return ret;
}

template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_same_v<T, std::string_view>
{
    std::optional<T> ret;

    if (auto sv = std::string_view{}; tr_variantGetStrView(variant, &sv))
    {
        ret = std::string_view(std::data(sv), std::size(sv));
    }

    return ret;
}

template<typename T>
auto getValue(tr_variant const* variant)
    requires std::is_enum_v<T>
{
    std::optional<T> ret;

    if (auto const value = getValue<int>(variant); value)
    {
        ret = static_cast<T>(*value);
    }

    return ret;
}

template<typename C, typename T = typename C::value_type>
auto getValue(tr_variant const* var)
    requires std::is_same_v<C, QStringList> || std::is_same_v<C, QList<T>> || std::is_same_v<C, std::vector<T>>
{
    std::optional<C> ret;

    if (var != nullptr && var->holds_alternative<tr_variant::Vector>())
    {
        auto list = C{};

        for (size_t i = 0, n = tr_variantListSize(var); i < n; ++i)
        {
            tr_variant* const child = tr_variantListChild(const_cast<tr_variant*>(var), i);
            auto const value = getValue<T>(child);
            if (value)
            {
                list.push_back(*value);
            }
        }

        ret = list;
    }

    return ret;
}

template<typename T>
bool change(T& setme, T const& value)
{
    bool const changed = setme != value;

    if (changed)
    {
        setme = value;
    }

    return changed;
}

bool change(Peer& setme, tr_variant const* var);
bool change(TorrentFile& setme, tr_variant const* var);
bool change(TrackerStat& setme, tr_variant const* var);

template<typename T>
bool change(T& setme, tr_variant const* var)
{
    return var && tr::serializer::set(setme, *var);
}

template<typename T>
bool change(std::vector<T>& setme, tr_variant const* value)
{
    bool changed = false;

    auto const n = tr_variantListSize(value);
    if (setme.size() != n)
    {
        setme.resize(n);
        changed = true;
    }

    for (size_t i = 0; i < n; ++i)
    {
        changed = change(setme[i], tr_variantListChild(const_cast<tr_variant*>(value), i)) || changed;
    }

    return changed;
}

///

template<typename T>
std::optional<T> dictFind(tr_variant* dict, tr_quark key)
{
    if (dict)
    {
        if (auto const* const map = dict->get_if<tr_variant::Map>())
        {
            if (auto const iter = map->find(key); iter != map->end())
            {
                return tr::serializer::to_value<T>(iter->second);
            }
        }
    }

    return {};
}
} // namespace trqt::variant_helpers
