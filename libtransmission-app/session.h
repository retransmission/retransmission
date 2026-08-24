// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <optional>

#include <sigslot/signal.hpp>

#include <woke/woke.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

struct tr_session;

namespace tr::app
{

class Prefs;
class RpcClient;

class Session
{
public:
    // How the app reaches its `tr_session`:
    enum class Type : uint8_t {
        Embedded, // the session runs inside this process
        Local, // a separate daemon process on this machine
        Remote, // a daemon on another machine
    };

    Session(Prefs& prefs, RpcClient& rpc);
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;
    virtual ~Session() = default;

    [[nodiscard]] constexpr std::optional<Type> type() const noexcept
    {
        return session_type_;
    }

    // keep the machine awake: a local-filesystem session with active transfers
    [[nodiscard]] bool should_inhibit_sleep() const;

    // returns true iff the session runs inside this process
    [[nodiscard]] constexpr bool is_embedded() const noexcept
    {
        return session_type_ == Type::Embedded;
    }

    // keep this process un-throttled: only when it hosts the session itself
    [[nodiscard]] bool should_inhibit_nap() const noexcept
    {
        return is_embedded();
    }

    // Copy the session's settings, e.g. a `session_get` response, into Prefs
    // without echoing them back to the session. Also refreshes the RPC
    // server settings, which for security reasons are only readable from
    // an embedded session.
    void import_session_settings(tr_variant::Map const& settings);

    // The embedded session's settings, e.g. for `Prefs::save()`;
    // nullopt when there is no embedded session.
    [[nodiscard]] std::optional<tr::Settings> embedded_settings() const;

    // Fired after syncing a pref whose change invalidates other session info,
    // e.g. changing `download_dir` changes `session_get`'s freespace argument.
    sigslot::signal<> session_refresh_needed;

protected:
    void set_session_type(std::optional<Type> type);
    void set_busy(bool busy);

    // The embedded session, or nullptr when there isn't one. Some settings,
    // e.g. the RPC server's, can only be applied to an embedded session.
    void set_embedded_session(tr_session* session) noexcept
    {
        embedded_session_ = session;
    }

private:
    void update_sleep_inhibit();
    void update_nap_inhibit();
    void sync_pref_to_session(tr_quark key);

    [[nodiscard]] bool is_local_filesystem() const noexcept
    {
        return session_type_.value_or(Type::Remote) != Type::Remote;
    }

    Prefs& prefs_;
    RpcClient& rpc_;
    tr_session* embedded_session_ = nullptr;
    woke::SleepInhibitor sleep_inhibitor_;
    woke::NapInhibitor nap_inhibitor_;
    std::optional<Type> session_type_;
    bool busy_ = false;
    bool importing_settings_ = false;
    sigslot::scoped_connection prefs_connection_;
};

} // namespace tr::app
