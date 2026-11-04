#!/usr/bin/env bash
# Grade one step: rebuild the candidate from source and run this step's corpus.
set -u
source /tests/helpers.sh

LOG=/logs/verifier

# score_and_report: turn /logs/verifier/cases.json into the step's reward.
#
# The reward is the PRODUCT of two families:
#   reward = T1 x T2
#   T1 (milestone)  = fraction of THIS step's own fork cases that pass -- the newest fork in
#                     this corpus (Constantinople and ConstantinopleFix share one activation
#                     rank, so step 06's two new forks are both T1), plus the unknown-fork
#                     negative CLI cases that test this step's own behaviour.
#   T2 (inherited)  = fraction of the earlier-fork (regression) cases that pass.
# A tree that implements only the previous fork passes T2 but scores T1 = 0, so its reward is
# 0. Step 01 (Frontier) has no earlier fork, so T2 is absent and reward = T1 alone.
#
# Score over the whole corpus even if the run was cut short: a case with no result counts as a
# failure (it is absent from cases.json["results"]), so a hanging candidate is still graded.
# The scorer's Python runs from / and isolated (-I), so nothing in /app or in any user site
# directory is on its import path. cases.jsonl (root-only /tests) gives each case's fork;
# cases.json (written by run_corpus.py) gives which cases passed. Fails closed: any read error,
# or a milestone family with no cases, scores 0.
score_and_report() {
    cd / || true
    local reward
    reward=$(python3 -I - <<'PY'
import json, sys
# Canonical hard-fork order. Constantinople, its bugfix (ConstantinopleFix / Petersburg) and
# the difficulty-bomb-only forks that share an activation keep one rank, so a step's whole new
# milestone lands in T1 together.
RANK = {
    "Frontier": 0, "Homestead": 1, "EIP150": 2, "EIP158": 3, "Byzantium": 4,
    "Constantinople": 5, "ConstantinopleFix": 5, "Petersburg": 5,
    "Istanbul": 6, "MuirGlacier": 6, "Berlin": 7, "London": 8,
    "ArrowGlacier": 8, "GrayGlacier": 8, "Paris": 9, "Merge": 9,
    "Shanghai": 10, "Cancun": 11, "Prague": 12, "Osaka": 13,
}
try:
    recs = [json.loads(l) for l in open("/tests/corpus/cases.jsonl")]
except Exception:
    print("ERR"); sys.exit(0)
if not recs:
    print("ERR"); sys.exit(0)
id2fork = {r["id"]: r.get("fork") for r in recs}
known = [RANK[r.get("fork")] for r in recs if r.get("fork") in RANK]
milestone = max(known) if known else None
# A real earlier fork present means this step has inherited cases (T2). Step 01 has none.
has_inherited = any((r.get("fork") in RANK and RANK[r.get("fork")] < milestone) for r in recs)
def is_t1(fork):
    # No inherited cases (step 01): the whole corpus is this step's milestone -> T1 alone.
    # Otherwise T1 is exactly the newest-fork (milestone) cases; earlier forks AND the
    # constant unknown-fork negative cases (CLI behaviour a previous-step tree also passes)
    # are T2, so a previous-step tree scores T1 = 0 and therefore reward 0.
    if not has_inherited:
        return True
    return fork in RANK and milestone is not None and RANK[fork] == milestone
t1_total = sum(1 for r in recs if is_t1(r.get("fork")))
t2_total = len(recs) - t1_total
okids = set()
try:
    data = json.load(open("/logs/verifier/cases.json"))
    for r in data.get("results", []):
        if r.get("ok"):
            okids.add(r.get("id"))
except Exception:
    print("ERR"); sys.exit(0)
t1_pass = sum(1 for i in okids if i in id2fork and is_t1(id2fork[i]))
t2_pass = sum(1 for i in okids if i in id2fork and not is_t1(id2fork[i]))
if t1_total <= 0:
    print("ERR"); sys.exit(0)   # a step with no milestone cases cannot be measured
t1 = t1_pass / t1_total
t2 = (t2_pass / t2_total) if t2_total > 0 else 1.0
sys.stderr.write("T1 %d/%d  T2 %d/%d\n" % (t1_pass, t1_total, t2_pass, t2_total))
print("%.6f" % (t1 * t2))
PY
)
    case "$reward" in
        ERR|''|*[!0-9.]*)
            echo "scorer could not compute the T1xT2 reward; corpus.log tail follows" >> "$LOG/build.log"
            tail -c 4000 "$LOG/corpus.log" >> "$LOG/build.log" 2>/dev/null
            report 0 ;;
    esac
    echo "scored T1xT2 reward=$reward" >> "$LOG/corpus.log"
    report "$reward"
}

