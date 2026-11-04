# Agent environment.
#
# Contents: the current C toolchain the migration targets, the interpreters the
# repository's own tooling already needs, and ordinary developer utilities.
# The repository itself is the pinned upstream tree, byte-for-byte, with
# nothing added to it and nothing removed from its tests.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        binutils \
        build-essential \
        ca-certificates \
        coreutils \
        curl \
        diffutils \
        file \
        gawk \
        gdb \
        git \
        iproute2 \
        less \
        libc6-dev \
        make \
        netcat-openbsd \
        pkg-config \
        procps \
        python3 \
        ripgrep \
        strace \
        tcl \
        tcpdump \
        util-linux \
        vim-tiny \
    && rm -rf /var/lib/apt/lists/*

# Agent harnesses need node and npm on PATH; without them Harbor's setup runs
# `apt-get install nodejs npm` (no --no-install-recommends) into this image.
# curl, bash and pgrep come from the apt block, node and npm from this block.
# Keep this block (or anything else that provides `node` and `npm`) unless the
# task deliberately wants Debian's node packages.
ARG NODE_VERSION=24.22.0
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) node_arch=x64 ;; \
      arm64) node_arch=arm64 ;; \
      *) echo "unsupported architecture: $arch" >&2; exit 1 ;; \
    esac; \
    tarball="node-v${NODE_VERSION}-linux-${node_arch}.tar.xz"; \
    curl -fsSLo "/tmp/${tarball}" "https://nodejs.org/dist/v${NODE_VERSION}/${tarball}"; \
    curl -fsSLo /tmp/SHASUMS256.txt "https://nodejs.org/dist/v${NODE_VERSION}/SHASUMS256.txt"; \
    (cd /tmp && grep " ${tarball}\$" SHASUMS256.txt | sha256sum -c -); \
    tar -xJf "/tmp/${tarball}" -C /usr/local --strip-components=1 --no-same-owner; \
    rm -f "/tmp/${tarball}" /tmp/SHASUMS256.txt; \
    test ! -e /usr/share/nodejs; \
    node --version; npm --version

WORKDIR /app

# hedis @ 270eb6512b8602cefc7883346755ffbb6fa3bc93 (2016-07-27), unmodified.
COPY hedis/ /app/

# Advisory countdown for the agent; Harbor's steps.agent.timeout_sec is the
# limit that actually applies. Keep the value in timer.sh synchronized with
# every [steps.agent] timeout_sec in task.toml.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
