#!/usr/bin/env bash
set -euo pipefail
# Reference solution: drop in the cumulative Java source tree for this step and
# build it exactly the way the grader does.
SRC="$(cd "$(dirname "$0")" && pwd)/tree"
mkdir -p /app
cp -a "$SRC/." /app/
cd /app
make
ls -l /app/src/redis-server
