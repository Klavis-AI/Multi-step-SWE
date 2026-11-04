#!/usr/bin/env bash
# Oracle solution for step 01. Run by Harbor's oracle agent against /app.
#
# The migration ships as a program rather than as a patch or a payload of
# replacement files, for three reasons:
#
#   * every edit asserts that it landed. `solve01.py` anchors each change on
#     the exact bytes it expects to find, counts the occurrences, and refuses
#     to continue if a count is wrong or a replacement would be a no-op. A
#     patch that applied with fuzz, or a payload copied over a tree that had
#     moved, would both be silent (INSTRUCTIONS trap 7).
#   * it is re-derivable. If the pin moves, the anchors say exactly where and
#     why each change was made instead of leaving 111 rewritten files to be
#     re-read.
#   * it reads as a description of the migration: the edits are grouped by the
#     construct they retire, and the ones with a judgement behind them carry
#     that judgement in a comment.
#
# 3.10.21 is this rung's interpreter, so the oracle uses it to run the
# transform as well -- nothing here needs a specific version, but a solution
# run under an interpreter the rung does not name would be its own small lie.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.10 "$here/solve01.py" /app
