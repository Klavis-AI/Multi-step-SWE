# Verifier image for step 04, built with steps/04-java8-lang/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so everything grading needs is copied in here. Harbor skips its usual
# tests upload and runs /tests/test.sh from this image.
#
# Step 04 grades on JDK 8 and nothing else, exactly as steps 01 through 03 do,
# and for the same reason: this image carries that one JDK, no Maven, and no
# `gradle` on PATH. Step 01 retired all three and a later step must not quietly
# give one back -- a gate that passed once has to keep passing against the image
# that grades the step after it.
#
# JDK 8 is also the right compiler for what this rung asks. The step's target is
# Java 8 itself: B-4 wants class-file major 52, which is what this javac emits
# without being asked, and family C-4 puts javac two questions about the tree --
# does it compile at -source 1.8, and does it FAIL at -source 1.7. Both options
# are fully supported here. They are not everywhere: JDK 20 removed `-source 7`
# outright, and on such a compiler every tree would answer "fails at 1.7" with a
# "Source option 7 is no longer supported" that is not a language-level error at
# all -- which C-4's taxonomy check would then read as a broken compile and
# score 0 on a tree that did the work. The must-flip that separates this rung
# from its predecessor depends on this image's JDK being 8.
#
# It is also the rung's own ladder position. The authoring notes' §8.3 decision
# puts steps 01 through 05 on this one JDK and has each of them express its
# target through Gradle's sourceCompatibility/targetCompatibility rather than
# through the compiler it is graded by; the JDK moves at step 06, where the
# language level finally outruns what JDK 8 can emit.
#
# The scorer's second toolchain is inherited from step 02 unchanged. assets/harness
# is the JUnit Platform console standalone jar the corroboration drives each
# inherited test class with; assets/testlib is the run-time closure those classes
# need once they have been compiled against Jupiter and Mockito 4. Both go on the
# classpath ahead of anything from the tree, and neither is taken from the tree,
# so "the migrated suite really runs" is measured by a harness the candidate
# cannot have edited. assets/testlib is also the classpath family C compiles the
# candidate's SOURCES against, for the same reason: a classpath a candidate can
# shorten is a compile a candidate can make fail, and family C reads a failed
# compile as a fact about the tree.
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

# Build-context guard (BuildKit collision). Every step's verifier context is a
# directory named `tests`, and BuildKit's context cache treats a file as
# unchanged when (relpath, size, mode, mtime) match -- mtimes do not survive a
# git clone or a 1-second tar -- so a later step's image could receive an
# earlier step's copy of a same-sized fixture answer, baseline file or
# STAGED.txt. These are graded data and keep their bytes; instead they ship
# under per-step names (fixtures-04/, baseline-04/, assets/STAGED-04.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-04; \
    test -d /tests/baseline-04; \
    test -f /tests/assets/STAGED-04.txt; \
    mv /tests/fixtures-04 /tests/fixtures; \
    mv /tests/baseline-04 /tests/baseline; \
    mv /tests/assets/STAGED-04.txt /tests/assets/STAGED.txt
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
# 37k files to 0664 and the leak control three stanzas down caught
# /tests/helpers.sh on the next build. Sweeping first and granting by name
# afterwards makes the image's permissions a property of the Dockerfile
# instead of a property of the machine it was built on.
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
RUN echo step04 > /tests/STEP_ID

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
            *) echo "RULE-6 VIOLATION: step 04 grades on JDK 8 only, found ${v} at ${j}" >&2; exit 1;; \
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
    grep -q '^staged.baseline.files=17$' /tests/assets/STAGED.txt; \
    grep -q '^staged.fixtures=173$'     /tests/assets/STAGED.txt; \
    grep -q '^staged.warm2.deps=6$'     /tests/assets/STAGED.txt; \
    grep -q '^staged.gates=Junit5.java Scan.java SelfStop.java SourceScan.java Tests.java $' \
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
    test "$(find /tests/baseline -type f | wc -l)" = 17; \
    test "$(find /tests/fixtures -type f | wc -l)" = 173; \
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
# the verdict function AND the run_class invocation read out of the shipped
# test.sh, under the same run_unprivileged demotion the scored run uses. Eight
# classes and nine faces: the ninth is a class name this tree does not have, and
# not having it is what that face is for.
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

