#!/usr/bin/env bash
# Oracle solution for step 05. Run by Harbor's oracle agent against /app,
# which arrives holding step 04's finished tree.
#
# Not a list of byte anchors like rungs 01-04: this rung's work is 776
# annotations across 158 files, so `solve05.py` carries a table keyed by
# signature, finds every site the rung requires by parsing each file, and
# fails rather than guess at one the table does not cover. It then re-parses
# the finished tree and fails unless every site in scope is annotated and
# every annotated file carries the future import -- the edits say what was
# written, only that pass says what was left (NOTES section 11.4).
#
# 3.13.15 is this rung's interpreter and the oracle runs under it. It has to
# be 3.12 or later whatever happens: the tree this solver writes uses PEP 695
# `type` aliases, and the final pass parses every file it wrote.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.13 "$here/solve05.py" /app
