#!/usr/bin/env bash
# Oracle solution for step 03. Run by Harbor's oracle agent against /app,
# which arrives holding step 02's finished tree.
#
# Same shape as the earlier rungs': `solve03.py` anchors every edit on the
# exact bytes it expects, counts the occurrences, and refuses to continue if a
# count is wrong or a replacement would change nothing (INSTRUCTIONS trap 7).
# Thirty-five anchored edits across eleven files, twenty-six string literals
# re-spelled as raw, and four files deleted.
#
# The raw-string pass is the one part that is not byte-anchored, because the
# edit is mechanical and a stronger assertion is available: every literal it
# rewrites is checked to evaluate to the same value it did before, so a
# docstring whose first line is scored cannot move underneath it.
#
# 3.12.14 is this rung's interpreter and the oracle runs under it: the
# rewriting pass decides which literals to touch by asking the compiler which
# ones it warns about, and only 3.12 and later warn.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.12 "$here/solve03.py" /app
