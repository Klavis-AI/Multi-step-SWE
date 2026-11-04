# Verifier image for step 02, built with steps/02-junit5-testing/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so everything grading needs is copied in here. Harbor skips its usual
# tests upload and runs /tests/test.sh from this image.
#
# Step 02 grades on JDK 8 and nothing else, exactly as step 01 does, and for the
# same reason: this image carries that one JDK, no Maven, and no `gradle` on
# PATH. Step 01 retired all three and a later step must not quietly give one
# back -- a gate that passed once has to keep passing against the image that
# grades the step after it.
#
# What is new here is a second toolchain, and it belongs to the SCORER rather
# than to the candidate. assets/harness is the JUnit Platform console
# standalone jar the corroboration drives each inherited test class with;
# assets/testlib is the run-time closure those classes need once they have been
# compiled against Jupiter and Mockito 4. Both go on the classpath ahead of
# anything from the tree, and neither is taken from the tree, so "the migrated
# suite really runs" is measured by a harness the candidate cannot have edited.
#
# /opt/arqwarm2 is a different thing that is easy to confuse with those two: it
# warms the candidate's OWN copy of that toolchain into this image's Gradle
# cache, so that the scored build resolves Jupiter and Mockito without a
# network. The scorer never reads it.
#
# HOME for candidate code is /tmp (helpers.sh: run_unprivileged), so that is
# where the caches the candidate's build reads from are baked: /tmp/.gradle and
# /tmp/.m2. Nothing else in the image depends on that choice; it is here so that
# no wrapper script has to be handed an environment variable of the task's
# invention to find them.
#
# Every RUN below is split at its comment rather than carrying comments inside a
# line continuation: a Dockerfile comment inside a `\` continuation is removed
# by some parsers and joined into the command by others, and the second
# behaviour silently truncates the command at the first comment.
FROM eclipse-temurin:8-jdk

# util-linux for setpriv (run_unprivileged). unzip for the gate build's jar
# assertions and for the Gradle distribution. The rest is what any
# diff-and-count verifier needs; nothing here comes from the agent's image, so a
# candidate that breaks its own toolchain does not change how it is graded.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils gawk unzip util-linux \
    && rm -rf /var/lib/apt/lists/*

# Gradle, present only long enough to generate a wrapper and pull the two caches
# below; removed before the image is finished. 8.14.5 and not 9.x because
# Gradle 9 needs Java 17+ to run and this image has only JDK 8 -- the same
# constraint the candidate's build is under.
ARG GRADLE_VERSION=8.14.5
ARG GRADLE_SHA256=6f74b601422d6d6fc4e1f9a1ab6522f642c2fdcbc15ae33ebd30ba3d7198e854
RUN set -eux; \
    curl -fsSL -o /tmp/gradle.zip \
        "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip"; \
    echo "${GRADLE_SHA256}  /tmp/gradle.zip" | sha256sum -c -; \
    unzip -q /tmp/gradle.zip -d /opt/gradle; \
    rm /tmp/gradle.zip

# The JVM does not read $HOME. On Linux it takes user.home from the passwd
# entry, and nobody's is /nonexistent -- so `mavenLocal()` would resolve
# /nonexistent/.m2/repository and publishToMavenLocal would try to write there,
# under the one identity every scored build runs as. Measured, not assumed:
# `setpriv --reuid 65534 ... env HOME=/tmp java -XshowSettings:properties`
# prints user.home = /nonexistent whether HOME is set or not.
#
# Giving nobody a home that exists is what makes helpers.sh's HOME=/tmp true for
# Java as well as for the shell.
RUN set -eux; \
    sed -i 's|^nobody:x:65534:65534:\(.*\):/nonexistent:|nobody:x:65534:65534:\1:/tmp:|' /etc/passwd; \
    getent passwd 65534; \
    getent passwd 65534 | grep -q ':/tmp:'; \
    setpriv --reuid 65534 --regid 65534 --clear-groups \
        java -XshowSettings:properties -version 2>&1 | grep -q 'user.home = /tmp$'

# Copied before the rest of the payload so this layer is shared between steps:
# warm/ is byte-identical in every step's context, and the 17 MB it pulls is the
# slowest thing in this build.
COPY warm /opt/arqwarm

# The wrapper distribution cache, warmed by running a wrapper rather than by
# guessing the directory name Gradle derives from the distribution URL. Only the
# default `bin` type is warmed; a wrapper that names `all`, or a different
# version, still works -- it downloads, which is why this image is not on a
# private network.
ENV GRADLE_USER_HOME=/tmp/.gradle
RUN set -eux; \
    mkdir -p /opt/arqwrap; \
    cd /opt/arqwrap; \
    : > settings.gradle; \
    "/opt/gradle/gradle-${GRADLE_VERSION}/bin/gradle" --quiet --no-daemon \
        wrapper --gradle-version "${GRADLE_VERSION}"; \
    ./gradlew --quiet --no-daemon --version

# The module cache, from the two repositories the pinned reactor's own
# dependencies actually live in. See /opt/arqwarm/build.gradle for why there are
# two and why the set is resolved transitively.
RUN set -eux; \
    cd /opt/arqwarm; \
    "/opt/gradle/gradle-${GRADLE_VERSION}/bin/gradle" --no-daemon -q warmUp

# The step-02 test toolchain, warmed by a second project rather than by adding
# six lines to warm/deps.txt. Gradle resolves one configuration with
# newest-wins, so junit:junit:4.13.2 (which the vintage engine needs) and
# junit:junit:4.8.1 (which the arquillian-testng module still compiles against)
# in one configuration select 4.13.2 and never fetch 4.8.1 -- and the module
# whose suite this step deliberately leaves on TestNG would then resolve over
# the network at grading time. Two projects, two resolutions, both versions
# cached.
#
# It is also what keeps warm/ byte-identical in every step's build context,
# which is what lets every step share the layer above this one.
COPY warm2 /opt/arqwarm2
RUN set -eux; \
    cd /opt/arqwarm2; \
    "/opt/gradle/gradle-${GRADLE_VERSION}/bin/gradle" --no-daemon -q warmUp

# The same artifacts again, in Maven layout, so that a build declaring
# mavenLocal() resolves too. Gradle's files-2.1 keeps every file it fetched --
# jars and the whole pom chain, parents included -- one sha1 directory down from
# <group>/<name>/<version>, which is the only difference from the layout
# mavenLocal() reads.
#
# The floor is 90 rather than step 01's 70 because this image warms two
# projects. Step 01's finished image was measured at 78 jars here, so 90 is a
# number an image whose /opt/arqwarm2 resolved nothing cannot reach: the check
# fails on a missing second warm rather than on a missing cache in general. It
# stays a floor and not an equality so that a patch release of Jupiter does not
# fail an image build over an artifact count.
RUN set -eux; \
    find "${GRADLE_USER_HOME}/caches/modules-2/files-2.1" -type f \
        \( -name '*.jar' -o -name '*.pom' \) | while read -r f; do \
            rel="${f#*/files-2.1/}"; \
            g="$(echo "${rel}" | cut -d/ -f1 | tr '.' '/')"; \
            a="$(echo "${rel}" | cut -d/ -f2)"; \
            v="$(echo "${rel}" | cut -d/ -f3)"; \
            d="/tmp/.m2/repository/${g}/${a}/${v}"; \
            mkdir -p "${d}"; \
            cp "${f}" "${d}/$(basename "${f}")"; \
        done; \
    test "$(find /tmp/.m2/repository -name '*.jar' | wc -l)" -ge 90

# The wrapper distribution zips are not read again once Gradle has written its
# .ok marker, and they are 130 MB. Removed, and then the wrapper is run once
# more -- if the marker were not what Gradle trusts, this is where it would say
# so rather than at grading time. Then Gradle itself goes: from here the only
# Gradle in this image is the one the candidate's wrapper resolves.
RUN set -eux; \
    find "${GRADLE_USER_HOME}/wrapper/dists" -name '*.zip' -delete; \
    cd /opt/arqwrap; \
    ./gradlew --quiet --no-daemon --version; \
    rm -rf /opt/gradle /opt/arqwrap/build /opt/arqwarm/build /opt/arqwarm2/build; \
    chown -R 65534:65534 /tmp/.gradle /tmp/.m2 /opt/arqwarm /opt/arqwarm2 /opt/arqwrap

# /app is repopulated from the collected artifact and /logs/verifier is bind-
# mounted at run time; creating them keeps workdir valid at container start.
RUN mkdir -p /app /logs/verifier

COPY . /tests
RUN chmod 0700 /tests/test.sh

# The scored build and the probe run as 65534, and this payload arrives 0700
# root:root, so without a grant the runner cannot read its own inputs. Family A
# scored 0 on exactly that, with "Permission denied" as the whole of the
# evidence, and the image's controls missed it because a Dockerfile RUN is root.
#
# Granted by name rather than with a blanket chmod -R a+rX /tests. 65534 is the
# identity that executes code the candidate wrote, so it gets the probe harness,
# the fixture inputs and the dependency pool -- and nothing else. assets/rec
# holds the expected transcripts and baseline/ holds the frozen counts; a build
# script able to read those could answer with them instead of migrating. The id
# list goes with the harness: the probe is handed it by path and reads it as
# 65534.
#
# assets/harness and assets/testlib join that list in step 02, and they have to.
# The corroboration launches a JVM through run_unprivileged, so if 65534 cannot
# read the console-standalone jar the JVM never reaches a main class and every
# one of the 55 classes comes back `fault`. That is measured rather than
# reasoned: the first run of fixtures/pv-rehearsal.sh did exactly that, in a
# container where /tests was 0700, and the verdict function was right to call
# it a fault. The rehearsal now replicates this grant instead of assuming it.
# Neither shelf carries an answer -- one is a test runner, the other is
# Jupiter, Mockito and JUnit 4.
#
# /tests
# and /tests/assets get +x without +r: traversable by name, not listable. /tests
# is set to 0711 outright rather than with a+x, because COPY creates the mount
# point itself 0755 and +x on an already-readable directory changes nothing.
#
# The `go-rwx` sweep is what makes "arrives 0700 root:root" true rather than
# hoped for. COPY carries the host's modes into the image, so the payload is
# only root-only if whoever staged it happened to have a restrictive umask --
# and on a host where the tree was checked out, synced or restored with the
# usual 0644, every grant below is a no-op against an already world-readable
# /tests. That is not hypothetical: a restore of this workspace normalised all
# 37k files to 0664 and step 03's leak control caught /tests/helpers.sh on the
# next build. Sweeping first and granting by name afterwards makes the image's
# permissions a property of the Dockerfile instead of a property of the
# machine it was built on.
RUN set -eux; \
    chmod -R go-rwx /tests; \
    chmod 0711 /tests; \
    chmod a+x /tests/assets; \
    chmod -R a+rX /tests/assets/probe /tests/assets/inputs /tests/assets/probe-lib; \
    chmod -R a+rX /tests/assets/harness /tests/assets/testlib; \
    chmod a+r /tests/assets/ids.txt

