// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/quark.h>
#include <libtransmission/session-settings.h> // tr::is_settings_key()
#include <libtransmission/transmission.h> // tr_encryption_mode, tr_sessionGetDefaultSettings()
#include <libtransmission/variant.h>

#include "libtransmission-app/display-modes.h"
#include "libtransmission-app/prefs.h"

#include "test-fixtures.h"

using namespace std::literals;

using tr::app::ShowMode;
using tr::app::SortMode;
using tr::app::StatsMode;

namespace
{

// A concrete `tr::app::Prefs` that records the keys passed to `on_changed()`,
// so tests can assert exactly when (and whether) change notifications fire.
class TestPrefs final : public tr::app::Prefs
{
public:
    using tr::app::Prefs::Prefs;

    [[nodiscard]] std::vector<tr_quark> const& changed_keys() const noexcept
    {
        return changed_keys_;
    }

    void clear_changed() noexcept
    {
        changed_keys_.clear();
    }

private:
    void on_changed(tr_quark const key) override
    {
        changed_keys_.push_back(key);
    }

    std::vector<tr_quark> changed_keys_;
};

// Sets `key` to `a` then `b`, asserting the round-trip through get/set for each.
template<typename T>
void expect_get_set_roundtrip(TestPrefs& prefs, tr_quark const key, T const& a, T const& b)
{
    ASSERT_NE(a, b);

    prefs.set(key, a);
    EXPECT_EQ(prefs.template get<T>(key), a);

    prefs.set(key, b);
    EXPECT_EQ(prefs.template get<T>(key), b);
}

} // namespace

// ---- in-memory tests (no filesystem needed) --------------------------------

using PrefsTest = TransmissionTest;

TEST_F(PrefsTest, prefsIsCoreClassifiesKeys)
{
    // core (session-owned) keys
    EXPECT_TRUE(tr::app::prefs_is_core(TR_KEY_download_dir));
    EXPECT_TRUE(tr::app::prefs_is_core(TR_KEY_rpc_enabled));
    EXPECT_TRUE(tr::app::prefs_is_core(TR_KEY_speed_limit_up));
    EXPECT_TRUE(tr::app::prefs_is_core(TR_KEY_encryption));

    // UI-only / app-owned keys
    EXPECT_FALSE(tr::app::prefs_is_core(TR_KEY_filter_text));
    EXPECT_FALSE(tr::app::prefs_is_core(TR_KEY_show_statusbar));
    EXPECT_FALSE(tr::app::prefs_is_core(TR_KEY_main_window_height));
    EXPECT_FALSE(tr::app::prefs_is_core(TR_KEY_sort_mode));
}

TEST_F(PrefsTest, defaultConstructorUsesDefaults)
{
    auto const prefs = TestPrefs{};

    // app-owned defaults
    EXPECT_EQ(prefs.get<ShowMode>(TR_KEY_show_mode), tr::app::DefaultShowMode);
    EXPECT_EQ(prefs.get<SortMode>(TR_KEY_sort_mode), tr::app::DefaultSortMode);
    EXPECT_EQ(prefs.get<StatsMode>(TR_KEY_statusbar_stats), tr::app::DefaultStatsMode);
    EXPECT_EQ(prefs.get<std::chrono::sys_seconds>(TR_KEY_blocklist_date), std::chrono::sys_seconds{});
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_show_statusbar));
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_prompt_before_exit));
    EXPECT_FALSE(prefs.get<bool>(TR_KEY_sort_reversed));
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 500);
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_width), 600);

    // session-owned defaults
    EXPECT_FALSE(prefs.get<bool>(TR_KEY_speed_limit_down_enabled));
    EXPECT_EQ(prefs.get<double>(TR_KEY_seed_ratio_limit), 0.0);
}

TEST_F(PrefsTest, getSetRoundTripsBool)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_sort_reversed, false, true);
}

TEST_F(PrefsTest, getSetRoundTripsInt)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_main_window_height, 4242, 2323);
}

TEST_F(PrefsTest, getSetRoundTripsDouble)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_seed_ratio_limit, 1.25, 5.5);
}

TEST_F(PrefsTest, getSetRoundTripsString)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_download_dir, "/tmp/dir-a"s, "/tmp/dir-b"s);
}

TEST_F(PrefsTest, getSetRoundTripsStringList)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(
        prefs,
        TR_KEY_torrent_complete_sound_command,
        std::vector<std::string>{ "one", "two", "three" },
        std::vector<std::string>{ "alpha", "beta" });
}

TEST_F(PrefsTest, getSetRoundTripsShowMode)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_show_mode, ShowMode::ShowAll, ShowMode::ShowActive);
}

