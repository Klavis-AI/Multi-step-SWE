#!/usr/bin/env bash
# Run the engine in /app against the 30 fonts in /data/fonts and report
# how many glyph lines match FreeType's own output, per stratum.
#
# The fonts are a representative sample of the kinds of fonts the engine must
# handle.
set -u
cd "$(dirname "$0")"
exec node ./run-public-tests.mjs "$@"
