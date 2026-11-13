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
#include "libtransmission/types.h"
#include "libtransmission/verify.h"

using namespace std::chrono_literals;

tr_verifier::tr_verifier(tr::LocalData& local_data, tr::TimerMaker& timer_maker, std::recursive_mutex& session_mutex)
    : local_data_{ local_data }
    , step_timer_{ timer_maker.create([this, &session_mutex] {
        auto const lock = std::scoped_lock{ session_mutex };
        step();
    }) }
{
}

tr_verifier::~tr_verifier() = default;

void tr_verifier::add(std::unique_ptr<Mediator> mediator, tr_priority_t const priority)
{
    todo_.emplace(Node{ .mediator = std::move(mediator), .priority = priority });
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
        if (std::empty(todo_)) {
            return;
        }

        active_ = Active{ .mediator = std::move(todo_.extract(std::begin(todo_)).value().mediator),
                          .token = ++next_token_,
                          .sha = std::make_unique<tr_sha1>() };
        active_->mediator->on_verify_started();
    }

    // a span is in flight
    if (!active_->sha) {
        return;
    }

    auto const& block_info = active_->mediator->block_info();
    if (active_->piece >= block_info.piece_count()) {
        finish(false);
        return;
    }

    auto const piece_span = block_info.byte_span_for_piece(active_->piece);
    auto const begin = piece_span.begin + active_->piece_bytes_hashed;
    auto const span = tr_byte_span_t{ .begin = begin,
                                      .end = std::min(piece_span.end, begin + tr::LocalData::max_hash_span_bytes()) };

    in_step_ = true;
    local_data_.hash_span(
        active_->mediator->id(),
        span,
        std::move(active_->sha),
        [this, token = active_->token](tr_torrent_id_t, tr_byte_span_t const hashed, tr_error const& error, auto sha) {
            on_span_hashed(token, hashed.size(), !error, std::move(sha));
        });
    in_step_ = false;
}

void tr_verifier::on_span_hashed(uint64_t const token, uint64_t const n_bytes, bool const ok, std::unique_ptr<tr_sha1> sha)
{
    // a verify that remove() ended
    if (!active_ || active_->token != token) {
        return;
    }

    auto const piece = active_->piece;
    active_->piece_bytes_hashed += n_bytes;

    // an unreadable piece skips the rest of its spans
    if (ok && active_->piece_bytes_hashed < active_->mediator->block_info().piece_size(piece)) {
        active_->sha = std::move(sha);
    } else {
        auto const hash = ok ? std::optional{ sha->finish() } : std::nullopt;
        sha->clear();
        active_->sha = std::move(sha);
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
    if (!active_) {
        return;
    }

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