# Outer pass: lock the reward directory, write a provisional zero, then run the graded
# pass below under one wall-clock bound for the whole step.  Harbor discards a verifier
# that overruns `[steps.verifier] timeout_sec` (3480 s) even when reward.json exists, and
# aborts every later step, so the bound has to hold whatever the candidate does.  The
# graded pass always ends in `report`, which exits 0; any other ending means it was cut
# off, and this pass sweeps the candidate account and scores what was recorded.
if [ "${1:-}" != "--graded" ]; then
    cd / || true
    harden_reward_dir
    # Write a provisional score before doing anything that can be killed from outside.
    # Harbor aborts every remaining step when a verifier produces no reward file, so an
    # OOM-kill part-way through grading step k would forfeit steps k+1..14 as well.  A zero
    # that the real score overwrites costs nothing and bounds the damage to the one step
    # that actually failed.
    printf '{"reward":0.000000}\n' > "$LOG/reward.json"
    : > "$LOG/build.log"
    VERIFY_T0=$(date +%s) timeout -k 10 3210 bash /tests/test.sh --graded
    rc=$?
    sweep_candidate
    [ "$rc" -eq 0 ] && { chmod 755 "$LOG" 2>/dev/null; exit 0; }
    echo "graded pass ended with status $rc before reporting; scoring what was recorded" >> "$LOG/build.log"
    score_and_report
fi

# ---- graded pass ----------------------------------------------------------------------
# Every component's cap is the smaller of what it wants and what is left of the step's own
# budget, so the caps cannot sum past it however they are edited.  STEP_BUDGET equals
# `[steps.verifier] timeout_sec`.  RESERVE ends every phase by the outer pass's
# 3210-second bound, which still fits the 1,500-second build and the 1,700-second sweep
# the instruction promises; the outer pass's kill grace, sweep and scoring then keep the
# whole step under timeout_sec - 240 (3240 s).
T0=${VERIFY_T0:-$(date +%s)}
STEP_BUDGET=3480
RESERVE=270

budget() {
    local left=$(( STEP_BUDGET - RESERVE - ( $(date +%s) - T0 ) ))
    [ "$left" -lt 10 ] && left=10
    if [ "$1" -lt "$left" ]; then echo "$1"; else echo "$left"; fi
}

if [ ! -f /app/evm/Makefile ]; then
    echo "no Makefile at /app/evm/Makefile" >> "$LOG/build.log"
    report 0
fi

# The graded binary must be produced by `make` from the sources in /app/evm:
# drop build products and any committed object files, archives or executables,
# anywhere under /app -- a prebuilt program kept beside the sources is not one they describe.
# These walk a tree the candidate controls, so they are bounded like everything else, and
# a path travels NUL-terminated (-print0, read -d ""): any other byte, a newline included,
# can be part of a name the candidate chose, and a split name is a path root then acts on
# that the walk never found.
rm -f /app/evm/evm
timeout -k 10 "$(budget 300)" \
    find /app -type f \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.so.*' \) -delete 2>/dev/null
timeout -k 10 "$(budget 600)" bash -c '
{ find /app -type f -size +64c -print0 2>/dev/null
  for d in /tmp /var/tmp /dev/shm /run/lock /dev/mqueue; do
      [ -d "$d" ] && find "$d" -xdev -type f -size +64c -print0 2>/dev/null
  done
} | while IFS= read -r -d "" f; do
    case "$(file -b "$f" 2>/dev/null)" in
        ELF*|*"ar archive"*) rm -f "$f" ;;
    esac
done'

# Build and run everything as an unprivileged user that cannot read /tests.
timeout -k 10 "$(budget 300)" chown -R "$CAND_UID:$CAND_UID" /app 2>/dev/null
cd /app/evm || report 0
chmod 1777 /tmp 2>/dev/null
timeout -k 10 "$(budget 1500)" setpriv --reuid="$CAND_UID" --regid="$CAND_UID" --clear-groups \
    env HOME=/tmp TMPDIR=/tmp PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    make > "$LOG/build.log" 2>&1
rc=$?
# Nothing the build started outlives it: whatever it left running is killed before the
# binary is inspected or scored.
sweep_candidate
cd / || true
tail -c 20000 "$LOG/build.log" > "$LOG/build.tail.log" 2>/dev/null
if [ $rc -ne 0 ] || [ ! -x /app/evm/evm ]; then
    echo "make exited $rc; /app/evm/evm present: $([ -x /app/evm/evm ] && echo yes || echo no)" >> "$LOG/build.log"
    report 0
