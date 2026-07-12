# CI reference — the four GitHub Actions workflows

Read this when a CI check fails and you need to know which workflow owns it, what
it gates, or how to reproduce the failure locally. All paths are under
`.github/workflows/`. Verified against the tree on 2026-07-12; if a job name here
no longer matches `grep -nE '^  [a-z0-9-]+:' .github/workflows/actions.yml`, trust
the file.

## 1. `actions.yml` — workflow name **"Sanity"** (the big one, ~1800 lines)

Triggers: `push` and `pull_request` to `main`. Has `concurrency:
cancel-in-progress`, so pushing a new commit to a PR cancels the previous run.

The first job, **`what-to-make`**, diffs your branch against `origin/main`
(`git merge-base`) and emits boolean outputs (`make-core`, `make-qt`, `tidy-core`,
`test-style`, …). Every other job has an `if:` guard on those outputs, so a
docs-only or web-only PR skips the compile/test matrix entirely. **On a push to
`main` all guards are forced true** (`is-main-push == 1`) — everything runs
post-merge even if it was skipped on the PR. That is why a PR can be green and the
`main` build still break: the PR only built the areas it touched.

Full job list (each is one required-ish check; the `tidy-*` and platform jobs
self-skip when unaffected):

| Job | Gates |
| --- | --- |
| `what-to-make` | Path-diff dispatcher; sets every other job's `if:` |
| `code-style` | Runs `./code_style.sh --force`; fails if `git diff` is non-empty. Uploads `code-style.diff` artifact |
| `sanitizer-tests-linux` / `-macos` / `-windows` | ASan/UBSan test builds |
| `clang-tidy-linux` | Matrix `core, gtk3, gtk4, qt5, qt6`; diff-only tidy (see below) |
| `clang-tidy-windows` | clang-tidy on the Windows toolchain |
| `macos-xcodebuild-universal` | Builds the Xcode project (catches `.pbxproj` breakage) |
| `macos-cmake-universal` / `macos` | macOS CMake build + tests |
| `alpine-musl` | musl libc build |
| `windows` | MSVC build |
| `make-source-tarball` | Builds the `dist` tarball; catches files missing from CMake install |
| `debian` / `fedora` | Distro builds with system libraries |
| `utp-disabled` | Build with µTP off (guards `#if` fences) |
| `android` | Android NDK build of libtransmission |
| `cxx23` | Builds under `-std=c++23` (forward-compat gate) |
| `ubuntu-24-04-arm64` | ARM64 Linux build + tests |
| `freebsd` / `openbsd` / `netbsd` | BSD builds |
| `crypto` | Build against the alternate crypto backend |

### Reproducing Sanity failures locally

- **`code-style`**: `./code_style.sh --check` (report only) or `./code_style.sh`
  (fix in place). CI uses `--force`, which skips the clang-format version guard.
  Locally, if `code_style.sh` prints that it is skipping the C/C++ checks because
  your clang-format is not version 22, install clang-format 22 (`pip install
  clang-format~=22.0`, per the script's own hint) rather than reaching for
  `--force` — forcing a mismatched major version reformats the tree with the wrong
  binary ("other versions give slightly different results") and may still fail CI.
- **`clang-tidy-linux`**: CI runs `.github/actions/clang-tidy-diff` — a wrapper
  around `clang-tidy-diff.py` with `clang-tidy-22` — so it only flags lines you
  changed since the merge base, not the whole file. See the **building** skill for
  the local clang-tidy invocation (configure with `-DRUN_CLANG_TIDY=NO` while
  iterating, then do a full tidy pass before pushing). The `core` cell's regex
  covers `libtransmission`, `libtransmission-app`, and `tests/libtransmission`; it
  generates headers (Qt moc/uic, bundled-lib installs) but never builds the whole
  target — it only parses the translation units you changed.
- **compile/test jobs**: configure + build + `ctest` as in the **building** and
  **testing** skills. You rarely need to reproduce every platform — reproduce the
  one that failed (e.g. `cxx23` = add `-DCMAKE_CXX_STANDARD=23`).
- **From the CLI**: `gh run list --branch <your-branch>`, then
  `gh run view <run-id> --log-failed` to read only the failed step, or
  `gh run watch` to follow a run live. Re-run flaky jobs with
  `gh run rerun <run-id> --failed`.

## 2. `webapp.yml` — workflow name **"webapp"**

Triggers: `push`/`pull_request` to `main`, `paths-ignore: docs/**, .github/**`.
`decide-what-jobs-to-run` checks whether `web/` or `CMakeLists.txt` changed.

- **`code-style`**: `npm --prefix web ci` then `npm --prefix web run lint:fix`
  (oxlint + stylelint + prettier — the JS linter migrated off eslint to oxlint in
  commit `cef646b6a`, but a CSS/SCSS or formatting-only diff comes from stylelint
  or prettier, not oxlint), then fails if `git diff web` is non-empty.
- **`test-generated-files`** (PRs): **rejects any PR that hand-edits the generated
  web bundle** — `web/public_html/transmission-app.{js,css,map,LEGAL.txt}` and
  `web/package.json.buildonly`. Regenerate them instead of editing; see the
  **web-client** skill.
- **`update-generated-files`** (push to `main` only): regenerates the bundle and
  opens an automated `chore: update generated transmission-web files` PR (labels
  `notes:none`, `scope:web`, `type:chore`). You do not commit generated web files
  in a feature PR — this bot does it after merge.

Reproduce locally: `npm --prefix web ci && npm --prefix web run lint:fix` then
`git diff web`.

## 3. `codeql.yml` — workflow name **"CodeQL"** (security scanning)

Triggers: `push`/`pull_request` to `main` (`paths-ignore: docs/**, .github/**`)
plus a weekly `cron` (Mondays 14:29 UTC). Matrix `cpp` + `javascript`. The `cpp`
leg configures with `-DENABLE_TESTS=OFF -DENABLE_NLS=OFF -DRUN_CLANG_TIDY=OFF`,
builds the bundled third-party libs, then the full project, and uploads results to
the Security tab. Findings surface as PR annotations. There is no fast local
equivalent; read the annotation and reason about the flagged path.

## 4. `update-copyright.yml` — housekeeping (not contributor-facing)

Triggers: `cron` on Jan 1 (+ manual `workflow_dispatch`). Uses
`FantasticFiasco/action-update-license-year` to bump copyright years in `COPYING`,
`gtk/Application.cc`, `qt/LicenseDialog.ui`, the macOS `Info.plist*`/`.strings`
files, and `cmake/Transmission.rc.in`, opening a `chore: update copyright years`
PR. You never trigger this by hand; just be aware those PRs are automated.

## Composite actions (shared steps, under `.github/actions/`)

`clang-tidy-diff`, `detect-platform`, `install-deps`, `prepare-deps-win32`,
`run-tests`, `set-test-env`, `setup-vs-env-win32`. `install-deps` is the one to
read if you need the exact apt/brew package list a job installs.
