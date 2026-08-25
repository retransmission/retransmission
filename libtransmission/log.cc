// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef> // size_t
#include <deque>
#include <iterator> // back_insert_iterator, empty
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h> // GetTimeZoneInformation
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <small/map.hpp>

#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/macros.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{
// TODO(c++20): switch to std::chrono::zoned_time, GCC 13.1, clang 19 (or clang 21 with std::format), fmt 11.2
template<typename T>
inline constexpr bool HasTmGmtoffV = requires(T t) { t.tm_gmtoff; };

tr_log_level log_level = TR_LOG_ERROR;

class tr_log_queue
{
public:
    [[nodiscard]] tr_log_messages take()
    {
        auto const lock = std::scoped_lock{ queue_mutex_ };

        return std::exchange(queue_, {});
    }

    void append(tr_log_message&& message)
    {
        auto const lock = std::scoped_lock{ queue_mutex_ };

        queue_.push_back(std::move(message));
        if (std::size(queue_) > MaxQueueLength) {
            queue_.pop_front();
        }
    }

    constexpr void set_enabled(bool const is_enabled) noexcept
    {
        is_enabled_ = is_enabled;
    }

    [[nodiscard]] constexpr bool is_enabled() const noexcept
    {
        return is_enabled_;
    }

private:
    static constexpr auto MaxQueueLength = 10000U;

    std::mutex queue_mutex_;

    tr_log_messages queue_;

    bool is_enabled_ = false;
};

auto log_queue = tr_log_queue{};

// ---

void logAddImpl(
    [[maybe_unused]] std::string_view const file,
    [[maybe_unused]] long const line,
    [[maybe_unused]] tr_log_level const level,
    std::string&& msg,
    [[maybe_unused]] std::string_view const name)
{
    if (std::empty(msg)) {
        return;
    }

#if defined(__ANDROID__)

    int prio;

    switch (level) {
    case TR_LOG_OFF:
        prio = ANDROID_LOG_SILENT;
        break;
    case TR_LOG_CRITICAL:
        prio = ANDROID_LOG_FATAL;
        break;
    case TR_LOG_ERROR:
        prio = ANDROID_LOG_ERROR;
        break;
    case TR_LOG_WARN:
        prio = ANDROID_LOG_WARN;
        break;
    case TR_LOG_INFO:
        prio = ANDROID_LOG_INFO;
        break;
    case TR_LOG_DEBUG:
        prio = ANDROID_LOG_DEBUG;
        break;
    case TR_LOG_TRACE:
        prio = ANDROID_LOG_VERBOSE;
    }

#ifdef NDEBUG
    auto const szmsg = fmt::format("{:s}", msg);
#else
    auto const szmsg = fmt::format("[{:s}:{:d}] {:s}", file, line, msg);
#endif
    __android_log_write(prio, TR_PROJ_APPNAME, szmsg.c_str());

#else

    if (log_queue.is_enabled()) {
        log_queue.append(
            {
                .level = level,
                .file = file,
                .line = line,
                .when = std::chrono::system_clock::now(),
                .name = std::string{ name },
                .message = std::move(msg),
            });
    } else {
        auto buf = std::array<char, 64U>{};
        auto const timestr = tr_logGetTimeStr(std::data(buf), std::size(buf));

        if (std::empty(name)) {
            fmt::print(stderr, "[{:s}] {:s}\n", timestr, msg);
        } else {
            fmt::print("[{:s}] {:s}: {:s}\n", timestr, name, msg);
        }
    }
#endif
}

} // unnamed namespace

tr_log_level tr_logGetLevel()
{
    return log_level;
}

bool tr_logLevelIsActive(tr_log_level const level)
{
    return log_level >= level;
}

void tr_logSetLevel(tr_log_level const level)
{
    log_level = level;
}

void tr_logSetQueueEnabled(bool const is_enabled)
{
    log_queue.set_enabled(is_enabled);
}

tr_log_messages tr_logGetQueue()
{
    return log_queue.take();
}

void tr_logClearQueue()
{
    (void)tr_logGetQueue();
}

// ---

