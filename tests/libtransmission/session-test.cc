// This file Copyright (C) 2013-2022 Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <future>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <event2/http.h>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/announcer.h>
#include <libtransmission/bandwidth.h>
#include <libtransmission/constants.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/file-utils.h> // tr_file_read(), tr_file_save()
#include <libtransmission/file.h>
#include <libtransmission/quark.h>
#include <libtransmission/session-id.h>
#include <libtransmission/session.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>

#include "loopback-server.h"
#include "test-fixtures.h"

using namespace std::literals;
using namespace tr::Values;

namespace tr::test
{

TEST_F(SessionTest, propertiesApi)
{
    // Note, this test is just for confirming that the getters/setters
    // in both the tr_session class and in the C API bindings all work,
    // e.g. you can get back the same value you set in.
    //
    // Confirming that each of these settings _does_ something in the session
    // is a much broader scope and left to other tests :)

    auto* const session = session_;

    // download dir

    for (auto const& value : { "foo"sv, "bar"sv, ""sv }) {
        session->setDownloadDir(value);
        EXPECT_EQ(value, session->downloadDir());
        EXPECT_EQ(value, tr_sessionGetDownloadDir(session));

        tr_sessionSetDownloadDir(session, value);
        EXPECT_EQ(value, session->downloadDir());
        EXPECT_EQ(value, tr_sessionGetDownloadDir(session));
    }

    // bind interface

    {
        auto constexpr Value = "lo0"sv;
        EXPECT_EQ(""sv, tr_sessionGetBindInterface(session));
        tr_sessionSetBindInterface(session, Value);
        EXPECT_TRUE(waitFor([session, Value] { return tr_sessionGetBindInterface(session) == Value; }, 5s));
        EXPECT_EQ(Value, tr_sessionGetBindInterface(session));
        tr_sessionSetBindInterface(session, ""sv);
        EXPECT_TRUE(waitFor([session] { return tr_sessionGetBindInterface(session).empty(); }, 5s));
    }

    // incomplete dir

    for (auto const& value : { "foo"sv, "bar"sv, ""sv }) {
        session->setIncompleteDir(value);
        EXPECT_EQ(value, session->incompleteDir());
        EXPECT_EQ(value, tr_sessionGetIncompleteDir(session));

        tr_sessionSetIncompleteDir(session, value);
        EXPECT_EQ(value, session->incompleteDir());
        EXPECT_EQ(value, tr_sessionGetIncompleteDir(session));
    }

    // script

    for (auto const& type : { TR_SCRIPT_ON_TORRENT_ADDED, TR_SCRIPT_ON_TORRENT_DONE }) {
        for (auto const& value : { "foo"sv, "bar"sv, ""sv }) {
            session->setScript(type, value);
            EXPECT_EQ(value, session->script(type));
            EXPECT_EQ(value, tr_sessionGetScript(session, type));

            tr_sessionSetScript(session, type, value);
            EXPECT_EQ(value, session->script(type));
            EXPECT_EQ(value, tr_sessionGetScript(session, type));
        }

        for (auto const value : { true, false }) {
            session->useScript(type, value);
            EXPECT_EQ(value, session->useScript(type));
            EXPECT_EQ(value, tr_sessionIsScriptEnabled(session, type));

            tr_sessionSetScriptEnabled(session, type, value);
            EXPECT_EQ(value, session->useScript(type));
            EXPECT_EQ(value, tr_sessionIsScriptEnabled(session, type));
        }
    }

    // incomplete dir enabled

    for (auto const value : { true, false }) {
        session->useIncompleteDir(value);
        EXPECT_EQ(value, session->useIncompleteDir());
        EXPECT_EQ(value, tr_sessionIsIncompleteDirEnabled(session));

        tr_sessionSetIncompleteDirEnabled(session, value);
        EXPECT_EQ(value, session->useIncompleteDir());
        EXPECT_EQ(value, tr_sessionIsIncompleteDirEnabled(session));
    }

    // blocklist url

    for (auto const& value : { "foo"sv, "bar"sv, ""sv }) {
        session->setBlocklistUrl(value);
        EXPECT_EQ(value, session->blocklistUrl());
        EXPECT_EQ(value, tr_blocklistGetURL(session));

        tr_blocklistSetURL(session, value);
        EXPECT_EQ(value, session->blocklistUrl());
        EXPECT_EQ(value, tr_blocklistGetURL(session));
    }

    // rpc username

    for (auto const& value : { "foo"sv, "bar"sv, ""sv }) {
        tr_sessionSetRPCUsername(session, value);
        EXPECT_EQ(value, tr_sessionGetRPCUsername(session));
    }

    // rpc password (unsalted)

    {
        auto const value = "foo"sv;
        tr_sessionSetRPCPassword(session, value);
        EXPECT_NE(value, tr_sessionGetRPCPassword(session));
        EXPECT_EQ('{', tr_sessionGetRPCPassword(session)[0]);
    }

    // rpc password (salted)

    {
        auto const plaintext = "foo"sv;
        auto const salted = tr_ssha1(plaintext);
        tr_sessionSetRPCPassword(session, salted);
        EXPECT_EQ(salted, tr_sessionGetRPCPassword(session));
    }

    // blocklist enabled

    for (auto const value : { true, false }) {
        session->set_blocklist_enabled(value);
        EXPECT_EQ(value, session->blocklist_enabled());
        EXPECT_EQ(value, tr_blocklistIsEnabled(session));

        tr_sessionSetIncompleteDirEnabled(session, value);
        EXPECT_EQ(value, session->blocklist_enabled());
        EXPECT_EQ(value, tr_blocklistIsEnabled(session));
    }
}

TEST_F(SessionTest, recentDownloadDirs)
{
    auto* const session = session_;
    auto const& recent = session->recent_download_paths(); // live ref into the session

    // empty input is ignored
    auto const before = recent; // copy
    session->add_recent_download_dir(""sv);
    EXPECT_EQ(before, recent);

    // a new dir is prepended (most-recent-first)
    session->add_recent_download_dir("/a"sv);
    EXPECT_EQ("/a", recent.front());
    session->add_recent_download_dir("/b"sv);
    EXPECT_EQ("/b", recent.front());

    // re-adding an existing dir moves it to the front without duplicating
    // (regression guard for the bubble-to-front fix)
    session->add_recent_download_dir("/a"sv);
    EXPECT_EQ("/a", recent.front());
    EXPECT_EQ(1, std::ranges::count(recent, "/a"s));

    // the list is capped at TrMaxRecentDirs, dropping the oldest
    for (auto i = 0; i < 10; ++i) {
        session->add_recent_download_dir("/dir/" + std::to_string(i));
    }
    EXPECT_LE(std::size(recent), TrMaxRecentDirs);
    EXPECT_EQ("/dir/9", recent.front());
}

TEST_F(SessionTest, recentRelocateDirs)
{
    auto* const session = session_;
    auto const& recent = session->recent_relocate_paths(); // live ref into the session

    session->add_recent_relocate_dir("/x"sv);
    EXPECT_EQ("/x", recent.front());

    // re-adding an existing dir moves it to the front without duplicating
    session->add_recent_relocate_dir("/y"sv);
    session->add_recent_relocate_dir("/x"sv);
    EXPECT_EQ("/x", recent.front());
    EXPECT_EQ(1, std::ranges::count(recent, "/x"s));
}

TEST_F(SessionTest, peerId)
{
    auto const peer_id_prefix = std::string{ PEERID_PREFIX };

    for (int i = 0; i < 100000; ++i) {
        // get a new peer-id
        auto const buf = tr_peerIdInit();

        // confirm that it begins with peer_id_prefix
        auto const peer_id = std::string_view{ reinterpret_cast<char const*>(buf.data()), std::size(buf) };
        EXPECT_EQ(peer_id_prefix, peer_id.substr(0, peer_id_prefix.size()));

        // confirm that its total is evenly divisible by 36
        int val = 0;
        auto const suffix = peer_id.substr(peer_id_prefix.size());
        for (char const ch : suffix) {
            auto const tmp = std::to_array<char>({ ch, '\0' });
            val += strtoul(tmp.data(), nullptr, 36);
        }

        EXPECT_EQ(0, val % 36);
    }
}

namespace current_time_mock
{
namespace
{

auto value = time_t{};

[[nodiscard]] time_t get()
{
    return value;
}

void set(time_t now)
{
    value = now;
}

} // unnamed namespace
} // namespace current_time_mock

TEST_F(SessionTest, sessionId)
{
#ifdef __sun
    // FIXME: File locking doesn't work as expected
    GTEST_SKIP();
#endif

    EXPECT_FALSE(tr_session_id::is_local(""));
    EXPECT_FALSE(tr_session_id::is_local("test"));

    current_time_mock::set(0U);
    auto session_id = std::make_unique<tr_session_id>(current_time_mock::get);

    EXPECT_NE(""sv, session_id->sv());
    EXPECT_EQ(session_id->sv(), session_id->c_str()) << session_id->sv() << ", " << session_id->c_str();
    EXPECT_EQ(48U, strlen(session_id->c_str()));
    auto session_id_str_1 = std::string{ session_id->sv() };
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_1));

    current_time_mock::set(current_time_mock::get() + (3600U - 1U));
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_1));
    auto session_id_str_2 = std::string{ session_id->sv() };
    EXPECT_EQ(session_id_str_1, session_id_str_2);

    current_time_mock::set(3600U);
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_1));
    session_id_str_2 = std::string{ session_id->sv() };
    EXPECT_NE(session_id_str_1, session_id_str_2);
    EXPECT_EQ(session_id_str_2, session_id->c_str());
    EXPECT_EQ(48U, strlen(session_id->c_str()));

    EXPECT_TRUE(tr_session_id::is_local(session_id_str_2));
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_1));
    current_time_mock::set(7200U);
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_2));
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_1));

    auto const session_id_str_3 = std::string{ session_id->sv() };
    EXPECT_EQ(48U, std::size(session_id_str_3));
    EXPECT_NE(session_id_str_2, session_id_str_3);
    EXPECT_NE(session_id_str_1, session_id_str_3);

    EXPECT_TRUE(tr_session_id::is_local(session_id_str_3));
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id::is_local(session_id_str_1));

    current_time_mock::set(36000U);
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_3));
    EXPECT_TRUE(tr_session_id::is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id::is_local(session_id_str_1));

    session_id.reset();
    EXPECT_FALSE(tr_session_id::is_local(session_id_str_3));
    EXPECT_FALSE(tr_session_id::is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id::is_local(session_id_str_1));
}

