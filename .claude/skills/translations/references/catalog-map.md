# Translation catalog map — full reference

Read this when adding/removing a language, auditing locale coverage, or wiring a new
translatable resource. Everything here is verified against the repo; re-check with the commands at the
bottom if the tree has moved on.

## `.tx/config` — the authoritative source→translation map

Transifex org/project: `transmissionbt` / `transmissionbt` (host `https://www.transifex.com`).
Each `[o:transmissionbt:p:transmissionbt:r:<res>]` block maps one resource:

| Resource | `source_file` (English) | `file_filter` (translations) | type |
|---|---|---|---|
| `gtk` | `po/en.po` *(not in tree)* | `po/<lang>.po` | PO |
| `qt` | `qt/translations/transmission_en.ts` | `qt/translations/transmission_<lang>.ts` | QT |
| `mac` | `macosx/en.lproj/Localizable.strings` | `macosx/<lang>.lproj/Localizable.strings` | STRINGS |
| `mac-AddMagnetWindow` | `macosx/en.lproj/AddMagnetWindow.strings` *(not in tree)* | `macosx/<lang>.lproj/AddMagnetWindow.strings` | STRINGS |
| `mac-AddWindow` | `macosx/en.lproj/AddWindow.strings` *(not in tree)* | … | STRINGS |
| `mac-Creator` | `macosx/en.lproj/Creator.strings` *(not in tree)* | … | STRINGS |
| `mac-GlobalOptionsPopover` | `macosx/en.lproj/GlobalOptionsPopover.strings` *(not in tree)* | … | STRINGS |
| `mac-GroupRules` | `macosx/en.lproj/GroupRules.strings` *(not in tree)* | … | STRINGS |
| `mac-InfoActivityView` | `macosx/en.lproj/InfoActivityView.strings` *(not in tree)* | … | STRINGS |
| `mac-InfoGeneralView` | `macosx/en.lproj/InfoGeneralView.strings` *(not in tree)* | … | STRINGS |
| `mac-InfoOptionsView` | `macosx/en.lproj/InfoOptionsView.strings` *(not in tree)* | … | STRINGS |
| `mac-MainMenu` | `macosx/en.lproj/MainMenu.strings` *(not in tree)* | … | STRINGS |
| `mac-PrefsWindow` | `macosx/en.lproj/PrefsWindow.strings` *(not in tree)* | … | STRINGS |
| `mac-ql` | `macosx/QuickLookExtension/en.lproj/Localizable.strings` | `macosx/QuickLookExtension/<lang>.lproj/Localizable.strings` | STRINGS |

The 10 `mac-*` per-window resources correspond 1:1 to the `.xib` files in `macosx/Base.lproj/`
(`AddMagnetWindow`, `AddWindow`, `Creator`, `GlobalOptionsPopover`, `GroupRules`, `InfoActivityView`,
`InfoGeneralView`, `InfoOptionsView`, `MainMenu`, `PrefsWindow`). For these, the `source_file` path in
`.tx/config` (`en.lproj/<Xib>.strings`) is **not in the tree** — the `Base.lproj/<Xib>.xib` itself is
the English source, and `macosx/CMakeLists.txt` skips the `en` per-xib `.strings` (`if(NOT LANG STREQUAL
"en")`, ~L371) so Transifex extracts from the `.xib`. Add a new `.xib` → add a matching
`[…:r:mac-<Name>]` block to `.tx/config` and list the `.xib` in `BASE_XIB_FILES` in
`macosx/CMakeLists.txt` — do **not** hand-create an `en.lproj/<Name>.strings` file.

**`lang_map`** (mac resources only) renames locales between Transifex and disk, in `<remote>: <local>`
form: `pt_BR: pt-BR`, `pt_PT: pt-PT`, `zh_CN: zh-CN`, `zh_TW: zh-TW`. So Transifex `pt_BR` writes to
`macosx/pt-BR.lproj/…` (underscore remote → hyphen on disk). GTK/Qt use the underscore form directly (no map).