# B-8, B-9 and family C, the same treatment: each scored block is read out of
# the shipped test.sh and executed against inputs whose answer is known.
#
# B-8 and B-9 first. They are step 02's C-1 and C-2 lifted verbatim, but the
# block around them is new -- the baseline reads for step 02's C-3, C-4 and C-5
# were removed, the variables were renamed, and the block was given its own else
# branch and its own two facts. None of that changes what the gates measure and
# all of it is an opportunity to have changed it by accident, so the two gates
# are re-measured on seven of step 02's eleven trees and must reproduce the four
# figures step 02 measured for them, exactly. The seven are chosen for the
# properties this block can still get wrong: F1 is membership-versus-equality,
# F3 is arrival-versus-departure, F7 and F8 are deletion.
#
# mkfix.sh derives the five adversarial trees by rewriting arqgate.Junit5 output,
# which is legitimate because the five call summaries are exactly the column sums
# of the per-class j5.calls= lines: measured on both real trees, and re-derived by
# the generator. It reads baseline/junit5.txt, which is why that file is still
# staged for a step whose scorer never opens it.
#
# Then the three blocks that read the typed AST, on twenty-nine source reports.
# Two are measurements -- the tree step 04 starts from, which is step 03's
# golden, and step 04's own golden solution -- and twenty-seven are stated edits
# of a measurement.
#
# THREE blocks and not one, which is the way this rehearsal differs from step
# 03's. The compiles were hoisted above family B so that B-10 could read them,
# so the block under test is no longer contiguous: the counters are read in one
# place, spent by a family-B gate in a second, and spent again by family C in a
# third. fixtures/c-rehearsal.sh extracts all three by their own banners, asserts
# they are disjoint and in that order, and composes them in it. That ordering
# assertion is not decoration -- a hoist that silently un-hoisted would leave
# B-10 reading variables that do not exist yet, and under `set -u` that is a dead
# verifier rather than a wrong number.
#
# It asserts two figures per fixture for the same reason: familyC AND
# B10.java7-held. A rehearsal that scored only family C would pass a B-10 that
# had drifted, and B-10 is the gate holding a closed rung in place.
#
# Reports and not trees, which is the one way this rehearsal differs in kind
# from step 02's. Step 02's gate reads compiled classes and there is no honest
# way to fake one; these blocks read a flat list of counts and per-site records
# that arqgate.SourceScan wrote from a typed AST, so a fixture can state them
# directly -- and can state combinations no real tree would produce. Sixty
# lambdas in a tree that still compiles at -source 1.7 is not a tree; it is the
# only way to show that C-4 is an independent channel rather than a restatement
# of the counts, and a generator that had to emit a compilable tree could not
# write it. What that costs: nothing here says SourceScan's counts are right,
# only that the arithmetic over them is. The counts are verified separately and
# by a different instrument -- fixtures/src-rehearsal.sh, four stanzas down,
# compiles a smoke tree with the plugin attached and asserts every counter and
# every record shape against figures measured first and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files -- baseline/lambdafiles.txt, baseline/streamfiles.txt,
# baseline/samifaces.txt -- so each arrival half is rehearsed against the paths
# it will actually be asked about. Fixtures with invented paths would agree with
# a gate pointed at any set at all.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The ones in between are what make either
# of those mean anything, and each is asserted to six decimals rather than to a
# band -- a gate that has drifted lands somewhere, just not here. The figures are
# computed by fixtures/mkfix04.py, an independent implementation of the same
# composition, and written into the rehearsal by hand: a rehearsal that computes
# its own expectations at run time agrees with the scorer by construction and
# asserts nothing. This image has no python3, which makes that separation
# structural rather than a matter of discipline.
#
# Twelve of the twenty-nine carry a measured history or a load-bearing claim
# rather than a suspicion:
#
#   pinned must score familyC 0.000000 AND B-10 1.000000. It is the tree the
#      candidate is handed, and the whole floor argument at the foot of test.sh
#      rests on the second half of that: the unmigrated tree collects family B in
#      full, B-10 included, and 0.4505 is what that comes to.
#   golden must score B-10 1.000000 too, and that is the assertion that the gate
#      reads the non-anonymous generic remainder and not the raw count.
#      `newgeneric.explicit` falls 50 -> 36 across this step for an honest reason
#      -- 14 explicit-generic sites sit on anonymous classes that become lambdas
#      -- so a gate reading `explicit` would score the golden 36/50 = 0.72 on
#      that third for doing the step correctly. 36 - 13 is the same 23 that
#      50 - 27 was, and both ends of that identity are re-derived by
#      authoring/verify-baseline-step04.sh.
#   H4 and H10 are arrival in the WRONG FILES, at C-1 and C-3. They are why the
#      three set files exist and why SourceScan grew per-site stream[...] records
#      at all: `foreach.collect` reaching 0 says the 24 collecting loops are
#      gone, not that they were migrated, and a tree that deleted them and wrote
#      24 unrelated pipelines in a file of its own moves every total exactly as
#      far as one that did the work.
#   H5 is the counter this gate deliberately does not read. Adding one member to
#      an anonymous body makes it inconvertible, so `anonymousfunctional
#      .convertible` falls 36 -> 0 for a tree that typed 36 unused methods and
#      migrated nothing; `anonymousfunctional.size` does not move for that, and
#      it is what C-1's departure half divides. H5 reads 0 and its twin H3 reads
#      0 by a different route, which is why both are here.
#   H6 is round 1 of step 03's static review, applied a rung early. That round
#      found a gate whose denominator scored one correct tree's SPELLING; here,
#      24 of the golden's arriving sites are method references, so C-1's arrival
#      half counts a lambda OR a method reference in each file. H6 migrates all
#      36 to method references and nothing to lambdas, and must read 1.000000.
#      A gate reading lambda[...] records alone scores it 0.
#   H7 is the count-of-24 trick, and the reason C-2 is frozen as a set of
#      (path, Name) pairs rather than as a count: 24 trivial annotated interfaces
#      in a file of one's own satisfy any count and leave the tree's own 24
#      unmarked. `iface.samannotated` reads 24 on it and the gate reads 0.
#   H16 is the same static review's other finding, kept. A binary C-4 hands its
#      whole weight to the cheapest tree that flips it -- one lambda, plus 1.8 in
#      the build files -- and that tree already collects B-4. C-4 now scales with
#      how many file-features javac objects to at -source 1.7, 20 for full credit
#      against the golden's 47, and H16 is the one-lambda tree reading 1/20.
#      H17 is its paired control at exactly the floor, and `golden` is the other
#      one: the scaling must cost a real migration nothing.
#   H15 is the instrument admitting it has no reading. No compile produced a
#      report, so both blocks must SAY so -- gate.src=broken, gate.b10=no-report
#      -- rather than publish clean zeros that read like a finding about the tree.
#   H20 through H24 are the ladder's ninth trap, one construct at a time. Each
#      undoes a third of step 03 while doing the whole of step 04, so family C
#      reads 1.000000 on all five and B-10 does not. Under the mean a rung's
#      regression is otherwise a third of a step spread over thirteen, which is
#      to say invisible.
#   H25 and H26 are round 1 of THIS step's static review, which found C-3 --
#      0.60 of family C -- reachable without writing a single pipeline, by two
#      independent routes. H25 is the reshape: the 24 collecting loops respelled
#      as `while`, so `foreach.collect` reaches 0 while the tree still loops.
#      H26 is the bare producer: a `xs.stream();` on its own line in each of the
#      14 frozen files, so both `stream.files` and the frozen set are covered by
#      a statement that computes nothing. C-3 is three terms now -- departure,
#      no-reshape against the whole-tree loop census, and arrival requiring a
#      producer AND a stage in the same file -- and both of these read 1.000000
#      against the gate as it was and 0.000000 against the gate as it is.
#   H27 is the instrument, not the tree: a report from a verifier built on a
#      gate jar too old to emit `loop.size` at all. An absent counter read as 0
#      would make the no-reshape term read "all 139 loops removed" and hand it
#      full credit, which is this ladder's seventh trap wearing C-3's clothes.
#      The gate scores the term 0 and publishes gate.src.loop=absent, and the
#      build asserts the key is present on the smoke tree so that `absent` can
#      only ever mean a stale jar.
#
# And the rehearsal proves it can fail before its clean runs are believed: it
# perturbs the golden's report in one counter and requires the composed blocks to
# notice. That control was itself checked, against a deliberately re-weighted
# copy of test.sh, where it reported nine wrong family-C totals and four wrong
# B-10 totals.
#
# mkfix.sh writes into /tests/fixtures and its output is removed again, so the
# image's /tests stays byte-identical to the build context that produced it.
RUN set -eux; \
    bash /tests/fixtures/mkfix.sh \
        /tests/fixtures/j5-pinned.txt /tests/fixtures/j5-golden.txt \
        /tests /tests/fixtures; \
    bash /tests/fixtures/b89-rehearsal.sh; \
    rm -f /tests/fixtures/must.txt /tests/fixtures/F1.txt /tests/fixtures/F2.txt \
          /tests/fixtures/F3.txt /tests/fixtures/F5.txt /tests/fixtures/F6.txt \
          /tests/fixtures/F7.txt /tests/fixtures/F8.txt /tests/fixtures/F9.txt \
          /tests/fixtures/F10.txt

RUN bash /tests/fixtures/c-rehearsal.sh

