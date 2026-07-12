---
name: release-engineering
description: >-
  How Transmission releases are versioned and cut. Load this whenever you bump
  the version (the five `set(TR_VERSION_*` lines in CMakeLists.txt), touch
  `libtransmission/version.h.in`, `update-version-h.sh`, or the `REVISION`
  file; assemble release notes from PR `Notes:` lines into `news/news-*.md`;
  cut or tag a beta / point release (`4.1.0-beta.5`, `4.1.3`); backport a fix
  onto the `4.1.x` maintenance branch; add a `docs/rpc-spec.md` changelog row
  and bump `rpc_version_semver`; or answer "how do we ship a release", "what is
  4.2.0-dev", "where does the appcast / DMG / MSI come from", "why is
  version.h generated two ways". Much of the actual *publish* (Git tags,
  GitHub Releases, DMG notarization, Sparkle appcast signing, Transifex,
  website) is maintainer-only and off-repo — this skill marks those as
  handoffs rather than inventing steps.
---

# Release engineering

How a Transmission version number is set, how release notes are built, and how
a release is cut. This was the most in-the-maintainer's-head workflow, so read
the caveat first: **the repo only holds half of it.**

## The big caveat: half of a release lives off-repo

Everything below the "REPO STEPS" line is evidenced by files in this tree and
is safe to do from a clone. Everything under "MAINTAINER-ONLY" requires org
permissions or private keys and leaves *no trace in this repo* — do not guess
these; hand them to a maintainer. Off-repo surfaces this repo references but
does not contain:

- **Git tags + GitHub Releases** — no CI job pushes a tag or creates a Release
  (see "What CI builds", below). A human does this. Note the remotes:
  `origin = transmissiontorrent/transmission` (active dev), `upstream =
  transmission/transmission` (historical canonical, where public Releases have
  lived). Which org a given release publishes under is a maintainer decision,
  not repo-evidenced.
- **The release-notes script.** `CONTRIBUTING.md` says PR `Notes:` lines are
  "for the release notes script," but **that script is not in this repo**
  (verified: nothing under `release/`, `extras/`, or anywhere reads PR/`Notes:`
  data). It is a maintainer-local tool. You assemble `news/news-*.md` by hand
  or with that private tool.
- **macOS DMG** — built/signed/notarized in Xcode by the maintainer; published
  DMGs live in the separate **`transmission/transmission-releases`** repo (see
  the raw links in `docs/Previous-Releases.md`), not here.
- **Sparkle auto-update appcast** — `macosx/Info.plist*` points at
  `update.transmission*.com/appcast.xml`, hosted server-side and signed with an
  EdDSA key whose public half is `SUPublicEDKey` in the plist. Private key =
  maintainer.
- **Transifex** translation pull and **the website**. See the `translations`
  skill for the Transifex side.

## Where the version lives: five lines in CMakeLists.txt

`CMakeLists.txt` is the single source of truth. The block starts at
`set(TR_VERSION_MAJOR ...)` (~line 119) and the comment says it plainly:
*"these should be the only five lines you need to change."*

```cmake
set(TR_VERSION_MAJOR "4")
set(TR_VERSION_MINOR "2")
set(TR_VERSION_PATCH "0")
set(TR_VERSION_BETA_NUMBER "") # empty string for not beta
set(TR_VERSION_DEV TRUE)
```

HEAD right now is **4.2.0-dev**. From those five values CMake *derives*
everything else in the same block — do not edit the derived parts by hand:

- **Release type** (mutually exclusive): `TR_VERSION_DEV TRUE` →
  `TR_NIGHTLY_RELEASE`; else non-empty `TR_VERSION_BETA_NUMBER` →
  `TR_BETA_RELEASE`; else `TR_STABLE_RELEASE`.
- **`TR_SEMVER`** / user-agent: `4.2.0`, `4.1.0-beta.5`, `4.2.0-dev`,
  `4.1.0-beta.1.dev` (a dev build *between* beta 1 and 2).
- **Peer-id prefix** (BEP-20): `-TR420Z-` (dev), `-TR410B-` (beta), `-TR4000-`
  (stable) — 7th char `Z`/`B`/`0`.
- **`CFBUNDLE_VERSION`** for the macOS bundle (packs patch+beta into 3 numbers).

The exact derivation math, with worked examples, is in
`references/version-and-artifacts.md` — read it before touching that block.

## How version.h and REVISION get generated (two paths)

`libtransmission/version.h` is **generated, never committed.** It is produced
*two different ways* because there are two build systems (see `macos-client`):

