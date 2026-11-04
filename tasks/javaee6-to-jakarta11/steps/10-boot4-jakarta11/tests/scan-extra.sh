#!/usr/bin/env bash
# Step 10 only. Run the platform's own Boot 4 upgrade path over the candidate's
# tree in dry-run mode and keep the patch it would have applied.
#
# This is the one gate on this rung whose instrument is somebody else's
# software, so three things are deliberate:
#
#   It runs on a *copy*. `rewrite:dryRun` binds to `process-test-classes` and
#   therefore drives a real build; pointed at /app it would recompile the tree
#   the behaviour suite is about to be replayed against, and an instrument that
#   can damage the thing it measures is not one this ladder will use. The copy
#   costs a few seconds against a warm local repository.
#
#   The plugin and the recipe module are named by full coordinates and pinned.
#   A goal prefix takes its version from the tree's own pluginManagement, and
#   an instrument the thing under test gets to choose is not an instrument --
#   the same reason scan.sh pins maven-dependency-plugin. For the same reason
#   nothing the copied poms say about the plugin reaches it (wave H, rootcfg
#   sweep): Maven reads a mojo parameter from the pom before the -D options
#   below, so a rewrite-maven-plugin <configuration> (`rewriteSkip`,
#   `activeRecipes`, `exclusions`, `runPerSubmodule`, ...), a project property
#   of the same name, or a bound execution would choose what the dry run
#   looks at -- `rewriteSkip` alone turned the predecessor's 36 kB patch into
#   "rc=0, no patch". The instrument guard (helpers.sh, instrument_guard_ran)
#   drops the plugin's configuration, plugin dependencies and every execution,
#   and every project property in REWRITE_GUARD_PROPS: the `${...}` property
#   of each parameter of `dryRun`, as the pinned plugin's descriptor declares
#   them (META-INF/maven/plugin.xml of 6.46.1, 22 names). Two defaults are
#   read from the tree and are pinned here instead: the YAML configuration
#   (`${maven.multiModuleProjectDirectory}/rewrite.yml`, the copy's own) goes
#   to a root-held path with no file, and the patch goes to RWREPORT rather
#   than `${project.build.directory}/rewrite`, which a pom's <directory> moves.
#
#   Nothing here decides anything. It writes the patch (or does not) and
#   records its own rc; `rewrite-clean` in score.py reads both and is the only
#   thing that judges. A dry run that could not resolve the recipe writes no
#   patch, and a gate that read "no patch" as "nothing to change" would be
#   passing because its instrument failed.
set -u
source /tests/helpers.sh

REWRITE_PLUGIN=org.openrewrite.maven:rewrite-maven-plugin:6.46.1
REWRITE_RECIPES=org.openrewrite.recipe:rewrite-spring:6.37.1
REWRITE_ACTIVE=org.openrewrite.java.spring.boot4.UpgradeSpringBoot_4_0
REWRITE_GUARD_PROPS=failOnDryRunResults,reportOutputDirectory,rewrite.activeRecipes,rewrite.activeStyles,rewrite.additionalPlainTextMasks,rewrite.checkstyleConfigFile,rewrite.checkstyleDetectionEnabled,rewrite.configLocation,rewrite.exclusions,rewrite.exportDatatables,rewrite.failOnInvalidActiveRecipes,rewrite.options,rewrite.plainTextMasks,rewrite.pomCacheDirectory,rewrite.pomCacheEnabled,rewrite.recipeArtifactCoordinates,rewrite.recipeChangeLogLevel,rewrite.resolvePropertiesInYaml,rewrite.runPerSubmodule,rewrite.skip,sizeThresholdMb,skipMavenParsing
# /score/out is root's, so no file can appear at this path.
REWRITE_NO_CONFIG=/score/out/no-rewrite-config.yml

