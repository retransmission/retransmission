# Testing reference: suite inventory + CI details

Read this when you need the full picture beyond the day-to-day loop in SKILL.md — the
per-suite fixture/asset detail, the Qt wiring, and the exact CI jobs and sanitizer command.

## Full suite inventory

### tests/libtransmission/ → `libtransmission-test` (prefix `LT.`)
The core library suite and by far the largest — well over 90% of all registered ctest cases
(~615 of ~665 today; the rest: ~40 `LTA.`, 6 `transmission-show-*` golden tests, 3 `QT.`,
1 `UR.`). Sources are the
`target_sources(libtransmission-test PRIVATE …)` list in
`tests/libtransmission/CMakeLists.txt`. Key facts baked into that CMakeLists:
- `LIBTRANSMISSION_TEST_ASSETS_DIR` is defined to `${CMAKE_CURRENT_SOURCE_DIR}/assets`, so
  test code reads sample torrents from `tests/libtransmission/assets/` at runtime.
- Links `${TR_NAME}` (the `transmission` static lib), `GTest::gtest_main`, `dht::dht`,
  `transmission::fmt-header-only`, `libevent::core`/`extra`, `WideInteger::WideInteger`.
- Compiles with `-Wno-sign-compare` (comment in-file: "patches welcomed").
- On Apple, additionally compiles `utils-apple-test.mm`.
- A companion `subprocess-test` executable is built and made a dependency; the
  `subprocess-test-script.cmd` is copied next to the test binary for the subprocess tests.

Sample assets in `tests/libtransmission/assets/` include real `.torrent` files (ubuntu,
debian, archlinux, gimp, alice_in_wonderland, Android-x86), deliberately malformed ones
(`bad-pieces-key.torrent`, `too-few-pieces.torrent`, `invalid-pieces-length.torrent`,
`bad-utf8-path.torrent`, `unordered-info-dict.torrent`, …), a `.magnet`, a `.resume`, a
`.torrent.added`, and `user-dirs.dirs`. Reuse an existing malformed file before inventing one.

### tests/libtransmission-app/ → `libtransmission-app-test` (prefix `LTA.`)
The `tr::app` shared-client layer. Sources: `converter-tests.cc`, `prefs-test.cc`,
`rpc-client-test.cc`, `test-fixtures.h`. Links `${TR_NAME}-app` and `${TR_NAME}`. Has its
own `test-fixtures.h` (separate from the libtransmission one) with its own
`TransmissionTest`/`Sandbox`. `LTA.PrefsTest.defaultConstructorMatchesEverySessionDefault`
is the app-side mirror of the RPC key canary — it fails when app Prefs defaults drift from
session defaults.

### tests/utils/ → transmission-show golden tests + `transmission-remote-test` (prefix `UR.`)
Two things in `tests/utils/CMakeLists.txt`:
- `AddShowTest(<ctest-name> <basename>)` runs `transmission-show` on
  `assets/<basename>.torrent` via `run_transmission_show.cmake` and diffs stdout against
  `tests/utils/assets/<basename>.show`. These are plain `add_test` cases, not gtest. One is
  intentionally disabled in-file (bittorrent-v2). If you change `transmission-show` output,
  regenerate the affected `.show` golden file.
- `transmission-remote-test` (built only when `transmission-remote` exists) is a gtest
  binary; `TR_REMOTE_EXE` is passed as a compile def so it can exec the real remote binary.

### tests/qt/ → `qt-tests` (prefix `QT.`)
Built only when `ENABLE_QT`. `add_trqt_test(<file>)` in `tests/qt/CMakeLists.txt` creates
**one executable per test file** (`qt-test-options-dialog`, `qt-test-prefs`,
`qt-test-session`), turns on AUTOMOC/AUTORCC, links the `${TR_NAME}-qt-lib` and the
`application.qrc` resources, and registers `add_test(NAME QT.<name> …)`. On Linux each case
runs with `ENVIRONMENT "QT_QPA_PLATFORM=offscreen"` so it needs no display. `session-test`
and `options-dialog-test` additionally link libevent because they drive a real loopback
evhttp server instead of a fake QNetworkAccessManager (`qt-test-fixtures.h`,
`rpc-test-fixtures.h`). To add a Qt test you must both call `add_trqt_test(foo-test.cc)` and
add `qt-test-foo` to the `qt-tests` custom target's `DEPENDS`.

