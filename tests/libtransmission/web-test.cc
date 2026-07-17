// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <event2/http.h>

#include <fmt/format.h>

#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/web.h>

#include "loopback-server.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace
{

// LoopbackServer is shared via loopback-server.h.
using tr::test::LoopbackServer;

// tr_web needs a Mediator; this one only overrides the user-agent so a test
// can confirm the header reaches the server.
class TestMediator final : public tr_web::Mediator
{
public:
    [[nodiscard]] std::optional<std::string_view> userAgent() const override
    {
        if (user_agent_) {
            return std::string_view{ *user_agent_ };
        }
        return std::nullopt;
    }

    std::optional<std::string> user_agent_;
};

class WebTest : public ::tr::test::SandboxedTest
{
protected:
    // Run a fetch synchronously and return the response. done_func is invoked
    // from tr_web's own thread, so hand the result back through a promise.
    tr_web::FetchResponse fetch(tr_web::FetchOptions options)
    {
        return fetch(*web_, std::move(options));
    }

    static tr_web::FetchResponse fetch(tr_web& web, tr_web::FetchOptions options)
    {
        auto promise = std::make_shared<std::promise<tr_web::FetchResponse>>();
        auto future = promise->get_future();
        options.done_func = [promise](tr_web::FetchResponse const& response) {
            promise->set_value(response);
        };

        web.fetch(std::move(options));

        if (future.wait_for(15s) != std::future_status::ready) {
            EXPECT_TRUE(false) << "fetch did not complete";
            return {};
        }
        return future.get();
    }

    [[nodiscard]] tr_web::FetchOptions options(std::string_view path = "/"sv, void* user_data = nullptr)
    {
        return tr_web::FetchOptions{ server_.url(path), nullptr, user_data };
    }

    TestMediator mediator_;
    std::unique_ptr<tr_web> web_ = tr_web::create(mediator_);
    LoopbackServer server_;
};

TEST_F(WebTest, getReturnsBody)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", "hello world"sv); });

    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("hello world"sv, response.body);
    EXPECT_TRUE(response.did_connect);
    EXPECT_FALSE(response.did_timeout);
    EXPECT_EQ("127.0.0.1"sv, response.primary_ip);
}

TEST_F(WebTest, noBodyIsGet)
{
    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("GET"sv, server_.lastRequest().method);
}

TEST_F(WebTest, httpErrorStatusIsSurfaced)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, 404, "Not Found", "nope"sv); });

    auto const response = fetch(options());
    EXPECT_EQ(404, response.status);
    EXPECT_TRUE(response.did_connect);
}

TEST_F(WebTest, postSendsBody)
{
    auto opts = options();
    opts.body = "the request body";

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(200, response.status);

    auto const req = server_.lastRequest();
    EXPECT_EQ("POST"sv, req.method);
    EXPECT_EQ("the request body"sv, req.body);
}

TEST_F(WebTest, requestHeadersAreSent)
{
    auto opts = options();
    opts.headers.insert_or_assign("X-Custom-Header", "custom-value");
    opts.headers.insert_or_assign("Content-Type", "application/json");

    fetch(std::move(opts));

    auto const req = server_.lastRequest();
    EXPECT_EQ("custom-value"sv, req.headers.at("x-custom-header"));
    EXPECT_EQ("application/json"sv, req.headers.at("content-type"));
}

TEST_F(WebTest, responseHeadersAreCaptured)
{
    server_.setHandler([](evhttp_request* req) {
        auto* const out = evhttp_request_get_output_headers(req);
        evhttp_add_header(out, "X-Reply-Header", "reply-value");
        LoopbackServer::reply(req, HTTP_OK, "OK", "body"sv);
    });

    auto const response = fetch(options());

    // lookup is case-insensitive
    ASSERT_TRUE(response.header("x-reply-header"));
    EXPECT_EQ("reply-value"sv, *response.header("x-reply-header"));
    EXPECT_EQ("reply-value"sv, *response.header("X-REPLY-HEADER"));

    // a missing header is nullopt
    EXPECT_FALSE(response.header("X-Does-Not-Exist"));
}