# Step identity, baked in. A local Docker environment that reuses one verifier
# image for several steps would otherwise run an earlier step's test.sh and
# still report a score; this marker is what makes that visible from the job
# directory afterwards.
RUN echo step02 > /tests/STEP_ID

# The bytecode gate. Built here, from source, with this image's own JDK 8 javac
# -- the same jar the JDK 25 images of steps 12-13 build from the same source.
RUN bash /tests/gates/build.sh /tests/gates

# Rule 6, asserted rather than intended: one JDK, and it is this step's. Every
# executable `java` on the filesystem is asked its own version rather than
# JAVA_HOME being trusted. The retired build system is not here, and neither is
# a Gradle the candidate's wrapper did not resolve.
RUN set -eux; \
    found=0; \
    for j in $(find / -xdev -type f -name java -perm -u+x 2>/dev/null); do \
        v="$("$j" -XshowSettings:properties -version 2>&1 | sed -n 's/.*java\.version = //p')"; \
        echo "reachable JVM ${v} at ${j}"; \
        case "${v}" in 1.8.*) found=$((found + 1));; \
            *) echo "RULE-6 VIOLATION: step 02 grades on JDK 8 only, found ${v} at ${j}" >&2; exit 1;; \
        esac; \
    done; \
    test "${found}" -gt 0; \
    if command -v mvn    >/dev/null 2>&1; then echo "RULE-6 VIOLATION: Maven is reachable from the scorer" >&2; exit 1; fi; \
    if command -v gradle >/dev/null 2>&1; then echo "RULE-6 VIOLATION: a gradle on PATH would bypass the candidate's wrapper" >&2; exit 1; fi; \
    test ! -e /opt/gradle; \
    javap -version

# The payload landed. Counts, not directory existence: a COPY that arrived empty
# and a COPY that arrived whole both leave the directory there. The numbers are
# repeated from assets/STAGED.txt rather than read out of it, so that a staging
# run against the wrong era or the wrong probe-lib pin fails the image build.
RUN set -eux; \
    grep -q '^staged.era=jdk8$'        /tests/assets/STAGED.txt; \
    grep -q '^staged.orderdep=1$'      /tests/assets/STAGED.txt; \
    test "$(grep -c '^as_untrusted timeout -s KILL' /tests/runner/score.sh)" = 2; \
    test "$(grep -vc '^#' /tests/baseline/orderdep.txt)" = 1; \
    grep -q '^staged.ids=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.rec=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.problib.jars=80$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/assets/ids.txt)" = 935; \
    test "$(find /tests/assets/rec/jdk8 -name '*.txt' | wc -l)" = 935; \
    test "$(find /tests/assets/probe-lib -name '*.jar' | wc -l)" = 80; \
    test -f /tests/assets/probe-lib/junit-4.8.1.jar; \
    test -f /tests/assets/probe-lib/testng-5.14.6.jar; \
    test "$(find /tests/assets/probe-lib -name 'junit-*.jar' | wc -l)" = 1; \
    unzip -l /tests/assets/probe-lib/junit-4.8.1.jar > /tmp/jl.txt; \
    grep -q 'org/junit/runner/JUnitCore.class'    /tmp/jl.txt; \
    grep -q 'org/junit/internal/TextListener.class' /tmp/jl.txt; \
    grep -q 'org/hamcrest/'                       /tmp/jl.txt; \
    grep -q 'junit/framework/'                    /tmp/jl.txt; \
    rm -f /tmp/jl.txt; \
    test -r /tests/assets/probe/run.sh; \
    grep -q '^staged.probe.srcns=2$'  /tests/assets/STAGED.txt; \
    test -r /tests/assets/probe/src-ns/javax/arqprobe/fam/webrunner/Out.java; \
    test -r /tests/assets/probe/src-ns/jakarta/arqprobe/fam/webrunner/Out.java; \
    test -r /tests/runner/score.sh; \
    test -s /tests/baseline/artifacts.txt; \
    grep -q '^staged.api=1147$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/baseline/api.txt)" = 1147; \
    test "$(grep -c ' TYPE ' /tests/baseline/api.txt)" = 279; \
    test "$(grep -c ':: Compiled from' /tests/baseline/api.txt)" = 0; \
    test -r /tests/gates/apisig.sh; \
    grep -q '^staged.tests.methods=164$' /tests/assets/STAGED.txt; \
    test "$(grep -c '^test.method=' /tests/baseline/tests.txt)" = 164; \
    test "$(grep -c '^test.class=' /tests/baseline/tests.txt)" = 55; \
    test "$(grep -c '^test.disabled=' /tests/baseline/tests.txt)" = 7; \
    grep -q '^tests.frameworks=junit4,testng$' /tests/baseline/tests.txt; \
    test "$(grep -c . /tests/baseline/testresults.txt)" = 157; \
    grep -q '^staged.classnames=322$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/baseline/classnames.txt)" = 322; \
    test "$(grep -c '^org[.]jboss[.]arquillian[.]' /tests/baseline/classnames.txt)" = 322; \
    test "$(tr -cd '$' < /tests/baseline/classnames.txt | wc -c)" = 0

# The three shelves step 02 adds, on the same terms: a count, and then the one
# property of the payload that the count cannot see.
#
# The harness jar is pinned by sha1 and by size because it is the only thing in
# this image that decides whether an inherited test passed. A different build
# of the same version number would still be "one jar named
# junit-platform-console-standalone-1.14.4.jar".
#
# What it contains is asserted too, because the design rests on it: putting one
# shaded jar first on the classpath is what puts the launcher, BOTH engines and
# BOTH assertion APIs out of the tree's reach in a single move. If a future
# bump split any of them out, the corroboration would silently start resolving
# that piece from the candidate's own jars.
#
# And what it does NOT contain: no TestNG engine. That is load-bearing rather
# than incidental -- step 02 deliberately leaves the arquillian-testng module on
# TestNG (NOTES 7.0), so the harness must not be able to discover a
# @org.testng.annotations.Test either. An engine appearing here in a later
# version bump would change which classes the corroboration runs without
# changing a line of the scorer.
RUN set -eux; \
    grep -q '^staged.harness.jars=1$'   /tests/assets/STAGED.txt; \
    grep -q '^staged.testlib.jars=76$'  /tests/assets/STAGED.txt; \
    grep -q '^staged.baseline.files=9$' /tests/assets/STAGED.txt; \
    grep -q '^staged.fixtures=14$'      /tests/assets/STAGED.txt; \
    grep -q '^staged.warm2.deps=6$'     /tests/assets/STAGED.txt; \
    grep -q '^staged.gates=Junit5.java Scan.java SelfStop.java Tests.java $' \
        /tests/assets/STAGED.txt; \
    H=/tests/assets/harness/junit-platform-console-standalone-1.14.4.jar; \
    test "$(find /tests/assets/harness -name '*.jar' | wc -l)" = 1; \
    test -f "${H}"; \
    test "$(stat -c %s "${H}")" = 3178851; \
    echo "02deb74daae8c2187cfc9fe85392f8421eb004c6  ${H}" | sha1sum -c -; \
    unzip -l "${H}" > /tmp/hl.txt; \
    grep -q 'org/junit/platform/console/ConsoleLauncher\.class' /tmp/hl.txt; \
    grep -q 'org/junit/platform/launcher/'                       /tmp/hl.txt; \
    grep -q 'org/junit/jupiter/api/Assertions\.class'            /tmp/hl.txt; \
    grep -q 'org/junit/jupiter/engine/'                          /tmp/hl.txt; \
    grep -q 'org/junit/vintage/engine/'                          /tmp/hl.txt; \
    grep -q 'org/junit/runner/JUnitCore\.class'                  /tmp/hl.txt; \
    grep -q 'org/opentest4j/'                                    /tmp/hl.txt; \
    if grep -qi 'testng' /tmp/hl.txt; then \
        echo "the harness jar now carries a TestNG engine; step 02 leaves that module on TestNG" >&2; \
        exit 1; \
    fi; \
    rm -f /tmp/hl.txt; \
    test "$(find /tests/assets/testlib -name '*.jar' | wc -l)" = 76; \
    test -f /tests/assets/testlib/mockito-core-4.11.0.jar; \
    test -f /tests/assets/testlib/mockito-junit-jupiter-4.11.0.jar; \
    test -f /tests/assets/testlib/testng-5.14.6.jar; \
    test -f /tests/assets/testlib/jboss-vfs-3.0.0.CR5.jar; \
    test "$(find /tests/assets/testlib -name 'junit-jupiter*.jar' | wc -l)" = 0; \
    test "$(find /tests/assets/testlib -name 'junit-platform*.jar' | wc -l)" = 0; \
    test "$(find /tests/assets/testlib -name 'mockito-core-1*.jar' | wc -l)" = 0; \
    test "$(find /tests/baseline -type f | wc -l)" = 9; \
    test "$(find /tests/fixtures -type f | wc -l)" = 14; \
    test "$(grep -v '^#' /tests/warm2/deps.txt | grep -c .)" = 6; \
    grep -q '^org.junit.jupiter:junit-jupiter:5.14.4$'            /tests/warm2/deps.txt; \
    grep -q '^org.junit.platform:junit-platform-launcher:1.14.4$' /tests/warm2/deps.txt; \
    grep -q '^org.junit.vintage:junit-vintage-engine:5.14.4$'     /tests/warm2/deps.txt; \
    grep -q '^org.mockito:mockito-core:4.11.0$'                   /tests/warm2/deps.txt; \
    test "$(grep -v '^#' /tests/baseline/compat.txt | grep -c .)" = 6; \
    grep -qx 'org.jboss.arquillian.testng.TestNGTestRunnerTestCase' /tests/baseline/compat.txt; \
    grep -qx 'org.jboss.arquillian.junit.JUnitTestRunnerTestCase'   /tests/baseline/compat.txt; \
    grep -q '^j5.base.tests=58$'              /tests/baseline/junit5.txt; \
    grep -q '^j5.base.tests.junit5=0$'        /tests/baseline/junit5.txt; \
    grep -q '^j5.base.expected.onmust=19$'    /tests/baseline/junit5.txt; \
    grep -q '^j5.base.runwith.onmust=16$'     /tests/baseline/junit5.txt; \
    grep -q '^j5.base.assertthrows.floor=19$' /tests/baseline/junit5.txt; \
    grep -q '^j5.base.call.legacy=272$'       /tests/baseline/junit5.txt; \
    grep -q '^j5.base.call.legacy.msg=155$'   /tests/baseline/junit5.txt

