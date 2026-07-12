# Windows CI mechanics (detail)

Companion to `../SKILL.md`. Verified against `.github/workflows/actions.yml`,
`.github/actions/prepare-deps-win32/action.yml`, `.github/actions/setup-vs-env-win32/action.yml`,
`.github/actions/run-tests/action.yml`, `.github/actions/set-test-env/action.yml`,
`release/windows/main.ps1`, and `appveyor.yml`. Re-verify before trusting; CI drifts.

## How a Windows job is assembled

Every Windows job (`windows`, `sanitizer-tests-windows`, `clang-tidy-windows`) shares four steps:

1. **`prepare-deps-win32`** (composite action). `choco install jom nasm wixtoolset`, uninstalls the
   preinstalled OpenSSL, then runs `release/windows/main.ps1 -Mode Build -BuildArch <arch>
   -BuildPart <CoreDeps|Deps>` to build the external deps from source into a cached prefix. It
   exports `DEPS_PREFIX` (e.g. `C:\x64-prefix`) and adds its `bin/` to `PATH`. The prefix is cached
   by a content hash (`-Mode DepsHash`), so most runs restore it instead of rebuilding.
2. **`setup-vs-env-win32`** (composite action). Uses `vswhere` to locate a VS install with
   `VC.Tools.x86.x64` (or `VC.Tools.ARM64` for arm64), then exports `VCVARSALL` = path to
   `vcvarsall.bat` and `VC_ARCH` = the arch string (`x86` / `x64` / `arm64`).
3. **Configure / Make.** Every cmake/ninja call is wrapped:
   `cmd /c "\"$Env:VCVARSALL\" $Env:VC_ARCH >nul && cmake …"` — i.e. enter the MSVC dev environment,
   then run CMake with `-G Ninja`. Common args: `-DCMAKE_BUILD_TYPE=RelWithDebInfo`
   (Debug only for the tidy job), `-DCMAKE_PREFIX_PATH="$Env:DEPS_PREFIX"`, `-DUSE_SYSTEM_DEFAULT=OFF`,
   `-DRUN_CLANG_TIDY=OFF`.
4. **Test** (when built): the `run-tests` composite action runs `ctest --test-dir obj -j <nproc>
   --output-on-failure --build-config RelWithDebInfo --timeout 600`. See the **testing** skill.

There is **no MSBuild and no `.sln`** anywhere in this path. `jom` is a parallel `nmake` used only
in the deps step (the OpenSSL and Qt-from-source builds), never to build Transmission itself.

## What each dep part builds

`release/windows/main.ps1 -BuildPart` selects which externals get built into `DEPS_PREFIX`:

- **`CoreDeps`** → Zlib, OpenSSL, Curl.
- **`Deps`** → CoreDeps **plus** Expat, DBus, Qt (`build-qt5.ps1` / `build-qt6.ps1`).
- **`App`** → Transmission itself (used by AppVeyor's `All`, not by the GH Actions jobs, which
  build the app with the workflow's own cmake step).

Everything *else* the app links — the `third-party/` submodules (fmt, libevent, dht, libutp,
libnatpmp, miniupnp, libpsl, simdutf, fast_float, rapidjson, sigslot, wide-integer, small,
libarchive, googletest) — is built by CMake from source because `USE_SYSTEM_DEFAULT=OFF` flips
every `USE_SYSTEM_*` default to bundled. Dep wiring itself is the **third-party-deps** skill.

Qt versions are pinned in the build scripts (currently `5.15.19` and `6.10.3`; see
`a323dfad7`). Only Windows CI builds **Qt5** from source, which is why Qt5-only breakage tends to
appear on Windows first (`clang-tidy-windows` omits qt5, but the `windows` and AppVeyor jobs build
it).

## Job-by-job

### `windows` — the full build+test+package matrix
- **Matrix:** `x86`/`windows-2022`, `x64`/`windows-2022`, `x64`/`windows-2025-vs2026`,
  `arm64`/`windows-11-arm`.
- **Config:** `RelWithDebInfo`, `-DENABLE_WERROR=ON`, `-DUSE_SYSTEM_DEFAULT=OFF`, frontends toggled
  by the `what-to-make` outputs (`ENABLE_QT`, `ENABLE_DAEMON`, `ENABLE_CLI`, `ENABLE_TESTS`,
  `ENABLE_UTILS=ON`, `ENABLE_GTK=OFF`, `ENABLE_MAC=OFF`).
- **Steps:** Make (whole build) → Test (ctest, if tests changed) → Install → Package (runs the
  `pack-msi` CMake target, when building a dist or daemon+qt) → upload artifacts.
- **Why it fails:** MSVC compiles at `/W4` (`CMakeLists.txt` `WARNING_CANDIDATES /W4`), but the job
  passes `-DENABLE_GTK=OFF` and `ENABLE_WERROR`'s only wiring is the GTK target (`gtk/CMakeLists.txt`
  maps it to `/WX` for MSVC) — so **no `/WX` reaches any Transmission target here** and `/W4`
  warnings do *not* fail the build. A red `windows` job is a genuine compile/link/test error, not a
  warning. (The `foreach(FLAG -Werror /WX)` loop in `CMakeLists.txt` is a GCC/Clang flag-probe helper
  in the non-MSVC branch, unrelated to `ENABLE_WERROR`.) arm64 is a **native** build on the
  `windows-11-arm` runner — a different ISA where ABI/template/intrinsic issues can appear only in
  that cell.

