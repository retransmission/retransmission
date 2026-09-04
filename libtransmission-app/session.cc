// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/session.h"

#include "libtransmission/log.h"
#include "libtransmission/macros.h"
#include "libtransmission/quark.h"
#include "libtransmission/transmission.h"
#include "libtransmission/variant.h"

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/rpc-client.h"

namespace tr::app
{

Session::Session(Prefs& prefs, RpcClient& rpc)
    : prefs_{ prefs }
    , rpc_{ rpc }
{
    prefs_connection_ = prefs_.observe_changes([this](tr_quark const key) {
        if (key == TR_KEY_inhibit_desktop_hibernation) {
            update_sleep_inhibit();
        }

        if (prefs_is_core(key) && !importing_settings_) {
            sync_pref_to_session(key);
        }
    });
}

void Session::sync_pref_to_session(tr_quark const key)
{
    auto* const embedded = embedded_session_;

    switch (key) {
    // For security reasons, the RPC server's own settings can't be changed
    // via RPC, so apply them directly when the session is embedded.
    case TR_KEY_rpc_authentication_required:
        if (embedded != nullptr) {
            tr_sessionSetRPCPasswordEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    case TR_KEY_rpc_enabled:
        if (embedded != nullptr) {
            tr_sessionSetRPCEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    case TR_KEY_rpc_password:
        if (embedded != nullptr) {
            tr_sessionSetRPCPassword(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_port:
        if (embedded != nullptr) {
            tr_sessionSetRPCPort(embedded, static_cast<uint16_t>(prefs_.get<int>(key)));
        }
        break;

    case TR_KEY_rpc_username:
        if (embedded != nullptr) {
            tr_sessionSetRPCUsername(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_whitelist:
        if (embedded != nullptr) {
            tr_sessionSetRPCWhitelist(embedded, prefs_.get<std::string>(key));
        }
        break;

    case TR_KEY_rpc_whitelist_enabled:
        if (embedded != nullptr) {
            tr_sessionSetRPCWhitelistEnabled(embedded, prefs_.get<bool>(key));
        }
        break;

    // The log level applies to this process's logger, not the session,
    // so there is nothing to send over RPC.
    case TR_KEY_message_level:
        tr_logSetLevel(prefs_.get<tr_log_level>(key));
        break;

    default:
        {
            auto params = tr_variant::Map{ 1U };
            params.insert_or_assign(key, prefs_.get<tr_variant>(key));
            rpc_.exec(TR_KEY_session_set, std::move(params), {});

            if (key == TR_KEY_download_dir) {
                session_refresh_needed();
            }
        }
        break;
    }
}

void Session::import_session_settings(tr_variant::Map const& settings)
{
    importing_settings_ = true;

    for (auto const& [key, value] : settings) {
        if (prefs_is_core(key)) {
            prefs_.set(key, value);
        }
    }

    // For security reasons, the RPC server's own settings aren't in
    // `session_get` responses; read them directly when the session is embedded.
    if (auto* const embedded = embedded_session_; embedded != nullptr) {
        prefs_.set(TR_KEY_rpc_authentication_required, tr_sessionIsRPCPasswordEnabled(embedded));
        prefs_.set(TR_KEY_rpc_enabled, tr_sessionIsRPCEnabled(embedded));
        prefs_.set(TR_KEY_rpc_password, tr_sessionGetRPCPassword(embedded));
        // int, not uint16_t: Converter<uint16_t> is tr_mode_t's octal-string
        // converter, which an integral pref field would silently reject
        prefs_.set(TR_KEY_rpc_port, static_cast<int>(tr_sessionGetRPCPort(embedded)));
        prefs_.set(TR_KEY_rpc_username, tr_sessionGetRPCUsername(embedded));
        prefs_.set(TR_KEY_rpc_whitelist, tr_sessionGetRPCWhitelist(embedded));
        prefs_.set(TR_KEY_rpc_whitelist_enabled, tr_sessionGetRPCWhitelistEnabled(embedded));
    }

    importing_settings_ = false;
}

std::optional<tr::Settings> Session::embedded_settings() const
{
    if (embedded_session_ != nullptr) {
        return tr_sessionGetSettings(embedded_session_);
    }

    return {};
}

void Session::set_session_type(std::optional<Type> const type)
{
    // should_inhibit_sleep() ignores busyness once we're non-local,
    // so a non-local session's remembered busyness is meaningless --
    // drop it here so a later switch back to a local session
    // starts from a clean slate.
    if (type.value_or(Type::Remote) == Type::Remote) {
        busy_ = false;
    }

    if (session_type_ != type) {
        session_type_ = type;
        update_sleep_inhibit();
        update_nap_inhibit();
    }
}

void Session::set_busy(bool const busy)
{
    if (busy_ != busy) {
        busy_ = busy;
        update_sleep_inhibit();
    }
}

bool Session::should_inhibit_sleep() const
{
    return is_local_filesystem() && busy_ && prefs_.get<bool>(TR_KEY_inhibit_desktop_hibernation);
}

void Session::update_sleep_inhibit()
{
    if (should_inhibit_sleep()) {
        sleep_inhibitor_.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Torrents are active");
    } else {
        sleep_inhibitor_.uninhibit();
    }
}

void Session::update_nap_inhibit()
{
    if (should_inhibit_nap()) {
        nap_inhibitor_.inhibit(TR_PROJ_APPNAME_CAPITALIZED, "Application is running");
    } else {
        nap_inhibitor_.uninhibit();
    }
}

} // namespace tr::app
