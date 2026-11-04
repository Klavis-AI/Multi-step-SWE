#!/usr/bin/env bash
# Score step 4 (04-session-entry-point) of the Spark 2.4 -> 4.0 migration.
#
# The order below is the whole design, and it is not step 1's order. Reading
# top to bottom:
#
#   0  lock the reward directory, before anything else runs at all
#   1  preconditions: the payload, the tree, the toolchain, the corpus, the
#      mirror, every instrument's own selftest -- and the listener made to see
#      one real SQL execution before it is believed about the candidate's
#   2  hand /app to nobody, and only then build it
#   3  one prebuild, which is also what gives the probe and the replay a jar
#   4  the session probe: one real Spark job off the candidate's own artifact
#   5  the behavior replay: 140 recorded jobs, re-run and compared, each with
#      the listener attached
#   6  the gate: scanners as nobody, facts and rules root's, reading the replay's own
#      probe files
#   7  the same rules again as root over a root-owned copy of the same facts
#   8  the repository's own tests, from a pristine copy of src/test
#   9  score.py turns all of that into one number
#
# Five and six are swapped against step 1, as they were at steps 2 and 3, and
# at this step the swap stops being a convenience and starts deciding a score.
# What step 1 asked -- which Spark does this build resolve -- is answerable
# from a tree standing still. Five of this step's standing rules are not
# answerable from a tree at all: three ask whether adaptive query execution is
# on and actually re-planned something, which is an event the engine emits
# while it runs or does not emit, and two ask for the effective value of
# `spark.sql.legacy.statisticalAggregate`, a default nobody writes down until
# somebody overrides it, so a tree that says nothing about it and a tree that
# has it right look identical on disk.
#
# What is new here is that five of this step's own twelve graded rules read the
# same kind of fact. `m1-session-334` and `m1-session-334-uniform` ask which
# Spark the sessions the jar actually ran in reported, because `spark-core` is
# `provided` scope in this pom and the pom's answer and the engine's answer are
# two different measurements (NOTES 6.3.11). All three of `m3-*` ask about
# `spark.sql.ansi.enabled` -- one at the builder, one in the effective
# configuration, one for the absence of a dissenting session. None of the five
# has an answer until something runs. So the ordering below is not a matter of
# taste: put the gate first and a whole requirement family scores zero on a
# correct tree.
#
# One thing about this step is worth knowing before reading the preconditions:
# it moves the rung again. Step 3 ran on Spark 3.2.4; this one lands on 3.3.4,
# which means the mirror this run resolves against is a different census (c05b)
# and the recordings the replay is compared against were taken there. A mirror
# check that passed at step 3 is not evidence about this one.
#
# The rung move is also the reason the behavior half of this step is a
# stability claim rather than a change claim. `rung-diff.py c04 c05b` is
# SAME=140 -- 3.2 -> 3.3 moves nothing at all on this corpus -- and
# `rung-diff.py c05b c05ansi` is SAME=140 too, so turning ANSI on, which is
# what this step's m3 asks for, moves nothing either (NOTES 6.4.1). Both were
# measured rather than assumed, and that is what makes them usable: the replay
# below can demand all 140 jobs answer exactly as recorded, and a candidate
# that consolidated 63 entry points and flipped ANSI on has no licence to move
# a single one of them.
#
# Everything from 2 onward runs as nobody. Everything before it is the verifier
# checking itself, and a failure there is reported as a broken verifier rather
# than as a red tree: a mirror that is down or a payload that is short a file
# makes every measurement after it meaningless, and a mean that silently
# includes such a run is worse than a run that never happened (traps 5 and 6).
#
# Always exits 0 through report(), which always writes a reward (rule 8).
set -u
# Root works from /, not from the image's WORKDIR /app (wave H, 2026-10-13).
# Build tools read project-local configuration from their working directory:
# `mvn` takes .mvn/jvm.config, .mvn/maven.config and .mvn/extensions.xml from
# the nearest directory above it holding a .mvn, and javac with no -cp uses "."
# as its class path and loads annotation processors from it. Started in /app,
# each of those is the candidate's, read by root before anything is handed to
# nobody. Whatever has to run in /app runs as nobody, from a subshell that
# changes there itself.
cd /
source /tests/helpers.sh
harden_reward_dir

# Trap 8: no interpreter of ours may leave bytecode beside the payload. The
# manifest check below would see it, and /tests is read at the same moment it
# is being verified.
# The prefix is root's own 0700 home, not /tmp: Python also *reads* cached
# bytecode from the prefix, and a .pyc the candidate's uid planted under a
# world-writable one would run inside every root interpreter below.
export PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=/root/.pycache-verifier

# The rules file. The step id is read once, here, and every later use is this
# variable -- including the stamp assertion below, which is the only literal
# `04` in the file and is a check on the payload rather than a second source of
# truth. Derived from the payload's own stamp rather than written out, because
# trap 2
# is exactly this: a step-scoped verifier that runs the wrong step's rules
# produces an arithmetically correct number about the wrong requirements, and
# nothing in the job directory shows it. step.tsv is covered by the manifest,
# score.py prints it as the first line of every score.log, and check-job.sh
# compares that line against the step directory's own name -- so deriving the
# rules path from it puts the gate and the stamp on the same evidence. A
# payload with no stamp gets `stepNA.tsv`, which does not exist, and the
# precondition below says so rather than letting gate-run.sh fail obscurely
# nine minutes in.
STEPID=$(awk -F'\t' '$1 == "step" {print $2}' /tests/step.tsv 2>/dev/null)
RULES=/tests/gates/rules/step${STEPID:-NA}.tsv

RUN=/score
# Emptied rather than removed: a local docker run bind-mounts this path to
# collect the evidence afterwards, and rm -rf on a mount point fails.
mkdir -p "$RUN"
find "$RUN" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
# Root keeps this directory; only the named subdirectories below are handed to
# nobody. But nobody has to be able to *walk* to them, and a bind-mounted /score
# arrives with whatever mode the host directory had -- 0700 under a umask of
# 077, which is how the first real run of this file lost its whole gate to
# `mkdir: cannot create directory '/score'` one line at a time.
chmod 0755 "$RUN"
PRE=$RUN/preflight.tsv; : > "$PRE"
LOG=$RUN/test.log; : > "$LOG"
JAVA=${JAVA_HOME:-/opt/jdk}
SPARK=${SPARK_HOME:-/opt/spark}

say()  { printf '%s\n' "$*" | tee -a "$LOG"; }
ok()   { printf 'ok\t%s\t%s\n'   "$1" "${2-}" >> "$PRE"; say "pre    ok   $1: ${2-}"; }
no()   { printf 'fail\t%s\t%s\n' "$1" "${2-}" >> "$PRE"; say "pre    FAIL $1: ${2-}"; }
phase() { say ""; say "== $* ($(date -u +%H:%M:%S), $((SECONDS / 60))m$((SECONDS % 60))s in)"; }

