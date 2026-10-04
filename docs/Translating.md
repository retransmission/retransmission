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
- **`_` marks a keyboard mnemonic** in the GTK and Qt clients: `_Open` underlines the O.
  Keep one in your translation, on a letter that nothing else in the same menu or dialog uses.
  The Mac client shows the same text without the marker, and without a parenthesized one such as `(_O)`.
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

- The macOS QuickLook extension keeps its text in `macosx/QuickLookExtension/<language>.lproj/Localizable.strings`.
- Maintainers: the header of `po/POTFILES.in` has the commands that update the catalogs from the source code.
