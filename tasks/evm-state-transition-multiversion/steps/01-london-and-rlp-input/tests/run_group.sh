#!/usr/bin/env bash
# run_group.sh <group-name> <fork> <corpus-dir> <evm-path> [timeout-sec]
#
# Runs one scored group. Prints one TSV line per case to stderr for the log and
# a single "<passed> <total>" summary to stdout.
#
# A case counts as passed only when every check it declares passes. The check
# vocabulary is the case's own (`expect.checks`), so an inherited case asserts
# fork semantics and not this step's new result schema.
set -u
source /tests/helpers.sh

GROUP="$1"; FORK="$2"; CORPUS="$3"; EVM="$4"
TIMEOUT="${5:-120}"
REWARD="${REWARD:--1}"
JOBS="${JOBS:-4}"

W=$(mktemp -d /tmp/group-XXXXXX)
chmod 777 "$W"

run_one() {
  local c="$1" name od chainid txs rc
  name=$(basename "$c")
  od="$W/$name"
  mkdir -p "$od"; chmod 777 "$od"
  chainid=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['chainid'])" "$c/case.json" 2>/dev/null) || chainid=1

  # A corpus carries its transactions either as a signed RLP list or in the
  # older JSON input form; the case directory says which. An inherited group
  # must not be fed a format this step introduced.
  txs="$c/txs.rlp"; [ -f "$c/txs.json" ] && txs="$c/txs.json"

  run_unprivileged timeout "$TIMEOUT" "$EVM" t8n \
      --input.alloc="$c/alloc.json" \
      --input.env="$c/env.json" \
      --input.txs="$txs" \
      --state.fork="$FORK" \
      --state.chainid="$chainid" \
      --state.reward="$REWARD" \
      --output.basedir="$od" \
      --output.result=result.json \
      --output.alloc=allocout.json >"$od/stdout.txt" 2>"$od/stderr.txt"
  rc=$?

  if python3 /tests/t8ncheck.py --case "$c" --result "$od/result.json" \
        --alloc "$od/allocout.json" --exit-status "$rc" --check alloc \
        >"$od/check.txt" 2>&1; then
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
