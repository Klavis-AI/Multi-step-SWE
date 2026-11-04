#!/usr/bin/env bash
# Replay the recorded behaviour suite against the candidate's artifact.
#
# Per-step copy: every step's tests/ holds an identical copy of this file;
# keep them in sync.
#
# Builds the fixture site three times -- the same seeded repositories under
# three different etc/gerrit.config files -- starts one instance of the
# artifact over each, drives every recorded case through real git, ssh and curl
# clients, and compares what came back with what the untouched baseline said.
#
# Every expectation in expected.jsonl was recorded twice against the pristine
# tree and kept only where the two recordings agreed, so this file is glue and
# the baseline is the oracle.
#
# What the suite reaches, measured once with JaCoCo over all three profiles:
# 76.5% of lines in the classes this deployment actually loads -- 1711 of 2238
# lines across 116 classes -- against a bar of 75%.
#
# Two compiled types are reached by no fixture, and not because the fixtures
# are thin. com.google.gerrit.httpd.CookieBase64 and
# com.google.gerrit.util.ssl.BlindSSLSocketFactory are referenced by nothing in
# the pinned tree: grep -rn over every .java and every XML in it finds no
# caller outside the two files themselves, so they are orphan code the 2011
# fork left behind and no fixture can reach them, because nothing in the
# program calls them. Writing one would mean first writing a caller, which is
# changing the program to suit the recorder.
#
# Deleting either file is caught, by the source-file and declared-type
# floors in each step's score.py. Stripping the declared API out of either one
# is caught where a step asks for it, by step 02's orphan-types-intact, which
# floors the number of methods each file still declares at the baseline's own
# four and thirteen. Gutting a method body -- keeping the signature and
# returning null -- is caught by nothing, here or in any score.py, and is
# accepted: neither type is on a path this replay drives or a path the
# application takes, so emptying one wins a candidate nothing it could be
# scored for.
#
# Writes /score/out/behaviour.json  {"total":N,"passed":M,...}
# and /logs/verifier/behaviour-fails.json, which the candidate never sees.
set -u
source /tests/helpers.sh

EXPECT=/tests/expected.jsonl
# Optional, per step, never synced between steps: the enumerated observations
# this step's target libraries are documented to change. Absent means "this
# step changes nothing observable", which is the right answer for step 1.
MUSTFLIP=/tests/must-flip.jsonl
CASES=/tests/cases
TOTAL=$(grep -c . "$EXPECT" 2>/dev/null || echo 0)

write_zero() {                      # write_zero <why>
    record behaviour "0/$TOTAL ($1)"
    say "behaviour 0/$TOTAL ($1)"
    printf '{"total":%s,"passed":0,"score":0.0,"why":"%s","cases":[]}\n' \
        "$TOTAL" "$1" > /score/out/behaviour.json
    exit 0
}

ART=$(cat /score/out/artifact.path 2>/dev/null || true)
[ -n "$ART" ] && [ -s "$ART" ] || write_zero "no-artifact"

# ---------------------------------------------------------------------------
# The fixture site, generated here rather than shipped as a tarball: a git
# repository committed into the verifier image would carry that image's git
# version in its reflog and its index, and the object ids have to be identical
# to the ones the recording saw. mksite.sh pins the clock, the identity and the
# default branch, so generating it twice gives the same show-ref -- which is
# checked below, not assumed.
# ---------------------------------------------------------------------------
: > /score/out/server.pids
mkdir -p /score/tmp
# The sites are the unprivileged user's (mksite.sh builds them as that user and
# the servers write their logs there), so they live under /score/work. The
# names /score/site-<p> that the probes and score.py read are root's links to
# them, which candidate code can neither move nor repoint.
for p in main alt ssl; do
    rm -rf "/score/site-$p" "/score/wk-$p" \
           "/score/work/site-$p" "/score/work/wk-$p"
    ln -s "/score/work/site-$p" "/score/site-$p"
    ln -s "/score/work/wk-$p" "/score/wk-$p"
done

