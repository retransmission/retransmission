# Debugging reference

Longer cheat sheets for the `debugging` skill. Everything here is verified against the
source at the time of writing; when in doubt, re-check with `--help` on the tool or grep the
cited file. Binary paths assume the repo-root `build/` tree.

## transmission-remote cheat sheet

First positional arg is `[host][:port]`, defaulting to `localhost:9091`. Everything is a
flag; there are no subcommands. All flags below are verified in `utils/remote.cc`'s
`Options` array (numbers are the internal option ids, not something you type).

### Selecting which torrent(s) a command targets

`-t` / `--torrent <all | active | id | hash>` sets the **current torrent(s)**, and it
applies to every request *after* it on the command line. So order matters:

```console
$ build/utils/transmission-remote -t 2 -S            # stop torrent 2
$ build/utils/transmission-remote -t all -v          # verify every torrent
$ build/utils/transmission-remote -t active -i       # info on recently-active torrents
```

`--print-ids` / `-ids` prints the ids of the current selection (useful in scripts).

### Common actions

| Flag | Long form | Does |
|------|-----------|------|
| `-l` | `--list` | List all torrents (id, %done, ETA, up/down, ratio, status, name) |
| `-a` | `--add <file\|URL\|magnet>` | Add a torrent |
| `-i` | `--info` | Details for the current torrent(s) |
| `-f` | `--files` / `--info-files` | Per-file list |
| `--info-peers` / `-ip` | | Peer list |
| `--info-trackers` / `-it` | | Tracker list |
| `-s` | `--start` | Start current torrent(s) |
| `-S` | `--stop` | Stop current torrent(s) |
| `-v` | `--verify` | Re-hash/verify local data |
| `--reannounce` | | Force a tracker announce |
| `-r` | `--remove` | Remove torrent (keep data) |
| `--remove-and-delete` / `-rad` | | Remove torrent **and delete data** |
| `-si` | `--session-info` | Session settings |
| `-st` | `--session-stats` | Cumulative + current session totals |
| `--exit` | | Tell the daemon to shut down |

### Speed / limits / auth

