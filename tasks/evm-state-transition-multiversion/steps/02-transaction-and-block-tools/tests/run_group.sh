#!/usr/bin/env bash
# run_group.sh <group-name> <tool> <fork> <corpus-dir> <evm-path> [timeout-sec]
#
# Runs one scored group. Prints one TSV line per case to stderr for the log and
# a single "<passed> <total>" summary to stdout.
#
# <tool> selects which of the three subcommands the corpus drives: `t8n` for
# the inherited state-transition groups, `t9n` for the transaction groups,
# `b11r` for the block-assembly groups. Each has its own checker, because each
# produces a different document -- t8n writes files named by flags, t9n writes
# its array to stdout and takes no output flags at all, b11r writes one object.
#
# A case counts as passed only when every check it declares passes.
set -u
source /tests/helpers.sh

GROUP="$1"; TOOL="$2"; FORK="$3"; CORPUS="$4"; EVM="$5"
TIMEOUT="${6:-120}"
REWARD="${REWARD:--1}"
JOBS="${JOBS:-4}"

W=$(mktemp -d /tmp/group-XXXXXX)
chmod 777 "$W"

run_one() {
  local c="$1" name od chainid fork rc
  name=$(basename "$c")
  od="$W/$name"
  mkdir -p "$od"; chmod 777 "$od"
  chainid=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('chainid',1))" "$c/case.json" 2>/dev/null) || chainid=1

  case "$TOOL" in
    t8n)
      # An inherited group is fed the transaction form the previous step's
      # binary can already read; the case directory says which it carries.
      local txs="$c/txs.rlp"; [ -f "$c/txs.json" ] && txs="$c/txs.json"
      run_unprivileged timeout "$TIMEOUT" "$EVM" t8n \
          --input.alloc="$c/alloc.json" --input.env="$c/env.json" --input.txs="$txs" \
          --state.fork="$FORK" --state.chainid="$chainid" --state.reward="$REWARD" \
          --output.basedir="$od" --output.result=result.json \
          --output.alloc=allocout.json >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?
      python3 /tests/t8ncheck.py --case "$c" --result "$od/result.json" \
          --alloc "$od/allocout.json" --exit-status "$rc" --check alloc \
          >"$od/check.txt" 2>&1
      ;;
    t9n)
      # t9n has no output flags: the result document is stdout, so stdout is
      # what gets captured and scored.
      fork=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('fork',''))" "$c/case.json" 2>/dev/null)
      [ -n "$fork" ] || fork="$FORK"
      run_unprivileged timeout "$TIMEOUT" "$EVM" t9n \
          --input.txs="$c/txs.rlp" --state.fork="$fork" --state.chainid="$chainid" \
          >"$od/stdout.txt" 2>"$od/stderr.txt"
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
      echo "unknown tool $TOOL" >"$od/check.txt" ;;
  esac

  # Every checker prints one PASS/FAIL line per check and exits 0; the case
  # passes only when no line says FAIL. Reading the lines rather than the exit
  # status keeps a checker that crashed from looking like a passing case.
  # A checker that crashed leaves a Python traceback in check.txt: non-empty,
  # and with no line starting with FAIL, so a "non-empty and no FAIL" test
  # scores the crash as a PASS. Require a check that actually passed.
  if grep -q '^PASS' "$od/check.txt" 2>/dev/null && ! grep -q '^FAIL' "$od/check.txt"; then
    printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$name" >&2
    echo PASS
  else
    printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$name" \
        "$(grep '^FAIL' "$od/check.txt" 2>/dev/null | tr '\n' ';' | cut -c1-300)" >&2
    echo FAIL
  fi
  rm -rf "$od"
}
# run_unprivileged must travel into the xargs subshell too. Without it every
# case fails with "command not found" and the group scores 0 for a correct
# implementation -- a false negative that looks exactly like a real one.
export -f run_one run_unprivileged
export GROUP TOOL TIMEOUT EVM FORK REWARD W SCORE_UID SCORE_GID

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
