# Untrusted-input surfaces (a review lens)

A BitTorrent client parses bytes from anonymous peers, trackers, magnet links,
downloaded blocklists, and LAN multicast. This file inventories every
attacker-influenced parse path so a review can ask "what feeds this, and what
guards it?" It is a **defensive checklist**, not exploit material: each row names
the files, what the input is, what to stay paranoid about, and an *existing*
hardening pattern in the same code worth imitating when you add a new parser.

Every path is under `libtransmission/` unless noted. Re-verify a guard before
trusting it — line numbers drift.

## The core reusable guard: the bounded benc parser

`benc.h` (the generic bencode parser) is the single most important pattern here,
because nearly every untrusted blob is benc. `ParserStack<MaxDepth>::push()`
(`benc.h:236`) returns `E2BIG` the moment nesting would exceed a **caller-chosen**
compile-time depth cap — so an attacker can't blow the stack with `dddd…`. Each
caller picks a cap sized to its data: metainfo `MaxBencDepth = 32`
(`torrent-metainfo.cc:56`), tracker responses `MaxBencDepth = 8`
(`announcer-http.cc:67`), the general-purpose `tr_variant` parser `512`
(`variant-benc.cc:246`). **When you add a benc parse path, pick the smallest cap
that fits your schema** — don't reuse 512 by reflex.

## Surface inventory

