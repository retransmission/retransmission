#!/usr/bin/env python3
"""Run transport-benchmark behind an emulated link, optionally with a choking bottleneck.

Loopback has no delay and no bottleneck, so it cannot show window limits or congestion
control. This script runs a host-built transport-benchmark in a Docker container that has
its own network namespace. netem on that namespace's loopback adds delay, and optionally a
rate limit and loss; the loopback MTU is an Ethernet-sized 1500 bytes by default. The
container mounts the host's root read-only and chroots into it, so the benchmark runs with
the host's libraries and the invoking user's uid; only the link setup (and perf, with
--perf) runs as root, and only inside the container.

    transport-link.py BENCHMARK [--rtt-ms N] [--rate-mbit N [--queue-ms N]] [--loss-percent N] [--perf]
                      [-- BENCHMARK-ARGS...]
    transport-link.py BENCHMARK --choke short|long [--transport utp|tcp|both] [--rtt-ms N] [--queue-ms N]
                      [--csv FILE]

--perf records a CPU profile of the run and prints the most expensive binaries and
functions. The host's perf_event_paranoid usually keeps an unprivileged user from
profiling; the container gets CAP_PERFMON and the host's PID namespace for this run.

--choke steps the bottleneck rate through phases during one timed transfer, and pings
across the same queue. Each phase passes when, after settling:
  - the rate is at least half the bottleneck (the transport keeps using the link)
  - the rate stays within the bottleneck (a check on the harness)
  - no sample is zero (no stall)
  - µTP only: queueing delay stays near LEDBAT's 100 ms target. A transport without
    working congestion control fills the netem queue instead.
The exit status is 0 when every phase passes.

The bottleneck queue holds 1000 packets: seconds of data at these rates, like a bufferbloated
home router, so congestion shows as delay and nothing is dropped. --queue-ms N sizes it to N ms
at each phase's rate instead, like a shallow router buffer, so a transport that overfills it
loses packets. --csv writes the run second by second: link rate,
transfer rate, queueing delay, and the netem queue's drops and length (support/choke-chart.py draws it).
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import re
import shlex
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# (bottleneck Mbit/s, seconds). Rates stay below what a µTP window allows at the default
# RTT, so a phase measures congestion control, not the window.
CHOKE_SCHEDULES = {
    "short": [(60, 4), (15, 4), (60, 4)],
    "long": [(60, 30), (15, 30), (4, 30), (60, 30), (15, 30)],
}
SETTLE_SECONDS = {"short": 1.5, "long": 5.0}
SAMPLE_MS = {"short": 250, "long": 1000}

MIN_UTILIZATION = 0.5
MAX_UTILIZATION = 1.1
# LEDBAT's target is 100 ms of queueing delay; allow for its overshoot while it adapts.
MAX_UTP_QUEUE_DELAY_MS = 150.0
# netem's queue, in packets, unless --queue-ms sizes it. At the slowest long phase this holds
# about 3 s of MTU-sized packets: a deep buffer, in which congestion shows as delay, not loss.
# TCP's GSO super-packets count as one packet each, so for TCP the queue holds more bytes.
NETEM_LIMIT = 1000

DEFAULT_IMAGE = "debian:trixie-slim"


@dataclass
class Phase:
    rate_mbit: float
    seconds: float
    start: float = 0.0  # seconds after the benchmark started


def netem_args(rtt_ms: float, rate_mbit: float | None, loss_percent: float = 0.0) -> str:
    # Every packet crosses loopback's egress once each way, so half the RTT per crossing.
    args = f"delay {int(rtt_ms * 500)}us limit {NETEM_LIMIT}"
    if loss_percent:
        args += f" loss {loss_percent}%"
    if rate_mbit:
        args += f" rate {rate_mbit}mbit"
    return args


def tbf_args(rate_mbit: float, queue_ms: float, mtu: int) -> str:
    """A bottleneck whose queue holds queue_ms of data at its rate.

    netem limits its queue in packets, and loopback hands it whole GSO super-packets of up to
    64 KiB, so only a byte limit holds. tbf limits bytes, and splits GSO packets larger than its
    burst into MTU-sized ones, as a real link does.
    """
    limit = max(2 * mtu, math.ceil(rate_mbit * 1e6 / 8 * queue_ms / 1000))
    return f"rate {rate_mbit}mbit burst {2 * mtu} limit {limit}"


def link_setup(rtt_ms: float, rate_mbit: float | None, loss_percent: float, queue_ms: float | None,
               mtu: int) -> list[str]:
    if not queue_ms:
        return [f"tc qdisc add dev lo root netem {netem_args(rtt_ms, rate_mbit, loss_percent)}"]
    # netem delays (and drops), then tbf rate-limits into a byte-limited queue.
    return [f"tc qdisc add dev lo root handle 1: netem {netem_args(rtt_ms, None, loss_percent)}",
            f"tc qdisc add dev lo parent 1:1 handle 10: tbf {tbf_args(rate_mbit, queue_ms, mtu)}"]


def link_change(rtt_ms: float, rate_mbit: float, queue_ms: float | None, mtu: int) -> str:
    if not queue_ms:
        return f"tc qdisc change dev lo root netem {netem_args(rtt_ms, rate_mbit)}"
    return f"tc qdisc change dev lo parent 1:1 handle 10: tbf {tbf_args(rate_mbit, queue_ms, mtu)}"


def link_mib_s(rate_mbit: float) -> float:
    return rate_mbit * 1e6 / 8 / (1024 * 1024)


# Once a second, the counters of this namespace's netem queue, UDP and TCP, the TCP sockets, and
# the benchmark's CPU time. The container's /proc is the host's, so the benchmark's pid is a host pid.
STATS_LOGGER = """set +e
while :; do
    T=$(date +%s.%N)
    echo "qdisc $T $(tc -s qdisc show dev lo | tr '\\n' ' ')"
    echo "snmp $T $(awk '/^(Tcp|Udp):/ {
        if (seen[$1]) { for (i = 2; i <= NF; i++) printf "%s%s=%s ", $1, key[$1, i], $i }
        else { seen[$1] = 1; for (i = 2; i <= NF; i++) key[$1, i] = $i } }' /proc/net/snmp)"
    echo "ss $T $(ss -tinH | tr '\\n' ' ')"
    [ -s /tmp/link/bench.pid ] && echo "cpu $T $(cut -d' ' -f14,15 /proc/$(cat /tmp/link/bench.pid)/stat)"
    sleep 1
