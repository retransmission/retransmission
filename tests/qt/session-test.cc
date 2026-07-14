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
};
} // namespace

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = SessionTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "session-test.moc"