# baseline/testng-cased.txt -- the one class B-7's corroboration may not charge
# `unloadable` for, and the four facts that make it that class rather than a
# name someone added. The file is small enough to re-derive here from the two
# frozen lists it claims to follow from, so the exemption cannot quietly grow.
#
#   1. it names exactly one class, and that class is TestNGTestRunnerTestCase
#   2. every name in it is in baseline-cased.txt -- the exemption only ever
#      shrinks the charged set, and a name outside that set would be exempting
#      something this reasoning does not cover
#   3. it is exactly the set of recorded classes in the arquillian-testng
#      module, which is the derivation the file's own header states
#   4. the ids the frozen run recorded for it are the two
#      @org.testng.annotations.Test fixture methods and NOT the class's own two
#      JUnit 4 tests -- which is the whole argument: those two ids are
#      unreachable by a Jupiter-plus-vintage harness on every tree, the golden
#      one included, so their absence never corroborated anything
RUN set -eux; \
    grep -v '^#' /tests/baseline/testng-cased.txt | grep . | LC_ALL=C sort -u > /tmp/tng.txt; \
    test "$(grep -c . /tmp/tng.txt)" = 1; \
    grep -qx 'org.jboss.arquillian.testng.TestNGTestRunnerTestCase' /tmp/tng.txt; \
    sed -n 's/^\([^#]*\)#.*/\1/p' /tests/baseline/testresults.txt | LC_ALL=C sort -u > /tmp/cased.txt; \
    test "$(LC_ALL=C comm -23 /tmp/tng.txt /tmp/cased.txt | grep -c .)" = 0; \
    grep '^org\.jboss\.arquillian\.testng\.' /tmp/cased.txt | LC_ALL=C sort -u > /tmp/tngmod.txt; \
    test "$(LC_ALL=C diff /tmp/tng.txt /tmp/tngmod.txt | grep -c .)" = 0; \
    grep -qx 'org.jboss.arquillian.testng.TestNGTestRunnerTestCase#shouldProvidePassingTestToRunner pass' \
        /tests/baseline/testresults.txt; \
    grep -qx 'org.jboss.arquillian.testng.TestNGTestRunnerTestCase#shouldProvideExpectedExceptionToRunner error' \
        /tests/baseline/testresults.txt; \
    test "$(grep -c '^org\.jboss\.arquillian\.testng\.TestNGTestRunnerTestCase#' /tests/baseline/testresults.txt)" = 2; \
    for jm in shouldReturnPassedTest shouldReturnExceptionOnPassedTest; do \
        if grep -q "^org\.jboss\.arquillian\.testng\.TestNGTestRunnerTestCase#${jm} " \
               /tests/baseline/testresults.txt; then \
            echo "the frozen run DID record ${jm}; the testng-cased argument does not hold" >&2; \
            exit 1; \
        fi; \
        grep -qx "test.method=org.jboss.arquillian.testng.TestNGTestRunnerTestCase#${jm}" \
            /tests/baseline/tests.txt; \
    done; \
    grep -qF 'grep -v '"'"'^#'"'"' /tests/baseline/testng-cased.txt | grep . | LC_ALL=C sort -u' /tests/test.sh; \
    grep -qF ': > "${WORK}/testng-cased.txt"' /tests/test.sh

# c_skipped is the sum of the buckets skipped.detail prints, and the comment
# above it says so. It has been wrong once already -- cs_unloadable was in the
# detail line and not in the sum, and since the golden tree puts 0 in that
# bucket the shortfall never showed in any run. So the two lists are compared
# here by name rather than by eye: every ${cs_*} in the detail fact appears in
# the sum, and every cs_* in the sum appears in the detail fact.
RUN set -eux; \
    grep -o 'skipped\.detail=.*' /tests/test.sh | grep -o 'cs_[a-z]*' \
        | LC_ALL=C sort -u > /tmp/detail-buckets.txt; \
    grep -A1 -F 'c_skipped=$(( cs_noroot' /tests/test.sh | grep -o 'cs_[a-z]*' \
        | LC_ALL=C sort -u > /tmp/sum-buckets.txt; \
    test "$(grep -c . /tmp/detail-buckets.txt)" = 11; \
    test "$(grep -c . /tmp/sum-buckets.txt)" = 11; \
    if ! LC_ALL=C diff /tmp/detail-buckets.txt /tmp/sum-buckets.txt; then \
        echo "c_skipped and skipped.detail no longer count the same buckets" >&2; exit 1; \
    fi

# The must-migrate set the C block scores over, derived here and compared with
# the set the scorer derives at run time. 55 frozen test classes minus the six
# on the compat surface is 49, and that 49 is the denominator of C-1 and C-2 --
# so it is worth one line to know the two files still disagree by nothing.
#
# The same treatment for the two frozen lists C-3 and C-4 retire against. Their
# counts -- 19 expected= sites, 16 @RunWith classes -- were already asserted
# above; what is asserted here is that the lists are as long as their counts say
# and that every entry names a must-migrate class. A site attributed to a class
# outside the set would be a site no candidate can retire and a gate no oracle
# can satisfy, and the frozen counts alone cannot see it.
RUN set -eux; \
    sed -n 's/^test\.class=//p' /tests/baseline/tests.txt | LC_ALL=C sort -u > /tmp/all.txt; \
    grep -v '^#' /tests/baseline/compat.txt | grep . | LC_ALL=C sort -u > /tmp/cmp.txt; \
    LC_ALL=C comm -23 /tmp/all.txt /tmp/cmp.txt > /tmp/must.txt; \
    test "$(grep -c . /tmp/all.txt)" = 55; \
    test "$(grep -c . /tmp/cmp.txt)" = 6; \
    test "$(grep -c . /tmp/must.txt)" = 49; \
    test "$(LC_ALL=C comm -13 /tmp/all.txt /tmp/cmp.txt | grep -c .)" = 0; \
    test "$(grep -c '^j5.base.expected.site=' /tests/baseline/junit5.txt)" = 19; \
    test "$(grep -c '^j5.base.runwith.class=' /tests/baseline/junit5.txt)" = 16; \
    sed -n 's|^j5.base.expected.site=||p' /tests/baseline/junit5.txt \
        | sed 's/#.*$//; s/[$].*$//' | tr '/' '.' | LC_ALL=C sort -u > /tmp/es.txt; \
    sed -n 's|^j5.base.runwith.class=||p' /tests/baseline/junit5.txt \
        | sed 's/[$].*$//' | tr '/' '.' | LC_ALL=C sort -u > /tmp/rw.txt; \
    test "$(LC_ALL=C comm -23 /tmp/es.txt /tmp/must.txt | grep -c .)" = 0; \
    test "$(LC_ALL=C comm -23 /tmp/rw.txt /tmp/must.txt | grep -c .)" = 0; \
    rm -f /tmp/all.txt /tmp/cmp.txt /tmp/must.txt /tmp/es.txt /tmp/rw.txt

# B-7's report parser, against a fixture whose answer is known, and against the
# shape that got it wrong. tr collapses a report to one line, so sed emits no
# trailing newline and appending several reports welds the last <testcase> of
# one onto the preamble of the next. On the real suite that turned 160 cases
# into 108 and scored a fully migrated tree 0 with every test passing, and it
# was invisible to a per-file count because each file alone parses correctly.
#
# So the fixture is three files, 2 + 2 + 1 = 5 cases, and BOTH arms are
# measured: the current shape must recover 5, and the old shape must recover 3.
# Asserting only the 5 would pass against a fixture too small to weld anything,
# which is a control that cannot fail.
RUN set -eux; \
    mkdir -p /tmp/pp; cd /tmp/pp; \
    printf '<testsuite name="a">\n  <testcase classname="A" name="one"/>\n  <testcase classname="A" name="two"/>\n</testsuite>\n' > TEST-a.xml; \
    printf '<testsuite name="b">\n  <testcase classname="B" name="one"/>\n  <testcase classname="B" name="two"/>\n</testsuite>\n' > TEST-b.xml; \
    printf '<testsuite name="c">\n  <testcase classname="C" name="one"/>\n</testsuite>\n' > TEST-c.xml; \
    ls TEST-a.xml TEST-b.xml TEST-c.xml > xml.txt; \
    : > fixed.txt; \
    while IFS= read -r f; do \
        { tr '\n' ' ' < "${f}" | sed 's|<testcase |\n<testcase |g' | tail -n +2; echo; } >> fixed.txt; \
    done < xml.txt; \
    while IFS= read -r f; do \
        tr '\n' ' ' < "${f}" | sed 's|<testcase |\n<testcase |g' | tail -n +2; \
    done < xml.txt > welded.txt; \
    nfixed="$(grep -c . fixed.txt)"; \
    nwelded="$(grep -c . welded.txt)"; \
    nraw="$(xargs -a xml.txt -r grep -oh '<testcase ' | grep -c .)"; \
    echo "parser fixture: raw=${nraw} fixed=${nfixed} welded=${nwelded}"; \
    test "${nraw}" = 5; \
    test "${nfixed}" = 5; \
    test "${nwelded}" = 3; \
    cd /; rm -rf /tmp/pp

# B-7's coverage rule, against a fixture whose answer is known, and in every
# arm that has to be able to fail. The rule: the ids the frozen baseline
# reported must all be covered by what this run reported, except that an id
# whose method part is its own class name is Surefire's collapse of a
# class-level @Ignore and is covered by any case reported for that class.
#
# Four arms, because a coverage check that only ever sees a complete run is a
# control that cannot fail. `none` is the one that caught a real defect: written
# the usual way, with NR == FNR, an empty first file leaves NR == FNR true for
# every line of the second, the whole baseline is swallowed as though it had
# been reported, and a tree whose reports name no test at all comes back as
# fully covered.
RUN set -eux; \
    mkdir -p /tmp/cov; cd /tmp/cov; \
    printf 'a.B#one pass\na.B#two pass\na.C#a.C skipped\n' > base.txt; \
    printf 'a.B#one\na.B#two\na.C#m1\n' > ran-ok.txt; \
    printf 'a.B#one\na.C#m1\n' > ran-drop.txt; \
    seq 1 150 | sed 's|^|z.Stub#dummy|' > ran-stub.txt; \
    : > ran-none.txt; \
    for r in ok drop stub none; do \
        awk -v idfile="ran-${r}.txt" 'FILENAME == idfile { gotid[$0] = 1; c = $0; sub(/#.*$/, "", c); gotcls[c] = 1; next } { id = $1; c = id; sub(/#.*$/, "", c); m = id; sub(/^[^#]*#/, "", m); if (m == c) { if (!(c in gotcls)) print id } else if (!(id in gotid)) print id }' \
            "ran-${r}.txt" base.txt | LC_ALL=C sort -u > "out-${r}.txt"; \
        echo "coverage fixture ${r}: uncovered=$(grep -c . "out-${r}.txt" || true)"; \
    done; \
    test "$(grep -c . out-ok.txt)"   = 0; \
    test "$(grep -c . out-drop.txt)" = 1; \
    grep -qx 'a.B#two' out-drop.txt; \
    test "$(grep -c . out-stub.txt)" = 3; \
    test "$(grep -c . out-none.txt)" = 3; \
    cd /; rm -rf /tmp/cov

