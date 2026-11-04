#!/usr/bin/env bash
# Oracle solution for step 01-proto2.
#
# Installs the reference implementation into /app and builds it through the
# same entry point the instruction names: `make` at /app producing
# /app/bin/protoc.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

mkdir -p /app
cp -a "$HERE/tree/." /app/

cd /app
rm -rf bin
find . -name '*.o' -delete 2>/dev/null || true
make

test -x /app/bin/protoc || { echo "solve.sh: /app/bin/protoc was not produced" >&2; exit 1; }
/app/bin/protoc --version
