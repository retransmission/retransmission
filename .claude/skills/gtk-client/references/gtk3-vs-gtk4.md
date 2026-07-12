# GTK 3 vs GTK 4 in the gtkmm client — reference

Read this when a change touches toolkit-version-specific behavior, when you add a new `.ui` file or a
new `gtk/` source file, or when a compile error appears under one gtkmm version but not the other.
All facts below are from `gtk/CMakeLists.txt`, `gtk/GtkCompat.h`, `gtk/Utils.h`, and top-level
`CMakeLists.txt`.

## How the version is selected

Top-level `CMakeLists.txt`:
- `set(GTKMM3_MINIMUM 3.24.0)`, `set(GTKMM4_MINIMUM 4.11.1)`.
- `tr_list_option(USE_GTK_VERSION "Use specific GTK version" AUTO 3 4)` — cache option.
- In the `if(ENABLE_GTK)` block: `AUTO` (or `4`) tries `pkg_check_modules(GTK4 gtkmm-4.0>=… glibmm-2.68>=…
  giomm-2.68>=…)` first; if not found (and `AUTO` or `3`) it tries `gtkmm-3.0>=… glibmm-2.4 giomm-2.4`.
  Sets `GTK_VERSION` to 4 or 3 and builds the `transmission::gtk_impl` INTERFACE target from the
  matching `GTK${GTK_VERSION}_*` pkg-config vars.
- `gtk/CMakeLists.txt` picks `UI_SUBDIR` = `ui/gtk4` when `GTK_VERSION EQUAL 4`, else `ui/gtk3`, and
  passes `${UI_SUBDIR}/transmission-ui.gresource.xml` to `tr_target_glib_resources`.

So a single configure builds exactly one toolkit. To exercise both, configure two build dirs with
`-DUSE_GTK_VERSION=3` and `=4`.

## The compat layer (`gtk/GtkCompat.h`)

