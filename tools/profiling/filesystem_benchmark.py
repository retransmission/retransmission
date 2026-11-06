"""Provide filesystem identification and fio disk baselines for benchmark.py.

Overview
--------
This importable helper has no command-line entry point. filesystem_info(path)
uses findmnt JSON to identify the mount/device/type/options, returning an error
record if detection fails. disk_target_eligible() excludes RAM and unknown
filesystems from the disk-backed throughput target. summarize_fio() converts
fio byte counters to decimal Gbit/s, MiB/s, and IOPS and rejects failed jobs.

Usage (from the repository root)
-------------------------------
Run the helper through the transfer harness, supplying your local IPv4 address:
    mkdir -p .tmp/work
    python3 tools/profiling/benchmark.py --daemon build/daemon/transmission-daemon --create build/utils/transmission-create --peer-address 192.168.2.21 --work-dir .tmp/work --disk-baseline --output .tmp/results/filesystem-baseline

run_fio(root, output, size_mib) requires the fio I/O benchmark executable and an
existing disk-backed work/output directory. It runs 1 MiB sequential writes,
1 MiB sequential reads, and four-job random 16 KiB writes using libaio, direct
I/O, and depth 16. Each case has a 1-second ramp and 5-second measurement; the
shared temporary file is capped at 1024 MiB and removed on exit.

Outputs and interpretation
--------------------------
Saves fio-<case>.json and fio-<case>.log; the harness also writes disk-baseline.json
and includes the summary in metrics.json. Raw JSON retains fio latency details;
the compact summary contains bandwidth/IOPS. All writes target a private
TemporaryDirectory, never a raw device. Direct-I/O baselines do not reproduce
the application's buffered-write batching, queue delays, or Btrfs lock contention.
"""

import json
from pathlib import Path
import subprocess
import tempfile


def filesystem_info(path):
    try:
        result = subprocess.run(
            ["findmnt", "--json", "--target", str(path), "--output", "TARGET,SOURCE,FSTYPE,OPTIONS"],
            check=True, capture_output=True, text=True, timeout=10)
        return json.loads(result.stdout)["filesystems"][0]
    except (OSError, subprocess.SubprocessError, ValueError, KeyError, IndexError) as error:
        return {"fstype": "unknown", "error": str(error)}


def disk_target_eligible(filesystem, storage):
    return storage != "tmpfs" and (filesystem.get("fstype") or "unknown") not in {"tmpfs", "ramfs", "unknown"}


def summarize_fio(result, direction):
    rate = 0.0
    iops = 0.0
    for job in result["jobs"]:
        if job.get("error", 0):
            raise RuntimeError(f"fio job {job.get('jobname')} failed: {job['error']}")
        counters = job[direction]
        runtime_ms = counters["runtime"]
        if runtime_ms > 0:
            rate += counters["io_bytes"] * 1000 / runtime_ms
        iops += counters["iops"]
    return {"gbit_per_second": rate * 8 / 1e9, "mib_per_second": rate / (1024 * 1024), "iops": iops}


def run_fio(root, output, size_mib):
    summary = {}
    cases = [("sequential_write", "write", "1m", 1), ("sequential_read", "read", "1m", 1),
             ("random_write_16k", "randwrite", "16k", 4)]
    with tempfile.TemporaryDirectory(prefix="fio-", dir=root) as directory:
        data = Path(directory) / "data.bin"
        for name, operation, block_size, jobs in cases:
            result_path = output / f"fio-{name}.json"
            command = ["fio", f"--name={name}", f"--filename={data}",
                       f"--size={min(size_mib, 1024) * 1024 * 1024}", f"--rw={operation}", f"--bs={block_size}",
                       "--ioengine=libaio", "--direct=1", "--iodepth=16", f"--numjobs={jobs}",
                       "--runtime=5", "--ramp_time=1", "--time_based=1", "--end_fsync=1",
                       "--refill_buffers=1", "--group_reporting=1", "--output-format=json", f"--output={result_path}"]
            with (output / f"fio-{name}.log").open("w") as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            summary[name] = summarize_fio(json.loads(result_path.read_text()), "read" if operation == "read" else "write")
    return summary