TEST_F(SessionTest, getDefaultSettingsIncludesSubmodules)
{
    auto settings = tr_sessionGetDefaultSettings();

    // Choose a setting from each of [tr_session, tr_session_alt_speeds, tr_rpc_server] to test all of them.
    // These are all `false` by default
    for (auto const& key : { TR_KEY_peer_port_random_on_start, TR_KEY_alt_speed_time_enabled, TR_KEY_rpc_enabled }) {
        auto flag = settings.value_if<bool>(key);
        ASSERT_TRUE(flag);
        EXPECT_FALSE(*flag);
    }
}

TEST_F(SessionTest, loadSettingsHonorsAppDefaults)
{
    // `rpc_enabled` is `false` in libtransmission's defaults,
    // but apps, e.g. the daemon, may default it to `true`
    static auto constexpr Key = TR_KEY_rpc_enabled;
    auto const config_dir = tr_pathbuf{ sandboxDir(), "/app-defaults-config"sv };
    auto app_defaults = tr::Settings{};
    app_defaults.try_emplace(Key, true);

    // app defaults fill in keys missing from the settings file
    // and take precedence over libtransmission's defaults
    auto settings = tr_sessionLoadSettings(config_dir, app_defaults);
    auto flag = settings.value_if<bool>(Key);
    ASSERT_TRUE(flag);
    EXPECT_TRUE(*flag);

    // values from the settings file take precedence over app defaults
    createFileWithContents(tr_pathbuf{ config_dir, "/settings.json"sv }, R"({ "rpc-enabled": false })");
    settings = tr_sessionLoadSettings(config_dir, app_defaults);
    flag = settings.value_if<bool>(Key);
    ASSERT_TRUE(flag);
    EXPECT_FALSE(*flag);

    // keys absent from both fall back to libtransmission's defaults
    flag = settings.value_if<bool>(TR_KEY_peer_port_random_on_start);
    ASSERT_TRUE(flag);
    EXPECT_FALSE(*flag);
}

