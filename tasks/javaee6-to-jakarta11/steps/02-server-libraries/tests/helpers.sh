#!/usr/bin/env bash
# Per-step copy: the verifier image is built with this step's tests/ as the
# Docker build context, so it cannot COPY from the task root. Every step's
# tests/ holds an identical copy of this file; keep them in sync.
set -u

# Unprivileged identity for candidate code. nobody:nogroup already exists in
# debian:bookworm-slim.
SCORE_UID=65534
SCORE_GID=65534

# The scorer must not depend on the PATH it was invoked with. The image sets
# one, but a login shell, a Makefile or a harness wrapper each replace it, and
# the first symptom is `env: 'java': No such file or directory` out of
# launch.sh -- which is indistinguishable from a candidate whose artifact will
# not start. Pin it from JAVA_HOME, which the Dockerfile always sets, so that
# every step's toolchain is the one its own image installed.
: "${JAVA_HOME:?verifier image must set JAVA_HOME}"
PATH="$JAVA_HOME/bin:/opt/maven/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH
command -v java >/dev/null || { echo "no java on PATH=$PATH" >&2; }
command -v mvn  >/dev/null || { echo "no mvn on PATH=$PATH"  >&2; }

# Lock the reward directory to root. Call this first in test.sh.
#
# Harbor chmods /logs/verifier to 777 just before this container starts, so
# anything running here can overwrite reward.json -- including candidate code
# the verifier builds or runs. Writing the real score last does not help: a
# detached child outlives the scorer and writes after it.
#
# This runs after Harbor's chmod, so this is the mode that sticks.
harden_reward_dir() {
    mkdir -p /logs/verifier
    chown root:root /logs/verifier 2>/dev/null || true
    chmod 700 /logs/verifier
    _bound_whole_run
}

# The whole run, under one wall-clock limit.
#
# Each candidate stage runs the candidate's own Maven (package, test, the
# dependency listing, the OpenRewrite dry run) and the recorded driver, and none
# of those had a bound of its own: a test that never returns, or a server that
# hangs, would hold the verifier until Harbor killed the container at
# [steps.verifier] timeout_sec = 2400. Harbor discards a step whose verifier
# times out even when reward.json exists, drops it from the mean and aborts
# every later step -- so a tree that hangs here would lock in the steps already
# scored instead of scoring this one.
#
# So harden_reward_dir hands the scoring body to a child of this shell: the same
# test.sh, run again under `timeout`, TERM at VERIFY_WALL_SEC and KILL 30 s
# later (timeout signals the child's whole process group). If the child wrote no
# reward, this shell reports 0. /logs/verifier is root-owned 0700 throughout, so
# nothing of the candidate's that outlives the child can write it. Worst case:
# 2100 + 30 + report, inside 2400 - 240. The dearest oracle step measured is
# ~280 s, so a correct tree does not feel this. Every candidate invocation also
# carries its own `timeout -k` so one hung phase is bounded well below this.
VERIFY_WALL_SEC=2100
_bound_whole_run() {
    [ -z "${JEE_VERIFY_BOUNDED:-}" ] || return 0
    if [ ! -f "$0" ]; then
        echo "VERIFIER-DEADLINE: cannot re-run $0 under a limit; scoring unbounded" >&2
        return 0
    fi
    local rc=0
    JEE_VERIFY_BOUNDED=1 timeout -k 30 "${VERIFY_WALL_SEC}" bash "$0" || rc=$?
    if [ "${rc}" -eq 124 ] || [ "${rc}" -eq 137 ]; then
        echo "VERIFIER-DEADLINE: the scoring run passed ${VERIFY_WALL_SEC}s and was stopped (rc=${rc})" >&2
    fi
    if [ ! -f /logs/verifier/reward.json ]; then
        echo "VERIFIER-DEADLINE: the scoring run wrote no reward (rc=${rc}); writing 0" >&2
        report 0
    fi
    exit 0
}

