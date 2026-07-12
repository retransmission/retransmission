// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <string>
#include <string_view>

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

} // namespace
} // namespace tr::test