TEST_F(SessionTest, honorsSettings)
{
    // Baseline: confirm that these settings are disabled by default
    EXPECT_FALSE(session_->isPortRandom());
    EXPECT_FALSE(tr_sessionUsesAltSpeedTime(session_));
    EXPECT_FALSE(tr_sessionIsRPCEnabled(session_));

    // Choose a setting from each of [tr_session, tr_session_alt_speeds, tr_rpc_server] to test all of them.
    // These are all `false` by default
    auto settings = tr_sessionGetDefaultSettings();
    for (auto const& key : { TR_KEY_peer_port_random_on_start, TR_KEY_alt_speed_time_enabled, TR_KEY_rpc_enabled }) {
        settings.insert_or_assign(key, true);
    }
    auto* session = tr_sessionInit(sandboxDir(), false, settings);

    // confirm that these settings were enabled
    EXPECT_TRUE(session->isPortRandom());
    EXPECT_TRUE(tr_sessionUsesAltSpeedTime(session));
    EXPECT_TRUE(tr_sessionIsRPCEnabled(session));

    tr_sessionClose(session, 0.5);
}

TEST_F(SessionTest, savesSettings)
{
    // Baseline: confirm that these settings are disabled by default
    EXPECT_FALSE(session_->isPortRandom());
    EXPECT_FALSE(tr_sessionUsesAltSpeedTime(session_));
    EXPECT_FALSE(tr_sessionIsRPCEnabled(session_));

    tr_sessionSetPeerPortRandomOnStart(session_, true);
    tr_sessionUseAltSpeedTime(session_, true);
    tr_sessionSetRPCEnabled(session_, true);

    // Choose a setting from each of [tr_session, tr_session_alt_speeds, tr_rpc_server] to test all of them.
    auto settings = tr_sessionGetSettings(session_);
    for (auto const& key : { TR_KEY_peer_port_random_on_start, TR_KEY_alt_speed_time_enabled, TR_KEY_rpc_enabled }) {
        auto flag = settings.value_if<bool>(key);
        ASSERT_TRUE(flag);
        EXPECT_TRUE(*flag);
    }
}

