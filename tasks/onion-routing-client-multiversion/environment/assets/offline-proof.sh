#!/usr/bin/env bash
# Offline proof for the agent image.  Run INSIDE a container started from the
# built image with the network removed:
#
#   docker run --rm --network none \
#       -v <task>/steps:/steps:ro -v <this script>:/proof.sh:ro \
#       onion-env:prefetch bash /proof.sh
#
# Nothing below can reach any host.  Every command is one the agent is told to
# run, or the state the agent is in after the migration each step describes.
set -u
export CARGO_TERM_COLOR=never
S=/steps
rc=0
step_dir() { ls -d $S/$1-*/ | head -1; }

echo "########## 0. the container really has no network ##########"
timeout 10 curl -sS -o /dev/null https://static.crates.io/ 2>&1 || echo "crates.io: unreachable (exit $?)"

echo
echo "########## 1. the instruction's own command, on the workspace the agent is handed ##########"
rm -rf /tmp/w; cp -a "$(step_dir 01)/workdir" /tmp/w
( cd /tmp/w && timeout 900 cargo build --workspace ) 2>&1 | tail -2
echo "cargo build --workspace exit: ${PIPESTATUS[0]}"

echo
echo "########## 2. the same, --offline ##########"
rm -rf /tmp/w; cp -a "$(step_dir 01)/workdir" /tmp/w
( cd /tmp/w && timeout 900 cargo build --workspace --offline ) 2>&1 | tail -2
echo "cargo build --workspace --offline exit: ${PIPESTATUS[0]}"

echo
echo "########## 3. cargo test / clippy targets resolve: --all-targets --offline ##########"
rm -rf /tmp/w; cp -a "$(step_dir 01)/workdir" /tmp/w
( cd /tmp/w && timeout 900 cargo build --workspace --all-targets --offline ) 2>&1 | tail -2
echo "cargo build --workspace --all-targets --offline exit: ${PIPESTATUS[0]}"

echo
echo "########## 4. every step's release graph resolves offline ##########"
for s in $S/*/; do
    n=$(basename "$s")
    rm -rf /tmp/cp; cp -a "$s/tests/assets/prefetch/checkpoint" /tmp/cp
    if ( cd /tmp/cp && timeout 300 cargo fetch --locked --offline >/tmp/cp.out 2>&1 ); then
        printf '%-46s cargo fetch --locked --offline: ok\n' "$n"
    else
        printf '%-46s FAILED\n' "$n"; tail -4 /tmp/cp.out; rc=1
    fi
done

echo
echo "########## 5. the widest graph: step 12, full build, offline ##########"
rm -rf /tmp/cp; cp -a "$(step_dir 12)/tests/assets/prefetch/checkpoint" /tmp/cp
( cd /tmp/cp && timeout 1800 cargo build --workspace --offline ) 2>&1 | tail -2
echo "step 12 cargo build --workspace --offline exit: ${PIPESTATUS[0]}"

echo
echo "########## 6. the migrations: release-NN manifests carrying the release-(NN-1) lock ##########"
prev=""
for s in $S/*/; do
    n=$(basename "$s"); nn=${n:0:2}
    if [ -n "$prev" ]; then
        rm -rf /tmp/m; cp -a "$s/tests/assets/prefetch/checkpoint" /tmp/m
        cp "$prev/tests/assets/prefetch/checkpoint/Cargo.lock" /tmp/m/Cargo.lock
        if ( cd /tmp/m && timeout 900 cargo build --workspace >/tmp/m.out 2>&1 ); then
            printf 'step %s after the migration, cargo build --workspace: ok\n' "$nn"
        else
            printf 'step %s after the migration: FAILED\n' "$nn"; tail -3 /tmp/m.out; rc=1
        fi
    fi
    prev=$s
done

echo
echo "########## 7. Cargo.lock deleted: cargo must re-resolve from scratch, offline ##########"
rm -rf /tmp/w; cp -a "$(step_dir 01)/workdir" /tmp/w; rm /tmp/w/Cargo.lock
( cd /tmp/w && timeout 900 cargo build --workspace ) 2>&1 | tail -2
echo "cargo build --workspace with no lockfile exit: ${PIPESTATUS[0]}"

echo
echo "########## 8. the same resolution as a non-root user ##########"
rm -rf /tmp/nr; cp -a "$(step_dir 02)/tests/assets/prefetch/checkpoint" /tmp/nr
chmod -R a+rwX /tmp/nr
setpriv --reuid=65534 --regid=65534 --clear-groups env HOME=/tmp CARGO_TERM_COLOR=never \
    cargo fetch --locked --offline --manifest-path /tmp/nr/Cargo.toml >/tmp/nr.out 2>&1 \
    && echo "as uid 65534 (nobody): cargo fetch --locked --offline ok" \
    || { echo "as uid 65534 (nobody): FAILED"; tail -4 /tmp/nr.out; rc=1; }

echo
echo "########## result ##########"
[ $rc -eq 0 ] && echo "ALL OFFLINE CHECKS PASSED" || echo "SOME CHECKS FAILED"
exit $rc