RW=/score/work/rw
RWREPORT=/score/work/rwreport
rm -rf "$RW" "$RWREPORT"
mkdir -p "$RW" "$RWREPORT"
chown "$SCORE_UID:$SCORE_GID" "$RWREPORT"
# -a would carry /app's ownership; the copy has to be writable by the
# unprivileged user that runs maven.
cp -r /app/. "$RW"/ 2>/dev/null
rm -rf "$RW"/target "$RW"/*/target
chown -R "$SCORE_UID:$SCORE_GID" "$RW"
# The copy carries whatever .mvn/ the tree has; this run reads none of it, and
# MAVEN_BASEDIR below keeps mvn from looking for one above the copy, in
# /score/work, which the candidate's uid can write (helpers.sh).
maven_inputs_reset "$RW" rewrite
# The instrument's bytes and the session's profiles, as in scan.sh's listing
# (wave H round 3): every jar and pom the image recorded in /opt/m2 is checked
# before this run and after it, and the run must activate the profiles the
# scored package run activated (helpers.sh, profiles_match); the copy has no
# target/, as build.sh's tree had none when it packaged.
repo_before=0
instrument_repo_intact rewrite-before || repo_before=1

say "rewrite: dry run on a copy of /app"
# Bounded, because this is the one stage of the verifier that runs software
# nobody here wrote against a tree nobody here wrote. 600 s is thirteen times
# the 47 s the reference takes warm and still a quarter of the step's verifier
# budget, so a recipe that hangs costs one gate rather than the whole step. A
# timeout leaves rc=124, which `rewrite-clean` reads as "the recipe did not run
# to completion" and fails -- which is the right answer, and is not the same
# answer as "found nothing".
s=$(date +%s)
run_unprivileged MAVEN_OPTS="-Xmx2g" MAVEN_BASEDIR="$RW" timeout -k 30 600 \
    "${SCORED_PACKAGE[@]}" \
    -Dscore.guard.plugin="${REWRITE_PLUGIN%:*}" \
    -Dscore.guard.executions=all \
    -Dscore.guard.props="$REWRITE_GUARD_PROPS" \
    -f "$RW/pom.xml" \
    "$REWRITE_PLUGIN:dryRun" \
    -Drewrite.recipeArtifactCoordinates="$REWRITE_RECIPES" \
    -Drewrite.activeRecipes="$REWRITE_ACTIVE" \
    -Drewrite.exportDatatables=false \
    -Drewrite.configLocation="$REWRITE_NO_CONFIG" \
    -DreportOutputDirectory="$RWREPORT" \
    > /score/log/rewrite.log 2>&1
rc=$?
e=$(date +%s)
reap_unprivileged
# A dry run the guard did not see is not the verifier's dry run: an rc no
# completed run has, which `rewrite-clean` fails.
if ! instrument_guard_ran /score/log/rewrite.log "${REWRITE_PLUGIN%:*}"; then
    record rewrite.guard "absent from the run's log (mvn rc=$rc)"
    rc=97
fi
if ! profiles_match /score/log/rewrite.log rewrite; then
    record rewrite.profiles "active profiles differ from the scored package run (mvn rc=$rc)"
    rc=97
fi
if [ "$repo_before" -ne 0 ] || ! instrument_repo_intact rewrite-after; then
    record rewrite.repo "the image's /opt/m2 jars/poms changed (before=$repo_before; mvn rc=$rc)"
    rc=97
fi
record rewrite.rc "$rc"
record rewrite.seconds "$((e-s))"
# As a file as well as a journal line, because score.py reads /score/out and
# not the journal -- the same shape scan.sh gives `deps.rc`. `rewrite-clean`
# has to be able to tell "the recipe found nothing" from "the recipe never
# ran", and an absent patch looks identical either way.
echo "$rc" > /score/out/rewrite.rc

# The plugin writes the patch into reportOutputDirectory, pinned above. The
# dry run was the candidate's build, so it is reaped before anything is read.
reap_unprivileged
P="$RWREPORT/rewrite.patch"
if [ -f "$P" ]; then
    # Read as the unprivileged user: a link in the patch's place must not
    # carry a root-only file into a directory the servers can read
    # (helpers.sh, copy_as_unprivileged).
    copy_as_unprivileged "$P" /score/out/rewrite.patch
else
    : > /score/out/rewrite.patch
fi
record rewrite.patch_bytes "$(wc -c < /score/out/rewrite.patch)"

# The tree itself is not kept: it is a build tree, it is large, and nothing
# downstream reads it. The patch and the log are the evidence.
rm -rf "$RW" "$RWREPORT"
say "rewrite: rc=$rc patch=$(wc -c < /score/out/rewrite.patch) bytes in $((e-s))s"
exit 0
