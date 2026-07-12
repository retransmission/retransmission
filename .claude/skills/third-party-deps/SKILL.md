---
name: third-party-deps
description: >-
  How Transmission bundles third-party libraries under third-party/ and wires
  them into the build. Load this whenever you touch third-party/, .gitmodules,
  a cmake/Find*.cmake module, or the tr_add_external_auto_library / USE_SYSTEM_*
  machinery — i.e. adding, removing, bumping, or debugging a bundled dependency
  (libevent, fmt, miniupnpc, simdutf, libarchive, zlib, dht, libutp, etc.),
  deciding submodule vs vendored source, or fixing "why isn't my new library
  found / linked / building on macOS". Also load before pinning a new submodule
  under the transmissiontorrent org.
---

# Managing third-party bundled libraries

Transmission vendors its dependencies under `third-party/` so it can build
self-contained on every platform, while still letting distro packagers link
system copies. Two rules make this area unforgiving:

1. **Correctness costs are amplified.** A dep is consumed by CMake, the Xcode
   project, CI package manifests, and docs at once. Half a change compiles on
   your box and breaks someone else's platform.
2. **The tree moves fast and your checkout may lag.** Always establish current
   state before editing — do not trust memory (including this file's examples).

```bash
git submodule status                       # submodules + their pinned versions
git log --oneline -20 -- third-party/      # recent adds / bumps / removals
git rev-list --left-right --count HEAD...origin/main   # are you behind main?
```

Both `#106` (drop `libdeflate` for system zlib) and `#108` (add the `libarchive`
submodule) have since landed on `main`, so the current tree has a `libarchive`
submodule and **no** `libdeflate` — though an untracked `third-party/libdeflate/`
dir can linger in a working tree from before the removal. Expect this kind of skew
and let `git submodule status` settle it, not memory.

## The two kinds of entries

- **Submodules** (in `.gitmodules`; `third-party/<name>` is a gitlink). Most
  point at forks under **`github.com/transmissiontorrent/`** with a
  `# synced with <upstream>` comment — Transmission mirrors upstreams into its
  own org on purpose. Some track a patched branch (`branch =
  post-<ver>-transmission`). **New submodules go under the transmissiontorrent
  org**, matching the existing pattern (libarchive did:
  `url = https://github.com/transmissiontorrent/libarchive.git`). That fork must
  exist under the org *before* `git submodule add` will work — a manual GitHub
  step needing transmissiontorrent write access. If you don't have it, ask a
  maintainer to mirror the upstream; the org-admin side is outside this skill.
- **Vendored source** committed directly (no `.gitmodules` entry): `wildmat`,
  `madler-crcany`, and the in-flight `librecycle` / `tinygettext`. Edited in
  place; no submodule update.

`cmake/` also holds a bundled submodule of pure CMake helpers,
`third-party/rpavlik-cmake-modules`, added to `CMAKE_MODULE_PATH`.

Full annotated list with per-dep purpose and wiring:
**`references/inventory.md`**.

## How a bundled lib reaches the build (mental model)

Almost every linkable dep is declared with one macro plus a matching trio of
names. Master this and the rest follows.

```cmake
# in CMakeLists.txt, near the other <ID>_MINIMUM / USE_SYSTEM_<ID> lines:
set(FOO_MINIMUM 1.2.3)
tr_auto_option(USE_SYSTEM_FOO "Use system foo library" ${USE_SYSTEM_DEFAULT})
...
tr_add_external_auto_library(FOO libfoo
    LIBNAME foo
    TARGET foo::foo
    CMAKE_ARGS -DFOO_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF)
```

- **`USE_SYSTEM_FOO`** (default `AUTO`) picks system vs bundled. `AUTO` tries the
  system copy and silently falls back to bundled; `ON` requires the system copy;
  `OFF` forces bundled. This is the same `USE_SYSTEM_*` pattern the whole project
  uses.
- **`FOO`** (the `ID`) only feeds `USE_SYSTEM_<ID>` and `<ID>_MINIMUM`.
- **`libfoo`** (the `PACKAGENAME`, 2nd arg) is what `find_package` searches for,
  the `cmake/Find<PACKAGENAME>.cmake` filename, and the default source subdir
  `third-party/<PACKAGENAME>`. **It is case-sensitive and often differs from the
  dir name** (`Libevent`, `WideInteger`, `FastFloat`, `PalSigslot`, `GTest`).
- The macro produces the **same imported target** (`foo::foo`) whether the lib
  is system or bundled, so `libtransmission/CMakeLists.txt` links one name
  unconditionally.
- **Check for a Windows static-link guard.** Many portable C libraries gate
  their public headers on a `<LIB>_STATICLIB`-style macro — e.g. libnatpmp's
  `natpmp_declspec.h` does `#if defined(_WIN32) && !defined(NATPMP_STATICLIB)` →
  `__declspec(dllimport)`. If the new lib's headers have one, add
  `target_compile_definitions(<target> INTERFACE <LIB>_STATICLIB)` guarded by
  `if(NOT USE_SYSTEM_<ID>)` right after the macro call (natpmp and miniupnpc both
  do — `CMakeLists.txt:566-590`). Miss it and Windows static builds fail to link
  (`unresolved external __imp_*`) while Linux/macOS build fine — a gap local
  testing won't catch.

Under the hood the macro either `find_package`s a system copy, `add_subdirectory`s
the source (`SUBPROJECT`), or builds it isolated via `ExternalProject`
(the default; needs `LIBNAME`). When to use which, the `Find*.cmake` contract,
and the RapidJSON / vendored special cases are all in
**`references/cmake-wiring.md`** — read it before writing a new block or Find
module.

## Adding a new bundled dependency

The canonical template is **`#108` `5a985269e` "deps: blocklist pt 2: add
libarchive dep"**, now on `main` — it added `libarchive` as a **submodule** under
the transmissiontorrent org. `git show 5a985269e` and imitate it; the enumerated
file-by-file table is in `references/cmake-wiring.md`.
The checklist:

1. **Pin the source — first decide submodule vs vendored.** If upstream ships a
   usable `CMakeLists.txt` (or you will add one to a transmissiontorrent fork),
   make it a **submodule** wired the libarchive way this checklist follows
   (`tr_add_external_auto_library` + `USE_SYSTEM_<ID>` + a `Find*.cmake`):
   `git submodule add <url> third-party/<name>` under the transmissiontorrent
   org, then `git -C third-party/<name> checkout <tag>` — prefer a released tag
   over a floating branch. This writes `.gitmodules` + the gitlink. **Or**, if the
   library is tiny enough that a hand-written `CMakeLists.txt` beats maintaining a
   fork, **vendor** the source: drop it in a new `third-party/<name>/` dir with
   its own `CMakeLists.txt`, wired by a bare `add_subdirectory(...)` at root
   (`CMakeLists.txt:593-594`) — no submodule, no `USE_SYSTEM_*`, no
   `<ID>_MINIMUM`, no `Find*.cmake`. `third-party/wildmat` (3 files) and
   `madler-crcany` are the templates for that path; steps 2, 3, and 6 below don't
   apply to it.
2. **Declare it in `CMakeLists.txt`:** a `set(<ID>_MINIMUM ...)` (skip if the
   upstream's version file can't do ranges — see the fast_float/simdutf comments
   there), a `tr_auto_option(USE_SYSTEM_<ID> ...)`, and a
   `tr_add_external_auto_library(<ID> <PACKAGENAME> ...)` block. Turn **off**
   every optional feature via `CMAKE_ARGS` (static only, no tests, no shared) —
   the libarchive block is the model for aggressive trimming.
3. **Write `cmake/Find<PACKAGENAME>.cmake`** so a system copy also works — unless
   the upstream installs its own CMake config package, in which case skip it (the
   SUBPROJECT deps sigslot/fast_float/wide-integer/small have no Find module and
   resolve via that config alone). Two valid shapes: for a plain `LIBNAME`+`TARGET`
   ExternalProject lib, copy the **minimal** `cmake/Findlibnatpmp.cmake` or
   `cmake/Finddht.cmake` — set the `<PKG>_INCLUDE_DIR(S)`/`_LIBRARY(IES)`/`_FOUND`
   vars and let the macro synthesize the target. libarchive's fuller
   config-probe + `add_library` module is only *required* for SUBPROJECT deps and
   is redundant for an ordinary C lib. `cmake/FindRapidJSON.cmake` is the
   header-only template. Details in `references/cmake-wiring.md`.
4. **Link it** in `libtransmission/CMakeLists.txt`'s
   `target_link_libraries(${TR_NAME} PRIVATE ...)` list (or the relevant target)
   using the imported target name.
5. **Mirror it into the Xcode project** — CMake alone breaks the macOS build
   (see below).
6. **Update CI + docs** if it can be a system package (see the last section).
7. Configure and build to confirm both `USE_SYSTEM_<X>=OFF` and, where a system
   package exists, `=ON` resolve. See the building and testing skills; do not
   commit generated build dirs.

## Removing a dependency

Mirror image of an add, and it is never just deleting the submodule. Template:
**`#106` `041ee23f7` "deps: blocklist pt 1: use system zlib; drop libdeflate"**.
In one commit it removed the `.gitmodules` stanza + gitlink + `cmake/Find*.cmake`
+ the CMake option/minimum/block + the Xcode target, **and** ported every call
site off the old API (`rpc-server.*`, `rpcimpl.cc`), swapped the link target
(`libdeflate::libdeflate` → `ZLIB::ZLIB`), and updated CI manifests + docs.
File-by-file table in `references/cmake-wiring.md`.

## Bumping an existing submodule

```bash
git -C third-party/<name> fetch --tags
git -C third-party/<name> checkout <new-tag>   # move the pinned commit
git add third-party/<name>                      # stage the new gitlink
```

Then: bump `<ID>_MINIMUM` in `CMakeLists.txt` only if the new floor matters,
skim the upstream changelog for renamed CMake options (fix the `CMAKE_ARGS`
block if so), rebuild + run tests, and note whether the Xcode target needs
new/removed source files. Real exemplars: `aba9a7d6f` "Bump fmt to 12.1 (#7793)",
`9e5b72df0` "bump miniupnpc to 2.3.3 (#7783)". For branch/PR conventions — especially the
one-sentence `Notes:` release-notes paragraph every PR must include — follow
`CONTRIBUTING.md` (repo root, "Pull Requests" section).

Onboarding a fresh clone or after a bump, sync submodules the way
`docs/Building-Transmission.md` documents:
`git submodule update --init --recursive`.

## The macOS / Xcode duplication rule

`Transmission.xcodeproj/project.pbxproj` builds the bundled libs in its **own**
static-library targets — it does **not** invoke CMake. So **adding a dep to
CMake alone silently breaks the macOS build.** For most small libs the Xcode side
is just a new static-lib target compiling the few C sources (natpmp's whole
addition is essentially `OTHER_CFLAGS = "-DENABLE_STRNATPMPERR"` plus a product
name; psl adds no defines at all).

An autoconf-style `config.h` is a **special case, not a standard step** — only
libraries whose own sources `#include` a generated `config.h` need one.
`-DHAVE_CONFIG_H` is used by exactly one bundled target today, libevent
(`third-party/macosx-libevent-*.h`); libarchive's add is the other, carrying a
hand-authored `third-party/libarchive-config/config.h` selected with
`GCC_PREPROCESSOR_DEFINITIONS = HAVE_CONFIG_H=1` alongside +604 lines of pbxproj
(a new `archive` target compiling ~123 C sources). That config header's own
top-comment explains why. **If your library doesn't ship a `config.h`, don't
fabricate one.**

Editing the pbxproj from Linux is a discipline of its own — **see the
macos-client skill** for how to splice a from-source static-lib target (and, only
if the library needs it, a Darwin `config.h`) without corrupting the project
file. That skill's reference points at a plain lib target like `psl` (and `archive`
for the config.h case) as the representative case, reserving config.h for the
libraries that require it.

