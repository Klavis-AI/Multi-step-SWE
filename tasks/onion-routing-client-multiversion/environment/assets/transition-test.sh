#!/usr/bin/env bash
# For each step transition: release-NN manifests carrying the release-(NN-1)
# lockfile -- the state a candidate is in after migrating its manifests but
# before cargo has updated the lock.
set -u
export CARGO_TERM_COLOR=never
prev=""
for s in /steps/*/; do
    n=$(basename "$s"); nn=${n:0:2}
    if [ -n "$prev" ]; then
        rm -rf /tmp/t; cp -a "$s/tests/assets/prefetch/checkpoint" /tmp/t
        cp "$prev/tests/assets/prefetch/checkpoint/Cargo.lock" /tmp/t/Cargo.lock
        out=$( cd /tmp/t && timeout 300 cargo fetch --offline 2>&1 ); rc=$?
        if [ $rc -eq 0 ]; then
            echo "$nn: release-$nn manifests + previous lock: resolves offline OK"
        else
            first=$(printf '%s\n' "$out" | grep -E "^error|failed to download" | head -2 | tr '\n' ' ')
            echo "$nn: FAILED -- $first"
        fi
    fi
    prev=$s
done
