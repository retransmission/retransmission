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

// Translated text whose English needs a context to tell its meanings apart:
// TR_TEXT_C("Verb", "Seeding")
#define TR_TEXT_C(context, msgid) TRTextInContext(context, msgid)

// Loads the catalogs of the languages that AppKit picked for the app, which translate libtransmission's messages,
// and has {fmt}'s "L" fields format numbers for the user's region.
// Call this before anything shows text.
void TRSetUpLocalization();

// What TR_TEXT_C calls. Code calls the macro, which xgettext extracts.
[[nodiscard]] NSString* TRTextInContext(char const* context, char const* msgid);
