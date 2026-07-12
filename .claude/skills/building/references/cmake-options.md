# CMake options reference

Full annotated list, verified against `CMakeLists.txt` (root) lines ~68-104. Read this when you
are changing dependency wiring, forcing a client on/off, switching crypto/Qt/GTK versions, or
debugging a "could not find <lib>" configure error. For the everyday flags, the table in
`SKILL.md` is enough.

Each option is passed as `-D<NAME>=<VALUE>`. Boolean options take `ON`/`OFF`. Options declared
with `tr_auto_option` are tri-state and also accept `AUTO` (resolve to ON if the feature/lib is
found, else OFF); `tr_list_option` ones take one of an enumerated set (also `AUTO`).

## Components to build

| Option | Default | What it controls |
|---|---|---|
| `ENABLE_DAEMON` | `ON` | `transmission-daemon` |
| `ENABLE_UTILS` | `ON` | CLI utils: `transmission-remote`, `-create`, `-edit`, `-show` |
| `ENABLE_CLI` | `OFF` | the `transmission-cli` client (opt in) |
| `ENABLE_TESTS` | `ON` | gtest unit tests; aggregate target `all-tests` |
| `ENABLE_GTK` | `AUTO` | GTK client; builds when gtkmm is found |
| `ENABLE_QT` | `AUTO` | Qt client; builds when Qt is found |
| `ENABLE_MAC` | `AUTO` | macOS client; only builds on Apple (`ENABLE_MAC` forced OFF elsewhere) |
| `REBUILD_WEB` | `OFF` | regenerate the web client's assets (needs Node.js ≥ `NPM_MINIMUM`, network). Web UI source lives in `web/` (see the web-client skill). |
| `INSTALL_WEB` | `ON` | install the prebuilt web assets |
| `INSTALL_DOC` | `ON` | install docs (`README.md`, `docs/rpc-spec.md`, etc.) |
| `INSTALL_LIB` | `OFF` | install `libtransmission` |

## Feature toggles

| Option | Default | Notes |
|---|---|---|
| `ENABLE_UTP` | `ON` | µTP transport |
| `ENABLE_NLS` | `ON` | native-language support; pulls in gettext for the GTK client |
| `ENABLE_WERROR` | `OFF` | warnings→errors. CI turns this ON only in release/packaging jobs; leave it OFF for iteration, but expect CI to fail on any warning. |
| `ENABLE_IPO` | `AUTO` | interprocedural / link-time optimization when the toolchain supports it |
| `WITH_INOTIFY` | `AUTO` | watch-dir via inotify (Linux) |
| `WITH_KQUEUE` | `AUTO` | watch-dir via kqueue (BSD/macOS) |
| `WITH_SYSTEMD` | `AUTO` | systemd startup notification for the daemon |
| `WITH_APPINDICATOR` | `AUTO` | tray icon for the GTK 3 client |

## Client version selectors

| Option | Values | Default |
|---|---|---|
| `USE_GTK_VERSION` | `AUTO` / `3` / `4` | `AUTO` (prefers 4, falls back to 3) |
| `USE_QT_VERSION` | `AUTO` / `5` / `6` | `AUTO` (prefers 6, falls back to 5) |
| `WITH_CRYPTO` | `AUTO` / `ccrypto` / `mbedtls` / `openssl` / `wolfssl` | `AUTO` |

`AUTO` for GTK/Qt tries the newer major version first. If AUTO picks the wrong toolkit or you have
both installed, pin it, e.g. `-DENABLE_QT=ON -DUSE_QT_VERSION=6`.

## Bundled vs system libraries: `USE_SYSTEM_*`

Every bundled dependency has a `USE_SYSTEM_<LIB>` option, all defaulting to the master switch
`USE_SYSTEM_DEFAULT` (itself `AUTO`, and marked advanced). `AUTO`/`ON` uses the OS-installed copy
when found; `OFF` forces building the in-tree `third-party/` submodule instead. For adding, bumping,
or debugging a bundled dep itself, see the **third-party-deps** skill.

Recognized libs (from `CMakeLists.txt`): `EVENT2`, `ARCHIVE`, `DHT`, `FAST_FLOAT`, `FMT`,
`MINIUPNPC`, `NATPMP`, `SIGSLOT`, `SIMDUTF`, `SMALL`, `UTP`, `WIDE_INTEGER`, `PSL`.

When to force `-DUSE_SYSTEM_<LIB>=OFF`: the distro package is missing or too old (the version
floors are the `*_MINIMUM` variables at the top of `CMakeLists.txt`, e.g. `FMT_MINIMUM 8.0.1`,
`CURL_MINIMUM 7.28.0`, `EVENT2_MINIMUM 2.1.0`). CI does this a lot — e.g. `-DUSE_SYSTEM_DHT=OFF`,
`-DUSE_SYSTEM_SIGSLOT=OFF`, `-DUSE_SYSTEM_SMALL=OFF`, `-DUSE_SYSTEM_UTP=OFF`,
`-DUSE_SYSTEM_WIDE_INTEGER=OFF` — because those aren't packaged on Ubuntu/Homebrew. To build a
fully self-contained tree that ignores everything on the system, set `-DUSE_SYSTEM_DEFAULT=OFF`
(this is what the Windows/vcpkg CI jobs do).

Note: `CURL` is a system-only hard requirement — `find_package(CURL ${CURL_MINIMUM} REQUIRED)`, and
a missing/old libcurl is a hard configure error, so a curl dev package is the one non-optional
dependency on Linux (`docs/Building-Transmission.md` lists `libcurl4-openssl-dev`). `RapidJSON` is
the opposite: `cmake/FindRapidJSON.cmake` never searches the system — it always uses the bundled
`third-party/rapidjson` submodule (an INTERFACE target on its include dir), so there is no
`USE_SYSTEM_` toggle and installing a system RapidJSON has no effect.

## Build type

`-DCMAKE_BUILD_TYPE=` one of `RelWithDebInfo` (recommended default), `Debug` (sanitizers,
step-through), `Release` (shipping). Not setting it yields an unoptimized no-debug-info build.

## clang-tidy

`RUN_CLANG_TIDY` defaults to `USE_SYSTEM_DEFAULT` (i.e. `AUTO`). With AUTO, if a `clang-tidy`
binary is on PATH, CMake sets `CMAKE_CXX_CLANG_TIDY` and tidy runs on every C++ TU — the reason
builds feel slow. `find_program` prefers `clang-tidy-22`, then `clang-tidy`. Pass
`-DRUN_CLANG_TIDY=OFF` (or `NO`) to disable while iterating. Per-directory `.clang-tidy` files
(`libtransmission/`, `libtransmission-app/`, `gtk/`, `qt/`, `tests/libtransmission/`, `tests/qt/`)
select the checks; there is no root `.clang-tidy`.
