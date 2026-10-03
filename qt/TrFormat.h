// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <concepts>
#include <cstdint> // uint64_t
#include <locale>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <QtCore/QString>

// Qt's strings come from the shared gettext catalog,
// through the translator that tr::app::l10n::use_catalogs() installs.
// xgettext extracts the text in these macros, with the keywords in po/POTFILES.in.

// Translated text: TR_TEXT("Seeding complete")
#define TR_TEXT(msgid) ::trqt::detail::text(msgid)

// Translated text whose English needs a context to tell its meanings apart:
// TR_TEXT_C("Verb", "Seeding")
#define TR_TEXT_C(context, msgid) ::trqt::detail::textInContext(context, msgid)

// Translated text in the plural form for `n`:
// TR_TEXT_N("{count:L} file", "{count:L} files", count)
// Text that doesn't show the count picks its wording with `count == 1` instead,
// because a language's form for one can cover other counts, e.g. 21 in Russian.
#define TR_TEXT_N(msgid, msgid_plural, n) ::trqt::detail::pluralText(msgid, msgid_plural, n)

// Translated text for a widget that underlines a mnemonic, such as a menu item, button or buddy label:
// TR_MNEMONIC("_File") returns "&File".
// The catalog marks a mnemonic with "_" and writes a literal underscore as "__", as GTK does.
#define TR_MNEMONIC(msgid) ::trqt::detail::mnemonicText(msgid)

// Translated text, formatted with {fmt} named arguments:
// TR_FORMAT("Created by {creator}", fmt::arg("creator", creator))
// A translation whose fields don't fit the arguments is ignored in favor of the English text,
// because {fmt} is built without exceptions and aborts on a bad format string.
#define TR_FORMAT(msgid, ...) ::trqt::detail::formatTranslation(TR_TEXT(msgid), msgid, __VA_ARGS__)

// Like TR_FORMAT, in the plural form for `n`.
#define TR_FORMAT_N(msgid, msgid_plural, n, ...) ::trqt::detail::formatPlural(msgid, msgid_plural, n, __VA_ARGS__)

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

// Adds fmtLocale()'s number formatting to the C++ global locale,
// which {fmt} uses when it is passed no locale, as in libtransmission and libtransmission-app.
void setGlobalFmtLocale();

// Splits `translation` around its `name` field, e.g. into a spin box's prefix and suffix.
// A translation without exactly that one field is ignored in favor of `source`.
[[nodiscard]] std::pair<QString, QString> splitAtField(QString const& translation, char const* source, std::string_view name);

// Translates text from a Qt Designer file. uic calls this, as its --tr option asks.
// Designer files mark mnemonics with "_", as TR_MNEMONIC does, and use "_" for nothing else,
// so text with a "_" is for a widget that underlines a mnemonic.
// A Designer disambiguation reaches this as the text's context.
[[nodiscard]] QString uiText(char const* msgid, char const* context);

// What the macros above call. Code calls the macros, which xgettext extracts.
namespace detail
{

[[nodiscard]] QString text(char const* msgid);

[[nodiscard]] QString textInContext(char const* context, char const* msgid);

[[nodiscard]] QString pluralText(char const* msgid, char const* msgid_plural, uint64_t n);

template<std::integral T>
[[nodiscard]] QString pluralText(char const* const msgid, char const* const msgid_plural, T const n)
{
    return pluralText(msgid, msgid_plural, static_cast<uint64_t>(n));
}

[[nodiscard]] QString mnemonicText(char const* msgid);

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

// Formats `translation` with {fmt} named arguments,
// or formats `source` if `translation` has a field that doesn't fit them.
// Named arguments are taken by value: {fmt} registers the names of non-const ones only.
template<NamedArg... Args>
[[nodiscard]] QString formatTranslation(QString const& translation, char const* source, Args... args)
{
    auto const arg_infos = std::array<ArgInfo, sizeof...(Args)>{
        ArgInfo{ args.name, IsNumber<std::remove_cvref_t<decltype(args.value)>> }...
    };
    return formatTranslation(translation, source, arg_infos, fmt::vargs<Args...>{ { args... } });
}

template<std::integral T, NamedArg... Args>
[[nodiscard]] QString formatPlural(char const* const msgid, char const* const msgid_plural, T const n, Args... args)
{
    return formatTranslation(pluralText(msgid, msgid_plural, n), n == 1 ? msgid : msgid_plural, args...);
}

} // namespace detail

} // namespace trqt
