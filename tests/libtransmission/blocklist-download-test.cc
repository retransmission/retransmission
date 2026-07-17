// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <event2/http.h>

#include <archive.h>
#include <archive_entry.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include "loopback-server.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

// Wrap `content` into an archive/compression the way a blocklist provider
// might serve it, using libarchive's write side. `configure` picks the format
// and filter (tar, zip, or raw + gzip); this exercises the same peel-off logic
// tr_blocklistUpdate() relies on to read it back.
[[nodiscard]] std::string makeArchive(void (*configure)(archive*), std::string_view name, std::string_view content)
{
    auto* const arc = archive_write_new();
    configure(arc);

    // our fixtures are tiny; this buffer is comfortably large enough
    auto out = std::string{};
    out.resize(size_t{ 64U } * 1024U);
    auto used = size_t{};
    archive_write_open_memory(arc, std::data(out), std::size(out), &used);

    auto* const entry = archive_entry_new();
    archive_entry_set_pathname(entry, std::string{ name }.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, static_cast<la_int64_t>(std::size(content)));
    archive_write_header(arc, entry);
    archive_write_data(arc, std::data(content), std::size(content));
    archive_entry_free(entry);

    archive_write_close(arc);
    archive_write_free(arc);

    out.resize(used);
    return out;
}

void asGzip(archive* arc)
{
    archive_write_set_format_raw(arc);
    archive_write_add_filter_gzip(arc);
}

void asTar(archive* arc)
{
    archive_write_set_format_ustar(arc);
}

void asZip(archive* arc)
{
    archive_write_set_format_zip(arc);
}

class BlocklistDownloadTest : public SessionTest
{
protected:
    // Two non-overlapping ranges => two rules.
    static auto constexpr Rules =
        "Austin Law Firm:216.16.1.144-216.16.1.151\n"
        "Evilcorp:216.88.88.0-216.88.88.255\n"sv;

    // Point the session's blocklist URL at the loopback server, kick off an
    // update, and wait for it to finish. tr_blocklistUpdate() invokes its
    // callback on the session thread, so hand the result back via a promise.
    tr_blocklist_update_result runUpdate(std::string_view path = "/blocklist"sv)
    {
        tr_blocklistSetURL(session_, server_.url(path));

        auto promise = std::make_shared<std::promise<tr_blocklist_update_result>>();
        auto future = promise->get_future();
        tr_blocklistUpdate(session_, [promise](tr_blocklist_update_result const& result) { promise->set_value(result); });

        if (future.wait_for(15s) != std::future_status::ready) {
            EXPECT_TRUE(false) << "blocklist update did not complete";
            return {};
        }
        return future.get();
    }

    LoopbackServer server_;
};

TEST_F(BlocklistDownloadTest, installsPlainText)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Rules); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
    EXPECT_TRUE(std::empty(result.error));
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_));
}

TEST_F(BlocklistDownloadTest, installsGzip)
{
    static auto const Compressed = makeArchive(asGzip, "blocklist"sv, Rules);
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Compressed); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_));
}

TEST_F(BlocklistDownloadTest, installsTar)
{
    static auto const Tarred = makeArchive(asTar, "blocklist"sv, Rules);
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Tarred); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_));
}

TEST_F(BlocklistDownloadTest, installsZip)
{
    static auto const Zipped = makeArchive(asZip, "blocklist"sv, Rules);
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Zipped); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_));
}

TEST_F(BlocklistDownloadTest, reportsDownloadError)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, 404, "Not Found", "nope"sv); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::DownloadError, result.status);
    EXPECT_EQ(0U, result.n_rules);
    EXPECT_FALSE(std::empty(result.error));
}

TEST_F(BlocklistDownloadTest, reportsInvalidData)
{
    server_.setHandler(
        [](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", "this is not a blocklist\ngarbage\n"sv); });

    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::InvalidData, result.status);
    EXPECT_EQ(0U, result.n_rules);
}

TEST_F(BlocklistDownloadTest, updatesMTimeOnSuccess)
{
    EXPECT_EQ(0, tr_blocklistGetMTime(session_));

    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Rules); });
    auto const result = runUpdate();
    ASSERT_EQ(tr_blocklist_update_status::Ok, result.status);

    EXPECT_GT(tr_blocklistGetMTime(session_), 0);
}

TEST_F(BlocklistDownloadTest, cancelSuppressesCallback)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Rules); });
    tr_blocklistSetURL(session_, server_.url("/blocklist"sv));

    // Kick off an update and immediately cancel it. update() and cancel() are
    // both marshalled to the session thread in call order, so `cancelled` is set
    // there before the (slower) network response is processed.
    auto callback_fired = std::make_shared<std::atomic<bool>>(false);
    tr_blocklistUpdate(
        session_,
        [callback_fired](tr_blocklist_update_result const&) { callback_fired->store(true, std::memory_order_release); });
    tr_blocklistUpdateCancel(session_);

    // Cancellation suppresses the callback, not the HTTP transfer, so wait for
    // the request to actually reach the server, then give finish_request() a
    // moment to run on the session thread -- a non-suppressed callback would
    // fire in that window.
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (std::empty(server_.lastRequest().method) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    ASSERT_FALSE(std::empty(server_.lastRequest().method)) << "request never reached the server";
    std::this_thread::sleep_for(250ms);

    EXPECT_FALSE(callback_fired->load(std::memory_order_acquire));
    EXPECT_EQ(0U, tr_blocklistGetRuleCount(session_)); // nothing was installed

    // The updater is still usable afterward: a fresh update completes normally.
    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
}

TEST_F(BlocklistDownloadTest, secondUpdateSupersedesFirst)
{
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Rules); });
    tr_blocklistSetURL(session_, server_.url("/blocklist"sv));

    // Start one update, then immediately start another. The second supersedes
    // the first, so only the second's callback should fire.
    auto first_fired = std::make_shared<std::atomic<bool>>(false);
    tr_blocklistUpdate(
        session_,
        [first_fired](tr_blocklist_update_result const&) { first_fired->store(true, std::memory_order_release); });

    auto promise = std::make_shared<std::promise<tr_blocklist_update_result>>();
    auto future = promise->get_future();
    tr_blocklistUpdate(session_, [promise](tr_blocklist_update_result const& result) { promise->set_value(result); });

    ASSERT_EQ(std::future_status::ready, future.wait_for(15s));
    auto const result = future.get();
    EXPECT_EQ(tr_blocklist_update_status::Ok, result.status);
    EXPECT_EQ(2U, result.n_rules);
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_)); // installed exactly once

    // Give the superseded request's response time to arrive; its callback must
    // not fire.
    std::this_thread::sleep_for(250ms);
    EXPECT_FALSE(first_fired->load(std::memory_order_acquire));
}

} // namespace
} // namespace tr::test
