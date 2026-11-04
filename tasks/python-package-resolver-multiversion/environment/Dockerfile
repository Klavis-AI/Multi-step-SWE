# Agent environment. The candidate works in /app on a Rust workspace and is
# graded by a *separate* verifier image, so nothing here is trusted at grade
# time: the verifier installs its own toolchain and its own crate registry.
#
# The toolchain here is the newest one any step in the chain declares, so the
# same environment builds the workspace at every step. The verifier pins the
# per-step toolchain instead, which is the one that matters for grading.
FROM rust:1.98.1-bookworm

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       bash ca-certificates cmake curl git jq less make pkg-config procps \
       python3 python3-venv ripgrep vim xz-utils \
    && rm -rf /var/lib/apt/lists/*

# The workspace the candidate inherits ships `channel = "1.75"` in
# /app/rust-toolchain.toml, and rustup resolves that file before every `cargo`
# invocation. With the agent phase on an allowlist there is no way to download
# it, so a missing 1.75 does not fail the workspace's build -- it fails the
# first `cargo build` the candidate runs, with a rustup DNS error that says
# nothing about the task. 1.98.1 (the base image's toolchain, still the default)
# stays available for a candidate that overrides the channel.
RUN rustup toolchain install 1.75 --profile minimal \
    && rustup toolchain list

# --- the crate registry -------------------------------------------------- #
# Nothing is reachable at agent time, so every crate the candidate's
# `cargo build` may ask for has to be here already. `assets/prefetch/NN/<tag>/`
# holds one stand-in crate per lockfile that step's checkpoint prewarms (the
# releases the step spans plus the starter tree the workspace was seeded from):
# The lockfile's external packages, exact-pinned, with a stub src/, and no
# source of the workspace's own crates -- the only path members are the empty
# `stubs/<name>/` a stand-in needs where the workspace's own `[patch.crates-io]`
# replaces a registry crate with one of its `crates/` copies. `cargo fetch` in
# each directory downloads that lockfile's set into the one shared CARGO_HOME;
# the union accumulates. The tree is deleted afterwards: only the registry
# cache ships, and no manifest of a later release is left behind.
#
# One stand-in per lockfile rather than one per step, because a union of
# lockfiles is not itself a resolution cargo will accept: two of them can name
# two versions of one crate that cargo's semver rules require it to unify
# (`anstream 0.6.11` in the starter tree, `0.6.13` one release later). See
# environment/assets/prefetch/generate.py.
#
# It runs once per toolchain a stand-in's own lockfile can be built with: the
# image's default, always, plus the channel the release declares when that
# channel is installed -- see assets/prefetch/fetch.sh for why both are needed.
#
# `cargo fetch` is deliberately not `--locked`: the stand-in's own lock is a
# version hint for cargo, and the exact `=version` pins -- not the lock -- are
# what force the versions the workspace's lockfiles name.
COPY assets/prefetch /opt/prefetch
RUN bash /opt/prefetch/fetch.sh /opt/prefetch \
    && rm -rf /opt/prefetch

# The candidate may build as a user other than root, and cargo still has to
# write its package lock and unpack sources into CARGO_HOME. Read for everyone,
# write for everyone on the directories (never on the files).
RUN chmod -R a+rX /usr/local/cargo \
    && find /usr/local/cargo -type d -exec chmod a+w {} +

WORKDIR /app

# Keep the script and every step's timeout_sec synchronized. The script is an
# agent-visible countdown; the configured timeout is authoritative.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
