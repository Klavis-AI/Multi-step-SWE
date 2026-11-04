#!/usr/bin/env bash
# Oracle solution for step 07. Run by Harbor's oracle agent against /app,
# which arrives holding step 06's finished tree.
#
# Three rules derived by parsing the tree and four byte anchors, rather than
# the flat anchor list rungs 01-04 and 06 use: this rung touches 185 sites in
# 156 files, and 185 anchors would be a list of the answers instead of a
# program that derives them. The anchors that remain are the two places where
# the mechanical answer is the wrong one -- the import cycle in
# `client/utils.py` and the PEP 765 `return` in `analytics/consumer.py`.
#
# `python3.14` is not merely this rung's interpreter here, it is a
# requirement: the solver's final pass imports the tree it has just written
# and reads `__annotations__` off sixteen public modules, and on any earlier
# interpreter that reads back the strings PEP 563 stored rather than the
# objects PEP 649 builds. `solve07.py` refuses to run below 3.14 for that
# reason.
#
# Then the rung's idiom half (migration review round 2, 2026-10-05), as a
# patch over what solve07.py writes: the task line's ruff `UP`/`PYI`/`B`/
# `ASYNC`-at-zero and pyupgrade-idempotent gates, across all 123 files they
# touch. It is a recorded result rather than a program because the program
# that produced it is two pinned third-party tools the agent image does not
# carry. Recipe, on solve07.py's output with the verifier's pins (ruff
# 0.16.10, pyupgrade 3.21.2): re-encode lib/dateutil/parser.py from
# ISO-8859-1 to UTF-8 and drop its coding line; `ruff check --isolated
# --ignore-noqa --target-version py314 --select UP,PYI,B,ASYNC --fix
# --unsafe-fixes`; every B904 `raise` gets `from None` (the recording pins
# `__cause__`, and `from <caught>` changes it on 30 recorded cases); every
# `stacklevel=2` that fix added becomes the default `stacklevel=1`; the 37
# findings no fixer handles are edited by hand (`%d`-family formats to `{:d}`,
# with `int()` where dateutil can hand a float to what was `%d`; `_ = x.content`
# for the property reads kept for their side effect; `socket.timeout` to
# `TimeoutError`; urllib3's `Url` onto a `typing.NamedTuple` base; a
# module-level default serializer for `Transport`; B017/B020/B007 in place);
# the two `reindex` kwargs become `dict[str, object] | None`; the thirteen
# `# flake8: noqa` lines in chardet go; then pyupgrade --py314-plus and ruff
# again until neither changes anything. Calibrated: 541/541 inherited and 3/3
# owned cases, every gate, step-07 reward 1.0 on the result.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3.14 "$here/solve07.py" /app
cd /app
git apply -p2 --whitespace=nowarn --check "$here/idioms.patch"
git apply -p2 --whitespace=nowarn "$here/idioms.patch"
echo "solve07: idioms.patch applied"
