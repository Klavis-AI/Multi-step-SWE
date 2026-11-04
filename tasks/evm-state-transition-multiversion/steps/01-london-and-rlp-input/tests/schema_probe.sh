#!/usr/bin/env bash
# schema_probe.sh <evm-path> <probe-case-dir>
#
# The `result-schema-and-errors` group. Everything here is about the shape of
# the tool's output and its behaviour on bad input, not about fork semantics,
# so it is driven by direct invocations rather than a corpus.
#
# Prints one TSV line per probe to stderr and "<passed> <total>" to stdout.
set -u
source /tests/helpers.sh

EVM="$1"; CASE="$2"
W=$(mktemp -d /tmp/schema-XXXXXX); chmod 777 "$W"
passed=0; total=0

ok()   { passed=$((passed+1)); total=$((total+1)); printf 'CASE\tresult-schema-and-errors\t%s\tPASS\n' "$1" >&2; }
bad()  { total=$((total+1)); printf 'CASE\tresult-schema-and-errors\t%s\tFAIL\t%s\n' "$1" "$2" >&2; }

t8n() {
  local od="$1"; shift
  mkdir -p "$od"; chmod 777 "$od"
  run_unprivileged timeout 120 "$EVM" t8n "$@" \
      --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
      >"$od/stdout.txt" 2>"$od/stderr.txt"
}

BASE=(--input.alloc="$CASE/alloc.json" --input.env="$CASE/env.json"
      --input.txs="$CASE/txs.rlp" --state.chainid=1 --state.reward=-1)

# ---- 1. the result field set, by exact name ----------------------------------
t8n "$W/good" "${BASE[@]}" --state.fork=London
GOOD_RC=$?
if [ -f "$W/good/result.json" ]; then
  miss=$(python3 - "$W/good/result.json" <<'PY'
import json,sys
want = {"stateRoot","txRoot","receiptsRoot","logsHash","logsBloom","receipts","gasUsed"}
d = json.load(open(sys.argv[1]))
print(",".join(sorted(want - set(d))) or "-")
PY
)
  [ "$miss" = "-" ] && ok schema-required-fields || bad schema-required-fields "missing: $miss"

  # The pre-London tool spelled this `receiptRoot`. Accepting both would let a
  # candidate keep the old spelling and still score.
  if python3 -c "import json,sys;d=json.load(open(sys.argv[1]));sys.exit(0 if 'receiptRoot' not in d else 1)" "$W/good/result.json"; then
    ok schema-no-legacy-receiptroot
  else
    bad schema-no-legacy-receiptroot "result.json still carries the pre-London key 'receiptRoot'"
  fi

  # Top-level gasUsed must be the block total, not absent and not a copy of the
  # first receipt's own gas.
  if python3 - "$W/good/result.json" <<'PY'
import json,sys
d = json.load(open(sys.argv[1]))
r = d.get("receipts") or []
if "gasUsed" not in d:
    sys.exit(1)
if r and int(d["gasUsed"], 16) != int(r[-1]["cumulativeGasUsed"], 16):
    sys.exit(1)
PY
  then ok schema-gasused-is-block-total
  else bad schema-gasused-is-block-total "top-level gasUsed absent or != last receipt cumulativeGasUsed"
  fi

  # Hex digit case is not part of the contract (TEST_INTERFACE.md fixes the
  # 0x prefix and the width, not the spelling), so the hash and bloom shape
  # probes accept either case.
  for k in stateRoot txRoot receiptsRoot logsHash; do
    if python3 -c "
import json,sys,re
d=json.load(open(sys.argv[1]))
v=d.get(sys.argv[2],'')
sys.exit(0 if re.fullmatch(r'0x[0-9a-fA-F]{64}', v or '') else 1)" "$W/good/result.json" "$k"; then
      ok "schema-$k-is-32-byte-hash"
    else
      bad "schema-$k-is-32-byte-hash" "not a 0x-prefixed 32-byte hex hash"
    fi
  done

  # TEST_INTERFACE.md calls logsBloom a 256-byte bloom filter. Nothing else in
  # the suite looks at it, so without this a tool could emit "0x" and never be
  # noticed.
  if python3 -c "
import json,sys,re
d=json.load(open(sys.argv[1]))
sys.exit(0 if re.fullmatch(r'0x[0-9a-fA-F]{512}', d.get('logsBloom') or '') else 1)" "$W/good/result.json"; then
    ok schema-logsBloom-is-256-bytes
  else
    bad schema-logsBloom-is-256-bytes "logsBloom is not 256 hex bytes"
  fi

  # "receipts and rejected together account for every transaction in
  # --input.txs" -- counted against the input rather than against itself, so a
  # tool that silently drops a transaction is caught.
  if python3 - "$W/good/result.json" "$CASE/txs.rlp" <<'PY'
