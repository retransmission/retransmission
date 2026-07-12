# CMake wiring for third-party libraries — deep reference

Read this when you need the full behavior of `tr_add_external_auto_library`,
the `Find<PACKAGENAME>.cmake` contract, or the exact file-by-file diff for
adding/removing a dependency. Line numbers are as of this writing — grep to
re-anchor if they have drifted.

All paths are repo-relative to `/home/charles/src/transmission`.

## Where the plumbing lives

- `cmake/TrMacros.cmake` — `tr_add_external_auto_library` (macro, ~line 160),
  plus the auto-option helpers `tr_auto_option` (~45), `tr_auto_option_changed`
  (~33), `tr_fixup_auto_option` (~52), `tr_get_required_flag` (~91).
- `CMakeLists.txt` (root):
  - lines 26–28 define `TR_THIRD_PARTY_DIR_NAME` (`third-party`),
    `TR_THIRD_PARTY_SOURCE_DIR` (`${PROJECT_SOURCE_DIR}/third-party`),
    `TR_THIRD_PARTY_BINARY_DIR` (`${PROJECT_BINARY_DIR}/third-party`).
  - line 32 adds `third-party/rpavlik-cmake-modules` to `CMAKE_MODULE_PATH`.
  - ~lines 44–66 declare `<ID>_MINIMUM` version floors.
  - ~lines 82–96 declare the `tr_auto_option(USE_SYSTEM_<ID> ...)` switches.
  - ~line 290 `find_package(...)` for pure-system deps (CURL, ZLIB, crypto).
  - ~lines 531–650 the `tr_add_external_auto_library(...)` calls plus the two
    `add_subdirectory(...)` vendored deps (madler-crcany, wildmat).
- `cmake/Find<PACKAGENAME>.cmake` — one per dep that needs a *custom* system-copy
  probe. Deps whose upstream installs its own CMake config package
  (sigslot/`PalSigslot`, fast_float/`FastFloat`, wide-integer/`WideInteger`,
  small) have **no** Find module and resolve through that config alone.
- `libtransmission/CMakeLists.txt` ~line 267 — `target_link_libraries(${TR_NAME}
  PRIVATE ...)` is where the imported target gets consumed.

## `tr_add_external_auto_library(ID PACKAGENAME [options])`

Signature (from `cmake_parse_arguments` at the top of the macro):

- Positional: `ID` `PACKAGENAME`
- Flags: `SUBPROJECT`, `HEADER_ONLY`
- One-value: `LIBNAME`, `SOURCE_DIR`, `TARGET`
- Multi-value: `CMAKE_ARGS`, `COMPONENTS`

`ID` vs `PACKAGENAME` — do not conflate them:

- **`ID`** is the UPPERCASE token used only to look up `USE_SYSTEM_${ID}` and
  `${ID}_MINIMUM`. Examples: `EVENT2`, `WIDE_INTEGER`, `ARCHIVE`.
- **`PACKAGENAME`** is what `find_package(${PACKAGENAME} ...)` searches for. It
  must match (case-sensitively) the upstream's installed CMake config package
  **and**, for the deps that have one, the `cmake/Find${PACKAGENAME}.cmake`
  filename. Examples: `Libevent`,
  `WideInteger`, `libarchive`, `PalSigslot`, `FastFloat`. It also defaults the
  source subdir to `third-party/${PACKAGENAME}` unless `SOURCE_DIR` overrides it
  (libevent, miniupnp/miniupnpc, sigslot, wide-integer, fast_float, googletest
  all override because dir name != package name).

### Resolution order (what the macro actually does)

1. If `USE_SYSTEM_${ID}` is truthy (ON or AUTO): `find_package(${PACKAGENAME}
   ${${ID}_MINIMUM} ...)`, then `tr_fixup_auto_option` flips an `AUTO` to `ON`
   (found) or `OFF` (not found, not required). `REQUIRED` is added only when the
   option was explicitly `ON` (via `tr_get_required_flag`), so `AUTO` degrades
   gracefully to the bundled copy.
2. System found → use it; no build target is added. The `Find` module already
   defined the imported target.
3. Else `SUBPROJECT` → `add_subdirectory(third-party/<dir> ...)` with
   `EXCLUDE_FROM_ALL` (and `SYSTEM` on CMake ≥ 3.25). `CMAKE_ARGS` `-D...`
   entries are pushed into the cache first so the subproject reads them.
4. Else (default) → `ExternalProject_Add` builds a **static** lib into
   `third-party/<dir>.bld/pfx`, and the macro synthesizes an
   `INTERFACE IMPORTED` target named by `TARGET` that carries the include dir +
   the static lib and an `add_dependencies` edge to the sub-build.

