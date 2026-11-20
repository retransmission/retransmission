#!/usr/bin/env bash
# This file Copyright © Retransmission authors and contributors.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosaic LLC.
# License text can be found in the licenses/ folder.

# Checks that a daemon bound to one interface sends nothing through any other.
#
# The daemon reaches a fake tracker at 192.0.2.1 by two links: lan0, the
# default route, and vpn0, a second route standing in for a VPN. It runs a
# magnet whose trackers are HTTP and UDP on 192.0.2.1, with DHT, LPD and port
# forwarding on. tcpdump records every packet the daemon's side sends, leaving
# out only DNS and the kernel's own ARP, ICMPv6, IGMP and MLD reports (ICMPv6
# behind a hop-by-hop header, which the icmp6 filter doesn't match).
#
#   1. bind_interface vpn0: nothing may leave through lan0, and the tracker
#      must hear the daemon through vpn0.
#   2. bind_interface blocked: nothing may leave through either link.
#   3. bind_interface empty: traffic leaves through lan0, which shows the
#      capture sees what it would have to report.
#
# Needs root in a network namespace of its own with no other link, since it adds
# links and routes, and a default route elsewhere would carry phase 3's traffic.
# With Docker, running the host's build from a container:
#   docker run --rm --network none --cap-add NET_ADMIN --cap-add SYS_ADMIN \
#     --security-opt apparmor=unconfined -v /:/host:ro -v "$PWD/out:/host$PWD/out" \
#     debian:trixie-slim sh -c "mount -t proc proc /host/proc && chroot /host \
#     $PWD/tests/network/bind-interface-leak.sh $PWD/build/daemon/transmission-daemon $PWD/out"
#
# Usage: bind-interface-leak.sh <transmission-daemon> <work-dir>

set -eu

DAEMON_BIN="$(realpath "$1")"
WORK="$2"
PHASE_SECS="${PHASE_SECS:-20}"
RPC="http://127.0.0.1:9091/transmission/rpc"

# a fresh directory inside the given one, so a mistyped path loses nothing
mkdir -p "$WORK"
WORK="$(mktemp -d "$WORK/leak.XXXXXX")"

pids=()
cleanup() {
    for pid in "${pids[@]}"; do
        kill "$pid" 2> /dev/null || true
    done
}
trap cleanup EXIT

# --- the network

ip link set lo up

# the far side: the tracker's address and both links' other ends
unshare -n sleep infinity &
far=$!
pids+=("$far")
sleep 0.2

ip link add lan0 type veth peer name lan1 netns "$far"
ip link add vpn0 type veth peer name vpn1 netns "$far"
ip addr add 10.10.0.1/24 dev lan0
ip addr add 10.20.0.1/24 dev vpn0
ip link set lan0 up
ip link set vpn0 up
nsenter -t "$far" -n sh -c '
    ip link set lo up
    ip addr add 192.0.2.1/32 dev lo
    ip addr add 10.10.0.2/24 dev lan1
    ip addr add 10.20.0.2/24 dev vpn1
    ip link set lan1 up
    ip link set vpn1 up'

# the default route takes lan0; a socket bound to vpn0 takes the second route
ip route add default via 10.10.0.2 dev lan0 metric 100
ip route add default via 10.20.0.2 dev vpn0 metric 200

# --- the tracker: answers HTTP announces, and logs every request's source

cat > "$WORK/tracker.py" << 'EOF'
import http.server, socket, sys, threading

log = open(sys.argv[1], "a", buffering=1)

class Tracker(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        log.write(f"http {self.client_address[0]} {self.path}\n")
        body = b"d8:intervali1800e5:peers0:e"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

def udp():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 8000))
    while True:
        _, addr = sock.recvfrom(2048)
        log.write(f"udp {addr[0]}\n")

threading.Thread(target=udp, daemon=True).start()
http.server.ThreadingHTTPServer(("0.0.0.0", 8000), Tracker).serve_forever()
EOF
nsenter -t "$far" -n python3 -I "$WORK/tracker.py" "$WORK/tracker.log" &
pids+=("$!")

# --- captures, each phase in its own files

# Sets $capture_pid. Not run as $(capture ...): the command substitution would
# wait for tcpdump, which holds its output open until it is killed.
capture() { # <phase> <interface>
    tcpdump -i "$2" -n -U -w "$WORK/$1-$2.pcap" \
        'not arp and not icmp6 and not igmp and not port 53 and not (ip6 and ip6[6] == 0)' > /dev/null 2>&1 &
    capture_pid=$!
    pids+=("$capture_pid")
    # tcpdump drops what arrives before it has opened the interface
    sleep 1
}

