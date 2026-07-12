# Session setting: exhaustive file-by-file checklist

Every file a new/changed **session** preference may touch, in the order to work
them, with the exact edit and a real anchor to copy from. Verified against the
repo at the time of writing (main). Line numbers drift — grep for the anchor
symbol, don't trust the number.

Legend: **[core]** = always required (C API + RPC floor). **[opt]** = only when
the setting should be user-visible in that surface.

---

## 1. Core — the setting itself

### `libtransmission/session-settings.h` **[core]**
Two edits inside `struct SessionSettings`:

1. A data member with its default, e.g.
   `bool my_thing_enabled = false;`
   (Members are grouped by type/alignment — put it with its neighbors, e.g.
   the `bool` block or the `std::string` block.)
2. A matching entry in the `static constexpr auto Fields = std::make_tuple(...)`
   list, kept alphabetical **by the C++ member name** — the `TR_KEY_` string
   doesn't always match the member (e.g. `is_incomplete_file_naming_enabled` →
   `TR_KEY_rename_partial_files`), so sort by the `&SessionSettings::member` side.
   Nothing enforces this order (unlike `quark.cc`), so it's purely cosmetic; don't
   reorder existing entries that look "wrong":
   `Field<&SessionSettings::my_thing_enabled>{ TR_KEY_my_thing_enabled },`

That is the whole persistence story: `tr::serializer::load`/`save` walk `Fields`
to read/write `settings.json` and to supply defaults. No hand-written I/O.

Pick the right struct:
- Normal session setting → `SessionSettings`.
- Alt-speed / turtle-mode scheduler setting → `SessionAltSpeedSettings`.
- RPC server setting (`rpc_*`) → `RpcServerSettings`.

`is_settings_key()` at the bottom of the header already covers all three via
`tr::serializer::has_key<...>`, so it needs no edit.

### `libtransmission/quark.h` **[core]**
Add `TR_KEY_my_thing_enabled,` to the anonymous `enum`. Use **snake_case**.
Position it so the *string value* stays alphabetically sorted relative to
neighbors (the enum mirrors the sorted string array in `quark.cc`). Do **not**
add `_camel`/`_kebab`/`_APICOMPAT` variants for a new key — those exist only to
keep deprecated legacy RPC spellings working.

### `libtransmission/quark.cc` **[core]**
Add `"my_thing_enabled"sv,` to the `MyStatic` array at the position that keeps
the array sorted by string. A trailing `// rpc` / `// gtk app` style comment is
customary. Two `static_assert`s guard you:
`static_assert(quarks_are_sorted(), ...)` and
`static_assert(std::size(MyStatic) == TR_N_KEYS)`. Wrong position or a
mismatched count = compile error.

### `libtransmission/session.h` / `session.cc` **[core, usually]**
Add typed accessor(s) so C-API callers (GTK/macOS clients) and internal code can
read/write the value. Simple mirrors of `settings_.my_thing_enabled` can be
inline in `session.h`; anything with logic goes in `session.cc`. Copy a
neighbor. Exemplar (`f9088a36e`): `recent_download_paths()` getter inline in
`session.h`, `add_recent_download_dir()` defined in `session.cc`.

If the setting also needs a public C function (`tr_sessionSetX` /
`tr_sessionGetX` in `libtransmission/transmission.h`) — required if the macOS or
GTK client will drive it through the C API — add and implement it there too.
Grep `transmission.h` for an existing `tr_sessionSet*` pair and follow it.

---

## 2. RPC

### `libtransmission/rpcimpl.cc` **[core]**
Inside `session_accessors()` add one entry (conventionally alphabetical by member
name to match neighbors, but placement here is cosmetic — `max_size_map` is
ordered and self-sorts on insert):

```cpp
map.try_emplace(
    TR_KEY_my_thing_enabled,
    [](tr_session const& src) -> tr_variant { return src.my_thing_enabled(); },
    [](tr_session& tgt, tr_variant const& src, ErrorInfo& /*err*/) {
        if (auto const val = src.value_if<bool>()) {
            tgt.set_my_thing_enabled(*val);
        }
    });
```

- The pair is `{getter, setter}`. Pass `nullptr` as the setter for a
  **read-only** key (exemplar: `recent_download_paths` in `f9088a36e`).
- For enum/bespoke types use `tr::serializer::to_value<T>(src)` instead of
  `src.value_if<T>()` (see the `alt_speed_time_day` accessor).
- **Capacity gotcha:** the map is `small::max_size_map<tr_quark,
  SessionAccessors, 64U>` and holds ~60 entries. If you'd exceed 64, bump the
  `64U` — overflow throws `std::length_error` when the static map is first built
  (crashing the RPC tests). It is not a compile error and not a silent drop.