Version predicates (defined if the toolkit headers don't already provide them):
`GTKMM_CHECK_VERSION`, `GLIBMM_CHECK_VERSION`, `CAIROMM_CHECK_VERSION`, `PANGOMM_CHECK_VERSION`.

Ternary selectors that expand to the first arg on the newer API, the second on the older:
- `IF_GTKMM4(then, else)` — gtkmm 4 vs 3.
- `IF_GLIBMM2_68(then, else)` — glibmm 2.68 (paired with gtkmm4) vs 2.4.
- `IF_CAIROMM1_16(then, else)`, `IF_PANGOMM2_48(then, else)`.

Enum-name bridges (GTK 4 scoped enums vs GTK 3 `SHOUTY_NAMES`). Pass the leaf name only:
- Gtk: `TR_GTK_ALIGN`, `TR_GTK_BUTTONS_TYPE`, `TR_GTK_CELL_RENDERER_STATE`,
  `TR_GTK_FILE_CHOOSER_ACTION`, `TR_GTK_MESSAGE_TYPE`, `TR_GTK_ORIENTATION`, `TR_GTK_POLICY_TYPE`,
  `TR_GTK_POSITION_TYPE`, `TR_GTK_RESPONSE_TYPE`, `TR_GTK_SELECTION_MODE`, `TR_GTK_SORT_TYPE`,
  `TR_GTK_STATE_FLAGS`, `TR_GTK_TREE_MODEL_FLAGS`, `TR_GTK_TREE_VIEW_COLUMN_SIZING`.
- Two helpers that bridge API shape, not just names: `TR_GTK_TREE_MODEL_CHILD_ITER(obj)`,
  `TR_GTK_WIDGET_GET_ROOT(obj)` (gtk4 `get_root()` vs gtk3 `get_toplevel()`).
- Gdk: `TR_GDK_COLORSPACE`, `TR_GDK_EVENT_TYPE`, `TR_GDK_DRAG_ACTION`, `TR_GDK_MODIFIED_TYPE`.
- Glib/Gio: `TR_GLIB_FILE_TEST`, `TR_GLIB_NODE_TREE_TRAVERSE_FLAGS`, `TR_GLIB_PARAM_FLAGS`,
  `TR_GLIB_SPAWN_FLAGS`, `TR_GLIB_USER_DIRECTORY`, `TR_GLIB_EXCEPTION_WHAT` (gtk3 `.what()` returns
  `Glib::ustring`, needs `.c_str()`), `TR_GIO_APP_INFO_CREATE_FLAGS`, `TR_GIO_APPLICATION_FLAGS`,
  `TR_GIO_DBUS_BUS_TYPE`, `TR_GIO_DBUS_PROXY_FLAGS`, `TR_GIO_FILE_MONITOR_EVENT`.
- Cairo/Pango: `TR_CAIRO_SURFACE_FORMAT`, `TR_CAIRO_CONTEXT_OPERATOR`, `TR_PANGO_ALIGNMENT`,
  `TR_PANGO_ELLIPSIZE_MODE`, `TR_PANGO_WEIGHT`.

The header also backfills a few `Glib::RefPtr` operators for the pre-2.68 (glibmm 2.4 / gtk3) case.

Rule of thumb: if you need an enum value or a renamed method that differs between toolkits, look for
an existing `TR_*`/`IF_*` macro first; only add a new one (or a narrow `#if GTKMM_CHECK_VERSION(...)`)
if none fits. The model to imitate is any existing `.cc` that already uses these.

## Where the two toolkits genuinely diverge

- **Torrent list rendering.** `TorrentCellRenderer.cc` is guarded
  `tr_allow_compile_if([=[[GTK_VERSION EQUAL 3]]=] TorrentCellRenderer.cc)` — compiled for GTK 3 only.
  GTK 4 renders list rows from `ui/gtk4/TorrentListItemCompact.ui` and `ui/gtk4/TorrentListItemFull.ui`
  (there are no gtk3 counterparts). Touching how a torrent row looks means editing the cell renderer
  for gtk3 *and* the two list-item `.ui` files for gtk4.
- **Widget lookup API.** `gtk/Utils.h` hides the split: `gtr_get_widget<T>` uses
  `builder->get_widget<T>(name)` on gtkmm4 vs the out-param `get_widget(name, widget)` on gtkmm3;
  `gtr_get_widget_derived<T>` likewise. Always go through these wrappers.
- **Option parsing.** `main.cc` only calls `Gtk::Main::add_gtk_option_group` under
  `#if !GTKMM_CHECK_VERSION(4, 0, 0)`.

## Checklist: add a new `.ui`-backed dialog/widget

1. Create the layout twice: `gtk/ui/gtk3/Foo.ui` and `gtk/ui/gtk4/Foo.ui`. Give the top object a
   stable id and mark user-facing strings `translatable="yes"`.
2. Register the file in **both** GResource manifests:
   `gtk/ui/gtk3/transmission-ui.gresource.xml` and `gtk/ui/gtk4/transmission-ui.gresource.xml`
   (`<file compressed="true" preprocess="xml-stripblanks">Foo.ui</file>`).
3. Add the two `.ui` paths to the two `target_sources(${TR_NAME}-gtk ... ui/gtk3/... / ui/gtk4/...)`
   blocks in `gtk/CMakeLists.txt`.
4. Write `Foo.{cc,h}`. The class derives from the appropriate gtkmm base; its constructor takes
   `(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder, …)` and forwards
   `cast_item` to the base (pattern: `FilterBar`, `PrefsDialog`'s `*Page` classes, `MainWindow`).
   Provide a `static create(...)` that does
   `Gtk::Builder::create_from_resource(gtr_get_full_resource_path("Foo.ui"))` then
   `gtr_get_widget_derived<Foo>(builder, "Foo", …)`.
5. Add `Foo.cc` / `Foo.h` to the main `target_sources` list in `gtk/CMakeLists.txt`.
6. Copy the license header from a sibling file (the tree mixes MIT and GPL headers).
7. Build both `-DUSE_GTK_VERSION=3` and `=4`.

## Checklist: add a plain source file (no `.ui`)

Just add `Foo.cc` / `Foo.h` to the first `target_sources(${TR_NAME}-gtk PRIVATE …)` block in
`gtk/CMakeLists.txt`, alphabetically with the rest. No POTFILES edit is needed even if it contains
`_()` strings (extraction is done on Transifex — see the SKILL body).
