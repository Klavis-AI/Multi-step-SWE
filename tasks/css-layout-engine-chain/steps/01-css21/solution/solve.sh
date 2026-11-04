#!/usr/bin/env bash
# Golden solution: materialise the engine into the cumulative workspace and
# build it exactly the way the verifier will.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cp -a "$HERE/app/." /app/
cd /app

# NOT $(nproc): inside a container that reports the NODE's CPU count (64 here)
# while the pod is scheduled at 7, so the default would oversubscribe ninefold.
make -j7

test -x /app/build/cssdriver || { echo "solve.sh: build produced no /app/build/cssdriver" >&2; exit 1; }
echo "solve.sh: built $(ls -l /app/build/cssdriver | awk '{print $5}') bytes"
