#!/usr/bin/env python3
"""Build transport-benchmark for several refs and compare them on one suite.

Each ref is checked out, detached, in a scratch worktree, the benchmark tools are merged in
(so refs without them can be measured), and transport-benchmark is built and run. A ref whose
merge changes files outside the tools is skipped: it would bring in the tools ref's base, and
the build would no longer be the ref's code.

  - loopback: µTP, --repeat runs of 16 MiB; TCP, 2 runs of 64 MiB
  - emulated link at 25 ms RTT: TCP and µTP, 32 MiB each
  - transport-link.py --choke short, µTP; its per-second log in choke.csv, drawn in choke.md
A run that fails counts in the "failed" column; a µTP loopback run that fails counts as a stall.

Before measuring each ref, it waits until at most --max-load CPUs (default 4) are busy over 20 s.

    transport-compare.py --worktree DIR [--tools-ref REF] [--libutp-from DIR] [--out DIR] REF...

The worktree must be a git worktree of this repository that nothing else uses: the
tool checks out, merges and builds there. Builds share one build directory inside it, so
later refs build incrementally. Each ref's raw output goes to --out; the summary table
goes to stdout. A µTP loopback run that takes over 5 s for 16 MiB counts as a stall.
Builds run at nice 19; the benchmark runs at the caller's priority.
"""

from __future__ import annotations

import argparse
import os
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
STALL_SECONDS = 5.0
# What --tools-ref may change in a ref when it is merged in.
TOOL_PATHS = ("tests/libtransmission/CMakeLists.txt", "tests/libtransmission/transport-benchmark.cc",
              "tools/profiling/")


@dataclass
class Run:
    transport: str
    mib: float
    connect_s: float
    transfer_s: float
    mib_per_s: float
    cpu_s_per_gib: float


@dataclass
class RefResult:
    ref: str
    commit: str = ""
    loopback_utp: list[Run] = field(default_factory=list)
    utp_missing: int = 0  # µTP loopback runs without a result
    loopback_tcp: list[Run] = field(default_factory=list)
    rtt25: list[Run] = field(default_factory=list)
    choke_ok: bool | None = None
    failed: list[str] = field(default_factory=list)  # suite steps that exited with an error
    error: str = ""


def parse_runs(stdout: str) -> list[Run]:
    """Reads transport-benchmark's result table, ignoring every other line."""
    runs = []
    for line in stdout.splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[0] in ("tcp", "utp"):
            try:
                mib, connect, transfer, rate = (float(f) for f in fields[1:5])
                runs.append(Run(fields[0], mib, connect, transfer, rate, float(fields[7])))
            except ValueError:
                continue
    return runs


def git(worktree: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(worktree), *args], check=True, capture_output=True,
                          text=True).stdout.strip()


def build(worktree: Path, build_dir: Path, log: Path) -> None:
    with log.open("w") as out:
        if not (build_dir / "CMakeCache.txt").exists():
            subprocess.run(
                ["nice", "-n", "19", "cmake", "-S", str(worktree), "-B", str(build_dir), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_TESTS=ON", "-DENABLE_GTK=OFF", "-DENABLE_QT=OFF",
                 "-DENABLE_MAC=OFF", "-DENABLE_CLI=OFF", "-DENABLE_DAEMON=OFF", "-DENABLE_UTILS=OFF",
                 "-DREBUILD_WEB=OFF", "-DRUN_CLANG_TIDY=OFF"],
                check=True, stdout=out, stderr=subprocess.STDOUT)
        subprocess.run(
            ["nice", "-n", "19", "cmake", "--build", str(build_dir), "--target", "transport-benchmark"],
            check=True, stdout=out, stderr=subprocess.STDOUT)


def run(cmd: list[str], log: Path) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    log.write_text(proc.stdout + proc.stderr)
    return proc


def cpu_jiffies() -> tuple[int, int] | None:
    """Busy and total CPU time from /proc/stat, or None where it does not exist."""
    try:
        with open("/proc/stat", encoding="ascii") as stat:
            fields = [int(f) for f in stat.readline().split()[1:9]]
    except (OSError, ValueError):
        return None
    total = sum(fields)
    return total - fields[3] - fields[4], total  # idle and iowait are not busy