namespace
{
// What a session saves for a bandwidth group.
struct SavedBandwidthGroup {
    tr_bandwidth_limits limits;
    bool honors_session_limits = true;
};

// Sets a bandwidth group on the session thread, where `group_set` sets it over RPC.
void setBandwidthGroup(tr_session* const session, std::string_view const name, SavedBandwidthGroup const& saved)
{
    auto done = std::promise<void>{};
    session->run_in_session_thread([&]() {
        auto& group = session->getBandwidthGroup(name);
        group.set_limits(saved.limits);
        group.honor_parent_limits(tr_direction::Up, saved.honors_session_limits);
        group.honor_parent_limits(tr_direction::Down, saved.honors_session_limits);
        done.set_value();
    });
    done.get_future().wait();
}

// What a new session on `config_dir` loads for the group `name`, if it loads that group at all.
[[nodiscard]] std::optional<SavedBandwidthGroup> loadBandwidthGroup(
    std::string_view const config_dir,
    tr::Settings const& settings,
    std::string_view const name)
{
    auto* const session = tr_sessionInit(config_dir, false, settings);
    auto loaded = std::optional<SavedBandwidthGroup>{};
    for (auto const& [group_name, group] : session->bandwidthGroups()) {
        if (group_name == name) {
            loaded = SavedBandwidthGroup{
                .limits = group->get_limits(),
                .honors_session_limits = group->are_parent_limits_honored(tr_direction::Up),
            };
        }
    }
    tr_sessionClose(session, 1);
    return loaded;
}
} // namespace

// The groups persist even for a client that never calls tr_sessionSaveSettings().
TEST_F(SessionTest, savesBandwidthGroupsWhenItCloses)
{
    static auto constexpr Name = "capped"sv;
    auto const saved = SavedBandwidthGroup{
        .limits = {
            .up_limit = Speed{ 20, Speed::Units::KByps },
            .down_limit = Speed{ 30, Speed::Units::KByps },
            .up_limited = true,
            .down_limited = true,
        },
        .honors_session_limits = false,
    };

    setBandwidthGroup(session_, Name, saved);
    closeSession();

    auto const reloaded = loadBandwidthGroup(sandboxDir(), quietSettings(), Name);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(saved.limits.up_limit, reloaded->limits.up_limit);
    EXPECT_EQ(saved.limits.down_limit, reloaded->limits.down_limit);
    EXPECT_EQ(saved.limits.up_limited, reloaded->limits.up_limited);
    EXPECT_EQ(saved.limits.down_limited, reloaded->limits.down_limited);
    EXPECT_EQ(saved.honors_session_limits, reloaded->honors_session_limits);
}

