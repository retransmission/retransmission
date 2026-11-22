// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility> // for std::move()

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

// Hashes the next span, or starts the next torrent once this one is done.
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

    // The next span would wait for the move or rename, and every other
    // verify would wait with it.
    if (local_data_.admin_op_pending(active_->mediator->id())) {
        park();
        return;
    }

    auto const piece_span = block_info.byte_span_for_piece(active_->piece);
    auto const begin = piece_span.begin + active_->piece_bytes_hashed;
    auto const span = tr_byte_span_t{ .begin = begin, .end = std::min(piece_span.end, begin + local_data_.hash_span_bytes()) };

    in_step_ = true;
    local_data_.hash_span(
        active_->mediator->id(),
        span,
        std::move(active_->sha),
        [this, token = active_token_](tr_torrent_id_t, tr_byte_span_t const hashed, tr_error const& error, auto sha) {
            on_span_hashed(token, hashed.size(), !error, std::move(sha));
        });
    in_step_ = false;
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

// Puts the active verify back in the queue with its place kept, so that
// the next one can go while a move or rename holds this one back.
void tr_verifier::park()
{
    TR_ASSERT(active_);

    todo_.insert(std::move(*active_));
    active_.reset();
    schedule_step();
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

void tr_verifier::on_span_hashed(uint64_t const token, uint64_t const n_bytes, bool const ok, std::unique_ptr<tr_sha1> sha)
{
    // a verify that remove() ended
    if (!active_ || token != active_token_) {
        return;
    }

    auto const piece = active_->piece;
    active_->piece_bytes_hashed += n_bytes;
    active_->sha = std::move(sha);

    // an unreadable piece skips the rest of its spans
    if (!ok || active_->piece_bytes_hashed >= active_->mediator->block_info().piece_size(piece)) {
        auto const hash = ok ? std::optional{ active_->sha->finish() } : std::nullopt;
        active_->sha->clear();
        ++active_->piece;
        active_->piece_bytes_hashed = 0U;

        // last, since the torrent may end this verify
        active_->mediator->on_piece_hashed(piece, hash);
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

// Sleeping a few msec once a second goes a long way toward reducing the IO load.
bool tr_verifier::sleep_due() const
{
    return active_ && sleep_per_seconds_during_verify_ > 0ms && std::chrono::steady_clock::now() - last_slept_at_ >= 1s;
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
    // higher priority first, then smaller torrents, since they verify faster
    return std::tuple{ -priority, mediator->block_info().total_size(), mediator->id() } <
        std::tuple{ -that.priority, that.mediator->block_info().total_size(), that.mediator->id() };
}
