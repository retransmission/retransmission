// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <cstdint> // uint64_t
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility> // for std::move()
#include <vector>

#include "libtransmission/block-info.h"
#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/local-data.h"
#include "libtransmission/timer.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"
#include "libtransmission/verify.h"

using namespace std::chrono_literals;

namespace
{
// how often to look again when every queued verify waits for a move or rename
auto constexpr BarrierPollInterval = 1s;

// pieces of a torrent with no local data to report missing per step
auto constexpr MissingPiecesPerStep = tr_piece_index_t{ 1024U };
} // namespace

tr_verifier::tr_verifier(tr::LocalData& local_data, tr::TimerMaker& timer_maker, std::recursive_mutex& session_mutex)
    : local_data_{ local_data }
    , step_timer_{ timer_maker.create([this, &session_mutex] {
        auto const lock = std::scoped_lock{ session_mutex };
        step();
    }) }
{
}

tr_verifier::~tr_verifier() = default;

void tr_verifier::add(std::unique_ptr<Mediator> mediator, tr_priority_t const priority, bool const has_local_data)
{
    todo_.emplace(
        Node{ .mediator = std::move(mediator), .priority = priority, .has_local_data = has_local_data, .sha = nullptr });
    if (!active_) {
        schedule_step();
    }
}

void tr_verifier::remove(tr_torrent_id_t const id)
{
    if (active_ && active_->mediator->id() == id) {
        finish(true);
        return;
    }

    if (auto const iter = std::ranges::find_if(todo_, [id](Node const& node) { return node.mediator->id() == id; });
        iter != std::end(todo_)) {
        auto node = todo_.extract(iter);
        node.value().mediator->on_verify_done(true);
    }
}

// Hashes the next span of the first verify in line that can go, or
// finishes the active verify once its pieces are done.
void tr_verifier::step()
{
    if (!active_) {
        start_next();
    }

    // nothing can go, or a span is in flight
    if (!active_ || !active_->sha) {
        return;
    }

    auto const& block_info = active_->mediator->block_info();
    if (active_->piece >= block_info.piece_count()) {
        finish(false);
        return;
    }

    if (!active_->has_local_data) {
        report_all_missing();
        return;
    }

    in_step_ = true;
    local_data_.hash_span(
        active_->mediator->id(),
        next_span(),
        std::move(active_->sha),
        [this, token = active_token_](
            tr_torrent_id_t,
            tr_byte_span_t const span,
            tr_error const& error,
            auto const& hashes,
            auto sha) { on_span_hashed(token, span, error, hashes, std::move(sha)); });
    in_step_ = false;
}

// The active verify's next span: as many whole pieces as fit in one, or
// the next part of a piece too big to fit. A piece at a time while it
// reads a span that failed again.
tr_byte_span_t tr_verifier::next_span() const
{
    TR_ASSERT(active_);

    auto const& block_info = active_->mediator->block_info();
    auto const max_bytes = uint64_t{ local_data_.hash_span_bytes() };
    auto const piece = active_->piece;
    auto n_pieces = std::max(uint64_t{ 1U }, max_bytes / block_info.piece_size());
    if (piece < active_->single_piece_spans_end) {
        n_pieces = 1U;
    }
    auto const end_piece = std::min(uint64_t{ block_info.piece_count() }, piece + n_pieces);
    auto const last_piece = static_cast<tr_piece_index_t>(end_piece - 1U);

    auto const begin = block_info.byte_span_for_piece(piece).begin + active_->piece_bytes_hashed;
    return { .begin = begin, .end = std::min(block_info.byte_span_for_piece(last_piece).end, begin + max_bytes) };
}

// Makes the first verify in line whose torrent has no move or rename
// pending the active one.
void tr_verifier::start_next()
{
    auto const iter = std::ranges::find_if(todo_, [this](Node const& node) {
        return !local_data_.admin_op_pending(node.mediator->id());
    });
    if (iter == std::end(todo_)) {
        // Nothing says when a move or rename is done, so look again later.
        if (!std::empty(todo_)) {
            step_timer_->start_single_shot(BarrierPollInterval);
        }
        return;
    }

    active_ = std::move(todo_.extract(iter).value());
    ++active_token_;
    if (!active_->sha) {
        active_->sha = std::make_unique<tr_sha1>();
        active_->mediator->on_verify_started();
    }
}

