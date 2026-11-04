#!/usr/bin/env bash
# prague_probe.sh <evm-path> <probe-set-dir> <group-label>
#
# Runs the synthetic, spec-grounded probe cases of one probe set. A probe case
# is shaped exactly like a corpus case -- alloc.json, env.json, txs.json or
# txs.rlp, case.json -- and is scored by the same `t8ncheck.py` every other
# group uses. This script exists for the two things `run_group.sh` cannot do:
#
#   * a probe set is not single-fork. Half its point is the fork boundary, so
#     `h04` and `r02` run at Cancun while their siblings run at Prague, and the
#     fork comes from each case's own `case.json` rather than from the command
#     line.
#   * `expect.result_absent`: the assertion that a result field is *not* there.
#     `t8ncheck.py` compares fields that are expected to be present; a field
#     that must stay absent before the fork that defines it is a schema
#     assertion, and step 01's `schema_probe.sh` is the precedent for scoring
#     it outside the checker.
#
# Everything else -- `expect.checks`, `expect.storage`, the hash fields -- is
# delegated to `t8ncheck.py` unmodified, so a probe case and a fixture case
# agree on what "equal" means.
#
# Two probe sets ship with this step:
#
#   probe/history          EIP-2935. The history contract's runtime code in
#                          alloc.json is the `Bytecode` section of
#                          docs/fork/prague/eip-2935.md (its `Deployment`
#                          input minus the 9-byte constructor prefix
#                          60538060095f395ff3), and every expected slot is
#                          `set()`'s own rule: storage[(block.number-1) % 8191]
#                          = block.parent.hash.
#   probe/requests-schema  EIP-7685. compute_requests_hash over an empty block
#                          requests list is sha256(b'') = e3b0c442...b855, and
#                          the field exists from Prague and from no earlier
#                          fork.
#
# No expected value here was read back from a running tool. The Oracle was run
# afterwards to confirm the EIP had been read correctly, never to decide what
# the answer is.
#
# Prints one TSV line per case to stderr and "<passed> <total>" to stdout.
set -u
source /tests/helpers.sh

EVM="$1"; P="$2"; GROUP="${3:-probe}"
CHECK=/tests/t8ncheck.py
W=$(mktemp -d /tmp/prague-probe-XXXXXX); chmod 777 "$W"
passed=0; total=0

for c in "$P"/cases/*/; do
  c="${c%/}"
  [ -d "$c" ] || continue
  name=$(basename "$c")
  total=$((total + 1))
  od="$W/$name"; mkdir -p "$od"; chmod 777 "$od"

  # fork, chainid, and which of the two scoring halves this case asks for.
  meta=$(python3 - "$c/case.json" <<'PY' 2>/dev/null
import json, sys
c = json.load(open(sys.argv[1]))
e = c.get("expect") or {}
print("%s %s %d %d" % (c.get("fork", "Prague"), c.get("chainid", 1),
                       1 if (e.get("checks") or []) else 0,
                       1 if (e.get("result_absent") or []) else 0))
PY
) || meta=""
  if [ -z "$meta" ]; then
    printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$name" "unreadable case.json" >&2
    continue
  fi
  set -- $meta
  fork="$1"; chainid="$2"; has_checks="$3"; has_absent="$4"

  txs="$c/txs.rlp"; [ -f "$c/txs.json" ] && txs="$c/txs.json"

  run_unprivileged timeout "${PROBE_TIMEOUT:-60}" "$EVM" t8n \
      --input.alloc="$c/alloc.json" \
      --input.env="$c/env.json" \
      --input.txs="$txs" \
      --state.fork="$fork" \
      --state.chainid="$chainid" \
      --state.reward=-1 \
      --output.basedir="$od" \
      --output.result=result.json \
      --output.alloc=allocout.json >"$od/stdout.txt" 2>"$od/stderr.txt"
  rc=$?

  detail=""
  ok=1

  # Every probe input is a well-formed document that the rules accept, so a
  # non-zero exit is the tool failing rather than the input being refused.
  if [ "$rc" -ne 0 ]; then
    ok=0
    detail="tool exited $rc: $(tr '\n' ' ' < "$od/stderr.txt" | cut -c1-160)"
  else
    if [ "$has_checks" = 1 ]; then
      out=$(python3 "$CHECK" --case "$c" \
              --result "$od/result.json" --alloc "$od/allocout.json" 2>&1) || {
          ok=0
          detail="$(printf '%s' "$out" | grep '^FAIL' | tr '\n' ';' | cut -c1-240)"
      }
    fi
    if [ "$has_absent" = 1 ]; then
      out=$(python3 - "$c/case.json" "$od/result.json" <<'PY' 2>&1
import json, sys
case = json.load(open(sys.argv[1]))
try:
    result = json.load(open(sys.argv[2]))
except Exception as exc:
    print("FAIL result_absent no readable result.json (%s)" % type(exc).__name__)
    sys.exit(1)
bad = [k for k in (case.get("expect") or {}).get("result_absent") or [] if k in result]
for k in bad:
    print("FAIL result_absent %s is present and must not be (=%s)" % (k, result[k]))
sys.exit(1 if bad else 0)
PY
) || {
          ok=0
          detail="${detail:+$detail; }$(printf '%s' "$out" | tr '\n' ';' | cut -c1-240)"
      }
    fi
    if [ "$has_checks" = 0 ] && [ "$has_absent" = 0 ]; then
      ok=0
      detail="case.json asserts nothing"
    fi
  fi

  if [ "$ok" = 1 ]; then
    passed=$((passed + 1))
    printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$name" >&2
  else
    printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$name" "$detail" >&2
  fi
  rm -rf "$od"
done

rm -rf "$W"
echo "$passed $total"
