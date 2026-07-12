---
name: rpc-api
description: >-
  How to change Transmission's JSON-RPC / bespoke RPC API safely — the public contract in
  docs/rpc-spec.md consumed by many third-party apps. Load this WHENEVER you: add or change
  a field in torrent_get / session_get / session_set, add a new RPC method, touch
  libtransmission/rpcimpl.cc, rpc-server.cc, api-compat.{cc,h}, or docs/rpc-spec.md, bump
  rpc_version / rpc_version_minimum / rpc_version_semver, edit the X-Transmission-Session-Id
  CSRF handshake or the 409 flow, change the RPC host/IP whitelist or HTTP Basic auth
  (rpc-server.cc's is_authorized / is_address_allowed), deprecate an RPC key, update rpc-test.cc
  ExpectedKeysUnsorted or api-compat-test.cc, or wire a new field into the RPC consumers
  (web/src, transmission-remote, libtransmission-app/rpc-client). Also load when hand-testing
  the daemon over curl or asking "why does the old camelCase key still work". Treat every
  change as public API design. It does NOT cover adding a persisted session preference
  end-to-end (add-session-setting skill); to launch a daemon for hand-testing, see the curl
  recipe below.
---

# Changing the RPC API

`docs/rpc-spec.md` is a **public contract**. Many third-party clients — and the wrapper
libraries listed in its §1.3 — depend on exact key names, value types, and behavior. A
rename or type change breaks
software you will never see. Design every change as if it ships forever: **add, don't
remove; deprecate, don't rename.** The api-compat layer in this repo exists precisely
because keys from a decade ago must still work.

Detailed step-by-step checklists live in **`references/reference.md`** — read it before you
start editing. This body is the map and the load-bearing rules.

## The one mental model that prevents mistakes

Transmission speaks two wire protocols:

- **Tr5** (current): JSON-RPC 2.0 envelope (`jsonrpc`/`method`/`params`/`id`/`result`),
  all keys `snake_case`. Added in `rpc_version_semver` 6.0.0 (Transmission 4.1.0).
- **Tr4** (legacy, deprecated): the old bespoke envelope (`method`/`arguments`/`tag`/
  `result`) with a mix of `camelCase` and `kebab-case` keys.

**The server processes everything internally as Tr5.** `libtransmission/api-compat.cc` is
the *only* code that knows Tr4 exists. On the way in, a legacy request is rewritten to Tr5
(`convert_incoming_data`); on the way out, if the request was legacy, the Tr5 response is
rewritten back to Tr4 (`convert(response, Style::Tr4)` in `tr_rpc_idle_done`). So:

> Write every handler, field, and quark in `snake_case` only. The legacy spelling is
> api-compat's job, not yours.

A brand-new field or method (one that never existed before 6.0.0) therefore needs **no**
legacy alias — verified: `webseeds_ex` and `active_reqs_to_peer` have no `_APICOMPAT`
sibling and no api-compat entry. Legacy clients just see the `snake_case` name.

## Where the pieces live

| Concern | File |
|:---|:---|
| Public spec (the contract) | `docs/rpc-spec.md` |
| HTTP transport, CSRF, auth, whitelist | `libtransmission/rpc-server.cc` |
| Method dispatch + all handlers + field tables | `libtransmission/rpcimpl.cc` |
| Version constants, JSON-RPC error codes | `libtransmission/rpcimpl.h` |
| Endpoint paths, header-name constants | `libtransmission/constants.h`, `libtransmission/macros.h` |
| Tr4 ⇄ Tr5 translation | `libtransmission/api-compat.{cc,h}` |
| Interned key strings | `libtransmission/quark.{h,cc}` |
| Tests | `tests/libtransmission/rpc-test.cc`, `tests/libtransmission/api-compat-test.cc` |

## Transport and the CSRF handshake (rpc-server.cc)

Default endpoint: `http://host:9091/transmission/rpc` — port `9091` (`TrDefaultRpcPort`),
base path `/transmission/` (`TR_PROJ_WEB_SERVER_BASE_PATH`) + `rpc`
(`TrHttpServerRpcRelativePath`). RPC accepts only `POST` of a JSON body; `GET`/`OPTIONS`
are handled specially (`handle_request`, `handle_rpc`).

CSRF protection (guarded by `#define REQUIRE_SESSION_ID`): every request must carry the
header **`X-Transmission-Session-Id`** (built from `TR_PROJ_RPC_SESSION_ID_HEADER` in
`macros.h`, = `"X-" + AppNameCapitalized + "-Session-Id"`). `test_session_id()` compares it
to `session->sessionId()`. On mismatch the server returns **HTTP 409** and includes the
correct id in the response headers (plus `X-Transmission-Rpc-Version:` = the semver). The
client must read that header and resend. This is the exact code path in `handle_request`
around the `test_session_id` / `send_simple_response(req, 409, ...)` block — do not change
the header name or the 409 status without treating it as a contract break (it is documented
in `rpc-spec.md` §2.2.1 and every client depends on it).

Auth (`is_authorized`, optional HTTP Basic) and IP/host whitelists (`is_address_allowed`,
`isHostnameAllowed`, DNS-rebinding defense) are also here; the default whitelist
`127.0.0.1,::1` means localhost works with no password.

## Method dispatch (rpcimpl.cc)

`tr_rpc_request_exec_impl()` parses the request, detects Tr5 vs legacy, then looks the
`method` up in two tables (both near line 2517):

- `sync_handlers` — return a `pair<Error::Code, string>` immediately.
- `async_handlers` — take a `tr_rpc_idle_data*` and finish later (network/disk).

Each entry is `{ TR_KEY_method, { fn, has_side_effects } }`. `has_side_effects` decides
whether a JSON-RPC *notification* (no `id`) may run — read-only methods sent as
notifications short-circuit to success without executing.

⚠️ **Both tables are `small::max_size_map` sized *exactly* to their contents** —
`sync_handlers` is `20U` with 20 entries, `async_handlers` is `4U` with 4. Adding a method
means bumping that template size argument too, or the fixed-capacity map overflows. The
per-field maps (`session_accessors()` is `64U`, ~60 used) have the same trap.

## Adding a field or method — the short version

Full checklists in `references/reference.md` §B (torrent_get), §C (session_get/set),
§D (new method). The recurring steps:

1. Add a `snake_case` quark to **both** `quark.h` (enum) and `quark.cc` (`MyStatic` string
   array). `MyStatic` is `static_assert`-checked to stay strictly sorted by string value and
   to match `TR_N_KEYS`, so a misordered string or a forgotten file is a **compile error**,
   not silent. The one *silent* trap is putting the `quark.h` enum entry at a slot that does
   not match where the string sorts in `quark.cc` — that misaligns every later key
   (reference §E).
2. `torrent_get`: extend `isSupportedTorrentGetField()` (allowlist) **and**
   `make_torrent_field()` (producer). `session_get/set`: add a getter/setter pair in
   `session_accessors()`. New method: write the handler and register it in the right table.
3. **Document** in `docs/rpc-spec.md` (the right §3/§4 subsection **and** a line in the
   current release block of §5).
4. **Bump `rpc_version_semver`** — *only* if the current cycle has already shipped. If the
   last §5 block in `docs/rpc-spec.md` already matches `rpc_version_semver` in `rpcimpl.h`
   (it does today: `6.1.0`, still unreleased), the cycle is open — just add your doc row and
   skip the code bump (next section).
5. Update **consumers** that need it (reference §G): `web/src`, `utils/remote.cc`,
   and where relevant the app RpcClient.
6. Update **tests** — especially `session_get`'s `ExpectedKeysUnsorted` canary.

## The session_get test canary — do not forget it

`tests/libtransmission/rpc-test.cc` test `sessionGet` holds `ExpectedKeysUnsorted`: the
*exact* set of keys `session_get` must return. It fails on **missing and unexpected** keys
alike, so adding or removing any `session_get` field **requires** editing that array in the
same commit. This is the single most-forgotten step in an RPC change.

## Versioning and the never-remove rule

Three numbers, all from `session_get`, defined in code:

- **`rpc_version_semver`** — the **source of truth**. `RPC_VERSION_VARS(6, 1, 0)` in
  `rpcimpl.h` (→ `TrRpcVersionSemver` = `"6.1.0"`). Bump **minor** for a new
  field/method/optional arg; **major** for any breaking change (:bomb: in the spec).
- **`rpc_version`** (int `RpcVersion`, currently `18`) and **`rpc_version_minimum`**
  (`RpcVersionMin`, `14`) in `rpcimpl.cc` — **both DEPRECATED**. `RpcVersion` is
  deliberately frozen (its TODO: bump once "before releasing 6.1.0", not per release), so
  the doc's integer column in §5 and the code constant can legitimately disagree mid-cycle.
  Don't bump the integer to chase the doc; follow its TODO and check with the maintainer.

**Never remove or rename a public key — deprecate it.** Keep its code working, mark it
`**DEPRECATED** X. Use Y instead.` in the spec (match the target §5 block's style — older
blocks prefix `:warning:`, the 4.2.0 block does not; see reference §F), and stop
recommending it. See
reference §F for the real deprecation history (`trackerAdd`→`trackerList`,
`utp_enabled`→`preferred_transports`, `webseeds`→`webseeds_ex`, …). Removal, if ever, is a
separate major-version event.

## api-compat.{cc,h} — what it is and when you touch it

It is the bidirectional Tr4⇄Tr5 translator: a large `RpcKeys` table mapping each
`snake_case` quark to its legacy `_camel`/`_kebab` spelling, plus envelope conversion
(`arguments`↔`params`/`result`, `tag`↔`id`, error-object shape, a handful of "crazy case"
special fields like `encryption` and `files_wanted`). `tests/libtransmission/api-compat-test.cc`
pins the round-trips with literal JSON fixtures.

You touch api-compat **only** when adding a legacy alias for a *pre-6.0.0* key, or changing
how legacy translation works. Adding a genuinely new field/method does **not** need an
api-compat entry (see the mental-model section).

## Hand-testing with curl

Do the CSRF two-step against a running daemon — start one with
`transmission-daemon -f -w /tmp/dl` (foreground, saving to `/tmp/dl`). Full incantation with
the token-grab loop is in reference §I;
the gist: POST once to get a 409 + `X-Transmission-Session-Id`, then resend with that
header and a `{"jsonrpc":"2.0","method":"session_get","id":1}` body. `session_get` with no
`fields` returns everything; `torrent_get` requires a non-empty `params.fields`.
`transmission-remote --debug` dumps real RPC traffic for reference (rpc-spec.md §1.2).

## Related skills

- **add-session-setting** — a settings-backed `session_get/set` field is far more than an
  RPC change (settings.json, all frontends, CLI, docs). Use that skill; it includes the RPC
  step. This skill covers the RPC surface itself.
- **testing** — running/registering the RPC tests, fixtures, ctest prefixes.
- **qt-client** / **gtk-client** / **web-client** — updating each frontend's field usage;
  **web-client** owns the web/src edit + rebuild loop (dev via `npm run dev` +
  `TRANSMISSION_WEB_HOME`; never commit the generated `web/public_html/transmission-app.*`).
- **codebase-map** — where libtransmission subsystems live if a handler needs new core data.
- Branch/commit/PR conventions for landing the change: the repo-root `CONTRIBUTING.md`.
