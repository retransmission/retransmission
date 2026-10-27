// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <event2/http.h>

#include <fmt/format.h>

#include <libtransmission/constants.h>
#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/utils.h> // tr_num_parse()
#include <libtransmission/web.h>

#include "loopback-server.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace
{

// LoopbackServer is shared via loopback-server.h.
using tr::test::LoopbackServer;

// Releasing on scope exit keeps failed assertions from stranding a worker.
class ThreadGate
{
public:
    ThreadGate() = default;

    ThreadGate(ThreadGate const&) = delete;
    ThreadGate(ThreadGate&&) = delete;
    ThreadGate& operator=(ThreadGate const&) = delete;
    ThreadGate& operator=(ThreadGate&&) = delete;

    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~ThreadGate()
    {
        open();
    }

    void open()
    {
        if (!open_) {
            promise_.set_value();
            open_ = true;
        }
    }

    [[nodiscard]] std::shared_future<void> future() const
    {
        return future_;
    }

private:
    std::promise<void> promise_;
    std::shared_future<void> const future_ = promise_.get_future().share();
    bool open_ = false;
};

TEST(FetchOptionsTest, effectiveMaxFileSize)
{
    auto options = tr_web::FetchOptions{ "http://example.invalid/"s, nullptr, nullptr };

    EXPECT_EQ(TrWebDefaultMaxBodyBytes, options.effective_max_file_size());

    options.max_file_size = 123U;
    EXPECT_EQ(123U, options.effective_max_file_size());
    EXPECT_NE(TrWebDefaultMaxBodyBytes, options.effective_max_file_size());

    options.range = std::make_pair(uint64_t{ 100 }, uint64_t{ 199 });
    EXPECT_EQ(100U, options.effective_max_file_size());
    EXPECT_NE(TrWebDefaultMaxBodyBytes, options.effective_max_file_size());
}

class TestMediator final : public tr_web::Mediator
{
public:
    [[nodiscard]] tr_web::BindAddresses bind_addresses() const override
    {
        auto const lock = std::scoped_lock{ mutex_ };
        return bind_addresses_;
    }

    void setBindAddress(tr_address_type const type, std::optional<std::string> address)
    {
        auto const lock = std::scoped_lock{ mutex_ };
        bind_addresses_[type] = std::move(address);
    }

    [[nodiscard]] std::optional<std::string_view> userAgent() const override
    {
        if (user_agent_) {
            return std::string_view{ *user_agent_ };
        }
        return std::nullopt;
    }

    std::optional<std::string> user_agent_;

private:
    mutable std::mutex mutex_;
    tr_web::BindAddresses bind_addresses_;
};

class WebTest : public ::tr::test::SandboxedTest
{
protected:
    void SetUp() override
    {
        ::tr::test::SandboxedTest::SetUp();
        if (!server4_) {
            GTEST_SKIP() << "IPv4 loopback is unavailable";
        }
    }

    // Run a fetch asynchronously and return a future for the response.
    std::future<tr_web::FetchResponse> fetchAsync(tr_web::FetchOptions options)
    {
        return fetchAsync(*web_, std::move(options));
    }

    // Run a fetch synchronously and return the response. done_func is invoked
    // from tr_web's own thread, so hand the result back through a promise.
    tr_web::FetchResponse fetch(tr_web::FetchOptions options)
    {
        return fetch(*web_, std::move(options));
    }

    static std::future<tr_web::FetchResponse> fetchAsync(tr_web& web, tr_web::FetchOptions options)
    {
        auto promise = std::make_shared<std::promise<tr_web::FetchResponse>>();
        auto future = promise->get_future();
        options.done_func = [promise](tr_web::FetchResponse const& response) {
            promise->set_value(response);
        };

        web.fetch(std::move(options));

        return future;
    }

    static tr_web::FetchResponse fetch(tr_web& web, tr_web::FetchOptions options)
    {
        auto future = fetchAsync(web, std::move(options));
        if (future.wait_for(15s) != std::future_status::ready) {
            EXPECT_TRUE(false) << "fetch did not complete";
            return {};
        }
        return future.get();
    }

    [[nodiscard]] tr_web::FetchOptions options(std::string_view path = "/"sv, void* user_data = nullptr) const
    {
        auto options = tr_web::FetchOptions{ server4_->url(path), nullptr, user_data };
        options.max_file_size = TestMaxFileSize;
        return options;
    }

    std::future<void> pauseWebThread(std::shared_future<void> resume)
    {
        auto paused = std::make_shared<std::promise<void>>();
        auto future = paused->get_future();
        auto opts = options();
        opts.done_func = [paused, resume = std::move(resume)](tr_web::FetchResponse const& response) {
            EXPECT_EQ(200, response.status);
            paused->set_value();
            resume.wait();
        };
        web_->fetch(std::move(opts));
        return future;
    }

    TestMediator mediator_;
    std::unique_ptr<tr_web> web_ = tr_web::create(mediator_);
    std::unique_ptr<LoopbackServer> server4_ = LoopbackServer::create(TR_AF_INET);
    std::unique_ptr<LoopbackServer> server6_ = LoopbackServer::create(TR_AF_INET6);

    static auto constexpr TestMaxFileSize = 128U;
};

class WebTestIPv6 : public WebTest
{
protected:
    void SetUp() override
    {
        WebTest::SetUp();
        if (!server6_) {
            GTEST_SKIP() << "IPv6 loopback is unavailable";
        }
    }

    [[nodiscard]] tr_web::FetchOptions options6(std::string_view path = "/"sv, void* user_data = nullptr) const
    {
        auto options = tr_web::FetchOptions{ server6_->url(path), nullptr, user_data };
        options.max_file_size = TestMaxFileSize;
        return options;
    }
};

TEST_F(WebTest, getReturnsBody)
{
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", "hello world"sv); });

    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("hello world"sv, response.body);
    EXPECT_TRUE(response.did_connect);
    EXPECT_FALSE(response.did_timeout);
    EXPECT_FALSE(response.errmsg);
    EXPECT_EQ("127.0.0.1"sv, response.primary_ip);
}

TEST_F(WebTest, requestUrlIsCaptured)
{
    static constexpr auto Path = "/request-url"sv;
    auto const response = fetch(options(Path));

    EXPECT_EQ(server4_->url(Path), response.request_url);
}

TEST_F(WebTest, noBodyIsGet)
{
    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("GET"sv, server4_->lastRequest().method);
}

TEST_F(WebTest, httpErrorStatusIsSurfaced)
{
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, 404, "Not Found", "nope"sv); });

    auto const response = fetch(options());
    EXPECT_EQ(404, response.status);
    EXPECT_TRUE(response.did_connect);
    EXPECT_FALSE(response.errmsg);
}

