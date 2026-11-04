#!/usr/bin/env bash
# Oracle for milestone 2: the CSS 2.1 engine plus CSS Values 3 calc().
set -euo pipefail
cp -a /solution/app/. /app/
cd /app
make -j7
test -x build/cssdriver