TEST_F(PrefsTest, getSetRoundTripsSortMode)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_sort_mode, SortMode::SortBySize, SortMode::SortByName);
}

TEST_F(PrefsTest, getSetRoundTripsStatsMode)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_statusbar_stats, StatsMode::SessionRatio, StatsMode::TotalTransfer);
}

TEST_F(PrefsTest, getSetRoundTripsEncryptionMode)
{
    auto prefs = TestPrefs{};
    expect_get_set_roundtrip(prefs, TR_KEY_encryption, TR_ENCRYPTION_REQUIRED, TR_ENCRYPTION_PREFERRED);
}

TEST_F(PrefsTest, getSetRoundTripsDate)
{
    auto prefs = TestPrefs{};
    auto const a = std::chrono::sys_seconds{ std::chrono::seconds{ 1700000000 } };
    auto const b = std::chrono::sys_seconds{ std::chrono::seconds{ 1700000123 } };
    expect_get_set_roundtrip(prefs, TR_KEY_blocklist_date, a, b);
}

TEST_F(PrefsTest, getReturnsDefaultForUnknownKey)
{
    auto const prefs = TestPrefs{};

    // TR_KEY_reqq is a session-settings key but not a `Prefs` field, so it is
    // absent and `get()` should fall back to a value-initialized result.
    EXPECT_EQ(prefs.get<int>(TR_KEY_reqq), 0);
    EXPECT_EQ(prefs.get<std::string>(TR_KEY_reqq), std::string{});
}

TEST_F(PrefsTest, getAsVariantReturnsUnderlyingValue)
{
    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_main_window_height, 4242);

    auto const var = prefs.get<tr_variant>(TR_KEY_main_window_height);
    EXPECT_EQ(var.value_if<int64_t>(), 4242);
}

TEST_F(PrefsTest, setNotifiesOnChangeOnly)
{
    static auto constexpr Key = TR_KEY_sort_reversed;

    auto prefs = TestPrefs{};
    auto const original = prefs.get<bool>(Key);

    // a real change fires exactly one notification for the key
    prefs.set(Key, !original);
    ASSERT_EQ(std::size(prefs.changed_keys()), 1U);
    EXPECT_EQ(prefs.changed_keys().front(), static_cast<tr_quark>(Key));

    // setting the same value again does not notify
    prefs.clear_changed();
    prefs.set(Key, !original);
    EXPECT_TRUE(std::empty(prefs.changed_keys()));
}

TEST_F(PrefsTest, setFromVariantAppliesValueAndNotifies)
{
    static auto constexpr Key = TR_KEY_main_window_height;

    auto prefs = TestPrefs{};
    prefs.set(Key, tr_variant{ int64_t{ 321 } });

    EXPECT_EQ(prefs.get<int>(Key), 321);
    ASSERT_EQ(std::size(prefs.changed_keys()), 1U);
    EXPECT_EQ(prefs.changed_keys().front(), static_cast<tr_quark>(Key));
}

TEST_F(PrefsTest, setFromVariantIgnoresWrongType)
{
    static auto constexpr Key = TR_KEY_main_window_height;

    auto prefs = TestPrefs{};
    auto const original = prefs.get<int>(Key);

    // a string is not convertible to the int field: no change, no notification
    prefs.set(Key, tr_variant{ "not-an-int"sv });

    EXPECT_EQ(prefs.get<int>(Key), original);
    EXPECT_TRUE(std::empty(prefs.changed_keys()));
}

TEST_F(PrefsTest, settingsConstructorLoadsProvidedValues)
{
    auto settings = tr::Settings{};
    settings.insert_or_assign(TR_KEY_main_window_height, int64_t{ 777 }); // app-owned
    settings.insert_or_assign(TR_KEY_sort_reversed, true); // app-owned
    settings.insert_or_assign(TR_KEY_download_dir, "/from/settings"sv); // session-owned

    auto const prefs = TestPrefs{ settings };
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 777);
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_sort_reversed));
    EXPECT_EQ(prefs.get<std::string>(TR_KEY_download_dir), "/from/settings");
}

TEST_F(PrefsTest, settingsConstructorLeavesUnspecifiedKeysAtDefault)
{
    auto settings = tr::Settings{};
    settings.insert_or_assign(TR_KEY_main_window_height, int64_t{ 777 });

    auto const prefs = TestPrefs{ settings };
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 777);
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_width), 600); // untouched default
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_show_statusbar)); // untouched default
}

// ---- filesystem tests (config-dir constructor and save) --------------------

class PrefsFileTest : public SandboxedTest
{
protected:
    [[nodiscard]] std::string settings_filename() const
    {
        return fmt::format("{:s}/settings.json", sandbox_dir());
    }

    void write_settings_file(tr::Settings const& settings) const
    {
        tr::settings::save(settings_filename(), settings);
    }

