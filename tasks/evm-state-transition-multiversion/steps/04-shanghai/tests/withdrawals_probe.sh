#!/usr/bin/env bash
# withdrawals_probe.sh <evm-path> <probe-input-dir>
#
# The `withdrawals-root-and-schema` group. EIP-4895 is a *block-level* change:
# `withdrawals` is a new `--input.env` array and `withdrawalsRoot` a new
# `--output.result` field, and the credits happen outside any transaction. The
# vendored state tests cannot express any of that, so this group is driven by
# direct invocations over hand-authored inputs rather than by a fixture corpus.
#
# Every expected value is derived from the EIP, not from a running tool:
#
#   * the roots come from `wdroot.py`, an independent implementation of
#     EIP-4895's `compute_trie_root_from_indexed_data` built on the vendored
#     pure-Python keccak. It was self-tested at authoring time against the
#     `withdrawalsRoot` recorded in all 246 withdrawals blockchain-test fixture
#     headers on disk -- fixture data, never tool output -- and the values it
#     produced are frozen into `probe/expect.json`.
#   * the balances come from "the `amount` is given in units of Gwei so a
#     conversion to units of wei must be performed": credit = amount * 1e9.
#   * the pre-withdrawal `BALANCE` reading comes from "the `withdrawals` ...
#     are processed **after** any user-level transactions are applied".
#
# Prints one TSV line per probe to stderr and "<passed> <total>" to stdout.
# 19 probes. Partial credit is real here as it is in every other group: `ok`
# and `bad` count one assertion each, so a run that satisfies most of them
# scores the fraction it satisfied rather than nothing.
set -u
source /tests/helpers.sh

EVM="$1"; P="$2"
GROUP=withdrawals-root-and-schema
W=$(mktemp -d /tmp/wdprobe-XXXXXX); chmod 777 "$W"
passed=0; total=0

ok()  { passed=$((passed+1)); total=$((total+1)); printf 'CASE\t%s\t%s\tPASS\n' "$GROUP" "$1" >&2; }
bad() { total=$((total+1)); printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$GROUP" "$1" "$2" >&2; }

# t8n <outdir> <env-file> <txs-file> <fork>
t8n() {
  local od="$W/$1" envf="$2" txf="$3" fork="$4"
  mkdir -p "$od"; chmod 777 "$od"
  run_unprivileged timeout 120 "$EVM" t8n \
      --input.alloc="$P/alloc.json" --input.env="$P/$envf" --input.txs="$P/$txf" \
      --state.fork="$fork" --state.chainid=1 --state.reward=-1 \
      --output.basedir="$od" --output.result=result.json --output.alloc=allocout.json \
      >"$od/stdout.txt" 2>"$od/stderr.txt"
}

# check_scenario <outdir> <expect-key> <label>
#
# Asserts the recorded `withdrawalsRoot` and every recorded post-withdrawal
# balance for one run. `withdrawalsRoot` is compared as bytes, balances as
# quantities, so "0x0a" and "0xA" and 10 all compare equal.
check_scenario() {
  local od="$W/$1" key="$2" label="$3" out
  if [ ! -f "$od/result.json" ] || [ ! -f "$od/allocout.json" ]; then
    bad "$label-root"     "no output written (exit was non-zero, or the files are missing)"
    bad "$label-balances" "no output written (exit was non-zero, or the files are missing)"
    return
  fi
  out=$(python3 - "$P/expect.json" "$key" "$od/result.json" "$od/allocout.json" <<'PY'
import json, sys
exp_all, key, rpath, apath = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

def qty(v):
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

def hsh(v):
    if not isinstance(v, str):
        return None
    s = v.strip().lower()
    return s[2:] if s.startswith("0x") else s

exp = json.load(open(exp_all))[key]
res = json.load(open(rpath))
alloc = json.load(open(apath))
alloc = {k.lower(): v for k, v in alloc.items()}

got = res.get("withdrawalsRoot", "<absent>")
if hsh(got) == hsh(exp["root"]):
    print("ROOT\tPASS\t-")
else:
    print("ROOT\tFAIL\twithdrawalsRoot=%s want %s" % (got, exp["root"]))

diffs = []
for addr, want in (exp.get("balances") or {}).items():
    have = qty((alloc.get(addr.lower()) or {}).get("balance", "0x0"))
    if have != qty(want):
        diffs.append("%s balance=%s want %s" % (addr, hex(have or 0), want))
print("BAL\t%s\t%s" % ("FAIL" if diffs else "PASS", "; ".join(diffs) or "-"))
PY
) || out=""
  local rline bline
  rline=$(printf '%s\n' "$out" | awk -F'\t' '$1=="ROOT"{print $2"\t"$3}')
  bline=$(printf '%s\n' "$out" | awk -F'\t' '$1=="BAL"{print $2"\t"$3}')
  case "$rline" in PASS*) ok "$label-root" ;; *) bad "$label-root" "${rline#*	}" ;; esac
  case "$bline" in PASS*) ok "$label-balances" ;; *) bad "$label-balances" "${bline#*	}" ;; esac
}