# Run candidate-authored code as nobody instead of root.
#
# Needed for harden_reward_dir to mean anything: root can write a root-owned
# directory whatever its mode. Use it everywhere candidate code runs -- a
# Makefile, a linked library's static initializer, a probe compiled against
# candidate source -- not just a service.
#
# The environment is the verifier's, every Maven input in it included:
# MAVEN_OPTS is whatever the caller sets; MAVEN_ARGS and
# MAVEN_DEBUG_OPTS are empty; MAVEN_SKIP_RC keeps mvn from sourcing
# ~/.mavenrc and the system rc files; MAVEN_BASEDIR names the project
# directory, so mvn takes .mvn/ from there and searches no parent for one
# (a caller running Maven elsewhere passes its own); and the JVM's own option
# variables are dropped. maven_inputs_reset, below, clears what is left.
run_unprivileged() {
    ${UNPRIVILEGED_EXEC:-} \
    setpriv --reuid "$SCORE_UID" --regid "$SCORE_GID" --clear-groups \
        env -u JAVA_TOOL_OPTIONS -u JDK_JAVA_OPTIONS -u _JAVA_OPTIONS \
            HOME=/score/home TMPDIR=/score/tmp \
            JAVA_HOME="$JAVA_HOME" \
            PATH="$PATH" \
            LANG=C.UTF-8 LC_ALL=C.UTF-8 TZ=UTC \
            MAVEN_OPTS="${MAVEN_OPTS:-}" \
            MAVEN_ARGS= MAVEN_DEBUG_OPTS= MAVEN_SKIP_RC=true MAVEN_BASEDIR=/app \
            "$@"
}

# Background form, for a process that has to outlive this shell and still be
# killable by the pid we wrote down.
#
# `run_unprivileged cmd &` backgrounds a *subshell*, and $! is that subshell,
# not the JVM it forks underneath. Killing it leaves the JVM holding the port:
# stop_servers reports success, the next launch gets "address already in use",
# and -- worse -- an HTTP probe aimed at that port answers from the previous
# instance, so a run can score a server it did not start. The exec makes the
# backgrounded shell *become* setpriv, so $! is the pid that has to die.
#
# $! is set in the caller, so `run_unprivileged_bg cmd; pid=$!` works.
run_unprivileged_bg() {
    { UNPRIVILEGED_EXEC=exec run_unprivileged "$@"; } &
}

# report <score>
#
#   reward  this step's local score in [0, 1]. Harbor's
#           multi_step_reward_strategy = "mean" averages this key across every
#           step that produced a verifier_result.
#
# Always exits 0: a non-zero exit is a verifier crash, and Harbor then skips
# remaining steps even without min_reward.
report() {
    local reward
    reward=$(awk -v v="${1:-0}" 'BEGIN {
        v += 0
        if (v < 0) v = 0
        if (v > 1) v = 1
        printf "%.6f", v
    }')
        # Binary step reward: 1 only when the step reward is exactly 1.0.
    if [[ "${reward}" != "1.000000" ]]; then reward="0.000000"; else reward="1.000000"; fi
printf '{"reward":%s}\n' "$reward" > /logs/verifier/reward.json
    # Reopen to 0755 so a local docker run, which reads this from the host as
    # an ordinary user, can collect it. Still root-owned, so the candidate
    # still cannot write it.
    chmod 755 /logs/verifier 2>/dev/null || true
    chmod 644 /logs/verifier/reward.json 2>/dev/null || true
    exit 0
}

# ---------------------------------------------------------------------------
# Scoring scratch space.
#
# Everything the scorer generates lives under /score, which the candidate's
# tree never sees. /app holds only the candidate's own work.
#
# Who owns what. The evidence --
# /score/out and /score/log -- belongs to root and is written only by root, so
# nothing candidate code starts later (the dependency listing's build, the
# servers) can rewrite a fact after the stage that recorded it, or plant a
# symlink for root to write through. Candidate code and the unprivileged
# scanners get /score/home, /score/work and /score/tmp; what root needs from
# there it copies with copy_as_unprivileged, after the stage has been reaped.
# /score itself is root's, so neither /score/out nor /score/log can be renamed
# away and replaced. SCORE_APP is the root-held copy of /app that score.py
# reads (scan.sh takes it), for the same reason. The bytecode cache prefix is
# root-only: Python *reads* cached bytecode from that prefix even with
# PYTHONDONTWRITEBYTECODE set, so under the world-writable /score/tmp a planted
# .pyc would have run inside the root scorer.
# ---------------------------------------------------------------------------
score_setup() {
    rm -rf /score
    mkdir -p /score/home /score/tmp /score/work /score/log /score/out \
             /score/pycache
    chown root:root /score /score/log /score/out /score/pycache
    chmod 0755 /score /score/log /score/out
    chmod 0700 /score/pycache
    chown "$SCORE_UID:$SCORE_GID" /score/home /score/work
    chmod 1777 /score/tmp
    # Syntax-checking or importing the scorer's own Python writes __pycache__
    # into whatever directory it is read from. Keep every byte of that out of
    # /tests and out of any Docker build context.
    export PYTHONPYCACHEPREFIX=/score/pycache
    export PYTHONDONTWRITEBYTECODE=1
    export SCORE_APP=/score/app
}

