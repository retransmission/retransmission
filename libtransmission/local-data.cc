// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::ranges::shuffle
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "libtransmission/local-data.h"

#include "libtransmission/block-info.h"
#include "libtransmission/error.h"
#include "libtransmission/inout.h"
#include "libtransmission/open-files.h"
#include "libtransmission/storage-descriptor.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/transmission.h"

namespace tr
{
namespace
{
[[nodiscard]] tr_error make_error(tr_error_code_t err)
{
    auto error = tr_error{};
    if (err != 0) {
        error.set_from_errno(err);
    }

    return error;
}

class DefaultBackend final : public LocalData::Backend
{
public:
    DefaultBackend(tr_torrents const& torrents, tr_open_files& open_files)
        : open_files_{ open_files }
        , torrents_{ torrents }
    {
    }

    [[nodiscard]] tr_error_code_t read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData& setme)
        override
    {
        if (!byte_span.is_valid()) {
            return TR_ERROR_EINVAL;
        }

        auto const len = byte_span.size();
        if (len > tr_block_info::BlockSize) {
            return TR_ERROR_EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto const* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        setme.resize(span_size);
        return tr_ioRead(*tor->storage_descriptor(), open_files_, byte_span.begin, std::span{ std::data(setme), span_size });
    }

    [[nodiscard]] tr_error_code_t test_piece(
        tr_torrent_id_t const id,
        tr_piece_index_t const piece,
        tr_sha1_digest_t& setme_hash) override
    {
        auto const* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        return tr_ioRecalculateHash(*tor->storage_descriptor(), open_files_, piece, setme_hash);
    }

    [[nodiscard]] tr_error_code_t write(
        tr_torrent_id_t const id,
        tr_byte_span_t const byte_span,
        LocalData::BlockData const& data) override
    {
        if (!byte_span.is_valid()) {
            return TR_ERROR_EINVAL;
        }

        auto const len = byte_span.size();
        if (len > std::size(data)) {
            return TR_ERROR_EINVAL;
        }
        auto const span_size = static_cast<size_t>(len);

        auto const* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        auto const result = tr_ioWrite(
            *tor->storage_descriptor(),
            open_files_,
            byte_span.begin,
            std::span{ std::data(data), span_size });
        for (auto i = size_t{}; i < result.n_files_created; ++i) {
            tor->session->add_file_created();
        }

        return result.error;
    }

    [[nodiscard]] tr_error_code_t move(
        tr_torrent_id_t const id,
        std::string_view const old_parent,
        std::string_view const parent,
        std::string_view const parent_name) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        auto error = tr_error{};
        if (tor->files().move(old_parent, parent, parent_name, &error)) {
            return 0;
        }

        return error ? error.code() : EIO;
    }

    [[nodiscard]] tr_error_code_t remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        if (!remove_func) {
            remove_func = tr_sys_path_remove;
        }

        auto error = tr_error{};
        tor->files().remove(tor->current_dir().sv(), tor->name(), remove_func, &error);
        return error ? error.code() : 0;
    }

    void rename(
        tr_torrent_id_t const id,
        std::string_view const oldpath,
        std::string_view const newname,
        tr_torrent_rename_done_func callback) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr) {
            if (callback != nullptr) {
                callback(id, oldpath, newname, make_error(TR_ERROR_EINVAL));
            }
            return;
        }

        tor->rename_path_in_session_thread(oldpath, newname, callback);
    }

    void close_all() override
    {
        open_files_.close_all();
    }

    void close_torrent(tr_torrent_id_t const tor_id) override
    {
        open_files_.close_torrent(tor_id);
    }

    void close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num) override
    {
        open_files_.close_file(tor_id, file_num);
    }

private:
    tr_open_files& open_files_;
    tr_torrents const& torrents_;
};

/**
 * Block buffers kept after their writes finish, so that a piece can be
 * hashed from memory instead of being read back from disk.
 *
 * Entries are per piece. Once the entries exceed the cap, the piece
 * written to least recently is dropped first. A hash takes its piece's
 * entry and can use it only if every block is still here; a block that
 * was dropped means the hash reads the piece from disk instead.
 *
 * Thread-safe.
 */
class RetainedBlocks
{
public:
    using Block = std::shared_ptr<LocalData::BlockData const>;

    explicit RetainedBlocks(size_t const max_bytes)
        : max_bytes_{ max_bytes }
    {
    }

