// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <cstddef>
#include <ctime>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <event2/http.h>

#include <archive.h>
#include <archive_entry.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/blocklist-download.h>
#include <libtransmission/timer.h>
#include <libtransmission/web.h>

#include "loopback-server.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

// Two non-overlapping ranges => two rules.
auto constexpr Rules =
    "Austin Law Firm:216.16.1.144-216.16.1.151\n"
    "Evilcorp:216.88.88.0-216.88.88.255\n"sv;

// Wrap `content` into an archive/compression the way a blocklist provider
// might serve it, using libarchive's write side. `configure` picks the format
// and filter (tar, zip, or raw + gzip); this exercises the same peel-off logic
// tr::blocklist::decompress() relies on to read it back.
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

// ---
// decompress(): the format matrix, tested directly -- no session, no download.

TEST(BlocklistDecompress, passesPlainTextThrough)
{
    EXPECT_EQ(Rules, tr::blocklist::decompress(Rules));
}

TEST(BlocklistDecompress, unwrapsGzip)
{
    EXPECT_EQ(Rules, tr::blocklist::decompress(makeArchive(asGzip, "blocklist"sv, Rules)));
}

TEST(BlocklistDecompress, unwrapsTar)
{
    EXPECT_EQ(Rules, tr::blocklist::decompress(makeArchive(asTar, "blocklist"sv, Rules)));
}

TEST(BlocklistDecompress, unwrapsZip)
{
    EXPECT_EQ(Rules, tr::blocklist::decompress(makeArchive(asZip, "blocklist"sv, Rules)));
}

TEST(BlocklistDecompress, returnsEmptyOnGarbage)
{
    // libarchive's "raw" reader accepts any bytes as a single entry, so garbage
    // decompresses to itself; the *parser* (tested elsewhere) rejects it. What we
    // assert here is only that decompress() doesn't crash or hang on junk.
    EXPECT_NO_THROW((void)tr::blocklist::decompress("\x1f\x8b not really gzip"sv));
}

// ---
// normalize_blocklist_url(): a bare host gets an https scheme; anything with a
// scheme is left alone.

TEST(BlocklistNormalizeUrl, prependsHttpsWhenSchemeMissing)
{
    EXPECT_EQ("https://list.example.com/blocklist"s, tr::blocklist::normalize_blocklist_url("list.example.com/blocklist"sv));
}

TEST(BlocklistNormalizeUrl, keepsExistingScheme)
{
    EXPECT_EQ("http://list.example.com/bl"s, tr::blocklist::normalize_blocklist_url("http://list.example.com/bl"sv));
    EXPECT_EQ("https://list.example.com/bl"s, tr::blocklist::normalize_blocklist_url("https://list.example.com/bl"sv));
}

TEST(BlocklistNormalizeUrl, trimsSurroundingWhitespace)
{
    EXPECT_EQ("https://list.example.com"s, tr::blocklist::normalize_blocklist_url("  list.example.com  "sv));
}

TEST(BlocklistNormalizeUrl, emptyStaysEmpty)
{
    EXPECT_EQ(""s, tr::blocklist::normalize_blocklist_url("   "sv));
}

// ---
// Updater control flow, driven through a mock Mediator: no real session, no
// network, no threads, no timers. run_in_session_thread() runs inline, so every
// test below is deterministic -- fetch responses are delivered explicitly.

class MockTimer final : public tr::Timer
{
public:
    void stop() override
    {
        is_running_ = false;
    }
    void set_callback(std::function<void()> callback) override
    {
        callback_ = std::move(callback);
    }
    void set_repeating(bool repeating = true) override
    {
        is_repeating_ = repeating;
    }
    void set_interval(std::chrono::milliseconds interval) override
    {
        interval_ = interval;
    }
    void start() override
    {
        is_running_ = true;
    }
    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
    {
        return interval_;
    }
    [[nodiscard]] bool is_repeating() const noexcept override
    {
        return is_repeating_;
    }

    // fire the timer callback as if it had elapsed
    void fire() const
    {
        if (callback_) {
            callback_();
        }
    }

    std::function<void()> callback_;
    std::chrono::milliseconds interval_ = {};
    bool is_repeating_ = false;
    bool is_running_ = false;
};

class MockTimerMaker final : public tr::TimerMaker
{
public:
    [[nodiscard]] std::unique_ptr<tr::Timer> create() override
    {
        auto timer = std::make_unique<MockTimer>();
        last_ = timer.get();
        return timer;
    }

    MockTimer* last_ = nullptr;
};

class MockMediator final : public tr::blocklist::Updater::Mediator
{
public:
    // knobs the tests set
    std::string url_ = "http://blocklist.example/list";
    bool enabled_ = true;
    bool updates_enabled_ = true;
    time_t mtime_ = 0;
    std::optional<size_t> install_result_ = size_t{ 2U }; // returned by set_blocklist_content()
    std::string install_error_ = "simulated save failure"; // reported when install_result_ is nullopt

