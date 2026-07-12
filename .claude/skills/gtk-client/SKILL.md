---
name: gtk-client
description: Develop the GTK desktop client in gtk/ (a gtkmm C++ app). Load when editing gtk/*.cc / gtk/*.h, gtk/ui/gtk3/*.ui or gtk/ui/gtk4/*.ui, PrefsDialog/MainWindow/DetailsDialog/Application/Session, adding or wiring a GTK preference widget, touching the tray/appindicator icon or desktop notifications, adding translatable _()/ngettext() strings in the GTK client, or building/running transmission-gtk. Covers GTK3-vs-GTK4 dual-targeting, GResource .ui loading, the Session core wrapper, and the fact that POTFILES.in is stale/unused.
---

# GTK client development

The GTK client lives in `gtk/`. It is a **gtkmm (C++) application**, not C and not GTK-in-C.
Every window/dialog is a C++ class (`Application`, `MainWindow`, `PrefsDialog`, `DetailsDialog`,
`Session`, …). It embeds libtransmission **in-process**: `main.cc` parses options and constructs
`Application`, whose startup path (in `Application.cc`) calls `tr_sessionInit()` and hands the
`tr_session*` to `Session::create()` — the bridge every other GTK class talks through.

Before diving in, know the sibling skills so you don't re-derive their material:
- **building** — CMake/Ninja configure, `RUN_CLANG_TIDY`, `code_style.sh`. This skill only gives the
  GTK-specific target/options.
- **cpp-conventions** — C++ style, license headers, idioms shared with `libtransmission`.
- **codebase-map** — where subsystems live, including the shared `libtransmission-app/` layer.
- **add-session-setting** — the full end-to-end checklist for a *new* persisted preference across all
  clients. This skill covers only the GTK half (binding a widget to an existing key).
- **translations** — the three translation pipelines in depth.
- **debugging** — running the daemon/clients, poking RPC, logs.

## GTK 3 and GTK 4 are both supported — always

This is the single most important fact. The client builds against **either** gtkmm-3 **or** gtkmm-4
from one source tree. Minimums (from top-level `CMakeLists.txt`): `gtkmm-3.0 >= 3.24.0`
(with glibmm/giomm 2.4) or `gtkmm-4.0 >= 4.11.1` (with glibmm/giomm 2.68). The version is chosen at
configure time by `USE_GTK_VERSION` (`AUTO` | `3` | `4`; `AUTO` prefers 4). CMake sets `GTK_VERSION`
and defines `transmission::gtk_impl` accordingly.

Consequences you must respect in any change:
- **Never write toolkit-version-specific code inline.** Use the compat layer in `gtk/GtkCompat.h`:
  `GTKMM_CHECK_VERSION(major,minor,micro)`, `IF_GTKMM4(then, else)`, and the `TR_GTK_*` /
  `TR_GDK_*` / `TR_GLIB_*` / `TR_CAIRO_*` / `TR_PANGO_*` macros that paper over enum renames
  between the two APIs (e.g. `TR_GTK_ORIENTATION(HORIZONTAL)`). There are parallel `IF_GLIBMM2_68`,
  `IF_CAIROMM1_16`, `IF_PANGOMM2_48` guards. When you hit a compile error that exists in one
  toolkit only, reach for these before `#if`.
- **UI files are duplicated per toolkit:** `gtk/ui/gtk3/*.ui` and `gtk/ui/gtk4/*.ui`. Editing a
  dialog's layout means editing **both** copies (or deliberately diverging them). The build compiles
  only the `UI_SUBDIR` matching `GTK_VERSION`.
- **The list rendering model differs.** `TorrentCellRenderer.cc` is compiled **GTK-3-only**
  (`tr_allow_compile_if([[GTK_VERSION EQUAL 3]] ...)` in `gtk/CMakeLists.txt`); GTK 4 instead uses
  the list-item widgets `ui/gtk4/TorrentListItemCompact.ui` and `TorrentListItemFull.ui` (no gtk3
  equivalents). Keep this in mind when touching the torrent list.

For the full compat-macro catalog, the gtk3/gtk4 divergences, and the "add a new .ui / source file"
checklist, read `references/gtk3-vs-gtk4.md`.

## Directory tour

Everything is under `gtk/`. Highlights:
- `main.cc` — entry point: `tr::app::init()`, i18n init, option parsing, then constructs and runs `Application(...)`.
- `Application.{cc,h}` — `Gtk::Application` subclass; owns the `Session`, the `MainWindow`, and the
  tray icon; routes `on_startup/on_activate/on_open` and reacts to preference changes.
- `Session.{cc,h}` — the wrapper around the in-process `tr_session*`. See below.
- `MainWindow`, `DetailsDialog`, `PrefsDialog`, `MakeDialog`, `OptionsDialog`, `StatsDialog`,
  `MessageLogWindow`, `RelocateDialog` — the windows/dialogs, each paired with a `.ui`.
- `FilterBar`, `TorrentCellRenderer` (gtk3), `Torrent`, `TorrentFilter`, `TorrentSorter`,
  `FileList`, `FreeSpaceLabel`, `PathButton` — reusable widgets/models.
- `Actions.{cc,h}` — the `Gio::Action` map wired to menus (`transmission-ui.xml`).
- `Notify.{cc,h}` — desktop notifications. `SystemTrayIcon.{cc,h}` — the tray icon.
- `Prefs.{cc,h}` — the `gtr_pref_*` free-function API over the settings store.
- `GtkCompat.h`, `Utils.{cc,h}`, `Macros.h` — helpers.
- `.clang-format`, `.clang-tidy` — GTK-tree-specific lint config (see below).

Shared, toolkit-agnostic logic lives one level up in `libtransmission-app/` (`tr::app` namespace):
`FaviconCache`, `Prefs` template, `RpcClient`/`RpcQueue`, `converters`, `app.h`. The GTK `Session`
and `Prefs` build on these. When behavior is shared with the Qt client, it usually belongs there —
see the codebase-map skill.

## Talking to the core: the `Session` class

`gtk/Session.h` includes `<libtransmission/transmission.h>` and hands out raw `tr_session*` /
`tr_torrent*` — the GTK client manipulates libtransmission directly (it is not a remote RPC client
by default). But it *also* drives a local RPC path for some operations: `Session::exec(tr_quark
method, tr_variant&& params)` and the `tr::app::RpcClient` in `libtransmission-app/`. Recently-used
paths, for example, are fetched via a blocking local RPC (`get_recent_download_paths()`).

`Session` exposes sigc++ signals the UI subscribes to: `signal_prefs_changed()`,
`signal_torrents_changed()`, `signal_add_error()`, `signal_add_prompt()`, `signal_busy()`,
`signal_port_tested()`, `signal_blocklist_updated()`. Emit/observe these rather than reaching across
classes. `Session::set_pref<T>(key, val)` is the canonical write path: it updates the store, emits
`signal_prefs_changed(key)`, and saves to disk — only if the value actually changed. It does **not**
itself apply the value to the running `tr_session*`; that happens in `Application`'s `on_prefs_changed`
handler (see Preferences below).

## Preferences: reading, writing, binding a widget

The settings store is reached through the free functions in `gtk/Prefs.h`:
`gtr_pref_flag_get/set`, `gtr_pref_string_get/set`, `gtr_pref_int_get<T>/int_set`,
`gtr_pref_get<T>(key, default)`, `gtr_pref_lookup<T>(key)`, `gtr_pref_get_all()` (the backing
`tr::Settings`), plus `gtr_pref_init(config_dir)` and `gtr_pref_save(session)`. Keys are `tr_quark`s
(`TR_KEY_*`).

`PrefsDialog.cc` is the model to imitate for wiring a widget to a key. Its `PageBase` provides
helpers — `init_check_button(name, key)`, spin-button/entry/text-buffer/path-button/combo binders —
each of which looks a widget up in the `Gtk::Builder` by its `.ui` id, seeds it from
`gtr_pref_*_get(key)`, and connects the widget's change signal to `core_->set_pref(key, value)`.
Example (`init_check_button`): `set_active(gtr_pref_flag_get(key))` then
`signal_toggled().connect([...]{ core_->set_pref(key, button->get_active()); })`.

To surface an **existing** key in the GTK prefs UI:
1. Add the control to **both** `ui/gtk3/PrefsDialog.ui` and `ui/gtk4/PrefsDialog.ui` with a stable id.
2. In the matching `*Page` class in `PrefsDialog.cc`, call the right `init_*` helper with that id and
   the `TR_KEY_*` quark.
3. **Usually required for a real session setting:** add a `case TR_KEY_*` to the big switch in
   `Application::Impl::on_prefs_changed` (`Application.cc`, ~line 1051) that calls the matching core
   setter. Step 2's `set_pref` only writes the store and saves to disk — it does **not** push the
   value into the running `tr_session*` — so without this case the checkbox persists across restarts
   yet does nothing to the live session until relaunch (no compile error; you catch it only by
   toggling and seeing no effect). That switch already has ~45 such arms, e.g. `TR_KEY_pex_enabled` →
   `tr_sessionSetPexEnabled`, `TR_KEY_port_forwarding_enabled` → `tr_sessionSetPortForwardingEnabled`,
   plus the speed/ratio/blocklist/dht/rpc keys. Skip this step **only** for GTK-only UI-state prefs
   (sort order, window geometry, filterbar visibility) that no libtransmission API consumes.

Watch for a similarly-named sibling: `Session::Impl::on_pref_changed` (**singular**, `gtk/Session.cc`)
is a *second, smaller* switch on the same `signal_prefs_changed` signal, handling only `sort_mode`,
`sort_reversed`, `peer_limit_global`/`peer_limit_per_torrent`, `inhibit_desktop_hibernation`, and
`watch_dir`/`watch_dir_enabled`. Grepping `on_pref` finds both — put a session-wide setter in
`Application`'s `on_prefs_changed` (**plural**), not here.

`gtk/Prefs.cc`'s `get_default_app_settings()` carries a comment saying you *must* add a default there
for any new key. That applies **only** to the GTK-UI-only prefs above; a genuine tr_session-backed
`TR_KEY_*` already gets its default from libtransmission's `session-settings.h`, and that map is
merged *fill-missing-only*, so an entry there for such a key is a redundant no-op — don't add one.

Adding a brand-new preference (new key, defaults, RPC, other clients, tests) is a cross-cutting job —
follow the **add-session-setting** skill; do the GTK piece with the pattern above.

## Loading `.ui` files (GResource + derived widgets)

`.ui` files are compiled into the binary as a GResource, not read from disk. `gtk/CMakeLists.txt`
calls `tr_target_glib_resources(... transmission.gresource.xml ${UI_SUBDIR}/transmission-ui.gresource.xml)`.
The resource prefix is `/com/transmissiontorrent/transmission`; build paths with
`gtr_get_full_resource_path("Foo.ui")` (defined in `Utils.cc`).

A window/dialog is created by loading its builder and pulling a **derived** widget out:
```cpp
auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("PrefsDialog.ui"));
return std::unique_ptr<PrefsDialog>(gtr_get_widget_derived<PrefsDialog>(builder, "PrefsDialog", parent, core));
```
`gtr_get_widget_derived<T>` (in `gtk/Utils.h`) wraps the gtk3/gtk4 API difference; the class's
constructor takes `(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, ...)` and
forwards `cast_item` to its gtkmm base (see `FilterBar`, the `PrefsDialog` `*Page` classes,
`MainWindow`). Non-derived children come from `gtr_get_widget<T>(builder, "id")`.

When you add a **new** `.ui`, register it in **both** `gtk/ui/gtk3/transmission-ui.gresource.xml` and
`gtk/ui/gtk4/transmission-ui.gresource.xml` **and** in the two `target_sources` blocks in
`gtk/CMakeLists.txt`. (Non-`.ui` resources — icons, CSS, the `transmission-ui.xml` menu model — live
in `gtk/transmission.gresource.xml`.)

## Translatable strings

In the GTK client, `_()` and `ngettext()` come from **`<glibmm/i18n.h>`** (include it), i.e. real
gettext — *not* libtransmission's no-op `#define _(a) (a)`. `main.cc` binds the text domain
`transmission-gtk` to `TRANSMISSIONLOCALEDIR`. Mark user-facing strings with `_("…")`; use
`ngettext(singular, plural, n)` for counts. For fmt-style strings that contain `{}` placeholders,
put a `// xgettext:no-c-format` comment on the line above (see `gtk/Torrent.cc`,
`gtk/DetailsDialog.cc`). In `.ui` files, set `translatable="yes"` on the relevant properties.

**Do not touch `po/POTFILES.in` or `po/POTFILES.skip`.** They are **stale legacy files**: they still
list pre-C++-port paths like `gtk/actions.c`, `gtk/tr-core.c`, `gtk/tr-prefs.c` that no longer exist,
and nothing in CMake or the build references them (last meaningful edit was commit `0a27346ef`,
years ago). No `.pot` is generated in-repo; string **extraction happens on Transifex**, and the
`po/*.po` catalogs are periodically synced back in bulk commits (e.g. `cf0cec32f chore: sync
translations`). So adding a string or a source file needs **no** POTFILES bookkeeping.

Note also that only a subset of languages is actually compiled/installed: `po/CMakeLists.txt`
hardcodes 17 `LINGUAS` (da es eu fi fr he hu is ja nl pl ru sv tr uk zh_CN zh_TW), even though `po/`
holds ~90 `.po` files (the gettext-standard `po/LINGUAS` lists most of them, 86). Only that hardcoded
17-language list matters for the build. `docs/Translating.md` is the authoritative policy doc
(Transifex, 95%-complete rule). The Qt and macOS pipelines differ (Qt uses `.ts` files compiled with
`lrelease`; macOS uses `.strings`); the **translations** skill covers all three in depth.

## Tray icon and appindicator

`SystemTrayIcon.{cc,h}` provides the notification-area icon, gated by the `TR_KEY_show_notification_area_icon`
preference; `Application` creates/destroys it in `on_prefs_changed`. Two backends:
- **AppIndicator** — only when `WITH_APPINDICATOR` is on, which CMake forces **GTK-3-only**
  (`if(ENABLE_GTK AND WITH_APPINDICATOR AND GTK_VERSION EQUAL 3)`; the option's help text literally
  says "GTK+ 3 only"). `cmake/FindAPPINDICATOR.cmake` prefers Ayatana (`ayatana-appindicator3-0.1`),
  setting the `APPINDICATOR_IS_AYATANA` compile define, and falls back to the classic
  `appindicator3-0.1`. The source path is guarded by the `HAVE_APPINDICATOR` compile define.
- Otherwise the code falls back to `Gtk::StatusIcon` (`#if !defined(HAVE_APPINDICATOR)`).

Desktop **notifications** (torrent added/finished pop-ups) are separate: `Notify.cc` talks to
`org.freedesktop.Notifications` over D-Bus via `Gio::DBus::Proxy`, initialized by `gtr_notify_init()`.

## Building and running just the GTK client

Configure with the GTK client enabled (see the **building** skill for the full configure/generator
command and cache options). GTK-specific knobs:
- `-DENABLE_GTK=ON`
- `-DUSE_GTK_VERSION=3` or `=4` to pin a toolkit (default `AUTO` prefers 4) — build both ways when a
  change could affect either.
- `-DWITH_APPINDICATOR=ON|OFF|AUTO` (GTK 3 only).

Build only this target (`TR_NAME` is `transmission`, so the target is `transmission-gtk`):
```
cmake --build <build-dir> --target transmission-gtk
```
The binary lands at `<build-dir>/gtk/transmission-gtk`. Run it against a throwaway profile so you
don't disturb your real session: `<build-dir>/gtk/transmission-gtk --config-dir /tmp/tr-gtk`. CLI
options (from `main.cc`): `--config-dir`/`-g`, `--paused`/`-p`, `--minimized`/`-m`, `--version`/`-v`.
See the **debugging** skill for running a throwaway daemon
(`transmission-daemon --foreground --config-dir /tmp/tr-daemon`) and poking it over RPC with
`transmission-remote`.

## Lint and headers

`gtk/.clang-tidy` and `gtk/.clang-format` scope lint to `.*/gtk/.*` with a GTK-specific check set —
don't fight it; run the tree's `code_style.sh` / clang-tidy as the **building** skill describes
(tidy is slow; save it for presubmit). One gotcha: the GTK tree mixes **two license headers** — some
files are MIT ("Transmission authors and contributors", e.g. `Application.h`, `main.cc`), others are
GPL ("Mnemosaic LLC", e.g. `PrefsDialog.h`, `GtkCompat.h`). Copy the header from the file you're
editing or its nearest sibling rather than assuming one; see the **cpp-conventions** skill.
