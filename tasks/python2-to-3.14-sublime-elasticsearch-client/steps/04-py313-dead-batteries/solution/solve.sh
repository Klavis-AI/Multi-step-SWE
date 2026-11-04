#!/usr/bin/env bash
# Oracle solution for step 04. Run by Harbor's oracle agent against /app,
# which arrives holding step 03's finished tree.
#
# Same shape as the earlier rungs': `solve04.py` anchors every edit on the
# exact bytes it expects, counts the occurrences, and refuses to continue if a
# count is wrong or a replacement would change nothing (INSTRUCTIONS trap 7).
# Nineteen anchored edits across seven files, three of them optional because
# step 03's instruction leaves them free, and a final AST pass over the whole
# tree that fails the script if any of the three constructs this rung retires
# survives -- an anchor says what was edited and only that pass says what was
# left (NOTES section 9.7).
#
# 3.13.15 is this rung's interpreter and the oracle runs under it. It has to
# be 3.11 or later whatever happens: the final pass parses every file in the
# tree and step 02 wrote `except*` into two of them.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.13 "$here/solve04.py" /app
