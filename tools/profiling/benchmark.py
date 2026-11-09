"""Run and validate one local seed-to-downloader transfer.

Overview
--------
Starts two daemons with private configuration/download directories and a local
HTTP tracker and creates a deterministic payload/torrent. It flushes the source
before measuring, transfers over TCP or µTP, then flushes, re-verifies, and
checks the downloaded SHA-256. Temporary work files
are removed when the run exits; logs and measurements remain in the output.

Usage (from the repository root)
-------------------------------
    mkdir -p .tmp/work
    python3 tools/profiling/benchmark.py --daemon build/daemon/transmission-daemon --create build/utils/transmission-create --peer-address 192.168.2.21 --work-dir .tmp/work --output .tmp/results/manual-profile
    python3 tools/profiling/benchmark.py --help

Use a local non-loopback IPv4 address. Seed/downloader use adjacent peer and RPC
ports; choose unused pairs with --peer-port and --rpc-port. --disk-io-workers,
--disk-write-budget-mib, --download-limit-kilobytes-per-second, --encryption,
--transport, --size-mib, and --piece-size-kib control the workload. CPU affinity is unrestricted
unless --seed-cpus/--leech-cpus are set.
Requires built transmission-daemon/transmission-create and Python 3.11+; Linux
provides the CPU accounting and filesystem details. No third-party Python modules.

Outputs and optional diagnostics
--------------------------------
Writes metrics.json, transfer.jsonl, report.txt, and daemon/creation logs under a
UTC timestamp directory, or directly under --output with --output-exact. A build
with ENABLE_BLOCK_PROFILING=ON also writes seed.csv/leech.csv on normal shutdown.
TR_PROFILE_BUILD_ENABLED=ON/OFF records the build mode; --expect-block-profiling
checks that declaration and instrumented runs require both profiles. Set
TR_PROFILE_WRITE_BATCHES=1 for opt-in batch traces in an instrumented build.
--disk-baseline (or TR_PROFILE_DISK_BASELINE=1) adds fio filesystem baselines.
Live target-device disk load is recorded separately for transfer and final
fsync in metrics.json and diskload-*.jsonl, without running an extra workload.
Device counters include other processes; unavailable devices have an error record.
Block timings are overlapping wall times, not CPU time; measure throughput with
profiling disabled and use separate diagnostic runs to explain bottlenecks.
"""

import argparse
import base64
import csv
from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import os
from pathlib import Path
import platform
import random
import signal
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, build_opener, ProxyHandler

from report import summarize
from filesystem_benchmark import disk_target_eligible, filesystem_info, run_fio
from disk_load import DiskLoad


class Tracker(BaseHTTPRequestHandler):
    def do_GET(self):
        # Tracker peers must be non-loopback; use the container's private IPv4.
        peer = socket.inet_aton(self.server.peer_address) + struct.pack("!H", self.server.peer_port)
        body = b"d8:completei1e10:incompletei1e8:intervali1e5:peers6:" + peer + b"e"
        self.send_response(200)
        self.send_header("Content-Type", "application/x-bittorrent")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


class Rpc:
    def __init__(self, port):
        self.url = f"http://127.0.0.1:{port}/transmission/rpc"
        self.token = ""
        self.opener = build_opener(ProxyHandler({}))

    def call(self, method, **params):
        data = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode()
        for _ in range(2):
            request = Request(self.url, data, {"X-Transmission-Session-Id": self.token,
                                              "Content-Type": "application/json"})
            try:
                with self.opener.open(request, timeout=5) as response:
                    result = json.load(response)
                if "error" in result:
                    raise RuntimeError(f"{method}: {result['error']}")
                return result["result"]
            except HTTPError as error:
                if error.code != 409:
                    raise
                self.token = error.headers["X-Transmission-Session-Id"]
                error.close()
        raise RuntimeError("RPC session token negotiation failed")


def wait_for(check, timeout, label, process, poll_interval=0.1):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Daemon exited ({process.returncode}) while waiting for {label}")
        try:
            result = check()
            if result:
                return result
        except (URLError, TimeoutError, ConnectionError):
            pass
        time.sleep(poll_interval)
    raise TimeoutError(f"Timed out after {timeout}s waiting for {label}")


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError("Daemon did not shut down; profile may be missing")
    if process.returncode != 0:
        raise RuntimeError(f"Daemon exited with status {process.returncode}")


