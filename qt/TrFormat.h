// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <concepts>
#include <locale>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <QtCore/QString>

// Translates `source` with the calling class's tr(),
// then formats it with {fmt} named arguments:
// TR_FORMAT("Created by {creator}", fmt::arg("creator", creator))
//
// A translation whose fields don't fit the arguments is ignored in favor of `source`,
// because {fmt} is built without exceptions and aborts on a bad format string.
//
// lupdate extracts these strings only when run with
// -tr-function-alias QT_TR_NOOP+=TR_FORMAT,QT_TR_N_NOOP+=TR_FORMAT_N
#define TR_FORMAT(source, ...) ::trqt::formatTranslation(tr(source), source, __VA_ARGS__)

// Like TR_FORMAT, with the plural form picked by `n`.
#define TR_FORMAT_N(source, n, ...) ::trqt::formatTranslation(tr(source, nullptr, n), source, __VA_ARGS__)

template<>
struct fmt::formatter<QString> : formatter<std::string_view> {
    template<typename FormatContext>
    auto format(QString const& str, FormatContext& ctx) const
    {
        auto const utf8 = str.toUtf8();
        return formatter<std::string_view>::format(std::string_view{ utf8.constData(), static_cast<size_t>(utf8.size()) }, ctx);
    }
};

namespace trqt
{

// The locale that {fmt} uses for "L" fields.
// It formats numbers with QLocale{}, so they match QString::arg("%L1").
[[nodiscard]] std::locale const& fmtLocale();

// Splits `translation` around its `name` field, e.g. into a spin box's prefix and suffix.
// A translation without exactly that one field is ignored in favor of `source`.
[[nodiscard]] std::pair<QString, QString> splitAtField(QString const& translation, char const* source, std::string_view name);

namespace format_detail
{

template<typename T>
concept NamedArg = requires(T const& arg) {
    { arg.name } -> std::convertible_to<char const*>;
    arg.value;
};

// {fmt}'s "L" spec suits numbers, but not bool or characters.
template<typename T>
inline constexpr bool IsNumber = std::is_floating_point_v<T> ||
    (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, wchar_t> &&
     !std::is_same_v<T, char8_t> && !std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>);

struct ArgInfo {
    std::string_view name;
    bool is_number = false;
};

[[nodiscard]] QString formatTranslation(
    QString const& translation,
    char const* source,
    std::span<ArgInfo const> arg_infos,
    fmt::format_args args);

} // namespace format_detail

// Formats `translation` with {fmt} named arguments,
// or formats `source` if `translation` has a field that doesn't fit them.
// Named arguments are taken by value: {fmt} registers the names of non-const ones only.
template<format_detail::NamedArg... Args>
[[nodiscard]] QString formatTranslation(QString const& translation, char const* source, Args... args)
{
    auto const arg_infos = std::array<format_detail::ArgInfo, sizeof...(Args)>{
        format_detail::ArgInfo{ args.name, format_detail::IsNumber<std::remove_cvref_t<decltype(args.value)>> }...
    };
    return format_detail::formatTranslation(translation, source, arg_infos, fmt::vargs<Args...>{ { args... } });
}

} // namespace trqt
