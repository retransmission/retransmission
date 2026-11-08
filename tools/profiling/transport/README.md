# Transport benchmarks

Use these tools when a change touches how peers move bytes: peer IO, peer
sockets, bandwidth allocation or libutp. The daemon benchmark in
[../README.md](../README.md) measures the same transfer, but with disk writes,
hashing and handshakes in it as well, so a transport change can disappear in
its noise. These tools time the transport alone. Run the commands from the
repository root.

| To find out | Use |
|---|---|
| peak throughput and CPU cost per GiB of TCP and µTP | `transport-benchmark` on loopback |
| how they behave with real delay, a slower link or loss | `transport-link.py` |
| whether µTP yields to the link and keeps queueing delay low | `transport-link.py --choke` |
| where the CPU time goes | `transport-link.py --perf` |

## Example: checking a µTP change

On a Debian or Ubuntu machine, install Docker and the tools that set up the
link. The container runs the host's own `tc`, `ip`, `ping` and `setpriv`, so
they must be on the host. Membership in the `docker` group is equivalent to
root on that machine; log in again after adding it.

```sh
sudo apt install docker.io iproute2 iputils-ping util-linux
sudo apt install linux-tools-$(uname -r)    # only for --perf
sudo usermod -aG docker $USER
```

Build the benchmark with the tests enabled:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON
cmake --build build -t transport-benchmark
B=build/tests/libtransmission/transport-benchmark
```

Start on loopback, for peak rate and CPU cost:

```console
$ $B --transport both --size-mib 256
transport  size_mib  connect_s  transfer_s    MiB/s  Gbit/s   cpu_s  cpu_s/GiB
tcp             256      0.500       0.053   4796.8   40.24    0.10       0.40
utp             256      0.446      29.928      8.6    0.07    2.54      10.17
```

`cpu_s/GiB` is the CPU cost, in seconds per GiB moved: µTP costs 25 times as
much as TCP here.
A `transfer_s` near 30 s on a small transfer is a stall, not a slow
transport: the connection waited for libutp's 30 s keepalive. Rerun with
`--repeat 6` to see how often that happens.

Then add a real path. At 25 ms RTT the rate depends on window and buffer
sizes, which loopback never fills:

```console
$ python3 tools/profiling/transport/transport-link.py $B --rtt-ms 25 -- --transport both --size-mib 32
transport  size_mib  connect_s  transfer_s    MiB/s  Gbit/s   cpu_s  cpu_s/GiB
tcp              32      0.499       0.352     90.8    0.76    0.02       0.63
utp              32      0.158       5.807      5.5    0.05    0.21       6.71
```

Last, check that µTP still yields to the link while the rate changes:

```console
$ python3 tools/profiling/transport/transport-link.py $B --choke short --transport utp
utp: choke short, RTT 10 ms (measured 10.0 ms), 1000-packet queue
  phase  link_mbit  link_MiB/s  rate_MiB/s  min_sample  queue_delay_ms  result
      0         60        7.15        5.78        5.50              11  ok
      1         15        1.79        1.60        1.60              56  ok
      2         60        7.15        5.87        5.70              12  ok
```

Each phase must use at least half the link (`rate_MiB/s` against
`link_MiB/s`), never drop to zero (`min_sample`), and keep `queue_delay_ms`
under 150 ms. The exit status is 0 only when every phase passes, so the
command also works as a check in a script.

## Tools

`transport-benchmark` (built with the tests) measures TCP and µTP between two
sessions in one process over loopback. It takes seconds, so it suits quick
iterations on CPU cost. Loopback has no delay and no bottleneck, though, so it
cannot show window or buffer limits, congestion control or loss recovery, which
decide µTP's speed on a real connection. `transport-link.py` runs the benchmark
in a container with its own network namespace and netem on that loopback, which
adds delay, a rate limit and loss:

```sh
B=build/tests/libtransmission/transport-benchmark
$B --transport both --size-mib 256
python3 tools/profiling/transport/transport-link.py $B --rtt-ms 25 -- --transport both --size-mib 64
python3 tools/profiling/transport/transport-link.py $B --rtt-ms 25 --rate-mbit 100 -- --transport utp --size-mib 64
```

µTP exists to move data in the background without slowing the user's other
traffic: LEDBAT backs off as soon as it sees queueing delay build up. A change
that makes µTP faster can break that, and throughput numbers will not show it.
`--choke short|long` checks for it: it steps the bottleneck rate during one
timed transfer and pings across the same queue. A phase passes when the
transport uses at least half the link without stalling and, for µTP, keeps
queueing delay near LEDBAT's 100 ms target. `short` takes 12 s per
transport, `long` 150 s.

```sh
python3 tools/profiling/transport/transport-link.py $B --choke short --transport both
```

The bottleneck queue holds 1000 packets, seconds of data at these rates, so
congestion shows as delay and nothing is dropped. netem counts each of TCP's GSO
super-packets as one packet, so for TCP that queue holds more bytes than for µTP.
`--queue-ms N` makes it a shallow buffer of N ms at each phase's rate, limited in
bytes, so a transport that overfills it loses packets.

A failed phase tells you that congestion control broke, not how. For that,
`--csv FILE` also writes the run second by second: link and transfer rate,
queueing delay, the netem queue's throughput, drops and length, the namespace's
UDP and TCP counters (buffer errors, retransmissions), TCP's congestion window,
and the benchmark's CPU use. `support/choke-chart.py` draws that as Mermaid charts in
Markdown, one point per second around each rate change and wider buckets
between them, with a table of how each change settled:

```sh
python3 tools/profiling/transport/transport-link.py $B --choke long --transport both --csv choke.csv
python3 tools/profiling/transport/support/choke-chart.py choke.csv -o choke.md
```

Wi-Fi and mobile links drop packets without any congestion. `--loss-percent N`
drops that share of packets in each direction, to measure how a transport
recovers from it. `--mtu` sets the link's MTU, 1500 bytes by default.

When a transport is CPU-bound, `--perf` shows where the time goes: it adds a
CPU profile of the run, by binary and by function. Without read access to
`/proc/kallsyms`, kernel time shows as `[unknown]`.

```sh
python3 tools/profiling/transport/transport-link.py $B --rtt-ms 0 --perf -- --transport utp --size-mib 512
```

The container mounts the host's root read-only and chroots into it, so the
benchmark runs with the host's libraries, as the invoking user. It needs
Docker and the host's `ip`, `tc`, `ping` and `setpriv`, and `perf` for
`--perf`.

When you change the scripts, run their unit tests; they need neither Docker nor
a build:

```sh
python3 -m unittest discover -s tools/profiling/transport -p 'test_transport_scripts.py' -v
```
