// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/profile.h"

#include <chrono>
#include <clocale>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

void child()
{
    TR_PROFILE_SCOPE("child");
    std::this_thread::sleep_for(10ms);
}

int main()
{
    // As clients do. A decimal-comma locale must not change the CSV.
    std::setlocale(LC_ALL, "");

#ifdef TR_ENABLE_BLOCK_PROFILING
    if (tr::profile::Collector::instance().batches_enabled()) {
        tr::profile::Collector::instance().record_batch({ 7, 16384, 32768, 2, 100, 150, 200, 0 });
    }
#endif

    {
        TR_PROFILE_SCOPE("parent");
        child();
        child();
    }
    auto worker = std::thread([] {
        child();
        child();
    });
    worker.join();

    auto const queued = tr::profile::Stamp{};
    std::this_thread::sleep_for(10ms);
    queued.record_since("queued");

    auto stalled = tr::profile::Interval{ "stalled" };
    stalled.stop(); // never started: records nothing
    stalled.start();
    std::this_thread::sleep_for(10ms);
    stalled.start(); // already holding: keeps the first start
    std::this_thread::sleep_for(10ms);
    stalled.stop();
    stalled.start(); // still holding at exit: records nothing

    try {
        TR_PROFILE_SCOPE("quoted,\"scope");
        throw std::runtime_error("unwind");
    } catch (std::runtime_error const&) {
    }
}
