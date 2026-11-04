# Agent environment. One image serves all seven steps, because a Harbor
# multi-step task reuses its environment; each step's instruction names the
# interpreter that step targets.
#
# Python 2.7 is deliberately absent (rule 6: retired toolchains are absent from
# the agent's image and the verifier's). The tree that lands in /app is written
# for it, and there is no way to run it as-is here -- that is the task.
FROM debian:bookworm-slim

# No python from apt: the interpreters come from uv below, and a stray
# /usr/bin/python3 would be a version nothing in the task ever names.
# build-essential is for the agent harnesses only: mini-swe-agent's setup
# apt-installs it, with recommends, when gcc or make is missing. Without
# recommends it brings no python; the last command below keeps it that way.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash build-essential ca-certificates coreutils curl git less openssl \
        procps ripgrep vim-tiny xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && ! command -v python3

# Pinned on purpose. `uv:latest` decides which CPython patch releases exist,
# and the steps name exact ones; a floating tag would let the set of
# installable interpreters drift out from under the task.
COPY --from=ghcr.io/astral-sh/uv:0.12.15 /uv /usr/local/bin/uv

# The whole ladder, installed once. Each `uv python install` drops a versioned
# executable into UV_PYTHON_BIN_DIR, so `python3.10` ... `python3.14` and the
# free-threaded `python3.13t` are all on PATH by their own names and none of
# them is `python3`: a step that says "3.12" is run as python3.12, not as
# whatever `python3` happens to mean.
ENV UV_PYTHON_INSTALL_DIR=/opt/python \
    UV_PYTHON_BIN_DIR=/usr/local/bin \
    UV_NO_CACHE=1
RUN uv python install --no-progress \
        3.10.21 3.11.16 3.12.14 3.13.15 3.13.15+freethreaded 3.14.7 \
    && for v in 3.10 3.11 3.12 3.13 3.13t 3.14; do \
           "python$v" -c "import sys; print(sys.version)" || exit 1; \
       done \
    && ! command -v python2 \
    && ! command -v python2.7

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

# Agent-visible countdown. Harbor enforces the real timeout; keep the constant
# in this script and every [steps.agent].timeout_sec in task.toml in step.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh

# The codebase, exactly as the pinned commit left it. Nothing of the author's
# goes in here -- no recording, expectation, runner, gate or solution (rule 6).
COPY repo/ /app/
