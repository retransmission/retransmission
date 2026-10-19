// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

// Required by libtransmission public headers in this test TU.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define __TRANSMISSION__ // for libtransmission/net.h (via rpc-test-fixtures.h)

#include <algorithm>
#include <cstdint> // int64_t
#include <optional>
#include <string>
#include <string_view>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSignalSpy>
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
using Style = api_compat::Style;

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
        qRegisterMetaType<int64_t>("int64_t");
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

        // action: changing download-dir fires two RPCs -- a "session-set" to push
        // the value, and a "session_get" (refreshSessionInfo) to re-read freespace.
        auto session_get_done = QSignalSpy{ &session, &Session::sessionUpdated };
        prefs.set(TR_KEY_download_dir, downloads_dir);

        // verify that session_set::download_dir was POSTed
        auto const payload_re = getSessionSetDownloadDirRegEx(initial_style, downloads_dir);
        auto const has_session_set = [&server, &payload_re]() {
            auto const bodies = server.request_bodies();
            return std::ranges::any_of(bodies, [&payload_re](std::string const& body) {
                return payload_re.match(QString::fromStdString(body)).hasMatch();
            });
        };
        QVERIFY(waitUntil(has_session_set));

        // drain the session_get round-trip so its RpcQueue finishes instead of
        // being left in flight (and leaked) when the test tears down.
        QVERIFY(waitUntil([&session_get_done]() { return !session_get_done.isEmpty(); }));
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

        auto const has_session_set = [&server]() {
            auto const bodies = server.request_bodies();
            return std::ranges::any_of(bodies, [](std::string const& body) {
                auto const q = QString::fromStdString(body);
                return q.contains(QStringLiteral("session_set")) && q.contains(QStringLiteral("blocklist_updates_enabled"));
            });
        };
        QVERIFY(waitUntil(has_session_set));
    }

    // A blocklist_update reply maps to the right signal: a success reports the new
    // rule count via blocklistUpdated(); a JSON-RPC error reports its message via
    // blocklistUpdateFailed(). The two signals never both fire.
    static void blocklist_update_reports_result_data()
    {
        QTest::addColumn<QString>("reply");
        QTest::addColumn<qlonglong>("expect_size"); // < 0 means expect the failure signal instead
        QTest::addColumn<QString>("expect_error");

        QTest::newRow("ok") << QStringLiteral(R"({"result":"success","arguments":{"blocklist_size":42}})") << 42LL << QString{};
        // An error must resolve via the queue's error handler, or the caller waits
        // forever for a reply that maps to no signal. Escaped literals, not raw
        // ones: moc mis-lexes an apostrophe inside a raw string and then silently
        // emits no metaobject for this class.
        QTest::newRow("download_error") << QStringLiteral(
                                               "{\"result\":\"Couldn\'t fetch blocklist: Not Found\",\"arguments\":{}}")
                                        << -1LL << QStringLiteral("Couldn\'t fetch blocklist: Not Found");
    }
    void blocklist_update_reports_result()
    {
        QFETCH(QString const, reply);
        QFETCH(qlonglong const, expect_size);
        QFETCH(QString const, expect_error);

        api_compat::set_default_style(Style::Tr5);

        auto server = MockRpcServer{};
        // quoted, so the marker can't also match a `blocklist_updates_enabled` request
        server.set_reply_for(R"("blocklist_update")", reply.toStdString());

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::blocklistUpdated };
        auto failed = QSignalSpy{ &session, &Session::blocklistUpdateFailed };
        auto refreshed = QSignalSpy{ &session, &Session::sessionUpdated };
        session.updateBlocklist();

        if (expect_size >= 0) {
            QVERIFY(waitUntil([&updated]() { return !updated.isEmpty(); }));
            QCOMPARE(updated.first().at(0).toLongLong(), expect_size);
            QVERIFY(failed.isEmpty());

            // A success also sends session_get for the new size and date.
            // Drain it, or its unfinished RpcQueue leaks under LSan at teardown.
            QVERIFY(waitUntil([&refreshed]() { return !refreshed.isEmpty(); }));
        } else {
            QVERIFY(waitUntil([&failed]() { return !failed.isEmpty(); }));
            QCOMPARE(failed.first().at(0).toString(), expect_error);
            QVERIFY(updated.isEmpty());
        }
    }

    // refreshBlocklistInfo() asks for the blocklist fields only,
    // so its reply can't overwrite prefs that changed while it was in flight.
    void refresh_blocklist_info_asks_for_only_its_fields()
    {
        api_compat::set_default_style(Style::Tr5);

        auto server = MockRpcServer{};
        server.set_reply_for(
            R"("session_get")",
            R"({"result":"success","arguments":{"blocklist_date":1700000000,"blocklist_size":42}})");

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::sessionUpdated };
        session.refreshBlocklistInfo();
        QVERIFY(waitUntil([&updated]() { return !updated.isEmpty(); }));
        QCOMPARE(session.blocklist_size().value_or(-1), int64_t{ 42 });
        QVERIFY(session.blocklist_date().has_value());

        auto const bodies = server.request_bodies();
        auto const get = std::ranges::find_if(bodies, [](std::string const& body) {
            return body.find(R"("session_get")") != std::string::npos;
        });
        QVERIFY(get != std::end(bodies));
        QVERIFY2(get->find(R"("fields":["blocklist_date","blocklist_size"])") != std::string::npos, get->c_str());
    }

    // stop() forgets the blocklist info and says so,
    // so the next session never shows the previous one's values.
    void stop_clears_blocklist_info()
    {
        api_compat::set_default_style(Style::Tr5);

        auto server = MockRpcServer{};
        server.set_reply_for(
            R"("session_get")",
            R"({"result":"success","arguments":{"blocklist_date":1700000000,"blocklist_size":42}})");

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::sessionUpdated };
        session.refreshSessionInfo();
        QVERIFY(waitUntil([&updated]() { return !updated.isEmpty(); }));
        QCOMPARE(session.blocklist_size().value_or(-1), int64_t{ 42 });
        QVERIFY(session.blocklist_date().has_value());

        auto stopped = QSignalSpy{ &session, &Session::stopped };
        session.stop();
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!session.blocklist_size().has_value());
        QVERIFY(!session.blocklist_date().has_value());
    }

    // Restarting an embedded session mid-update closes it.
    // Closing must answer the pending update, or its progress dialog would never resolve.
    void restart_fails_a_pending_embedded_blocklist_update()
    {
        // Nothing listens on port 1, so the download fails without leaving this machine.
        // That failure reaches the session thread only after the close that restart() queues.
        auto settings = tr::Settings{};
        settings.insert_or_assign(TR_KEY_blocklist_url, std::string_view{ "http://127.0.0.1:1/blocklist" });
        settings.insert_or_assign(TR_KEY_dht_enabled, false);
        settings.insert_or_assign(TR_KEY_lpd_enabled, false);
        settings.insert_or_assign(TR_KEY_pex_enabled, false);
        settings.insert_or_assign(TR_KEY_port_forwarding_enabled, false);
        settings.insert_or_assign(TR_KEY_utp_enabled, false);
        tr::settings::save(tr_sessionGetSettingsFilename(sandboxDir().toStdString()), settings);

        auto prefs = Prefs{};
        prefs.set(TR_KEY_remote_session_enabled, false);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();
        QVERIFY(session.is_embedded());

        auto updated = QSignalSpy{ &session, &Session::blocklistUpdated };
        auto failed = QSignalSpy{ &session, &Session::blocklistUpdateFailed };
        session.updateBlocklist();
        session.restart();

        QVERIFY(waitUntil([&failed]() { return !failed.isEmpty(); }));
        QCOMPARE(failed.count(), 1);
        QVERIFY(updated.isEmpty());
    }

    // The next three tests restart the session before returning to the event loop.
    // The reply to the request sent before the restart then arrives after stop(),
    // so RpcClient drops it as a network error.

    // The port test still ends, or portTestPending() would stay true for good.
    void restart_ends_a_pending_port_test()
    {
        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto tested = 0;
        auto status = std::optional<bool>{ true };
        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        // A lambda, not QSignalSpy, which needs portTested()'s argument types registered as metatypes.
        QObject::connect(
            &session,
            &Session::portTested,
            [&tested, &status](std::optional<bool> const result, tr_address_type /*ip_protocol*/) {
                ++tested;
                status = result;
            });

        session.portTest(TR_AF_INET);
        QVERIFY(session.portTestPending(TR_AF_INET));
        session.restart();

        QVERIFY(waitUntil([&tested]() { return tested > 0; }));
        QCOMPARE(tested, 1);
        QVERIFY(!status.has_value());
        QVERIFY(!session.portTestPending(TR_AF_INET));
    }

    // The blocklist update still reports a result, or its progress dialog would never resolve.
    void restart_fails_a_pending_blocklist_update()
    {
        auto server = MockRpcServer{};

        auto prefs = Prefs{};
        setRemotePrefs(prefs, server);

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::blocklistUpdated };
        auto failed = QSignalSpy{ &session, &Session::blocklistUpdateFailed };
        session.updateBlocklist();
        session.restart();

        QVERIFY(waitUntil([&failed]() { return !failed.isEmpty(); }));
        QCOMPARE(failed.count(), 1);
        QVERIFY(updated.isEmpty());
    }

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

        QVERIFY(waitUntil([timer]() { return timer->isActive(); }));
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