# ---- 1. the empty list has the empty-trie root -------------------------------
# keccak(rlp("")), the root every empty Merkle-Patricia trie has. A tool that
# omits `withdrawalsRoot` when the list is empty, or invents a zero hash, is
# wrong: the field commits to the list, and the commitment to an empty list is
# not nothing.
t8n empty env-empty.json txs-none.json Shanghai
check_scenario empty empty wd-empty

# ---- 2. one withdrawal: gwei -> wei ------------------------------------------
# 1e9 gwei credited onto a pre-balance of 0x11 must read 1e18 + 0x11 wei. A
# tool that credits the raw `amount` is off by a factor of a billion and this
# is where that shows.
t8n single env-single.json txs-none.json Shanghai
check_scenario single single wd-single

# ---- 3. six withdrawals: branch nodes, a zero amount, a repeated recipient ---
# Six entries put a branch node in the trie rather than a lone leaf, so the
# root exercises real trie construction. One `amount` is zero (it still
# occupies a slot in the trie) and one recipient appears three times (the
# credits accumulate).
t8n many env-many.json txs-none.json Shanghai
check_scenario many many wd-many

# ---- 4. the trie is keyed by list position, not by the `index` field ---------
# EIP-4895 keys the trie "by index in the list of withdrawals". The `index`
# fields in these lists descend, so they are not their own positions, and
# reversing the list must therefore move the root while leaving every credited
# balance identical. A tool that keyed on the withdrawal's own `index` field,
# or that sorted the list first, produces the same root for both orders and
# fails exactly one of these two checks.
t8n reordered env-reordered.json txs-none.json Shanghai
check_scenario reordered reordered wd-reordered
if python3 -c "
import json,sys
e=json.load(open(sys.argv[1]))
sys.exit(0 if e['many']['root'].lower()!=e['reordered']['root'].lower() else 1)" \
      "$P/expect.json" \
   && [ -f "$W/many/result.json" ] && [ -f "$W/reordered/result.json" ] \
   && python3 -c "
import json,sys
a=json.load(open(sys.argv[1])).get('withdrawalsRoot')
b=json.load(open(sys.argv[2])).get('withdrawalsRoot')
sys.exit(0 if a and b and a.lower()!=b.lower() else 1)" \
      "$W/many/result.json" "$W/reordered/result.json"; then
  ok wd-order-changes-root
else
  bad wd-order-changes-root "reversing the withdrawals list left withdrawalsRoot unchanged"
fi

# ---- 5. withdrawals are applied after the transactions -----------------------
# The probe contract reads the recipient's BALANCE into slot 0. The recipient
# is also the target of a withdrawal in the same block, so the slot must hold
# the *pre*-withdrawal balance: "the `withdrawals` in an execution payload are
# processed **after** any user-level transactions are applied."
t8n order env-order.json txs-probe.json Shanghai
check_scenario order order wd-order
# This check needs a positive precondition or it cannot fail on its own: a tool
# that does not implement EIP-4895 at all never credits the recipient, so the
# in-transaction BALANCE trivially equals the pre-withdrawal value and an
# ordering-only assertion would pass. All three of the following must hold:
#   (a) the probe read the *pre*-withdrawal balance -- the ordering claim;
#   (b) the recipient's final balance is the *post*-withdrawal value, i.e. the
#       credit really happened in this very run;
#   (c) the two differ, so the run is discriminating at all.
if [ -f "$W/order/allocout.json" ] && python3 - "$P/expect.json" "$W/order/allocout.json" <<'PY'
import json, sys
exp = json.load(open(sys.argv[1]))
want = int(exp["order"]["probe_slot0"], 16)
probe = exp["addresses"]["probe"].lower()
rcpt_key = exp["addresses"]["rcptA"]
post = int(str(exp["order"]["balances"][rcpt_key]), 16)
alloc = {k.lower(): v for k, v in json.load(open(sys.argv[2])).items()}
st = (alloc.get(probe) or {}).get("storage") or {}
got = 0
for k, v in st.items():
    if int(k, 16) == 0:
        got = int(v, 16)
have = int(str((alloc.get(rcpt_key.lower()) or {}).get("balance", "0x0")), 16)
sys.exit(0 if (got == want and have == post and want != post) else 1)
PY
then
  ok wd-applied-after-transactions
