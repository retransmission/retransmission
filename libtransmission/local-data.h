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
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "libtransmission/constants.h"
#include "libtransmission/digest.h"
#include "libtransmission/error-types.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

class tr_open_files;
class tr_torrents;

namespace tr
{

struct StorageDescriptor;

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
 * 3. Admin ops are barriers on their torrent. `move`, `rename`,
 *    `remove`, `close_file`, `close_torrent`, and `close_all` wait for
 *    the ops already in flight. Ops started later wait for them.
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
 * session thread. Reads still run on the session thread.
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

    using OnMove = std::function<void(tr_torrent_id_t, tr_error const& error)>;

    using OnRemove = std::function<void(tr_torrent_id_t, tr_error const& error)>;

    using OnClose = std::function<void(tr_torrent_id_t)>;

    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual tr_error_code_t read(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData& setme) = 0;
        [[nodiscard]] virtual tr_error_code_t test_piece(
            tr_torrent_id_t tor_id,
            tr_piece_index_t piece,
            tr_sha1_digest_t& setme_hash) = 0;
        [[nodiscard]] virtual tr_error_code_t write(
            tr_torrent_id_t tor_id,
            tr_byte_span_t byte_span,
            BlockData const& data) = 0;
        [[nodiscard]] virtual tr_error_code_t move(tr_torrent_id_t id, std::string_view parent) = 0;
        [[nodiscard]] virtual tr_error_code_t remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func) = 0;
        virtual void rename(
            tr_torrent_id_t id,
            std::string_view oldpath,
            std::string_view newname,
            tr_torrent_rename_done_func callback) = 0;
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

    // Called on the session thread when a write created files on disk.
    using OnFilesCreated = std::function<void(tr_torrent_id_t, size_t n_files)>;

    // Counters for tests and diagnostics.
    struct Stats {
        // disk writes issued, after adjacent blocks were combined
        uint64_t write_runs = 0U;
        uint64_t blocks_written = 0U;
        // piece hashes computed from still-buffered block data
        uint64_t hashes_from_buffers = 0U;
        // piece hashes that read the piece back from disk
        uint64_t hashes_from_disk = 0U;
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
     * `tr_torrent` or `tr_session`. Leave `provider` and
     * `on_files_created` unset to use the torrents passed to the
     * constructor. Tests pass their own.
     *
     * Call at most once, before any ops are enqueued. A `worker_count`
     * of zero keeps the synchronous backend.
     *
     * Throws if the worker threads can't be spawned. The synchronous
     * backend stays in place when it does.
     */
    void start_workers(
        size_t worker_count,
        Marshal marshal,
        DescriptorProvider provider = {},
        OnFilesCreated on_files_created = {});

    [[nodiscard]] bool is_threaded() const noexcept
    {
        return threaded_ != nullptr;
    }

    void read(tr_torrent_id_t id, tr_byte_span_t byte_span, OnRead on_read);
    void test_piece(tr_torrent_id_t id, tr_piece_index_t piece, OnTest on_test);
    void write(tr_torrent_id_t id, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write);
    void close_torrent(tr_torrent_id_t tor_id, OnClose on_close = {});
    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num, OnClose on_close = {});
    void close_all();
    void move(tr_torrent_id_t id, std::string_view parent, OnMove on_move);
    void remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func, OnRemove on_remove = {});
    void rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback);

    // Deliver every outstanding completion and stop the workers.
    // Later ops run on the synchronous backend.
    void shutdown();

    // Bytes of block data waiting to be written, or being written now.
    // Always zero on the synchronous backend.
    [[nodiscard]] uint64_t enqueued_write_bytes() const noexcept;

    [[nodiscard]] Stats stats() const noexcept;

    // For tests. Paused workers take no new ops.
    void set_workers_paused(bool paused);

    // LocalData calls `wake` when it parks the first completion. The owner
    // answers by calling pump() from the session thread. That thread is the
    // same in every mode, so the owner binds this once.
    void set_wake(std::function<void()> wake);

    void set_completions(Completions completions, uint32_t seed = DefaultShuffleSeed);

    // Deliver the parked completions in a random order.
    void pump();

private:
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

    // Run an admin op as a barrier. See the definition.
    void admin(tr_torrent_id_t id, std::function<void()> body);

    void park(std::unique_ptr<Parked> completion);

    // Deliver every parked completion, including ones parked along the way.
    void drain();

    // The threaded backend. See start_workers().
    class Threaded;

    std::unique_ptr<Backend> backend_;

    tr_torrents const* torrents_ = nullptr;
    tr_open_files* open_files_ = nullptr;

    std::shared_ptr<Threaded> threaded_;

    std::vector<std::unique_ptr<Parked>> parked_;
    std::function<void()> wake_;

    // A fixed seed so that tests can replay a shuffled run.
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937 rng_{ DefaultShuffleSeed };

    Completions completions_ = Completions::Inline;
};

} // namespace tr
