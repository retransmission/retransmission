// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <atomic>
#include <cstddef> // std::byte
#include <cstdint>
#include <future>
#include <memory> // std::shared_ptr
#include <string>
#include <string_view>
#include <utility> // std::pair
#include <vector>

#include "libtransmission/announce-list.h"
#include "libtransmission/block-info.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/macros.h" // TR_CONSTEXPR_VEC
#include "libtransmission/types.h"

class tr_metainfo_builder
{
public:
    explicit tr_metainfo_builder(std::string_view single_file_or_parent_directory);

    ~tr_metainfo_builder() = default;

    tr_metainfo_builder(tr_metainfo_builder&&) = delete;
    tr_metainfo_builder(tr_metainfo_builder const&) = delete;
    tr_metainfo_builder& operator=(tr_metainfo_builder&&) = delete;
    tr_metainfo_builder& operator=(tr_metainfo_builder const&) = delete;

    // The result of a `make_checksums()` run:
    // on failure, `error` is set.
    // on success, `error` is empty and `piece_hashes` has the piece checksums.
    struct Checksums {
        tr_error error;
        std::vector<std::byte> piece_hashes;
    };

    // Generate piece checksums asynchronously.
    // - Runs in a worker thread because it can be time-consuming.
    // - The worker owns copies of the builder's state, so the builder
    //   may safely be destroyed while the worker is still running.
    // - Can be cancelled with `cancel_checksums()` and polled with `checksum_status()`.
    // - On success, pass the result to `set_piece_hashes()` before calling `benc()` or `save()`.
    [[nodiscard]] std::future<Checksums> make_checksums()
    {
        checksums_state_ = std::make_shared<ChecksumsState>();
        return std::async(
            std::launch::async,
            blocking_make_checksums,
            std::string{ tr_sys_path_dirname(top_) },
            files_,
            block_info_,
            checksums_state_);
    }

    // Returns the status of a `make_checksums()` run:
    // The current piece being tested and the total number of pieces in the torrent.
    [[nodiscard]] std::pair<tr_piece_index_t, tr_piece_index_t> checksum_status() const noexcept
    {
        return std::make_pair(checksums_state_->current_piece.load(), block_info_.piece_count());
    }

    // Tell the `make_checksums()` worker thread to cleanly exit ASAP.
    void cancel_checksums() noexcept
    {
        checksums_state_->cancel = true;
    }

    // generate the metainfo
    [[nodiscard]] std::string benc(tr_error* error = nullptr) const;

    // generate the metainfo and save it to a torrent file
    bool save(std::string_view filename, tr_error* error = nullptr) const;

    /// setters

    void set_announce_list(tr_announce_list announce)
    {
        announce_ = std::move(announce);
    }

    // whether or not to include User-Agent and creation time
    constexpr void set_anonymize(bool anonymize) noexcept
    {
        anonymize_ = anonymize;
    }

    void set_comment(std::string_view comment)
    {
        comment_ = comment;
    }

    // Store the piece checksums from a successful `make_checksums()` run
    // for `benc()` and `save()` to use.
    void set_piece_hashes(std::vector<std::byte> piece_hashes)
    {
        piece_hashes_ = std::move(piece_hashes);
    }

    bool set_piece_size(uint32_t piece_size) noexcept;

    constexpr void set_private(bool is_private) noexcept
    {
        is_private_ = is_private;
    }

    void set_source(std::string_view source)
    {
        source_ = source;
    }

    void set_webseeds(std::vector<std::string> webseeds)
    {
        webseeds_ = std::move(webseeds);
    }

    /// getters

    [[nodiscard]] constexpr auto const& announce_list() const noexcept
    {
        return announce_;
    }

    [[nodiscard]] constexpr auto const& anonymize() const noexcept
    {
        return anonymize_;
    }

    [[nodiscard]] constexpr auto const& comment() const noexcept
    {
        return comment_;
    }

    [[nodiscard]] constexpr auto file_count() const noexcept
    {
        return files_.file_count();
    }

    [[nodiscard]] TR_CONSTEXPR_VEC auto file_size(tr_file_index_t i) const noexcept
    {
        return files_.file_size(i);
    }

    [[nodiscard]] constexpr auto is_private() const noexcept
    {
        return is_private_;
    }

    [[nodiscard]] auto name() const noexcept
    {
        return tr_sys_path_basename(top_);
    }

    [[nodiscard]] TR_CONSTEXPR_VEC auto const& path(tr_file_index_t i) const noexcept
    {
        return files_.path(i);
    }

    [[nodiscard]] constexpr auto piece_size() const noexcept
    {
        return block_info_.piece_size();
    }

    [[nodiscard]] constexpr auto piece_count() const noexcept
    {
        return block_info_.piece_count();
    }

    [[nodiscard]] constexpr auto const& source() const noexcept
    {
        return source_;
    }

    [[nodiscard]] constexpr auto const& top() const noexcept
    {
        return top_;
    }

    [[nodiscard]] constexpr auto total_size() const noexcept
    {
        return files_.total_size();
    }

    [[nodiscard]] constexpr auto const& webseeds() const noexcept
    {
        return webseeds_;
    }

    ///

    [[nodiscard]] static uint32_t default_piece_size(uint64_t total_size) noexcept;

    [[nodiscard]] constexpr static bool is_legal_piece_size(uint32_t x) noexcept
    {
        // It must be a power of two and at least 16KiB
        auto constexpr MinSize = uint32_t{ 1024U * 16U };
        auto const is_power_of_two = (x & (x - 1)) == 0;
        return x >= MinSize && is_power_of_two;
    }

private:
    // Cancellation flag and progress shared between a `make_checksums()`
    // worker and the builder that launched it. Shared ownership is what
    // lets the worker outlive the builder: the worker holds the block and
    // copies of everything else it reads.
    struct ChecksumsState {
        std::atomic<tr_piece_index_t> current_piece = 0;
        std::atomic<bool> cancel = false;
    };

    [[nodiscard]] static Checksums blocking_make_checksums(
        std::string parent_dir,
        tr_torrent_files files,
        tr_block_info block_info,
        std::shared_ptr<ChecksumsState> state);

    std::string top_;
    tr_torrent_files files_;
    tr_announce_list announce_;
    tr_block_info block_info_;
    std::vector<std::byte> piece_hashes_;
    std::vector<std::string> webseeds_;

    std::string comment_;
    std::string source_;

    std::shared_ptr<ChecksumsState> checksums_state_ = std::make_shared<ChecksumsState>();

    bool is_private_ = false;
    bool anonymize_ = false;
};
