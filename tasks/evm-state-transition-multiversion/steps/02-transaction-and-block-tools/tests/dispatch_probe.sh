#!/usr/bin/env bash
# dispatch_probe.sh <evm-path> <t9n-case-dir> <b11r-case-dir> <t8n-case-dir>
#
# The `tool-dispatch-and-errors` group. Everything here is about the two new
# subcommands existing, the old one still existing, and what each does with
# input it cannot use -- not about fork semantics, so it is driven by direct
# invocations rather than a corpus.
#
# What is graded is *that* a bad input is refused and that no output document
# is left behind, never the exit code chosen or the sentence printed. The one
# code this step does fix is the boundary between the two kinds of failure:
# a tool-level failure (the file is unreadable, the fork is unknown) is not a
# result, while a transaction the rules reject *is* a result and belongs in
# the array as an entry carrying an error.
#
# Prints one TSV line per probe to stderr and "<passed> <total>" to stdout.
set -u
source /tests/helpers.sh

EVM="$1"; T9N_CASE="$2"; B11R_CASE="$3"; T8N_CASE="$4"
W=$(mktemp -d /tmp/dispatch-XXXXXX); chmod 777 "$W"
passed=0; total=0
G=tool-dispatch-and-errors

ok()  { passed=$((passed+1)); total=$((total+1)); printf 'CASE\t%s\t%s\tPASS\n' "$G" "$1" >&2; }
bad() { total=$((total+1)); printf 'CASE\t%s\t%s\tFAIL\t%s\n' "$G" "$1" "$2" >&2; }

# stdout of a t9n run, parsed as the result document it is meant to be.
is_json_array() {
  python3 -c 'import json,sys
d=json.load(open(sys.argv[1]))
sys.exit(0 if isinstance(d,list) else 1)' "$1" 2>/dev/null
}

# ---- 1. the three subcommands dispatch ---------------------------------------
for sub in t8n t9n b11r; do
  if run_unprivileged timeout 60 "$EVM" "$sub" --help >/dev/null 2>&1; then
    ok "dispatch-$sub"
  else
    bad "dispatch-$sub" "'evm $sub --help' did not exit 0"
  fi
done

# A subcommand the tool does not have must not be silently accepted, or a
# candidate could route every unknown name into a no-op that exits 0.
run_unprivileged timeout 60 "$EVM" definitely-not-a-subcommand >/dev/null 2>&1
[ $? -ne 0 ] && ok dispatch-unknown-refused \
              || bad dispatch-unknown-refused "an unknown subcommand exited 0"

