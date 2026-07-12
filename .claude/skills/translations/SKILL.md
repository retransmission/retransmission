---
name: translations
description: >
  How Transmission handles translatable UI strings and internationalization (i18n/l10n). Load this
  whenever you add, change, or remove user-visible text; wrap a string in _(), tr(), ngettext(), or
  NSLocalizedString; touch po/ (.po), qt/translations/ (.ts), or macosx/*.lproj (.strings); edit
  .tx/config, docs/Translating.md, POTFILES.in, or a LINGUAS list; add or drop a language; or answer
  "why isn't my new string translated / how do plurals work / can I concatenate these strings / is the
  web UI translated". Three separate catalog systems (gettext, Qt, Cocoa) all sync through Transifex —
  read this before editing any of them so you touch the right file and don't hand-break a locale.
---

# Translations & translatable strings

Transmission ships **three independent translation catalogs**, one per GUI toolkit, all syncing through
one **Transifex** project (`transmissionbt`). Important subtlety: the **gettext catalog (`po/`) also
covers `libtransmission`, the CLI, and the daemon** — those mark strings with `_()` / `tr_ngettext()`
(from `libtransmission/utils.h`) and share the GTK client's `transmission-gtk` gettext domain. That is
why `po/POTFILES.in` historically listed core and CLI sources alongside `gtk/`. The Qt and macOS
catalogs cover only their own client UI text.

| Client | Mark a string with | English *source* (Transifex source_file) | Translated files (checked in) | Compiled by | Language list lives in |
|---|---|---|---|---|---|
| **GTK** (gettext) | `_("…")`, `ngettext(…)` | `po/en.po` — **NOT in the tree** (Transifex-only) | `po/<lang>.po` (89 files) | `msgfmt` → `.mo` | `po/CMakeLists.txt` `set(LINGUAS …)` (17) |
| **Qt** | `tr("…")` | `qt/translations/transmission_en.ts` (tracked) | `qt/translations/transmission_<lang>.ts` | `lrelease` → `.qm` | `qt/CMakeLists.txt` `set(LINGUAS …)` (33) |
| **macOS** (Cocoa) | `NSLocalizedString(@"key", "comment")` | `macosx/en.lproj/Localizable.strings` (tracked); per-`.xib` text lives in `Base.lproj/*.xib` | `macosx/<lang>.lproj/*.strings` | packaged as-is | `macosx/CMakeLists.txt` `set(LINGUAS …)` |
| **Web** | — none — | — | — | — | not localized (see below) |

The canonical map of every source→translation pairing is `.tx/config` (the Transifex client config).
Full per-toolkit language lists, the resource inventory, and exemplar commits are in
`references/catalog-map.md` — read it when adding a language or auditing coverage.

## The golden rule

**You only ever edit the *English source*: the code at the call site, plus — for macOS only — a
matching entry in the one tracked English catalog `macosx/en.lproj/Localizable.strings` (see the macOS
section). For GTK and Qt you edit *only the code*: the Transifex sync refreshes their English catalogs
for you, so do not hand-edit `po/en.po` (not in the tree anyway) or `qt/translations/transmission_en.ts`
(its history is sync-bot-only). And never hand-edit `po/<lang>.po`, `transmission_<lang>.ts`, or
`<lang>.lproj/*.strings` for any other language.** Translators own those, and a bot round-trips them
through Transifex in bulk "Sync translations" PRs (e.g. `cf0cec32f chore: sync translations (#8059)`). A hand-edited locale file will
be silently overwritten on the next sync, and worse, can desync msgid hashes. If a translation is
wrong, fix it on Transifex, not in git.

An untranslated msgid **falls back to the English source** at runtime (that is how gettext/Qt/Cocoa all
work), so a brand-new string shows up in English in every locale until translators catch up. That is
expected — do not "fix" it by editing locale files.

## Adding or changing a string, per client

Wrap the English text at the call site. Keep msgids as whole sentences with placeholders — see
"Placeholders & plurals" for why concatenation is forbidden. Add a translator comment above anything
ambiguous (units, terse labels, format specifiers); it travels to Transifex and is the only context a
translator gets. **The comment syntax differs per toolkit** — GTK/core `/* Translators: … */`, Qt `//:`
(lupdate ignores `/* Translators */`), macOS the second `NSLocalizedString` argument — so use the one
shown in each client's section below.

**GTK** — macros come from `<glibmm/i18n.h>`; the text domain is bound in `gtk/main.cc` (`bindtextdomain`
/ `textdomain`, domain = `transmission-gtk`). Modern GTK strings use **gettext + libfmt together**:
the msgid carries `fmt`-style named placeholders and is formatted through `fmt::runtime`:

```cpp
d->add_button(_("_Cancel"), …);                                  // gtk/Dialogs.cc:103  (leading _ = GTK mnemonic)
fmt::format(fmt::runtime(ngettext("Started {count:L} time",       // gtk/StatsDialog.cc:73
                                  "Started {count:L} times", n)),
            fmt::arg("count", n));
g_dpgettext2(nullptr, "Logging level", name);                    // context-qualified lookup
```
`{count:L}` is `fmt`'s locale-aware number format. Because the placeholder is *named*, translators may
reorder or omit it safely. `// xgettext:no-c-format` (see `gtk/Torrent.cc`) suppresses printf-format
warnings on strings that contain a literal `%`.

*Core/CLI/daemon strings (same `po/` catalog):* in `libtransmission/`, `cli/`, and `daemon/`, use `_()`
and `tr_ngettext()` from `libtransmission/utils.h` (**not** glibmm — that header is GTK-only), likewise
with `fmt::runtime` + named placeholders:

```cpp
tr_logAddInfo(fmt::format(fmt::runtime(_("Reloading settings from '{path}'")),   // daemon/daemon.cc:755
                          fmt::arg("path", config_dir)));
fmt::runtime(tr_ngettext("Blocklist '{path}' has {count} entry",                 // libtransmission/blocklist.cc:85
                         "Blocklist '{path}' has {count} entries", n_ranges));
```
`utils.h` maps `_`→`gettext` and `tr_ngettext`→`ngettext` only when `ENABLE_GETTEXT` is set
(`Intl_FOUND`, `libtransmission/CMakeLists.txt:240`); otherwise `_(a)` is a no-op but `tr_ngettext`
still does English `count == 1` plural selection (`utils.h:27-28`) — not a passthrough. These
strings translate **at runtime only where a gettext text domain is bound** — i.e. inside the GTK client
(`gtk/main.cc` binds `transmission-gtk`). The standalone daemon/CLI bind no domain, so they show English
even though the calls compile to `gettext()`; the Qt/macOS clients don't use gettext at all and re-wrap
their own UI text. Still mark core strings with `_()` so they get extracted for the GTK catalog.

**Qt** — use `tr()` inside any `QObject` subclass; it captures the class as the disambiguation context.
Substitute with `.arg()` positional placeholders (`%1`, `%2`), never `+`. Plurals use the 3-arg
numerus form with `%Ln`:

```cpp
tr("%1 free").arg(Formatter::storage_to_string(*bytes));          // qt/FreeSpaceLabel.cc:97
tr("Started %Ln time(s)", nullptr, int(total.sessionCount));      // qt/StatsDialog.cc:62  (numerus)
tr("Showing %L1 of %Ln torrent(s)", nullptr, total).arg(visible); // qt/MainWindow.cc:741
```
`%Ln` inserts the count with locale digit grouping and selects the plural form; `(s)`/`(torrent(s))`
is boilerplate Qt Linguist expands per language. `.ui` files (Qt Designer XML) are scanned too — text
in them is translated automatically, no code needed. For a translator note, put a `//:` comment on the
line *directly above* the `tr()` — lupdate reads only `//:`, **not** `/* Translators */`:

```cpp
//: %1 is the tracker host
tr("Couldn't connect to %1")                                     // cf. qt/TrackerDelegate.cc:207, qt/PrefsDialog.cc:447
```

**macOS** — `NSLocalizedString(@"English key", "comment for translator")`. The second argument is the
translator comment, not a value. The `.xib` interface files live in `macosx/Base.lproj/` (base/English
layout); the `.xib` *is* the English source for its own text — there is **no** committed English
`<Xib>.strings` (only the *translated* `<lang>.lproj/<Xib>.strings` are checked in). Programmatic
strings go in `Localizable.strings`:

```objc
k_str = NSLocalizedString(@"KB/s", "Transfer speed (kilobytes per second)");  // macosx/Controller.mm:165
```
The only committed English sources you hand-edit are `macosx/en.lproj/Localizable.strings` and
`macosx/QuickLookExtension/en.lproj/Localizable.strings` (`InfoPlist.strings` isn't a Transifex
resource). Unlike Qt, extraction here is manual — there is no `genstrings` step — so **in the same PR as
the `NSLocalizedString` call you must add a matching entry to `Localizable.strings`**, in the file's
rough alphabetical order:

    /* comment for translator */
    "English key" = "English key";

as in feature commit `7e353588d`. Skip this and the build still passes and the app still shows the
English key (silent fallback), but the string never reaches Transifex and no CI check catches it. Every
other `<lang>.lproj` is translator-owned. See the macos-client skill for editing `.xib`s and the Xcode
project.

## Placeholders & plurals — what NOT to do

- **Never build a sentence by concatenation.** `_("Downloaded ") + n + _(" files")` is untranslatable —
  word order and grammar differ per language. Use one msgid with a placeholder.
- **Never pluralize with an `if`.** `n == 1 ? _("file") : _("files")` is wrong in languages with 3–6
  plural forms (e.g. Russian, Arabic, Polish). Use `ngettext()` (GTK), the 3-arg `tr(…, nullptr, n)`
  numerus form (Qt), or a `.stringsdict` / count-keyed string (macOS). Pass the raw count so the
  catalog picks the form.
