#!/usr/bin/env bash
# Scan the candidate's artifacts and its source, for the migrated-ness gates.
#
# Per-step copy: every step's tests/ holds an identical copy of this file;
# keep them in sync.
#
# Two scans, because a migration can be incomplete in two different ways:
#
#   artifact.jsonl  every class in everything the build produced, read with
#                   ASM: class-file version, supertypes, interfaces,
#                   annotations, the whole constant pool, and any dotted string
#                   literal. Archives are opened recursively, so a shaded jar
#                   or a war's WEB-INF/lib is scanned too. Checking artifacts
#                   rather than text means a dependency reached by
#                   Class.forName is as visible here as an import.
#
#   ast.jsonl       every .java file under the repository, parsed with
#                   JavaParser: declarations, suppression sites, and the
#                   construct classes the ladder retires. A parse error is a
#                   record, never a silent skip -- a gate that counts zero
#                   `new Thread` because the file did not parse is a gate that
#                   passes for the wrong reason.
#
# Scanning every module's target/ and the whole source tree, with no exclude
# list, is deliberate. A construct moved into a directory the scan skips is
# still in the shipped program.
set -u
source /tests/helpers.sh
cd /app

: "${SCORE_APP:=/score/app}"
GATE_CP=/opt/gatejars/asm-9.9.jar:/opt/gatejars/asm-tree-9.9.jar:/opt/gates
AST_CP=/opt/gatejars/javaparser-core-3.27.0.jar:/opt/gates
DEP_PLUGIN=org.apache.maven.plugins:maven-dependency-plugin:3.7.0

# Everything the build produced, plus the loose classes: a module whose
# packaging the candidate changed still compiles to target/classes, and a gate
# that only looked inside the deployable would stop seeing a module the moment
# it was left out of it.
: > /score/out/scan-roots.txt
find /app -maxdepth 3 -type d -name target -printf '%p\n' 2>/dev/null \
    | sort >> /score/out/scan-roots.txt
mapfile -t ROOTS < /score/out/scan-roots.txt
record scan.roots "${#ROOTS[@]}"

# The scanners run unprivileged and write into /score/work; root copies what
# they wrote into /score/out once they are done (helpers.sh, score_setup).
SCANW=/score/work/scan
rm -rf "$SCANW"
mkdir -p "$SCANW"
chown "$SCORE_UID:$SCORE_GID" "$SCANW"

if [ "${#ROOTS[@]}" -gt 0 ]; then
    run_unprivileged timeout -k 30 600 java -cp "$GATE_CP" ArtifactScan \
        "$SCANW/artifact.jsonl" "${ROOTS[@]}" \
        > /score/log/artifactscan.log 2>&1
    record artifactscan.rc "$?"
else
    : > "$SCANW/artifact.jsonl"
    record artifactscan.rc "no-target-dirs"
fi

run_unprivileged timeout -k 30 600 java -cp "$AST_CP" AstScan "$SCANW/ast.jsonl" /app \
    > /score/log/astscan.log 2>&1
record astscan.rc "$?"

# Seal what the build left, before any candidate code runs again. Everything
# above ran on a tree whose build has been reaped; everything below -- the
# dependency listing's own build, then run_behaviour.sh's servers -- runs the
# candidate's code as the user that owns /app. So the scans, and a copy of
# the tree for score.py's source-level gates (SCORE_APP), are taken now and
# held by root. The copy is read as the unprivileged user with symlinks
# followed, so it holds what the tree pointed at *now* and nothing that user
# could not read; root then takes it over and closes it. .git is left out:
# no gate reads it.
reap_unprivileged
copy_as_unprivileged "$SCANW/artifact.jsonl" /score/out/artifact.jsonl
copy_as_unprivileged "$SCANW/ast.jsonl" /score/out/ast.jsonl
record artifactscan.lines "$(wc -l < /score/out/artifact.jsonl)"
record astscan.lines "$(wc -l < /score/out/ast.jsonl)"
rm -rf "$SCANW" "$SCORE_APP" /score/work/app-copy
mkdir -p /score/work/app-copy
chown "$SCORE_UID:$SCORE_GID" /score/work/app-copy
run_unprivileged timeout -k 10 300 \
    tar -C /app --exclude=./.git -chf - . 2>/score/log/app-copy.log \
    | run_unprivileged tar -C /score/work/app-copy -xf - 2>>/score/log/app-copy.log
