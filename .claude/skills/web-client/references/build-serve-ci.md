# Web assets: how they're built, found, installed, and gated by CI

Read this when the daemon can't find your UI, when a `webapp` CI job fails, or when you need the
exact asset search order / CMake behavior. Verify against the cited files if anything looks off.

## How a running Transmission locates the web assets

`tr_getWebClientDir()` in `libtransmission/platform.cc` decides the directory, in this order.
`isWebClientDir(dir)` accepts a candidate only if `dir/index.html` exists.

1. `CLUTCH_HOME` env var (legacy name — the web UI was once "Clutch"; undocumented, still checked
   **first**).
2. `TRANSMISSION_WEB_HOME` env var (the documented override — see `docs/Environment-Variables.md`).
   This is what the dev loop uses.
3. Platform default search:
   - **macOS** (`BUILD_MAC_CLIENT`): `<configDir>/public_html`, then the app bundle's
     `Contents/Resources/public_html`.
   - **Windows**: `<LocalAppData|RoamingAppData|ProgramData>/Transmission/public_html`, then
     `<dir of the running .exe>/public_html`.
   - **Everyone else (XDG)**: `<XDG_DATA_HOME>/transmission/public_html` (default
     `~/.local/share/transmission/public_html`), then each of `PACKAGE_DATA_DIR`, `XDG_DATA_DIRS`,
     `/usr/local/share`, `/usr/share`, suffixed with `/transmission/public_html`.

If none match, `web_client_dir_` is empty and the RPC server returns an explanatory page telling
you to set `TRANSMISSION_WEB_HOME` (`rpc-server.cc` `handle_web_client`). That empty-dir case is
the usual cause of a blank web UI in a from-source build that wasn't installed.

## How requests map to files

Default RPC port is **9091** (`TrDefaultRpcPort`, `libtransmission/constants.h`). The server's base
path is `/transmission/` (`TR_PROJ_WEB_SERVER_BASE_PATH`), the web sub-path is `web/`, RPC is `rpc`
(`TrHttpServerWebRelativePath` / `TrHttpServerRpcRelativePath`). So:

- `http://host:9091/` → 301 redirect to `/transmission/web/` (`rpc-server.cc handle_request`).
- `/transmission/web/…` → `handle_web_client` strips the `/transmission/web/` prefix and serves the
  remainder from `web_client_dir_` (empty path → `index.html`). `..` is rejected.
- `/transmission/rpc` → JSON-RPC endpoint the app POSTs to (`remote.js` uses `fetch('../rpc', …)`).

All of that is configurable via the `rpc-url` / `rpc-port` session settings; the above are defaults.

## CMake options (root `CMakeLists.txt`)

- `set(TR_WEB_ASSETS ${PROJECT_SOURCE_DIR}/web/public_html)` — by default the **committed** bundle
  is what gets installed. No Node required for a normal build.
- `REBUILD_WEB` (`tr_auto_option`, default **OFF**): when ON, `add_subdirectory(web)` runs. That
  target (`web/CMakeLists.txt`) copies sources into the build tree, runs `npm ci --no-audit
  --no-fund --no-progress` then `npm run build`, and re-exports `TR_WEB_ASSETS` to the build-dir
  `public_html`. Guarded by an npm version probe (`NPM_MINIMUM 10.8.2`, comment: Node 20.19 for
  stylelint@17). Requires network access for `npm ci`.
- `INSTALL_WEB` (`option`, default **ON**): `tr_install_web()` installs `TR_WEB_ASSETS` to
  `<CMAKE_INSTALL_DATAROOTDIR>/transmission` (i.e. `share/transmission/public_html`) — only invoked
  when `ENABLE_DAEMON OR ENABLE_GTK OR ENABLE_QT`.

For day-to-day web work you want neither: build normally (REBUILD_WEB OFF), run `npm run dev` by
hand, and point the daemon at `web/public_html` with `TRANSMISSION_WEB_HOME`. Turn `REBUILD_WEB` ON
only to reproduce a CI build or verify the CMake path itself. See the **building** skill for
configure/build mechanics.

## CI: the `webapp` workflow (`.github/workflows/webapp.yml`)

Triggers on pushes/PRs that touch `CMakeLists.txt` or `web/` (ignores `docs/**`, `.github/**`).
Three jobs, gated by whether `web/` actually changed:

1. **code-style** (PRs and main): `npm --prefix web ci` → `npm --prefix web run lint:fix` → fail if
   `git diff web` is non-empty. Uploads the wanted changes as a `code-style.diff` artifact. Fix:
   run `npm run lint:fix` locally and commit.
2. **test-generated-files** (PRs only): fails if the PR changed any of the generated files —
   `web/package.json.buildonly`, `web/public_html/transmission-app.{css,css.LEGAL.txt,css.map,js,js.LEGAL.txt,js.map}`.
   This is the "never commit the rebuilt bundle" gate. Fix: `git restore 'web/public_html/transmission-app.*'`
   (and `web/package.json.buildonly`) so your PR is source-only.
3. **update-generated-files** (push to `main` only): re-runs `npm run build` +
   `npm run generate-buildonly`, `git add --update web`, and opens a `chore/update-webapp-files` PR
   labelled `notes:none scope:web type:chore`. This bot regenerates the bundle after your source
   PR lands — you don't.

Separately, the main build workflow (`.github/workflows/actions.yml`) passes
`-DREBUILD_WEB=ON` only for platforms in a run where `web/` changed (else `OFF`), and installs Node
`lts/*` for those jobs — a second reason the committed bundle must stay authoritative.

## `package.json.buildonly` and `generate-buildonly.js`

`generate-buildonly.js` derives `web/package.json.buildonly` from `package.json`, keeping only the
`build` script and the `esbuild`/`esbuild-sass-plugin` devDeps (plus runtime deps). It's a
minimal manifest for build environments that can't install the full lint/format toolchain. It is a
**generated file** (regenerated by the bot), so don't hand-edit it or commit changes to it in a PR.
