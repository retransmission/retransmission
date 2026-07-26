// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission-app/session.h"

#include <optional>

#include "libtransmission/macros.h"
#include "libtransmission/quark.h"

#include "libtransmission-app/prefs.h"

namespace tr::app
{

Session::Session(Prefs& prefs)
    : prefs_{ prefs }
{
    prefs_connection_ = prefs_.observe_changes([this](tr_quark const key) {
        if (key == TR_KEY_inhibit_desktop_hibernation) {
            update_sleep_inhibit();
        }
    });
}

void Session::set_session_type(std::optional<Type> const type)
{
    // should_inhibit_sleep() ignores the busy count once we're non-local, so a
    // non-local session's remembered activity is meaningless -- drop it here so
    // a later switch back to a local session starts from a clean slate.
    if (type.value_or(Type::Remote) == Type::Remote) {
        has_busy_torrents_ = false;
    }

    if (session_type_ != type) {
        session_type_ = type;
        update_sleep_inhibit();
        update_nap_inhibit();
    }
}

void Session::set_has_busy_torrents(bool const has_busy)
{
    if (has_busy_torrents_ != has_busy) {
        has_busy_torrents_ = has_busy;
        update_sleep_inhibit();
    }
}

bool Session::should_inhibit_sleep() const
{
    return is_local_filesystem() && has_busy_torrents_ && prefs_.get<bool>(TR_KEY_inhibit_desktop_hibernation);
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
