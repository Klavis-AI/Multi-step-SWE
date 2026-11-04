#!/usr/bin/env bash
# blobgas_probe.sh <evm-path> <probe-input-dir>
#
# The `eip4844-blobgas-and-precompile` group. EIP-4844 is the one Cancun EIP
# that is not purely an EVM change: it adds two *header* fields that t8n has to
# emit (`blobGasUsed`, `currentExcessBlobGas`), a second gas market whose fee is
# burned rather than paid to the coinbase, a validity rule on
# `maxFeePerBlobGas`, and a precompile. The vendored `eip4844_blobs` state tests
# exercise the transaction type -- that is the `eip4844-blob-transactions`
# group -- but a state test pins only `post.hash`, so it cannot assert either
# new result field and cannot separate a burned fee from a paid one. It does
# reach the precompile: 32 of that group's 74 cases come from
# test_point_evaluation_precompile.py (26) and
# test_point_evaluation_precompile_gas.py (6). What a `post.hash` cannot say is
# *which* of the precompile's rules was broken, so the scenarios here drive its
# four rejection paths and its 64-byte return value one at a time. This group is
# therefore driven by direct invocations over hand-authored inputs.
#
# Every expected value comes from `workdir/docs/fork/cancun/eip-4844.md`, not
# from running a tool. See the header of the generator that produced
# probe4844/, and `expect.json`'s per-scenario `derivation` string, which
# records the arithmetic for each one:
#
#   * `blobGasUsed` from `get_total_blob_gas` = `GAS_PER_BLOB * len(hashes)`;
#   * `currentExcessBlobGas` from `calc_excess_blob_gas(parent)`, on both sides
#     of the `TARGET_BLOB_GAS_PER_BLOCK` branch;
#   * the sender's balance from "the actual `blob_fee` ... is deducted from the
#     sender balance before transaction execution and burned", with the blob
#     base fee from `fake_exponential`, and the coinbase's balance from the
#     same sentence -- burned means nobody receives it;
#   * rejection from "assert tx.max_fee_per_blob_gas >=
#     get_base_fee_per_blob_gas(block.header)", probed at the exact boundary;
#   * `BLOBHASH` from "replaces it on the stack with
#     `tx.blob_versioned_hashes[index]` ... and otherwise with a zeroed bytes32";
#   * the precompile's 64-byte return value from the `point_evaluation_precompile`
#     pseudocode, and its four rejection paths from that function's three
#     `assert`s plus "MUST reject non-canonical field elements";
#   * the three blob-transaction validity rules from the "Execution layer
#     validation" block -- `assert len(tx.blob_versioned_hashes) > 0`,
#     `assert h[0] == VERSIONED_HASH_VERSION_KZG` and
#     `assert blob_gas_used <= MAX_BLOB_GAS_PER_BLOCK`.
#
# The three checks after the scenario loop are about the *interface* rather than
# about Cancun: TEST_INTERFACE.md says a malformed input document is a non-zero
# exit with no partial `--output.result` behind it, and says that distinction is
# scored. The fourth, `blob-tx-must-not-be-a-create`, is the EIP's "the field
# `to` MUST NOT be `nil`" rule; it accepts either refusal layer, because the EIP
# fixes that such a transaction does not execute and does not fix whether a tool
# discovers that while decoding the input or while validating the transaction.
#
# The two probe contracts write a 0x42 sentinel into slot 3. Without it a tool
# that never executed the code at all would be indistinguishable from one whose
# precompile call correctly failed, since both leave the other slots zero.
#
# Prints one TSV line per check to stderr and "<passed> <total>" to stdout.
set -u
source /tests/helpers.sh

EVM="$1"; P="$2"
GROUP=eip4844-blobgas-and-precompile
W=$(mktemp -d /tmp/blobprobe-XXXXXX); chmod 777 "$W"
passed=0; total=0