# The corroboration's verdict, rehearsed on real runs of real classes rather
# than on canned captures. Step 01 could hand its verdict a printf, because
# JUnitCore's whole verdict was a line of text. This harness reads two channels
# and one of them is a directory the platform writes after the engine finishes,
# so the only honest way to rehearse it is to run the platform.
#
# fixtures/cl/ is nine classes, compiled here by this image's own javac against
# the staged harness jar, and fixtures/pv-rehearsal.sh drives ten faces through
# verdict function AND the run_class invocation read out of the shipped
# test.sh, under the same run_unprivileged demotion the scored run uses:
#
#   PassCase        2 green + 1 @Disabled          pass 2
#   VintageCase     JUnit 4, 2 green + 1 @Ignore   pass 2
#   FailCase        1 assertion + 1 throw          fail 3, both headers named
#   ClassInitFails  static initialiser throws      fail 1
#   BeforeAllFails  @BeforeAll throws              classfail 1
#   NoTests         no runnable method             notloadable
#   does.not.Exist  never compiled                 notloadable
#   HaltCase        3 of 12 counter lines, halt(0) exited
#   HaltFull       all 12 counter lines, halt(0)   exited
#   FlakyCase       red on its 1st JVM, green after  fail 2 once, pass 2 retried
#
# The last two are what this costs its build time for. HaltFull prints the
# entire twelve-line summary block of a clean green run, exits 0, and satisfies
# every check that reads stdout -- and is refused, because the engine never
# flushed TEST-junit-jupiter.xml. That is step 01's ctlD7 wearing this
# harness's costume, and it is why --reports-dir is the second channel rather
# than a convenience. PassCase is its control: an honest run leaves the engine
# report, which the rehearsal asserts rather than assumes.
#
# Two things the rehearsal proves about itself, both of which were wrong once:
# the FailCase headers are written without Jupiter's trailing "()", so they can
# be compared with the frozen 2011 ids; and 65534 can traverse /tests to reach
# the harness jar, which on the first run it could not -- every face came back
# `fault`, correctly.
#
# The tenth face is the one B-7's excusal rests on, and it is a pair rather than
# a row. FlakyCase creates a marker file in its first JVM and fails; in every
# JVM after it the file is there and it passes. Run once it is `fail 2`; run
# through the shipped corroborate_class it is `pass 2`, because a charge has to
# survive a second run. FailCase is its control -- deterministic, and still
# `fail 3` through that same function -- so a retry that simply answered `pass`
# fails this build, and one that never re-ran fails it too.
#
# The same file rehearses corrob_passed_ids, the green half of the verdict's own
# parse and what B-7 excuses a reported failure against: the red face names its
# one green method and neither red one, a skipped case is not a pass on either
# engine, and a run that left before the platform wrote its report names nothing
# at all -- an excusal cannot be bought by halting.
RUN bash /tests/fixtures/pv-rehearsal.sh

# Family C, the same treatment: the whole scored block is read out of the
# shipped test.sh and executed against eleven trees whose answer is known.
#
# Two of the eleven are measurements -- the tree step 02 starts from and the
# golden solution -- and nine are adversarial edits of a measurement. mkfix.sh
# derives those eight by rewriting arqgate.Junit5 output, which is legitimate
# because the five call summaries are exactly the column sums of the per-class
# j5.calls= lines: measured on both trees, and re-derived by the generator.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The nine in between are what make
# either of those mean anything, and each is asserted to six decimals rather
# than to a band -- a gate that has drifted lands somewhere, just not here.
# c-rehearsal.sh prints the gate-by-gate reading for all eleven afterwards, so
# the build log carries what each gate said and not only the weighted total.
#
# F7 and F8 are the two deletion trees, and they are here because the scorer
# they now police once paid for deletion: the step-01 golden tree with every
# src/test/java removed measured 0.654375, above this step's own floor, since
# C-2, C-3's departure half and C-4 were all written as "baseline minus what is
# still there". F7 deletes all 49 and must score 0; F8 deletes 10 of them from
# the finished tree and must lose about a fifth rather than nothing or all.
#
# F9 is the same kind of fixture for C-3's arrival half, and it has the same
# kind of history. That half was a tree-wide count of assertThrows calls, and
# F9 -- the golden tree with all nineteen calls moved onto a class that owned
# none of the nineteen frozen @Test(expected=) sites -- measured family C =
# 1.000000 under it, indistinguishable from the golden tree, on a tree where
# not one site was rewritten. The half is now scored per class against the
# sites each class owned, F9 must score 0.925000, and c-rehearsal.sh asserts
# F9's two distinguishing gate facts as well as its total.
#
# F10 is C-6's fixture and has no such history -- C-6 was added because a review
# round pointed out that the one gate asking whether the rewritten tests are
# still Java 6 bytecode (B-4) is worth 0.035 of the step, since the predecessor
# passes it and the floor arithmetic caps family B. F10 is the golden tree with
# the must-migrate classes recompiled to major 52 and nothing else changed: it
# must score C-1 through C-5 exactly as the golden tree does, and C-6 at 0.
#
# mkfix.sh writes into /tests/fixtures and its output is removed again, so the
# image's /tests stays byte-identical to the build context that produced it.
RUN set -eux; \
    bash /tests/fixtures/mkfix.sh \
        /tests/fixtures/j5-pinned.txt /tests/fixtures/j5-golden.txt \
        /tests /tests/fixtures; \
    bash /tests/fixtures/c-rehearsal.sh; \
    rm -f /tests/fixtures/must.txt /tests/fixtures/F1.txt /tests/fixtures/F2.txt \
          /tests/fixtures/F3.txt /tests/fixtures/F5.txt /tests/fixtures/F6.txt \
          /tests/fixtures/F7.txt /tests/fixtures/F8.txt /tests/fixtures/F9.txt \
          /tests/fixtures/F10.txt

