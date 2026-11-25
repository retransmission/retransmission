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
#include <vector>

#include "libtransmission/digest.h"
#include "libtransmission/types.h"

class tr_sha1;
struct tr_block_info;
struct tr_error;

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
 * A span holds as many whole pieces as LocalData::hash_span_bytes()
 * allows, or the next part of a piece too big for one span. A span of
 * several pieces that can't be read is read again a piece at a time, so
 * that one missing file or bad sector fails only the pieces it is in.
 * A span inside one file that finds the file missing ends the reading of
 * that file: each of its pieces is missing, and is reported unread.
 *
 * Between spans, the first verify in line whose torrent has no move or
 * rename pending hashes the next one. Verifies under way come first, so a
 * verify that waited out a move or rename picks up where it left off, and
 * a long copy holds up only its own torrent's verify.
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
        // The bytes of the file that holds byte `byte` of the torrent
        [[nodiscard]] virtual tr_byte_span_t file_span(uint64_t byte) const = 0;

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

    // `has_local_data` is false if none of the torrent's files exist, so
    // that every piece is missing and there is nothing to read.
    void add(std::unique_ptr<Mediator> mediator, tr_priority_t priority, bool has_local_data);

    // Ends the torrent's verify, queued or under way, as aborted. Doesn't wait
    // for the span in flight; its result is dropped.
    void remove(tr_torrent_id_t id);

    void set_sleep_per_seconds_during_verify(std::chrono::milliseconds sleep_per_seconds_during_verify) noexcept
    {
        sleep_per_seconds_during_verify_ = sleep_per_seconds_during_verify;
    }

private:
    // A verify, queued or under way, and where it is.
    struct Node {
        [[nodiscard]] bool operator<(Node const& that) const noexcept;

        std::unique_ptr<Mediator> mediator;
        tr_priority_t priority;
        tr_piece_index_t piece = 0U;
        uint64_t piece_bytes_hashed = 0U;
        // Pieces before this one are known to be missing, and are reported
        // unread: one that couldn't be read, those of a missing file, or
        // every piece of a torrent with no local data.
        tr_piece_index_t missing_end = 0U;
        // Spans before this piece hold one piece each: they retry a span
        // of several that couldn't be read.
        tr_piece_index_t single_piece_spans_end = 0U;
        // The piece's hash so far. Null until the verify starts, and while
        // a span is in flight.
        std::unique_ptr<tr_sha1> sha;
    };

    void step();
    [[nodiscard]] tr_byte_span_t next_span() const;
    void start_next();
    void report_missing();
    [[nodiscard]] bool report_piece(std::optional<tr_sha1_digest_t> const& hash);
    [[nodiscard]] std::optional<tr_piece_index_t> missing_end_after(tr_byte_span_t span, tr_error const& error) const;
    void schedule_step();
    [[nodiscard]] bool sleep_due() const;
    void on_span_hashed(
        uint64_t token,
        tr_byte_span_t span,
        tr_error const& error,
        std::vector<tr_sha1_digest_t> const& hashes,
        std::unique_ptr<tr_sha1> sha);
    void finish(bool aborted);

    tr::LocalData& local_data_;
    std::unique_ptr<tr::Timer> const step_timer_;

    std::set<Node> todo_;
    std::optional<Node> active_;
    // Bumped each time a verify becomes the active one. A span's result
    // counts only if this hasn't changed since the span went out.
    uint64_t active_token_ = 0U;

    // True while step() waits for the backend, which may answer before it returns.
    bool in_step_ = false;

    std::chrono::milliseconds sleep_per_seconds_during_verify_ = {};
    std::chrono::steady_clock::time_point last_slept_at_;
};
