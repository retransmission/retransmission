"""Measure target-device disk load during an application benchmark.

Overview and usage
------------------
Imported by benchmark.py; no standalone command or extra workload is launched.
Run benchmark.py normally: disk_load in metrics.json and diskload-*.jsonl record
actual device activity during transfer and final file flush. Linux sysfs block
statistics and permission to stat the mount source device are required. Missing
or unresolvable devices produce an explicit unavailable/error record.

The listed filesystem source device is resolved through /sys/dev/block. For a
partition, the parent disk is reported separately. Do not sum partition and disk
figures: they overlap. Device-wide counters include other processes, cached
writes may reach storage later, and NVMe busy percentage is not capacity usage.
A multi-device filesystem may use devices beyond its listed mount source.

Outputs and units
-----------------
Samples contain read/write MiB/s, IOPS, mean completed-operation latency (ms),
weighted average queue depth, current requests in flight, and device busy time
percentage. Sectors in Linux block statistics are always 512 bytes. Transfer
samples are polled at most every 100 ms; flush has endpoint snapshots because
fsync blocks the polling thread. Summaries use counter deltas over each phase;
JSONL retains the interval measurements and raw snapshots for inspection.
"""

import json
import os
from pathlib import Path
import stat
import time


FIELDS = ("read_ios", "read_merges", "read_sectors", "read_ms", "write_ios",
          "write_merges", "write_sectors", "write_ms", "in_flight", "busy_ms", "weighted_ms")


def read_stats(path):
    values = [int(value) for value in path.read_text().split()]
    if len(values) < len(FIELDS):
        raise ValueError(f"Incomplete block statistics: {path}")
    return dict(zip(FIELDS, values))


def rates(before, after, seconds):
    if seconds <= 0:
        raise ValueError("Disk sample duration must be positive")
    delta = {key: after[key] - before[key] for key in FIELDS if key != "in_flight"}
    if any(value < 0 for value in delta.values()):
        raise ValueError("Block statistics reset during measurement")
    return {"seconds": seconds,
            "read_mib_per_second": delta["read_sectors"] * 512 / (1024 ** 2) / seconds,
            "write_mib_per_second": delta["write_sectors"] * 512 / (1024 ** 2) / seconds,
            "read_iops": delta["read_ios"] / seconds, "write_iops": delta["write_ios"] / seconds,
            "read_await_ms": delta["read_ms"] / delta["read_ios"] if delta["read_ios"] else None,
            "write_await_ms": delta["write_ms"] / delta["write_ios"] if delta["write_ios"] else None,
            "average_queue_depth": delta["weighted_ms"] / (1000 * seconds),
            "in_flight": after["in_flight"], "busy_percent": delta["busy_ms"] / (10 * seconds),
            "read_bytes": delta["read_sectors"] * 512, "write_bytes": delta["write_sectors"] * 512}


def device_paths(filesystem):
    source = filesystem.get("source", "").split("[", 1)[0]
    try:
        info = os.stat(source)
    except OSError:
        # Containers can expose sysfs counters without exposing /dev nodes.
        if not source.startswith("/dev/"):
            raise
        device = (Path("/sys/class/block") / Path(source).name).resolve(strict=True)
    else:
        if not stat.S_ISBLK(info.st_mode):
            raise ValueError(f"Mount source is not an accessible block device: {source}")
        device = Path(f"/sys/dev/block/{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}").resolve(strict=True)
    paths = {device.name: device / "stat"}
    if (device / "partition").exists():
        paths[device.parent.name] = device.parent / "stat"
    return paths


class DiskLoad:
    def __init__(self, filesystem, output, phase):
        self.filesystem = filesystem
        self.output = output / f"diskload-{phase}.jsonl"
        self.phase = phase
        self.error = None
        self.paths = {}
        self.samples = []
        self.stream = None

    def __enter__(self):
        try:
            self.paths = device_paths(self.filesystem)
            self.stream = self.output.open("w")
            self.sample(force=True)
        except (OSError, ValueError) as error:
            self.error = str(error)
        return self

    def sample(self, force=False):
        if self.error or not self.paths:
            return
        now = time.monotonic()
        if self.samples and not force and now - self.samples[-1][0] < .1:
            return
        try:
            counters = {name: read_stats(path) for name, path in self.paths.items()}
            row = {"phase": self.phase, "seconds": now - self.samples[0][0] if self.samples else 0,
                   "counters": counters}
            if self.samples:
                previous_time, previous = self.samples[-1]
                row["devices"] = {name: rates(previous[name], value, now - previous_time)
                                  for name, value in counters.items()}
            self.stream.write(json.dumps(row) + "\n")
            self.samples.append((now, counters))
        except (OSError, ValueError) as error:
            self.error = str(error)

    def summary(self):
        if self.error or len(self.samples) < 2:
            return {"available": False, "error": self.error or "Insufficient disk samples"}
        start, before = self.samples[0]
        end, after = self.samples[-1]
        return {"available": True, "scope": "device-wide", "trace": self.output.name,
                "source_device": next(iter(self.paths)),
                "devices": {name: rates(before[name], value, end - start) for name, value in after.items()}}

    def __exit__(self, *_):
        self.sample(force=True)
        if self.stream:
            self.stream.close()
