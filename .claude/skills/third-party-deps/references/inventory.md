# third-party/ inventory (annotated)

A snapshot of what lives under `third-party/` and how each entry reaches the
build. **This rots** — pinned versions and the exact set change constantly.
Always regenerate the live truth:

```bash
git submodule status                    # submodules + pinned commit/tag
git log --oneline -20 -- third-party/   # recent adds/bumps/removes
git -C third-party/<name> describe --tags   # what a submodule is pinned to
```

Purposes below are anchored to each entry's upstream identity (the URL/comment
in `.gitmodules`) and its use in-tree; treat them as high-confidence but verify
if you are about to make a decision that depends on one.

## Two kinds of entries

- **git submodules** — listed in `.gitmodules`; `third-party/<name>` is a
  gitlink to a pinned commit. Most point at forks under the
  **`github.com/transmissiontorrent/`** org (a deliberate supply-chain choice:
  Transmission mirrors upstreams into its own org rather than depending on the
  original repos directly). A `# synced with <upstream>` comment records the
  real upstream. Several track a custom branch
  (`branch = post-<ver>-transmission`) carrying local patches.
- **vendored source** — committed directly into the tree, no `.gitmodules`
  entry. Update by editing files in place (or re-copying from upstream); there
  is no `git submodule update`.

## Submodules

Regenerate the list with `git submodule status`. As last verified, 16 entries:

| Dir | Upstream (per `.gitmodules`) | Purpose | Build wiring |
|---|---|---|---|
| `dht` | transmissiontorrent/dht (`post-0.27-transmission` branch) | Mainline (Kademlia) DHT for trackerless peer discovery | `tr_add_external_auto_library(DHT dht LIBNAME dht TARGET dht::dht)` — ExternalProject |
| `libevent` | fork of libevent.git (`post-2.1.12` branch) | Async event loop / non-blocking socket I/O | `EVENT2 Libevent SUBPROJECT SOURCE_DIR libevent COMPONENTS core extra` |
| `libnatpmp` | transmissiontorrent/libnatpmp | NAT-PMP port mapping | `NATPMP libnatpmp LIBNAME natpmp TARGET natpmp::natpmp` — ExternalProject; adds `NATPMP_STATICLIB` when bundled |
| `libutp` | fork of libutp (`post-3.4` branch) | µTP — BitTorrent's UDP-based transport | `UTP libutp SUBPROJECT` (guarded by `ENABLE_UTP`) |
| `miniupnp` | synced with miniupnp/miniupnp | UPnP-IGD port mapping (uses the `miniupnpc/` subdir) | `MINIUPNPC miniupnpc LIBNAME miniupnpc SOURCE_DIR miniupnp/miniupnpc` — ExternalProject; adds `MINIUPNP_STATICLIB` |
| `libpsl` | fork of libpsl (`post-3.0.0` branch) | Public Suffix List — cookie/domain scoping | `PSL libpsl LIBNAME psl TARGET psl::psl` — ExternalProject |
| `fmt` | synced with fmtlib/fmt | `{fmt}` formatting library (used everywhere) | `FMT fmt SUBPROJECT`; wrapped as `transmission::fmt-header-only` with exceptions disabled |
| `fast_float` | synced with fastfloat/fast_float | Fast, correct string→float parsing | `FAST_FLOAT FastFloat SUBPROJECT SOURCE_DIR fast_float` |
| `wide-integer` | synced with ckormanyos/wide-integer | Fixed-width big integers (128-bit+) | `WIDE_INTEGER WideInteger SUBPROJECT SOURCE_DIR wide-integer`; wrapped as `transmission::WideInteger` |
| `small` | synced with alandefreitas/small | Small-buffer-optimized containers (`small_vector` etc.) | `SMALL small SUBPROJECT`; wrapped as `transmission::small`, exceptions disabled |
| `sigslot` | transmissiontorrent/sigslot (palacaze/sigslot) | Header-only signal/slot | `SIGSLOT PalSigslot SUBPROJECT SOURCE_DIR sigslot` |
| `simdutf` | synced with simdutf/simdutf | SIMD base64 + UTF validation/transcoding (replaced libb64 + utfcpp in `85c75a8ae`) | `SIMDUTF simdutf SUBPROJECT` |
| `rapidjson` | synced with Tencent/rapidjson | JSON parsing (`variant-json.cc`) | **Special:** header-only via `find_package(RapidJSON)` → `cmake/FindRapidJSON.cmake`; no `USE_SYSTEM_*` |
| `googletest` | google/googletest | Unit-test framework (tests only) | `GTEST GTest SUBPROJECT SOURCE_DIR googletest` in `cmake/TrGTest.cmake`; own `USE_SYSTEM_GTEST` (default OFF). See testing skill |
| `rpavlik-cmake-modules` | synced with rpavlik/cmake-modules | CMake helper modules | added to `CMAKE_MODULE_PATH` at `CMakeLists.txt:32`; not a linked library |
| `libarchive` | transmissiontorrent/libarchive | tar/zip/gzip extraction for the blocklist downloader | `ARCHIVE libarchive LIBNAME archive TARGET libarchive::libarchive` — ExternalProject; `CMAKE_ARGS` disables every format/filter except tar/zip/raw + gzip. Added by `#108` (`5a985269e`) |

Casing gotcha: the `PACKAGENAME` (2nd arg / `Find<PACKAGENAME>.cmake` /
`foo::foo` target) frequently differs from the dir name and the `ID` — e.g.
`Libevent`, `WideInteger`, `FastFloat`, `PalSigslot`, `GTest`. Match the
upstream's installed CMake config package name exactly.

## Vendored (non-submodule) source

| Dir | What it is | Wiring | License file |
|---|---|---|---|
| `madler-crcany` | Mark Adler's CRC code (crc32c / crc32iscsi) | `add_subdirectory` at `CMakeLists.txt:593` | `third-party/madler-crcany/LICENSE` |
| `wildmat` | Wildcard/glob matcher (`wildmat.c/.h`) | `add_subdirectory` at `CMakeLists.txt:594`; `STATIC wildmat` target linked at `libtransmission/CMakeLists.txt:284` | none as separate file — legacy vendored, notice in-source |

(`librecycle` and `tinygettext` are **not** in this table — they carry their own
`CMakeLists.txt` but are still uncommitted WIP; see the next section.)

Also at the `third-party/` root: `macosx-libevent-evconfig-private.h` and
`macosx-libevent-event-config.h` — hand-authored libevent config headers for the
macOS Xcode build (see the macOS/Xcode section of SKILL.md).

## Not-yet-on-main entries (verify before trusting)

The tree moves fast; some `third-party/` dirs are uncommitted WIP that this file
must not present as landed. As last verified:

- **`librecycle`** (cross-platform trash/recycle) and **`tinygettext`** (gettext
  replacement that reads `.po` directly; see the translations skill) each carry
  their own `CMakeLists.txt`, but sit as **untracked (`??`)** dirs that **no parent
  `add_subdirectory` pulls into the build yet** — do not cite them as landed.
- **`libdeflate`** is gone from `main` (removed by `#106`), but an untracked
  `third-party/libdeflate/` dir can still linger in a working tree from before the
  removal.

Run `git submodule status`, `git log --oneline -- third-party/`, and compare
`HEAD` against `origin/main` before relying on any of the above.