import json, sys

def items(b, i, end):
    """Yield the start offset of each RLP item in b[i:end]."""
    while i < end:
        yield i
        p = b[i]
        if p < 0x80:
            i += 1
        elif p < 0xb8:
            i += 1 + (p - 0x80)
        elif p < 0xc0:
            n = p - 0xb7
            i += 1 + n + int.from_bytes(b[i + 1:i + 1 + n], "big")
        elif p < 0xf8:
            i += 1 + (p - 0xc0)
        else:
            n = p - 0xf7
            i += 1 + n + int.from_bytes(b[i + 1:i + 1 + n], "big")

raw = bytes.fromhex(json.load(open(sys.argv[2]))[2:])
p = raw[0]
if p < 0xf8:
    start, end = 1, 1 + (p - 0xc0)
else:
    n = p - 0xf7
    start = 1 + n
    end = start + int.from_bytes(raw[1:1 + n], "big")
n_txs = sum(1 for _ in items(raw, start, end))

d = json.load(open(sys.argv[1]))
got = len(d.get("receipts") or []) + len(d.get("rejected") or [])
sys.exit(0 if got == n_txs and n_txs > 0 else 1)
PY
  then ok schema-receipts-plus-rejected-covers-input
  else bad schema-receipts-plus-rejected-covers-input \
           "len(receipts)+len(rejected) != number of transactions in --input.txs"
  fi
else
  for p in schema-required-fields schema-no-legacy-receiptroot schema-gasused-is-block-total \
           schema-stateRoot-is-32-byte-hash schema-txRoot-is-32-byte-hash \
           schema-receiptsRoot-is-32-byte-hash schema-logsHash-is-32-byte-hash \
           schema-logsBloom-is-256-bytes schema-receipts-plus-rejected-covers-input; do
    bad "$p" "no result.json produced for a valid input"
  done
fi

# ---- 1b. the empty block ------------------------------------------------------
# TEST_INTERFACE.md fixes what every field holds when no transaction is
# included. This is the one place the *empty-block* constants are asserted by
# value; the corpus groups also assert txRoot/receiptsRoot (the ten
# block-derived rlp-tx-input cases) and logsBloom (every case whose fixture
# supplies it) by value, but only for non-empty blocks, so a tool that
# special-cases "0xc0" wrongly is caught here and nowhere else.
EMPTY_TRIE=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
EMPTY_LOGS=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347
printf '"0xc0"' > "$W/empty.rlp"
t8n "$W/empty" --input.alloc="$CASE/alloc.json" --input.env="$CASE/env.json" \
    --input.txs="$W/empty.rlp" --state.chainid=1 --state.reward=-1 --state.fork=London
if [ -f "$W/empty/result.json" ]; then
  run_empty_check() {
    if python3 - "$W/empty/result.json" "$EMPTY_TRIE" "$EMPTY_LOGS" "$1" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
trie, logs, which = sys.argv[2], sys.argv[3], sys.argv[4]

def hexval(v):
    # Hex digit case is not part of the contract -- see the note on the shape
    # probes above, and t8ncheck.py's as_bytes(), which decodes rather than
    # compares. Comparing these constants as strings would fail a tool that
    # spells the same root in upper case, which no document forbids.
    return v.lower() if isinstance(v, str) else v

if which == "roots":
    ok = (hexval(d.get("txRoot")) == trie
          and hexval(d.get("receiptsRoot")) == trie)
elif which == "logs":
    lb = d.get("logsBloom") or ""
    ok = (hexval(d.get("logsHash")) == logs
          and len(lb) == 514 and lb.startswith("0x") and set(lb[2:]) == {"0"})
else:
    r = d.get("receipts")
    ok = r == [] and int(d.get("gasUsed") or "0x0", 16) == 0
sys.exit(0 if ok else 1)
PY
    then ok "schema-empty-block-$1"
    else bad "schema-empty-block-$1" "empty transaction list: $1 not as TEST_INTERFACE.md fixes them"
    fi
  }
  run_empty_check roots
  run_empty_check logs
  run_empty_check receipts
else
  for p in roots logs receipts; do
    bad "schema-empty-block-$p" "no result.json produced for an empty transaction list"
  done
fi

