// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

// Optional timing instrumentation for the peer-transfer benchmark in
// tools/profiling/.
//
// Wrap code in `TR_PROFILE_SCOPE("name");` to time it. Each thread's
// totals (calls, total/self/min/max time) are written as CSV at exit to
// the path in $TR_PROFILE_OUTPUT. Unset, nothing is recorded.
// Setting $TR_PROFILE_WRITE_BATCHES also writes per-batch disk write
// timings to "<path>.batches.tsv".
//
// Stamp times from a point in the past; Interval times how long a
// condition holds.
//
// Compiled in only when TR_ENABLE_BLOCK_PROFILING is defined (CMake option
// ENABLE_BLOCK_PROFILING). Otherwise every macro and class here is an empty
// stub.
//
// Names must be string literals. Disabled builds have no timing overhead.
// Stamp and Interval change size with TR_ENABLE_BLOCK_PROFILING, which only
// libtransmission's own sources define. Keep them out of public headers.
#ifdef TR_ENABLE_BLOCK_PROFILING

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace tr::profile
{
using Clock = std::chrono::steady_clock;

struct Totals {
    uint64_t calls = 0;
    int64_t total_ns = 0;
    int64_t self_ns = 0;
    int64_t min_ns = std::numeric_limits<int64_t>::max();
    int64_t max_ns = 0;
};

struct WriteBatch {
    uint64_t torrent, offset, bytes, blocks;
    int64_t queue_mean_ns, queue_max_ns, write_ns;
    int error;
};

class Collector
{
public:
    Collector()
    {
        if (auto const* path = std::getenv("TR_PROFILE_OUTPUT"); path != nullptr) {
            path_ = path;
        }
    }

    ~Collector()
    {
        if (path_.empty()) {
            return;
        }
        auto* out = std::fopen(path_.c_str(), "w");
        if (out == nullptr) {
            std::fprintf(stderr, "Cannot write block profile: %s\n", path_.c_str());
            return;
        }
        // os_thread matches /proc/<pid>/task/<tid> on Linux and is 0 elsewhere.
        std::fputs("thread,scope,calls,total_ms,self_ms,min_ms,max_ms,os_thread\n", out);
        for (auto const& [key, value] : totals_) {
            std::fprintf(out, "%zu,\"", key.first);
            for (char const c : key.second) {
                std::fputc(c, out);
                if (c == '"') {
                    std::fputc(c, out);
                }
            }
            std::fprintf(out, "\",%llu", static_cast<unsigned long long>(value.calls));
            for (auto const ns : { value.total_ns, value.self_ns, value.min_ns, value.max_ns }) {
                write_ms(out, ns);
            }
            std::fprintf(out, ",%lld\n", static_cast<long long>(os_threads_[key.first]));
        }
        if (std::fclose(out) != 0) {
            std::fprintf(stderr, "Cannot finish block profile: %s\n", path_.c_str());
        }
        if (!batches_.empty()) {
            auto const batch_path = path_ + ".batches.tsv";
            if (auto* batches = std::fopen(batch_path.c_str(), "w"); batches != nullptr) {
                std::fputs("thread\ttorrent\toffset\tbytes\tblocks\tqueue_mean_ns\tqueue_max_ns\twrite_ns\terror\n", batches);
                for (auto const& [thread, row] : batches_) {
                    std::fprintf(
                        batches,
                        "%lld\t%llu\t%llu\t%llu\t%llu\t%lld\t%lld\t%lld\t%d\n",
                        static_cast<long long>(os_threads_[thread]),
                        static_cast<unsigned long long>(row.torrent),
                        static_cast<unsigned long long>(row.offset),
                        static_cast<unsigned long long>(row.bytes),
                        static_cast<unsigned long long>(row.blocks),
                        static_cast<long long>(row.queue_mean_ns),
                        static_cast<long long>(row.queue_max_ns),
                        static_cast<long long>(row.write_ns),
                        row.error);
                }
                if (std::fclose(batches) != 0) {
                    std::fprintf(stderr, "Cannot finish batch profile: %s\n", batch_path.c_str());
                }
            } else {
                std::fprintf(stderr, "Cannot write batch profile: %s\n", batch_path.c_str());
            }
        }
    }

    static Collector& instance()
    {
        static auto collector = Collector{};
        return collector;
    }

    bool enabled() const noexcept
    {
        return !path_.empty();
    }

    bool batches_enabled() const noexcept
    {
        return enabled() && batches_enabled_;
    }

    void record_batch(WriteBatch row) noexcept
    {
        try {
            auto const lock = std::scoped_lock{ mutex_ };
            thread_local auto const thread = register_thread();
            batches_.emplace_back(thread, row);
        } catch (...) {
            // Diagnostic allocation failure must not affect storage.
        }
    }

    void record(char const* name, int64_t total, int64_t self)
    {
        auto const lock = std::scoped_lock{ mutex_ };
        thread_local auto const thread = register_thread();
        auto& row = totals_[{ thread, name }];
        ++row.calls;
        row.total_ns += total;
        row.self_ns += self;
        row.min_ns = std::min(row.min_ns, total);
        row.max_ns = std::max(row.max_ns, total);
    }

private:
    // Integer arithmetic only: clients set a locale, so %f could print a
    // decimal comma and split the CSV field.
    static void write_ms(std::FILE* out, int64_t const ns)
    {
        std::fprintf(out, ",%lld.%06lld", static_cast<long long>(ns / 1000000), static_cast<long long>(ns % 1000000));
    }

    // Call with mutex_ held.
    size_t register_thread()
    {
#ifdef __linux__
        os_threads_.push_back(static_cast<int64_t>(syscall(SYS_gettid)));
#else
        os_threads_.push_back(0);
#endif
        return std::size(os_threads_) - 1U;
    }

    std::string path_;
    std::mutex mutex_;
    std::vector<int64_t> os_threads_;
    std::map<std::pair<size_t, std::string_view>, Totals> totals_;
    bool batches_enabled_ = [] {
        auto const* value = std::getenv("TR_PROFILE_WRITE_BATCHES");
        return value != nullptr && std::string_view{ value } == "1";
    }();
    std::vector<std::pair<size_t, WriteBatch>> batches_;
};

class Scope
{
public:
    explicit Scope(char const* name)
        : name_{ name }
    {
        if (Collector::instance().enabled()) {
            parent_ = current_;
            current_ = this;
            active_ = true;
            start_ = Clock::now();
        }
    }

    ~Scope() noexcept
    {
        if (!active_) {
            return;
        }
        auto const end = Clock::now();
        auto const total = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        current_ = parent_;
        try {
            Collector::instance().record(name_, total, std::max<int64_t>(0, total - children_ns_));
        } catch (...) {
            // Profiling must not turn a completed operation into a failure.
        }
        if (parent_ != nullptr) {
            // Exclude this scope's bookkeeping from its parent's self time.
            parent_->children_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count();
        }
    }

    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;

private:
    inline static thread_local Scope* current_ = nullptr;
    char const* name_;
    Scope* parent_ = nullptr;
    Clock::time_point start_{};
    int64_t children_ns_ = 0;
    bool active_ = false;
};
// Records a duration measured outside a lexical scope.
inline void record(char const* name, int64_t const ns) noexcept
{
    if (!Collector::instance().enabled()) {
        return;
    }
    try {
        Collector::instance().record(name, ns, ns);
    } catch (...) {
        // Profiling must not turn a completed operation into a failure.
    }
}

// A creation time, kept so a later step can record how long an item
// waited, e.g. in a queue or for another thread.
class Stamp
{
public:
    int64_t elapsed_ns() const noexcept
    {
        return taken_ != Clock::time_point{} ?
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - taken_).count() :
            0;
    }
    void record_since(char const* name) const noexcept
    {
        if (taken_ != Clock::time_point{}) {
            record(name, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - taken_).count());
        }
    }