record app-copy.rc "${PIPESTATUS[0]}/${PIPESTATUS[1]}"
reap_unprivileged
mv /score/work/app-copy "$SCORE_APP"
chown -R root:root "$SCORE_APP"
chmod 0700 "$SCORE_APP"
record app-copy.files "$(find "$SCORE_APP" -type f | wc -l)"

# The deployable as built and scanned above, held by root under its own name:
# the dependency listing below runs `package` again, and the instance that
# run_behaviour.sh starts and score.py reads must be the one scanned here, not
# whatever that second build leaves behind. Readable, so the server can run it.
ART=$(cat /score/out/artifact.path 2>/dev/null || true)
rm -rf /score/art
if [ -n "$ART" ]; then
    mkdir -p /score/art
    chmod 0755 /score/art
    copy_as_unprivileged "$ART" "/score/art/$(basename "$ART")"
    chmod 0644 "/score/art/$(basename "$ART")"
    if [ -s "/score/art/$(basename "$ART")" ]; then
        printf '%s' "/score/art/$(basename "$ART")" > /score/out/artifact.path
    else
        : > /score/out/artifact.path
    fi
    record artifact.sealed "$(cat /score/out/artifact.path)"
fi

# The resolved dependency graph, asked of Maven rather than read off the pom:
# a version that arrives through dependency management or a bom is invisible in
# the pom text and decides what is on the classpath.
#
# Two things about this invocation are load-bearing, and neither was obvious.
#
#   `package` runs again, in the *same* session. On its own, `dependency:list`
#   resolves a module's sibling modules from the local repository, and nothing
#   ever puts them there -- build.sh runs `package`, not `install`. Standalone,
#   the goal therefore dies at the first module that depends on another module
#   and lists only the leaves: on the reference tree that is deps.rc=1 and a
#   graph covering two of the seven modules, and the two "no such dependency"
#   gates would pass on a list that had never seen five of the poms.
#   Re-running the phase in-session puts the reactor's own
#   artifacts in front of the resolver, and `deps.rc` becomes a real assertion
#   that every module was listed.
#
#   The plugin is named by full coordinates. `dependency:list` is a goal
#   prefix, and a prefix takes its version from the tree's own
#   pluginManagement -- the pinned tree pins maven-dependency-plugin 2.1 there.
#   An instrument the thing under test gets to choose is not an instrument.
#
# The output goes to one file per reactor project, named by an expression the
# plugin expands per project. `-DappendOutput=true` into a single file reads
# like the obvious way to aggregate and is not: each project's execution
# replaces the file, so what survives is the last module's graph alone. That
# looks like a complete union here, because the last module is the war and
# everything compile-scoped is reachable from it -- but it silently drops
# every test-scope dependency of every other module: a tree that added
# junit:junit:4.13.2 to mini-git-server-sshd would still pass
# `no-junit3-4-dep`. Per project: 7 files, 174 entries, junit found.
DEPW=/score/work/deps
rm -rf "$DEPW"
mkdir -p "$DEPW"
chown "$SCORE_UID:$SCORE_GID" "$DEPW"
# Maven reads no configuration of the candidate's beyond the poms, including
# any the two earlier runs wrote (helpers.sh, maven_inputs_reset).
#
# And what the poms say about the instrument itself is dropped. Maven takes a
# mojo parameter from the pom before it reads the -D options below: a
# maven-dependency-plugin <configuration> with <excludeGroupIds>, a project
# property named `excludeGroupIds`, or <project.reporting.outputEncoding> (the
# default of `outputEncoding`) would each take a group out of the listing, or
# every line out of the parser's reach, while the run still reported success.
# The guard (helpers.sh,
# instrument_guard_ran) removes the plugin's configuration, plugin
# dependencies and default-cli execution, and every project property in
# DEP_GUARD_PROPS: the `${...}` property of each parameter of `list`, as the
# pinned plugin's own descriptor declares them (META-INF/maven/plugin.xml of
# 3.7.0, 28 names). outputEncoding is pinned because its default is read from
# the pom. The candidate's own executions of the plugin are its build's and
# stay.
DEP_GUARD_PROPS=appendOutput,classifier,excludeArtifactIds,excludeClassifiers,excludeGroupIds,excludeReactor,excludeScope,excludeTransitive,excludeTypes,includeArtifactIds,includeClassifiers,includeGroupIds,includeParents,includeScope,includeTypes,markersDirectory,outputAbsoluteArtifactFilename,outputEncoding,outputFile,mdep.outputScope,overWriteIfNewer,overWriteReleases,overWriteSnapshots,mdep.prependGroupId,silent,mdep.skip,sort,type
#
# Two more things make this the verifier's listing of the scored build. The
# instrument's bytes: /opt/m2 belongs to the candidate's uid,
# so every jar and pom the image recorded is checked before this run and again
# after it (a package-phase plugin runs before the instrument's class realm
# is made). And the session: the guard reports the profiles Maven activated,
# which must be the ones build.sh's package run activated (helpers.sh,
# profiles_match) -- this run's own -D options and the target/ directories
# the package run left are exactly what a profile's activation can key on.
# Any of the three wrong, and the listing is no listing: rc 97.
reap_unprivileged
maven_inputs_reset /app deps
repo_before=0
instrument_repo_intact deps-before || repo_before=1
# Bounded: runs the candidate's own build again.
run_unprivileged MAVEN_OPTS="-Xmx2g" timeout -k 30 600 \
    "${SCORED_PACKAGE[@]}" \
    -Dscore.guard.plugin="${DEP_PLUGIN%:*}" \
    -Dscore.guard.props="$DEP_GUARD_PROPS" \
    package "$DEP_PLUGIN:list" \
    '-DoutputFile=/score/work/deps/${project.artifactId}.txt' \
    -DoutputAbsoluteArtifactFilename=false -Dsort=true -DoutputEncoding=UTF-8 \
    > /score/log/deps.log 2>&1
