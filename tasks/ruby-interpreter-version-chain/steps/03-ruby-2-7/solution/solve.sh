#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
rm -f /app/*.c /app/*.h /app/*.o /app/ruby
cp "$here"/src/*.c "$here"/src/*.h /app/
cp "$here"/Makefile /app/Makefile
make -C /app -j7
