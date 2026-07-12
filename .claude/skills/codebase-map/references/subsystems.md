# libtransmission subsystem inventory (annotated)

Read this when the quick index in `SKILL.md` is not enough — e.g. you need the
exact entry-point type for a subsystem or want to see every file that belongs to
it. Entries are `file → type/function → role`. Regenerate/verify with
`ls libtransmission/` and `grep -nE '^(class|struct) ' libtransmission/<f>.h`;
the tree changes often.

Every path below is under `libtransmission/` unless noted.

## The public surface

- `transmission.h` — header comment: "defines the public API for the
  libtransmission library." The C API that gtk/macosx/daemon/cli call. Pulls in
  `constants.h`, `types.h`, `values.h`, `variant.h`. If a symbol is here it is
  meant for external callers.
- `types.h` — small shared typedefs (`tr_torrent_id_t`, `tr_file_index_t`, …).
- `constants.h` — protocol/default constants (`TrDefaultRpcPort`, …).
- `macros.h` — project macros, incl. the `TR_PROJ_*` compile-time strings added
  in `#90` (e.g. `TR_PROJ_WEB_SERVER_BASE_PATH`).

## Session & settings

- `session.h` / `session.cc` (~2000 lines) — `struct tr_session`, the root
  owner. Holds settings, `tr_torrents`, announcer, DHT/LPD, port-forwarding,
  `tr_rpc_server`, `tr_web`, `tr_ip_cache`, verify worker, and session threads.
  Trace ownership from here.
- `session-settings.h` — `tr::SessionSettings` (and `SessionAltSpeedSettings`),
  the serialized engine preferences. This is what an add-session-setting change
  edits first (see the add-session-setting skill).
- `session-alt-speeds.h` — scheduled turtle/alt-speed logic.
- `session-id.h` — `class tr_session_id`, the rotating token used for RPC CSRF
  protection. Sent/checked via the `TrRpcSessionIdHeader` constant
  (`constants.h`), whose value is composed from the `TR_PROJ_*` app-name macros.
- `session-thread.h` — `tr_session_thread`, the libevent loop thread the
  session runs on; the boundary UI clients must marshal across.

## Torrent lifecycle

- `torrent.h` / `torrent.cc` (~2500 lines) — `struct tr_torrent`, one active
  torrent. Owns metadata, swarm, completion, verify state, file layout.
- `torrents.h` — `class tr_torrents`, the session's id/hash-indexed container.
- `torrent-ctor.h` — `struct tr_ctor`, the builder for adding a torrent
  (source, download dir, paused, priorities) before it becomes a `tr_torrent`.
- `torrent-metainfo.h` — `tr_torrent_metainfo`, immutable parsed `.torrent`.
- `magnet-metainfo.h` / `torrent-magnet.h` — magnet URIs and fetching metadata
  from peers for magnet links.
- `torrent-files.h` — file list, wanted/priority, on-disk path resolution.
- `torrent-queue.h` — `tr_torrent_queue`, the download/seed queue order
  (perf-tuned in `#73`).
- `resume.h` / `resume.cc` — read/write the per-torrent `.resume` bencode state
  (progress, peers, speed-limit *values* — persistence only, not enforcement;
  the throttle itself lives in `bandwidth.h`). See
  `docs/Transmission-Resume-Files.md`.