// The periodic save writes the groups after a change, and only then.
TEST_F(SessionTest, savesChangedBandwidthGroupsPeriodically)
{
    static auto constexpr Name = "capped"sv;
    auto const filename = tr_pathbuf{ sandboxDir(), "/bandwidth-groups.json"sv };
    auto const saved = SavedBandwidthGroup{
        .limits = {
            .up_limit = Speed{ 20, Speed::Units::KByps },
            .down_limit = Speed{ 30, Speed::Units::KByps },
            .up_limited = true,
            .down_limited = true,
        },
    };

    setBandwidthGroup(session_, Name, saved);
    runSaveTimer();
    auto const reloaded = loadBandwidthGroup(sandboxDir(), quietSettings(), Name);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(saved.limits.up_limit, reloaded->limits.up_limit);

    ASSERT_TRUE(tr_sys_path_remove(filename));
    runSaveTimer();
    EXPECT_FALSE(tr_sys_path_exists(filename));
}

// Files from some builds hold the speed limits as doubles.
// They load with any fraction dropped.
TEST_F(SessionTest, loadsBandwidthGroupSpeedLimitsSavedAsDoubles)
{
    static auto constexpr Name = "capped"sv;
    closeSession();

    auto const filename = tr_pathbuf{ sandboxDir(), "/bandwidth-groups.json"sv };
    ASSERT_TRUE(tr_file_save(
        filename,
        R"({"capped":{"download_limit":30.9,"download_limited":true,"honors_session_limits":true,"name":"capped","upload_limit":20.0,"upload_limited":true}})"sv));

    auto const reloaded = loadBandwidthGroup(sandboxDir(), quietSettings(), Name);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ((Speed{ 20, Speed::Units::KByps }), reloaded->limits.up_limit);
    EXPECT_EQ((Speed{ 30, Speed::Units::KByps }), reloaded->limits.down_limit);
}

TEST_F(SessionTest, loadTorrentsThenMagnets)
{
    static auto constexpr TorrentFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/archlinux-2025.05.01-x86_64.iso.torrent";
    static auto constexpr MagnetFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/archlinux-2025.05.01-x86_64.iso.magnet";

    if (auto error = tr_error{};
        !tr_sys_path_copy(
            TorrentFile,
            tr_pathbuf{ session_->torrentDir(), "/2e34989b1c60df821b2d046c884d8f4d1858b97a.torrent"sv },
            &error) ||
        !tr_sys_path_copy(
            MagnetFile,
            tr_pathbuf{ session_->torrentDir(), "/2e34989b1c60df821b2d046c884d8f4d1858b97a.magnet"sv },
            &error)) {
        GTEST_SKIP() << fmt::format("Failed to setup torrents dir: {} ({})", error.message(), error.code());
    }

    auto builder = tr_torrent_builder{ session_ };
    builder.set_paused(false);
    EXPECT_EQ(tr_sessionLoadTorrents(session_, &builder), 1U);

    auto* const tor = session_->torrents().get(1U);
    ASSERT_NE(tor, nullptr);

    EXPECT_TRUE(tor->has_metainfo());
}

TEST_F(SessionTest, isBusy)
{
    // no torrents -> not busy
    EXPECT_FALSE(session_->is_busy(tr_time()));

    // a complete torrent added paused -> not busy once its add-time verify finishes
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    EXPECT_TRUE(waitFor([this]() { return !session_->is_busy(tr_time()); }, 5s));

    // starting it counts as busy, even with no peer to transfer with
    tr_torrentStart(tor);
    EXPECT_TRUE(waitFor([this]() { return session_->is_busy(tr_time()); }, 5s));

    // stopping it -> not busy again, without waiting for the activity to go stale
    tr_torrentStop(tor);
    EXPECT_TRUE(waitFor([this]() { return !session_->is_busy(tr_time()); }, 5s));
}

