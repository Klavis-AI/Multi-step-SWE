#!/usr/bin/env bash
# Reference solution for this milestone: install the sources and build them
# exactly the way the grader will.
set -euo pipefail
cd /app
tar -xJf "$(dirname "$0")/solution.tar.xz" -C /app
make
ls -l bin/