def busy_cpus(interval_s: float) -> float:
    """CPUs busy on average over the next interval_s: unlike the load average, no older load lingers."""
    before = cpu_jiffies()
    time.sleep(interval_s)
    after = cpu_jiffies()
    if before is None or after is None or after[1] == before[1]:
        return os.getloadavg()[0]
    return (after[0] - before[0]) / (after[1] - before[1]) * (os.cpu_count() or 1)


def wait_for_quiet(max_busy: float, measure=busy_cpus, first_s: float = 5.0, poll_s: float = 20.0) -> float:
    """Wait until at most max_busy CPUs are busy, printing a dot per recheck; return the last reading."""
    busy = measure(first_s)
    if busy <= max_busy:
        return busy
    print(f"{busy:.1f} CPUs busy > {max_busy:g}, waiting", end="", file=sys.stderr, flush=True)
    while busy > max_busy:
        busy = measure(poll_s)
        print(".", end="", file=sys.stderr, flush=True)
    print(f" {busy:.1f}", file=sys.stderr, flush=True)
    return busy


def outside_tools(paths: list[str]) -> list[str]:
    return [p for p in paths if not any(p == t or (t.endswith("/") and p.startswith(t)) for t in TOOL_PATHS)]


def update_submodules(worktree: Path, libutp_from: Path | None) -> None:
    if libutp_from:
        libutp = worktree / "third-party" / "libutp"
        # Clones the submodule; checking out an unpublished pin fails until the fetch below.
        subprocess.run(["git", "-C", str(worktree), "submodule", "update", "-q", "--init", "--",
                        "third-party/libutp"], capture_output=True, check=False)
        # Without the clone, git -C would find the superproject and fetch into it.
        if Path(git(libutp, "rev-parse", "--show-toplevel")).resolve() != libutp.resolve():
            raise subprocess.CalledProcessError(1, ["git", "submodule", "update", "third-party/libutp"])
        git(libutp, "fetch", "-q", str(libutp_from), "+refs/heads/*:refs/remotes/transport-compare/*")
    git(worktree, "submodule", "update", "-q", "--init", "--recursive")


def measure(ref: str, args: argparse.Namespace) -> RefResult:
    result = RefResult(ref)
    out_dir = args.out / ref.replace("/", "_")
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        git(args.worktree, "checkout", "-q", "--detach", ref)
        git(args.worktree, "merge", "-q", "--no-edit", args.tools_ref)
        result.commit = git(args.worktree, "rev-parse", "--short", ref)
        if others := outside_tools(git(args.worktree, "diff", "--name-only", ref, "HEAD").splitlines()):
            more = f" and {len(others) - 2} more" if len(others) > 2 else ""
            result.error = f"merging the tools changes {', '.join(others[:2])}{more}"
            return result
        update_submodules(args.worktree, args.libutp_from)
        build(args.worktree, args.build_dir, out_dir / "build.log")
    except subprocess.CalledProcessError as error:
        subprocess.run(["git", "-C", str(args.worktree), "merge", "--abort"],
                       capture_output=True, check=False)
        result.error = f"{' '.join(error.cmd[:4])} failed"
        return result

    # This ref's own build keeps CPUs busy, so the wait comes after it.
    if args.max_load > 0:
        load = wait_for_quiet(args.max_load)
        (out_dir / "load.txt").write_text(f"{load:.2f}\n")

    benchmark = str(args.build_dir / "tests" / "libtransmission" / "transport-benchmark")
    link_script = args.worktree / "tools" / "profiling" / "transport" / "transport-link.py"
    link = [sys.executable, str(link_script), benchmark]
    utp_cmd = [benchmark, "--transport", "utp", "--size-mib", "16", "--repeat", str(args.repeat),
               "--timeout", "60"]
    tcp_cmd = [benchmark, "--transport", "tcp", "--size-mib", "64", "--repeat", "2"]
    rtt25_cmd = [*link, "--rtt-ms", "25", "--", "--transport", "both", "--size-mib", "32", "--timeout", "120"]
    runs = {}
    for name, cmd in (("loopback-utp", utp_cmd), ("loopback-tcp", tcp_cmd), ("rtt25", rtt25_cmd)):
        proc = run(cmd, out_dir / f"{name}.txt")
        runs[name] = parse_runs(proc.stdout)
        if proc.returncode != 0:
            result.failed.append(name)
    result.loopback_utp, result.loopback_tcp, result.rtt25 = (
        runs["loopback-utp"], runs["loopback-tcp"], runs["rtt25"])
    # The benchmark stops at its first failed run, so the runs after it are missing too.
    result.utp_missing = max(0, args.repeat - len(result.loopback_utp))

    choke_csv = out_dir / "choke.csv"
    choke_csv.unlink(missing_ok=True)  # a previous invocation's
    result.choke_ok = run([*link, "--choke", "short", "--csv", str(choke_csv)],
                          out_dir / "choke.txt").returncode == 0
    if choke_csv.exists():
        run([sys.executable, str(HERE / "support" / "choke-chart.py"), str(choke_csv),
             "-o", str(out_dir / "choke.md")], out_dir / "choke-chart.log")
    return result