TEST_F(WebTest, curlErrorIsReported)
{
    auto const response = fetch(tr_web::FetchOptions{ "http://example.invalid", nullptr, nullptr });
    EXPECT_EQ(0, response.status);
    EXPECT_FALSE(response.did_timeout);
    ASSERT_TRUE(response.errmsg);
    EXPECT_NE(std::string::npos, response.errmsg->find("curl error: "));
}

TEST_F(WebTest, postSendsBody)
{
    auto opts = options();
    opts.body = "the request body";

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(200, response.status);

    auto const req = server4_->lastRequest();
    EXPECT_EQ("POST"sv, req.method);
    EXPECT_EQ("the request body"sv, req.body);
}

TEST_F(WebTest, requestHeadersAreSent)
{
    auto opts = options();
    opts.headers.insert_or_assign("X-Custom-Header", "custom-value");
    opts.headers.insert_or_assign("Content-Type", "application/json");

    fetch(std::move(opts));

    auto const req = server4_->lastRequest();
    EXPECT_EQ("custom-value"sv, req.headers.at("x-custom-header"));
    EXPECT_EQ("application/json"sv, req.headers.at("content-type"));
}

TEST_F(WebTest, responseHeadersAreCaptured)
{
    server4_->setHandler([](evhttp_request* req) {
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
    EXPECT_EQ(expected, server4_->lastRequest().headers.at("authorization"));
}

TEST_F(WebTest, basicAuthEmptyPassword)
{
    auto opts = options();
    opts.username = "user";
    // no password

    fetch(std::move(opts));

    auto const expected = fmt::format("Basic {:s}", tr_base64_encode("user:"sv));
    EXPECT_EQ(expected, server4_->lastRequest().headers.at("authorization"));
}

TEST_F(WebTest, netrcCredentialsAreSent)
{
    auto const netrc_path = fmt::format("{:s}/netrc", sandboxDir());
    createFileWithContents(netrc_path, "machine 127.0.0.1 login aladdin password opensesame\n"sv);

    auto opts = options();
    opts.netrc_file = netrc_path;

    fetch(std::move(opts));

    auto const expected = fmt::format("Basic {:s}", tr_base64_encode("aladdin:opensesame"sv));
    EXPECT_EQ(expected, server4_->lastRequest().headers.at("authorization"));
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
    auto const& headers = server4_->lastRequest().headers;
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
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, 206, "Partial Content", "data"sv); });

    auto opts = options();
    opts.range = std::make_pair(uint64_t{ 0 }, uint64_t{ 3 });

    auto const response = fetch(std::move(opts));

    EXPECT_EQ(206, response.status);
    EXPECT_FALSE(response.errmsg);
    EXPECT_EQ("data"sv, response.body);
    EXPECT_EQ("bytes=0-3"sv, server4_->lastRequest().headers.at("range"));
}

