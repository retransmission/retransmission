---
name: codebase-map
description: >-
  Orientation map of the Transmission repo — what every top-level directory is
  for, how the libtransmission core is organized into subsystems, and which
  integration surface (C API vs JSON-RPC) each frontend uses. Load this FIRST
  whenever you are new to the tree or asking locating questions: "where is X",
  "which file handles Y", "where do peers / trackers / DHT / the RPC server /
  resume files / the write path live", "what is libtransmission-app for", "how
  does the Qt/GTK/macOS client talk to core", or "where do I start to change
  Z". Also crosswalks BitTorrent BEPs (protocol spec) to code and inventories
  untrusted-input surfaces for security review. It tells you which file to
  open; sibling skills (building, testing, cpp-conventions) tell you what to do
  once you are there.
---

# Navigating the Transmission codebase

Use this skill to turn "where does X live?" into an exact file to open. It is a
map, not a how-to: once you know the file, hand off to the relevant sibling
skill or the concrete files named inline below. Each "skill" handoff below names
an existing sibling skill and points at the real file/dir it covers.

`docs/Transmission-Architecture.md` exists but is a 27-line conceptual overview
(control flow, product list, external wiki GIFs). It does **not** describe the
code layout and predates `libtransmission-app/`. Treat it as background only;
this skill is the authoritative navigation aid.

For the full annotated file-by-file inventory of every core subsystem, read
`references/subsystems.md`. The body below is the fast index. Two more
references answer protocol/security "where" questions: `references/beps.md`
(BitTorrent BEP ↔ implementing file crosswalk) and
`references/security-surfaces.md` (every untrusted-input parse path, as a review
checklist).

## Top-level directory map

| Path | What it is |
| --- | --- |
| `libtransmission/` | The core engine (CMake target `transmission`, no namespaced alias; the code is global `tr_*` types plus `tr::` namespaces). ~180 files. All BitTorrent logic, session, RPC server, I/O. Everything else links this. |
| `libtransmission-app/` | Newer **shared GUI-client** layer (`tr::app`). Preferences, an RPC client, type converters shared by the Qt and GTK apps. See below. |
| `daemon/` | `transmission-daemon` — headless core + RPC server. `daemon.cc` plus posix/win32 backends. |
| `cli/` | `transmission-cli` — minimal single-torrent CLI (`cli.cc`). No dedicated skill yet. |
| `utils/` | Standalone CLI tools: `remote.cc` (transmission-remote, an RPC client), `create.cc`, `edit.cc`, `show.cc` (link core directly). |
| `qt/` | Qt desktop client. Talks to core over RPC via `libtransmission-app`. |
| `gtk/` | GTK (gtkmm) desktop client. Links core directly (C API). |
| `macosx/` | Cocoa/Objective-C++ client. Links core directly (C API). |
| `web/` | Web UI (vanilla JavaScript, esbuild). `src/` sources (RPC via `src/remote.js`), `public_html/` output. See the web-client skill. |
| `android/` | Android client scaffolding — no dedicated skill covers it yet. |
| `tests/` | GoogleTest suites: `tests/libtransmission/`, `tests/libtransmission-app/`, `tests/qt/`, `tests/utils/`. See testing skill. |
| `third-party/` | Bundled deps as git submodules (libevent, libutp, dht, miniupnp, libnatpmp, fmt, etc.), each in `third-party/<name>/`. See the third-party-deps skill. |
| `cmake/` | `Find*.cmake` modules + `TrMacros.cmake`, `TrGTest.cmake`. See building skill. |
| `docs/` | Wiki-style Markdown incl. `rpc-spec.md` (the RPC contract; impl in `libtransmission/rpcimpl.cc`). |
| `po/` | gettext `.po` catalogs (core + GTK strings). See the translations skill. |
| `dist/`, `release/`, `extras/` | Packaging (MSI/wxs), release scripts, misc assets (valgrind supp, sample scripts). |
| `Transmission.xcodeproj/` | Xcode project for the macOS build (separate from CMake). See the macos-client skill. |
| `CMakeLists.txt` (root) | Top-level build config; each component has its own `CMakeLists.txt`. |

