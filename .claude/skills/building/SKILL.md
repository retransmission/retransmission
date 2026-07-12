---
name: building
description: >
  How to configure and build Transmission with CMake + Ninja on Linux (the primary dev
  platform), plus what CI builds so you can reproduce a failure. Load this whenever you are
  about to compile the project or a single target (transmission-daemon, transmission-qt,
  transmission-gtk, transmission-remote, all-tests, the transmission static lib), when
  `cmake`/`ninja` errors out, when a build is mysteriously slow (clang-tidy!), when you hit a
  stale CMakeCache or out-of-date submodule, when clangd can't find compile_commands.json, or
  when you need to run code_style.sh / clang-tidy before submitting. Covers the build/ dir
  convention, the ENABLE_*/USE_SYSTEM_*/RUN_CLANG_TIDY options and their defaults, where
  binaries land, and reproducing GitHub Actions build/tidy failures locally.
---

# Building Transmission

Primary dev platform is Linux with **CMake + Ninja**. macOS/Xcode builds are out of scope here —
see the **macos-client** skill, `docs/Building-Transmission.md` ("On macOS"), and
`Transmission.xcodeproj`. Running and writing tests is the **testing** skill; this skill gets you a
working build and a fast iteration loop.

Ground truth lives in `CMakeLists.txt` (root), `cmake/`, `code_style.sh`, and
`docs/Building-Transmission.md`. This skill distills the day-to-day path and adds the reasoning
those files omit. If a command here ever disagrees with the repo, trust the repo and fix this
skill.

## First: get the submodules

Most of `third-party/` is git submodules (see `.gitmodules`): dht, libevent, libnatpmp, libutp,
miniupnp, googletest, libarchive, libpsl, fmt, fast_float, wide-integer, small, rapidjson,
sigslot, simdutf, and rpavlik-cmake-modules (the CMake helper modules). A build **cannot**
configure without them. A couple of `third-party/` dirs (`madler-crcany`, `wildmat`) are vendored
in-tree, not submodules.

Fresh clone:
```bash
git clone --recurse-submodules https://github.com/transmissiontorrent/transmission
```
Already cloned, or after a pull that bumped a submodule:
```bash
git submodule update --init --recursive
```
"Undefined reference" / missing-header errors right after a `git pull` almost always mean a
submodule moved and you didn't re-run the line above. Bumping or vendoring a dep — editing
`.gitmodules`, `third-party/`, and the `cmake/Find*.cmake` modules — is a deliberate change, not
something you do casually; see the **third-party-deps** skill.

## Configure

Repo convention is a single build tree named **`build/` at the repo root** (one already exists —
inspect it, never `rm -rf` it reflexively; you'll throw away a cache that took minutes to warm).
`build/` is git-ignored.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRUN_CLANG_TIDY=OFF
```

Why each flag:
- **`-G Ninja`** — the whole project assumes Ninja for fast incremental rebuilds. `docs/Building-Transmission.md` sometimes omits it (default Make generator still works), but every CI job and the macOS/GTK/Qt doc examples use Ninja. Prefer it.
- **`-DCMAKE_BUILD_TYPE=RelWithDebInfo`** — the recommended default (optimized + debug info). Use `Debug` for a step-through/sanitizer session, `Release` for a shipping binary.
- **`-DRUN_CLANG_TIDY=OFF`** — see below; this is the single biggest speed lever.

Add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` if you use clangd (see the clangd section).

### The clang-tidy speed lever (read this)

`RUN_CLANG_TIDY` defaults to `AUTO`, and AUTO means **"run clang-tidy on every C++ TU if a
`clang-tidy` binary is on PATH"** (`CMakeLists.txt` ~line 900-920 wires it into
`CMAKE_CXX_CLANG_TIDY`). clang-tidy roughly doubles compile time here and dominates every
rebuild. **While iterating, always configure with `-DRUN_CLANG_TIDY=OFF`** (`NO` works too — both
are CMake false values). Save tidy for your final presubmit (see "Before you submit"). This is the
maintainer's #1 build-speed rule and it is verified against the current `CMakeLists.txt`.

### Options you actually touch (defaults verified in CMakeLists.txt)

| Option | Default | Notes |
|---|---|---|
| `ENABLE_DAEMON` | `ON` | `transmission-daemon` |
| `ENABLE_UTILS` | `ON` | `transmission-remote/-create/-edit/-show` |
| `ENABLE_TESTS` | `ON` | gtest suite; target `all-tests` |
| `ENABLE_CLI` | `OFF` | old curses-free CLI client; opt in |
| `ENABLE_GTK` | `AUTO` | builds if gtkmm found; `USE_GTK_VERSION` 3/4/AUTO |
| `ENABLE_QT` | `AUTO` | builds if Qt found; `USE_QT_VERSION` 5/6/AUTO |
| `ENABLE_MAC` | `AUTO` | only builds on Apple; macOS build is out of scope (see `docs/Building-Transmission.md`) |
| `ENABLE_WERROR` | `OFF` | warnings→errors; CI turns it ON for release/packaging jobs |
| `ENABLE_UTP` / `ENABLE_NLS` | `ON` | µTP transport / native-language (gettext) support |
| `REBUILD_WEB` | `OFF` | regenerate web assets — needs Node; web UI source lives in `web/` (see the web-client skill) |
| `USE_SYSTEM_*` | `AUTO` | per-dep: use the OS copy if present, else build the bundled submodule |

`AUTO` is tri-state (`tr_auto_option`): resolve to ON if the dependency is found, else OFF.
Force a client with `-DENABLE_GTK=ON` (or `OFF`) when AUTO guesses wrong. The full annotated
option list, crypto/`WITH_*` knobs, and the `USE_SYSTEM_DEFAULT` master switch live in
**references/cmake-options.md** — read it when you're changing dependency wiring or debugging a
"library not found" configure failure.