say "sites"
for p in main alt ssl; do
    run_unprivileged bash /tests/mksite.sh "/score/work/site-$p" "/score/work/wk-$p" \
        /tests/keys "$p" > "/score/log/site-$p.log" 2>&1 \
        || { tail -30 "/score/log/site-$p.log" >&2; write_zero "site-$p-failed"; }
done

# The fingerprint of the generated fixtures, so a suite that fails wholesale
# can be told apart from a site that came out different. Recorded, not
# compared: the comparison that matters is the one over the observations.
record site.fingerprint \
    "$(cd /score/site-main/repos && find . -name 'packed-refs' -o -name 'HEAD' \
        | sort | xargs cat 2>/dev/null | sha256sum | cut -c1-16)"

# Installed before the first launch, not after the last: a run that gives up
# because the second instance never bound still has the first one holding port
# 8080, and the next thing to look at that port would be a later verifier.
stop_servers() {
    while read -r pid; do
        [ -n "$pid" ] || continue
        kill "$pid" 2>/dev/null || true
    done < /score/out/server.pids
    sleep 3
    while read -r pid; do
        [ -n "$pid" ] || continue
        kill -9 "$pid" 2>/dev/null || true
    done < /score/out/server.pids
}
trap stop_servers EXIT

# One profile that will not bind is not a zero. Each instance answers its own
# slice of the suite -- the alt profile's second host key, the ssl profile's
# redirects -- and every case that never names it is answerable without it.
# Giving up on the first failure would discard all 428 observations over one
# config the candidate may not have touched, and a tree that works would score
# zero behaviour.
#
# All three failing is still a short circuit, and has to be: a completely dead
# artifact would otherwise spend 428 cases collecting connect timeouts, and
# that does not fit inside this step's verifier budget. "None of the three came
# up" is the guard that keeps that path fast.
#
# `stop_servers` is trapped above rather than here, and it reads the pid file
# rather than anything counted below, so a partially-successful launch set is
# still fully reaped: every instance that did bind appended its pid before the
# one that failed, and the trap kills all of them on the way out.
say "launch x3"
launched=0
main_up=0

try_launch() {                      # try_launch <profile> <http> <ssh>
    if bash /tests/launch.sh "$ART" "/score/work/site-$1" "$2" "$3" "$1"; then
        launched=$((launched + 1))
        return 0
    fi
    record "launch.$1" "failed"
    say "launch-$1 never came up; the suite runs over the profiles that did"
    return 1
}

try_launch main 8080 29418 && main_up=1
try_launch alt  8081 29428
try_launch ssl  8082 29438
record launch.profiles "$launched/3"
[ "$launched" -gt 0 ] || write_zero "launch-none-of-three"

# The main profile declares a second listen address; a server that bound only
# the first would pass every case that never names it, so wait for both here
# rather than discovering it one case at a time. Skipped when main is one of
# the profiles that did not come up, since there is then nothing to wait for.
if [ "$main_up" = 1 ]; then
    for i in $(seq 1 60); do nc -z 127.0.0.1 29419 2>/dev/null && break; sleep 1; done
fi

