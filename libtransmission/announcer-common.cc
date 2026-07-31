// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::shuffle, std::rotate
#include <cstddef> // size_t, ptrdiff_t
#include <iterator> // std::next
#include <numeric> // std::iota
#include <optional>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "libtransmission/announcer-common.h"
#include "libtransmission/crypto-utils.h" // tr_urbg

// --- TRYING ORDER

tr_tracker_trying_order::tr_tracker_trying_order(size_t const tracker_count)
    : order_(tracker_count)
{
    static thread_local auto urbg = tr_urbg<size_t>{};
    std::iota(std::begin(order_), std::end(order_), size_t{});
    std::shuffle(std::begin(order_), std::end(order_), urbg);
}

std::optional<size_t> tr_tracker_trying_order::current() const
{
    if (pos_) {
        return order_[*pos_];
    }

    return {};
}

std::optional<size_t> tr_tracker_trying_order::advance()
{
    if (std::empty(order_)) {
        pos_ = std::nullopt;
    } else if (!pos_) {
        pos_ = 0U;
    } else {
        pos_ = (*pos_ + 1U) % std::size(order_);
    }

    return current();
}

void tr_tracker_trying_order::promote_current()
{
    if (pos_ && *pos_ != 0U) {
        auto const iter = std::next(std::begin(order_), static_cast<std::ptrdiff_t>(*pos_));
        std::rotate(std::begin(order_), iter, std::next(iter));
        pos_ = 0U;
    }
}

void tr_tracker_trying_order::set_current(std::optional<size_t> const index)
{
    if (index) {
        if (auto const iter = std::ranges::find(order_, *index); iter != std::end(order_)) {
            pos_ = static_cast<size_t>(iter - std::begin(order_));
            return;
        }
    }

    pos_ = std::nullopt;
}
