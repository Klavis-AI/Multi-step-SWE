# Agent environment. One image for every step: task.toml reuses the environment
# across the fourteen milestones, and the cumulative /app artifact is what
# carries work forward.
#
# The pinned toolchain is the Rust stable release contemporary with the final
# milestone's upstream release (v1.0.0-beta.6, 2022-03-28). No single toolchain
# compiles the whole ladder, so the era compiler the first two steps need is
# installed beside it -- see the toolchain layer below. The verifier pins its
# own either way.
FROM rust:1.59-bullseye

# librocksdb-sys and zcash_script run bindgen and a C++ build in their build
# scripts, so clang, libclang and llvm-config must be present before any
# `cargo build` of zebra-state or zebra-script can succeed.
#
# bullseye is out of support and half-withdrawn: deb.debian.org still serves the
# suite and security.debian.org still serves the bullseye-security *index*, but
# the pool behind it is gone -- so `apt-get update` succeeds and the install then
# 404s on every package that has a security version, which is most of them. So
# try the image's own sources first and fall back to archive.debian.org, exactly
# as the verifier images do (they carry the same ladder for buster as well).
# The security suite is appended only if it is
# really there -- archive.debian.org has no bullseye-security at all, and
# bullseye's final point release folded those fixes into main.
RUN set -eux; \
    PKGS="bash build-essential ca-certificates clang cmake curl git jq less \
          libclang-dev llvm-dev libssl-dev pkg-config procps python3 ripgrep \
          zlib1g-dev"; \
    . /etc/os-release; \
    from_archive() { \
        printf 'deb http://archive.debian.org/debian %s main\n' "$VERSION_CODENAME" \
            > /etc/apt/sources.list; \
        for suite in "$VERSION_CODENAME/updates" "$VERSION_CODENAME-security"; do \
            if curl -sfI "http://archive.debian.org/debian-security/dists/$suite/InRelease" \
                   > /dev/null; then \
                printf 'deb http://archive.debian.org/debian-security %s main\n' "$suite" \
                    >> /etc/apt/sources.list; \
            fi; \
        done; \
        rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources || true; \
        echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid; \
        apt-get update; \
        apt-get install -y --no-install-recommends $PKGS; \
    }; \
    if apt-get update 2>/dev/null && apt-get install -y --no-install-recommends $PKGS; \
    then :; else from_archive; fi; \
    command -v clang; command -v cmake; command -v git; command -v jq; command -v rg; \
    rm -rf /var/lib/apt/lists/*

ENV CARGO_TERM_COLOR=never \
    LIBCLANG_PATH=/usr/lib/llvm-11/lib \
    LLVM_CONFIG_PATH=/usr/bin/llvm-config

# Warm the registry index in its own layer, exactly as every verifier image in
# this task does. The dependency named here is irrelevant; cloning the index is
# the whole point -- on cargo 1.5x that is a full git clone of crates.io-index,
# and doing it in a layer of its own keeps the per-step fetch below cheap when
# a prefetch directory changes. The `|| true` is belt and braces: by the time
# resolution can fail, the index is already on disk, which is all this layer is
# for.
RUN set -eux; \
    mkdir -p /tmp/idx/src; cd /tmp/idx; \
    printf '[package]\nname = "idx"\nversion = "0.0.0"\nedition = "2018"\n[dependencies]\nlibc = "=0.2.93"\n' > Cargo.toml; \
    : > src/lib.rs; \
    cargo fetch || true; \
    test -d "${CARGO_HOME:-/usr/local/cargo}/registry"; \
    cd /; rm -rf /tmp/idx

# Registry and git cache for the whole ladder.
#
# The agent's work runs against an allowlist that reaches nothing but the model
# endpoint, so every crate any step's build can name has to be here already.
# Each assets/prefetch/NN is a dependency-graph stand-in for one step -- that
# step's pinned skeleton lock with the `zebra*` path members replaced by an
# empty stub crate, every third-party package listed as a direct, renamed,
# exactly-pinned dependency, and no implementation of anything (see
# bin/gen-prefetch.py, which generates them from the step's own frozen lock).
# They share one CARGO_HOME, so what lands in the cache is their union: the
# seed's graph, the two crates step 03 adds, step 05's librustzcash trio, step
# 07's incrementalmerkletree, step 12's five, and the git pins each step's
# release moved to -- including the two the instructions name verbatim for the
# candidate to write into a manifest (`halo2` at step 03, `librustzcash`'s
# `zcash_history`/`zcash_primitives` at step 05).
#
# The upstream `zebra*` crates are not in the cache: they are the task itself,
# they are published on crates.io from 1.0.0-beta.25 onward, and the generator
# refuses to cache any of them, so neither the closed network nor this cache
# can supply a copy.
#
# `cargo fetch` and not `cargo fetch --locked`. A stand-in resolves the same
# *packages* as its step's lock -- measured after a full fetch, the cache holds
# the locks' 1071 registry packages exactly, none dropped and none added -- but
# not the same per-package edge lists, because cargo derives those itself from
# the features the stand-in enables. So the lock is rewritten on every fetch, and
# `--locked` (or `--frozen`) refuses to let that happen. Letting it happen is
# harmless: the package set and the checksums are what the cache is for, and
# every verifier image in this task fetches its skeleton the same way.
#
# The era compiler, for the two steps the modern one cannot build.
#
# No single toolchain compiles this ladder. The seed (and step 02) lock
# `lexical-core 0.7.4`, a host build-dependency of librocksdb-sys reached
# through bindgen, and it does not compile on a rustc newer than 1.52 -- 27
# errors, `cannot mod \`usize\` by \`u32\`` and friends, reproduced offline
# against this image; while the last releases in the ladder are edition 2021,
# which needs 1.56 or newer. The two ranges are disjoint. That is the same
# fact that puts steps 01-02 on `rust:1.52-buster` and steps 03-14 on
# `rust:1.59-bullseye` in this task's verifier images.
#
# The container keeps 1.59.0 as its default toolchain, because that is what
# the step documents promise, and it can no longer install anything: the
# network is closed at agent time. So the era compiler those two steps need is
# installed here, at image build time, and is reachable as
# `cargo +1.52.1 ...` / `rustc +1.52.1 ...` for the candidate that goes
# looking. `--profile minimal` keeps rustdoc and the rest out of the image.
RUN rustup toolchain install 1.52.1 --profile minimal --no-self-update \
    && rustup run 1.52.1 rustc --version \
    && rustup run 1.52.1 cargo --version \
    && rustup toolchain list

# Read-only for everyone: the candidate builds with it, never installs into it.
RUN chmod -R a+rX "${RUSTUP_HOME:-/usr/local/rustup}"

# Steps 01 and 02 are then re-fetched with the era cargo, and that second pass
# is not optional. Cargo 1.53 and later keep the registry index as a cache of
# parsed entries with no checkout behind it, and a 1.52 cargo cannot read those
# entries -- it falls back to the raw index file, finds none, and reports "no
# matching package named `abscissa_core` found" for a crate sitting unpacked in
# its own cache. Running one 1.52 fetch rewrites the entries in the form it
# expects, after which *both* toolchains resolve the same cache offline
# (measured: 1.52 and 1.59 each fetch and resolve the seed's graph and the
# step-02 graph with the network removed). Only the era band needs it: the
# later steps cannot be built by 1.52 at all, and their graphs carry edition
# 2021 manifests that cargo 1.52 cannot parse.
COPY assets/prefetch /opt/prefetch
RUN set -eu; \
    for d in /opt/prefetch/*/; do \
        echo "prefetching $d"; \
        ( cd "$d" && cargo fetch ); \
    done; \
    for n in 01 02; do \
        echo "re-warming $n with 1.52.1"; \
        ( cd "/opt/prefetch/$n" && rustup run 1.52.1 cargo fetch ); \
    done; \
    rm -rf /opt/prefetch; \
    du -sh "${CARGO_HOME:-/usr/local/cargo}"/registry "${CARGO_HOME:-/usr/local/cargo}"/git 2>/dev/null || true

