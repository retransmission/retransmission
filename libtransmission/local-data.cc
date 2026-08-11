// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::ranges::shuffle
#include <cerrno>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "libtransmission/local-data.h"

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/inout.h"
#include "libtransmission/open-files.h"
#include "libtransmission/storage-descriptor.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/transmission.h"

namespace tr
{
namespace
{
struct HashResult {
    tr_error_code_t error = 0;
    std::optional<tr_sha1_digest_t> hash;
};

[[nodiscard]] tr_error make_error(tr_error_code_t err)
{
    auto error = tr_error{};
    if (err != 0) {
        error.set_from_errno(err);
    }

    return error;
}

[[nodiscard]] HashResult recalculate_hash(
    LocalData::Backend& backend,
    tr_torrent_id_t const id,
    tr_block_info const block_info,
    tr_piece_index_t const piece)
{
    TR_ASSERT(piece < block_info.piece_count());

    auto sha = tr_sha1{};
    auto buffer = LocalData::BlockData{};

    auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
    auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block) {
        auto const byte_span = block_info.byte_span_for_block(block);

        buffer.clear();
        if (auto const err = backend.read(id, byte_span, buffer); err != 0) {
            return { .error = err, .hash = {} };
        }

        auto span = std::span{ buffer.data(), static_cast<size_t>(byte_span.size()) };

        if (block + 1U == end_block) {
            span = span.first(end_byte - byte_span.begin);
        }
        if (block == begin_block) {
            span = span.subspan(begin_byte - byte_span.begin);
        }

        sha.add(span);
        n_bytes_checked += span.size();
    }

    TR_ASSERT(block_info.piece_size(piece) == n_bytes_checked);
    return { .error = 0, .hash = sha.finish() };
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

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        setme.resize(span_size);
        return tr_ioRead(*tor, open_files_, loc, std::span{ std::data(setme), span_size });
    }

