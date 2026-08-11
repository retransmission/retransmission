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

/**
 * The gateway for all torrent local-data IO.
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
 * 5. The world can change before a callback runs. The torrent may have
 *    stopped or been removed, the peer may be gone, the files may have
 *    moved. Look up what you need by id, and drop the work if it's gone.
 *
 * Reads and hashes still see finished writes, even though rule 2
 * promises no such thing. That works because we don't start the second
 * op until the write's callback has run. A piece is hashed from its last
 * write completion, and we only read pieces we already have.
 */
class LocalData
{
public:
    /**
     * A fixed-capacity buffer holding up to one block of data.
     *
     * Growing the buffer leaves the new bytes uninitialized. Callers
     * always overwrite them right away, so zeroing them first would
     * waste a pass over 16 KiB on every block.
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
        [[nodiscard]] virtual tr_error_code_t move(
            tr_torrent_id_t id,
            std::string_view old_parent,
            std::string_view parent,
            std::string_view parent_name) = 0;
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
     * How completions are delivered. See rule 4.
     *
     * `Deferred` and `Shuffled` are for testing. `Deferred` parks every
     * callback until `pump()` is called. `Shuffled` decides per op, so a
     * run mixes callbacks that fire before the enqueue call returns with
     * callbacks that arrive much later. `pump()` delivers what it parked
     * in an order unrelated to the order the ops were enqueued.
     *
     * The rules above allow all of this, so a caller that breaks under
     * these modes would also break under a threaded backend.
     */
    enum class Completions : uint8_t { Inline, Deferred, Shuffled };

    // Shuffled mode replays exactly when given the same seed.
    static auto constexpr DefaultShuffleSeed = uint32_t{ 20260812U };

    explicit LocalData(tr_torrents const& torrents, tr_open_files& open_files, size_t worker_count = {});
    explicit LocalData(std::unique_ptr<Backend> backend, size_t worker_count = {});

    LocalData(LocalData const&) = delete;
    LocalData(LocalData&&) = delete;
    LocalData& operator=(LocalData const&) = delete;
    LocalData& operator=(LocalData&&) = delete;

    ~LocalData();

    void read(tr_torrent_id_t id, tr_byte_span_t byte_span, OnRead on_read);
    void test_piece(tr_torrent_id_t id, tr_piece_index_t piece, OnTest on_test);
    void write(tr_torrent_id_t id, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write);
    void close_torrent(tr_torrent_id_t tor_id);
    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num);
    void close_all();
    void move(
        tr_torrent_id_t id,
        std::string_view old_parent,
        std::string_view parent,
        std::string_view parent_name,
        OnMove on_move);
    void remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func);
    void rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback);
    void shutdown();
    [[nodiscard]] static uint64_t enqueued_write_bytes() noexcept;

    // `wake` is called when the first completion is parked. The owner should
    // answer it by calling pump() from the session thread. Which thread that
    // is doesn't change with the mode, so the owner binds this once.
    void set_wake(std::function<void()> wake);

    void set_completions(Completions completions, uint32_t seed = DefaultShuffleSeed);

    // Deliver the parked completions in a random order.
    void pump();

private:
    // A completion waiting to be delivered.
    // std::function can't hold one of these. A read completion owns the
    // buffer it read into, so it can't be copied.
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

    void park(std::unique_ptr<Parked> completion);

    // Deliver every parked completion, including ones parked along the way.
    void drain();

    std::unique_ptr<Backend> backend_;

    std::vector<std::unique_ptr<Parked>> parked_;
    std::function<void()> wake_;

    // Seeded, so a shuffled run can be replayed. Predictability is the
    // point here; tr_urbg() draws from the CSPRNG and cannot be seeded.
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp)
    std::mt19937 rng_{ DefaultShuffleSeed };

    Completions completions_ = Completions::Inline;
};

} // namespace tr
