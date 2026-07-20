// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <gtest/gtest.h>

#include <libtransmission/quark.h>

#include "libtransmission-app/prefs.h"
#include "libtransmission-app/session.h"

namespace
{

using namespace tr::app;

// exposes the protected setters so the test can drive the two inputs directly
class TestSession : public Session
{
public:
    using Session::Session;
    using Session::set_has_busy_torrents;
    using Session::set_session_is_local;
};

TEST(AppSessionTest, inhibitsOnlyWhenLocalActiveAndEnabled)
{
    auto prefs = Prefs{};
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);

    auto session = TestSession{ prefs };
    EXPECT_FALSE(session.should_inhibit_sleep()); // not local, nothing active

    session.set_session_is_local(true);
    EXPECT_FALSE(session.should_inhibit_sleep()); // still nothing active

    session.set_has_busy_torrents(true);
    EXPECT_TRUE(session.should_inhibit_sleep()); // local + active + enabled

    // toggling the preference is honored immediately
    prefs.set(TR_KEY_inhibit_desktop_hibernation, false);
    EXPECT_FALSE(session.should_inhibit_sleep());
    prefs.set(TR_KEY_inhibit_desktop_hibernation, true);
    EXPECT_TRUE(session.should_inhibit_sleep());

    // a remote session never inhibits, even with active transfers
    session.set_session_is_local(false);
    EXPECT_FALSE(session.should_inhibit_sleep());

    // going non-local also forgets the busy state, so returning to a local
    // session starts clean instead of inheriting the earlier activity
    session.set_session_is_local(true);
    EXPECT_FALSE(session.should_inhibit_sleep());
}

} // namespace