# Same widening the verifier images do, and for the same reason: some crates
# ship source with no other-read bit, and an agent that chooses to build as a
# non-root user must not hit a permission error on a dependency it never named.
# Both halves of the cache are widened -- the registry, and `git/`, because
# this ladder's graph is full of `[patch.crates-io]` pins (tower, hyper,
# metrics, halo2, orchard, librustzcash, redjubjub) and a checkout the agent
# cannot extend is the same failure one level down.
RUN chmod -R a+rX "${CARGO_HOME:-/usr/local/cargo}" \
    && find "${CARGO_HOME:-/usr/local/cargo}" -type d -exec chmod a+w {} +

# One line of global cargo config, and it is what makes the workspace's own
# documented first build work. `WORKSPACE_HANDOFF.md` tells the candidate to
# run `cargo build` *without* `--offline` for its first build; with the network
# closed that command does not fall back to the cache, it dies on name
# resolution:
#
#     $ cargo metadata --format-version 1        # no --offline, no network
#     error: failed to resolve address for github.com: Temporary failure in
#     name resolution
#
# `net.offline = true` makes cargo behave the way the environment actually is:
# a plain `cargo build` then resolves from the cache exactly as `--offline`
# does, and a genuinely uncached crate gets an honest "can't make HTTP request
# in the offline mode" instead of a DNS failure. It is written after every
# fetch above, so the image build's own network work is unaffected, and cargo
# 1.36+ reads it, which covers both toolchains here.
RUN set -eu; \
    printf '[net]\noffline = true\n' > "${CARGO_HOME:-/usr/local/cargo}/config.toml"; \
    cat "${CARGO_HOME:-/usr/local/cargo}/config.toml"

WORKDIR /app

# Agent-visible, informational countdown for the per-step timeout configured in
# task.toml. Harbor remains responsible for enforcing the hard limit.
COPY timer.sh /app/timer.sh
RUN chmod +x /app/timer.sh