    [[nodiscard]] tr::Settings load_settings_file() const
    {
        return tr::settings::load(settings_filename());
    }

    // saves `prefs` into the sandbox, then reads the resulting settings.json back
    [[nodiscard]] tr::Settings save_and_reload(
        TestPrefs const& prefs,
        std::optional<tr::Settings> const& local_session_settings = std::nullopt) const
    {
        prefs.save(sandbox_dir(), local_session_settings);
        return load_settings_file();
    }
};

TEST_F(PrefsFileTest, configDirConstructorUsesDefaultsWhenFileMissing)
{
    ASSERT_FALSE(tr_sys_path_exists(settings_filename()));

    auto const prefs = TestPrefs{ sandbox_dir() };
    EXPECT_EQ(prefs.get<ShowMode>(TR_KEY_show_mode), tr::app::DefaultShowMode);
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 500);
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_show_statusbar));
    EXPECT_FALSE(prefs.get<std::string>(TR_KEY_download_dir).empty()); // session default
}

TEST_F(PrefsFileTest, configDirConstructorLoadsFileOverridingDefaults)
{
    auto settings = tr::Settings{};
    settings.insert_or_assign(TR_KEY_main_window_height, int64_t{ 1234 }); // app-owned
    settings.insert_or_assign(TR_KEY_show_statusbar, false); // app-owned, non-default
    settings.insert_or_assign(TR_KEY_download_dir, "/custom/dl"sv); // session-owned
    settings.insert_or_assign(TR_KEY_speed_limit_down, int64_t{ 999 }); // session-owned
    write_settings_file(settings);

    auto const prefs = TestPrefs{ sandbox_dir() };
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 1234);
    EXPECT_FALSE(prefs.get<bool>(TR_KEY_show_statusbar));
    EXPECT_EQ(prefs.get<std::string>(TR_KEY_download_dir), "/custom/dl");
    EXPECT_EQ(prefs.get<int>(TR_KEY_speed_limit_down), 999);

    // a key absent from the file still falls back to its default
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_width), 600);
}

TEST_F(PrefsFileTest, configDirConstructorFallsBackWhenFileIsNotAnObject)
{
    // a top-level JSON array is not a settings object; load() should ignore it
    tr_variant_serde::json().to_file(tr_variant{ tr_variant::Vector{} }, settings_filename());

    auto const prefs = TestPrefs{ sandbox_dir() };
    EXPECT_EQ(prefs.get<int>(TR_KEY_main_window_height), 500);
    EXPECT_TRUE(prefs.get<bool>(TR_KEY_show_statusbar));
}

TEST_F(PrefsFileTest, saveWritesLiveAppPrefs)
{
    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_main_window_height, 4242);
    prefs.set(TR_KEY_show_statusbar, false);
    prefs.set(TR_KEY_sort_mode, SortMode::SortByQueue);

    auto const saved = save_and_reload(prefs);
    EXPECT_EQ(saved.value_if<int64_t>(TR_KEY_main_window_height), 4242);
    EXPECT_EQ(saved.value_if<bool>(TR_KEY_show_statusbar), false);
    EXPECT_EQ(saved.value_if<std::string_view>(TR_KEY_sort_mode), "sort_by_queue"sv);
}

TEST_F(PrefsFileTest, saveOverwritesStaleAppPrefInFile)
{
    auto stale = tr::Settings{};
    stale.insert_or_assign(TR_KEY_main_window_height, int64_t{ 111 });
    write_settings_file(stale);

    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_main_window_height, 222); // live value wins over the on-disk value

    auto const saved = save_and_reload(prefs);
    EXPECT_EQ(saved.value_if<int64_t>(TR_KEY_main_window_height), 222);
}

TEST_F(PrefsFileTest, saveOmitsTransientFilterText)
{
    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_filter_text, "needle"s);
    prefs.set(TR_KEY_sort_reversed, true);

    auto const saved = save_and_reload(prefs);
    EXPECT_FALSE(saved.contains(TR_KEY_filter_text)); // transient: never persisted
    EXPECT_TRUE(saved.contains(TR_KEY_sort_reversed)); // ordinary app pref: persisted
}

TEST_F(PrefsFileTest, saveRemovesUnrecognizedKeys)
{
    auto const custom_key = tr_quark_new("custom-unknown-setting"sv);

    auto existing = tr::Settings{};
    existing.insert_or_assign(custom_key, int64_t{ 123 }); // neither a pref nor a settings key
    existing.insert_or_assign(TR_KEY_download_dir, "/keep/me"sv); // recognized session key
    write_settings_file(existing);

    ASSERT_FALSE(tr::app::prefs_is_core(custom_key));
    ASSERT_FALSE(tr::is_settings_key(custom_key));

    auto const prefs = TestPrefs{};
    auto const saved = save_and_reload(prefs);
    EXPECT_FALSE(saved.contains(custom_key)); // unrecognized: dropped
    EXPECT_TRUE(saved.contains(TR_KEY_download_dir)); // recognized: kept
}