    void stash(tr_torrent_id_t const id, tr_block_info const& block_info, tr_block_index_t const block, Block const& data)
    {
        auto const [begin_byte, end_byte] = block_info.byte_span_for_block(block);
        auto const first_piece = block_info.byte_loc(begin_byte).piece;
        auto const last_piece = block_info.byte_loc(end_byte - 1U).piece;
        auto const n_bytes = std::size(*data);

        auto const lock = std::scoped_lock{ mutex_ };

        // A block that straddles two pieces belongs to both entries.
        for (auto piece = first_piece; piece <= last_piece; ++piece) {
            auto const key = Key{ .tor_id = id, .piece = piece };
            auto& entry = entries_[key];
            if (std::empty(entry.blocks)) {
                auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
                entry.first_block = begin_block;
                entry.blocks.resize(end_block - begin_block);
                entry.lru = lru_.insert(std::end(lru_), key);
            } else {
                lru_.splice(std::end(lru_), lru_, entry.lru);
            }

            auto& slot = entry.blocks[block - entry.first_block];
            if (!slot) {
                ++entry.n_have;
                entry.bytes += n_bytes;
                bytes_ += n_bytes;
            }
            slot = data;
        }

        while (bytes_ > max_bytes_ && !std::empty(lru_)) {
            erase(entries_.find(lru_.front()));
        }
    }

    // Takes the piece's blocks. Empty unless every one of them is here.
    [[nodiscard]] std::vector<Block> take(tr_torrent_id_t const id, tr_piece_index_t const piece)
    {
        auto const lock = std::scoped_lock{ mutex_ };

        auto const it = entries_.find(Key{ .tor_id = id, .piece = piece });
        if (it == std::end(entries_)) {
            return {};
        }

        auto blocks = std::vector<Block>{};
        if (auto& entry = it->second; entry.n_have == std::size(entry.blocks)) {
            blocks = std::move(entry.blocks);
        }

        erase(it);
        return blocks;
    }

    void forget(tr_torrent_id_t const id)
    {
        auto const lock = std::scoped_lock{ mutex_ };

        auto it = entries_.lower_bound(Key{ .tor_id = id, .piece = 0U });
        while (it != std::end(entries_) && it->first.tor_id == id) {
            it = erase(it);
        }
    }

private:
    struct Key {
        tr_torrent_id_t tor_id;
        tr_piece_index_t piece;

        [[nodiscard]] auto operator<=>(Key const&) const noexcept = default;
    };

    struct Entry {
        std::vector<Block> blocks;
        tr_block_index_t first_block = 0U;
        size_t n_have = 0U;
        size_t bytes = 0U;
        std::list<Key>::iterator lru;
    };

    using Entries = std::map<Key, Entry>;

    Entries::iterator erase(Entries::iterator const it)
    {
        bytes_ -= it->second.bytes;
        lru_.erase(it->second.lru);
        return entries_.erase(it);
    }

    std::mutex mutex_;
    Entries entries_;
    std::list<Key> lru_;
    size_t bytes_ = 0U;
    size_t const max_bytes_;
};

} // namespace

/**
 * The threaded backend.
 *
 * Threading model:
 *
 * - Every public method runs on the session thread. Only the session
 *   thread touches the gate state (`gates_`), so it needs no lock.
 * - The pending ops and the finished completions are the two
 *   structures shared with the workers, each behind its own mutex.
 *   Workers pull pending ops, run them, and post completion closures
 *   to `done_`. The session thread delivers those from pump_done().
 * - `retained_` is shared too and locks itself.
 *
 * The admission gate implements rule 3 of the ordering contract in
 * local-data.h. Data ops on a torrent are admitted freely and run
 * concurrently. An admin op waits in the torrent's queue until every
 * admitted op's completion has been delivered, and then runs
 * exclusively on the session thread. Ops enqueued behind it wait in
 * the queue until it finishes. This gate is what makes move, rename,
 * remove, and close safe against in-flight writes.
 *
 * Reads run on the session thread the moment the gate admits them, so
 * a read completes before the enqueue call returns unless a barrier
 * is holding it back.
 *
 * Write scheduling: pending writes are indexed by (torrent, offset). A
 * worker takes the next one past its last position, sweeping the index
 * like an elevator, then takes that write's contiguous successors and
 * writes the whole run with one call. That turns the swarm's random
 * block order into sequential disk writes of up to MaxRunBytes.
 *
 * A written block's buffer is kept in `retained_` so that the piece
 * hash can consume it instead of reading the piece back from disk.
 */
class LocalData::Threaded final : public std::enable_shared_from_this<LocalData::Threaded>
{
public:
    using ReadExec = std::function<tr_error_code_t(tr_torrent_id_t, tr_byte_span_t, BlockData&)>;

