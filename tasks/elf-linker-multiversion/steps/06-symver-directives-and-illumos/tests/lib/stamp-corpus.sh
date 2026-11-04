#!/usr/bin/env bash
# Re-stamp a step's case-corpus manifest.  Run this after adding, removing or
# editing any file under <step>/tests/cases/, or after editing
# tests/lib/caselib.sh, tests/lib/run-cases.sh or tests/groups.tsv.
#
#   stamp-corpus.sh <path-to-a-step's-tests-dir>
#
# test.sh refuses to score anything if the corpus it finds does not match this
# manifest, so a stale manifest fails the whole step -- always re-stamp.
set -euo pipefail
T=${1:?usage: stamp-corpus.sh <step>/tests}
[ -d "$T/cases" ] || { echo "no $T/cases" >&2; exit 1; }
count=$(find "$T/cases" -type f -name '*.case' | wc -l)
sha=$( { find "$T/cases" -type f -name '*.case' -printf '%P\n' | LC_ALL=C sort; \
         find "$T/cases" -type f -name '*.case' -print0 | LC_ALL=C sort -z | xargs -0 cat; \
         cat "$T/lib/caselib.sh" "$T/lib/run-cases.sh" "$T/groups.tsv"; } \
       | sha256sum | cut -d' ' -f1 )
printf 'count %s\nsha256 %s\n' "$count" "$sha" > "$T/CORPUS.sha256"
echo "stamped $T/CORPUS.sha256  count=$count sha=${sha:0:16}"