std::string_view tr_logGetTimeStr(std::chrono::system_clock::time_point const now, char* const buf, size_t const buflen)
{
    auto* walk = buf;
    auto const now_time_t = std::chrono::system_clock::to_time_t(now);
    auto const now_tm = *std::localtime(&now_time_t);
    static bool constexpr HasTmGmtoff = HasTmGmtoffV<std::tm>;
    static auto constexpr Fmt = HasTmGmtoff ? "{0:%FT%R:}{1:%S}{0:%z}"sv : "{0:%FT%R:}{1:%S}"sv;
    walk = fmt::format_to_n(walk, buflen, Fmt, now_tm, std::chrono::time_point_cast<std::chrono::milliseconds>(now)).out;
#ifdef _WIN32
    if (auto tz_info = TIME_ZONE_INFORMATION{}; GetTimeZoneInformation(&tz_info) != TIME_ZONE_ID_INVALID) {
        // https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-gettimezoneinformation
        // All translations between UTC time and local time are based on the following formula:
        //     UTC = local time + bias
        // The bias is the difference, in minutes, between UTC time and local time.
        auto const offset = tz_info.Bias < 0 ? -tz_info.Bias : tz_info.Bias;
        walk = fmt::format_to_n(
                   walk,
                   buflen - (walk - buf),
                   "{:c}{:02d}{:02d}",
                   tz_info.Bias < 0 ? '+' : '-',
                   offset / 60,
                   offset % 60)
                   .out;
    }
#endif
    return { buf, static_cast<size_t>(walk - buf) };
}

std::string_view tr_logGetTimeStr(char* buf, size_t buflen)
{
    auto const a = std::chrono::system_clock::now();
    return tr_logGetTimeStr(a, buf, buflen);
}

void tr_logAddMessage(char const* file, long line, tr_log_level level, std::string&& msg, std::string_view name)
{
    TR_ASSERT(!std::empty(msg));

    // strip source path to only include the filename
    auto filename = tr_sys_path_basename(file);
    if (std::empty(filename)) {
        filename = "?"sv;
    }

    auto name_fallback = std::string{};
    if (std::empty(name)) {
        name_fallback = fmt::format("{:s}:{:d}", filename, line);
        name = name_fallback;
    }

    // message logging shouldn't affect errno
    int const err = errno;

    // skip unwanted messages
    if (!tr_logLevelIsActive(level)) {
        errno = err;
        return;
    }

    // don't log the same warning ad infinitum.
    // at some point, it stops being useful.
    bool last_one = false;
    if (level == TR_LOG_CRITICAL || level == TR_LOG_ERROR || level == TR_LOG_WARN) {
        static auto constexpr MaxRepeat = size_t{ 30 };
        static auto* const counts = new small::map<std::pair<std::string_view, long>, size_t>{};

        auto& count = (*counts)[std::make_pair(filename, line)];
        ++count;
        last_one = count == MaxRepeat;
        if (count > MaxRepeat) {
            errno = err;
            return;
        }
    }

    // log the messages
    logAddImpl(filename, line, level, std::move(msg), name);
    if (last_one) {
        char const* final_msg = _("Too many messages like this! I won't log this message anymore this session.");
        logAddImpl(filename, line, level, final_msg, name);
    }

    errno = err;
}

// ---

namespace
{

auto constexpr LogKeys = std::to_array<std::pair<std::string_view, tr_log_level>>({
    { "off", TR_LOG_OFF },
    { "critical", TR_LOG_CRITICAL },
    { "error", TR_LOG_ERROR },
    { "warn", TR_LOG_WARN },
    { "info", TR_LOG_INFO },
    { "debug", TR_LOG_DEBUG },
    { "trace", TR_LOG_TRACE },
});

bool constexpr keysAreOrdered()
{
    for (size_t i = 0, n = std::size(LogKeys); i < n; ++i) {
        if (LogKeys[i].second != static_cast<tr_log_level>(i)) {
            return false;
        }
    }

    return true;
}

static_assert(keysAreOrdered());

} // unnamed namespace

std::optional<tr_log_level> tr_logGetLevelFromKey(std::string_view key_in)
{
    auto const key = tr_strlower(tr_strv_strip(key_in));

    for (auto const& [name, level] : LogKeys) {
        if (key == name) {
            return level;
        }
    }

    return std::nullopt;
}