def median(values: list[float], digits: int = 1) -> str:
    return f"{statistics.median(values):.{digits}f}" if values else "-"


def choke_verdict(choke_ok: bool | None) -> str:
    return "-" if choke_ok is None else ("ok" if choke_ok else "FAIL")


def format_table(results: list[RefResult]) -> str:
    header = ("ref", "commit", "utp MiB/s", "utp cpu_s/GiB", "utp stalls", "tcp MiB/s",
              "25ms utp", "25ms tcp", "choke", "failed")
    rows = [header]
    for r in results:
        if r.error:
            rows.append((r.ref, r.commit or "-", r.error, "", "", "", "", "", "", ""))
            continue
        utp = [x for x in r.loopback_utp if x.transfer_s <= STALL_SECONDS]
        stalls = sum(1 for x in r.loopback_utp if x.transfer_s > STALL_SECONDS) + r.utp_missing
        rtt_utp = [x.mib_per_s for x in r.rtt25 if x.transport == "utp"]
        rtt_tcp = [x.mib_per_s for x in r.rtt25 if x.transport == "tcp"]
        rows.append((
            r.ref, r.commit, median([x.mib_per_s for x in utp]), median([x.cpu_s_per_gib for x in utp], 2),
            f"{stalls}/{len(r.loopback_utp) + r.utp_missing}", median([x.mib_per_s for x in r.loopback_tcp]),
            median(rtt_utp), median(rtt_tcp), choke_verdict(r.choke_ok), ",".join(r.failed) or "-"))
    widths = [max(len(str(row[i])) for row in rows) for i in range(len(header))]
    return "\n".join("  ".join(str(cell).ljust(w) for cell, w in zip(row, widths)).rstrip() for row in rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("refs", nargs="+")
    parser.add_argument("--worktree", type=Path, required=True, help="scratch git worktree to build in")
    parser.add_argument("--tools-ref", help="merged into each ref so it has the benchmark "
                        "(default: the commit checked out where this script runs)")
    parser.add_argument("--build-dir", type=Path, help="default: WORKTREE/.transport-compare/build")
    parser.add_argument("--libutp-from", type=Path,
                        help="libutp repository to fetch unpublished commits from that refs pin")
    parser.add_argument("--out", type=Path, default=Path("transport-compare-results"))
    parser.add_argument("--repeat", type=int, default=6, help="µTP loopback runs per ref")
    parser.add_argument("--max-load", type=float, default=4.0,
                        help="before measuring each ref, wait until at most this many CPUs are busy, "
                             "averaged over 20 s and rechecked every 20 s; 0 disables the wait")
    args = parser.parse_args()
    args.worktree = args.worktree.resolve()
    if not args.worktree.is_dir():
        parser.error(f"--worktree {args.worktree} does not exist; create it with git worktree add --detach")
    args.tools_ref = args.tools_ref or git(HERE, "rev-parse", "HEAD")
    args.build_dir = (args.build_dir or args.worktree / ".transport-compare" / "build").resolve()
    args.out = args.out.resolve()
    if os.path.samefile(args.worktree, HERE.parent.parent.parent):
        parser.error("--worktree must not be the checkout this script runs from")

    results = []
    for ref in args.refs:
        results.append(measure(ref, args))
        print(format_table(results[-1:]).splitlines()[-1], file=sys.stderr, flush=True)
    print(format_table(results))
    return 0 if all(not r.error and not r.failed and r.choke_ok for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
