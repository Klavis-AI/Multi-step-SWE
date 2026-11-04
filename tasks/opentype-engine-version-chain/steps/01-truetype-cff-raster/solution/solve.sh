#!/usr/bin/env bash
# Reference solution for this milestone: install the engine source as this
# milestone leaves it.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p /app
cp -a "$HERE/tree/." /app/
node /app/bin/ftlint.js -f 2 -r 0 -i 0-3 16 /data/fonts/$(ls /data/fonts | head -1) >/dev/null
echo "reference engine installed"