TEST_F(WebTest, basicAuthSendsCredentials)
{
    auto opts = options();
    opts.username = "aladdin";
    opts.password = "opensesame";

    fetch(std::move(opts));

    auto const expected = fmt::format("Basic {:s}", tr_base64_encode("aladdin:opensesame"sv));
    EXPECT_EQ(expected, server_.lastRequest().headers.at("authorization"));
}

TEST_F(WebTest, basicAuthEmptyPassword)
{
    auto opts = options();
    opts.username = "user";
    // no password

    fetch(std::move(opts));

    auto const expected = fmt::format("Basic {:s}", tr_base64_encode("user:"sv));
    EXPECT_EQ(expected, server_.lastRequest().headers.at("authorization"));
}

TEST_F(WebTest, netrcCredentialsAreSent)
{
    auto const netrc_path = fmt::format("{:s}/netrc", sandboxDir());
    createFileWithContents(netrc_path, "machine 127.0.0.1 login aladdin password opensesame\n"sv);

    auto opts = options();
    opts.netrc_file = netrc_path;

    fetch(std::move(opts));

    auto const expected = fmt::format("Basic {:s}", tr_base64_encode("aladdin:opensesame"sv));
    EXPECT_EQ(expected, server_.lastRequest().headers.at("authorization"));
}

TEST_F(WebTest, authSchemeAnyDefersCredentials)
{
    auto opts = options();
    opts.username = "aladdin";
    opts.password = "opensesame";
    opts.auth_scheme = tr_web::FetchOptions::AuthScheme::Any;

    fetch(std::move(opts));

    // Under CURLAUTH_ANY curl waits for a challenge before sending credentials,
    // so a server that answers 200 immediately never sees an Authorization header.
    auto const& headers = server_.lastRequest().headers;
    EXPECT_EQ(headers.end(), headers.find("authorization"));
}

TEST_F(WebTest, userDataRoundTrips)
{
    auto sentinel = 42;
    auto const response = fetch(options("/"sv, &sentinel));
    EXPECT_EQ(&sentinel, response.user_data);
}

TEST_F(WebTest, rangeRequestSetsHeader)
{
    auto opts = options();
    opts.range = std::make_pair(uint64_t{ 0 }, uint64_t{ 3 });

    fetch(std::move(opts));

    EXPECT_EQ("bytes=0-3"sv, server_.lastRequest().headers.at("range"));
}

TEST_F(WebTest, cookiesAreSent)
{
    auto opts = options();
    opts.cookies = "a=b; c=d";

    fetch(std::move(opts));

    EXPECT_EQ("a=b; c=d"sv, server_.lastRequest().headers.at("cookie"));
}

TEST_F(WebTest, userAgentFromMediatorIsSent)
{
    // the mediator's user agent is read once when the tr_web is created, so
    // set it before building a dedicated tr_web for this test
    mediator_.user_agent_ = "TestAgent/1.0";
    auto web = tr_web::create(mediator_);

    fetch(*web, options());

    EXPECT_EQ("TestAgent/1.0"sv, server_.lastRequest().headers.at("user-agent"));
}

TEST_F(WebTest, onDataReceivedReportsByteCount)
{
    static auto constexpr Body = "0123456789abcdef"sv;
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Body); });

    auto total = std::size_t{ 0 };
    auto opts = options();
    opts.on_data_received = [&total](std::size_t n_bytes) {
        total += n_bytes;
    };

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(std::size(Body), std::size(response.body));
    EXPECT_EQ(std::size(Body), total);
}

TEST_F(WebTest, timeoutIsReported)
{
    // Handler that never replies, so the transfer exceeds the timeout.
    server_.setHandler([](evhttp_request* /*req*/) {});

    auto opts = options();
    opts.timeout_secs = 1s;

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(0, response.status);
    EXPECT_TRUE(response.did_timeout);
}

TEST_F(WebTest, destroyRightAfterFetchDoesNotHang)
{
    // Stress the shutdown path: destroying a tr_web immediately after a
    // fetch completes races the destructor's deadline against the curl
    // thread returning to its condition-variable wait. A lost wakeup there
    // hangs the destructor's join() forever, so this test times out.
    for (auto i = 0; i < 50; ++i) {
        auto web = tr_web::create(mediator_);
        fetch(*web, options());
    }
}

} // namespace
