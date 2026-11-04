# Agent image.
#
# Harbor runs every step of a multi-step trial in ONE long-lived agent container
# (harbor/trial/multi_step.py: the environment is stopped only after the last
# step), and StepConfig has no per-step agent environment. So the ladder's eight
# toolchains cannot be eight images, and they cannot all be baked in either:
# the retired ones must be gone, and nothing from a future step may be visible.
#
# What this image ships is therefore exactly step 1's toolchain — the baseline
# stack in stack.txt — installed as the venv /opt/venv, which is first on PATH.
# Every later step's steps/<name>/workdir/setup.sh
# deletes /opt/venv, rebuilds it for that step from PyPI, wipes the pip cache
# and deletes itself, so at any moment the container holds one stack: not the
# retired one, not a future one.
#
# The interpreter is part of that one stack, not part of the platform.
# Bullseye's own archive cannot produce the newer CPythons, but baking them in
# here would put /opt/py39 and /opt/py312 in front of a step-1 agent, which is
# the ladder's shape and therefore leakage. The step that needs a newer
# interpreter fetches it in its own workdir/setup.sh, from a pinned
# python-build-standalone release verified by sha256 — the same
# PyPI-and-network boundary every step's stack rebuild already stands on. So
# this image contains Python 3.7 and nothing else that could be mistaken for a
# rung.

# A pinned ruff, first, so the COPY below can take the binary out of it.
FROM ghcr.io/astral-sh/ruff:0.14.0 AS ruffbin

# python:3.7-slim-bullseye is the last published bullseye image whose installed
# libc6/perl-base still match what archive.debian.org serves for `bullseye
# main`, so it is the only base on which apt can still resolve a complete
# gcc/gfortran toolchain for this suite. Python 3.7 is also the newest
# interpreter with wheels for scikit-learn 0.20.4 + pandas 0.23.4 + NumPy
# 1.14.6, which is the stack the task starts on.
FROM python:3.7-slim-bullseye

RUN printf 'deb http://archive.debian.org/debian bullseye main\n' > /etc/apt/sources.list \
 && apt-get -o Acquire::Check-Valid-Until=false update -qq \
 && apt-get install -y --no-install-recommends \
      bash ca-certificates coreutils curl diffutils file gawk git less make patch \
      procps ripgrep util-linux vim-tiny xz-utils \
      gcc g++ gfortran libc6-dev pkg-config \
      libatlas-base-dev libopenblas-dev liblapack-dev \
 && rm -rf /var/lib/apt/lists/*

# The active stack, whichever step is running. Docker ENV is inherited by every
# exec into the container, which covers `bash -c` and every non-login shell.
ENV PATH=/opt/venv/bin:$PATH

# It does not cover a *login* shell: Debian's /etc/profile assigns PATH
# outright for uid 0, so without the line below `bash -lc 'python -V'` resolves
# /usr/local/bin/python -- the bare 3.7 with no NumPy, pandas or scikit-learn --
# and `python -m pip install .`, the command the step 1 instruction names, would
# install into it. /etc/profile sources profile.d *after* that
# assignment, so this restores the venv for login shells too. Path-based, so it
# survives each later step's rm -rf /opt/venv and rebuild at the same path.
RUN printf '%s\n' 'PATH=/opt/venv/bin:$PATH' 'export PATH' \
      > /etc/profile.d/00-venv.sh \
 && chmod 0644 /etc/profile.d/00-venv.sh

# Headless container: no display, no Tk. Set for every interpreter in the image
# so Matplotlib never selects an interactive backend at import time.
ENV MPLBACKEND=Agg

COPY stack.txt /tmp/stack.txt
COPY verify-stack.py /tmp/verify-stack.py
RUN set -eux; \
    /usr/local/bin/python3.7 -m venv /opt/venv; \
    /opt/venv/bin/python -m pip install -q --no-cache-dir --upgrade 'pip<25'; \
    /opt/venv/bin/python -m pip install -q --no-cache-dir -r /tmp/stack.txt; \
    find /opt/venv -name '__pycache__' -prune -exec rm -rf {} +; \
    /opt/venv/bin/python /tmp/verify-stack.py /tmp/stack.txt; \
    rm -f /tmp/stack.txt /tmp/verify-stack.py

# ruff, as the same pinned static binary every step's verifier copies in.
#
# Two rows of every step's manifest are ruff findings, so ruff is part of the
# instrument the work is measured with, and an agent that cannot run the
# instrument is being asked to hit a number it cannot read. `pip install ruff`
# would give it *a* ruff -- whichever one PyPI serves that day -- and a
# different release fixes a different set and renames rules between minor
# versions, so a tree that is clean under the agent's ruff can still carry
# findings under the scorer's. One pin, in both images.
#
# Outside /opt/venv because each later step's workdir/setup.sh does
# `rm -rf /opt/venv` and rebuilds it, and the linter is not part of any step's
# stack. It is also not a rung -- ruff 0.14.0 says nothing about which pandas
# comes next -- so it is not future-step leakage.
#
# Placed after the venv build so that adding it does not invalidate the cached
# layer that installs the scientific stack.
COPY --from=ruffbin /ruff /usr/local/bin/ruff
RUN ruff --version

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

# The repository the agent works on. Source only: no compiled extension, no
# egg-info, no build directory — every step builds it with its own toolchain.
COPY baseline /app

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; Harbor enforces the actual timeout.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
