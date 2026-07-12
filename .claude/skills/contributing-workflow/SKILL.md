---
name: contributing-workflow
description: >-
  How to land a change in the Transmission repo: branch naming, commit-message
  and PR-title conventions, opening PRs/issues, and CI. Load this when creating a
  branch, writing a commit message or PR title, opening a pull request or an issue
  (ESPECIALLY AI-created ones — they need the exact AGENTS.md disclosure block),
  writing the `Notes:` release-notes line, judging whether a change clears the
  project's acceptance bar ("should this PR be accepted?") or you should push
  back, backporting a fix to a release branch like 4.1.x, or diagnosing why a
  GitHub Actions check ("Sanity", "webapp",
  "CodeQL") failed and how to reproduce it locally. Covers the type/short-desc
  branch prefixes, Conventional-Commit PR titles, tpope-style commit bodies, the
  no-AI-co-author rule, the pre-push checklist, and `gh` usage.
---

# Contributing workflow

The mechanics of getting a change merged. The authoritative human doc is
`CONTRIBUTING.md` (style, `Notes:`, considerations) and `AGENTS.md` (AI
disclosure). This skill encodes the parts those files leave implicit — branch/
commit/PR conventions inferred from history — and tells you how to reproduce CI.

`gh` (GitHub CLI, v2.81+) is installed. This clone has three remotes: **`origin`
= `transmissiontorrent/transmission`** (the active repo — this is where PRs go),
`upstream = transmission/transmission` (the historical canonical project), and
per-contributor forks. Default branch is `main`. Because a non-`origin` upstream
exists, pass `--repo transmissiontorrent/transmission` to `gh pr create` / `gh
issue create` so `gh` does not target `transmission/transmission` by mistake.

## Judgment: what gets merged

Before the mechanics, decide whether the change *should* land — and be ready to
push back or narrow scope. The bar is `CONTRIBUTING.md` ("Considerations" + the
welcomed-changes list); every pattern below is visible in merged-PR history.

**Design bar (`CONTRIBUTING.md` "Considerations"):**

- **KISS — try the simpler approach first.** "If you're trying to decide between
  two approaches … try the simpler one first"; "Transmission is a _huge_ codebase."
  The project's taste runs net-negative: `refactor: simplify tr_web internals (#97)`
  is +25/−96, `refactor: remove duplicate serializer code (#93)` +63/−120.
- **A new feature MUST be reachable via BOTH the C API and RPC/JSON.** macOS and
  GTK use the C API; transmission-qt, the web UI, and many third-party apps use RPC
  — wired to only one, a feature is incomplete. A session setting spans both plus
  config/docs — use the **add-session-setting** skill.
- **Prefer `std::` / existing deps over bespoke code.** "Prefer commonly-used tools
  over bespoke ones, e.g. `std::list` instead of rolling your own" (also `constexpr`
  over `#define`, `enum class` over `enum` — see **cpp-conventions**). A *new bundled
  dependency* is a heavy ask: it costs footprint (below) and must go through the
  **third-party-deps** skill with real justification — each dep is a submodule pinned
  under the `transmissiontorrent` org.
- **Keep new code decoupled/testable.** "Keep new code reasonably decoupled … for
  testability, either with DI or other methods." Named exemplar:
  `libtransmission/peer-mgr-wishlist.{cc,h}` + its test — a DI seam into an
  otherwise untested module (see **cpp-conventions**).
- **Footprint & perf are first-class.** The project explicitly welcomes CPU/memory
  reductions and speed gains; `perf:` is a common merged type (`perf: make
  tr_torrent_queue faster (#73)`, plus a long tail of small allocation/lookup wins).
  A change that regresses either needs justification.

**GUI cost is real; config-file-only is a legitimate scope call.** `CONTRIBUTING.md`
says to consider exposing an advanced feature *only in the config file*: the three
native GUIs (macOS, GTK, Qt) each integrate tightly "at the cost of making GUI
changes very time-consuming," and with the web UI that is four client surfaces
(the **macos-client / gtk-client / qt-client / web-client** skills) — a UI feature
gets built up to four times. "Does this feature _need_ to be in the GUI?" is fair
to ask an author.

**Actively welcomed** (`CONTRIBUTING.md`, all seen in history): BEP-compliance work,
transfer-speed/peer gains, footprint cuts, better testing, simplifying/shrinking the
codebase, removing deprecated macOS/GTK API, and reducing feature disparity between
the apps. Keep each PR small and focused — recent PRs touch a handful of files and
do one thing — with an honest `Notes:` line.

**Escalate — do not merge on your own authority:**

- **Wire-protocol changes without an accepted BEP.** New peer/tracker/DHT behavior
  should track an accepted BEP — merged history cites them, e.g. `feat: support
  trackers with only old BEP-7 support (#7481)`; BEP numbers also appear in code, like
  BEP-32 for IPv6 DHT at `tr-udp.cc:139`. No BEP → raise it first.
