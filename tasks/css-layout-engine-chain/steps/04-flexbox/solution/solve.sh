#!/usr/bin/env bash
# Reference solution: copy the engine into the workspace and build it.
set -euo pipefail
cp -a /solution/app/. /app/
cd /app
make -j7
test -x build/cssdriver