private:
    Clock::time_point taken_ = Collector::instance().enabled() ? Clock::now() : Clock::time_point{};
};

// Times how long a condition holds: from the first start() to the next
// stop(). A condition that is still holding at exit is not recorded.
class Interval
{
public:
    explicit Interval(char const* name) noexcept
        : name_{ name }
    {
    }

    void start() noexcept
    {
        if (!since_ && Collector::instance().enabled()) {
            since_ = Clock::now();
        }
    }

    void stop() noexcept
    {
        if (since_) {
            record(name_, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - *since_).count());
            since_.reset();
        }
    }

private:
    char const* name_;
    std::optional<Clock::time_point> since_;
};
} // namespace tr::profile

#define TR_PROFILE_JOIN_IMPL(a, b) a##b
#define TR_PROFILE_JOIN(a, b) TR_PROFILE_JOIN_IMPL(a, b)
// Not const: a child scope adds its time to the parent while the parent is alive.
#define TR_PROFILE_SCOPE(name) \
    auto TR_PROFILE_JOIN(tr_profile_scope_, __LINE__) = tr::profile::Scope \
    { \
        name \
    }

#else
namespace tr::profile
{
class Stamp
{
public:
    void record_since(char const* /*name*/) const noexcept
    {
    }
};

class Interval
{
public:
    explicit constexpr Interval(char const* /*name*/) noexcept
    {
    }

    void start() noexcept
    {
    }

    void stop() noexcept
    {
    }
};
} // namespace tr::profile

#define TR_PROFILE_SCOPE(name) static_cast<void>(0)
#endif
