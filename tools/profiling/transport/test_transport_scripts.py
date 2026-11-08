"""Check transport-link.py's link setup and choke verdicts, choke-chart.py's charts, and
transport-compare.py's parsing.

Usage (from the repository root)
-------------------------------
    python3 -m unittest discover -s tools/profiling/transport -p 'test_transport_scripts.py' -v

Uses only the standard library; no Docker, netem or benchmark build is needed. These are
checks of the scripts' own logic: run the scripts for real transfers.
"""

import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent


def load(path: str):
    spec = importlib.util.spec_from_file_location(Path(path).name.replace("-", "_"), HERE / f"{path}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses look their module up while it loads
    spec.loader.exec_module(module)
    return module


link = load("transport-link")
compare = load("transport-compare")
chart = load("support/choke-chart")

# 60 Mbit/s in MiB/s
LINK_60 = 60e6 / 8 / (1024 * 1024)


class NetemArgsTest(unittest.TestCase):
    def test_delay_is_half_the_rtt_per_crossing(self):
        self.assertTrue(link.netem_args(25, None).startswith("delay 12500us "))

    def test_rate_and_loss_are_optional(self):
        self.assertNotIn("rate", link.netem_args(10, None))
        self.assertNotIn("loss", link.netem_args(10, None))
        self.assertIn("rate 15mbit", link.netem_args(10, 15))
        self.assertIn("loss 1%", link.netem_args(10, None, 1))


class QueueTest(unittest.TestCase):
    def test_default_is_netem_alone_with_a_deep_queue(self):
        self.assertEqual(["tc qdisc add dev lo root netem delay 5000us limit 1000 rate 15mbit"],
                         link.link_setup(10, 15, 0.0, None, 1500))

    def test_queue_ms_is_a_byte_limit_at_the_link_rate(self):
        # 15 Mbit/s for 50 ms is 93750 bytes
        setup = link.link_setup(10, 15, 0.0, 50, 1500)
        self.assertEqual("tc qdisc add dev lo root handle 1: netem delay 5000us limit 1000", setup[0])
        self.assertEqual("tc qdisc add dev lo parent 1:1 handle 10: tbf rate 15mbit burst 3000 limit 93750",
                         setup[1])

    def test_choke_resizes_the_queue_with_each_rate(self):
        phases = [link.Phase(60, 4), link.Phase(15, 4)]
        script = link.container_script(["/b"], 10, phases, None, 1500, queue_ms=50)
        self.assertIn("tbf rate 60mbit burst 3000 limit 375000", script)
        self.assertIn("tc qdisc change dev lo parent 1:1 handle 10: tbf rate 15mbit burst 3000 limit 93750",
                      script)

    def test_two_qdiscs_add_up(self):
        text = (QDISC + "qdisc tbf 10: parent 1:1 rate 15Mbit burst 3000b lat 50ms  Sent 99 bytes 1 pkt "
                "(dropped 4, overlimits 9 requeues 0)  backlog 3000b 2p requeues 0 ")
        values = link.parse_stats("qdisc", text)
        self.assertEqual(7, values["dropped"])
        self.assertEqual(18140, values["backlog_bytes"])
        self.assertEqual(1048576, values["sent_bytes"])  # the root's


class ContainerTest(unittest.TestCase):
    def test_plain_run_execs_benchmark_as_caller(self):
        script = link.container_script(["/b", "--transport", "tcp"], 10, None, None, 1500)
        self.assertIn("ip link set lo up mtu 1500", script)
        self.assertIn(f"setpriv --reuid={os.getuid()}", script)
        self.assertTrue(script.splitlines()[-1].startswith("exec setpriv"))
        self.assertNotIn("perf", script)

    def test_perf_run_records_and_reports(self):
        script = link.container_script(["/b"], 10, None, None, 1500, perf=True)
        self.assertIn("perf record -q -g -o /tmp/link/perf.data -- setpriv", script)
        self.assertIn("--sort dso", script)
        self.assertIn("--sort sym", script)

    def test_perf_reports_a_failed_run_and_keeps_its_status(self):
        script = link.container_script(["/bin/false"], 10, None, None, 1500, perf=True)
        record = next(line for line in script.splitlines() if line.startswith("perf record"))
        self.assertTrue(record.endswith("|| STATUS=$?"))  # set -e would end the script here
        self.assertEqual("exit $STATUS", script.splitlines()[-1])

    def test_choke_changes_rate_after_each_phase(self):
        phases = [link.Phase(60, 4), link.Phase(15, 4), link.Phase(60, 4)]
        lines = link.container_script(["/b"], 10, phases, None, 1500).splitlines()
        changes = [line for line in lines if line.startswith(("sleep", "tc qdisc change"))]
        self.assertEqual(["sleep 4", "tc qdisc change", "sleep 4", "tc qdisc change"],
                         [line[:15] if line.startswith("tc") else line for line in changes])
        self.assertIn("rate 15mbit", changes[1])
        self.assertIn("rate 60mbit", changes[3])
        self.assertIn("tc qdisc add dev lo root netem delay 5000us limit 1000 rate 60mbit", lines)

    def test_perf_privileges_only_with_perf(self):
        plain = link.docker_command("true", "image")
        perf = link.docker_command("true", "image", perf=True)
        self.assertNotIn("PERFMON", plain)
        self.assertNotIn("host", plain[plain.index("--network"):plain.index("-v")])
        self.assertIn("PERFMON", perf)
        self.assertIn("--pid", perf)


class ChokeOutputTest(unittest.TestCase):
    def test_times_are_relative_to_the_benchmark_start(self):
        out = link.parse_choke_output(
            "base_rtt_ms 10.2\n"
            "start 1000.5\n"
            "sample utp 0.250 6.1\n"
            "transport  size_mib\n"
            "ping 1001.0 30.2\n")
        self.assertEqual(1000.5, out.start)
        self.assertEqual(10.2, out.base_rtt_ms)
        self.assertEqual([(0.25, 6.1)], out.samples)
        self.assertEqual([(0.5, 30.2)], out.pings)
        self.assertEqual(["transport  size_mib"], out.other)

    def test_samples_move_by_the_benchmark_startup(self):
        # The benchmark's own clock starts 0.4 s after the launch, once its sessions are up.
        out = link.parse_choke_output("start 1000.0\nstarted utp 1000.4\nsample utp 0.250 6.1\n")
        self.assertEqual(1, len(out.samples))
        self.assertAlmostEqual(0.65, out.samples[0][0])

    def test_pings_before_the_start_are_ignored(self):
        out = link.parse_choke_output("ping 999.0 10.0\nstart 1000.0\n")
        self.assertEqual([], out.pings)


class EvaluateTest(unittest.TestCase):
    phase = link.Phase(60, 4, start=0)

    def evaluate(self, transport, rates, ping_rtts=(), base=10.0):
        samples = [(1.0 + i * 0.5, r) for i, r in enumerate(rates)]
        pings = [(1.0 + i * 0.5, rtt) for i, rtt in enumerate(ping_rtts)]
        return link.evaluate(transport, [self.phase], 0.5, samples, pings, base)[0]

    def test_steady_rate_within_the_link_passes(self):
        result = self.evaluate("utp", [LINK_60 * 0.9] * 5, [30.0] * 5)
        self.assertEqual([], result.failures)
        self.assertAlmostEqual(10.0, result.queue_delay_ms)  # (30 - 10) / 2: the queue counts twice

    def test_a_zero_sample_is_a_stall(self):
        result = self.evaluate("utp", [LINK_60 * 0.9, 0.0, LINK_60 * 0.9])
        self.assertIn("stalled", result.failures)

    def test_under_half_the_link_fails(self):
        result = self.evaluate("tcp", [LINK_60 * 0.3] * 5)
        self.assertTrue(any(f.startswith("rate") for f in result.failures))

    def test_more_than_the_link_fails(self):
        result = self.evaluate("tcp", [LINK_60 * 1.5] * 5)
        self.assertTrue(any("> link" in f for f in result.failures))

    def test_queue_delay_limit_applies_to_utp_only(self):
        rtts = [10.0 + 2 * 200.0] * 5  # 200 ms of queueing
        utp = self.evaluate("utp", [LINK_60] * 5, rtts)
        self.assertTrue(any(f.startswith("queue delay") for f in utp.failures))
        self.assertEqual([], self.evaluate("tcp", [LINK_60] * 5, rtts).failures)

    def test_a_ping_faster_than_the_base_rtt_is_no_delay(self):
        result = self.evaluate("utp", [LINK_60] * 3, [10.0, 8.0, 9.0], base=10.0)
        self.assertEqual(0.0, result.queue_delay_ms)

    def test_samples_before_settling_are_ignored(self):
        samples = [(0.25, 0.0), (1.0, LINK_60), (2.0, LINK_60)]
        result = link.evaluate("utp", [self.phase], 0.5, samples, [], 10.0)[0]
        self.assertEqual([], result.failures)


QDISC = ("qdisc netem 8001: root refcnt 2 limit 1000 delay 5ms rate 60Mbit  Sent 1048576 bytes 700 pkt "
         "(dropped 3, overlimits 0 requeues 1)  backlog 15140b 10p requeues 1 ")


class StatsTest(unittest.TestCase):
    def test_qdisc_counters_and_backlog(self):
        values = link.parse_stats("qdisc", QDISC)
        self.assertEqual(1048576, values["sent_bytes"])
        self.assertEqual(3, values["dropped"])
        self.assertEqual(10, values["backlog_pkts"])
        self.assertEqual(15140, values["backlog_bytes"])

    def test_snmp_pairs(self):
        values = link.parse_stats("snmp", "Udp:InDatagrams=12 Udp:RcvbufErrors=0 Tcp:RetransSegs=4 ")
        self.assertEqual({"Udp:InDatagrams": 12, "Udp:RcvbufErrors": 0, "Tcp:RetransSegs": 4}, values)

    def test_ss_takes_the_sending_socket(self):
        text = ("ESTAB 0 0 127.0.0.1:1 127.0.0.1:2 \t cubic rtt:0.1/0.05 mss:1448 cwnd:10 "
                "ESTAB 0 0 127.0.0.1:2 127.0.0.1:1 \t cubic rtt:85.5/3 mss:1448 cwnd:700 ")
        self.assertEqual({"cwnd": 700, "rtt_ms": 85.5}, link.parse_stats("ss", text))

    def test_stats_lines_are_relative_to_the_start(self):
        out = link.parse_choke_output(f"start 100.0\nqdisc 100.5 {QDISC}\ncpu 101.0 30 12\n")
        self.assertEqual(0.5, out.stats["qdisc"][0][0])
        self.assertEqual({"cpu_ticks": 42}, out.stats["cpu"][0][1])


class TimelineTest(unittest.TestCase):
    def test_counters_are_interpolated_at_each_second(self):
        series = [(0.5, {"n": 100.0}), (1.5, {"n": 200.0}), (2.5, {"n": 400.0})]
        self.assertEqual(150.0, link.counter_delta(series, "n", 1.0, 2.0))  # 150 -> 300
        self.assertEqual(0.0, link.counter_delta(series, "n", 0.0, 0.5))  # before the first reading
        self.assertIsNone(link.counter_delta(series, "n", 2.0, 3.0))  # after the last

    def test_one_row_per_second_with_rates_and_drops(self):
        phases = [link.Phase(60, 1, start=0), link.Phase(15, 1, start=1)]
        out = link.ChokeOutput(0.0, 10.0, [(0.5, 6.0), (1.5, 1.5)], [(0.5, 30.0), (1.5, 50.0)], [])
        out.stats["qdisc"] = [(0.0, {"dropped": 0.0, "backlog_pkts": 2.0}),
                              (1.0, {"dropped": 0.0, "backlog_pkts": 40.0}),
                              (2.0, {"dropped": 5.0, "backlog_pkts": 90.0})]
        rows = link.timeline("utp", phases, out, 10.0)
        self.assertEqual(["0", "1"], [str(r["second"]) for r in rows])
        self.assertEqual("7.15", rows[0]["link_mib_s"])
        self.assertEqual("10", rows[0]["queue_delay_ms"])  # (30 - 10) / 2
        self.assertEqual("5", rows[1]["dropped"])
        self.assertEqual("90", rows[1]["queued_pkts"])
        self.assertEqual(1, rows[1]["pings"])


def chart_rows(schedule, transport="utp"):
    """Rows like a --csv run: (link Mbit/s, seconds) phases, rate at 90% of the link."""
    rows, second = [], 0
    for mbit, seconds in schedule:
        for _ in range(seconds):
            mib = mbit * 1e6 / 8 / (1024 * 1024)
            rows.append({"transport": transport, "second": str(second), "link_mib_s": f"{mib:.2f}",
                         "rate_mib_s": f"{mib * 0.9:.2f}", "queue_delay_ms": "20", "queue_delay_max_ms": "30",
                         "dropped": "0", "tcp_cwnd": ""})
            second += 1
    return rows


class ChokeChartTest(unittest.TestCase):
    def test_every_second_near_a_change_is_a_point(self):
        rows = chart_rows([(60, 30), (15, 30)])
        groups = chart.buckets(rows, 30)
        self.assertLessEqual(len(groups), 30)
        singles = {g.start for g in groups if len(g) == 1}
        self.assertTrue(set(range(28, 38)) <= singles)  # 2 s before to 8 s after the change at 30
        self.assertEqual(list(range(60)), [i for g in groups for i in g])  # every second, once

    def test_many_changes_narrow_the_window(self):
        rows = chart_rows([(60, 30), (15, 30), (4, 30), (60, 30), (15, 30)])
        groups = chart.buckets(rows, 30)
        self.assertLessEqual(len(groups), 30)
        self.assertTrue({30, 60, 90, 120} <= {g.start for g in groups if len(g) == 1})

    def test_labels_mark_changes_and_ranges(self):
        rows = chart_rows([(60, 20), (15, 20)])
        labels = chart.labels_for(rows, chart.buckets(rows, 15))
        self.assertIn('"↓20 15M"', labels)
        self.assertTrue(any("–" in label for label in labels))

    def test_all_zero_charts_are_left_out(self):
        markdown = chart.render(chart_rows([(60, 5), (15, 5)]), 30, show_all=False)
        self.assertIn("rate vs link", markdown)
        self.assertNotIn("Drops and errors", markdown.split("Left out")[0])
        self.assertIn("Left out, all zero: µTP: Drops and errors, per bucket.", markdown)  # not TCP's
        self.assertIn("Drops and errors", chart.render(chart_rows([(60, 5), (15, 5)]), 30, show_all=True)
                      .split("Left out")[0])

    def test_transition_table_measures_each_change(self):
        markdown = chart.render(chart_rows([(60, 5), (15, 5)]), 30, show_all=False)
        self.assertIn("| 5 | 60 → 15 | µTP | 0 | 30 | 0 | 0 |", markdown)

    def test_a_zero_delay_counts_as_settled(self):
        rows = chart_rows([(60, 3), (15, 4)])
        for row, delay in zip(rows[3:], ["300", "0", "0", "5"]):
            row["queue_delay_ms"] = row["queue_delay_max_ms"] = delay
        self.assertIn("| 3 | 60 → 15 | µTP | 0 | 300 | 1 | 0 |", chart.render(rows, 30, show_all=False))

    def test_both_transports_get_a_comparison(self):
        rows = chart_rows([(60, 5)], "tcp") + chart_rows([(60, 5)], "utp")
        self.assertIn("### TCP vs µTP", chart.render(rows, 30, show_all=False))


BENCHMARK_OUTPUT = """\
[2026-11-08T09:48:42.915+0100] ip-cache.cc:275: Couldn't obtain source address
sample utp 0.500 12.0
transport  size_mib  connect_s  transfer_s    MiB/s  Gbit/s   cpu_s  cpu_s/GiB
tcp              64      0.500       0.014   4518.7   37.91    0.03       0.48
utp              16      0.499      28.992      0.6    0.00    0.19      12.16
utp              16      0.007       0.091    176.4    1.48    0.17      10.88
"""


class CompareTest(unittest.TestCase):
    def test_parse_runs_reads_only_the_result_table(self):
        runs = compare.parse_runs(BENCHMARK_OUTPUT)
        self.assertEqual(["tcp", "utp", "utp"], [r.transport for r in runs])
        self.assertEqual(176.4, runs[2].mib_per_s)
        self.assertEqual(10.88, runs[2].cpu_s_per_gib)

    def test_table_counts_stalls_and_leaves_them_out_of_the_rates(self):
        utp = [r for r in compare.parse_runs(BENCHMARK_OUTPUT) if r.transport == "utp"]
        result = compare.RefResult("main", "abc", loopback_utp=utp, choke_ok=True)
        row = compare.format_table([result]).splitlines()[1].split()
        self.assertEqual(["main", "abc", "176.4", "10.88", "1/2"], row[:5])
        self.assertEqual(["ok", "-"], row[-2:])

    def test_missing_utp_runs_count_as_stalls(self):
        utp = [r for r in compare.parse_runs(BENCHMARK_OUTPUT) if r.transport == "utp"]
        result = compare.RefResult("main", "abc", loopback_utp=utp, utp_missing=4, choke_ok=True,
                                   failed=["loopback-utp"])
        row = compare.format_table([result]).splitlines()[1].split()
        self.assertEqual("5/6", row[4])
        self.assertEqual("loopback-utp", row[-1])

    def test_only_tool_paths_may_change(self):
        self.assertEqual([], compare.outside_tools(["tools/profiling/transport/support/choke-chart.py",
                                                    "tests/libtransmission/transport-benchmark.cc"]))
        self.assertEqual(["libtransmission/peer-io.cc", "tests/libtransmission/peer-io-test.cc"],
                         compare.outside_tools(["libtransmission/peer-io.cc", "tools/profiling/README.md",
                                                "tests/libtransmission/peer-io-test.cc"]))

    def test_table_shows_a_failed_ref(self):
        table = compare.format_table([compare.RefResult("x", error="git merge failed")])
        self.assertIn("git merge failed", table)

    def test_waits_until_few_cpus_are_busy(self):
        readings = iter([9.0, 6.0, 3.5])
        intervals = []

        def measure(interval_s):
            intervals.append(interval_s)
            return next(readings)

        with patch("sys.stderr") as stderr:
            self.assertEqual(3.5, compare.wait_for_quiet(4.0, measure))
        self.assertEqual([5.0, 20.0, 20.0], intervals)
        printed = "".join(call.args[0] for call in stderr.write.call_args_list)
        self.assertEqual("9.0 CPUs busy > 4, waiting.. 3.5\n", printed)

    def test_does_not_wait_when_quiet(self):
        with patch("sys.stderr") as stderr:
            self.assertEqual(1.0, compare.wait_for_quiet(4.0, lambda _: 1.0))
        stderr.write.assert_not_called()

    def test_busy_cpus_counts_all_but_idle_and_iowait(self):
        jiffies = iter([(100, 1000), (900, 2000)])  # 800 of 1000 jiffies busy
        with patch.object(compare, "cpu_jiffies", lambda: next(jiffies)), patch("time.sleep"), \
                patch("os.cpu_count", lambda: 16):
            self.assertAlmostEqual(12.8, compare.busy_cpus(20.0))

    def test_refuses_its_own_checkout(self):
        argv = ["transport-compare.py", "--worktree", str(HERE.parent.parent.parent), "main"]
        with patch("sys.argv", argv), self.assertRaises(SystemExit), patch("sys.stderr"):
            compare.main()


if __name__ == "__main__":
    unittest.main()
