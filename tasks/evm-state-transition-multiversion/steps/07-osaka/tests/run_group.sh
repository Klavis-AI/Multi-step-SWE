#!/usr/bin/env bash
# run_group.sh <group-name> <fork> <corpus-dir> <evm-path> [timeout-sec]
#
# Runs one scored group. Prints one TSV line per case to stderr for the log and
# a single "<passed> <total>" summary to stdout.
#
# A case counts as passed only when every check it declares passes. That set is
# the case's own (`expect.checks`) plus the ones this script opts into with
# `--check`; the `--check` names are skipped for any case that does not carry
# the matching fixture value. So an inherited case still asserts its fork's
# semantics rather than this step's new result schema.
set -u
source /tests/helpers.sh

GROUP="$1"; FORK="$2"; CORPUS="$3"; EVM="$4"
TIMEOUT="${5:-120}"
REWARD="${REWARD:--1}"
JOBS="${JOBS:-4}"

W=$(mktemp -d /tmp/group-XXXXXX)
chmod 777 "$W"

run_one() {
  local c="$1" name od chainid txs rc cfork tool
  name=$(basename "$c")
  od="$W/$name"
  mkdir -p "$od"; chmod 777 "$od"
  chainid=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['chainid'])" "$c/case.json" 2>/dev/null) || chainid=1

  # A case may name the fork it grades; otherwise the group's fork applies.
  # `osaka-spec-vectors` uses this for the two gating cases, which assert what
  # an earlier fork must *not* do with this step's opcode and precompile. The
  # group's `osaka-spec-vectors` scope covers them: the EIP text says the
  # feature is new at Osaka, and the negative half of that claim is only
  # checkable at the fork where the feature is absent.
  cfork=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('fork') or '')" "$c/case.json" 2>/dev/null) || cfork=
  [ -n "$cfork" ] || cfork="$FORK"

  # Which tool the case grades. Step 02 introduced `t9n` and `b11r`; nothing
  # after it re-exercised them at this step, so the two inherited groups carry
  # a few of each. A candidate that deletes either subcommand while adding
  # Osaka has broken an inherited milestone, and an inherited group that never
  # invokes the tool cannot notice.
  tool=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('tool','t8n'))" "$c/case.json" 2>/dev/null) || tool=t8n
  [ -n "$tool" ] || tool=t8n

  case "$tool" in
    t8n)
      # A corpus carries its transactions either as a signed RLP list or in the
      # older JSON input form; the case directory says which. An inherited group
      # must not be fed a format this step introduced.
      txs="$c/txs.rlp"; [ -f "$c/txs.json" ] && txs="$c/txs.json"

      run_unprivileged timeout "$TIMEOUT" "$EVM" t8n \
          --input.alloc="$c/alloc.json" \
          --input.env="$c/env.json" \
          --input.txs="$txs" \
          --state.fork="$cfork" \
          --state.chainid="$chainid" \
          --state.reward="$REWARD" \
          --output.basedir="$od" \
          --output.result=result.json \
          --output.alloc=allocout.json >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?

      # t8ncheck.py returns its verdict in the exit status. That is what this
      # step has always graded t8n on and it is left exactly as it was, so no
      # existing case can change verdict because of this dispatch.
      if python3 /tests/t8ncheck.py --case "$c" --result "$od/result.json" \
            --alloc "$od/allocout.json" --exit-status "$rc" --check alloc \
            --check logsBloom \
            >"$od/check.txt" 2>&1; then
        printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$name" >&2
        echo PASS
      else
        printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$name" \
            "$(tr '\n' ';' <"$od/check.txt" | cut -c1-300)" >&2
        echo FAIL
      fi
      ;;
    t9n)
      # t9n has no output flags: the result document is stdout, so stdout is
      # what gets captured and scored. The case runs at its own stored fork, so
      # the tool is exercised as the step that introduced it exercised it.
      run_unprivileged timeout "$TIMEOUT" "$EVM" t9n \
          --input.txs="$c/txs.rlp" --state.fork="$cfork" --state.chainid="$chainid" \
          >"$od/stdout.txt" 2>"$od/stderr.txt"
      rc=$?
      python3 /tests/t9ncheck.py --case "$c" --stdout "$od/stdout.txt" \
          --exit-status "$rc" >"$od/check.txt" 2>&1
      verdict_from_lines "$od" "$name"
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
      verdict_from_lines "$od" "$name"
      ;;
    *)
      printf 'CASE\t%s\t%s\tFAIL\ttool expected=<t8n|t9n|b11r> actual=%s\n' \
          "$GROUP" "$name" "$tool" >&2
      echo FAIL
      ;;
  esac
  rm -rf "$od"
}

# t9ncheck.py and b11rcheck.py print one PASS/FAIL line per check and exit 0
# even when a check failed, so their verdict has to be read from the lines.
# Grading them on the exit status would pass every case unconditionally -- and
# in a gate group that is a veto which has silently stopped working.
verdict_from_lines() {
  local od="$1" name="$2"
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
}
# run_unprivileged must travel into the xargs subshell too. Without it every
# case fails with "command not found" and the group scores 0 for a correct
# implementation -- a false negative that looks exactly like a real one.
export -f run_one run_unprivileged verdict_from_lines
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
