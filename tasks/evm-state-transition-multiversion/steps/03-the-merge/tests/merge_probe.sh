#!/usr/bin/env bash
# merge_probe.sh <evm-path> <probe-input-dir> <group>
#
#   group = result-schema  -> the `merge-result-schema` group
#   group = env-errors     -> the `merge-env-errors` group
#
# Two of step 03's six new groups are about the *shape* of a run at
# `--state.fork=Merge` rather than about the state it computes: which fields
# `--output.result` carries, and which post-merge `--input.env` documents are
# well-formed. Neither is expressible as a state-test fixture -- a fixture
# states a post-state, not a schema and not an exit status -- so both are
# driven by direct invocations over hand-authored inputs.
#
# Every expectation comes from the specifications in the workspace, not from a
# running tool:
#
#   * `docs/fork/merge/eip-3675.md` sets the post-merge header `difficulty` to
#     `0` ("Replacing `difficulty` with `0`") and lists `difficulty != 0` among
#     the conditions that make a post-merge block invalid. So a post-merge env
#     stating a non-zero `currentDifficulty` is a malformed input document, and
#     the `currentDifficulty` the tool reports back is zero.
#   * `docs/fork/merge/eip-4399.md` requires `PREVRANDAO (0x44)` to return the
#     header's `mixHash`/`prevRandao` field, which `--input.env` spells
#     `currentRandom`. A post-merge env that omits it leaves the opcode with
#     nothing to return, so it too is malformed. A `currentRandom` of all
#     zeroes is a legal value and not an omission.
#   * `TEST_INTERFACE.md` fixes the always-present `--output.result` fields,
#     adds `currentDifficulty` from this fork onward, spells the receipts root
#     `receiptsRoot`, and fixes the exit-status rule: a transaction the fork
#     rejects is a result (exit 0, an entry in `rejected[]`), while a malformed
#     input document is a non-zero exit that leaves no partial result file.
#
# ---------------------------------------------------------------------------
# Positive controls
#
# Most of `env-errors` asks whether a bad input is *refused*. A tool that does
# not have the Merge fork at all refuses everything, so each refusal probe
# would otherwise pass for a candidate who implemented none of this step --
# measured on the step-02 Oracle (`evm-v1.10.13`), which exits 3 with
# `unsupported fork "Merge"` on every invocation here. Each group therefore
# runs a known-good Merge input first and, if that does not produce the
# document it should, records the dependent probes as failures rather than
# crediting them.
# ---------------------------------------------------------------------------
set -u
source /tests/helpers.sh

EVM="$1"; P="$2"; WHICH="$3"
W=$(mktemp -d /tmp/mergeprobe-XXXXXX); chmod 777 "$W"
passed=0; total=0
case "$WHICH" in
  result-schema) G=merge-result-schema ;;
  env-errors)    G=merge-env-errors ;;
  *) echo "0 0"; exit 0 ;;
esac