    Threaded(
        tr_open_files& open_files,
        DescriptorProvider provider,
        Marshal marshal,
        ReadExec read_exec,
        OnFilesCreated on_files_created,
        size_t const n_workers)
        : open_files_{ open_files }
        , provider_{ std::move(provider) }
        , marshal_{ std::move(marshal) }
        , read_exec_{ std::move(read_exec) }
        , on_files_created_{ std::move(on_files_created) }
    {
        workers_.reserve(n_workers);

        // If a spawn fails partway, join the workers that did start.
        // Letting the exception destroy joinable threads would call
        // std::terminate.
        try {
            for (auto i = size_t{}; i < n_workers; ++i) {
                workers_.emplace_back([this]() { worker_main(); });
            }
        } catch (...) {
            stop_workers();
            throw;
        }
    }

    Threaded(Threaded const&) = delete;
    Threaded(Threaded&&) = delete;
    Threaded& operator=(Threaded const&) = delete;
    Threaded& operator=(Threaded&&) = delete;

    ~Threaded()
    {
        stop_workers();
    }

    void read(tr_torrent_id_t const id, tr_byte_span_t const span, OnRead on_read)
    {
        submit(id, ReadOp{ .span = span, .on_read = std::move(on_read) });
    }

    void test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
    {
        submit(id, TestOp{ .piece = piece, .on_test = std::move(on_test) });
    }

    void write(tr_torrent_id_t const id, tr_byte_span_t const span, std::unique_ptr<BlockData> data, OnWrite on_write)
    {
        submit(id, WriteOp{ .span = span, .data = std::move(data), .on_write = std::move(on_write) });
    }

    // Enqueue an admin op. `body` runs exclusively on the session
    // thread once the ops in flight have drained. It must deliver its
    // own completion.
    void barrier(tr_torrent_id_t const id, std::function<void()> body)
    {
        submit(id, BarrierOp{ .body = std::move(body) });
    }

    // Drop the buffers kept for a torrent whose files are closed.
    void forget(tr_torrent_id_t const id)
    {
        retained_.forget(id);
    }

    // Block until every op in flight, and every op those completions
    // enqueue, has been delivered. On return the gates are idle.
    void drain_all()
    {
        for (;;) {
            pump_done();

            if (idle()) {
                return;
            }

            auto lock = std::unique_lock{ done_mutex_ };
            done_cv_.wait(lock, [this]() { return !std::empty(done_); });
        }
    }

    void shutdown()
    {
        drain_all();
        stop_workers();
    }

    [[nodiscard]] uint64_t enqueued_write_bytes() const noexcept
    {
        return enqueued_write_bytes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] Stats stats() const noexcept
    {
        return { .write_runs = write_runs_.load(std::memory_order_relaxed),
                 .blocks_written = blocks_written_.load(std::memory_order_relaxed),
                 .hashes_from_buffers = hashes_from_buffers_.load(std::memory_order_relaxed),
                 .hashes_from_disk = hashes_from_disk_.load(std::memory_order_relaxed) };
    }

    void set_paused(bool const paused)
    {
        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            paused_ = paused;
        }
        work_cv_.notify_all();
    }