namespace
{
[[nodiscard]] bool configDirIsContended(std::string_view const config_dir, tr::Settings const& settings)
{
    auto* const session = tr_sessionInit(config_dir, false, settings);
    auto const is_contended = tr_sessionConfigDirIsContended(session);
    tr_sessionClose(session, 1);
    return is_contended;
}

// Every regular file under `dir` and what is in it, so a later snapshot says what changed.
void snapshotDir(std::string const& dir, std::map<std::string, std::string>& out)
{
    depthFirstWalk(dir, [&out](std::string_view const path) {
        if (auto const info = tr_sys_path_get_info(path); !info || !info->isFile()) {
            return;
        }

        auto contents = std::vector<char>{};
        tr_file_read(path, contents);
        out.try_emplace(std::string{ path }, std::data(contents), std::size(contents));
    });
}
} // namespace

// The second session still starts. It takes the lock to find out the dir is not its own.
// `session_` already holds this dir.
TEST_F(SessionTest, doesNotOwnAConfigDirAnotherSessionHolds)
{
    EXPECT_TRUE(configDirIsContended(sandboxDir(), quietSettings()));
}

// A session that found the config dir contended must not write there at all.
// Its settings, stats, resume data and queue would all clobber the owner's copies.
// `session_` already holds this dir.
TEST_F(SessionTest, doesNotWriteToAConfigDirAnotherSessionHolds)
{
    auto before = std::map<std::string, std::string>{};
    snapshotDir(sandboxDir(), before);

    auto const settings = quietSettings();
    auto* const session = tr_sessionInit(sandboxDir(), false, settings);
    ASSERT_TRUE(tr_sessionConfigDirIsContended(session));
    tr_sessionSaveSettings(session, sandboxDir(), settings);
    tr_sessionClose(session, 1);

    auto after = std::map<std::string, std::string>{};
    snapshotDir(sandboxDir(), after);

    for (auto const& [path, contents] : after) {
        if (auto const it = before.find(path); it == std::end(before)) {
            ADD_FAILURE() << "created " << path;
        } else if (it->second != contents) {
            ADD_FAILURE() << "modified " << path;
        }
    }

    for (auto const& [path, contents] : before) {
        if (!after.contains(path)) {
            ADD_FAILURE() << "removed " << path;
        }
    }
}

TEST_F(SessionTest, releasesTheConfigDirWhenItCloses)
{
    closeSession();

    EXPECT_FALSE(configDirIsContended(sandboxDir(), quietSettings()));
}

TEST_F(SessionTest, claimsAConfigDirItHadToCreate)
{
    EXPECT_FALSE(configDirIsContended(sandboxDir() + "/brand-new", quietSettings()));
}

TEST_F(SessionTest, startsOnAConfigDirItCannotLock)
{
    // A lock that is a directory can never be opened as a file.
    // Every real "could not be locked at all" behaves the same way.
    auto const dir = sandboxDir() + "/unlockable";
    ASSERT_TRUE(tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700));
    ASSERT_TRUE(tr_sys_dir_create(dir + "/lock", TR_SYS_DIR_CREATE_PARENTS, 0700));

    auto* const session = tr_sessionInit(dir, false, quietSettings());
    EXPECT_EQ(tr_config_dir_lock::Status::Unavailable, session->configDirLockStatus());
    EXPECT_FALSE(tr_sessionConfigDirIsContended(session));
    tr_sessionClose(session, 1);
}

// A new UDP core binds the port the old one held, which macOS refuses while both are open.
TEST_F(SessionTest, udpCoreSurvivesABindInterfaceChange)
{
#ifndef __APPLE__
    GTEST_SKIP() << "only macOS binds sockets to an interface by name";
#else
    auto constexpr Loopback = "lo0"sv;

    if (!is_valid_socket(udpSocket4())) {
        GTEST_SKIP() << "the session has no IPv4 UDP socket to begin with";
    }

    tr_sessionSetBindInterface(session_, Loopback);
    EXPECT_TRUE(waitFor([this, Loopback] { return tr_sessionGetBindInterface(session_) == Loopback; }, 5s));

    EXPECT_TRUE(is_valid_socket(udpSocket4()));
#endif
}