def start_daemon(stack, binary, root, output, name, rpc_port, peer_port, disk_io_workers=2,
                 encryption="preferred", verify_mode="fast", disk_write_budget_mib=None, peer_limit=None, cpus=None,
                 transport="tcp", preallocation=None):
    # Fail before launching or sending RPC to an unrelated running daemon.
    with ExitStack() as ports:
        # µTP runs over UDP on the peer port.
        for port, kind in ((rpc_port, socket.SOCK_STREAM), (peer_port, socket.SOCK_STREAM),
                           (peer_port, socket.SOCK_DGRAM)):
            listener = ports.enter_context(socket.socket(socket.AF_INET, kind))
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind(("0.0.0.0", port))
    config = root / name / "config"
    download = root / name / "data"
    config.mkdir(parents=True)
    download.mkdir()
    settings = {
        "rpc_enabled": True, "rpc_bind_address": "127.0.0.1", "rpc_port": rpc_port,
        "rpc_authentication_required": False, "rpc_whitelist_enabled": False,
        "rpc_host_whitelist_enabled": False, "peer_port": peer_port,
        "peer_port_random_on_start": False, "dht_enabled": False,
        "lpd_enabled": False, "pex_enabled": False,
        # one transport per run, so the measured transfer can't use the other
        "tcp_enabled": transport == "tcp", "utp_enabled": transport == "utp",
        "port_forwarding_enabled": False, "speed_limit_up_enabled": False,
        "speed_limit_down_enabled": False, "alt_speed_enabled": False,
        "download_dir": str(download), "incomplete_dir_enabled": False,
        "rename_partial_files": False, "sleep_per_seconds_during_verify": 0,
        "ratio_limit_enabled": False, "idle_seeding_limit_enabled": False,
        "disk_io_workers": disk_io_workers,
        "encryption": encryption,
        "torrent_added_verify_mode": verify_mode,
    }
    if preallocation is not None:
        settings["preallocation"] = preallocation
    if disk_write_budget_mib is not None:
        settings["disk_write_budget_mib"] = disk_write_budget_mib
    if peer_limit is not None:
        # Outbound candidate selection reserves headroom below the global
        # limit. Leave enough space for the active transfer peer.
        settings["peer_limit_global"] = peer_limit * 2
        settings["peer_limit_per_torrent"] = peer_limit
    (config / "settings.json").write_text(json.dumps(settings))
    log = stack.enter_context((output / f"{name}.log").open("w"))
    env = dict(os.environ, TR_PROFILE_OUTPUT=str(output / f"{name}.csv"))
    command = [str(binary), "--foreground", "--config-dir", str(config)]
    if cpus:
        command = ["taskset", "--cpu-list", cpus, *command]
    process = subprocess.Popen(command,
                               stdout=log, stderr=subprocess.STDOUT, env=env)
    stack.callback(stop, process)
    rpc = Rpc(rpc_port)
    wait_for(lambda: rpc.call("session_get"), 30, f"{name} startup", process)
    return process, rpc, download


def torrent_state(rpc, torrent_id, progress=False):
    fields = ["id", "status", "percent_done", "left_until_done", "recheck_progress", "error", "error_string"]
    if progress:
        fields += ["downloaded_ever", "rate_download", "peers_connected", "peers_sending_to_us", "peers"]
    torrents = rpc.call("torrent_get", ids=[torrent_id], fields=fields)["torrents"]
    if len(torrents) != 1:
        raise RuntimeError("Benchmark torrent disappeared")
    state = torrents[0]
    if state["error"]:
        raise RuntimeError(state["error_string"])
    return state


def complete(rpc, torrent_id):
    state = torrent_state(rpc, torrent_id)
    return is_complete(state)


def is_complete(state):
    return state["percent_done"] == 1 and state["left_until_done"] == 0 and state["status"] == 6


