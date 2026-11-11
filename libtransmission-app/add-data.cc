// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/add-data.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/magnet-metainfo.h"
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/web-utils.h"

#include "libtransmission-app/interop.h"

using namespace std::string_view_literals;

namespace tr::app
{
namespace
{

[[nodiscard]] std::string absolute_filename(std::string_view const key)
{
    auto filename = tr::interop::local_filename(key);
    return tr_sys_path_is_relative(filename) ? fmt::format("{:s}/{:s}", tr_sys_dir_get_current(), filename) : filename;
}

[[nodiscard]] std::string url_last_segment(std::string_view const url)
{
    auto const parsed = tr_urlParse(url);
    if (!parsed) {
        return {};
    }

    auto path = parsed->path;
    if (auto const slash = path.rfind('/'); slash != std::string_view::npos) {
        path.remove_prefix(slash + 1U);
    }

    return tr_urlPercentDecode(path);
}

} // namespace

AddData::AddData(std::string_view const key)
{
    if (std::empty(key)) {
        return;
    }

    if (tr_urlIsValid(key)) {
        type = Type::Url;
        source = key;
    } else if (tr_magnet_metainfo{}.parseMagnet(key)) {
        type = Type::Magnet;
        source = key;
    } else if (auto filename = absolute_filename(key); tr_sys_path_exists(filename)) {
        type = Type::File;
        // A file that can't be read stays a File, so that adding it reports the failure.
        if (auto contents = std::vector<char>{}; tr_file_read(filename, contents)) {
            metainfo.assign(std::data(contents), std::size(contents));
        }
        source = std::move(filename);
    } else if (auto contents = tr::interop::decode_metainfo_torrent(key)) {
        type = Type::Metainfo;
        metainfo = std::move(*contents);
    }
}

AddData AddData::from_wire_metainfo(std::string_view const metainfo)
{
    // The sending end classified with is_metainfo_link() too,
    // so the same argument cannot be a link there and a torrent here.
    if (tr::interop::is_metainfo_link(metainfo)) {
        return AddData{ metainfo };
    }

    auto ret = AddData{};
    if (auto contents = tr::interop::decode_metainfo_torrent(metainfo)) {
        ret.type = Type::Metainfo;
        ret.metainfo = std::move(*contents);
    }

    return ret;
}

std::string AddData::readable_name() const
{
    switch (type) {
    case Type::File:
    case Type::Url:
        return source;

    case Type::Magnet:
    case Type::Metainfo:
        if (auto const tm = parsed()) {
            return std::string{ tm->name() };
        }
        break;

    case Type::None:
        break;
    }

    return {};
}

std::string AddData::readable_short_name() const
{
    switch (type) {
    case Type::File:
        {
            auto name = tr_sys_path_basename(source);
            if (auto constexpr Suffix = ".torrent"sv; name.ends_with(Suffix)) {
                name.remove_suffix(std::size(Suffix));
            }
            return std::string{ name };
        }

    case Type::Url:
        return url_last_segment(source);

    case Type::Magnet:
    case Type::Metainfo:
    case Type::None:
        break;
    }

    return readable_name();
}

std::optional<tr_torrent_metainfo> AddData::parsed() const
{
    auto ret = tr_torrent_metainfo{};
    auto ok = false;

    switch (type) {
    case Type::Magnet:
        ok = ret.parseMagnet(source);
        break;

    case Type::File:
    case Type::Metainfo:
        ok = ret.parse_benc(metainfo);
        break;

    case Type::Url:
    case Type::None:
        break;
    }

    if (ok) {
        return ret;
    }

    return {};
}

} // namespace tr::app
