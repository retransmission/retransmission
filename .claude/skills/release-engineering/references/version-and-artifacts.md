# Version-string math, the two generators, CI artifacts, Sparkle

Deep-dive backing `SKILL.md`. Everything here is derived by reading
`CMakeLists.txt` (~lines 119–230), `libtransmission/version.h.in`,
`update-version-h.sh`, `macosx/Info.plist{,.in}`,
`.github/workflows/actions.yml`, and `dist/msi/`. Re-verify against those files
if they have moved.

## The five inputs → everything else

Only these change (`CMakeLists.txt`):

```cmake
set(TR_VERSION_MAJOR "4")
set(TR_VERSION_MINOR "2")
set(TR_VERSION_PATCH "0")
set(TR_VERSION_BETA_NUMBER "")   # "" = not a beta
set(TR_VERSION_DEV TRUE)         # TRUE = a dev/nightly build
```

### Release type (mutually exclusive, in this order)
`DEV TRUE` → `TR_NIGHTLY_RELEASE=1`; else `BETA_NUMBER != ""` →
`TR_BETA_RELEASE=1`; else `TR_STABLE_RELEASE=1`. Surfaced into `version.h` via
`#cmakedefine` in `version.h.in`.

### `TR_SEMVER` (== `TR_USER_AGENT_PREFIX`, == `SHORT_VERSION_STRING`)
Start `MAJOR.MINOR.PATCH`; if `DEV` or beta, append `-`; if beta append
`beta.N`; if `DEV && beta` append `.`; if `DEV` append `dev`.

| MAJOR.MINOR.PATCH | BETA | DEV | → semver |
|---|---|---|---|
| 4.2.0 | "" | TRUE | `4.2.0-dev` (HEAD today) |
| 4.1.0 | 5 | FALSE | `4.1.0-beta.5` |
| 4.0.0 | 1 | TRUE | `4.0.0-beta.1.dev` (a dev build between beta 1 and 2) |
| 4.0.0 | "" | FALSE | `4.0.0` |

### Peer-id prefix (BEP-20), `PEERID_PREFIX`
`-TR` + base62(MAJOR) + base62(MINOR) + base62(PATCH) + type + `-`, where
`BASE62 = "0123..9A..Za..z"` and type is `Z` dev / `B` beta / `0` stable. Note
only MAJOR/MINOR/PATCH go in — the beta *number* does not. So `4.2.0-dev` →
`-TR420Z-`; `4.1.0-beta.5` → `-TR410B-` (the `5` is absent); `4.0.0` →
`-TR4000-`. (base62 means minor/patch ≥ 10 map to letters, e.g. patch 10 → `A`.)

### `CFBUNDLE_VERSION` (macOS `CFBundleVersion`)
`(MAJOR+14714) . MINOR . (PATCH*100 + stable*99 + BETA)`. CFBundleVersion only
honors three numbers, so the third packs patch+beta. The in-file examples:
`5.0.1-dev → 14719.0.100`, `5.0.1-beta.1 → 14719.0.101`, `5.0.1 → 14719.0.199`.

## The two `version.h` generators

`libtransmission/version.h` is generated, not hand-written: the CMake build
writes it into the build dir (`${CMAKE_CURRENT_BINARY_DIR}/version.h`), the
Xcode script writes it into the source tree (`$(SRCROOT)/libtransmission/
version.h`). Both emit the same macro set:

| Macro | Purpose |
|---|---|
| `PEERID_PREFIX`, `USERAGENT_PREFIX` | on-wire identity: `USERAGENT_PREFIX` → `peer-msgs.cc` (extended-handshake `v`), `PEERID_PREFIX` → `session.cc` (peer-id). The HTTP user agent is separate — `SHORT_VERSION_STRING` via `session.cc` → `web.cc` `CURLOPT_USERAGENT`, not these macros |
| `SHORT_VERSION_STRING`, `LONG_VERSION_STRING` | UI/RPC/`makemeta.cc`/`rpcimpl.cc` |
| `VCS_REVISION` (10-char), `VCS_REVISION_NUM` | commit id |
| `VERSION_STRING_INFOPLIST`, `BUILD_STRING_INFOPLIST` | **unquoted** — fed to the Xcode Info.plist C-preprocessor |
| `MAJOR/MINOR/PATCH_VERSION` | numeric |
| `TR_{BETA,NIGHTLY,STABLE}_RELEASE` | release type |

1. **CMake:** `configure_file(version.h.in version.h)`
   (`libtransmission/CMakeLists.txt:198`). Substitutes the `@TR_*@` vars and
   `#cmakedefine`s computed in the CMakeLists block. Authoritative.
2. **Xcode:** run-script build phase `sh update-version-h.sh`
   (`Transmission.xcodeproj/project.pbxproj:3490`). Re-`grep`s the five
   `TR_VERSION_*` out of `CMakeLists.txt` and recomputes the same strings in
   bash, then `replace_if_differs` into `libtransmission/version.h` (only
   rewrites on change, to avoid needless rebuilds).

**Caveat (Xcode path only):** `update-version-h.sh` picks the release-type
`#define` with `case "${peer_id_prefix}" in *X-) …TR_BETA_RELEASE`, but the
beta peer-id ends in `B-`, not `X-` — so under that script a beta currently
falls through to `TR_STABLE_RELEASE`. This is presently harmless because **no
committed C++ consumes `TR_*_RELEASE`** (they appear only in `version.h.in`);
if you wire one up, rely on the CMake/`version.h.in` path and fix the shell
`case` (`*B-`) before trusting it in an Xcode build.

## macOS Info.plist: two files, two mechanisms

