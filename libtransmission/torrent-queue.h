// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <string>
#include <vector>

#include "libtransmission/macros.h"
#include "libtransmission/types.h"

class tr_torrent_queue
{
public:
    struct Mediator {
        virtual ~Mediator() = default;

        [[nodiscard]] virtual std::string config_dir() const = 0;
        [[nodiscard]] virtual std::string store_filename(tr_torrent_id_t id) const = 0;
    };

    explicit tr_torrent_queue(Mediator const& mediator)
        : mediator_{ mediator }
    {
    }
    ~tr_torrent_queue() = default;
    tr_torrent_queue(tr_torrent_queue const&) = delete;
    tr_torrent_queue(tr_torrent_queue&&) = delete;
    tr_torrent_queue& operator=(tr_torrent_queue const&) = delete;
    tr_torrent_queue& operator=(tr_torrent_queue&&) = delete;

    size_t add(tr_torrent_id_t id);
    void remove(tr_torrent_id_t id);

    [[nodiscard]] size_t get_pos(tr_torrent_id_t id) const noexcept;
    [[nodiscard]] std::vector<tr_torrent_id_t> set_pos(tr_torrent_id_t id, size_t new_pos);

    [[nodiscard]] TR_CONSTEXPR_VEC auto size() const noexcept
    {
        return queue_.size();
    }

    bool to_file(); // NOLINT(modernize-use-nodiscard)
    [[nodiscard]] std::vector<std::string> from_file();

    static auto constexpr MinQueuePosition = size_t{};
    static auto constexpr MaxQueuePosition = ~size_t{};

private:
    [[nodiscard]] constexpr auto is_dirty() const noexcept
    {
        return is_dirty_;
    }

    constexpr void set_dirty(bool is_dirty = true) noexcept
    {
        is_dirty_ = is_dirty;
    }

    // Records that `id` currently lives at `pos`, growing pos_of_ as needed.
    void set_pos_of(tr_torrent_id_t id, size_t pos);

    // Rewrites pos_of_ for the ids in queue_[first, last) so it stays in sync
    // with the queue after a mutation. Both bounds must be <= queue_.size().
    void reindex(size_t first, size_t last) noexcept;

    // queue_[pos] is the id at queue position `pos`.
    std::vector<tr_torrent_id_t> queue_;

    // pos_of_[id] is the queue position of `id`, or NotQueued if absent.
    // Kept eagerly in sync with `queue_` so that `get_pos()` is always O(1).
    std::vector<size_t> pos_of_;

    bool is_dirty_ = false;

    Mediator const& mediator_;

    // Sentinel stored in `pos_of_` for ids that are not in the queue.
    // A real queue position can never reach this value.
    static auto constexpr NotQueued = MaxQueuePosition;
};
