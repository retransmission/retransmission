// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <chrono>
#include <cstdint> // uint64_t
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#include "libtransmission/digest.h"
#include "libtransmission/types.h"

class tr_sha1;
struct tr_block_info;

namespace tr
{
class LocalData;
class Timer;
class TimerMaker;
} // namespace tr

/**
 * Verifies torrents one at a time, a span at a time, through
 * tr::LocalData. So a verify waits for barriers like any other data op,
 * and a move or rename never pulls a file out from under it.
 *
 * Session thread only. The synchronous backend hashes each span on the
 * session thread, so the verify waits for a timer between spans there,
 * and other work gets in.
 */
class tr_verifier
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_torrent_id_t id() const = 0;
        [[nodiscard]] virtual tr_block_info const& block_info() const = 0;

        virtual void on_verify_started() = 0;
        // `hash` is empty if the piece couldn't be read
        virtual void on_piece_hashed(tr_piece_index_t piece, std::optional<tr_sha1_digest_t> const& hash) = 0;
        virtual void on_verify_done(bool aborted) = 0;
    };

    // Callbacks run under `session_mutex`.
    tr_verifier(tr::LocalData& local_data, tr::TimerMaker& timer_maker, std::recursive_mutex& session_mutex);
    ~tr_verifier();

    tr_verifier(tr_verifier const&) = delete;
    tr_verifier(tr_verifier&&) = delete;
    tr_verifier& operator=(tr_verifier const&) = delete;
    tr_verifier& operator=(tr_verifier&&) = delete;

    void add(std::unique_ptr<Mediator> mediator, tr_priority_t priority);

    // Ends the torrent's verify, queued or under way, as aborted. Doesn't wait
    // for the span in flight; its result is dropped.
    void remove(tr_torrent_id_t id);

    void set_sleep_per_seconds_during_verify(std::chrono::milliseconds sleep_per_seconds_during_verify) noexcept
    {
        sleep_per_seconds_during_verify_ = sleep_per_seconds_during_verify;
    }

private:
    struct Node {
        [[nodiscard]] bool operator<(Node const& that) const noexcept;

        std::unique_ptr<Mediator> mediator;
        tr_priority_t priority;
    };

    struct Active {
        std::unique_ptr<Mediator> mediator;
        uint64_t token;
        tr_piece_index_t piece = 0U;
        uint64_t piece_bytes_hashed = 0U;
        // the piece's hash so far. Empty while a span is in flight.
        std::unique_ptr<tr_sha1> sha;
    };

    void step();
    void schedule_step();
    [[nodiscard]] bool sleep_due() const;
    void on_span_hashed(uint64_t token, uint64_t n_bytes, bool ok, std::unique_ptr<tr_sha1> sha);
    void finish(bool aborted);

    tr::LocalData& local_data_;
    std::unique_ptr<tr::Timer> const step_timer_;

    std::set<Node> todo_;
    std::optional<Active> active_;
    uint64_t next_token_ = 0U;

    // True while step() waits for the backend, which may answer before it returns.
    bool in_step_ = false;

    std::chrono::milliseconds sleep_per_seconds_during_verify_ = {};
    std::chrono::steady_clock::time_point last_slept_at_;
};
