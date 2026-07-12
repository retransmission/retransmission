---
name: add-session-setting
description: >-
  Step-by-step checklist for adding or changing a libtransmission SESSION
  preference (a settings.json / RPC session_get+session_set key) end-to-end.
  Load this whenever you add a new key to libtransmission/session-settings.h,
  add a TR_KEY_ quark for a session option, wire a setting through
  rpcimpl.cc session_accessors(), expose a config-file setting in the Qt/GTK/
  macOS/web prefs dialogs or transmission-remote, or touch docs/rpc-spec.md /
  docs/Editing-Configuration-Files.md for a session option. This is the change
  type most often done INCOMPLETELY — the frontends, docs, and the rpc-test
  ExpectedKeysUnsorted list are the easy-to-forget parts.
---

# Adding a session setting end-to-end

A "session setting" lives in `settings.json` and is readable/writable over RPC
via `session_get` / `session_set`. Because it crosses the whole stack (core →
RPC → docs → four GUIs → CLI → tests), it is the change most often shipped
half-finished. Work the layers in order and don't stop at "it compiles."

Before you start, skim the two exemplar commits — they are the source of truth
for the mechanical steps and worth imitating literally:

- `f9088a36e feat: recent paths (#55)` — the freshest full example (core, RPC,
  docs, tests, Qt, GTK). Read-only session_get keys. **Primary pattern.** One
  caveat: its docs change is `rpc-spec.md` only — it adds no
  `Editing-Configuration-Files.md` entry (its keys are read-only MRU history), so
  don't treat it as the model for that file; checklist section 2 still requires it.