# ---- 2. bad input is refused, and refused cleanly ----------------------------
# A tool that exits 0 on garbage, or leaves a half-written result file behind,
# turns a broken run into a plausible-looking one.
printf 'not rlp at all' > "$W/garbage.rlp"
t8n "$W/e1" --input.alloc="$CASE/alloc.json" --input.env="$CASE/env.json" \
    --input.txs="$W/garbage.rlp" --state.chainid=1 --state.reward=-1 --state.fork=London
rc=$?
refused_rc "$rc" && ok errors-malformed-txs-nonzero-exit \
                 || bad errors-malformed-txs-nonzero-exit "expected a refusal, got exit $rc"
[ ! -f "$W/e1/result.json" ] && ok errors-malformed-txs-no-partial-output \
                || bad errors-malformed-txs-no-partial-output "left a result.json behind after failing"

# `instruction.md` names "a bad RLP payload" as its own non-zero cause, distinct
# from unparseable JSON: this file *is* valid JSON and *is* a `0x`-hex string,
# and the bytes it yields are still not a decodable RLP list (0xde is a list
# header claiming thirty payload bytes, with three present). A reader that
# skipped what would not decode, and carried on with the transactions it could
# read, would pass every other probe in this file.
#
# A refusal is only meaningful once the tool has been shown to accept the good
# input. The empty starter -- the v1.10.0 t8n this step begins from -- reads
# the JSON array form only and exits non-zero on *every* `.rlp` file, so
# without this control it collects both refusals below for free. The control is
# the section-1 run re-read as its own assertion: the same alloc, env and chain
# id, fork London, with a well-formed signed-transaction RLP list, must exit 0
# and write its result.
if [ "$GOOD_RC" -eq 0 ] && [ -f "$W/good/result.json" ]; then
  ok errors-well-formed-rlp-accepted
  RLP_LIVE=yes
else
  bad errors-well-formed-rlp-accepted \
      "a well-formed .rlp transaction document exited $GOOD_RC$([ -f "$W/good/result.json" ] && printf ' and left a result.json' || true)"
  RLP_LIVE=no
fi

if [ "$RLP_LIVE" != yes ]; then
  bad errors-bad-rlp-payload-nonzero-exit \
      "no positive control: a well-formed .rlp document was not accepted (see errors-well-formed-rlp-accepted)"
  bad errors-bad-rlp-payload-no-partial-output \
      "no positive control: a well-formed .rlp document was not accepted (see errors-well-formed-rlp-accepted)"
else
  printf '"0xdeadbeef"' > "$W/badrlp.rlp"
  t8n "$W/e5" --input.alloc="$CASE/alloc.json" --input.env="$CASE/env.json" \
      --input.txs="$W/badrlp.rlp" --state.chainid=1 --state.reward=-1 --state.fork=London
  rc=$?
  refused_rc "$rc" && ok errors-bad-rlp-payload-nonzero-exit \
                   || bad errors-bad-rlp-payload-nonzero-exit "expected a refusal of an undecodable RLP payload, got exit $rc"
  [ ! -f "$W/e5/result.json" ] && ok errors-bad-rlp-payload-no-partial-output \
                  || bad errors-bad-rlp-payload-no-partial-output "left a result.json behind after failing"
fi

t8n "$W/e2" "${BASE[@]}" --state.fork=NoSuchFork
rc=$?
refused_rc "$rc" && ok errors-unknown-fork-nonzero-exit \
                 || bad errors-unknown-fork-nonzero-exit "expected a refusal of an unknown --state.fork, got exit $rc"

t8n "$W/e3" --input.alloc="$W/does-not-exist.json" --input.env="$CASE/env.json" \
    --input.txs="$CASE/txs.rlp" --state.chainid=1 --state.reward=-1 --state.fork=London
rc=$?
refused_rc "$rc" && ok errors-missing-alloc-nonzero-exit \
                 || bad errors-missing-alloc-nonzero-exit "expected a refusal of a missing --input.alloc, got exit $rc"

printf '{"this": "is valid json but not an alloc"' > "$W/trunc.json"
t8n "$W/e4" --input.alloc="$W/trunc.json" --input.env="$CASE/env.json" \
    --input.txs="$CASE/txs.rlp" --state.chainid=1 --state.reward=-1 --state.fork=London
rc=$?
refused_rc "$rc" && ok errors-truncated-alloc-nonzero-exit \
                 || bad errors-truncated-alloc-nonzero-exit "expected a refusal of a truncated --input.alloc, got exit $rc"

rm -rf "$W"
echo "$passed $total"
