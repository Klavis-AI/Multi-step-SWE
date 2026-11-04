# Agent environment for the EVM task line.
#
# A C toolchain and the cryptographic libraries Debian ships, and nothing else:
# no Go, no Rust, no JVM, and no Ethereum client, library or fixture corpus.
# The grading container is built from the same package list, without Node.
# Node and npm are here only for the agent harness (block below); the agent
# phase has no network, so there is no registry to pull a JS Ethereum
# implementation from.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates coreutils curl diffutils file findutils gawk git \
       gdb grep less make patch procps python3 ripgrep sed tar unzip xxd \
       xz-utils \
       build-essential libc6-dev pkg-config \
       libssl-dev libsecp256k1-dev libgmp-dev \
       time valgrind \
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

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# Upstream interface specification, upstream tool fixtures and the local runner.
COPY app/ /app/

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