# ---------------------------------------------------------------- 1. the payload
# First, and before any of it is used. mk-step-tests.sh wrote this manifest over
# the directory it assembled; if a file arrived truncated, or a scanner wrote
# into its own payload, the difference is here rather than in a rule that
# quietly read half a table (trap 7).
phase "preconditions"
if ( cd /tests && sha256sum -c --quiet --status manifest.sha256 ) 2>/dev/null; then
    ok payload "$(grep -c . /tests/manifest.sha256) file(s) match manifest.sha256"
else
    ok_detail=$( cd /tests && sha256sum -c --quiet manifest.sha256 2>&1 | head -3 | tr '\n' ' ')
    no payload "${ok_detail:-manifest.sha256 is missing}"
fi

# ---------------------------------------------------------------- the stamp
# Read immediately after the manifest, because every fact below it is scoped by
# it. `04` is asserted rather than merely reported: this file is copied forward
# from one step to the next, and the copy that would be wrong is the one whose
# header still says step 4 while its payload is somebody else's.
census=$(awk -F'\t' '$1 == "census" {print $2}' /tests/step.tsv 2>/dev/null)
if [ "$STEPID" = 04 ] && [ -f "$RULES" ] && [ -n "$census" ]; then
    ok stamp "payload step $STEPID, census $census, rules $(basename "$RULES")"
else
    no stamp "step=${STEPID:-<unstamped>} census=${census:-<unstamped>} \
rules=$RULES $([ -f "$RULES" ] && echo present || echo ABSENT); this verifier \
scores step 04 and nothing else"
fi

# ---------------------------------------------------------------- the tree
# Trap 5's shape at this step: an /app that never received the repository is not
# a candidate who deleted the source, and the two must not score alike. So this
# asks only the question the *author* owns -- did the mount arrive at all -- and
# leaves "is the tree whole" to the gate, where `ast-nonempty` reads the 230
# sources the tree ships with and a shortfall costs the migrated family.
#
# The two must not be the same number. This check used to read >= 200, which put
# a candidate who deleted 31 files into the same bucket as an empty /app and had
# score.py tell whoever read the log to "re-read this run as a broken verifier
# rather than as a red tree" -- about a tree that was exactly, measurably red.
napp=$(find /app/src -name '*.java' 2>/dev/null | wc -l)
if [ -f /app/pom.xml ] && [ "$napp" -ge 1 ]; then
    ok app "pom.xml and $napp java source(s) under /app/src"
else
    no app "pom.xml $([ -f /app/pom.xml ] && echo present || echo absent), $napp java source(s)"
fi

# ---------------------------------------------------------------- the toolchain
jv=$("$JAVA/bin/java" -version 2>&1 | head -1)
mv_=$(mvn -v 2>&1 | head -1)
if [ -x "$JAVA/bin/java" ] && [ -x "$SPARK/bin/spark-submit" ] && [ -n "$mv_" ]; then
    ok toolchain "$jv | $mv_ | $SPARK"
else
    no toolchain "java=$jv maven=$mv_ spark-submit=$([ -x "$SPARK/bin/spark-submit" ] && echo ok || echo absent)"
fi
# Read as nobody, and that is the whole of the check. Everything that touches
# the corpus from phase 2 on runs unprivileged, so root's `test -d` answers a
# question nobody asked: the image's data layer once carried 0700 directories
# straight out of the staging archive, this line called the corpus ok, and all
# 140 recorded jobs then failed to open a single file of it. The image now sets
# the bits and asserts them at build time (pkg/verifier/Dockerfile.data); this
# is the same assertion, taken at scoring time on the image that is running.
for d in full reduced cif pdb; do
    [ -d "/data/$d" ] || { no corpus "/data/$d is absent"; break; }
done
ndata=$(find /data -type f 2>/dev/null | wc -l)
nread=$(run_unprivileged find /data -type f -readable 2>/dev/null | wc -l)
if grep -q "^fail.corpus" "$PRE"; then
    :
elif [ "$ndata" -gt 0 ] && [ "$nread" = "$ndata" ]; then
    ok corpus "$(du -sh /data 2>/dev/null | cut -f1) under /data, \
$ndata file(s), all readable as nobody"
else
    no corpus "$nread of $ndata file(s) under /data can be read as nobody"
fi

# ---------------------------------------------------------------- the mirror
# Two clients, because the tree uses both and they trust different stores: curl
# reads /etc/ssl/certs, and every fetch the codebase itself makes goes through
# java.net.URL, which reads $JAVA_HOME's cacerts. A green curl says nothing
# about whether MmtfReader can reach the mirror. Compiled rather than run as a
# single source file because the baseline JDK is 8, which predates `java
# Foo.java`. This is check-image.sh's probe, run again here: the image was
# checked when it was built, and this is the run that is being scored.
mkdir -p /tmp/j
cat > /tmp/j/Probe.java <<'JAVA'
import java.io.*; import java.net.*;
public class Probe {
    static int get(String u) throws Exception {
        InputStream in = new URL(u).openStream();
        byte[] b = new byte[8192]; int n, t = 0;
        while ((n = in.read(b)) > 0) t += n;
        in.close(); return t;
    }
    public static void main(String[] a) throws Exception {
        System.out.println("java_https=" + get("https://mmtf.rcsb.org/v1.0/full/1AA6"));
        System.out.println("java_ftp=" + get("ftp://ftp.wwpdb.org/pub/pdb/data/status/20180427/added.pdb"));
    }
}
JAVA
#
# Compiled by root -- the probe is the verifier's own code -- and every fetch
# made as nobody, for the same reason the corpus check above is: the jobs that
# depend on the mirror are the unprivileged ones, and a certificate store or a
# proxy setting that only root can read would pass here and fail there.
"$JAVA/bin/javac" -d /tmp/j /tmp/j/Probe.java 2>>"$LOG"
chmod -R a+rX /tmp/j
live=$(
    run_unprivileged curl -sf -o /tmp/b https://mmtf.rcsb.org/v1.0/full/1AA6 \
        && echo "curl_https=$(wc -c < /tmp/b)"
    run_unprivileged curl -sf -o /tmp/f ftp://ftp.wwpdb.org/pub/pdb/data/status/20180427/added.pdb \
        && echo "curl_ftp=$(wc -c < /tmp/f)"
    run_unprivileged "$JAVA/bin/java" -cp /tmp/j Probe
) 2>>"$LOG"
mirror_bad=
for k in curl_https curl_ftp java_https java_ftp; do
    n=$(printf '%s\n' "$live" | sed -n "s/^$k=//p")
    [ -n "$n" ] && [ "$n" -gt 0 ] 2>/dev/null || mirror_bad="$mirror_bad $k=${n:-<nothing>}"