// "blocked" refuses every fetch made after the swap, so a tracker hears
// "stopped" only if it went out on the binding the torrent started on.
// More tiers than one announcer upkeep sends prove none of them wait for it.
TEST_F(SessionTest, interfaceChangeSendsStoppedOnTheOldBinding)
{
    static auto constexpr NumTiers = 21U;

    auto tracker = LoopbackServer::create();
    if (!tracker) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }

    auto n_started = std::make_shared<std::atomic<size_t>>();
    auto n_stopped = std::make_shared<std::atomic<size_t>>();
    tracker->setHandler([n_started, n_stopped](evhttp_request* req) {
        auto const uri = std::string_view{ evhttp_request_get_uri(req) };
        if (uri.contains("event=started"sv)) {
            ++*n_started;
        } else if (uri.contains("event=stopped"sv)) {
            ++*n_stopped;
        }
        LoopbackServer::reply(req, HTTP_OK, "OK", "d8:intervali1800e5:peers0:e"sv);
    });

    auto trackers = std::string{};
    for (size_t i = 0U; i < NumTiers; ++i) {
        trackers += tracker->url(fmt::format("/{:d}/announce", i));
        trackers += "\n\n";
    }

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    ASSERT_TRUE(tr_torrentSetTrackerList(tor, trackers));
    tr_torrentStartNow(tor);
    ASSERT_TRUE(waitFor([&n_started] { return *n_started == NumTiers; }, 10s));

    tr_sessionSetBindInterface(session_, "blocked"sv);
    EXPECT_TRUE(waitFor([&n_stopped] { return *n_stopped == NumTiers; }, 10s)) << *n_stopped;
    EXPECT_TRUE(blockingRunInSessionThread([tor] { return tor->is_running(); }));
}

// The flushed "stopped" reports the session's traffic, so the "started" on the
// new binding begins a new count.
TEST_F(SessionTest, interfaceChangeRestartsTrafficCounts)
{
#ifndef __APPLE__
    GTEST_SKIP() << "only macOS binds sockets to an interface by name";
#else
    static auto constexpr Uploaded = uint32_t{ 12345U };

    auto tracker = LoopbackServer::create();
    if (!tracker) {
        GTEST_SKIP() << "IPv4 loopback is unavailable";
    }

    auto const mutex = std::make_shared<std::mutex>();
    auto const announces = std::make_shared<std::vector<std::string>>();
    tracker->setHandler([mutex, announces](evhttp_request* req) {
        if (auto uri = std::string{ evhttp_request_get_uri(req) }; uri.starts_with("/announce"sv)) {
            auto const lock = std::scoped_lock{ *mutex };
            announces->emplace_back(std::move(uri));
        }
        LoopbackServer::reply(req, HTTP_OK, "OK", "d8:intervali1800e5:peers0:e"sv);
    });
    // the first "stopped", and the first "started" after it
    auto const restart_announces = [mutex, announces]() -> std::optional<std::pair<std::string, std::string>> {
        auto const lock = std::scoped_lock{ *mutex };
        auto const stopped = std::ranges::find_if(*announces, [](auto const& uri) { return uri.contains("event=stopped"sv); });
        auto const started = std::find_if(stopped, std::end(*announces), [](auto const& uri) {
            return uri.contains("event=started"sv);
        });
        if (started == std::end(*announces)) {
            return {};
        }
        return std::pair{ *stopped, *started };
    };

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    ASSERT_TRUE(tr_torrentSetTrackerList(tor, tracker->url("/announce")));
    tr_torrentStartNow(tor);
    ASSERT_TRUE(waitFor(
        [mutex, announces] {
            auto const lock = std::scoped_lock{ *mutex };
            return !std::empty(*announces);
        },
        10s));
    blockingRunInSessionThread([tor] { tr_announcerAddBytes(tor, TR_ANN_UP, Uploaded); });

    tr_sessionSetBindInterface(session_, "lo0"sv);
    ASSERT_TRUE(waitFor([&restart_announces] { return restart_announces().has_value(); }, 10s));

    auto const [stopped, started] = *restart_announces();
    EXPECT_TRUE(stopped.contains(fmt::format("uploaded={:d}&", Uploaded))) << stopped;
    EXPECT_TRUE(started.contains("uploaded=0&"sv)) << started;
#endif
}

} // namespace tr::test
