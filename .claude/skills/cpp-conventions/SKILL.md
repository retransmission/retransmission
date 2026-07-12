---
name: cpp-conventions
description: >-
  The C++ dialect, naming, and core idioms of Transmission's C++ (libtransmission,
  libtransmission-app, daemon, transmission-remote/cli — and the base style still applies
  inside qt/gtk/macosx, which only layer extra rules on top; see their own skills). Load
  this WHENEVER you write or
  edit C++ here: naming a class/method/variable/member, choosing constexpr vs #define or
  enum class vs enum, deciding how to propagate an error (tr_error), intern a settings/RPC
  key (tr_quark), serialize benc/json (tr_variant + tr::serializer), express bytes/speeds
  (tr::Values), assert (TR_ASSERT), or format a string (fmt, not std::format). Also load
  when a clang-tidy/clang-format warning appears, when adding a new *testable* module
  (peer-mgr-wishlist Mediator/DI pattern), or when a reviewer cites "style" / "the core
  guidelines" / "run code_style.sh". Covers what .clang-format, the per-directory
  .clang-tidy files, and CONTRIBUTING.md actually enforce day-to-day.
---

# Transmission C++ conventions

This is the house style for the C++ core and its shared app layer. The Qt/GTK/macOS
front-ends have their own extra rules (see the `qt-client`/`gtk-client`/`macos-client`
skills, plus the code under `qt/`, `gtk/`, and `macosx/`). For *running* the
formatter/tidy and the build, see `building`;
for writing tests, see `testing`.

The guiding tension (from `CONTRIBUTING.md`): follow the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) **with
pragmatism about the C legacy**. Transmission was C for over a decade; old code won't all
match modern style, and reviewers know that. Hold *new* code to the modern bar; don't
churn unrelated old code to match.

## The dialect

- **C++20.** `CMakeLists.txt` pins `CMAKE_CXX_STANDARD 20` and `.clang-format` sets
  `Standard: c++20`. Concepts, ranges, `<=>`, designated initializers, `std::span` are all
  in-bounds and actively used.
- **Formatting is mechanical — never hand-format.** `.clang-format` is the source of truth
  (column limit 128, 4-space indent, Linux braces, `PointerAlignment: Left`,
  `QualifierAlignment: Right` → **east const**: write `tr_error const*`, `Candidate const&`,
  not `const tr_error*`). Run `./code_style.sh` before you commit to auto-fix. CI runs
  `./code_style.sh --force` and fails if that leaves a `git diff`. It targets
  **clang-format 22** — other versions format slightly differently, and a local
  `./code_style.sh --check` silently *skips* the C/C++ files unless clang-format 22 is
  installed (`--force` overrides that version gate), so a clean `--check` on the wrong
  version doesn't prove CI will pass (see `building`).
- **Includes are not auto-sorted.** `.clang-format` has `SortIncludes: Enabled: false`;
  ordering is manual and meaningful (see below).

## Naming — the part people get wrong

Two worlds coexist. Match whichever the surrounding file is in.

| Thing | Convention | Real example |
|---|---|---|
| Public C-API free functions (`transmission.h`) | `tr_` + **camelCase** | `tr_sessionSetDownloadDir`, `tr_torrentStart` |
| Public C-API / core types | `tr_` + **snake_case** | `tr_session`, `tr_torrent`, `tr_error`, `tr_quark`, `tr_block_span_t` |
| Modern C++ classes/structs | **PascalCase** | `Wishlist`, `Candidate`, `Mediator`, `Field`, `Value` |
| Namespaces | `tr::` + short lower/Pascal | `tr::app`, `tr::platform`, `tr::serializer`, `tr::Values` |
| Methods & free functions in new code | **snake_case** | `client_has_block()`, `on_got_block()`, `make_spans()` |
| Variables & parameters | **snake_case** | enforced: clang-tidy `VariableCase/ParameterCase: lower_case` |
| Private/protected data members | trailing `_` | `message_`, `code_`, `candidates_`, `mediator_` |
| `constexpr` variables (incl. local) | **CamelCase** | `NotAdjacent`, `MemberPointer` (clang-tidy `ConstexprVariableCase: CamelCase`) |
| Numeric literal suffixes | **uppercase** | `0U`, `1U`, `15.0` (clang-tidy + clang-format) |

