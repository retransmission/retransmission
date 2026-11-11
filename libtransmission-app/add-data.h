// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/types.h" // tr_priority_t

namespace tr::app
{

// A torrent that someone asked to add, before the session has it.
struct AddData {
    enum class Type : uint8_t {
        None, // not something that can be added
        Magnet, // `source` is the link
        Url, // `source` is the link, which the session will fetch
        File, // `source` is the local .torrent's absolute path, `metainfo` its contents
        Metainfo, // `metainfo` is a .torrent's contents; `source` is any URL they came from
    };

    AddData() = default;

    // Classifies `key`: a URL, a magnet link, a local file's path or `file://` URI, or base64'd metainfo.
    // Anything else is `Type::None`.
    explicit AddData(std::string_view key);

    // The two forms interop-names.h defines for AddMetainfo, and no others:
    // a link as itself, or a torrent file's contents base64'd. Anything else is `Type::None`.
    // A bare path is not one of them. The constructor would resolve it against this process's
    // working directory and read whatever it found there. The sender named that file
    // but does not share it, and across a bus need not even own it.
    [[nodiscard]] static AddData from_wire_metainfo(std::string_view metainfo);

    [[nodiscard]] std::string readable_name() const;
    [[nodiscard]] std::string readable_short_name() const;

    // What can be known of the torrent before the session adds it: a magnet link's name and hash,
    // or a .torrent's contents. nullopt for a URL or for a file that isn't a torrent.
    [[nodiscard]] std::optional<tr_torrent_metainfo> parsed() const;

    Type type = Type::None;
    std::string source;
    std::string metainfo;

    // What becomes of a local .torrent once the session has the torrent:
    // trashed if `trash_source`, else renamed to `<name>.added` if `from_watch_dir`, else kept.
    // A watch dir's file must not stay as it is, or the watch dir would offer it again.
    bool trash_source = false;
    bool from_watch_dir = false;
};

// What the user chose for a torrent they're adding.
// Whatever is left unset gets the session's default.
struct AddOptions {
    std::string download_dir;
    std::optional<bool> paused; // defaults to the `start_added_torrents` pref instead
    std::optional<tr_priority_t> bandwidth_priority;

    // file indices
    std::vector<int> files_unwanted;
    std::vector<int> priority_low;
    std::vector<int> priority_high;
};

} // namespace tr::app
