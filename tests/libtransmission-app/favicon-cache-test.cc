// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef> // size_t
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include "libtransmission-app/favicon-cache.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

// never decodes, so no fetch finds an icon
struct TestIcon {
    explicit constexpr operator bool() const noexcept
    {
        return false;
    }
};

// Stands in for the UI thread's event loop.
class UiQueue
{
public:
    // When set, post() returns only after the UI thread has run and destroyed the task.
    bool wait_for_run = false;

    void post(std::function<void()> func)
    {
        auto ran = std::future<void>{};

        {
            auto const lock = std::scoped_lock{ mutex_ };
            if (closed_) {
                return;
            }

            auto task = Task{ std::move(func), {} };
            if (wait_for_run) {
                ran = task.ran.get_future();
            }
            tasks_.emplace_back(std::move(task));
        }

        cv_.notify_one();

        if (ran.valid()) {
            ran.wait();
        }
    }

    // Runs tasks until `done()` holds. Returns false if `timeout` passes first.
    [[nodiscard]] bool run_until(std::function<bool()> const& done, std::chrono::seconds const timeout)
    {
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            run_one(100ms);
        }
        return true;
    }

    // Runs the queued tasks and drops any posted later.
    size_t close()
    {
        auto tasks = std::deque<Task>{};

        {
            auto const lock = std::scoped_lock{ mutex_ };
            closed_ = true;
            std::swap(tasks, tasks_);
        }

        for (auto& task : tasks) {
            run(task);
        }

        return std::size(tasks);
    }

private:
    struct Task {
        std::function<void()> func;
        std::promise<void> ran;
    };

    void run_one(std::chrono::milliseconds const timeout)
    {
        auto task = Task{};

        {
            auto lock = std::unique_lock{ mutex_ };
            if (!cv_.wait_for(lock, timeout, [this] { return !std::empty(tasks_); })) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        run(task);
    }

    static void run(Task& task)
    {
        task.func();
        task.func = {};
        task.ran.set_value();
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    bool closed_ = false;
};

std::string g_cache_dir;
UiQueue* g_ui_queue = nullptr;

class FaviconCacheTest : public SandboxedTest
{
protected:
    void SetUp() override
    {
        g_cache_dir = sandbox_dir();
        g_ui_queue = &ui_queue_;
    }

    void TearDown() override
    {
        g_ui_queue = nullptr;
    }

    UiQueue ui_queue_;
};

// nothing listens there, so every fetch fails fast
auto constexpr AnnounceUrl = "http://127.0.0.1:80/announce"sv;

} // namespace

namespace tr::app
{

template<>
TestIcon FaviconCache<TestIcon>::create_from_file(std::string_view /*filename*/) const
{
    return {};
}

template<>
TestIcon FaviconCache<TestIcon>::create_from_data(void const* /*data*/, size_t /*datalen*/) const
{
    return {};
}

template<>
std::string FaviconCache<TestIcon>::app_cache_dir() const
{
    return g_cache_dir;
}

template<>
void FaviconCache<TestIcon>::add_to_ui_thread(std::function<void()> idlefunc)
{
    g_ui_queue->post(std::move(idlefunc));
}

} // namespace tr::app

TEST_F(FaviconCacheTest, callbackRunsOnUiThreadWhenNoIconFound)
{
    // The curl thread releases each fetch callback after its UI task is gone.
    // Anything the two share is destroyed on the curl thread.
    ui_queue_.wait_for_run = true;

    auto const ui_thread_id = std::this_thread::get_id();
    auto n_calls = 0;
    auto callback_thread_id = std::thread::id{};
    auto got_icon = true;

    auto cache = tr::app::FaviconCache<TestIcon>{};
    cache.load(AnnounceUrl, [&](TestIcon const* const icon) {
        ++n_calls;
        callback_thread_id = std::this_thread::get_id();
        got_icon = icon != nullptr;
    });

    EXPECT_TRUE(ui_queue_.run_until([&n_calls] { return n_calls > 0; }, 30s));
    ui_queue_.close();

    EXPECT_EQ(1, n_calls);
    EXPECT_EQ(ui_thread_id, callback_thread_id);
    EXPECT_FALSE(got_icon);
}

TEST_F(FaviconCacheTest, destroyWhileFetching)
{
    auto n_calls = 0;
    auto cache = std::make_unique<tr::app::FaviconCache<TestIcon>>();
    cache->load(AnnounceUrl, [&n_calls](TestIcon const* /*icon*/) { ++n_calls; });

    // destruction cancels the fetches, which still queue their UI tasks
    cache.reset();

    EXPECT_LT(0U, ui_queue_.close());
    EXPECT_EQ(0, n_calls);
}

TEST_F(FaviconCacheTest, sitesAreMarkedScrapedWhenAnswered)
{
    // a new cache answers a site marked scraped at once, with its placeholder
    auto const is_marked_scraped = [] {
        auto answered = false;
        auto cache = tr::app::FaviconCache<TestIcon>{};
        cache.load(AnnounceUrl, [&answered](TestIcon const* /*icon*/) { answered = true; });
        return answered;
    };

    // quit before the site is answered
    auto cache = std::make_unique<tr::app::FaviconCache<TestIcon>>();
    cache->load(AnnounceUrl, [](TestIcon const* /*icon*/) {});
    cache.reset();
    EXPECT_FALSE(is_marked_scraped());

    auto answered = false;
    cache = std::make_unique<tr::app::FaviconCache<TestIcon>>();
    cache->load(AnnounceUrl, [&answered](TestIcon const* /*icon*/) { answered = true; });
    EXPECT_TRUE(ui_queue_.run_until([&answered] { return answered; }, 30s));
    EXPECT_TRUE(is_marked_scraped());

    ui_queue_.close();
}

TEST_F(FaviconCacheTest, sitesQueuedForAConnectionAreAnswered)
{
#if !defined(__linux__) && !defined(_WIN32)
    GTEST_SKIP() << "127.0.0.2 and up are not loopback addresses here";
#else
    // 16 fetches per site, one site more than 96 connections allow
    static auto constexpr NumSites = 7;

    auto n_calls = std::array<int, NumSites>{};
    auto cache = tr::app::FaviconCache<TestIcon>{};
    for (auto i = 0; i < NumSites; ++i) {
        cache.load(fmt::format("http://127.0.0.{:d}:81/announce", i + 1), [&n_calls, i](TestIcon const* /*icon*/) {
            ++n_calls[i];
        });
    }

    EXPECT_TRUE(ui_queue_.run_until([&n_calls] { return std::ranges::count(n_calls, 0) == 0; }, 60s));
    ui_queue_.close();

    auto expected = decltype(n_calls){};
    expected.fill(1);
    EXPECT_EQ(expected, n_calls);
#endif
}
