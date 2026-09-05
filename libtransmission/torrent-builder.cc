// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <cerrno> // for EINVAL
#include <cstdint> // uint16_t
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "libtransmission/torrent-builder.h"

#include "libtransmission/bandwidth.h" // tr_isPriority()
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/session.h"
#include "libtransmission/torrent.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

using namespace std::literals;

tr_torrent_builder::tr_torrent_builder(tr_session* const session)
    : session_{ session }
{
}

tr_session* tr_torrent_builder::session() const noexcept
{
    return session_;
}

// ---

bool tr_torrent_builder::set_metainfo_from_file(std::string_view const filename, tr_error* const error)
{
    if (std::empty(filename)) {
        if (error != nullptr) {
            error->set(EINVAL, "no filename specified"sv);
        }

        return false;
    }

    if (!metainfo_.parse_torrent_file(filename, &contents_, error)) {
        return false;
    }

    torrent_filename_ = filename;
    return true;
}

std::string const& tr_torrent_builder::source_filename() const noexcept
{
    return torrent_filename_;
}

bool tr_torrent_builder::set_metainfo(std::string_view const contents, tr_error* const error)
{
    torrent_filename_.clear();
    contents_.assign(std::begin(contents), std::end(contents));
    return metainfo_.parse_benc(contents, error);
}

bool tr_torrent_builder::set_metainfo_from_magnet_link(std::string_view const magnet_link, tr_error* const error)
{
    torrent_filename_.clear();
    contents_.clear();
    metainfo_ = {};
    return metainfo_.parseMagnet(magnet_link, error);
}

tr_torrent_metainfo const& tr_torrent_builder::metainfo() const noexcept
{
    return metainfo_;
}

tr_torrent_metainfo tr_torrent_builder::steal_metainfo() noexcept
{
    return std::exchange(metainfo_, {});
}

bool tr_torrent_builder::save(std::string_view const filename, tr_error* const error) const
{
    TR_ASSERT(!std::empty(filename));

    if (std::empty(contents_)) {
        if (error != nullptr) {
            error->set(EINVAL, "torrent builder has no contents to save"sv);
        }

        return false;
    }

    return tr_file_save(filename, contents_, error);
}

// ---

void tr_torrent_builder::set_files_wanted(std::span<tr_file_index_t const> const files, bool const wanted)
{
    auto& indices = wanted ? wanted_ : unwanted_;
    indices.assign(files.begin(), files.end());
}

void tr_torrent_builder::init_torrent_wanted(tr_torrent& tor) const
{
    tor.init_files_wanted(unwanted_, false);
    tor.init_files_wanted(wanted_, true);
}

// ---

void tr_torrent_builder::set_file_priorities(std::span<tr_file_index_t const> const files, tr_priority_t const priority)
{
    switch (priority) {
    case TR_PRI_LOW:
        low_.assign(files.begin(), files.end());
        break;

    case TR_PRI_HIGH:
        high_.assign(files.begin(), files.end());
        break;

    default: // TR_PRI_NORMAL
        normal_.assign(files.begin(), files.end());
        break;
    }
}

void tr_torrent_builder::init_torrent_priorities(tr_torrent& tor) const
{
    tor.set_file_priorities(low_, TR_PRI_LOW);
    tor.set_file_priorities(normal_, TR_PRI_NORMAL);
    tor.set_file_priorities(high_, TR_PRI_HIGH);
}

// ---

tr_priority_t tr_torrent_builder::bandwidth_priority() const noexcept
{
    return priority_;
}

void tr_torrent_builder::set_bandwidth_priority(tr_priority_t const priority) noexcept
{
    if (tr_isPriority(priority)) {
        priority_ = priority;
    }
}

// ---

std::string const& tr_torrent_builder::download_dir() const noexcept
{
    return download_dir_;
}

void tr_torrent_builder::set_download_dir(std::string_view const dir)
{
    download_dir_.assign(dir);
}

// ---

std::string const& tr_torrent_builder::incomplete_dir() const noexcept
{
    return incomplete_dir_;
}

void tr_torrent_builder::set_incomplete_dir(std::string_view const dir)
{
    incomplete_dir_.assign(dir);
}

// ---

tr_labels_t const& tr_torrent_builder::labels() const noexcept
{
    return labels_;
}

void tr_torrent_builder::set_labels(tr_labels_t&& labels) noexcept
{
    labels_ = std::move(labels);
}

// --

std::optional<bool> tr_torrent_builder::paused() const noexcept
{
    return paused_;
}

void tr_torrent_builder::set_paused(bool const paused) noexcept
{
    paused_ = paused;
}

// --

std::optional<uint16_t> tr_torrent_builder::peer_limit() const noexcept
{
    return peer_limit_;
}

void tr_torrent_builder::set_peer_limit(uint16_t const peer_limit) noexcept
{
    peer_limit_ = peer_limit;
}

// ---

std::optional<bool> tr_torrent_builder::sequential_download() const noexcept
{
    return sequential_download_;
}

void tr_torrent_builder::set_sequential_download(bool const seq) noexcept
{
    sequential_download_ = seq;
}

std::optional<tr_piece_index_t> tr_torrent_builder::sequential_download_from_piece() const noexcept
{
    return sequential_download_from_piece_;
}

void tr_torrent_builder::set_sequential_download_from_piece(tr_piece_index_t const piece) noexcept
{
    sequential_download_from_piece_ = piece;
}