def transfer_progress(rpc, torrent_id, started, stream, milestones, size_bytes, samples=None, disk_load=None):
    if disk_load is not None:
        disk_load.sample()
    state = torrent_state(rpc, torrent_id, progress=True)
    elapsed = time.monotonic() - started
    sample = {"elapsed_seconds": elapsed, **state}
    if samples is not None:
        samples.append(sample)
    stream.write(json.dumps(sample) + "\n")
    stream.flush()
    conditions = {
        "first_peer_seconds": state["peers_connected"] > 0,
        "first_sender_seconds": state["peers_sending_to_us"] > 0,
        "first_payload_seconds": state["downloaded_ever"] > 0,
        "download_counter_reached_size_seconds": state["downloaded_ever"] >= size_bytes,
    }
    for name, reached in conditions.items():
        if reached:
            milestones.setdefault(name, elapsed)
    return is_complete(state)


def verified_throughput(samples, size_bytes):
    """Measure actual verified-byte deltas between observed 10% and 90% crossings."""
    start = None
    for sample in samples:
        verified = size_bytes - sample["left_until_done"]
        if start is None and verified >= size_bytes * 0.1:
            start = sample, verified
        if start is not None and verified >= size_bytes * 0.9:
            first, first_bytes = start
            elapsed = sample["elapsed_seconds"] - first["elapsed_seconds"]
            n_bytes = verified - first_bytes
            if elapsed <= 0 or n_bytes < size_bytes * 0.5:
                return None
            return {"method": "observed 10%-90% verified-byte window", "seconds": elapsed,
                    "bytes": n_bytes, "start_verified_bytes": first_bytes, "end_verified_bytes": verified,
                    "mib_per_second": n_bytes / elapsed / (1024 * 1024),
                    "gbit_per_second": n_bytes * 8 / elapsed / 1e9}
    return None


def preallocation_mode(requested):
    """auto: none on tmpfs, where fast preallocation writes every page of the file; else the daemon default."""
    if requested != "auto":
        return int(requested)
    return 0 if os.environ.get("TR_PROFILE_STORAGE") == "tmpfs" else None


def data_phase(start, end, size_mib):
    """CPU from the first payload byte the leech reports to completion: no startup, connecting or unchoke wait."""
    if not start or any(value is None for value in [*start["cpu"].values(), *end["cpu"].values()]):
        return None
    seconds = end["time"] - start["time"]
    cpu = {name: {key: end["cpu"][name][key] - start["cpu"][name][key] for key in start["cpu"][name]}
           for name in start["cpu"]}
    total = sum(sum(values.values()) for values in cpu.values())
    return {"seconds": seconds, "cpu": cpu, "cpu_seconds": total,
            "cpu_seconds_per_gib": total / (size_mib / 1024),
            "mib_per_second": size_mib / seconds if seconds > 0 else None}