## How ctest names are formed
`gtest_discover_tests(<target> TEST_PREFIX "<PREFIX>" DISCOVERY_TIMEOUT 15)` in
`tr_setup_gtest_target` (`cmake/TrMacros.cmake`) runs the freshly built binary at build time
and registers one ctest entry per gtest case as `<PREFIX><Suite>.<Case>`. Cross-compiling
without an emulator falls back to a single `add_test` per binary. This is why:
- editing a `*-test.cc` requires a rebuild before `ctest -N` shows new cases, and
- `-R` regexes match the prefixed name while `--gtest_filter` uses the bare `Suite.Case`.

## CI jobs (.github/workflows/)

`actions.yml` is the C++ workflow. Relevant jobs, gated by a path-diff step so they only run
when code/tests/CI changed (or on push to `main`):
- **code-style** (gated by the `test-style` output flag) — runs `./code_style.sh --force`, then fails if `git diff` is non-empty and
  uploads the diff as the `code-style.diff` artifact. This is clang-format formatting **only**;
  `code_style.sh` invokes no clang-tidy. clang-tidy is a separate mechanism entirely: the
  `RUN_CLANG_TIDY` cmake option (build-time) and the `.github/actions/clang-tidy-diff` action,
  run by the dedicated `clang-tidy-linux` / `clang-tidy-windows` jobs. Owned by the building
  skill.
- **sanitizer-tests-linux** (`ubuntu-latest`) and **sanitizer-tests-macos** (`macos-26`) —
  configure Debug with clang and ASan+LSan+UBSan, `cmake --build obj --config Debug --target
  all-tests`, then `.github/actions/run-tests`.
- **sanitizer-tests-windows** (`windows-2022`) — MSVC AddressSanitizer **only** (address; no
  leak/UB sanitizers, which MSVC doesn't provide). Built `RelWithDebInfo`, not Debug, because
  Debug's `/RTC1` is ASan-incompatible; compiled with `/fsanitize=address
  /D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION` so the un-instrumented prebuilt
  Qt libs don't trip container-annotation (`annotate_string`/`annotate_vector`) link errors.
  Builds `all-tests` and runs `.github/actions/run-tests` with ASan enabled. A Windows-only
  ASan failure originates here.

`.github/actions/run-tests` boils down to:
```
ctest --test-dir obj -j <nproc> --output-on-failure [--build-config Debug]
```
(plus `catchsegv` on non-sanitized Linux and macOS crash-report dumping on failure). No
magic — the same command against your build dir reproduces CI's pass/fail set.

`webapp.yml` is separate and has no web unit tests. On web changes its `code-style` job runs
`npm --prefix web ci` then `npm --prefix web run lint:fix` and fails on any diff. On **PRs**
the `test-generated-files` job runs **no build** — it is only a `git diff --exit-code` that
**fails the PR if any generated file was touched** (`web/public_html/transmission-app.*`,
`web/package.json.buildonly`); PRs must not change generated files. The actual `npm run build`
+ `generate-buildonly` regeneration runs only in the `update-generated-files` job on **push to
`main`**, which opens an automated bot PR. So never regenerate and commit `transmission-app.*`
in a PR — that is the exact thing CI rejects.

## Reproducing the sanitizer build locally

The Linux CI configure line (trimmed to the load-bearing flags):
```
cmake -S . -B obj-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS='-gdwarf-4 -fno-omit-frame-pointer -fsanitize=address,leak,undefined' \
  -DCMAKE_CXX_FLAGS='-gdwarf-4 -fno-omit-frame-pointer -fsanitize=address,leak,undefined' \
  -DENABLE_TESTS=ON -DRUN_CLANG_TIDY=OFF
cmake --build obj-asan --target all-tests
ctest --test-dir obj-asan -j "$(nproc)" --output-on-failure
```
When Qt is in the build, CI sets `ASAN_OPTIONS=detect_container_overflow=0` (via
`.github/actions/set-test-env`) because Qt libs aren't ASan-instrumented and would trip
false container-overflow reports. See the building skill for the canonical configure
options; only the sanitizer flags above are specific to reproducing this CI job.
