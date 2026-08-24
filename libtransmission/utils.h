// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <atomic>
#include <concepts>
#include <cstddef> // size_t
#include <cstdint> // uint8_t, uint32_t, uint64_t
#include <ctime> // time_t
#include <locale>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @addtogroup utils Utilities
 * @{
 */

#ifdef ENABLE_GETTEXT
#include <libintl.h>
#define _ gettext
#define tr_ngettext ngettext
#else
#define _(a) (a)
#define tr_ngettext(singular, plural, count) ((count) == 1 ? (singular) : (plural))
#endif

std::optional<std::locale> tr_locale_set_global(char const* locale_name) noexcept;

std::optional<std::locale> tr_locale_set_global(std::locale const& locale) noexcept;

// ---

[[nodiscard]] std::string_view tr_get_mime_type_for_filename(std::string_view filename);

/** @brief return the current date in milliseconds */
[[nodiscard]] uint64_t tr_time_msec();

#ifdef _WIN32

int tr_main_win32(int argc, char** argv, int (*real_main)(int, char**));

#define tr_main(...) \
    main_impl(__VA_ARGS__); \
    int main(int argc, char* argv[]) \
    { \
        return tr_main_win32(argc, argv, &main_impl); \
    } \
    int main_impl(__VA_ARGS__)

#else

#define tr_main main

#endif

// ---

template<typename T>
[[nodiscard]] constexpr int tr_compare_3way(T const& left, T const& right)
{
    if (left < right) {
        return -1;
    }

    if (right < left) {
        return 1;
    }

    return 0;
}

// ---

/**
 * Calculates the number of bytes needed to store `bit_count` bits.
 */
[[nodiscard]] constexpr size_t tr_bytes_needed(size_t const bit_count) noexcept
{
    return (bit_count >> 3U) + ((bit_count & 7U) != 0U ? 1U : 0U);
}

// ---

/**
 * Folds `b` into the running hash `a`, so a sequence of values can be
 * hashed one at a time. Order-sensitive: the same values in a different
 * order give a different hash.
 */
// https://stackoverflow.com/a/27952689/11390656
[[nodiscard]] constexpr size_t tr_hash_combine(size_t const a, size_t const b) noexcept
{
    return a ^ (b + 0x9e3779b9U + (a << 6U) + (a >> 2U));
}

// ---

template<std::integral T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* setme_remainder = nullptr, int base = 10);

template<std::floating_point T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* setme_remainder = nullptr);

/**
 * @brief truncate a double value at a given number of decimal places.
 *
 * this can be used to prevent a `printf()` call from rounding up:
 * call with the `decimal_places` argument equal to the number of
 * decimal places in the `printf()`'s precision:
 *
 * - printf("%.2f%%", 99.999) ==> "100.00%"
 *
 * - printf("%.2f%%", tr_truncd(99.999, 2)) ==> "99.99%"
 *             ^                        ^
 *             |   These should match   |
 *             +------------------------+
 */
[[nodiscard]] double tr_truncd(double x, int decimal_places);

/* return a percent formatted string of either x.xx, xx.x or xxx */
[[nodiscard]] std::string tr_strpercent(double x);

/** @brief return `TR_RATIO_NA`, `TR_RATIO_INF`, or a number in [0..1]
    @return `TR_RATIO_NA`, `TR_RATIO_INF`, or a number in [0..1] */
[[nodiscard]] double tr_getRatio(uint64_t numerator, uint64_t denominator);

/** @param ratio    the ratio to convert to a string
    @param infinity the string representation of "infinity" */
[[nodiscard]] std::string tr_strratio(double ratio, std::string_view none, std::string_view infinity);

// ---

namespace tr::detail::tr_time
{
// atomic so that readers on other threads, e.g. the verify and web
// threads, get a coherent value without synchronizing with the session
// thread's once-per-second update
extern std::atomic<time_t> current_time;
} // namespace tr::detail::tr_time

/**
 * @brief very inexpensive form of time(nullptr)
 * @return the current epoch time in seconds
 *
 * This function returns a second counter that is updated once per second.
 * If something blocks the libtransmission thread for more than a second,
 * that counter may be thrown off, so this function is not guaranteed
 * to always be accurate. However, it is *much* faster when 100% accuracy
 * isn't needed
 */
[[nodiscard]] static inline time_t tr_time() noexcept
{
    return tr::detail::tr_time::current_time.load(std::memory_order_relaxed);
}

/** @brief Private libtransmission function to update `tr_time()`'s counter */
inline void tr_timeUpdate(time_t now) noexcept
{
    tr::detail::tr_time::current_time.store(now, std::memory_order_relaxed);
}

/** @brief Portability wrapper for `htonll()` that uses the system implementation if available */
[[nodiscard]] uint64_t tr_htonll(uint64_t hostlonglong);

/** @brief Portability wrapper for `ntohll()` that uses the system implementation if available */
[[nodiscard]] uint64_t tr_ntohll(uint64_t netlonglong);

// ---

/** @brief Initialise libtransmission for each app */
void tr_lib_init();
