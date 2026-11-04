#!/usr/bin/env bash
# negative_probe.sh <evm-path> <probe-input-dir>
#
# The `osaka-negative` group. EIP-7825 is the milestone's signature property
# and it is a *rejection* rule: a transaction that every earlier fork accepts
# is invalid at Osaka. A corpus of state tests can show the accepting side of
# that; it cannot show that the rejection is fork-gated, that it survives every
# transaction type, that it leaves the rest of the block alone, or that a
# rejection is reported through `result.rejected` with exit 0 rather than by
# failing the process. Those are what this probe asserts.
#
# Every expected value is derived from the vendored specification, not from a
# running tool:
#
#   * the cap itself -- `/app/docs/fork/osaka/eip-7825.md`, "Enforce a
#     protocol-level maximum of 16,777,216 gas (2^24) for any single
#     transaction". 0x1000000 is exactly the cap and must be accepted;
#     0x1000001 is one over and must be rejected.
#   * "This cap applies regardless of the block gas limit" -- `env.json` sets
#     `currentGasLimit` to 0x5f5e100 (100,000,000), far above the cap, so a
#     rejection here cannot be the ordinary block-gas-limit check.
#   * "if the `gasLimit` specified by the sender exceeds 16,777,216, the
#     transaction is invalidated" -- the rule is on the declared `gasLimit`,
#     so it applies to a legacy, a 2930, a 1559 and a creation transaction
#     alike, and it fires before execution (no gas is consumed, no nonce is
#     spent).
#   * the reporting contract -- `/app/TEST_INTERFACE.md`: an invalid
#     transaction is listed in `result.rejected` and the process still exits
#     0; only a malformed *invocation* exits non-zero, and then no
#     `--output.result` file is written.
#
# Prints one TSV line per probe to stderr and "<passed> <total>" to stdout.
set -u
source /tests/helpers.sh

EVM="$1"; P="$2"
GROUP=osaka-negative
W=$(mktemp -d /tmp/negprobe-XXXXXX); chmod 777 "$W"
passed=0; total=0

