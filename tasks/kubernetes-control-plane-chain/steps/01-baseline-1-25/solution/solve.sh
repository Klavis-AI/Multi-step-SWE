#!/usr/bin/env bash
# Reference implementation for this step.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/tree"
mkdir -p /app/src
cp -a "$SRC"/src/. /app/src/
cp -a "$SRC"/Makefile /app/Makefile
make -C /app