# The same treatment for the gate that answers ctlD7. A transcript written by
# the JVM the test runs in can be forged by that test; what cannot be forged is
# whether the tree holds a compiled class able to stop the JVM before the runner
# reports. arqgate.SelfStop is what reads that, and it is rehearsed here on real
# class files compiled by this image's own javac, on eleven faces:
#
#   clean  -- the precise false positive that retired the grep this replaced, and
#             the one that retired the first draft of its replacement. It throws
#             a RuntimeException, prints the literal "java.lang.Runtime", holds
#             the string "exit", and calls Runtime.getRuntime(). Every needle
#             either scan ever looked for is in its constant pool -- as a
#             substring for the grep, and as an exact string constant for the
#             reflective rule. It calls none of them, so it must come back 0. A
#             gate whose clean face is empty of everything interesting proves
#             only that it can find nothing.
#   reflect - the other side of that rule: the same names, reached through
#             Class.forName and Method.invoke. Strings plus the machinery is a
#             call, and must come back 1 -- otherwise "clean must be 0" would be
#             satisfied by deleting the rule.
#   refowner  the spelling neither the string-pair rule nor the type rule could
#             see, and the reason the owner half is now read a third way, as the
#             owner of an invoke: Runtime.getRuntime().getClass() takes the
#             class object from the factory and from Object.getClass, so the
#             only mention of java/lang/Runtime in the whole class is the owner
#             of getRuntime -- there is no class literal to be an ldc and no
#             dotted name to be a Utf8. The face asserts both absences, on the
#             class file's bytes and on its disassembly, so that "clean must be
#             0" can never be satisfied by keeping the owner half to constants.
#             Must come back 1.
#   owneronly the other side of THAT rule, and the face that bounds the third
#             reading rather than the narrowing: it calls Runtime.getRuntime()
#             and asks the result for availableProcessors, and it calls the
#             reflection machinery, and it names no member -- no "halt", no
#             "exit", no "load" anywhere in its constant pool, which the face
#             asserts on the bytes. An invoke owner is the owner half and never
#             a hit on its own. Must come back 0. Without it, a rule that
#             charged any class that both touches the runtime and reflects
#             would pass every other face here: clean does not reflect, and
#             typed carries a class literal that would fail it for a different
#             reason.
#   sysowner  the false positive that the third reading would create if it were
#             applied to System as well as to Runtime, and the reason it is
#             not. It reads a system property and it reflects and it holds the
#             word "exit" -- three things ordinary 2011 code does together all
#             the time -- and it names System only as the owner of an invoke.
#             Must come back 0. System is never instantiated, so no candidate
#             can reach its class object the way Runtime.getRuntime() reaches
#             Runtime's; every route to it is already a Type constant or a
#             Utf8. Collecting System owners would therefore close nothing and
#             charge this face, and this face is what says so.
#   classref  the spelling the string-pair rule could not see, and the reason
#             the owner half is now read as a type as well: Runtime.class is a
#             CONSTANT_Class and never a CONSTANT_String, so
#             Runtime.class.getMethod("halt", int.class).invoke(...) puts no
#             dotted name in the pool at all -- which the face asserts, so that
#             "clean must be 0" can never again be satisfied by keeping the rule
#             Utf8-only. Must come back 1. It takes its receiver as a parameter
#             and never calls into Runtime, so the invoke-owner reading cannot
#             satisfy it either -- which the face also asserts, because a
#             control that two rules can satisfy tests neither.
#   handle -- a method-handle constant naming Runtime.halt, reached by an
#             ordinary method reference. No pair is required for this one: a
#             Handle is resolved against exactly one owner and one name, and
#             invoking it is the only thing left to do with it. The face asserts
#             the instruction stream never names halt, so the hit can only have
#             come from the constant. Must come back 1.
#   dirty  -- System.exit, as a call site.
#   jar    -- the same halt, in a jar handed over as a root, with no loose copy
#             anywhere. This is the route the loose-class-file scan could not
#             see: package the helper, delete the class, halt from the jar.
#   buried -- the same jar, one directory down instead of named. Must come back
#             0: the roots are the tree and the twelve module jars the candidate
#             published, and a tree can hold a cache of downloaded jars that
#             call System.exit for reasons that are not this candidate's.
#   typed  -- the other side of the classref face, and the only one that bounds
#             the widening rather than the narrowing. It names the two stop
#             owners AS TYPES -- a Runtime.class literal, a System.class
#             literal, a checkcast -- and it calls the reflection machinery, but
#             it never names a member: no "halt", no "exit", no "load" anywhere
#             in its constant pool, which the face asserts on the class file's
#             own bytes. Must come back 0. Without it, a rule that charged a
#             bare type reference on its own would pass every other face here:
#             golden is major 50, so the clean face has no class literal, no
#             invokedynamic and no method handle to catch it with, and
#             System.out is a field access rather than a type reference. That
#             over-widening would charge ordinary code, and this is the face
#             that sees it.
RUN set -eux; \
    GCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"; \
    unzip -l /tests/gates/build/arqgate.jar > /tmp/jar3.txt; \
    grep -q 'arqgate/SelfStop\.class' /tmp/jar3.txt; \
    rm -f /tmp/jar3.txt; \
    mkdir -p /tmp/ss/src /tmp/ss/clean /tmp/ss/dirty /tmp/ss/refl /tmp/ss/jarsrc /tmp/ss/buried \
        /tmp/ss/classref /tmp/ss/handle /tmp/ss/typed /tmp/ss/refowner \
        /tmp/ss/owneronly /tmp/ss/sysowner; \
    cd /tmp/ss; \
    printf 'public class Clean { static final String W = "exit";\n' > src/Clean.java; \
    printf '  public void go(java.io.File f) {\n' >> src/Clean.java; \
    printf '    if (!f.exists()) { throw new RuntimeException("halt " + W); }\n' >> src/Clean.java; \
    printf '    System.out.println("java.lang.Runtime");\n' >> src/Clean.java; \
    printf '    System.out.println(Runtime.getRuntime().availableProcessors()); } }\n' >> src/Clean.java; \
    printf 'public class Dirty { public void go() { System.exit(0); } }\n' > src/Dirty.java; \
    printf 'public class Halt { public void go() { Runtime.getRuntime().halt(0); } }\n' > src/Halt.java; \
    printf 'public class Refl { public void go() throws Exception {\n' > src/Refl.java; \
    printf '  Class<?> c = Class.forName("java.lang.Runtime");\n' >> src/Refl.java; \
    printf '  c.getMethod("halt", int.class).invoke(c.getMethod("getRuntime").invoke(null), 0); } }\n' \
        >> src/Refl.java; \
    printf 'public class ClassRef { public void go(Object r) throws Exception {\n' > src/ClassRef.java; \
    printf '  Runtime.class.getMethod("halt", int.class).invoke(r, 0); } }\n' \
        >> src/ClassRef.java; \
    printf 'public class RefOwner { public void go() throws Exception {\n' > src/RefOwner.java; \
    printf '  Runtime.getRuntime().getClass().getMethod("halt", int.class)\n' >> src/RefOwner.java; \
    printf '    .invoke(Runtime.getRuntime(), 0); } }\n' >> src/RefOwner.java; \
    printf 'public class OwnerOnly { public Object go(String[] a) throws Exception {\n' > src/OwnerOnly.java; \
    printf '  Runtime r = Runtime.getRuntime();\n' >> src/OwnerOnly.java; \
    printf '  return r.availableProcessors() + a.getClass().getMethod("toString").getName(); } }\n' \
        >> src/OwnerOnly.java; \
    printf 'public class SysOwner { public Object go(String[] a) throws Exception {\n' > src/SysOwner.java; \
    printf '  return System.getProperty("user.dir") + a.getClass().getMethod("exit"); } }\n' \
        >> src/SysOwner.java; \
    printf 'import java.util.function.IntConsumer;\n' > src/Handled.java; \
    printf 'public class Handled { public IntConsumer go() { return Runtime.getRuntime()::halt; } }\n' \
        >> src/Handled.java; \
    printf 'public class Typed { public Object go(String n) throws Exception {\n' > src/Typed.java; \
    printf '  Object r = Class.forName(n).getMethod(n).invoke(null);\n' >> src/Typed.java; \
    printf '  return new Class<?>[] { Runtime.class, System.class, ((Runtime) r).getClass() }; } }\n' \
        >> src/Typed.java; \
    javac -nowarn -d /tmp/ss/typed src/Typed.java; \
    javac -nowarn -d /tmp/ss/clean src/Clean.java; \
    javac -nowarn -d /tmp/ss/dirty src/Dirty.java; \
    javac -nowarn -d /tmp/ss/refl src/Refl.java; \
    javac -nowarn -d /tmp/ss/classref src/ClassRef.java; \
    javac -nowarn -d /tmp/ss/refowner src/RefOwner.java; \
    javac -nowarn -d /tmp/ss/owneronly src/OwnerOnly.java; \
    javac -nowarn -d /tmp/ss/sysowner src/SysOwner.java; \
    javac -nowarn -d /tmp/ss/handle src/Handled.java; \
    javac -nowarn -d /tmp/ss/jarsrc src/Halt.java; \
    (cd /tmp/ss/jarsrc && jar cf /tmp/ss/module.jar .); \
    cp /tmp/ss/module.jar /tmp/ss/buried/; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/clean.out /tmp/ss/clean; \
    test "$(grep -c . /tmp/ss/clean.out || true)" = 0; \
    test "$(grep -c . /tmp/ss/clean.out.unreadable || true)" = 0; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/dirty.out /tmp/ss/dirty; \
    test "$(grep -c . /tmp/ss/dirty.out || true)" = 1; \
    grep -q 'System\.exit$' /tmp/ss/dirty.out; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/refl.out /tmp/ss/refl; \
    test "$(grep -c . /tmp/ss/refl.out || true)" = 1; \
    grep -q 'reflect:Runtime$' /tmp/ss/refl.out; \
    if LC_ALL=C grep -qaF 'java.lang.Runtime' /tmp/ss/classref/ClassRef.class; then \
        echo "the classref face carries the dotted name; it is not testing the type route" >&2; \
        exit 1; \
    fi; \
    javap -c -p /tmp/ss/classref/ClassRef.class > /tmp/ss/classref.javap; \
    if grep -qF 'java/lang/Runtime.' /tmp/ss/classref.javap; then \
        echo "the classref face calls into Runtime; the invoke-owner reading would satisfy it and it is not testing the type route alone" >&2; \
        exit 1; \
    fi; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/classref.out /tmp/ss/classref; \
    test "$(grep -c . /tmp/ss/classref.out || true)" = 1; \
    grep -q 'reflect:Runtime$' /tmp/ss/classref.out; \
    javap -c -p /tmp/ss/refowner/RefOwner.class > /tmp/ss/refowner.javap; \
    javap -c -p /tmp/ss/owneronly/OwnerOnly.class > /tmp/ss/owneronly.javap; \
    typerefs() { awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^(ldc|ldc_w|ldc2_w|checkcast|instanceof|anewarray|new)$/) { print; break } }' "$1" | grep -c 'java/lang/Runtime' || true; }; \
    if [ "$(typerefs /tmp/ss/classref.javap)" = 0 ]; then \
        echo "the type detector found no Runtime type reference in classref, whose whole point is a Runtime.class literal: it is not an instrument" >&2; \
        exit 1; \
    fi; \
    for d in refowner owneronly; do \
        if LC_ALL=C grep -qaF 'java.lang.Runtime' "/tmp/ss/${d}/"*.class; then \
            echo "the ${d} face carries the dotted name; it is not testing the invoke-owner route" >&2; \
            exit 1; \
        fi; \
        if [ "$(typerefs "/tmp/ss/${d}.javap")" != 0 ]; then \
            echo "the ${d} face names Runtime as a type; it is not testing the invoke-owner route" >&2; \
            exit 1; \
        fi; \
        grep -qF 'java/lang/Runtime.getRuntime' "/tmp/ss/${d}.javap"; \
        grep -qF 'java/lang/Class.getMethod' "/tmp/ss/${d}.javap"; \
    done; \
    for w in halt exit load loadLibrary; do \
        if LC_ALL=C grep -qaF "${w}" /tmp/ss/owneronly/OwnerOnly.class; then \
            echo "the owneronly face names the member ${w}; it is not testing the owner half alone" >&2; \
            exit 1; \
        fi; \
    done; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/refowner.out /tmp/ss/refowner; \
    test "$(grep -c . /tmp/ss/refowner.out || true)" = 1; \
    grep -q 'reflect:Runtime$' /tmp/ss/refowner.out; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/owneronly.out /tmp/ss/owneronly; \
    test "$(grep -c . /tmp/ss/owneronly.out || true)" = 0; \
    test "$(grep -c . /tmp/ss/owneronly.out.unreadable || true)" = 0; \
    javap -c -p /tmp/ss/sysowner/SysOwner.class > /tmp/ss/sysowner.javap; \
    if LC_ALL=C grep -qaF 'java.lang.System' /tmp/ss/sysowner/SysOwner.class; then \
        echo "the sysowner face carries the dotted name; it is not testing the invoke-owner route" >&2; \
        exit 1; \
    fi; \
    n=$(awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^(ldc|ldc_w|ldc2_w|checkcast|instanceof|anewarray|new)$/) { print; break } }' /tmp/ss/sysowner.javap | grep -c 'java/lang/System' || true); \
    if [ "${n}" != 0 ]; then \
        echo "the sysowner face names System as a type; it is not testing the invoke-owner route" >&2; \
        exit 1; \
    fi; \
    grep -qF 'java/lang/System.getProperty' /tmp/ss/sysowner.javap; \
    grep -qF 'java/lang/Class.getMethod' /tmp/ss/sysowner.javap; \
    LC_ALL=C grep -qaF 'exit' /tmp/ss/sysowner/SysOwner.class; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/sysowner.out /tmp/ss/sysowner; \
    test "$(grep -c . /tmp/ss/sysowner.out || true)" = 0; \
    test "$(grep -c . /tmp/ss/sysowner.out.unreadable || true)" = 0; \
    javap -c -p /tmp/ss/handle/Handled.class > /tmp/ss/handle.javap; \
    if grep -qF 'java/lang/Runtime.halt' /tmp/ss/handle.javap; then \
        echo "the handle face calls halt directly; it is not testing the Handle route" >&2; \
        exit 1; \
    fi; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/handle.out /tmp/ss/handle; \
    test "$(grep -c . /tmp/ss/handle.out || true)" = 1; \
    grep -q 'Runtime\.halt$' /tmp/ss/handle.out; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/jar.out /tmp/ss/clean /tmp/ss/module.jar; \
    test "$(grep -c . /tmp/ss/jar.out || true)" = 1; \
    grep -q 'Runtime\.halt$' /tmp/ss/jar.out; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/buried.out /tmp/ss/buried; \
    test "$(grep -c . /tmp/ss/buried.out || true)" = 0; \
    javap -c -p -v /tmp/ss/typed/Typed.class > /tmp/ss/typed.javap; \
    grep -qF 'java/lang/Runtime' /tmp/ss/typed.javap; \
    grep -qF 'java/lang/System' /tmp/ss/typed.javap; \
    grep -qF 'java/lang/Class.forName' /tmp/ss/typed.javap; \
    for w in halt exit load loadLibrary; do \
        if LC_ALL=C grep -qaF "${w}" /tmp/ss/typed/Typed.class; then \
            echo "the typed face names the member ${w}; it is not testing the type route alone" >&2; \
            exit 1; \
        fi; \
    done; \
    java -cp "${GCP}" arqgate.SelfStop /tmp/ss/typed.out /tmp/ss/typed; \
    test "$(grep -c . /tmp/ss/typed.out || true)" = 0; \
    test "$(grep -c . /tmp/ss/typed.out.unreadable || true)" = 0; \
    cd /; rm -rf /tmp/ss

