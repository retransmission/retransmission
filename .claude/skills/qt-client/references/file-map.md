# `qt/` annotated file map

Read this when you need to find *where* something lives in the Qt client. All paths are under
`qt/`. Source of truth is `qt/CMakeLists.txt` (the `transmission-qt-lib` source list) and the
files themselves. Every widget class named here is compiled into `transmission-qt-lib`; the
`transmission-qt` executable target holds only `main.cc` and `application.qrc`, linking the lib
for the rest.

## Entry point & app object

- `main.cc` — `tr_main`: parses the command line with `tr_getopt`, builds `Prefs`, `RpcClient`,
  and `Application`, runs `QApplication::exec()`, then `prefs.save(...)`. Defines the `-r/-p/-u/-w`
  etc. option table.
- `Application.{h,cc}` — the `QApplication` subclass. Owns `Session`, `TorrentModel`,
  `MainWindow`, `WatchDir`; holds a reference to the `Prefs` built in `main.cc` (which is why
  `main.cc`, not `Application`, calls `prefs.save` on exit). Wires the global signal graph
  (`connect(&prefs_, &Prefs::changed, …)`, session refresh timers), single-instance interop,
  `loadTranslations()`, blocklist auto-update.
- `TrQtInit.{h,cc}` — tiny `trqt::trqt_init()` → `tr::app::init()` bootstrap.

## Top-level windows / dialogs (each usually `.h` + `.cc` + `.ui`)

- `MainWindow` — the main window: toolbar, torrent list view, filter bar, status bar, tray icon,
  menus. `MainWindow.ui` is the layout.
- `DetailsDialog` — per-torrent tabs (info, peers, trackers, files, options). Largest dialog;
  drives `TrackerModel`/`FileTreeModel`.
- `PrefsDialog` — the preferences dialog. Binds widgets to prefs via `initWidget` overloads and
  the `updaters_` multimap (see SKILL.md).
- `AboutDialog`, `LicenseDialog`, `StatsDialog`, `SessionDialog` (choose local/remote session),
  `RelocateDialog`, `MakeDialog` (+`MakeProgressDialog.ui`), `OptionsDialog` (add-torrent
  options), `TrackersDialog.ui`.
- `BaseDialog.h` — shared dialog base class.

## Models / views / delegates (Qt Model-View)

- `TorrentModel` — `QAbstractListModel` of torrents, fed by `Session::torrentsUpdated`.
- `TorrentFilter` — `QSortFilterProxyModel` over `TorrentModel`; reacts to `Prefs::changed`.
- `TorrentView` — the list view widget.
- `TorrentDelegate` / `TorrentDelegateMin` — paint a torrent row (full vs compact view).
- `Torrent.{h,cc}` — the per-torrent data object (fields mirrored from RPC).
- `TrackerModel` / `TrackerModelFilter` / `TrackerDelegate` — tracker list in DetailsDialog.
- `FileTreeModel` / `FileTreeItem` / `FileTreeView` / `FileTreeDelegate` — the files tab tree.
- `FilterBar` / `FilterBarComboBox` / `FilterBarComboBoxDelegate` / `Filters` / `TorrentFilter` —
  the status/tracker filter bar under the toolbar.

## Session / RPC / prefs glue

- `Session.{h,cc}` — connection owner and high-level operations (see SKILL.md).
- `RpcClient.{h,cc}` — Qt wrapper over `tr::app::RpcClient` (injects the Qt UI-thread hook).
- `Prefs.h` — `QObject` wrapper over `tr::app::Prefs`, emits `changed(tr_quark)`.
- `VariantHelpers.{h,cc}` — convert between `tr_variant` and Qt types (`dictFind<QString>`, etc.).
- `Speed.h`, `Typedefs.h` (`torrent_ids_t`), `UserMetaType.h`, `Formatter.{h,cc}` — value types
  and Qt metatype registration / human-readable formatting.

## Small widgets & helpers

- `SqueezeLabel` / `AccessibleSqueezeLabel` (elided labels), `FreeSpaceLabel`, `PathButton`
  (directory picker used in PrefsDialog), `IconToolButton`, `ColumnResizer`, `StyleHelper`.
- `IconCache`, `NativeIcon` (+`NativeIconMac.mm` on Apple), `FaviconCache`, `Utils.{h,cc}`
  (`Utils::qstringFromUtf8`, etc.), `WatchDir` (auto-add watch folder), `AddData` (parse a
  file/magnet/URL/base64 into an add request).

## Single-instance interop (COM on Windows, D-Bus elsewhere)

- `InteropHelper` — front door used by `main.cc` `tryDelegate` / `initialize`.
- `DBusInteropHelper` (built when `ENABLE_QT_DBUS_INTEROP`) and `ComInteropHelper` (built when
  `ENABLE_QT_COM_INTEROP`) — see the `tr_allow_compile_if` block in `qt/CMakeLists.txt`.
- `InteropObject` — the object exported over the bus (`Q_CLASSINFO` D-Bus interface).
- `transmission-qt.idl` / `.tlb.rc` — COM type library inputs (Windows only).

## Resources, packaging, translations

- `application.qrc` — Qt resource file; aliases SVG icons from `../icons/` under `:/icons/…`.
- `translations/transmission_<lang>.ts` — Qt Linguist catalogs. Enabled set = `LINGUAS` in
  `qt/CMakeLists.txt`. (`qt/transmission_en.qm` is a stray committed `.qm` at the `qt/` root,
  unreferenced by the build — not in `translations/` and not embedded or installed.)
- `transmission-qt.desktop`, `transmission-qt.1` (man page), `qtr.rc` — packaging/metadata.
- `QtCompat.h` — Qt5/Qt6 shim macros (`IF_QT6`, `QtrSizeArgType`).
