// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>

#include <libtransmission/macros.h>

#include <libtransmission-app/l10n.h>

// The app's strings come from the gettext catalog that it shares with the other clients,
// through the translator that TRSetUpLocalization() installs.
// xgettext extracts the text in these macros, with the keywords in po/POTFILES.in.

// Translated text: TR_TEXT("Seeding complete")
#define TR_TEXT(msgid) TRText(msgid)

// Translated text whose English needs a context to tell its meanings apart:
// TR_TEXT_C("Verb", "Seeding")
#define TR_TEXT_C(context, msgid) TRTextInContext(context, msgid)

// Translated text, formatted with {fmt} named arguments:
// TR_FORMAT("Created by {creator}", TRArg("creator", creator))
// {fmt} is built without exceptions and aborts on a format string that doesn't fit its arguments,
// so the catalog reader drops any translation whose fields don't fit its English text.
#define TR_FORMAT(msgid, ...) TRFormat(msgid, { __VA_ARGS__ })

// Like TR_FORMAT, in the plural form for `n`.
// Text that doesn't show the count picks its wording with `count == 1` instead,
// because a language's form for one can cover other counts, e.g. 21 in Russian.
#define TR_FORMAT_N(msgid, msgid_plural, n, ...) TRFormatPlural(msgid, msgid_plural, static_cast<uint64_t>(n), { __VA_ARGS__ })

// A named argument of TR_FORMAT or TR_FORMAT_N: text or a number.
// A field can localize a number with {fmt}'s "L" spec, as in "{count:L}".
// nil formats as no text.
[[nodiscard]] inline tr::app::l10n::Arg TRArg(char const* const name, NSString* const value)
{
    return { name, std::string{ value.UTF8String ?: "" } };
}

template <std::integral T> [[nodiscard]] tr::app::l10n::Arg TRArg(char const* const name, T const value)
{
    if constexpr (std::is_signed_v<T>) {
        return { name, static_cast<int64_t>(value) };
    } else {
        return { name, static_cast<uint64_t>(value) };
    }
}

template <std::floating_point T> [[nodiscard]] tr::app::l10n::Arg TRArg(char const* const name, T const value)
{
    return { name, static_cast<double>(value) };
}

// The app's name, for text with an "{appname}" field.
[[nodiscard]] inline tr::app::l10n::Arg TRAppNameArg()
{
    return { "appname", std::string{ TR_PROJ_APPNAME_CAPITALIZED } };
}

// Loads the catalogs of the languages that AppKit picked for the app,
// and has {fmt}'s "L" fields format numbers for the user's region.
// Call this before anything shows text.
void TRSetUpLocalization();

// What the macros above call. Code calls the macros, which xgettext extracts.

[[nodiscard]] NSString* TRText(char const* msgid);

[[nodiscard]] NSString* TRTextInContext(char const* context, char const* msgid);

[[nodiscard]] NSString* TRFormat(char const* msgid, std::initializer_list<tr::app::l10n::Arg> args);

[[nodiscard]] NSString* TRFormatPlural(char const* msgid, char const* msgid_plural, uint64_t n, std::initializer_list<tr::app::l10n::Arg> args);