## Build

Everything:
```bash
cmake --build build          # or: ninja -C build
```
A single target (much faster — build only what you're testing):
```bash
cmake --build build -t transmission-daemon
cmake --build build -t transmission-qt
cmake --build build -t transmission-gtk
cmake --build build -t all-tests
```
Target names are verified via `ninja -C build -t targets` and the `add_executable`/`add_library`
calls: `transmission-daemon`, `transmission-gtk`, `transmission-qt`, `transmission-cli`,
`transmission-remote`, `transmission-create`, `transmission-edit`, `transmission-show`,
`transmission-mac` (Apple only), `all-tests`, and `transmission` (the `libtransmission` static
lib — `TR_NAME` is `transmission`). The shared app layer builds as target `transmission-app`
(directory `libtransmission-app/`, alias `transmission::app`). `check-format` and `format` are
utility targets (see below).

### Where binaries land

No global runtime-output dir is set, so each artifact stays under its subdir — `build/bin/` is
empty, don't look there:

| Target | Path |
|---|---|
| daemon | `build/daemon/transmission-daemon` |
| Qt client | `build/qt/transmission-qt` |
| GTK client | `build/gtk/transmission-gtk` |
| CLI | `build/cli/transmission-cli` |
| utils | `build/utils/transmission-{remote,create,edit,show}` |
| macOS app | `build/macosx/Transmission.app` |

### Incremental rebuilds & stale-cache gotchas

Ninja tracks header deps, so after editing sources just re-run `cmake --build build` — only the
affected TUs recompile. Two things that bite:

- **Changing a `-D` option** requires re-running the `cmake -B build ...` configure line;
  Ninja re-configures automatically on the next build if only `CMakeLists.txt` changed, but a new
  `-DENABLE_*`/`-DUSE_SYSTEM_*` value must be passed explicitly.
- **Weird configure/link errors after switching branches or bumping submodules** are usually a
  stale `build/CMakeCache.txt`. Try re-configuring first; if it persists, `rm -rf build` and
  reconfigure. That's the nuclear option — it forces a full rebuild.

## compile_commands.json for clangd

It is **not** generated by default. Add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (the clang-tidy CI
jobs pass exactly this). It lands at `build/compile_commands.json`; point clangd at it (e.g. a
top-level `compile_commands.json` symlink, or `--compile-commands-dir=build`).

## Before you submit

**1. Format.** `./code_style.sh` (from the repo root) formats all C/C++ (`*.c *.cc *.h *.m *.mm`,
per `.clang-format-include`/`.clang-format-ignore`, skipping `third-party/`) with **clang-format
version 22** — it *skips* formatting if your clang-format isn't v22 (override with `--force`), so
install v22 (`pip install 'clang-format~=22.0'` or LLVM apt). It also lints web JS via
`npm`, but only when `web/` has staged changes. It also greps
`Transmission.xcodeproj/project.pbxproj` for three required lines (`objectVersion = 54;`,
`compatibilityVersion = "Xcode 12.0";`, `BuildIndependentTargetsInParallel = YES;`), so a careless
pbxproj splice that drops one fails here — re-run it after any pbxproj edit. `./code_style.sh
--check` is the **local** read-only dry-run (non-zero exit = something needs formatting); CI's
`code-style` job instead runs `./code_style.sh --force` and fails on any resulting `git diff` (see
references/ci.md), and `--check` only matches CI when clang-format 22 is installed — any other
version makes `--check` silently skip the C/C++ checks. Equivalent CMake targets:
`cmake --build build -t format` and `-t check-format`. Configuring also installs
`extras/pre-commit` as your git pre-commit hook: it runs `code_style.sh --check` over the whole
tree (skip with `TR_SKIP_HOOKS=1`), so any unformatted tracked C/C++ file fails the commit — not
just the files you staged (only the `web/` JS lint is gated on staged changes).

**2. clang-tidy.** CI checks tidy only on the lines you changed (per-directory `.clang-tidy`
configs exist in `libtransmission/`, `libtransmission-app/`, `gtk/`, `qt/`, `tests/libtransmission/`,
`tests/qt/`). The binary is **clang-tidy-22** on Linux. For a quick local pass, reconfigure a
throwaway build with `-DRUN_CLANG_TIDY=ON` and build the dirs you touched — but that's slow, which
is exactly why you kept it OFF while iterating. To mirror CI's changed-lines-only run, see
**references/ci.md**.

Branches, commit style, PR/Notes tags, and the AI-disclosure convention are out of scope for this
skill — see the **contributing-workflow** skill (and `AGENTS.md` / `CONTRIBUTING.md`).

## CI (reproducing a red check)

CI is `.github/workflows/actions.yml` (plus `codeql.yml`, `webapp.yml`). The CMake-based jobs
configure with `-G Ninja` into a dir named `obj` (not `build`), and most of them cache compiles
with **ccache** via `hendrikmuhs/ccache-action@v1` + `-DCMAKE_C[XX]_COMPILER_LAUNCHER=ccache`
(heads-up: PR #102's subject says "sccache" but the workflow it landed actually uses ccache). The
`macos-xcodebuild-universal` job is the exception — it drives `xcodebuild`, not CMake. Jobs of note:
sanitizer builds (`Debug`; the sanitizer set varies by platform — see references/ci.md), separate
changed-lines `clang-tidy-linux`/`-windows` jobs, and release/packaging jobs that add
`-DENABLE_WERROR=ON`. To reproduce a specific failing job's exact configure line and the
changed-lines clang-tidy invocation, read **references/ci.md**.
