#!/usr/bin/env python3
"""Draw a choke run's per-second CSV (transport-link.py --choke --csv) as Mermaid charts in Markdown.

    choke-chart.py CSV [-o OUT.md] [--max-points N] [--all]

The rate changes are what the charts are for: from 2 s before to 8 s after each change, every
second is a point; steady stretches share the rest of the --max-points (default 30) in wider
buckets, in which rates are averaged and events (drops, errors, retransmissions) are summed.
An x label like "↓12" marks the second the link slowed. A table measures each change: the
delay peak, and how long the rate and the delay took to settle. A chart whose values are all zero is
left out unless --all is given; the note under the charts names what was left out. Charts
with no data at all, such as TCP's window on a µTP run, are not drawn. With both
transports in the CSV, a TCP vs µTP comparison comes first. Mermaid's xychart has no legend, so
each chart has a line of coloured squares in its series' colours.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path

# (colour for Mermaid, square for the legend)
GREY = ("#cbd5e1", "⬜")
BLUE = ("#2563eb", "🟦")
RED = ("#dc2626", "🟥")
ORANGE = ("#ea580c", "🟧")
GREEN = ("#16a34a", "🟩")
PURPLE = ("#9333ea", "🟪")

TRANSPORT_NAMES = {"utp": "µTP", "tcp": "TCP"}
LEDBAT_TARGET_MS = 100.0
MBIT_PER_MIB = 1024 * 1024 * 8 / 1e6
SUMMED = {"dropped", "overlimits", "requeues", "udp_rcvbuf_errors", "udp_sndbuf_errors", "udp_in_errors",
          "tcp_retrans_segs", "pings"}
MAXED = {"queue_delay_max_ms", "queued_pkts", "queued_kib", "queue_limit_kib"}


@dataclass
class Series:
    column: str
    label: str
    colour: tuple[str, str]
    bar: bool = False


@dataclass
class Chart:
    title: str
    unit: str
    series: list[Series]
    always: bool = False  # drawn even when every value is zero


def charts_for(transport: str) -> list[Chart]:
    name = TRANSPORT_NAMES.get(transport, transport)
    delay = [Series("queue_delay_ms", "median", BLUE), Series("queue_delay_max_ms", "max", ORANGE)]
    if transport == "utp":
        delay.append(Series("ledbat_target_ms", "LEDBAT target", RED))
    return [
        Chart(f"{name} rate vs link", "MiB/s",
              [Series("link_mib_s", "link", GREY, bar=True), Series("rate_mib_s", f"{name} rate", BLUE)],
              always=True),
        Chart("Queueing delay", "ms", delay, always=True),
        Chart("Drops and errors, per bucket", "packets",
              [Series("dropped", "netem drops", RED), Series("tcp_retrans_segs", "TCP retransmits", ORANGE),
               Series("udp_rcvbuf_errors", "UDP receive buffer full", PURPLE),
               Series("udp_sndbuf_errors", "UDP send buffer full", GREEN),
               Series("udp_in_errors", "UDP receive errors", BLUE)]),
        Chart("Bottleneck queue", "KiB",
              [Series("queue_limit_kib", "limit", GREY, bar=True), Series("queued_kib", "queued", BLUE)]),
        Chart("TCP congestion window", "packets", [Series("tcp_cwnd", "cwnd", BLUE)]),
        Chart("TCP smoothed RTT", "ms", [Series("tcp_rtt_ms", "srtt", BLUE)]),
        Chart("Packets per second", "packets/s",
              [Series("netem_pkts", "through netem", GREY, bar=True), Series("udp_out", "UDP sent", BLUE),
               Series("tcp_out_segs", "TCP sent", ORANGE)]),
        Chart("Benchmark CPU", "% of one CPU", [Series("cpu_pct", "CPU", BLUE)]),
    ]


def comparison_charts() -> list[Chart]:
    return [
        Chart("Rate: TCP vs µTP", "MiB/s",
              [Series("link_mib_s", "link", GREY, bar=True), Series("tcp:rate_mib_s", "TCP", ORANGE),
               Series("utp:rate_mib_s", "µTP", BLUE)], always=True),
        Chart("Queueing delay: TCP vs µTP", "ms",
              [Series("tcp:queue_delay_ms", "TCP", ORANGE), Series("utp:queue_delay_ms", "µTP", BLUE),
               Series("ledbat_target_ms", "LEDBAT target", RED)], always=True),
    ]


def value(cell: str) -> float | None:
    try:
        return float(cell)
    except ValueError:
        return None


FINE_BEFORE_S = 2
FINE_AFTER_S = 8


def changes(rows: list[dict]) -> list[int]:
    """Row indexes where the link rate differs from the row before, and the start: the ramp from nothing."""
    return [0] + [i for i in range(1, len(rows)) if rows[i]["link_mib_s"] != rows[i - 1]["link_mib_s"]]


def buckets(rows: list[dict], max_points: int) -> list[range]:
    """Row ranges: single seconds around each change, wider buckets between them."""
    n = len(rows)
    for before, after in [(FINE_BEFORE_S, FINE_AFTER_S), (1, 4), (0, 2), (0, 0)]:
        fine = set()
        for c in changes(rows):
            fine.update(range(max(0, c - before), min(n, c + after)))
        # the steady stretches between fine seconds
        stretches, i = [], 0
        while i < n:
            if i in fine:
                i += 1
                continue
            j = i
            while j < n and j not in fine:
                j += 1
            stretches.append(range(i, j))
            i = j
        room = max_points - len(fine)
        if room >= len(stretches):
            break
    size = 1
    while len(fine) + sum(math.ceil(len(r) / size) for r in stretches) > max_points and size < n:
        size += 1
    out = [range(i, i + 1) for i in fine]
    for r in stretches:
        out += [range(k, min(k + size, r.stop)) for k in range(r.start, r.stop, size)]
    return sorted(out, key=lambda r: r.start)


def bucket(rows: list[dict], column: str, groups: list[range]) -> list[float]:
    """The column per group of rows; a group without data repeats the one before it."""
    out: list[float] = []
    base = column.split(":")[-1]
    for group in groups:
        values = [v for v in (value(rows[i].get(column, "")) for i in group) if v is not None]
        if not values:
            out.append(0.0 if base in SUMMED else (out[-1] if out else 0.0))
        elif base in SUMMED:
            out.append(sum(values))
        elif base in MAXED:
            out.append(max(values))
        else:
            out.append(statistics.fmean(values))
    return out


def labels_for(rows: list[dict], groups: list[range]) -> list[str]:
    """The second or seconds of each group; at a link change, an arrow and the new rate in Mbit/s."""
    out = []
    for group in groups:
        i = group.start
        label = rows[i]["second"]
        if len(group) > 1:
            label += f"–{rows[group.stop - 1]['second']}"
        if i in changes(rows) and i > 0:
            new, old = value(rows[i]["link_mib_s"]) or 0.0, value(rows[i - 1]["link_mib_s"]) or 0.0
            mbit = new * MBIT_PER_MIB
            label = f"{'↓' if new < old else '↑'}{label}"
            label += f" {mbit:.0f}M"
        out.append(f'"{label}"')
    return out


def nice_ceiling(x: float) -> float:
    """1, 2 or 5 times a power of ten, at least x."""
    if x <= 0:
        return 1.0
    power = 10 ** math.floor(math.log10(x))
    return next(m * power for m in (1, 2, 5, 10) if m * power >= x)


def fmt(x: float) -> str:
    return f"{x:.2f}".rstrip("0").rstrip(".") or "0"


def mermaid(chart: Chart, labels: list[str], values: dict[str, list[float]]) -> str:
    top = nice_ceiling(max(max(values[s.column]) for s in chart.series) * 1.1)
    palette = ", ".join(s.colour[0] for s in chart.series)
    lines = [
        "```mermaid",
        '%%{init: {"xyChart": {"width": 900, "height": 300}, '
        f'"themeVariables": {{"xyChart": {{"plotColorPalette": "{palette}"}}}}}}}}%%',
        "xychart-beta",
        f'    title "{chart.title}"',
        f'    x-axis "seconds" [{", ".join(labels)}]',
        f'    y-axis "{chart.unit}" 0 --> {fmt(top)}',
    ]
    for s in chart.series:
        lines.append(f'    {"bar" if s.bar else "line"} [{", ".join(fmt(v) for v in values[s.column])}]')
    lines.append("```")
    legend = " · ".join(f"{s.colour[1]} {s.label}" for s in chart.series)
    return f"{legend}\n\n" + "\n".join(lines)


def draw(charts: list[Chart], rows: list[dict], groups: list[range],
         show_all: bool) -> tuple[list[str], list[str]]:
    """Mermaid blocks for the charts with data, and the titles of those left out."""
    labels = labels_for(rows, groups)
    drawn, hidden = [], []
    for chart in charts:
        present = [s for s in chart.series if any(value(r.get(s.column, "")) is not None for r in rows)]
        if not any(not s.bar and s.column != "ledbat_target_ms" for s in present):
            continue  # nothing measured for this transport, such as TCP's window on a µTP run
        values = {s.column: bucket(rows, s.column, groups) for s in present}
        busy = [s for s in present if any(v != 0 for v in values[s.column])]
        shown = present if chart.always or show_all else busy
        # A bar or a fixed target line alone says nothing about the run.
        if not any(not s.bar and s.column != "ledbat_target_ms" for s in shown):
            hidden.append(chart.title)
            continue
        drawn.append(mermaid(Chart(chart.title, chart.unit, shown), labels, values))
    return drawn, hidden


def below(x: float | None, limit: float) -> bool:
    """Whether a measured x is at most limit; a second without a measurement is not."""
    return x is not None and x <= limit


def settle_s(rows: list[dict], start: int, end: int, ok) -> str:
    """Seconds from start until the first second in [start, end) where ok(row) holds, or "-"."""
    for i in range(start, end):
        if ok(rows[i]):
            return str(i - start)
    return "-"


def transitions_table(by_transport: dict[str, list[dict]]) -> str:
    """Per link change and transport: delay peak and settling times, until the next change."""
    lines = ["| at s | link Mbit/s | transport | rate ≥ 80% of link after s | delay peak ms "
             f"| delay ≤ {fmt(LEDBAT_TARGET_MS)} ms after s | drops |",
             "|---|---|---|---|---|---|---|"]
    for t, rows in by_transport.items():
        marks = changes(rows)
        for c, nxt in zip(marks, marks[1:] + [len(rows)]):
            link = value(rows[c]["link_mib_s"]) or 0.0
            old = value(rows[c - 1]["link_mib_s"]) or 0.0 if c > 0 else 0.0
            mbit = f"{'start' if c == 0 else f'{old * MBIT_PER_MIB:.0f}'} → {link * MBIT_PER_MIB:.0f}"
            delays = [value(r["queue_delay_max_ms"]) for r in rows[c:nxt]]
            peak = max((d for d in delays if d is not None), default=None)
            drops = sum(value(r.get("dropped", "")) or 0.0 for r in rows[c:nxt])
            rate_ok = settle_s(rows, c, nxt, lambda r: (value(r["rate_mib_s"]) or 0.0) >= 0.8 * link)
            delay_ok = settle_s(rows, c, nxt, lambda r: below(value(r["queue_delay_ms"]), LEDBAT_TARGET_MS))
            lines.append(f"| {rows[c]['second']} | {mbit} | {TRANSPORT_NAMES.get(t, t)} | {rate_ok} "
                         f"| {f'{peak:.0f}' if peak is not None else '-'} | {delay_ok} | {fmt(drops)} |")
    return "\n".join(lines)


def table(rows: list[dict], columns: list[str]) -> str:
    used = [c for c in columns if any(r.get(c, "") not in ("", "0") for r in rows)]
    lines = ["| " + " | ".join(used) + " |", "|" + "---|" * len(used)]
    lines += ["| " + " | ".join(r.get(c, "") for c in used) + " |" for r in rows]
    return "\n".join(lines)


def render(rows: list[dict], max_points: int, show_all: bool) -> str:
    transports = list(dict.fromkeys(r["transport"] for r in rows))
    by_transport = {t: [r for r in rows if r["transport"] == t] for t in transports}
    for t_rows in by_transport.values():
        for r in t_rows:
            r["ledbat_target_ms"] = fmt(LEDBAT_TARGET_MS)
    longest = max(by_transport.values(), key=len)
    groups = buckets(longest, max_points)
    widest = max(len(g) for g in groups)
    parts = ["One point per second around each link change"
             + (f", up to {widest} s per point between them (rates averaged, events summed)."
                if widest > 1 else ".")]
    parts.append(transitions_table(by_transport))
    hidden_all: list[str] = []

    if {"tcp", "utp"} <= set(transports):
        merged = []
        for tcp, utp in zip(by_transport["tcp"], by_transport["utp"]):
            row = {"second": tcp["second"], "link_mib_s": tcp["link_mib_s"],
                   "ledbat_target_ms": fmt(LEDBAT_TARGET_MS)}
            row.update({f"tcp:{k}": v for k, v in tcp.items()})
            row.update({f"utp:{k}": v for k, v in utp.items()})
            merged.append(row)
        drawn, _ = draw(comparison_charts(), merged, groups, show_all)
        parts += ["### TCP vs µTP", *drawn]

    for t in transports:
        drawn, hidden = draw(charts_for(t), by_transport[t], groups, show_all)
        parts += [f"### {TRANSPORT_NAMES.get(t, t)}", *drawn]
        hidden_all += [f"{TRANSPORT_NAMES.get(t, t)}: {h}" for h in hidden]
        columns = [c for c in by_transport[t][0] if c not in ("transport", "ledbat_target_ms")]
        parts.append(f"<details><summary>{TRANSPORT_NAMES.get(t, t)} per second</summary>\n\n"
                     f"{table(by_transport[t], columns)}\n\n</details>")
    if hidden_all:
        parts.append("Left out, all zero: " + "; ".join(hidden_all) + ".")
    return "\n\n".join(parts) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path)
    parser.add_argument("-o", "--out", type=Path, help="default: stdout")
    parser.add_argument("--max-points", type=int, default=30, help="most points per chart")
    parser.add_argument("--all", action="store_true", help="also draw charts whose values are all zero")
    args = parser.parse_args()
    with args.csv.open(newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        parser.error(f"{args.csv} has no rows")
    markdown = render(rows, args.max_points, args.all)
    if args.out:
        args.out.write_text(markdown)
    else:
        sys.stdout.write(markdown)
    return 0


if __name__ == "__main__":
    sys.exit(main())
