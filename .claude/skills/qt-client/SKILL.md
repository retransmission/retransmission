---
name: qt-client
description: >
  How to work in the Qt desktop client under qt/ — the transmission-qt app. Load this whenever
  you touch anything in qt/ (MainWindow, DetailsDialog, PrefsDialog, Torrent/TrackerModel,
  delegates, *.ui files, Session, RpcClient, Prefs), or when you need to know how the Qt client
  talks to a session (in-process vs a local/remote daemon), how transmission-qt's command-line
  flags (-r/--remote, -p/--port, -u/--username, -w/--password, -g, -m) work, how a preference is
  stored and bound to a widget in PrefsDialog, which Qt versions are supported, how .ts/.qm
  translations are built, or why you must concatenate TR_PROJ_* macros into a QStringLiteral
  instead of passing them through QString::arg(). Also load it before building or running just
  the Qt client against a daemon.
---

# Qt client (`qt/`)

The Qt client is a **thin GUI over the RPC API** — it never calls libtransmission's torrent
internals directly. Everything it does (add/remove torrents, read stats, change a session
setting) is a JSON-RPC request. The one exception is starting an *in-process* session, where it
spins up a `tr_session` in the same process and still talks to it over RPC. So when you add a
feature, the question is almost always "which RPC method carries this?" — see the **rpc-api**
skill for the wire format and `docs/rpc-spec.md`.

Ground truth is the source under `qt/` and the shared layer under `libtransmission-app/`. If this
skill ever disagrees with the repo, trust the repo and fix this skill. For the repo-wide layout
see the **codebase-map** skill; for C++ idioms see **cpp-conventions**; for build/tidy mechanics
see **building**; for tests see **testing**.

## Two layers: `qt/` and `libtransmission-app/`

The client was refactored so toolkit-agnostic logic lives in `libtransmission-app/` (shared with
the GTK client), and `qt/` holds only the Qt glue. This split is recent and ongoing — see commits
`a62413a66 refactor: migrate RpcClient, RpcQueue to tr::app (#85)`,
`82b6da8f8 refactor: add tr::app::Prefs base class (#21)`, and
`0acc01d3a feat: project macros (#90)`.

- `libtransmission-app/rpc-client.{h,cc}` — `tr::app::RpcClient`, the real RPC engine. Speaks to
  an in-process session via `tr_rpc_request_exec` or to a remote via `tr_web` (curl). It marshals
  every response back onto the UI thread through an injected `run_on_ui_thread` hook, so callers
  never see worker threads.
- `libtransmission-app/prefs.{h,cc}` — `tr::app::Prefs`, `AppPrefs`, `SessionPrefs` (see below).
- `libtransmission-app/rpc-queue.h` — `tr::app::RpcQueue`, a promise-like chain of RPC calls.
- `qt/RpcClient.{h,cc}` — a `QObject` wrapper that injects the Qt UI-thread hook
  (`QTimer::singleShot(0, this, fn)` — see `makeUiMarshaler` in `RpcClient.cc`) and re-exposes
  the engine's sigslot signals as Qt signals.

When a `qt/` type has a lowercase-named twin in `libtransmission-app/` (`RpcClient` ↔
`rpc-client`, `Prefs` ↔ `prefs`), the `qt/` one is the Qt adapter and the logic lives in the
shared file. Put new toolkit-independent logic in the shared layer.

## How the client reaches a session

`qt/Session.{h,cc}` owns the connection and every high-level operation (`addTorrent`,
`torrentSet`, `refreshAllTorrents`, `portTest`, …). `Session::start()` (Session.cc) branches on
one pref:

- **`TR_KEY_remote_session_enabled` true** → build a `QUrl` from the remote-session prefs
  (`remote_session_https` picks `http`/`https`, `remote_session_host`, `remote_session_port`,
  `remote_session_url_base_path` + `TrHttpServerRpcRelativePath` = `"rpc"`, plus username/password
  if `remote_session_requires_authentication`) and call `rpc_.start(url)`.
- **false** → `tr_sessionInit(config_dir, …)` in-process, then `rpc_.start(session_)`, and load
  torrents from disk.

`Session::Type` (in Session.h) is `InProcess`, `Local`, or `Remote`, computed in `computeType()`:
in-process if the `tr_session*` is non-null; otherwise from the session-id
(`tr_session_id::is_local` → `Local`, else `Remote`). Use the helpers, don't re-derive:
- `isServer()` — true only for `InProcess` (we own the `tr_session`).
- `isLocalFilesystem()` — true for `InProcess` or `Local`; controls whether path pickers /
  free-space labels make sense. `PrefsDialog` disables filesystem widgets when this is false.

