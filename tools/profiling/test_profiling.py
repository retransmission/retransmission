"""Check the profiling harness, filesystem helper, and C++ instrumentation.

Usage (from the repository root)
-------------------------------
    python3 -m unittest discover -s tools/profiling -v
    PYTHONPATH=tools/profiling python3 -m unittest test_profiling.ScopeTest -v

Requires Python 3.11+ and a C++17 compiler named c++, with thread support.
Uses Python's standard unittest/mock modules; no third-party Python packages,
Docker image, installed daemon binaries, fio, or perf permissions are required.
Temporary directories hold compiled smoke binaries and generated profiles and
are removed automatically.

Coverage and output
-------------------
Compiles profile-smoke.cc with instrumentation enabled and disabled. Checks
nested/self timing, threads, queued/stall durations, exception unwinding, CSV
escaping, optional write-batch traces, and disabled/runtime opt-out behavior.
Harness tests cover tracker replies, RPC behavior, daemon settings, completion,
CPU accounting, filesystem/fio summaries, live disk-load counter units/resets,
and throughput calculations using mocks
or synthetic data. Decimal-comma locale coverage is skipped when unavailable.
Unittest prints pass/fail/skip results and exits nonzero on failure. These are
component checks; run benchmark.py or storage-study.py for real transfer/storage
performance and end-to-end data-integrity validation.
"""

from contextlib import ExitStack, nullcontext
import csv
from email.message import Message
import io
import json
import os
from pathlib import Path
import runpy
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
from urllib.error import HTTPError

from benchmark import (Tracker, Rpc, check_profiles, complete, process_cpu, start_daemon, thread_cpu, thread_cpu_delta, thread_roles,
                       torrent_state, transfer_progress, verified_throughput, wait_for)
from report import summarize
from filesystem_benchmark import disk_target_eligible, filesystem_info, summarize_fio
from disk_load import DiskLoad, FIELDS, rates, read_stats


class ScopeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        repo = Path(__file__).resolve().parents[2]
        cls.binaries = {}
        for enabled in (False, True):
            binary = cls.directory / ("enabled" if enabled else "disabled")
            command = ["c++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(repo),
                       str(Path(__file__).with_name("profile-smoke.cc")), "-o", str(binary)]
            if enabled:
                command.append("-DTR_ENABLE_BLOCK_PROFILING")
            subprocess.run(command, check=True, capture_output=True, text=True)
            cls.binaries[enabled] = binary

    def test_nested_scopes_threads_and_unwinding(self):
        path = self.directory / "timings.csv"
        subprocess.run([str(self.binaries[True])], check=True, env=dict(os.environ, TR_PROFILE_OUTPUT=str(path)))
        with path.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        child = [row for row in rows if row["scope"] == "child"]
        self.assertEqual(len(child), 2)
        self.assertEqual(sum(int(row["calls"]) for row in child), 4)
        self.assertEqual(len({row["thread"] for row in child}), 2)
        parent = next(row for row in rows if row["scope"] == "parent")
        self.assertGreater(float(parent["total_ms"]), 15)
        self.assertGreater(float(parent["total_ms"]) - float(parent["self_ms"]), 15)
        self.assertTrue(any(row["scope"] == 'quoted,"scope' for row in rows))
        queued = next(row for row in rows if row["scope"] == "queued")
        self.assertEqual(int(queued["calls"]), 1)
        self.assertGreater(float(queued["total_ms"]), 9)
        stalled = next(row for row in rows if row["scope"] == "stalled")
        self.assertEqual(int(stalled["calls"]), 1)
        self.assertGreater(float(stalled["total_ms"]), 19)
        if sys.platform == "linux":
            self.assertEqual(len({row["os_thread"] for row in child}), 2)
            self.assertNotIn("0", {row["os_thread"] for row in child})
        report = summarize(path)
        self.assertIn("parent", report)
        self.assertIn("child", report)

    def test_decimal_comma_locale_keeps_csv_fields(self):
        available = subprocess.run(["locale", "-a"], capture_output=True, text=True).stdout.split()
        comma = next((name for name in ("nb_NO.utf8", "de_DE.utf8", "fr_FR.utf8") if name in available), None)
        if comma is None:
            self.skipTest("no decimal-comma locale installed")
        path = self.directory / "comma.csv"
        subprocess.run([str(self.binaries[True])], check=True, env=dict(os.environ, LC_ALL=comma, TR_PROFILE_OUTPUT=str(path)))
        with path.open(newline="") as stream:
            rows = list(csv.reader(stream))
        self.assertEqual({len(row) for row in rows}, {8})
        parent = next(row for row in rows if row[1] == "parent")
        self.assertGreater(float(parent[3]), 15)

    def test_disabled_build_writes_nothing(self):
        path = self.directory / "disabled.csv"
        subprocess.run([str(self.binaries[False])], check=True, env=dict(os.environ, TR_PROFILE_OUTPUT=str(path)))
        self.assertFalse(path.exists())

    def test_write_batch_trace_is_opt_in(self):
        for enabled in (False, True):
            path = self.directory / f"batch-{enabled}.csv"
            subprocess.run([str(self.binaries[enabled])], check=True,
                           env=dict(os.environ, TR_PROFILE_OUTPUT=str(path), TR_PROFILE_WRITE_BATCHES="1"))
            trace = Path(str(path) + ".batches.tsv")
            if not enabled:
                self.assertFalse(trace.exists())
                continue
            with trace.open() as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 1)
            self.assertEqual({key: int(value) for key, value in rows[0].items() if key != "thread"},
                             dict(torrent=7, offset=16384, bytes=32768, blocks=2,
                                  queue_mean_ns=100, queue_max_ns=150, write_ns=200, error=0))
        path = self.directory / "batch-opt-out.csv"
        subprocess.run([str(self.binaries[True])], check=True,
                       env=dict(os.environ, TR_PROFILE_OUTPUT=str(path), TR_PROFILE_WRITE_BATCHES="0"))
        self.assertFalse(Path(str(path) + ".batches.tsv").exists())

    def test_no_runtime_output_writes_nothing(self):
        env = {key: value for key, value in os.environ.items() if key != "TR_PROFILE_OUTPUT"}
        subprocess.run([str(self.binaries[True])], check=True, env=env, cwd=self.directory)