Those cases aren't folklore — most are enforced by `libtransmission/.clang-tidy`
`readability-identifier-naming.*`, so a mismatch is a build-time tidy warning, not a nit.

## Modern-C++ preferences (from CONTRIBUTING.md, and lived in the code)

- `constexpr` / `constexpr` functions over `#define` for values.
- `enum class` (with an explicit underlying type) over plain `enum` — e.g.
  `enum class MemoryUnits : uint8_t {...}` in `values.h`.
- New-style headers: `<cstring>` not `<string.h>`, `<cstddef>` for `size_t`.
- `[[nodiscard]]` on every accessor / pure query — it's nearly universal here.
- **Prefer `std::` over bespoke.** Use `std::list`/`std::vector`/ranges instead of rolling
  your own. The niche exception already in the tree is `small::vector` / `small::set`
  (bundled `small` lib) for hot paths that benefit from inline storage — imitate, don't
  invent.
- Prefer the **free-function** `std::begin/end/size/empty/data(x)` forms over member calls;
  the codebase uses them consistently (see `variant.h`, `peer-mgr-wishlist.cc`).
- `std::span` is the ongoing direction for "pointer + length" pairs. Recent conversions:
  `18ea12186 refactor: change file-piece-map to use std::span (#98)`,
  `ef80786e8 …tr_bitfield… (#71)`, `0114d7aad …crypto utils… (#91)`. Prefer a span over a
  raw `T*, size_t` in new signatures. (The corresponding tidy check
  `cppcoreguidelines-pro-bounds-pointer-arithmetic` is disabled *only* until that migration
  finishes — the top-of-file TODO in `.clang-tidy` says so.)

## Includes & file boilerplate

Every file starts with the SPDX/`Mnemosaic LLC` copyright block (copy it from any neighbor)
and `#pragma once` for headers. Include order, grouped with blank lines and **not** sorted:

1. C++ standard library — `<algorithm>`, `<vector>`, … (add a trailing comment when the
   include is for one symbol, e.g. `#include <cstddef> // size_t`).
2. Third-party in angle brackets — `<fmt/format.h>`, `<small/vector.hpp>`.
3. Project headers in quotes, **repo-root-relative**: `#include "libtransmission/quark.h"`
   — never a bare `"quark.h"`.

Some peer-module headers begin with `#error` guards requiring
`#define LIBTRANSMISSION_PEER_MODULE` before inclusion (see `peer-mgr-wishlist.h`); the
`.cc` and its test define that macro before the include.

## Error handling & core idioms (details in references/idioms.md)

- **`tr_error` (`error.h`) is the primary propagation idiom, not exceptions.** Functions
  that can fail take a trailing `tr_error* error = nullptr` out-param and return `bool`/an
  `optional`; the caller passes `nullptr` to ignore. Forward-declare with
  `struct tr_error;` in headers. See `file.h`, `file-utils.h` for the canonical shape.
- **`std::optional<T>`** is the other go-to for "maybe a value" returns.
- **Exceptions exist but are rare** and local (a handful of `throw`/`catch` sites in
  `libtransmission/*.cc`). Don't reach for them as your default error channel; prefer
  `tr_error`/`optional`.
- **`tr_quark` (`quark.h`)** interns well-known strings (settings keys, RPC fields, BT
  protocol) into `TR_KEY_*` ints. New keys go in **snake_case**; `_camel`/`_kebab`/
  `_APICOMPAT` suffixed entries are deprecated back-compat aliases — never use them in new
  code.