done
if [ -z "$mirror_bad" ]; then
    ok mirror "$(printf '%s' "$live" | tr '\n' ' ')"
else
    no mirror "the mirror did not serve:$mirror_bad"
fi

# --------------------------------------------------- the service tree, read direct
# The four fetches above go over the loopback, where root reads the blob and
# hands it back. That is a different question from "can the job open the file":
# HarnessDriver-siftslocal is given a filesystem path and reads
# /srv/svc/ftp.ebi.ac.uk/sifts/split_xml itself, as nobody. When that tree
# arrived 0700 out of the staging archive the job retried three times per entry
# and printed an empty table -- one job of 140, one block of 247 scored inputs,
# and an oracle run that came back 0.999109 instead of 1. Small enough to read
# as noise and entirely a defect in the box (NOTES 6.1.11). The image asserts
# this at build time in two places now; this is the same assertion taken on the
# image that is actually running.
nsvc=$(find /srv/mmtf /srv/svc -type f 2>/dev/null | wc -l)
nsvcr=$(run_unprivileged find /srv/mmtf /srv/svc -type f -readable 2>/dev/null | wc -l)
sifts=$(run_unprivileged find /srv/svc/ftp.ebi.ac.uk/sifts/split_xml \
            -name '*.xml.gz' -print -quit 2>/dev/null)
if [ "$nsvc" -gt 0 ] && [ "$nsvcr" = "$nsvc" ] && [ -n "$sifts" ]; then
    ok svc "$nsvc file(s) under /srv, all readable as nobody, \
sifts among them ($(basename "$sifts"))"
else
    no svc "$nsvcr of $nsvc file(s) under /srv can be read as nobody; \
sifts entry reachable as nobody: ${sifts:-<none>}"
fi

# The pinned logging config, for the third instance of the same defect: both
# files are COPYed from an authoring tree and spark-submit reads them as nobody
# or not at all. Nothing downstream fails loudly when it cannot -- Spark logs
# one line about its default profile and carries on -- so this is the only place
# it shows up before a whole census moves.
logbad=
for f in "$SPARK/conf/log4j.properties" "$SPARK/conf/log4j2.properties"; do
    [ -f "$f" ] || continue
    run_unprivileged test -r "$f" || logbad="$logbad $(basename "$f")"
done
if [ -z "$logbad" ]; then
    ok logging "$SPARK/conf/log4j{,2}.properties readable as nobody"
else
    no logging "spark's pinned logging config is not readable as nobody:$logbad"
fi

# ---------------------------------------------------------------- the instruments
# Six selftests, and the scorer's own. Each one proves its instrument still
# fails on a known-bad input today; a gate that has only ever been seen to pass
# is a gate nobody has seen work (trap 6). rules.py's is the strongest of them:
# it requires every one of the 131 rules this step composes to go red against
# its own counterexample fixture, so a rule that stopped matching anything
# cannot reach this run green.
selftest() {  # selftest <name> <cmd...>
    local name=$1; shift
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" = 0 ]; then ok "selftest.$name" "$(printf '%s' "$out" | tail -1)"
    else no "selftest.$name" "rc=$rc $(printf '%s' "$out" | tail -2 | tr '\n' ' ')"; fi
}
selftest rules      python3 /tests/gates/rules.py --rules "$RULES" --selftest
selftest build-scan python3 /tests/gates/build-scan.py --selftest
selftest pom-scan   python3 /tests/gates/pom-scan.py --selftest
selftest projection python3 /tests/suite/projection.py --selftest
selftest inputs     python3 /tests/suite/inputs.py --selftest
selftest norm       python3 /tests/suite/norm.py --selftest
selftest score      python3 /tests/score.py --selftest

# ------------------------------------------------------- the listener, made to see
# Trap 6 in the shape this step needs it. Five of this step's twelve graded
# rules -- `m1-session-334`, `m1-session-334-uniform` and all three of `m3-*`
# -- and the behavior gate score.py opens on are readings of one listener's
# output, and all of them are false in the same direction when that listener
# sees nothing. Two whole requirements of three, on the weights in
# weights.tsv, are hostage to this one instrument. A run
# in which no job reports an execution is exactly what an unmigrated tree looks
# like *and* exactly what a probe that failed to attach looks like -- nothing
# else in the run tells those two apart, and the second one scores the
# candidate 0 for the verifier's fault.
#
# So the listener is made to see one before it is believed about the
# candidate's tree, against gate.GateMain: the verifier's own job, which runs
# one `select count(*) from range(10)` and therefore one SQL execution by
# construction. Its facts go into the gate below as well, under their own
# label, because a control that is not kept is a control nobody can check.
#
# The jar is built by root -- it is the verifier's code, not the candidate's --
# and handed on readable, so the unprivileged half below reuses it rather than
# compiling its own copy against the same Spark.
PJAR=$(bash /tests/gates/probe-jar.sh 2>>"$LOG" || true)
if [ -n "${PJAR:-}" ] && [ -f "$PJAR" ]; then
    chmod -R a+rX "$(dirname "$PJAR")" 2>/dev/null || true
    ok probe-jar "$(basename "$PJAR"), $(wc -c < "$PJAR") bytes"
else
    no probe-jar "probe-jar.sh produced no jar; see test.log"
fi
# From /app, as it ran before root moved to / (wave H): spark-submit resolves
# its warehouse directory against the working directory, and the probe
# records the session's configuration.
( cd /app && run_unprivileged env GATE_PROBE_LABEL=gate-selftest \
    /tests/gates/session-probe.sh ) \
    > "$RUN/probe.selftest.tsv" 2> "$RUN/probe.selftest.err"
psinside=$(awk -F'\t' '$1 == "job" && $3 ~ /^[0-9]+$/' "$RUN/probe.selftest.tsv" | wc -l)
psexec=$(awk -F'\t' '$1 == "event" && $2 == "sqlExecutionStart" { print $3; exit }' \
         "$RUN/probe.selftest.tsv")
psrc=$(awk -F'\t' '$2 == "submit.rc" { print $3; exit }' "$RUN/probe.selftest.tsv")
if [ "${psinside:-0}" -ge 1 ] && [ "${psexec:-0}" -ge 1 ] && [ "${psrc:-1}" = 0 ]; then
    ok probe-live "gate.GateMain: ${psexec} execution(s) started, \
${psinside} job(s) inside one"
else
    no probe-live "the listener saw ${psexec:-0} execution(s) and ${psinside:-0} \
job(s) inside one (submit.rc=${psrc:-<none>}); every job rule below would read \
false about any tree"
fi
chmod 0644 "$RUN/probe.selftest.tsv" 2>/dev/null || true