# copy_as_unprivileged <src> <dest>
#
# Bring a file the unprivileged side produced into root's evidence. The read
# is done as that user, so a symlink planted in its place can only reach what
# the unprivileged user could read anyway -- never /tests/expected.jsonl into a
# file the servers can then read. The write is root's, into root's directory.
# Call only after the stage that wrote <src> has been reaped.
copy_as_unprivileged() {
    run_unprivileged cat -- "$1" > "$2" 2>/dev/null || : > "$2"
}

# Stop every process running as the unprivileged user, by pid, and wait until
# none is left.
#
# Candidate code runs as SCORE_UID (the build, the servers) and so does the
# driver, and /score/work and /score/tmp belong to that user, so a child the
# candidate's build or server leaves behind -- `setsid`, a double fork -- could
# rewrite what a stage left there (the driver's observations, the scans) before
# root copies it into /score/out. Locking /logs/verifier does not reach those.
# Call this wherever a stage that ran candidate code ends and root is about to
# read what it left. The pids are
# read from this container's own /proc, so nothing outside it is named; each
# round stops them all before killing them, so a forking child cannot replace
# itself between the two, and rounds repeat until /proc shows none.
reap_unprivileged() {
    local round pid uid pids
    for round in 1 2 3 4 5 6 7 8 9 10; do
        pids=""
        for pid in /proc/[0-9]*; do
            uid=$(awk '/^Uid:/ {print $2; exit}' "$pid/status" 2>/dev/null) \
                || continue
            [ "$uid" = "$SCORE_UID" ] && pids="$pids ${pid#/proc/}"
        done
        [ -n "$pids" ] || return 0
        kill -s STOP $pids 2>/dev/null || true
        kill -s KILL $pids 2>/dev/null || true
        sleep 0.2
    done
    say "reap: processes of uid $SCORE_UID survived ten rounds"
}

# Give /app to the unprivileged user. Call before an unprivileged build.
#
# /app arrives owned by whoever wrote the artifact. Without this the build
# cannot create its output directory, so every candidate scores 0 and the step
# looks merely very hard instead of broken.
give_app_to_unprivileged() {
    chown -R "$SCORE_UID:$SCORE_GID" /app 2>/dev/null || true
}

# maven_inputs_reset <basedir> <label>
#
# Every Maven run the verifier makes takes its configuration from the poms, its
# own command line and the environment run_unprivileged pins, and from nothing
# else the candidate can reach. Left alone, Maven reads three
# more things: <basedir>/.mvn/ (maven.config is prepended to the command line,
# so a `-q` there drops every [WARNING] warning-free-build counts; jvm.config
# joins the JVM's options; extensions.xml loads code into Maven's core), the rc
# files, and ~/.m2's settings.xml and toolchains.xml. So right before each run,
# as root and with nothing of the candidate's running: <basedir>/.mvn,
# whatever it is, goes to a root-only directory unread (a rename, which
# follows no link), and the home directory the run gets starts empty, so
# nothing an earlier run left there is read either.
maven_inputs_reset() {
    local base="$1" label="${2:-maven}" held
    mkdir -p /score/held
    chown root:root /score/held
    chmod 0700 /score/held
    if [ -e "$base/.mvn" ] || [ -L "$base/.mvn" ]; then
        held=$(mktemp -u /score/held/mvn.XXXXXX)
        mv -T -- "$base/.mvn" "$held" 2>/dev/null || rm -rf -- "$base/.mvn"
        record maven.config.held "$label: $base/.mvn"
    fi
    rm -rf /score/home
    mkdir -p /score/home
    chown "$SCORE_UID:$SCORE_GID" /score/home
}

