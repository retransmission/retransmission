// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint16_t
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/types.h"

struct tr_error;
struct tr_session;
struct tr_torrent;

struct tr_torrent_builder {
    explicit tr_torrent_builder(tr_session* session);

    // Owns the torrent's metainfo and raw benc, so copying is never what a caller wants.
    tr_torrent_builder(tr_torrent_builder const&) = delete;
    tr_torrent_builder& operator=(tr_torrent_builder const&) = delete;
    tr_torrent_builder(tr_torrent_builder&&) = default;
    tr_torrent_builder& operator=(tr_torrent_builder&&) = default;
    ~tr_torrent_builder() = default;

    bool set_metainfo(std::string_view contents, tr_error* error = nullptr);
    bool set_metainfo_from_file(std::string_view filename, tr_error* error = nullptr);
    bool set_metainfo_from_magnet_link(std::string_view magnet_link, tr_error* error = nullptr);
    [[nodiscard]] tr_torrent_metainfo const& metainfo() const noexcept;

    // The file set_metainfo_from_file() read, or empty for the other sources.
    // tr_torrent::torrent_filename() is a different thing: where the torrent is stored.
    [[nodiscard]] std::string const& source_filename() const noexcept;

    void set_bandwidth_priority(tr_priority_t priority) noexcept;

    void set_download_dir(std::string_view dir);
    [[nodiscard]] std::string const& download_dir() const noexcept;

    void set_file_priorities(std::span<tr_file_index_t const> files, tr_priority_t priority);
    void set_files_wanted(std::span<tr_file_index_t const> files, bool wanted);

    // Not a supported API call. It only exists so the mac client can migrate
    // its older incompleteDir settings, and that's the only place it should be used.
    void set_incomplete_dir(std::string_view dir);

    void set_labels(tr_labels_t&& labels) noexcept;

    void set_paused(bool paused) noexcept;
    [[nodiscard]] std::optional<bool> paused() const noexcept;

    void set_peer_limit(uint16_t peer_limit) noexcept;
    [[nodiscard]] std::optional<uint16_t> peer_limit() const noexcept;

    void set_sequential_download(bool seq) noexcept;
    void set_sequential_download_from_piece(tr_piece_index_t piece) noexcept;
    [[nodiscard]] std::optional<bool> sequential_download() const noexcept;
    [[nodiscard]] std::optional<tr_piece_index_t> sequential_download_from_piece() const noexcept;

    bool save(std::string_view filename, tr_error* error = nullptr) const;

private:
    // Read back only while tr_torrentNew() builds the torrent.
    friend struct tr_torrent;
    friend tr_torrent* tr_torrentNew(tr_torrent_builder* builder, tr_torrent** setme_duplicate_of);

    [[nodiscard]] tr_session* session() const noexcept;
    [[nodiscard]] tr_torrent_metainfo steal_metainfo() noexcept;
    [[nodiscard]] tr_priority_t bandwidth_priority() const noexcept;
    [[nodiscard]] std::string const& incomplete_dir() const noexcept;
    [[nodiscard]] tr_labels_t const& labels() const noexcept;
    void init_torrent_priorities(tr_torrent& tor) const;
    void init_torrent_wanted(tr_torrent& tor) const;

    tr_torrent_metainfo metainfo_ = {};

    std::optional<bool> paused_;
    std::optional<bool> sequential_download_;
    std::optional<tr_piece_index_t> sequential_download_from_piece_;
    std::optional<uint16_t> peer_limit_;
    std::string download_dir_;

    tr_labels_t labels_;

    std::vector<tr_file_index_t> wanted_;
    std::vector<tr_file_index_t> unwanted_;
    std::vector<tr_file_index_t> low_;
    std::vector<tr_file_index_t> normal_;
    std::vector<tr_file_index_t> high_;

    std::vector<char> contents_;

    std::string incomplete_dir_;
    std::string torrent_filename_;

    tr_session* session_;

    tr_priority_t priority_ = TR_PRI_NORMAL;
};