# The three baselines and the four frozen set files family C reads, checked
# against each other rather than against a number typed a second time here.
#
# Every threshold in family C and in B-10 comes out of baseline/source.txt and
# baseline/target.txt, and the scorer derives none of them from the tree it is
# grading. That is the right design and it moves the failure: a baseline with a
# wrong number in it now produces a confident wrong score instead of an error,
# and nothing else in the image would notice. So the files are required to agree
# with each other, which is a property no single typo can satisfy.
#
# The load-bearing identity is the non-anonymous generic remainder, and it is
# load-bearing because B-10 is what holds step 03 in place while step 04 rewrites
# the same code. 50 explicit type-argument sites at the baseline, 27 of them on
# anonymous classes; 50 - 27 = 23 that are not, and that no amount of lambda
# conversion can remove. target.explicit.nonanon is that same 23 measured from
# the other end, on the golden tree, where `explicit` itself has fallen to 36
# because 14 of the 27 went away with the anonymous classes that carried them.
# If those two do not agree then B-10's generics third is measuring the step it
# is supposed to be independent of, and it scores the golden 36/50 = 0.72 for
# doing this rung correctly.
#
# The anonymous-body arithmetic is the same shape from the other side: 48
# anonymous functional bodies, 36 of them convertible, 12 left in the golden.
# 48 - 12 must come to 36, or C-1's departure half and C-1's arrival half were
# measured on two different trees.
#
# Then six invariants -- the things this rung must NOT move, each appearing in
# both files and required to be equal: the interface census (65 declared, 24
# single-abstract-method), the diamond count (93), the resource clauses (9), the
# manual close() count (0), the @SuppressWarnings sites (44) and the raw-type
# count (0). A rung that moves one of them has either redone step 03's work or
# undone it, and B-10 exists because the second is the likelier accident.
#
# And the floor claim, asserted on the baselines rather than only through a
# fixture: the tree the candidate is handed already satisfies every requirement
# B-10 states -- 93 diamonds present, the 23 non-anonymous sites still explicit,
# 9 resource clauses, no manual closes, java.io already down to its 2 pinned
# sites -- and none that family C states: no lambdas, no method references, no
# stream pipelines, no @FunctionalInterface, 36 convertible bodies unconverted
# and 24 collecting loops still loops. Those are the two halves of the
# calibration table's `pinned` row, familyC 0.0000 with B-10 1.0000, and if
# either stops being true the row stops meaning what the floor argument at the
# foot of test.sh says it means.
#
# C-3's two newer halves are asserted here as relations and not as figures,
# because round 1 of this step's static review found the figures alone
# satisfiable without a pipeline. The loop census must hold 139 statements at
# the baseline against the golden's 115, and the difference must be exactly
# foreach.collect -- 24 -- so the no-reshape term divides by the same population
# the departure half does. The 15 indexed `for`s and 10 `while`s must be the
# same at both ends, so that the term is measuring the collecting loops and not
# loop-writing in general. The records must sum to the total, and the foreach
# spelling must come to foreach.size, or the census is a counter rather than a
# census. And the baseline must hold no stage records at all: C-3's arrival half
# asks for a producer AND a stage where the loop was, and if the input tree
# already had stages there the arrival half would be paid for work the candidate
# did not do.
#
# Round 2 of the same review found the hole one level in: "where the loop was"
# was the FILE. Fourteen files hold the 24 collecting loops, so one
# `xs.stream().count();` per file -- fourteen statements, none of them a
# conversion -- satisfied the arrival half outright, and any non-loop
# replacement of the 24 (a Java 5 `addAll`, a Java 8 `Iterable.forEach`) dropped
# the census, kept the adds for family A, and left that dummy stage collecting
# 0.60 of family C. The scope is the METHOD now: each of the 24 loops is asked
# to be replaced by a pipeline in the method that held it. The relations below
# are what make that a refinement rather than a rename -- the input's stage
# buckets must be 0, the frozen denominator must be the input's own bucket
# count, the golden must reach every one of them, and there must be strictly
# more buckets (24) than files (14), or the finer scope is not finer.
#
# The four set files are not checked here, they are DERIVED -- recomputed from
# source.txt's own per-site records and required to match byte for byte:
#
#   lambdafiles.txt     the distinct paths of the anonfn[...] records whose
#                       third field reads convertible=yes                  (20)
#   streamfiles.txt     the distinct paths of the foreach[...] records whose
#                       second field reads collect=yes                     (14)
#   samifaces.txt       the distinct (path, Name) of the interfacedecl[...]
#                       records declaring exactly one abstract method      (24)
#   collectmethods.txt  the distinct <path>#<method> of those same foreach
#                       records, read off their in= field                  (24)
#
# collectmethods.txt is required to live inside streamfiles.txt exactly -- the
# 24 buckets' files are the 14, no more and no fewer -- so the two sets cannot
# drift apart, and no bucket may be the `-` sentinel SourceScan writes for a
# site with no enclosing method that survives the migration: nothing could ever
# arrive in one, so freezing one would put a term permanently out of reach.
#
# authoring/verify-baseline-step04.sh derives them the same way when it writes
# them, so this is a re-derivation and not an agreement between two copies of one
# hand-typed list. It matters because those four sets are what C-1's and C-3's
# arrival halves and C-2's whole denominator are POINTED AT, and aim is the one
# property no count can check: a path that had drifted -- a file renamed, a
# record mis-parsed, a list hand-edited once -- would move the gate's target
# without moving any total, and every other check in this image would still
# pass. Round 1 of step 03's static review found exactly that in a hand-typed
# list of two paths. This is the same finding taken seriously at 58.
#
# baseline/nioids.txt names four recordings that must still exist in
# assets/ids.txt. It is read by family A as four of 935 ids and by B-10 as the
# proviso that halves its NIO third, so an id renamed out of the recording set
# would quietly stop protecting anything in both places at once.
RUN set -eux; \
    test "$(grep -c '^major=' /tests/baseline/level.txt)" = 1; \
    grep -qxF 'major=52' /tests/baseline/level.txt; \
    anf=$(sed -n 's/^anonymousfunctional\.size=//p'        /tests/baseline/source.txt | head -1); \
    con=$(sed -n 's/^anonymousfunctional\.convertible=//p' /tests/baseline/source.txt | head -1); \
    lam=$(sed -n 's/^lambda\.size=//p'                     /tests/baseline/source.txt | head -1); \
    mrf=$(sed -n 's/^methodref\.size=//p'                  /tests/baseline/source.txt | head -1); \
    fec=$(sed -n 's/^foreach\.collect=//p'                 /tests/baseline/source.txt | head -1); \
    stp=$(sed -n 's/^stream\.producers=//p'                /tests/baseline/source.txt | head -1); \
    sof=$(sed -n 's/^stream\.opfiles=//p'                  /tests/baseline/source.txt | head -1); \
    sopm=$(sed -n 's/^stream\.opmethods=//p'               /tests/baseline/source.txt | head -1); \
    fcm=$(sed -n 's/^foreach\.collectmethods=//p'          /tests/baseline/source.txt | head -1); \
    blop=$(sed -n 's/^loop\.size=//p'                      /tests/baseline/source.txt | head -1); \
    idc=$(sed -n 's/^iface\.decl=//p'                      /tests/baseline/source.txt | head -1); \
    ism=$(sed -n 's/^iface\.sam=//p'                       /tests/baseline/source.txt | head -1); \
    isa=$(sed -n 's/^iface\.samannotated=//p'              /tests/baseline/source.txt | head -1); \
    exp=$(sed -n 's/^newgeneric\.explicit=//p'             /tests/baseline/source.txt | head -1); \
    ean=$(sed -n 's/^newgeneric\.explicitanon=//p'         /tests/baseline/source.txt | head -1); \
    dia=$(sed -n 's/^newgeneric\.diamond=//p'              /tests/baseline/source.txt | head -1); \
    raw=$(sed -n 's/^newgeneric\.raw=//p'                  /tests/baseline/source.txt | head -1); \
    res=$(sed -n 's/^try\.resources=//p'                   /tests/baseline/source.txt | head -1); \
    clo=$(sed -n 's/^close\.manual=//p'                    /tests/baseline/source.txt | head -1); \
    ion=$(sed -n 's/^iofile\.new=//p'                      /tests/baseline/source.txt | head -1); \
    iod=$(sed -n 's/^iofile\.decl=//p'                     /tests/baseline/source.txt | head -1); \
    ref=$(sed -n 's/^niofile\.ref=//p'                     /tests/baseline/source.txt | head -1); \
    srf=$(sed -n 's/^niofile\.staticref=//p'               /tests/baseline/source.txt | head -1); \
    sup=$(sed -n 's/^suppress\.sites=//p'                  /tests/baseline/source.txt | head -1); \
    tanf=$(sed -n 's/^target\.anonfn=//p'            /tests/baseline/target.txt | head -1); \
    tcon=$(sed -n 's/^target\.convertible=//p'       /tests/baseline/target.txt | head -1); \
    tlam=$(sed -n 's/^target\.lambda=//p'            /tests/baseline/target.txt | head -1); \
    tmrf=$(sed -n 's/^target\.methodref=//p'         /tests/baseline/target.txt | head -1); \
    tlfa=$(sed -n 's/^target\.lambda\.files=//p'     /tests/baseline/target.txt | head -1); \
    tlfn=$(sed -n 's/^target\.lambdafiles=//p'       /tests/baseline/target.txt | head -1); \
    tsam=$(sed -n 's/^target\.sam=//p'               /tests/baseline/target.txt | head -1); \
    tsan=$(sed -n 's/^target\.samannotated=//p'      /tests/baseline/target.txt | head -1); \
    tidc=$(sed -n 's/^target\.iface\.decl=//p'       /tests/baseline/target.txt | head -1); \
    tfec=$(sed -n 's/^target\.foreach\.collect=//p'  /tests/baseline/target.txt | head -1); \
    tstp=$(sed -n 's/^target\.stream\.producers=//p' /tests/baseline/target.txt | head -1); \
    tsop=$(sed -n 's/^target\.stream\.ops=//p'       /tests/baseline/target.txt | head -1); \
    tstf=$(sed -n 's/^target\.stream\.files=//p'     /tests/baseline/target.txt | head -1); \
    tsof=$(sed -n 's/^target\.stream\.opfiles=//p'   /tests/baseline/target.txt | head -1); \
    topm=$(sed -n 's/^target\.stream\.opmethods=//p' /tests/baseline/target.txt | head -1); \
    tcm=$(sed -n 's/^target\.collectmethods=//p'     /tests/baseline/target.txt | head -1); \
    tlop=$(sed -n 's/^target\.loop\.size=//p'        /tests/baseline/target.txt | head -1); \
    tlfo=$(sed -n 's/^target\.loop\.for=//p'         /tests/baseline/target.txt | head -1); \
    tlwh=$(sed -n 's/^target\.loop\.while=//p'       /tests/baseline/target.txt | head -1); \
    terr=$(sed -n 's/^target\.level17\.errors=//p'   /tests/baseline/target.txt | head -1); \
    tflo=$(sed -n 's/^target\.level17\.floor=//p'    /tests/baseline/target.txt | head -1); \
    tdia=$(sed -n 's/^target\.diamond=//p'           /tests/baseline/target.txt | head -1); \
    tnon=$(sed -n 's/^target\.explicit\.nonanon=//p' /tests/baseline/target.txt | head -1); \
    tres=$(sed -n 's/^target\.try\.resources=//p'    /tests/baseline/target.txt | head -1); \
    tclo=$(sed -n 's/^target\.close\.manual=//p'     /tests/baseline/target.txt | head -1); \
    tio=$(sed -n  's/^target\.iofile=//p'            /tests/baseline/target.txt | head -1); \
    tref=$(sed -n 's/^target\.niofile\.ref=//p'      /tests/baseline/target.txt | head -1); \
    traw=$(sed -n 's/^target\.raw=//p'               /tests/baseline/target.txt | head -1); \
    tsup=$(sed -n 's/^target\.suppress\.sites=//p'   /tests/baseline/target.txt | head -1); \
    bdia=$(sed -n 's/^target\.b10\.diamond=//p'      /tests/baseline/target.txt | head -1); \
    bclo=$(sed -n 's/^target\.b10\.close=//p'        /tests/baseline/target.txt | head -1); \
    bnio=$(sed -n 's/^target\.b10\.nio=//p'          /tests/baseline/target.txt | head -1); \
    test "${anf}" = 48; \
    test "${con}" = 36; \
    test "${exp}" = 50; \
    test "${ean}" = 27; \
    test "${dia}" = 93; \
    test "${res}" = 9; \
    test "${idc}" = 65; \
    test "${ism}" = 24; \
    test "${sup}" = 44; \
    test "${tanf}" = 12; \
    test "${tlam}" = 60; \
    test "${tmrf}" = 24; \
    test "${tlfn}" = 20; \
    test "${tsan}" = 24; \
    test "${tstp}" = 24; \
    test "${tsop}" = 48; \
    test "${tstf}" = 14; \
    test "${terr}" = 47; \
    test "${tflo}" = 20; \
    test "${tnon}" = 23; \
    test "${tio}"  = 2; \
    test "${bclo}" = 11; \
    test "${bnio}" = 11; \
    test "$(( exp - ean ))" = "${tnon}"; \
    test "$(( anf - tanf ))" = "${con}"; \
    test "${dia}" = "${tdia}"; \
    test "${dia}" = "${bdia}"; \
    test "${idc}" = "${tidc}"; \
    test "${ism}" = "${tsam}"; \
    test "${ism}" = "${tsan}"; \
    test "${res}" = "${tres}"; \
    test "${clo}" = "${tclo}"; \
    test "${sup}" = "${tsup}"; \
    test "${raw}" = "${traw}"; \
    test "${raw}" = 0; \
    test "${clo}" = 0; \
    test "${lam}" = 0; \
    test "${mrf}" = 0; \
    test "${stp}" = 0; \
    test "${isa}" = 0; \
    test "${tcon}" = 0; \
    test "${tfec}" = 0; \
    test "${fec}" -gt 0; \
    test "${con}" -gt 0; \
    test "${anf}" -gt "${tanf}"; \
    test "$(( ion + iod ))" = "${tio}"; \
    test "$(( ref + srf ))" = "${tref}"; \
    test "${bclo}" -gt "${clo}"; \
    test "${bclo}" -ge "${tres}"; \
    test "${bnio}" -gt 0; \
    test "${bnio}" -le "${ref}"; \
    test "${tsop}" -ge "$(( tstp * 2 ))"; \
    test "${sof}"  = 0; \
    test "${tsof}" = "${tstf}"; \
    test "${sopm}" = 0; \
    test "${fcm}"  = "${tcm}"; \
    test "${topm}" = "${tcm}"; \
    test "${tcm}" -le "${fec}"; \
    test "${tcm}" -gt "${tstf}"; \
    test "${blop}" = 139; \
    test "${tlop}" = 115; \
    test "$(( blop - tlop ))" = "${fec}"; \
    test "${tlfo}" = 15; \
    test "${tlwh}" = 10; \
    test "$(sed -n 's/^loop\[[0-9]*\]=//p' /tests/baseline/source.txt | grep -c .)" = "${blop}"; \
    test "$(sed -n 's/^loop\[[0-9]*\]=//p' /tests/baseline/source.txt | awk '$2 == "for" { n++ } END { print n + 0 }')" = "${tlfo}"; \
    test "$(sed -n 's/^loop\[[0-9]*\]=//p' /tests/baseline/source.txt | awk '$2 == "while" { n++ } END { print n + 0 }')" = "${tlwh}"; \
    test "$(sed -n 's/^loop\[[0-9]*\]=//p' /tests/baseline/source.txt | awk '$2 == "foreach" { n++ } END { print n + 0 }')" = "$(sed -n 's/^foreach\.size=//p' /tests/baseline/source.txt | head -1)"; \
    test "$(sed -n 's/^streamop\[[0-9]*\]=//p' /tests/baseline/source.txt | grep -c .)" = 0; \
    grep -q '^stream\.opmethods=' /tests/baseline/source.txt; \
    grep -q '^foreach\.collectmethods=' /tests/baseline/source.txt; \
    test "$(sed -n 's/^foreach\[[0-9]*\]=//p' /tests/baseline/source.txt | awk '$0 !~ / in=/ { n++ } END { print n + 0 }')" = 0; \
    test "$(sed -n 's/^stream\[[0-9]*\]=//p'  /tests/baseline/source.txt | awk '$0 !~ / in=/ { n++ } END { print n + 0 }')" = 0; \
    test "${tflo}" -gt 0; \
    test "${tflo}" -lt "${terr}"; \
    test "${tlfn}" -lt "${tlfa}"; \
    test "${tlfa}" -le "$(( tlfn + tstf ))"; \
    sed -n 's/^anonfn\[[0-9]*\]=//p' /tests/baseline/source.txt \
        | awk '$3 == "convertible=yes" { split($1, a, ":"); print a[1] }' \
        | LC_ALL=C sort -u > /tmp/derived-lambdafiles.txt; \
    sed -n 's/^foreach\[[0-9]*\]=//p' /tests/baseline/source.txt \
        | awk '$2 == "collect=yes" { split($1, a, ":"); print a[1] }' \
        | LC_ALL=C sort -u > /tmp/derived-streamfiles.txt; \
    sed -n 's/^interfacedecl\[[0-9]*\]=//p' /tests/baseline/source.txt \
        | awk '$3 == "abstract=1" { split($1, a, ":"); print a[1], $2 }' \
        | LC_ALL=C sort -u > /tmp/derived-samifaces.txt; \
    sed -n 's/^foreach\[[0-9]*\]=//p' /tests/baseline/source.txt \
        | awk '$2 == "collect=yes" { split($1, a, ":"); m = ""; \
                 for (i = 3; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                 if (m != "") print a[1] "#" m }' \
        | LC_ALL=C sort -u > /tmp/derived-collectmethods.txt; \
    for s in lambdafiles streamfiles samifaces collectmethods; do \
        grep '^[a-z]' "/tests/baseline/${s}.txt" > "/tmp/frozen-${s}.txt"; \
        if ! cmp -s "/tmp/derived-${s}.txt" "/tmp/frozen-${s}.txt"; then \
            echo "baseline/${s}.txt is not what baseline/source.txt's own per-site records say it is:" >&2; \
            diff "/tmp/derived-${s}.txt" "/tmp/frozen-${s}.txt" >&2 || true; \
            exit 1; \
        fi; \
    done; \
    test "$(grep -c . /tmp/frozen-lambdafiles.txt)" = "${tlfn}"; \
    test "$(grep -c . /tmp/frozen-streamfiles.txt)" = "${tstf}"; \
    test "$(grep -c . /tmp/frozen-samifaces.txt)"   = "${tsam}"; \
    test "$(grep -c . /tmp/frozen-collectmethods.txt)" = "${tcm}"; \
    awk -F'#' '{ print $1 }' /tmp/frozen-collectmethods.txt \
        | LC_ALL=C sort -u > /tmp/cm-files.txt; \
    if ! cmp -s /tmp/cm-files.txt /tmp/frozen-streamfiles.txt; then \
        echo "the 24 collect buckets do not live in exactly the 14 frozen collect files:" >&2; \
        diff /tmp/cm-files.txt /tmp/frozen-streamfiles.txt >&2 || true; \
        exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java$' \
        /tmp/frozen-lambdafiles.txt /tmp/frozen-streamfiles.txt; then \
        echo "baseline/lambdafiles.txt or streamfiles.txt: a line is not a source path" >&2; \
        exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java [A-Z][A-Za-z0-9_]*$' \
        /tmp/frozen-samifaces.txt; then \
        echo "baseline/samifaces.txt: a line is not a (path, interface name) pair" >&2; \
        exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]*$' \
        /tmp/frozen-collectmethods.txt; then \
        echo "baseline/collectmethods.txt: a line is not a <path>#<method> bucket" >&2; \
        exit 1; \
    fi; \
    if grep -F '#-' /tmp/frozen-collectmethods.txt; then \
        echo "baseline/collectmethods.txt: a bucket with no enclosing method cannot be reached" >&2; \
        exit 1; \
    fi; \
    rm -f /tmp/derived-lambdafiles.txt /tmp/derived-streamfiles.txt /tmp/derived-samifaces.txt \
          /tmp/derived-collectmethods.txt /tmp/cm-files.txt \
          /tmp/frozen-lambdafiles.txt /tmp/frozen-streamfiles.txt /tmp/frozen-samifaces.txt \
          /tmp/frozen-collectmethods.txt; \
    test "$(grep -c '^[a-z]' /tests/baseline/nioids.txt)" = 4; \
    grep '^[a-z]' /tests/baseline/nioids.txt | while read -r id; do \
        if ! grep -qxF "${id}" /tests/assets/ids.txt; then \
            echo "baseline/nioids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; \
            exit 1; \
        fi; \
    done; \
    echo "step 04: the source, target, level, nioids and set-file baselines agree with each other"