say "drive $TOTAL cases"
# Bounded: the driver talks to the candidate's server. It writes where the
# unprivileged user can; root takes its record over once the servers are gone.
# -s -E: HOME is the candidate build's, so no user site-packages and no
# PYTHON* variables reach the driver.
# The clients' git config and HOME are root's, prepared here: the servers run
# as the driver's user and the candidate's build had /tmp, so neither may hold
# a file the clients read (drive.py, git_env).
rm -rf /score/drive
mkdir -p /score/drive/home/.ssh
python3 -I -c 'import sys; sys.path.insert(0, "/tests"); import drive
open(sys.argv[1], "w").write(drive.GITCONFIG_TEXT)' /score/drive/gitconfig
chmod 0755 /score/drive /score/drive/home /score/drive/home/.ssh
chmod 0644 /score/drive/gitconfig
ACTUAL_W=/score/work/actual.jsonl
rm -f "$ACTUAL_W"
run_unprivileged timeout -k 30 900 \
    env DRIVE_GITCONFIG=/score/drive/gitconfig DRIVE_HOME=/score/drive/home \
    python3 -s -E /tests/drive.py \
    --site /score/work/site-main --work /score/work/wk-main --keys /tests/keys \
    --alt-site /score/work/site-alt --ssl-site /score/work/site-ssl \
    --tmp /score/tmp/drive --http-port 8080 --ssh-port 29418 \
    run --cases "$CASES"/*.jsonl --out "$ACTUAL_W" \
    > /score/log/drive.log 2>&1
drc=$?
record drive.rc "$drc"
[ -s "$ACTUAL_W" ] || { tail -40 /score/log/drive.log >&2
                        write_zero "driver-produced-nothing"; }

# Optional per-step runtime probe, run while the instance that just answered
# the whole suite is still up. Some rungs make a claim that exists only in a
# live process -- which kind of thread served those requests -- and it cannot
# be read off the artifact, nor from score.py, which runs after the trap below
# has killed every instance. This is the one place that can ask. A step with no
# such claim ships no probe.sh and nothing happens.
if [ -s /tests/probe.sh ]; then
    say "probe"
    bash /tests/probe.sh > /score/log/probe.log 2>&1
    record probe.rc "$?"
fi

# The servers are done. Stop them and everything else running as the
# unprivileged user before root compares what the driver wrote: the driver and
# the candidate's server share that user, and the driver's record is in
# /score/work, which is theirs.
stop_servers
reap_unprivileged
copy_as_unprivileged "$ACTUAL_W" /score/out/actual.jsonl

# A refused overlay -- a must-flip record whose baseline does not match
# expected.jsonl -- is a defect in the step, not a candidate failure. It has to
# read as zero out of the full suite rather than zero out of zero, which is a
# number a mean will happily swallow.
python3 /tests/drive.py compare --expect "$EXPECT" \
    --must-flip "$MUSTFLIP" \
    --actual /score/out/actual.jsonl --out /score/out/behaviour.json \
    > /score/out/behaviour.count 2>> /score/log/drive.log \
    || { tail -5 /score/log/drive.log >&2; write_zero "compare-refused"; }
record behaviour "$(cat /score/out/behaviour.count 2>/dev/null)"
say "behaviour $(cat /score/out/behaviour.count 2>/dev/null)"

# A failure report for the maintainer, truncated so one broken build cannot
# produce a hundred megabytes of diff. /logs/verifier is collected by the
# harness and is never handed to the candidate -- not in this step and not in
# the next, whose only inheritance is /app.
# Isolated mode: this shell's cwd is /app, and a plain `python3 -` there would
# import a planted /app/json.py, as root.
python3 -I - <<'PY' 2>/dev/null || true
import json
rep = json.load(open("/score/out/behaviour.json"))
bad = [c for c in rep["cases"] if not c["ok"]]
def clip(o, n=600):
    s = json.dumps(o, sort_keys=True)
    return s if len(s) <= n else s[:n] + "...<clipped>"
# Per-family counts as well as the total. The behaviour fraction is one
# number, so a later rung that keeps HTTP and loses SSH scores like a
# half-migrated tree. These three numbers say which it was. They are for the
# maintainer only: nothing reads them back, and /logs/verifier never reaches
# the candidate.
fam = {}
for c in rep["cases"]:
    k = c["id"].split(".", 1)[0]
    t, p_ = fam.get(k, (0, 0))
    fam[k] = (t + 1, p_ + (1 if c["ok"] else 0))
out = {"total": rep["total"], "passed": rep["passed"],
       "by_family": {k: "%d/%d" % (v[1], v[0]) for k, v in sorted(fam.items())},
       "failed_ids": [c["id"] for c in bad],
       "first_diffs": [{"id": c["id"],
                        "expected": clip(c.get("expected")),
                        "actual": clip(c.get("actual"))} for c in bad[:40]]}
json.dump(out, open("/logs/verifier/behaviour-fails.json", "w"), indent=1)
PY
exit 0