if grep -q '^fail' "$PRE"; then
    say ""
    say "a precondition failed; nothing measured after one means anything"
    python3 /tests/score.py --tests /tests --run "$RUN" >> "$LOG" 2>&1
    cp -f "$LOG" "$PRE" /logs/verifier/ 2>/dev/null || true
    report 0
fi

# ---------------------------------------------------------------- 2. hand it over
phase "handing /app to nobody"
# Two things run-suite.sh does for itself when it runs as root and cannot do as
# nobody, done here instead, so that the replay happens under the recording's
# conditions rather than under a near miss:
#
#   /etc/hosts   without the container's own name in it, log4j2's status logger
#                resolves the host while configuring itself, fails, and writes
#                an UnknownHostException carrying a wall clock and a fresh
#                container id to stdout ahead of the job's own output. norm.py
#                absorbs the line, but a recording that needs absorbing is a
#                recording taken under other conditions (NOTES 4.9.4).
#   the request log  run-suite.sh truncates it between jobs so each job's reqlog
#                is its own; serve.py created it as root, so nobody cannot.
grep -q "[[:space:]]$(hostname)\$" /etc/hosts 2>/dev/null \
    || printf '127.0.0.1 %s\n' "$(hostname)" >> /etc/hosts
touch /var/log/mmtf-mirror-requests.log && chmod 0666 /var/log/mmtf-mirror-requests.log

give_app_to_unprivileged
for d in "$RUN/suite"; do give_dir_to_unprivileged "$d"; done

# The gate's own directory stays root's. gate-run.sh runs as root and starts
# only its three tree-reading scanners as nobody (GATE_AS), so every fact file
# it writes is opened by root, and nothing the candidate's build leaves running
# can rewrite one after its scanner has exited -- which a nobody-owned
# $RUN/gate allowed, between the scan and root's copy below (2026-10-12
# reward-hacking audit, F1). The two work directories the build and pom
# scanners keep their own logs in are the only parts handed over.
#
# ast-scan.sh compiles its scanner into a cache on first use, and the default
# cache is under /tmp, where anything nobody runs could put a class first. So
# root compiles it here, before any candidate code has run, into a directory
# nobody cannot write, and the gate is pointed at that copy (GATE_CACHE).
GATE_AS="setpriv --reuid $SCORE_UID --regid $SCORE_GID --clear-groups env HOME=/tmp TMPDIR=/tmp"
# A read-only copy of the pre-warmed Maven repository, taken here, before any
# candidate code has run. The candidate's own builds run against
# /scratch/.m2, which nobody owns, so anything they leave there -- a plugin jar
# included -- would otherwise be what pom-scan.sh's Maven runs. pom-scan reads
# plugins and artifacts from this copy instead (GATE_M2_PRISTINE; root-tool-
# config sweep, 2026-10-13).
GATE_M2_PRISTINE="$RUN/m2-pristine"
cp -a /scratch/.m2/repository "$GATE_M2_PRISTINE" 2>/dev/null \
    && chown -R root:root "$GATE_M2_PRISTINE" \
    && chmod -R go-w,a+rX "$GATE_M2_PRISTINE" \
    || GATE_M2_PRISTINE=
# Which repository pom-scan reads is a precondition, not a fallback (wave H
# round 2, G2-m3). No candidate code has run yet, so a copy that failed is
# this verifier failing, and it is reported as one rather than letting
# pom-scan quietly read the /scratch/.m2 the candidate's builds write.
if [ -n "$GATE_M2_PRISTINE" ] && [ -d "$GATE_M2_PRISTINE" ]; then
    ok m2-pristine "$(find "$GATE_M2_PRISTINE" -type f | wc -l) file(s) of /scratch/.m2 \
copied root-owned to $GATE_M2_PRISTINE before any candidate code ran"
else
    no m2-pristine "root could not copy /scratch/.m2/repository to $RUN/m2-pristine, \
so pom-scan would read the repository the candidate's builds write"
fi
mkdir -p "$RUN/gate" "$RUN/astcache" "$RUN/astempty"
chmod 0755 "$RUN/gate" "$RUN/astcache"
for d in "$RUN/gate/build" "$RUN/gate/pom"; do give_dir_to_unprivileged "$d"; done
GATE_CACHE="$RUN/astcache" /tests/gates/ast-scan.sh "$RUN/astempty" > /dev/null 2>&1 || true
chmod -R a+rX "$RUN/astcache" 2>/dev/null || true
if [ -f "$RUN/astcache/AstScan.class" ]; then
    ok ast-cache "AstScan compiled by root into $RUN/astcache before any candidate code ran"
else
    no ast-cache "root could not compile AstScan into $RUN/astcache, so the ast \
scan would compile its own copy where candidate code can reach it"
fi

# The answer key, sealed before anything unprivileged runs.
#
# ref/<job>.norm is the recording the replay is compared against, and it is
# 0.24 of this step's weight. score.py reads it and score.py runs as root;
# run-suite.sh reads jobs.tsv and writes its own output, and nothing that runs
# as 65534 opens ref/ at all. Left world-readable it is the exam and the mark
# scheme in the same room: a main that printed its own job's .norm byte for
# byte would replay perfectly without migrating a line, and the projection
# cannot tell a correct program from a correct transcript. 0700 on the
# directory is enough -- nobody cannot walk into it whatever the files say.
# Round-3 review, R5.
if [ -d /tests/suite/ref ]; then
    chown root:root /tests/suite/ref 2>/dev/null || true
    chmod 700 /tests/suite/ref
    if run_unprivileged ls /tests/suite/ref > /dev/null 2>&1; then
        no ref-sealed "/tests/suite/ref is still listable as nobody after chmod 700"
    else
        ok ref-sealed "$(ls /tests/suite/ref | wc -l) reference recording(s), \
0700 root:root before the first unprivileged process, unlistable as nobody"
    fi
else
    no ref-sealed "no /tests/suite/ref in the payload to seal"
fi
mkdir -p "$RUN/surefire"
: > "$RUN/suite/summary.tsv"      # so a run that never reaches the suite still
chown 65534:65534 "$RUN/suite/summary.tsv" 2>/dev/null || true   # scores 0 rather than crashing the scorer