private:
    struct ReadOp {
        tr_byte_span_t span;
        OnRead on_read;
    };

    struct TestOp {
        tr_piece_index_t piece;
        OnTest on_test;
    };

    struct WriteOp {
        tr_byte_span_t span;
        std::unique_ptr<BlockData> data;
        OnWrite on_write;
    };

    struct BarrierOp {
        std::function<void()> body;
    };

    using Queued = std::variant<ReadOp, TestOp, WriteOp, BarrierOp>;

    struct Gate {
        size_t n_running = 0U;
        bool barrier_running = false;
        std::deque<Queued> queue;
#ifdef TR_ENABLE_ASSERTS
        // The byte spans of the data ops in flight, and whether each
        // one is a write. New ops assert that they don't overlap them.
        std::vector<std::pair<tr_byte_span_t, bool>> running_spans;
#endif
    };

    // A write the gate admitted, waiting for a worker.
    struct PendingWrite {
        tr_torrent_id_t tor_id;
        std::shared_ptr<StorageDescriptor const> desc;
        tr_byte_span_t span;
        std::unique_ptr<BlockData> data;
        OnWrite on_write;
    };

    // A piece hash the gate admitted, waiting for a worker.
    struct PendingTest {
        tr_torrent_id_t tor_id;
        std::shared_ptr<StorageDescriptor const> desc;
        tr_piece_index_t piece;
        OnTest on_test;
    };

    struct WriteKey {
        tr_torrent_id_t tor_id;
        uint64_t begin;

        [[nodiscard]] auto operator<=>(WriteKey const&) const noexcept = default;
    };

    // Adjacent pending writes are combined into runs of up to this many
    // bytes and written with one call.
    static auto constexpr MaxRunBytes = uint64_t{ 1024U * 1024U };

    // How much written block data to keep around for piece hashes.
    static auto constexpr MaxRetainedBytes = size_t{ 32U * 1024U * 1024U };

    // --- the admission gate. Session thread only.

    [[nodiscard]] static bool is_blocked(Gate const& gate) noexcept
    {
        return gate.barrier_running || !std::empty(gate.queue);
    }

    // Every pending or running op holds its gate's n_running, and idle
    // gates are erased, so the map alone says whether anything is in
    // flight.
    [[nodiscard]] bool idle() const noexcept
    {
        return std::ranges::all_of(gates_, [](auto const& id_and_gate) {
            auto const& gate = id_and_gate.second;
            return gate.n_running == 0U && !gate.barrier_running && std::empty(gate.queue);
        });
    }

    // Queue the op if the gate blocks it, else run it, then run
    // whatever the gate allows next. Barriers always queue; advance()
    // is what grants them.
    void submit(tr_torrent_id_t const id, Queued&& op)
    {
        if (auto& gate = gates_[id]; std::holds_alternative<BarrierOp>(op) || is_blocked(gate)) {
            gate.queue.emplace_back(std::move(op));
        } else {
            dispatch(id, gate, std::move(op));
        }

        advance(id);
    }

    void dispatch(tr_torrent_id_t const id, Gate& gate, Queued&& op)
    {
        if (auto* const read = std::get_if<ReadOp>(&op); read != nullptr) {
            exec_read(id, *read);
        } else if (auto* const test = std::get_if<TestOp>(&op); test != nullptr) {
            admit_test(id, gate, std::move(*test));
        } else {
            admit_write(id, gate, std::get<WriteOp>(std::move(op)));
        }
    }

    // Run queued ops that the gate now allows, and erase the gate once
    // nothing references it.
    void advance(tr_torrent_id_t const id)
    {
        for (;;) {
            auto const it = gates_.find(id);
            if (it == std::end(gates_)) {
                return;
            }

            auto& gate = it->second;
            if (gate.barrier_running) {
                return;
            }

            if (std::empty(gate.queue)) {
                if (gate.n_running == 0U) {
                    gates_.erase(it);
                }
                return;
            }

            if (std::holds_alternative<BarrierOp>(gate.queue.front())) {
                if (gate.n_running != 0U) {
                    return; // still draining
                }

                auto op = std::get<BarrierOp>(std::move(gate.queue.front()));
                gate.queue.pop_front();
                gate.barrier_running = true;
                op.body();

                // the body may have added or erased gates
                if (auto const it2 = gates_.find(id); it2 != std::end(gates_)) {
                    it2->second.barrier_running = false;
                }

                continue;
            }

            auto op = std::move(gate.queue.front());
            gate.queue.pop_front();
            dispatch(id, gate, std::move(op));
        }
    }

    void release(tr_torrent_id_t const id)
    {
        auto const it = gates_.find(id);
        TR_ASSERT(it != std::end(gates_));
        TR_ASSERT(it->second.n_running > 0U);

        --it->second.n_running;
        advance(id);
    }

    // A write must never overlap an op in flight, and a hash must never
    // overlap a write in flight. The protocol already guarantees both:
    // we write only blocks we lack, and hash or read only pieces whose
    // writes have all completed. A failure here is a caller bug, not a
    // backend race.
    static void register_running_span(
        [[maybe_unused]] Gate& gate,
        [[maybe_unused]] tr_byte_span_t const span,
        [[maybe_unused]] bool const is_write)
    {
#ifdef TR_ENABLE_ASSERTS
        for (auto const& [running, running_is_write] : gate.running_spans) {
            if (is_write || running_is_write) {
                TR_ASSERT(span.end <= running.begin || running.end <= span.begin);
            }
        }

        gate.running_spans.emplace_back(span, is_write);
#endif
    }

    void unregister_running_span([[maybe_unused]] tr_torrent_id_t const id, [[maybe_unused]] tr_byte_span_t const span)
    {
#ifdef TR_ENABLE_ASSERTS
        auto& spans = gates_[id].running_spans;
        auto const it = std::ranges::find_if(spans, [&span](auto const& running) {
            return running.first.begin == span.begin && running.first.end == span.end;
        });
        TR_ASSERT(it != std::end(spans));
        spans.erase(it);
#endif
    }

    void exec_read(tr_torrent_id_t const id, ReadOp const& op)
    {
        auto data = std::make_unique<BlockData>();
        auto const err = read_exec_(id, op.span, *data);
        if (err != 0) {
            data = nullptr;
        }

        if (op.on_read) {
            op.on_read(id, op.span, make_error(err), std::move(data));
        }
    }

    void admit_test(tr_torrent_id_t const id, Gate& gate, TestOp op)
    {
        auto desc = provider_(id);
        if (!desc || op.piece >= desc->block_info.piece_count()) {
            if (op.on_test) {
                std::move(op.on_test)(id, op.piece, make_error(TR_ERROR_EINVAL), {});
            }
            return;
        }

        ++gate.n_running;
        register_running_span(gate, desc->block_info.byte_span_for_piece(op.piece), false);

        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            pending_tests_.push_back(
                PendingTest{ .tor_id = id, .desc = std::move(desc), .piece = op.piece, .on_test = std::move(op.on_test) });
        }
        work_cv_.notify_one();
    }

    void admit_write(tr_torrent_id_t const id, Gate& gate, WriteOp op)
    {
        auto const span = op.span;
        auto desc = std::shared_ptr<StorageDescriptor const>{};
        if (span.is_valid() && op.data != nullptr && span.size() <= std::size(*op.data)) {
            desc = provider_(id);
        }

        if (desc && span.end > desc->block_info.total_size()) {
            desc = nullptr;
        }

        if (!desc) {
            if (op.on_write) {
                std::move(op.on_write)(id, span, make_error(TR_ERROR_EINVAL));
            }
            return;
        }

        ++gate.n_running;
        register_running_span(gate, span, true);
        enqueued_write_bytes_.fetch_add(span.size(), std::memory_order_relaxed);

        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            pending_writes_.emplace(
                WriteKey{ .tor_id = id, .begin = span.begin },
                PendingWrite{ .tor_id = id,
                              .desc = std::move(desc),
                              .span = span,
                              .data = std::move(op.data),
                              .on_write = std::move(op.on_write) });
        }
        work_cv_.notify_one();
    }

    // The completion closures own move-only state, so they can't live
    // in a std::function. Park them instead.
    template<typename Fn>
    void post_completion(Fn&& fn)
    {
        auto completion = std::make_unique<ParkedFn<std::decay_t<Fn>>>(std::forward<Fn>(fn));

        auto need_wake = bool{};
        {
            auto const lock = std::scoped_lock{ done_mutex_ };
            done_.emplace_back(std::move(completion));
            need_wake = !pump_scheduled_;
            pump_scheduled_ = true;
        }

        done_cv_.notify_all();

        if (need_wake) {
            marshal_([weak = weak_from_this()]() {
                if (auto const self = weak.lock(); self) {
                    self->pump_done();
                }
            });
        }
    }

    void pump_done()
    {
        auto done = std::deque<std::unique_ptr<Parked>>{};
        {
            auto const lock = std::scoped_lock{ done_mutex_ };
            std::swap(done, done_);
            pump_scheduled_ = false;
        }

        for (auto& completion : done) {
            completion->deliver();
        }
    }

    // --- worker-side code

    void worker_main()
    {
        for (;;) {
            auto test = std::optional<PendingTest>{};
            auto run = std::vector<PendingWrite>{};

            {
                auto lock = std::unique_lock{ work_mutex_ };
                work_cv_.wait(lock, [this]() { return has_work() || (stopping_ && !paused_); });

                if (!std::empty(pending_tests_) && !paused_) {
                    test = std::move(pending_tests_.front());
                    pending_tests_.pop_front();
                } else if (!std::empty(pending_writes_) && !paused_) {
                    run = take_write_run();
                } else {
                    return; // stopping
                }
            }

            if (test) {
                exec_test(std::move(*test));
            } else {
                exec_write_run(std::move(run));
            }
        }
    }

    [[nodiscard]] bool has_work() const noexcept
    {
        return !paused_ && (!std::empty(pending_tests_) || !std::empty(pending_writes_));
    }

    // Take the next write past the cursor, plus the writes contiguous
    // with it. Call with work_mutex_ held and pending_writes_ nonempty.
    [[nodiscard]] std::vector<PendingWrite> take_write_run()
    {
        auto it = pending_writes_.lower_bound(cursor_);
        if (it == std::end(pending_writes_)) {
            it = std::begin(pending_writes_);
        }

        auto const tor_id = it->second.tor_id;
        auto const desc = it->second.desc;
        auto next_byte = it->second.span.begin;
        auto n_bytes = uint64_t{};

        auto run = std::vector<PendingWrite>{};
        while (it != std::end(pending_writes_)) {
            auto const& op = it->second;
            if (op.tor_id != tor_id || op.desc != desc || op.span.begin != next_byte ||
                n_bytes + op.span.size() > MaxRunBytes) {
                break;
            }

            n_bytes += op.span.size();
            next_byte = op.span.end;
            cursor_ = it->first;
            run.emplace_back(std::move(it->second));
            it = pending_writes_.erase(it);
        }

        return run;
    }

    void stop_workers()
    {
        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            stopping_ = true;
            paused_ = false;
        }
        work_cv_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();
    }

    void exec_write_run(std::vector<PendingWrite> run)
    {
        TR_ASSERT(!std::empty(run));

        auto const& desc = *run.front().desc;
        auto const begin = run.front().span.begin;
        auto const n_bytes = static_cast<size_t>(run.back().span.end - begin);

        auto result = tr_io_write_result{};
        if (std::size(run) == 1U) {
            result = tr_ioWrite(desc, open_files_, begin, { std::data(*run.front().data), n_bytes });
        } else {
            // Adjacent blocks go to the disk as one write.
            thread_local auto buf = std::vector<uint8_t>{};
            buf.resize(n_bytes);
            for (auto const& op : run) {
                std::copy_n(std::data(*op.data), op.span.size(), std::data(buf) + (op.span.begin - begin));
            }

            result = tr_ioWrite(desc, open_files_, begin, buf);
        }

        write_runs_.fetch_add(1U, std::memory_order_relaxed);
        blocks_written_.fetch_add(std::size(run), std::memory_order_relaxed);

        for (auto& op : run) {
            enqueued_write_bytes_.fetch_sub(op.span.size(), std::memory_order_relaxed);

            // Keep the block for its piece's hash. Only whole blocks
            // are kept, since that's the unit the hash pulls.
            auto const block = desc.block_info.byte_loc(op.span.begin).block;
            auto const block_span = desc.block_info.byte_span_for_block(block);
            if (result.error == 0 && op.span.begin == block_span.begin && op.span.end == block_span.end) {
                retained_.stash(op.tor_id, desc.block_info, block, std::move(op.data));
            }
        }

        post_completion([this, run = std::move(run), err = result.error, n_created = result.n_files_created]() mutable {
            auto const id = run.front().tor_id;

            if (n_created > 0U && on_files_created_) {
                on_files_created_(id, n_created);
            }

            // Every callback runs before any release, so a barrier
            // enqueued by one of them can't overtake the rest of the run.
            for (auto const& op : run) {
                unregister_running_span(id, op.span);
            }
            for (auto& op : run) {
                if (op.on_write) {
                    std::move(op.on_write)(id, op.span, make_error(err));
                }
            }
            for (auto i = size_t{}; i < std::size(run); ++i) {
                release(id);
            }
        });
    }

    void exec_test(PendingTest op)
    {
        auto const& block_info = op.desc->block_info;
        auto hash = tr_sha1_digest_t{};
        auto err = tr_error_code_t{};

        if (auto const blocks = retained_.take(op.tor_id, op.piece); !std::empty(blocks)) {
            auto const first_block = block_info.block_span_for_piece(op.piece).begin;
            auto const found = tr_ioHashPiece(
                block_info,
                op.piece,
                [&blocks, first_block](tr_block_index_t const block) -> std::span<uint8_t const> {
                    auto const& data = *blocks[block - first_block];
                    return { std::data(data), std::size(data) };
                });
            TR_ASSERT(found.has_value()); // every block was here, and none is empty
            hash = found.value_or(tr_sha1_digest_t{});
            err = found ? 0 : EIO;
            hashes_from_buffers_.fetch_add(1U, std::memory_order_relaxed);
        } else {
            err = tr_ioRecalculateHash(*op.desc, open_files_, op.piece, hash);
            hashes_from_disk_.fetch_add(1U, std::memory_order_relaxed);
        }

        post_completion([this, op = std::move(op), err, hash]() mutable {
            auto const id = op.tor_id;
            unregister_running_span(id, op.desc->block_info.byte_span_for_piece(op.piece));
            if (op.on_test) {
                std::move(op.on_test)(
                    id,
                    op.piece,
                    make_error(err),
                    err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
            }
            release(id);
        });
    }

    // --- session-thread state

    tr_open_files& open_files_;
    DescriptorProvider provider_;
    Marshal marshal_;
    ReadExec read_exec_;
    OnFilesCreated on_files_created_;

    std::map<tr_torrent_id_t, Gate> gates_;

    // --- state shared with the workers

    std::mutex work_mutex_;
    std::condition_variable work_cv_;
    std::deque<PendingTest> pending_tests_;
    std::map<WriteKey, PendingWrite> pending_writes_;
    WriteKey cursor_ = {};
    bool stopping_ = false;
    bool paused_ = false;

    std::mutex done_mutex_;
    std::condition_variable done_cv_;
    std::deque<std::unique_ptr<Parked>> done_;
    bool pump_scheduled_ = false;

    RetainedBlocks retained_{ MaxRetainedBytes };

    std::atomic<uint64_t> enqueued_write_bytes_ = 0U;
    std::atomic<uint64_t> write_runs_ = 0U;
    std::atomic<uint64_t> blocks_written_ = 0U;
    std::atomic<uint64_t> hashes_from_buffers_ = 0U;
    std::atomic<uint64_t> hashes_from_disk_ = 0U;

    std::vector<std::thread> workers_;
};

