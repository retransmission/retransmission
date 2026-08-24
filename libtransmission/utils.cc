// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // for std::sort, std::transform
#include <array> // std::array
#include <cfloat> // DBL_DIG
#include <charconv> // std::from_chars()
#include <chrono>
#include <concepts>
#include <cstdint> // SIZE_MAX
#include <ctime>
#include <exception>
#include <iostream>
#include <iterator> // for std::back_inserter
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept> // std::runtime_error
#include <string>
#include <string_view>
#include <system_error>
#include <utility> // std::cmp_equal
#include <vector>

#ifdef _WIN32
#include <windows.h> /* Sleep(), GetEnvironmentVariable() */
#include <ws2tcpip.h> /* htonl(), ntohl() */

#include <shellapi.h> /* CommandLineToArgv() */
#else
#include <arpa/inet.h>
#endif

#include <curl/curl.h>

#include <fmt/format.h>

#include <fast_float/fast_float.h>

#include "libtransmission/mime-types.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

using namespace std::literals;

std::atomic<time_t> tr::detail::tr_time::current_time = {};

// ---

#if defined(_WIN32) && defined(__clang_analyzer__)
// See https://github.com/llvm/llvm-project/issues/44701
#define WORKAROUND_CLANG_TIDY_GH44701
#endif

std::optional<std::locale> tr_locale_set_global(char const* locale_name) noexcept
{
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    try
#endif
    {
        return tr_locale_set_global(std::locale{ locale_name });
    }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    catch (std::runtime_error const&) {
        return {};
    }
#endif
}

std::optional<std::locale> tr_locale_set_global(std::locale const& locale) noexcept
{
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    try
#endif
    {
        auto old_locale = std::locale::global(locale);

        std::cout.imbue(std::locale{});
        std::cerr.imbue(std::locale{});

        return old_locale;
    }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    catch (std::exception const&) {
        return {};
    }
#endif
}

// ---

uint64_t tr_time_msec()
{
    return std::chrono::system_clock::now().time_since_epoch() / 1ms;
}

// ---

double tr_getRatio(uint64_t numerator, uint64_t denominator)
{
    if (denominator > 0) {
        return static_cast<double>(numerator) / static_cast<double>(denominator);
    }

    if (numerator > 0) {
        return TR_RATIO_INF;
    }

    return TR_RATIO_NA;
}

#ifdef _WIN32

namespace
{
namespace tr_main_win32_impl
{

std::optional<std::vector<std::string>> win32MakeUtf8Argv()
{
    int argc = 0;
    auto argv = std::vector<std::string>{};
    if (wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc); wargv != nullptr) {
        for (int i = 0; i < argc; ++i) {
            if (wargv[i] == nullptr) {
                break;
            }

            auto str = tr_win32_native_to_utf8(wargv[i]);
            if (std::empty(str)) {
                break;
            }

            argv.emplace_back(std::move(str));
        }

        LocalFree(reinterpret_cast<HLOCAL>(wargv));
    }

    if (std::cmp_equal(std::size(argv), argc)) {
        return argv;
    }

    return {};
}

} // namespace tr_main_win32_impl
} // namespace

int tr_main_win32(int argc, char** argv, int (*real_main)(int, char**))
{
    using namespace tr_main_win32_impl;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // build an argv from GetCommandLineW + CommandLineToArgvW
    if (auto argv_strs = win32MakeUtf8Argv(); argv_strs) {
        auto argv_cstrs = std::vector<char*>{};
        argv_cstrs.reserve(std::size(*argv_strs));
        std::transform(std::begin(*argv_strs), std::end(*argv_strs), std::back_inserter(argv_cstrs), [](auto& str) {
            return std::data(str);
        });
        argv_cstrs.push_back(nullptr); // argv is nullptr-terminated
        return (*real_main)(static_cast<int>(std::size(*argv_strs)), std::data(argv_cstrs));
    }

    return (*real_main)(argc, argv);
}

#endif

// ---

double tr_truncd(double x, int decimal_places)
{
    auto buf = std::array<char, 128>{};
    auto const [out, len] = fmt::format_to_n(std::data(buf), std::size(buf) - 1, "{:.{}f}", x, DBL_DIG);
    *out = '\0';

    if (auto* const pt = strchr(std::data(buf), '.'); pt != nullptr) {
        pt[decimal_places != 0 ? decimal_places + 1 : 0] = '\0';
    }

    return tr_num_parse<double>(std::data(buf)).value_or(0.0);
}

std::string tr_strpercent(double x)
{
    if (x < 5.0) {
        return fmt::format("{:.2Lf}", tr_truncd(x, 2));
    }

    if (x < 100.0) {
        return fmt::format("{:.1Lf}", tr_truncd(x, 1));
    }

    return fmt::format("{:.0Lf}", x);
}

std::string tr_strratio(double ratio, std::string_view const none, std::string_view const infinity)
{
    if (static_cast<int>(ratio) == TR_RATIO_NA) {
        return std::string{ none };
    }

    if (static_cast<int>(ratio) == TR_RATIO_INF) {
        return std::string{ infinity };
    }

    return tr_strpercent(ratio);
}

