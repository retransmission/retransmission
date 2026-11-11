// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <thread>

#include <glibmm/main.h>

#include "../libtransmission-app/test-fixtures.h"

using GtkTest = TransmissionTest;

// Spins the default main loop for `duration`.
inline void spin_for(std::chrono::milliseconds const duration)
{
    auto const context = Glib::MainContext::get_default();
    auto const deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!context->iteration(false)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
        }
    }
}

// Spins the default main loop until `pred` holds or `timeout` passes.
template<typename Pred>
[[nodiscard]] bool spin_until(Pred pred, std::chrono::milliseconds const timeout = std::chrono::seconds{ 5 })
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        spin_for(std::chrono::milliseconds{ 1 });
    }
    return true;
}