# Family C's instrument, rehearsed on a real tree.
#
# fixtures/c-rehearsal.sh proves the arithmetic over SourceScan's counters is
# the arithmetic that was calibrated; it cannot prove the counters, because it
# feeds the block hand-written reports. This is the other half. fixtures/smoke
# is one file carrying one instance of every construct family C reads, it is
# compiled here by this image's own javac with the plugin attached, and every
# count is asserted separately against a figure that was measured first and
# frozen afterwards -- not predicted from reading SourceScan.java, which is the
# way a rehearsal ends up asserting the bug.
#
# It is also the only place C-4's mechanism runs end to end. The tree compiles
# at -source 1.8 and fails at -source 1.7 with exactly two errors -- one about a
# lambda and one about a method reference, both "not supported in -source 1.7";
# the rehearsal then cuts the file back to its Java 7 form and requires the 1.7
# probe to stop failing and the Java 8 counters to fall to zero. A probe that
# answered "not supported" for every tree would pass the first half and fail the
# second.
#
# The classpath expression and srcgate() are lifted out of /tests/test.sh by
# anchor rather than copied, so what runs here is what the scorer runs. A shelf
# change that shortens the scorer's compile classpath fails this layer -- which
# is measured, not hoped for: the check was run against a deliberately shortened
# SRCCP and reported 76 jars where 77 were staged.
RUN bash /tests/fixtures/src-rehearsal.sh

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
#             clean carries no class literal to catch it with, and System.out is
#             a field access rather than a type reference. That over-widening
#             would charge ordinary code, and this is the face that sees it.
#
# What changes at THIS rung, and it is not the faces. Step 03's golden was major
# 51 and carried no lambda, so its classes held no invokedynamic and no method
# handle at all; step 04's golden compiles 60 lambdas and 24 method references,
# which is 84 invokedynamic sites and 84 bootstrap method-handle constants
# arriving in the tree this gate reads. None of them is a hit, because
# noteHandle and noteType are keyed on the OWNER and
# java/lang/invoke/LambdaMetafactory is not a stop owner -- but "none of them is
# a hit" is now a claim about the golden rather than a vacuous truth, and the
# handle face is what stands behind it. That face reaches Runtime.halt through
# an ordinary method reference, which is to say through the exact construct this
# step asks the candidate to write 24 of: the same bytecode shape, one stop
# owner away from the golden's. It must come back 1 while the golden comes back
# clean, and the calibration run asserts the second half on the real tree.
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
    grep -qF 'LC_ALL=C comm -23 "${WORK}/all-testclasses.txt" "${WORK}/compat.txt" > "${WORK}/must.txt"' /tests/test.sh; \
    # Step 02's C-3, C-4 and C-5 are gone, and the four frozen denominators they \
    # read with them. Asserted as gone rather than left to rot: a dead read of \
    # baseline/junit5.txt would still score B-8 and B-9 correctly while telling \
    # a reviewer this step grades something it does not. \
    for dead in 'c5jc' 'base_exp=' 'base_rw=' 'base_lc=' 'base_lm=' \
                'j5.base.expected.onmust' 'j5.base.runwith.onmust' \
                'j5.base.call.legacy'; do \
        if grep -qF "${dead}" /tests/test.sh; then \
            echo "step 02's retired gate reads are still in test.sh: ${dead}" >&2; exit 1; \
        fi; \
    done; \
    # Family C reaches the tree through srcgate and nowhere else, and srcgate is \
    # a scorer-owned compile against a scorer-owned classpath. Both halves are \
    # asserted: the classpath comes off the staged shelf, and the level comes \
    # from the argument rather than from anything the tree carries. \
    grep -qF 'GATE_GATES=/tests/gates GATE_SOURCE="${lvl}"' /tests/test.sh; \
    grep -qF 'ls /tests/assets/testlib/*.jar; ls /tests/assets/harness/*.jar;' /tests/test.sh; \
    if grep -E '^SRCCP=' /tests/test.sh | grep -qE 'TREE|/app'; then \
        echo "family C compiles against a classpath derived from the candidate's tree" >&2; exit 1; \
    fi; \
    test "$(grep -cE '^ *ok(18|17|def)=\$\(srcgate ' /tests/test.sh)" = 3; \
    # The three frozen files family C reads, each read from /tests and not from \
    # the tree. bv() and tv() name their paths as literals for exactly this. \
    grep -qF "bv() { sed -n \"s/^\$1=//p\" /tests/baseline/source.txt | head -1; }" /tests/test.sh; \
    grep -qF "tv() { sed -n \"s/^\$1=//p\" /tests/baseline/target.txt | head -1; }" /tests/test.sh; \
    grep -qF '/tests/baseline/nioids.txt' /tests/test.sh; \
    grep -qF 'sed -n '"'"'s/^major=//p'"'"' /tests/baseline/level.txt' /tests/test.sh; \
    grep -qF 'p * (0.15*a + 0.15*b + 0.60*c + 0.10*d)' /tests/test.sh; \
    grep -qF '0.4*d + 0.3*r + 0.3*n' /tests/test.sh; \
    grep -qF '(0.30*a + 0.175*b) / 0.475' /tests/test.sh; \
    grep -qxF 'mig="${c_score}"' /tests/test.sh; \
    grep -qF 'printf "%.6f", t1 * t2' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when step 03's C-6 \
    # was added the arithmetic was changed and the report was not -- so every run \
    # printed five gates at the old weights while scoring six at the new ones. A \
    # number that is right, described wrongly, is the one defect a reviewer cannot \
    # see. Every weight in this rung's two families is a fresh literal: family C \
    # was re-weighted for this step because C-3 is the third of it automation \
    # cannot do, and re-weighted a second time once the automated migrator had \
    # been run and measured; family B grew a tenth gate, which moved the other \
    # nine. \
    grep -qF "printf '    B10 java 7 held     %s  x0.12" /tests/test.sh; \
    grep -qF "printf '    C1 lambdas              %s  x0.15" /tests/test.sh; \
    grep -qF "printf '    C2 @FunctionalInterface %s  x0.15" /tests/test.sh; \
    grep -qF "printf '    C3 streams              %s  x0.60" /tests/test.sh; \
    grep -qF "printf '    C4 requires java 8      %s  x0.10" /tests/test.sh; \
    # The multiplier is printed too, and deliberately NOT as an `x0.NN` row: it \
    # is not a weighted term, and a report that listed it as one would be \
    # describing a different composition than the one being scored. \
    grep -qF "printf '    anti-gaming penalty     %s  (multiplies family C)" /tests/test.sh; \
    nrep=$(grep -cE "^printf '    C[0-9][0-9]* " /tests/test.sh); \
    nfact=$(grep -cE '^fact "C[0-9][0-9]*\.' /tests/test.sh); \
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
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B's tenth gate arrives at this rung, and a one-digit \
    # pattern reads nine rows against ten coefficients and calls the difference a \
    # mismatch -- or worse, would have silently skipped B10 on both sides if the \
    # coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    for fam in B:10:'0\.12\*b10$' C:4:'0\.10\*d[)]$'; do \
        letter="${fam%%:*}"; rest="${fam#*:}"; \
        want="${rest%%:*}"; anchor="${rest#*:}"; \
        rows=$(sed -n "s/^printf '    ${letter}[0-9][0-9]* .*  x\(0\.[0-9][0-9]*\).*/\1/p" \
               /tests/test.sh | tr '\n' ' '); \
        nrows=$(printf '%s\n' ${rows} | grep -c .); \
        if [ "${nrows}" != "${want}" ]; then \
            echo "family ${letter}: ${nrows} weighted gates in the report, want ${want}" >&2; \
            exit 1; \
        fi; \
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
    for g in C1.lambdas C2.functionalinterface C3.streams C4.requires-java8 \
             B8.jupiter-held B9.junit4-held B10.java7-held; do \
        grep -qF "fact \"${g}=" /tests/test.sh; \
    done; \
    # Step 03's family C is gone, and its four gate names with it. Asserted as \
    # gone rather than left to rot: a dead read of target.explicit.nonanon under \
    # its old name would still score this rung's family C correctly while telling \
    # a reviewer the step grades the diamond operator. The retired constructs \
    # themselves are still graded -- by B-10, under new names and new \
    # denominators -- which is exactly the confusion worth foreclosing. \
    # \
    # `-source 1.6` is deliberately NOT in this list. C-4's comment names it in \
    # prose, correctly, to explain which javac message the taxonomy check has to \
    # tell apart from this rung's, and a sweep that banned the string would ban \
    # the explanation. \
    for dead in 'C1.diamond' 'C2.try-with-resources' 'C3.nio-file' \
                'C4.requires-java7' 'requires-java7' 'ok16=' '0.35*a' \
                'pinned.iofile' 'pinned.iocall'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 03's retired family-C gate reads are still in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # The order of the whole scorer, asserted rather than assumed. Two of these \
    # are new at this rung and both are hoists: the three srcgate compiles moved \
    # ABOVE family B so that B-10 could read their counters, and B-10 itself sits \
    # between family B's own gates and family C. Under `set -u` an un-hoist is not \
    # a wrong number, it is a dead verifier -- which is the good case. The bad \
    # case is a hoist that lands after the reads and leaves the variables at their \
    # earlier values, and only the line numbers can tell those apart. \
    awk '/^# -+ the source AST, compiled once$/{s=NR} \
         /^# -+ family B$/{b=NR} /^# -+ B-10: step 03, held$/{t=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || c == 0 || fc == 0 || v == 0 \
                   || b9 == 0 || fb10 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " Cbanner=" c " C=" fc " B-7=" v " B-9=" b9 \
                        " B10fact=" fb10 " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so B-10 cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, family C, its score, the report"; exit 1 } \
               if (!(fc < fb)) { \
                  print "family B is summed before family C has finished scoring"; exit 1 } }' \
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