LocalData::LocalData(tr_torrents const& torrents, tr_open_files& open_files)
    : backend_{ std::make_unique<DefaultBackend>(torrents, open_files) }
    , torrents_{ &torrents }
    , open_files_{ &open_files }
{
}

LocalData::LocalData(std::unique_ptr<Backend> backend)
    : backend_{ std::move(backend) }
{
}

LocalData::~LocalData() = default;

void LocalData::start_workers(
    size_t worker_count,
    Marshal marshal,
    DescriptorProvider provider,
    OnFilesCreated on_files_created)
{
    TR_ASSERT(!threaded_);
    TR_ASSERT(completions_ == Completions::Inline); // see set_completions()

    if (worker_count == 0U) {
        return;
    }

    // The setting is an arbitrary number from a config file. More
    // workers than this can't help any disk, and an absurd value
    // would exhaust threads or fds.
    static auto constexpr MaxWorkerCount = size_t{ 64U };
    worker_count = std::min(worker_count, MaxWorkerCount);

    if (!provider) {
        TR_ASSERT(torrents_ != nullptr);
        provider = [torrents = torrents_](tr_torrent_id_t const id) -> std::shared_ptr<StorageDescriptor const> {
            auto const* const tor = torrents->get(id);
            return tor != nullptr ? tor->storage_descriptor() : nullptr;
        };
    }

    if (!on_files_created && torrents_ != nullptr) {
        on_files_created = [torrents = torrents_](tr_torrent_id_t const id, size_t const n_files) {
            if (auto const* const tor = torrents->get(id); tor != nullptr) {
                for (auto i = size_t{}; i < n_files; ++i) {
                    tor->session->add_file_created();
                }
            }
        };
    }

    TR_ASSERT(open_files_ != nullptr);
    threaded_ = std::make_shared<Threaded>(
        *open_files_,
        std::move(provider),
        std::move(marshal),
        [this](tr_torrent_id_t const id, tr_byte_span_t const span, BlockData& setme) {
            return backend_->read(id, span, setme);
        },
        std::move(on_files_created),
        worker_count);
}