- `makemeta.h` — `class tr_metainfo_builder`, creates new `.torrent` files
  (used by `utils/create.cc` and the GUIs' "make torrent").
- `block-info.h` — piece/block geometry math.
- `completion.h`, `bitfield.h` — which blocks/pieces we have (`tr_bitfield`,
  moved to `std::span` in `#71`).

## Peer subsystem

- `peer-mgr.h` / `peer-mgr.cc` (~2500 lines) — `struct tr_peerMgr` (session-wide)
  and `class tr_swarm` (per-torrent peer set) and `class tr_peer_info`. The heart
  of peer selection, choking, and connection management.
- `peer-mgr-wishlist.h` / `.cc` — decides which blocks to request next
  (rarest-first etc.).
- `peer-common.h`, `peer-msgs.h` — `class tr_peerMsgs : tr_peer`, the BitTorrent
  wire-protocol message handler for one peer.
- `peer-io.h` — `class tr_peerIo`, buffered read/write over a peer socket with
  bandwidth accounting and optional encryption.
- `handshake.h` — `class tr_handshake`, the incoming/outgoing peer handshake
  state machine (plaintext or encrypted).
- `peer-mse.h` — Message Stream Encryption (the `DH` key exchange / RC4-style
  obfuscation). See `extras/encryption.txt`.
- `peer-socket.h` + `peer-socket-tcp.h` / `peer-socket-utp.h` — the socket
  abstraction over TCP and µTP.
- `bandwidth.h` — `tr_bandwidth`, the hierarchical rate-limit tree; this is
  where **speed limits** are enforced (`clamp()` is called from `peer-io.cc`).
- `webseed.h` — HTTP/BEP-19 web seeds (uses `tr_web`).
- `clients.h` — decodes peer-id prefixes into client names (see
  `docs/Peer-ID-and-User-Agent.md`).

## Trackers / announce

- `announcer.h` / `announcer.cc` — `class tr_announcer`, schedules announces and
  scrapes for all torrents; emits `tr_tracker_event`.
- `announcer-http.cc` / `announcer-udp.cc` — the two tracker transports.
- `announcer-common.h` — shared request/response structs.
- `announce-list.h` — `class tr_announce_list`, a torrent's tiered tracker URLs.

## DHT / LPD / UDP / µTP

- `tr-dht.h` — `class tr_dht`, `tr_dht::create(...)`. Mainline DHT for
  trackerless peer discovery.
- `tr-lpd.h` — `class tr_lpd`, Local Peer Discovery on the LAN.
- `tr-udp.cc` / `tr-utp.h` — the shared UDP socket demultiplexed between DHT and
  µTP.

## Port forwarding

- `port-forwarding.h` — `class tr_port_forwarding`, the coordinator.
- `port-forwarding-natpmp.h` — NAT-PMP backend (libnatpmp).
- `port-forwarding-upnp.h` — UPnP backend (miniupnpc). See
  `docs/Port-Forwarding-Guide.md`.

## RPC (server + implementation)

- `rpc-server.h` — `class tr_rpc_server`, the HTTP listener that both serves the
  web UI (`web/`) and accepts RPC. Handles auth, session-id, gzip via
  zlib.
- `rpcimpl.h` / `rpcimpl.cc` (~2700 lines) — `tr_rpc_request_exec(session,
  request, callback)`, the dispatcher that turns a `tr_variant` request into
  session/torrent calls and a `tr_variant` response. This is where RPC methods
  live. Contract: `docs/rpc-spec.md` (see the rpc-api skill).
- `api-compat.h` / `api-compat.cc` — `tr::api_compat`, converts between `Tr4`
  (legacy bespoke RPC, mixed-case keys) and `Tr5` (jsonrpc, snake_case). Relevant
  when touching RPC field names.

## HTTP fetching

- `web.h` / `web.cc` — `class tr_web`, the libcurl-backed async fetcher used by
  announces, web seeds, favicon and blocklist downloads, and the remote
  `RpcClient`. Extended in `#81` so `transmission-remote` can share it.
- `web-utils.h` — URL parse/normalize helpers.

## File I/O & storage

- `file.h` + `file-posix.cc` / `file-win32.cc` — low-level cross-platform file
  ops (`tr_sys_file_*`), the only place that touches the OS filesystem API.
- `file-utils.h`, `local-data.h` — path/config-dir helpers.
- `open-files.h` — `class tr_open_files`, an LRU pool of open file descriptors
  keyed by `(torrent id, file index)` so we don't reopen on every block.
- `inout.h` — free functions `tr_ioRead` / `tr_ioWrite`, the block-level
  read/write path that maps piece offsets onto files via `file-piece-map`.
- `file-piece-map.h` — maps pieces↔files (changed to `std::span` in `#98`).
- There is **no** `cache.cc`; `unused_cache_size_mbytes` is a session setting,
  not a separate module.

## Verify & watch

- `verify.h` — `class tr_verify_worker`, background thread that re-hashes files
  to confirm completion.
- `watchdir.h` + `watchdir-inotify.cc` / `watchdir-kqueue.cc` /
  `watchdir-win32.cc` / `watchdir-generic.cc` — `tr::Watchdir`, auto-add
  torrents dropped into a watched folder (`watchdir-base.h` is the shared base).

## Blocklists

- `blocklist.h` — the in-memory blocked-IP-range lookup.
- `blocklist-download.h` / `.cc` — download & decompress remote blocklists
  (newly added; currently uncommitted per the blocklist refactor work). See
  `docs/Blocklists.md`.

## Serialization & the key vocabulary

- `variant.h` — `struct tr_variant`, the universal tagged value (null/bool/int/
  double/string/vector/map) that RPC, settings, resume files, and `.torrent`
  parsing all pass around. `tr_variant::Map` is a vector of `(tr_quark,
  tr_variant)`.
- `variant-json.cc` — JSON codec (RapidJSON; UTF-8 validation/repair is not
  here — it lives in `string-utils.cc` via simdutf). `variant-benc.cc` +
  `benc.h` — bencode codec for `.torrent`/`.resume`.
- `serializer.h` — `tr::serializer`, compile-time registration mapping a struct's
  members to variant keys (used by `SessionSettings` and `tr::app::Prefs`).
  Converters declared with `TR_DECLARE_CONVERTER` (`converters.h`).
- `quark.h` — `tr_quark`, interned strings. Every setting name and RPC key is a
  quark; `tr_quark_new` / `tr_quark_lookup`. Adding a setting/RPC field means
  adding a quark.

## Cross-cutting utilities

- `utils.h` — general helpers (numbers, time, strings).
- `values.h` — `tr::Values`, strongly-typed `Speed`/`Memory`/`Storage` units so
  KB/MB/ratio conversions are centralized.
- `error.h` — `struct tr_error`, the lightweight error carrier;
  `error-types.h` for codes.
- `log.h` — logging; `tr_logAddMessage`, `tr_logSetLevel`.
- `crypto-utils.h` + `crypto-utils-{openssl,mbedtls,wolfssl,ccrypto}.cc` — one
  header, per-backend implementations chosen at configure time (see building
  skill for which SSL backend a build picks).
- `net.h` — sockets/addresses; `ip-cache.h` — `tr_ip_cache` (our global
  IPv4/IPv6 address discovery).
- `tr-buffer.h`, `tr-strbuf.h`, `interned-string.h`, `string-utils.h` — buffers
  and string types.
- `timer.h` / `timer-ev.h` — the timer abstraction over libevent.
- `bandwidth.h`, `history.h`, `lru-cache.h`, `bitfield.h` — reusable data
  structures.
- `platform.h`, `env.h`, `subprocess-{posix,win32}.cc` — OS glue
  (config dirs, env vars, spawning the "torrent done" script — see
  `docs/Scripts.md`).

## Tests mirror this layout

Most subsystems have a sibling test in `tests/libtransmission/<name>-test.cc`
(e.g. `announcer-test.cc`, `handshake-test.cc`, `rpc-test.cc`, `variant-test.cc`,
`session-test.cc`, `torrent-test.cc`, `torrents-test.cc`) — but not every module
has one (there is no `resume-test.cc`, for instance). App-layer tests live in
`tests/libtransmission-app/` (`prefs-test.cc`, `rpc-client-test.cc`,
`converter-tests.cc`). See the testing skill for running and adding them.