// With no local data there is nothing to read, and every piece is missing.
// Reports a batch of them, so that each turn of the session thread is short.
void tr_verifier::report_all_missing()
{
    TR_ASSERT(active_);

    auto const token = active_token_;
    auto const end = std::min(active_->mediator->block_info().piece_count(), active_->piece + MissingPiecesPerStep);
    while (active_ && token == active_token_ && active_->piece < end) {
        auto const piece = active_->piece++;
        // the torrent may end this verify
        active_->mediator->on_piece_hashed(piece, std::nullopt);
    }

    schedule_step();
}

// Reports the active verify's next piece, and moves past it.
// False if the torrent ended the verify.
bool tr_verifier::report_piece(std::optional<tr_sha1_digest_t> const& hash)
{
    TR_ASSERT(active_);

    auto const token = active_token_;
    auto const piece = active_->piece++;
    active_->mediator->on_piece_hashed(piece, hash);
    return active_ && token == active_token_;
}

void tr_verifier::on_span_hashed(
    uint64_t const token,
    tr_byte_span_t const span,
    tr_error const& error,
    std::vector<tr_sha1_digest_t> const& hashes,
    std::unique_ptr<tr_sha1> sha)
{
    // a verify that remove() ended
    if (!active_ || token != active_token_) {
        return;
    }

    active_->sha = std::move(sha);

    auto const& block_info = active_->mediator->block_info();
    if (!error) {
        // The span may end partway through the piece after those it finished.
        auto const next_piece = active_->piece + static_cast<tr_piece_index_t>(std::size(hashes));
        auto const next_begin = next_piece < block_info.piece_count() ? block_info.byte_span_for_piece(next_piece).begin :
                                                                        span.end;
        TR_ASSERT(next_begin <= span.end);
        active_->piece_bytes_hashed = span.end - next_begin;

        for (auto const& hash : hashes) {
            if (!report_piece(hash)) {
                break;
            }
        }
    } else if (auto const last_piece = block_info.byte_loc(span.end - 1U).piece; last_piece > active_->piece) {
        // read them again a piece at a time, so that the readable ones count
        active_->single_piece_spans_end = last_piece + 1U;
    } else {
        // an unreadable piece skips the rest of its spans
        active_->sha->clear();
        active_->piece_bytes_hashed = 0U;
        std::ignore = report_piece(std::nullopt);
    }

    // The next span goes to the first verify in line, which may be one
    // that waited for a move or rename. A verify with no pieces left stays
    // active, and finishes at the next step.
    if (active_ && token == active_token_ && active_->piece < active_->mediator->block_info().piece_count()) {
        todo_.insert(std::move(*active_));
        active_.reset();
    }

    // A threaded backend answers on a turn of its own, so go on at once.
    // The synchronous one answers inside step(), which mustn't recurse.
    if (in_step_ || sleep_due()) {
        schedule_step();
    } else {
        step();
    }
}

void tr_verifier::finish(bool const aborted)
{
    TR_ASSERT(active_);

    auto const mediator = std::move(active_->mediator);
    active_.reset();
    mediator->on_verify_done(aborted);
    schedule_step();
}

// Sleeping a few msec once a second goes a long way toward reducing the IO
// load. Only a verify under way sleeps. Between its spans, it's first in line.
bool tr_verifier::sleep_due() const
{
    auto const under_way = active_ || (!std::empty(todo_) && std::begin(todo_)->sha != nullptr);
    return under_way && sleep_per_seconds_during_verify_ > 0ms && std::chrono::steady_clock::now() - last_slept_at_ >= 1s;
}

void tr_verifier::schedule_step()
{
    if (sleep_due()) {
        last_slept_at_ = std::chrono::steady_clock::now();
        step_timer_->start_single_shot(sleep_per_seconds_during_verify_);
        return;
    }

    step_timer_->start_single_shot(0ms);
}

bool tr_verifier::Node::operator<(Node const& that) const noexcept
{
    // Verifies under way first, so that one that waited for a move or rename
    // goes on. Then higher priority, then smaller torrents, which verify faster.
    return std::tuple{ !sha, -priority, mediator->block_info().total_size(), mediator->id() } <
        std::tuple{ !that.sha, -that.priority, that.mediator->block_info().total_size(), that.mediator->id() };
}