# ...and test.sh is the one that uses the shapes just proven correct, with the
# parser-health comparison and the coverage rule wired into B-7's verdict. A
# fixture that passes in /tmp proves nothing about the scorer unless the scorer
# runs the same thing.
#
# The bare 150-case floor B-7 used to carry is asserted gone, not merely
# replaced: a floor is satisfied by any 150 ids whatever, so a `test` task
# writing fabricated reports cleared it without running one inherited test.
#
# B-1's build half is asserted in the form that needs a published module jar
# and not only a zero exit status, because a `gradlew` that is a shell script
# exiting 0 satisfies the exit status while publishing nothing.
#
# The cap on the family-B test run is pinned the same way, and for the same
# reason the corroboration's `c_limit` is: it is the only wait in this scorer
# whose length the candidate writes, and a scorer that waits forever writes no
# reward.json at all. Every other long phase here was already pinned at image
# build; this one was not, so a revert of the wrapper would have built clean.
# Both halves are asserted -- the limit's own line, and that what the wrapper
# wraps is still the gradlew invocation and not something adjacent to it.
RUN set -eux; \
    grep -q 'tail -n +2; echo; }' /tests/test.sh; \
    grep -q 'nraw=\$(xargs -a "\${WORK}/xml.txt"' /tests/test.sh; \
    grep -q 'if \[ "\${nparsed}" -ne "\${nraw}" \]; then' /tests/test.sh; \
    test "$(grep -c 'gradlew --no-daemon --continue test' /tests/test.sh)" = 1; \
    grep -qxF 't_limit="${TESTRUN_LIMIT:-900}"' /tests/test.sh; \
    grep -A1 -F 'run_unprivileged env -C "${TREE}" timeout -s KILL "${t_limit}"' /tests/test.sh \
        | grep -qF './gradlew --no-daemon --continue test'; \
    grep -qF 'FILENAME == idfile {' /tests/test.sh; \
    grep -qF 'if (m == c) { if (!(c in gotcls)) print id }' /tests/test.sh; \
    grep -qF '"${WORK}/ran-ids.txt" /tests/baseline/testresults.txt' /tests/test.sh; \
    grep -qF 'elif [ "${uncovered}" -eq 0 ] && [ "${newfail_charged}" -eq 0 ] && [ "${contradicted}" -eq 0 ]' /tests/test.sh; \
    grep -qF 'newfail_charged=${newfail}' /tests/test.sh; \
    grep -qF 'newfail_charged=$(grep -c . "${WORK}/newfail-charged.txt" || true)' /tests/test.sh; \
    grep -qF 'LC_ALL=C comm -23 "${WORK}/newfail.txt" "${WORK}/corrob-passed-u.txt"' /tests/test.sh; \
    grep -qF 'corrob_passed_ids() {' /tests/test.sh; \
    grep -qF 'if (index($0, "<skipped") > 0) next' /tests/test.sh; \
    test "$(grep -cF 'corrob_passed_ids "${crd}" "${fq}" >> "${WORK}/corrob-passed.txt"' /tests/test.sh)" = 2; \
    if grep -qF 'comm -12 "${WORK}/newfail.txt" "${WORK}/xml-passed.txt"' /tests/test.sh; then \
        echo "the newfail excusal is reading the candidate's own report" >&2; exit 1; \
    fi; \
    grep -qF '&& [ "${declined}" -eq 0 ] &&' /tests/test.sh; \
    grep -qF 'declined=$(( cs_noroot + cs_dup + cs_nosrc + cs_exited + cs_unloadable + cs_timeout + cs_budget ))' /tests/test.sh; \
    grep -qF 'corrob_state=declined' /tests/test.sh; \
    grep -qF '&& [ "${declined}" -eq 0 ] && [ "${selfstop}" -eq 0 ]; then' /tests/test.sh; \
    for charged in cs_noroot cs_dup cs_nosrc cs_exited cs_unloadable cs_timeout cs_budget; do \
        if ! grep 'declined=' /tests/test.sh | grep -q "${charged}"; then \
            echo "the declined count no longer charges ${charged}" >&2; \
            exit 1; \
        fi; \
    done; \
    for free in cs_nested cs_init cs_other cs_testngonly; do \
        if grep 'declined=' /tests/test.sh | grep -q "${free}"; then \
            echo "the declined count charges ${free}, which is not the candidate's doing" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qF 'sed -n '"'"'s/^\([^#]*\)#.*/\1/p'"'"' /tests/baseline/testresults.txt' /tests/test.sh; \
    grep -qF 'if ! LC_ALL=C grep -qxF "${fq}" "${WORK}/baseline-cased.txt"; then' /tests/test.sh; \
    grep -qF 'elif LC_ALL=C grep -qxF "${fq}" "${WORK}/testng-cased.txt"; then' /tests/test.sh; \
    grep -qF 'cs_testngonly=$(( cs_testngonly + 1 ))' /tests/test.sh; \
    grep -qF 'testngonly=${cs_testngonly}' /tests/test.sh; \
    grep -qF 'arqgate.SelfStop "${WORK}/selfstop.txt" "${TREE}"' /tests/test.sh; \
    grep -qF '$(cat "${WORK}/modulejars.txt") > "${WORK}/selfstop.log"' /tests/test.sh; \
    grep -qF 'selfstop=$(( selfstop_hits + selfstop_unread ))' /tests/test.sh; \
    grep -qF 'if [ "${selfstop_reached}" -lt "${allclass:-0}" ]; then' /tests/test.sh; \
    if grep -qF 'selfstop_seen:-0}" -lt "${nclass}' /tests/test.sh; then \
        echo "the selfstop coverage check is comparing against the mirror, not the tree" >&2; \
        exit 1; \
    fi; \
    grep -qF 'org.junit.platform.console.ConsoleLauncher execute' /tests/test.sh; \
    grep -qF -- '--select-class="${fq}" --details=summary --disable-ansi-colors' /tests/test.sh; \
    grep -qF -- '--fail-if-no-tests --reports-dir="${crd}"' /tests/test.sh; \
    grep -qF 'run_unprivileged env -C "${md}" timeout -s KILL "${c_limit}"' /tests/test.sh; \
    grep -qE '^   c_limit=300$' /tests/test.sh; \
    grep -qF 'if [ "$(( SECONDS - c_start ))" -ge 900 ]; then' /tests/test.sh; \
    grep -qF 'java -cp "${harnessjars}:${root}:' /tests/test.sh; \
    grep -qF "harnessjars=\$(find /tests/assets/harness -name 'junit-platform-console-standalone-*.jar'" /tests/test.sh; \
    grep -qF "find /tests/assets/testlib -name '*.jar'" /tests/test.sh; \
    grep -qF '[ -z "${harnessjars}" ]; then' /tests/test.sh; \
    for sub in 'tail -1 "${cout}"' 'tail -n 1 "${cout}"'; do \
        if grep -qF -- "${sub}" /tests/test.sh; then \
            echo "the corroboration reads its verdict from one line again: ${sub}" >&2; exit 1; \
        fi; \
    done; \
    for sub in '--class-path' '--details=none' 'probe-lib'; do \
        if grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF -- "${sub}"; then \
            echo "the corroboration uses ${sub} outside a comment" >&2; exit 1; \
        fi; \
    done; \
    grep -qF 'cverdict=$(platform_verdict "${crd}" "${cout}" "${crc}" "${fq}"' /tests/test.sh; \
    grep -qF "grep -q '^Thanks for using JUnit!' \"\${out}\"" /tests/test.sh; \
    grep -qF 'if (n == 12) { for (i = 1; i <= 12; i++) print b[i]; n = 0; delete b }' /tests/test.sh; \
    grep -qF 'for x in "${rd}/TEST-junit-jupiter.xml" "${rd}/TEST-junit-vintage.xml"; do' /tests/test.sh; \
    if grep -qF '${rd}/TEST-junit-platform-suite.xml"' /tests/test.sh; then \
        echo "the verdict counts the suite report, which a halted JVM still writes" >&2; exit 1; \
    fi; \
    grep -qF 'if [ "${found:-0}" -ne "${xcases}" ]; then echo exited; return; fi' /tests/test.sh; \
    test "$(grep -cF 'sub(/\(\)$/, "", nm)' /tests/test.sh)" = 4; \
    grep -qF '&& [ "${celapsed}" -ge "${c_limit}" ]' /tests/test.sh; \
    grep -qF 'if timed_out; then cverdict=timeout; return; fi' /tests/test.sh; \
    grep -qF 'if [ "${cverdict}" = timeout ]; then cs_timeout=$(( cs_timeout + 1 )); continue; fi' /tests/test.sh; \
    grep -qF 'exited|fail|classfail) ;;' /tests/test.sh; \
    grep -qF 'run_class "${cout}.retry"' /tests/test.sh; \
    if grep -qF 'if [ "${cverdict}" = exited ]; then' /tests/test.sh; then \
        echo "only exited survives a second run; a hiccup in a charged verdict is charged" >&2; \
        exit 1; \
    fi; \
    if grep -qF 'if [ "${crc}" -eq 124 ] || [ "${crc}" -eq 137 ]; then' /tests/test.sh; then \
        echo "a timeout status is being believed without the clock agreeing" >&2; exit 1; \
    fi; \
    grep -qF 'fact "gate.testrun.corroborate.classfail=${c_classfail}"' /tests/test.sh; \
    grep -qF 'java -cp "${GCP}" arqgate.Junit5 "${WORK}/j5.txt"' /tests/test.sh; \
    grep -qF '$1 == "j5.test" && $3 == "junit5" && ($2 in m) { print $2 }' /tests/test.sh; \
    grep -qF 'if [ "${c5jc:-0}" -ge 1 ] && [ "${c5jc:-0}" -ge "${c5lc:-0}" ]; then' /tests/test.sh; \
    grep -qF 's/^j5\.base\.expected\.onmust=//p' /tests/test.sh; \
    grep -qF 's/^j5\.base\.runwith\.onmust=//p' /tests/test.sh; \
    grep -qF 's/^j5\.base\.call\.legacy=//p' /tests/test.sh; \
    grep -qF 's/^j5\.base\.call\.legacy\.msg=//p' /tests/test.sh; \
    if grep -E '^(base_exp|base_rw|base_lc|base_lm)=' /tests/test.sh | grep -q 'WORK'; then \
        echo "a frozen denominator is being read out of the candidate's own tree" >&2; exit 1; \
    fi; \
    grep -qF 'LC_ALL=C comm -23 "${WORK}/all-testclasses.txt" "${WORK}/compat.txt" > "${WORK}/must.txt"' /tests/test.sh; \
    grep -qF '0.30*a + 0.20*b + 0.15*c + 0.10*d + 0.10*e + 0.15*f' /tests/test.sh; \
    grep -qF '(0.30*a + 0.175*b) / 0.475' /tests/test.sh; \
    grep -qxF 'mig="${c_score}"' /tests/test.sh; \
    grep -qF 'printf "%.6f", t1 * t2' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when C-6 was added \
    # the arithmetic was changed and the report was not -- so every run printed \
    # five gates at the old weights while scoring six at the new ones. A number \
    # that is right, described wrongly, is the one defect a reviewer cannot see. \
    grep -qF "printf '    C1 jupiter population   %s  x0.30" /tests/test.sh; \
    grep -qF "printf '    C2 junit4 retired       %s  x0.20" /tests/test.sh; \
    grep -qF "printf '    C3 assertThrows         %s  x0.15" /tests/test.sh; \
    grep -qF "printf '    C4 runwith retired      %s  x0.10" /tests/test.sh; \
    grep -qF "printf '    C5 assertions kept      %s  x0.10" /tests/test.sh; \
    grep -qF "printf '    C6 still java 6 bytecode %s  x0.15" /tests/test.sh; \
    nrep=$(grep -cE "^printf '    C[0-9] " /tests/test.sh); \
    nfact=$(grep -cE '^fact "C[0-9]\.' /tests/test.sh); \
    if [ "${nrep}" != "${nfact}" ]; then \
        echo "the report prints ${nrep} family-C gates and the scorer publishes ${nfact}" >&2; \
        exit 1; \
    fi; \
    # And the general form of the same check, for every family at once, because \
    # asserting six literals by hand closes one instance of the hole and leaves \
    # the class of it open -- family B's seven printfs and family B's awk are the \
    # same pair of spellings, and they agree today only by habit. For each family \
    # this pulls the weights out of the report rows in order and out of that \
    # family's awk expression in order, and requires the two sequences to be \
    # equal. A family added later is covered by adding its letter to the list. \
    for fam in B:'0\.10\*b7$' C:'0\.15\*f$'; do \
        letter="${fam%%:*}"; anchor="${fam#*:}"; \
        rows=$(sed -n "s/^printf '    ${letter}[0-9] .*  x\(0\.[0-9][0-9]*\).*/\1/p" \
               /tests/test.sh | tr '\n' ' '); \
        nexpr=$(grep -cE "printf \"%\.6f\", .*${anchor}" /tests/test.sh); \
        if [ "${nexpr}" != 1 ]; then \
            echo "family ${letter}: ${nexpr} lines look like its weight expression, want 1" >&2; \
            exit 1; \
        fi; \
        coef=$(grep -E "printf \"%\.6f\", .*${anchor}" /tests/test.sh \
               | grep -oE '0\.[0-9][0-9]*\*' | sed 's/\*$//' | tr '\n' ' '); \
        if [ -z "${rows}" ] || [ -z "${coef}" ]; then \
            echo "family ${letter}: found rows [${rows}] and coefficients [${coef}]" >&2; \
            exit 1; \
        fi; \
        if [ "${rows}" != "${coef}" ]; then \
            echo "family ${letter} prints weights [${rows}] and scores with [${coef}]" >&2; \
            exit 1; \
        fi; \
        echo "family ${letter}: report weights ${rows}match the arithmetic"; \
    done; \
    for g in C1.jupiter-population C2.junit4-retired C3.assert-throws \
             C4.runwith-retired C5.assertion-conservation \
             C6.still-java6-bytecode; do \
        grep -qF "fact \"${g}=" /tests/test.sh; \
    done; \
    awk '/^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^fact "B7.tests-run=/{v=NR} /^report /{r=NR} \
         END { if (c == 0 || fc == 0 || v == 0 || r == 0) { \
                  print "family C anchor missing: banner=" c " score=" fc \
                        " B-7=" v " report=" r; exit 1 } \
               if (!(v < c && c < fc && fc < r)) { \
                  print "family C must be read after B-7 and scored before the report"; exit 1 } }' \
        /tests/test.sh; \
    grep -qF 'sed -n '"'"'s/^test\.class=//p'"'"' /tests/baseline/tests.txt' /tests/test.sh; \
    bash -n /tests/test.sh; \
    if grep -qF '"${ncases}" -ge 150' /tests/test.sh; then \
        echo "B-7 still scores on a bare case-count floor" >&2; exit 1; \
    fi; \
    grep -qF 'if [ "${poms}" -eq 0 ] && [ "${b_ok:-}" = ok ] && [ -s "${WORK}/modulejars.txt" ]; then' /tests/test.sh; \
    grep -qF 'arqgate.Scan "${WORK}/scan.txt" "${mirror}"' /tests/test.sh; \
    grep -qF 'names=/tests/baseline/classnames.txt' /tests/test.sh; \
    for carve in "-not -path './.gradle/*'" "-not -path './.git/*'"; do \
        if grep -qF -- "${carve}" /tests/test.sh; then \
            echo "the class mirror still carves out a path: ${carve}" >&2; exit 1; \
        fi; \
    done; \
    awk '/^# ---.*the inherited suite/{s=NR} /^# B-4 /{b4=NR} /^# B-5 /{b5=NR} /^# B-7 /{b7=NR} \
         /^   # --- corroboration/{co=NR} /^fact "B7.tests-run=/{v=NR} \
         END { if (s == 0 || b4 == 0 || b5 == 0 || b7 == 0 || co == 0 || v == 0) { \
                  print "anchor missing: suite=" s " B-4=" b4 " B-5=" b5 " B-7=" b7 \
                        " corroboration=" co " verdict=" v; exit 1 } \
               if (!(s < b4 && b4 < b5 && b5 < b7)) { \
                  print "B-4 and B-5 must read the tree after the suite runs"; exit 1 } \
               if (!(b7 < co && co < v)) { \
                  print "the corroboration run must sit inside B-7, before its verdict"; exit 1 } }' \
        /tests/test.sh

