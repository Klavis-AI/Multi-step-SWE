#!/usr/bin/env bash
# Oracle solution for step 02. Run by Harbor's oracle agent against /app,
# which arrives holding step 01's finished tree.
#
# Same shape as step 01's, and for the same reasons: `solve02.py` anchors every
# edit on the exact bytes it expects, counts the occurrences, and refuses to
# continue if a count is wrong or a replacement would change nothing
# (INSTRUCTIONS trap 7). Eight edits across four files, each one a site where a
# loop that retried an operation has to start reporting every attempt.
#
# 3.11.16 is this rung's interpreter, and the oracle runs the transform under
# it: the edited files use `except*`, so a solver run under 3.10 would write
# out source its own interpreter cannot parse and say nothing about it.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.11 "$here/solve02.py" /app
