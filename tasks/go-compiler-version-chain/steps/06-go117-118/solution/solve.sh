#!/usr/bin/env bash
set -euo pipefail
# Reference implementation: a Go toolchain written in C11.
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p /app
cp -a "$DIR/payload/." /app/
cd /app
make -j"$(nproc)"
/app/goroot/bin/go version