TEST_F(WebTest, rangeLengthTakesPrecedenceOverMaxFileSize)
{
    static auto constexpr Body = "data"sv;
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, 206, "Partial Content", Body); });

    auto opts = options();
    opts.max_file_size = 1U;
    opts.range = std::make_pair(uint64_t{ 0 }, uint64_t{ 3 });

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(206, response.status);
    EXPECT_FALSE(response.errmsg);
    EXPECT_EQ(Body, response.body);
}

TEST_F(WebTest, cookiesAreSent)
{
    auto opts = options();
    opts.cookies = "a=b; c=d";

    fetch(std::move(opts));

    EXPECT_EQ("a=b; c=d"sv, server4_->lastRequest().headers.at("cookie"));
}

TEST_F(WebTest, userAgentFromMediatorIsSent)
{
    // the mediator's user agent is read once when the tr_web is created, so
    // set it before building a dedicated tr_web for this test
    mediator_.user_agent_ = "TestAgent/1.0";
    auto web = tr_web::create(mediator_);

    fetch(*web, options());

    EXPECT_EQ("TestAgent/1.0"sv, server4_->lastRequest().headers.at("user-agent"));
}

TEST_F(WebTest, bindAddressChangeLetsInFlightFetchesComplete)
{
    auto destination = LoopbackServer::create();
    if (!destination) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }
    destination->setHandler([&destination](evhttp_request* req) {
        LoopbackServer::reply(req, HTTP_OK, "OK", std::to_string(destination->lastRequest().peer_port));
    });
    auto gate = ThreadGate{};
    auto started = std::make_shared<std::promise<void>>();
    auto started_future = started->get_future();
    server4_->setHandler([started, resume = gate.future(), target = destination->url()](evhttp_request* req) {
        started->set_value();
        resume.wait();
        LoopbackServer::reply(req, HTTP_MOVETEMP, "Found", {}, { { { "Location", target } } });
    });

    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    auto first_fetch = fetchAsync(options("/slow"sv));
    ASSERT_EQ(std::future_status::ready, started_future.wait_for(15s));

    mediator_.setBindAddress(TR_AF_INET, std::nullopt);
    auto const second_response = fetch({ destination->url(), nullptr, nullptr });
    ASSERT_EQ(200, second_response.status);

    gate.open();
    ASSERT_EQ(std::future_status::ready, first_fetch.wait_for(15s));
    auto const first_response = first_fetch.get();

    EXPECT_EQ(200, first_response.status);
    EXPECT_FALSE(first_response.errmsg);
    EXPECT_NE(second_response.body, first_response.body);

    auto const third_response = fetch({ destination->url(), nullptr, nullptr });
    EXPECT_EQ(200, third_response.status);
    EXPECT_EQ(second_response.body, third_response.body);
}

TEST_F(WebTest, connectionsNotReusedAcrossBindAddresses)
{
    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_EQ("127.0.0.1"sv, response.primary_ip);

    mediator_.setBindAddress(TR_AF_INET, "192.0.2.1");

    auto const failure = fetch(options());
    EXPECT_EQ(0, failure.status);
    EXPECT_FALSE(failure.did_connect);
    ASSERT_TRUE(failure.errmsg);
    EXPECT_NE(std::string::npos, failure.errmsg->find("curl error: "));
}

TEST_F(WebTest, bindAddressesCapturedWhenQueued)
{
    auto destination = LoopbackServer::create();
    if (!destination) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }
    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    auto gate = ThreadGate{};
    auto paused = pauseWebThread(gate.future());
    ASSERT_EQ(std::future_status::ready, paused.wait_for(15s));

    auto response = fetchAsync({ destination->url(), nullptr, nullptr });
    mediator_.setBindAddress(TR_AF_INET, "192.0.2.1");
    gate.open();

    ASSERT_EQ(std::future_status::ready, response.wait_for(15s));
    EXPECT_EQ(200, response.get().status);
    EXPECT_EQ("127.0.0.1", destination->lastRequest().peer_address);
}

