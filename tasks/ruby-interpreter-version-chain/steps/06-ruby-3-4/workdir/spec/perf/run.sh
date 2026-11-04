#!/bin/sh
# Measure the interpreter against this release's instruction-count budgets.
#   sh /app/spec/perf/run.sh
# Each program in bench/ is executed under `valgrind --tool=callgrind`, which
# counts the machine instructions the process executes; budgets.tsv holds one
# name<TAB>budget<TAB>sha256-of-expected-output row per program.  A benchmark
# meets its budget only when the program prints the right answer AND stays under budget:
# a binary that prints nothing executes almost no instructions.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
RUBY=${RUBY:-/app/ruby}
[ -x "$RUBY" ] || { echo "no interpreter at $RUBY (run make in /app first)"; exit 1; }
command -v valgrind >/dev/null || { echo "valgrind is not installed"; exit 1; }
pass=0; total=0
while IFS='	' read -r name budget want; do
    case "$name" in \#*|'') continue;; esac
    total=$((total+1))
    out=$(mktemp); errf=$(mktemp)
    valgrind --tool=callgrind --callgrind-out-file=/dev/null "$RUBY" \
             "$HERE/bench/$name.rb" >"$out" 2>"$errf"
    ir=$(sed -n 's/.*I  *refs: *\([0-9,]*\).*/\1/p' "$errf" | tr -d ',' | head -1)
    got=$(sha256sum < "$out" | cut -d' ' -f1)
    rm -f "$out" "$errf"
    [ -n "$ir" ] || ir=0
    if [ -n "${want:-}" ] && [ "$got" != "$want" ]; then
        verdict="WRONG OUTPUT"
    elif [ "$ir" -gt 0 ] && [ "$ir" -le "$budget" ]; then
        pass=$((pass+1)); verdict=ok
    else
        verdict=OVER
    fi
    printf '%-12s %12s / %12s  %s\n' "$name" "$ir" "$budget" "$verdict"
done < "$HERE/budgets.tsv"
echo "within budget: $pass / $total"
