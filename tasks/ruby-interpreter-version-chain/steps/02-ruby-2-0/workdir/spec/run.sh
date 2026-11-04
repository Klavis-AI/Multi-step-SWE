#!/bin/sh
# Run the conformance corpus in this directory against ./ruby and report which
# programs produce what the release under implementation produces.
#
#   sh /app/spec/run.sh              # run everything, print a summary
#   sh /app/spec/run.sh s001234      # run one program and show the difference
#
# expected.tsv holds one row per program:
#   id <TAB> exit-status <TAB> exception-class ("-" when none) <TAB> warned
#      <TAB> check_warn <TAB> switches <TAB> base64 of the expected stdout
# A program passes when its standard output and its exit status are identical to
# the recorded ones, when (for a non-zero status) the exception class named in
# parentheses at the end of a line on standard error matches, and -- when the row's check_warn
# column is 1 -- when it agrees about whether a warning was printed.
set -u
SPEC=$(cd "$(dirname "$0")" && pwd)
RUBY=${RUBY:-/app/ruby}
[ -x "$RUBY" ] || { echo "no interpreter at $RUBY (run make in /app first)"; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
ONE=${1:-}
pass=0; fail=0; failed=''
while IFS='	' read -r id status exc warned check_warn switches b64; do
    case "$id" in \#*|'') continue;; esac
    [ -n "$ONE" ] && [ "$ONE" != "$id" ] && continue
    [ "$exc" = "-" ] && exc=""
    printf '%s' "$b64" | base64 -d > "$WORK/want" 2>/dev/null || : > "$WORK/want"
    ( cd "$WORK" && timeout 10 "$RUBY" $switches "$SPEC/cases/$id.rb" >"$WORK/out" 2>"$WORK/err" )
    got=$?
    ok=1
    cmp -s "$WORK/out" "$WORK/want" || ok=0
    [ "$got" = "$status" ] || ok=0
    if [ "$status" != "0" ]; then
        gotexc=$(sed -n 's/.*(\([A-Z][A-Za-z0-9_:]*\))[[:space:]]*$/\1/p' "$WORK/err" | head -1)
        if [ -z "$gotexc" ] && grep -qE 'syntax error|SyntaxError' "$WORK/err"; then gotexc=SyntaxError; fi
        [ "$gotexc" = "$exc" ] || ok=0
    fi
    if [ "$check_warn" = "1" ]; then
        gotw=0; grep -q 'warning:' "$WORK/err" && gotw=1
        [ "$gotw" = "$warned" ] || ok=0
    fi
    if [ "$ok" = 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); failed="$failed $id"; fi
    if [ -n "$ONE" ]; then
        echo "--- $id   ruby $switches cases/$id.rb"
        echo "--- expected exit status $status${exc:+, $exc}; actual $got"
        echo "--- expected stdout:"; cat "$WORK/want"
        echo "--- actual stdout:";   cat "$WORK/out"
        echo "--- actual stderr:";   cat "$WORK/err"
    fi
done < "$SPEC/expected.tsv"
[ -n "$ONE" ] && exit 0
echo "pass $pass / $((pass+fail))"
if [ "$fail" -gt 0 ]; then
    echo "failing:"; for f in $failed; do echo "  $f"; done | head -60
fi