TEST_F(WebTestIPv6, connectionsPartitionedWhenMatchingFamilyBindAddressChanges)
{
    ASSERT_EQ(200, fetch(options()).status);
    auto const ipv4_unbound = server4_->lastRequest();
    ASSERT_EQ(200, fetch(options()).status);
    EXPECT_EQ(ipv4_unbound.peer_port, server4_->lastRequest().peer_port);

    ASSERT_EQ(200, fetch(options6()).status);
    auto const ipv6_unbound = server6_->lastRequest();
    ASSERT_EQ(200, fetch(options6()).status);
    EXPECT_EQ(ipv6_unbound.peer_port, server6_->lastRequest().peer_port);

    mediator_.setBindAddress(TR_AF_INET6, "::1");
    ASSERT_EQ(200, fetch(options6()).status);
    auto const ipv6_bound = server6_->lastRequest();
    EXPECT_EQ("::1", ipv6_bound.peer_address);
    EXPECT_NE(ipv6_unbound.peer_port, ipv6_bound.peer_port);

    ASSERT_EQ(200, fetch(options()).status);
    auto const ipv4_unbound_v6_bound = server4_->lastRequest();

    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    ASSERT_EQ(200, fetch(options()).status);
    auto const bound = server4_->lastRequest();
    EXPECT_EQ("127.0.0.1", bound.peer_address);
    EXPECT_NE(ipv4_unbound_v6_bound.peer_port, bound.peer_port);

    mediator_.setBindAddress(TR_AF_INET, std::nullopt);
    ASSERT_EQ(200, fetch(options()).status);
    EXPECT_NE(bound.peer_port, server4_->lastRequest().peer_port);
}

TEST_F(WebTest, mixedQueuedBindSnapshotsComplete)
{
    auto gate = ThreadGate{};
    auto paused = pauseWebThread(gate.future());
    ASSERT_EQ(std::future_status::ready, paused.wait_for(15s));

    static auto constexpr Addresses = std::array{ "127.0.0.1", "192.0.2.1", "127.0.0.1", "192.0.2.1" };
    auto futures = std::array<std::future<tr_web::FetchResponse>, Addresses.size()>{};
    for (size_t i = 0; i < Addresses.size(); ++i) {
        mediator_.setBindAddress(TR_AF_INET, Addresses[i]);
        futures[i] = fetchAsync(options());
    }
    gate.open();

    for (size_t i = 0; i < futures.size(); ++i) {
        ASSERT_EQ(std::future_status::ready, futures[i].wait_for(15s));
        auto const response = futures[i].get();
        EXPECT_EQ(i % 2 == 0 ? 200 : 0, response.status);
        EXPECT_EQ(i % 2 != 0, response.errmsg.has_value());
    }
}

TEST_F(WebTest, bindAddressUsesResolvedIPv4Family)
{
    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    mediator_.setBindAddress(TR_AF_INET6, "2001:db8::1");

    EXPECT_EQ(200, fetch(options()).status);
    EXPECT_EQ("127.0.0.1", server4_->lastRequest().peer_address);
}

TEST_F(WebTestIPv6, bindAddressUsesResolvedIPv6Family)
{
    mediator_.setBindAddress(TR_AF_INET, "192.0.2.1");
    mediator_.setBindAddress(TR_AF_INET6, "::1");

    EXPECT_EQ(200, fetch(options6()).status);
    EXPECT_EQ("::1", server6_->lastRequest().peer_address);
}

TEST_F(WebTest, bindSnapshotSurvivesRedirect)
{
    auto destination = LoopbackServer::create();
    if (!destination) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }
    mediator_.setBindAddress(TR_AF_INET, "127.0.0.1");
    server4_->setHandler([this, target = destination->url()](evhttp_request* req) {
        mediator_.setBindAddress(TR_AF_INET, "192.0.2.1");
        LoopbackServer::reply(req, HTTP_MOVETEMP, "Found", {}, { { { "Location", target } } });
    });

    EXPECT_EQ(200, fetch(options()).status);
    EXPECT_EQ("127.0.0.1", destination->lastRequest().peer_address);
}

TEST_F(WebTest, onDataReceivedReportsByteCount)
{
    static auto constexpr Body = "0123456789abcdef"sv;
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Body); });

    auto total = std::size_t{ 0 };
    auto opts = options();
    opts.on_data_received = [&total](std::size_t n_bytes) {
        total += n_bytes;
    };

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(std::size(Body), std::size(response.body));
    EXPECT_EQ(std::size(Body), total);
}

