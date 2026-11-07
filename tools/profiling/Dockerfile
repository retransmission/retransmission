# syntax=docker/dockerfile:1
FROM debian:trixie-slim

RUN rm -f /etc/apt/apt.conf.d/docker-clean
RUN --mount=type=cache,id=retransmission-profile-apt-cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,id=retransmission-profile-apt-lists,target=/var/lib/apt/lists,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates ccache cmake fio ninja-build pkg-config python3 util-linux \
    libarchive-dev libcurl4-openssl-dev libevent-dev libminiupnpc-dev \
    libnatpmp-dev libpsl-dev libssl-dev zlib1g-dev

COPY . /src
WORKDIR /src
RUN test -f third-party/fmt/CMakeLists.txt || \
    (echo 'Initialize dependencies before building: git submodule update --init --recursive' >&2; exit 1)
ARG BLOCK_PROFILING=ON
ARG BUILD_JOBS=2
ARG SOURCE_REVISION=0
ARG TARGETARCH
ENV TR_PROFILE_BUILD_ENABLED=${BLOCK_PROFILING}
ENV CCACHE_DIR=/root/.cache/ccache
# Every image compiles in a fresh tree, so no object can outlive its source.
# ccache keeps that cheap.
RUN --mount=type=cache,id=retransmission-profile-ccache-${TARGETARCH},target=/root/.cache/ccache,sharing=locked \
    cmake -S . -B /build-tree -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DTR_VCS_REVISION=${SOURCE_REVISION} \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_FLAGS=-fno-omit-frame-pointer \
    -DENABLE_IPO=OFF -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
    -DENABLE_TESTS=OFF -DENABLE_NLS=OFF -DINSTALL_DOC=OFF \
    -DENABLE_BLOCK_PROFILING=${BLOCK_PROFILING} \
    && cmake --build /build-tree --parallel ${BUILD_JOBS} \
        --target transmission-daemon transmission-create \
    && mkdir -p /build/daemon /build/utils \
    && cp /build-tree/daemon/transmission-daemon /build/daemon/ \
    && cp /build-tree/utils/transmission-create /build/utils/ \
    && rm -rf /build-tree \
    && ccache --show-stats

ENTRYPOINT ["python3", "/src/tools/profiling/benchmark.py"]
