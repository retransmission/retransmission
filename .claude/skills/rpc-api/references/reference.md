# RPC API reference — checklists, inventory, version history

Read this after the SKILL.md body when you are actually editing the API. Line numbers
drift; every anchor below is also grep-able by the symbol or string quoted. Re-grep
before trusting a line number.

---

## A. How a request flows through the server

The dispatcher is `tr_rpc_request_exec_impl()` in `libtransmission/rpcimpl.cc` (near
line 2553). The whole server works in **Tr5** form internally — JSON-RPC 2.0 envelope,
`snake_case` keys. `libtransmission/api-compat.cc` is the only place that knows about the
old bespoke ("Tr4") protocol.

1. Parse: string requests go through `tr_variant_serde::json()` (`tr_rpc_request_exec`,
   near line 2674). A top-level Array is a JSON-RPC batch (`tr_rpc_request_exec_batch`).
2. Detect protocol: if the request has `"jsonrpc": "2.0"` it is Tr5. Otherwise it is
   legacy, and `tr::api_compat::convert_incoming_data(*map)` rewrites it **in place** to
   Tr5 before dispatch (rpcimpl.cc near line 2577). So handlers never see legacy keys.
3. Look up `method` in `async_handlers` then `sync_handlers` (the two `small::max_size_map`
   tables near lines 2517/2542). Miss → `Error::METHOD_NOT_FOUND`.
4. Run the handler. It reads `params` (a `tr_variant::Map`) and fills `args_out`, all in
   `snake_case`.
5. Respond: `tr_rpc_idle_done()` (near line 203) builds the JSON-RPC response. **If the
   request was legacy**, it calls `tr::api_compat::convert(response, Style::Tr4)` to turn
   the response back into the bespoke shape and legacy key spellings.

Consequence you must internalize: **write handlers and field code in `snake_case` only.**
The legacy spelling of a key is not your handler's problem; it lives in api-compat's key
table (section D).

---

## B. Checklist — add a field to `torrent_get`

Fields are gated by an allowlist and produced by a big switch, both in `rpcimpl.cc`.

1. **Quark.** If the field name is not already a quark, add it. Two files, kept in
   lockstep by array index (see section E):
   - `libtransmission/quark.h` — add `TR_KEY_<name>` in the enum, `snake_case`, marked
     `// rpc`.
   - `libtransmission/quark.cc` — add the matching `"<name>"sv` string at the **same
     index** in the `MyStatic` array (near line 23).
   - A brand-new field (one that never existed pre-6.0.0) needs **no** legacy sibling and
     **no** api-compat entry. Confirmed by `webseeds_ex` and `active_reqs_to_peer`, which
     have neither. Legacy clients will simply see the `snake_case` name.
2. **Allowlist.** Add `case TR_KEY_<name>:` to `isSupportedTorrentGetField()` (rpcimpl.cc
   near line 657). Requests for keys not in this list are silently dropped.
3. **Producer.** Add `case TR_KEY_<name>:` to `make_torrent_field()` (near line 748),
   returning a `tr_variant`. This one function serves both the `object` and `table`
   response formats (both route through it via `make_torrent_info`, near line 943), so you
   only write it once.
4. **Document** in `docs/rpc-spec.md` §3.3 (`torrent_get`) and add a line to the current
   release block in §5 (see section F).
5. **Bump the semver** (section F).
6. **Consumers** that want the field (section G): add it to their field lists.
7. **Tests**: `torrent_get` has no exhaustive key canary like `session_get` does, but add
   coverage in `tests/libtransmission/rpc-test.cc` if the field has interesting behavior.

Real top-level example to imitate: commit `eac1f24f0` "feat(web): add webseeds list (#8421)"
added `webseeds_ex` — the quark (E), a `case TR_KEY_webseeds_ex:` in **both**
`isSupportedTorrentGetField()` (step 2) and the top-level `make_torrent_field()` switch
(step 3, producing via a new `make_webseed_ex_vec` helper), a §5 doc row, and a web consumer
— with **no** semver bump, because it landed in the already-open cycle (section F).