TEST_F(WebTest, responseBodyLimitReportsCurlError)
{
    static auto constexpr Body = "0123456789"sv;
    static auto constexpr MaxFileSize = std::size_t{ 4 };
    server4_->setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Body); });

    auto opts = options();
    opts.max_file_size = MaxFileSize;

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(200, response.status);
    EXPECT_TRUE(response.did_connect);
    ASSERT_TRUE(response.errmsg);
    EXPECT_FALSE(std::empty(*response.errmsg));
    EXPECT_LT(std::size(response.body), std::size(Body));
    EXPECT_LE(std::size(response.body), MaxFileSize);
}

TEST_F(WebTest, defaultResponseBodyLimitReportsCurlError)
{
    auto body = std::string(TestMaxFileSize + 1U, 'x');
    server4_->setHandler([&body](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", body); });

    auto const response = fetch(options());
    EXPECT_EQ(200, response.status);
    EXPECT_TRUE(response.did_connect);
    ASSERT_TRUE(response.errmsg);
    EXPECT_FALSE(std::empty(*response.errmsg));
    EXPECT_LE(std::size(response.body), TestMaxFileSize);
}

TEST_F(WebTest, zeroMaxFileSizeDisablesLimit)
{
    auto body = std::string(TestMaxFileSize + 1U, 'x');
    server4_->setHandler([&body](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", body); });

    auto opts = options();
    opts.max_file_size = 0U;

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(200, response.status);
    EXPECT_FALSE(response.errmsg);
    EXPECT_EQ(std::size(body), std::size(response.body));
}

TEST_F(WebTest, timeoutIsReported)
{
    // Handler that never replies, so the transfer exceeds the timeout.
    server4_->setHandler([](evhttp_request* /*req*/) {});

    auto opts = options();
    opts.timeout_secs = 1s;

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(0, response.status);
    ASSERT_TRUE(response.errmsg);
    EXPECT_TRUE(response.did_timeout) << *response.errmsg;
    EXPECT_NE(std::string::npos, response.errmsg->find("curl error: ")) << *response.errmsg;
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

TEST_F(WebTest, redirectBodyBiggerThanRangeDoesNotAbort)
{
    static auto constexpr Body = "0123456789"sv;
    static auto constexpr First = uint64_t{ 0 };
    static auto constexpr Last = uint64_t{ 3 };

    server4_->setHandler([this](evhttp_request* req) {
        if (auto const& path = server4_->lastRequest().uri; path.ends_with("/redirect"sv)) {
            static auto constexpr RedirectBody = "Moved permanently to /bigfile"sv;
            static_assert(std::size(RedirectBody) > Last + 1U - First);
            LoopbackServer::reply(
                req,
                HTTP_MOVEPERM,
                "Moved Permanently",
                RedirectBody,
                { { { "Location", server4_->url("/bigfile") } } });
        } else if (path.ends_with("/bigfile"sv)) {
            auto const range = server4_->lastRequest().headers.at("range");
            auto constexpr Prefix = "bytes="sv;
            if (!range.starts_with(Prefix)) {
                LoopbackServer::reply(req, HTTP_BADREQUEST, "Bad Request", {});
                return;
            }

            auto const range_values = std::string_view{ range }.substr(Prefix.size());
            auto const separator = range_values.find('-');
            if (separator == std::string_view::npos) {
                LoopbackServer::reply(req, HTTP_BADREQUEST, "Bad Request", {});
                return;
            }

            auto first_remainder = std::string_view{};
            auto const first = tr_num_parse<uint64_t>(range_values.substr(0, separator), &first_remainder);
            auto last_remainder = std::string_view{};
            auto const last = tr_num_parse<uint64_t>(range_values.substr(separator + 1U), &last_remainder);
            if (!first || !last || !first_remainder.empty() || !last_remainder.empty() || *last < *first) {
                LoopbackServer::reply(req, HTTP_BADREQUEST, "Bad Request", {});
                return;
            }

            LoopbackServer::reply(req, 206, "Partial Content", Body.substr(*first, *last + 1U - *first));
        } else {
            LoopbackServer::reply(req, HTTP_NOTFOUND, "Not Found", ""sv);
        }
    });

    auto opts = options("/redirect");
    opts.range = std::make_pair(First, Last);

    auto const response = fetch(std::move(opts));
    EXPECT_EQ(206, response.status);
    EXPECT_TRUE(response.did_connect);
    EXPECT_FALSE(response.errmsg) << *response.errmsg;
    EXPECT_EQ(Body.substr(First, Last + 1U - First), response.body);
}

} // namespace