# The gate jar exists, contains the class it exists for, and reads a class file.
# The listing goes to a file rather than into `grep -q`: grep -q exits at the
# first match, unzip can then take EPIPE mid-write, and under `set -o pipefail`
# that becomes a failed assertion -- intermittently, depending on how much of
# the listing fit in the pipe buffer. See jar_has() in gates/build.sh.
RUN set -eux; \
    unzip -l /tests/gates/build/arqgate.jar > /tmp/jar.txt; \
    grep -q 'arqgate/Scan\.class' /tmp/jar.txt; \
    rm -f /tmp/jar.txt; \
    java -cp "/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar" \
        arqgate.Scan /tmp/scan-smoke.txt /tests/gates/build/arqgate.jar; \
    grep -q '^classes=' /tmp/scan-smoke.txt; \
    grep -q '^major.52=' /tmp/scan-smoke.txt; \
    rm -f /tmp/scan-smoke.txt /tmp/scan-smoke.txt.pairs

# arqgate.Tests runs, and separates "found nothing" from "never ran": a
# three-method class where exactly two carry @Test must come back as 2, and the
# gate jar itself -- which has no tests in it -- must come back as 0. A check
# that only ever observed a zero would pass just as well against a tool that
# does nothing at all. The unannotated third method is there so the positive
# control cannot be satisfied by counting public methods.
RUN set -eux; \
    GCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"; \
    unzip -l /tests/gates/build/arqgate.jar > /tmp/jar2.txt; \
    grep -q 'arqgate/Tests\.class' /tmp/jar2.txt; \
    rm -f /tmp/jar2.txt; \
    mkdir -p /tmp/tsmoke/classes; \
    printf 'import org.junit.Test;\npublic class Smoke { @Test public void a() {} @Test public void b() {} public void c() {} }\n' \
        > /tmp/tsmoke/Smoke.java; \
    javac -nowarn -cp /tests/assets/probe-lib/junit-4.8.1.jar \
        -d /tmp/tsmoke/classes /tmp/tsmoke/Smoke.java; \
    java -cp "${GCP}" arqgate.Tests /tmp/pos.txt /tmp/tsmoke/classes; \
    grep -q '^tests.methods=2$' /tmp/pos.txt; \
    grep -q '^tests.classes=1$' /tmp/pos.txt; \
    grep -q '^tests.frameworks=junit4$' /tmp/pos.txt; \
    java -cp "${GCP}" arqgate.Tests /tmp/neg.txt /tests/gates/build/arqgate.jar; \
    grep -q '^tests.methods=0$' /tmp/neg.txt; \
    grep -q '^tests.classes=0$' /tmp/neg.txt; \
    rm -rf /tmp/tsmoke /tmp/pos.txt /tmp/neg.txt