- **Don't interpolate a value into the msgid.** `fmt::format("Peers: {}", n)` with a hard-coded English
  prefix, or `tr("Peers: " + QString::number(n))`, hides text from the extractor. Put the placeholder
  *inside* the translatable literal.
- **Don't translate machine-readable strings.** User-facing log/status text in the core *is* localized
  (it's wrapped in `_()`), but RPC method/argument keys, `settings.json` keys, resume-file keys, tracker
  announce params, and BitTorrent protocol constants must stay stable English/ASCII — never wrap those
  in `_()`/`tr()`. If in doubt: would a machine parse it? Then leave it untranslated.

## The web client is NOT translated

`web/` (the built-in remote WebUI) has **no** translation catalog — no `.po`/`.mo`/`.ts`, no i18n
library, and no entry in `.tx/config`. Strings are hard-coded English in the `.js` sources. What looks
like i18n in `web/src/formatter.js` is not: `ngettext(msgid, msgid_plural, n)` is a local shim that
returns the English singular/plural via `Intl.PluralRules`, and `Intl.NumberFormat` only formats
numbers for the browser locale. If asked to translate the web UI, say plainly that the pipeline does
not exist yet — it would be net-new work, not a tweak. See the web-client skill for that codebase.

## Build & sync mechanics

- NLS is gated by the CMake option `ENABLE_NLS` (default **ON**, `CMakeLists.txt:79`). With it off, no
  catalogs compile. See the building skill for configure/build.
- **Only compilation is wired into the build**: `msgfmt` (`.po`→`.mo`, `po/CMakeLists.txt`) and
  `lrelease` (`.ts`→`.qm`, via `tr_qt_add_translation` in `cmake/TrMacros.cmake`). macOS `.strings` are
  copied into the bundle as-is.
- **Extraction is NOT in the build.** There is no `xgettext`, `lupdate`, or `genstrings` CMake target.
  For **GTK/core and Qt**, refreshing the English source catalogs (`po/en.po`, `transmission_en.ts`) and
  merging new strings into every locale is done **on the Transifex side** and lands via the "Sync
  translations" PRs — your job ends at writing a good English source string with a translator comment;
  the sync discovers it, so do not regenerate those by hand. **macOS is the exception:** with no
  `genstrings` step, you *do* hand-add the new string to `macosx/en.lproj/Localizable.strings` in your
  own PR (see the macOS section). Never hand-edit any *other-language* locale file, though.
- Only a **subset** of the ~89 available locales is actually shipped — each toolkit's
  `set(LINGUAS …)` is a hand-curated "good enough quality" list (GTK ships 17, Qt 33). Low-quality
  locales were deliberately dropped (`6a84dbf56 Remove low-quality gettext locales from the list (#6475)`).
  **To add a language to a build, add it to that toolkit's `set(LINGUAS …)` in its `CMakeLists.txt`** —
  and confirm a translation file for it exists. See `references/catalog-map.md` for the exact lists.

## Stale files — do not trust these

- **`po/POTFILES.in` and `po/POTFILES.skip` are stale.** They list `.c` paths (`gtk/actions.c`,
  `libtransmission/session.c`) and googletest files that no longer exist — the tree is C++ (`.cc`,
  CamelCase in `gtk/`). They are not driving any live extraction. Don't cite them as truth or "fix" a
  string problem by editing them; treat them as vestigial until someone rewrites the extraction step.
- **`po/LINGUAS` (the file, ~87 langs) is not read by CMake.** It is autotools-era leftover. The
  authoritative build list is `set(LINGUAS …)` in `po/CMakeLists.txt` (17 langs). Editing the file does
  nothing.
- `docs/Translating.md` is accurate on Transifex and on Launchpad being dead, but predates the
  gettext+fmt work — treat it as the translator-facing intro, this skill as the developer reality.

## Maintainer direction (for new-work decisions)

Today's **three-catalog reality is a fact you must work within** — a new Qt string still needs `tr()`,
a new GTK string still needs `_()`. But the long-term direction is to **unify all clients on
`gettext` + `libfmt`** (the pattern GTK already uses: `fmt::runtime(ngettext(...))` with named
`{placeholder:L}` args). `third-party/tinygettext/` is checked out under evaluation as a portable
gettext runtime toward that goal — but it is **currently untracked and not wired into the build**
(no CMake references it yet). Mention it only as direction. When you design new translatable surfaces,
prefer named `fmt` placeholders and gettext-shaped calls so they migrate cleanly; don't invent a fourth
mechanism.

## Related skills

macos-client (`.xib`/Xcode) · qt-client · gtk-client · web-client · building (ENABLE_NLS, configure) ·
add-session-setting (user-facing setting labels are translatable strings). For the contributor/PR
workflow, see `CONTRIBUTING.md` at the repo root.
