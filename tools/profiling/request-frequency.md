# Packet request frequency investigation, 2026-10-28

Small outbound buffers previously skipped the zero-delay flush when they held
fewer than 128 bytes. A request is 17 bytes, so one to seven requests could wait
for the next 500 ms bandwidth pass. Small interest, unchoke, and have messages
could also wait. Existing writable socket events could sometimes flush them sooner.

The change in `peer-io.cc` gives small buffers a 1 ms coalescing delay and then
uses the normal bandwidth-limited write path. Larger batches retain zero-delay
flushing. The delay decision happens in the callback, after headers and bodies
are queued, avoiding timer restarts for each short header. Further small writes
retain the deadline; reaching 128 bytes permits an earlier flush.

## Measurements

Native RelWithDebInfo, IPO ON, profiling OFF; local TCP, four disk workers,
full seed verification, Btrfs with zstd:3 compression. Both binaries contained
the same earlier local RC4 and buffer optimizations. Those changes and local
benchmark harness changes were present during measurement and are separate
from this packet-flush change.

Three alternating 1 GiB pairs per mode, with 4 MiB pieces:

| Mode | Before steady Gbit/s | After steady Gbit/s | Before transfer s | After transfer s |
| --- | ---: | ---: | ---: | ---: |
| Plaintext | 2.054 | 2.045 | 6.188 | 5.779 |
| Required encryption | 2.575 | 2.357 | 6.268 | 5.275 |

Values are medians. Plaintext steady throughput was essentially unchanged.
Encrypted steady throughput was 8.5% lower, despite 15.8% shorter whole-transfer
time. This unresolved tradeoff needs follow-up: these results establish a latency
improvement, not a bulk throughput improvement. The 10 Gbit/s target remains unmet.
Short transfers, warm caches, and a shared host limit generalization.

A separate 1 MiB plaintext diagnostic set `reqq=1` through `session_set` on both
daemons before adding torrents, with 256 KiB pieces. Whole-transfer duration
fell from **18.281 s to 1.782 s**. The old progress samples had a median 492.5 ms
gap between payload updates; the new transfer finished receiving within about
62 ms after its first payload sample. The baseline diagnostic overlapped a build,
so its exact duration is not a clean CPU comparison. The repeated half-second
waits identify the scheduling stall. The optimized window is too short to infer
sustained throughput or individual request latency from 20 ms RPC polling.

All twelve final bulk transfers and both diagnostics passed explicit torrent
verification and SHA-256 checks. An initial implementation's twelve transfers
also passed, but are excluded from the final table above.

## Validation

`SmallProtocolMessagesFlushWithoutBandwidthPulse` tests split writes of 5, 17,
119, 128, and 256 bytes outside the periodic bandwidth tree. The old version
left 5- and 17-byte writes unsent beyond the 100 ms test limit. The final version
passed all sizes. All 16 enabled peer-message, handshake, and timer tests passed;
four pre-existing timer tests remain disabled. Host socket access was required.
Build and `git diff --check` passed.

## Other scheduling findings

- Downloads refill on data-read callbacks and unchoke/have/reject events as well
  as the 500 ms maintenance pass; request batches are not generally limited to
  two per second.
- The request target is ten seconds of measured speed, with a 32-block floor
  bounded by the peer-advertised queue. The peer limit overrides the floor.
- This client advertises 2,000 requests: 31.25 MiB at 16 KiB per block, covering
  about 26.2 ms at 10 Gbit/s per peer. The 500-request fallback covers 6.55 ms.
- Disk workers share a default 64 MiB budget for requests and pending writes.
  Disk completions do not explicitly refill requests. If all requests drain
  while the budget is exhausted, maintenance appears to be the fallback for
  restarting downloads. This remains an unmeasured candidate.
- Timeout cleanup sorts accumulated requests and repeatedly erases vector
  entries. Wishlist selection scans candidates, including pieces without
  unrequested blocks. Neither has been established as the throughput bottleneck.
