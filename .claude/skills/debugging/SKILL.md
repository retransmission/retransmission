---
name: debugging
description: >-
  How to run and hand-debug Transmission locally: launch an already-built
  transmission-daemon in the foreground on a throwaway --config-dir (if it won't even
  compile, that is the building skill), watch it over
  transmission-remote or raw curl (the X-Transmission-Session-Id / HTTP 409 handshake),
  raise log verbosity, set TR_* env vars, inspect .torrent files with transmission-show,
  mint test torrents with transmission-create, get a gdb backtrace, and profile CPU/memory
  usage (perf, valgrind callgrind/massif). Load this WHENEVER
  you: need to reproduce a bug by running the daemon or a CLI tool, are told "run it
  locally", "poke the RPC", "turn on debug/trace logging", "why does settings.json keep
  reverting", "get a stacktrace/backtrace", "enable curl/tracker verbose logging", "build
  with ASan/UBSan", "profile the daemon / measure a perf or memory change", or need a live
  daemon to hand-test an RPC or web-UI change. Covers
  daemon/, utils/ (transmission-remote/-show/-create/-edit), settings.json, log levels, and
  TR_CURL_VERBOSE / TRANSMISSION_HOME / TR_AUTH. NOT for writing unit tests (testing skill),
  configure/build flags (building skill), or the RPC spec itself (rpc-api skill).
---

# Debugging Transmission locally

The core loop: **build → run the daemon in the foreground on a throwaway config → drive
it over RPC → read the logs → narrow the repro**. This skill owns that loop. It does not
cover configuring/building (that is the **building** skill — it documents `build/` and
where each binary lands), writing unit tests (**testing** skill — usually the *fastest*
repro once you know the code path), or the RPC wire format (**rpc-api** skill).

All commands below assume the repo-root `build/` tree and are run from the repo root.
Binary paths are verified against `build/daemon/` and `build/utils/`.

## 1. Run the daemon in the foreground on a throwaway config

```console
$ build/daemon/transmission-daemon -f -g /tmp/tr-debug --log-level=debug
```

