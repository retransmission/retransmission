// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef> // size_t
#include <ctime> // time_t
#include <functional> // std::less
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <event2/http.h>

#include <fmt/format.h>

#include <libtransmission/transmission.h>
#include <libtransmission/types.h>

#include "loopback-server.h"
#include "test-fixtures.h"

namespace tr::test
{

// A fake HTTP tracker on a LoopbackServer.
// Each announce path acts as its own tracker, with its own reply and request count.
// The paths don't end in "announce", so the trackers have no scrape URL and are never scraped.
class LoopbackTracker
{
public:
    LoopbackTracker()
    {
        server_.setHandler([this](evhttp_request* req) { handle(req); });
    }

    ~LoopbackTracker()
    {
        releaseReplies();
    }

    LoopbackTracker(LoopbackTracker const&) = delete;
    LoopbackTracker(LoopbackTracker&&) = delete;
    LoopbackTracker& operator=(LoopbackTracker const&) = delete;
    LoopbackTracker& operator=(LoopbackTracker&&) = delete;

    [[nodiscard]] std::string url(std::string_view const path) const
    {
        return server_.url(path);
    }

    void setReply(std::string_view const path, time_t const interval, time_t const min_interval)
    {
        auto const lock = std::scoped_lock{ mutex_ };
        replies_.insert_or_assign(
            std::string{ path },
            Reply{ .interval = interval, .min_interval = min_interval, .failure_reason = {} });
    }

    // Replies with a failure reason and no intervals
    void setFailure(std::string_view const path, std::string_view const reason)
    {
        auto const lock = std::scoped_lock{ mutex_ };
        replies_.insert_or_assign(std::string{ path }, Reply{ .failure_reason = std::string{ reason } });
    }

    [[nodiscard]] size_t announceCount(std::string_view const path) const
    {
        auto const lock = std::scoped_lock{ mutex_ };
        auto const iter = counts_.find(path);
        return iter == std::end(counts_) ? 0U : iter->second;
    }

    // Holds every reply until releaseReplies(), keeping those announces in flight.
    void holdReplies()
    {
        auto const lock = std::scoped_lock{ mutex_ };
        holding_ = true;
    }

    void releaseReplies()
    {
        {
            auto const lock = std::scoped_lock{ mutex_ };
            holding_ = false;
        }
        released_.notify_all();
    }

private:
    struct Reply {
        time_t interval = 0; // 0 omits the key
        time_t min_interval = 0; // 0 omits the key
        std::string failure_reason; // nonempty makes the reply a failure
    };

    void handle(evhttp_request* const req)
    {
        auto const uri = std::string_view{ evhttp_request_get_uri(req) };
        auto const path = std::string{ uri.substr(0, uri.find('?')) };

        auto lock = std::unique_lock{ mutex_ };
        ++counts_[path];
        released_.wait_for(lock, 10s, [this]() { return !holding_; });
        auto const body = bencode(replies_[path]);
        lock.unlock();

        LoopbackServer::reply(req, HTTP_OK, "OK", body);
    }

    [[nodiscard]] static std::string bencode(Reply const& reply)
    {
        if (!std::empty(reply.failure_reason)) {
            return fmt::format("d14:failure reason{:d}:{:s}e", std::size(reply.failure_reason), reply.failure_reason);
        }

        auto benc = std::string{ "d8:completei1e10:downloadedi0e10:incompletei1e" };
        if (reply.interval != 0) {
            benc += fmt::format("8:intervali{:d}e", reply.interval);
        }
        if (reply.min_interval != 0) {
            benc += fmt::format("12:min intervali{:d}e", reply.min_interval);
        }
        benc += "5:peers0:e";
        return benc;
    }

    LoopbackServer server_;

    mutable std::mutex mutex_;
    std::condition_variable released_;
    std::map<std::string, Reply, std::less<>> replies_;
    std::map<std::string, size_t, std::less<>> counts_;
    bool holding_ = false;
};

// A session whose torrents announce to a LoopbackTracker
class LoopbackTrackerTest : public SessionTest
{
protected:
    // Adds a paused torrent with one tier per path
    [[nodiscard]] tr_torrent* addTorrent(std::initializer_list<std::string_view> const paths)
    {
        auto tracker_list = std::string{};
        for (auto const path : paths) {
            tracker_list += tracker_.url(path);
            tracker_list += "\n\n";
        }

        auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
        EXPECT_TRUE(tr_torrentSetTrackerList(tor, tracker_list));
        return tor;
    }

    // Adds and starts a torrent with one tier per path,
    // then waits until every tier has its first announce response
    [[nodiscard]] tr_torrent* startTorrent(std::initializer_list<std::string_view> const paths)
    {
        auto* const tor = addTorrent(paths);
        tr_torrentStart(tor);

        auto const all_announced = [this, tor]() {
            auto const views = trackerViews(tor);
            return std::ranges::all_of(views, [](auto const& view) { return view.hasAnnounced; });
        };
        EXPECT_TRUE(waitFor(all_announced, 10s));

        return tor;
    }

    // The tracker view of the tier that announces to `path`.
    // It is read on the session thread, so it reflects every manual update made before the call.
    [[nodiscard]] tr_tracker_view trackerView(tr_torrent* const tor, std::string_view const path)
    {
        auto const url = tracker_.url(path);
        for (auto const& view : trackerViews(tor)) {
            if (view.announce.sv() == url) {
                return view;
            }
        }

        ADD_FAILURE() << "no tracker for " << url;
        return {};
    }

    LoopbackTracker tracker_;

private:
    [[nodiscard]] std::vector<tr_tracker_view> trackerViews(tr_torrent* const tor)
    {
        return blockingRunInSessionThread([tor]() {
            auto views = std::vector<tr_tracker_view>{};
            for (size_t idx = 0U, n = tr_torrentTrackerCount(tor); idx < n; ++idx) {
                views.emplace_back(tr_torrentTracker(tor, idx));
            }
            return views;
        });
    }
};

} // namespace tr::test
