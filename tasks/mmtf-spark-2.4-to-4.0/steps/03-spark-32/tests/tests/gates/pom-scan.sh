#!/usr/bin/env bash
# Ask Maven what a tree's build actually resolves to, and turn the answer into
# facts (pom-scan.py).
#
# Offline by default. Both plugins are pinned and pre-warmed into the verifier's
# /scratch/.m2 (pkg/verifier/Dockerfile.data), so the scanner never has to reach
# the network for its own tools -- and if the warm layer ever stops doing its
# job, `-o` turns that into a loud failure here rather than a quiet 40-second
# download at scoring time. POM_SCAN_ONLINE=1 lifts it.
#
# A verifier whose step asks the candidate to *change* the dependency graph sets
# it. `-o` then resolves the candidate's graph against a cache warmed from the
# author's, and a legitimate version the author never picked would arrive as an
# `error` fact, reddening the standing `pom-resolves` rule and voiding the whole
# reading -- for doing the thing the step asked for. Step 1 is such a step and
# its verifier sets POM_SCAN_ONLINE=1; rule 9's public network is there for
# exactly this. A step that only reads a graph it did not ask anyone to touch
# should leave it off.
#
# Maven's own exit code is not the finding. A tree whose POM does not parse is
# something the gate has to be able to *report*, so a failed invocation becomes
# an `error` fact and the script still exits 0 -- same contract as ast-scan.
#
#   usage: pom-scan.sh <tree> [<workdir>]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${1:?usage: pom-scan.sh <tree> [workdir]}
WORK=${2:-$(mktemp -d /tmp/pom-scan.XXXXXX)}
HELP_PLUGIN=org.apache.maven.plugins:maven-help-plugin:3.4.0
DEP_PLUGIN=org.apache.maven.plugins:maven-dependency-plugin:3.6.1
OFF=-o
[ "${POM_SCAN_ONLINE:-0}" = 1 ] && OFF=

mkdir -p "$WORK"

# Maven also takes configuration from places the candidate's build can write:
# .mvn/{jvm,maven}.config and .mvn/extensions.xml under the project base
# directory, ~/.mavenrc and /tmp/.mavenrc, and plugins from the local
# repository the candidate's own build ran against (/scratch/.m2, nobody's).
# None of it may shape this reading (root-tool-config sweep, 2026-10-13): the
# base directory is an empty one, rc files are off, and when test.sh hands
# over the read-only snapshot it took before any candidate code ran
# (GATE_M2_PRISTINE), plugins and artifacts come from that, with a fresh
# head for anything an online scan has to download. gate-run.sh makes the
# base directory and the head (POM_SCAN_BASEDIR, POM_SCAN_HEAD) fresh, outside
# this script's work directory and just before it runs (wave H round 3,
# G2-m1): the work directory belongs to the uid the candidate's build runs as,
# and a base directory or head made in it was whatever that build had left.
# Without them -- an authoring rehearsal -- both are made in the work directory.
BASEDIR=${POM_SCAN_BASEDIR:-$WORK/basedir}
mkdir -p "$BASEDIR"
export MAVEN_SKIP_RC=1 MAVEN_BASEDIR="$BASEDIR"
if [ -n "${GATE_M2_PRISTINE:-}" ] && [ -d "$GATE_M2_PRISTINE" ]; then
    HEAD=${POM_SCAN_HEAD:-$WORK/m2}
    mkdir -p "$HEAD"
    export MAVEN_ARGS="-Dmaven.repo.local=$HEAD -Dmaven.repo.local.tail=$GATE_M2_PRISTINE"
fi

if [ ! -f "$TREE/pom.xml" ]; then
    printf 'error\tpom.xml\tno pom.xml under %s\n' "$TREE"
    exit 0
fi

