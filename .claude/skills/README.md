# Transmission project skills

Sixteen skills that encode how this project is actually built, tested, debugged,
extended, reviewed, and released — written so that new contributors and
smaller/cheaper AI sessions can work at the standard the project holds today.
Claude Code discovers them automatically from each `SKILL.md`'s frontmatter;
this README is the human-facing index.

Every load-bearing claim in these skills was verified against the repo when
written (file reads, git history, or by actually running the commands), and the
library went through adversarial fact-checking, junior-engineer walkthroughs,
and cross-skill consistency audits (2026-07). When a skill and the repo
disagree, the repo has moved: trust the repo, then fix the skill.

## Orientation and daily loop

| Skill | What it covers |
|---|---|
| [codebase-map](codebase-map/SKILL.md) | Repo/subsystem orientation, C API vs RPC surfaces, BEP↔code crosswalk, untrusted-input security surfaces. Start here for "where is X". |
| [building](building/SKILL.md) | CMake + Ninja workflow, ENABLE_*/USE_SYSTEM_*/RUN_CLANG_TIDY options, code_style.sh, reproducing CI builds. |
| [testing](testing/SKILL.md) | GoogleTest + ctest: running one test fast, adding test files, fixtures/sandbox, the ExpectedKeysUnsorted canary. |
| [debugging](debugging/SKILL.md) | Running the daemon on a throwaway config dir, RPC over transmission-remote/curl (409 handshake), log levels, TR_* env vars, gdb, perf. |
| [cpp-conventions](cpp-conventions/SKILL.md) | The C++ dialect: tr_error, tr_quark, tr_variant/serializer, tr::Values, TR_ASSERT, fmt, clang-format/tidy reality. |

## Making changes

| Skill | What it covers |
|---|---|
| [add-session-setting](add-session-setting/SKILL.md) | The full checklist for a session preference: core → RPC → docs → frontends → tests. The most-often-half-shipped change type. |
| [rpc-api](rpc-api/SKILL.md) | Changing the public RPC contract safely: rpc-spec.md, versioning, api-compat (Tr4/Tr5), consumers, hand-testing. |
| [qt-client](qt-client/SKILL.md) | transmission-qt development: Session/RpcClient, PrefsDialog binding, .ts translations, the TR_PROJ_* QStringLiteral gotcha. |
| [gtk-client](gtk-client/SKILL.md) | gtkmm client: GTK3/GTK4 dual-targeting, GResource .ui files, Session wrapper, _() strings. |
| [macos-client](macos-client/SKILL.md) | macosx/ + Transmission.xcodeproj — including editing project.pbxproj from Linux and the two-build-systems trap. |
| [web-client](web-client/SKILL.md) | The esbuild JS/SCSS web UI: dev loop, generated-vs-committed assets, TRANSMISSION_WEB_HOME, the webapp CI job. |
| [translations](translations/SKILL.md) | The three catalog systems (gettext .po / Qt .ts / Cocoa .strings) and Transifex — which file to touch and what not to hand-edit. |
| [third-party-deps](third-party-deps/SKILL.md) | Bundled libraries: submodule vs vendored, USE_SYSTEM_* wiring, adding/bumping a dep on every platform including Xcode. |
| [windows-portability](windows-portability/SKILL.md) | Diagnosing Windows/MSVC CI failures from a Linux desk; the tr_sys_* portability layer; MSVC breakage patterns from real history. |

## Landing and shipping

| Skill | What it covers |
|---|---|
| [contributing-workflow](contributing-workflow/SKILL.md) | Branches, commits, PR titles, Notes: lines, the AGENTS.md AI-disclosure blocks, CI checks, and the acceptance-bar judgment section. |
| [release-engineering](release-engineering/SKILL.md) | Version plumbing, news/ release notes from PR Notes: lines, tagging/backport reality, and which steps are maintainer-only handoffs. |

## Maintaining this library

- One skill per directory: `SKILL.md` (frontmatter: exactly `name` +
  `description`; body ≲300 lines) plus optional `references/*.md` for long
  material. The description is the trigger — keep it specific and symptom-rich.
- When a change makes a skill wrong (a renamed job, a moved file, a new
  workflow), fix the skill in the same PR — these documents are only as good
  as their last verification.
- When you learn something the hard way (a gotcha, a trap, a "why did CI do
  that"), it belongs in the matching skill, verified, with a file/commit
  citation.
