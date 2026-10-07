# Translating Retransmission

The GTK, Qt, and macOS clients share one set of translations:
a [gettext](https://www.gnu.org/software/gettext/) catalog per language, `po/<language>.po`.
Text that several clients show is translated once.

A release includes a language once more than 90% of its text is translated.
A catalog shows untranslated text in English,
so a language much below that mixes the two on every screen.

## Contributing

1. Edit `po/<language>.po` in a catalog editor such as [Poedit](https://poedit.net/), or in a text editor.
2. Check your work: `python3 release/find-broken-translations.py po/<language>.po`
3. Open a pull request.

To start a language that has no catalog yet, open an issue and a maintainer will add one.

## What the catalog's markup means

- **`{name}` fields** stand for values that the client fills in, such as `{count:L}` or `{torrent_name}`.
  Copy each field exactly, including anything after a colon.
  You may reorder fields, and a singular form may leave the count out.
  Leave out any other field only if your translation uses no field that comes after it in the English:
  the macOS client can't skip one.
- **`_` marks a keyboard mnemonic** in the GTK and Qt clients: `_Open` underlines the O.
  Keep one in your translation, on a letter that nothing else in the same menu or dialog uses.
- **Plural entries** have one `msgstr[n]` per plural form of your language.
  The catalog's `Plural-Forms` header says how many there are and which counts each one covers.
- **`#. Translators:` comments** say where a piece of text appears or what it means.
- **`msgctxt`** tells apart entries whose English text is the same but whose meaning differs,
  such as "Error" as a logging level and as a torrent's status.
- **`#, fuzzy` entries** hold a translation of English text that has since been reworded;
  the `#|` lines show the earlier English.
  The clients show such an entry in English until you correct the translation and remove the `fuzzy` flag.

## Seeing your translation in a client

[Build](Building-Transmission.md) the client, then start it in your language.

- **GTK and Qt** read catalogs from where the client is installed, so install it first:
  `LANGUAGE=<language> retransmission-gtk` or `LANGUAGE=<language> retransmission-qt`.
  They build only the languages listed in `po/CMakeLists.txt`,
  so add yours to that list in your checkout if it isn't there.
- **macOS** builds every language that has a `macosx/<language>.lproj` folder.
  Start the app with `-AppleLanguages '(<language>)'`.

## Other text

The macOS QuickLook extension keeps its text in `macosx/QuickLookExtension/<language>.lproj/Localizable.strings`.

## Marking text for translation in the macOS client

xgettext extracts the English from the code into the catalogs' template,
along with a `// Translators:` comment on the line above the one where the text starts.
The build writes each language's tables from its catalog with `po/compile-mac-catalogs.sh`.

- **Plain text:** `NSLocalizedString(@"Seeding Complete", nil)`, with a string literal.
  xgettext ignores the comment argument, so pass `nil`.
- **Text with a context**, for English that has more than one meaning: `TR_TEXT_C("Verb", "Seeding")`.
- **Formatted text:** declare the English once, in the catalog's `{fmt}` syntax, in `macosx/L10nDeclarations.h`,
  ```c
  // Translators: Inspector -> Activity tab -> have
  TR_DECLARE("{size} verified");
  ```
  and look it up by that English in Cocoa's format syntax:
  ```objc
  [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"%@ verified", @"Formats", nil), size]
  ```
  The fields become specifiers in their order:
  `{name}` is `%@` for an `NSString`, `{name:L}` is `%lu` for an `NSUInteger` that the text shows as a count,
  and `{name:d}` is `%ld` for an `NSInteger`. A literal `%` is `%%`.
- **Plural text:** declare both English forms, and look the text up by its plural:
  ```c
  TR_DECLARE_N("{count:L} file", "{count:L} files");
  ```
  ```objc
  [NSString localizedStringWithFormat:NSLocalizedStringFromTable(@"%lu files", @"Formats", nil), count]
  ```
  The plural counts its `{count}` field, or else its last `{name:L}` field.
  Text that doesn't show the count picks its wording with `count == 1` instead,
  because a language's form for one can cover other counts, such as 21 in Russian.

Two declarations whose English reads the same in Cocoa's syntax would share a key,
which the build allows only while every language translates them alike.
`TR_DECLARE("…", NUMBERED_KEY)` gives a declaration a key with numbered specifiers, such as `%1$@`, instead.

CI checks every lookup against the template and the declarations.
It also builds the app with each formatted lookup replaced by its English,
so that the compiler checks the arguments; on a Mac: `cmake --build build -t transmission-mac-format-check`.

## Updating the template

The header of `po/POTFILES.in` has the xgettext options that make the template from the code,
and the msgmerge options that update each catalog from the template.
Weblate's Update POT file (xgettext) add-on takes the same settings:
`po/POTFILES.in` as its POTFILES manifest, `po` as an ITS data directory,
comments tagged `Translators:`, and these additional keywords:
```
_
N_
C_:1c,2
NC_:1c,2
ngettext:1,2
tr_ngettext:1,2
tr_pgettext:1c,2
TR_TEXT
TR_TEXT_C:1c,2
TR_MNEMONIC
TR_FORMAT
TR_FORMAT_N:1,2
NSLocalizedString:1
TR_DECLARE
TR_DECLARE_N:1,2
updateSpinBoxFormat:2,3
```
The macOS client's are `NSLocalizedString:1`, `TR_TEXT_C:1c,2`, `TR_DECLARE` and `TR_DECLARE_N:1,2`.
A keyword added to `po/POTFILES.in` goes into the add-on's settings too.
