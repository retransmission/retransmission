// This file Copyright © Mnemosaic LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosaic LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <concepts>
#include <locale>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <QtCore/QString>

#include <libtransmission/utils.h> // tr_gettext(), tr_ngettext()

// Qt's strings come from the shared gettext catalog,
// through the translator that tr::app::l10n::use_catalogs() installs.
// xgettext extracts the text in these macros, with the keywords in po/POTFILES.in.

// Translated text: TR_TEXT("Seeding complete")
#define TR_TEXT(msgid) ::trqt::detail::text(msgid)

// Translated text whose English needs a context to tell its meanings apart:
// TR_TEXT_C("Verb", "Seeding")
#define TR_TEXT_C(context, msgid) ::trqt::detail::textInContext(context, msgid)

// Translated text for a widget that reads "&" as a mnemonic marker, such as a menu item, button or buddy label,
// whether or not the English marks a mnemonic: TR_MNEMONIC("_File") returns "&File", and a translation's "&" stays literal.
// The catalog marks a mnemonic with "_" and writes a literal underscore as "__", as GTK does.
#define TR_MNEMONIC(msgid) ::trqt::detail::mnemonicText(msgid)

// Translated text, formatted with {fmt} named arguments:
// TR_FORMAT("Created by {creator}", fmt::arg("creator", creator))
// {fmt} is built without exceptions and aborts on a format string that doesn't fit its arguments,
// so the catalog reader drops any translation whose fields don't fit its English text.
#define TR_FORMAT(msgid, ...) ::trqt::detail::formatTranslation(tr_gettext(msgid), __VA_ARGS__)

// Like TR_FORMAT, in the plural form for `n`.
// Text that doesn't show the count picks its wording with `count == 1` instead,
// because a language's form for one can cover other counts, e.g. 21 in Russian.
#define TR_FORMAT_N(msgid, msgid_plural, n, ...) \
    ::trqt::detail::formatTranslation(tr_ngettext(msgid, msgid_plural, n), __VA_ARGS__)

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
[[nodiscard]] std::pair<QString, QString> splitAtField(char const* translation, char const* source, std::string_view name);

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

[[nodiscard]] QString mnemonicText(char const* msgid);

template<typename T>
concept NamedArg = requires(T const& arg) {
    { arg.name } -> std::convertible_to<char const*>;
    arg.value;
};

// Formats `translation` with {fmt} named arguments.
// Named arguments are taken by value: {fmt} registers the names of non-const ones only.
template<NamedArg... Args>
[[nodiscard]] QString formatTranslation(char const* translation, Args... args)
{
    return QString::fromStdString(fmt::vformat(fmtLocale(), translation, fmt::vargs<Args...>{ { args... } }));
}

} // namespace detail

} // namespace trqt