# ---------------------------------------------------------------- 3. the prebuild
# One package before the gate, for the jar the probe submits. The gate's own
# --build then runs its two clean rounds on top of it; the extra round costs
# about twenty seconds and keeps the probe reading the artifact of the tree
# under test rather than a jar this script built differently.
#
# Online, and deliberately. One of the things this step asks the candidate to
# change is what the build resolves, so a candidate who legitimately lands on a
# version the image's warm repository never heard of must be able to fetch it.
# Offline here would turn that into a red pom-resolves, which is a standing
# rule: the whole reading would be void, for exactly the change the step is
# about. That is not hypothetical on this box -- the authoring copy of
# build-scan.sh run with BUILD_SCAN_OFFLINE=1 cannot complete a single round in
# any image here, because it runs `clean` between rounds and no image warms
# maven-clean-plugin (NOTES 6.4.4). Offline is a fragility, not a safety.
phase "prebuild: mvn -B clean package -DskipTests"
( cd /app && run_unprivileged timeout -k 30 900 mvn -B clean package -DskipTests ) > "$RUN/prebuild.log" 2>&1
prerc=$?
JAR=$(ls /app/target/mmtf-spark-*.jar 2>/dev/null | grep -v -e original- -e -sources -e -javadoc | head -1)
say "prebuild rc=$prerc, jar=${JAR:-<none>}, $(wc -l < "$RUN/prebuild.log") log line(s)"
[ "$prerc" = 0 ] || say "$(tail -5 "$RUN/prebuild.log")"

# ---------------------------------------------------------------- 4. the probe
# GATE_PROBE_LABEL, which step 1 did not need, because from step 2 on the
# replay probes too and the gate reads both files as one table. Unlabelled,
# this job would key its rows on its application name -- ReadMmtfReduced -- and
# land them beside the replay's own ReadMmtfReduced rows, where
# m1-reader-count-outside would then be reading two runs of one class as
# though they were one run.
phase "probe: one real job off the candidate's own jar"
if [ -n "$JAR" ]; then
    # From /app, as it ran before root moved to / (wave H): spark-submit
    # resolves its warehouse directory against the working directory, and the
    # probe records the session's configuration.
    ( cd /app && run_unprivileged timeout -k 30 600 env MMTF_REDUCED=/data/reduced MMTF_FULL=/data/full \
        GATE_PROBE_LABEL=session-probe \
        /tests/gates/session-probe.sh \
        --class edu.sdsc.mmtf.spark.io.demos.ReadMmtfReduced --jar "$JAR" ) \
        > "$RUN/probe.tsv" 2> "$RUN/probe.err"
else
    printf 'error\tjar\tthe prebuild produced no shaded jar under /app/target\n' > "$RUN/probe.tsv"
fi
# Root keeps it: readable by the gate, not rewritable by the replay.
chmod 0644 "$RUN/probe.tsv" 2>/dev/null || true
# submit.rc is in the line because the first run of this file printed
# "23 fact(s), 0 error(s)" over a job that had exited 1: session-probe.sh
# reports a failure as a non-zero submit.rc, not as an `error` fact, and the
# standing probe-submit-ok rule was added at the same time as this line.
say "probe   $(grep -c . "$RUN/probe.tsv") fact(s), \
$(awk -F'\t' '$1 == "error"' "$RUN/probe.tsv" | wc -l) error(s), submit.rc=\
$(awk -F'\t' '$2 == "submit.rc" { print $3; exit }' "$RUN/probe.tsv")"

# ---------------------------------------------------------------- 5. the replay
# Before the gate, which is the reverse of step 1, for the reason at the top of
# this file: the facts half of m1 and all of m3 are read from are what the tree
# *did*, and the tree does it here. m2 is the one family that would score the
# same in either order -- it is read off the source and the bytecode -- which
# is why the order is stated as a constraint rather than a convention.
#
# CORES=1 is not a speed setting. It is the width the 140 recordings were made
# at: 58 of the classes call setMaster themselves, and Spark's default
# parallelism decides how many part-files a job writes and in what order its
# rows arrive. A replay at a different width is a comparison against a different
# recording (NOTES 4.5).
#
# PROBE attaches gate.GateProbe to every job in the plan -- on the driver class
# path and as spark.extraListeners, never on the application's own class path --
# and writes one facts file per job under $RUN/suite/probe, keyed on the plan's
# own name for the job. The recordings this replay is compared against were
# taken with it attached, at *this* step's rung -- Spark 3.3.4, census c05b --
# and not at step 3's. That distinction is the one the replay family lives or
# dies by here: 3.1 changed what a single-row stddev renders (SPARK-13860) and
# 3.2 turned adaptive execution on by default, so a recording taken at a lower
# rung would be a comparison against a different engine, and every difference
# it found would be the rung rather than the candidate. build/c05b is that
# recording set, taken from the tree step 4 starts from.
#
# 3.2 -> 3.3 is the one rung move in this ladder that changes nothing:
# `rung-diff.py c04 c05b` is SAME=140 ORDER=0 VALUE=0 RC=0 ABSENT=0. That does
# not make c05b interchangeable with c04, and c04 is not what ships here. The
# two sets are equal on what the projection compares and were taken by
# different engines; comparing against the older one would be asserting the
# equality this step is entitled to rely on only because it was measured
# (NOTES 6.4.1).
# The probe channel, opened here rather than in the runner.
#
# The runner and every job it submits are one uid, so a file the runner writes
# is a file a later job can empty, relabel or rewrite -- and every rule over
# the per-program kinds is `absent`, which reads an emptied file as a clean
# one. Round 3 of the item-5 review priced that at the whole behaviour gate:
# 29 of the handed tree's programs report a red row, and 29 truncations are
# cheaper than 29 migrations.
#
# So nobody never gets a probe file to hold. Root makes one fifo, mode 0222
# inside a 0700 directory -- writable only through a descriptor already open on
# it, not reachable by name -- and drains it for the length of the replay.
# Which rows are whose is not the runner's word for it, because the runner's
# word would arrive on the same pipe: each job is given a random token, the
# listener stamps it on every row (GateProbe.java), and the runner records it
# against the job's name on descriptor 8, which the job subshell closes before
# spark-submit. A process that outlives its job still holds the pipe and still
# holds its own token, so its late rows land in its own file beside the real
# ones, where a repeated key is `tampered`. Rows under a token no job was
# issued are `stray`, which is a rule of its own (cheats4.tsv).
#
# One residual, recorded rather than closed: a process of job k that is still
# alive during job k+1 can read /proc/<pid>/environ of the new job's shell and
# find the next token (both run as 65534). It buys the forger the ability to
# add rows to a file that is about to exist -- not to remove one, and the real
# rows still arrive, so the repeated-key reading is what it gets (NOTES 6.4.10).
STREAM=$RUN/probestream
rm -rf "$STREAM"
mkdir -p "$STREAM/blocks"
chmod 700 "$STREAM" "$STREAM/blocks"
mkfifo -m 0222 "$STREAM/fifo"
: > "$STREAM/ctl"
chmod 0600 "$STREAM/ctl"
# Root's and only root's: the stop line is a row like any other on this pipe,
# so a token the jobs could guess would be a way to end the reading early.
STOPTOK=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
python3 /tests/gates/probe-split.py drain --fifo "$STREAM/fifo" \
    --blocks "$STREAM/blocks" --stop "$STOPTOK" --ready "$STREAM/ready" \
    >> "$LOG" 2>&1 &
