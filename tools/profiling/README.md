# Retransmission profiling

The benchmark runs a local seeder, downloader, and private HTTP tracker inside
one container. It transfers a deterministic 1024 MiB file over TCP or µTP,
explicitly re-verifies the downloaded torrent, checks SHA-256, then shuts down
both daemons to write their code-block timings. An internal Docker network
isolates the benchmark from external networks; no ports are published. Image
building needs network access. Peers use the container's private IPv4 because
Retransmission rejects tracker-provided loopback addresses. Retransmission sends
UDP only once it has a route to the internet, so the container starts as root to
give that network a default route without a gateway, then runs the benchmark as
your UID.

From the repository root:

```sh
git submodule update --init --recursive
bash tools/profiling/run.sh
```

Docker must be accessible to the current user. See Docker's
[Linux post-installation instructions](https://docs.docker.com/engine/install/linux-postinstall/).
The script uses your UID/GID so result files belong to you.

For a longer workload or a build with instrumentation disabled:

```sh
SIZE_MIB=2048 TIMEOUT=600 bash tools/profiling/run.sh
BLOCK_PROFILING=OFF bash tools/profiling/run.sh
DISK_IO_WORKERS=4 bash tools/profiling/run.sh
```

When using `sudo`, pass these settings as script arguments so they survive
environment filtering:

```sh
sudo bash tools/profiling/run.sh --block-profiling OFF
sudo bash tools/profiling/run.sh --disk-io-workers 4
```

The script prints the selected settings before building. The container checks
that its profiling mode matches the requested mode. Confirm `block_profiling`
and `disk_io_workers` in `metrics.json` before comparing runs. An OFF build
must produce no timing CSV files.

`BUILD_JOBS` defaults to 2. Run several repetitions of each configuration and
compare throughput before drawing conclusions about instrumentation overhead.
The image uses `RelWithDebInfo`, frame pointers, and disabled IPO to preserve
useful stacks for subsequent sampling with a CPU profiler.

Docker builds reuse image layers plus persistent BuildKit caches for apt and
`ccache`. Every image compiles in a fresh build tree, and only the daemon and
torrent-creation binaries stay in the runnable image. Source edits reuse
previous compilation work through `ccache`; the first build must populate it. Build logs show cumulative `ccache` statistics when compilation
runs. Cache storage belongs to the Docker builder and can be removed by Docker
cache pruning. See Docker's [cache guide](https://docs.docker.com/build/cache/optimize/).

Use the same command for subsequent runs; changing `SIZE_MIB`, `TIMEOUT`, or
`DISK_IO_WORKERS` changes runtime settings and does not invalidate the image build.

`DISK_IO_WORKERS` defaults to 2, matching the application default. Workers handle
writes and finished-piece hashing; upload reads remain on the session thread.
Explicitly select 0 for synchronous I/O. Positive counts enable the `storage.*`
timing scopes in instrumented builds. Compare 0, 1, 2, and 4 with the worker study.

## Sustained Throughput

`steady_state` measures actual verified-byte deltas between the first observed
10% and 90% completion crossings. It uses wanted bytes completed, so duplicate
network bytes cannot inflate throughput. Each run still performs explicit final
torrent verification and SHA-256 validation. A window spanning less than half
the payload or zero time produces `null`, rather than a throughput estimate.

`transfer_gbit_per_second` includes startup and completion. The separate
`steady_state.gbit_per_second` excludes startup. `payload_rate_target_met`
compares that window against `TARGET_GBIT` (default 10). `target_met` additionally
requires a detected non-RAM work filesystem; tmpfs and unknown filesystems cannot
satisfy the disk-backed target. Decimal 10 Gbit/s is approximately
1,192 MiB/s. The window's actual duration and byte count are included: a short
1 GiB test alone does not establish that a rate can be maintained for minutes.
Use repeated runs and a larger `SIZE_MIB` for confirmation.

`PEER_ENCRYPTION` defaults to `preferred`, matching the application. `allowed`
prefers plaintext, while `required` requires encryption. Negotiated peer flags
are saved in progress samples and summarized by `observed_peer_encryption`.
Compare these modes explicitly; plaintext results do not establish encrypted
throughput.

```sh
sudo BLOCK_PROFILING=OFF PEER_ENCRYPTION=preferred bash tools/profiling/run.sh
sudo BLOCK_PROFILING=OFF PEER_ENCRYPTION=allowed DISK_IO_WORKERS=4 bash tools/profiling/run.sh
```

`PEER_TRANSPORT` (`--transport`) is `tcp` or `utp`; both daemons disable the other
transport. A run fails unless `observed_peer_transports` matches the request.
Idle peers connect over TCP, so `IDLE_PEERS` needs `tcp`.

```sh
sudo BLOCK_PROFILING=OFF PEER_TRANSPORT=utp bash tools/profiling/run.sh
```

`PIECE_SIZE_KIB` defaults to 256; use 4096 to measure the effect of larger
torrent pieces. `SEED_VERIFY_MODE` defaults to `fast`, which can defer most
seed-side hashing until upload. Set it to `full` to fully verify the seed
before the transfer timer starts; `seed_ready_seconds` records preparation.
This models an already verified seed and retains downloaded-piece hashing.
Record both settings when comparing results.

```sh
sudo BLOCK_PROFILING=OFF PEER_ENCRYPTION=allowed DISK_IO_WORKERS=4 \
    PIECE_SIZE_KIB=4096 SEED_VERIFY_MODE=full bash tools/profiling/run.sh
```

`WORK_STORAGE` defaults to `container-layer`. Set it to `bind` to use a host
directory directly, bypassing Docker's writable filesystem layer. The default
host directory is `tools/profiling/work/` in this repository. Override it
with `WORK_DIRECTORY=/path/on/the/download/disk`. Only a randomly named temporary
subdirectory is used and removed; existing files are left untouched. Filesystem
type, source, and mount options are saved in `work_filesystem`.

```sh
sudo BLOCK_PROFILING=OFF PEER_ENCRYPTION=allowed DISK_IO_WORKERS=4 \
    PIECE_SIZE_KIB=4096 SEED_VERIFY_MODE=full WORK_STORAGE=bind DISK_BASELINE=1 \
    bash tools/profiling/run.sh
```

`filesystem_benchmark.py` supplies the filesystem baseline helper.
`DISK_BASELINE=1` runs three fio cases on a private temporary file on that same
filesystem before the transfer: sequential 1 MiB writes and reads, then random
16 KiB writes with four jobs. Each runs for five seconds after a one-second
ramp, using libaio and direct I/O, with refilled buffers and an end fsync.
This is a storage-capacity reference, not a replica of Retransmission's buffered
I/O. Raw fio JSON/logs and a `disk-baseline.json` summary are saved. fio never
targets a raw device or an existing user file. See the
[fio documentation](https://fio.readthedocs.io/en/latest/fio_doc.html).

The downloaded file is also fsynced after transfer; `download_fsync_seconds`
reports time to flush pending file writes. Payload-window rates exclude this
final flush and do not alone establish durable device throughput.

To isolate the Docker filesystem's
cost, set it to `tmpfs` (or pass `--storage tmpfs`). This mounts the container's
/tmp in RAM. The benchmark records the choice as `work_storage`; result logs
remain in the host results directory. RAM-backed results measure a different
storage condition and must be labeled separately from container-layer results.

```sh
sudo BLOCK_PROFILING=OFF PEER_ENCRYPTION=allowed DISK_IO_WORKERS=4 \
    PIECE_SIZE_KIB=4096 SEED_VERIFY_MODE=full WORK_STORAGE=tmpfs bash tools/profiling/run.sh
```

The tmpfs limit defaults to 4 GiB (`TMPFS_SIZE=4g`). Both seed and download
payloads occupy RAM, approximately twice `SIZE_MIB`, plus daemon memory. Ensure
the host has enough available RAM; tmpfs can use swap under memory pressure.
The benchmark checks mount capacity before generating the payload.

`transfer_cpu` records per-daemon Linux user/system CPU deltas during the whole
transfer, including startup. A percentage is relative to one CPU core and may
exceed 100% with workers. Counters are omitted when /proc access is unavailable.

`thread_cpu` breaks the same interval down per thread, busiest first. With
profiling ON, each thread gets a role from the scopes it recorded: `session`,
`disk worker` or `verify`. A session thread near 100% means the single-threaded
peer path is the limit; well below it means the transfer is waiting on
something else.

`--disk-write-budget-mib` sets the daemon's `disk_write_budget_mib`: how much
downloaded data may be in flight toward the disk. It only applies with disk
workers. Omitted, the daemon default applies.

## Session-thread refill cost

`--idle-peers N` connects N inert TCP peers to a separate 1 MiB torrent on the
downloader before the timed transfer. They remain connected but supply no data.
The harness raises the downloader's peer limits, checks that all N peers are
connected before and after the transfer, and fails the run if any disappear.
This increases the number of peers visited by a request-refill pass without
changing the source of the payload. The Docker runner accepts `IDLE_PEERS=N` and
`DISK_WRITE_BUDGET_MIB=N`; native runs can pass both options to `benchmark.py`.

To measure the cost of a request-refill change, build it and its base commit
with the same compiler, CMake options, and profiling mode. Use this version of
`benchmark.py` with `--daemon` pointing to each binary. Run alternating
before/after pairs with `--idle-peers` set to 0, 100, and 500, four disk workers,
an 8 MiB disk write budget, and a payload large enough to produce a sustained
transfer window. Use at least three pairs per peer count. Keep storage,
encryption, piece size, and ports the same between paired runs.

The primary comparison uses profiling OFF: compare downloader CPU seconds per
GiB and verified-byte throughput. Run a second diagnostic set with profiling
ON to inspect `stall.write_budget_full` and `storage.completion_wait`. The
scope times are wall time, so use `thread_cpu` for CPU cost. Zero
`stall.write_budget_full` time means the write budget did not fill, so the run
did not exercise the budget path. The idle peers themselves add
maintenance work in both binaries; compare the **change** between binaries at
each peer count, rather than comparing 0 peers directly with 500 peers.

## Results

Each run creates a timestamped directory under `tools/profiling/results/`:

- `metrics.json`: transfer duration/throughput, verification duration, version,
  payload SHA-256, and environment details.
- `seed.csv`, `leech.csv`: counts, inclusive wall time, self wall time, and
  minimum/maximum duration per block and thread. `os_thread` is the Linux thread
  id, matching `thread_cpu`.
- `report.txt`: CPU per thread, then blocks ranked by self wall time, summed
  across threads.
- `transfer.jsonl`: timestamped progress samples, including received bytes,
  connected peers, sending peers, and download rate. `metrics.json` also records
  the first observed peer, sender, payload, and download-byte-counter milestones as seconds
  since the download was added. Transfer polling defaults to 20 ms (`POLL_INTERVAL`);
  observations also include RPC latency. A payload counter includes retransmitted
  bytes.
- `seed.log`, `leech.log`, `create.log`: daemon and torrent-creation logs.
- `failure.txt`: failure reason when a run does not complete.

Rebuild a report with:

```sh
python3 tools/profiling/report.py tools/profiling/results/<run>/leech.csv
```

Timings include startup, initial verification, transfer, explicit verification,
and shutdown. `total_ms` includes instrumented children on the same thread;
`self_ms` excludes them and their recording overhead. An asynchronous worker's
time belongs to that worker, not the submitting scope. These are wall times:
I/O waits, locks, and scheduling delays count. Inclusive rows overlap, and
parallel threads can accumulate more time than the elapsed benchmark duration.
Do not add inclusive rows or interpret these numbers as CPU percentages.
`session.close` happens after the transfer measurement. A large shutdown row
does not explain slow transfer throughput. Timer scopes measure execution of
selected blocks; time between callbacks, such as waiting for a peer to unchoke,
is visible in progress samples rather than those execution totals.

The benchmark uses warm filesystem caches, local networking, two competing
daemons, and the container's temporary filesystem. It measures this controlled
workload; it does not establish real-disk or internet throughput. Verification
throttling is set to zero. Startup and explicit verification poll at 100 ms;
transfer progress polls at 20 ms. No cache-dropping or host sysctl changes are performed.

## Code Blocks

Build any client with `-DENABLE_BLOCK_PROFILING=ON` and set a unique
`TR_PROFILE_OUTPUT=/path/to/profile.csv` for each process. Output is written at
normal process exit; a forced kill loses it. An unset or empty output path
disables recording. The build option defaults to OFF, compiling out the timers.

Instrument additional library functions or lexical blocks with:

```cpp
#include "libtransmission/profile.h"

void work()
{
    TR_PROFILE_SCOPE("subsystem.operation");
    // Work to measure.
}
```

Waits that span callbacks or threads have their own rows, with each wait
recorded as one call:

- `stall.write_budget_full`: from a request refill the disk write budget turned
  away to the next refill it admits.
- `stall.no_requests_outstanding`: from the last outstanding request draining to
  the next request sent.
- `storage.write_queue_wait`: a block admitted for writing until a disk worker
  takes it.
- `storage.completion_wait`: a finished disk op until the session thread runs its
  completion. Long waits mean the session thread was busy elsewhere.

A max near 500 ms in a `stall.*` row points at the peer manager's 500 ms
bandwidth pulse as what ended the wait. Instrument a wait with
`tr::profile::Interval` (start/stop) or `tr::profile::Stamp` (`record_since`).
Both change size with profiling, so keep them out of headers that code built
without `TR_ENABLE_BLOCK_PROFILING` includes.

Names must be string literals. Scopes must stay on their originating
thread. Recording takes a shared mutex and adds overhead to frequent calls;
use a sampling profiler to investigate hot blocks with less instrumentation.
The existing blocks cover session startup/shutdown, RPC dispatch, peer reads,
writes and message processing, bandwidth allocation, storage write batches,
memory-buffer hashing, disk reads/writes and hashing, and verification.

## Storage worker study

### Live disk load and write-copy comparison

Every native benchmark records the target filesystem's source block device
and, for partitions, the parent disk during transfer and final `fsync`.
`disk_load.py` reads Linux sysfs counters without generating an extra I/O
workload. `metrics.json` contains `disk_load.transfer` and `disk_load.flush`;
`diskload-transfer.jsonl` retains interval samples (at most every 100 ms), and
`diskload-flush.jsonl` retains endpoint snapshots around the blocking flush.
Measurements include read/write MiB/s, IOPS, average operation latency, queue
depth, requests in flight, and device busy percentage. These are device-wide
figures: other processes and delayed cached writes can contribute. Parent and
partition counters overlap; do not sum them. Busy percentage on NVMe is not a
measure of capacity saturation. Unsupported/hidden devices produce an explicit
`available: false` with an error instead of zero load. Multi-device filesystems
may use devices beyond the listed mount source. No fio test runs unless the
existing `--disk-baseline` option is explicitly enabled.

Compare an original copy/coalesce build with a vectored-write candidate using
two disk workers, the same 8 MiB budget, and no pinning:

```sh
python3 tools/profiling/compare-storage-writes.py \
    --before-build .tmp/binaries/storage-optimization/before \
    --after-build .tmp/binaries/storage-optimization/vectored \
    --output .tmp/results/storage-study/data/my-write-comparison \
    --work-dir .tmp/work --peer-address <local-IPv4>
```

A `pwritev` vectored-write candidate measured neutral and is not in the
application. Both builds must have profiling OFF; finish compilation before
measuring.
The script records binary hashes and runs three 4 GiB repeats per variant in
plaintext and RC4, alternating variant order. Each transfer verifies its data
and requires available disk-load counters. Reuse identical arguments/binaries
to resume; use a new output directory after changing either. `summary.md` shows
median throughput, downloader CPU, and target partition/device load. Per-run
metrics retain separate transfer and flush measurements.

`storage-study.py` compares 1, 2, and 4 disk workers with a fixed 8 MiB write
budget and 256 KiB pieces, without CPU pinning. It runs three 4 GiB
transfers per worker count in plaintext and RC4, rotating worker order between
repetitions. Finish both builds before running the study; concurrent compilation
would contaminate the timing measurements.

For an uninstrumented comparison of synchronous I/O with one and two workers:

```sh
python3 tools/profiling/storage-study.py --timing-build build --timing-only \
    --workers 0 1 2 --output .tmp/results/storage-study/data/workers-012 \
    --work-dir .tmp/work --peer-address <local-IPv4>
```

This runs 18 transfers (three repeats, two encryption modes, three counts),
without an instrumented build. Worker count 0 uses the synchronous backend;
its I/O runs on the session thread and the worker write budget does not apply.
The summary includes live target-device disk load when available, with final
flush measured separately. Other worker lists can be selected with `--workers`.

```sh
python3 tools/profiling/storage-study.py \
    --timing-build build --diagnostic-build .tmp/build/storage-diagnostics \
    --output .tmp/results/storage-study/data/my-run --work-dir .tmp/work \
    --peer-address <local-IPv4>
```

The timing build must have `ENABLE_BLOCK_PROFILING=OFF`; the diagnostic build
must have it `ON`. Each path must contain `CMakeCache.txt`,
`daemon/transmission-daemon`, and `utils/transmission-create`. Use the same source
and record the build options when interpreting results. An existing output is
resumed only with matching study arguments and daemon binaries. Extra worker counts may be appended
while keeping all other arguments unchanged; the settings history records this
extension. Completed runs are skipped.

Diagnostic runs additionally enable `TR_PROFILE_WRITE_BATCHES=1`. With block
profiling enabled and `TR_PROFILE_OUTPUT` set, this writes
`<profile-path>.batches.tsv` on normal exit. Each row contains the OS worker ID,
torrent ID, torrent byte offset (not a physical disk offset), bytes, block count,
mean/max block queue delay within the batch, `tr_ioWrite` wall duration, and error
code. Queue delays end when the worker dequeues the block. Write duration excludes
batch-buffer assembly but includes the write helper's internal work.

`batch-summary.json` reports median, p95, maximum, and mean across batches.
Queue-delay percentiles therefore describe per-batch means/maxima, not individual
blocks. `summary.md` compares only uninstrumented throughput. Diagnostic traces
are retained in memory until shutdown and add allocation/locking overhead; use
them to explain behavior, not to measure production throughput. No batch tracing
code is compiled into builds with block profiling disabled.

## Checks Without Docker

For a native benchmark alongside a running client, choose unused ports:

```sh
mkdir -p tools/profiling/work
python3 tools/profiling/benchmark.py --daemon build/daemon/transmission-daemon \
    --create build/utils/transmission-create --peer-address <local-IPv4> \
    --peer-port 52413 --rpc-port 19191 --work-dir tools/profiling/work
```

The seed uses the selected ports and the downloader uses the next port of each
pair. The harness checks ports before starting each daemon and fails if a port
is occupied. The work directory must already exist.

```sh
python3 -m unittest discover -s tools/profiling -v
docker compose -f tools/profiling/compose.yaml config --quiet
```

The standalone checks compile the timer with and without instrumentation and
check nesting, threads, exception unwinding, CSV escaping, CSRF negotiation,
RPC errors, completion detection, and daemon crashes. They need Python 3.11+
and a C++17 compiler. The full transfer still needs the Docker benchmark.
