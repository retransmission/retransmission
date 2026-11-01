// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdlib> // std::getenv()
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/file.h>
#include <libtransmission/macros.h>

#include "libtransmission-app/app.h"
#include "libtransmission-app/rpc-client.h"

class TransmissionTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tr::app::init();
    }
};

// Creates a unique temporary directory on construction and recursively removes
// it on destruction. A lightweight counterpart to the sandbox helper in
// tests/libtransmission/ that avoids that fixture's heavier dependencies.
class Sandbox
{
public:
    Sandbox()
        : path_{ create_sandbox(get_default_parent_dir(), TR_PROJ_APPNAME "-app-test-XXXXXX") }
    {
    }

    ~Sandbox()
    {
        rimraf(path_);
    }

    Sandbox(Sandbox const&) = delete;
    Sandbox(Sandbox&&) = delete;
    Sandbox& operator=(Sandbox const&) = delete;
    Sandbox& operator=(Sandbox&&) = delete;

    [[nodiscard]] std::string const& path() const noexcept
    {
        return path_;
    }

private:
    [[nodiscard]] static std::string get_default_parent_dir()
    {
        if (auto const* const tmpdir = std::getenv("TMPDIR"); tmpdir != nullptr) {
            return tmpdir;
        }

        return tr_sys_dir_get_current();
    }

    [[nodiscard]] static std::string create_sandbox(std::string_view const parent_dir, std::string_view const tmpl)
    {
        auto path = fmt::format("{:s}/{:s}", tr_sys_path_resolve(parent_dir), tmpl);
        tr_sys_dir_create_temp(std::data(path));
        tr_sys_path_native_separators(std::data(path));
        return path;
    }

    static void rimraf(std::string_view const path)
    {
        if (auto const info = tr_sys_path_get_info(path); info && info->isFolder()) {
            for (auto const& name : tr_sys_dir_get_files(path, tr_basename_accept_all)) {
                rimraf(fmt::format("{:s}/{:s}", path, name));
            }
        }

        tr_sys_path_remove(path);
    }

    std::string const path_;
};

// A `TransmissionTest` that additionally owns a per-test `Sandbox` directory.
class SandboxedTest : public TransmissionTest
{
protected:
    [[nodiscard]] std::string const& sandbox_dir() const noexcept
    {
        return sandbox_.path();
    }

private:
    Sandbox sandbox_;
};

// Stands in for the toolkit's UI thread. RpcClient marshals every response and
// signal through this hook; the test drives it like an event loop from the main
// thread, so continuations run where a real toolkit would run them.
// Declare it before the RpcClient that uses its marshaler: a late embedded reply
// calls the marshaler from the session thread and must find the loop alive.
// The destructor drops queued closures unrun.
class UiLoop
{
public:
    [[nodiscard]] tr::app::RpcClient::UiThreadFunc marshaler()
    {
        return [this](std::function<void()> fn) {
            auto const lock = std::scoped_lock{ mutex_ };
            queue_.push_back(std::move(fn));
            cv_.notify_one();
        };
    }

    // Runs one queued closure, waiting up to `wait` for one to arrive. Returns whether one ran.
    bool pump_one(std::chrono::milliseconds const wait = std::chrono::milliseconds{ 10 })
    {
        auto fn = std::function<void()>{};
        {
            auto lock = std::unique_lock{ mutex_ };
            cv_.wait_for(lock, wait, [this] { return !queue_.empty(); });
            if (queue_.empty()) {
                return false;
            }
            fn = std::move(queue_.front());
            queue_.pop_front();
        }

        fn();
        return true;
    }

    // Pumps until `pred` holds. Returns false if `timeout` passes first.
    template<typename Pred>
    [[nodiscard]] bool pump_until(Pred pred, std::chrono::milliseconds const timeout = std::chrono::seconds{ 10 })
    {
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (!pred()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            pump_one();
        }
        return true;
    }

    // Pumps until `fut` is ready, then returns its value. Throws if that takes too long.
    template<typename T>
    [[nodiscard]] T pump_until_ready(std::future<T>& fut)
    {
        if (!pump_until([&fut] { return fut.wait_for(std::chrono::seconds{}) == std::future_status::ready; })) {
            throw std::runtime_error{ "timed out waiting for RPC response" };
        }
        return fut.get();
    }

    // Pumps whatever arrives for `quiet`. Returns true if nothing did.
    [[nodiscard]] bool quiet_for(std::chrono::milliseconds const quiet)
    {
        auto const deadline = std::chrono::steady_clock::now() + quiet;
        auto ran = false;
        for (auto now = std::chrono::steady_clock::now(); now < deadline; now = std::chrono::steady_clock::now()) {
            if (pump_one(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now))) {
                ran = true;
            }
        }
        return !ran;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
};