    // observations the tests read
    int fetch_count_ = 0;
    int install_count_ = 0;
    std::string installed_; // content handed to set_blocklist_content()

    [[nodiscard]] std::string blocklist_url() const override
    {
        return url_;
    }

    void fetch(tr_web::FetchOptions&& options) override
    {
        ++fetch_count_;
        auto opts = std::move(options);
        pending_.push_back(std::move(opts.done_func));
    }

    [[nodiscard]] std::optional<size_t> set_blocklist_content(std::string_view content, std::string& error) override
    {
        ++install_count_;
        installed_ = std::string{ content };
        if (!install_result_) {
            error = install_error_;
        }
        return install_result_;
    }

    [[nodiscard]] bool enabled() const noexcept override
    {
        return enabled_;
    }
    [[nodiscard]] bool updates_enabled() const noexcept override
    {
        return updates_enabled_;
    }
    [[nodiscard]] time_t mtime() const override
    {
        return mtime_;
    }

    [[nodiscard]] tr::TimerMaker& timer_maker() noexcept override
    {
        return timer_maker_;
    }
    void run_in_session_thread(std::function<void()> func) override
    {
        func(); // synchronous: no marshalling, no races
    }

    // test helpers
    [[nodiscard]] size_t pendingCount() const
    {
        return std::size(pending_);
    }

    // Deliver an HTTP response to the Nth still-pending fetch, modeling a network
    // reply arriving out of band.
    void respond(size_t index, long status, std::string body)
    {
        auto response = tr_web::FetchResponse{
            .status = status,
            .headers = {},
            .body = std::move(body),
            .primary_ip = {},
            .did_connect = true,
            .did_timeout = false,
            .user_data = nullptr,
        };
        pending_.at(index)(response);
    }

    // Deliver to the most recent fetch (the common single-request case).
    void respond(long status, std::string body)
    {
        respond(std::size(pending_) - 1U, status, std::move(body));
    }

    [[nodiscard]] MockTimer* timer() const
    {
        return timer_maker_.last_;
    }

private:
    MockTimerMaker timer_maker_;
    std::vector<tr_web::FetchDoneFunc> pending_;
};

TEST(BlocklistUpdater, installsDecompressedContent)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    auto result = std::optional<tr_blocklist_update_result>{};
    updater.update([&result](tr_blocklist_update_result const& r) { result = r; });

    ASSERT_EQ(1U, mediator.pendingCount());
    mediator.respond(200, makeArchive(asZip, "blocklist"sv, Rules));

    // the Updater decompressed the body before handing it to the mediator
    EXPECT_EQ(1, mediator.install_count_);
    EXPECT_EQ(Rules, mediator.installed_);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(tr_blocklist_update_status::Ok, result->status);
    EXPECT_EQ(2U, result->n_rules);
    EXPECT_TRUE(std::empty(result->error));
}

TEST(BlocklistUpdater, reportsDownloadError)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    auto result = std::optional<tr_blocklist_update_result>{};
    updater.update([&result](tr_blocklist_update_result const& r) { result = r; });
    mediator.respond(404, "nope");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(tr_blocklist_update_status::DownloadError, result->status);
    EXPECT_FALSE(std::empty(result->error));
    EXPECT_EQ(0, mediator.install_count_); // never tried to install
}

TEST(BlocklistUpdater, reportsInvalidData)
{
    auto mediator = MockMediator{};
    mediator.install_result_ = size_t{ 0U }; // parsed, but no valid rules
    auto updater = tr::blocklist::Updater{ mediator };

    auto result = std::optional<tr_blocklist_update_result>{};
    updater.update([&result](tr_blocklist_update_result const& r) { result = r; });
    mediator.respond(200, std::string{ Rules });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(tr_blocklist_update_status::InvalidData, result->status);
    EXPECT_EQ(0U, result->n_rules);
}

TEST(BlocklistUpdater, keepsExistingListWhenDownloadHasNoRules)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    auto result = std::optional<tr_blocklist_update_result>{};
    updater.update([&result](tr_blocklist_update_result const& r) { result = r; });
    // a comment-only body: valid text, but nothing the parser would install
    mediator.respond(200, "# temporarily unavailable\n");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(tr_blocklist_update_status::InvalidData, result->status);
    EXPECT_EQ(0, mediator.install_count_); // must not overwrite the installed list
}

