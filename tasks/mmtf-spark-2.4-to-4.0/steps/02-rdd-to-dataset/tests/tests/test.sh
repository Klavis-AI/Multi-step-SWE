#!/usr/bin/env bash
# Score step 2 (02-rdd-to-dataset) of the Spark 2.4 -> 4.0 migration.
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
# Five and six are swapped against step 1, and that is this step's design
# rather than a tidy-up. What step 1 asked -- which Spark does this build
# resolve -- is answerable from a tree standing still. What step 2 asks -- does
# the count come back through the SQL engine -- is not answerable from a tree
# at all: `createDataset`, `createDataFrame`, `toDF` and `as(Encoders.bean())`
# are one migration spelled four ways, and an AST rule over them would grade
# spelling. So the facts the gate reads are the ones the replay produces while
# the tree runs, and the gate reads them afterwards.
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
# it requires every rule in step02.tsv to go red against its own counterexample
# fixture, so a rule that stopped matching anything cannot reach this run green.
selftest() {  # selftest <name> <cmd...>
    local name=$1; shift
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" = 0 ]; then ok "selftest.$name" "$(printf '%s' "$out" | tail -1)"
    else no "selftest.$name" "rc=$rc $(printf '%s' "$out" | tail -2 | tr '\n' ' ')"; fi
}
selftest rules      python3 /tests/gates/rules.py --rules /tests/gates/rules/step02.tsv --selftest
selftest build-scan python3 /tests/gates/build-scan.py --selftest
selftest pom-scan   python3 /tests/gates/pom-scan.py --selftest
selftest projection python3 /tests/suite/projection.py --selftest
selftest inputs     python3 /tests/suite/inputs.py --selftest
selftest norm       python3 /tests/suite/norm.py --selftest
selftest score      python3 /tests/score.py --selftest

# ------------------------------------------------------- the listener, made to see
# Trap 6 in the shape this step needs it. Every m1..m4 rule, and the behavior
# gate score.py opens on, is a reading of one listener's output, and all of
# them are false in the same direction when that listener sees nothing. A run
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
# 0.22 of this step's weight. score.py reads it and score.py runs as root;
# run-suite.sh reads jobs.tsv and writes its own output, and nothing that runs
# as 65534 opens ref/ at all. Left world-readable it is the exam and the mark
# scheme in the same room: a main that printed its own job's .norm byte for
# byte would replay perfectly without migrating a line, and the projection
# cannot tell a correct program from a correct transcript. 0700 on the
# directory is enough -- nobody cannot walk into it whatever the files say.
# Steps 4 and 5 have sealed it since their round-3 review (R5); this step did
# not until the 2026-10-12 reward-hacking audit (F1).
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
# Online, and deliberately. The one thing this step asks the candidate to change
# is what the build resolves, so a candidate who legitimately lands on a version
# the image's warm repository never heard of must be able to fetch it. Offline
# here would turn that into a red pom-resolves, which is a standing rule: the
# whole reading would be void, for exactly the change the step is about.
phase "prebuild: mvn -B clean package -DskipTests"
( cd /app && run_unprivileged timeout -k 30 900 mvn -B clean package -DskipTests ) > "$RUN/prebuild.log" 2>&1
prerc=$?
JAR=$(ls /app/target/mmtf-spark-*.jar 2>/dev/null | grep -v -e original- -e -sources -e -javadoc | head -1)
say "prebuild rc=$prerc, jar=${JAR:-<none>}, $(wc -l < "$RUN/prebuild.log") log line(s)"
[ "$prerc" = 0 ] || say "$(tail -5 "$RUN/prebuild.log")"

# ---------------------------------------------------------------- 4. the probe
# GATE_PROBE_LABEL, which step 1 did not need, because at this step the
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
# this file: the facts m1..m4 are read from are what the tree *did*, and the
# tree does it here.
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
# taken with it attached, at this rung, and 140 of 140 came back identical:
# NOTES 6.2.4 is that measurement, and build/p02 is its output.
phase "replay: 140 recorded jobs at CORES=1, with the listener attached"
run_unprivileged env APP=/app SUITE=/tests/suite OUT="$RUN/suite" DATA=/data \
    CORES=1 PHASES="1 2" PROBE="${PJAR:-}" \
    bash /tests/suite/run-suite.sh > "$RUN/suite.log" 2>&1
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
say "replay  rc=$suiterc, $(grep -c . "$RUN/suite/summary.tsv") job(s) ran of \
$(grep -c . /tests/suite/jobs.tsv) planned, \
$(ls "$RUN/suite/probe"/*.tsv 2>/dev/null | wc -l) probe file(s)"
[ "$suiterc" = 0 ] || say "$(tail -8 "$RUN/suite.log")"

# ---------------------------------------------------------------- 6. the gate
# --only carries com/databricks as well as edu/sdsc/mmtf so the held
# m2-no-xml-in-jar can ask about a package the project does not own. scala/ is
# deliberately not in the list: the pin's uber jar shades 3863 scala-library
# classes, each arriving with its constant-pool references, for a fact the
# resolved graph already carries (NOTES 6.1.7).
#
# --probe repeats and gate-run.sh concatenates: the listener's control run, the
# session probe, and one file per replayed job, in that order, into
# $RUN/gate/probe.tsv. All three sources are read from outside $RUN/gate on
# purpose -- a probe file already at the destination path would be truncated by
# its own copy, which reads as an empty probe: a red probe-ran, a voided
# migrated-ness family, and a behavior gate that never opens, all from jobs
# that ran perfectly.
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
probes=(--probe "$RUN/probe.selftest.tsv" --probe "$RUN/probe.tsv")
nprobe=0
for f in "$RUN/suite/probe"/*.tsv; do
    [ -f "$f" ] || continue
    probes+=(--probe "$f"); nprobe=$((nprobe + 1))
done
say "gate    $nprobe per-job probe file(s) from the replay, plus 2 session probe(s)"
env POM_SCAN_ONLINE=1 GATE_AS="$GATE_AS" GATE_CACHE="$RUN/astcache" GATE_M2_PRISTINE="$GATE_M2_PRISTINE" /tests/gates/gate-run.sh \
    --rules /tests/gates/rules/step02.tsv \
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
python3 /tests/gates/rules.py --rules /tests/gates/rules/step02.tsv "${srcs[@]}" \
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

# ---------------------------------------------------------------- 9. the number
phase "scoring"
python3 /tests/score.py --tests /tests --run "$RUN" 2>&1 | tee -a "$LOG"

# The evidence Harbor reads back. Everything a reviewer needs to disagree with
# the number without re-running half an hour of Spark.
cp -f "$RUN/score.log" "$RUN/preflight.tsv" "$RUN/test.log" \
      "$RUN/gate/findings.root.tsv" "$RUN/suite/summary.tsv" \
      /logs/verifier/ 2>/dev/null || true
# The concatenated probe table too, because at this step it is the evidence
# behind six of the seven numbers: every m1..m4 verdict and the behavior gate
# are readings of it, and a reviewer who cannot see it can only re-run.
[ -f "$RUN/gate/probe.tsv" ] && cp -f "$RUN/gate/probe.tsv" \
      /logs/verifier/probe.tsv 2>/dev/null || true
[ -f "$RUN/gate/findings.tsv" ] && cp -f "$RUN/gate/findings.tsv" \
      /logs/verifier/findings.nobody.tsv 2>/dev/null || true

say "total   $((SECONDS / 60))m$((SECONDS % 60))s"
report "$(cat "$RUN/reward.txt" 2>/dev/null || echo 0)"