# B-6's instrument, rehearsed on the one transformation this step is made of.
#
# New at this rung, and not because apisig.sh changed -- it is inherited whole
# from step 01. What changed is what the tree does. B-6 asks whether the public
# surface moved and charges every EXTRA declaration as well as every missing
# one, at x0.14, the largest single weight in family B. Step 04 rewrites 36
# anonymous classes into 60 lambdas and 24 method references, which deletes 36
# class files and adds 84 invokedynamic sites and a private synthetic method per
# lambda. If any of that reached the signature set, the golden solution would
# lose B-6 for doing the step correctly, and so would every honest candidate.
#
# The argument that it does not is short -- anonymous classes are dropped by the
# $[0-9] filter and javap -protected never prints a private method -- and short
# arguments about an instrument are exactly what gets asserted here rather than
# believed. Three faces of one class, differing only in how the Runnable is
# produced:
#
#   anon  an anonymous Runnable, which is what the tree holds now
#   lam   the same thing as a lambda, which is what 60 of the sites become
#   mref  the same thing as a method reference, which is what 24 of them become
#
# All three must produce BYTE-IDENTICAL signature sets, and the faces are
# required to be genuinely different first: anon must carry an ApiSmoke$1 class
# file and neither of the others may, lam must carry exactly one invokedynamic
# and one lambda$ method, and mref must carry an invokedynamic and NO lambda$ --
# a method reference to an existing method needs no synthesised body, which is
# the difference between the two arrivals and the reason both are here.
#
# The count is pinned at 6 declarations because `cmp` of two empty files
# succeeds. A rehearsal whose clean control is "the two outputs agree" agrees
# perfectly when javap has silently written nothing at all.
#
# Both halves of the short argument were then checked by breaking them, because
# an argument that has not been broken has not been tested. Deleting apisig.sh's
# $[0-9] filter makes the anon face gain `TYPE class ApiSmoke$1 implements
# java.lang.Runnable` and this block fails. Changing its `javap -protected` to
# `javap -p` makes the anon face gain `static void access$000(ApiSmoke)` -- the
# synthetic accessor javac has to synthesise so that an anonymous class can
# reach its enclosing instance's private method -- and this block fails again.
# That second one is the finding worth keeping: a lambda calling the same
# private method needs no accessor, so the surface really does differ between
# the two forms, and B-6 survives this step only because public+protected is
# where it draws the line. It draws it there deliberately and now demonstrably.
#
# The class implements Callable<String>, so javac emits a bridge -- the
# signature set carries both `public java.lang.String call()` and
# `public java.lang.Object call()`. apisig.sh keeps bridges deliberately (see
# its header), so the pair also shows that the bridge does not move when the
# lambda arrives, which is the one way erasure could have made this differ.
#
# And the paired synthetic failure, because a gate that cannot fail has not
# passed: a fourth face adds one public method to the anon face and nothing
# else. It must produce 7 declarations, differ from anon by exactly one line,
# and that line must name the method. That is also the true positive this gate
# exists for at this rung -- a candidate who "converts" an anonymous class by
# promoting it to a named nested class widens the surface, because $Helper is
# not $1 and the filter does not drop it.
RUN set -eux; \
    mkdir -p /tmp/api/src-anon /tmp/api/src-lam /tmp/api/src-mref /tmp/api/src-extra; \
    cd /tmp/api; \
    printf 'public class ApiSmoke implements java.util.concurrent.Callable<String> {\n' > base.java; \
    printf '   public String call() { return "x"; }\n' >> base.java; \
    printf '   protected java.util.List<String> names() { return null; }\n' >> base.java; \
    printf '   private void helper() { }\n' >> base.java; \
    printf 'BODY\n}\n' >> base.java; \
    anonbody='   public Runnable go() { return new Runnable() { public void run() { helper(); } }; }'; \
    sed "s|BODY|${anonbody}|" base.java > src-anon/ApiSmoke.java; \
    sed 's|BODY|   public Runnable go() { return () -> helper(); }|' base.java > src-lam/ApiSmoke.java; \
    sed 's|BODY|   public Runnable go() { return this::helper; }|' base.java > src-mref/ApiSmoke.java; \
    sed "s|BODY|${anonbody}\n   public void extra() { }|" base.java > src-extra/ApiSmoke.java; \
    for face in anon lam mref extra; do \
        mkdir -p "/tmp/api/b-${face}" "/tmp/api/j-${face}"; \
        javac -nowarn -d "/tmp/api/b-${face}" "/tmp/api/src-${face}/ApiSmoke.java"; \
        ( cd "/tmp/api/b-${face}" && jar cf "/tmp/api/j-${face}/arquillian-smoke-1.0.0-SNAPSHOT.jar" . ); \
        bash /tests/gates/apisig.sh "/tmp/api/api-${face}.txt" \
            "/tmp/api/j-${face}/arquillian-smoke-1.0.0-SNAPSHOT.jar"; \
    done; \
    test -f /tmp/api/b-anon/'ApiSmoke$1.class'; \
    test -f /tmp/api/b-extra/'ApiSmoke$1.class'; \
    test ! -e /tmp/api/b-lam/'ApiSmoke$1.class'; \
    test ! -e /tmp/api/b-mref/'ApiSmoke$1.class'; \
    test "$(javap -p -c -classpath /tmp/api/b-anon ApiSmoke | grep -c invokedynamic || true)" = 0; \
    test "$(javap -p -c -classpath /tmp/api/b-lam  ApiSmoke | grep -c invokedynamic)" = 1; \
    test "$(javap -p -c -classpath /tmp/api/b-mref ApiSmoke | grep -c invokedynamic)" = 1; \
    test "$(javap -p -classpath /tmp/api/b-anon ApiSmoke | grep -c 'lambda\$' || true)" = 0; \
    test "$(javap -p -classpath /tmp/api/b-lam  ApiSmoke | grep -c 'lambda\$')" = 1; \
    test "$(javap -p -classpath /tmp/api/b-mref ApiSmoke | grep -c 'lambda\$' || true)" = 0; \
    cat /tmp/api/api-anon.txt; \
    for face in anon lam mref; do \
        test "$(grep -c . "/tmp/api/api-${face}.txt")" = 6; \
        if ! cmp -s /tmp/api/api-anon.txt "/tmp/api/api-${face}.txt"; then \
            echo "apisig.sh reads a different public surface off the ${face} face:" >&2; \
            diff /tmp/api/api-anon.txt "/tmp/api/api-${face}.txt" >&2 || true; \
            exit 1; \
        fi; \
    done; \
    grep -qF ':: public java.lang.String call();'  /tmp/api/api-anon.txt; \
    grep -qF ':: public java.lang.Object call() throws java.lang.Exception;' /tmp/api/api-anon.txt; \
    grep -qF ':: public java.lang.Runnable go();'  /tmp/api/api-anon.txt; \
    grep -qF ':: protected java.util.List<java.lang.String> names();' /tmp/api/api-anon.txt; \
    if grep -q 'lambda\$\|\$1' /tmp/api/api-lam.txt /tmp/api/api-anon.txt; then \
        echo "a synthesised lambda body or an anonymous class reached the signature set" >&2; \
        exit 1; \
    fi; \
    test "$(grep -c . /tmp/api/api-extra.txt)" = 7; \
    test "$(diff /tmp/api/api-anon.txt /tmp/api/api-extra.txt | grep -c '^> ')" = 1; \
    diff /tmp/api/api-anon.txt /tmp/api/api-extra.txt | grep '^> ' | grep -qF ':: public void extra();'; \
    echo "step 04: B-6 reads the same public surface through an anonymous class, a lambda and a method reference"; \
    rm -rf /tmp/api