fi
case "$(file -b /app/evm/evm 2>/dev/null)" in
    ELF*) ;;
    *) echo "/app/evm/evm is not an ELF executable: $(file -b /app/evm/evm 2>/dev/null)" >> "$LOG/build.log"
       report 0 ;;
esac

# The graded binary has to be the one this step's sources describe.  A Go runtime in it
# means `make` emitted something built by a Go toolchain -- there is no Go toolchain in
# either image, so it was carried in or fetched, and it is not what is being asked for.
has_go_runtime() {
    readelf -SW "$1" 2>/dev/null | grep -qE '\.gopclntab|\.go\.buildinfo|\.note\.go\.buildid' \
        || grep -aqE 'go1\.[0-9]+(\.[0-9]+)?|runtime\.gopanic|golang\.org/x/' "$1" 2>/dev/null
}
# Bounded like the other walks over candidate bytes, and failing closed: a check that
# could not finish is not a check that passed.
timeout -k 10 "$(budget 300)" bash -c "$(declare -f has_go_runtime); has_go_runtime /app/evm/evm"
case $? in
    1) ;;
    0) echo "/app/evm/evm carries a Go runtime" >> "$LOG/build.log"
       report 0 ;;
    *) echo "/app/evm/evm could not be inspected for a Go runtime" >> "$LOG/build.log"
       report 0 ;;
esac
# The same rule over everything else the build left behind. The entry binary is not all
# that runs: a Go-built program kept elsewhere in the tree, or written out by `make` into a
# temporary directory, would do the work the entry binary only hands off. The pre-build
# sweep above removed every committed executable under /app, so any ELF present now was
# produced while `make` ran. The scan roots cover every directory the candidate uid can
# write: /app plus the world-writable scratch dirs the base image exposes (/tmp, /var/tmp,
# /dev/shm, and /run/lock and /dev/mqueue, both 1777 in debian:bookworm-slim). Bounded and
# failing closed like the check above: a walk that never printed its completion marker did
# not finish, and scores 0. Paths are NUL-terminated, as in the sweep above.
go_scan=$(timeout -k 10 "$(budget 600)" bash -c "$(declare -f has_go_runtime)"'
for d in /app /tmp /var/tmp /dev/shm /run/lock /dev/mqueue; do
    [ -d "$d" ] && find "$d" -xdev -type f -size +64c -print0 2>/dev/null
done | while IFS= read -r -d "" f; do
    case "$(file -b "$f" 2>/dev/null)" in
        ELF*) if has_go_runtime "$f"; then printf "GO-RUNTIME %s\n" "$f"; fi ;;
    esac
done
echo SCAN-COMPLETE')
case "$go_scan" in
    *SCAN-COMPLETE*) ;;
    *) echo "the tree could not be scanned for Go-built programs" >> "$LOG/build.log"
       report 0 ;;
esac
case "$go_scan" in
    *GO-RUNTIME*) echo "a Go-built program is present after the build:" >> "$LOG/build.log"
                  printf '%s\n' "$go_scan" | grep '^GO-RUNTIME ' | head -5 >> "$LOG/build.log"
                  report 0 ;;
esac
chmod a+rx /app/evm/evm 2>/dev/null

# The scorer itself is written in Python and runs as root; the candidate runs as an
# unprivileged user, and must not be able to hand the actual work to another interpreter.
# Taking the execute bit away from every interpreter in the image for anyone but root
# leaves the scorer working and closes that route.
for interp in python3 python3.11 python3.12 python2 perl node nodejs ruby php lua tclsh; do
    p=$(command -v "$interp" 2>/dev/null) || continue
    chmod 0750 "$p" 2>/dev/null || true
done

# JOBS x MEM_LIMIT_MB must stay under the container's memory_mb (16384): the ceiling
# only helps if a runaway case dies on its own rlimit before the kernel reaches for the
# whole container.  7 x 1536 MiB leaves headroom for the scorer itself.
JOBS=7 CASE_TIMEOUT=90 MEM_LIMIT_MB=1536 SANDBOX_UID=$CAND_UID \
    timeout -k 10 "$(budget 1700)" python3 -I /tests/run_corpus.py /tests/corpus /app/evm/evm --json "$LOG/cases.json" \
    > "$LOG/corpus.log" 2>&1
sweep_candidate
cat "$LOG/corpus.log" || true

score_and_report