done"""


def record_pid(run: str) -> str:
    """Run a command line so that it writes its (host) pid to /tmp/link/bench.pid first."""
    return ("sh -c 'read pid _ < /proc/self/stat; echo $pid > /tmp/link/bench.pid; exec \"$@\"' sh "
            + run)


def container_script(benchmark_cmd: list[str], rtt_ms: float, phases: list[Phase] | None,
                     rate_mbit: float | None, mtu: int, loss_percent: float = 0.0, perf: bool = False,
                     queue_ms: float | None = None) -> str:
    uid, gid = os.getuid(), os.getgid()
    first_rate = phases[0].rate_mbit if phases else rate_mbit
    lines = [
        "set -e",
        f"ip link set lo up mtu {mtu}",
        *link_setup(rtt_ms, first_rate, loss_percent, queue_ms, mtu),
        "mkdir -m 1777 -p /tmp/link",
    ]
    run = (f"setpriv --reuid={uid} --regid={gid} --clear-groups env HOME=/tmp TMPDIR=/tmp "
           + shlex.join(benchmark_cmd))
    if not phases and perf:
        report = "perf report -i /tmp/link/perf.data --stdio -g none --percent-limit 1"
        lines += [
            # A failed run still gets its profile.
            "STATUS=0",
            f"perf record -q -g -o /tmp/link/perf.data -- {run} || STATUS=$?",
            "set +e",
            "echo '== CPU by binary'",
            f"{report} --no-children --sort dso 2>/dev/null | grep -v '^#' | grep -v '^$'",
            "echo '== CPU by function, self'",
            f"{report} --no-children --sort sym 2>/dev/null | grep -v '^#' | grep -v '^$' | head -25",
            "exit $STATUS",
        ]
        return "\n".join(lines)
    if not phases:
        lines.append(f"exec {run}")
        return "\n".join(lines)

    lines += [
        # base RTT before any load
        "ping -n -q -c 5 -i 0.2 127.0.0.1"
        " | sed -n 's/^rtt .* = [0-9.]*\\/\\([0-9.]*\\)\\/.*/base_rtt_ms \\1/p'",
        "ping -n -D -i 0.2 127.0.0.1 > /tmp/link/ping.txt 2>&1 &",
        "PING=$!",
        f"( {STATS_LOGGER} ) > /tmp/link/stats.txt &",
        "STATS=$!",
        "echo start $(date +%s.%N)",
        f"{record_pid(run)} > /tmp/link/bench.txt 2>&1 &",
        "BENCH=$!",
    ]
    for previous, phase in zip(phases, phases[1:]):
        lines += [
            f"sleep {previous.seconds}",
            link_change(rtt_ms, phase.rate_mbit, queue_ms, mtu),
        ]
    lines += [
        "set +e",
        "wait $BENCH",
        "STATUS=$?",
        "kill $PING $STATS",
        "cat /tmp/link/bench.txt",
        "cat /tmp/link/stats.txt",
        "sed -n 's/^\\[\\([0-9.]*\\)\\].*time=\\([0-9.]*\\) ms/ping \\1 \\2/p' /tmp/link/ping.txt",
        "exit $STATUS",
    ]
    return "\n".join(lines)


def docker_command(script: str, image: str, perf: bool = False) -> list[str]:
    cmd = [
        "docker", "run", "--rm", "--network", "none",
        "--cap-add", "NET_ADMIN", "--cap-add", "NET_RAW", "--cap-add", "SYS_CHROOT",
        "--cap-add", "SETUID", "--cap-add", "SETGID",
    ]
    if perf:
        # perf resolves samples through /proc, so it needs the PID namespace it reads, and
        # CAP_SYSLOG to read kernel symbol addresses where kptr_restrict hides them.
        cmd += ["--cap-add", "PERFMON", "--cap-add", "SYS_ADMIN", "--cap-add", "SYSLOG", "--pid", "host",
                "--security-opt", "seccomp=unconfined"]
    cmd += [
        "-v", "/:/host:ro", "--tmpfs", "/host/tmp:exec,mode=1777",
        "--entrypoint", "chroot", image, "/host", "sh", "-c", script,
    ]
    return cmd


def run_in_container(script: str, image: str, perf: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(docker_command(script, image, perf), capture_output=True, text=True, check=False)


@dataclass
class ChokeOutput:
    start: float | None  # epoch seconds when the benchmark was launched; every time below counts from it
    base_rtt_ms: float | None
    samples: list[tuple[float, float]]  # (seconds since start, MiB/s)
    pings: list[tuple[float, float]]  # (seconds since start, RTT ms)
    other: list[str]
    # per kind of stats line, (seconds since start, values); counters are totals since the namespace began
    stats: dict[str, list[tuple[float, dict[str, float]]]] = field(default_factory=dict)


def parse_stats(kind: str, text: str) -> dict[str, float]:
    values: dict[str, float] = {}
    if kind == "qdisc":
        # With --queue-ms, a netem and a tbf: drops and backlogs add up; the root counts what was sent.
        for name in ("dropped", "overlimits", "requeues"):
            values[name] = float(sum(int(n) for n in re.findall(rf"\b{name} (\d+)", text)))
        if m := re.search(r"Sent (\d+) bytes (\d+) pkt", text):
            values["sent_bytes"], values["sent_pkts"] = float(m[1]), float(m[2])
        backlogs = re.findall(r"backlog (\d+(?:\.\d+)?)([KMG]?)b (\d+)p", text)
        if backlogs:
            scale = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3}
            values["backlog_bytes"] = sum(float(b) * scale[unit] for b, unit, _ in backlogs)
            values["backlog_pkts"] = float(sum(int(n) for _, _, n in backlogs))
    elif kind == "snmp":
        for key, value in re.findall(r"(\w+:\w+)=(-?\d+)", text):
            values[key] = float(value)
    elif kind == "ss":
        # The sending socket has the largest congestion window.
        sockets = re.findall(r"\brtt:([\d.]+)/.*?\bcwnd:(\d+)", text)
        if sockets:
            rtt, cwnd = max(sockets, key=lambda rtt_cwnd: int(rtt_cwnd[1]))
            values["cwnd"], values["rtt_ms"] = float(cwnd), float(rtt)
    elif kind == "cpu":
        ticks = text.split()
        if len(ticks) == 2:
            values["cpu_ticks"] = float(ticks[0]) + float(ticks[1])
    return values


def parse_choke_output(stdout: str) -> ChokeOutput:
    out = ChokeOutput(None, None, [], [], [])
    # The benchmark times its samples from its own start, after its sessions are up.
    sample_offset = 0.0
    for line in stdout.splitlines():
        fields = line.split()
        if fields[:1] == ["start"]:
            out.start = float(fields[1])
        elif fields[:1] == ["base_rtt_ms"]:
            out.base_rtt_ms = float(fields[1])
        elif fields[:1] == ["started"] and out.start is not None:
            sample_offset = float(fields[2]) - out.start
        elif fields[:1] == ["sample"]:
            out.samples.append((float(fields[2]) + sample_offset, float(fields[3])))
        elif fields[:1] == ["ping"] and out.start is not None:
            out.pings.append((float(fields[1]) - out.start, float(fields[2])))
        elif fields[:1] in (["qdisc"], ["snmp"], ["ss"], ["cpu"]) and out.start is not None:
            kind, t = fields[0], float(fields[1]) - out.start
            text = line.split(None, 2)[2] if len(fields) > 2 else ""
            out.stats.setdefault(kind, []).append((t, parse_stats(kind, text)))
        elif fields:
            out.other.append(line)
    return out


def queue_delays(pings: list[tuple[float, float]], base_rtt_ms: float, lo: float, hi: float) -> list[float]:
    """Queueing delay of each ping in (lo, hi]. Loopback has one queue, and both the ping and its reply
    wait in it. A ping faster than the base RTT measured no queue."""
    return [max(0.0, (rtt - base_rtt_ms) / 2) for t, rtt in pings if lo < t <= hi]


@dataclass
class PhaseResult:
    phase: Phase
    rate_mib: float
    min_sample_mib: float
    queue_delay_ms: float | None
    failures: list[str]


def evaluate(transport: str, phases: list[Phase], settle: float, samples: list[tuple[float, float]],
             pings: list[tuple[float, float]], base_rtt_ms: float) -> list[PhaseResult]:
    results = []
    for phase in phases:
        lo, hi = phase.start + settle, phase.start + phase.seconds
        rates = [mib for t, mib in samples if lo < t <= hi]
        delays = queue_delays(pings, base_rtt_ms, lo, hi)
        link_mib = link_mib_s(phase.rate_mbit)
        failures = []
        rate = statistics.fmean(rates) if rates else 0.0
        delay = statistics.median(delays) if delays else None
        if not rates:
            failures.append("no samples")
        else:
            if rate < MIN_UTILIZATION * link_mib:
                failures.append(f"rate {rate:.2f} MiB/s < {MIN_UTILIZATION:.0%} of {link_mib:.2f}")
            if rate > MAX_UTILIZATION * link_mib:
                failures.append(f"rate {rate:.2f} MiB/s > link {link_mib:.2f}")
            if min(rates) <= 0.0:
                failures.append("stalled")
        if transport == "utp" and delay is not None and delay > MAX_UTP_QUEUE_DELAY_MS:
            failures.append(f"queue delay {delay:.0f} ms > {MAX_UTP_QUEUE_DELAY_MS:.0f}")
        results.append(PhaseResult(phase, rate, min(rates, default=0.0), delay, failures))
    return results


# Per second of the run. Counters become per-second differences; gauges keep that second's maximum.
CSV_FIELDS = [
    "transport", "second", "link_mib_s", "rate_mib_s",
    "queue_delay_ms", "queue_delay_max_ms", "pings",
    "netem_mib_s", "netem_pkts", "dropped", "overlimits", "requeues",
    "queued_pkts", "queued_kib", "queue_limit_kib",
    "udp_in", "udp_out", "udp_rcvbuf_errors", "udp_sndbuf_errors", "udp_in_errors",
    "tcp_out_segs", "tcp_retrans_segs", "tcp_cwnd", "tcp_rtt_ms",
    "cpu_pct",
]
COUNTERS = {  # CSV column: (stats kind, counter, scale)
    "netem_mib_s": ("qdisc", "sent_bytes", 1 / (1024 * 1024)),
    "netem_pkts": ("qdisc", "sent_pkts", 1),
    "dropped": ("qdisc", "dropped", 1),
    "overlimits": ("qdisc", "overlimits", 1),
    "requeues": ("qdisc", "requeues", 1),
    "udp_in": ("snmp", "Udp:InDatagrams", 1),
    "udp_out": ("snmp", "Udp:OutDatagrams", 1),
    "udp_rcvbuf_errors": ("snmp", "Udp:RcvbufErrors", 1),
    "udp_sndbuf_errors": ("snmp", "Udp:SndbufErrors", 1),
    "udp_in_errors": ("snmp", "Udp:InErrors", 1),
    "tcp_out_segs": ("snmp", "Tcp:OutSegs", 1),
    "tcp_retrans_segs": ("snmp", "Tcp:RetransSegs", 1),
    "cpu_pct": ("cpu", "cpu_ticks", 100 / os.sysconf("SC_CLK_TCK")),
}
GAUGES = {  # CSV column: (stats kind, value, scale)
    "queued_pkts": ("qdisc", "backlog_pkts", 1),
    "queued_kib": ("qdisc", "backlog_bytes", 1 / 1024),
    "tcp_cwnd": ("ss", "cwnd", 1),
    "tcp_rtt_ms": ("ss", "rtt_ms", 1),
}


def number(value: float) -> str:
    return f"{value:.2f}".rstrip("0").rstrip(".")


Series = list[tuple[float, dict[str, float]]]


def counter_at(series: Series, key: str, t: float) -> float | None:
    """The counter at time t, interpolated between the readings around it.

    Readings come about once a second, not on the second, so a second's growth is taken between
    interpolated values at its edges. Before the first reading, the counter is at that reading.
    """
    readings = [(rt, v[key]) for rt, v in series if key in v]
    if not readings or t > readings[-1][0]:
        return None
    if t <= readings[0][0]:
        return readings[0][1]
    for (t0, v0), (t1, v1) in zip(readings, readings[1:]):
        if t0 <= t <= t1:
            return v0 + (v1 - v0) * (t - t0) / (t1 - t0) if t1 > t0 else v1
    return None


def counter_delta(series: Series, key: str, lo: float, hi: float) -> float | None:
    """The counter's growth from lo to hi."""
    before, after = counter_at(series, key, lo), counter_at(series, key, hi)
    if before is None or after is None:
        return None
    return max(0.0, after - before)  # a counter that went back was reset