ok()  { passed=$((passed+1)); total=$((total+1)); printf 'CASE\t%s\t%s\tPASS\n' "$G" "$1" >&2; }
bad() { total=$((total+1)); printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$G" "$1" "$2" >&2; }

# t8n <tag> <env-file> <txs-file> <fork>; leaves the exit status in $RC.
#
# The fork is passed through exactly as written at the call site, in the graded
# vocabulary -- `Merge` for this step's fork, `London` for the one below it.
# Every invocation below uses the same spelling `run_group.sh` passes to the
# corpus groups, so a tool that answers one and not the other is charged in
# both places, which is the point: `TEST_INTERFACE.md` fixes the vocabulary,
# not this script. (A tree that reaches the fork through go-ethereum v1.10.x's
# own `Merged` entry has the step's source patch available; the host-side
# Oracle harness bridges the name rather than the script bending to it.)
RC=0
t8n() {
  local od="$W/$1" envf="$2" txf="$3" fork="$4"
  rm -rf "$od"; mkdir -p "$od"; chmod 777 "$od"
  run_unprivileged timeout 120 "$EVM" t8n \
      --input.alloc="$P/alloc.json" --input.env="$P/$envf" --input.txs="$P/$txf" \
      --state.fork="$fork" --state.chainid=1 --state.reward=-1 \
      --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
      >"$od/stdout.txt" 2>"$od/stderr.txt"
  RC=$?
}

# jq-free JSON predicates over a result document.
has_key()  { python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit(1)
sys.exit(0 if isinstance(d,dict) and sys.argv[2] in d else 1)' "$1" "$2" 2>/dev/null; }

# A quantity that is zero, however it is spelled: absent-as-null, "0x0", "0x00",
# 0, "". Anything else -- notably a proof-of-work difficulty -- is not zero.
is_zero_qty() { python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit(1)
v=d.get(sys.argv[2],"<absent>")
if v is None: sys.exit(0)
if isinstance(v,bool): sys.exit(1)
if isinstance(v,int): sys.exit(0 if v==0 else 1)
if isinstance(v,str):
    s=v.strip()
    if s=="": sys.exit(0)
    try: sys.exit(0 if int(s,16 if s.lower().startswith("0x") else 10)==0 else 1)
    except ValueError: sys.exit(1)
sys.exit(1)' "$1" "$2" 2>/dev/null; }

# The indexes in `rejected[]`, one per line. Tolerates bare integers.
rejected_idx() { python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit(1)
r=d.get("rejected") or []
out=[]
for it in r:
    if isinstance(it,bool): continue
    if isinstance(it,int): out.append(it)
    elif isinstance(it,dict) and it.get("index") is not None:
        try: out.append(int(str(it["index"]),0))
        except Exception: pass
print(",".join(str(i) for i in sorted(out)))' "$1" 2>/dev/null; }


# ===========================================================================
# merge-result-schema
# ===========================================================================
if [ "$WHICH" = result-schema ]; then

  # ---- control: a well-formed Merge run produces both documents -------------
  t8n merge env-merge.json txs-probe.json Merge
  if [ "$RC" -eq 0 ] && [ -f "$W/merge/result.json" ] && [ -f "$W/merge/allocout.json" ]; then
    LIVE=yes; ok schema-merge-run-succeeds
  else
    LIVE=no
    bad schema-merge-run-succeeds \
        "a well-formed --state.fork=Merge run exited $RC / wrote no result: $(head -c 140 "$W/merge/stderr.txt" 2>/dev/null | tr '\n' ' ')"
  fi

  # Every check below reads $W/merge/result.json, so without the control they
  # would all be vacuous on a tool that has no Merge fork.
  gate() { if [ "$LIVE" = yes ]; then return 0; fi
           bad "$1" "no Merge result document to inspect (see schema-merge-run-succeeds)"
           return 1; }

  # ---- 1. currentDifficulty is carried, from this fork onward ---------------
  # TEST_INTERFACE.md: `currentDifficulty` is present "from the fork that
  # defines" it, the Merge.
  if gate schema-currentdifficulty-present; then
    if has_key "$W/merge/result.json" currentDifficulty; then
      ok schema-currentdifficulty-present
    else
      bad schema-currentdifficulty-present "result.json has no currentDifficulty at Merge"
    fi
  fi

  # ---- 2. and it is zero ----------------------------------------------------
  # EIP-3675: "replacing the block header `difficulty` field with `0` constant".
  # There is no proof-of-work seal left to report a difficulty for.
  if gate schema-currentdifficulty-is-zero; then
    if is_zero_qty "$W/merge/result.json" currentDifficulty; then
      ok schema-currentdifficulty-is-zero
    else
      bad schema-currentdifficulty-is-zero \
          "post-merge currentDifficulty is not zero: $(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("currentDifficulty"))' "$W/merge/result.json" 2>/dev/null)"
    fi
  fi

  # ---- 3. the always-present fields are still all present at the new fork ---
  # A new fork is where a result schema quietly loses a field. TEST_INTERFACE.md
  # lists these as always present, so they are present at Merge too.
  if gate schema-required-fields-present; then
    missing=$(python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
req=["stateRoot","txRoot","receiptsRoot","logsHash","logsBloom","gasUsed","receipts"]
print(",".join(k for k in req if k not in d))' "$W/merge/result.json" 2>/dev/null)
    if [ -z "$missing" ]; then
      ok schema-required-fields-present
    else
      bad schema-required-fields-present "absent from result.json at Merge: $missing"
    fi
  fi

  # ---- 4. the receipts root keeps its documented spelling -------------------
  # TEST_INTERFACE.md spells it `receiptsRoot` and says explicitly: not
  # `receiptRoot`. Upstream's own struct field is `ReceiptRoot`, so this is a
  # spelling a carried-forward tree can lose.
  if gate schema-receiptsroot-spelling; then
    if has_key "$W/merge/result.json" receiptsRoot; then
      ok schema-receiptsroot-spelling
    else
      bad schema-receiptsroot-spelling "no receiptsRoot key at Merge (receiptRoot is not the graded spelling)"
    fi
  fi

  # ---- 5. a rejected transaction is a result, not a tool error --------------
  # TEST_INTERFACE.md: exit 0 "includes a run where every transaction was
  # rejected". Graded at Merge because that is the fork this step adds.
  t8n rejected env-merge.json txs-rejected.json Merge
  if [ "$RC" -ne 0 ]; then
    bad schema-rejected-tx-is-not-a-tool-error \
        "a transaction the fork rejects made the tool exit $RC"
  elif [ ! -f "$W/rejected/result.json" ]; then
    bad schema-rejected-tx-is-not-a-tool-error "exit 0 but no result.json"
  elif [ "$(rejected_idx "$W/rejected/result.json")" = "0" ]; then
    ok schema-rejected-tx-is-not-a-tool-error
  else
    bad schema-rejected-tx-is-not-a-tool-error \
        "rejected[] held [$(rejected_idx "$W/rejected/result.json")], expected index 0"
  fi

  # ---- 6. an empty block at Merge is still a complete result ----------------
  # No transactions is not an error, and the new field is not conditional on
  # there having been any.
  t8n empty env-merge.json txs-none.json Merge
  if [ "$RC" -ne 0 ] || [ ! -f "$W/empty/result.json" ]; then
    bad schema-empty-block-still-reports-currentdifficulty \
        "an empty post-merge block exited $RC / wrote no result"
  elif has_key "$W/empty/result.json" currentDifficulty \
       && is_zero_qty "$W/empty/result.json" currentDifficulty; then
    ok schema-empty-block-still-reports-currentdifficulty
  else
    bad schema-empty-block-still-reports-currentdifficulty \
        "currentDifficulty absent or non-zero for an empty post-merge block"
  fi

  # ---- 7. currentBaseFee is never echoed back into the result ---------------
  # The document above says it plainly: `currentBaseFee` is an *input* field.
  # It is the one input quantity whose name also appears in the header the
  # result describes, so it is the one a carried-forward tree is likeliest to
  # start echoing. Read against the same document step 1 already gated on.
  if gate schema-currentbasefee-not-echoed; then
    if has_key "$W/merge/result.json" currentBaseFee; then
      bad schema-currentbasefee-not-echoed \
          "result.json carries currentBaseFee, which is an --input.env field and not a result field"
    else
      ok schema-currentbasefee-not-echoed
    fi
  fi

  # ---- 8. receipts[] has one entry per *included* transaction ---------------
  # The always-present-fields check above only asks that `receipts` exists. A
  # transaction the fork rejects is left out of it, so `receipts` is the count
  # of the transactions that actually ran -- `txRoot`'s own arity, not the
  # input array's. Graded on the all-rejected run, where the two differ by one:
  # a tool that appends a receipt for the rejected transaction, or that copies
  # the input length, reports 1 here and is charged.
  if [ -f "$W/rejected/result.json" ]; then
    nrx=$(python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit(1)
r=d.get("receipts")
print(len(r) if isinstance(r,list) else "notalist")' "$W/rejected/result.json" 2>/dev/null)
    if [ "$nrx" = "0" ]; then
      ok schema-receipts-only-for-included-transactions
    else
      bad schema-receipts-only-for-included-transactions \
          "an all-rejected run reported receipts: $nrx, expected 0"
    fi
  else
    bad schema-receipts-only-for-included-transactions \
        "no result from the all-rejected run (re-run case 5)"
  fi

  # ---- 9. a rejected entry carries an index *and* an error ------------------
  # Documented as `{"index": N, "error": "..."}`. The index alone only says
  # *which* transaction was refused; without the error member the document
  # does not say why, and this step's whole subject is a new reason to refuse.
  # The wording is not graded -- the consensus rules fix that a transaction is
  # rejected, not the sentence an implementation prints -- only that a non-empty
  # string is there. In the same all-rejected document as step 8.
  if [ -f "$W/rejected/result.json" ]; then
    why=$(python3 -c 'import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: sys.exit(1)
r=d.get("rejected") or []
if not r: print("empty"); sys.exit(0)
it=r[0]
if not isinstance(it,dict): print("notanobject"); sys.exit(0)
e=it.get("error")
print("ok" if isinstance(e,str) and e.strip() else "no error member")' "$W/rejected/result.json" 2>/dev/null)
    if [ "$why" = "ok" ]; then
      ok schema-rejected-entry-carries-error-member
    else
      bad schema-rejected-entry-carries-error-member \
          "rejected[0] is $why (want {index, error} with a non-empty error)"
    fi
  else
    bad schema-rejected-entry-carries-error-member \
        "no result from the all-rejected run (re-run case 5)"
  fi
fi


# ===========================================================================
# merge-env-errors
# ===========================================================================
if [ "$WHICH" = env-errors ]; then

  # ---- control -------------------------------------------------------------
  t8n ok env-merge.json txs-probe.json Merge
  if [ "$RC" -eq 0 ] && [ -f "$W/ok/result.json" ]; then
    LIVE=yes; ok errors-well-formed-merge-env-accepted
  else
    LIVE=no
    bad errors-well-formed-merge-env-accepted \
        "a post-merge env with currentRandom and no difficulty exited $RC: $(head -c 140 "$W/ok/stderr.txt" 2>/dev/null | tr '\n' ' ')"
  fi

  # A refusal only means something once the tool has been shown to accept the
  # good input. Without this, "refuses everything" scores full marks.
  refuses() {
    local label="$1" tag="$2"
    if [ "$LIVE" != yes ]; then
      bad "$label" "no positive control: the tool refuses every Merge input (see errors-well-formed-merge-env-accepted)"
      return
    fi
    if refused_rc "$RC"; then ok "$label"
    elif killed_rc "$RC"; then bad "$label" "killed (exit $RC) rather than refusing: this measured load, not behaviour"
    else bad "$label" "exit 0 on a malformed post-merge env"; fi
  }
  no_partial() {
    local label="$1" tag="$2"
    if [ "$LIVE" != yes ]; then
      bad "$label" "no positive control (see errors-well-formed-merge-env-accepted)"
      return
    fi
    if [ ! -f "$W/$tag/result.json" ]; then ok "$label"
    else bad "$label" "left a partial result.json behind after a non-zero exit"; fi
  }

  # ---- 1. currentDifficulty stated as zero is accepted ----------------------
  # EIP-3675 puts `difficulty` = 0 in the post-merge header, so stating it is
  # correct, not an error. A tool that rejects any stated difficulty fails here.
  t8n zerodiff env-merge-zero-difficulty.json txs-probe.json Merge
  if [ "$RC" -eq 0 ] && [ -f "$W/zerodiff/result.json" ]; then
    ok errors-zero-currentdifficulty-accepted
  else
    bad errors-zero-currentdifficulty-accepted \
        "currentDifficulty = 0x0 at Merge exited $RC"
  fi

  # ---- 2. an omitted currentDifficulty is accepted --------------------------
  # `docs/fork/merge/README.md`: "`env.currentDifficulty` may be absent or zero
  # in post-merge blocks, and the tool must handle its absence without error."
  # (env-merge.json omits it; this is the control re-read as its own assertion.)
  if [ "$LIVE" = yes ]; then ok errors-omitted-currentdifficulty-accepted
  else bad errors-omitted-currentdifficulty-accepted \
           "an env omitting currentDifficulty was refused at Merge"; fi

  # ---- 3. a proof-of-work difficulty is refused -----------------------------
  # EIP-3675 lists `difficulty != 0` among the conditions that make a
  # post-merge block invalid. There is no seal to have produced it.
  t8n nonzerodiff env-merge-nonzero-difficulty.json txs-probe.json Merge
  refuses     errors-nonzero-currentdifficulty-refused nonzerodiff
  no_partial  errors-nonzero-currentdifficulty-no-partial-output nonzerodiff

  # ---- 4. a missing currentRandom is refused --------------------------------
  # EIP-4399: 0x44 "MUST return the value of the `mixHash` field". An env with
  # no `currentRandom` leaves the opcode with nothing to return, so the input
  # document is incomplete rather than merely unusual.
  t8n norandom env-merge-no-random.json txs-probe.json Merge
  refuses     errors-missing-currentrandom-refused norandom
  no_partial  errors-missing-currentrandom-no-partial-output norandom

  # ---- 5. a zero currentRandom is a value, not an omission ------------------
  # The RANDAO mix can legitimately be zero. A tool that tests for truthiness
  # rather than presence refuses this and fails here while passing 4.
  t8n zerorandom env-merge-zero-random.json txs-probe.json Merge
  if [ "$RC" -eq 0 ] && [ -f "$W/zerorandom/result.json" ]; then
    ok errors-zero-currentrandom-accepted
  else
    bad errors-zero-currentrandom-accepted \
        "an all-zero currentRandom was treated as missing (exit $RC)"
  fi

  # ---- 6. the requirement belongs to the fork, not to the tool --------------
  # The same env shape that is malformed at Merge -- a difficulty, no randao --
  # is exactly what a London block looks like, and must keep working. This is
  # the regression guard on "a fork that stops being accepted is a regression
  # the verifier scores against you".
  t8n london env-london.json txs-probe.json London
  if [ "$RC" -eq 0 ] && [ -f "$W/london/result.json" ]; then
    ok errors-london-env-unaffected
  else
    bad errors-london-env-unaffected \
        "requiring currentRandom at London too: a pre-merge env exited $RC"
  fi

  # ---- 7. a fork name the tool does not know is a non-zero exit -------------
  # From the exit-status rule: a fork name the tool does not know makes the
  # *input* malformed. Graded one step below `Merged`, in the same vocabulary
  # the Merge case is spelled in, so a tool that answers only the literal
  # string it was handed and refuses the rest of the ladder is charged here.
  t8n unknowngfork env-merge.json txs-probe.json Paris
  refuses    errors-unknown-fork-name-refused unknowngfork
  no_partial errors-unknown-fork-name-no-partial-output unknowngfork

  # ---- 8. unparseable JSON in an input document is a non-zero exit ----------
  # The document is cut off mid-object: well-formed enough to be read as JSON
  # at all, truncated enough that no reader can accept it.
  t8n truncated env-truncated.json txs-probe.json Merge
  refuses    errors-truncated-env-document-refused truncated
  no_partial errors-truncated-env-document-no-partial-output truncated

  # ---- 9. a value that is not hex is a malformed document -------------------
  # `currentRandom` spelled `0xzz`: a JSON string, a non-empty one, and not a
  # quantity at all. Distinct from 8 -- the document is complete and
  # well-formed as JSON, and only a field value is unusable -- so a tool that
  # validates presence but not the value is charged here. (Note the value has
  # to be *unparseable*, not merely oddly sized: `0x08` is a legal two-digit
  # spelling and is accepted, as it must be.)
  t8n badhex env-badhex.json txs-probe.json Merge
  refuses    errors-unparseable-quantity-refused badhex
  no_partial errors-unparseable-quantity-no-partial-output badhex

  # ---- 10. a bad RLP payload is a non-zero exit -----------------------------
  # An element that is not a decodable transaction envelope. At `t8n` an
  # element the tool cannot decode leaves it nothing to compute -- unlike at
  # `t9n`, where the same element is one of the findings the run reports.
  t8n badrlp env-merge.json txs-bad.rlp Merge
  refuses    errors-bad-rlp-payload-refused badrlp
  no_partial errors-bad-rlp-payload-no-partial-output badrlp
fi

rm -rf "$W"
echo "$passed $total"
