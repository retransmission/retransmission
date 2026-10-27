// This file Copyright (C) 2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <ctime>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h> // tr_rand_obj()
#include <libtransmission/log.h>
#include <libtransmission/net.h>
#include <libtransmission/session.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/timer.h>
#include <libtransmission/tr-lpd.h>
#include <libtransmission/types.h> // tr_torrent_activity

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{

using LpdTest = SessionTest;

namespace
{

class MyMediator final : public tr_lpd::Mediator
{
public:
    explicit MyMediator(tr_session& session)
        : session_{ session }
    {
    }

    [[nodiscard]] tr_address bind_address(tr_address_type type) const override
    {
        auto ret = tr_address{};
        ret.type = type;
        return ret;
    }

    [[nodiscard]] std::string_view bind_interface() const override
    {
        return bind_interface_;
    }

    [[nodiscard]] std::optional<tr_address> source_address(tr_address_type type) const override
    {
        return type == TR_AF_INET ? source_address4_ : std::nullopt;
    }

    [[nodiscard]] tr_port port() const override
    {
        return port_;
    }

    [[nodiscard]] bool allowsLPD() const override
    {
        return allows_lpd_;
    }

    [[nodiscard]] std::vector<TorrentInfo> torrents() const override
    {
        return torrents_;
    }

    [[nodiscard]] tr::TimerMaker& timerMaker() override
    {
        return session_.timerMaker();
    }

    void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_after) override
    {
        for (auto& tor : torrents_) {
            if (tor.info_hash_str == info_hash_str) {
                tor.announce_after = announce_after;
                break;
            }
        }
    }

    bool onPeerFound(std::string_view info_hash_str, tr_address /*address*/, tr_port /*port*/) override
    {
        found_.insert(std::string{ info_hash_str });
        return found_returns_;
    }

    tr_session& session_;
    std::string bind_interface_;
    std::optional<tr_address> source_address4_;
    tr_port port_ = tr_port::from_host(51413);
    bool allows_lpd_ = true;
    std::vector<TorrentInfo> torrents_;
    std::set<std::string> found_;
    bool found_returns_ = true;
};

[[nodiscard]] auto makeRandomHash()
{
    return tr_sha1::digest(tr_rand_obj<std::array<char, 256>>());
}

[[nodiscard]] auto makeRandomHashString()
{
    return tr_strupper(tr_sha1_string{ makeRandomHash() });
}

} // namespace

TEST_F(LpdTest, HelloWorld)
{
    auto mediator = MyMediator{ *session_ };
    auto lpd = tr_lpd::create(mediator, session_->event_base());
    EXPECT_TRUE(lpd);
    EXPECT_EQ(0U, std::size(mediator.found_));
}

TEST_F(LpdTest, startsOnABoundInterface)
{
    if (!TrNetCanBindInterface) {
        GTEST_SKIP() << "this platform can't bind sockets to an interface by name";
    }

    auto mediator = MyMediator{ *session_ };
    mediator.bind_interface_ = LoopbackInterface;
    mediator.source_address4_ = tr_address::from_string("127.0.0.1");

    auto const old_level = tr_logGetLevel();
    tr_logSetLevel(TR_LOG_WARN);
    tr_logSetQueueEnabled(true);
    tr_logClearQueue();

    auto lpd = tr_lpd::create(mediator, session_->event_base());
    EXPECT_TRUE(lpd);

    // IPv4 joins the group on the bound interface, which must not fail
    auto const messages = tr_logGetQueue();
    for (auto const& msg : messages) {
        EXPECT_EQ(std::string::npos, msg.message.find("Couldn't initialize IPv4 LPD")) << msg.message;
    }

    tr_logSetQueueEnabled(false);
    tr_logClearQueue();
    tr_logSetLevel(old_level);
}

// TODO(anyone): flaky test should be fixed instead of disabled
TEST_F(LpdTest, DISABLED_CanAnnounceAndRead)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->event_base());
    EXPECT_TRUE(lpd_a);

    auto const info_hash_str = makeRandomHashString();
    auto info = tr_lpd::Mediator::TorrentInfo{};
    info.info_hash_str = info_hash_str;
    info.activity = TR_STATUS_SEED;
    info.allows_lpd = true;
    info.announce_after = 0; // never announced

    auto mediator_b = MyMediator{ *session_ };
    mediator_b.torrents_.push_back(info);
    auto lpd_b = tr_lpd::create(mediator_b, session_->event_base());

    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);
    EXPECT_EQ(1U, mediator_a.found_.count(info_hash_str));
    EXPECT_EQ(0U, mediator_b.found_.count(info_hash_str));
}

// TODO(anyone): flaky test should be fixed instead of disabled
TEST_F(LpdTest, DISABLED_canMultiAnnounce)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->event_base());
    EXPECT_TRUE(lpd_a);

    auto info_hash_strings = std::array<std::string, 2>{};
    auto infos = std::array<tr_lpd::Mediator::TorrentInfo, 2>{};
    auto mediator_b = MyMediator{ *session_ };
    for (size_t i = 0; i < std::size(info_hash_strings); ++i) {
        auto& info_hash_string = info_hash_strings[i];
        auto& info = infos[i];

        info_hash_string = makeRandomHashString();

        info.info_hash_str = info_hash_string;
        info.activity = TR_STATUS_SEED;
        info.allows_lpd = true;
        info.announce_after = 0; // never announced
    }

    for (auto const& info : infos) {
        mediator_b.torrents_.push_back(info);
    }

    auto lpd_b = tr_lpd::create(mediator_b, session_->event_base());
    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);

    for (auto const& info : infos) {
        EXPECT_EQ(1U, mediator_a.found_.count(std::string{ info.info_hash_str }));
    }
}

// TODO(anyone): flaky test should be fixed instead of disabled
TEST_F(LpdTest, DISABLED_DoesNotReannounceTooSoon)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->event_base());
    EXPECT_TRUE(lpd_a);

    // similar to canMultiAnnounce...
    auto info_hash_strings = std::array<std::string, 2>{};
    auto infos = std::array<tr_lpd::Mediator::TorrentInfo, 2>{};
    auto mediator_b = MyMediator{ *session_ };
    for (size_t i = 0; i < std::size(info_hash_strings); ++i) {
        auto& info_hash_string = info_hash_strings[i];
        auto& info = infos[i];

        info_hash_string = makeRandomHashString();

        info.info_hash_str = info_hash_string;
        info.activity = TR_STATUS_SEED;
        info.allows_lpd = true;
        info.announce_after = 0; // never announced
    }

    // ...except one torrent has already been announced
    // and doesn't need to be reannounced until later
    auto const now = time(nullptr);
    infos[0].announce_after = now + 60;

    for (auto const& info : infos) {
        mediator_b.torrents_.push_back(info);
    }

    auto lpd_b = tr_lpd::create(mediator_b, session_->event_base());
    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);

    for (auto& info : infos) {
        auto const expected_count = info.announce_after <= now ? 1U : 0U;
        EXPECT_EQ(expected_count, mediator_a.found_.count(std::string{ info.info_hash_str }));
    }
}

} // namespace tr::test