## libtransmission core: the two god objects + subsystem index

Two long-lived owning objects tie the core together:

- **`tr_session`** (`session.h` / `session.cc`, ~2000 lines) — the top of the
  ownership tree. Owns settings, the torrent container, the announcer, DHT/LPD,
  port-forwarding, the RPC server, `tr_web`, caches, and thread plumbing. Almost
  every subsystem hangs off it. Start here to trace who owns what.
- **`tr_torrent`** (`torrent.h` / `torrent.cc`, ~2500 lines) — one running
  torrent. Wraps immutable metadata (`tr_torrent_metainfo`), the peer swarm
  (`tr_swarm` in `peer-mgr.cc`), completion/verify state, and file layout.

Subsystem quick index (files → the type/function you enter through). Full
detail with member notes is in `references/subsystems.md`:

- **Public C API**: `transmission.h` — declared public API for the library
  (`tr_session`, `tr_torrent`, `tr_ctor`, `tr_variant`). This is the surface
  gtk/macosx/daemon/cli code calls.
- **Session & settings**: `session.h/.cc`, `session-settings.h`
  (`tr::SessionSettings`), `session-alt-speeds.h`, `session-id.h`,
  `session-thread.h`. Adding a preference is its own
  multi-file workflow (session-settings.h, quark.h, rpcimpl.cc, every client,
  and the `ExpectedKeysUnsorted` list in `tests/libtransmission/rpc-test.cc`);
  see the add-session-setting skill.
- **Torrent lifecycle**: `torrent.h/.cc`, `torrents.h` (`tr_torrents`
  container), `torrent-ctor.h` (`tr_ctor` add-torrent builder),
  `torrent-metainfo.h`, `magnet-metainfo.h`, `torrent-magnet.h`,
  `torrent-files.h`, `torrent-queue.h`, `resume.h/.cc` (`.resume` state files),
  `makemeta.h` (`tr_metainfo_builder`).
- **Peer subsystem**: `peer-mgr.h/.cc` (`tr_peerMgr`, `tr_swarm`, `tr_peer_info`),
  `peer-mgr-wishlist.h` (block request planning), `peer-msgs.h` (`tr_peerMsgs`,
  wire protocol), `peer-io.h` (`tr_peerIo`, buffered socket), `handshake.h`
  (`tr_handshake`), `peer-mse.h` (Message Stream Encryption),
  `peer-socket*.h` (`peer-socket-tcp`, `peer-socket-utp`), `bandwidth.h`,
  `webseed.h`, `bitfield.h`, `completion.h`.
- **Trackers / announce**: `announcer.h/.cc` (`tr_announcer`),
  `announcer-http.cc`, `announcer-udp.cc`, `announcer-common.h`,
  `announce-list.h` (`tr_announce_list`).
- **DHT / LPD / uTP / UDP**: `tr-dht.h` (`tr_dht::create`), `tr-lpd.h`
  (`tr_lpd`, local peer discovery), `tr-utp.h`/`tr-udp.cc` (µTP over the shared
  UDP socket).
- **Port forwarding**: `port-forwarding.h` (`tr_port_forwarding`),
  `port-forwarding-natpmp.h`, `port-forwarding-upnp.h`.
- **RPC**: `rpc-server.h` (`tr_rpc_server`, the HTTP endpoint + web UI host),
  `rpcimpl.h` (`tr_rpc_request_exec` — executes a request against a session),
  `api-compat.h` (`tr::api_compat` — translates between the Tr4 mixed-case RPC
  and the newer Tr5 snake_case/jsonrpc style). Changing methods → `rpcimpl.cc`
  + `docs/rpc-spec.md` (see the rpc-api skill).
