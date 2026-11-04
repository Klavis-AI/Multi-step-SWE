# Agent environment for protobuf-grpc-edition-chain.
#
# A C toolchain and nothing else that could do the work for the candidate:
# no protoc, no libprotobuf, no gRPC, no HTTP/2 library, no Go, no Rust, no Python
# (nothing in the task needs an interpreter; the agent phase has no network).
# zlib is present because gRPC's `gzip` message encoding is a published part of
# the protocol and DEFLATE is not what this task is about.
#
# Ubuntu 24.04 is the same base the verifier uses, so "it builds
# here" and "it builds when graded" mean the same thing.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash build-essential gcc g++ make libc6-dev zlib1g-dev \
       ca-certificates curl git procps ripgrep \
       binutils file xxd gdb valgrind less vim-tiny \
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

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
