#!/usr/bin/env bash
# Prove that the image built from environment/Dockerfile can build the task's
# dependency graph with the network removed -- the check the offline round
# requires, and the one that would have caught the failure mode it exists to
# prevent (an agent whose first `cargo build` dies on a crate that is not in
# the image, scored as a model failure).
#
# Six phases, all in containers started with `--network none`:
#
#   1. every stand-in in environment/assets/prefetch: `cargo fetch --locked
#      --offline`. Proves the registry cache (and the git checkout cache) holds
#      every crate of every step's dependency set, and that each frozen lock
#      resolves without a single network call.
#   2. `cargo build --locked --offline` of step 01's milestone workspace (the
#      prewarm tree the verifier itself builds), so the graph is not merely
#      present but compilable offline.
#   3. the same for step 15 -- the widest graph, and the only one that reaches
#      `boring-sys` (which compiles its bundled BoringSSL from source) and the
#      git-pinned `wirefilter-engine` that `qlog-dancer` depends on.
#   4. the single pin the rollout review names as the step-14/15 killer,
#      `url = "1.7.2"` carried forward from step 06, resolved offline.
#   5. an honesty check on the cache itself: the crates this task asks the
#      agent to write, and every other QUIC implementation, must be absent.
#   6. the version bump step 09 needs (`boring = "3"` over the shipped 2.1.0),
#      resolved offline to the 3.1.0 the verifier built against.
#
# The step trees are mounted read-only and copied inside the container, because
# a build writes to its target directory.
#
# Usage: environment/bin/offline-proof.sh [IMAGE]      (default: the round's tag)
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
pkg=$(cd "$here/../.." && pwd)
image=${1:-${IMAGE:-quic-http3-env:offline-round}}
prefetch="$pkg/environment/assets/prefetch"

fetch_all() {
    echo "### phase 1: cargo fetch --offline --locked in every stand-in"
    docker run --rm --network none \
        -e CARGO_TERM_COLOR=never \
        -v "$prefetch":/prefetch:ro \
        "$image" bash -euc '
            for d in /prefetch/*/; do
                printf "%s  " "$(basename "$d")"
                ( cd "$d" && cargo fetch --locked --offline )
                echo ok
            done
            echo "phase 1: every stand-in fetched with the network removed"'
}

build_step() {
    local number=$1 pin=$2 label=$3
    local tree env_args=()
    tree=$(ls -d "$pkg"/steps/"$number"-*/tests/assets/prewarm/candidate)
    if [ -n "$pin" ]; then
        env_args+=(-e QUICHE_BSSL_PATH="$pin")
        echo "# QUICHE_BSSL_PATH=$pin (the variable the step's .cargo/config.toml sets)"
    else
        echo "# QUICHE_BSSL_PATH unset, as this step's verifier leaves it: the release"
        echo "# takes its BoringSSL from boring-sys, which compiles its bundled copy."
    fi
    echo
    echo "### phase $label: cargo build --offline --locked, step $number"
    docker run --rm --network none \
        -e CARGO_TERM_COLOR=never \
        -e CARGO_TARGET_DIR=/tmp/target \
        "${env_args[@]}" \
        -v "$tree":/tree:ro \
        "$image" bash -euc '
            cp -r /tree /tmp/ws && cd /tmp/ws
            cargo build --locked --offline
            echo "phase '"$label"': step '"$number"' built offline"'
}

pin_check() {
    echo
    echo "### phase 4: the pin the rollout's steps 14-15 died on, offline"
    echo "# 'url = \"1.7.2\"', written at step 06 out of that step's shipped lock"
    echo "# and carried forward; the review found its tree could not build because"
    echo "# idna 0.1.5 -> unicode-bidi was absent from the offline index."
    docker run --rm --network none \
        -e CARGO_TERM_COLOR=never \
        "$image" bash -euc '
            mkdir -p /tmp/pin/src && cd /tmp/pin
            printf "%s\n" \
                "[package]" "name = \"pin-check\"" "version = \"0.0.0\"" \
                "edition = \"2021\"" "" "[workspace]" "" "[dependencies]" \
                "url = \"=1.7.2\"" > Cargo.toml
            echo "// scratch" > src/lib.rs
            cargo fetch --offline
            cargo tree --offline --depth 2
            echo "phase 4: url 1.7.2 resolves with the network removed"'
}

honesty_check() {
    echo
    echo "### phase 5: the cache holds dependencies, not the answer"
    docker run --rm --network none \
        -e CARGO_TERM_COLOR=never \
        "$image" bash -euc '
            cd "$CARGO_HOME"/registry/cache/*/
            bad=$(ls | grep -E "^(quiche|qlog|octets|h3i|tokio-quiche|buffer-pool|datagram-socket|task-killswitch|netlog|qlog-dancer|quinn|quinn-proto|neqo|s2n-quic|h3)[-_0-9]" || true)
            if [ -n "$bad" ]; then
                echo "FOUND a crate the task asks the agent to write:"
                echo "$bad"
                exit 1
            fi
            echo "none of the milestone crates and no QUIC stack is cached"
            echo "registry cache: $(ls | wc -l) .crate files"
            echo "git cache: $(ls "$CARGO_HOME"/git/db/ 2>/dev/null | tr "\n" " ")"'
}

bump_check() {
    echo
    echo "### phase 6: the version bump steps 07/09/10 require, offline"
    echo "# step 09's milestone manifest asks for 'boring = \"3\"', replacing the"
    echo "# shipped lock's 2.1.0; the verifier's target graph resolved it to 3.1.0."
    echo "# With no network, cargo must reach that same version out of the cache."
    docker run --rm --network none \
        -e CARGO_TERM_COLOR=never \
        "$image" bash -euc '
            mkdir -p /tmp/bump/src && cd /tmp/bump
            printf "%s\n" \
                "[package]" "name = \"bump-check\"" "version = \"0.0.0\"" \
                "edition = \"2021\"" "" "[workspace]" "" "[dependencies]" \
                "boring = \"3\"" > Cargo.toml
            echo "// scratch" > src/lib.rs
            cargo fetch --offline
            got=$(cargo tree --offline --depth 1 2>/dev/null | grep -oE "boring v[0-9.]+" | head -1)
            echo "   resolved offline: $got"
            case "$got" in "boring v3.1.0") ;; *)
                echo "phase 6 FAILED: expected boring v3.1.0"; exit 1;; esac
            echo "phase 6: boring 3.1.0 reached with the network removed"'
}

echo "==> image: $image"
docker image inspect "$image" >/dev/null
echo "==> prepared: $(date -u +%FT%TZ)"
fetch_all
build_step 01 f18bd55240b229a65df48e7905da98fff18cbf59 2
build_step 15 "" 3
pin_check
honesty_check
bump_check

echo
echo "OFFLINE-PROOF-OK"
