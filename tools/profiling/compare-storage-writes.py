#!/usr/bin/env python3
"""Compare copy/coalesce and vectored-write builds with live target disk load.

Usage (from the repository root)
-------------------------------
    python3 tools/profiling/compare-storage-writes.py --before-build .tmp/binaries/storage-optimization/before --after-build .tmp/binaries/storage-optimization/vectored --output .tmp/results/storage-study/data/my-comparison --work-dir .tmp/work --peer-address 192.168.2.21

Both directories must contain CMakeCache.txt, daemon/transmission-daemon, and
utils/transmission-create. Build both before starting; each must have profiling
OFF and use matching optimization flags. Runs three 4 GiB repeats per variant
in plaintext and RC4, with two workers, an 8 MiB budget, 256 KiB pieces, and no
CPU pinning. Variant order alternates between repeats. Requires local sockets,
free ports 52713/52714 and 19491/19492, Python 3.11+, and readable Linux sysfs
device counters. No fio workload is run.

Output and resuming
-------------------
Saves arguments and binary hashes in settings.json. Each mode/variant/run-N has
benchmark.py's metrics, verification results, logs, and diskload-transfer/flush
JSONL traces. summary.md compares median throughput, downloader CPU, and device
load during transfer; separate flush metrics remain in each metrics.json.
Disk figures are device-wide and include unrelated activity and delayed cached
writes; parent disk/partition counters overlap and must not be added together.
Reuse identical arguments and unchanged binaries to resume. Use a new output
directory after changing the workload or either executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("before-build", "after-build", "output", "work-dir"):
        parser.add_argument("--" + flag, type=Path, required=True)
    parser.add_argument("--peer-address", required=True)
    parser.add_argument("--size-mib", type=int, default=4096)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 3 or args.size_mib < 1:
        parser.error("Use at least three repeats and a positive size")
    args.output.mkdir(parents=True, exist_ok=True)
    args.work_dir.mkdir(parents=True, exist_ok=True)
    builds = {"copy": args.before_build.resolve(), "vectored": args.after_build.resolve()}
    settings = {key: str(value.resolve()) if isinstance(value, Path) else value for key, value in vars(args).items()}
    settings["binary_sha256"] = {}
    for variant, build in builds.items():
        cache = (build / "CMakeCache.txt").read_text()
        actual = next((line.split("=", 1)[1] for line in cache.splitlines()
                       if line.startswith("ENABLE_BLOCK_PROFILING:")), None)
        # Revisions that predate the option always build without profiling.
        if actual not in (None, "OFF"):
            raise RuntimeError(f"{build}: profiling must be OFF")
        settings["binary_sha256"][variant] = hashlib.sha256((build / "daemon/transmission-daemon").read_bytes()).hexdigest()
    path = args.output / "settings.json"
    if path.exists() and json.loads(path.read_text()) != settings:
        raise RuntimeError("Study settings or binaries changed; use a new output directory")
    path.write_text(json.dumps(settings, indent=2) + "\n")
    for repeat in range(args.repeats):
        for mode in ("allowed", "required"):
            variants = list(builds) if repeat % 2 == 0 else list(reversed(builds))
            for variant in variants:
                dest = args.output / mode / variant / f"run-{repeat + 1}"
                existing = dest / "metrics.json"
                if existing.exists() and not json.loads(existing.read_text()).get("source_fsynced_before_transfer"):
                    archive = args.output / "excluded-source-writeback" / mode / variant / dest.name
                    archive.parent.mkdir(parents=True, exist_ok=True)
                    dest.rename(archive)
                if not (dest / "metrics.json").exists():
                    dest.mkdir(parents=True, exist_ok=True)
                    build = builds[variant]
                    command = [sys.executable, str(Path(__file__).with_name("benchmark.py")),
                               "--daemon", str(build / "daemon/transmission-daemon"),
                               "--create", str(build / "utils/transmission-create"),
                               "--output", str(dest.resolve()), "--output-exact", "--work-dir", str(args.work_dir.resolve()),
                               "--size-mib", str(args.size_mib), "--timeout", "600", "--peer-address", args.peer_address,
                               "--peer-port", "52713", "--rpc-port", "19491", "--disk-io-workers", "2",
                               "--disk-write-budget-mib", "8", "--piece-size-kib", "256", "--seed-verify-mode", "full",
                               "--encryption", mode, "--expect-block-profiling", "OFF"]
                    print(f"{mode}: {variant}, repeat {repeat + 1}", flush=True)
                    with (dest / "runner.log").open("w") as log:
                        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                                env=dict(os.environ, TR_PROFILE_BUILD_ENABLED="OFF",
                                                         TR_PROFILE_WRITE_BATCHES="0", TR_PROFILE_DISK_BASELINE="0"))
                    if result.returncode:
                        raise RuntimeError(f"Benchmark failed; see {dest / 'runner.log'}")
                metrics = json.loads((dest / "metrics.json").read_text())
                if metrics["observed_peer_encryption"] != [mode == "required"]:
                    raise RuntimeError(f"Wrong encryption in {dest}")
                if metrics["steady_state"] is None:
                    raise RuntimeError(f"No steady-state window; use a larger --size-mib: {dest}")
                if not all(metrics["disk_load"][phase]["available"] for phase in ("transfer", "flush")):
                    raise RuntimeError(f"Missing disk load in {dest}")
    lines = ["| Mode | Variant | Median steady MiB/s | Min–max MiB/s | Leech CPU s | Device write MiB/s | Device busy % | Write await ms |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for mode in ("allowed", "required"):
        for variant in builds:
            rows = [json.loads((args.output / mode / variant / f"run-{i+1}" / "metrics.json").read_text())
                    for i in range(args.repeats)]
            rates = [r["steady_state"]["mib_per_second"] for r in rows]
            cpu = statistics.median(sum(r["transfer_cpu"]["leech"][key] for key in ("user_seconds", "system_seconds")) for r in rows)
            disks = [r["disk_load"]["transfer"]["devices"][r["disk_load"]["transfer"]["source_device"]] for r in rows]
            def med(key):
                values = [d[key] for d in disks if d[key] is not None]
                return statistics.median(values) if values else float("nan")
            lines.append(f"| {mode} | {variant} | {statistics.median(rates):.1f} | {min(rates):.1f}–{max(rates):.1f} | {cpu:.2f} | "
                         f"{med('write_mib_per_second'):.1f} | {med('busy_percent'):.1f} | {med('write_await_ms'):.3f} |")
    report = "\n".join(lines) + "\n"
    (args.output / "summary.md").write_text(report)
    print(report)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(f"Error: {error}")
