#!/usr/bin/env bash
set -euo pipefail

BLOCK_PROFILING="${BLOCK_PROFILING:-ON}"
DISK_IO_WORKERS="${DISK_IO_WORKERS:-2}"
PEER_ENCRYPTION="${PEER_ENCRYPTION:-preferred}"
PEER_TRANSPORT="${PEER_TRANSPORT:-tcp}"
WORK_STORAGE="${WORK_STORAGE:-container-layer}"
while (( $# )); do
    case "$1" in
        --block-profiling)
            BLOCK_PROFILING="${2:?--block-profiling requires ON or OFF}"
            shift 2
            ;;
        --disk-io-workers)
            DISK_IO_WORKERS="${2:?--disk-io-workers requires a count}"
            shift 2
            ;;
        --encryption)
            PEER_ENCRYPTION="${2:?--encryption requires preferred, allowed, or required}"
            shift 2
            ;;
        --transport)
            PEER_TRANSPORT="${2:?--transport requires tcp or utp}"
            shift 2
            ;;
        --storage)
            WORK_STORAGE="${2:?--storage requires container-layer, bind, or tmpfs}"
            shift 2
            ;;
        --help|-h)
            echo "Usage: bash tools/profiling/run.sh [--block-profiling ON|OFF] [--disk-io-workers 0..64] [--encryption preferred|allowed|required] [--transport tcp|utp] [--storage container-layer|bind|tmpfs]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done
if [[ "$BLOCK_PROFILING" != ON && "$BLOCK_PROFILING" != OFF ]]; then
    echo 'Block profiling must be ON or OFF' >&2
    exit 1
fi
if [[ ! "$DISK_IO_WORKERS" =~ ^([0-9]|[1-5][0-9]|6[0-4])$ ]]; then
    echo 'Disk IO workers must be between 0 and 64' >&2
    exit 1
fi
case "$PEER_ENCRYPTION" in
    preferred|allowed|required) ;;
    *) echo 'Encryption must be preferred, allowed, or required' >&2; exit 1 ;;
esac
case "$PEER_TRANSPORT" in
    tcp|utp) ;;
    *) echo 'Transport must be tcp or utp' >&2; exit 1 ;;
esac
export BLOCK_PROFILING DISK_IO_WORKERS PEER_ENCRYPTION PEER_TRANSPORT
echo "Benchmark settings: BLOCK_PROFILING=$BLOCK_PROFILING DISK_IO_WORKERS=$DISK_IO_WORKERS PEER_ENCRYPTION=$PEER_ENCRYPTION PEER_TRANSPORT=$PEER_TRANSPORT"

cd "$(dirname "$0")/../.."
compose_args=(-f tools/profiling/compose.yaml)
case "$WORK_STORAGE" in
    container-layer) ;;
    bind)
        WORK_DIRECTORY="${WORK_DIRECTORY:-$PWD/tools/profiling/work}"
        mkdir -p "$WORK_DIRECTORY"
        WORK_DIRECTORY="$(realpath "$WORK_DIRECTORY")"
        export WORK_DIRECTORY
        compose_args+=(-f tools/profiling/compose.bind.yaml)
        ;;
    tmpfs) compose_args+=(-f tools/profiling/compose.tmpfs.yaml) ;;
    *) echo 'Storage must be container-layer, bind, or tmpfs' >&2; exit 1 ;;
esac
echo "Benchmark storage: $WORK_STORAGE"
if git submodule status --recursive | grep '^-' > /dev/null; then
    echo 'Initialize dependencies first: git submodule update --init --recursive' >&2
    exit 1
fi
docker info > /dev/null
mkdir -p tools/profiling/results
PROFILE_UID="$(id -u)"
PROFILE_GID="$(id -g)"
SOURCE_REVISION="$(git rev-parse HEAD)"
export PROFILE_UID PROFILE_GID SOURCE_REVISION
docker compose "${compose_args[@]}" build
docker compose "${compose_args[@]}" run --rm benchmark