### `SUBPROJECT` vs `ExternalProject` — which to pick

- **`SUBPROJECT`** (add_subdirectory) is preferred when the upstream's
  `CMakeLists.txt` is well-behaved: it exports proper `foo::foo` targets and
  does not stomp global `CMAKE_*` vars. Used by libevent, libutp, fast_float,
  fmt, sigslot, simdutf, small, wide-integer, googletest. You do **not** pass
  `LIBNAME` (the subproject defines its own targets); you often add glue after
  the call (e.g. `add_library(transmission::small INTERFACE IMPORTED)` wrapping
  `small::small` with extra compile defs).
- **`ExternalProject`** (the default, needs `LIBNAME` + `TARGET`) is for
  uppercase libs whose `CMakeLists.txt` mutates global state or whose build must
  be isolated. libarchive is wired this way: its `CMakeLists.txt` would otherwise
  stomp global `CMAKE_*` variables and compiler options (the now-removed libdeflate
  block carried a comment saying exactly that). It runs as a fully separate CMake
  configure/build, so you
  forward toolchain bits via `CMAKE_ARGS` (the macro already forwards compiler,
  flags, sysroot, OSX archs, Android, vcpkg).
- **`HEADER_ONLY`** skips the library entirely — include dirs only.

### Trimming the sub-build

Pass `-D...=OFF` for every optional feature via `CMAKE_ARGS`. The libarchive
block is the reference for aggressive trimming — it disables every format,
filter, and crypto backend except tar/zip/raw + the gzip(zlib) filter, and
forwards `CMAKE_PREFIX_PATH` (escaping `;` as `$<SEMICOLON>`) so the sub-build's
`find_package(ZLIB)` resolves the parent's staged zlib. Keep bundled builds
lean: static, no shared, no tests, no install of headers you don't need.

## The `Find<PACKAGENAME>.cmake` contract

A dep needs a Find module **only when its upstream does not reliably install a
CMake config package** for `find_package` to pick up — sigslot, fast_float,
wide-integer, and small each ship one and so have no `Find*.cmake` at all. When
you do write one, it must leave the **same imported target** whether the lib is
system or bundled, so `libtransmission/CMakeLists.txt` links one name
unconditionally. Two shapes are in use, and which you need depends on how the dep
is wired:

- **Minimal** — `cmake/Finddht.cmake`, `cmake/Findlibnatpmp.cmake`,
  `cmake/Findlibpsl.cmake`, for a plain `ExternalProject` dep declared with
  `LIBNAME` + `TARGET`. The module only probes (`pkg_check_modules` +
  `find_path`/`find_library`) and sets `<PKG>_INCLUDE_DIR(S)`,
  `<PKG>_LIBRARY(IES)`, `<PKG>_FOUND`. It does **not** call `add_library` —
  `tr_add_external_auto_library` synthesizes the imported target from those vars
  (`cmake/TrMacros.cmake:278`, guarded by `NOT SUBPROJECT AND ... NOT TARGET`),
  in both the system-found and bundled paths. **Copy one of these for an ordinary
  small C library.**
- **Full** — `cmake/Findlibarchive.cmake` (added in `#108`),
  `cmake/Findminiupnpc.cmake`, `cmake/FindLibevent.cmake`. It does both jobs
  itself: (1) try the upstream
  config package — `find_package(${CMAKE_FIND_PACKAGE_NAME} QUIET NO_MODULE)`,
  return via `find_package_handle_standard_args(... CONFIG_MODE)` if found — then
  (2) the same probe as above followed by `add_library(foo::foo INTERFACE
  IMPORTED)` wired to the discovered dir + lib. This is **required for
  `SUBPROJECT` deps** (the macro skips its own target synthesis when `SUBPROJECT`
  is set, so the Find module must produce the target) and merely
  redundant-but-harmless for an `ExternalProject` dep like miniupnpc that also
  uses it.

`cmake/FindRapidJSON.cmake` is the minimal header-only template (see below).

## Special cases (not the standard macro path)

- **RapidJSON** — header-only JSON parser used by
  `libtransmission/variant-json.cc`. No `USE_SYSTEM_*`, no `*_MINIMUM`, not via
  `tr_add_external_auto_library`. `CMakeLists.txt:286` just calls
  `find_package(RapidJSON)`; `cmake/FindRapidJSON.cmake` unconditionally makes an
  `INTERFACE IMPORTED` target on `third-party/rapidjson/include` and defines
  `RAPIDJSON_HAS_STDSTRING=1`. Linked as `RapidJSON` at
  `libtransmission/CMakeLists.txt:282`. This is the pattern for a pure
  always-bundled header lib.
