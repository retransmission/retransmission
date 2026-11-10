// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <array>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "libtransmission/constants.h"
#include "libtransmission/digest.h"
#include "libtransmission/error.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

class tr_open_files;
class tr_torrents;

namespace tr
{

class LocalData;
struct StorageDescriptor;

namespace detail
{

// For tests. While the copy lane is held, no move that has to copy
// files from another filesystem starts placing them.
void hold_copy_lane(LocalData& local_data, bool held);

// For tests. Clients call LocalData::start_workers().
//
// Selects the threaded backend without starting its threads. The work
// that `worker_count` workers and the two admin threads would do waits
// for the session thread, which does it a step at a time between its
// other work: never inside the call that enqueued an op, and never
// inside a callback. A step is one thread's next move, and a random
// number generator picks the thread, so a seed replays its schedule.
// The gate, the queues, and the delivery of callbacks are the ones the
// threads use.
//
// `marshal` and `provider` are a LocalData::Marshal and a
// LocalData::DescriptorProvider.
void start_threadless(
    LocalData& local_data,
    size_t worker_count,
    uint32_t seed,
    tr_open_files& open_files,
    std::function<void(std::function<void()>)> marshal,
    std::function<std::shared_ptr<StorageDescriptor const>(tr_torrent_id_t)> provider);

enum class DiskThread : uint8_t { Any, Worker, LongLane, ShortLane };

// Takes one step now, on the calling thread, paused or not. A worker
// takes its next hash, run of reads, or run of writes, or does the one
// it took earlier. A lane does its next job. False if no thread of
// `which` has anything to do.
bool step(LocalData& local_data, DiskThread which = DiskThread::Any);

// Does now what the backend left for the session thread to do later:
// delivers the callbacks of the ops that have finished, and hands the
// reads that waited for that to the workers.
void deliver(LocalData& local_data);

} // namespace detail

/**
 * All torrent local-data IO goes through here.
 *
 * # Ordering contract
 *
 * Every backend follows these rules. No caller may assume more.
 *
 * The rules are weaker than FIFO on purpose. They leave a backend free
 * to merge ops, to reorder them to suit the disk, and to answer from the
 * page cache without a thread hop.
 *
 * 1. Ops on different torrents are unordered.
 *
 * 2. Data ops on the same torrent are unordered too. `read`,
 *    `test_piece`, and `write` may run at the same time and may finish
 *    in any order. If op B needs to see op A's result, wait for A's
 *    callback before starting B.
 *
 * 3. Admin ops are barriers. `move`, `rename`, `remove`,
 *    `close_torrent`, and `close_all` wait for the ops already in
 *    flight on their torrent, and ops started later wait for them.
 *    `close_file` does the same for the ops that touch its file.
 *
 * 4. Every callback fires exactly once. It may fire before the enqueue
 *    call returns, or long afterwards from the session thread. Callers
 *    have to work either way.
 *
 * 5. Anything may have changed before a callback runs. The torrent may
 *    have stopped or been removed, the peer may be gone, the files may
 *    have moved. Look up what you need by id, and drop the work if it's
 *    gone.
 *
 * Reads and hashes still see finished writes, even though rule 2
 * promises no such thing. That works because we don't start the second
 * op until the write's callback has run. A piece is hashed from its last
 * write completion, and we only read pieces we already have.
 *
 * # Backends
 *
 * The synchronous backend runs every op on the session thread before
 * the enqueue call returns. It is the default.
 *
 * start_workers() switches to the threaded backend. Writes and piece
 * hashes then run on worker threads and complete later, from the
 * session thread. A read completes inline when the page cache can
 * answer it without waiting for the disk, and otherwise runs on a
 * worker like the rest. `move`, `rename`, and `remove` do their disk
 * work on background threads of their own, so that a long copy doesn't
 * hold up other torrents' data ops. Their callbacks run on the session
 * thread, and the barrier stays up until the last of them has returned.
 * `close_torrent` and `close_file` complete as soon as their barrier is
 * reached: the files are out of the pool by then, and are closed on one
 * of those background threads, ahead of any admin op enqueued later.
 */
class LocalData
{
public:
    /**
     * A fixed-capacity buffer holding up to one block of data.
     *
     * Growing the buffer leaves the new bytes uninitialized. Callers
     * always overwrite them, so zeroing them first would waste a pass
     * over 16 KiB on every block.
     */
    class BlockData
    {
    public:
        using value_type = uint8_t;

