/*
 * wildmat.h -- shell-style wildcard matching, e.g. match("foo.txt", "*.t?t").
 *
 * A header-only, constexpr C++17 rewrite of Rich $alz's classic
 * public-domain wildmat.c (1986; star-loop revision April 1991).
 *
 * Pattern syntax:
 *
 *   *      matches any sequence of characters, including none
 *   ?      matches any single character
 *   [...]  matches any single character in the set; a set beginning with
 *          '^' matches any character NOT in it. "a-z" is an inclusive
 *          range. ']' ends the set unless it follows a '-', which
 *          consumes it as a range endpoint.
 *   \x     matches the character x literally, e.g. "\*" matches "*"
 *   x      any other character matches itself
 *
 * The pattern must cover the entire text: "*at" matches "bat" but "at"
 * does not.
 *
 * Differences from the original:
 *
 *   - Iterative instead of recursive. On a mismatch after a '*', it
 *     backtracks to just after that star and lets the star swallow one
 *     more character. This matches the same strings, needs no ABORT
 *     trick to avoid pathological backtracking -- the worst case is
 *     O(text length * pattern length) -- and is constexpr-friendly.
 *   - Operates on string_views, so inputs need not be zero-terminated.
 *   - Malformed patterns never match. The original documented that an
 *     unterminated set such as "foo[a-" "could cause a segmentation
 *     violation"; here it just returns false.
 *   - Characters compare as unsigned char on every platform, keeping
 *     the original's "8bit clean" promise even where char is signed.
 */

#pragma once

#include <cstddef>
#include <string_view>

namespace wildmat
{

namespace detail
{

// Compare characters as unsigned so that the matcher is 8-bit clean
// regardless of whether the platform's plain char is signed.
[[nodiscard]] constexpr unsigned int uchar(char ch) noexcept
{
    return static_cast<unsigned char>(ch);
}

// returned by match_set/match_element when the element does not match
inline constexpr auto no_match = std::string_view::npos;

// Match the character set whose '[' is at pattern[pos] against ch.
// Returns the pattern position just past the set's closing ']' on a
// match, or no_match. A set that reaches the end of the pattern without
// finding its closing ']' is malformed and matches nothing.
[[nodiscard]] constexpr std::size_t match_set(std::string_view pattern, std::size_t pos, unsigned int ch) noexcept
{
    ++pos; // skip the '['

    bool const negate = pos < pattern.size() && pattern[pos] == '^';
    if (negate)
    {
        ++pos;
    }

    auto matched = false;
    auto prev = 256U; // out-of-range bound: a range with no preceding character matches nothing
    while (pos < pattern.size() && pattern[pos] != ']')
    {
        if (pattern[pos] == '-') // range from the previous through the next character
        {
            if (++pos == pattern.size())
            {
                break; // caught by the unterminated-set check below
            }
            matched |= prev <= ch && ch <= uchar(pattern[pos]);
        }
        else
        {
            matched |= ch == uchar(pattern[pos]);
        }
        prev = uchar(pattern[pos]);
        ++pos;
    }

    if (pos == pattern.size()) // no closing ']'
    {
        return no_match;
    }

    return matched != negate ? pos + 1 : no_match;
}

// Match the single pattern element at pattern[pos] (never a '*') against
// the text character text_ch. Returns the pattern position just past the
// element on a match, or no_match.
[[nodiscard]] constexpr std::size_t match_element(std::string_view pattern, std::size_t pos, char text_ch) noexcept
{
    auto const ch = uchar(text_ch);

    switch (pattern[pos])
    {
    case '?': // any character
        return pos + 1;

    case '\\': // escape: the next pattern character, literally.
        // (a trailing backslash matches nothing)
        return pos + 1 < pattern.size() && ch == uchar(pattern[pos + 1]) ? pos + 2 : no_match;

    case '[':
        return match_set(pattern, pos, ch);

    default: // a literal
        return ch == uchar(pattern[pos]) ? pos + 1 : no_match;
    }
}

} // namespace detail

/**
 * Shell-style wildcard test: does `pattern` match all of `text`?
 * e.g. `match("foo.torrent", "*.torrent")` returns true.
 */
[[nodiscard]] constexpr bool match(std::string_view text, std::string_view pattern) noexcept
{
    auto t = std::size_t{ 0 }; // position in text
    auto p = std::size_t{ 0 }; // position in pattern

    // the most recent '*', for backtracking: when matching fails past a
    // star, rewind to just after it and let it swallow one more character
    auto star_p = std::string_view::npos;
    auto star_t = std::size_t{ 0 };

    while (t < text.size())
    {
        if (p < pattern.size())
        {
            if (pattern[p] == '*')
            {
                while (p < pattern.size() && pattern[p] == '*') // consecutive stars act as one
                {
                    ++p;
                }

                if (p == pattern.size()) // a trailing star matches the rest
                {
                    return true;
                }

                star_p = p;
                star_t = t;
                continue;
            }

            if (auto const next = detail::match_element(pattern, p, text[t]); next != detail::no_match)
            {
                p = next;
                ++t;
                continue;
            }
        }

        if (star_p == std::string_view::npos)
        {
            return false; // mismatch, and no star to fall back on
        }

        p = star_p;
        t = ++star_t;
    }

    // out of text: only trailing stars may remain in the pattern
    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.size();
}

} // namespace wildmat