`sessionGet` and `sessionSet` iterate this map generically — no other edit in
`rpcimpl.cc` is needed.

### `docs/rpc-spec.md` **[core]**
1. **Session args table (§4.1 "Session parameters"):** add a row, alphabetical:
   `| \`my_thing_enabled\` | boolean | what it does`
   Append ` *read-only*` to the description if there's no setter.
2. **Read-only list** (the bulleted "except:" list just below the table): add
   the key there too *only if* it is read-only.
3. **Changelog:** add a row under the current in-development version block at the
   bottom of the file (currently `Transmission 4.2.0 (rpc_version_semver 6.1.0
   ...)`). Read/write keys get two rows:
   `| \`session_get\` | new arg \`my_thing_enabled\``
   `| \`session_set\` | new arg \`my_thing_enabled\``
   Read-only keys get only the `session_get` row.

Whether this addition should also bump `rpc_version_semver` is decided by the
**rpc-api skill** — do not invent a version bump here.

### `docs/Editing-Configuration-Files.md` **[core]**
Add the key to the `### Options` list under the appropriate `####` subsection
(Bandwidth, Blocklists, Peers, Queuing, etc.), with type and the **real**
default copied from `session-settings.h`:
` * **my_thing_enabled:** Boolean (default = false) One-line description.`
(This file drifts from code — verify neighbors' defaults while you're here.)

---

## 3. Tests

### `tests/libtransmission/rpc-test.cc` **[core]**  ← most-forgotten
In `TEST_F(RpcTest, sessionGet)`, add `TR_KEY_my_thing_enabled,` to the
`ExpectedKeysUnsorted` array (alphabetical). The test does a two-way
`set_difference` of expected vs. returned keys, so any getter you added that
isn't listed here fails as an *unexpected* key. Read-only or read/write, if it
has a getter it goes in this list.

### `tests/libtransmission/session-test.cc` **[core, if you added logic]**
Add a `TEST_F(SessionTest, ...)` exercising your accessor's behavior. Exemplar:
`recentDownloadDirs` / `recentRelocateDirs` in `f9088a36e` test the getter,
setter, and edge cases against a live session.

### `tests/libtransmission/settings-test.cc` **[rarely]**
Only touch this if your field introduces a **new type** that needs a
serializer/converter round-trip test. It tests representative types
(bool/double/encryption-mode), not every key, so an ordinary
bool/int/string/vector field needs no change here.

### `tests/libtransmission-app/prefs-test.cc` **[opt]**
If you wired the setting into the shared Qt app layer (`SessionPrefs`, below),
this is where get/set round-trips for that layer are tested. Extend it if you
add non-trivial app-layer behavior.

See the **testing skill** for how to build and run these (ctest / gtest).

---

## 4. Frontends — add only where the setting should be visible

Per `CONTRIBUTING.md`, GUI exposure is optional; a config-file-only setting is
complete after sections 1–3.

### Qt **[opt]**
The Qt client now shares the `libtransmission-app` layer — there is **no**
`qt/Prefs.cc` and no pref enum anymore (`qt/Prefs.h` just subclasses
`tr::app::Prefs`). To expose a session setting:

1. `libtransmission-app/prefs.h` — add a member to `struct SessionPrefs` **and**
   a `Field<&SessionPrefs::my_thing_enabled_>{ TR_KEY_my_thing_enabled }` entry
   in its tuple (exemplar `f9088a36e` added `recent_download_paths_`). This is
   what makes `prefs_is_core()` recognize the key.
2. `qt/PrefsDialog.ui` — add the widget (checkbox/spinbox/etc.). No same-domain
   neighbor is required; if the obvious one is missing (e.g. the whole Queue
   domain has no checkbox today) copy the shape of any checkbox, e.g.
   `enablePexCheck`.
3. `qt/PrefsDialog.cc` — bind it in the constructor with the generic
   `initWidget(ui_.myThingCheck, TR_KEY_my_thing_enabled);`. `initWidget` is
   overloaded per widget type and handles get/set through `prefs_` for you.
4. **`qt/Session.cc` — add `case TR_KEY_my_thing_enabled:` to the switch in
   `Session::updatePref()`**, grouped with the plain passthrough keys that run
   `sessionSet(key, prefs_.get<tr_variant>(key)); break;`. **Not optional.** Steps
   1–3 only update the local `prefs_` cache and persist to `settings.json`; this
   case is what fires the `session_set` RPC that pushes the value onto the
   *running* session. Skip it and the checkbox compiles, toggles, and saves but
   never applies until restart — and no test catches it (`tr_quark` is a `size_t`,
   so `-Wswitch` can't flag the missing case; the `default:` arm just logs
   `unhandled pref`).

