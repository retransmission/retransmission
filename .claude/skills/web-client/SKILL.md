---
name: web-client
description: >
  How to work in the built-in web UI under web/ — the JS/SCSS app that transmission-daemon (and
  any flavor with remote access on) serves at http://localhost:9091/. Load this whenever you edit
  anything in web/ (web/src/*.js, web/assets/css/transmission-app.scss, web/assets/img/*.svg,
  web/public_html/index.html), rebuild the bundle with esbuild, run `npm run dev`/`build`/`lint`,
  or hit questions like "why don't my web changes show up?", "do I commit transmission-app.js?",
  "how do I point a running daemon at my dev assets?", "what does TRANSMISSION_WEB_HOME do?",
  "REBUILD_WEB / INSTALL_WEB", or "the CI 'webapp' job failed on generated files / code style".
  Also load it before touching the web half of a preference (web/src/prefs-dialog.js).
---

# Web client (`web/`)

A single web interface is compiled into **all** Transmission flavors. It is a **vanilla
JavaScript** app (no React/Vue/framework, no TypeScript) authored in `web/src/`, bundled by
**esbuild** into one JS file + one CSS file, and **served by libtransmission's RPC server** over
HTTP. There is no standalone dev server — the browser always talks to a real Transmission process.

Ground truth is `web/` plus the C++ that serves and locates the assets
(`libtransmission/rpc-server.cc`, `libtransmission/platform.cc`). If this skill disagrees with the
repo, trust the repo and fix this skill. `web/README.md` is the upstream dev doc but is partly
stale (see "Stale docs" below). `docs/Web-Interface.md` is end-user usage, not dev material.

Sibling skills: repo layout → **codebase-map**; CMake/Ninja mechanics and where the built binaries
land → **building**; the JSON-RPC wire format, `docs/rpc-spec.md`, and hand-testing RPC →
**rpc-api**; adding a session pref end-to-end (its web wiring lives here) → **add-session-setting**.
For branch/commit/PR and Notes-tag conventions see the repo-root `CONTRIBUTING.md` and `AGENTS.md`.

## The one rule that trips everyone up: never commit the rebuilt bundle in a PR

The generated bundle **is committed** to the repo (so packagers can build without Node), yet a
**pull request must not modify it**. CI enforces both halves:

- `web/public_html/transmission-app.js`, `.js.LEGAL.txt`, `.css`, `.css.map`, `.css.LEGAL.txt`
  and `web/package.json.buildonly` are tracked, but the `webapp` workflow's
  **`test-generated-files`** job fails any PR that changes them ("for security reasons").
- After your source-only PR merges to `main`, the **`update-generated-files`** job re-runs the
  build and opens a follow-up `chore: update generated transmission-web files` PR. That bot owns
  the bundle; you never do.

So your change set is **source only**: `web/src/`, `web/assets/`, and `web/public_html/index.html` —
never the generated `web/public_html/transmission-app.*` bundle. Real example — `eac1f24f0
feat(web): add webseeds list (#8421)`: its web-side edits were exactly `web/src/inspector.js`,
`web/src/torrent.js`, and `web/assets/css/transmission-app.scss`, and it left the committed bundle
under `web/public_html/` untouched. (It also changed `libtransmission` RPC code and
`docs/rpc-spec.md`, since webseeds was a new RPC field — see the **rpc-api** skill.) The matching
bundle arrived later via a bot commit (e.g. `fdfd37804 chore: update generated transmission-web
files (#8779)`, which touched only `web/public_html/*`).

Consequence for the dev loop below: `npm run dev`/`build` **overwrite** the committed
`web/public_html/transmission-app.*`. Before you commit, throw those edits away:

```sh
git restore 'web/public_html/transmission-app.*'   # keep index.html / images if you meant to change them
```

`transmission-app.js.map` is gitignored (`.gitignore` line 25), so it never shows up in `git
status`; the CSS map *is* tracked. Don't try to "fix" that asymmetry.

## Dev loop: edit → rebuild → view in a running daemon

```sh
cd web
npm ci                 # or `npm install`; CI uses `npm ci` (clean install from package-lock.json)
npm run dev            # DEV=true node esbuild.mjs — watches src/scss, rebuilds public_html/*.{js,css} on save
```

`npm run dev` **only watches and rebuilds** — it does **not** start an HTTP server (nothing listens
on the `:9000` the README mentions; see "Stale docs"). It also **blocks**, so leave it running and
open a **second terminal at the repo root** to point a running Transmission at the assets you just
built and open its web port:

```sh
# second terminal, at the repo ROOT (not web/ — $PWD must be the repo root for the path below).
# The from-source binary isn't on $PATH; it lands under build/ (see the building skill):
TRANSMISSION_WEB_HOME="$PWD/web/public_html" ./build/daemon/transmission-daemon
# then browse to http://localhost:9091/  (redirects to /transmission/web/)
```

On a config dir that **already has RPC enabled**, `transmission-daemon` serves the web UI on
localhost with no extra flags — the default whitelist allows `127.0.0.1`/`::1`, so **same-machine**
dev needs no remote-access or whitelist flags (opening it up to *other* machines is a separate
step). The GTK/Qt/macOS flavors ship with RPC off, so enable it there first.

**But a fresh `--config-dir` can come up with RPC _disabled_** — then the browser (and
`transmission-remote`) get connection-refused at `:9091`. Timeless rule: **connection refused ⇒
check `"rpc-enabled"` in `<config-dir>/settings.json`** (edit it while the daemon is stopped — it
rewrites the file on exit). On `main` as of 2026-07 this is guaranteed for a brand-new dir: a
`daemon.cc` settings-merge bug drops the daemon's intended `rpc-enabled=true` default. The
**debugging** skill's §1 has the full story.

`TRANSMISSION_WEB_HOME` is honored by libtransmission itself, so **any** flavor (daemon, GTK, Qt,
macOS) that serves the UI will use it — it short-circuits the normal install-path search. Edit a
source file, let the watcher rebuild, then **hard-refresh** the browser (Ctrl/Cmd+Shift+R, or keep
devtools open with "Disable cache" on): the RPC server sends assets with a 24h `Expires` and no
revalidation headers (`rpc-server.cc` `serve_file`), and the bundle filenames are unversioned, so a
plain reload can keep serving the pre-edit bundle from disk cache. The page mounts into
`web/public_html/index.html`, which loads `transmission-app.js`/`.css`; the app POSTs JSON to
`../rpc` (i.e. `http://localhost:9091/transmission/rpc`) via `fetch`.

`npm run build` is the same thing without the watcher (one-shot). See **references/build-serve-ci.md**
for how the daemon finds assets when `TRANSMISSION_WEB_HOME` is unset, and for the CMake/CI details.

## Lint & formatting (CI gate)

Three tools, all run from `web/`:

```sh
npm run lint        # run-p oxlint + stylelint + prettier --check
npm run lint:fix    # oxlint --fix, stylelint --fix, prettier -w  (mutates files)
```

- **oxlint** (`.oxlintrc.json`): `correctness` + `suspicious` categories as errors, es2024,
  `src/*.js` is browser env, `*.mjs`/config files are node env, `public_html/` ignored.
- **stylelint** (`stylelint.config.js`, sass-guidelines): lints `assets/css/*scss` only.
- **prettier** (`prettier.config.js`: `singleQuote: true`): checks the whole tree except
  `public_html/` — but **not** `public_html/index.html`, which stays formatted (`.prettierignore`
  re-includes it). So keep `index.html` prettier-clean.

The `webapp` CI **code-style** job runs `npm run lint:fix` then fails if it produced **any** diff.
Translation: run `npm run lint:fix` and commit the result before you push, or CI fails and uploads
a `code-style.diff` artifact showing what it wanted to change.

## CMake: REBUILD_WEB and INSTALL_WEB

Most builds **do not** rebuild the web UI — they ship the committed bundle as-is.

- `REBUILD_WEB` (`tr_auto_option`, default **OFF**): when ON, `add_subdirectory(web)` runs
  `npm ci && npm run build` at configure/build time and points the install at the freshly built
  assets. It needs Node/npm (min npm **10.8.2**) and network access, so leave it OFF for normal
  work. CI only flips it ON for jobs where `web/` actually changed.
- `INSTALL_WEB` (`option`, default **ON**): whether `make install` copies the assets to
  `<datadir>/transmission/public_html/` (only when a daemon/GTK/Qt target is enabled).

You almost never need either during web development — the `TRANSMISSION_WEB_HOME` dev loop above
sidesteps the whole install path. Details and the exact search order live in
**references/build-serve-ci.md**.

## Source layout

Entry point is `web/src/main.js`, which constructs `ActionManager`, `Prefs`, `Notifications`, and
the `Transmission` app controller. It's plain ES modules, one class per file; `Transmission extends
EventTarget` is the controller, `Remote` (`remote.js`) is the RPC client, dialogs are one file
each. Styling is a single `assets/css/transmission-app.scss` (`:root` CSS custom properties for
theming), imported from `main.js`. UI strings are hardcoded English — only number/speed/plural
formatting is locale-aware (via `Intl` in `formatter.js`); there is **no** gettext catalog for the
web UI (contrast the native clients — see the **translations** skill).

For an annotated file-by-file inventory and the architecture (renderers, virtualized list,
inspector, menus, cookie-backed display prefs) read **references/source-tour.md**.

## Stale docs to distrust

- `web/README.md` "Notes for Developers" says to open **localhost:9000**. The current
  `esbuild.mjs` only calls `ctx.watch()` — nothing serves on `:9000`. Use the
  `TRANSMISSION_WEB_HOME` + `:9091` loop above instead.
- `web/.nvmrc` pins **v16.14.0** but is stale (last touched 2022, PR #2586). The toolchain now
  requires npm ≥ 10.8.2 (Node ≥ 20.19 per the `NPM_MINIMUM` comment) and CI installs Node
  `lts/*`. Use a current Node LTS (20+); Node 16 will not satisfy the build.
- `web/README.md` "Building Without Node" (rsass/perl/esbuild) is a Debian-packager fallback, not
  the developer path.