ok()  { passed=$((passed+1)); total=$((total+1)); printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$1" >&2; }
bad() { total=$((total+1)); printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$1" \
        "$(printf '%s' "$2" | tr '\n' ';' | cut -c1-300)" >&2; }

RC=0
# t8n <outdir> <txs-file> <fork> [alloc-file]
t8n() {
  local od="$W/$1" txf="$2" fork="$3" allocf="${4:-alloc.json}"
  rm -rf "$od"; mkdir -p "$od"; chmod 777 "$od"
  run_unprivileged timeout 120 "$EVM" t8n \
      --input.alloc="$P/$allocf" --input.env="$P/env.json" --input.txs="$P/$txf" \
      --state.fork="$fork" --state.chainid=1 --state.reward=-1 \
      --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
      >"$od/stdout.txt" 2>"$od/stderr.txt"
  RC=$?
}

# assert <label> <outdir> <exit-status> <expect-json>
#
# `expect-json` names the properties to check:
#   rejected  -- list of transaction indexes that must be in result.rejected,
#                and no others
#   gasUsed   -- "zero" or "positive"
#   slots     -- {address: {slot: value}} read out of the post-state alloc;
#                a value of null means the slot must be unset
assert() {
  local label="$1" od="$W/$2" rc="$3" expect="$4" out
  out=$(python3 - "$od" "$rc" "$expect" <<'PY'
import json, os, sys
od, rc, expect = sys.argv[1], int(sys.argv[2]), json.loads(sys.argv[3])
problems = []

def qty(v):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, int):
        return v
    if v is None:
        return None
    s = str(v).strip()
    if s == "":
        return 0
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except ValueError:
        return None

rpath = os.path.join(od, "result.json")
apath = os.path.join(od, "allocout.json")

if rc != 0:
    print("process exited %d, expected 0 (stderr: %s)"
          % (rc, open(os.path.join(od, "stderr.txt")).read()[-200:]))
    raise SystemExit(0)
try:
    result = json.load(open(rpath))
except Exception as exc:
    print("no readable result.json (%s)" % exc)
    raise SystemExit(0)
try:
    alloc = json.load(open(apath))
except Exception:
    alloc = {}

if "rejected" in expect:
    raw = result.get("rejected") or []
    got = sorted(qty(x.get("index")) if isinstance(x, dict) else qty(x)
                 for x in raw if not isinstance(x, bool))
    want = sorted(expect["rejected"])
    if got != want:
        problems.append("rejected=%s want %s" % (got, want))

if "gasUsed" in expect:
    g = qty(result.get("gasUsed"))
    if g is None:
        problems.append("gasUsed missing")
    elif expect["gasUsed"] == "zero" and g != 0:
        problems.append("gasUsed=%d want 0" % g)
    elif expect["gasUsed"] == "positive" and g <= 0:
        problems.append("gasUsed=%d want >0" % g)

def norm(addr, width=20):
    s = str(addr).lower()
    if s.startswith("0x"):
        s = s[2:]
    return "0x" + s.rjust(width * 2, "0")

lower = {norm(k): v for k, v in alloc.items()} if isinstance(alloc, dict) else {}
for addr, slots in (expect.get("slots") or {}).items():
    acct = lower.get(norm(addr)) or {}
    have = {norm(k, 32): qty(v) for k, v in (acct.get("storage") or {}).items()}
    for slot, val in slots.items():
        got = have.get(norm(slot, 32), 0)
        want = 0 if val is None else qty(val)
        if got != want:
            problems.append("%s[%s]=%s want %s"
                            % (norm(addr)[:10], norm(slot, 32)[:10], got, want))

print("; ".join(problems))
PY
)
  if [ -z "$out" ]; then ok "$label"; else bad "$label" "$out"; fi
}

# assert_invocation_fails <label> <extra-args...>
#
# A malformed *invocation* must fail loudly: a non-zero exit status and no
# `--output.result` file left behind for a caller to misread as a result.
assert_invocation_fails() {
  local label="$1"; shift
  local od="$W/$label"
  rm -rf "$od"; mkdir -p "$od"; chmod 777 "$od"
  run_unprivileged timeout 120 "$EVM" t8n \
      --input.env="$P/env.json" --state.chainid=1 --state.reward=-1 \
      --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
      "$@" >"$od/stdout.txt" 2>"$od/stderr.txt"
  local rc=$?
  if killed_rc "$rc"; then
    bad "$label" "killed (exit $rc) rather than refusing: this measured load, not behaviour"
  elif [ "$rc" -eq 0 ]; then
    bad "$label" "exit 0, expected a refusal"
  elif [ -f "$od/result.json" ]; then
    bad "$label" "exit $rc but a result.json was still written"
  else
    ok "$label"
  fi
}

# -------------------------------------------------------------------------
# EIP-7825: a gasLimit one over the cap is invalid, whatever the transaction
# type. Nothing runs, so gasUsed is 0 and the callee's slot stays unset.
# -------------------------------------------------------------------------
over='{"rejected":[0],"gasUsed":"zero","slots":{"0x1000":{"0x0":null}}}'
t8n legacy-over  tx-legacy-over.json  Osaka
assert over-cap-legacy-rejected  legacy-over  "$RC" "$over"
t8n type1-over   tx-type1-over.json   Osaka
assert over-cap-type1-rejected   type1-over   "$RC" "$over"
t8n type2-over   tx-type2-over.json   Osaka
assert over-cap-type2-rejected   type2-over   "$RC" "$over"
t8n create-over  tx-create-over.json  Osaka
assert over-cap-create-rejected  create-over  "$RC" \
  '{"rejected":[0],"gasUsed":"zero"}'

# -------------------------------------------------------------------------
# 16,777,216 is the cap, not one below it, and the cap is not the block gas
# limit: `currentGasLimit` here is 100,000,000.
# -------------------------------------------------------------------------
t8n at-cap  tx-legacy-at-cap.json  Osaka
assert at-cap-accepted  at-cap  "$RC" \
  '{"rejected":[],"gasUsed":"positive","slots":{"0x1000":{"0x0":"0x1"}}}'
t8n under-cap  tx-legacy-under.json  Osaka
assert under-cap-accepted  under-cap  "$RC" \
  '{"rejected":[],"gasUsed":"positive","slots":{"0x1000":{"0x0":"0x1"}}}'

# -------------------------------------------------------------------------
# The rule is new at Osaka. The same transaction at Prague must still run --
# a tool that rejects it everywhere has not implemented EIP-7825, it has
# broken Prague.
# -------------------------------------------------------------------------
t8n over-prague  tx-legacy-over.json  Prague
assert over-cap-accepted-at-prague  over-prague  "$RC" \
  '{"rejected":[],"gasUsed":"positive","slots":{"0x1000":{"0x0":"0x1"}}}'

# -------------------------------------------------------------------------
# A rejection removes one transaction, not the block. It also does not spend
# the sender's nonce, so the next transaction may reuse it.
# -------------------------------------------------------------------------
t8n reject-then-ok  txs-reject-then-ok.json  Osaka
assert rejection-does-not-stop-the-block  reject-then-ok  "$RC" \
  '{"rejected":[0],"gasUsed":"positive","slots":{"0x1000":{"0x0":null},"0x1001":{"0x0":"0x1"}}}'
t8n ok-then-reject  txs-ok-then-reject.json  Osaka
assert rejection-after-a-good-transaction  ok-then-reject  "$RC" \
  '{"rejected":[1],"gasUsed":"positive","slots":{"0x1000":{"0x0":null},"0x1001":{"0x0":"0x1"}}}'

# -------------------------------------------------------------------------
# An invalid transaction is data, reported in `rejected` with exit 0; a
# malformed invocation is an error, reported by the exit status with no
# output file written. Confusing the two is the failure mode this block
# exists to catch.
# -------------------------------------------------------------------------
assert_invocation_fails exit-status-missing-alloc \
  --input.alloc="$P/does-not-exist.json" --input.txs="$P/tx-legacy-under.json" \
  --state.fork=Osaka
assert_invocation_fails exit-status-malformed-txs \
  --input.alloc="$P/alloc.json" --input.txs="$P/txs-malformed.json" \
  --state.fork=Osaka
assert_invocation_fails exit-status-unknown-fork \
  --input.alloc="$P/alloc.json" --input.txs="$P/tx-legacy-under.json" \
  --state.fork=NoSuchFork
# A `.rlp` transaction file is a JSON-quoted hex string, so it has two
# quite different failure modes and they are graded apart on purpose.
# `probe/txs-truncated.rlp` is a corpus `.rlp` list cut short *inside the
# quoted string*: the document never closes, so the tool cannot read the
# input at all -- non-zero, no result. Measured on both binaries before the
# probe was written: `evm-v1.16.2` and `evm-v1.15.0` each exit 1 with an
# empty result directory. The contrasting case, measured and recorded here
# but deliberately *not* asserted, is a cut that lands on a value boundary
# with the document still well-formed: both binaries then exit **0** and
# report `rejected[0] error="unexpected EOF"` -- a rejected transaction, not
# a failed invocation. Treating those two as one behaviour is the error; the
# exit status distinguishes them.
assert_invocation_fails exit-status-truncated-rlp \
  --input.alloc="$P/alloc.json" --input.txs="$P/txs-truncated.rlp" \
  --state.fork=Osaka

rm -rf "$W"
echo "$passed $total"
