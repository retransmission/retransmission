#!/usr/bin/env python3
"""Compare application disk workers using timing runs and separate batch traces.

Overview
--------
Runs plaintext and RC4 transfers with 1, 2, and 4 disk workers, a fixed 8 MiB
write budget, 256 KiB pieces, and no CPU pinning. Defaults to three repeats of
4 GiB per configuration with worker ordering rotated between repeats, followed
by one instrumented run per configuration (24 transfers in total). Every transfer
uses benchmark.py's verification and SHA-256 checks.

Usage (from the repository root)
-------------------------------
    python3 tools/profiling/storage-study.py --timing-build build --diagnostic-build .tmp/build/storage-diagnostics --output .tmp/results/storage-study/data/my-run --work-dir .tmp/work --peer-address 192.168.2.21
    python3 tools/profiling/storage-study.py --help

Build the daemon/create targets first, using the same source for both builds. --timing-build
must have ENABLE_BLOCK_PROFILING=OFF; --diagnostic-build must have it ON. Both
builds must have their CMakeCache.txt available. Do not compile or run competing
benchmarks during measurements. Requires Python 3.11+, local socket access, a
local non-loopback IPv4, and free peer ports 52613/52614 and RPC ports 19391/19392.
--size-mib changes the payload size; --repeats must be at least three.
--workers selects counts (default: 1 2 4); --timing-only skips diagnostic runs
and removes the requirement for --diagnostic-build. For example:
    python3 tools/profiling/storage-study.py --timing-build build --timing-only --workers 0 1 2 --output .tmp/results/storage-study/data/workers-012 --work-dir .tmp/work --peer-address 192.168.2.21
Worker count 0 uses synchronous session-thread I/O and has no worker batch trace;
the worker write budget is not enforced in that backend. No fio workload runs.

Outputs and resuming
--------------------
Writes study-settings.json plus timing/<mode>/workers-N/run-N and equivalent
diagnostic directories. Per-run logs/metrics come from benchmark.py; diagnostic
runs also write leech.csv.batches.tsv and batch-summary.json with size, block,
queue-delay, and write-duration distributions. Live device load is retained in
metrics.json and diskload-*.jsonl and summarized when available. summary.md compares uninstrumented
rates separately from diagnostic batch statistics. Reuse the identical command
to resume completed runs. Additional worker counts may be appended while all
other arguments remain unchanged; a history record identifies the extension.
Other study-setting changes require a new output path.
Queue percentiles describe batch means/maxima, not individual block percentiles.
Traces add memory/locking overhead and are not production throughput benchmarks.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys


def distribution(values):
    ordered = sorted(values)
    return {"count": len(ordered), "mean": statistics.mean(ordered),
            "median": statistics.median(ordered),
            "p95": ordered[math.ceil(len(ordered) * .95) - 1], "max": ordered[-1]}


def batches(path):
    with path.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows:
        raise RuntimeError(f"No write batches: {path}")
    result = {key: distribution([int(row[key]) for row in rows]) for key in
              ("bytes", "blocks", "queue_mean_ns", "queue_max_ns", "write_ns")}
    result["total_bytes"] = sum(int(row["bytes"]) for row in rows)
    result["total_blocks"] = sum(int(row["blocks"]) for row in rows)
    result["single_block_batches"] = sum(int(row["blocks"]) == 1 for row in rows)
    result["block_weighted_queue_mean_ns"] = sum(
        int(row["queue_mean_ns"]) * int(row["blocks"]) for row in rows) / result["total_blocks"]
    result["errors"] = sum(int(row["error"]) != 0 for row in rows)
    result["workers"] = sorted({int(row["thread"]) for row in rows})
    return result


def previous_workers_for_resume(previous, settings):
    previous = dict(previous)
    previous.setdefault("workers", [1, 2, 4])
    previous.setdefault("timing_only", False)
    old_workers = previous["workers"]
    compatible = dict(previous, workers=settings["workers"]) == settings
    if not compatible or settings["workers"][:len(old_workers)] != old_workers:
        raise RuntimeError("Output belongs to a study with different settings")
    return old_workers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing-build", type=Path, required=True)
    parser.add_argument("--diagnostic-build", type=Path)
    parser.add_argument("--timing-only", action="store_true", help="Skip instrumented diagnostic runs")
    parser.add_argument("--workers", type=int, nargs="+", default=[1, 2, 4], help="Worker counts to compare (0: synchronous I/O)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--peer-address", required=True)
    parser.add_argument("--size-mib", type=int, default=4096)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 3 or args.size_mib <= 0:
        parser.error("Use at least three repeats and a positive size")
    if len(set(args.workers)) != len(args.workers) or any(not 0 <= n <= 64 for n in args.workers):
        parser.error("Worker counts must be distinct integers between 0 and 64")
    if not args.timing_only and args.diagnostic_build is None:
        parser.error("--diagnostic-build is required unless --timing-only is set")
    args.output.mkdir(parents=True, exist_ok=True)
    args.work_dir.mkdir(parents=True, exist_ok=True)
    settings = vars(args)
    settings = {key: str(value.resolve()) if isinstance(value, Path) else value for key, value in settings.items()}
    settings["cpu_affinity"] = "unpinned"
    settings_path = args.output / "study-settings.json"
    if settings_path.exists():
        previous = json.loads(settings_path.read_text())
        old_workers = previous_workers_for_resume(previous, settings)
        if old_workers != settings["workers"]:
            with (args.output / "study-settings-history.jsonl").open("a") as history:
                history.write(json.dumps({"updated_utc": datetime.now(timezone.utc).isoformat(),
                                          "previous_workers": old_workers, "workers": settings["workers"]}) + "\n")
    settings_path.write_text(json.dumps(settings, indent=2) + "\n")
    builds = [(args.timing_build, "OFF")]
    if not args.timing_only:
        builds.append((args.diagnostic_build, "ON"))
    for build, expected in builds:
        cache = (build / "CMakeCache.txt").read_text()
        actual = next((line.split("=", 1)[1] for line in cache.splitlines()
                       if line.startswith("ENABLE_BLOCK_PROFILING:")), None)
        # Older baseline revisions predate the profiling option. An unused
        # CMake -D value has type UNINITIALIZED, but the build is still OFF.
        if actual != expected and not (expected == "OFF" and actual is None):
            raise RuntimeError(f"{build}: expected ENABLE_BLOCK_PROFILING={expected}")
        if expected == "OFF" and "-DTR_ENABLE_BLOCK_PROFILING" in (build / "build.ninja").read_text():
            raise RuntimeError(f"{build}: compile flags enable block profiling")
    # Rotate worker ordering to reduce systematic warm-cache/order bias.
    phases = [("timing", args.repeats, args.timing_build)]
    if not args.timing_only:
        phases.append(("diagnostic", 1, args.diagnostic_build))
    for phase, count, build in phases:
        for repeat in range(count):
            workers_order = args.workers
            rotation = repeat % len(workers_order)
            workers_order = workers_order[rotation:] + workers_order[:rotation]
            for encryption in ("allowed", "required"):
                for workers in workers_order:
                    if phase == "diagnostic" and workers == 0:
                        continue # synchronous I/O has no worker batch trace
                    dest = args.output / phase / encryption / f"workers-{workers}" / f"run-{repeat + 1}"
                    if not (dest / "metrics.json").exists():
                        dest.mkdir(parents=True, exist_ok=True)
                        env = dict(os.environ, TR_PROFILE_BUILD_ENABLED="ON" if phase == "diagnostic" else "OFF",
                                   TR_PROFILE_WRITE_BATCHES="1" if phase == "diagnostic" else "0",
                                   TR_PROFILE_DISK_BASELINE="0")
                        command = [sys.executable, str(Path(__file__).with_name("benchmark.py")),
                                   "--daemon", str(build.resolve() / "daemon/transmission-daemon"),
                                   "--create", str(build.resolve() / "utils/transmission-create"),
                                   "--output", str(dest.resolve()), "--output-exact", "--work-dir", str(args.work_dir.resolve()),
                                   "--size-mib", str(args.size_mib), "--timeout", "600",
                                   "--peer-address", args.peer_address, "--peer-port", "52613", "--rpc-port", "19391",
                                   "--disk-io-workers", str(workers), "--disk-write-budget-mib", "8",
                                   "--piece-size-kib", "256", "--seed-verify-mode", "full", "--encryption", encryption,
                                   "--expect-block-profiling", env["TR_PROFILE_BUILD_ENABLED"]]
                        print(f"{phase}: {encryption}, workers={workers}, repeat={repeat + 1}", flush=True)
                        with (dest / "runner.log").open("w") as log:
                            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                        if result.returncode:
                            raise RuntimeError(f"Benchmark failed (exit {result.returncode}); see {dest / 'runner.log'}")
                    metrics = json.loads((dest / "metrics.json").read_text())
                    if metrics["observed_peer_encryption"] != [encryption == "required"]:
                        raise RuntimeError(f"Unexpected encryption: {dest}")
                    if phase == "diagnostic":
                        summary = batches(dest / "leech.csv.batches.tsv")
                        if summary["errors"] or summary["total_bytes"] != args.size_mib * 1024 * 1024:
                            raise RuntimeError(f"Invalid batch trace: {dest}")
                        (dest / "batch-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    disk_lines = []
    lines = ["| Mode | Workers | Median steady MiB/s | Min–max MiB/s | Median transfer s | Median leech CPU s |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for encryption in ("allowed", "required"):
        for workers in args.workers:
            root = args.output / "timing" / encryption / f"workers-{workers}"
            rows = [json.loads((root / f"run-{i+1}" / "metrics.json").read_text()) for i in range(args.repeats)]
            rates = [row["steady_state"]["mib_per_second"] for row in rows]
            seconds = [row["transfer_seconds"] for row in rows]
            cpu = [row["transfer_cpu"]["leech"]["user_seconds"] + row["transfer_cpu"]["leech"]["system_seconds"] for row in rows]
            lines.append(f"| {encryption} | {workers} | {statistics.median(rates):.1f} | {min(rates):.1f}–{max(rates):.1f} | {statistics.median(seconds):.2f} | {statistics.median(cpu):.2f} |")
            if all(row.get("disk_load", {}).get("transfer", {}).get("available") for row in rows):
                loads = [row["disk_load"]["transfer"] for row in rows]
                disks = [load["devices"][load["source_device"]] for load in loads]
                def med(key):
                    values = [disk[key] for disk in disks if disk[key] is not None]
                    return statistics.median(values) if values else float("nan")
                flush = statistics.median(row["download_fsync_seconds"] for row in rows)
                disk_lines.append(f"| {encryption} | {workers} | {med('read_mib_per_second'):.1f} | "
                                  f"{med('write_mib_per_second'):.1f} | {med('write_iops'):.1f} | "
                                  f"{med('write_await_ms'):.3f} | {med('average_queue_depth'):.2f} | "
                                  f"{med('busy_percent'):.1f} | {flush:.3f} |")
    if disk_lines:
        lines += ["", "Target-device disk load during transfer (device-wide medians; final flush is separate):", "",
                  "| Mode | Workers | Read MiB/s | Write MiB/s | Write IOPS | Write await ms | Avg queue | Busy % | Final flush s |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |", *disk_lines]
    if not args.timing_only:
        lines += ["", "Diagnostic runs (instrumented; separate from timing runs):", "",
              "| Mode | Workers | Batches | Mean KiB | Median KiB | p95 KiB | p95 batch mean queue ms | p95 write ms |",
              "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for encryption in (() if args.timing_only else ("allowed", "required")):
        for workers in args.workers:
            if workers == 0:
                continue
            path = args.output / "diagnostic" / encryption / f"workers-{workers}" / "run-1" / "batch-summary.json"
            row = json.loads(path.read_text())
            size = row["bytes"]
            lines.append(f"| {encryption} | {workers} | {size['count']} | {size['mean']/1024:.1f} | "
                         f"{size['median']/1024:.1f} | {size['p95']/1024:.1f} | "
                         f"{row['queue_mean_ns']['p95']/1e6:.3f} | {row['write_ns']['p95']/1e6:.3f} |")
    report = "\n".join(lines) + "\n"
    (args.output / "summary.md").write_text(report)
    print(report)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(f"Error: {error}")