1. **CMake build** — `configure_file(version.h.in version.h)`
   (`libtransmission/CMakeLists.txt:198`) substitutes the `@TR_*@` /
   `#cmakedefine` values computed in the CMakeLists block above.
2. **Xcode build** — a run-script phase runs `sh update-version-h.sh`
   (`Transmission.xcodeproj/project.pbxproj:3490`). That script re-`grep`s the
   same five `TR_VERSION_*` values straight out of `CMakeLists.txt` and emits
   the same `#define`s. So **CMakeLists.txt stays authoritative even for Xcode.**

Both paths also maintain **`REVISION`** — a **gitignored, build-generated**
file (verified: `git check-ignore REVISION` matches; not tracked). It holds the
full HEAD commit sha. In a git checkout the build overwrites it from
`git rev-list --max-count=1 HEAD`; its purpose is to carry the commit into
**source-tarball builds that have no `.git`** (the CI `make-source-tarball` job
configures — regenerating REVISION — then packages, baking the sha in).
`version.h` truncates it to 10 chars for `VCS_REVISION`.

The macOS `Info.plist` gets the version too: the Xcode build C-preprocesses
`macosx/Info.plist` (bare tokens `VERSION_STRING_INFOPLIST` /
`BUILD_STRING_INFOPLIST` come from the `#define`s — that is why `version.h.in`
emits them *unquoted*); the CMake build `@CFBUNDLE_VERSION@`-substitutes
`macosx/Info.plist.in`. Details in the reference.

## The version-bump commit

A version change is a tiny, standalone commit touching only those five lines
(plus a news file for a real release). Real exemplars:

| What | Commit | Touches |
|------|--------|---------|
| Open next dev cycle | `0222d9d46 chore: bump to 4.2.0-dev (#8589)` | `CMakeLists.txt` only (MINOR 1→2, BETA "5"→"", DEV FALSE→TRUE) |
| Cut a beta | `c92b42de9 chore: bump to 4.1.0-beta.4 (#7869)`, `ac5c9e082` (beta.2), `f20fd5e37` (beta.3) | `CMakeLists.txt` (set BETA_NUMBER, DEV FALSE) + news |
| Cut a stable point release | `838877323 chore: 4.1.3 (#8955)` | `CMakeLists.txt` (PATCH 2→3) + `news/news-4.1.3.md` |
| Open a new major's betas | `98cf7d9b3 build: set version to 4.0.0-beta.1 (#3872)` | `CMakeLists.txt` + `news/news-4.0.0-beta-1.md` |

Pattern: a **stable/beta** commit sets `TR_VERSION_DEV FALSE`. A **dev-bump**
back to `TRUE` comes in two flavors: an *interim* one after a beta that flips
*only* DEV (BETA/MAJOR/MINOR/PATCH untouched → `beta.N.dev`, e.g. `3aff760b5`,
`841419d06`), versus the *end-of-train* one that also advances MINOR/MAJOR and
clears `TR_VERSION_BETA_NUMBER` (`0222d9d46`) — don't conflate them (see step 7).
Subjects are Conventional-Commits `chore:` (see `contributing-workflow`).

## Release notes: `Notes:` → `news/news-*.md`

Every PR is asked to add a one-line `Notes: <user-facing sentence>` (or
`Notes: none`) per `CONTRIBUTING.md`. Those lines are the raw material for the
per-release file under **`news/`** — but they live **only in the PR
description**. This repo squash-merges and keeps just the PR's `* subject`
sub-lines, so `Notes:` is **not** in `git log` (0 hits across the 451 commits
since the 4.1 fork). Gathering them means enumerating the merged PRs in range via
`gh pr list` / `gh api` (or the web UI), *not* `git log --grep=Notes` — a much
bigger fan-out (potentially hundreds of PRs since the last stable), though it
needs only public-repo read access, no maintainer creds.

`news/` mechanics (read a real one, e.g. `news/news-4.1.0-beta.2.md`):

- One Markdown file per release: `news/news-<version>.md`. **Name it to match
  the tag**: current convention is `news-4.1.0-beta.5.md`, `news-4.1.3.md`
  (dotted `beta.N`). Older 4.0 betas used `news-4.0.0-beta-1.md` (hyphen) —
  don't copy that.