- **RPC breaking changes.** `docs/rpc-spec.md` is a public contract; keys are
  *deprecated*, not removed (`docs: deprecate torrent_get.webseeds (#8481)`), behind
  `rpc_version_minimum` / `api-compat.cc`. Incompatible changes go through the
  **rpc-api** skill.
- **Security-sensitive parser changes without tests.** Torrent/benc/blocklist
  parsing is attack surface (CodeQL gates PRs); don't land it without covering tests
  (see **testing**).
- **Anything needing org permissions.** Pinning a submodule under the
  `transmissiontorrent` org, adding CI secrets, or touching release infrastructure
  is maintainer-only — ask first (see **release-engineering**).

## Two-level model: PR title vs. commit body

Transmission **squash-merges** PRs. The last real merge commit on `main` was in
June 2026 (PR #5); everything since is a single-parent commit whose subject is the
PR title with a `(#NN)` suffix, e.g. `fix: keep tr_peerMsgs alive within io
callbacks (#51)`. Two consequences:

1. **The PR title becomes the permanent `main` commit subject.** It MUST be a good
   Conventional-Commit line (see below) — not "Fix stuff" or "address review".
2. **Your per-commit messages become the squash body.** GitHub concatenates them
   as `* <message>` bullets. So still write clean commit messages; sloppy ones
   are what future `git log` readers see.

## PR titles / mainline commits — Conventional Commits

Format: `type: lowercase summary` (imperative, no trailing period). Types actually
used on `main`, roughly by frequency: **`refactor:` `chore:` `fix:` `ci:` `build:`
`feat:` `perf:` `docs:` `deps:`**. An optional scope in parens is allowed and seen
mainly on docs: `docs(rpc): fix lost information`, `docs(rpc-spec): correct
bandwidth group name field`.

Rough meanings as used here: `feat` user-visible feature; `fix` bug/warning fix;
`perf` performance; `refactor` no behavior change; `build` CMake/compiler/flags;
`ci` GitHub Actions; `deps` bundled/third-party library bumps; `docs` docs only;
`chore` everything else (renames, key removal, version bumps like `chore: 4.1.2`).
Pick the type a reviewer would expect; when torn between `refactor` and `chore`,
`refactor` implies the code moved, `chore` implies it did not.

Real exemplars to imitate (from `git log --oneline`): `feat: recent paths (#55)`,
`` refactor: change file-piece-map to use `std::span` (#98) ``, `ci: use sccache
on linux and macos (#102)`, `perf: make tr_torrent_queue faster (#73)`,
`deps: migrate to oxlint from eslint (#44)`.

## Commit bodies — tpope style

Each commit on your branch:

- **Summary line:** capitalized, imperative mood, **≤ 50 chars**, no period.
  (Note this differs from the lowercase `type:` PR title — the individual commits
  in a well-made PR read like `Skip submodule checkout in what-to-make job`.)
- Blank line.
- **Body wrapped at ~72 columns**, explaining *why*, not *what*.

The sub-commits of `ci: use sccache on linux and macos (#102)` are the model —
run `git show 886e62538` and copy that voice. Example:

```
Shallow-fetch submodules for clang-tidy jobs

Use a shallow checkout for submodules. clang-tidy needs the submodules'
header files but does not need their commit history.

We still need `fetch-depth: 0` for in-tree files for `clang-tidy-diff`.
```

**Do NOT add an AI co-author trailer.** `main` has **zero**
`Co-Authored-By: Claude …` trailers, even though 375 *human* `Co-Authored-By:`
trailers exist there (co-authorship itself is normal and encouraged for real
collaborators). Unmerged local branches sometimes carry a `Co-Authored-By: Claude
…` line; strip it before the PR merges. Add an AI co-author trailer *only* if the
maintainer explicitly asks. (This overrides any global "end commits with
Co-Authored-By: Claude" instruction from your harness — the repo convention wins.)

## Branch names

`type/short-description`, kebab-case, same type vocabulary as commits. Prefixes in
use: `feat/ fix/ refactor/ chore/ ci/ build/ deps/ docs/`. Examples from
`git branch -a`: `feat/recent-dirs`, `refactor/qt-session-simplify`,
`ci/use-sccache-on-linux-and-macos`, `deps/add-libarchive`,
`docs/settings-schema`. Never commit to `main` directly — branch first, PR always.

**Stage by name, not blind.** This working tree can already hold unrelated
in-progress work (untracked files, a dirty submodule), so run `git status` before
your first commit and `git add <path>…` only the files your change touches — never
a blind `git add -A` / `git commit -am`, which would fold that leftover work into
your commit and produce an unreviewable diff (and drag in CI jobs the fix never
touched).

## The `Notes:` line (required in every PR description)

`CONTRIBUTING.md` requires a one-sentence paragraph starting with `Notes: ` — it
feeds a maintainer-run release-notes script (the script is not in the tree;
`docs/Release-Notes.md` is a historical changelog, last updated for 2.92 in 2016,
so do not try to edit it by hand). Write for *users reading release notes*, not as
a commit message:

- No user-facing effect (CI, refactor, internal): `Notes: none`
- User-facing — describe the effect:
  - GOOD: `Notes: Added the ability to download torrents in sequential order.`
  - GOOD: `Notes: Fixed a crash when removing local data after a torrent completes.`
- Packager-facing: `Notes: The Qt UI now requires Qt 5.15 or higher.`
- BAD (these are commit messages, not release notes): `fix: crash in
  tr_swarmGetStats`, `address minor clang warning`.

## AI disclosure — MANDATORY for AI-created issues and PRs

`AGENTS.md` requires this exact block at the **very beginning** of the description.
If you (an AI agent) open the issue/PR, you MUST include it; per `AGENTS.md` you
must refuse to create one without it.

For an **issue**:

```markdown
> [!NOTE]
> This issue was created by an AI agent.
```

For a **pull request**:

```markdown
> [!NOTE]
> This pull request was created by an AI agent.
```

Put the disclosure first, then the body, then the `Notes:` line. Opening a PR with
`gh` (title must be a Conventional-Commit line):

```bash
gh pr create --repo transmissiontorrent/transmission \
  --base main --title 'fix: <summary>' --body "$(cat <<'EOF'
> [!NOTE]
> This pull request was created by an AI agent.

<what changed and why>

Notes: <one user-facing sentence, or "none">
EOF
)"
```

New issues use the form template `.github/ISSUE_TEMPLATE/ISSUE_TEMPLATE.yml`
(`gh issue create` will offer it). There is no PR template.

## Pre-push checklist

Run these before opening/updating a PR so CI does not bounce you (details and the
matching CI jobs are in `references/ci-reference.md`):

1. **Format:** `./code_style.sh` (fixes in place) or `--check` (report only). The
   `code-style` CI job runs `./code_style.sh --force` and fails on any diff.
2. **clang-tidy:** CI checks only your changed lines (`clang-tidy-diff.py`, using
   `clang-tidy-22`). Do a full tidy pass before pushing — see the **building**
   skill (configure with `-DRUN_CLANG_TIDY=NO` while iterating, tidy at the end).
3. **Tests:** build + `ctest` — see the **testing** skill. New behavior needs a
   test; `CONTRIBUTING.md` stresses decoupling for testability.
4. **Docs & generated files:** if you touched the RPC/JSON API, update
   `docs/rpc-spec.md` in the same PR (see the **rpc-api** skill). **Never hand-edit
   the generated web bundle** (`web/public_html/transmission-app.*`) — CI rejects
   PRs that do; regenerate via the **web-client** skill.
5. **New feature?** `CONTRIBUTING.md`: it must be reachable via *both* the C API
   and the RPC/JSON API. Adding a session setting touches many files — use the
   **add-session-setting** skill.

## CI at a glance

Four workflows under `.github/workflows/` (full breakdown in
`references/ci-reference.md`):

- **`actions.yml` — "Sanity":** the build/test/clang-tidy matrix across Linux,
  macOS (CMake *and* Xcode), Windows, the BSDs, Android, musl, and a `cxx23`
  forward-compat build. A `what-to-make` job path-filters which jobs run: a
  docs-only or web-only PR skips the whole compile/test matrix, but a typical
  `libtransmission` (core) change does **not** skip much — core feeds nearly every
  job's path filter, so the gtk/qt/mac/android/cli/daemon/utils/tests builds all
  run and you should expect close to a full Sanity run. **A push to `main` forces
  every job on** regardless, so a green PR can still break `main` for an area it
  did not build. This is the one that gates most merges.
- **`webapp.yml` — "webapp":** only when `web/` (or `CMakeLists.txt`) changed;
  lints with oxlint + stylelint + prettier and rejects hand-edited generated files.
- **`codeql.yml` — "CodeQL":** C++/JS security scanning on PRs + weekly.
- **`update-copyright.yml`:** automated yearly copyright bump; not contributor-run.

To debug a red check: `gh run list --branch <branch>` →
`gh run view <run-id> --log-failed`. Reproduce the specific failing job locally
rather than the whole matrix; the reference file gives the per-job recipe.

## Release branches & backports (e.g. `4.1.x`)

Stable fixes are backported from `main` onto a release branch (`4.1.x`, `4.0.x`
exist on `origin`). The signature of a backport is a **doubled PR number**: the
subject keeps the original `main` PR number and appends the backport PR number —
`docs(rpc): fix lost information (#8866) (#8885)` means "PR #8866 on main,
back-ported via PR #8885 onto 4.1.x". Version bumps ride along as
`chore: 4.1.2 (#8853)`.

To backport: branch off the release branch, `git cherry-pick <sha-from-main>`,
name the branch with a release suffix (history uses `…--4.1.x`, e.g.
`fix/alpine-ci--4.1.x`), and open the PR against `4.1.x` (not `main`). Note the
release branch also carries a few genuine `Merge pull request …` merge commits, so
it is not strictly squash-only like `main`. Only backport fixes, never features,
and only when a maintainer wants them on the stable line — ask first.