# The other half of B-6's instrument check, and the half this rung turned out to
# need: the same bridge, in an interface.
#
# Moving -source/-target to 1.8 changes what javac writes for a covariant
# override between two interfaces, with no source change at all. `interface
# ProtocolDef extends ContainerDef` redeclares `property` with a narrower return
# type; at class-file 50 and 51 the bridge for that can only live on the
# implementing classes, because those versions forbid a method body in an
# interface, and at 52 javac emits it in the interface as well. The first
# calibration of the golden solution scored B-6 at 0.999128 for exactly this --
# one extra declaration, no source change, 0.14 of family B, and a step that
# cannot be completed correctly for full marks.
#
# So: a two-interface hierarchy compiled twice, at 1.6 and at 1.8, and the
# requirement is that apisig.sh reads the SAME surface off both. The faces are
# proved different first -- -target 1.6 must NOT carry the member and -target
# 1.8 must, with ACC_BRIDGE on it -- or the clean comparison below is a javac
# that never emitted the bridge rather than a filter that dropped it.
#
# And the paired failure, because the filter reads flags for a reason: a third
# face adds a default method the AUTHOR wrote. That is a widened surface, it
# lands in exactly the place the filter drops from, and it must still be
# charged. A filter written as "an interface method that is not abstract" would
# pass the first half of this block and go blind to the second.
RUN set -eux; \
    mkdir -p /tmp/iface/src; cd /tmp/iface; \
    printf 'public interface IBase { IBase self(); }\n' > src/IBase.java; \
    printf 'public interface IDeep extends IBase { IDeep self(); }\n' > src/IDeep.java; \
    cp -r src src-def; \
    printf 'public interface IDeep extends IBase { IDeep self(); default void authored() { } }\n' \
        > src-def/IDeep.java; \
    mkdir -p b6 b8 bdef j6 j8 jdef; \
    javac -nowarn -source 1.6 -target 1.6 -Xlint:-options -d b6 src/*.java; \
    javac -nowarn -d b8 src/*.java; \
    javac -nowarn -d bdef src-def/*.java; \
    for f in 6 8 def; do \
        ( cd "b${f}" && jar cf "../j${f}/arquillian-iface-1.0.0-SNAPSHOT.jar" . ); \
    done; \
    if javap -protected -classpath b6 IDeep | grep -q 'public IBase self();'; then \
        echo "CONTROL FAILED: a class-file 50 interface already carries the bridge" >&2; \
        exit 1; \
    fi; \
    javap -protected -classpath b8 IDeep | grep -q 'public IBase self();'; \
    javap -v -protected -classpath b8 IDeep | grep -q 'ACC_BRIDGE'; \
    for f in 6 8 def; do \
        bash /tests/gates/apisig.sh "/tmp/iface/api-${f}.txt" \
            "/tmp/iface/j${f}/arquillian-iface-1.0.0-SNAPSHOT.jar"; \
    done; \
    cat /tmp/iface/api-8.txt; \
    test "$(grep -c . /tmp/iface/api-6.txt)" = 4; \
    if ! cmp -s /tmp/iface/api-6.txt /tmp/iface/api-8.txt; then \
        echo "B-6 reads a different public surface at -target 1.8 than at 1.6:" >&2; \
        diff /tmp/iface/api-6.txt /tmp/iface/api-8.txt >&2 || true; \
        exit 1; \
    fi; \
    test "$(grep -c . /tmp/iface/api-8.txt.dropped)" = 1; \
    test "$(grep -c . /tmp/iface/api-6.txt.dropped)" = 0; \
    grep -qF ':: public IBase self();' /tmp/iface/api-8.txt.dropped; \
    test "$(grep -c . /tmp/iface/api-def.txt)" = 5; \
    test "$(diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep -c '^> ')" = 1; \
    diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep '^> ' \
        | grep -qF ':: public void authored();'; \
    echo "step 04: B-6 reads the same interface surface at class-file 50 and at 52, and still charges a default method the author wrote"; \
    rm -rf /tmp/iface

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

# arqgate.Junit5 is the instrument B-8 and B-9 read, and it is the one tool in
# this image that did not exist before step 02. Rehearsed on two classes
# compiled by this image's own javac, with every number known in advance: one
# Jupiter class and one JUnit 4 class, because a gate that can only ever answer
# "junit4" satisfies a one-sided control.
#
# The readings pinned here are more than B-8 and B-9 consume, and the rehearsal
# is kept whole anyway. It was written when this tool WAS family C -- at step 02
# its C-1 through C-6 read every field below -- and the two gates that survived
# the move to family B read four of them. Narrowing a reviewed rehearsal to
# today's consumers buys nothing and costs the fields' history, and a field this
# image stops checking is a field the next rung's author may quietly break.
#
# @Test(expected=) is an expected SITE and not just a legacy reference
# (j5.expected=J4Smoke#boom). assertThrows is counted separately from the
# ordinary Jupiter assertions and is also one of them (throws=1 out of
# jupiter=2), which is what let step 02's C-3 and C-5 read the same tree without
# double-counting. That count is pinned TWICE: once as the summary
# j5.call.jupiter.throws and once in the per-class j5.calls= line of the class
# that makes the call, because a per-class question cannot be answered from a
# summary. The JUnit 4 face pins the same per-class field at zero, so it cannot
# be a constant. And the framework field is what the class IS -- the Jupiter
# face must carry no j5.runwith at all -- which is the field B-8 reads for
# equality and not for membership.
#
# The fifth is the class-file major version: j5.major=<binary>|<n>, read from
# bytes 6-7 of the file header the same way arqgate.Scan reads it, so the two
# gates cannot disagree about what a class was compiled to. Both compiled faces
# pin it at 52 -- the image's javac is JDK 8 and the smoke sources carry no
# -source/-target -- which is a value neither gate ever credits, so a hard-wired
# 50 would fail here.
#
# The fourth is presence. j5.seen is the only line the tool emits about a class
# that says nothing else, and B-9 consults it before it credits a departure -- so
# a third face is run over an empty directory and must report j5.seen.count=0,
# no j5.seen line, and no j5.major line at all. That is the deleted tree at n=1,
# and it is the control that fails if presence ever goes back to being inferred
# from the absence of findings.
#
# The Jupiter face reports TWO seen classes and one test: the anonymous
# Executable is a class file of its own. That is why every presence question the
# fixtures and the scorer ask is asked about the OWNER -- deleting one source
# file removes several class files, and counting lines would read that as
# several classes deleted.
#
# And a FOURTH face, new at this rung, which is the same Jupiter class with the
# Executable written as a lambda. Step 03's version of this rehearsal did not
# need it: that suite compiled at -source 1.7, two releases short of a lambda,
# so the anonymous Executable was the only form the golden could write. At
# -source 1.8 it is not, and the anonymous Executable in the inherited suite is
# one of the 48 anonymous functional bodies SourceScan counts -- ten of the
# twenty files C-1 reads are test sources. So the form B-8 and B-9 are handed
# changes underneath them at exactly this step, and the instrument has to be
# shown reading both.
#
# Measured, and the interesting half is what moves: j5.classes and
# j5.seen.count fall from 2 to 1, because the lambda leaves no class file
# behind. Everything the two surviving gates read is identical --
# j5.test=...|junit5, j5.tests.junit5=1, j5.call.jupiter=2,
# j5.call.jupiter.throws=1, and the whole per-class j5.calls= line including
# throws=1. assertThrows is counted at the CALL SITE and not by the shape of its
# second argument, which is the property that makes B-8 and B-9 survive this
# rung, and the face is what says so rather than the reasoning above.
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
    mkdir -p /tmp/j5s/lam; \
    printf 'import org.junit.jupiter.api.Test;\nimport static org.junit.jupiter.api.Assertions.*;\npublic class J5Lambda {\n   @Test public void a() { assertEquals(1, 1, "msg"); }\n   @Test public void b() { assertThrows(IllegalStateException.class, () -> { }); }\n}\n' \
        > /tmp/j5s/J5Lambda.java; \
    javac -nowarn -cp "${H}" -d /tmp/j5s/lam /tmp/j5s/J5Lambda.java; \
    java -cp "${GCP}" arqgate.Junit5 /tmp/j5s/d.txt /tmp/j5s/lam; \
    cat /tmp/j5s/d.txt; \
    test "$(find /tmp/j5s/lam -name '*.class' | wc -l)" = 1; \
    test "$(find /tmp/j5s/j5  -name '*.class' | wc -l)" = 2; \
    grep -qxF 'j5.test=J5Lambda|junit5'  /tmp/j5s/d.txt; \
    grep -qxF 'j5.tests.junit5=1'        /tmp/j5s/d.txt; \
    grep -qxF 'j5.tests.junit4=0'        /tmp/j5s/d.txt; \
    grep -qxF 'j5.legacyrefs=0'          /tmp/j5s/d.txt; \
    grep -qxF 'j5.call.jupiter=2'        /tmp/j5s/d.txt; \
    grep -qxF 'j5.call.jupiter.msg=1'    /tmp/j5s/d.txt; \
    grep -qxF 'j5.call.jupiter.throws=1' /tmp/j5s/d.txt; \
    grep -qxF 'j5.calls=J5Lambda|jupiter=2,jupiter.msg=1,legacy=0,legacy.msg=0,throws=1' /tmp/j5s/d.txt; \
    grep -qxF 'j5.seen=J5Lambda'         /tmp/j5s/d.txt; \
    grep -qxF 'j5.seen.count=1'          /tmp/j5s/d.txt; \
    grep -qxF 'j5.classes=1'             /tmp/j5s/d.txt; \
    grep -qxF 'j5.major=J5Lambda|52'     /tmp/j5s/d.txt; \
    if grep -q '^j5.seen=J5Lambda\$' /tmp/j5s/d.txt; then \
        echo "the lambda face reported a nested class; it is not the anonymous form" >&2; exit 1; \
    fi; \
    if grep -q '^j5.runwith=' /tmp/j5s/d.txt; then \
        echo "the lambda face reports a @RunWith" >&2; exit 1; \
    fi; \
    norm='grep -vE "[$]1|^j5[.](seen[.]count|classes)=" | LC_ALL=C sort'; \
    sed 's/J5Smoke/CLS/'  /tmp/j5s/a.txt | eval "${norm}" > /tmp/j5s/a-norm.txt; \
    sed 's/J5Lambda/CLS/' /tmp/j5s/d.txt | eval "${norm}" > /tmp/j5s/d-norm.txt; \
    if ! cmp -s /tmp/j5s/a-norm.txt /tmp/j5s/d-norm.txt; then \
        echo "arqgate.Junit5 reads the anonymous and lambda Executable differently:" >&2; \
        diff /tmp/j5s/a-norm.txt /tmp/j5s/d-norm.txt >&2 || true; \
        exit 1; \
    fi; \
    test "$(grep -c . /tmp/j5s/a-norm.txt)" = 19; \
    grep -qxF 'j5.call.jupiter.throws=1' /tmp/j5s/a-norm.txt; \
    echo "step 04: arqgate.Junit5 reads assertThrows at the call site, anonymous Executable or lambda"; \
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
    echo "step 04 verifier image: assertions passed"

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
                  /tests/baseline/testng-cased.txt /tests/baseline/source.txt \
                  /tests/baseline/target.txt /tests/baseline/nioids.txt \
                  /tests/baseline/level.txt \
                  /tests/baseline/lambdafiles.txt /tests/baseline/samifaces.txt \
                  /tests/baseline/streamfiles.txt /tests/baseline/collectmethods.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/gates/build/arqsource.jar \
                  /tests/gates/source-gate.sh /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh \
                  /tests/fixtures/b89-rehearsal.sh /tests/fixtures/c/golden/report.txt \
                  /tests/fixtures/src-rehearsal.sh \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk8 /tests/baseline \
                  /tests/runner /tests/gates /tests/warm /tests/warm2 \
                  /tests/fixtures /tests/fixtures/cl /tests/fixtures/c \
                  /tests/fixtures/c/golden /tests/fixtures/smoke; do \
        if $SP ls "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can list ${secret}" >&2; exit 1; \
        fi; \
    done; \
    if $SP ls /tests > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests" >&2; exit 1; fi; \
    if $SP ls /tests/assets > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests/assets" >&2; exit 1; fi; \
    echo "step 04: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