Do **not** use commit `1b623afd` (#89, `active_reqs_to_client`/`active_reqs_to_peer`) as a
template for this checklist: those are nested sub-fields of the already-allow-listed `peers`
array, so it touches only the peers producer (`make_peer_vec`, bumping its `tr_variant::Map`
capacity hint `19U`→`21U`) and neither the allowlist nor the top-level `make_torrent_field`
switch — a materially different code path from a plain top-level field.

---

## C. Checklist — add a field to `session_get` / `session_set`

Session fields are registered as getter/setter pairs, not a switch.

1. **Quark** — same as B.1.
2. **Accessor pair.** In `session_accessors()` (rpcimpl.cc near line 1894) add a
   `map.try_emplace(TR_KEY_<name>, getter, setter)`. The getter is
   `[](tr_session const& src) -> tr_variant { ... }`. The setter is
   `[](tr_session& tgt, tr_variant const& src, ErrorInfo& err) { ... }`, or `nullptr` for
   a read-only field (see `TR_KEY_rpc_version_semver`, near line 2248). Setters typically
   guard with `if (auto const val = src.value_if<T>()) { ... }`.
   - **Capacity:** the map is `small::max_size_map<..., 64U>` and is nearly full (~60
     entries). If your addition pushes it past 64, bump the `64U`.
3. **`session_set` validation** — most setters ignore bad input via `value_if`; set
   `err` if you need to reject.
4. **Test canary — do not skip.** `tests/libtransmission/rpc-test.cc`, test `sessionGet`,
   has `ExpectedKeysUnsorted` (near line 584): the exact set of keys `session_get` must
   return. The test fails on **both** missing and unexpected keys. Add your `TR_KEY_<name>`
   there. This is the single most-forgotten step.
5. **Document** in `docs/rpc-spec.md` §4.1 and §5; **bump semver** (section F).
6. If this field is also a persisted *session preference* (settings.json, GTK/Qt/CLI
   surfaces), it is much more than an RPC change — use the **add-session-setting skill**,
   which subsumes this checklist.

---

## D. Checklist — add a whole new method

1. **Write the handler** in `rpcimpl.cc`. Two shapes:
   - **Sync** (returns immediately):
     `std::pair<JsonRpc::Error::Code, std::string> myMethod(tr_session*, tr_variant::Map const& args_in, tr_variant::Map& args_out)`.
     Return `{ Error::SUCCESS, {} }` or an error code + message.
   - **Async** (needs a callback, e.g. network or disk I/O):
     `void myMethod(tr_session*, tr_variant::Map const& args_in, tr_rpc_idle_data* idle)`.
     Imitate `portTest`, `torrentAdd`, `blocklistUpdate`, `torrentRenamePath` and finish
     with `tr_rpc_idle_done(idle, code, msg)`.
2. **Register** in `sync_handlers` (near line 2517) or `async_handlers` (near line 2542):
   `{ TR_KEY_<method>, { myMethod, has_side_effects } }`. `has_side_effects` is `true` if
   the method mutates state; it controls whether a JSON-RPC *notification* (a request with
   no `id`) is allowed to run — read-only methods sent as notifications short-circuit to
   `SUCCESS` without executing (see the `is_notification && !has_side_effects` branches).
   - **Capacity gotcha:** both tables are currently sized **exactly** to their contents —
     `sync_handlers` is `20U` with 20 entries, `async_handlers` is `4U` with 4. Adding a
     method **requires bumping** that template size argument, or the fixed-capacity
     container overflows.
3. **Method-name quark** — add `TR_KEY_<method>` (snake_case) in quark.h/quark.cc
   (section E). Historically each method also had a legacy kebab-case spelling
   (`session-get` etc.); a genuinely new method needs no legacy sibling.
4. **Document** the method in the right subsection of `docs/rpc-spec.md` §3 or §4, and add
   a `| \`<method>\` | new method` line to the current release block in §5.
5. **Bump semver** (section F).
6. **Consumers**: expose it in whichever of web / `transmission-remote` / app RpcClient
   need it (section G).
7. **Tests**: add a `TEST_F(RpcTest, ...)` in `tests/libtransmission/rpc-test.cc`. If the
   method's request/response must survive the legacy round-trip, add cases to
   `tests/libtransmission/api-compat-test.cc` too.

`group_get` / `group_set` (documented in §4.8, added in the 4.0.0 block of §5) are a clean
two-method pattern to copy.

---

## E. The quark system (why two files, index-locked)

`tr_quark` is just a `size_t` index. `libtransmission/quark.h` is the enum of names;
`libtransmission/quark.cc`'s `MyStatic` array (near line 23) is the parallel array of
strings. **They are aligned purely by position** — `TR_KEY_NONE == 0 == ""`,
enum index 1 == `MyStatic[1]`, and so on, capped by `TR_N_KEYS`.

Two of these constraints are compile-enforced (`quark.cc` near line 755):
`static_assert(quarks_are_sorted())` requires `MyStatic` to be **strictly sorted by string
value**, and a second `static_assert` requires `std::size(MyStatic) == TR_N_KEYS`. So a
misordered string, or adding the entry to only one of the two files, **fails to compile** —
those mistakes are loud, not silent. The **one silent hazard** is inserting the `quark.h`
enum entry at a position that does not match where the string sorts in `MyStatic`: the
asserts still pass, but `TR_KEY_<name>` then indexes the wrong string and every later key is
off. Add to **both**, placing each entry at the slot dictated by the string's strict
lexicographic (ASCII, so `-` < uppercase < `_` < lowercase) sort order.

Deprecated spellings carry suffix tags, documented at the top of quark.h:
`_camel` (camelCase string), `_kebab` (kebab-case string), `_APICOMPAT` (used only by
api-compat). New code uses the plain `snake_case` quark; never introduce a new `_camel` /
`_kebab` / `_APICOMPAT` unless you are adding a legacy alias for a pre-6.0.0 name.

---

## F. Versioning and compatibility policy

Three version numbers, all surfaced by `session_get`:

| key | defined in | current value | status |
|:---|:---|:---|:---|
| `rpc_version_semver` | `libtransmission/rpcimpl.h`, `RPC_VERSION_VARS(6, 1, 0)` (line 14-18) → `TrRpcVersionSemver` | `"6.1.0"` | **source of truth** |
| `rpc_version` (int) | `libtransmission/rpcimpl.cc`, `RpcVersion` (near line 183) | `18` | **DEPRECATED** |
| `rpc_version_minimum` (int) | `libtransmission/rpcimpl.cc`, `RpcVersionMin` (near line 184) | `14` | **DEPRECATED** |

- **Semver is the source of truth — but do not bump reflexively.** First decide whether the
  current release cycle is still open by comparing the version in the **last** `rpc-spec.md`
  §5 block against `RPC_VERSION_VARS` in `rpcimpl.h`:
  - **They already match** (today both are `6.1.0`, and `git tag` tops out at `4.1.3`, so the
    "4.2.0 (`rpc_version_semver` 6.1.0)" block is unreleased) → the cycle is **open**. A new
    field/method/optional arg just **appends a row to that existing block; do not edit
    `rpcimpl.h` and do not start a new block.** Two merged commits did exactly this —
    `f9088a36e` (recent paths) and `1b623afda` (#89) — adding fields to the open 6.1.0 block
    with no code bump.
  - **The last block's version is already a shipped `git tag`** (the open cycle has released)
    → your change opens the next cycle: bump `RPC_VERSION_VARS(major, minor, patch)` in
    rpcimpl.h **and** start a fresh §5 block.
- **When you do bump:** a new field/method/optional arg is a **minor** bump; a breaking
  change (remove or rename a key, change a value's type or meaning) is a **major** bump.
  `rpc-spec.md` §5 marks breaking changes with a :bomb: emoji.
- **The integer `rpc_version` is frozen and deprecated.** Its `RpcVersion` constant carries
  a TODO: `// 18 == 6.0.0, bump after all 6.0.x releases and before releasing 6.1.0` — i.e.
  it is intentionally *not* bumped per release the way it used to be. `rpc-spec.md` §5 still
  lists integer numbers per historical release (e.g. 4.1.1 → 19, 4.2.0 → `?`), so the doc's
  integer column and the code constant can disagree mid-cycle. **Do not bump `RpcVersion`
  casually** to match the doc; it is deprecated (4.1.0 block: `DEPRECATED rpc_version` /
  `rpc_version_minimum`, use `rpc_version_semver`). If a change seems to need the integer
  bumped, follow its TODO and confirm with the maintainer.

**Never remove or rename a public key. Deprecate instead.** The whole api-compat layer
exists to honor keys from a decade ago. Real deprecation examples in `rpc-spec.md` §5 — the
prefix is **inconsistent across blocks, so match whatever the block you are editing already
uses**:

- 4.0.0 and 4.1.0 blocks use `:warning: **DEPRECATED** X. Use Y instead.` —
  `trackerAdd`/`trackerRemove`/`trackerReplace` → use `trackerList`,
  `download-dir-free-space` → use `free-space` (4.0.0);
  `tcp_enabled`/`utp_enabled` → use `preferred_transports`,
  `rpc_version`/`rpc_version_minimum` → use `rpc_version_semver`,
  `manual_announce_time` ("it never worked") (4.1.0).
- 4.2.0 block (current) omits the `:warning:` — bare `**DEPRECATED** X. Use Y instead.`:
  `webseeds` → use `webseeds_ex`; `cache_size_mib` deprecated because the memory cache is
  being removed, but kept gettable/settable "until Transmission 5.0.0 to avoid client
  breakage."

A deprecated key keeps working (its getter/setter stays) — you just document it as
deprecated and stop recommending it. Removal, if ever, is a separate major-version event.

The `X-Transmission-Rpc-Version` HTTP header (returned in the 409 CSRF response, value =
`TrRpcVersionSemver`) lets a dual-protocol client detect the server's version without an
extra request. See `rpc-spec.md` §5 lines ~835-840.

---

## G. Consumers to update in lockstep

The server is the contract; several clients hard-code field lists and must be updated
together or they silently miss new data. All speak `snake_case` (Tr5) today.

| Consumer | Files | Where the field/method lists live |
|:---|:---|:---|
| Web UI | `web/src/remote.js`, `web/src/torrent.js` | `remote.js` builds each request (`method: 'torrent_get'`, etc., snake_case). `torrent.js` `Torrent.Fields.{Metadata,Stats,InfoExtra,StatsExtra}` (near line 592) are the requested field lists; plain methods like `getWebseedsEx()` read `this.fields.webseeds_ex` (i.e. `this.fields.<snake_case>`) — there are no ES6 `get` property getters. Web-asset dev loop (edit + rebuild) = **web-client skill** (`npm run dev` + `TRANSMISSION_WEB_HOME`; never commit the generated `web/public_html/transmission-app.*`). |
| `transmission-remote` | `utils/remote.cc` | `DetailsKeys` (near line 738) and `ListKeys` (near line 798) are `std::to_array<tr_quark>` field lists; add your `TR_KEY_<name>` and render it in the print code. |
| App RpcClient (GTK/Qt shared layer) | `libtransmission-app/rpc-client.{cc,h}` | Generic transport (`build_request`, `exec`), not a field list — it forwards `tr_variant` maps. It sets `network_style_ = api_compat::default_style()` and converts per request, so it talks to old *and* new servers. Usually no change needed for a new field; callers in the GTK/Qt code choose fields. See **gtk-client** / **qt-client** skills. |

To find every place a field is referenced: `grep -rn TR_KEY_<name>` (C++) and
`grep -rn <name> web/src` (JS).

---

## H. Tests

- `tests/libtransmission/rpc-test.cc` (ctest prefix `LT.`) — dispatcher behavior:
  batches, notifications, `id`/`tag`, error paths, `sessionGet` (the `ExpectedKeysUnsorted`
  canary), `torrentGet`, legacy variants (`torrentGetLegacy`, `tagSyncLegacy`).
- `tests/libtransmission/api-compat-test.cc` — round-trips legacy ⇄ JSON-RPC using literal
  JSON fixtures (`LegacySessionGetJson`, `CurrentSessionGetJson`, …). Edit these fixtures
  when you change key spellings, envelopes, or error mapping.
- `tests/libtransmission-app/rpc-client-test.cc`, `tests/utils/remote-test.cc`,
  `tests/qt/rpc-test-fixtures.h` — each stubs the CSRF 409 handshake with
  `TR_PROJ_RPC_SESSION_ID_HEADER`; imitate them if you add a client-side test.

Running/registering tests, fixtures, and the ctest prefixes are the **testing skill's**
domain.

---

## I. Hand-testing with curl

Values are verified from code, not from a running daemon: default port `9091`
(`TrDefaultRpcPort`, constants.h line 23), path `/transmission/rpc` (base path
`/transmission/` from `TR_PROJ_WEB_SERVER_BASE_PATH` + `TrHttpServerRpcRelativePath`
`"rpc"`), header `X-Transmission-Session-Id` (`TR_PROJ_RPC_SESSION_ID_HEADER`,
macros.h line 73), default whitelist `127.0.0.1,::1` (`TrDefaultRpcWhitelist`) so localhost
works with no password.

Two-step CSRF dance (first request 409s and hands back the token):

```sh
URL=http://127.0.0.1:9091/transmission/rpc

# 1. grab the session id from the 409 response headers
SID=$(curl -s -D - -o /dev/null -X POST "$URL" \
      | grep -Fi 'X-Transmission-Session-Id:' | sed 's/.*: //' | tr -d '\r')

# 2. real request, JSON-RPC 2.0, snake_case
curl -s -X POST "$URL" \
     -H "X-Transmission-Session-Id: $SID" \
     -H 'Content-Type: application/json' \
     -d '{"jsonrpc":"2.0","method":"session_get","id":1}'
```

- `session_get` with no `params.fields` returns **all** fields
  (`get_session_fields()` near line 2404: "no fields specified; get them all").
- `torrent_get` **requires** `params.fields`; an empty list returns
  `INVALID_PARAMS "no fields specified"` (rpcimpl.cc near line 991). Example params:
  `{"jsonrpc":"2.0","method":"torrent_get","params":{"fields":["id","name","webseeds_ex"]},"id":2}`.
- If auth is enabled, add `-u user:password` (HTTP Basic; `is_authorized()` in
  rpc-server.cc).
- The token expires / rotates; on any `409` just repeat step 1. This is exactly the loop
  `rpc-spec.md` §2.2.1 tells third-party clients to implement.
- Easiest way to see real traffic: run `transmission-remote --debug ...`, or set
  `TR_RPC_VERBOSE` for `transmission-qt` (rpc-spec.md §1.2). Need a server to hit? Start one
  with `transmission-daemon -f -w /tmp/dl` (foreground, download dir `/tmp/dl`).