| # | Files | Untrusted input | Be paranoid about | Existing hardening to imitate |
| --- | --- | --- | --- | --- |
| 1 | `torrent-metainfo.cc`, `benc.h`, `variant-benc.cc` | A `.torrent` file / info-dict bytes (from disk, RPC, or reconstructed over BEP 9) | Deep nesting; integer overflow in piece count/length; huge allocations; `piece length` vs total-size mismatch; path traversal in file names | `MaxBencDepth=32` cap (`:56`); post-parse sanity block at `:441-467` rejects zero files, zero `piece length`, and `piece_count` ≠ `pieces` size; file names go through `sanitize_subpath` (row 13). |
| 2 | `variant-benc.cc`, `benc.h`, `variant-json.cc` | Any benc/JSON blob (wire, disk, RPC) | Same nesting/overflow; malformed UTF-8 in JSON | Bounded `ParserStack<512>` (`variant-benc.cc:246`); typed accessors on the result rather than raw indexing. |
| 3 | `magnet-metainfo.cc/.h` | A magnet URI (user paste, RPC, web-added link) | Malformed `xt=urn:btih`/`btmh`; wrong hash length; base32-vs-hex confusion; oversized `dn=` / unbounded `tr=`/`ws=` lists | `parseHash`/`parseHash2` validate exact hash length and the `1220` sha256 multihash tag (`magnet-metainfo.cc:138-239`); nonconformant notation is silently ignored, not trusted. |
| 4 | `handshake.cc`, `peer-mse.cc/.h`, `tr-arc4.h` | The first bytes from an unauthenticated TCP/µTP peer, incl. the MSE encrypted handshake | Wrong protocol name; info_hash for a torrent we don't run; DH/RC4 state confusion; attacker-chosen PadA/PadB/PadC lengths; resync scan for the obfuscated `VC` | Reject on name mismatch (`:250`), on unknown/not-running info_hash (`:268`), and on outgoing hash mismatch (`:278`); MSE key/private-key sizes are fixed `constexpr` (`peer-mse.h:31-42`), not peer-controlled. |
| 5 | `peer-msgs.cc`, `peer-io.cc`, `torrent-magnet.cc` | Length-prefixed peer wire messages + `ut_metadata`/`ut_pex` extension payloads from a connected peer | Message length vs actual buffer; out-of-range piece/offset in `request`; unknown LTEP ids; `ut_metadata` piece index/len; unbounded PEX lists | Unknown LTEP ids are logged and skipped, not dispatched (`peer-msgs.cc:932`); `set_metadata_piece` checks the piece index range **and** exact expected length before copying (`torrent-magnet.cc:257-266`), and the assembled info-dict is verified against the info_hash before it is accepted (`torrent-magnet.cc:194`); metadata is hard-capped at `MetadataPieceSize=16 KiB`/piece with `is_valid_metadata_size` (`torrent-magnet.h:26-35`). |
| 6 | `announcer-http.cc`, `web.cc` | The bencoded body of an HTTP(S) tracker announce/scrape response | Malformed benc; enormous peer lists; compact-peer buffer walked past its end | `MaxBencDepth=8` (`:67`); compact peers parsed at fixed stride via `tr_pex::from_compact_ipv4/ipv6` (`:363-365`), which consume length explicitly. |
| 7 | `announcer-udp.cc` | UDP tracker datagrams — **unauthenticated and trivially spoofable** | Off-path response spoofing; short/truncated buffers; action/transaction mismatch | A random per-request `transaction_id` is matched against every response before it is believed (`:684-716`); explicit length checks like `buflen >= 3*sizeof(uint32_t)` (`:273`); the BEP 15 `connection_id` handshake gates announces. |
| 8 | `tr-dht.cc` + **bundled** `third-party/dht/dht.c` | DHT KRPC packets over UDP | The real packet parsing is in the **vendored C library**, not this repo's C++ — the trust boundary is the callback surface | Parsing is delegated to jech's `dht.c`; `tr-dht.cc:60-90` only provides `dht_sendto`/`dht_random_bytes`/`dht_hash`/`dht_blacklisted`. **Review implication:** audit the pinned submodule version (see third-party-deps skill), not just `tr-dht.cc`; keep `dht_random_bytes` cryptographically strong. |
| 9 | `tr-lpd.cc` | Multicast UDP `BT-SEARCH` datagrams from anyone on the LAN | Flooding/DoS; malformed headers; being tricked into announcing to yourself | Fixed `MaxDatagramLength` recv buffer; require the `BT-SEARCH * HTTP/` prefix before parsing; rate-limit via `MaxIncomingPerUpkeep`; drop wrong-version messages and your own `cookie` (`tr-lpd.cc:445-465`). |
| 10 | `rpc-server.cc`, `rpcimpl.cc`, `api-compat.cc` | HTTP requests (JSON/benc bodies + headers), reachable from a browser or the LAN | CSRF from a web page; auth bypass; DNS-rebinding via forged `Host`; whitelist bypass | X-Transmission-Session-Id CSRF token enforced with the 409 re-request flow (`rpc-server.cc:454-593`); HTTP Basic checked against a **salted-SHA1** hash via `tr_ssha1_matches` (`:481`), never plaintext compare; IP whitelist (`:390`) **and** host/DNS-rebinding whitelist (`:425-449`). Treat the whole rpc-api surface as public — see the rpc-api skill. |
| 11 | `blocklist.cc` | Downloaded/user blocklist files (P2P, eMule, CIDR text) and a precompiled `.bin` | Malformed lines; reversed/invalid ranges; a hostile binary `.bin` header mmap'd directly | Every line parser returns `std::optional` and tolerates junk (`blocklist.cc:98-207`); unparseable lines are skipped with a logged base64 dump (`:251`); the `.bin` fast-path is accepted only after its `BinContentsPrefix` magic matches (`:354`) before it is memory-mapped. |
| 12 | `resume.cc` | Benced `.resume` state files loaded from the config dir at startup | Corrupt/tampered fields; hostile peer lists if the config dir is shared or synced | Goes through the same bounded benc parser; reads via typed `tr_variant::Map::find_if<T>` accessors (`resume.cc:73-107`) so a wrong-typed field is ignored rather than mis-cast. |
| 13 | `torrent-files.cc/.h`, `watchdir-*.cc` | File paths inside a torrent, and filenames appearing in a watched directory | Path traversal (`../`), absolute paths, reserved device names, writing outside the download dir | `append_sanitized_component` / `sanitize_subpath` reject `.`/`..` and reserved components (`torrent-files.cc:333-472`); `is_subpath_sanitized` (`torrent-files.h:165`) is the predicate to assert with before using any peer-supplied path on disk. |

## Review heuristics

- **Follow the bytes to a cap.** For any new parser of remote data, the first
  question is "what bounds the size and the nesting?" If the answer isn't a
  compile-time cap or an explicit length check, that's the finding.
- **Spoofable transports need a nonce.** UDP (tracker, DHT, LPD) has no
  connection; the pattern here is a random transaction/cookie matched on reply
  (rows 7–9). New UDP request/response code must do the same.
- **Verify hashes before trusting reconstructed data.** BEP 9 metadata is only
  accepted after the whole info-dict hashes to the expected info_hash (row 5) —
  mirror that for anything reassembled from peers.
- **Vendored C parsers are still your attack surface.** DHT (and µTP) parsing
  lives in `third-party/`; a CVE there is a CVE here. Track submodule pins.
