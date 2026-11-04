#!/usr/bin/env bash
# Run the sample in this directory through the programs this workspace builds
# and print, for each group, how many comparison units match (INTERFACE.md
# section 4).
#
# The sample is staged at /dw/out because that is where these ELF files were
# compiled: paths recorded inside their debug information resolve there.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

mkdir -p /dw/out
rm -rf /dw/out/fx /dw/out/cores
cp -r "$HERE/fx" /dw/out/fx
[ -d "$HERE/cores" ] && cp -r "$HERE/cores" /dw/out/cores

exec python3 "$HERE/compare.py" "$ROOT" "$HERE/exp"