- `24f58f70e feat: sequential download ... session-* RPC (#7047)` — a
  read/**write** session key's doc shape (both `session_get` and `session_set`
  changelog rows). Its `rpcimpl.cc` predates today's accessor map, so copy the
  RPC structure from current code, not from that commit.

## Mental model: what is automatic and what is not

The settings system is a compile-time serializer, not hand-written load/save.
Each settings struct (`tr::SessionSettings` in
`libtransmission/session-settings.h`) owns a `Fields` tuple of
`Field<&Struct::member>{ TR_KEY_x }` entries. Adding a member **and** its
`Field` entry is all it takes for the value to round-trip to/from
`settings.json` — **persistence, defaults, and type conversion are automatic.**
You never write parsing code.

Everything else (RPC exposure, docs, GUI widgets, CLI flags) is *not* automatic
and is exactly what people forget.

## Minimum vs. full surface

Per `CONTRIBUTING.md` (lines 69–72):

- **Required for every new setting:** reachable via the **C API** *and* the
  **RPC/JSON API**. That means: the core field, the quark, the RPC accessor,
  and the docs. This is the floor — do not skip it.
- **Optional per setting:** the GUI. "When adding advanced features, consider
  exposing them only in the config file instead of the UI" — four native
  clients make GUI work expensive, so a config-file-only setting is a
  legitimate, complete change. Add GUI widgets only where the setting genuinely
  belongs in front of users.

So a valid minimal change is **core + RPC + docs + tests**, with zero frontend
edits. A "full" change additionally wires whichever of Qt/GTK/macOS/web/CLI make
sense.

## The ordered checklist (summary)

Read `references/checklist.md` for the exact edit in each file, exemplar line
references, and the per-frontend binding patterns. The short form:

**Core (always):**
1. `libtransmission/session-settings.h` — add the member (with default) to
   `struct SessionSettings` **and** an entry in its `Fields` tuple. (Alt-speed
   settings → `SessionAltSpeedSettings`; RPC-server settings →
   `RpcServerSettings`.)
2. `libtransmission/quark.h` — add `TR_KEY_x` to the enum.
3. `libtransmission/quark.cc` — add `"x"sv,` to the `MyStatic` array.
4. `libtransmission/session.h` / `session.cc` — add typed accessor(s) if the
   setting needs programmatic access (the C API used by the GTK/macOS clients,
   or internal callers). Copy a neighbor.

**RPC (always):**
5. `libtransmission/rpcimpl.cc` — add a `map.try_emplace(TR_KEY_x, getter,
   setter)` inside `session_accessors()`. `setter = nullptr` makes it
   read-only.
6. `docs/rpc-spec.md` — session args table row (§4.1); read-only list if no
   setter; a changelog row under the current in-development version.
7. `docs/Editing-Configuration-Files.md` — Options entry with type + real
   default.

**Tests (always):**
8. `tests/libtransmission/rpc-test.cc` — add `TR_KEY_x` to
   `ExpectedKeysUnsorted` in the `sessionGet` test **(the famous forgotten
   one)**.
9. `tests/libtransmission/session-test.cc` — behavior test if you added
   accessor logic.

**Frontends (only where the setting should be user-visible):**
10. Qt → `libtransmission-app/prefs.h` `SessionPrefs` + `qt/PrefsDialog.{ui,cc}`
    **+ a `case` in `Session::updatePref()` in `qt/Session.cc`** — without it the
    widget never reaches the *running* session (see checklist §4).
11. GTK → `gtk/PrefsDialog.cc` + **both** `gtk/ui/gtk3/PrefsDialog.ui` and
    `gtk/ui/gtk4/PrefsDialog.ui` **+ a `case` in `on_prefs_changed()` in
    `gtk/Application.cc`** — same reason.
12. macOS → `macosx/Defaults.plist` + `macosx/PrefsController.mm` + the `.xib`.
13. web → `web/src/prefs-dialog.js` (`dataset.key = 'x'`).
14. transmission-remote → `utils/remote.cc` + `utils/transmission-remote.1`.

## Famous gotchas (verified against the repo)

- **`ExpectedKeysUnsorted` in `tests/libtransmission/rpc-test.cc`.** The
  `sessionGet` test diffs expected vs. actual keys **both directions**. A new
  `session_get` key you forget to add here fails the test as an *unexpected*
  key, even though your code is correct. This is the single most-forgotten step.
  (Only keys with a getter belong here.)

- **The `session_accessors()` map is capacity-bounded.** It is a
  `small::max_size_map<tr_quark, SessionAccessors, 64U>` and already holds ~60
  entries. If your addition would push it past 64, bump the `64U` template
  argument. Overflow is **not** silent and **not** a compile error: the map is a
  static local built on first use, and the overflowing `try_emplace` throws
  `std::length_error` at runtime — the first RPC test to run crashes.

- **Quark ordering is compile-time enforced.** `libtransmission/quark.cc` has
  `static_assert(quarks_are_sorted(), ...)` and
  `static_assert(std::size(MyStatic) == TR_N_KEYS)`. The `MyStatic` string
  array must stay sorted **by string value**, and the `quark.h` enum and the
  `quark.cc` array must stay in lockstep (same order, same count). Insert
  `TR_KEY_x` and `"x"sv` at the position that keeps the strings alphabetical. A
  wrong spot is a build error, not a silent bug — but expect to fix it.

- **Use `snake_case` for the new key.** Entries containing `_camel`, `_kebab`,
  or `_APICOMPAT` are deprecated back-compat aliases for legacy RPC names; do
  **not** add those for a brand-new setting (see the header comment in
  `quark.h`). You only touch `api-compat` machinery when adding an alias for an
  *existing* renamed key — not here.

- **Doc defaults drift.** `docs/Editing-Configuration-Files.md` is
  hand-maintained and can lag the code, so **copy the *actual* default from
  `session-settings.h`, not from the doc.** Defaults have drifted before — at the
  time of writing `upload_slots_per_torrent` was listed as 14 in the doc vs `8U`
  in `session-settings.h`.

- **Whether to bump `rpc_version_semver` is not decided here.** Adding a
  `session_get`/`session_set` key is an additive RPC change. The version-bump
  policy (and where the version numbers live in `docs/rpc-spec.md`) is owned by
  the **rpc-api skill** — consult it rather than guessing.

## Build, format, test

- Configure/build and the clang-tidy policy: **building skill**.
- Running/adding the gtest cases above: **testing skill**. At minimum run the
  `rpc`, `session`, and `settings` libtransmission tests.
- Run `./code_style.sh` (clang-format 22; also formats JS) before committing.
- Branch/commit/PR conventions: `CONTRIBUTING.md` (repo root). **Every PR
  description must start with the AI-agent disclosure block mandated by
  `AGENTS.md`** — the `> [!NOTE]` / `> This pull request was created by an AI
  agent.` admonition.

## Cross-references

- RPC spec edits, versioning, and back-compat: **rpc-api skill**.
- Per-client idioms and where dialogs live: **qt-client**, **gtk-client**.
  The **macos-client** skill (incl. editing the Xcode project from Linux) and the
  **web-client** skill cover those halves; the macOS/web mechanical steps are also
  laid out in `references/checklist.md` §4.
- C++ style for the fields/lambdas you add: **cpp-conventions skill**.
