# Reproducing CI builds locally

CI is `.github/workflows/actions.yml` (plus `codeql.yml` for CodeQL and `webapp.yml` for the web
UI). Read this when a GitHub Actions check is red and you want to run the *same* build locally.
The CMake-based jobs configure with `-G Ninja` into a build dir named **`obj`** (not `build`); most
of them cache compiles with **ccache** (`hendrikmuhs/ccache-action@v1`, keyed by `github.job`, plus
`-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`). The `clang-tidy` jobs are
the ones that check out submodules shallowly (`git submodule update --init --recursive --depth 1`);
most other jobs use `actions/checkout` with `submodules: recursive` (a full checkout). The
`macos-xcodebuild-universal` job uses `xcodebuild` and has no CMake/Ninja/`obj` step at all.

Naming note: PR #102's commit subject is "ci: use sccache on linux and macos", but the change it
landed (and the current workflow) uses **ccache**, not sccache — there is no `sccache` or
`variant:` anywhere in `.github/`. Trust the file, not the subject line.

## Job map (grep `runs-on:` in actions.yml for the full list)

- `what-to-make` — path-filter gate that decides which downstream jobs/toolkits run.
- `code-style` — runs `./code_style.sh --force` then `git diff --exit-code` (fails on any diff).
  `--check` is only equivalent when clang-format 22 is installed; with any other version `--check`
  silently skips the C/C++ checks (exit 0), so `--force` is what mirrors CI.
- `sanitizer-tests-linux` / `-macos` / `-windows` — `Debug` build then the gtest suite. The
  sanitizer set differs per platform: linux = `address,leak,undefined`; macos = `address,undefined`
  (no LeakSanitizer); windows = MSVC `/fsanitize=address` only (ASan, no LSan/UBSan).
- `clang-tidy-linux` / `clang-tidy-windows` — changed-lines clang-tidy (see below).
- `macos-xcodebuild-universal`, `macos-cmake-universal`, `macos` — Apple builds (out of scope here;
  see the **macos-client** skill, `docs/Building-Transmission.md`, and `Transmission.xcodeproj`).
- release/packaging & platform matrix: `windows`, `alpine-musl`, `debian`, `fedora`,
  `ubuntu-24-04-arm64`, `freebsd`, `openbsd`, `netbsd`, `android`, `cxx23`, `crypto`,
  `utp-disabled`, `make-source-tarball`. Several set `-DENABLE_WERROR=ON`.

## Sanitizer build (mirror `sanitizer-tests-linux`)

Configures with clang, ASan/LSan/UBSan, and bundles the not-packaged libs:
```bash
cmake -S . -B obj -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS='-gdwarf-4 -fno-omit-frame-pointer -fsanitize=address,leak,undefined' \
  -DCMAKE_CXX_FLAGS='-gdwarf-4 -fno-omit-frame-pointer -fsanitize=address,leak,undefined' \
  -DENABLE_GTK=OFF -DENABLE_QT=ON -DRUN_CLANG_TIDY=OFF \
  -DUSE_SYSTEM_DEFAULT=ON \
  -DUSE_SYSTEM_DHT=OFF -DUSE_SYSTEM_SIGSLOT=OFF -DUSE_SYSTEM_SIMDUTF=OFF \
  -DUSE_SYSTEM_SMALL=OFF -DUSE_SYSTEM_UTP=OFF -DUSE_SYSTEM_WIDE_INTEGER=OFF
cmake --build obj --target all-tests
```
Then run the tests (see the **testing** skill). A green local `all-tests` under these flags is the
fastest way to confirm a sanitizer-job fix.

## Changed-lines clang-tidy (mirror `clang-tidy-linux`)

CI does **not** run tidy over the whole tree — it runs LLVM's `clang-tidy-diff.py` against the
merge base so only your changed lines are checked (composite action
`.github/actions/clang-tidy-diff`). Per-directory `.clang-tidy` files are auto-discovered. Key
facts to reproduce it:

- Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and `-DRUN_CLANG_TIDY=OFF` (tidy is driven
  by the diff script, not by CMake here). The matrix splits into cells by area, each with a
  `-regex` scoping which files it checks:
  - `core`: `(libtransmission|libtransmission-app|tests/libtransmission)/.*`
  - `gtk3`/`gtk4`: `gtk/.*`
  - `qt5`/`qt6`: `(qt|tests/qt)/.*`
- Because it only *parses* changed TUs (never builds the whole target), CI first generates the
  headers those TUs include: the Qt `*_autogen` moc/uic targets and the bundled `ExternalProject`
  `_<lib>` targets that install headers like `dht/dht.h`. If you build the real targets normally,
  those headers already exist.
- The binary is **clang-tidy-22** on Linux (`clang-tidy` on Windows). Install it (LLVM apt or
  `pip install 'clang-tidy'` matching 22) and invoke the same `clang-tidy-diff.py` that ships
  beside it, e.g.:
  ```bash
  git diff -U0 "$(git merge-base origin/main HEAD)" \
    | clang-tidy-diff-22.py -p1 -path obj -clang-tidy-binary clang-tidy-22 \
        -regex '(libtransmission|libtransmission-app|tests/libtransmission)/.*'
  ```
  (`-path obj` points at the dir holding `compile_commands.json`.) Simpler but slower: reconfigure
  with `-DRUN_CLANG_TIDY=ON` and rebuild the dirs you changed.

Simplest of all: run the same `code-style`/tidy locally via the CMake targets `check-format`,
`format`, or a `-DRUN_CLANG_TIDY=ON` throwaway build, and keep your day-to-day `build/` dir at
`-DRUN_CLANG_TIDY=OFF`.