- **madler-crcany, wildmat** — vendored C source with hand-written
  `CMakeLists.txt` files, pulled in by bare `add_subdirectory(...)` at
  `CMakeLists.txt:593–594`. `wildmat` builds a `STATIC` `wildmat` target linked
  at `libtransmission/CMakeLists.txt:284`. No system option — always built.
- **googletest** — wired in `cmake/TrGTest.cmake` (not the root list) via
  `tr_add_external_auto_library(GTEST GTest SUBPROJECT SOURCE_DIR googletest ...)`
  with its own `option(USE_SYSTEM_GTEST ... OFF)`. Test-only; see the testing
  skill.
- **tinygettext, librecycle** — vendored subprojects with their own
  `CMakeLists.txt`; part of in-flight work (shared i18n / recycle). See the
  translations skill for tinygettext.

## Exemplar diff — ADD a dependency (`5a985269e`, #108, libarchive)

`git show 5a985269e` — merged to `main` as *"deps: blocklist pt 2: add libarchive
dep (#108)"*. Nine files, all additions (the branch-only precursor `58a83e714` had
the same core wiring minus the CI-manifest edits):

| File | Change |
|---|---|
| `.gitmodules` | `[submodule "third-party/libarchive"]` stanza, `url = https://github.com/transmissiontorrent/libarchive.git` |
| `third-party/libarchive` | the submodule gitlink (transmissiontorrent/libarchive fork) |
| `CMakeLists.txt` (+58) | `set(ARCHIVE_MINIMUM 3.4)`; `tr_auto_option(USE_SYSTEM_ARCHIVE ...)`; a `tr_add_external_auto_library(ARCHIVE libarchive LIBNAME archive TARGET libarchive::libarchive CMAKE_ARGS ...)` block that disables all formats/filters except tar/zip/raw + gzip and forwards `CMAKE_PREFIX_PATH` |
| `cmake/Findlibarchive.cmake` (+84) | system-find + `libarchive::libarchive` imported target |
| `libtransmission/CMakeLists.txt` (+1) | `libarchive::libarchive` added to the `PRIVATE` link list |
| `Transmission.xcodeproj/project.pbxproj` (+604) | new static-library **archive** target compiling the same C sources |
| `third-party/libarchive-config/config.h` (+202) | hand-authored Darwin `config.h` for the Xcode target (`HAVE_CONFIG_H`) |
| `.github/actions/install-deps/action.yml`, `.github/workflows/actions.yml` | add `libarchive` to the system-dep manifests so `USE_SYSTEM_ARCHIVE` can find a package |

## Exemplar diff — REMOVE a dependency (`041ee23f7` / squashed `a4f13f7c8`, drop libdeflate)

`git show 041ee23f7` — on `main` as *"deps: blocklist pt 1: use
system zlib; drop libdeflate (#106)"*. It is the mirror image of an add, plus
the source migration off the removed lib:

| File | Change |
|---|---|
| `.gitmodules` | delete the `third-party/libdeflate` stanza |
| `third-party/libdeflate` | delete the gitlink |
| `CMakeLists.txt` | delete `DEFLATE_MINIMUM`, `USE_SYSTEM_DEFLATE`, and the whole `tr_add_external_auto_library(DEFLATE ...)` block; add `set(ZLIB_MINIMUM 1.2.1)` + `find_package(ZLIB ${ZLIB_MINIMUM} REQUIRED)` |
| `cmake/Findlibdeflate.cmake` | delete (73 lines) |
| `libtransmission/CMakeLists.txt` | swap `libdeflate::libdeflate` → `ZLIB::ZLIB` in the link list |
| `libtransmission/rpc-server.{cc,h}`, `libtransmission/rpcimpl.cc` | port call sites off the libdeflate API onto zlib |
| `.github/actions/install-deps/action.yml` | drop `libdeflate-devel`/`libdeflate-dev`/`libdeflate` from all three lists; add a zlib package to **only** the dnf (`zlib-devel`) and apt (`zlib1g-dev`) lists — `BASE_PACKAGES` already provides zlib, so it just loses libdeflate |
| `.github/workflows/actions.yml` | drop libdeflate from the Alpine/Homebrew/MacPorts install lists (Alpine also gains `zlib-dev`) — CI **workflow** files carry their own package lists, not just the composite action |
| `.github/workflows/codeql.yml` | same sweep in the CodeQL job's apt list (`-libdeflate-dev`, `+zlib1g-dev`) |
| `docs/Building-Transmission.md` | update the apt/dnf dependency command lines |
| `Transmission.xcodeproj/project.pbxproj` | remove the libdeflate static-lib target (~280 lines) |

The lesson: removing a bundled lib is never just deleting the submodule — the
**source that used it**, **CI package manifests**, **docs**, and the **Xcode
target** all move together in one commit.