        // Don't replace this with `= default`. That would make the
        // constructor trivial, and `BlockData{}` would zero all 16 KiB.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
        BlockData() noexcept
        {
        }

        explicit BlockData(std::span<value_type const> const data) // NOLINT(cppcoreguidelines-pro-type-member-init)
        {
            assign(data);
        }

        void assign(std::span<value_type const> const data) noexcept
        {
            TR_ASSERT(std::size(data) <= std::size(buf_));
            size_ = std::min(std::size(data), std::size(buf_));
            std::copy_n(std::begin(data), size_, std::begin(buf_));
        }

        void assign(std::initializer_list<value_type> const data) noexcept
        {
            assign(std::span{ std::data(data), std::size(data) });
        }

        constexpr void resize(size_t const size) noexcept
        {
            TR_ASSERT(size <= std::size(buf_));
            size_ = std::min(size, std::size(buf_));
        }

        constexpr void clear() noexcept
        {
            size_ = 0U;
        }

        [[nodiscard]] constexpr auto* data() noexcept
        {
            return std::data(buf_);
        }

        [[nodiscard]] constexpr auto const* data() const noexcept
        {
            return std::data(buf_);
        }

        [[nodiscard]] constexpr auto size() const noexcept
        {
            return size_;
        }

        [[nodiscard]] constexpr auto begin() noexcept
        {
            return std::begin(buf_);
        }

        [[nodiscard]] constexpr auto begin() const noexcept
        {
            return std::begin(buf_);
        }

        [[nodiscard]] constexpr auto end() noexcept
        {
            return std::begin(buf_) + size_;
        }

        [[nodiscard]] constexpr auto end() const noexcept
        {
            return std::begin(buf_) + size_;
        }

    private:
        std::array<value_type, TrBlockSize> buf_;
        size_t size_ = 0U;
    };

    using OnRead = std::function<
        void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error, std::unique_ptr<BlockData> data)>;

    using OnTest = std::function<
        void(tr_torrent_id_t, tr_piece_index_t piece, tr_error const& error, std::optional<tr_sha1_digest_t> hash)>;

