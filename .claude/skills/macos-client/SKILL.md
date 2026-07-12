---
name: macos-client
description: >
  How to work in the native macOS client under macosx/ and the Transmission.xcodeproj Xcode
  project — including from a Linux machine where you cannot run Xcode. Load this whenever you
  touch anything in macosx/ (Controller.mm, the *WindowController / *ViewController classes,
  Torrent.mm, the NS*Additions categories, *.xib, *.lproj/*.strings, QuickLook*, VDKQueue,
  Sparkle), or edit Transmission.xcodeproj/project.pbxproj, or ADD/REMOVE/RENAME any source file
  (even a pure libtransmission/*.cc) and need the Xcode project kept in sync, or when the macOS
  CI job (macos-xcodebuild-universal / macos-cmake-universal) fails after a build-wiring change,
  or when someone says "add a file to the mac client", "the Xcode build broke", "edit the pbxproj
  from Linux", or asks how ENABLE_MAC / the mac app is built. There are TWO independent build
  systems here that both run in CI — read this before assuming a CMake-only edit is enough.
---

# macOS client (`macosx/` + `Transmission.xcodeproj`)

The macOS client is a **native Cocoa app written in Objective-C++** (`.mm` files — AppKit and C++
in one translation unit). It embeds libtransmission **in-process and calls the C API directly** —
it is *not* an RPC client. `macosx/main.mm` calls `tr_lib_init()` then `NSApplicationMain(...)`;
`Controller.mm` and `Torrent.mm` `#include <libtransmission/transmission.h>` and hold a live
`tr_session*` / `tr_torrent*`. In that respect it resembles the **gtk-client** (in-process C API),
not the **qt-client** (thin RPC layer).

Ground truth is `macosx/`, `macosx/CMakeLists.txt`, and `Transmission.xcodeproj/project.pbxproj`.
If this skill disagrees with the repo, trust the repo and fix this skill. Sibling skills — read
them instead of re-deriving: **building** (CMake/Ninja/tidy mechanics), **cpp-conventions** (C++
style, license headers), **codebase-map** (repo layout, libtransmission subsystems),
**add-session-setting** (a new persisted pref across all clients), **third-party-deps** (bundled
libraries), **translations** (the three localization pipelines).

## The one thing that will bite you: two independent build systems

The mac client is built **two different ways, and both run in CI on every mac-affecting change**:

1. **CMake** — `macosx/CMakeLists.txt`, target **`transmission-mac`** (from `add_executable(${TR_NAME}-mac …)`;
   `TR_NAME` is `transmission`), output bundle `Transmission.app`.
2. **Xcode** — `Transmission.xcodeproj/project.pbxproj`, scheme **`Transmission`**.

These two build systems **share no wiring**. The Xcode project is a *complete, hand-maintained
duplicate* of the build graph: it has its own from-source **static-library target for
libtransmission and for every bundled third-party C lib** — verified targets: `libtransmission`,
`simdutf`, `event` (libevent), `natpmp`, `miniupnp`, `dht`, `utp`, `archive` (libarchive), `psl`
(libpsl), `wildmat`, `madler-crcany` — plus the app, the CLI/daemon/remote/create/edit/show
tools, and the QuickLook plugin/extension (20 native targets total). It does **not** invoke CMake.

**Consequence — the trap:** any time you **add, remove, or rename a source file**, you must edit
**both** `macosx/CMakeLists.txt` (or the relevant `libtransmission/CMakeLists.txt`, etc.) **and**
`project.pbxproj`, in the **same commit**. A CMake-only change compiles fine on Linux and silently
breaks the macOS Xcode build. This is **not limited to `macosx/` files** — the most common way to
hit it is a *pure libtransmission change*: the xcodeproj lists libtransmission's own sources
(`session.cc`, `torrent.cc`, `announcer.cc`, …), so adding/removing a `libtransmission/*.cc`
breaks the mac build even though you never opened `macosx/`. Real examples that touched
`libtransmission/` **and** `Transmission.xcodeproj` but **not** `macosx/`:
`5c71c39b3 refactor: move x509 openssl code into web.cc (#78)` and
`cbaefab59 refactor: add libtransmission/types.h (#8449)`.

## You cannot build or run the mac client on Linux — CI is your only feedback loop

Both build systems require the Apple toolchain: `macosx/CMakeLists.txt` does
`find_program(ACTOOL_EXECUTABLE actool REQUIRED)` and `find_program(CODESIGN_EXECUTABLE codesign
REQUIRED)` and links `-framework AppKit`; the Xcode build needs `xcodebuild`. None of that exists
on Linux. So when you edit `macosx/` or the pbxproj from Linux, **you cannot compile it locally** —
you must get the edit right by inspection and let CI verify it.

The gate is **`.github/workflows/actions.yml`**. Three macOS jobs, all triggered by the `make-mac`
flag (computed from changes to `CMakeLists.txt cmake Transmission.xcodeproj third-party
libtransmission macosx`):

- **`macos-xcodebuild-universal`** — the Xcode gate. Runs
  `xcodebuild -project src/Transmission.xcodeproj -scheme Transmission -configuration 'Release - Debug' -derivedDataPath pfx ONLY_ACTIVE_ARCH=NO build`.
  This is what catches an out-of-sync `project.pbxproj`.
- **`macos-cmake-universal`** and **`macos`** — build the CMake path with `-DENABLE_MAC=ON` on
  `macos-15-intel` and `macos-26` runners.

Push to a branch and watch these jobs (`gh run watch` / the PR checks). A green
`macos-xcodebuild-universal` is the real confirmation your pbxproj edit is valid.

## `ENABLE_MAC` and the CMake build (what it actually does)

`CMakeLists.txt` declares `tr_auto_option(ENABLE_MAC "Build Mac client" AUTO)`. The resolver block
(around line 515) sets `MAC_FOUND ON` **iff `APPLE`**, else `OFF` (a hard error only if the option
was explicitly required). So on Linux, `ENABLE_MAC=AUTO` resolves **OFF** and `macosx/` is skipped
entirely — `-DENABLE_MAC=ON` on Linux just makes configure `SEND_ERROR` ("Mac build is impossible
on non-Mac system."). The subdirectory is added by the generic
`foreach(P cli daemon gtk mac qt utils)` loop via `set(MAC_PROJECT_DIR macosx)`.

Deployment target: `MACOS_SUPPORT_MINIMUM 11.0` in the root `CMakeLists.txt` (kept at "latest
stable Xcode's RECOMMENDED_MACOSX_DEPLOYMENT_TARGET"); `macosx/CMakeLists.txt` FATAL_ERRORs if
`CMAKE_OSX_DEPLOYMENT_TARGET` is older. ARC and `-fobjc-weak` are forced on for the directory. The
`.xcodeproj` has its own three build configurations — `Debug`, `Release`, and `Release - Debug`
(the one CI builds).

## Editing `project.pbxproj` from Linux — the playbook

`project.pbxproj` is a single ~6100-line OpenStep/NeXTSTEP-style property list. **Edit it by hand
with targeted string-splices — never round-trip it through a Python pbxproj library.** A no-op
load+save through `pbxproj` (mod-pbxproj) reshuffles **~314 lines** (measured on this repo; the
maintainer recalls ~333) because the library re-serializes and re-sorts every object. That churn
buries your real 6-line change and makes the diff unreviewable. Copy the shape of an existing
sibling entry instead.

**Adding a compiled source file** (`Foo.mm`) means inserting one line in **each of four sections**,
tied together by two fresh 24-hex-character object IDs. A header (`Foo.h`) needs only two of them
(app headers are not in a Headers build phase). Then mirror it in `macosx/CMakeLists.txt`. The
**canonical exemplar to imitate is `d1985b05c macos: View-based FileOutlineView (#7760)`** — a
clean, minimal, hand-written diff that adds `.h/.mm` pairs to both files. `git show d1985b05c --
Transmission.xcodeproj/project.pbxproj macosx/CMakeLists.txt` and copy its structure.

The exact per-section line templates, the object-ID rules, the annotated `d1985b05c` walkthrough,
the libtransmission-file variant (its `.cc` also joins the xcodeproj's `libtransmission` target),
how to remove/rename, and the validate-before-CI commands are in **`references/pbxproj-edit.md`**. Read it
before your first pbxproj edit.

Quick sanity check before pushing (does not need a Mac; `pbxproj` v4.3.3 is installed here):

```bash
python3 -c "from pbxproj import XcodeProject; XcodeProject.load('Transmission.xcodeproj/project.pbxproj'); print('parse OK')"
```

A clean parse only proves the file is still structurally valid plist — it does **not** prove your
IDs are wired correctly. Only `macos-xcodebuild-universal` in CI proves that.

Also run `./code_style.sh --check` after any pbxproj edit: besides formatting, it greps
`project.pbxproj` for three required lines (`objectVersion = 54;`, `compatibilityVersion = "Xcode
12.0";`, `BuildIndependentTargetsInParallel = YES;`). A splice that drops one fails the CI
`code-style` gate before `xcodebuild` ever runs (see the **building** skill and
`references/pbxproj-edit.md`).

## `macosx/` tour

Everything is flat in `macosx/`. The hub:

- **`Controller.{h,mm}`** — the `NSApplicationDelegate` and central coordinator (the largest file,
  ~198 KB). It owns the `tr_session*` (`@property … tr_session* sessionHandle`), the torrent list,
  the main window/toolbar, drag-and-drop, Sparkle updates, and dispatches almost everything. Start
  here to trace app behavior.
- **`Torrent.{h,mm}`** — the Objective-C wrapper around a `tr_torrent*`; the model object the UI
  binds to. **`FileListNode`**, **`TrackerNode`** — sub-models.
- **Window/sheet controllers** — `AddWindowController`, `AddMagnetWindowController`,
  `CreatorWindowController`, `InfoWindowController` (+ the `Info*ViewController` tabs),
  `MessageWindowController`, `PrefsController`, `StatsWindowController`, `AboutWindowController`,
  `URLSheetWindowController`, `GroupsPrefsController`, `FileRenameSheetController`.
- **Views / cells / table views** — `TorrentTableView`, `TorrentCell`, `FileOutlineView`,
  `PiecesView`, `ProgressBarView`, `FilterBarController`, `BadgeView`, and the `*CellView` /
  `*Cell` families.
- **`NS*Additions`** (e.g. `NSStringAdditions`, `NSImageAdditions`) — category extensions on
  Foundation/AppKit classes. **`main.mm`** — the tiny entry point.
- **Subdirectories with extra build wiring — three different cases, don't lump them:**
  `QuickLookPlugin` / `QuickLookExtension` (Finder Quick Look previews) are the only ones with their
  own target in **both** build systems — CMake `transmission-mac-ql` / `transmission-mac-qlappex`
  **and** the xcodeproj native targets `QuickLookPlugin` / `QuickLookExtension`. `VDKQueue` (a bundled
  filesystem-watch helper) is a static lib `vdkqueue` **only on the CMake side**; in the xcodeproj it
  has **no target** — `VDKQueue.mm` is compiled straight into the `Transmission` app target's Sources
  phase (which is why the 20-target list above omits it). The vendored **`Sparkle.framework`**
  (auto-update) is a **prebuilt framework with no target in either build system** — weak-linked and
  copied into the app bundle.

UI layout lives in **`.xib`** files. The base-language layouts are in **`Base.lproj/`**
(`MainMenu.xib`, `PrefsWindow.xib`, `AddWindow.xib`, …); a handful of standalone `.xib`s sit at the
`macosx/` top level. Objective-C++ convention: `.mm` for anything mixing Cocoa with C++/libtransmission.

## Localization (the third pipeline)

The mac client uses **Cocoa `.strings`**, not gettext `.po` (GTK) or Qt `.ts`. Base UI text lives
in the `Base.lproj/*.xib`; `en.lproj/` holds `InfoPlist.strings` + `Localizable.strings`; each
`<lang>.lproj/` (≈20 locales) holds a `.strings` per base XIB plus those two files. The enabled set
is the `LINGUAS` list in `macosx/CMakeLists.txt`, gated by `ENABLE_NLS`. Adding a translatable
string touches the `Localizable.strings` / XIB flow; the deep mechanics (and how all three client
pipelines relate, plus that extraction happens on Transifex — don't hand-edit translated catalogs)
belong to the **translations** skill. If you add or rename a `.xib` or `.strings`, it
must be registered in **both** `macosx/CMakeLists.txt` (the `XIB_FILES` / `BASE_XIB_FILES` /
`LANG_STRINGS_FILES` lists) **and** `project.pbxproj` — same two-build-system rule as source files.