`Session::local_settings()` returns the in-process session's settings (or `nullopt` when remote);
`qt/main.cc` saves them back through `Prefs::save` on exit.

### Command line (`qt/main.cc`)

Parsed with `tr_getopt` (libtransmission), not Qt. Verified options:

| Short | Long           | Arg       | Meaning |
|-------|----------------|-----------|---------|
| `-g`  | `--config-dir` | `<path>`  | config dir (default `tr::platform::get_default_config_dir("transmission")`) |
| `-m`  | `--minimized`  | –         | start minimized to tray (only if the tray icon is enabled) |
| `-p`  | `--port`       | `<port>`  | port of an existing session to connect to |
| `-r`  | `--remote`     | `<host>`  | hostname of an existing session to connect to |
| `-u`  | `--username`   | `<user>`  | auth username for the remote session |
| `-w`  | `--password`   | `<pass>`  | auth password for the remote session |
| `-v`  | `--version`    | –         | print version and exit |

Any of `-r/-p/-u/-w` sets `TR_KEY_remote_session_enabled = true`, so `--remote localhost` makes an
otherwise in-process client attach to a daemon. Positional args are torrent files/magnets. Two
separators exist: `--` starts file arguments, `---` starts raw Qt arguments (so `-style`,
`-platform`, etc. reach `QApplication` without colliding with Transmission's own flags). A second
running instance receives the files via COM/D-Bus interop and this instance exits (`tryDelegate`).

Fuller directory tour (dialogs, models, delegates, `.ui` files, interop): read
`references/file-map.md`.

## Preferences: `tr::app::Prefs` + `qt/Prefs.h`

Prefs are split into two structs in `libtransmission-app/prefs.h`:

- **`AppPrefs`** — client-only state, always meaningful whether local or remote (window geometry,
  sort/show mode, filter text, `session_remote_*` connection details, tray/notification toggles).
- **`SessionPrefs`** — a mirror of settings the *session* owns (download dir, speed limits,
  encryption, blocklist, peer/rpc ports, queueing…). These hold the value locally so the UI has
  something to show when attached to a remote daemon.

Each struct declares its fields once as a `std::tuple` of
`tr::serializer::Field<&Struct::member_>{ TR_KEY_xxx }` — the member and the quark key. That one
tuple drives load, save, get, set, and `prefs_is_core(key)` (which is just "is this key in
`SessionPrefs`?"). `Prefs::isCore(key)` in `qt/Prefs.h` forwards to it, and `Session::updatePref`
uses it to decide whether a changed key must be pushed to the session over RPC.

`qt/Prefs.h` is a ~50-line `QObject` subclass of `tr::app::Prefs`. Its only job is to turn the
base class's `on_changed(key)` hook into a Qt signal:

```cpp
signals:
    void changed(tr_quark key);
protected:
    void on_changed(tr_quark const key) override { emit changed(key); }
```

**Adding or changing a session preference is a cross-cutting checklist — use the
`add-session-setting` skill.** The Qt-only slice of it: add the member + a `Field{ TR_KEY_… }`
entry to `AppPrefs` or `SessionPrefs` in `libtransmission-app/prefs.h`, wire a widget in
`PrefsDialog` (next section), and add the `.ui` control. Keep the `Fields` tuple sorted the way
the surrounding entries are (alphabetical by member name — the keys don't always match, so sort
by the `&Struct::member_` side).

### The pref ⇄ UI data flow

Setting any pref runs `serializer::set` then `on_changed(key)` → `Prefs::changed(key)`. Listeners
connect to that one signal (`connect(&prefs_, qOverload<tr_quark>(&Prefs::changed), …)`):
`Session::updatePref` (pushes core keys to the session), `Application::refreshPref`, `MainWindow`,
`DetailsDialog`, `FilterBar`, `TorrentFilter`. This is the spine — a new pref that other widgets
must react to just needs a `case` in the relevant `refreshPref`.

## Binding widgets in `PrefsDialog`

`qt/PrefsDialog.{h,cc}` builds each tab (`initDownloadingTab`, `initSpeedTab`, …) by calling a set
of overloaded `initWidget(WidgetType*, tr_quark)` helpers — one per Qt widget class
(`QCheckBox`, `QLineEdit`, `QSpinBox`, `QDoubleSpinBox`, `QTimeEdit`, `QPlainTextEdit`, plus
project widgets `PathButton`, `FreeSpaceLabel`, and combo helpers). Every overload follows the
same two-way pattern (`initWidget(QCheckBox*)` is the model):

```cpp
auto updater = [this, key, w]() {
    auto const blocker = QSignalBlocker{ w };      // don't re-emit while we set the value
    w->setChecked(prefs_.get<bool>(key));          // pref -> widget
};
updater();                                         // seed the widget now
updaters_.emplace(key, std::move(updater));        // remember for later refreshes
connect(w, &QAbstractButton::toggled,
        [this, key](bool const val) { set(key, val); }); // widget -> pref
```

- `updaters_` is a `std::multimap<tr_quark, std::function<void()>>`. `refreshPref(key)` runs
  `equal_range(key)` at its end, so any number of widgets can track one key.
- `PrefsDialog::set(key, val)` writes the pref *and* calls `refreshPref(key)` locally.
- `refreshPref` also holds the enable/disable logic (e.g. whitelist widgets follow
  `rpc_enabled && rpc_whitelist_enabled`), so add cross-widget rules there, not in the lambdas.
  If you add such a `case`, also add its controlling key to the `InitKeys` array in the
  `PrefsDialog` constructor (and bump the array size): `InitKeys` replays `refreshPref` once after
  all tabs are built, so without it the dependent widgets show the wrong initial enabled state
  until the pref is toggled once.

To add a control: drop the widget into `PrefsDialog.ui`, then one `initWidget(ui_.myWidget,
TR_KEY_…)` call in the right `init*Tab()`. `PrefsDialog.ui` is a large hand-formatted Designer
file whose group boxes are `QGridLayout`s with an explicit `row`/`column`/`colspan` on every
`<item>` — append your control at the next unused row (or edit in Qt Designer); reusing a
row/column silently overlaps the two widgets instead of erroring. If it must be greyed out for
remote sessions, append it
to `unsupported_when_remote_` (disabled in the constructor when `!is_server_`); if it only makes
sense on a local filesystem, it belongs to the download/seeding "locality" widget lists.

## Qt versions & the build

- **Qt 5.15 minimum** (`QT_MINIMUM 5.15` in the root `CMakeLists.txt`); both **Qt 5 and Qt 6**
  build. Configure prefers Qt6 (`set(Qt_NAMES Qt6 Qt5)`). Force one with
  `-DUSE_QT_VERSION=5` or `=6` (default `AUTO`). Bridge version differences with `qt/QtCompat.h`
  (`IF_QT6(then, else)`, `QtrSizeArgType`) rather than ad-hoc `#if QT_VERSION`.
- Required Qt modules: `Core Gui Widgets Network Svg LinguistTools` (+`Test` when `ENABLE_TESTS`).
  Optional: `DBus` (non-Windows) / `AxContainer`+`AxServer` (Windows COM). **The Qt client is
  disabled unless one interop backend is available** — the root CMake turns `QT_FOUND` off if
  neither COM nor D-Bus is present (single-instance delegation depends on it).
- Enable with `-DENABLE_QT=ON` (default `AUTO` = build if Qt is found).
- CMake targets (`qt/CMakeLists.txt`): **`transmission-qt-lib`** (static lib holding every source
  and `.ui`/`.qrc`/`.ts`) and **`transmission-qt`** (the executable — `main.cc` plus
  `application.qrc`, which AUTORCC embeds into the exe too; it links `transmission-qt-lib` for
  everything else). Build only the app with:

```bash
cmake -B build -G Ninja -DENABLE_QT=ON -DRUN_CLANG_TIDY=OFF   # see the building skill for flags
ninja -C build transmission-qt
```

The binary lands at `build/qt/transmission-qt` (verified). AUTOMOC/AUTORCC/AUTOUIC are on, so moc
runs from `Q_OBJECT`, `.ui` → `ui_*.h`, and `.qrc` are handled automatically — no manual codegen.

### Running against a daemon

Start a daemon (`build/daemon/transmission-daemon`; see the **building** skill), then attach:

```bash
build/qt/transmission-qt -r localhost -p 9091            # 9091 = TrDefaultRpcPort
build/qt/transmission-qt -r host -u me -w secret         # with RPC auth
build/qt/transmission-qt -g /tmp/tr-qt-config            # isolated in-process session
```

## Strings, translations, and the `TR_PROJ_*` gotcha

`QT_NO_CAST_FROM_ASCII` is defined **PUBLIC** on the Qt target (commit
`4d0cd3e61 build: define QT_NO_CAST_FROM_ASCII as PUBLIC (#19)`). There is therefore **no
implicit `const char*` → `QString`**. Build QStrings with `QStringLiteral("…")` for literals,
`tr("…")` for user-facing text, or `QString::fromUtf8`/`Utils::qstringFromUtf8` for runtime bytes.

**Maintainer gotcha (verified):** the `TR_PROJ_*` macros in `libtransmission/macros.h`
(`TR_PROJ_APPNAME`, `TR_PROJ_APPNAME_CAPITALIZED`, `TR_PROJ_URL_HELP`, `TR_PROJ_DBUS_SERVICE`,
`TR_PROJ_WEB_SERVER_BASE_PATH`, …) expand to bare **`const char*` string literals**. Do **not**
pass one through `QString::arg()` (its overloads take `QString`, and with
`QT_NO_CAST_FROM_ASCII` the `const char*` won't convert). Instead **concatenate the macro into the
`QStringLiteral` format string** via adjacent-string-literal concatenation, and reserve `%1`/`.arg`
for the runtime values. Real examples:

```cpp
// qt/AboutDialog.cc:28  — macro is part of the literal; %1 is the version arg
QStringLiteral("<b style='font-size:x-large'>" TR_PROJ_APPNAME_CAPITALIZED " %1</b>")
    .arg(QStringLiteral(LONG_VERSION_STRING));

// qt/MainWindow.cc:618  — macro concatenated; only MAJOR/MINOR go through .arg
QStringLiteral(TR_PROJ_URL_HELP "/gtk/%1.%2x").arg(MAJOR_VERSION).arg(MINOR_VERSION / 10);
```

### Translation pipeline (Qt is one of three; see the translations skill)

- Source strings come from `tr(...)` / `QObject::tr(...)`, and also from `.ui` widget text set
  via a Designer `<string>` property — uic wires that through `QCoreApplication::translate` in the
  generated `retranslateUi()`, so a new label/checkbox is translatable by default. Leave
  `notr="true"` off; it's reserved for literal/non-user-facing text (the `hh:mm` and `...`
  placeholders in `PrefsDialog.ui`). Per-language catalogs live in
  `qt/translations/transmission_<lang>.ts` (Qt Linguist XML).
- The enabled languages are the **`LINGUAS` list in `qt/CMakeLists.txt`** (gated by `ENABLE_NLS`),
  *not* the file count — a few `.ts` files on disk (e.g. `ar`, `hi`, `hr`) are not in `LINGUAS`
  and won't be built. Update `LINGUAS` to enable one.
- The build only **compiles** `.ts` → `.qm` via `tr_qt_add_translation` →
  `qt6_add_translation`/`qt5_add_translation` (which runs `lrelease`). **`lupdate` is not wired
  into CMake** (verified — no `lupdate` reference anywhere in the build) and there is no CMake
  target that re-extracts strings. Translations are managed on **Transifex**
  (`docs/Translating.md`); don't hand-edit translated `.ts` entries — add source strings via
  `tr()` and let the Transifex tooling populate them.
- At runtime `Application::loadTranslations()` installs `QTranslator`s from the installed
  `TRANSLATIONS_DIR` (a compile define) or `<appdir>/translations`, plus Qt's own `qtbase_*`
  catalog.

## `qt/.clang-tidy` notes

Mostly the repo-wide policy (see **building** / **cpp-conventions**), with Qt-specific points:
- `HeaderFilterRegex: .*/qt/[^/]*$` — tidy checks only headers **directly** under `qt/`, so
  generated `ui_*.h` / `moc_*` (which land in the build tree) are skipped.
- `readability-redundant-access-specifiers` is disabled **on purpose** so `private:` followed by
  `private slots:` doesn't warn — keep Qt's `signals:` / `slots:` sections as-is.
- Naming enforced here: `PrivateMemberSuffix`/`ProtectedMemberSuffix` `_`, classes/structs
  `CamelCase`, variables `lower_case`, constants/constexpr `CamelCase`. Match `session_`, `ui_`,
  `updaters_` when you add members.
