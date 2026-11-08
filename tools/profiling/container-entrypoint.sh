#!/bin/sh
# Transmission sends UDP, and so µTP, only once it has found its own source address, which needs a
# route to the internet. The benchmark network is internal and has none, so eth0 gets a default
# route without a gateway: the lookup succeeds, and nothing can leave the network, because no
# address outside it answers on eth0. Then the benchmark runs as the invoking user.
set -e
ip route add default dev eth0
exec setpriv --reuid="${PROFILE_UID:-1000}" --regid="${PROFILE_GID:-1000}" --clear-groups \
    env HOME=/tmp python3 /src/tools/profiling/benchmark.py "$@"