drainpid=$!
# Waited for rather than assumed: opening a fifo for writing blocks until a
# reader arrives, and a splitter that never started would hang the verifier on
# the redirection below instead of failing it.
n=0
while [ ! -f "$STREAM/ready" ] && [ "$n" -lt 100 ]; do sleep 0.1; n=$((n + 1)); done
if [ -f "$STREAM/ready" ]; then
    ok probe-stream "root drains $STREAM/fifo as pid $(cat "$STREAM/ready"), \
0222 inside a 0700 directory"
    STREAMON=1
else
    no probe-stream "the splitter did not open the fifo in 10s, so the \
replay's probe files would be owned by the uid the replay runs as; this \
reading is void rather than red"
    STREAMON=
    kill "$drainpid" 2>/dev/null || true
    drainpid=
fi

phase "replay: 140 recorded jobs at CORES=1, with the listener attached"
if [ -n "$STREAMON" ]; then
    # The redirections are opened by root, before setpriv: descriptor 9 is the
    # write end of the drained fifo and descriptor 8 is the token table. Both
    # are inherited, neither is a path the replay is told.
    run_unprivileged env APP=/app SUITE=/tests/suite OUT="$RUN/suite" DATA=/data \
        CORES=1 PHASES="1 2" PROBE="${PJAR:-}" PROBESTREAM=1 \
        bash /tests/suite/run-suite.sh > "$RUN/suite.log" 2>&1 \
        9>"$STREAM/fifo" 8>>"$STREAM/ctl"
else
    run_unprivileged env APP=/app SUITE=/tests/suite OUT="$RUN/suite" DATA=/data \
        CORES=1 PHASES="1 2" PROBE="${PJAR:-}" \
        bash /tests/suite/run-suite.sh > "$RUN/suite.log" 2>&1
fi
suiterc=$?
# Nothing the replay started outlives it into the gate (helpers.sh says why).
reap_unprivileged
# The replay's transcripts, its summary and its probe files, taken out of the
# unprivileged uid's reach before any more candidate code runs as that uid --
# the repository's own suite, and the gate's build and pom rounds wherever they
# follow the replay -- and read as that uid, so root follows nothing it left
# behind (helpers.sh,
# take_suite_from_unprivileged; 2026-10-12 final review, N-1 and F1c-i).
if took=$(take_suite_from_unprivileged "$RUN/suite" /tests/suite/jobs.tsv 2>>"$LOG"); then
    ok suite-taken "$took"
else
    no suite-taken "root could not take $RUN/suite out of the unprivileged uid's reach"
fi

# The reading is closed before it is named, and named by root from root's own
# record. probe-derive.py is then told the name rather than reading it out of
# the file, which is the last of the four displacements the derivation lists:
# a file whose rows are real and whose label is somebody else's.
PROBEDIR=$RUN/suite/probe
if [ -n "$STREAMON" ]; then
    # Bounded: if the splitter has already died, opening the fifo for writing
    # blocks until a reader arrives and no reader is coming.
    timeout 30 sh -c 'printf "__probe-split-stop__\t%s\n" "$1" >> "$2"' \
        _ "$STOPTOK" "$STREAM/fifo" 2>/dev/null || true
    n=0
    while kill -0 "$drainpid" 2>/dev/null && [ "$n" -lt 100 ]; do
        sleep 0.1; n=$((n + 1))
    done
    kill "$drainpid" 2>/dev/null || true
    wait "$drainpid" 2>/dev/null || true
    PROBEDIR=$RUN/proberoot
    mkdir -p "$PROBEDIR"
    chmod 755 "$PROBEDIR"
    python3 /tests/gates/probe-split.py assemble --blocks "$STREAM/blocks" \
        --ctl "$STREAM/ctl" --out "$PROBEDIR" 2>&1 | tee -a "$LOG"
    # A stray file is rows on the channel under a token this run never issued.
    # It is kept, not merged into anybody's file, and priced by no-probe-stray.
    [ -f "$PROBEDIR/stray.tsv" ] \
        && say "replay  $(grep -c . "$PROBEDIR/stray.tsv") unattributable row(s) \
on the probe channel; kept as stray.tsv"
fi

say "replay  rc=$suiterc, $(grep -c . "$RUN/suite/summary.tsv") job(s) ran of \
$(grep -c . /tests/suite/jobs.tsv) planned, \
$(ls "$PROBEDIR"/*.tsv 2>/dev/null | wc -l) probe file(s)"
[ "$suiterc" = 0 ] || say "$(tail -8 "$RUN/suite.log")"

# A probe file exists for every job that got as far as starting a SparkContext:
# GateProbe is registered through spark.extraListeners and writes on a fifo the
# runner opened, so a job that exits during argument validation never
# constructs the listener and leaves no file behind. Seventeen of this census's
# 140 jobs are exactly that, and on the handed tree and the golden one alike
# exactly one job that exits 0 leaves no file -- the suite's own
# HarnessDriver-outliers, which the derivation drops anyway (NOTES 6.4.9).
#
# So a job that finished successfully and left no probe file is not a program
# that never reached a context; it is a file something removed. That matters
# because every rule over the per-program kinds is `absent`, and an absent rule
# reads a missing file as a clean one: deleting the twenty-nine files whose
# rows are red is worth more than fixing the twenty-nine programs. The row
# below is what the deletion is worth instead. Root does this, after the
# candidate's last process has exited and before the gate runs.
mkdir -p "$PROBEDIR"
seeded=0
while IFS="$(printf '\t')" read -r job cls rc rest; do
    [ -n "$job" ] || continue
    [ "$rc" = 0 ] || continue
    case $job in HarnessDriver*) continue ;; esac
    [ -f "$PROBEDIR/$job.tsv" ] && continue
    printf 'probe\tapp.label\t%s\nprobe\treplay.dropped\ttrue\n' "$job" \
        > "$PROBEDIR/$job.tsv"
    chmod 0644 "$PROBEDIR/$job.tsv"
    seeded=$((seeded + 1))
done < "$RUN/suite/summary.tsv"
[ "$seeded" = 0 ] \
    || say "replay  $seeded job(s) exited 0 and left no probe file; seeded as dropped"

