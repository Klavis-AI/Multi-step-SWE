#!/usr/bin/env bash
# Harbor verifier for one step of the IPv4-only -> IPv6-only migration.
#
# Every step's tests/ directory holds an identical copy of this file and of the
# harness; what differs between them is /tests/step.txt, which names the step,
# and the expectations composed for it. The expectations ship as
# expectations-<NN>/ and the Dockerfile renames them to /tests/expectations,
# the path this script reads (the Dockerfile says why).
#
# The shape is fixed: build the candidate tree, replay the recorded corpus
# against it, and combine the two scored families into the reward. Nothing here
# reads the candidate's own tests, and nothing the candidate can write is
# executed except its own build.
#
# `set -e` is deliberately not used. A verifier that exits early leaves no
# reward behind, and "the tree was so broken the verifier died" has to score
# zero rather than nothing.
set -uo pipefail

# Out of /app before anything runs. Harbor execs this script with the working
# directory set to /app -- the candidate's tree -- and `python3 -` (the reward
# below) and `python3 -c` (lib/no4.py's probe, run as root) put the working
# directory first on sys.path, so a /app/json.py would be imported as root by
# the first `import json`. Nothing here uses a relative path.
cd / || exit 1

LOGS=/logs/verifier
mkdir -p "$LOGS"
# Only root writes here. Harbor opens this directory to everyone just before
# the verifier starts, and every candidate process -- the build included, see
# build.py -- runs as uid 65534: a directory that uid can write is a reward it
# can replace after this script has written it. 0755 rather than 0700, because
# the host reads reward.json back as an ordinary user.
chmod 755 "$LOGS"

# Kill whatever uid 65534 still has running. The build is over by the time this
# is called, so anything left is a process the build started and did not wait
# for, and it does not get to run alongside the scenarios that read the build's
# output as evidence. The container has its own PID namespace.
#
# kill(-1) sent *as* uid 65534 reaches exactly the processes that uid may
# signal -- here, every candidate process and nothing of root's -- in one call,
# so a process that keeps forking is swept in the pass that finds it; and pgrep
# reads the real uid from /proc/<pid>/status, which a process that made itself
# non-dumpable (its /proc/<pid> then reads as root's) cannot hide. Looped until
# nothing is left.
reap_score_uid() {
    local n round
    n=$(pgrep -c -u 65534 2>/dev/null) || n=0
    for round in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -u 65534 >/dev/null 2>&1 || break
        setpriv --reuid 65534 --regid 65534 --clear-groups kill -9 -1 \
            >/dev/null 2>&1
        sleep 0.2
    done
    [ "$n" -gt 0 ] && echo "== reaped $n process(es) left running as uid 65534"
    pgrep -u 65534 >/dev/null 2>&1 \
        && echo "== uid 65534 still has processes after 10 rounds"
    return 0
}
REWARD="$LOGS/reward.json"
# Line 1 only: the rest of step.txt is a per-step size pad (step.txt says why).
STEP="$(sed -n 1p /tests/step.txt)"
SN="$(printf '%02d' "$STEP")"
JOBS="${VERIFIER_JOBS:-4}"

write_reward() { printf '{"reward": %s}\n' "$1" >"$REWARD"; }