- `-d <kB/s>` / `--downlimit`, `-u <kB/s>` / `--uplimit`; `-D` / `-U` disable them (the
  tool's own help spells the unit `kB/s` — SI, via `SPEED_K_STR`). With a torrent selected
  they are per-torrent, otherwise global.
- Auth (when `rpc_authentication_required` is true): `-n user:pw`, `--authenv` (reads
  `TR_AUTH`), or `-N <netrc-file>`.

### Debugging aids

- `-b` / `--debug` — print the raw JSON request **and** response for each call. The single
  most useful flag when you're unsure what the client is actually sending.
- `-j` / `--json` — print the RPC response as a JSON string instead of the formatted
  human-readable output; handy for piping into `jq` or diffing responses.
- Full method/field semantics live in `docs/rpc-spec.md` and the **rpc-api** skill.

## Environment variables (annotated)

Verified present in current source. **Only the three `TR_CURL_*` flags are existence-only:**
`tr_env_key_exists()` (`libtransmission/env.cc`) returns `getenv(key) != nullptr`, so any
value — including empty or `0` — **enables** them; `unset` to disable. The value-carrying
vars (`TRANSMISSION_HOME`, `TRANSMISSION_WEB_HOME`, `CLUTCH_HOME`, `CURL_CA_BUNDLE`, and the
XDG lookups) are read with `tr_env_get_string` and an **empty value is treated as unset**;
`TR_SAVE_VERSION_FORMAT` is compared against `5`.

| Variable | Effect | Verified in |
|----------|--------|-------------|
| `TR_CURL_VERBOSE` | libcurl verbose logging for tracker HTTP(S) | `web.cc`, `announcer-http.cc` |
| `TR_CURL_SSL_NO_VERIFY` | Skip TLS cert verification for trackers | `web.cc` |
| `TR_CURL_PROXY_SSL_NO_VERIFY` | Skip TLS cert verification for the proxy (undocumented) | `web.cc` |
| `CURL_CA_BUNDLE` | CA bundle path for tracker TLS (standard libcurl var) | `web.cc` |
| `TRANSMISSION_HOME` | Override the config dir | `platform.cc` |
| `TRANSMISSION_WEB_HOME` | Directory holding web-client files | `platform.cc` |
| `TR_SAVE_VERSION_FORMAT` | `4` or `5`; selects settings-file format (`5` = snake_case now) | `api-compat.cc` |
| `TR_AUTH` | `user:pw` for `transmission-remote --authenv` | `utils/remote.cc` |
| `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_DATA_DIRS`, `HOME` | Standard XDG lookups for config/data dirs | `platform.cc` |
| `CLUTCH_HOME` | Legacy override for the web-client dir (undocumented) | `platform.cc` |

**Stale in `docs/Environment-Variables.md`:** the doc lists `TR_DHT_VERBOSE`, but it no
longer exists in the source (grep finds it only in docs; the bundled `third-party/dht`
library exposes a `dht_debug` FILE* that nothing wires to an env var). Do not rely on it.

## settings.json keys most useful while debugging

Full list with defaults: `docs/Editing-Configuration-Files.md`. Remember the
edit-while-stopped rule (or `SIGHUP` to reload). Names below are snake_case, but the
default-format file on disk writes them **kebab-case** (`rpc-enabled`, `message-level`, …)
until `TR_SAVE_VERSION_FORMAT=5`; either spelling is accepted on read (see SKILL §2). A
handful that matter for a debug session:

- `rpc_enabled` — **must be `true` or nothing listens** and every RPC/web/`transmission-remote`
  call is refused (see SKILL §1). A fresh daemon config dir currently writes it `false`.
- `message_level` — log verbosity `0`–`6` (see SKILL §6).
- `rpc_authentication_required`, `rpc_username`, `rpc_password` — turn auth on/off. A
  plaintext `rpc_password` (not starting with `{`) is salted and rewritten on next start.
- `rpc_whitelist`, `rpc_whitelist_enabled` — default whitelist is `127.0.0.1,::1`; widen
  or disable it if you drive RPC from another host.
- `rpc_bind_address`, `rpc_port` — where RPC/web listens (default `0.0.0.0:9091`; daemon
  default whitelist still restricts callers).
- `download_dir`, `incomplete_dir`, `incomplete_dir_enabled` — where data lands.
- `peer_port`, `port_forwarding_enabled` — incoming peer port and NAT-PMP/UPnP mapping.
- `blocklist_enabled`, `dht_enabled`, `pex_enabled`, `lpd_enabled` — toggle subsystems to
  isolate behavior.

`transmission-daemon --dump-settings -g <dir>` prints the fully-resolved settings (defaults
+ file + any CLI overrides) as JSON without starting the daemon — the fastest way to see
what a config actually evaluates to.

## gdb recipes

Build with symbols first (building skill; `RelWithDebInfo` is the documented default,
`Debug` for un-optimized stepping). The daemon is multithreaded — always use
`thread apply all bt`, not plain `bt`.

Run under gdb (use `-f` so it does not daemonize away from the debugger):

```console
$ gdb --args build/daemon/transmission-daemon -f -g /tmp/tr-debug
(gdb) run
(gdb) thread apply all bt
```

Attach to an already-running daemon:

```console
$ gdb -p "$(pgrep -f transmission-daemon)"
(gdb) thread apply all bt
(gdb) continue
```

Post-mortem from a core dump:

```console
$ ulimit -c unlimited
$ build/daemon/transmission-daemon -f -g /tmp/tr-debug   # let it crash
$ gdb build/daemon/transmission-daemon core
(gdb) thread apply all bt
```

Catch a failing `TR_ASSERT` at the moment it fires (it ultimately calls `abort()`):

```console
(gdb) catch signal SIGABRT
(gdb) run
```

The other CLI tools (`transmission-remote`, `transmission-show`, `transmission-create`) are
short-lived and single-shot; debug them the same way with `gdb --args build/utils/<tool> …`.

## Performance work

`CONTRIBUTING.md` explicitly welcomes changes that "improve transfer speeds or peer
communication" and "reduce the app's footprint in CPU or memory use." But speed *is* the
product: measure every change — a prettier hot loop nobody profiled is a regression risk.

### Hot paths worth respecting (all `libtransmission/`)

- **`bandwidth.cc` / `.h`** — the clamp/allocate cycle: `tr_bandwidth::allocate()` hands each
  peer a per-pulse byte budget (`bytes_left_`) in fair-share `phase_one` passes, and
  `clamp(dir, n)` walks the parent chain to cap each I/O. Runs constantly, per peer.
- **`peer-io.cc`** — per-connection socket I/O: `inbuf_`/`outbuf_` buffering, `try_write`/read
  gated by `bandwidth().clamp()`.
- **`peer-mgr.cc`** — one of the largest core files: peer-info bookkeeping, connectability, rechoke
  and reconnect pulses, connection-candidate selection, on timers over every swarm.
- **`inout.cc` + `open-files.h`** — block read/write: `tr_ioRead`/`tr_ioWrite` fetch an fd via
  `get_fd` from `tr_open_files`, an LRU pool (`lru-cache.h`) of open files.
- **`verify.cc`** — piece hashing: `verify_torrent` streams each piece through an incremental
  `tr_sha1`, comparing `finish()` to `metainfo.piece_hash()`.

### Allocation discipline (see the cpp-conventions skill)

Hot code avoids heap churn with stack-first, small-buffer-optimized containers — don't swap in
plain `std::vector`/`std::map`:

- **`small::vector` / `small::map`** (`<small/vector.hpp>`, `<small/map.hpp>`) in the transfer
  path (`peer-mgr.cc`, `peer-msgs.cc`, `peer-mgr-wishlist.cc`); **`small::max_size_map`** for
  the fixed-capacity RPC dispatch tables in `rpcimpl.cc`.
- **`tr-buffer.h`** (`libtransmission::Buffer`, built on `small::vector`) — I/O buffers in
  `peer-io.h`, `peer-msgs.cc`, `handshake.cc`, `webseed.cc`.
- **`lru-cache.h`** — the bounded cache behind `open-files.h`.

### Measuring a change (local A/B)

No special harness; reuse the daemon mechanics from SKILL §1/§3/§8:

1. `transmission-create <dir> -o test.torrent -t <announce>` (§8).
2. Run a **seed** and a **leech** daemon on two isolated `-g` config dirs with **different
   `peer-port`s and RPC ports** so they don't collide on localhost (§1).
3. Point both at `test.torrent`; watch `transmission-remote -st` / `-l` on each (§3, and the
   two-daemon note in §8). Compare baseline vs. patched over an identical script.

### Profiling (standard Linux tooling, not repo-specific)

```console
$ perf record -p "$(pgrep -f transmission-daemon)" -- sleep 30
$ perf report
```

Build `RelWithDebInfo` (building skill) so symbols survive optimization; `valgrind
--tool=callgrind` / `massif` and the SKILL §10 sanitizer builds cover CPU vs. allocation.

## Where the pieces live (for going deeper)

- `daemon/daemon.cc` — CLI option table, config-dir resolution, foreground vs. daemonize,
  `SIGHUP` reload, log-stream setup.
- `libtransmission/log.{h,cc}` — global log level, level-name keys, message struct.
- `libtransmission/web.cc`, `announcer-http.cc` — the `TR_CURL_*` / `CURL_CA_BUNDLE` env
  var handling.
- `libtransmission/rpc-server.cc` — the 409 / session-id handshake and web-client serving.
- `libtransmission/env.cc`, `platform.cc` — env-var lookup semantics and config/data dirs.
- `utils/remote.cc`, `show.cc`, `create.cc`, `edit.cc` — the CLI tools' option tables.
