// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

// Required by libtransmission public headers in this test TU.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define __TRANSMISSION__ // for libtransmission/net.h (via rpc-test-fixtures.h)

#include <algorithm>
#include <string>
#include <string_view>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QMessageBox>
#include <QStringList>
#include <QTest>
#include <QTimer>

#include <libtransmission/transmission.h>
#include <libtransmission/api-compat.h>

#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "Session.h"
#include "qt-test-fixtures.h"
#include "rpc-test-fixtures.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#define QCOMPARE_EQ(actual, expected) QCOMPARE(actual, expected)
#define QCOMPARE_NE(actual, expected) QVERIFY((actual) != (expected))
#endif

namespace api_compat = tr::api_compat;
using api_compat::Style;

Q_DECLARE_METATYPE(Style)

namespace
{
[[nodiscard]] QRegularExpression getSessionSetDownloadDirRegEx(Style const style, QString dir)
{
    dir = QRegularExpression::escape(dir);

    switch (style) {
    case Style::Tr4:
        return QRegularExpression{
            QStringLiteral(R"(^\{"arguments":\{"download-dir":"%1"\},"method":"session-set","tag":[0-9]+\}$)").arg(dir)
        };
    case Style::Tr5:
        return QRegularExpression{
            QStringLiteral(R"(^\{"id":[0-9]+,"jsonrpc":"2\.0","method":"session_set","params":\{"download_dir":"%1"\}\}$)")
                .arg(dir)
        };
    }

    abort();
    return {};
}

// Point `prefs` at `server`, which the next Session::restart() then connects to.
void setRemotePrefs(Prefs& prefs, MockRpcServer const& server)
{
    prefs.set(TR_KEY_remote_session_enabled, true);
    prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
    prefs.set(TR_KEY_remote_session_port, static_cast<int>(server.port()));
}

// Pump the Qt event loop until `pred` is true or we time out. Returning control
// to the event loop lets RpcClient deliver its queued-connection continuations.
template<typename Predicate>
[[nodiscard]] bool waitUntil(Predicate pred, int const timeout_ms = 10000)
{
    auto timer = QElapsedTimer{};
    timer.start();
    while (!pred()) {
        if (timer.elapsed() > timeout_ms) {
            return false;
        }
        QTest::qWait(20);
    }
    return true;
}

class SessionTest
    : public QObject
    , SandboxedTest
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        TR_QT_SKIP_UNLESS_SIGNALS_WORK();
    }

    static void download_dir_change_posts_session_set_data()
    {
        QTest::addColumn<Style>("initial_style");
        QTest::newRow("Tr4") << Style::Tr4;
        QTest::newRow("Tr5") << Style::Tr5;
    }
    void download_dir_change_posts_session_set()
    {
        // setup: set api_compat style
        QFETCH(Style const, initial_style);
        api_compat::set_default_style(initial_style);

        // setup: the sandbox
        auto const sandbox_dir = sandboxDir();
        auto const downloads_dir = QDir{ sandbox_dir }.filePath(QStringLiteral("Downloads"));
        QDir{}.mkpath(downloads_dir);

        // setup: a loopback RPC server for the session to talk to
        auto server = MockRpcServer{};

        // setup: make a Prefs that points at the loopback server
        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);
        prefs.set(TR_KEY_download_dir, sandbox_dir);

        // setup: make a Session
        auto rpc = RpcClient{};
        auto session = Session{ sandbox_dir, prefs, rpc };
        session.restart();

        // action: changing download-dir posts a "session-set" with the value
        prefs.set(TR_KEY_download_dir, downloads_dir);

        // verify that session_set::download_dir was POSTed
        auto const payload_re = getSessionSetDownloadDirRegEx(initial_style, downloads_dir);
        auto const has_session_set = [&server, &payload_re] {
            auto const bodies = server.request_bodies();
            return std::ranges::any_of(bodies, [&payload_re](std::string const& body) {
                return payload_re.match(QString::fromStdString(body)).hasMatch();
            });
        };
        QVERIFY(waitUntil(has_session_set));
    }

    // Toggling blocklist_updates_enabled (now a session-owned pref) must push a
    // session_set carrying it, i.e. the client asks the session to (dis)arm its
    // periodic blocklist auto-update over RPC.
    void auto_update_toggle_posts_session_set()
    {
        api_compat::set_default_style(Style::Tr5);

        auto const sandbox_dir = sandboxDir();
        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandbox_dir, prefs, rpc };
        session.restart();

        // toggling the pref is a bare session_set (no follow-up session_get to
        // drain), so just assert it reaches the wire
        prefs.set(TR_KEY_blocklist_updates_enabled, !prefs.get<bool>(TR_KEY_blocklist_updates_enabled));

        auto const has_session_set = [&server] {
            auto const bodies = server.request_bodies();
            return std::ranges::any_of(bodies, [](std::string const& body) {
                auto const q = QString::fromStdString(body);
                return q.contains(QStringLiteral("session_set")) && q.contains(QStringLiteral("blocklist_updates_enabled"));
            });
        };
        QVERIFY(waitUntil(has_session_set));
    }

    // A failed request is worth a message box only while the app is on screen.
    static void failed_add_shows_a_message_box_unless_shutting_down_data()
    {
        QTest::addColumn<bool>("shutting_down");
        QTest::newRow("running") << false;
        QTest::newRow("shutting down") << true;
    }
    void failed_add_shows_a_message_box_unless_shutting_down()
    {
        QFETCH(bool const, shutting_down);

        auto prefs = Prefs{};
        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        if (shutting_down) {
            session.setShuttingDown();
        }

        // Session's own slot connected first, so it has run by the time this one does.
        auto reported = false;
        auto const tag = session.torrent_add_report.connect_scoped(
            [&reported](Session::AddReport const& /*report*/) { reported = true; });

        // never started, the session fails each request with no round trip
        session.add_torrent(tr::app::AddData{ "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567" });
        QVERIFY(waitUntil([&reported] { return reported; }));

        auto const boxes = QApplication::topLevelWidgets();
        auto const n_boxes = std::ranges::count_if(boxes, [](QWidget const* const widget) {
            return qobject_cast<QMessageBox const*>(widget) != nullptr;
        });
        QCOMPARE(n_boxes, shutting_down ? 0 : 1);
        qDeleteAll(boxes);
    }

    // Like duplicates: one box for the batch, with each entry in its details.
    void several_failed_adds_share_a_message_box()
    {
        auto prefs = Prefs{};
        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };

        auto reported = false;
        auto const tag = session.torrent_add_report.connect_scoped(
            [&reported](Session::AddReport const& /*report*/) { reported = true; });

        // never started, the session fails each request with no round trip
        session.add_torrent(tr::app::AddData{ "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=first" });
        session.add_torrent(tr::app::AddData{ "magnet:?xt=urn:btih:89abcdef0123456789abcdef0123456789abcdef&dn=second" });
        QVERIFY(waitUntil([&reported] { return reported; }));

        auto const widgets = QApplication::topLevelWidgets();
        auto details = QStringList{};
        for (auto const* const widget : widgets) {
            if (auto const* const box = qobject_cast<QMessageBox const*>(widget); box != nullptr) {
                details << box->detailedText();
            }
        }
        QCOMPARE(details.size(), 1);
        QVERIFY(details.front().contains(QStringLiteral("first")));
        QVERIFY(details.front().contains(QStringLiteral("second")));
        qDeleteAll(widgets);
    }

    // The reply to a request sent before a restart arrives after stop(), so RpcClient drops it as a network error.
    // FreeSpaceLabel still schedules its next request, or it would stop updating.
    void restart_keeps_free_space_polling()
    {
        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto label = FreeSpaceLabel{};
        auto const* const timer = label.findChild<QTimer*>();
        QVERIFY(timer != nullptr);
        if (timer == nullptr) {
            return;
        }

        // Setting the path sends a free_space request and stops the timer until it's answered.
        label.setSession(session);
        label.setPath(sandboxDir());
        QVERIFY(!timer->isActive());
        session.restart();

        QVERIFY(waitUntil([timer] { return timer->isActive(); }));
    }
};
} // namespace

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = SessionTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "session-test.moc"