**Key asymmetry:** the GTK source `po/en.po` is *not committed and not gitignored* — `tx` fetches it on
demand; the tree holds only translated `po/<lang>.po`. Qt's `transmission_en.ts` **is** committed, but
you still don't hand-edit it — its history is sync-bot-only, so a new `tr()` string lands in it via the
next "Sync translations" PR, just like `po/en.po`. **macOS is the only one edited by hand:**
`macosx/en.lproj/Localizable.strings` (and `macosx/QuickLookExtension/en.lproj/Localizable.strings`)
are committed English sources you update directly in your PR. The 10 per-xib `en.lproj/<Xib>.strings`
named as `source_file` are **not** in the tree (the `Base.lproj/<Xib>.xib` is their source), so there's
nothing to hand-edit there either.

## Shipped-language lists (the `set(LINGUAS …)` that actually build)

These are hand-curated quality subsets, **not** the full set of translation files on disk. Edit the
relevant list to add/drop a shipped language.

**GTK** — `po/CMakeLists.txt` (17): `da es eu fi fr he hu is ja nl pl ru sv tr uk zh_CN zh_TW`
(`en` is absent — English is the fallback, nothing to compile).

**Qt** — `qt/CMakeLists.txt` (33): `af ca da de el en es eu fi fr he hu id is it ja ka kk ko lt nb nl
pl pt pt_BR pt_PT ru sl sv tr uk zh_CN zh_TW` (includes `en`; `transmission_en.qm` is also checked in
at `qt/`).

**macOS** — `macosx/CMakeLists.txt` (20): `da de en es eu fr he hu it ja nl pl pt-BR pt-PT ru sv tr uk
zh-CN zh-TW` — note the hyphen/`lang_map` names (on disk: `macosx/pt-BR.lproj`, `macosx/zh-CN.lproj`, …)
and that `en` is included. When `ENABLE_NLS` is off, macOS still builds `en` (`set(ENABLED_LINGUAS en)`),
unlike GTK/Qt which build nothing. Some `.lproj` dirs exist on disk but aren't shipped (e.g. `af.lproj`
is not in this list).

By contrast the **on-disk translation files** are far more numerous: `ls po/*.po | wc -l` = 89,
`macosx/*.lproj` = 23 dirs. The gap is intentional (see `6a84dbf56`).

## Vestigial / stale files (verified out of date)

- `po/POTFILES.in` — the *intent* still holds (it lists `libtransmission/*`, `cli/cli`, and `gtk/*`
  because all three feed the one gettext `po/` catalog), but the **paths are stale**: it names `.c`
  files (`cli/cli.c`, `gtk/actions.c`, `libtransmission/session.c`) that no longer exist — the tree is
  `.cc` (and `gtk/` is CamelCase: `gtk/Actions.cc`). Last touched 2022 (`0a27346ef`); last
  *i18n-motivated* update was a decade earlier, 2012 (`a805f866f`). Not driving live extraction; don't
  cite the filenames as truth.
- `po/POTFILES.skip` — lists `libtransmission/crypto-test.c` and `third-party/googletest/**`; also stale.
- `po/LINGUAS` (the file) — ~87 langs, autotools-era; **not read by any CMake**. The build list is
  `set(LINGUAS)` in `po/CMakeLists.txt`.
- `po/ChangeLog` — historical; not maintained per-commit.

## How the pieces compile (verified)

- GTK: `po/CMakeLists.txt` runs `msgfmt --output-file=transmission-gtk-<lang>.mo <lang>.po`, installs to
  `${CMAKE_INSTALL_LOCALEDIR}/<lang>/LC_MESSAGES/transmission-gtk.mo`. `GETTEXT_PACKAGE = ${TR_NAME}-gtk`
  = `transmission-gtk`; runtime domain `MY_NAME = TR_PROJ_APPNAME "-gtk"` (`gtk/Macros.h:10`) bound in
  `gtk/main.cc:59-61`.
