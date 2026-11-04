#!/usr/bin/env bash
# Progress checks against the corpora in /data, for both tools.
#   run-public-tests.sh shape [file-substring]   shaping tests
#   run-public-tests.sh ftlint [stratum]         rasterization tests
set -u
cd "$(dirname "$0")"
case "${1:-shape}" in
  shape)  exec node ./run-shape-tests.mjs "${2:-}" ;;
  ftlint) exec node ./run-ftlint-tests.mjs "${2:-}" ;;
  *) echo "usage: $0 {shape|ftlint} [filter]"; exit 2 ;;
esac