- **`macosx/Info.plist`** (Xcode; `INFOPLIST_FILE`, preprocessed with
  `INFOPLIST_OTHER_PREPROCESSOR_FLAGS = -traditional`): contains *bare* tokens
  `VERSION_STRING_INFOPLIST` / `BUILD_STRING_INFOPLIST` that the C preprocessor
  replaces using the unquoted `#define`s from the generated `version.h`.
  `SUFeedURL` = `https://update.transmissionbt.com/appcast.xml`.
- **`macosx/Info.plist.in`** (CMake; `MACOSX_BUNDLE_INFO_PLIST`,
  `macosx/CMakeLists.txt:434`): `@CFBUNDLE_VERSION@`-substituted at configure
  time. `SUFeedURL` = `https://update.transmissiontorrent.com/appcast.xml`.

The two feed URLs differ (old `transmissionbt.com` vs newer
`transmissiontorrent.com`) — consistent with the org move. Confirm the intended
appcast host with a maintainer before a macOS release; don't assume they're
interchangeable. Bundle id (CMake `MACOSX_BUNDLE_GUI_IDENTIFIER`):
`org.m0k.transmission`.

## Sparkle (macOS auto-update) — mostly off-repo

- Framework is **vendored** at `macosx/Sparkle.framework` (not a submodule),
  weak-linked (`macosx/CMakeLists.txt:403,418`).
- `SUPublicEDKey` in the plist is the **public** EdDSA key; the appcast XML and
  the DMG it lists are signed with the private key (MAINTAINER-ONLY) and hosted
  on the `update.transmission*.com` server. Neither the appcast nor the signing
  tooling is in this repo.
- `macosx/SparkleProxy.mm` only exists so an *unsigned* local dev build can show
  a helpful "app must be codesigned to run Sparkle" alert instead of crashing —
  it is not part of the release path.
- The CMake mac build post-build **ad-hoc** signs the bundle
  (`codesign -s -`, `macosx/CMakeLists.txt:426`) so it launches locally; this
  runs even in the CI `macos` job. It is *not* Developer-ID signing — a shipped
  build still needs real signing + notarization from the maintainer.

## CI (`.github/workflows/actions.yml`, "Sanity") artifact matrix

Trigger: push/PR to `main` only. No tag/release/schedule trigger. A gating job
`what-to-make` decides which components to build from the diff. Artifacts
(uploaded to the run only — **never** attached to a GitHub Release by CI):

| Job | Artifact | Notes |
|---|---|---|
| `make-source-tarball` | `source-tarball` | `cmake --build --target package_source` → `transmission-<semver>.tar.xz` (CPack `TXZ`). Non-stable builds append `+r<rev>` to the name. Downloaded+unpacked by `macos`, `debian`, `fedora`, `utp-disabled`, `ubuntu-24-04-arm64`, `crypto`, and the `*bsd`s; `windows`, `alpine-musl`, `android`, `cxx23`, and the universal jobs `checkout` git instead. |
| `macos` | `binaries-macos-15-intel`, `binaries-macos-26` | `cmake --install` output; no dmg/notarize. |
| `macos-cmake-universal` | `binaries-<os>-cmake-universal` | universal build check. |
| `windows` | `binaries-windows-<arch>`, `binaries-windows-<arch>-msi` | MSI via `--target pack-msi` (WiX, `dist/msi/`) when a dist build is requested, or when both the daemon and Qt are built. |
| `debian`, `fedora`, `alpine-musl`, `*bsd`, `android`, `ubuntu-24-04-arm64`, `utp-disabled`, `crypto` | `binaries-*` | platform build/test matrix. |

There is **no** dmg target in CMake and **no** codesign / notarize / appcast /
tag / GitHub-Release step anywhere in the workflow. The other workflows
(`codeql.yml`, `update-copyright.yml`, `webapp.yml`) are not release-related.

## Packaging targets (invoke via `building` skill)

- `package_source` — CPack source `TXZ`; `CPACK_SOURCE_PACKAGE_FILE_NAME =
  transmission-<TR_SEMVER>`; `CPACK_SOURCE_IGNORE_FILES` drops the build dir,
  `.git`, `node_modules`. `REVISION` is *not* ignored, so the configured value
  (the real HEAD sha) ships inside the tarball — that is how a `.git`-less
  tarball build still reports its commit.
- `pack-msi` — custom target in `dist/msi/CMakeLists.txt` (WiX; `.wxs`
  components under `dist/msi/components/`, config from `TransmissionConfig.wxi.in`).

## docs changelog surfaces

- **`docs/rpc-spec.md`** — the maintained per-release RPC changelog. Each
  RPC-affecting release adds a section like *"Transmission 4.1.0
  (`rpc_version_semver` 6.0.0, `rpc_version`: 18)"* with a table of arg
  changes. Owned by the `rpc-api` skill.
- **`docs/Release-Notes.md`**, **`docs/Previous-Releases.md`** — legacy/archival
  (trac-era; stop around 2.9x). `Previous-Releases.md` maps old OS versions to
  the last supported build and links DMGs in the separate
  `transmission/transmission-releases` repo. Not the modern notes channel —
  4.x user-facing notes live in `news/`.

## `release/` directory contents (verified)

- `release/find-broken-translations.py` — scans `po/*.po` for libfmt
  placeholder-name mismatches that would `abort()` at runtime (fmt built with
  `FMT_USE_EXCEPTIONS=0`). Run before a release; see the `translations` skill.
- `release/windows/*.ps1` — from-source build scripts for the Windows
  dependency stack (curl, openssl, qt5/qt6, zlib, …) and `main.ps1` driver;
  used to produce the Windows build environment, not run by GitHub CI.

There is **no** release-notes generator, tag script, or publish script in
`release/` (or anywhere in the repo). Those are maintainer-local / off-repo.
