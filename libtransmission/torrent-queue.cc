// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/torrent-queue.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/variant.h"

namespace
{
using namespace std::literals;

[[nodiscard]] auto get_file_path(std::string_view config_dir) noexcept
{
    return tr_pathbuf{ config_dir, '/', "queue.json"sv };
}
} // namespace

void tr_torrent_queue::set_pos_of(tr_torrent_id_t const id, size_t const pos)
{
    auto const uid = static_cast<size_t>(id);
    if (uid >= std::size(pos_of_)) {
        pos_of_.resize(uid + 1U, NotQueued);
    }
    pos_of_[uid] = pos;
}

void tr_torrent_queue::reindex(size_t const first, size_t const last) noexcept
{
    for (auto pos = first; pos < last; ++pos) {
        pos_of_[static_cast<size_t>(queue_[pos])] = pos;
    }
}

size_t tr_torrent_queue::add(tr_torrent_id_t const id)
{
    auto const pos = std::size(queue_);
    queue_.push_back(id);
    set_pos_of(id, pos);
    set_dirty();
    return pos;
}

void tr_torrent_queue::remove(tr_torrent_id_t const id)
{
    auto const pos = get_pos(id);
    if (pos >= std::size(queue_)) {
        return; // not queued
    }

    using diff_type = decltype(queue_)::difference_type;
    queue_.erase(std::begin(queue_) + static_cast<diff_type>(pos));
    pos_of_[static_cast<size_t>(id)] = NotQueued;

    // everything after the removed slot shifted down by one
    reindex(pos, std::size(queue_));
    set_dirty();
}

size_t tr_torrent_queue::get_pos(tr_torrent_id_t const id) const noexcept
{
    auto const uid = static_cast<size_t>(id);
    return uid < std::size(pos_of_) ? pos_of_[uid] : NotQueued;
}

// returns the list of torrent IDs whose queue position changed
std::vector<tr_torrent_id_t> tr_torrent_queue::set_pos(tr_torrent_id_t const id, size_t new_pos)
{
    auto const n_queue = std::size(queue_);
    auto const old_pos = get_pos(id);
    if (old_pos >= n_queue) {
        return {};
    }

    new_pos = std::min(new_pos, n_queue - 1U);

    if (old_pos == new_pos) {
        return {};
    }

    auto ret = std::vector<tr_torrent_id_t>{};

    using diff_type = decltype(queue_)::difference_type;
    auto const begin = std::begin(queue_);
    auto const old_it = std::next(begin, static_cast<diff_type>(old_pos));
    auto const old_next_it = std::next(old_it);
    auto const new_it = std::next(begin, static_cast<diff_type>(new_pos));
    if (old_pos > new_pos) {
        ret.assign(new_it, old_next_it);
        std::rotate(new_it, old_it, old_next_it);
    } else {
        auto const new_next_it = std::next(new_it);
        ret.assign(old_it, new_next_it);
        std::rotate(old_it, old_next_it, new_next_it);
    }

    // only the ids in [min, max] were permuted by the rotate
    reindex(std::min(old_pos, new_pos), std::max(old_pos, new_pos) + 1U);
    set_dirty();
    return ret;
}

bool tr_torrent_queue::to_file()
{
    if (!is_dirty()) {
        return false;
    }
    set_dirty(false);

    auto vec = tr_variant::Vector{};
    vec.reserve(std::size(queue_));
    for (auto const id : queue_) {
        vec.emplace_back(mediator_.store_filename(id));
    }

    return tr_variant_serde::json().to_file(std::move(vec), get_file_path(mediator_.config_dir()));
}

std::vector<std::string> tr_torrent_queue::from_file()
{
    auto top = tr_variant_serde::json().parse_file(get_file_path(mediator_.config_dir()));
    if (!top) {
        return {};
    }

    auto const* const vec = top->get_if<tr_variant::Vector>();
    if (vec == nullptr) {
        return {};
    }

    auto ret = std::vector<std::string>{};
    ret.reserve(std::size(*vec));
    for (auto const& var : *vec) {
        if (auto file = var.value_if<std::string_view>(); file) {
            ret.emplace_back(*file);
        }
    }

    return ret;
}
