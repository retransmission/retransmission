# BitTorrent protocol (BEP) crosswalk

Which BitTorrent Enhancement Proposal maps to which file in this tree. Use it to
jump from "the spec for X" to the code that implements X, or the reverse. Every
path is under `libtransmission/` unless noted.

**Where to read the spec:** the canonical index is
`https://www.bittorrent.org/beps/bep_0000.html`; each BEP is
`https://www.bittorrent.org/beps/bep_00NN.html` (zero-padded to 4 digits). The
code already links there — e.g. `peer-msgs.cc:414` cites `bep_0006.html`. The
`wiki.theory.org/BitTorrentSpecification` page is cited in `handshake.h` for the
wire details BEP 3 leaves implicit.

How this was built: `grep -rn "BEP" libtransmission` (BEP numbers are cited in
code comments) plus the `quark.cc` key table, where most interned keys carry a
`// BEPxxxx` provenance comment. Re-verify with that grep if a row looks stale.

## Implemented / recognized

| BEP | Name | Where it lives (verified) |
| --- | --- | --- |
| **3** | The BitTorrent Protocol (bencoding, .torrent, peer wire, HTTP tracker) | Bencoding: `benc.h`, `variant-benc.cc`. Metainfo parse: `torrent-metainfo.cc`. Peer wire msgs: `peer-msgs.cc`. Bitfield wire format: `bitfield.h:62` ("raw is in BEP0003 format"). `announce` key: `quark.cc:54`. Non-BEP wire details: `handshake.h` cites `wiki.theory.org`. |
| **5** | DHT Protocol | `tr-dht.cc` + `tr-udp.cc`. **Packet parsing is delegated** to the bundled `third-party/dht/dht.c` (jech's lib); `tr-dht.cc:60-90` only supplies the callbacks (`dht_sendto`, `dht_random_bytes`, `dht_hash`, `dht_blacklisted`, `dht_gettimeofday`). Handshake DHT flag: `handshake.cc` (`DhtFlag`). |
| **6** | Fast Extension | `peer-msgs.cc:88-92` (`FextSuggest/HaveAll/HaveNone/Reject/AllowedFast` message ids), reject logic at `peer-msgs.cc:414` (cites `bep_0006.html`). Negotiated via reserved bit `FextFlag` in `handshake.cc`. |
| **7** | IPv6 Tracker Extension | `announcer-http.cc:245-247` (`ipv4=` / `ipv6=` announce params). |
| **9** | Extension for Peers to Send Metadata Files (`ut_metadata`) + Magnet URI | Metadata transfer: `torrent-magnet.h/.cc` (header says "defined by BEP #9"), advertised in `peer-msgs.cc:1072,1217`. `metadata_size` key: `quark.cc:341`. Magnet parse: `magnet-metainfo.cc/.h` (`magnet-metainfo.cc:219` notes the `x.y` piece notation vs BEP 9). |
| **10** | Extension Protocol (LTEP) | `peer-msgs.cc`: `parse_ltep`, `parse_ltep_handshake`, `send_ltep_handshake`, `"ltep"` (msgs `122`, `907-932`); "Extension Protocol in BEP 10" at `peer-msgs.cc:714`. Handshake keys `m`/`p`/`v`/`reqq`/`yourip`/`ipv4`/`ipv6`: `quark.cc` (each tagged `// BEP0010`). `LtepFlag` in `handshake.cc`. |
| **11** | Peer Exchange (PEX, `ut_pex`) | `peer-msgs.cc`: `parse_ut_pex`, `send_ut_pex`, `UT_PEX_ID` (`936-1060`). Keys `added`/`added.f`/`added6`/`dropped`/`ut_pex`: `quark.cc:32-36,167-168,725` (tagged `// BEP0011`). Gated by `tr_torrent::allows_pex()` (`torrent.h:664`). |
| **12** | Multitracker Metadata Extension | `announce-list.h/.cc` (`tr_announce_list`). `announce-list` key: `quark.cc:57`. Parsed in `torrent-metainfo.cc` (`AnnounceListKey`). |
| **14** | Local Service Discovery (LPD) | `tr-lpd.cc/.h` (`tr-lpd.cc:445,593` cite BEP14; matches the `BT-SEARCH * HTTP/` multicast message). Gated by `allows_lpd()` (`torrent.h:674`). |
| **15** | UDP Tracker Protocol | `announcer-udp.cc` (`TAU_ACTION_CONNECT/ANNOUNCE/SCRAPE/ERROR` at `:76`, `connection_id` handshake, `transaction_id` matching). |
| **17 & 19** | WebSeeding — HTTP/FTP seeding (BEP 17 `httpseeds`, Hoffman-style; BEP 19 `url-list`, GetRight-style) | `webseed.h/.cc`. **Both** keys are read and funneled into `tr_torrent_metainfo::add_webseed`: `torrent-metainfo.cc:289-314` (`UrlListKey` `"url-list"`, `HttpSeedsKey` `"httpseeds"`). URL fixup: `torrent-metainfo.h:197` (`fix_webseed_url`). |
| **21** | Extension for Partial Seeds (`upload_only`, downloader count) | `peer-msgs.cc:1228` ("upload_only (BEP 21)"). `upload_only` key: `quark.cc:707`. Tracker `downloader_count`: `types.h:639`. Scrape extension note: `announcer-common.h:205`. |
| **23** | Tracker Returns Compact Peer Lists | `&compact=1` request: `announcer-http.cc:185`. Parsing: `tr_pex::from_compact_ipv4/ipv6` (`peer-mgr.cc:1316`), called from `announcer-http.cc:363-365` and `announcer-udp.cc:280-283`. |
| **27** | Private Torrents | `tr_torrent_metainfo::is_private()` from the `private` flag (`torrent-metainfo.cc`, `quark.cc`); `torrent.h:540-547` (`is_private`/`is_public`). Gates DHT/PEX/LPD — all three `allows_*` in `torrent.h:664-676` require `is_public()`. |
| **29** | µTorrent Transport Protocol (µTP) | `peer-socket-utp.cc:34` (BEP-29 `wnd_size`), `tr-utp.cc` — wraps the bundled `third-party/libutp` (`#include <libutp/utp.h>`). `peer-socket-utp.h` is one of the two `tr_peer_socket` backends. |
| **32** | DHT Extensions for IPv6 | `tr-udp.cc:139` (BEP-32 explains the single-IPv6-address bind). |
| **40** | Canonical Peer Priority | `peer-mgr.cc:251-316` (three BEP-40 references; used to rank/dedup peers). |
| **48** | Tracker Protocol Extension: Scrape | `announcer-http.cc`/`announcer-udp.cc` scrape paths; keys `complete`/`incomplete`/`downloaded`: `quark.cc:106,158,249` (tagged `// BEP0048`). |

Message Stream Encryption (the peer-handshake obfuscation/encryption in
`handshake.cc` + `peer-mse.h/.cc`) is **not a BEP** — it is the Vuze/Azureus spec
(`peer-mse.h:19` cites `wiki.vuze.com/w/Message_Stream_Encryption`). Listed here
because people look for it alongside the BEPs.

## Recognized but NOT (yet) fully supported — do not claim these work

| BEP | Name | Status in code |
| --- | --- | --- |
| **47** | Padding Files and Extended File Attributes | `torrent-metainfo.cc:245,258` — keys seen, marked `// currently unused. TODO support for BEP0047`. |
| **52** | BitTorrent v2 | Partial. `torrent-metainfo.cc:502-526` recognizes `file tree` / `meta version` / `piece layers` / `pieces root`, but `torrent-metainfo.cc:130-133` logs `"'file tree' is ignored"` ("v2, ignore for today"); hybrid dedup is a `FIXME` at `:394`. v2 **magnet** hashes are parsed — `magnet-metainfo.cc:138` (`parseHash2`) accepts the `1220` sha256 multihash tag — so a v2/hybrid magnet is recognized even though a v2 `.torrent` is not fully honored. If asked "does Transmission do v2?", the answer today is **no, only v1 (+ v2 magnet-hash recognition)**. |

## Not found

No evidence for e.g. BEP 30 (Merkle), BEP 38 (mutable/infohash-in-torrent), BEP
44 (DHT store), BEP 46 (mutable magnet) as implemented subsystems. If you need
one, grep first — absence here means "not seen", not "proven absent".
