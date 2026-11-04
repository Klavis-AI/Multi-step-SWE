FROM debian:bookworm-slim

# Toolchain for a C11 project plus the ordinary things an engineer reaches for.
# No CSS, HTML, HTTP or layout library is installed: the milestone is the engine.
# g++ is not installed: the verifier image carries a C toolchain only, so a C++
# tree built here could not be rebuilt by the grader.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates curl git procps ripgrep xz-utils \
        gcc make gdb valgrind \
        libc6-dev binutils file xxd \
        python3 less vim-tiny \
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

# Advisory display only; Harbor's steps.agent.timeout_sec is the real limit.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# No practice corpus in this image. This image is the agent's container for
# EVERY step, so anything in it is visible from step 1 on and would disclose the
# later milestones' strata. Each step ships its own slice -- the milestones up
# to and including that step -- through its workdir, at /app/css-corpus/.
# corpus/ here is the full corpus the slices are cut from; .dockerignore keeps
# it out of the build context so it cannot be copied in by accident.

# Documentation for the agent, which works with no network: the specs and
# manuals the instructions cite, stored in environment/docs/. They were
# downloaded once from their official sources and checked against pinned
# sha256 sums; docs-provenance.txt records the URLs, sums and conversions.
# /opt/docs is outside /app because /app is the workspace collected for
# grading.
COPY docs/ /opt/docs/
