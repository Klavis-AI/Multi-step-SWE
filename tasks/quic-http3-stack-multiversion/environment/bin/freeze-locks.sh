#!/usr/bin/env bash
# Finalize environment/assets/prefetch/*/Cargo.lock -- authoring-time tool.
#
# environment/bin/gen-prefetch.py writes each stand-in's Cargo.lock by hand,
# offline: it is the step's frozen lock with its workspace members replaced by
# the stand-in package. That seed is complete for everything the release
# declares, but it is not yet what cargo itself resolves. A stand-in names
# every crate as a direct dependency with default features, so cargo's feature
# unification pulls in optional dependencies the original graph reached with
# different features (clap's ansi_term/atty/strsim, plotters' image backends).
# `cargo fetch --locked` refuses the seed for that reason, and rightly so.
#
# This script runs `cargo fetch` once in each directory, which resolves the
# seed and rewrites the lock in place, and afterwards `cargo fetch --locked`
# succeeds -- which is what environment/Dockerfile runs. Run it with network
# at authoring time and check the rewritten locks in; the image build is then
# reproducible and a lock that drifts is a loud failure rather than a silent
# re-resolution.
#
# Usage: environment/bin/freeze-locks.sh [CARGO_HOME_CACHE_DIR]
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
prefetch=$(cd "$here/../assets/prefetch" && pwd)
cache=${1:-/tmp/quic-prefetch-cargo-home}
image=${PREFETCH_IMAGE:-rust:1.88-bookworm}

mkdir -p "$cache"
echo "==> resolving $(ls -d "$prefetch"/*/ | wc -l) stand-ins with $image"
docker run --rm \
    -v "$prefetch":/prefetch \
    -v "$cache":/cargo \
    -e CARGO_HOME=/cargo \
    -e CARGO_TERM_COLOR=never \
    "$image" \
    bash -euc 'for d in /prefetch/*/; do echo "--- $d"; ( cd "$d" && cargo fetch --quiet ); done'
echo "==> checking the frozen locks"
docker run --rm \
    -v "$prefetch":/prefetch:ro \
    -v "$cache":/cargo \
    -e CARGO_HOME=/cargo \
    -e CARGO_TERM_COLOR=never \
    "$image" \
    bash -euc 'for d in /prefetch/*/; do echo "--- $d"; ( cd "$d" && cargo fetch --locked --quiet ); done'
echo "==> done: every Cargo.lock now satisfies cargo fetch --locked"