# ---------------------------------------------------------------------------
# The deadline.
#
# Harbor throws a step's result away when its verifier outlives
# [steps.verifier] timeout_sec -- 3000 s at step 1, 3600 s after -- even with
# reward.json written, and then aborts every later step, so a tree that makes
# this script run long would lock in the mean of the steps before it. Nothing
# below is short enough on its own: two builds of up to 2 x 1800 s each
# (build.py), and 31 scenarios at up to 900 s each, four at a time.
#
# So the script runs itself again, as a child, under `timeout`, and this copy
# stays behind to finish the step whatever happens to it: 2700 s plus 30 s of
# grace is under step 1's 3000 s less the image build and artifact round trip
# harbor's timeout also covers. The golden runs take about 300 s. On expiry the
# child's process group gets TERM, then KILL; candidate processes outside that
# group are uid 65534 and go in the reap. A reward the child had finished
# writing is kept; anything else is 0.
VERIFY_BUDGET=2700
if [ -z "${HEDIS_VERIFY_INNER:-}" ]; then
    write_reward 0
    HEDIS_VERIFY_INNER=1 timeout -k 30 "$VERIFY_BUDGET" bash "${BASH_SOURCE[0]}"
    rc=$?
    reap_score_uid
    if ! grep -Eq '^\{"reward": ?[0-9.eE+-]+\}$' "$REWARD" 2>/dev/null; then
        echo "== verifier ended (status $rc) with no readable reward; reward 0"
        write_reward 0
    elif [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        echo "== verifier stopped at ${VERIFY_BUDGET}s; reward as last written: $(cat "$REWARD")"
    fi
    chmod 755 "$LOGS"
    chmod 644 "$REWARD" 2>/dev/null
    exit 0
fi

# From the first moment, so that a verifier killed for running long still
# reports a number.
write_reward 0
echo "== verifier for step $STEP"
# Which step's verifier actually ran, stamped where the job directory keeps it
# and before anything can fail. Every artifact this script writes carries the
# same number, so a step directory holding another step's number is a run that
# scored the wrong thing -- which is otherwise invisible, because an earlier
# step's verifier passes on a tree that went further than it asks.
date -u +"step $STEP verifier started %Y-%m-%dT%H:%M:%SZ" >"$LOGS/step$SN-verifier.txt"

# ---------------------------------------------------------------------------
# Fixture names.
#
# Docker rewrites /etc/hosts at container start, so this cannot be done in the
# image. Appended rather than replaced: the container's own name and the
# loopback entries above it are still needed.
cat /tests/fixtures/hosts >>/etc/hosts

# ---------------------------------------------------------------------------
# Set-id files. Every candidate process in this container runs as uid 65534.
# build.py's copies are lchown()ed to that uid, which clears any setuid or
# setgid bit they kept, but /app itself is the restored tree as it arrived: a
# root-owned set-id file there would run as root for any candidate process
# that reached it. Nothing after this reads /app except build.py's copy, which
# runs as root, so /app is stripped and locked to root.
find /app -xdev -type f -perm /6000 -exec chmod ug-s {} + 2>/dev/null
chmod 0700 /app 2>/dev/null

# ---------------------------------------------------------------------------
# The build.
#
# Into a copy, with `-save-temps=obj`, so the gates have preprocessed sources to
# read and the candidate's own src/ is left as the candidate left it. The copy
# is also what the behaviour scenarios run, so the tree is built exactly once.
echo "== building the candidate tree"
python3 /tests/harness/lib/build.py --src /app --dest /build --jobs "$(nproc)" \
    >"$LOGS/step$SN-build-summary.txt" 2>&1
build_rc=$?
cp -f /build/build.log "$LOGS/step$SN-build.log" 2>/dev/null
cp -f /build/build-report.json "$LOGS/step$SN-build-report.json" 2>/dev/null
sed -n '1,40p' "$LOGS/step$SN-build-summary.txt"

# The first build is over: nothing of the score uid runs past it (build.py
# reaps before it writes its report), and the copy becomes root's before the
# second build starts, so nothing that build runs can reach it either. Every
# source file the gates read as text is made identical to the untouched /app
# again: the build is the candidate's `make`, and what the gates are meant to
# read is the tree the candidate handed in, not one its recipe rewrote. What
# the build added is left as built. See freeze.py. Nothing the scenarios run
# needs to write into the copy.
reap_score_uid
python3 /tests/harness/lib/freeze.py --restore-from /app /build \
    >"$LOGS/step$SN-freeze.txt" 2>&1

# A second build, with plain `make` and nothing on the command line, which is
# how the project's own documentation says to build it. The build above names a
# flag there -- that is how the gates get preprocessed sources, and it is also,
# by itself, a test of something else: that a flag given that way still reaches
# every compilation. Neither build answers the other's question, so both are
# done and both are scored.
#
# Not fatal, and not gated on the first one succeeding: whether it worked is an
# observation, and a verifier that stopped here would score a tree that builds
# one way and not the other the same as a tree that builds neither.
echo "== building the candidate tree again, with plain make"
python3 /tests/harness/lib/build.py --src /app --dest /build-plain --plain \
    --jobs "$(nproc)" >"$LOGS/step$SN-build-plain-summary.txt" 2>&1
cp -f /build-plain/build.log "$LOGS/step$SN-build-plain.log" 2>/dev/null
sed -n '1,20p' "$LOGS/step$SN-build-plain-summary.txt"
export HARBOR_PLAIN_BUILD=/build-plain
reap_score_uid

# The second build becomes root's the same way. The gates read the sources,
# the `.i` files and the build reports out of both copies while the behaviour
# scenarios have the score uid's servers running, and a copy that uid can still
# write is evidence it can still change. Nothing of that uid is left running
# here (the reap above). Directories stay enterable and files readable.
python3 /tests/harness/lib/freeze.py --restore-from /app /build-plain \
    >>"$LOGS/step$SN-freeze.txt" 2>&1

if [ "$build_rc" -ne 0 ]; then
    # Every scored property downstream is a property of a program, and there are
    # no programs. Scoring the fragments that survive a failed build would pay
    # for a tree that does not compile.
    echo "== build failed (rc=$build_rc); reward 0"
    write_reward 0
    exit 0
fi

# ---------------------------------------------------------------------------
# The corpus.
mkdir -p /work
chmod 755 /work
HARBOR_GATE_BUILD=/build python3 /tests/harness/runner.py replay \
    --tree /build --expect /tests/expectations --step "$STEP" \
    --report "$LOGS/step$SN-report.json" --work /work --jobs "$JOBS" \
    2>&1 | tee "$LOGS/step$SN-replay.log"
replay_rc="${PIPESTATUS[0]}"

# Nothing of the candidate's outlives the corpus into the scoring.
reap_score_uid

if [ "$replay_rc" -ne 0 ] || [ ! -s "$LOGS/step$SN-report.json" ]; then
    echo "== replay did not produce a report (rc=$replay_rc); reward 0"
    write_reward 0
    exit 0
fi

# ---------------------------------------------------------------------------
# The reward.
#
#   reward = regression x (0.5 x behaviour progress + 0.5 x gate progress)
#
# The two families are weighted equally. They measure different things and a
# tree can be good at one and bad at the other in either direction: the
# behaviour corpus says the server still works, the gates say it was actually
# migrated, and neither on its own is the step.
#
# Only the *progress* observations earn: those in the scenarios recorded from
# this step's own golden tree (registry `flip_from` = this step) whose value
# differs both from the untouched baseline's and from the previous step's
# finished tree's. Everything else is regression, including the inherited
# corpus's must-flips, and regression scales the result rather than adding to
# it -- "this much of the step, of which you kept this much of what was already
# working". Roles are assigned when the expectations are composed; runner.py
# describes them.
#
# The corpus is cumulative, so by volume it is overwhelmingly regression. Paying
# for it directly would mean a tree that stopped after the previous step still
# collected most of this step's reward, and a tree that did nothing at all
# collected most of step 1's. Splitting it this way makes three numbers exact
# rather than approximate: the step's own solution scores 1, the previous step's
# scores 0, and an untouched tree scores 0.
#
# The reward written is all-or-nothing: 1 when that product reaches 1, and 0
# otherwise.
python3 -I - "$LOGS/step$SN-report.json" "$REWARD" <<'PY'
import json
import sys

report_path, reward_path = sys.argv[1], sys.argv[2]
report = json.load(open(report_path))
fam = report["family_totals"]

WEIGHT = {"behaviour": 0.5, "gate": 0.5}
progress = 0.0
scale = 0.0
for name, w in sorted(WEIGHT.items()):
    f = fam.get(name, {}).get("progress", {}).get("fraction")
    if f is None:          # this step has nothing for this family to earn on
        print("%-10s progress n/a" % name)
        continue
    scale += w
    progress += w * f
    print("%-10s progress %.4f  (weight %.2f)" % (name, f, w))
progress = progress / scale if scale else 0.0

r = report["regression_total"]["fraction"]
regression = 1.0 if r is None else r
print("%-10s %.4f" % ("regression", regression))

reward = max(0.0, min(1.0, regression * progress))
reward = 1.0 if reward >= 1.0 else 0.0

for name in sorted(report["scenarios"]):
    s = report["scenarios"][name]
    print("  %-24s %4d/%-4d  %-6s %s"
          % (name, s["matched"], s["inputs"], s["status"],
             ("misses: " + ", ".join(s["misses"][:6])) if s["misses"] else ""))
print("reward %.4f" % reward)
json.dump({"reward": round(reward, 6)}, open(reward_path, "w"))
PY

cat "$REWARD"
exit 0