# ---------------------------------------------------------------- 6. the gate
# --only carries com/databricks as well as edu/sdsc/mmtf so the held
# m2-no-xml-in-jar can ask about a package the project does not own. scala/ is
# deliberately not in the list: the pin's uber jar shades 3863 scala-library
# classes, each arriving with its constant-pool references, for a fact the
# resolved graph already carries (NOTES 6.1.7).
#
# --probe repeats and gate-run.sh concatenates: the session probe off the
# candidate's own jar, and one file per replayed job, into $RUN/gate/probe.tsv.
# Both sources are read from outside $RUN/gate on purpose -- a probe file
# already at the destination path would be truncated by its own copy, which
# reads as an empty probe: a red probe-ran, a voided migrated-ness family, and a
# behavior gate that never opens, all from jobs that ran perfectly.
#
# What is NOT in that list, and was until round 1 of the item-5 review: the
# listener's own control run. gate.GateMain is the verifier's jar on the
# verifier's image; it reports spark.version 3.3.4, adaptive on and the modern
# statistical-aggregate semantics whatever the candidate did, because those are
# facts about this image. Concatenated into the graded table it satisfied every
# `atleast:1` reading of the run for free -- m1-session-334, which is a paid
# requirement, and all three of rung3.tsv's preconditions, which exist to say
# that the reading was taken on this step's engine. At this step the exclusion
# is load-bearing in a second place: the control's session is built by the
# verifier, and if this step's ANSI flag ever reached it, `m3-ansi-set` would
# be reading the instrument instead of the tree. The control still runs and
# is still checked, directly, at probe-live above: that is an instrument check
# (trap 6), and an instrument check is not evidence about the tree.
#
# The count is said out loud because it is the denominator of everything below
# it. 123 of the 140 planned jobs reach a SparkContext at all -- the other 17
# print a usage message or die on their arguments before one exists -- so a
# replay that probed far fewer than that has lost jobs, and replay-probed in
# m02.tsv is the standing rule that refuses to read such a run.
# Every class this build compiled is in the scan, whatever package it was put
# in: the prefix list is derived from this tree rather than hard-coded, because
# a hard-coded allowlist is a carve-out a candidate can compile a helper into
# (round-3 review, R5). The derivation, and the reason each of the three floor
# prefixes is in it, is in gates/only-prefixes.sh. It lives there rather than
# here because the authoring rehearsals scan the same trees with the same
# scanner, and a rehearsal that read a different subset of a jar than this
# verifier reads would be exercising a different gate (trap 6).
only=$(/tests/gates/only-prefixes.sh /app)
# An empty list is not "scan nothing": class-scan.py reads it as no
# restriction at all, which would pull the shaded dependencies' 318
# reflective references into the scan and turn no-reflective-dispatch red on
# any tree at all, gold included. If the helper produced nothing, fall back
# to the floor rather than to everything, and say which happened.
[ -n "$only" ] || { only=edu/sdsc/mmtf,com/databricks,gate
    say "gate    --only helper produced nothing; falling back to the floor"; }
say "gate    --only names $(printf '%s\n' "$only" | tr , '\n' | wc -l) class prefix(es) from this build and this tree"
phase "gate: scanners as nobody, facts and rules as root"
probes=(--probe "$RUN/probe.tsv")
nprobe=0
for f in "$PROBEDIR"/*.tsv; do
    [ -f "$f" ] || continue
    # --probe-labelled and not --probe when root named the file: the label the
    # gate attributes these rows to then comes from root's token table rather
    # than from a row the candidate's JVM wrote. The session probe keeps
    # --probe -- it is one program into a file of its own, run by root.
    if [ -n "$STREAMON" ]; then
        probes+=(--probe-labelled "$(basename "$f" .tsv)=$f")
    else
        probes+=(--probe "$f")
    fi
    nprobe=$((nprobe + 1))
done
say "gate    $nprobe per-job probe file(s) from the replay\
${STREAMON:+, named by root from its own token table}, plus 1 session probe"
env POM_SCAN_ONLINE=1 GATE_AS="$GATE_AS" GATE_CACHE="$RUN/astcache" GATE_M2_PRISTINE="$GATE_M2_PRISTINE" /tests/gates/gate-run.sh \
    --rules "$RULES" \
    --out "$RUN/gate" \
    --tree /app --build /app \
    --classes '/app/target/*.jar' \
    --only "$only" \
    "${probes[@]}" 2>&1 | tee -a "$LOG"

# ---------------------------------------------------------------- 7. and again, as root
# findings.tsv above was written by root, but from facts its scanners produced
# while candidate code was running. So the verdicts are
# re-derived here from a root-owned copy of the five fact files, and score.py
# reads *this* file and diffs it against that one. rules.py is deterministic
# over its inputs: same rules and same facts give the same rows, so a difference
# is not noise -- it says a fact moved between being written and being read.
phase "gate: the same rules again, as root over a copy"
mkdir -p "$RUN/gateroot"
for f in ast pom bytecode build probe; do
    [ -f "$RUN/gate/$f.tsv" ] && cp -f "$RUN/gate/$f.tsv" "$RUN/gateroot/$f.tsv"
done
chmod -R go-w "$RUN/gateroot"
# And score.py reads the probe rows out of this copy rather than out of
# $RUN/gate/probe.tsv, for the same reason findings.root.tsv exists and one
# more. The gate's copy was written by a process running as nobody into a
# directory candidate code can reach, and section 8 below runs the repository's
# own suite as that same uid: a surefire test that appended one row to it would
# be writing the fact the behavior gate is about to read. /score is root-owned
# and 0755, so uid 65534 can neither rewrite this file nor replace the directory
# holding it, and nothing runs as that uid after the gate except a suite that
# can no longer reach the rows (round-1 review, R5).
if [ -s "$RUN/gateroot/probe.tsv" ]; then
    ok probe-snapshot "$(grep -c . "$RUN/gateroot/probe.tsv") row(s), root-owned"
else
    no probe-snapshot "the gate produced no probe rows for root to copy, so the behavior gate would read an unwritable file that is not there"
fi
srcs=()
for f in build ast pom bytecode probe; do
    [ -f "$RUN/gateroot/$f.tsv" ] && srcs+=("$f=$RUN/gateroot/$f.tsv")
done
python3 /tests/gates/rules.py --rules "$RULES" "${srcs[@]}" \
    > "$RUN/gate/findings.root.tsv" 2> "$RUN/gateroot/rules.err"
if [ -s "$RUN/gateroot/rules.err" ]; then
    # gate-run.sh appends exactly this line in the same case. The two files have
    # to be built the same way or the diff below reports the difference between
    # two scripts rather than between two readings.
    printf 'ERROR\tgate-run\t0\t%s\n' \
        "$(head -1 "$RUN/gateroot/rules.err" | cut -c1-200)" >> "$RUN/gate/findings.root.tsv"
fi
say "gate    $(awk -F'\t' '$1 == "PASS"' "$RUN/gate/findings.root.tsv" | wc -l) pass, \
$(awk -F'\t' '$1 == "FAIL"' "$RUN/gate/findings.root.tsv" | wc -l) fail, \
$(awk -F'\t' '$1 == "ERROR"' "$RUN/gate/findings.root.tsv" | wc -l) error (root re-evaluation)"

# ---------------------------------------------------------------- 8. the tree's own tests
# Last, because it is the one step that destroys the tree: src/test is replaced
# with the commit's own copy, so a candidate that deleted or weakened a test is
# measured against the suite the repository actually ships. Nothing after this
# point could run the replay again.
#
# The reports directory is removed first and only files newer than a marker are
# collected, so a TEST-*.xml planted in the image is not a passing test.
phase "inherited tests: the repository's own suite, from a pristine copy"
rm -rf /app/src/test /app/target/surefire-reports
cp -a /tests/overlay/src/test /app/src/test
chown -R 65534:65534 /app/src /app/target 2>/dev/null || true
touch "$RUN/.tmark"; sleep 1
( cd /app && run_unprivileged timeout -k 30 1500 mvn -B test \
    -Dtest='**/*Test.java' -DfailIfNoTests=false \
    -Dsurefire.timeout=900 -Dspark.driver.host=127.0.0.1 \
    -Dmaven.test.failure.ignore=true ) > "$RUN/tests.log" 2>&1