# instrument_guard_ran <log> <groupId:artifactId>
#
# Two scored scans run a plugin over the candidate's reactor: scan.sh's
# dependency listing and step 10's OpenRewrite dry run (scan-extra.sh). They
# give it its options as -D user properties, and Maven reads a mojo parameter
# from the pom first -- the plugin's <configuration> in build plugins,
# pluginManagement or a profile, a default-cli execution, the plugin's own
# <dependencies>, a project property named like the parameter -- so a pom
# could have told the instrument what to find. Both
# runs therefore load GUARD_JAR, a Maven core extension the image compiles
# from guard/InstrumentGuard.java, which drops all of that for the one plugin
# it is given, from the effective models, before Maven plans the run. It logs
# one line naming that plugin; a run whose log lacks the line ran without the
# guard, and its caller fails it as an instrument that did not run.
GUARD_JAR=/opt/guard/instrument-guard.jar
instrument_guard_ran() {
    grep -q -F "[INFO] [score-guard] $2: " "$1" 2>/dev/null
}

# guard_report <log> <profiles|javac>
#
# The guard also reports, in every run that loads it -- build.sh's scored
# `package` included -- one JSON line per project naming the profiles active
# in its whole pom lineage, and one per maven-compiler-plugin execution with
# its effective configuration. It logs them while Maven reads
# the reactor, before the first mojo runs, so only the part of <log> above the
# first `--- ` mojo line is read: nothing the candidate's build prints can
# stand in for them.
guard_report() {
    awk -v k="[INFO] [score-guard] $2 " '
        /^\[INFO\] --- / { exit }
        index($0, k) == 1 { print substr($0, length(k) + 1) }' "$1" 2>/dev/null
}

# profiles_match <log> <label>
#
# An instrument run measures the scored build only if Maven activated the same
# profiles in it: activation is decided per session, from that session's -D
# options, files on disk (target/ exists after `package`), JDK and OS, and a
# profile active in one run and not the other moves dependencies, modules and
# plugins between them. So each instrument run's per-project active-profile
# lines must equal the scored package run's (profiles.package.txt, build.sh),
# and an empty reference matches nothing.
profiles_match() {
    guard_report "$1" profiles | LC_ALL=C sort > "/score/out/profiles.$2.txt"
    [ -s /score/out/profiles.package.txt ] \
        && cmp -s /score/out/profiles.package.txt "/score/out/profiles.$2.txt"
}

# SCORED_PACKAGE
#
# The scored `package` command's Maven and its options, as build.sh runs it
# (the instruction's `mvn -DskipTests package`, plus the verifier's own
# repository, colour and guard), without the goal. The two instrument runs
# start from this same array and add only their own instrument options, so
# every user property the scored build saw is one the instrument run sees too
# -- profile activation reads user properties, and profiles_match holds the
# instrument runs to the scored build's profiles, so an extra or a missing -D
# in an instrument run would fail an honest profile keyed on it. What still
# differs is the instrument's own options and nothing else.
SCORED_PACKAGE=(mvn -B -ntp -Dmaven.repo.local="${M2_REPO:-/opt/m2}"
                -Dstyle.color=never -Dmaven.ext.class.path="$GUARD_JAR"
                -DskipTests)

# instrument_repo_intact <label>
#
# /opt/m2 belongs to the user the candidate's build runs as, so a build could
# replace a jar or pom an instrument loads -- before the instrument run, or
# inside it, from a package-phase plugin that runs before the instrument's own
# class realm is made. The image recorded every jar and pom the repository
# held when it was built (M2_MANIFEST, root-owned; the Dockerfile says how),
# and this says whether they all still have those bytes. Read as the
# unprivileged user, bounded: a link or a FIFO in a jar's place reads as a
# mismatch or a timeout, never as root opening something. A file that is gone
# is ignored -- Maven fetches it again from the repository it came from.
M2_MANIFEST=/opt/guard/m2.sha256
instrument_repo_intact() {
    [ -s "$M2_MANIFEST" ] || return 1
    run_unprivileged timeout -k 10 300 \
        sha256sum --check --quiet --strict --ignore-missing "$M2_MANIFEST" \
        > "/score/log/m2check.$1.log" 2>&1
}

# say <words...> -- one progress line, timestamped, to the verifier log.
say() {
    printf '[%s] %s\n' "$(date -u +%H:%M:%S)" "$*" >&2
}

# record <key> <value> -- append one fact to the scorer's machine-readable
# journal. Everything a human would want to re-derive later goes here, not
# only into prose.
record() {
    printf '%s\t%s\n' "$1" "$2" >> /score/out/journal.tsv
}
