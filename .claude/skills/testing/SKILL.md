---
name: testing
description: >-
  How to run and write Transmission's C++ unit tests (GoogleTest + ctest). Load this
  WHENEVER you: run the test suite or one test, add or edit a *-test.cc file, wire a new
  test into a tests/*/CMakeLists.txt, touch a test fixture (test-fixtures.h, SessionTest,
  the sandbox, tests/libtransmission/assets sample torrents), see a ctest/gtest failure
  locally or in CI, wonder "why isn't my new test running", need the fastest single-test
  iteration loop (--gtest_filter), or need local runs to predict the CI sanitizer/ctest
  jobs. Covers tests/libtransmission (LT.), tests/libtransmission-app (LTA.), tests/utils
  (UR.), tests/qt (QT.), and the rpc-test.cc ExpectedKeysUnsorted canary. It does NOT cover
  configuring/building (see the building skill) or the web UI's lint tooling.
---

# Testing Transmission

The tests are GoogleTest binaries registered with ctest. `ENABLE_TESTS` is `ON` by
default (`CMakeLists.txt:76`), so a normal configure already builds them. Configuring and
building are the **building skill's** job; this skill assumes you have a build dir. This
repo's is `build/`; CI uses `obj/`. Substitute your own path everywhere below.

`docs/Testing-Transmission.md` is **not** about running unit tests — it only explains how
to download and open CI build artifacts on macOS. There is no in-repo doc for running the
suite locally; this skill is it.

## The layout (four suites, four ctest prefixes)

Each executable is registered by `tr_setup_gtest_target(<target> "<PREFIX>")`
(`cmake/TrMacros.cmake:4`), which calls `gtest_discover_tests` with `TEST_PREFIX`. So every
ctest name is `<PREFIX><GTestSuite>.<GTestCase>`.

| Source dir | Test binary (under `build/`) | ctest prefix | CMake target |
|---|---|---|---|
| `tests/libtransmission/` | `tests/libtransmission/libtransmission-test` | `LT.` | `libtransmission-test` |
| `tests/libtransmission-app/` | `tests/libtransmission-app/libtransmission-app-test` | `LTA.` | `libtransmission-app-test` |
| `tests/utils/` | `tests/utils/transmission-remote-test` | `UR.` | `transmission-remote-test` |
| `tests/qt/` | `tests/qt/qt-test-{options-dialog,prefs,session}` | `QT.` | `qt-tests` |

Notes that matter:
- `tests/libtransmission-app/` is the newer `tr::app` shared-client layer (RpcClient, Prefs,
  converters) that Qt/GTK build on. New shared client logic gets tested here, not in a
  GUI-specific suite. See the codebase-map skill for what lives in `libtransmission-app/`.
- `tests/utils/` also registers non-gtest ctest cases via `AddShowTest(...)` that diff
  `transmission-show` output against `tests/utils/assets/*.show` golden files.
- Qt tests are wired differently — `add_trqt_test()` (`tests/qt/CMakeLists.txt`) makes **one
  executable per file** and calls `add_test(NAME QT.<name> ...)` directly, with
  `QT_QPA_PLATFORM=offscreen` on Linux. There is no single qt-test binary.
- `tests/assets/` at the top level holds only `benc2cpp.py`. The **sample torrents** live in
  `tests/libtransmission/assets/` (see Fixtures below).

## Running

**Whole suite / a subset — ctest.** ctest's `-R` matches a regex against the *full* name
(prefix included). Escape the dots.

```
ctest --test-dir build -j "$(nproc)" --output-on-failure      # all of it, like CI
ctest --test-dir build -N                                      # list every test, don't run
ctest --test-dir build -R 'LT\.Session'                        # one suite
ctest --test-dir build -R 'LT\.History\.recentHistory'         # one case
ctest --test-dir build --rerun-failed --output-on-failure      # just what failed last time
```

**One test, fastest loop — run the gtest binary directly.** This skips ctest's
per-test process spawn and discovery, and it takes gtest flags. `--gtest_filter` uses the
**gtest** name `Suite.Case` (NO `LT.` prefix — that prefix is a ctest-only decoration).

```
cmake --build build --target libtransmission-test \
  && ./build/tests/libtransmission/libtransmission-test --gtest_filter='History.recentHistory'
```

Handy gtest flags: `--gtest_list_tests`, `--gtest_filter='Session*.*'`,
`--gtest_repeat=100 --gtest_break_on_failure` (flake hunting), `--gtest_shuffle`. Build only
the target you're iterating on (`libtransmission-test`, `libtransmission-app-test`,
`transmission-remote-test`, or `qt-tests`); the umbrella target `all-tests` builds
everything and is what CI builds before the full ctest run.

Beware: the gtest **suite name** is whatever the `TEST(Suite, …)` / `TEST_F(Fixture, …)`
macro says — often *not* the filename. `history-test.cc` declares `TEST(History, …)`, so it
is `LT.History.recentHistory` in ctest and `History.recentHistory` in `--gtest_filter`. Use
`--gtest_list_tests` or `ctest -N` when unsure.

## Writing a test

Plain unit test (no session): include `<gtest/gtest.h>` plus the header under test and use
`TEST(Suite, Case)` with `EXPECT_*`/`ASSERT_*`. `tests/libtransmission/history-test.cc` is
the minimal template. Keep the copyright header (any `*-test.cc` shows the exact block).

Anything needing a live `tr_session`, a scratch directory, or a real torrent uses the
fixtures in `tests/libtransmission/test-fixtures.h` (namespace `tr::test`). The chain:

- `SandboxedTest` — gives each test a private temp dir, auto-`rimraf`'d on teardown. Use
  `sandboxDir()` and the `createFileWithContents(path, data)` helpers. Nothing touches the
  real config dir or `$HOME`.
- `SessionTest : SandboxedTest` — adds a `tr_session* session_`, built in `SetUp()` from a
  `settings()` map and closed in `TearDown()`. To customize the session, do **not** try to
  override `settings()` — it is non-virtual (`test-fixtures.h:480`), so a derived version only
  shadows it and the base `SetUp()` still calls its own, silently ignoring you. Instead give
  your fixture its own `SetUp()` that grabs `settings()` (it lazily builds the `tr::Settings`
  map on first call), `insert_or_assign`s the keys you need, then calls `SessionTest::SetUp()`.
  `tests/libtransmission/move-test.cc:40-51` is the exemplar. Helpers:
  `zeroTorrentInit(state)` and `zeroTorrentMagnetInit()` add a canned torrent to `session_`.
  Write `TEST_F(SessionTest, foo)`, or alias it: `using MyTest = tr::test::SessionTest;`
  (see `tests/libtransmission/torrents-test.cc`).

Sample torrents/resume files live in `tests/libtransmission/assets/` and are reachable via
the `LIBTRANSMISSION_TEST_ASSETS_DIR` compile definition (set in
`tests/libtransmission/CMakeLists.txt`) — e.g. `tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR,
'/', filename }`. Add a new fixture torrent there and reference it the same way. The
`libtransmission-app` suite has its own separate `tests/libtransmission-app/test-fixtures.h`.

### Wire it into CMake — this step is mandatory, not optional

A `*-test.cc` file that isn't listed in a `CMakeLists.txt` is **silently never compiled or
run**. Proof: `tests/libtransmission/dns-test.cc` is committed but absent from every
CMakeLists, so it has never executed. Do not assume "the file is in the folder" means it runs.

- **libtransmission:** add the filename to the `target_sources(libtransmission-test PRIVATE
  …)` list in `tests/libtransmission/CMakeLists.txt` (roughly alphabetical). Exemplar:
  `47eb4ee2b refactor: dedicated class for torrent queue` added `torrent-queue-test.cc` as a
  one-line addition.
- **libtransmission-app:** add to `target_sources(libtransmission-app-test …)` in
  `tests/libtransmission-app/CMakeLists.txt`. Exemplar: `a62413a66 refactor: migrate
  RpcClient, RpcQueue to tr::app` added `rpc-client-test.cc`.
- **qt:** add `add_trqt_test(foo-test.cc)` AND list `qt-test-foo` under the `qt-tests`
  `add_custom_target(... DEPENDS ...)` in `tests/qt/CMakeLists.txt`.

After editing CMake you must re-run the build so CMake reconfigures and ctest re-discovers
the new cases.

## rpc-test.cc ExpectedKeysUnsorted — the settings canary

`tests/libtransmission/rpc-test.cc` (the array starts at line 584) hardcodes
`ExpectedKeysUnsorted` — every `tr_quark` key the `session-get` RPC is expected to return.
The test builds a set from it and `set_difference`s it against the keys actually returned,
failing on any **missing** or **unexpected** key (lines ~651-665). So adding, renaming, or
removing a session setting that surfaces over RPC breaks this test until you update the
array. That is by design: it forces the session, RPC, and this list to stay in sync. A
related guard, `LTA.PrefsTest.defaultConstructorMatchesEverySessionDefault`, checks the app
Prefs defaults against every session default. If you're changing a session preference, expect
to touch many files spanning core, RPC, and every client (GTK/Qt/web/macOS/CLI) plus their
tests; this canary and the `LTA.` mirror are two of the spots people forget.

## Web UI

There are no web unit tests — `web/package.json` defines only lint scripts
(`npm --prefix web run lint` = oxlint + stylelint + prettier; `lint:fix` autofixes).
Generated web assets (`transmission-app.*`) are rebuilt by CI, not by hand — see the
`webapp.yml` notes in `references/ci-and-suites.md`, and never commit them in a PR.

## Making local green predict CI green

CI (`.github/workflows/actions.yml`) builds the `all-tests` target in Debug, then the
`.github/actions/run-tests` action runs exactly `ctest --test-dir obj -j <nproc>
--output-on-failure`. So `cmake --build build --target all-tests && ctest --test-dir build
-j "$(nproc)" --output-on-failure` reproduces the pass/fail set.

The catch: the `sanitizer-tests-{linux,macos}` jobs build with clang and
`-fsanitize=address,leak,undefined`. (A third job, `sanitizer-tests-windows`, runs MSVC
AddressSanitizer only — address, no leak/UB — under `RelWithDebInfo`; a Windows-only ASan
failure comes from there.) A test can pass in your plain build but fail CI on a
leak or UB the sanitizers catch. When touching memory/lifetime-sensitive code, configure a
separate sanitized build dir (clang + those flags — see `references/ci-and-suites.md` for the
exact configure line and the ASan option CI sets) and run ctest there before pushing. The
separate `code-style` CI job (gated by the `test-style` output flag) runs `./code_style.sh --force` and fails on any diff — that's
clang-format formatting only (clang-tidy is a separate mechanism: the `RUN_CLANG_TIDY` build
option and the `clang-tidy-diff` action / `clang-tidy-{linux,windows}` jobs), owned by the
**building** skill, not this one.

For the exhaustive per-suite inventory, the qt-test detail, and the full CI job matrix +
sanitizer configure command, read `references/ci-and-suites.md`.
