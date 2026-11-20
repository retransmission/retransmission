// This file Copyright © Retransmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdint>
#include <functional> // std::less
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__linux__) && defined(WITH_SYSTEMD)
#include <systemd/sd-bus.h>
#endif

#include "libtransmission/net.h"

using namespace std::literals;

#if defined(__linux__) && defined(WITH_SYSTEMD)
namespace
{
auto constexpr Service = "org.freedesktop.NetworkManager";
auto constexpr DeviceInterface = "org.freedesktop.NetworkManager.Device";
auto constexpr ActiveInterface = "org.freedesktop.NetworkManager.Connection.Active";

// NM_ACTIVATION_STATE_FLAG_EXTERNAL: NetworkManager follows a configuration made by someone else
auto constexpr ActivationStateFlagExternal = uint32_t{ 0x80U };

#ifdef HAVE_SD_BUS_SET_METHOD_CALL_TIMEOUT
// short, so a NetworkManager that hangs holds the next refresh up only briefly
auto constexpr CallTimeoutUsec = uint64_t{ 250'000U };
#endif

struct ErrorFree {
    void operator()(sd_bus_error* error) const noexcept
    {
        sd_bus_error_free(error);
    }
};

// a device's properties, read in one GetAll call
struct Device {
    std::string name;
    bool managed = false;
    std::string active_connection; // "/" when none is active
};

[[nodiscard]] std::optional<Device> read_device(sd_bus* bus, char const* path)
{
    auto error = sd_bus_error{ SD_BUS_ERROR_NULL };
    auto const error_free = std::unique_ptr<sd_bus_error, ErrorFree>{ &error };
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(
            bus,
            Service,
            path,
            "org.freedesktop.DBus.Properties",
            "GetAll",
            &error,
            &reply,
            "s",
            DeviceInterface) < 0) {
        return {};
    }
    auto const reply_unref = std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>{ reply, sd_bus_message_unref };

    auto device = Device{};
    if (sd_bus_message_enter_container(reply, 'a', "{sv}") < 0) {
        return {};
    }
    while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
        char const* key = nullptr;
        if (sd_bus_message_read(reply, "s", &key) < 0) {
            return {};
        }
        auto const property = std::string_view{ key };
        char const* str = nullptr;
        auto flag = int{};
        if (property == "Interface" && sd_bus_message_read(reply, "v", "s", &str) >= 0) {
            device.name = str;
        } else if (property == "ActiveConnection" && sd_bus_message_read(reply, "v", "o", &str) >= 0) {
            device.active_connection = str;
        } else if (property == "Managed" && sd_bus_message_read(reply, "v", "b", &flag) >= 0) {
            device.managed = flag != 0;
        } else if (sd_bus_message_skip(reply, "v") < 0) {
            return {};
        }
        if (sd_bus_message_exit_container(reply) < 0) {
            return {};
        }
    }

    if (std::empty(device.name)) {
        return {};
    }
    return device;
}

[[nodiscard]] tr_net_nm_state device_state(sd_bus* bus, Device const& device)
{
    if (!device.managed) {
        return tr_net_nm_state::Unmanaged;
    }
    if (std::empty(device.active_connection) || device.active_connection == "/") {
        return tr_net_nm_state::Inactive;
    }

    auto error = sd_bus_error{ SD_BUS_ERROR_NULL };
    auto const error_free = std::unique_ptr<sd_bus_error, ErrorFree>{ &error };
    auto flags = uint32_t{};
    if (sd_bus_get_property_trivial(
            bus,
            Service,
            device.active_connection.c_str(),
            ActiveInterface,
            "StateFlags",
            &error,
            'u',
            &flags) < 0) {
        return tr_net_nm_state::Unknown;
    }
    return (flags & ActivationStateFlagExternal) != 0U ? tr_net_nm_state::External : tr_net_nm_state::Configured;
}

[[nodiscard]] std::map<std::string, tr_net_nm_state, std::less<>> query_states()
{
    auto states = std::map<std::string, tr_net_nm_state, std::less<>>{};

    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) {
        return states;
    }
    auto const bus_unref = std::unique_ptr<sd_bus, decltype(&sd_bus_flush_close_unref)>{ bus, sd_bus_flush_close_unref };
#ifdef HAVE_SD_BUS_SET_METHOD_CALL_TIMEOUT
    sd_bus_set_method_call_timeout(bus, CallTimeoutUsec);
#endif

    auto error = sd_bus_error{ SD_BUS_ERROR_NULL };
    auto const error_free = std::unique_ptr<sd_bus_error, ErrorFree>{ &error };
    sd_bus_message* reply = nullptr;
    // fails at once when NetworkManager isn't running
    if (sd_bus_call_method(bus, Service, "/org/freedesktop/NetworkManager", Service, "GetDevices", &error, &reply, "") < 0) {
        return states;
    }
    auto const reply_unref = std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>{ reply, sd_bus_message_unref };

    auto paths = std::vector<std::string>{};
    if (sd_bus_message_enter_container(reply, 'a', "o") < 0) {
        return states;
    }
    char const* path = nullptr;
    while (sd_bus_message_read(reply, "o", &path) > 0) {
        paths.emplace_back(path);
    }

    for (auto const& device_path : paths) {
        if (auto const device = read_device(bus, device_path.c_str()); device) {
            states.try_emplace(device->name, device_state(bus, *device));
        }
    }

    return states;
}
} // namespace

// The settings UIs ask every few seconds while open, from the session thread, and
// the answer changes only when an interface comes or goes. So the answer is kept
// for a while, and refreshed on a thread of its own: the session thread never
// waits on D-Bus, at the cost of the first answer being empty.
std::map<std::string, tr_net_nm_state, std::less<>> tr_net_networkmanager_states()
{
    static auto constexpr MaxAge = std::chrono::seconds{ 10 };

    struct State {
        std::mutex mutex;
        std::map<std::string, tr_net_nm_state, std::less<>> states;
        std::optional<std::chrono::steady_clock::time_point> refreshed_at;
        bool refreshing = false;
    };
    // never destroyed, since a refresh thread may still be running at exit
    static auto* const state = new State{};

    auto const lock = std::scoped_lock{ state->mutex };
    if (auto const now = std::chrono::steady_clock::now();
        !state->refreshing && (!state->refreshed_at || now - *state->refreshed_at >= MaxAge)) {
        state->refreshing = true;
        std::thread([] {
            auto states = query_states();
            auto const refresh_lock = std::scoped_lock{ state->mutex };
            state->states = std::move(states);
            state->refreshed_at = std::chrono::steady_clock::now();
            state->refreshing = false;
        }).detach();
    }
    return state->states;
}
#else
std::map<std::string, tr_net_nm_state, std::less<>> tr_net_networkmanager_states()
{
    return {};
}
#endif