count() { # <phase> <interface> [extra filter]
    tcpdump -n -r "$WORK/$1-$2.pcap" ${3:+"$3"} 2> /dev/null | wc -l
}

# --- the daemon

config="$WORK/config"
mkdir -p "$config/downloads"
cat > "$config/settings.json" << EOF
{
    "bind_interface": "vpn0",
    "dht_enabled": true,
    "download_dir": "$config/downloads",
    "lpd_enabled": true,
    "message_level": 4,
    "port_forwarding_enabled": true,
    "rpc_authentication_required": false,
    "rpc_bind_address": "127.0.0.1",
    "rpc_whitelist_enabled": false
}
EOF

capture 1-vpn0 lan0
lan_cap=$capture_pid
"$DAEMON_BIN" -f -g "$config" > "$WORK/daemon.log" 2>&1 &
daemon=$!
pids+=("$daemon")
sleep 2

rpc() { # <json>
    local sid
    sid=$(curl -s -o /dev/null -D - "$RPC" | sed -n 's/^X-Transmission-Session-Id: *\([^\r]*\).*/\1/ip')
    curl -s -H "X-Transmission-Session-Id: $sid" "$RPC" -d "$1"
}

magnet='magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&tr=http%3A%2F%2F192.0.2.1%3A8000%2Fannounce&tr=udp%3A%2F%2F192.0.2.1%3A8000'
rpc "{\"jsonrpc\":\"2.0\",\"method\":\"torrent_add\",\"params\":{\"filename\":\"$magnet\"},\"id\":1}" > /dev/null

sleep "$PHASE_SECS"
kill "$lan_cap"
heard_vpn=$(grep -c ' 10\.20\.0\.1' "$WORK/tracker.log" || true)
heard_lan=$(grep -c ' 10\.10\.0\.1' "$WORK/tracker.log" || true)
leaked_1=$(count 1-vpn0 lan0)

# --- phase 2: blocked

# captured once the switch has returned: a packet sent before it is phase 1's
rpc '{"jsonrpc":"2.0","method":"session_set","params":{"bind_interface":"blocked"},"id":2}' > /dev/null
capture 2-blocked lan0
lan_cap=$capture_pid
capture 2-blocked vpn0
vpn_cap=$capture_pid
sleep "$PHASE_SECS"
kill "$lan_cap" "$vpn_cap"
leaked_2=$(($(count 2-blocked lan0) + $(count 2-blocked vpn0)))

# --- phase 3: the default route, so lan0 must see traffic

rpc '{"jsonrpc":"2.0","method":"session_set","params":{"bind_interface":""},"id":3}' > /dev/null
capture 3-default lan0
lan_cap=$capture_pid
sleep "$PHASE_SECS"
kill "$lan_cap"
seen_3=$(count 3-default lan0)

echo "1. bound to vpn0: tracker heard ${heard_vpn} via vpn0 and ${heard_lan} via lan0; ${leaked_1} packets on lan0"
echo "2. blocked: ${leaked_2} packets on lan0 and vpn0"
echo "3. default route: ${seen_3} packets on lan0"

status=0
if [ "$leaked_1" -ne 0 ] || [ "$heard_lan" -ne 0 ]; then
    echo "FAIL: traffic left through lan0 while bound to vpn0"
    tcpdump -n -r "$WORK/1-vpn0-lan0.pcap" 2> /dev/null | head -20
    status=1
fi
if [ "$heard_vpn" -eq 0 ]; then
    echo "FAIL: the tracker never heard the daemon through vpn0"
    status=1
fi
if [ "$leaked_2" -ne 0 ]; then
    echo "FAIL: traffic left while the binding was blocked"
    tcpdump -n -r "$WORK/2-blocked-lan0.pcap" 2> /dev/null | head -10
    tcpdump -n -r "$WORK/2-blocked-vpn0.pcap" 2> /dev/null | head -10
    status=1
fi
if [ "$seen_3" -eq 0 ]; then
    echo "FAIL: nothing on lan0 on the default route, so the capture proves nothing"
    status=1
fi
[ "$status" -eq 0 ] && echo "PASS"
exit "$status"