def process_cpu(pid):
    """Linux CPU counters for a child, without depending on perf permissions."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()
        ticks = os.sysconf("SC_CLK_TCK")
        return {"user_seconds": int(fields[11]) / ticks, "system_seconds": int(fields[12]) / ticks}
    except (OSError, ValueError, IndexError):
        return None


def thread_cpu(pid):
    """Linux CPU counters per thread, keyed by thread id."""
    threads = {}
    ticks = os.sysconf("SC_CLK_TCK")
    try:
        tasks = list(Path(f"/proc/{pid}/task").iterdir())
    except OSError:
        return threads
    for task in tasks:
        try:
            text = (task / "stat").read_text()
        except OSError:
            continue  # the thread exited
        fields = text.rpartition(")")[2].split()
        threads[int(task.name)] = {"comm": text[text.index("(") + 1:text.rindex(")")],
                                   "user_seconds": int(fields[11]) / ticks,
                                   "system_seconds": int(fields[12]) / ticks}
    return threads


def thread_cpu_delta(before, after, seconds):
    """Per-thread CPU over an interval, busiest first; percent is of one core."""
    rows = []
    for tid, end in after.items():
        start = before.get(tid, {"user_seconds": 0, "system_seconds": 0})
        user = end["user_seconds"] - start["user_seconds"]
        system = end["system_seconds"] - start["system_seconds"]
        rows.append({"tid": tid, "comm": end["comm"], "user_seconds": user, "system_seconds": system,
                     "percent_of_one_core": (user + system) / seconds * 100})
    return sorted(rows, key=lambda row: row["percent_of_one_core"], reverse=True)


# A thread that ran one of these scopes has that role; earlier entries win.
THREAD_ROLES = [("peer.bandwidth_pulse", "session"), ("storage.write_run", "disk worker"),
                ("storage.test_piece", "disk worker"), ("verify.torrent", "verify")]


def thread_roles(profile):
    """Map OS thread ids to roles, using the scopes each thread recorded."""
    scopes = {}
    try:
        with Path(profile).open(newline="") as stream:
            for row in csv.DictReader(stream):
                if row.get("os_thread", "0") != "0":
                    scopes.setdefault(int(row["os_thread"]), set()).add(row["scope"])
    except OSError:
        return {}
    roles = {}
    for tid, names in scopes.items():
        roles[tid] = next((role for scope, role in THREAD_ROLES if scope in names), "other")
    return roles


def thread_report(name, rows):
    lines = [f"{name}: CPU per thread during the transfer (percent of one core)",
             f"{'Thread':>10} {'Role':12} {'User s':>8} {'System s':>9} {'CPU %':>7}"]
    for row in rows:
        lines.append(f"{row['tid']:>10} {row.get('role', '?'):12} {row['user_seconds']:8.2f} "
                     f"{row['system_seconds']:9.2f} {row['percent_of_one_core']:7.1f}")
    return "\n".join(lines)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def receive_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ConnectionError("Idle peer disconnected during handshake")
        data.extend(chunk)
    return bytes(data)


def attach_idle_peers(stack, address, port, info_hash, count):
    """Connect inert peers to a separate seeding torrent on the downloader."""
    protocol = b"\x13BitTorrent protocol"
    if len(info_hash) != 20:
        raise ValueError("Torrent info hash must be 20 bytes")
    def connect_one(index):
        sock = socket.create_connection((address, port), timeout=5)
        try:
            sock.sendall(protocol + bytes(8) + info_hash + b"-RT0001-" + f"{index:012d}".encode())
            reply = receive_exact(sock, 68)
            if not reply.startswith(protocol) or reply[28:48] != info_hash:
                raise RuntimeError(f"Idle peer {index} received an invalid handshake")
            sock.settimeout(None)
            return sock
        except BaseException:
            sock.close()
            raise

    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=min(32, count)) as pool:
        futures = [pool.submit(connect_one, index) for index in range(count)]
        for connected, future in enumerate(as_completed(futures), start=1):
            stack.callback(future.result().close)
            if connected % 100 == 0:
                print(f"Connected {connected}/{count} idle peers in {time.monotonic() - started:.1f}s", flush=True)


def check_profiles(block_profiling, profiles):
    written = {path.name for path in profiles}
    if block_profiling == "ON" and written != {"seed.csv", "leech.csv"}:
        raise RuntimeError("Instrumented daemons did not write both profiles; check daemon logs")
    if block_profiling == "OFF" and written:
        raise RuntimeError(f"Run labelled profiling OFF wrote {sorted(written)}; the daemon is instrumented")


def run(args, output):
    metrics = {"size_mib": args.size_mib, "platform": platform.platform(),
               "started_utc": datetime.now(timezone.utc).isoformat(),
               "verification_throttle_ms": 0, "transport": args.transport,
               "peer_address": args.peer_address,
               "disk_io_workers": args.disk_io_workers,
               "encryption": args.encryption, "poll_interval_seconds": args.poll_interval,
               "piece_size_kib": args.piece_size_kib,
               "disk_write_budget_mib": args.disk_write_budget_mib,
               "download_speed_limit_kilobytes_per_second": args.download_limit_kilobytes_per_second,
               "download_speed_limit_gbit_per_second": (
                   args.download_limit_kilobytes_per_second * 8 / 1_000_000
                   if args.download_limit_kilobytes_per_second is not None else None),
               "idle_peers_requested": args.idle_peers,
               "seed_verify_mode": args.seed_verify_mode,
               "preallocation": preallocation_mode(args.preallocation),
               "work_storage": os.environ.get("TR_PROFILE_STORAGE", "unspecified"),
               "target_gbit_per_second": args.target_gbit,
               "block_profiling": os.environ.get("TR_PROFILE_BUILD_ENABLED", "unknown")}
    metrics["version"] = subprocess.check_output([str(args.daemon), "--version"], stderr=subprocess.STDOUT, text=True).strip()
    with ExitStack() as stack:
        root = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="retransmission-profile-", dir=args.work_dir)))
        metrics["work_filesystem"] = filesystem_info(root)
        metrics["disk_target_eligible"] = disk_target_eligible(metrics["work_filesystem"], metrics["work_storage"])
        if metrics["work_storage"] == "tmpfs" and shutil.disk_usage(root).free < args.size_mib * 1024 * 1024 * 2.1:
            raise RuntimeError("Insufficient tmpfs space for both payload copies; increase TMPFS_SIZE or reduce SIZE_MIB")
        if args.disk_baseline:
            if not metrics["disk_target_eligible"]:
                raise RuntimeError("Disk baseline requires a detected disk-backed work filesystem")
            metrics["disk_baseline"] = run_fio(root, output, args.size_mib)
            (output / "disk-baseline.json").write_text(json.dumps(metrics["disk_baseline"], indent=2) + "\n")
        tracker = ThreadingHTTPServer(("127.0.0.1", 0), Tracker)
        tracker.peer_address = args.peer_address
        tracker.peer_port = args.peer_port
        thread = threading.Thread(target=tracker.serve_forever, daemon=True)
        thread.start()
        stack.callback(tracker.server_close)
        stack.callback(thread.join)
        stack.callback(tracker.shutdown)
        seed_process, seed, seed_dir = start_daemon(
            stack, args.daemon, root, output, "seed", args.rpc_port, args.peer_port, args.disk_io_workers,
            args.encryption, args.seed_verify_mode, args.disk_write_budget_mib, cpus=args.seed_cpus,
            transport=args.transport, preallocation=metrics["preallocation"])
        leech_process, leech, leech_dir = start_daemon(
            stack, args.daemon, root, output, "leech", args.rpc_port + 1, args.peer_port + 1,
            args.disk_io_workers, "preferred" if args.idle_peers and args.encryption == "required" else args.encryption,
            disk_write_budget_mib=args.disk_write_budget_mib,
            peer_limit=max(64, args.idle_peers + 16) if args.idle_peers else None, cpus=args.leech_cpus,
            transport=args.transport, preallocation=metrics["preallocation"])
        if args.download_limit_kilobytes_per_second is not None:
            leech.call("session_set", speed_limit_down_enabled=True,
                       speed_limit_down=args.download_limit_kilobytes_per_second)
            settings = leech.call("session_get")
            if (settings.get("speed_limit_down_enabled") is not True
                    or settings.get("speed_limit_down") != args.download_limit_kilobytes_per_second):
                raise RuntimeError("downloader speed limit was not applied")
        if hasattr(os, "sched_getaffinity"):  # Linux only
            metrics["cpu_affinity"] = {name: sorted(os.sched_getaffinity(process.pid))
                                       for name, process in (("seed", seed_process), ("leech", leech_process))}
        metrics["daemon_pids"] = {"seed": seed_process.pid, "leech": leech_process.pid}
        payload = seed_dir / "payload.bin"
        rng = random.Random(0)
        with payload.open("wb") as stream:
            for _ in range(args.size_mib):
                stream.write(rng.randbytes(1024 * 1024))
            # Keep seed-file creation writeback out of the transfer disk-load window.
            stream.flush()
            os.fsync(stream.fileno())
        metrics["source_fsynced_before_transfer"] = True
        source_hash = digest(payload)
        torrent = root / "payload.torrent"
        with (output / "create.log").open("w") as log:
            subprocess.run([str(args.create), "--private", "--tracker",
                            f"http://127.0.0.1:{tracker.server_port}/announce", "--piecesize", str(args.piece_size_kib),
                            "--outfile", str(torrent), str(payload)], check=True, stdout=log, stderr=subprocess.STDOUT,
                           env={key: value for key, value in os.environ.items() if key != "TR_PROFILE_OUTPUT"},
                           timeout=args.timeout)
        metainfo = base64.b64encode(torrent.read_bytes()).decode()
        seed_started = time.monotonic()
        seed_id = seed.call("torrent_add", metainfo=metainfo, download_dir=str(seed_dir))["torrent_added"]["id"]
        wait_for(lambda: complete(seed, seed_id), args.timeout, "seed verification", seed_process)
        metrics["seed_ready_seconds"] = time.monotonic() - seed_started
        idle_id = None
        if args.idle_peers:
            decoy = leech_dir / "idle-peer-decoy.bin"
            decoy.write_bytes(bytes(1024 * 1024))
            decoy_torrent = root / "idle-peer-decoy.torrent"
            with (output / "decoy-create.log").open("w") as log:
                subprocess.run([str(args.create), "--outfile", str(decoy_torrent), str(decoy)],
                               check=True, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
            idle_id = leech.call("torrent_add", metainfo=base64.b64encode(decoy_torrent.read_bytes()).decode(),
                                 download_dir=str(leech_dir))["torrent_added"]["id"]
            wait_for(lambda: complete(leech, idle_id), args.timeout, "decoy verification", leech_process)
            hash_string = leech.call("torrent_get", ids=[idle_id], fields=["hash_string"])["torrents"][0]["hash_string"]
            attach_idle_peers(stack, args.peer_address, args.peer_port + 1, bytes.fromhex(hash_string), args.idle_peers)
            wait_for(lambda: torrent_state(leech, idle_id, progress=True)["peers_connected"] >= args.idle_peers,
                     args.timeout, "idle peer connections", leech_process)
        metrics["idle_peers_at_start"] = (
            torrent_state(leech, idle_id, progress=True)["peers_connected"] if idle_id is not None else 0)
        cpu_start = {name: process_cpu(process.pid) for name, process in (("seed", seed_process), ("leech", leech_process))}
        threads_start = {name: thread_cpu(process.pid) for name, process in (("seed", seed_process), ("leech", leech_process))}
        with DiskLoad(metrics["work_filesystem"], output, "transfer") as disk_load:
            started = time.monotonic()
            leech_id = leech.call("torrent_add", metainfo=metainfo, download_dir=str(leech_dir))["torrent_added"]["id"]
            metrics["transfer_milestones"] = {}
            samples = []
            data_start = {}

            def poll():
                done = transfer_progress(
                    leech, leech_id, started, progress, metrics["transfer_milestones"], args.size_mib * 1024 * 1024,
                    samples, disk_load)
                if not data_start and "first_payload_seconds" in metrics["transfer_milestones"]:
                    data_start.update(time=time.monotonic(), cpu={name: process_cpu(process.pid) for name, process in
                                                                  (("seed", seed_process), ("leech", leech_process))})
                return done

            with (output / "transfer.jsonl").open("w") as progress:
                wait_for(poll, args.timeout, "download", leech_process, args.poll_interval)
            metrics["transfer_seconds"] = time.monotonic() - started
            data_end = {"time": time.monotonic(), "cpu": {name: process_cpu(process.pid) for name, process in
                                                          (("seed", seed_process), ("leech", leech_process))}}
        metrics["disk_load"] = {"transfer": disk_load.summary()}
        metrics["idle_peers_at_end"] = (
            torrent_state(leech, idle_id, progress=True)["peers_connected"] if idle_id is not None else 0)
        if metrics["idle_peers_at_end"] < args.idle_peers:
            raise RuntimeError("Idle peer count fell during transfer; this run cannot measure peer-scan scaling")
        metrics["thread_cpu"] = {
            name: thread_cpu_delta(threads_start[name], thread_cpu(process.pid), metrics["transfer_seconds"])
            for name, process in (("seed", seed_process), ("leech", leech_process))}
        metrics["transfer_mib_per_second"] = args.size_mib / metrics["transfer_seconds"]
        metrics["transfer_gbit_per_second"] = args.size_mib * 1024 * 1024 * 8 / metrics["transfer_seconds"] / 1e9
        metrics["steady_state"] = verified_throughput(samples, args.size_mib * 1024 * 1024)
        metrics["payload_rate_target_met"] = (metrics["steady_state"] is not None and
                                 metrics["steady_state"]["gbit_per_second"] >= args.target_gbit)
        metrics["target_met"] = metrics["disk_target_eligible"] and metrics["payload_rate_target_met"]
        peers = [peer for sample in samples for peer in sample.get("peers", [])]
        metrics["observed_peer_encryption"] = sorted({peer["is_encrypted"] for peer in peers})
        metrics["observed_peer_transports"] = sorted({"utp" if peer["is_utp"] else "tcp" for peer in peers})
        if metrics["observed_peer_transports"] != [args.transport]:
            raise RuntimeError(f"Requested {args.transport}, but peers used {metrics['observed_peer_transports']}")
        metrics["transfer_cpu"] = {}
        for name, process in (("seed", seed_process), ("leech", leech_process)):
            before, after = cpu_start[name], process_cpu(process.pid)
            if before is not None and after is not None:
                cpu = {key: after[key] - before[key] for key in before}
                cpu["percent_of_one_core"] = sum(cpu.values()) / metrics["transfer_seconds"] * 100
                metrics["transfer_cpu"][name] = cpu
        metrics["data_phase"] = data_phase(data_start, data_end, args.size_mib)
        with DiskLoad(metrics["work_filesystem"], output, "flush") as disk_load:
            sync_started = time.monotonic()
            with (leech_dir / "payload.bin").open("r+b") as downloaded:
                os.fsync(downloaded.fileno())
            metrics["download_fsync_seconds"] = time.monotonic() - sync_started
        metrics["disk_load"]["flush"] = disk_load.summary()
        # Stop first so the end of verification is distinguishable from the prior seeding state.
        leech.call("torrent_stop", ids=[leech_id])
        wait_for(lambda: torrent_state(leech, leech_id)["status"] == 0,
                 args.timeout, "torrent stop", leech_process)
        started = time.monotonic()
        leech.call("torrent_verify", ids=[leech_id])
        wait_for(lambda: torrent_state(leech, leech_id)["status"] not in (1, 2),
                 args.timeout, "verification", leech_process)
        metrics["verify_seconds"] = time.monotonic() - started
        state = torrent_state(leech, leech_id)
        if state["percent_done"] != 1 or state["left_until_done"] != 0:
            raise RuntimeError(f"Verification failed: {state}")
        if digest(leech_dir / "payload.bin") != source_hash:
            raise RuntimeError("Downloaded SHA-256 differs from source")
        metrics["sha256"] = source_hash
    # Scope totals are written on normal daemon shutdown by ExitStack.
    for name, rows in metrics["thread_cpu"].items():
        roles = thread_roles(output / f"{name}.csv")
        for row in rows:
            row["role"] = roles.get(row["tid"], "unknown")
    profiles = sorted(output.glob("*.csv"))
    check_profiles(metrics["block_profiling"], profiles)
    # Resuming studies treat metrics.json as proof of a complete run, so it comes after every check.
    (output / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    report = "\n\n".join([thread_report(name, rows) for name, rows in metrics["thread_cpu"].items()] +
                         [summarize(path) for path in profiles])
    (output / "report.txt").write_text(report + "\n")
    print(json.dumps(metrics, indent=2), flush=True)
    print(report, flush=True)
    if not profiles:
        print("No block profiles: this build has profiling disabled.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("results"))
    parser.add_argument("--output-exact", action="store_true",
                        help="Write directly to --output instead of creating a timestamped subdirectory")
    parser.add_argument("--size-mib", type=int, default=1024)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--daemon", type=Path, default=Path("/build/daemon/transmission-daemon"))
    parser.add_argument("--create", type=Path, default=Path("/build/utils/transmission-create"))
    parser.add_argument("--peer-address", help="Local non-loopback IPv4 (default: resolve container hostname)")
    parser.add_argument("--peer-port", type=int, default=51413, help="Seed peer port; downloader uses the next port")
    parser.add_argument("--rpc-port", type=int, default=19091, help="Seed RPC port; downloader uses the next port")
    parser.add_argument("--disk-io-workers", type=int, default=2, help="Disk workers per daemon (default: 2; 0: session-thread I/O)")
    parser.add_argument("--download-limit-kilobytes-per-second", type=int,
                        help="Set the downloader session speed limit (decimal kB/s)")
    parser.add_argument("--expect-block-profiling", choices=["ON", "OFF"], help="Fail if image profiling mode differs")
    parser.add_argument("--encryption", choices=["preferred", "allowed", "required"], default="preferred")
    parser.add_argument("--transport", choices=["tcp", "utp"], default="tcp",
                        help="Peer transport; both daemons disable the other one (default: tcp)")
    parser.add_argument("--poll-interval", type=float, default=0.02)
    parser.add_argument("--target-gbit", type=float, default=10.0)
    parser.add_argument("--piece-size-kib", type=int, default=256)
    parser.add_argument("--disk-write-budget-mib", type=int,
                        default=int(os.environ["DISK_WRITE_BUDGET_MIB"]) if os.environ.get("DISK_WRITE_BUDGET_MIB") else None,
                        help="Downloaded data allowed in flight toward the disk (default: the daemon's)")
    parser.add_argument("--seed-cpus", help="Linux CPU list for all seed threads")
    parser.add_argument("--leech-cpus", help="Linux CPU list for all downloader threads")
    parser.add_argument("--idle-peers", type=int, default=0,
                        help="Inert peers connected to a separate torrent on the downloader")
    parser.add_argument("--seed-verify-mode", choices=["fast", "full"], default="fast")
    parser.add_argument("--preallocation", choices=["auto", "0", "1", "2"], default="auto",
                        help="Daemon preallocation; auto: 0 on tmpfs, else the daemon default")
    parser.add_argument("--work-dir", type=Path, default=os.environ.get("TR_PROFILE_WORK_DIR"))
    parser.add_argument("--disk-baseline", action="store_true", default=os.environ.get("TR_PROFILE_DISK_BASELINE") == "1")
    args = parser.parse_args()
    if not (1 <= args.peer_port < 65535 and 1 <= args.rpc_port < 65535):
        parser.error("peer-port and rpc-port must be between 1 and 65534")
    if {args.peer_port, args.peer_port + 1} & {args.rpc_port, args.rpc_port + 1}:
        parser.error("peer and RPC port pairs must not overlap")
    if args.size_mib <= 0 or args.timeout <= 0:
        parser.error("size and timeout must be positive")
    if args.poll_interval <= 0 or args.target_gbit <= 0:
        parser.error("poll interval and throughput target must be positive")
    if args.piece_size_kib < 16 or args.piece_size_kib & (args.piece_size_kib - 1):
        parser.error("piece size must be a power of two of at least 16 KiB")
    if not 0 <= args.disk_io_workers <= 64:
        parser.error("disk-io-workers must be between 0 and 64")
    if (args.download_limit_kilobytes_per_second is not None
            and args.download_limit_kilobytes_per_second <= 0):
        parser.error("download limit must be positive")
    if args.disk_write_budget_mib is not None and args.disk_write_budget_mib < 1:
        parser.error("disk-write-budget-mib must be positive")
    if not 0 <= args.idle_peers <= 1000:
        parser.error("idle-peers must be between 0 and 1000")
    if args.idle_peers and args.transport != "tcp":
        parser.error("idle peers connect over TCP, so they need --transport tcp")
    if args.expect_block_profiling and os.environ.get("TR_PROFILE_BUILD_ENABLED") != args.expect_block_profiling:
        parser.error("image profiling mode differs from requested mode; rebuild the benchmark image")
    print(f"Benchmark settings: block_profiling={os.environ.get('TR_PROFILE_BUILD_ENABLED', 'unknown')} "
          f"disk_io_workers={args.disk_io_workers} encryption={args.encryption} transport={args.transport} "
          f"piece_size_kib={args.piece_size_kib} seed_verify_mode={args.seed_verify_mode} "
          f"disk_write_budget_mib={args.disk_write_budget_mib or 'default'} "
          f"download_limit_kBps={args.download_limit_kilobytes_per_second or 'none'} "
          f"idle_peers={args.idle_peers}", flush=True)
    args.peer_address = args.peer_address or socket.gethostbyname(socket.gethostname())
    address = ipaddress.IPv4Address(args.peer_address)
    if address.is_loopback or address.is_unspecified or address.is_multicast:
        parser.error("peer-address must be a local non-loopback unicast IPv4 address")
    # torrent_add rejects relative paths, and mkdtemp keeps them relative before Python 3.12.
    if args.work_dir is not None:
        args.work_dir = args.work_dir.resolve()
    output = args.output.resolve()
    if not args.output_exact:
        output /= datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True, exist_ok=args.output_exact)
    print(f"Results: {output}", flush=True)
    try:
        run(args, output)
    except Exception as error:
        (output / "failure.txt").write_text(f"{type(error).__name__}: {error}\n")
        raise


if __name__ == "__main__":
    main()
