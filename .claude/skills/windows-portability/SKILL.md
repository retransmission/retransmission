---
name: windows-portability
description: >-
  Diagnose Windows/MSVC CI failures and keep libtransmission portable when you develop on
  Linux or macOS and cannot run a Windows build locally. Load this whenever a Windows GitHub
  Actions job goes red (`windows`, `sanitizer-tests-windows`, `clang-tidy-windows`), an
  AppVeyor build fails, or you see MSVC-only errors/warnings: FTBFS on MSVC, `error C2039`,
  `C2065`, `C4702`, `C3779`, `LNK2038 annotate_string`/`annotate_vector`, `unresolved external
  symbol`, `'min'/'max' is not a member of 'std'`, `'small': redefinition`, `__cplusplus ==
  199711L`, or "works on Linux but breaks on Windows". Also load before editing
  `libtransmission/file-win32.cc`, `file-posix.cc`, `file.h`, `subprocess-win32.cc`,
  `watchdir-win32.cc`, `platform.cc`, or `string-utils.cc`, before adding `#ifdef _WIN32` /
  `<windows.h>` code, when calling a raw `open`/`stat`/`close` instead of the `tr_sys_*` API,
  when touching UTF-8/UTF-16 path handling or `\\?\` long paths, or when a change targets the
  Windows arm64 runner. Covers the CMake+Ninja+MSVC CI reality, mapping a red job to source,
  the tr_sys_* portability layer, and MSVC-vs-gcc breakage patterns mined from git history.
---

# Windows portability from a Linux desk

Windows CI is the failure a Linux/macOS contributor can least reproduce. This skill takes you
from a red Windows check to the offending source line, and teaches the rules that keep a change
green on MSVC **without a Windows machine**.

Ground truth lives in `.github/workflows/actions.yml` (jobs `windows`, `sanitizer-tests-windows`,
`clang-tidy-windows`), `.github/actions/prepare-deps-win32` + `setup-vs-env-win32`,
`appveyor.yml`, the `if(WIN32)` / `if(MSVC)` blocks in `CMakeLists.txt`, and the
`libtransmission/*-win32.cc` files. If this skill and the repo ever disagree, **trust the repo and
fix this skill.**

> **The one fact that explains the rest:** every Windows build in this repo is **CMake + Ninja +
> MSVC `cl.exe`**. There is no `.sln` or MSBuild anywhere in Transmission's own build path (a few
> unused `.sln` files ship inside vendored `third-party/` trees), no Visual Studio *generator*, and
> no vcpkg (vcpkg appears only in the unrelated Android job). All three CI jobs and
> `release/windows/main.ps1` run `cmake … -G Ninja` inside a `vcvarsall.bat` shell. So a Windows
> failure is a *compiler*/*linker*/*runtime* difference, never a project-file difference — which
> is exactly why you can reason about it from Linux.

## The three Windows CI jobs

All are gated by the `what-to-make` job (they only run when relevant paths changed) and all
configure `-G Ninja`, `-DUSE_SYSTEM_DEFAULT=OFF` (bundle every dep), `-DRUN_CLANG_TIDY=OFF`,
under `cmd /c "<vcvarsall> <arch> && cmake …"`.

| Job | Runners / arch | What it does | How it bites you |
|---|---|---|---|
| **`windows`** | x86 & x64 on `windows-2022`, x64 on `windows-2025-vs2026`, **arm64 on `windows-11-arm`** | Full build of the enabled frontends (Qt/daemon/CLI/utils), `RelWithDebInfo`, **`-DENABLE_WERROR=ON`** but **`-DENABLE_GTK=OFF`**, then ctest (if tests changed), install, the `Package` step (`pack-msi` target). | The common red X — and it is a real compile/link/test **error**, not a warning. `ENABLE_WERROR=ON` only reaches the GTK target (`gtk/CMakeLists.txt`), which is off here, so MSVC `/W4` warnings stay warnings — worth cleaning up (they fire on things `-Wall -Wextra` never mention) but they never fail this job. arm64 is a **native** build on the `windows-11-arm` runner — a different ISA where template/ABI/intrinsic breakage can surface *only* there. |
| **`sanitizer-tests-windows`** | `windows-2022`, x64 | Builds `all-tests` with Qt ON under **MSVC AddressSanitizer** (`/fsanitize=address`), runs ctest. | ASan is **address-only** on MSVC (no leak/UBSan). Uses `RelWithDebInfo` (Debug's `/RTC1` is ASan-incompatible) and defines `_DISABLE_STRING_ANNOTATION`/`_DISABLE_VECTOR_ANNOTATION` — omit those and you get `LNK2038 … annotate_string/annotate_vector` against the non-instrumented prebuilt Qt. A real ASan finding here is a genuine memory bug, same as the Linux sanitizer job. |
| **`clang-tidy-windows`** | `windows-2025`, matrix `core` + `qt6` (**qt5 deliberately omitted on Windows**) | Runs `clang-tidy` (clang-cl parse) only on lines changed vs the merge base. | Tidy under the *MSVC* header set can flag what the Linux tidy job misses (different `_WIN32` code paths get parsed). |

Details of each job's exact CMake args, the dep build, and the arm64/vcvarsall mechanics are in
**`references/ci.md`**.

### AppVeyor is the signed-release path, not a PR gate
`appveyor.yml` is still live (last touched 2025-11-01, "Bump Windows dependencies (#7755)") but
`branches.only` restricts it to `main` and version branches/tags. It builds MSI
installers (VS2022, x86/x64 × Qt5/Qt6) via `release/windows/main.ps1`; the uploaded MSIs are
**unsigned**, and **on tags** it sends them to SignPath for code signing. So it does **not** run on
your PR — GitHub Actions is your only pre-merge Windows
signal. AppVeyor breakage after merge is usually a dependency-version or packaging issue; see the
**release-engineering** skill and `release/windows/*.ps1`.

## Reading a red Windows job and mapping it to source

1. **Open the failing step.** `Make` = compile/link (`cl.exe`/`link.exe`). `Test` = ctest. `Configure` = CMake/dep problem.
2. **Find the first `error` line** — MSVC keeps compiling after the first error, so later lines are noise. MSVC diagnostics look like `path\file.cc(LINE,COL): error CXXXX: message`. That `(LINE,COL)` maps straight to the same source file you have on Linux; there is no separate Windows source tree.
3. **Translate the code.** MSVC error codes are the real signal — decode the frequent ones:
   - `fatal error C1083 "Cannot open include file: 'X.h'"` → a **POSIX-only header with no MSVC equivalent** (`<unistd.h>`, `<dirent.h>`, `<pthread.h>`, `<sys/socket.h>`, `<sys/mman.h>`, …) reached portable code **unguarded**. The fix is architectural, not a new include: route file/dir/process work through `tr_sys_*` (see "The portability layer" below), and put socket/net headers behind `#ifdef _WIN32` with the Winsock path (`net.h`, `<winsock2.h>`) — the way the existing portable `.cc` files do.
   - `C2039 "X is not a member of Y"` / `C2065 "undeclared identifier"` → a **missing `#include`** that GCC/Clang pulled in transitively (libstdc++ leaks headers MSVC's STL does not). Fix = include what you use.
   - a syntax error pointing at a `std::min`/`std::max` (or `std::numeric_limits<>::max`) call → the Windows `min`/`max` **macros** expanding (guarded against by `-DNOMINMAX`; see below).
   - `C4702 unreachable code` (and other `/W4`-only warnings) → **not a build-breaker** on the `windows` job (no `/WX` reaches Transmission's MSVC targets — see the job table above), but still worth silencing since `/W4` is on and `-Wall -Wextra` never mention it. The narrowing warnings `C4244`/`C4267` are additionally suppressed project-wide (`/wd4244 /wd4267` in `CMakeLists.txt`).
   - `LNK2019 unresolved external symbol …WSA…`/`…Shell…` → a Win32 system lib not linked. This repo links them via CMake, not `#pragma comment(lib)` — e.g. `crypt32`/`shlwapi` in `libtransmission/CMakeLists.txt` and `iphlpapi ws2_32` in `TR_NETWORK_LIBRARIES`, each guarded by `$<$<BOOL:${WIN32}>:…>`. Add yours there.
4. **Reproduce the *category* on Linux** where you can (see "Sanity-check from Linux" below). You cannot run `cl.exe`, but most MSVC breaks are include hygiene, `/W4`, or `_WIN32`-path logic you *can* read and reason about.
5. **Iterate through CI, politely.** Small, focused commits; watch the one job; cross-ref **contributing-workflow** for the PR/commit conventions and CI etiquette.

## The portability layer: never call the OS directly

libtransmission never calls `open`/`read`/`stat`/`close`/`opendir` directly. It calls the
**`tr_sys_*`** wrappers declared in `libtransmission/file.h` and implemented twice:
`file-posix.cc` (POSIX) and `file-win32.cc` (Win32). `file.h` picks the types per platform:

```cpp
#ifndef _WIN32
using tr_sys_file_t = int;               // fd;    TR_BAD_SYS_FILE = -1
using tr_sys_dir_t  = void*;
#else
using tr_sys_file_t = HANDLE;            // TR_BAD_SYS_FILE = INVALID_HANDLE_VALUE
using tr_sys_dir_t  = tr_sys_dir_win32*; // opaque
#endif
```

**Rule:** if you need a file operation, use `tr_sys_file_*` / `tr_sys_path_*` / `tr_sys_dir_*`
(e.g. `tr_sys_file_open`, `tr_sys_file_read_at`, `tr_sys_file_write_at`, `tr_sys_file_close`,
`tr_sys_path_get_info`, `tr_sys_path_rename`, `tr_sys_path_remove`, `tr_sys_dir_open`,
`tr_sys_dir_read_name`). If the wrapper is missing something you need, add it to **both**
`file-posix.cc` and `file-win32.cc` — never `#ifdef _WIN32` a raw Win32 call into portable code.
The full API list and the `HANDLE`-vs-`int` gotchas are in **`references/portability-layer.md`**.

Related Win32-only modules that follow the same "one file, native API inside" pattern:
`subprocess-win32.cc` (`CreateProcessW`), `watchdir-win32.cc` (`ReadDirectoryChangesW`),
`platform.cc` (`SHGetKnownFolderPath`). Note there is **no** `watchdir-posix.cc`: the POSIX
watchers are `watchdir-inotify.cc`/`watchdir-kqueue.cc`/`watchdir-generic.cc`.

### The UTF-8 ⇄ UTF-16 boundary (the heart of Windows path handling)
The whole codebase speaks **UTF-8 `std::string`/`std::string_view`**. Win32's `…W` APIs speak
**UTF-16 `std::wstring`**. Convert *only* at the syscall boundary, using the two helpers in
`string-utils.cc` (declared in `string-utils.h`):

- `tr_win32_utf8_to_native(std::string_view) -> std::wstring` — wraps `MultiByteToWideChar(CP_UTF8, …)`
- `tr_win32_native_to_utf8(std::wstring_view) -> std::string` — wraps `WideCharToMultiByte(CP_UTF8, …)`

`file-win32.cc` layers path fixups on top: `path_to_fixed_native_path` converts `/`→`\` and squashes
duplicate separators, and `path_to_native_path` wraps it to prepend the `\\?\` long-path prefix
(`\\?\UNC\` for UNC paths) so paths can exceed the ~260-char `MAX_PATH` limit. **Do not** hand-build
native paths or call `…A` ANSI
APIs — round-trip through these helpers so non-ASCII filenames survive. `/utf-8` (a compiler flag,
below) guarantees the *source* string literals are UTF-8 to begin with.

## MSVC-vs-gcc breakage patterns (mined from history)

These are real fixes in this tree — grep them with `git show <hash>`. Treat them as the shortlist
of "what usually breaks":

- **Missing include / `ssize_t`.** GCC/Clang leak transitive headers; MSVC does not. `ssize_t` is
  POSIX-only — in this repo it appears **only** in `file-posix.cc`/`subprocess-posix.cc`, never in
  portable code. Use `int64_t`/`size_t`, and include what you use.
- **`/utf-8` (`e2b6dc48b` build: set /utf-8 flag when using MSVC).** Without it MSVC decodes
  sources and encodes literals in the local ANSI codepage, corrupting our UTF-8 strings. Set in
  `CMakeLists.txt`; know it exists so you don't reintroduce codepage-dependent literals.
- **`/Zc:__cplusplus` (`4f7b932fe` build: correctly set MSVC `__cplusplus`).** MSVC reports
  `__cplusplus == 199711L` unless told otherwise. Any feature check written as `#if __cplusplus >= …`
  (e.g. `macros.h`) is a trap on MSVC — this flag makes the macro honest.
- **`if constexpr` fall-through → `C4702` (`ae44cacc3` chore: silence MSVC C4702 …).** A sequence of
  independent `if constexpr (…) return …;` blocks with a trailing fallback makes MSVC see
  unreachable code under `/W4`. Chain them as `if constexpr {…} else if constexpr {…} else {…}`.
- **`min`/`max` macros (`123a8decc` Define WIN32_LEAN_AND_MEAN and NOMINMAX …).** `<windows.h>`
  defines `min`/`max` macros that shred `std::min`/`std::max`. Defended by `-DNOMINMAX` in
  `CMakeLists.txt`; if you add a header that pulls in raw `<windows.h>`, keep `NOMINMAX` intact.
  Same class of bug: `platform.cc` has to `#undef small` because `rpcndr.h` `#define`s `small`,
  colliding with the bundled `third-party/small` library.
- **Qt5-vs-Qt6 API drift (`b68fc15eb` fix: FTBFS on MSVC Qt5).** Only the Windows CI builds Qt5
  from source, so Qt5-only breaks surface there first (e.g. `QByteArray` wants `int`, not
  `qsizetype`, pre-Qt6). Guard with `#if QT_VERSION < QT_VERSION_CHECK(6,0,0)`. See **qt-client**.
- **`errno` vs `WSAGetLastError` (`58c4386dd` fix: correctly get Windows socket error codes).**
  Winsock errors do **not** land in `errno`. Use the `sockerrno` / `set_sockerrno()` macros from
  `net.h`, never bare `errno`, around socket calls.

### There is no DLL-export machinery — do not add any
`libtransmission` is a **static** library (`add_library(… STATIC)` in
`libtransmission/CMakeLists.txt`). There is **no** `__declspec(dllexport)`, no `TR_EXPORT`/
`*_EXPORT` macro, and none in `macros.h`. If an MSVC link error tempts you toward `dllexport`
guards, that is the wrong fix here — look for a missing source file in the target or a missing
system lib instead.

## Sanity-checking MSVC compatibility from Linux

You cannot run `cl.exe`, but the repo gives you leverage:

- **Read the `_WIN32` paths.** ~38 files in `libtransmission/` carry `_WIN32` guards. When you edit
  a guarded region, read *both* branches — a change that compiles the POSIX branch says nothing
  about the Win32 branch. `grep -rl '_WIN32' libtransmission` finds them.
- **Include-what-you-use.** The single highest-yield habit: every name you use needs its own
  `#include`. This is free to check on Linux and fixes the majority of MSVC FTBFS.
- **Respect the CMake flags as constraints** (advice, verify in `CMakeLists.txt`): code targets
  **Windows Vista** (`_WIN32_WINNT=0x0600`) — no newer-API assumptions; MSVC compiles at `/W4` but
  only the GTK target adds `/WX`, so `/W4` warnings won't fail the `windows` job — still keep
  narrowing conversions and unreachable branches out; designated initializers must be **in
  declaration order** (MSVC and GCC hard-error, Clang only warns — a Clang-only local build can slip
  this through to CI).
- **Prefer standard C++ and the existing wrappers** over anything platform-specific; if you must go
  native, put it behind `tr_sys_*` or a `*-win32.cc` sibling, not an inline `#ifdef`.

When in doubt, push a small commit and **watch the specific Windows job** rather than guessing —
the CI *is* your Windows box.

## See also

- **building** — CMake options (`ENABLE_*`, `USE_SYSTEM_*`, `ENABLE_WERROR`), the build/ dir, reproducing CI locally.
- **testing** — how ctest runs (the `run-tests` action drives the `Test` step on the Windows runner too).
- **third-party-deps** — how bundled libs are wired; `references/ci.md` covers the Windows-specific from-source dep build.
- **contributing-workflow** — branch/commit/PR conventions and CI etiquette for the iterate-through-CI loop.
- **release-engineering** — the AppVeyor signed-MSI release pipeline.
- **qt-client** — Qt5/Qt6 version guards that surface first on Windows.
