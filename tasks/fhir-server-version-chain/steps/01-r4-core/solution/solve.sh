#!/usr/bin/env bash
# Reference solution for rung 1: install the C sources and build the server.
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)/tree"
mkdir -p /app/src /app/bin
cp -f "$SRC"/src/* /app/src/
make -C /app/src
test -x /app/bin/blaze
echo "reference server built"