testrc=$?
reap_unprivileged
# -type f: a link in the reports directory is not a report, and root copying
# through one would carry a sealed recording into $RUN/surefire.
fresh=$(find /app/target/surefire-reports -type f -name 'TEST-*.xml' -newer "$RUN/.tmark" 2>/dev/null | wc -l)
find /app/target/surefire-reports -type f -name 'TEST-*.xml' -newer "$RUN/.tmark" \
    -exec cp -f {} "$RUN/surefire/" \; 2>/dev/null
say "tests   rc=$testrc, $fresh fresh report(s) of \
$(find /app/target/surefire-reports -name 'TEST-*.xml' 2>/dev/null | wc -l) present"
[ "$testrc" = 0 ] || say "$(tail -8 "$RUN/tests.log")"

# --------------------------------------------------- the overlay's own control
# The last precondition, and the only one that can only be taken after the
# suite has run. `StructType.toString()` dropped the space after the comma
# between 3.2.4 and 3.3.4, and CustomReportServiceTest hardcodes the old
# rendering as a string literal, so both of its two tests are red at this rung
# -- including on the golden tree, because the copy of src/test a candidate
# could have corrected was deleted ten lines above. score.py therefore leaves
# the class out of both baselines (46 classes, 140 tests).
#
# What this control asks is whether the overlay arrived and ran, which is a
# fact about this verifier: two tests reported, neither errored, neither
# skipped. It deliberately does not ask which way the two came out.
#
# That last clause is the fix item 4.4 made here, and the reason is measured
# rather than reasoned (NOTES 6.4.5). `spark-core` is a `provided` dependency,
# and `provided` is on the test classpath, so surefire runs against the Spark
# the *candidate's* pom resolves and not against $SPARK_HOME's. Two trees
# identical to the byte but for four characters in <spark.version>, both run in
# mmtf-rung-r05:1, whose own Spark is 3.3.4:
#
#     pom 3.2.4   tests="2" failures="0"    the old rendering; the literal matches
#     pom 3.3.4   tests="2" failures="2"    the new rendering; the literal is stale
#
# So "failures=2" is a reading of m1 wearing a precondition's clothes. Requiring
# it would make a tree that did m2 and m3 and left the pom alone fail a
# *precondition* -- score.py preflight() returns 0.0 with VERIFIER PRECONDITION
# FAILED -- so m1, which weights.tsv prices at 0.10, would cost that tree the
# other 0.56 as well. m1 is read by `m1-spark-334` and `m1-session-334`, once,
# where it is paid for.
#
# What stays red is everything that is really about the run: no report at all
# (the overlay did not arrive), a count other than two (it arrived edited), an
# error rather than a failure (the web service the class calls did not answer),
# a skip (surefire found the class and declined to run it), or one failure of
# two, which is neither rendering and so is not a run either baseline describes.
crs=$RUN/surefire/TEST-edu.sdsc.mmtf.spark.webservices.CustomReportServiceTest.xml
crs_attr=$(grep -o '<testsuite [^>]*' "$crs" 2>/dev/null | head -1)
crs_n=$(printf '%s' "$crs_attr" | grep -o 'tests="[0-9]*"' | head -1 | tr -dc 0-9)
crs_f=$(printf '%s' "$crs_attr" | grep -o 'failures="[0-9]*"' | head -1 | tr -dc 0-9)
crs_e=$(printf '%s' "$crs_attr" | grep -o 'errors="[0-9]*"' | head -1 | tr -dc 0-9)
crs_s=$(printf '%s' "$crs_attr" | grep -o 'skipped="[0-9]*"' | head -1 | tr -dc 0-9)
if [ "${crs_n:-}" = 2 ] && [ "${crs_e:-}" = 0 ] && [ "${crs_s:-}" = 0 ] \
   && { [ "${crs_f:-}" = 0 ] || [ "${crs_f:-}" = 2 ]; }; then
    ok overlay-control "CustomReportServiceTest reports tests=2 \
failures=$crs_f errors=0 skipped=0, so the pristine overlay arrived and ran; \
which of the two schema renderings it matched is m1's business and is read there"
else
    no overlay-control "CustomReportServiceTest reports \
tests=${crs_n:-<no report>} failures=${crs_f:-<no report>} \
errors=${crs_e:-<no report>} skipped=${crs_s:-<no report>}; the overlay this \
verifier scores against reports two tests, no errors and no skips, and either \
both failing or both passing"
fi

# ---------------------------------------------------------------- 9. the number
phase "scoring"
python3 /tests/score.py --tests /tests --run "$RUN" 2>&1 | tee -a "$LOG"

# The evidence Harbor reads back. Everything a reviewer needs to disagree with
# the number without re-running half an hour of Spark.
cp -f "$RUN/score.log" "$RUN/preflight.tsv" "$RUN/test.log" \
      "$RUN/gate/findings.root.tsv" "$RUN/suite/summary.tsv" \
      /logs/verifier/ 2>/dev/null || true
# The concatenated probe table too, because at this step it is the evidence
# behind three of the five numbers: half of m1, all of m3 and the behavior gate
# are readings of it, and a reviewer who cannot see it can only re-run. m2 is
# the exception -- it is read off the source and the bytecode, and its evidence
# is findings.root.tsv beside this.
[ -f "$RUN/gate/probe.tsv" ] && cp -f "$RUN/gate/probe.tsv" \
      /logs/verifier/probe.tsv 2>/dev/null || true
[ -f "$RUN/gate/findings.tsv" ] && cp -f "$RUN/gate/findings.tsv" \
      /logs/verifier/findings.nobody.tsv 2>/dev/null || true

say "total   $((SECONDS / 60))m$((SECONDS % 60))s"
report "$(cat "$RUN/reward.txt" 2>/dev/null || echo 0)"
