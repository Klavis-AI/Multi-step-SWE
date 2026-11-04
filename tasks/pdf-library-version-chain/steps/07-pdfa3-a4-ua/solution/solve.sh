#!/usr/bin/env bash
# Oracle solution for this milestone. Step 7 extends the step-6 workspace rather than replacing it.
#
# `tree/` is the reference implementation AS THIS MILESTONE LEAVES IT -- not the
# finished article. Its end state at step k is the workspace step k+1 starts from, so
# it implements only what this milestone and the earlier ones require.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p /app
cp -r "$HERE/tree/." /app/
cd /app
make -j"$(nproc)"

# Leave the workspace the way the milestone instruction requires: source plus the
# Makefile's own build/ directory, nothing else.
ls -la /app/build/