- **`tr_variant` (`variant.h`) + `tr::serializer` (`serializer.h`)** are the benc/json
  data model and the struct↔variant mapping layer. New serializable structs opt in with a
  `static constexpr auto Fields = std::make_tuple(Field<&T::member>{ TR_KEY_… }, … );` —
  the member must be named `Fields` (capital F, matching the ConstexprVariableCase rule
  above); the `Serializable` concept keys off `T::Fields`. (`serializer.h`'s own doc
  comment still says lowercase `fields` — it's stale; trust the concept.)
- **`tr::Values` (`values.h`)** — use `tr::Values::Memory/Storage/Speed` for
  bytes/rates instead of bare integers so units and human formatting are handled centrally.
- **`TR_ASSERT` / `TR_ASSERT_MSG` (`tr-assert.h`)** for invariants; they compile out unless
  `NDEBUG` is unset or `TR_FORCE_ASSERTIONS` is defined. Not for user-facing/runtime errors.
- **`fmt` is the string-formatting library — use `fmt::format`, not `std::format`.** ~60
  `.cc` files include `<fmt/format.h>`; `std::format` is not used in the core.
- **`macros.h`** holds `TR_CONSTEXPR_VEC` / `TR_CONSTEXPR_STR` / `TR_CONSTEXPR23`
  (feature-gated `constexpr`) and the `TR_PROJ_*` project constants (app name, URLs, D-Bus
  IDs, RPC headers) added in `0acc01d3a feat: project macros (#90)` — build identifiers
  from these macros, don't hardcode `"transmission"` / URLs.

Read `references/idioms.md` for a worked walkthrough of each idiom with file/line citations
before writing code that uses one for the first time.

## Decoupling & testability — imitate peer-mgr-wishlist

CONTRIBUTING.md names `peer-mgr-wishlist` as *the* example of adding new, tested code into
an old untested module. Study `libtransmission/peer-mgr-wishlist.{h,cc}` and
`tests/libtransmission/peer-mgr-wishlist-test.cc`. What makes it the model:

- **A narrow, pure-virtual `Mediator` interface** declares exactly what `Wishlist` needs
  from the outside world (`client_has_block`, `piece_count`, `priority`, …) — nothing more.
- **Dependency injection via the constructor**: `Wishlist(Mediator&)`. The class never
  reaches into globals or `tr_session` directly.
- **Tests inject a `MockMediator`** (a `final` `Mediator` backed by plain `std::map`/`std::set`)
  and drive the class in isolation, no torrents or sockets required.

Apply the same shape for new logic: define the seam as an abstract interface, take it by
reference, and land a unit test alongside. See the `testing` skill for mechanics.

## Two hard product rules

- **Every new feature must be reachable via BOTH the C API and the RPC/JSON API.** The
  macOS and GTK clients use the C API; the web UI, Qt (partly), and third-party apps use
  RPC. A feature exposed through only one is incomplete. See the `add-session-setting`
  and `rpc-api` skills for the end-to-end wiring — the path runs through
  `session-settings.h`, the RPC handlers, and each front-end.
- **KISS.** When two designs work, ship the simpler one first.

## Before you commit

1. `./code_style.sh` (auto-fixes formatting) — or `--check` to just verify (note `--check`
   skips C/C++ unless clang-format 22 is installed; CI uses `--force`).
2. **Fix every warning in new code before merging** (CONTRIBUTING.md). clang-tidy runs
   per-directory (`libtransmission/`, `libtransmission-app/`, `gtk/`, `qt/`,
   `tests/…` each have their own `.clang-tidy`; there is no root config). Don't silence a
   check with `// NOLINT` unless you match the existing, commented style and have a real
   reason. See `building` for how to run tidy (it's slow — save it for presubmit).
3. Confirm the change is reachable from both the C API and RPC where applicable.

## Before you open a PR

Two required lines in the PR description — both are hard repo rules and easy to forget:

- **`Notes: …`** — `CONTRIBUTING.md` requires a one-sentence, user-facing release-note
  paragraph beginning with `Notes: ` for the release-notes script (use `Notes: none` for
  CI-only or refactor-only changes that users won't notice).
- **AI-disclosure block** — repo-root `AGENTS.md` mandates that any PR (or issue) opened by
  an AI agent begin its description with this block; omitting it violates repo policy:
  ```markdown
  > [!NOTE]
  > This pull request was created by an AI agent.
  ```
