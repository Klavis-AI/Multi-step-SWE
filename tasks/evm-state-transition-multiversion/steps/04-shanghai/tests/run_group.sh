#!/usr/bin/env bash
# run_group.sh <group-name> <fork> <corpus-dir> <evm-path> [timeout-sec]
#
# Runs one scored group. Prints one TSV line per case to stderr for the log and
# a single "<passed> <total>" summary to stdout.
#
# A case counts as passed only when every check it declares passes. The check
# vocabulary is the case's own (`expect.checks`), so an inherited case asserts
# fork semantics and not this step's new result schema.
#
# Two per-case keys in `case.json` select what is run, both with a default that
# leaves every case written before they existed behaving exactly as it did:
#
#   "fork": the fork this case is graded at. Defaults to <fork>, the group's
#           fork. Every corpus case has always carried it and it has always
#           equalled the group's fork; the inherited tier now also carries a
#           handful of cases at an *earlier* step's fork -- Merge, which step 03
#           introduced -- because Harbor never re-runs step 03's verifier, so a
#           candidate who drops the Merge is otherwise never caught again.
#
#   "tool": t8n (default), t9n or b11r. Same reason: `t9n` and `b11r` are step
#           02's headline capability and nothing after step 02 exercised them.
#           Each subcommand has its own checker, because each produces a
#           different document -- t8n writes files named by flags, t9n writes
#           its array to stdout and takes no output flags at all, b11r writes
#           one object.
set -u
source /tests/helpers.sh

GROUP="$1"; FORK="$2"; CORPUS="$3"; EVM="$4"
TIMEOUT="${5:-120}"
REWARD="${REWARD:--1}"
JOBS="${JOBS:-4}"

W=$(mktemp -d /tmp/group-XXXXXX)
chmod 777 "$W"

run_one() {
  local c="$1" name od chainid fork tool txs rc
  name=$(basename "$c")
  od="$W/$name"
  mkdir -p "$od"; chmod 777 "$od"
  chainid=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('chainid',1))" "$c/case.json" 2>/dev/null) || chainid=1
  fork=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('fork') or '')" "$c/case.json" 2>/dev/null) || fork=""
  [ -n "$fork" ] || fork="$FORK"
  tool=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('tool') or 't8n')" "$c/case.json" 2>/dev/null) || tool=t8n

  case "$tool" in
    t9n)
      # t9n has no output flags: the result document is stdout, so stdout is
      # what gets captured and scored.
      run_unprivileged timeout "$TIMEOUT" "$EVM" t9n \
          --input.txs="$c/txs.rlp" \
          --state.fork="$fork" \
          --state.chainid="$chainid" >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?
      python3 /tests/t9ncheck.py --case "$c" --stdout "$od/stdout.txt" \
          --exit-status "$rc" >"$od/check.txt" 2>&1
      ;;
    b11r)
      # b11r takes neither --state.fork nor --state.chainid: it assembles a
      # block from the parts it is given and is fork-agnostic.
      run_unprivileged timeout "$TIMEOUT" "$EVM" b11r \
          --input.header="$c/header.json" --input.txs="$c/txs.rlp" \
          --input.ommers="$c/ommers.json" \
          --output.basedir="$od" --output.block=block.json \
          >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?
      python3 /tests/b11rcheck.py --case "$c" --block "$od/block.json" \
          --exit-status "$rc" >"$od/check.txt" 2>&1
      ;;
    *)
      # A corpus carries its transactions either as a signed RLP list or in the
      # older JSON input form; the case directory says which. An inherited group
      # must not be fed a format this step introduced.
      txs="$c/txs.rlp"; [ -f "$c/txs.json" ] && txs="$c/txs.json"

      run_unprivileged timeout "$TIMEOUT" "$EVM" t8n \
          --input.alloc="$c/alloc.json" \
          --input.env="$c/env.json" \
          --input.txs="$txs" \
          --state.fork="$fork" \
          --state.chainid="$chainid" \
          --state.reward="$REWARD" \
          --output.basedir="$od" \
          --output.result=result.json \
          --output.alloc=allocout.json >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?

      python3 /tests/t8ncheck.py --case "$c" --result "$od/result.json" \
          --alloc "$od/allocout.json" --exit-status "$rc" --check alloc \
          >"$od/check.txt" 2>&1
      ;;
  esac

  # The case passes only when the checker actually graded it and nothing failed.
  # "Graded it" means at least one PASS line: a checker that crashed on import
  # or on a malformed case prints nothing or prints only FAIL lines, and an
  # empty result must never read as a pass. "Nothing failed" means no `^FAIL`
  # line. The checker's own exit status is deliberately not consulted: the three
  # checkers disagree about it -- t8ncheck.py exits non-zero iff a check failed,
  # t9ncheck.py and b11rcheck.py always exit 0 and are read from their lines --
  # and in every one of the three the printed lines already say what happened.
  if grep -q '^PASS' "$od/check.txt" && ! grep -q '^FAIL' "$od/check.txt"; then
    printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$name" >&2
    echo PASS
  else
    printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$name" \
        "$(tr '\n' ';' <"$od/check.txt" | cut -c1-300)" >&2
    echo FAIL
  fi
  rm -rf "$od"
}
# run_unprivileged must travel into the xargs subshell too. Without it every
# case fails with "command not found" and the group scores 0 for a correct
# implementation -- a false negative that looks exactly like a real one.
export -f run_one run_unprivileged
export GROUP TIMEOUT EVM FORK REWARD W SCORE_UID SCORE_GID

# The denominator is the number of case directories, counted before anything
# runs -- not the number of lines that came back. A case whose worker is killed
# produces no line, and dividing by the lines received would let a group that
# lost half its cases still report passed == total and score 1.
expected=$(ls -d "$CORPUS"/cases/*/ 2>/dev/null | wc -l)

results=$(ls -d "$CORPUS"/cases/*/ 2>/dev/null | sed 's#/$##' |
          xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {} 2>>/tmp/verifier-cases.log)
rm -rf "$W"

passed=$(printf '%s\n' "$results" | grep -c '^PASS$' || true)
echo "$passed $expected"
