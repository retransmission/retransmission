// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility> // std::cmp_equal

#ifdef _WIN32
#include <windows.h>
#endif

#include <fmt/format.h>

#include <simdutf.h>

#include <wildmat.h>

#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"

bool tr_wildmat(std::string_view text, std::string_view pattern)
{
    return wildmat::match(text, pattern);
}

char const* tr_strerror(int errnum)
{
    if (char const* const ret = strerror(errnum); ret != nullptr) {
        return ret;
    }

    return "Unknown Error";
}

// ---

std::string_view tr_strv_strip(std::string_view str)
{
    auto constexpr Test = [](auto ch) {
        return isspace(static_cast<unsigned char>(ch));
    };

    auto const it = std::ranges::find_if_not(str, Test);
    str.remove_prefix(std::ranges::distance(std::ranges::begin(str), it));

    auto const rit = std::ranges::find_if_not(std::ranges::rbegin(str), std::ranges::rend(str), Test);
    str.remove_suffix(std::ranges::distance(std::ranges::rbegin(str), rit));

    return str;
}

// ---

#if !(defined(__APPLE__) && defined(__clang__))

std::string tr_strv_to_utf8_string(std::string_view sv)
{
    return tr_strv_replace_invalid(sv);
}

#endif

std::string tr_strv_replace_invalid(std::string_view sv, char32_t replacement)
{
    // stripping characters after first \0
    if (auto const first_null = sv.find('\0'); first_null != std::string_view::npos) {
        sv = { std::data(sv), first_null };
    }

    // pre-encode the replacement code point as UTF-8
    auto replacement_buf = std::array<char, 4>{};
    auto const replacement_len = simdutf::convert_utf32_to_utf8(&replacement, 1, std::data(replacement_buf));
    auto const replacement_utf8 = std::string_view{ std::data(replacement_buf), replacement_len };

    auto out = std::string{};
    out.reserve(std::size(sv));
    while (!std::empty(sv)) {
        auto const result = simdutf::validate_utf8_with_errors(std::data(sv), std::size(sv));
        if (result.error == simdutf::error_code::SUCCESS) {
            out += sv;
            break;
        }
        // keep the valid prefix, replace the single offending byte, then resync
        out += sv.substr(0, result.count);
        out += replacement_utf8;
        sv.remove_prefix(result.count + 1U);
    }
    return out;
}

#ifdef _WIN32

std::string tr_win32_native_to_utf8(std::wstring_view in)
{
    auto out = std::string{};
    out.resize(WideCharToMultiByte(CP_UTF8, 0, std::data(in), static_cast<int>(std::size(in)), nullptr, 0, nullptr, nullptr));
    [[maybe_unused]] auto len = WideCharToMultiByte(
        CP_UTF8,
        0,
        std::data(in),
        static_cast<int>(std::size(in)),
        std::data(out),
        static_cast<int>(std::size(out)),
        nullptr,
        nullptr);
    TR_ASSERT(std::cmp_equal(len, std::size(out)));
    return out;
}

std::wstring tr_win32_utf8_to_native(std::string_view in)
{
    auto out = std::wstring{};
    out.resize(MultiByteToWideChar(CP_UTF8, 0, std::data(in), static_cast<int>(std::size(in)), nullptr, 0));
    [[maybe_unused]] auto len = MultiByteToWideChar(
        CP_UTF8,
        0,
        std::data(in),
        static_cast<int>(std::size(in)),
        std::data(out),
        static_cast<int>(std::size(out)));
    TR_ASSERT(std::cmp_equal(len, std::size(out)));
    return out;
}

std::string tr_win32_format_message(uint32_t code)
{
    wchar_t* wide_text = nullptr;
    auto const wide_size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPWSTR>(&wide_text),
        0,
        nullptr);

    if (wide_size == 0) {
        return fmt::format("Unknown error ({:#08x})", code);
    }

    auto text = std::string{};

    if (wide_size != 0 && wide_text != nullptr) {
        text = tr_win32_native_to_utf8({ wide_text, wide_size });
    }

    LocalFree(wide_text);

    // Most (all?) messages contain "\r\n" in the end, chop it
    while (!std::empty(text) && isspace(text.back()) != 0) {
        text.resize(text.size() - 1);
    }

    return text;
}

#endif

std::string_view::size_type tr_strv_find_invalid_utf8(std::string_view const sv)
{
    auto const result = simdutf::validate_utf8_with_errors(std::data(sv), std::size(sv));
    return result.error == simdutf::error_code::SUCCESS ? std::string_view::npos : result.count;
}
