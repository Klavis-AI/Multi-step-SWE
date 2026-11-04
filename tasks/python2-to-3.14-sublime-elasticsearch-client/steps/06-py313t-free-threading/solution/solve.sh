#!/usr/bin/env bash
# Oracle solution for step 06. Run by Harbor's oracle agent against /app,
# which arrives holding step 05's finished tree.
#
# Nineteen byte anchors, in the shape rungs 01-04 use: this rung's work is
# four objects and a `threading.Lock` each, and every site it touches arrives
# here byte-identical from the pin. The anchors say what was written; the
# final pass in `solve06.py` re-parses the finished tree and says what was
# left, which is the half an anchor list cannot see (NOTES section 11.6).
#
# `python3.13t` is this rung's interpreter and the oracle runs under it. The
# solver only rewrites bytes and re-parses them, so any 3.12-or-later build
# would do -- the tree it parses uses PEP 695 `type` aliases -- but running a
# rung's oracle on anything but the rung's own interpreter is how a difference
# between the two goes unnoticed.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.13t "$here/solve06.py" /app