## Licensing & attribution

`COPYING` (repo root) licenses Transmission under **GPLv2 / GPLv3** (or a future
Mnemosaic-endorsed license), with an explicit OpenSSL-linking exception. A new
dependency must be **GPL-compatible** — check the upstream license before
vendoring; if it is not clearly compatible, stop and raise it rather than
guessing. Each dependency keeps its **own** license text in its directory
(submodules carry it automatically; for vendored source, copy the upstream
`LICENSE`/`COPYING` in alongside the code — see `third-party/madler-crcany/LICENSE`,
`third-party/librecycle/LICENSE`). Do not strip these files.

## CI and docs to keep in sync

A dep that can be satisfied by a **system** package appears in the CI system-
dependency manifest **`.github/actions/install-deps/action.yml`**, which has
parallel lists per package manager (dnf `*-devel`, apt `*-dev`, plus a
`BASE_PACKAGES` list). Sweep *every* list, but the right edit per list is
platform-specific: `#106` dropped `libdeflate-devel`/`libdeflate-dev`/`libdeflate`
from all three, yet added a zlib package only to the dnf (`zlib-devel`) and apt
(`zlib1g-dev`) lists — `BASE_PACKAGES` already provides zlib, so it only lost
libdeflate. Package names also live in the workflow files themselves
(`.github/workflows/actions.yml`, `.github/workflows/codeql.yml`) — grep those
too. The human build instructions in `docs/Building-Transmission.md` list the
same apt/dnf packages and must match.

## Related skills & docs

- **building** — configure/build flags, `USE_SYSTEM_*` at the command line,
  clang-tidy policy.
- **testing** — running/adding tests; googletest specifics.
- **macos-client** — editing `Transmission.xcodeproj` from Linux (required for
  every dep change).
- **translations** — tinygettext and the i18n pipelines.
- **`CONTRIBUTING.md`** (repo root — not a skill) — the required `Notes:`
  release-notes paragraph and other PR conventions for the change.