- **HTTP fetching**: `web.h` (`tr_web`, libcurl wrapper for announces, web
  seeds, blocklist/favicon downloads), `web-utils.h` (URL parsing).
- **File I/O & storage**: `file.h` + `file-posix.cc`/`file-win32.cc` (low-level
  fs ops), `open-files.h` (`tr_open_files`, pooled/cached fd handles),
  `inout.h` (`tr_ioRead`/`tr_ioWrite`, block-level read/write),
  `file-piece-map.h`, `block-info.h`. Note: there is **no** standalone
  `cache.cc`; the disk-cache setting is a session concern.
- **Verify**: `verify.h` (`tr_verify_worker`, background hash checking).
- **Watch dir**: `watchdir.h` (`tr::Watchdir`) + inotify/kqueue/win32/generic
  backends.
- **Blocklists**: `blocklist.h`, `blocklist-download.h` (note: blocklist-download
  is a new/uncommitted addition — see the blocklist refactor work).
- **Serialization**: `variant.h` (`tr_variant`, the universal value type),
  `variant-json.cc` / `variant-benc.cc` (JSON & bencode codecs), `benc.h`,
  `serializer.h` (`tr::serializer`, compile-time struct↔variant field
  mapping), `quark.h` (`tr_quark`, interned string keys — the vocabulary of
  settings & RPC).
- **Cross-cutting utilities**: `utils.h`, `values.h` (`tr::Values`, typed
  speed/size/ratio units), `error.h` (`tr_error`), `log.h` (`tr_logAddMessage`),
  `crypto-utils.h` (+ per-backend `crypto-utils-{openssl,mbedtls,wolfssl,
  ccrypto}.cc`), `net.h`, `ip-cache.h`, `interned-string.h`, `tr-strbuf.h`,
  `tr-buffer.h`, `timer.h`/`timer-ev.h`.

## libtransmission-app: the shared GUI layer

`libtransmission-app/` (target `transmission::app`, namespace `tr::app`) is a
**newer** static library that factors code common to the desktop GUI clients so
Qt and GTK don't each reimplement it. Verified from git history and current
sources, its pieces are:

- `prefs.h/.cc` — `tr::app::AppPrefs`, the *client-local* preferences (window
  geometry, remote-host/port/credentials, watch dir, filter text, show mode)
  that are meaningful whether the session is in-process or remote. Built on
  `tr::serializer`. Migrated here across PRs (`#21` base class, `#35`, `#55`
  recent paths, `#72`).
- `rpc-client.h/.cc` — `tr::app::RpcClient`. One RPC client that speaks to
  **either** an in-process session (via `tr_rpc_request_exec`) **or** a remote
  daemon (via `tr_web`), delivering every response back on the UI thread through
  an injected marshaller. Moved here in `#85`.
- `rpc-queue.h` — `tr::app::RpcQueue`, chains dependent RPC calls.
- `converters.h/.cc` — `tr::serializer` converters for app enums
  (`ShowMode`, `SortMode`, `StatsMode`) and `std::chrono::sys_seconds`.
- `display-modes.h`, `favicon-cache.h`, `app.h` (`tr::app::init()`).

`grep -rl libtransmission-app qt gtk macosx` confirms **Qt and GTK use it;
macOS does not.**

## The two integration surfaces — who uses which

Everything a client does is either a direct **C API** call into
`transmission.h`, or a **JSON-RPC** request (a `tr_variant`) executed by
`tr_rpc_request_exec` locally or POSTed to a daemon. Verified per-frontend by
reading includes:

| Frontend | Surface | Evidence |
| --- | --- | --- |
| **gtk** | Mostly **C API**, in-process | `gtk/Session.h` holds `tr_session*`, calls C API; ~24 files include `transmission.h`. Uses one `tr_rpc_request_exec` (`Session.cc:1127`) and `libtransmission-app` for prefs/favicon. |
| **macosx** | **C API**, in-process | ~19 files include `transmission.h`; no `libtransmission-app`, no `rpcimpl`. |
| **qt** | **RPC** (`tr::app::RpcClient`) | `qt/Session.h` has modes `InProcess`/`Local`/`Remote`; holds `tr_session*` only for the embedded case but talks to it through `RpcClient`. Includes `transmission.h` for *types* (`tr_variant`, `tr_quark`), not for driving the engine. |
| **web** | **RPC over HTTP** | `web/src/remote.js` POSTs JSON-RPC to `tr_rpc_server`. Only global/session speed limits are wired up — no per-torrent `download_limit`/`upload_limit`, unlike the other frontends. |
| **daemon / cli** | **C API**, in-process | embed a `tr_session` directly. |
| **utils/remote** | **RPC over HTTP** | `transmission-remote` is a pure JSON-RPC client. `create/edit/show` link core directly. |

Takeaway: to change GUI behavior, first decide **which surface**. A field the
Qt client needs must travel over RPC (`rpcimpl.cc` + `docs/rpc-spec.md`),
whereas GTK/macOS can read it straight off `tr_torrent`/`tr_session`.

## Where do I start for task X

- Add/rename/change a session **preference** → `session-settings.h`, `quark.h`,
  `rpcimpl.cc`, every client, and rpc-test.cc's `ExpectedKeysUnsorted`
  (see the add-session-setting skill). Client-only UI prefs live in
  `libtransmission-app/prefs.h`.
- Add/modify an **RPC method or field** → code in `rpcimpl.cc`, contract in
  `docs/rpc-spec.md`, compat in `api-compat.cc` (see the rpc-api skill).
- **Peer wire / choking / requests** → `peer-mgr.cc`, `peer-msgs.cc`,
  `peer-mgr-wishlist.cc`.
- **Per-torrent / session speed limits (throttling)** → `bandwidth.h/.cc`
  (the `tr_bandwidth` rate-limit tree), enforced by `peer-io.cc`'s
  `bandwidth().clamp()` on each read/write; periodic `allocate()` driven from
  `peer-mgr.cc`; C-API entry points `tr_torrentSetSpeedLimit_KBps` /
  `tr_torrentGetSpeedLimit_KBps` in `torrent.cc`.
- **Tracker announces** → `announcer*.cc`; tracker URL lists →
  `announce-list.cc`.
- **Disk read/write / partial files** → `inout.cc`, `open-files.cc`, `file-*.cc`.
- **Resume/state persistence** → `resume.cc`, `torrent-queue.cc`.
- **Parsing a .torrent / magnet** → `torrent-metainfo.cc`, `magnet-metainfo.cc`.
- **Which BEP / spec does file X implement** (or where DHT/PEX/magnet/µTP/
  webseed/UDP-tracker/LPD/private-torrent logic lives) → `references/beps.md`
  (BEP ↔ file crosswalk, with what is *not yet* supported: BEP 47, v2/BEP 52).
- **Security review / auditing an untrusted-input parse path** (metainfo &
  benc, magnet, peer handshake + MSE, extension messages, tracker HTTP/UDP, DHT,
  LPD, RPC inputs, blocklists, `.resume`, watchdir names) →
  `references/security-surfaces.md`.
- **A new bundled dependency** → `third-party/<name>/` + its CMake wiring
  (see the third-party-deps skill).
- **Build/config/lint questions** → building skill; **tests** → testing skill;
  **C++ idioms** → cpp-conventions skill.
- Client-specific UI work → `qt/`, `gtk/`, `macosx/`, `web/src/` respectively
  (see the qt-client, gtk-client, macos-client, and web-client skills).

## Verify before you trust

The tree moves fast (see `git log --oneline -25 -- libtransmission/`). If a path
here looks wrong, re-run `ls libtransmission/` and `grep`; trust the repo over
this map and update the map.