    [[nodiscard]] tr_error_code_t test_piece(
        tr_torrent_id_t const id,
        tr_piece_index_t const piece,
        tr_sha1_digest_t& setme_hash) override
    {
        auto const* const tor = torrents_.get(id);
        if (tor == nullptr || piece >= tor->piece_count()) {
            return TR_ERROR_EINVAL;
        }

        auto const result = recalculate_hash(*this, id, tor->block_info(), piece);
        if (!result.hash) {
            return result.error != 0 ? result.error : EIO;
        }

        setme_hash = *result.hash;
        return 0;
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

        auto* const tor = torrents_.get(id);
        if (tor == nullptr) {
            return TR_ERROR_EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        return tr_ioWrite(*tor, open_files_, loc, std::span{ std::data(data), span_size });
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

        tor->rename_path_in_session_thread(oldpath, newname, std::move(callback));
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

} // namespace

/**
 * The threaded backend.
 *
 * Threading model:
 *
 * - Every public method runs on the session thread, and so does all of
 *   the gate state: `gates_`, `staged_`, and `n_running_total_` need no
 *   locks.
 * - `work_` and `done_` are the only structures shared with workers,
 *   each behind its own mutex. Workers pop work items, execute them,
 *   and post completion closures to `done_`; the session thread
 *   delivers those from pump_done().
 *
 * The admission gate: data ops on a torrent are admitted freely and run
 * concurrently. An admin op waits in the torrent's queue until every
 * admitted op's completion has been delivered, then runs exclusively on
 * the session thread; ops enqueued behind it wait in the queue until it
 * finishes. That is rule 3, and it is the whole story for move / rename
 * / remove / close safety.
 *
 * The read scheduler:
 *
 * - Hot path: a read whose fd is already pooled is tried with a
 *   nonblocking page-cache read and completes inline on a hit — no
 *   thread hop, no queueing (legal under rule 4).
 * - Cold path: misses are staged, and flushed to the workers once per
 *   session-thread pass. A flush sorts the batch by disk position and
 *   merges contiguous reads into runs of up to MaxCoalescedRunBytes,
 *   so seeding many peers turns into long sequential reads instead of
 *   16 KiB seeks. Workers consume runs in the sorted (elevator) order.
 */
class LocalData::Threaded final : public std::enable_shared_from_this<LocalData::Threaded>
{
public:
    using WriteExec = std::function<tr_error_code_t(tr_torrent_id_t, tr_byte_span_t, BlockData const&)>;

    Threaded(
        tr_open_files& open_files,
        DescriptorProvider provider,
        Marshal marshal,
        WriteExec write_exec,
        size_t const n_workers)
        : open_files_{ open_files }
        , provider_{ std::move(provider) }
        , marshal_{ std::move(marshal) }
        , write_exec_{ std::move(write_exec) }
    {
        workers_.reserve(n_workers);
        for (auto i = size_t{}; i < n_workers; ++i) {
            workers_.emplace_back([this]() { worker_main(); });
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
        if (auto& gate = gates_[id]; is_blocked(gate)) {
            gate.queue.emplace_back(ReadOp{ span, std::move(on_read) });
        } else {
            admit_read(id, ReadOp{ span, std::move(on_read) });
        }
    }

    void test_piece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
    {
        if (auto& gate = gates_[id]; is_blocked(gate)) {
            gate.queue.emplace_back(TestOp{ piece, std::move(on_test) });
        } else {
            admit_test(id, TestOp{ piece, std::move(on_test) });
        }
    }

    void write(tr_torrent_id_t const id, tr_byte_span_t const span, std::unique_ptr<BlockData> data, OnWrite on_write)
    {
        if (auto& gate = gates_[id]; is_blocked(gate)) {
            gate.queue.emplace_back(WriteOp{ span, std::move(data), std::move(on_write) });
            return;
        }

        exec_write(id, WriteOp{ span, std::move(data), std::move(on_write) });

        // the write's completion may have enqueued a barrier
        advance(id);
    }

    // Enqueue an admin op. `body` runs exclusively on the session
    // thread once the ops in flight have drained. It must deliver its
    // own completion.
    void barrier(tr_torrent_id_t const id, std::function<void()> body)
    {
        gates_[id].queue.emplace_back(BarrierOp{ std::move(body) });
        advance(id);
    }

    // Forget a torrent whose files are closed. A no-op if ops for the
    // torrent arrived in the meantime; the state just gets rebuilt.
    // Call from inside a close_torrent or remove barrier.
    void forget(tr_torrent_id_t const id)
    {
        if (auto const it = gates_.find(id);
            it != std::end(gates_) && it->second.n_running == 0U && std::empty(it->second.queue)) {
            gates_.erase(it);
        }
    }

    // Block until every op in flight, and every op those completions
    // enqueue, has been delivered. On return the gates are idle.
    void drain_all()
    {
        for (;;) {
            flush_staged();
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
        // spans being read by ops in flight, for the S6 overlap assert
        std::vector<tr_byte_span_t> running_read_spans;
#endif
    };

    // A staged cold read, resolved and waiting for the batch flush.
    struct StagedRead {
        tr_torrent_id_t tor_id;
        std::shared_ptr<StorageDescriptor const> desc;
        tr_byte_span_t span;
        OnRead on_read;
        tr_file_index_t file;
        uint64_t file_offset;
        bool single_file;
    };

    // A batch of coalesced reads: `ops` covers `length` contiguous
    // bytes of one file, read with a single pread into one buffer.
    // A run that isn't `coalesced` holds one op that may span files.
    struct Run {
        tr_torrent_id_t tor_id;
        std::shared_ptr<StorageDescriptor const> desc;
        tr_file_index_t file;
        uint64_t offset;
        size_t length;
        bool coalesced;
        std::vector<StagedRead> ops;
    };

    static auto constexpr MaxCoalescedRunBytes = size_t{ 1024U * 1024U };

    [[nodiscard]] static bool is_blocked(Gate const& gate) noexcept
    {
        return gate.barrier_running || !std::empty(gate.queue);
    }

    [[nodiscard]] bool idle() const noexcept
    {
        if (n_running_total_ != 0U || !std::empty(staged_)) {
            return false;
        }

        for (auto const& [id, gate] : gates_) {
            if (gate.barrier_running || !std::empty(gate.queue)) {
                return false;
            }
        }

        return true;
    }

    // Run queued ops that the gate now allows.
    void advance(tr_torrent_id_t const id)
    {
        for (;;) {
            auto const it = gates_.find(id);
            if (it == std::end(gates_)) {
                return;
            }

            auto& gate = it->second;
            if (gate.barrier_running || std::empty(gate.queue)) {
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

                // the body may have erased the gate via forget()
                if (auto const it2 = gates_.find(id); it2 != std::end(gates_)) {
                    it2->second.barrier_running = false;
                }

                continue;
            }

            auto op = std::move(gate.queue.front());
            gate.queue.pop_front();

            if (std::holds_alternative<ReadOp>(op)) {
                admit_read(id, std::get<ReadOp>(std::move(op)));
            } else if (std::holds_alternative<TestOp>(op)) {
                admit_test(id, std::get<TestOp>(std::move(op)));
            } else {
                exec_write(id, std::get<WriteOp>(std::move(op)));
            }
        }
    }

    void release(tr_torrent_id_t const id)
    {
        auto const it = gates_.find(id);
        TR_ASSERT(it != std::end(gates_));
        TR_ASSERT(it->second.n_running > 0U);
        TR_ASSERT(n_running_total_ > 0U);

        --it->second.n_running;
        --n_running_total_;
        advance(id);
    }

    void register_running_span([[maybe_unused]] tr_torrent_id_t const id, [[maybe_unused]] tr_byte_span_t const span)
    {
#ifdef TR_ENABLE_ASSERTS
        gates_[id].running_read_spans.push_back(span);
#endif
    }

    void unregister_running_span([[maybe_unused]] tr_torrent_id_t const id, [[maybe_unused]] tr_byte_span_t const span)
    {
#ifdef TR_ENABLE_ASSERTS
        auto& spans = gates_[id].running_read_spans;
        auto const it = std::ranges::find_if(spans, [&span](tr_byte_span_t const& running) {
            return running.begin == span.begin && running.end == span.end;
        });
        TR_ASSERT(it != std::end(spans));
        spans.erase(it);
#endif
    }

    void admit_read(tr_torrent_id_t const id, ReadOp op)
    {
        auto const span = op.span;
        auto const desc = !span.is_valid() || span.size() > tr_block_info::BlockSize ? nullptr : provider_(id);
        if (!desc) {
            if (op.on_read) {
                std::move(op.on_read)(id, span, make_error(TR_ERROR_EINVAL), nullptr);
            }
            return;
        }

        auto const [file, file_offset] = desc->fpm.file_offset(span.begin);
        auto const single_file = span.end <= desc->fpm.byte_span_for_file(file).end;

        if (single_file && try_read_hot(id, span, file, file_offset, op)) {
            return; // completed inline from the page cache
        }

        ++gates_[id].n_running;
        ++n_running_total_;
        register_running_span(id, span);
        staged_.emplace_back(StagedRead{ id, desc, span, std::move(op.on_read), file, file_offset, single_file });
        schedule_flush();
    }

    // Serve a read from the page cache, inline, if the file is already
    // open and the data is resident. Rule 4 allows the inline delivery.
    [[nodiscard]] bool try_read_hot(
        tr_torrent_id_t const id,
        tr_byte_span_t const span,
        tr_file_index_t const file,
        uint64_t const file_offset,
        ReadOp& op)
    {
        auto const pin = open_files_.get(id, file, false);
        if (!pin) {
            return false;
        }

        auto const len = static_cast<size_t>(span.size());
        auto data = std::make_unique<BlockData>();
        data->resize(len);

        auto const n_read = tr_sys_file_read_at_nowait(*pin, std::data(*data), len, file_offset);
        if (!n_read || *n_read < len) {
            return false;
        }

        if (op.on_read) {
            std::move(op.on_read)(id, span, tr_error{}, std::move(data));
        }

        return true;
    }

    void admit_test(tr_torrent_id_t const id, TestOp op)
    {
        auto const desc = provider_(id);
        if (!desc || op.piece >= desc->block_info.piece_count()) {
            if (op.on_test) {
                std::move(op.on_test)(id, op.piece, make_error(TR_ERROR_EINVAL), {});
            }
            return;
        }

        auto const piece_span = desc->block_info.byte_span_for_piece(op.piece);
        ++gates_[id].n_running;
        ++n_running_total_;
        register_running_span(id, piece_span);

        push_work([this, id, desc, piece = op.piece, piece_span, on_test = std::move(op.on_test)]() mutable {
            auto hash = tr_sha1_digest_t{};
            auto const err = hash_piece(id, *desc, piece, hash);
            post_completion([this, id, piece, piece_span, err, hash, on_test = std::move(on_test)]() mutable {
                if (on_test) {
                    std::move(
                        on_test)(id, piece, make_error(err), err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
                }
                unregister_running_span(id, piece_span);
                release(id);
            });
        });
    }

    void exec_write(tr_torrent_id_t const id, WriteOp op)
    {
        // Writes stay synchronous on the session thread for now: the
        // blocking write is what throttles intake on a slow disk, and
        // making writes async without a memory budget would let a fast
        // swarm outrun the disk unboundedly. They still count as
        // running while their completion runs, so a completion that
        // enqueues a barrier can't have it jump the queue.
#ifdef TR_ENABLE_ASSERTS
        // S6: a write must never overlap an op that's reading. The
        // protocol already guarantees it - we write only blocks we
        // lack, and read or hash only pieces we have - so a failure
        // here is a caller bug, not a backend race.
        for (auto const& read_span : gates_[id].running_read_spans) {
            TR_ASSERT(op.span.end <= read_span.begin || read_span.end <= op.span.begin);
        }
#endif

        auto err = tr_error_code_t{ TR_ERROR_EINVAL };
        if (op.data != nullptr) {
            err = write_exec_(id, op.span, *op.data);
        }

        auto& gate = gates_[id];
        ++gate.n_running;
        ++n_running_total_;

        if (op.on_write) {
            std::move(op.on_write)(id, op.span, make_error(err));
        }

        --gates_[id].n_running;
        --n_running_total_;
    }

    void schedule_flush()
    {
        if (flush_scheduled_) {
            return;
        }

        flush_scheduled_ = true;
        marshal_([weak = weak_from_this()]() {
            if (auto const self = weak.lock(); self) {
                self->flush_staged();
            }
        });
    }

    // Sort the staged reads by disk position, merge contiguous spans
    // into runs, and hand the runs to the workers in sorted order.
    void flush_staged()
    {
        flush_scheduled_ = false;

        if (std::empty(staged_)) {
            return;
        }

        auto batch = std::move(staged_);
        staged_ = {};

        std::ranges::stable_sort(batch, [](StagedRead const& a, StagedRead const& b) {
            if (a.tor_id != b.tor_id) {
                return a.tor_id < b.tor_id;
            }
            if (a.file != b.file) {
                return a.file < b.file;
            }
            return a.file_offset < b.file_offset;
        });

        auto runs = std::vector<Run>{};
        for (auto& op : batch) {
            auto const len = static_cast<size_t>(op.span.size());

            if (auto* const back = std::empty(runs) ? nullptr : &runs.back(); back != nullptr && back->coalesced &&
                op.single_file && back->tor_id == op.tor_id && back->desc == op.desc && back->file == op.file &&
                back->offset + back->length == op.file_offset && back->length + len <= MaxCoalescedRunBytes) {
                back->length += len;
                back->ops.emplace_back(std::move(op));
                continue;
            }

            auto& run = runs.emplace_back(Run{ op.tor_id, op.desc, op.file, op.file_offset, len, op.single_file, {} });
            run.ops.emplace_back(std::move(op));
        }

        auto const lock = std::scoped_lock{ work_mutex_ };
        for (auto& run : runs) {
            work_.emplace_back([this, run = std::move(run)]() mutable { execute_run(std::move(run)); });
        }
        work_cv_.notify_all();
    }

    void push_work(std::function<void()> item)
    {
        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            work_.emplace_back(std::move(item));
        }
        work_cv_.notify_one();
    }

    // The completion closures own their read buffers, so they are
    // move-only and can't live in a std::function. Park them instead.
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
            auto item = std::function<void()>{};

            {
                auto lock = std::unique_lock{ work_mutex_ };
                work_cv_.wait(lock, [this]() { return stopping_ || !std::empty(work_); });
                if (std::empty(work_)) {
                    return; // stopping
                }

                item = std::move(work_.front());
                work_.pop_front();
            }

            item();
        }
    }

    void stop_workers()
    {
        {
            auto const lock = std::scoped_lock{ work_mutex_ };
            stopping_ = true;
        }
        work_cv_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();
    }

    [[nodiscard]] tr_open_files::Pinned open_for_read(
        tr_torrent_id_t const id,
        StorageDescriptor const& desc,
        tr_file_index_t const file)
    {
        if (auto pin = open_files_.get(id, file, false); pin) {
            return pin;
        }

        auto const found = desc.find(file);
        if (!found) {
            return {};
        }

        auto const filename = found->filename<tr_pathbuf>();
        return open_files_.get(id, file, false, filename, tr_file_preallocation::None, desc.files.file_size(file));
    }

    [[nodiscard]] static tr_error_code_t read_file_range(tr_sys_file_t const fd, uint64_t file_offset, std::span<uint8_t> buf)
    {
        while (!std::empty(buf)) {
            auto n_read = uint64_t{};
            auto error = tr_error{};
            if (!tr_sys_file_read_at(fd, std::data(buf), std::size(buf), file_offset, &n_read, &error)) {
                return error ? error.code() : EIO;
            }

            buf = buf.subspan(static_cast<size_t>(n_read));
            file_offset += n_read;
        }

        return 0;
    }

    // Read an arbitrary byte span, crossing file boundaries as needed.
    [[nodiscard]] tr_error_code_t read_span(
        tr_torrent_id_t const id,
        StorageDescriptor const& desc,
        tr_byte_span_t const span,
        std::span<uint8_t> buf)
    {
        auto [file, file_offset] = desc.fpm.file_offset(span.begin);

        while (!std::empty(buf)) {
            auto const file_size = desc.files.file_size(file);
            auto const n_this_pass = static_cast<size_t>(std::min(uint64_t{ std::size(buf) }, file_size - file_offset));

            if (n_this_pass > 0U) {
                auto const pin = open_for_read(id, desc, file);
                if (!pin) {
                    return ENOENT;
                }

                if (auto const err = read_file_range(*pin, file_offset, buf.first(n_this_pass)); err != 0) {
                    return err;
                }
            }

            buf = buf.subspan(n_this_pass);
            ++file;
            file_offset = 0U;
        }

        return 0;
    }

    void execute_run(Run run)
    {
        if (!run.coalesced) {
            TR_ASSERT(std::size(run.ops) == 1U);
            auto& op = run.ops.front();
            auto const len = static_cast<size_t>(op.span.size());
            auto data = std::make_unique<BlockData>();
            data->resize(len);
            auto const err = read_span(run.tor_id, *run.desc, op.span, { std::data(*data), len });
            complete_read(run.tor_id, std::move(op), err, err == 0 ? std::move(data) : nullptr);
            return;
        }

        auto err = tr_error_code_t{};
        if (auto const pin = open_for_read(run.tor_id, *run.desc, run.file); !pin) {
            err = ENOENT;
        } else {
            thread_local auto buf = std::vector<uint8_t>{};
            buf.resize(run.length);
            err = read_file_range(*pin, run.offset, buf);

            if (err == 0) {
                for (auto& op : run.ops) {
                    auto data = std::make_unique<BlockData>();
                    data->assign(
                        std::span{ buf }.subspan(
                            static_cast<size_t>(op.file_offset - run.offset),
                            static_cast<size_t>(op.span.size())));
                    complete_read(run.tor_id, std::move(op), 0, std::move(data));
                }
                return;
            }
        }

        for (auto& op : run.ops) {
            complete_read(run.tor_id, std::move(op), err, nullptr);
        }
    }

    void complete_read(tr_torrent_id_t const id, StagedRead op, tr_error_code_t const err, std::unique_ptr<BlockData> data)
    {
        post_completion([this, id, span = op.span, err, data = std::move(data), on_read = std::move(op.on_read)]() mutable {
            if (on_read) {
                std::move(on_read)(id, span, make_error(err), std::move(data));
            }
            unregister_running_span(id, span);
            release(id);
        });
    }

    [[nodiscard]] tr_error_code_t hash_piece(
        tr_torrent_id_t const id,
        StorageDescriptor const& desc,
        tr_piece_index_t const piece,
        tr_sha1_digest_t& setme)
    {
        auto const& block_info = desc.block_info;
        auto sha = tr_sha1{};
        auto buffer = BlockData{};

        auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
        auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
        for (auto block = begin_block; block < end_block; ++block) {
            auto const byte_span = block_info.byte_span_for_block(block);
            auto const len = static_cast<size_t>(byte_span.size());
            buffer.resize(len);

            if (auto const err = read_span(id, desc, byte_span, { std::data(buffer), len }); err != 0) {
                return err;
            }

            auto span = std::span<uint8_t const>{ std::data(buffer), len };
            if (block + 1U == end_block) {
                span = span.first(static_cast<size_t>(end_byte - byte_span.begin));
            }
            if (block == begin_block) {
                span = span.subspan(static_cast<size_t>(begin_byte - byte_span.begin));
            }

            sha.add(span);
        }

        setme = sha.finish();
        return 0;
    }

    // --- session-thread state

    tr_open_files& open_files_;
    DescriptorProvider provider_;
    Marshal marshal_;
    WriteExec write_exec_;

    std::map<tr_torrent_id_t, Gate> gates_;
    size_t n_running_total_ = 0U;

    std::vector<StagedRead> staged_;
    bool flush_scheduled_ = false;

    // --- state shared with the workers

    std::mutex work_mutex_;
    std::condition_variable work_cv_;
    std::deque<std::function<void()>> work_;
    bool stopping_ = false;

    std::mutex done_mutex_;
    std::condition_variable done_cv_;
    std::deque<std::unique_ptr<Parked>> done_;
    bool pump_scheduled_ = false;

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

void LocalData::start_workers(size_t const worker_count, Marshal marshal, DescriptorProvider provider)
{
    TR_ASSERT(!threaded_);

    if (worker_count == 0U) {
        return;
    }

    if (!provider) {
        TR_ASSERT(torrents_ != nullptr);
        provider = [torrents = torrents_](tr_torrent_id_t const id) -> std::shared_ptr<StorageDescriptor const> {
            auto const* const tor = torrents->get(id);
            return tor != nullptr ? tor->storage_descriptor() : nullptr;
        };
    }

    TR_ASSERT(open_files_ != nullptr);
    threaded_ = std::make_shared<Threaded>(
        *open_files_,
        std::move(provider),
        std::move(marshal),
        [this](tr_torrent_id_t const id, tr_byte_span_t const span, BlockData const& data) {
            return backend_->write(id, span, data);
        },
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
    if (threaded_) {
        threaded_->write(id, byte_span, std::move(data), std::move(on_write));
        return;
    }

    auto err = tr_error_code_t{ TR_ERROR_EINVAL };
    if (data != nullptr) {
        err = backend_->write(id, byte_span, *data);
    }

    if (on_write) {
        finish([id, byte_span, err, on_write = std::move(on_write)]() mutable {
            std::move(on_write)(id, byte_span, make_error(err));
        });
    }
}

void LocalData::close_torrent(tr_torrent_id_t const tor_id)
{
    if (threaded_) {
        threaded_->barrier(tor_id, [this, tor_id]() {
            backend_->close_torrent(tor_id);
            threaded_->forget(tor_id);
        });
        return;
    }

    drain();
    backend_->close_torrent(tor_id);
}

void LocalData::close_file(
    tr_torrent_id_t const tor_id,
    tr_file_index_t const file_num,
    OnCloseFile on_close) // NOLINT(performance-unnecessary-value-param)
{
    if (threaded_) {
        threaded_->barrier(tor_id, [this, tor_id, file_num, on_close = std::move(on_close)]() mutable {
            backend_->close_file(tor_id, file_num);
            if (on_close) {
                std::move(on_close)(tor_id);
            }
        });
        return;
    }

    drain();
    backend_->close_file(tor_id, file_num);

    if (on_close) {
        std::move(on_close)(tor_id);
    }
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
    if (threaded_) {
        threaded_->barrier(
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
        return;
    }

    drain();
    auto const err = backend_->move(id, old_parent, parent, parent_name);

    if (on_move) {
        std::move(on_move)(id, make_error(err));
    }
}

void LocalData::remove(
    tr_torrent_id_t const id,
    tr_torrent_remove_func remove_func,
    OnRemove on_remove) // NOLINT(performance-unnecessary-value-param)
{
    if (threaded_) {
        threaded_->barrier(id, [this, id, remove_func = std::move(remove_func), on_remove = std::move(on_remove)]() mutable {
            auto const err = backend_->remove(id, std::move(remove_func));
            threaded_->forget(id);
            if (on_remove) {
                std::move(on_remove)(id, make_error(err));
            }
        });
        return;
    }

    drain();
    auto const err = backend_->remove(id, std::move(remove_func));

    if (on_remove) {
        std::move(on_remove)(id, make_error(err));
    }
}

void LocalData::rename(
    tr_torrent_id_t const id,
    std::string_view const oldpath,
    std::string_view const newname,
    tr_torrent_rename_done_func callback)
{
    if (threaded_) {
        threaded_->barrier(
            id,
            [this,
             id,
             oldpath = std::string{ oldpath },
             newname = std::string{ newname },
             callback = std::move(callback)]() mutable { backend_->rename(id, oldpath, newname, std::move(callback)); });
        return;
    }

    drain();
    backend_->rename(id, oldpath, newname, std::move(callback));
}

void LocalData::shutdown()
{
    if (threaded_) {
        threaded_->shutdown();
        threaded_.reset();
    }

    drain();
}

void LocalData::set_completions(Completions const completions, std::function<void()> wake)
{
    drain();
    completions_ = completions;
    wake_ = std::move(wake);
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

    static thread_local auto urbg = tr_urbg<size_t>{};
    std::ranges::shuffle(parked, urbg);

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

uint64_t LocalData::enqueued_write_bytes() noexcept
{
    return 0U;
}

} // namespace tr
