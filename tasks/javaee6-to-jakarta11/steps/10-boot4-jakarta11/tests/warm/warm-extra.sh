#!/usr/bin/env bash
# Step 10 only. Put OpenRewrite's Boot 4 upgrade path into the image's local
# repository, and assert that it landed.
#
# `rewrite-clean` is the one gate on this rung whose instrument is somebody
# else's software, and `scan-extra.sh` runs it against the candidate's tree at
# score time. Two things follow. It has to be here rather than downloaded then:
# a gate that fails when Central is slow is a gate that measures the network.
# And the warming has to be the *same invocation*, because a plugin resolves
# its own runtime classpath and the recipe module resolves a second one --
# `dependency:get` on the two coordinates fetches neither completely.
#
# So this runs the real `dryRun`, over the reference's poms with no source
# under them. The recipe finds nothing to rewrite, which is fine; what it does
# is resolve every jar the scored run will ask for.
set -u

REWRITE_PLUGIN=org.openrewrite.maven:rewrite-maven-plugin:6.46.1
REWRITE_RECIPES=org.openrewrite.recipe:rewrite-spring:6.37.1
REWRITE_ACTIVE=org.openrewrite.java.spring.boot4.UpgradeSpringBoot_4_0

mvn -B -ntp -f /warm/pom.xml -Dmaven.repo.local="${M2_REPO:-/opt/m2}" \
    "$REWRITE_PLUGIN:dryRun" \
    -Drewrite.recipeArtifactCoordinates="$REWRITE_RECIPES" \
    -Drewrite.activeRecipes="$REWRITE_ACTIVE" \
    -Drewrite.exportDatatables=false \
    > /tmp/warm-rewrite.log 2>&1
rc=$?
tail -5 /tmp/warm-rewrite.log
echo "warm rewrite dryRun rc=$rc"

# Best-effort above, not here. If either jar is missing the image is built
# wrong and every scored run of this step would blame the candidate for it.
M=${M2_REPO:-/opt/m2}
test -f "$M/org/openrewrite/maven/rewrite-maven-plugin/6.46.1/rewrite-maven-plugin-6.46.1.jar"
test -f "$M/org/openrewrite/recipe/rewrite-spring/6.37.1/rewrite-spring-6.37.1.jar"
test "$rc" -eq 0
echo "warm rewrite: plugin and recipes cached"