rc=$?
reap_unprivileged
# A listing the guard did not see is not the verifier's listing: an rc no
# successful run has, so every dependency gate fails closed on it.
if ! instrument_guard_ran /score/log/deps.log "${DEP_PLUGIN%:*}"; then
    record deps.guard "absent from the run's log (mvn rc=$rc)"
    rc=97
fi
if ! profiles_match /score/log/deps.log deps; then
    record deps.profiles "active profiles differ from the scored package run (mvn rc=$rc)"
    rc=97
fi
if [ "$repo_before" -ne 0 ] || ! instrument_repo_intact deps-after; then
    record deps.repo "the image's /opt/m2 jars/poms changed (before=$repo_before; mvn rc=$rc)"
    rc=97
fi
record deps.rc "$rc"
echo "$rc" > /score/out/deps.rc
# The build that wrote these has exited; nothing of it is left to rewrite them
# once reaped. Read unprivileged, so a symlink among them reaches nothing root
# alone can read (copy_as_unprivileged in helpers.sh says why).
reap_unprivileged
run_unprivileged sh -c 'cat /score/work/deps/*.txt' > /score/out/deps.txt 2>/dev/null \
    || : > /score/out/deps.txt
record deps.projects "$(find "$DEPW" -name '*.txt' | wc -l)"
record deps.lines "$(wc -l < /score/out/deps.txt 2>/dev/null || echo 0)"
record deps.artifacts "$(grep -cE '^ +[A-Za-z0-9_.-]+:[A-Za-z0-9_.-]+:[A-Za-z0-9_.-]+:' \
    /score/out/deps.txt 2>/dev/null || echo 0)"

say "scans: artifact=$(wc -l < /score/out/artifact.jsonl) ast=$(wc -l < /score/out/ast.jsonl)"

# One measurement a single rung needs and no other rung does. A step that
# ships /tests/scan-extra.sh gets it run here -- after the artifact scan, the
# AST scan and the dependency graph, so nothing it does can be read back by a
# gate that was supposed to be looking at the candidate's own build. A step
# that ships no such file skips this.
#
# The hook is here rather than in test.sh because everything it can need is
# here: /app is built, /score/out is populated, and the helpers are sourced.
# Its rc is recorded rather than checked; a rung whose extra scan can fail is
# a rung whose gate has to say so itself, and a scan stage that aborted the
# whole verifier would turn one missing gate into a step that scored nothing.
if [ -f /tests/scan-extra.sh ]; then
    bash /tests/scan-extra.sh
    record scan_extra.rc "$?"
fi

exit 0
