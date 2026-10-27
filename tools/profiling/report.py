"""Turn block-profile CSV files into readable timing summaries.

Usage (from the repository root)
-------------------------------
    python3 tools/profiling/report.py path/to/seed.csv path/to/leech.csv
    python3 tools/profiling/report.py path/to/leech.csv > summary.txt

Accepts one or more CSVs written by libtransmission/profile.h; requires only
Python's standard library and does not need running daemons or perf permissions.
Also exposes summarize(path), which benchmark.py calls when creating report.txt.

Output and interpretation
-------------------------
Prints one table per input, combining threads by scope and sorting by self wall
time. Reports summed calls, total/self milliseconds, and the largest observed
call duration. Source CSVs remain unchanged and retain individual thread rows.
Overlapping operations across threads/requests can sum to more than elapsed run
time. These numbers are not sampled CPU percentages. Batch TSV traces use a
different schema and are summarized by storage-study.py instead.
"""

import argparse
import csv
from pathlib import Path


def summarize(path):
    totals = {}
    with Path(path).open(newline="") as stream:
        for row in csv.DictReader(stream):
            item = totals.setdefault(row["scope"], {"calls": 0, "total_ms": 0.0, "self_ms": 0.0, "max_ms": 0.0})
            item["calls"] += int(row["calls"])
            for field in ("total_ms", "self_ms"):
                item[field] += float(row[field])
            item["max_ms"] = max(item["max_ms"], float(row["max_ms"]))
    lines = [f"{Path(path).name}: wall time summed across threads",
             f"{'Scope':40} {'Calls':>10} {'Total ms':>12} {'Self ms':>12} {'Max ms':>12}"]
    for name, row in sorted(totals.items(), key=lambda item: item[1]["self_ms"], reverse=True):
        lines.append(f"{name:40} {row['calls']:10d} {row['total_ms']:12.3f} {row['self_ms']:12.3f} {row['max_ms']:12.3f}")
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiles", nargs="+", type=Path)
    args = parser.parse_args()
    for profile in args.profiles:
        print(summarize(profile), end="\n\n")