# -Doutput/-DoutputFile rather than stdout: both plugins interleave their
# result with Maven's own chatter on stdout.
# Both bounded: they run the tree's own pom, and a call that never ends would
# hold the verifier past its limit.
#
# Both readings' options are the verifier's alone (wave H round 2, G2-M2;
# round 3, G2r2-M1). Maven takes a mojo parameter from the pom before it reads
# a -D option. A maven-dependency-plugin <configuration> holding
# <excludeGroupIds>, or a project property named `excludeGroupIds`, took a
# group out of deps.txt while the run still succeeded, and the `absent` rules
# over pom.resolved read only what was left; effective-pom's `artifact`, never
# given here, set the same way chooses whose effective POM is reported. So
# each run loads the instrument guard (GUARD_JAR: a Maven core extension the
# image compiles from guard/InstrumentGuard.java, the source
# javaee6-to-jakarta11 ships) for its own plugin. On the effective models,
# before Maven plans the run, it drops that plugin's <configuration>, its
# <dependencies> and a default-cli execution, and removes from every
# project's properties each name in that run's props list: the `${...}`
# property of every parameter of the goal, as the pinned plugin's own
# descriptor (META-INF/maven/plugin.xml) declares them -- 3 for
# help:effective-pom 3.4.0, 28 for dependency:list 3.6.1. outputEncoding is
# pinned because its default is the pom's project.reporting.outputEncoding;
# effective-pom has no parameter whose default reads the pom. Neither run is
# -q: -q also hides the guard's own [INFO] line, and that line is how this
# script knows the guard ran. A reading from a run without it is withheld, so
# its `error` fact voids the gate through pom-resolves exactly as a reading
# Maven never wrote does.
GUARD_JAR=/opt/guard/instrument-guard.jar
HELP_GUARD_PROPS=artifact,output,verbose
DEP_GUARD_PROPS=appendOutput,classifier,excludeArtifactIds,excludeClassifiers,excludeGroupIds,excludeReactor,excludeScope,excludeTransitive,excludeTypes,includeArtifactIds,includeClassifiers,includeGroupIds,includeParents,includeScope,includeTypes,markersDirectory,outputAbsoluteArtifactFilename,outputEncoding,outputFile,mdep.outputScope,overWriteIfNewer,overWriteReleases,overWriteSnapshots,mdep.prependGroupId,silent,mdep.skip,sort,type
instrument_guard_ran() {  # instrument_guard_ran <log> <groupId:artifactId>
    awk -v s="[INFO] [score-guard] $2: " \
        'index($0, s) == 1 { f = 1 } END { exit !f }' "$1" 2>/dev/null
}
withhold_unguarded() {  # withhold_unguarded <reading> <log> <groupId:artifactId>
    instrument_guard_ran "$WORK/$2" "$3" && return 0
    if [ -e "$WORK/$1" ] || [ -L "$WORK/$1" ]; then
        mv -f "$WORK/$1" "$WORK/${1%.*}.unguarded.${1##*.}"
    fi
    return 1
}
timeout -k 30 600 mvn -B $OFF -f "$TREE/pom.xml" \
    -Dmaven.ext.class.path="$GUARD_JAR" \
    -Dscore.guard.plugin="${HELP_PLUGIN%:*}" -Dscore.guard.props="$HELP_GUARD_PROPS" \
    "$HELP_PLUGIN:effective-pom" \
    -Doutput="$WORK/effective-pom.xml" > "$WORK/help.log" 2>&1 || true
timeout -k 30 600 mvn -B $OFF -f "$TREE/pom.xml" \
    -Dmaven.ext.class.path="$GUARD_JAR" \
    -Dscore.guard.plugin="${DEP_PLUGIN%:*}" -Dscore.guard.props="$DEP_GUARD_PROPS" \
    "$DEP_PLUGIN:list" \
    -DoutputFile="$WORK/deps.txt" -DincludeScope=test -DoutputEncoding=UTF-8 \
    > "$WORK/deps.log" 2>&1 || true
help_guard=
withhold_unguarded effective-pom.xml help.log "${HELP_PLUGIN%:*}" \
    || help_guard="instrument guard absent from help.log, effective POM withheld; "
dep_guard=
withhold_unguarded deps.txt deps.log "${DEP_PLUGIN%:*}" \
    || dep_guard="instrument guard absent from deps.log, listing withheld; "

for pair in "effective-pom.xml:help.log" "deps.txt:deps.log"; do
    art=${pair%%:*}; log=${pair##*:}
    if [ ! -s "$WORK/$art" ]; then
        # The first [ERROR] line, flattened: a gate's output is one fact per
        # line and a Maven stack trace is not one line. Keyed "maven:<art>"
        # rather than "<art>" so it does not collide with pom-scan.py's own
        # "<art> absent" -- one missing artifact should be one missing
        # artifact to a rule that counts, with the reason beside it, not two.
        # A withheld reading says so first.
        printf 'error\tmaven:%s\t%s%s\n' "$art" \
            "$(if [ "$art" = deps.txt ]; then printf '%s' "$dep_guard"; else printf '%s' "$help_guard"; fi)" \
            "$(grep -m1 '\[ERROR\]' "$WORK/$log" 2>/dev/null \
               | tr '\t\n' '  ' | cut -c1-200 || echo 'maven produced nothing')"
    fi
done

exec python3 -s -E "$HERE/pom-scan.py" "$WORK/effective-pom.xml" "$WORK/deps.txt" \
     "$TREE/pom.xml"
