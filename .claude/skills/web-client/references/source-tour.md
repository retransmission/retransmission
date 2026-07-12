# Web UI source tour (`web/src/`)

Annotated map of the app. Symbols below are the real exported classes/consts (`grep -nE '^export'
web/src/*.js`). It's plain ES modules — no framework, no build-time templating; the DOM is built
imperatively in JS or lives in `web/public_html/index.html`. Almost every class `extends
EventTarget` and communicates by dispatching events the controller listens for.

## Boot & architecture

- `main.js` — entry point (esbuild's single entryPoint). Waits for `DOMContentLoaded`, constructs
  `ActionManager`, `Prefs`, `Notifications`, then the `Transmission` controller. Also
  `import '../assets/css/transmission-app.scss'` — that import is how the stylesheet enters the
  bundle (esbuild-sass-plugin compiles it to `public_html/transmission-app.css`).
- `transmission.js` — `class Transmission extends EventTarget`, the **app controller**. Owns the
  torrent list, polling loop, selection, filtering, the `Remote`, and every dialog. Uses
  `Clusterize` (from the `clusterize.js` dependency) to virtualize the torrent list. Start here to
  trace any user action.
- `remote.js` — `class Remote` + the `RPC` constant map. The **RPC client**: `fetch('../rpc', …)`
  (resolves to `/transmission/rpc`), sets `content-type: application/json`, and handles the
  `X-Transmission-Session-Id` CSRF handshake (a 409 replies with a new id to retry). `RPC` holds
  the JSON-RPC method/argument key strings. Add a new server call here.
- `action-manager.js` — `class ActionManager extends EventTarget`. Central command registry:
  buttons/menus declare a `data-action` (see `index.html`) and enabled-state, and fire through
  here. Keeps toolbar/menu/keyboard bindings in sync.

## State & formatting

- `prefs.js` — `class Prefs extends EventTarget`. **Client-side display** preferences (sort order,
  compact vs full rows, theme, etc.), persisted in `document.cookie` (SameSite=Strict). These are
  browser-local; they are *not* the daemon's session settings.
- `prefs-dialog.js` — `class PrefsDialog`. The Preferences window. This one **does** read/write the
  daemon's **session settings** over RPC (`remote.savePrefs({…})`, `remote.checkPort(…)`). When you
  add a session preference that needs a web control, this is the file to edit — see the
  **add-session-setting** skill for the full cross-client checklist.
- `formatter.js` — `Formatter` const. Human-readable sizes/speeds/ratios/dates/plurals via
  `Intl.NumberFormat`/`Intl.PluralRules`, keyed to the browser locale
  (`Intl.PluralRules().resolvedOptions().locale`). This is the extent of web i18n — UI *strings*
  are hardcoded English; there is no gettext catalog (contrast native clients → **translations**).
- `utils.js` — `Utils` const plus DOM helpers: `createDialogContainer`, `createTextualTabsContainer`,
  `createSection`/`createInfoSection`, `newOpts`, `icon` (inline SVG set), `debounce`, `deepEqual`
  (also the `fast-deep-equal` dependency is used elsewhere), `setEnabled`, `setTextContent`,
  `OutsideClickListener`, `makeUUID`.

## Torrent list & inspector

- `torrent.js` — `class Torrent extends EventTarget`. Client-side model of one torrent; absorbs RPC
  field updates and emits change events.
- `torrent-row.js` — `TorrentRendererFull` and `TorrentRendererCompact`, the two row renderers
  (the "compact vs full" display density). Row virtualization exemplar:
  `30bc84ebc feat(web): torrent list virtualization (#7674)`.
- `inspector.js` — `class Inspector` (the details pane: info / peers / trackers / files / webseeds
  tabs). Largest file. Webseeds-tab exemplar: `eac1f24f0 feat(web): add webseeds list (#8421)`.
- `file-row.js` — `class FileRow`, a single row in the inspector's Files tab (priority + wanted).

## Menus & dialogs (one class per file)

- `context-menu.js` (`ContextMenu`), `overflow-menu.js` (`OverflowMenu`) — right-click and the
  hamburger/overflow menu.
- Dialogs, each `extends EventTarget`: `about-dialog.js`, `alert-dialog.js`, `labels-dialog.js`,
  `move-dialog.js`, `open-dialog.js` (add-torrent), `remove-dialog.js`, `rename-dialog.js`,
  `shortcuts-dialog.js`, `statistics-dialog.js`. New-dialog exemplar:
  `cc66625fd feat: new window "Appearance settings" for web app (#7318)` (paired with
  `appearance-settings.js` → `class Appearance`).
- `notifications.js` (`Notifications`) — desktop/toast notifications, constructed from `Prefs`.

## Markup, styles, images

- `web/public_html/index.html` — hand-written HTML shell (**source**, not generated). Declares the
  toolbar/statusbar/workarea skeleton, wires buttons via `data-action`, and `<script src>`/`<link>`
  the bundle. Edit it directly and keep it prettier-clean (`.prettierignore` re-includes it).
- `web/assets/css/transmission-app.scss` — the entire stylesheet: `@mixin` viewport breakpoints and
  a big `:root { --… }` block of CSS custom properties (colors, sizes, z-index) for theming. Linted
  by stylelint (sass-guidelines).
- `web/assets/img/*.svg` — UI icons, bundled as `dataurl` (esbuild `loader`), referenced from JS/CSS.
  Distinct from `web/public_html/images/*` (favicons, apple-touch-icon), which are static committed
  files linked directly by `index.html`.

## Conventions worth copying

- Follow the existing class-per-file, `extends EventTarget`, snake_case-in-JSON style already in the
  tree; match the copyright/license header block at the top of neighboring files.
- Commit subjects use Conventional Commits with a web scope, e.g. `feat(web): …`, `fix(web): …`;
  PRs get the `scope:web` label (see the repo-root `CONTRIBUTING.md`).
- New server data flows source→UI as: add the key to `RPC` in `remote.js` → request/consume it in
  `transmission.js`/`inspector.js`/model → render. If the RPC method or field is itself new, that's
  a C++ change too — see the **rpc-api** skill and `docs/rpc-spec.md`.