# arqgate.Junit5 is the instrument every gate in family C reads, and it is the
# one tool in this image that did not exist before step 02. Rehearsed on two
# classes compiled by this image's own javac, with every number known in
# advance: one Jupiter class and one JUnit 4 class, because a gate that can
# only ever answer "junit4" satisfies a one-sided control.
#
# The pair pins the four readings the C block cannot do without, each of which
# a plausible narrowing of the tool would lose. @Test(expected=) is an expected
# SITE and not just a legacy reference (j5.expected=J4Smoke#boom). assertThrows
# is counted separately from the ordinary Jupiter assertions and is also one of
# them (throws=1 out of jupiter=2), which is what lets C-3 and C-5 read the
# same tree without double-counting. That count is pinned TWICE: once as the
# summary j5.call.jupiter.throws and once in the per-class j5.calls= line of
# the class that makes the call, because C-3's arrival half asks which class
# gained the assertThrows and a summary cannot answer that. The JUnit 4 face
# pins the same per-class field at zero, so it cannot be a constant. And the
# framework field is what the class IS: the Jupiter face must carry no
# j5.runwith at all, because C-4 scores the absence of one.
#
# The fifth is the class-file major version: j5.major=<binary>|<n>, read from
# bytes 6-7 of the file header the same way arqgate.Scan reads it, so the two
# gates cannot disagree about what a class was compiled to. Both compiled faces
# pin it at 52 -- the image's javac is JDK 8 and the smoke sources carry no
# -source/-target -- which is a value neither gate ever credits, so a hard-wired
# 50 would fail here.
#
# The fourth is presence. j5.seen is the only line the tool emits about a class
# that says nothing else, and three of the six C gates now consult it before
# they credit a departure -- so a third face is run over an empty directory and
# must report j5.seen.count=0, no j5.seen line, and no j5.major line at all. That is the deleted
# tree at n=1, and it is the control that fails if presence ever goes back to
# being inferred from the absence of findings.
#
# The Jupiter face reports TWO seen classes and one test: the anonymous
# Executable is a class file of its own. That is why every presence question the
# fixtures and the scorer ask is asked about the OWNER -- deleting one source
# file removes several class files, and counting lines would read that as
# several classes deleted.
#
# The anonymous Executable rather than a lambda: the inherited suite compiles
# at -source 1.6 to keep B-4's major-50 gate, so the golden solution writes
# assertThrows this way and the gate has to read it this way.
RUN set -eux; \
    GCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"; \
    unzip -l /tests/gates/build/arqgate.jar > /tmp/jar3.txt; \
    grep -q 'arqgate/Junit5\.class' /tmp/jar3.txt; \
    rm -f /tmp/jar3.txt; \
    H=/tests/assets/harness/junit-platform-console-standalone-1.14.4.jar; \
    mkdir -p /tmp/j5s/j5 /tmp/j5s/j4; \
    printf 'import org.junit.jupiter.api.Test;\nimport static org.junit.jupiter.api.Assertions.*;\npublic class J5Smoke {\n   @Test public void a() { assertEquals(1, 1, "msg"); }\n   @Test public void b() { assertThrows(IllegalStateException.class, new org.junit.jupiter.api.function.Executable() { public void execute() {} }); }\n}\n' \
        > /tmp/j5s/J5Smoke.java; \
    printf 'import org.junit.Test;\nimport org.junit.runner.RunWith;\nimport org.junit.runners.JUnit4;\nimport static org.junit.Assert.*;\n@RunWith(JUnit4.class)\npublic class J4Smoke {\n   @Test(expected = IllegalStateException.class) public void boom() { assertEquals("m", 1L, 1L); }\n}\n' \
        > /tmp/j5s/J4Smoke.java; \
    javac -nowarn -cp "${H}" -d /tmp/j5s/j5 /tmp/j5s/J5Smoke.java; \
    javac -nowarn -cp "${H}" -d /tmp/j5s/j4 /tmp/j5s/J4Smoke.java; \
    java -cp "${GCP}" arqgate.Junit5 /tmp/j5s/a.txt /tmp/j5s/j5; \
    java -cp "${GCP}" arqgate.Junit5 /tmp/j5s/b.txt /tmp/j5s/j4; \
    cat /tmp/j5s/a.txt /tmp/j5s/b.txt; \
    grep -qxF 'j5.test=J5Smoke|junit5'   /tmp/j5s/a.txt; \
    grep -qxF 'j5.tests.junit5=1'        /tmp/j5s/a.txt; \
    grep -qxF 'j5.tests.junit4=0'        /tmp/j5s/a.txt; \
    grep -qxF 'j5.legacytests=0'         /tmp/j5s/a.txt; \
    grep -qxF 'j5.legacyrefs=0'          /tmp/j5s/a.txt; \
    grep -qxF 'j5.expected.sites=0'      /tmp/j5s/a.txt; \
    grep -qxF 'j5.call.jupiter=2'        /tmp/j5s/a.txt; \
    grep -qxF 'j5.call.jupiter.msg=1'    /tmp/j5s/a.txt; \
    grep -qxF 'j5.call.jupiter.throws=1' /tmp/j5s/a.txt; \
    grep -qxF 'j5.call.legacy=0'         /tmp/j5s/a.txt; \
    grep -qxF 'j5.calls=J5Smoke|jupiter=2,jupiter.msg=1,legacy=0,legacy.msg=0,throws=1' /tmp/j5s/a.txt; \
    grep -qxF 'j5.seen=J5Smoke'          /tmp/j5s/a.txt; \
    grep -qxF 'j5.seen=J5Smoke$1'        /tmp/j5s/a.txt; \
    grep -qxF 'j5.seen.count=2'          /tmp/j5s/a.txt; \
    grep -qxF 'j5.major=J5Smoke|52'      /tmp/j5s/a.txt; \
    grep -qxF 'j5.major=J5Smoke$1|52'    /tmp/j5s/a.txt; \
    if grep -q '^j5.runwith=' /tmp/j5s/a.txt; then \
        echo "the Jupiter face reports a @RunWith; C-4 would never reach 1" >&2; exit 1; \
    fi; \
    grep -qxF 'j5.test=J4Smoke|junit4'   /tmp/j5s/b.txt; \
    grep -qxF 'j5.tests.junit5=0'        /tmp/j5s/b.txt; \
    grep -qxF 'j5.tests.junit4=1'        /tmp/j5s/b.txt; \
    grep -qxF 'j5.legacytest=J4Smoke'    /tmp/j5s/b.txt; \
    grep -qxF 'j5.legacyrefs=4'          /tmp/j5s/b.txt; \
    grep -qxF 'j5.expected=J4Smoke#boom' /tmp/j5s/b.txt; \
    grep -qxF 'j5.expected.sites=1'      /tmp/j5s/b.txt; \
    grep -qxF 'j5.runwith=J4Smoke|org/junit/runners/JUnit4' /tmp/j5s/b.txt; \
    grep -qxF 'j5.call.jupiter=0'        /tmp/j5s/b.txt; \
    grep -qxF 'j5.call.jupiter.throws=0' /tmp/j5s/b.txt; \
    grep -qxF 'j5.call.legacy=1'         /tmp/j5s/b.txt; \
    grep -qxF 'j5.call.legacy.msg=1'     /tmp/j5s/b.txt; \
    grep -qxF 'j5.calls=J4Smoke|jupiter=0,jupiter.msg=0,legacy=1,legacy.msg=1,throws=0' /tmp/j5s/b.txt; \
    grep -qxF 'j5.seen=J4Smoke'          /tmp/j5s/b.txt; \
    grep -qxF 'j5.seen.count=1'          /tmp/j5s/b.txt; \
    grep -qxF 'j5.major=J4Smoke|52'      /tmp/j5s/b.txt; \
    mkdir -p /tmp/j5s/none; \
    java -cp "${GCP}" arqgate.Junit5 /tmp/j5s/c.txt /tmp/j5s/none; \
    cat /tmp/j5s/c.txt; \
    grep -qxF 'j5.seen.count=0'          /tmp/j5s/c.txt; \
    grep -qxF 'j5.classes=0'             /tmp/j5s/c.txt; \
    if grep -q '^j5.seen=' /tmp/j5s/c.txt; then \
        echo "an empty tree reported a class as present; presence is not being measured" >&2; \
        exit 1; \
    fi; \
    if grep -q '^j5.major=' /tmp/j5s/c.txt; then \
        echo "an empty tree reported a class-file major version" >&2; exit 1; \
    fi; \
    rm -rf /tmp/j5s

# The wrapper cache is warm: a wrapper naming this version starts without a
# download. Run as the unprivileged identity that will run the candidate's
# build, because a cache root owned by root is a cache the build cannot use.
RUN set -eux; \
    setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwrap && ./gradlew --quiet --no-daemon --version' \
        | grep -q "Gradle ${GRADLE_VERSION}"

# The module cache is warm: the reactor's whole external dependency set resolves
# with --offline. "Grading resolves nothing from the network" reduces to this
# line, so it is measured. The negative control is the line after it: the same
# resolution against an empty cache must fail, or --offline is not doing what
# the first line reads as and the first line proves nothing.
RUN set -eux; \
    setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm && /opt/arqwrap/gradlew --no-daemon -q --offline warmUp' \
        > /tmp/warm-offline.txt; \
    cat /tmp/warm-offline.txt; \
    grep -qE '^arqwarm\.artifacts=[789][0-9]$' /tmp/warm-offline.txt; \
    if setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm && /opt/arqwrap/gradlew --no-daemon -q -g /tmp/cold --offline warmUp' \
        > /tmp/cold.txt 2>&1; then \
        echo "CONTROL FAILED: an empty Gradle cache resolved the whole set offline" >&2; exit 1; \
    fi; \
    grep -q 'available for offline mode' /tmp/cold.txt; \
    rm -rf /tmp/cold /tmp/cold.txt /tmp/warm-offline.txt

# The Maven-layout copy is complete: the same set resolves from it alone, from a
# cold cache, with no other repository declared. A missing parent pom would fail
# here, which is the whole reason this check uses a cold cache. And the tree's
# own coordinates are NOT in that repository -- they are what the build under
# test is supposed to produce, and pre-seeding them would let a build that
# produced nothing still be scored as if it had.
RUN set -eux; \
    setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm && /opt/arqwrap/gradlew --no-daemon -q -g /tmp/m2check \
                  -PuseMavenLocal=1 warmUp' > /tmp/m2check.txt 2>&1; \
    cat /tmp/m2check.txt; \
    grep -qE '^arqwarm\.artifacts=[789][0-9]$' /tmp/m2check.txt; \
    rm -rf /tmp/m2check /tmp/m2check.txt; \
    test ! -e /tmp/.m2/repository/org/jboss/arquillian

# The same two measurements for the second warm project, because step 02's
# scored build resolves Jupiter and Mockito and nothing above proves those are
# in the cache: warm/ and warm2/ resolve disjoint sets, so a green line for one
# says nothing about the other. Same shape, same negative control -- the same
# resolution against an empty cache must fail.
RUN set -eux; \
    setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm2 && /opt/arqwrap/gradlew --no-daemon -q --offline warmUp' \
        > /tmp/warm2-offline.txt; \
    cat /tmp/warm2-offline.txt; \
    grep -qE '^arqwarm2\.artifacts=[123][0-9]$' /tmp/warm2-offline.txt; \
    if setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm2 && /opt/arqwrap/gradlew --no-daemon -q -g /tmp/cold2 --offline warmUp' \
        > /tmp/cold2.txt 2>&1; then \
        echo "CONTROL FAILED: an empty Gradle cache resolved the step-02 toolchain offline" >&2; exit 1; \
    fi; \
    grep -q 'available for offline mode' /tmp/cold2.txt; \
    rm -rf /tmp/cold2 /tmp/cold2.txt /tmp/warm2-offline.txt

# And in Maven layout, from a cold cache, for the same reason as above: a
# candidate build that declares mavenLocal() has to find Jupiter and Mockito
# there too, parent poms included.
RUN set -eux; \
    if ! setpriv --reuid 65534 --regid 65534 --clear-groups env HOME=/tmp TMPDIR=/tmp \
        sh -c 'cd /opt/arqwarm2 && /opt/arqwrap/gradlew --no-daemon -q -g /tmp/m2check2 \
                  -PuseMavenLocal=1 warmUp' > /tmp/m2check2.txt 2>&1; then \
        echo "the step-02 toolchain does not resolve from the Maven-layout copy:" >&2; \
        cat /tmp/m2check2.txt >&2; exit 1; \
    fi; \
    cat /tmp/m2check2.txt; \
    grep -qE '^arqwarm2\.artifacts=[123][0-9]$' /tmp/m2check2.txt; \
    rm -rf /tmp/m2check2 /tmp/m2check2.txt; \
    test ! -e /tmp/.m2/repository/org/jboss/arquillian; \
    echo "step 02 verifier image: assertions passed"

# Both directions, because a grant that is too wide is indistinguishable from a
# correct one right up until something reads the answers. Every path 65534 must
# reach is read as 65534, and every path it must not reach is proven unreadable
# through the identical construction -- so the negative arm is a measurement and
# not an assumption about which paths happen to exist.
#
# Files are probed with cat and directories with ls, because those are the two
# different questions. `ls <file>` only stats it, which needs the traverse bit
# on the parent and nothing at all on the file: the first version of this check
# used it and reported a leak on a 0700 test.sh that 65534 cannot read a byte of.
RUN set -eux; \
    SP="setpriv --reuid 65534 --regid 65534 --clear-groups"; \
    $SP head -c 1 /tests/assets/probe/run.sh > /dev/null; \
    $SP head -c 1 /tests/assets/probe/src/arqprobe/Driver.java > /dev/null; \
    $SP sh -c 'ls /tests/assets/probe-lib/*.jar' > /dev/null; \
    $SP sh -c 'ls /tests/assets/inputs' > /dev/null; \
    $SP head -c 1 /tests/assets/ids.txt > /dev/null; \
    $SP head -c 1 /tests/assets/harness/junit-platform-console-standalone-1.14.4.jar > /dev/null; \
    $SP sh -c 'ls /tests/assets/harness/*.jar' > /dev/null; \
    $SP sh -c 'ls /tests/assets/testlib/*.jar' > /dev/null; \
    $SP head -c 1 /tests/assets/testlib/mockito-core-4.11.0.jar > /dev/null; \
    for secret in /tests/baseline/tests.txt /tests/baseline/orderdep.txt \
                  /tests/baseline/testresults.txt /tests/baseline/classnames.txt \
                  /tests/baseline/compat.txt /tests/baseline/junit5.txt \
                  /tests/baseline/testng-cased.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk8 /tests/baseline \
                  /tests/runner /tests/gates /tests/warm /tests/warm2 \
                  /tests/fixtures /tests/fixtures/cl; do \
        if $SP ls "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can list ${secret}" >&2; exit 1; \
        fi; \
    done; \
    if $SP ls /tests > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests" >&2; exit 1; fi; \
    if $SP ls /tests/assets > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests/assets" >&2; exit 1; fi; \
    echo "step 02: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