# ---- 2. probe inputs ---------------------------------------------------------
# A real JSON transaction array, under a `.json` name, built from the probe
# seed's own `case.json`. The three subcommands are told apart by which
# `--input.txs` form each accepts, so the probes below need a document that is
# unambiguously the *other* form: valid JSON holding transaction entries, named
# so that no suffix rule could call it RLP. Deriving it from the seed keeps the
# probe self-contained inside the verifier build context, which cannot read a
# t8n corpus.
python3 - "$T9N_CASE/case.json" "$W/txs.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
json.dump(d.get("expect", []), open(sys.argv[2], "w"))
PY
printf '{' > "$W/bad-header.json"            # JSON syntax error, not a bad RLP body
printf '{"input": "0x00"' > "$W/truncated.json"   # JSON document cut short
printf '"0x80"' > "$W/notalist.rlp"          # one RLP string where one list was expected
printf '"0xdeadbeef"' > "$W/truncated.rlp"   # RLP header promises more bytes than exist
chmod 644 "$W"/*.rlp "$W"/*.json

# ---- 3. positive controls ----------------------------------------------------
# Every probe below asks whether a bad input is *refused*. A tool that does not
# have the subcommand at all refuses everything, so without a positive control
# each of those probes would pass for a candidate who implemented nothing --
# measured on the step-01 Oracle, which has no t9n and no b11r: 10 of these 14
# negative checks passed by absence alone. The control is the same invocation
# with a known-good input; if it does not produce the document, the matching
# negative probes are recorded as failures rather than credited.
run_unprivileged timeout 60 "$EVM" t9n --input.txs="$T9N_CASE/txs.rlp" \
    --state.fork=London --state.chainid=1 >"$W/control.t9n" 2>/dev/null
if is_json_array "$W/control.t9n"; then T9N_LIVE=yes; else T9N_LIVE=no; fi

mkdir -p "$W/control-b11r"; chmod 777 "$W/control-b11r"
run_unprivileged timeout 60 "$EVM" b11r --input.header="$B11R_CASE/header.json" \
    --input.txs="$B11R_CASE/txs.rlp" --input.ommers="$B11R_CASE/ommers.json" \
    --output.basedir="$W/control-b11r" --output.block=block.json >/dev/null 2>&1
if [ -s "$W/control-b11r/block.json" ]; then B11R_LIVE=yes; else B11R_LIVE=no; fi

mkdir -p "$W/control-t8n"; chmod 777 "$W/control-t8n"
run_unprivileged timeout 60 "$EVM" t8n --input.alloc="$T8N_CASE/alloc.json" \
    --input.env="$T8N_CASE/env.json" --input.txs="$T8N_CASE/txs.json" \
    --state.fork=London --state.chainid=1 --state.reward=-1 \
    --output.basedir="$W/control-t8n" --output.result=result.json \
    --output.alloc=allocout.json >/dev/null 2>&1
if [ -s "$W/control-t8n/result.json" ]; then T8N_LIVE=yes; else T8N_LIVE=no; fi

[ "$T9N_LIVE" = yes ]  && ok control-t9n-works  || bad control-t9n-works  "a known-good t9n run produced no result array"
[ "$B11R_LIVE" = yes ] && ok control-b11r-works || bad control-b11r-works "a known-good b11r run produced no block document"
[ "$T8N_LIVE" = yes ]  && ok control-t8n-works  || bad control-t8n-works  "a known-good t8n run produced no result document"

# ---- 4. t9n tool-level failures ----------------------------------------------
t9n_fail_probe() {
  local name="$1"; shift
  local out="$W/$name.out"
  if [ "$T9N_LIVE" != yes ]; then
    bad "$name" "no working t9n to probe: refusing bad input proves nothing"
    return
  fi
  run_unprivileged timeout 60 "$EVM" t9n "$@" >"$out" 2>"$W/$name.err"
  local rc=$?
  if killed_rc "$rc"; then
    bad "$name" "killed (exit $rc) rather than refusing: this measured load, not behaviour"
  elif [ $rc -eq 0 ]; then
    # Any zero exit is a failure here, as it is for `b11r` below: the contract
    # says `t9n` exits non-zero on a path it cannot read, and a tool that exits
    # 0 while printing something that is not a result array has still reported
    # success for an input it refused to grade.
    bad "$name" "exited 0 on input it cannot use; the run must be refused"
  else
    ok "$name"
  fi
}

t9n_fail_probe t9n-missing-input-file \
    --input.txs="$W/does-not-exist.rlp" --state.fork=London --state.chainid=1
t9n_fail_probe t9n-truncated-rlp \
    --input.txs="$W/truncated.rlp" --state.fork=London --state.chainid=1
# `t9n` reads the RLP form only, and only under a `.rlp` filename. A document
# that parses as a JSON transaction array is the *other* form, whatever its
# length, so no reading of the framing rule can make it usable here.
t9n_fail_probe t9n-json-array-refused \
    --input.txs="$W/txs.json" --state.fork=London --state.chainid=1
# One RLP string where a single RLP list is required: not a list at all, so the
# document's framing is broken under every reading of the rule -- unlike a
# well-formed outer list with bytes after it, which the contract now states
# explicitly either way.
t9n_fail_probe t9n-non-list-rlp \
    --input.txs="$W/notalist.rlp" --state.fork=London --state.chainid=1
t9n_fail_probe t9n-unknown-fork \
    --input.txs="$T9N_CASE/txs.rlp" --state.fork=Definitelynotafork --state.chainid=1

# ---- 5. t9n per-transaction rejection is a result, not a failure -------------
# Signing binds the chain id, so replaying the same list under a different one
# makes every transaction unrecoverable. That is the rules rejecting each
# transaction, so the tool still produces its array -- one entry per input,
# each carrying an error.
out="$W/chainid.out"
run_unprivileged timeout 60 "$EVM" t9n --input.txs="$T9N_CASE/txs.rlp" \
    --state.fork=London --state.chainid=987654 >"$out" 2>"$W/chainid.err"
n_want=$(python3 -c 'import json,sys;print(len(json.load(open(sys.argv[1]))["expect"]))' \
         "$T9N_CASE/case.json" 2>/dev/null || echo 0)
if python3 - "$out" "$n_want" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
assert isinstance(d, list), "not an array"
assert len(d) == int(sys.argv[2]), "%d entries, want %s" % (len(d), sys.argv[2])
for e in d:
    assert e.get("error"), "an entry reports no error"
    assert "address" not in e, "an entry recovered a sender it could not have"
PY
then ok t9n-wrong-chainid-is-per-tx
else bad t9n-wrong-chainid-is-per-tx "$(tail -1 "$W/chainid.err" 2>/dev/null | cut -c1-120)"
fi

# A clean run must leave stdout machine-readable on its own: no banner, no
# progress line, nothing but the document.
out="$W/clean.out"
run_unprivileged timeout 60 "$EVM" t9n --input.txs="$T9N_CASE/txs.rlp" \
    --state.fork=London --state.chainid=1 >"$out" 2>/dev/null
is_json_array "$out" && ok t9n-stdout-is-the-document \
                     || bad t9n-stdout-is-the-document "stdout did not parse as a JSON array"

# ---- 6. b11r tool-level failures leave no partial block ----------------------
b11r_fail_probe() {
  local name="$1"; shift
  local od="$W/$name"; mkdir -p "$od"; chmod 777 "$od"
  if [ "$B11R_LIVE" != yes ]; then
    bad "$name" "no working b11r to probe: refusing bad input proves nothing"
    return
  fi
  run_unprivileged timeout 60 "$EVM" b11r "$@" \
      --output.basedir="$od" --output.block=block.json >"$od/stdout.txt" 2>"$od/stderr.txt"
  local rc=$?
  if killed_rc "$rc"; then
    bad "$name" "killed (exit $rc) rather than refusing: this measured load, not behaviour"
  elif [ $rc -eq 0 ]; then
    bad "$name" "exited 0 on input it cannot assemble a block from"
  elif [ -f "$od/block.json" ]; then
    bad "$name" "refused the input but still wrote block.json"
  else
    ok "$name"
  fi
}

python3 - "$B11R_CASE/header.json" "$W/header-no-stateroot.json" <<'PY'
import json, sys
h = json.load(open(sys.argv[1])); h.pop("stateRoot", None)
json.dump(h, open(sys.argv[2], "w"))
PY
printf '["0xdeadbeef"]' > "$W/bad-ommers.json"
chmod 644 "$W"/header-no-stateroot.json "$W"/bad-ommers.json

b11r_fail_probe b11r-missing-header-file \
    --input.header="$W/does-not-exist.json" --input.txs="$B11R_CASE/txs.rlp"
b11r_fail_probe b11r-incomplete-header \
    --input.header="$W/header-no-stateroot.json" --input.txs="$B11R_CASE/txs.rlp"
b11r_fail_probe b11r-malformed-txs \
    --input.header="$B11R_CASE/header.json" --input.txs="$W/truncated.rlp"
b11r_fail_probe b11r-malformed-ommer \
    --input.header="$B11R_CASE/header.json" --input.txs="$B11R_CASE/txs.rlp" \
    --input.ommers="$W/bad-ommers.json"
# `b11r` reads the RLP form of `--input.txs` whatever the file is called, so a
# JSON transaction array is refused; and a header document that is not JSON at
# all is the unparseable-input case no other probe supplies (`"0xdeadbeef"` is
# valid JSON holding a broken RLP body, which is a different failure).
b11r_fail_probe b11r-json-array-txs \
    --input.header="$B11R_CASE/header.json" --input.txs="$W/txs.json" \
    --input.ommers="$B11R_CASE/ommers.json"
b11r_fail_probe b11r-unparseable-header \
    --input.header="$W/bad-header.json" --input.txs="$B11R_CASE/txs.rlp" \
    --input.ommers="$B11R_CASE/ommers.json"

# ---- 7. t8n refuses a malformed input document too ---------------------------
# `t8n` takes **either** `--input.txs` form and chooses by filename, so a JSON
# array under a `.json` name is valid input for it -- the control-t8n-works
# probe above is exactly that invocation on a known-good case, and it is what
# stops a t8n that refuses everything from passing the probe below. What is
# graded here is the other half of the exit-status rule at the one subcommand
# the two earlier steps own: a JSON document that runs out mid-value is a
# malformed input document, so non-zero and no result file.
t8n_fail_probe() {
  local name="$1"; shift
  local od="$W/$name"; mkdir -p "$od"; chmod 777 "$od"
  if [ "$T8N_LIVE" != yes ]; then
    bad "$name" "no working t8n to probe: refusing bad input proves nothing"
    return
  fi
  run_unprivileged timeout 60 "$EVM" t8n \
      --input.alloc="$T8N_CASE/alloc.json" --input.env="$T8N_CASE/env.json" \
      --state.fork=London --state.chainid=1 --state.reward=-1 \
      --output.basedir="$od" --output.result=result.json \
      --output.alloc=allocout.json "$@" >"$od/stdout.txt" 2>"$od/stderr.txt"
  local rc=$?
  if killed_rc "$rc"; then
    bad "$name" "killed (exit $rc) rather than refusing: this measured load, not behaviour"
  elif [ $rc -eq 0 ]; then
    bad "$name" "exited 0 on an input document it cannot read"
  elif [ -s "$od/result.json" ]; then
    bad "$name" "refused the input but still wrote a result document"
  else
    ok "$name"
  fi
}

t8n_fail_probe t8n-truncated-json-txs --input.txs="$W/truncated.json"

rm -rf "$W"
echo "$passed $total"