    using OnWrite = std::function<void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error)>;

    struct MoveTarget {
        // The dir the files go under. Empty skips the move.
        std::string parent;

        // Also clear the copies that a killed move left at the old location.
        bool clear_killed_move = false;
    };

    // Runs when the move starts, after the ops queued before it.
    using MoveParent = std::function<MoveTarget()>;

    // Runs once every file is under `parent`, before the old location is cleared.
    // A location saved here can't name files that a kill has since removed.
    using OnPlaced = std::function<void(tr_torrent_id_t, std::string_view parent)>;

    // `parent` is the dir the files went under, or empty if the move was skipped.
    using OnMove = std::function<void(tr_torrent_id_t, std::string_view parent, tr_error const& error)>;

    using OnRemove = std::function<void(tr_torrent_id_t, tr_error const& error)>;

    using OnClose = std::function<void(tr_torrent_id_t)>;

    // Required. Runs when the rename starts, after the ops queued before it.
    // An error fails the rename without touching the disk.
    using RenameCheck = std::function<tr_error()>;

    // Required.
    using OnRename = std::function<void(tr_torrent_id_t, tr_error const& error)>;

    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual tr_error read(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData& setme) = 0;
        [[nodiscard]] virtual tr_error test_piece(
            tr_torrent_id_t tor_id,
            tr_piece_index_t piece,
            tr_sha1_digest_t& setme_hash) = 0;
        [[nodiscard]] virtual tr_error write(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData const& data) = 0;
        [[nodiscard]] virtual tr_error move(
            tr_torrent_id_t id,
            MoveTarget const& target,
            std::function<void()> const& on_placed) = 0;
        [[nodiscard]] virtual tr_error remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func) = 0;
        [[nodiscard]] virtual tr_error rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname) = 0;
        virtual void close_all() = 0;
        virtual void close_torrent(tr_torrent_id_t tor_id) = 0;
        virtual void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num) = 0;
    };

    /**
     * How the synchronous backend delivers completions. See rule 4.
     *
     * `Inline` runs every callback before the enqueue call returns.
     *
     * `Deferred` and `Shuffled` are for testing. `Deferred` parks every
     * callback until `pump()` runs. `Shuffled` picks per op, so one run
     * parks some callbacks and fires the rest inline. `pump()` delivers
     * what it parked in an order unrelated to the order the ops arrived
     * in.
     *
     * The rules above allow all of this, so a caller that breaks under
     * these modes would also break under the threaded backend.
     */
    enum class Completions : uint8_t { Inline, Deferred, Shuffled };

    // The same seed replays the same shuffled run.
    static auto constexpr DefaultShuffleSeed = uint32_t{ 20260812U };

    // Runs a function on the session thread. Must be callable from any
    // thread. Disk workers call it to deliver completions.
    using Marshal = std::function<void(std::function<void()>)>;

    // Returns the torrent's current storage descriptor, or nullptr if
    // the torrent is gone. Called on the session thread when an op is
    // admitted.
    using DescriptorProvider = std::function<std::shared_ptr<StorageDescriptor const>(tr_torrent_id_t)>;

    // Counters for tests and diagnostics.
    struct Stats {
        // disk writes that succeeded, after adjacent blocks were combined
        uint64_t write_runs = 0U;
        // blocks those writes put on disk
        uint64_t blocks_written = 0U;
        // piece hashes computed from still-buffered block data
        uint64_t hashes_from_buffers = 0U;
        // piece hashes that read the piece back from disk
        uint64_t hashes_from_disk = 0U;
        // reads answered from the page cache on the session thread
        uint64_t cached_reads = 0U;
        // disk reads that succeeded, after adjacent requests were combined
        uint64_t read_runs = 0U;
        // read requests those disk reads answered
        uint64_t blocks_read = 0U;
    };

    explicit LocalData(tr_torrents const& torrents, tr_open_files& open_files);
    explicit LocalData(std::unique_ptr<Backend> backend);

    LocalData(LocalData const&) = delete;
    LocalData(LocalData&&) = delete;
    LocalData& operator=(LocalData const&) = delete;
    LocalData& operator=(LocalData&&) = delete;

    ~LocalData();

    /**
     * Switch to the threaded backend.
     *
     * Workers resolve torrent data through `provider` and never touch
     * `tr_torrent` or `tr_session`.
     *
     * Call at most once, before any ops are enqueued. A `worker_count`
     * of zero keeps the synchronous backend.
     *
     * Throws if the worker threads can't be spawned. The synchronous
     * backend stays in place when it does.
     */
    void start_workers(size_t worker_count, tr_open_files& open_files, Marshal marshal, DescriptorProvider provider);

    void read(tr_torrent_id_t id, tr_byte_span_t byte_span, OnRead on_read);
    void test_piece(tr_torrent_id_t id, tr_piece_index_t piece, OnTest on_test);
    void write(tr_torrent_id_t id, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write);
    void close_torrent(tr_torrent_id_t tor_id, OnClose on_close = {});
    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num, OnClose on_close = {});
    void close_all();
    void move(tr_torrent_id_t id, MoveParent parent, OnMove on_move, OnPlaced on_placed = {});
    void remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func, OnRemove on_remove = {});
    void rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname, RenameCheck check, OnRename on_rename);

    // Deliver every outstanding completion and stop the workers.
    // Later ops run on the synchronous backend.
    void shutdown();

    // True while an admin op on the whole torrent is queued or running.
    // Blocks requested now would wait behind it, holding write budget that
    // other torrents need, so callers shouldn't request any.
    // Call from the session thread.
    [[nodiscard]] bool admin_op_pending(tr_torrent_id_t id) const;

    // Bytes of block data waiting to be written, or being written now.
    // Always zero on the synchronous backend.
    [[nodiscard]] uint64_t enqueued_write_bytes() const noexcept;

    [[nodiscard]] Stats stats() const noexcept;

    // The most bytes that may wait for the disk, counting blocks
    // requested from peers as well as blocks received. Also sizes the
    // retained-block cache.
    void set_write_budget(uint64_t bytes);

    // The retained-block cache's size: half the write budget, up to 32 MiB.
    [[nodiscard]] size_t retained_bytes() const noexcept;

    // Bytes still spare under the write budget once `requested` bytes
    // join what waits for the disk. No value means no bound: the
    // synchronous backend writes a block before it reads the next one
    // off the wire, so nothing buffers there.
    [[nodiscard]] std::optional<uint64_t> spare_write_bytes(uint64_t requested) const noexcept;

    // How many bytes of reads one requester should keep in flight: two
    // runs' worth, so that one run is read while the last is consumed.
    // Keeping more in flight would let one requester crowd out the rest
    // at the disk.
    [[nodiscard]] static constexpr size_t read_ahead_bytes() noexcept
    {
        return 2U * MaxRunBytes;
    }

    // For tests. Paused workers take no new ops. With no threads, the
    // session thread takes no steps of its own.
    void set_workers_paused(bool paused);

    // LocalData calls `wake` when it parks the first completion. The owner
    // answers by calling pump() from the session thread. That thread is the
    // same in every mode, so the owner binds this once.
    void set_wake(std::function<void()> wake);

    void set_completions(Completions completions, uint32_t seed = DefaultShuffleSeed);

    // Deliver the parked completions in a random order.
    void pump();

