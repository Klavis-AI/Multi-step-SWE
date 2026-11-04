#!/usr/bin/env bash
# Rung 3 reference solution: install the sources and build them the way the
# instruction says the grader will.
set -eux
SRC="$(cd "$(dirname "$0")" && pwd)/tree"
mkdir -p /app/src /app/bin
cp -f "$SRC"/src/* /app/src/
make -C /app/src
test -x /app/bin/blaze