void LocalData::read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, OnRead on_read)
{
    if (threaded_) {
        threaded_->read(id, byte_span, std::move(on_read));
        return;
    }

    auto data = std::make_unique<BlockData>();
    auto const err = backend_->read(id, byte_span, *data);
    if (err != 0) {
        data = nullptr;
    }

    if (on_read) {
        finish([id, byte_span, err, data = std::move(data), on_read = std::move(on_read)]() mutable {
            std::move(on_read)(id, byte_span, make_error(err), std::move(data));
        });
    }
}

void LocalData::test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
{
    if (threaded_) {
        threaded_->test_piece(id, piece, std::move(on_test));
        return;
    }

    auto hash = tr_sha1_digest_t{};
    auto const err = backend_->test_piece(id, piece, hash);

    if (on_test) {
        finish([id, piece, err, hash, on_test = std::move(on_test)]() mutable {
            std::move(on_test)(id, piece, make_error(err), err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
        });
    }
}

void LocalData::write(
    tr_torrent_id_t const id,
    tr_byte_span_t const byte_span,
    std::unique_ptr<BlockData> data,
    OnWrite on_write) // NOLINT(performance-unnecessary-value-param)
{
    if (data == nullptr) {
        if (on_write) {
            finish([id, byte_span, on_write = std::move(on_write)]() mutable {
                std::move(on_write)(id, byte_span, make_error(TR_ERROR_EINVAL));
            });
        }
        return;
    }

    if (threaded_) {
        threaded_->write(id, byte_span, std::move(data), std::move(on_write));
        return;
    }

    auto const err = backend_->write(id, byte_span, *data);

    if (on_write) {
        finish([id, byte_span, err, on_write = std::move(on_write)]() mutable {
            std::move(on_write)(id, byte_span, make_error(err));
        });
    }
}

// Run an admin op as a barrier: wait for the ops in flight, run
// exclusively, and hold back the ops enqueued behind it (rule 3).
// `body` is the whole op. It calls the backend and delivers its own
// completion.
void LocalData::admin(tr_torrent_id_t const id, std::function<void()> body)
{
    if (threaded_) {
        threaded_->barrier(id, std::move(body));
        return;
    }

    drain();
    body();
}

void LocalData::close_torrent(tr_torrent_id_t const tor_id, OnClose on_close) // NOLINT(performance-unnecessary-value-param)
{
    admin(tor_id, [this, tor_id, on_close = std::move(on_close)]() mutable {
        backend_->close_torrent(tor_id);
        if (threaded_) {
            threaded_->forget(tor_id);
        }
        if (on_close) {
            std::move(on_close)(tor_id);
        }
    });
}

void LocalData::close_file(
    tr_torrent_id_t const tor_id,
    tr_file_index_t const file_num,
    OnClose on_close) // NOLINT(performance-unnecessary-value-param)
{
    admin(tor_id, [this, tor_id, file_num, on_close = std::move(on_close)]() mutable {
        backend_->close_file(tor_id, file_num);
        if (on_close) {
            std::move(on_close)(tor_id);
        }
    });
}

void LocalData::close_all()
{
    if (threaded_) {
        threaded_->drain_all();
    }

    drain();
    backend_->close_all();
}

void LocalData::move(
    tr_torrent_id_t const id,
    std::string_view const old_parent,
    std::string_view const parent,
    std::string_view const parent_name,
    OnMove on_move) // NOLINT(performance-unnecessary-value-param)
{
    admin(
        id,
        [this,
         id,
         old_parent = std::string{ old_parent },
         parent = std::string{ parent },
         parent_name = std::string{ parent_name },
         on_move = std::move(on_move)]() mutable {
            auto const err = backend_->move(id, old_parent, parent, parent_name);
            if (on_move) {
                std::move(on_move)(id, make_error(err));
            }
        });
}

void LocalData::remove(
    tr_torrent_id_t const id,
    tr_torrent_remove_func remove_func,
    OnRemove on_remove) // NOLINT(performance-unnecessary-value-param)
{
    admin(id, [this, id, remove_func = std::move(remove_func), on_remove = std::move(on_remove)]() mutable {
        auto const err = backend_->remove(id, std::move(remove_func));
        if (threaded_) {
            threaded_->forget(id);
        }
        if (on_remove) {
            std::move(on_remove)(id, make_error(err));
        }
    });
}

void LocalData::rename(
    tr_torrent_id_t const id,
    std::string_view const oldpath,
    std::string_view const newname,
    tr_torrent_rename_done_func callback)
{
    admin(
        id,
        [this,
         id,
         oldpath = std::string{ oldpath },
         newname = std::string{ newname },
         callback = std::move(callback)]() mutable { backend_->rename(id, oldpath, newname, std::move(callback)); });
}

void LocalData::shutdown()
{
    if (threaded_) {
        threaded_->shutdown();
        threaded_.reset();
    }

    drain();
}

uint64_t LocalData::enqueued_write_bytes() const noexcept
{
    return threaded_ ? threaded_->enqueued_write_bytes() : 0U;
}

LocalData::Stats LocalData::stats() const noexcept
{
    return threaded_ ? threaded_->stats() : Stats{};
}

void LocalData::set_workers_paused(bool const paused)
{
    if (threaded_) {
        threaded_->set_paused(paused);
    }
}

void LocalData::set_wake(std::function<void()> wake)
{
    wake_ = std::move(wake);
}

void LocalData::set_completions(Completions const completions, uint32_t const seed)
{
    // Parking wraps only the synchronous backend, so the test modes and
    // the threaded backend are mutually exclusive.
    TR_ASSERT(!threaded_ || completions == Completions::Inline);

    drain();
    completions_ = completions;
    rng_.seed(seed);
}

bool LocalData::defer_next() noexcept
{
    switch (completions_) {
    case Completions::Deferred:
        return true;

    case Completions::Shuffled:
        return (rng_() & 1U) != 0U;

    default:
        return false;
    }
}

void LocalData::park(std::unique_ptr<Parked> completion)
{
    auto const was_idle = std::empty(parked_);
    parked_.emplace_back(std::move(completion));
    if (was_idle && wake_) {
        wake_();
    }
}

void LocalData::pump()
{
    // Take the whole queue up front. A completion can park more work,
    // and that work belongs to the next pump, not this one.
    auto parked = std::vector<std::unique_ptr<Parked>>{};
    std::swap(parked, parked_);

    std::ranges::shuffle(parked, rng_);

    for (auto& completion : parked) {
        completion->deliver();
    }
}

void LocalData::drain()
{
    while (!std::empty(parked_)) {
        pump();
    }
}

} // namespace tr