class HarnessTest(unittest.TestCase):
    def test_worker_study_resume_allows_appending_counts_without_changing_workload(self):
        check = runpy.run_path(str(Path(__file__).with_name("storage-study.py")))["previous_workers_for_resume"]
        previous = dict(workers=[0, 1, 2], timing_only=True, size_mib=4096, repeats=3, binary_sha256={"OFF": "a"})
        extended = dict(previous, workers=[0, 1, 2, 4])
        self.assertEqual(check(previous, extended), [0, 1, 2])
        self.assertEqual(previous["workers"], [0, 1, 2])
        for changed in (dict(extended, size_mib=1024), dict(extended, repeats=4),
                        dict(extended, binary_sha256={"OFF": "b"}),
                        dict(extended, workers=[2, 1, 0, 4]), dict(extended, workers=[0, 1])):
            with self.assertRaisesRegex(RuntimeError, "different settings"):
                check(previous, changed)

    def test_profile_files_must_match_the_declared_build(self):
        both = [Path("leech.csv"), Path("seed.csv")]
        check_profiles("ON", both)
        check_profiles("OFF", [])
        check_profiles("unknown", both)
        with self.assertRaisesRegex(RuntimeError, "both profiles"):
            check_profiles("ON", both[:1])
        with self.assertRaisesRegex(RuntimeError, "instrumented"):
            check_profiles("OFF", both)

    def test_disk_load_rates_use_sector_units_and_elapsed_time(self):
        before = dict.fromkeys(FIELDS, 0)
        after = dict(before, read_ios=10, read_sectors=2048, read_ms=30,
                     write_ios=20, write_sectors=4096, write_ms=100,
                     busy_ms=1000, weighted_ms=4000, in_flight=3)
        row = rates(before, after, 2)
        self.assertEqual(row["read_mib_per_second"], .5)
        self.assertEqual(row["write_mib_per_second"], 1)
        self.assertEqual(row["write_iops"], 10)
        self.assertEqual(row["write_await_ms"], 5)
        self.assertEqual(row["average_queue_depth"], 2)
        self.assertEqual(row["busy_percent"], 50)
        self.assertEqual(row["in_flight"], 3)
        self.assertIsNone(rates(before, before, 1)["write_await_ms"])
        with self.assertRaisesRegex(ValueError, "reset"):
            rates(after, before, 1)

    def test_disk_load_parses_optional_counters_and_reports_missing_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "stat"
            path.write_text(" ".join(str(i) for i in range(17)))
            self.assertEqual(read_stats(path)["weighted_ms"], 10)
            path.write_text("1 2")
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                read_stats(path)
            with patch("disk_load.device_paths", side_effect=FileNotFoundError("missing device")):
                with DiskLoad({}, root, "transfer") as monitor:
                    monitor.sample()
                self.assertFalse(monitor.summary()["available"])
                self.assertIn("missing device", monitor.summary()["error"])

    def test_batch_summary_keeps_units_and_weights_queue_by_blocks(self):
        summarize_batches = runpy.run_path(str(Path(__file__).with_name("storage-study.py")))["batches"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "batches.tsv"
            path.write_text("thread\ttorrent\toffset\tbytes\tblocks\tqueue_mean_ns\tqueue_max_ns\twrite_ns\terror\n"
                            "101\t7\t0\t16384\t1\t100\t100\t200\t0\n"
                            "102\t7\t16384\t49152\t3\t300\t500\t600\t5\n")
            result = summarize_batches(path)
            self.assertEqual(result["total_bytes"], 65536)
            self.assertEqual(result["total_blocks"], 4)
            self.assertEqual(result["single_block_batches"], 1)
            self.assertEqual(result["block_weighted_queue_mean_ns"], 250)
            self.assertEqual(result["write_ns"]["median"], 400)
            self.assertEqual(result["write_ns"]["p95"], 600)
            self.assertEqual(result["errors"], 1)
            self.assertEqual(result["workers"], [101, 102])

    def test_tracker_advertises_selected_seed_port(self):
        tracker = Tracker.__new__(Tracker)
        tracker.server = Mock(peer_address="192.0.2.1", peer_port=52413)
        tracker.wfile = io.BytesIO()
        tracker.send_response = Mock()
        tracker.send_header = Mock()
        tracker.end_headers = Mock()
        tracker.do_GET()
        peer = socket.inet_aton("192.0.2.1") + struct.pack("!H", 52413)
        self.assertIn(b"5:peers6:" + peer + b"e", tracker.wfile.getvalue())

    def test_ram_and_unknown_filesystems_cannot_meet_disk_target(self):
        for filesystem in ({"fstype": "tmpfs"}, {"fstype": "ramfs"}, {}, {"fstype": None}):
            self.assertFalse(disk_target_eligible(filesystem, "bind"))
        self.assertFalse(disk_target_eligible({"fstype": "btrfs"}, "tmpfs"))
        self.assertTrue(disk_target_eligible({"fstype": "btrfs"}, "bind"))

    def test_filesystem_info_uses_structured_findmnt_output(self):
        mount = {"fstype": "btrfs", "source": "/dev/nvme0n1p4", "target": "/work"}
        with patch("filesystem_benchmark.subprocess.run", return_value=Mock(stdout=json.dumps({"filesystems": [mount]}))):
            self.assertEqual(filesystem_info(Path("/work/with spaces")), mount)
        with patch("filesystem_benchmark.subprocess.run", side_effect=FileNotFoundError()):
            self.assertEqual(filesystem_info(Path("/work"))["fstype"], "unknown")

    def test_fio_summary_reports_decimal_gigabits(self):
        job = {"error": 0, "write": {"io_bytes": 6250000000, "runtime": 5000, "iops": 1000}}
        self.assertEqual(summarize_fio({"jobs": [job]}, "write")["gbit_per_second"], 10)
        job["error"] = 5
        with self.assertRaisesRegex(RuntimeError, "failed"):
            summarize_fio({"jobs": [job]}, "write")

    def test_daemon_settings_match_requested_transfer_modes(self):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            root = Path(directory)
            with patch("benchmark.subprocess.Popen"), patch("benchmark.wait_for"), patch("benchmark.stop"), \
                    patch("benchmark.socket.socket"):
                start_daemon(stack, Path("daemon"), root, root, "seed", 19091, 51413,
                             disk_io_workers=4, encryption="allowed", verify_mode="full")
                settings = json.loads((root / "seed/config/settings.json").read_text())
                self.assertEqual(settings["encryption"], "allowed")
                self.assertEqual(settings["disk_io_workers"], 4)
                self.assertEqual(settings["torrent_added_verify_mode"], "full")
                self.assertNotIn("disk_write_budget_mib", settings)
                start_daemon(stack, Path("daemon"), root, root, "leech", 19092, 51414, disk_write_budget_mib=256)
                settings = json.loads((root / "leech/config/settings.json").read_text())
                self.assertEqual(settings["disk_write_budget_mib"], 256)
                self.assertEqual(settings["disk_io_workers"], 2)
                stack.close()

    def test_thread_roles_come_from_recorded_scopes(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / "leech.csv"
            profile.write_text("thread,scope,calls,total_ms,self_ms,min_ms,max_ms,os_thread\n"
                               '0,"peer.bandwidth_pulse",1,1,1,1,1,101\n'
                               '0,"storage.deliver_completions",1,1,1,1,1,101\n'
                               '1,"storage.write_run",1,1,1,1,1,102\n'
                               '2,"session.close",1,1,1,1,1,103\n'
                               '3,"profile-off",1,1,1,1,1,0\n')
            self.assertEqual(thread_roles(profile), {101: "session", 102: "disk worker", 103: "other"})
            self.assertEqual(thread_roles(Path(directory) / "missing.csv"), {})

    def test_thread_cpu_delta_ranks_busiest_and_counts_new_threads(self):
        before = {1: {"comm": "a", "user_seconds": 1.0, "system_seconds": 0.0}}
        after = {1: {"comm": "a", "user_seconds": 1.5, "system_seconds": 0.0},
                 2: {"comm": "b", "user_seconds": 1.0, "system_seconds": 1.0}}
        rows = thread_cpu_delta(before, after, 2.0)
        self.assertEqual([row["tid"] for row in rows], [2, 1])
        self.assertEqual(rows[0]["percent_of_one_core"], 100)
        self.assertEqual(rows[1]["percent_of_one_core"], 25)

    @unittest.skipUnless(sys.platform == "linux", "reads /proc")
    def test_thread_cpu_reads_this_process(self):
        threads = thread_cpu(os.getpid())
        self.assertIn(os.getpid(), threads)
        self.assertGreaterEqual(threads[os.getpid()]["user_seconds"], 0)

    def test_busy_port_fails_before_daemon_launch_or_rpc(self):
        with ExitStack() as stack, patch("benchmark.socket.socket") as socket_mock, \
                patch("benchmark.subprocess.Popen") as launch, patch("benchmark.Rpc") as rpc:
            socket_mock.return_value.__enter__.return_value.bind.side_effect = OSError("Address already in use")
            with self.assertRaisesRegex(OSError, "Address already in use"):
                start_daemon(stack, Path("daemon"), Path("unused"), Path("unused"), "seed", 19091, 52413)
            launch.assert_not_called()
            rpc.assert_not_called()

    def test_verified_throughput_uses_unique_verified_bytes(self):
        samples = [
            {"elapsed_seconds": t, "left_until_done": remaining, "downloaded_ever": 999999}
            for t, remaining in [(1, 1000), (2, 850), (3, 400), (4, 50), (5, 0)]
        ]
        result = verified_throughput(samples, 1000)
        self.assertEqual(result["bytes"], 800)
        self.assertEqual(result["seconds"], 2)
        self.assertEqual(result["gbit_per_second"], 800 * 8 / 2 / 1e9)

    def test_verified_throughput_requires_a_measurable_window(self):
        self.assertIsNone(verified_throughput([], 1000))
        self.assertIsNone(verified_throughput([{"elapsed_seconds": 1, "left_until_done": 0}], 1000))
        self.assertIsNone(verified_throughput([{"elapsed_seconds": 1, "left_until_done": 950}], 1000))

    def test_process_cpu_handles_spaces_in_process_name(self):
        fields = ["0"] * 22
        fields[11], fields[12] = "250", "75"
        with patch("benchmark.Path.read_text", return_value="123 (name with ) spaces) " + " ".join(fields)), \
                patch("benchmark.os.sysconf", return_value=100):
            self.assertEqual(process_cpu(123), {"user_seconds": 2.5, "system_seconds": 0.75})

    def test_image_mode_mismatch_fails_before_starting_daemons(self):
        script = Path(__file__).with_name("benchmark.py")
        result = subprocess.run(
            ["python3", str(script), "--expect-block-profiling", "OFF"],
            env=dict(os.environ, TR_PROFILE_BUILD_ENABLED="ON"), capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("image profiling mode differs", result.stderr)

    def test_transfer_progress_records_first_observations(self):
        rpc = Mock()
        state = {"percent_done": 0, "left_until_done": 1024, "status": 4, "error": 0,
                 "downloaded_ever": 0, "rate_download": 0, "peers_connected": 0, "peers_sending_to_us": 0}
        rpc.call.return_value = {"torrents": [state]}
        stream = io.StringIO()
        milestones = {}
        with patch("benchmark.time.monotonic", side_effect=[11.0, 12.0, 13.0]):
            self.assertFalse(transfer_progress(rpc, 1, 10, stream, milestones, 1024))
            self.assertEqual(milestones, {})
            state.update(downloaded_ever=512, peers_connected=1, peers_sending_to_us=1)
            self.assertFalse(transfer_progress(rpc, 1, 10, stream, milestones, 1024))
            state.update(downloaded_ever=1024, percent_done=1, left_until_done=0, status=6)
            self.assertTrue(transfer_progress(rpc, 1, 10, stream, milestones, 1024))
        self.assertEqual(milestones["first_payload_seconds"], 2.0)
        self.assertEqual(milestones["first_peer_seconds"], 2.0)
        self.assertEqual(milestones["download_counter_reached_size_seconds"], 3.0)
        samples = [json.loads(line) for line in stream.getvalue().splitlines()]
        self.assertEqual([sample["downloaded_ever"] for sample in samples], [0, 512, 1024])

    def test_rpc_retries_csrf_and_uses_json_rpc(self):
        rpc = Rpc(19091)
        headers = Message()
        headers["X-Transmission-Session-Id"] = "test-token"
        response = io.BytesIO(json.dumps({"result": {"version": "test"}}).encode())
        rpc.opener = Mock()
        rpc.opener.open.side_effect = [HTTPError(rpc.url, 409, "CSRF", headers, None), nullcontext(response)]
        self.assertEqual(rpc.call("session_get"), {"version": "test"})
        second = rpc.opener.open.call_args_list[1].args[0]
        self.assertIn("test-token", second.headers.values())
        self.assertEqual(json.loads(second.data)["method"], "session_get")

    def test_rpc_error_is_not_a_success(self):
        rpc = Rpc(19091)
        rpc.opener = Mock()
        rpc.opener.open.return_value = nullcontext(io.BytesIO(b'{"error":{"code":1}}'))
        with self.assertRaisesRegex(RuntimeError, "session_get"):
            rpc.call("session_get")

    def test_completion_requires_verified_seeding_state(self):
        rpc = Mock()
        state = {"percent_done": 1, "left_until_done": 0, "status": 2, "error": 0}
        rpc.call.return_value = {"torrents": [state]}
        self.assertFalse(complete(rpc, 1))
        state["status"] = 6
        self.assertTrue(complete(rpc, 1))
        state.update(error=3, error_string="disk failure")
        with self.assertRaisesRegex(RuntimeError, "disk failure"):
            torrent_state(rpc, 1)

    def test_wait_detects_crashed_daemon(self):
        process = Mock(returncode=9)
        process.poll.return_value = 9
        with self.assertRaisesRegex(RuntimeError, "Daemon exited"):
            wait_for(lambda: False, 1, "download", process)


class RunScriptTest(unittest.TestCase):
    def test_arguments_reach_both_docker_build_and_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            git = root / "git"
            git.write_text("#!/bin/sh\nif [ \"$1\" = rev-parse ]; then echo test-revision; fi\n")
            docker = root / "docker"
            docker.write_text(
                "#!/usr/bin/env python3\nimport json, os, sys\n"
                "print(json.dumps({'command': sys.argv[1:], 'profiling': os.environ['BLOCK_PROFILING'], "
                "'workers': os.environ['DISK_IO_WORKERS'], 'encryption': os.environ['PEER_ENCRYPTION']}))\n")
            git.chmod(0o755)
            docker.chmod(0o755)
            script = Path(__file__).with_name("run.sh").resolve()
            env = dict(os.environ, PATH=f"{root}{os.pathsep}{os.environ['PATH']}",
                       BLOCK_PROFILING="ON", DISK_IO_WORKERS="0")
            for storage in ("tmpfs", "bind"):
                with self.subTest(storage=storage):
                    result = subprocess.run(
                        ["bash", str(script), "--block-profiling", "OFF", "--disk-io-workers", "4",
                         "--encryption", "allowed", "--storage", storage],
                        env=dict(env, WORK_DIRECTORY=str(root / "payload")), check=True, capture_output=True, text=True)
                    calls = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
                    self.assertEqual(len(calls), 2)  # docker info output is redirected.
                    self.assertEqual(calls[0]["command"][-1], "build")
                    self.assertEqual(calls[1]["command"][-3:], ["run", "--rm", "benchmark"])
                    for call in calls:
                        self.assertIn(f"tools/profiling/compose.{storage}.yaml", call["command"])
                        self.assertEqual(call["profiling"], "OFF")
                        self.assertEqual(call["workers"], "4")
                        self.assertEqual(call["encryption"], "allowed")

            # With no override, both compose invocations use the application default.
            default_env = dict(env)
            default_env.pop("DISK_IO_WORKERS")
            result = subprocess.run(["bash", str(script)], env=default_env,
                                    check=True, capture_output=True, text=True)
            calls = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            self.assertEqual(len(calls), 2)
            self.assertTrue(all(call["workers"] == "2" for call in calls))


if __name__ == "__main__":
    unittest.main()