See the **qt-client skill**.

### GTK **[opt]**
GTK stores session prefs generically by quark (no per-key struct field, and no
default entry in `gtk/Prefs.cc` — session defaults come from libtransmission via
`tr_sessionLoadSettings`; the default list in `gtk/Prefs.cc` is for GTK
app-only prefs). To expose a session setting:

1. Add the widget to **both** `gtk/ui/gtk3/PrefsDialog.ui` **and**
   `gtk/ui/gtk4/PrefsDialog.ui`, using the same object `id` in each. No
   same-domain neighbor is required; if the obvious one is missing (e.g. the whole
   Queue domain has no checkbox today) copy the shape of any existing checkbox.
2. `gtk/PrefsDialog.cc` — bind it with `init_check_button("my_thing_check",
   TR_KEY_my_thing_enabled);` (there are sibling helpers for spin buttons /
   entries / combos — grep `init_` in that file).
3. **`gtk/Application.cc` — add `case TR_KEY_my_thing_enabled:
   tr_sessionSetMyThing(tr, gtr_pref_flag_get(key)); break;` to the switch in
   `Application::Impl::on_prefs_changed()` (~line 1051).** **Not optional.** Step
   2's `init_check_button` → `set_pref` only writes the store and calls
   `gtr_pref_save()` (persist to `settings.json`); this case is what calls the
   `tr_sessionSetX` C function that pushes the value onto the *running* session.
   Skip it and the checkbox persists across restarts yet does nothing live until
   relaunch — no compile error (the `default:` arm is a silent `break;`). Beware
   the similarly-named `Session::Impl::on_pref_changed` (**singular**, in
   `gtk/Session.cc`): a session-wide setter goes in `Application`'s
   `on_prefs_changed` (**plural**), not there.

See the **gtk-client skill**.

### macOS **[opt]**
The macOS client is fully independent: it uses its **own** NSUserDefaults key
names (not `TR_KEY_` quarks) and bridges to the session through the `tr_session*`
**C API**. To expose a session setting:

1. `macosx/Defaults.plist` — add the default for your NSUserDefaults key.
2. `macosx/PrefsController.mm` — read it (`[self.fDefaults boolForKey:@"MyKey"]`)
   and push to the session (`tr_sessionSetMyThing(self.fHandle, ...)`); this is
   why section 1 may need a `tr_sessionSet*` C function.
3. Add the control to the relevant `.xib`.

Editing `Transmission.xcodeproj` from Linux and the macOS build have their own
rules — see the **macos-client** skill; the steps above cover the basic wiring.

### web **[opt]**
`web/src/prefs.js` is for **local web-UI cookie prefs**, not session settings —
don't touch it. Session settings bind by RPC key:

1. `web/src/prefs-dialog.js` — create the control and set
   `control.dataset.key = 'my_thing_enabled';` (the snake_case RPC key). The
   dialog's `_update()` populates every `[data-key=...]` from `session_get`, and
   changes are pushed via `remote.savePrefs({ [key]: value })` (session_set).
   Exemplar: the `utp_enabled` checkbox.

See the **web-client** skill for the web UI; the step above covers the basic wiring.

### transmission-remote **[opt]**
`utils/remote.cc`:

1. Add a `tr_option` to the `Options` array (unique numeric id, long/short flag,
   help text, arg kind).
2. Add a matching `case` in the getopt switch that stuffs the session-set args
   dict: `sargs->insert_or_assign(TR_KEY_my_thing_enabled, value);` (grep
   `insert_or_assign(TR_KEY_utp_enabled` and `insert_or_assign(TR_KEY_speed_limit_up`
   for the two shapes). If you want it printed in session info, add it to the
   session-get display code too.
3. Document the flag in `utils/transmission-remote.1`.

---

## Quick self-audit before you open the PR

- [ ] Field + `Fields` entry in `session-settings.h`
- [ ] `TR_KEY_` in `quark.h` **and** string in `quark.cc`, both sorted
- [ ] Accessor(s) in `session.{h,cc}` (+ `transmission.h` C fn if a GUI needs it)
- [ ] `session_accessors()` entry in `rpcimpl.cc` (setter or `nullptr`)
- [ ] `rpc-spec.md`: table row (+ read-only list) + changelog row(s)
- [ ] `Editing-Configuration-Files.md`: Options entry with the real default
- [ ] `rpc-test.cc` `ExpectedKeysUnsorted` updated  ← don't forget
- [ ] behavior test in `session-test.cc` if you added logic
- [ ] each intended GUI/CLI surface wired (or deliberately skipped as
      config-only)
- [ ] `./code_style.sh` clean; rpc/session/settings tests pass
