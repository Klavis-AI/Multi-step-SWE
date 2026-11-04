#!/usr/bin/env bash
# Reference solution: install this release's interpreter sources over the
# workspace, replacing whatever the previous milestone left behind, and build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
rm -rf /app/src /app/build /app/Makefile /app/lua /app/lua.in
mkdir -p /app
cp -r "$HERE/src" /app/src
cp "$HERE/Makefile" /app/Makefile
cp "$HERE/lua.in" /app/lua.in
cd /app
make