### `sanitizer-tests-windows` — MSVC AddressSanitizer
- **Runner:** `windows-2022`, x64. Builds the `all-tests` target with `ENABLE_QT=ON`.
- **Flags (set via `CFLAGS`/`CXXFLAGS` env):** `/fsanitize=address /D_DISABLE_STRING_ANNOTATION
  /D_DISABLE_VECTOR_ANNOTATION`. Build type is `RelWithDebInfo` because Debug's `/RTC1` is
  incompatible with ASan.
- **Two Windows-specific ASan facts:**
  - It's **address-only** — no LeakSanitizer, no UBSan (`set-test-env` sets `detect_leaks=0` for
    the `windows` os-family, same as macOS).
  - The container-overflow annotations must be disabled, or annotated `std::string`/`std::vector`
    trip `LNK2038 … 'annotate_string'` / `'annotate_vector'` against the prebuilt (non-instrumented)
    Qt libraries. That's why `_DISABLE_STRING_ANNOTATION`/`_DISABLE_VECTOR_ANNOTATION` are set.
- The job also locates `clang_rt.asan_dynamic-x86_64.dll` and `llvm-symbolizer.exe` and exports
  their native paths so ctest (which runs outside `vcvarsall`) can find the runtime and symbolize
  stacks. A real ASan report here is a real bug — fix it like any sanitizer finding (see
  **debugging** for ASan/UBSan).

### `clang-tidy-windows` — MSVC-header clang-tidy on the diff
- **Runner:** `windows-2025`. Matrix `core` (deps=`CoreDeps`, `ENABLE_QT=OFF`) and `qt6`
  (deps=`Deps`, `ENABLE_QT=ON`). **qt5 is intentionally omitted** (would need Qt5-from-source on the
  runner; Linux covers qt5).
- **Config:** `Debug`, `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. A "Generate sources" step builds the Qt
  `*_autogen` headers and the `_*` ExternalProject targets so tidy sees generated/bundled headers.
- **Runs:** `clang-tidy` via `.github/actions/clang-tidy-diff` over only the lines changed vs the
  merge base, with `extra-arg: -Wno-unused-command-line-argument` (MSVC compile-commands carry
  clang-cl-ignored flags like `/GL`). Same tooling as `clang-tidy-linux`; see **cpp-conventions**
  and **building** for clang-tidy generally.

## AppVeyor (`appveyor.yml`)

- **Status:** live; last edited 2025-11-01 (`2b4803a02` "Bump Windows dependencies (#7755)").
- **Scope:** `branches.only` = `main` and `/^\d+\.\d+.*/` (version branches/tags). **Not** run on
  PRs — do not expect it to gate a feature branch.
- **Matrix:** `Visual Studio 2022`, `TR_ARCH` ∈ {x86, x64} × `QT_VERSION` ∈ {5, 6}.
- **What it does:** `release/windows/main.ps1 -Mode Build` (deps + app + `pack-msi`) then
  `-Mode Test` (ctest). Artifacts: `*.msi` and `*-pdb.7z`. On tags it deploys the MSI to **SignPath**
  for code signing. This is the Windows half of the release pipeline — see **release-engineering**.
- **When it breaks after a green GH Actions run:** almost always a dependency-version bump, a WiX
  packaging change, or a signing/deploy credential — look in `release/windows/*.ps1`, not in
  portable source.

## Fast triage checklist for a red Windows job

1. Which job? `windows` = build/`/WX` warning or test; `sanitizer-tests-windows` = memory bug or
   annotation/link mismatch; `clang-tidy-windows` = a tidy finding on your diff.
2. Open the log, find the **first** `error CXXXX` / `LNK####` / `FAILED:` line (MSVC continues past
   the first error).
3. The `file(line,col)` in the message is your Linux source file — go read both branches of any
   nearby `_WIN32` guard.
4. Map the error code (see `../SKILL.md` "Reading a red Windows job") and fix the category.
5. Small commit, watch that one job.