ok()  { passed=$((passed+1)); total=$((total+1)); printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$1" >&2; }
bad() { total=$((total+1)); printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$1" "$2" >&2; }

SCENARIOS=$(python3 -c "
import json,sys
print(' '.join(sorted(json.load(open(sys.argv[1]))['scenarios'])))" "$P/expect.json" 2>/dev/null)
if [ -z "${SCENARIOS:-}" ]; then
    echo "0 0"
    printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "probe-inputs" "expect.json unreadable" >&2
    exit 0
fi

for s in $SCENARIOS; do
    od="$W/$s"; mkdir -p "$od"; chmod 777 "$od"
    fork=$(python3 -c "
import json,sys
print(json.load(open(sys.argv[1]))['scenarios'][sys.argv[2]].get('fork','Cancun'))" \
        "$P/expect.json" "$s")
    run_unprivileged timeout 120 "$EVM" t8n \
        --input.alloc="$P/alloc.json" \
        --input.env="$P/env-$s.json" \
        --input.txs="$P/txs-$s.json" \
        --state.fork="$fork" --state.chainid=1 --state.reward=-1 \
        --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
        >"$od/stdout.txt" 2>"$od/stderr.txt"

    # One line per assertion the scenario makes, so a group that fails says
    # which rule it broke rather than only that it broke one.
    out=$(python3 - "$P/expect.json" "$s" "$od/result.json" "$od/allocout.json" <<'PY'
import json, sys

expect_path, key, rpath, apath = sys.argv[1:5]


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


def load(p):
    try:
        with open(p) as fh:
            d = json.load(fh)
        return d if isinstance(d, dict) else None
    except Exception:
        return None


spec = json.load(open(expect_path))["scenarios"][key]
res = load(rpath)
alloc = load(apath)
alloc = {k.lower(): v for k, v in (alloc or {}).items()}


def emit(name, okflag, detail):
    print("%s\t%s\t%s" % (name, "PASS" if okflag else "FAIL", detail or "-"))


if res is None:
    for field in sorted(spec.get("result") or {}):
        emit("%s-result-%s" % (key, field), False, "no result.json produced")
    if "rejected" in spec:
        emit("%s-rejected" % key, False, "no result.json produced")
    if spec.get("balances"):
        emit("%s-balances" % key, False, "no result.json produced")
    if spec.get("storage"):
        emit("%s-storage" % key, False, "no result.json produced")
    sys.exit(0)

# ---- result.json scalar fields -------------------------------------------
for field, want in sorted((spec.get("result") or {}).items()):
    if field not in res:
        emit("%s-result-%s" % (key, field), False, "result.json has no %r" % field)
        continue
    got = qty(res.get(field))
    emit("%s-result-%s" % (key, field), got == qty(want),
         "%s=%s want %s" % (field, res.get(field), want))

# ---- rejected -------------------------------------------------------------
if "rejected" in spec:
    raw = res.get("rejected") or []
    got = sorted(
        r if isinstance(r, int) and not isinstance(r, bool) else qty((r or {}).get("index"))
        for r in raw if isinstance(r, (int, dict))
    )
    got = [g for g in got if g is not None]
    want = sorted(qty(x) for x in spec["rejected"])
    emit("%s-rejected" % key, got == want, "rejected=%s want %s" % (got, want))

# ---- balances -------------------------------------------------------------
if spec.get("balances"):
    diffs = []
    for addr, want in sorted(spec["balances"].items()):
        have = qty((alloc.get(addr.lower()) or {}).get("balance", "0x0")) or 0
        if have != qty(want):
            diffs.append("%s=%s want %s" % (addr, hex(have), want))
    emit("%s-balances" % key, not diffs, "; ".join(diffs))

# ---- named storage slots --------------------------------------------------
if spec.get("storage"):
    diffs = []
    for addr, slots in sorted(spec["storage"].items()):
        st = (alloc.get(addr.lower()) or {}).get("storage") or {}
        st = {qty(k): qty(v) for k, v in st.items()}
        for slot, want in sorted(slots.items()):
            have = st.get(qty(slot), 0) or 0
            if have != qty(want):
                diffs.append("%s[%s]=%s want %s" % (addr[:10], slot, hex(have), want))
    emit("%s-storage" % key, not diffs, "; ".join(diffs[:6]))
PY
) || out=""

    if [ -z "$out" ]; then
        bad "$s" "the scenario checker produced no output"
        continue
    fi
    while IFS=$'\t' read -r name verdict detail; do
        [ -n "$name" ] || continue
        if [ "$verdict" = PASS ]; then ok "$name"; else bad "$name" "$detail"; fi
    done <<< "$out"
done

# The two result fields EIP-4844 adds to the header must not appear at a fork
# that predates them: `blobGasUsed` and `excess_blob_gas` are a Cancun header
# extension, and TEST_INTERFACE.md lists both as "present from the fork that
# defines them". A tool that emits them at Shanghai has regressed the schema of
# every earlier fork it is still graded at. The env here carries no blob field
# of any kind, so this asks about the fork and not about echoing an input.
od="$W/shanghai"; mkdir -p "$od"; chmod 777 "$od"
run_unprivileged timeout 120 "$EVM" t8n \
    --input.alloc="$P/alloc.json" --input.env="$P/env-shanghai-schema.json" \
    --input.txs="$P/txs-shanghai-schema.json" \
    --state.fork=Shanghai --state.chainid=1 --state.reward=-1 \
    --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
    >"$od/stdout.txt" 2>"$od/stderr.txt"
if [ ! -f "$od/result.json" ]; then
    bad schema-blob-fields-absent-at-shanghai "no result.json produced for a valid Shanghai input"
elif python3 -c "
import json,sys
r = json.load(open(sys.argv[1]))
sys.exit(0 if 'blobGasUsed' not in r and 'currentExcessBlobGas' not in r else 1)" \
      "$od/result.json"; then
    ok schema-blob-fields-absent-at-shanghai
else
    bad schema-blob-fields-absent-at-shanghai \
        "result.json carries blobGasUsed or currentExcessBlobGas at Shanghai"
fi


# --- interface rules that no scenario can state --------------------------
# Each of these asks the tool to *refuse* something, and a tool that refuses
# everything would collect them all for free -- the step-04 binary does not
# accept `--state.fork=Cancun` at all, so every invocation below fails on it
# whether or not the input is the malformed one. A refusal is therefore only
# credited on top of a positive control: the same alloc, env and transaction
# with the defect removed, which must run.
CONTROL_OK=0
od="$W/interface-control"; mkdir -p "$od"; chmod 777 "$od"
run_unprivileged timeout 120 "$EVM" t8n \
    --input.alloc="$P/alloc.json" --input.env="$P/env-one-blob.json" \
    --input.txs="$P/txs-one-blob.json" \
    --state.fork=Cancun --state.chainid=1 --state.reward=-1 \
    --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
    >"$od/stdout.txt" 2>"$od/stderr.txt"
rc=$?
if [ "$rc" -eq 0 ] && [ -f "$od/result.json" ]; then
    ok interface-control-clean-input-runs
    CONTROL_OK=1
else
    bad interface-control-clean-input-runs \
        "the undefected input did not run (exit $rc); the refusal checks below are not meaningful"
fi

# A blob transaction in the create form. eip-4844.md: the field `to` MUST NOT
# be `nil`. The EIP fixes that such a transaction does not execute; it does not
# fix at which layer a tool notices, so a non-zero exit with no result and a
# result document that rejects the transaction both count, and only acceptance
# fails.
if [ "$CONTROL_OK" -ne 1 ]; then
    bad blob-tx-must-not-be-a-create "control failed"
else
    od="$W/bad-create"; mkdir -p "$od"; chmod 777 "$od"
    run_unprivileged timeout 120 "$EVM" t8n \
        --input.alloc="$P/alloc.json" --input.env="$P/env-one-blob.json" \
        --input.txs="$P/bad-create-txs.json" \
        --state.fork=Cancun --state.chainid=1 --state.reward=-1 \
        --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
        >"$od/stdout.txt" 2>"$od/stderr.txt"
    rc=$?
    if refused_rc "$rc" && [ ! -f "$od/result.json" ]; then
        ok blob-tx-must-not-be-a-create
    elif [ -f "$od/result.json" ] && python3 -c "
import json,sys
r = json.load(open(sys.argv[1]))
sys.exit(0 if (r.get('rejected') or []) else 1)" "$od/result.json"; then
        ok blob-tx-must-not-be-a-create
    else
        bad blob-tx-must-not-be-a-create \
            "a type-3 transaction with no 'to' was accepted (exit $rc)"
    fi
fi

# TEST_INTERFACE.md: a malformed *input document* -- unparseable JSON, a bad RLP
# payload, a fork name the tool does not know -- is a non-zero exit, and the
# tool must not leave a partial `--output.result` behind. One check per input
# file the tool reads, plus the fork name.
check_malformed() {
    local name="$1" env="$2" txs="$3" fork="$4"
    local od rc
    if [ "$CONTROL_OK" -ne 1 ]; then
        bad "$name" "control failed"
        return
    fi
    od="$W/$name"; mkdir -p "$od"; chmod 777 "$od"
    run_unprivileged timeout 120 "$EVM" t8n \
        --input.alloc="$P/alloc.json" --input.env="$env" --input.txs="$txs" \
        --state.fork="$fork" --state.chainid=1 --state.reward=-1 \
        --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
        >"$od/stdout.txt" 2>"$od/stderr.txt"
    rc=$?
    if killed_rc "$rc"; then
        bad "$name" "killed (exit $rc) rather than refusing: this measured load, not behaviour"
    elif [ "$rc" -eq 0 ]; then
        bad "$name" "exit 0 on a malformed input document"
    elif [ -f "$od/result.json" ]; then
        bad "$name" "left a partial result.json behind after a non-zero exit"
    else
        ok "$name"
    fi
}
check_malformed malformed-env-is-a-nonzero-exit \
    "$P/bad-env-unparseable.json" "$P/txs-one-blob.json" Cancun
check_malformed malformed-txs-is-a-nonzero-exit \
    "$P/env-one-blob.json" "$P/bad-txs-unparseable.rlp" Cancun
check_malformed unknown-fork-is-a-nonzero-exit \
    "$P/env-one-blob.json" "$P/txs-one-blob.json" NoSuchFork

rm -rf "$W"
echo "$passed $total"