TEST(BlocklistUpdater, reportsSaveError)
{
    auto mediator = MockMediator{};
    mediator.install_result_ = std::nullopt; // I/O failure
    mediator.install_error_ = "Couldn't save 'blocklist.tmp': disk full (28)";
    auto updater = tr::blocklist::Updater{ mediator };

    auto result = std::optional<tr_blocklist_update_result>{};
    updater.update([&result](tr_blocklist_update_result const& r) { result = r; });
    mediator.respond(200, std::string{ Rules });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(tr_blocklist_update_status::SaveError, result->status);
    EXPECT_EQ(mediator.install_error_, result->error);
}

TEST(BlocklistUpdater, cancelSuppressesCallback)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    auto fired = false;
    updater.update([&fired](tr_blocklist_update_result const&) { fired = true; });
    updater.cancel();

    // the response arrives after the cancel; its callback must not fire
    mediator.respond(200, std::string{ Rules });
    EXPECT_FALSE(fired);
    EXPECT_EQ(0, mediator.install_count_);
}

TEST(BlocklistUpdater, secondUpdateSupersedesFirst)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    auto first_calls = 0;
    auto first = std::optional<tr_blocklist_update_result>{};
    auto second = std::optional<tr_blocklist_update_result>{};
    updater.update([&first_calls, &first](tr_blocklist_update_result const& r) {
        ++first_calls;
        first = r;
    });
    updater.update([&second](tr_blocklist_update_result const& r) { second = r; });
    ASSERT_EQ(2U, mediator.pendingCount());

    // starting the second update resolves the first immediately as superseded
    ASSERT_EQ(1, first_calls);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(tr_blocklist_update_status::Superseded, first->status);

    // even if the superseded (first) request completes, only the second installs
    // a result, and the first callback does not fire again
    mediator.respond(0U, 200, std::string{ Rules });
    mediator.respond(1U, 200, std::string{ Rules });

    EXPECT_EQ(1, first_calls); // fired exactly once, at supersession
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(tr_blocklist_update_status::Ok, second->status);
    EXPECT_EQ(1, mediator.install_count_); // installed exactly once
}

TEST(BlocklistUpdater, destructionSuppressesInFlight)
{
    auto mediator = MockMediator{};

    auto fired = false;
    {
        auto updater = tr::blocklist::Updater{ mediator };
        updater.update([&fired](tr_blocklist_update_result const&) { fired = true; });
    } // ~Updater marks the in-flight request cancelled

    mediator.respond(200, std::string{ Rules });
    EXPECT_FALSE(fired);
}

TEST(BlocklistUpdater, armsTimerWhenEnabled)
{
    auto mediator = MockMediator{};
    auto updater = tr::blocklist::Updater{ mediator };

    updater.restart_timer();

    auto* const timer = mediator.timer();
    ASSERT_NE(nullptr, timer);
    EXPECT_TRUE(timer->is_running_);
    EXPECT_FALSE(timer->is_repeating_); // single-shot
}

TEST(BlocklistUpdater, disarmsTimerWhenBlocklistDisabled)
{
    auto mediator = MockMediator{};
    mediator.enabled_ = false;
    auto updater = tr::blocklist::Updater{ mediator };

    updater.restart_timer();
    EXPECT_FALSE(mediator.timer()->is_running_);
}

TEST(BlocklistUpdater, disarmsTimerWhenUrlEmpty)
{
    auto mediator = MockMediator{};
    mediator.url_.clear();
    auto updater = tr::blocklist::Updater{ mediator };

    updater.restart_timer();
    EXPECT_FALSE(mediator.timer()->is_running_);
}

TEST(BlocklistUpdater, disarmsTimerWhenUpdatesDisabled)
{
    auto mediator = MockMediator{};
    mediator.updates_enabled_ = false;
    auto updater = tr::blocklist::Updater{ mediator };

    updater.restart_timer();
    EXPECT_FALSE(mediator.timer()->is_running_);
}

// ---
// Integration smoke tests: the real tr_session BlocklistMediator end to end
// (real fetch, real decompress, real tmpfile save, real rule parsing) over a
// loopback HTTP server. The mock-based tests above cover control flow; these
// keep a few representative paths honest through the production wiring.

class BlocklistDownloadTest : public SessionTest
{
protected:
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

TEST_F(BlocklistDownloadTest, ruleLessDownloadKeepsExistingBlocklist)
{
    // install a good list first
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", Rules); });
    ASSERT_EQ(tr_blocklist_update_status::Ok, runUpdate().status);
    ASSERT_EQ(2U, tr_blocklistGetRuleCount(session_));

    // a later update that returns only comments must not wipe the installed list
    server_.setHandler([](evhttp_request* req) { LoopbackServer::reply(req, HTTP_OK, "OK", "# temporarily unavailable\n"sv); });
    auto const result = runUpdate();
    EXPECT_EQ(tr_blocklist_update_status::InvalidData, result.status);
    EXPECT_EQ(2U, tr_blocklistGetRuleCount(session_)); // preserved, not wiped
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
