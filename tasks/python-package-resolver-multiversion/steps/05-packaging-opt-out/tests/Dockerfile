# Verifier image for step 05, built with steps/05-packaging-opt-out/tests/ as the Docker
# build context: it cannot reach the task root or another step, so every
# shared helper is copied into each step. The usual tests upload is skipped
# here and /tests/test.sh runs from this image.
#
# Base image = the Rust channel `rust-toolchain.toml` declares at tag 0.4.20.
# The candidate's tree may be left on the starter tree's lockfile or carried to
# this release's, and this image builds either -- not because the toolchains
# are monotone (they are not: the starter tree's `time 0.3.34` compiles under
# 1.78 and stops compiling at 1.80.1), but because the starter tree's pinned
# dependency set is kept buildable on every channel the ladder uses. Do not
# re-derive a build decision from "a newer toolchain builds everything an older
# one did"; re-check the version instead. RUSTUP_TOOLCHAIN is read from
# /etc/rust-toolchain-id at grade time rather than hard-coded, because `rustup`
# would otherwise try to *download* the channel named in the candidate's
# rust-toolchain.toml and the verifier has no network.
FROM rust:1.81-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake coreutils diffutils gawk make pkg-config \
       procps python3 util-linux xz-utils curl \
    && rm -rf /var/lib/apt/lists/*

# The one toolchain this image has. Every later `cargo` invocation sets
# RUSTUP_TOOLCHAIN to this, which makes rust-toolchain.toml inert.
RUN rustup toolchain list | head -1 | cut -d' ' -f1 > /etc/rust-toolchain-id \
    && cat /etc/rust-toolchain-id

# The system interpreter is part of the contract, not an accident of the base
# image: probes in several steps discover it through a `/usr/bin` PATH entry and
# expect `.venv/lib/python3.11`. Debian bookworm ships 3.11 as `python3`, but the
# probes must fail loudly if that ever changes rather than silently mis-grade.
#
# The link is created only when the name is absent. `rust:1.75-bookworm` has
# shipped `python3 -> python3.11` since its April 2023 build, so in practice
# there is nothing to do; `ln -sf` onto an existing *symlink to the same path*
# exits non-zero ("are the same file"), which failed the image build outright
# and took the whole ladder with it. The version assertion above is the thing
# that must never be skipped, and it still runs unconditionally -- only the
# idempotent no-op link is conditional.
RUN test "$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')" = "3.11" \
    && { [ -e /usr/bin/python3.11 ] || ln -sf "$(command -v python3)" /usr/bin/python3.11; }

# --- the interpreters ---------------------------------------------------- #
# The probe corpus runs `uv` against fixed CPython builds. They are pinned by
# sha256 and unpacked here, so grading reads no unchecksummed byte and no
# interpreter the candidate could have touched. `pythons-payload/` carries the
# handful of files uv rewrites on install, with the prefix parameterised; see
# fetch-pythons.sh.
#
# This block is byte-identical in every step and sits above the per-step
# `COPY skeleton`, so steps that share a Rust channel share the ~486 MB layer
# instead of each building their own.
COPY pythons.sha256 fetch-pythons.sh /opt/
COPY pythons-payload /opt/pythons-payload
RUN set -eu; sh /opt/fetch-pythons.sh /opt/pythons /opt/pythons.sha256 /opt/pythons-payload \
    && rm -f /opt/fetch-pythons.sh \
    && rm -rf /opt/pythons-payload

# --- the crate registry -------------------------------------------------- #
# `cargo fetch --locked` over a *manifest skeleton* of the tags this step
# spans and of the starter tree the workspace was seeded from: every Cargo.toml
# plus Cargo.lock, with empty source stubs. ~230 KB of build context each, and
# it populates CARGO_HOME with exactly the checksummed dependency set the
# candidate's build will ask for, whichever of those trees its own is left at.
ENV CARGO_HOME=/opt/cargo
COPY skeleton /opt/skeleton
RUN set -eu; export RUSTUP_TOOLCHAIN="$(cat /etc/rust-toolchain-id)"; \
    for d in /opt/skeleton/*/; do \
      echo "fetching $d"; \
      cargo fetch --locked --manifest-path "$d/Cargo.toml"; \
    done; \
    rm -rf /opt/skeleton; \
    chown -R 65534:65534 /opt/cargo

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

# Step identity, for the `require_step` check at the top of test.sh.
#
# This does NOT prevent the wrong tests/ tree being baked in -- nothing inside
# a Dockerfile can. The builder keys its content checksum for a local build
# context on (path, size, mtime) and never reads the bytes, so two steps whose
# tests/ trees are staged at the same path with the same sizes and the same
# mtime resolve to the same copy, however different their contents. Only a
# distinct mtime (or size) per step prevents it, which is what
# bin/stamp-steps.py is for.
#
# What this line buys is *detection*. /etc/harbor-step-id is written before the
# copy below, so it names the image; test.sh's `require_step` names the tree.
# When the two disagree the verifier says so, instead of reporting a result for
# the wrong tree.
#
# Keep this number, the directory's number and test.sh's `require_step` equal.
# Written as a LITERAL, not as an ARG: the verifier build declares no `args:`,
# so no --build-arg can reach this build.
RUN echo "05" > /etc/harbor-step-id

COPY . /tests
RUN chmod +x /tests/test.sh

WORKDIR /app
