// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

// Required by libtransmission public headers in this test TU.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define __TRANSMISSION__ // for libtransmission/net.h (via rpc-test-fixtures.h)

#include <algorithm>
#include <string>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <libtransmission/transmission.h>
#include <libtransmission/api-compat.h>

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
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
        prefs.set(TR_KEY_remote_session_port, static_cast<int>(server.port()));
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
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
        prefs.set(TR_KEY_remote_session_port, static_cast<int>(server.port()));

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

    // A blocklist_update_v2 reply maps to the right signal: an "ok" status reports
    // the new rule count via blocklistUpdated(); any other status reports the error
    // string via blocklistUpdateFailed(). The two signals never both fire.
    static void blocklist_update_v2_reports_result_data()
    {
        QTest::addColumn<QString>("reply");
        QTest::addColumn<qlonglong>("expect_size"); // < 0 means expect the failure signal instead
        QTest::addColumn<QString>("expect_error");

        QTest::newRow("ok") << QStringLiteral(
                                   R"({"result":"success","arguments":{"status":"ok","blocklist_size":42,"error":""}})")
                            << 42LL << QString{};
        QTest::newRow("download_error")
            << QStringLiteral(
                   R"({"result":"success","arguments":{"status":"download_error","blocklist_size":0,"error":"boom"}})")
            << -1LL << QStringLiteral("boom");
    }
    void blocklist_update_v2_reports_result()
    {
        QFETCH(QString const, reply);
        QFETCH(qlonglong const, expect_size);
        QFETCH(QString const, expect_error);

        api_compat::set_default_style(Style::Tr5);

        auto server = MockRpcServer{};
        server.set_reply_for("blocklist_update_v2", reply.toStdString());

        auto prefs = Prefs{};
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
        prefs.set(TR_KEY_remote_session_port, static_cast<int>(server.port()));

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::blocklistUpdated };
        auto failed = QSignalSpy{ &session, &Session::blocklistUpdateFailed };
        session.updateBlocklist();

        if (expect_size >= 0) {
            QVERIFY(waitUntil([&updated]() { return !updated.isEmpty(); }));
            QCOMPARE(updated.first().at(0).toLongLong(), expect_size);
            QVERIFY(failed.isEmpty());
        } else {
            QVERIFY(waitUntil([&failed]() { return !failed.isEmpty(); }));
            QCOMPARE(failed.first().at(0).toString(), expect_error);
            QVERIFY(updated.isEmpty());
        }
    }

    // A JSON-RPC error (e.g. a daemon that doesn't implement blocklist_update_v2)
    // must resolve to the failure signal via the queue's error handler, not leave
    // the caller waiting for a reply that never maps to a signal.
    void blocklist_update_v2_rpc_error_reports_failure()
    {
        api_compat::set_default_style(Style::Tr5);

        auto server = MockRpcServer{};
        // "no method name" is the legacy errmsg that maps to METHOD_NOT_FOUND.
        server.set_reply_for("blocklist_update_v2", R"({"result":"no method name","arguments":{}})");

        auto prefs = Prefs{};
        prefs.set(TR_KEY_remote_session_enabled, true);
        prefs.set(TR_KEY_remote_session_host, QStringLiteral("127.0.0.1"));
        prefs.set(TR_KEY_remote_session_port, static_cast<int>(server.port()));

        auto rpc = RpcClient{};
        auto session = Session{ sandboxDir(), prefs, rpc };
        session.restart();

        auto updated = QSignalSpy{ &session, &Session::blocklistUpdated };
        auto failed = QSignalSpy{ &session, &Session::blocklistUpdateFailed };
        session.updateBlocklist();

        QVERIFY(waitUntil([&failed]() { return !failed.isEmpty(); }));
        QVERIFY(updated.isEmpty());
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
