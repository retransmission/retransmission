// This file Copyright © Retransmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

// The app's text comes from the gettext catalog that it shares with the other clients,
// through the tables that po/compile-mac-catalogs.sh writes from each language's catalog.
// xgettext extracts the English text with the keywords in po/POTFILES.in,
// along with a "// Translators:" comment directly above the line where the text starts.
// It ignores the comment argument of NSLocalizedString() and its siblings, so the code passes nil.
//
// Plain text:
//   NSLocalizedString(@"Seeding Complete", nil)
// Formatted text, whose English L10nDeclarations.h declares in the catalog's syntax:
//   [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"Created by %@", @"Formats", nil), creator]
// A plural, looked up by its plural English:
//   [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"%lu files", @"Formats", nil), count]
// Text that doesn't show the count picks its wording with `count == 1` instead,
// because a language's form for one can cover other counts, e.g. 21 in Russian.

// Compiled with TR_CHECK_FORMATS, each Formats lookup is its key, the English,
// so that the compiler's format checks (-Wformat) see each call's arguments against its specifiers.
// Such a build's text is all English; the transmission-mac-format-check CMake target is one.
#ifdef TR_CHECK_FORMATS
#undef NSLocalizedStringFromTable
#define NSLocalizedStringFromTable(key, table, comment) (key)
#endif

// Translated text whose English needs a context to tell its meanings apart:
//   TR_TEXT_C("Verb", "Seeding")
// It takes string literals, which xgettext extracts.
// The Contexts table keys each translation by its context and English joined with U+0004, as a compiled catalog does;
// where the table has no translation, the lookup returns the English.
#define TR_TEXT_C(context, text) \
    NSLocalizedStringWithDefaultValue(@context "\x04" text, @"Contexts", NSBundle.mainBundle, @text, nil)

// Loads the catalogs of the languages that AppKit picked for the app, which translate libtransmission's messages,
// and has {fmt}'s "L" fields format numbers for the user's region.
// Call this before anything shows text.
void TRSetUpLocalization();