- Structure: `# Transmission <ver>` intro paragraph, `## What's New`, then an
  optional `### Highlights` plus per-audience `###` sections — All Platforms,
  macOS Client, GTK Client, Qt Client, Web Client, Daemon, transmission-remote —
  each appearing **only when that audience has items** (e.g. `news-4.1.0-beta.2.md`
  has a `### Highlights` but no `### Qt Client`) — each a bullet list of
  one-sentence items with `([#PR](https://github.com/transmission/transmission/pull/PR))`
  links, and a `## Thank You!` contributor roll-up. A beta aggregates
  *everything since the last stable* (the 4.1.0-beta.1 file is ~77 KB); a point
  release lists only its handful of fixes. **Caveat:** `main` and `4.1.x` have
  diverged (merge-base `11d3fc6f5`; some 4.1.x fixes are on `main` under
  *different* PR numbers, some perhaps not at all), so "since the last stable" is
  **not** a clean `git log 4.1.3..main` — scope it against the `4.1.x` tip.
- `news/news-5.0.0-draft.md` shows the maintainer stages the *next major's*
  upgrade-path notes early, marked `(DRAFT PLEASE IGNORE)`.
- Release-notes commits are their own PRs: `c3a810607 docs: add 4.1.0-beta.1
  notes (#7288)`, `f360d5cc0 docs: add release notes for 4.1.0-beta.5 (#8065)`,
  `f0c13ecc9 docs: add 4.0.0 release notes (#4706)`. Point-release notes cut on a
  branch are *supposed* to get **added back to `main`** (exemplar: `1bcc95100
  chore: add 4.0.5 and 4.0.6 release notes to main branch (#7282)`) — but this
  lags: as of 2026-07 none of the 4.1.0–4.1.3 notes are on `main` (its `news/`
  stops at `news-4.1.0-beta.5.md`; 4.1.0's stable notes sit on the unmerged
  `origin/docs/release-notes-4.1.0`). Backfilling them first makes `main`'s
  `news/` reflect reality before you scope a new beta.

`docs/Release-Notes.md` and `docs/Previous-Releases.md` are **legacy/archival**
(trac-era, stop at ~2.9x) — not the modern channel. Don't append 4.x notes there.

## Tags and the 4.1.x maintenance branch

