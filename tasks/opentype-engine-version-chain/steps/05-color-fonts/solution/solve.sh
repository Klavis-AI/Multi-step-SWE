#!/usr/bin/env bash
# Reference solution for this milestone: install the engine source as this
# milestone leaves it.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p /app
cp -a "$HERE/tree/." /app/
echo "reference engine installed"
