#!/usr/bin/env bash
# Reference implementation for this milestone: the cumulative source tree.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p /app/src /app/bin
cp "$here"/src/*.js /app/src/
cp "$here"/bin/smbd /app/bin/smbd
chmod +x /app/bin/smbd
for f in /app/src/*.js; do node --check "$f"; done