- Qt: `tr_qt_add_translation` (`cmake/TrMacros.cmake:537`) → `qt6_add_translation` / `qt5_add_translation`
  with `-silent` (runs `lrelease`, `.ts`→`.qm`). No `lupdate` target anywhere.
- macOS: `macosx/CMakeLists.txt` (~L350-393) packages each `<lang>.lproj/*.strings` into the bundle via
  `MACOSX_PACKAGE_LOCATION`; `Base.lproj/*.xib` provides the base UI. No `genstrings`/string-extraction
  target — but `ibtool` *is* used, only to compile `Base.lproj/*.xib` → `.nib` (`tr_target_xib_files`,
  `cmake/TrMacros.cmake:592,630`; called from `macosx/CMakeLists.txt:399`), not to extract strings.

## i18n macro cheat-sheet with real call sites

- GTK simple: `_("_Cancel")` — `gtk/Dialogs.cc:103` (leading `_` = mnemonic accelerator, not part of text).
- GTK plural + fmt: `fmt::runtime(ngettext("Started {count:L} time", "Started {count:L} times", n))` —
  `gtk/StatsDialog.cc:73`; also `gtk/MakeDialog.cc:368`.
- GTK context: `g_dpgettext2(nullptr, "Logging level", name)` — `gtk/MessageLogWindow.cc:182`.
- GTK no-c-format hint: `// xgettext:no-c-format` — `gtk/Torrent.cc:414`.
- GTK translator-credits idiom: `/* Translators: translate "translator-credits" as your name */` —
  `gtk/Application.cc:1316`.
- Qt arg: `tr("%1 free").arg(...)` — `qt/FreeSpaceLabel.cc:97`.
- Qt numerus: `tr("Started %Ln time(s)", nullptr, n)` — `qt/StatsDialog.cc:62`; `qt/MakeDialog.cc:291`.
- macOS: `NSLocalizedString(@"B/s", "Transfer speed (bytes per second)")` — `macosx/Controller.mm:164`.
- Core (libtransmission/cli/daemon), macros from `libtransmission/utils.h`: `_("Forwarded")` —
  `libtransmission/port-forwarding.cc:156`; plural `tr_ngettext("… {count} entry", "… {count} entries", n)`
  — `libtransmission/blocklist.cc:85`; `_(...)` in `cli/cli.cc:95`, `daemon/daemon.cc:213`.
  `utils.h`: `#ifdef ENABLE_GETTEXT` → `_`=`gettext`, `tr_ngettext`=`ngettext`; else `_(a)` is a no-op
  but `tr_ngettext` still does English `count == 1` plural selection (`utils.h:27-28`), not a passthrough.

## Exemplar commits

- `cf0cec32f chore: sync translations (#8059)` — a routine Transifex `tx pull` PR (what NOT to hand-write).
- `6a84dbf56 Remove low-quality gettext locales from the list (#6475)` — how a shipped-locale list is pruned.
- `9b496350a feat: Data-based QuickLook extension for macOS 12+ (#7213)` — re-pointed the *existing*
  `mac-ql` resource from `QuickLookPlugin/` to `QuickLookExtension/` paths and added the extension's
  `lproj` files (it did **not** create the `mac-ql` block).
- `7e353588d` — a feature PR that hand-adds one `NSLocalizedString` and its matching
  `macosx/en.lproj/Localizable.strings` entry in the same commit (the macOS "add a string" pattern).

## Re-verify quickly

```sh
cat .tx/config                               # source→translation map
sed -n '/set(LINGUAS/,/)/p' po/CMakeLists.txt qt/CMakeLists.txt macosx/CMakeLists.txt
ls po/*.po | wc -l ; ls -d macosx/*.lproj    # on-disk file counts
git ls-files po/en.po                        # empty → GTK English source not in tree
grep -rn 'NSLocalizedString\|glibmm/i18n\|tr("' macosx qt gtk | head
git log --oneline -- po/ qt/translations/ 'macosx/*.lproj' .tx/   # sync history
```