// ---

uint64_t tr_htonll(uint64_t hostlonglong)
{
#ifdef HAVE_HTONLL

    return htonll(hostlonglong);

#else

    /* fallback code by bdonlan at https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
    union {
        std::array<uint32_t, 2> lx;
        uint64_t llx;
    } u = {};
    u.lx[0] = htonl(hostlonglong >> 32);
    u.lx[1] = htonl(hostlonglong & 0xFFFFFFFFULL);
    return u.llx;

#endif
}

uint64_t tr_ntohll(uint64_t netlonglong)
{
#ifdef HAVE_NTOHLL

    return ntohll(netlonglong);

#else

    /* fallback code by bdonlan at https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
    union {
        std::array<uint32_t, 2> lx;
        uint64_t llx;
    } u = {};
    u.llx = netlonglong;
    return (static_cast<uint64_t>(ntohl(u.lx[0])) << 32) | static_cast<uint64_t>(ntohl(u.lx[1]));

#endif
}

// ---

namespace
{
namespace tr_net_init_impl
{
class tr_net_init_mgr
{
private:
    tr_net_init_mgr()
    {
        // try to init curl with default settings (currently ssl support + win32 sockets)
        // but if that fails, we need to init win32 sockets as a bare minimum
        if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
            curl_global_init(CURL_GLOBAL_WIN32);
        }
    }

public:
    tr_net_init_mgr& operator=(tr_net_init_mgr&&) = delete;
    tr_net_init_mgr& operator=(tr_net_init_mgr const&) = delete;
    tr_net_init_mgr(tr_net_init_mgr&&) = delete;
    tr_net_init_mgr(tr_net_init_mgr const&) = delete;

    ~tr_net_init_mgr()
    {
        curl_global_cleanup();
    }

    static void create()
    {
        if (!instance) {
            instance = std::unique_ptr<tr_net_init_mgr>{ new tr_net_init_mgr };
        }
    }

private:
    static std::unique_ptr<tr_net_init_mgr> instance;
};

std::unique_ptr<tr_net_init_mgr> tr_net_init_mgr::instance;

} // namespace tr_net_init_impl
} // namespace

void tr_lib_init()
{
    static auto once = std::once_flag{};
    std::call_once(once, [] { tr_net_init_impl::tr_net_init_mgr::create(); });
}

// --- mime-type

std::string_view tr_get_mime_type_for_filename(std::string_view filename)
{
    static auto constexpr Project = [](mime_type_suffix const& entry) {
        return entry.suffix;
    };

    if (auto const pos = filename.rfind('.'); pos != std::string_view::npos) {
        auto const suffix_lc = tr_strlower(filename.substr(pos + 1));
        auto const it = std::ranges::lower_bound(MimeTypeSuffixes, suffix_lc, {}, Project);
        if (it != std::end(MimeTypeSuffixes) && suffix_lc == it->suffix) {
            std::string_view mime_type = it->mime_type;

            // https://github.com/transmission/transmission/issues/5965#issuecomment-1704421231
            // An mp4 file's correct mime-type depends on the codecs used in the file,
            // which we have no way of inspecting and which might not be downloaded yet.
            // Let's use `video/mp4` since that's by far the most common use case for torrents.
            if (mime_type == "application/mp4") {
                mime_type = "video/mp4";
            }

            return mime_type;
        }
    }

    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
    // application/octet-stream is the default value.
    // An unknown file type should use this type.
    auto constexpr Fallback = "application/octet-stream"sv;
    return Fallback;
}

// --- tr_num_parse()

template<std::integral T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* remainder, int base)
{
    auto val = T{};
    auto const* const begin_ch = std::data(str);
    auto const* const end_ch = begin_ch + std::size(str);
    auto const result = std::from_chars(begin_ch, end_ch, val, base);
    if (result.ec != std::errc{}) {
        return std::nullopt;
    }
    if (remainder != nullptr) {
        *remainder = str;
        remainder->remove_prefix(result.ptr - std::data(str));
    }
    return val;
}

template std::optional<long long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<int> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<char> tr_num_parse(std::string_view str, std::string_view* remainder, int base);

template std::optional<unsigned long long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned int> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned short> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned char> tr_num_parse(std::string_view str, std::string_view* remainder, int base);

template<std::floating_point T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* remainder)
{
    auto const* const begin_ch = std::data(str);
    auto const* const end_ch = begin_ch + std::size(str);
    auto val = T{};
    auto const result = fast_float::from_chars(begin_ch, end_ch, val);
    if (result.ec != std::errc{}) {
        return std::nullopt;
    }
    if (remainder != nullptr) {
        *remainder = str;
        remainder->remove_prefix(result.ptr - std::data(str));
    }
    return val;
}

template std::optional<double> tr_num_parse(std::string_view sv, std::string_view* remainder);