private:
    friend void detail::hold_copy_lane(LocalData& local_data, bool held);
    friend void detail::start_threadless(
        LocalData& local_data,
        size_t worker_count,
        uint32_t seed,
        tr_open_files& open_files,
        std::function<void(std::function<void()>)> marshal,
        std::function<std::shared_ptr<StorageDescriptor const>(tr_torrent_id_t)> provider);
    friend bool detail::step(LocalData& local_data, detail::DiskThread which);
    friend void detail::deliver(LocalData& local_data);

    // A completion waiting to be delivered.
    // A read completion owns the buffer it read into and can't be copied,
    // so std::function can't hold one.
    class Parked
    {
    public:
        virtual ~Parked() = default;
        virtual void deliver() = 0;
    };

    template<typename Fn>
    class ParkedFn final : public Parked
    {
    public:
        explicit ParkedFn(Fn fn)
            : fn_{ std::move(fn) }
        {
        }

        void deliver() override
        {
            fn_();
        }

    private:
        Fn fn_;
    };

    // Deliver the completion now, or park it for pump().
    // See set_completions().
    template<typename Fn>
    void finish(Fn&& fn)
    {
        if (defer_next()) {
            park(std::make_unique<ParkedFn<std::decay_t<Fn>>>(std::forward<Fn>(fn)));
            return;
        }

        fn();
    }

    // True if this completion should wait for pump() instead of firing now.
    [[nodiscard]] bool defer_next() noexcept;

    // Run an admin op as a barrier on the torrent, or with `file`, on
    // that file alone. See the definition.
    void admin(tr_torrent_id_t id, std::function<void()> body, std::optional<tr_file_index_t> file = {});

    void park(std::unique_ptr<Parked> completion);

    // Deliver every parked completion, including ones parked along the way.
    void drain();

    // The threaded backend. See start_workers().
    class Threaded;

    // The threaded backend combines pending ops that continue one another
    // into runs of up to this many bytes, and does each run with one call.
    static auto constexpr MaxRunBytes = size_t{ 1024U * 1024U };

    static auto constexpr MaxRetainedBytes = size_t{ 32U * 1024U * 1024U };

    std::unique_ptr<Backend> backend_;

    std::shared_ptr<Threaded> threaded_;
    std::optional<uint64_t> write_budget_;

    std::vector<std::unique_ptr<Parked>> parked_;
    std::function<void()> wake_;

    // A fixed seed so that tests can replay a shuffled run.
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937 rng_{ DefaultShuffleSeed };

    Completions completions_ = Completions::Inline;
};

} // namespace tr