- **Tag naming** (`git tag -l`): bare `MAJOR.MINOR.PATCH` and
  `MAJOR.MINOR.PATCH-beta.N`, **no `v` prefix** — `4.1.3`, `4.1.0-beta.5`,
  `4.0.0`. (Pre-4.0 used `2.30b1`; don't imitate.) Tags are pushed by a
  maintainer (MAINTAINER-ONLY), not CI.
- **`4.1.x`** is the live maintenance branch (`git branch` shows it; its tip is
  tagged `4.1.3`). Stable point releases are cut *there*, not on `main`.
- **Backports** are cherry-picks that land on `4.1.x` via their own PR. The
  tell in `git log 4.1.x --oneline` is a **double PR number**, e.g.
  `de25d7d9a docs(rpc): fix lost information (#8866) (#8885)` — `#8866` is the
  original `main` PR, `#8885` the backport PR. See `contributing-workflow` for
  the backport mechanics.

## What CI builds vs. what a human publishes

The only release-relevant workflow is `.github/workflows/actions.yml`, named
**"Sanity"** (the others — `codeql.yml`, `update-copyright.yml`, `webapp.yml` —
are unrelated to releases), triggered **only on push/PR to `main`** — there is
**no `on: release`, tag,
schedule, or publish trigger.** It is verification, not distribution. It does
produce artifacts (uploaded to the Actions run, retained per GH defaults — not
attached to any Release):

- `make-source-tarball` → `cmake --build --target package_source` →
  `transmission-<semver>.tar.xz` (CPack `TXZ`; non-stable builds append
  `+r<rev>`). Most Linux/BSD platform jobs (`debian`, `fedora`, the `*bsd`s,
  `crypto`, `utp-disabled`, `ubuntu-24-04-arm64`) plus `macos` download and
  unpack this tarball; `windows`, `alpine-musl`, `android`, `cxx23`, and the
  universal-build jobs `checkout` git directly instead.
- `macos`, `debian`, `fedora`, `windows`, `alpine`, `*bsd`… → `binaries-*`.
- `windows` also runs `--target pack-msi` (WiX, from `dist/msi/`) when a dist
  build is requested — or when both the daemon and Qt are being built →
  `binaries-windows-<arch>-msi`.

There is **no dmg target in CMake** and no notarization step anywhere in CI.
The CMake macOS build does *ad-hoc* sign the bundle (`codesign -s -`,
`macosx/CMakeLists.txt:426`) only so it runs locally — that is **not** the
Developer-ID signing + notarization + dmg + appcast a shipped macOS build
needs, which is the MAINTAINER-ONLY path. See
`references/version-and-artifacts.md` for the full CI job→artifact matrix and
the Sparkle plumbing.

## RPC version is its own thing

A release that changed the RPC surface also bumps the RPC version and adds a
changelog row in `docs/rpc-spec.md` (e.g. "Transmission 4.1.0
(`rpc_version_semver` 6.0.0, `rpc_version`: 18)"). That is owned by the
**`rpc-api`** skill — follow it; don't invent version numbers here.

## Checklist: cutting a beta

### REPO STEPS (from a clone; opening PRs needs no special access, merging needs repo write)
1. Land all intended changes; make sure each has a sensible `Notes:` line.
2. (RPC changed?) Bump `rpc_version_semver` + add the `docs/rpc-spec.md` row —
   see `rpc-api`. These are often *already done* by the feature PRs (main today
   already has `RPC_VERSION_VARS(6, 1, 0)` and a populated 4.2.0 rpc-spec
   section), so the real leftover is the deprecated integer `rpc_version` in
   `rpcimpl.cc` (still `18`, stale even vs. shipped 4.1.1's `19`) — reconcile it
   against the last *shipped* value (which may live only on the point-release
   branch, per the notes caveat), not main's.
3. Sync translations from Transifex (the `tx pull` itself needs Transifex
   credentials — a mini-handoff; see the `translations` skill), then run
   `python3 release/find-broken-translations.py` (it flags `.po` files whose
   libfmt placeholders would `abort()` at runtime — see its header). Fix any
   hits. Committing the synced catalogs is a normal PR.
4. Assemble `news/news-<version>.md` from the `Notes:` lines — which come from PR
   descriptions via `gh`, not `git log` (see above). This can share step 5's PR
   or be its own; both are precedent (`ac5c9e082` bundles news+bump, `f360d5cc0`
   splits the notes out).
5. Edit the five `set(TR_VERSION_*` lines in `CMakeLists.txt`: set
   `TR_VERSION_BETA_NUMBER "N"` and `TR_VERSION_DEV FALSE` (mirror
   `c92b42de9`). Do **not** hand-edit derived values, `version.h`, or `REVISION`.
6. Open the PR (`chore: bump to <version>`) — opening needs no special access —
   and let **Sanity** go green. **Merging into `main` needs repo write access**,
   the same permission class as the MAINTAINER-ONLY steps below — a clone alone
   doesn't grant it.
7. Follow-up PR (optional): flip `main` back to a `-dev` build — **which** flip
   depends on where you are in the train:
   - **More betas of this minor still coming** (usual case): set *only*
     `TR_VERSION_DEV TRUE`, leaving BETA/MAJOR/MINOR/PATCH alone →
     `4.2.0-beta.N.dev` (mirror `3aff760b5`/`841419d06`). Recent practice often
     *skips* even this — `main` sat at plain `4.1.0-beta.4` between beta.4/beta.5.
   - **The minor's whole train is done** (last beta + stable + points shipped):
     *then* advance MINOR/MAJOR and clear `TR_VERSION_BETA_NUMBER` (mirror
     `0222d9d46`, which jumped 4.1→4.2 only after all of 4.1 shipped). Doing this
     right after beta.1 would skip the rest of the betas and the stable release.

### MAINTAINER-ONLY STEPS (org perms / private keys — hand off)
8. Push the annotated tag `<version>` (e.g. `4.1.0-beta.6`).
9. Create the GitHub Release; attach/point to built artifacts.
10. Build, codesign & notarize the macOS DMG in Xcode; publish it (the
    `transmission-releases` repo) and update the **Sparkle appcast** (sign with
    the private EdDSA key).
11. Publish Windows MSI / other installers; update the website.

For a **stable point release**, it's the same shape but cut on `4.1.x`, set
`TR_VERSION_PATCH` (not BETA), and there is no "back to -dev" step — see
`838877323`.

## See also

- `contributing-workflow` — `Notes:` line rules, Conventional-Commit subjects,
  backport mechanics, the "Sanity" CI checks.
- `building` — CMake options, the `package_source` / `pack-msi` targets.
- `macos-client` — the Xcode build, `Info.plist`, `pbxproj`, Sparkle wiring.
- `rpc-api` — bumping `rpc_version_semver` and the `rpc-spec.md` changelog.
- `translations` — Transifex sync that precedes a release.
- `references/version-and-artifacts.md` — version-string math, the two
  `version.h` generators, the CI artifact matrix, Sparkle/appcast details.