def timeline(transport: str, phases: list[Phase], out: ChokeOutput, base_rtt_ms: float,
             queue_ms: float | None = None) -> list[dict]:
    """One row per whole second of the run; an empty cell where that second has no measurement."""
    end = phases[-1].start + phases[-1].seconds
    rows = []
    for second in range(int(end)):
        lo, hi = float(second), float(second + 1)
        phase = next((p for p in phases if p.start <= lo < p.start + p.seconds), phases[-1])
        rates = [mib for t, mib in out.samples if lo < t <= hi]
        delays = queue_delays(out.pings, base_rtt_ms, lo, hi)
        row = {field_name: "" for field_name in CSV_FIELDS}
        row.update(transport=transport, second=second, link_mib_s=number(link_mib_s(phase.rate_mbit)),
                   pings=len(delays))  # replies received; ping sends 5 a second
        if queue_ms:
            row["queue_limit_kib"] = number(phase.rate_mbit * 1e6 / 8 * queue_ms / 1000 / 1024)
        if rates:
            row["rate_mib_s"] = number(statistics.fmean(rates))
        if delays:
            row["queue_delay_ms"] = number(statistics.median(delays))
            row["queue_delay_max_ms"] = number(max(delays))
        for column, (kind, key, scale) in COUNTERS.items():
            delta = counter_delta(out.stats.get(kind, []), key, lo, hi)
            if delta is not None:
                row[column] = number(delta * scale) if scale != 1 else str(round(delta))
        for column, (kind, key, scale) in GAUGES.items():
            values = [v[key] for t, v in out.stats.get(kind, []) if lo < t <= hi and key in v]
            if values:
                row[column] = number(max(values) * scale)
        rows.append(row)
    return rows


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def choke(args: argparse.Namespace, transport: str) -> tuple[bool, list[dict]]:
    phases = [Phase(rate, secs) for rate, secs in CHOKE_SCHEDULES[args.choke]]
    t = 0.0
    for phase in phases:
        phase.start = t
        t += phase.seconds
    benchmark_cmd = [str(args.benchmark), "--transport", transport, "--seconds", str(int(t)),
                     "--sample-ms", str(SAMPLE_MS[args.choke])]
    script = container_script(benchmark_cmd, args.rtt_ms, phases, None, args.mtu, queue_ms=args.queue_ms)
    proc = run_in_container(script, args.image)

    out = parse_choke_output(proc.stdout)
    base_rtt_ms = out.base_rtt_ms if out.base_rtt_ms is not None else args.rtt_ms
    if proc.returncode != 0 or out.start is None:
        sys.stderr.write(proc.stdout + proc.stderr)
        print(f"{transport}: benchmark failed (exit {proc.returncode})")
        return False, []

    results = evaluate(transport, phases, SETTLE_SECONDS[args.choke], out.samples, out.pings, base_rtt_ms)
    queue = f"{args.queue_ms:g} ms queue" if args.queue_ms else f"{NETEM_LIMIT}-packet queue"
    print(f"{transport}: choke {args.choke}, RTT {args.rtt_ms:g} ms (measured {base_rtt_ms:.1f} ms), {queue}")
    print("  phase  link_mbit  link_MiB/s  rate_MiB/s  min_sample  queue_delay_ms  result")
    for i, r in enumerate(results):
        link_mib = link_mib_s(r.phase.rate_mbit)
        delay = f"{r.queue_delay_ms:.0f}" if r.queue_delay_ms is not None else "-"
        verdict = "ok" if not r.failures else "FAIL: " + "; ".join(r.failures)
        print(f"  {i:5}  {r.phase.rate_mbit:9g}  {link_mib:10.2f}  {r.rate_mib:10.2f}  "
              f"{r.min_sample_mib:10.2f}  {delay:>14}  {verdict}")
    if args.verbose:
        print("\n".join(out.other))
    return all(not r.failures for r in results), timeline(transport, phases, out, base_rtt_ms, args.queue_ms)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("benchmark", type=Path, help="host-built transport-benchmark")
    parser.add_argument("--rtt-ms", type=float, default=10.0)
    parser.add_argument("--mtu", type=int, default=1500,
                        help="loopback MTU (loopback's own is 65536, far above any internet path)")
    parser.add_argument("--loss-percent", type=float, default=0.0,
                        help="drop this share of packets each way, for loss recovery (not with --choke)")
    parser.add_argument("--perf", action="store_true",
                        help="print a CPU profile of the run (not with --choke)")
    parser.add_argument("--rate-mbit", type=float, help="fixed bottleneck rate (not with --choke)")
    parser.add_argument("--queue-ms", type=float,
                        help="size the bottleneck queue to this many ms at the link rate, so a full queue "
                             "drops packets (default: 1000 packets); needs --rate-mbit or --choke")
    parser.add_argument("--choke", choices=sorted(CHOKE_SCHEDULES),
                        help="step the bottleneck rate during a transfer")
    parser.add_argument("--transport", choices=["utp", "tcp", "both"],
                        help="with --choke, default utp; otherwise passed to the benchmark")
    parser.add_argument("--csv", type=Path, help="with --choke: write the run second by second to this file")
    parser.add_argument("--image", default=DEFAULT_IMAGE, help="any image with sh and chroot")
    parser.add_argument("-v", "--verbose", action="store_true")
    argv = sys.argv[1:]
    extra = argv[argv.index("--") + 1:] if "--" in argv else []
    args = parser.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    args.benchmark = args.benchmark.resolve()
    if args.benchmark.is_relative_to("/tmp"):
        parser.error("the benchmark can't be under /tmp: the container mounts its own /tmp over the host's")

    if args.csv and not args.choke:
        parser.error("--csv needs --choke")
    if args.queue_ms and not (args.rate_mbit or args.choke):
        parser.error("--queue-ms needs --rate-mbit or --choke: without a rate there is no queue")
    if args.choke:
        if args.rate_mbit or args.loss_percent or args.perf or extra:
            parser.error("--choke sets the rate and the benchmark arguments itself")
        transport = args.transport or "utp"
        transports = ["tcp", "utp"] if transport == "both" else [transport]
        runs = [choke(args, t) for t in transports]
        if args.csv:
            write_csv(args.csv, [row for _, rows in runs for row in rows])
        return 0 if all(ok for ok, _ in runs) else 1

    if args.transport:
        if "--transport" in extra:
            parser.error("--transport given twice: before and after --")
        extra = ["--transport", args.transport, *extra]
    script = container_script([str(args.benchmark), *extra], args.rtt_ms, None, args.rate_mbit, args.mtu,
                              args.loss_percent, args.perf, args.queue_ms)
    proc = run_in_container(script, args.image, args.perf)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