else
  bad wd-applied-after-transactions \
      "the in-transaction BALANCE must be the pre-withdrawal value AND the final balance the post-withdrawal value"
fi

# ---- 6. the field appears at Shanghai and not before -------------------------
# TEST_INTERFACE.md: `withdrawalsRoot` is "present from the fork that defines
# them", Shanghai. Emitting it at London would be a schema regression for every
# earlier fork the tool is still graded at.
if [ -f "$W/empty/result.json" ] && python3 -c "
import json,sys
sys.exit(0 if 'withdrawalsRoot' in json.load(open(sys.argv[1])) else 1)" \
      "$W/empty/result.json"; then
  ok schema-withdrawalsroot-present-at-shanghai
else
  bad schema-withdrawalsroot-present-at-shanghai "result.json has no withdrawalsRoot at Shanghai"
fi

t8n london env-london.json txs-none.json London
if [ ! -f "$W/london/result.json" ]; then
  bad schema-withdrawalsroot-absent-at-london "no result.json produced for a valid London input"
elif python3 -c "
import json,sys
sys.exit(0 if 'withdrawalsRoot' not in json.load(open(sys.argv[1])) else 1)" \
      "$W/london/result.json"; then
  ok schema-withdrawalsroot-absent-at-london
else
  bad schema-withdrawalsroot-absent-at-london "result.json carries withdrawalsRoot at London"
fi

# ---- 6b. the fields TEST_INTERFACE.md calls "always present" ------------------
# The `--output.result` schema says stateRoot, txRoot, receiptsRoot, logsHash,
# logsBloom, gasUsed and receipts are present in every result document. The
# empty-withdrawals run has no transactions at all, which is the case where a
# tool is most tempted to omit them.
if [ -f "$W/empty/result.json" ] && python3 -c "
import json,sys
r=json.load(open(sys.argv[1]))
want=('stateRoot','txRoot','receiptsRoot','logsHash','logsBloom','gasUsed','receipts')
missing=[k for k in want if k not in r]
sys.stderr.write((', '.join(missing) or '-'))
sys.exit(1 if missing else 0)" "$W/empty/result.json" 2>"$W/schema.err"; then
  ok schema-result-always-present
else
  bad schema-result-always-present "missing: $(tr -d '\n' <"$W/schema.err" 2>/dev/null)"
fi

# `receipts` carries one entry per *included* transaction: none for the empty
# run, one for the ordering run. Both directions, so a tool that always emits an
# empty list and a tool that always emits one entry each fail one half.
if [ -f "$W/empty/result.json" ] && [ -f "$W/order/result.json" ] && python3 -c "
import json,sys
a=json.load(open(sys.argv[1])).get('receipts')
b=json.load(open(sys.argv[2])).get('receipts')
sys.exit(0 if isinstance(a,list) and isinstance(b,list) and len(a)==0 and len(b)==1 else 1)" \
      "$W/empty/result.json" "$W/order/result.json"; then
  ok schema-receipts-one-per-transaction
else
  bad schema-receipts-one-per-transaction "receipts is not [] for 0 transactions and one entry for 1"
fi

# TEST_INTERFACE.md: `rejected` is present only when it is non-empty. Neither of
# these runs rejects anything, so neither result may carry the key.
if [ -f "$W/empty/result.json" ] && [ -f "$W/order/result.json" ] && python3 -c "
import json,sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2]))
sys.exit(0 if 'rejected' not in a and 'rejected' not in b else 1)" \
      "$W/empty/result.json" "$W/order/result.json"; then
  ok schema-rejected-absent-when-empty
else
  bad schema-rejected-absent-when-empty "rejected is present on a run that rejected nothing"
fi

# ---- 7. a malformed withdrawals record is refused, and refused cleanly -------
# The address in this env is 19 bytes. TEST_INTERFACE.md: a malformed input
# document is a non-zero exit, and "on a non-zero exit the tool must not leave
# a partial --output.result file behind".
t8n malformed env-malformed.json txs-none.json Shanghai
rc=$?
if refused_rc "$rc"; then
  ok errors-malformed-withdrawal-nonzero-exit
else
  bad errors-malformed-withdrawal-nonzero-exit "expected a refusal of a 19-byte withdrawal address, got exit $rc"
fi
if [ ! -f "$W/malformed/result.json" ]; then
  ok errors-malformed-withdrawal-no-partial-output
else
  bad errors-malformed-withdrawal-no-partial-output "left a result.json behind after failing"
fi

rm -rf "$W"
echo "$passed $total"