TEST_F(PrefsFileTest, saveUsesLocalSessionSettingsForSessionValues)
{
    auto prefs = TestPrefs{};

    auto local = std::optional<tr::Settings>{ std::in_place };
    local->insert_or_assign(TR_KEY_download_dir, "/local/session/dl"sv);
    local->insert_or_assign(TR_KEY_speed_limit_down, int64_t{ 555 });

    auto const saved = save_and_reload(prefs, local);
    EXPECT_EQ(saved.value_if<std::string_view>(TR_KEY_download_dir), "/local/session/dl"sv);
    EXPECT_EQ(saved.value_if<int64_t>(TR_KEY_speed_limit_down), 555);
}

TEST_F(PrefsFileTest, saveLocalSessionSettingsOverrideExistingFile)
{
    auto stale = tr::Settings{};
    stale.insert_or_assign(TR_KEY_download_dir, "/stale/dl"sv);
    write_settings_file(stale);

    auto local = std::optional<tr::Settings>{ std::in_place };
    local->insert_or_assign(TR_KEY_download_dir, "/fresh/dl"sv);

    auto const prefs = TestPrefs{};
    auto const saved = save_and_reload(prefs, local);
    EXPECT_EQ(saved.value_if<std::string_view>(TR_KEY_download_dir), "/fresh/dl"sv);
}

TEST_F(PrefsFileTest, saveWithoutLocalSessionSettingsKeepsExistingFileSessionValues)
{
    // Simulates a remote session: the on-disk (local) session value must be
    // preserved rather than overwritten by the proxied remote value.
    auto on_disk = tr::Settings{};
    on_disk.insert_or_assign(TR_KEY_download_dir, "/on-disk/dl"sv);
    write_settings_file(on_disk);

    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_download_dir, "/remote/dl"s); // mutates the proxied session pref

    auto const saved = save_and_reload(prefs, std::nullopt);
    EXPECT_EQ(saved.value_if<std::string_view>(TR_KEY_download_dir), "/on-disk/dl"sv);
}

TEST_F(PrefsFileTest, saveWithoutExistingFileFillsSessionDefaults)
{
    ASSERT_FALSE(tr_sys_path_exists(settings_filename()));

    auto const prefs = TestPrefs{};
    auto const saved = save_and_reload(prefs, std::nullopt);

    // session defaults are filled in from the fallbacks even when remote
    auto const download_dir = saved.value_if<std::string_view>(TR_KEY_download_dir);
    ASSERT_TRUE(download_dir.has_value());
    EXPECT_FALSE(download_dir->empty());

    // live app prefs are present too
    EXPECT_TRUE(saved.contains(TR_KEY_show_statusbar));
}

TEST_F(PrefsFileTest, saveRoundTripsThroughConfigDirConstructor)
{
    auto const blocklist_date = std::chrono::sys_seconds{ std::chrono::seconds{ 1700000000 } };

    auto prefs = TestPrefs{};
    prefs.set(TR_KEY_main_window_height, 1234); // app-owned
    prefs.set(TR_KEY_sort_mode, SortMode::SortByQueue); // app-owned
    prefs.set(TR_KEY_blocklist_date, blocklist_date); // app-owned
    prefs.set(TR_KEY_filter_text, "volatile"s); // transient app-owned

    auto local = std::optional<tr::Settings>{ std::in_place };
    local->insert_or_assign(TR_KEY_download_dir, "/round/trip/dl"sv); // session-owned
    local->insert_or_assign(TR_KEY_speed_limit_down, int64_t{ 321 }); // session-owned

    prefs.save(sandbox_dir(), local);

    auto const reloaded = TestPrefs{ sandbox_dir() };
    EXPECT_EQ(reloaded.get<int>(TR_KEY_main_window_height), 1234);
    EXPECT_EQ(reloaded.get<SortMode>(TR_KEY_sort_mode), SortMode::SortByQueue);
    EXPECT_EQ(reloaded.get<std::chrono::sys_seconds>(TR_KEY_blocklist_date), blocklist_date);
    EXPECT_EQ(reloaded.get<std::string>(TR_KEY_download_dir), "/round/trip/dl");
    EXPECT_EQ(reloaded.get<int>(TR_KEY_speed_limit_down), 321);
    EXPECT_EQ(reloaded.get<std::string>(TR_KEY_filter_text), std::string{}); // not persisted
}