Why each flag (all verified in `daemon/daemon.cc`'s `Options` array):

- **`-f` / `--foreground`** — by default the daemon `fork()`s into the background
  (`tr_daemon::spawn()` in `daemon/daemon-posix.cc` takes the `!foreground` path and
  daemonizes). `-f` keeps it attached to your terminal, and `tr_daemon::init()` then points
  the log stream at **stderr**, so you see logs live and stop it with Ctrl-C. Required for
  gdb and for watching a repro unfold.
- **`-g <dir>` / `--config-dir`** — an isolated, disposable config dir. The default is
  `$TRANSMISSION_HOME`, else `~/.config/transmission-daemon/` (the daemon passes its own
  app name `transmission-daemon` to the default-dir lookup). Point it at a scratch dir so
  you never touch your real torrents/settings and every run starts from known state.
  `rm -rf /tmp/tr-debug` resets everything.
- **`--log-level=<level>`** — `critical`, `error`, `warn`, `info` (default), `debug`, or
  `trace`. See §6.

On first start the daemon writes `settings.json` into the config dir.

> **Gotcha — a fresh config dir currently (`main`, 2026-07) comes up with RPC *disabled*.**
> `transmission-remote`, curl, and the web UI all get connection-refused ("Unable to send
> request") until it is on. `rpc-enabled` defaults to `false` in libtransmission
> (`session-settings.h`); the daemon's intended `rpc-enabled=true` default is silently lost
> because `load_settings()` (`daemon.cc`) backfills with `tr_variant::Map::merge`, which
> never overwrites an already-set key. **Rule: connection refused ⇒ check `"rpc-enabled"` in
> `<config-dir>/settings.json`.** Fix a scratch dir by starting once, Ctrl-C, setting
> `"rpc-enabled": true` (kebab spelling — see §2) while stopped, then restarting (or reuse a
> config dir that already has it on). `-p` sets the RPC *port* but does **not** enable the
> server.

When enabled, RPC + web listen on `0.0.0.0:9091`; the `127.0.0.1,::1` default is the
*whitelist* gating callers, not the bind address. Other handy one-offs: `-w <dir>`
(download dir), `-p <port>` (RPC port), `-t`/`-T` (require/skip auth), `-P <port>` (peer
port), `--dump-settings` / `-d` (print effective settings as JSON and exit — great for
confirming what a config actually resolves to). Run `transmission-daemon --help` for the
full list; do not trust memory for flag spellings.

## 2. settings.json and the edit-while-stopped rule

Lives at `<config-dir>/settings.json` (default
`~/.config/transmission-daemon/settings.json`). **The daemon rewrites this file on exit**,
so a live edit is silently clobbered. Two safe ways to change it:

1. Stop the daemon (`transmission-remote --exit`, or Ctrl-C in foreground), edit, restart.
2. Edit while running, then reload **without** exiting by sending `SIGHUP`:
   `pkill -HUP transmission-daemon` (the daemon logs "Reloading settings from …").

The authoritative key reference is **`docs/Editing-Configuration-Files.md`** (bandwidth,
peers, RPC, queuing, scheduling — every key with defaults). Note the repo is mid-migration
from kebab-case to snake_case keys; Transmission 4 still defaults to kebab-case, so the file
on disk shows the **kebab** spellings (`rpc-enabled`, `message-level`, …) — grepping it for
this skill's snake_case names finds nothing. Both spellings are accepted on read; force the
snake_case output with `TR_SAVE_VERSION_FORMAT=5` (see §7). To *add* a new setting, use
the **add-session-setting** skill. Keys most useful while debugging are collected in
`references/reference.md`.

## 3. Watch it with transmission-remote

`transmission-remote` is the built-in RPC client. First arg is an optional
`[host][:port]`; it defaults to **`localhost:9091`** (verified in `utils/remote.cc`), so
you can usually omit it.

```console
$ build/utils/transmission-remote -l                 # list all torrents
$ build/utils/transmission-remote -t 1 -i            # torrent 1 details
$ build/utils/transmission-remote -si                # session-info (settings)
$ build/utils/transmission-remote -st                # session-stats (totals)
$ build/utils/transmission-remote -a ./test.torrent  # add a torrent (file/URL/magnet)
$ build/utils/transmission-remote -t 1 --files       # per-file list for torrent 1
$ build/utils/transmission-remote -t 1 --info-trackers  # per-tracker announce/scrape status
$ build/utils/transmission-remote --exit             # shut the daemon down cleanly
```

`--info-trackers` / `-it` is the command that answers *what is the announce doing* — last
announce result, success/timeout, next-announce countdown, and peer counts returned. The
`-t 1 -i` details view above does **not** request `trackerStats`, so it shows none of that.

Two flags earn their keep when debugging:

- **`-b` / `--debug`** — prints the **raw JSON request and response** for each call. This
  is the quickest way to see exactly what the client sends and the daemon returns without
  dropping to curl.
- Auth (when `rpc_authentication_required` is on): `-n user:pw`, or `--authenv` (reads
  `user:pw` from the `TR_AUTH` env var), or `-N <netrc-file>`.

A fuller flag cheat sheet is in `references/reference.md`. For the JSON method/argument
semantics behind these, see the **rpc-api** skill and `docs/rpc-spec.md`.

## 4. Raw RPC over curl (the 409 / X-Transmission-Session-Id dance)

Reach for curl to reproduce a client's exact bytes, script against the daemon, or debug the
CSRF handshake itself. The RPC endpoint is `http://<host>:<port>/transmission/rpc` (path =
`TR_PROJ_WEB_SERVER_BASE_PATH` `/transmission/` + `rpc`, verified in `libtransmission/`
`constants.h` + `macros.h`).

Every request must carry a valid **`X-Transmission-Session-Id`** header or the server
replies **HTTP 409** — a CSRF guard. The 409's response *headers* carry two
`X-Transmission-*` fields: **`X-Transmission-Session-Id`** (the id to echo back) and
**`X-Transmission-Rpc-Version`** (currently `6.1.0`, a semver advertisement newer clients
read to pick the Tr5 JSON-RPC dialect — see the **rpc-api** skill). The handshake is: send
once, read the session id off the 409, resend with it.

```console
# Step 1 — first call gets a 409; capture the session id from the response headers
$ curl -sD - http://localhost:9091/transmission/rpc -o /dev/null | grep -i X-Transmission-Session-Id
X-Transmission-Session-Id: ABC123...

# Step 2 — resend with that header and a JSON body (this makes it a POST)
$ curl -s http://localhost:9091/transmission/rpc \
    -H 'X-Transmission-Session-Id: ABC123...' \
    -d '{"method":"session-get"}'
```

Scripted one-liner that does both steps (add `-u user:pw` if auth is on):

```console
$ SID=$(curl -sD - http://localhost:9091/transmission/rpc -o /dev/null \
        | grep -i X-Transmission-Session-Id | tr -d '\r' | awk '{print $2}')
$ curl -s -H "X-Transmission-Session-Id: $SID" \
    -d '{"method":"torrent-get","arguments":{"fields":["id","name","status"]}}' \
    http://localhost:9091/transmission/rpc
```

> **Gotcha — read the session id from headers only.** The 409 *body* also prints the literal
> line `X-Transmission-Session-Id: <sid>` (inside a `<code>` block), so a lazy
> `curl -si … | grep -i session-id` matches **two** lines; feeding that into `-H` embeds a
> newline and libevent replies with a bare, unstyled **`400 Bad Request`** that looks like a
> server bug. That is why the recipes above capture headers only (`-sD - -o /dev/null`).

The header name is *derived* from `TR_PROJ_APPNAME_CAPITALIZED` (currently `Transmission`,
so `X-Transmission-Session-Id`) — a rebrand would change it. Build it from the macro in
code, never hardcode the literal (see the **cpp-conventions** and **rpc-api** skills). The
server side of this handshake is documented in the **rpc-api** skill.

## 5. The web UI as a dashboard

The daemon serves the web client at **`http://localhost:9091/transmission/web/`** (verified
in `libtransmission/rpc-server.cc`). Open it in a browser for a live view of torrents while
you poke the backend — no separate server needed. If it serves a page saying it can't find
the web-interface files, the daemon can't locate the assets; point **`TRANSMISSION_WEB_HOME`**
at a web build (see §7). Developing the UI itself (asset rebuilds, lint) is the
**web-client** skill.

## 6. Log levels (global, not per-module)

Verbosity is a **single global level**, not per-subsystem. `tr_logSetLevel()` sets one
level and `tr_logLevelIsActive()` compares every message against it (`libtransmission/`
`log.cc`). Each message carries a module/torrent `name`, but that is only a **label in the
output**, not a filter — there is no way to raise verbosity for just "DHT" or just "RPC".
Don't burn time looking for one; raise the global level and grep the output.

Level names (from `log.cc`'s `LogKeys`): `off`, `critical`, `error`, `warn`, `info`,
`debug`, `trace`. Two ways to set it:

- Daemon flag: `--log-level=debug` (or `trace`). Deprecated `--log-error/--log-info/`
  `--log-debug` still work but warn.
- `settings.json`: `"message_level"` as a number — `0`=none, `1`=critical, `2`=error,
  `3`=warn, `4`=info (default), `5`=debug, `6`=trace. This is read back on a `SIGHUP`
  reload (`session.cc` reads `TR_KEY_message_level` when applying settings), so you can
  change verbosity on a running daemon without restarting it (see §2).

Capture to a file with `-e <path>` / `--logfile <path>` (the daemon reopens it on `SIGHUP`,
so log rotation works). In the foreground without `-e`, logs go to stderr. Note `-e` only
redirects Transmission's own `tr_log` stream: libcurl's `TR_CURL_VERBOSE` output (§7) is
written straight to the process's real stderr and is **not** captured by `-e` — to keep both
in one file, redirect the shell yourself (`… 2>&1 | tee run.log`).

## 7. Environment variables

Verified present in the current source (each with its `libtransmission/` location). The
canonical doc is `docs/Environment-Variables.md`, but **that doc is partly stale** — see
the note below.

- **`TR_CURL_VERBOSE`** — libcurl verbose logging for tracker HTTP(S) announces/scrapes
  (`web.cc`, `announcer-http.cc`). The best switch for "http(s) tracker won't connect"
  debugging. **Caveat:** it only instruments the curl path — `udp://` trackers (common in
  public tracker lists) never touch curl (`announcer-udp.cc`), so it prints *nothing* for
  them; use `--log-level=trace`, which covers the udp announcer's own logdbg/logtrace calls.
- **`TR_CURL_SSL_NO_VERIFY`** / **`TR_CURL_PROXY_SSL_NO_VERIFY`** — skip TLS cert
  verification for trackers / the proxy (`web.cc`). The proxy one is undocumented.
- **`TRANSMISSION_HOME`** — override the config dir (`platform.cc`).
- **`TRANSMISSION_WEB_HOME`** — where to find web-client files (`platform.cc`); see §5.
- **`TR_SAVE_VERSION_FORMAT`** — `4` or `5`, selects the settings-file format
  (`api-compat.cc`); `5` opts into snake_case now.
- **`TR_AUTH`** — `user:pw` read by `transmission-remote --authenv` (`utils/remote.cc`).
- **`CURL_CA_BUNDLE`** — standard libcurl var, honored for tracker cert bundles (`web.cc`).

> **Gotcha:** the `TR_CURL_*` flags are checked for *existence*, not truthiness
> (`tr_env_key_exists` in `libtransmission/env.cc` just tests `getenv() != nullptr`), so
> `TR_CURL_VERBOSE=0` and `TR_CURL_VERBOSE=` **both enable** verbose; `unset` to turn off.
> The value-carrying vars above (`TRANSMISSION_HOME`, `TRANSMISSION_WEB_HOME`,
> `CURL_CA_BUNDLE`, …) are read by value instead, and an empty value is treated as unset.

> **Stale doc:** `docs/Environment-Variables.md` still lists **`TR_DHT_VERBOSE`**, but no
> such variable exists in the source anymore (grep confirms zero hits outside docs). Don't
> rely on it. The full annotated env-var table is in `references/reference.md`.

## 8. Inspect torrents and make test data

- **`transmission-show <file.torrent>`** — dump a torrent's metainfo: name, size, piece
  count/size, tracker tiers, file list, info hash. Flags (from `utils/show.cc`): `-m`
  (print a magnet link), `-s` (`--scrape`: ask the trackers how many peers are in the
  swarm — a live network call), `-d` (`--header`: header section only).
- **`transmission-create <file|dir> -o out.torrent -t <announce-url>`** — mint a
  `.torrent` for local testing. Useful flags (from `utils/create.cc`): `-p` (private),
  `-s <KiB>` (piece size), `-c <comment>`, `-w <url>` (webseed), `-x` (anonymize — omit
  creation date/client). Point two daemons on isolated config dirs at the same created
  torrent to reproduce peer-to-peer behavior locally.
- **`transmission-edit`** — batch-rewrite announce URLs in existing `.torrent` files
  (`utils/edit.cc`); handy for repointing a test torrent at a local tracker.

## 9. Backtraces with gdb

Build with debug info first — the building skill's default `CMAKE_BUILD_TYPE=RelWithDebInfo`
already includes symbols (use `Debug` for un-optimized stepping). Then:

```console
$ gdb --args build/daemon/transmission-daemon -f -g /tmp/tr-debug
(gdb) run
# ...reproduce the crash/hang...
(gdb) thread apply all bt      # the daemon is multithreaded — get ALL threads
```

Use `-f` so the process stays in the foreground; gdb does not follow the background
daemonizing fork by default. For a crash you can't catch interactively, enable core dumps
(`ulimit -c unlimited`) and post-mortem with `gdb build/daemon/transmission-daemon core`.
More gdb recipes (attach to a running daemon, catch an assert) are in
`references/reference.md`.

## 10. Sanitizers (no CMake toggle)

There is **no** project CMake option for ASan/UBSan/TSan — grepping the build files finds
only `-DFASTFLOAT_SANITIZE=OFF`, which is a third-party knob, not ours. You enable
sanitizers by passing the compiler flags yourself, into a **separate** build dir (keep your
normal `build/` clean):

```console
$ cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRUN_CLANG_TIDY=OFF \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS='-fsanitize=address,leak,undefined -fno-omit-frame-pointer' \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,leak,undefined -fno-omit-frame-pointer'
$ cmake --build build-asan -t transmission-daemon
```

This is close to how CI does it — the `sanitizer-tests-linux` job in
`.github/workflows/actions.yml` builds with `clang`, `CMAKE_BUILD_TYPE=Debug`, and
`-fsanitize=address,leak,undefined` (the flags above). The `sanitizer-tests-macos` job uses
`address,undefined` only (no LeakSanitizer), and there is a `sanitizer-tests-windows` job on
MSVC `/fsanitize=address`. To reproduce a CI sanitizer failure specifically, build the
`all-tests` target under these flags and run it via the **testing** skill (the CI job
disables the daemon/CLI and runs the suite, not a live daemon). See the **building** skill
for configure basics.

## 11. Profiling and performance

No profiling harness is wired in (like sanitizers, §10): profile a `RelWithDebInfo` daemon with
standard Linux tools — `perf record -p "$(pgrep -f transmission-daemon)"` + `perf report`, or
`valgrind --tool=callgrind`/`massif` — and A/B every perf/footprint change with a two-daemon
seed↔leech run. Hot paths and the full recipe: **"Performance work"** in `references/reference.md`.

## When a unit test is the better tool

If you can express the bug as a function-level repro, a GoogleTest case is faster to run and
rerun than a live daemon and it lands as a regression guard. See the **testing** skill.
