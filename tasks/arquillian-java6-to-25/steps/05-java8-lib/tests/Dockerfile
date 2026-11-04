# Verifier image for step 05, built with steps/05-java8-lib/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so everything grading needs is copied in here. Harbor skips its usual
# tests upload and runs /tests/test.sh from this image.
#
# Step 05 grades on JDK 8 and nothing else, exactly as steps 01 through 04 do,
# and for the same reason: this image carries that one JDK, no Maven, and no
# `gradle` on PATH. Step 01 retired all three and a later step must not quietly
# give one back -- a gate that passed once has to keep passing against the image
# that grades the step after it.
#
# JDK 8 is also the right compiler for what this rung asks, and here that is a
# sharper requirement than it was below. The step's subject is the Java 8
# LIBRARY -- Optional, the Collectors factories, Objects, java.time -- and every
# construct gate in family C reads a TYPED AST: `Optional` in a javadoc and
# `Optional` in a return type differ by nothing a grep can see, so
# arqgate.SourceScan resolves each site through javac's own Elements, against a
# classpath the scorer owns. That resolution needs the java.* types the step is
# about to be on the compile's platform classpath, at the version the step
# targets. A JDK that shipped a different Optional would be answering a
# different question with the same counter names.
#
# What this rung does NOT need is step 04's `-source 1.7` probe, and the reason
# is worth stating because it changes the shape of the whole step. Steps 04 and
# 05 are both class-file major 52 -- B-4 does not move here -- so this rung has
# no class-file must-flip and no level taxonomy to get right. Its must-flip is
# C-5 instead, which reads the constant pools of the jars the candidate's own
# BUILD published: a different instrument, on a different artifact, asking
# whether the library reached what this project actually ships. That is why
# `unzip` is asserted present below rather than assumed. A missing unzip makes
# C-5 report "the published jars carry no class files", which reads as a finding
# about the tree rather than as a broken image, and it would be the ladder's
# sixth trap sprung in the one place where nothing else could corroborate it.
#
# It is also the rung's own ladder position, and the last one on this JDK. The
# authoring notes' §8.3 decision puts steps 01 through 05 on this one JDK and
# has each of them express its target through Gradle's
# sourceCompatibility/targetCompatibility rather than through the compiler it is
# graded by; the JDK moves at step 06, where the language level finally outruns
# what JDK 8 can emit.
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
# under per-step names (fixtures-05/, baseline-05/, assets/STAGED-05.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-05; \
    test -d /tests/baseline-05; \
    test -f /tests/assets/STAGED-05.txt; \
    mv /tests/fixtures-05 /tests/fixtures; \
    mv /tests/baseline-05 /tests/baseline; \
    mv /tests/assets/STAGED-05.txt /tests/assets/STAGED.txt
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
RUN echo step05 > /tests/STEP_ID

# The bytecode gate. Built here, from source, with this image's own JDK 8 javac
# -- the same jar the JDK 25 images of steps 12-13 build from the same source.
# arqgate.SourceScan is the javac plugin the whole of family C reads, so at this
# rung this layer builds the step's principal instrument and not only B-4's.
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
            *) echo "RULE-6 VIOLATION: step 05 grades on JDK 8 only, found ${v} at ${j}" >&2; exit 1;; \
        esac; \
    done; \
    test "${found}" -gt 0; \
    if command -v mvn    >/dev/null 2>&1; then echo "RULE-6 VIOLATION: Maven is reachable from the scorer" >&2; exit 1; fi; \
    if command -v gradle >/dev/null 2>&1; then echo "RULE-6 VIOLATION: a gradle on PATH would bypass the candidate's wrapper" >&2; exit 1; fi; \
    test ! -e /opt/gradle; \
    javap -version; \
    # C-5 shells out to unzip, and this rung is the first where that is a \
    # SCORING dependency rather than a build-time one: gates/build.sh has always \
    # used it to inspect a jar, but now the must-flip itself unzips the \
    # candidate's published jars to read their constant pools. Named here \
    # because the failure is silent -- no unzip means 0 classes extracted, which \
    # C-5 prints as "the published jars carry no class files", which is a \
    # sentence about the tree. \
    command -v unzip >/dev/null; \
    unzip -v | head -1

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
    grep -q '^staged.baseline.files=21$' /tests/assets/STAGED.txt; \
    grep -q '^staged.fixtures=257$'     /tests/assets/STAGED.txt; \
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
    test "$(find /tests/baseline -type f | wc -l)" = 21; \
    test "$(find /tests/fixtures -type f | wc -l)" = 257; \
    # And the twenty-one by NAME, because a count is satisfied by any \
    # twenty-one files and this rung's staging recipe adds four to the rung \
    # below's eight. Four of the twenty-one are this step's own frozen SETS, \
    # and a set that failed to stage does not make the scorer fail -- \
    # wantbuckets() over a missing file yields an empty want, frac() over an \
    # empty denominator yields the scorer's zero-denominator answer, and the \
    # gate pays the tree for a migration nobody checked. \
    for b in api artifacts classnames collectmethods compat junit5 lambdafiles \
             level nioids optbuckets orderdep samifaces sinks source \
             streamfiles target testng-cased testresults tests timesites \
             valuetypes; do \
        if [ ! -s "/tests/baseline/${b}.txt" ]; then \
            echo "baseline/${b}.txt did not stage, or staged empty" >&2; exit 1; \
        fi; \
    done; \
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
# Then the four blocks that read the typed AST, on forty source reports.
# Two are measurements -- the tree step 05 starts from, which is step 04's
# golden, and step 05's own golden solution -- and thirty-eight are stated edits of
# a measurement.
#
# FOUR blocks and not three, which is the way this rehearsal differs from step
# 04's. The compiles were hoisted above family B at step 04 so that B-10 could
# read them, and this rung adds B-11 beside it: the counters are read in one
# place, spent by B-10 in a second, spent again by B-11 in a third and by family
# C in a fourth. fixtures/c-rehearsal.sh extracts all four by their own banners,
# asserts they are disjoint and in that order, and composes them in it. That
# ordering assertion is not decoration -- a hoist that silently un-hoisted would
# leave B-10 or B-11 reading variables that do not exist yet, and under `set -u`
# that is a dead verifier rather than a wrong number.
#
# It asserts THREE figures per fixture for the same reason step 04 asserted two:
# familyC, B10.java7-held AND B11.java8lang-held. A rehearsal that scored only
# family C would pass a B-10 or a B-11 that had drifted, and those two are the
# gates holding two closed rungs in place. All three are compared AS STRINGS and
# not as numbers, which matters at exactly one row: the no-report fixture must
# print the literal `0` that the guarded blocks publish when they never ran, and
# `0.000000` is the same number and a different fact.
#
# Reports and not trees, which is the one way this rehearsal differs in kind
# from step 02's -- with one exception this rung introduces. These blocks read a
# flat list of counts and per-site records that arqgate.SourceScan wrote from a
# typed AST, so a fixture can state them directly, and can state combinations no
# real tree would produce. C-5 is not like that: it unzips the jars the
# candidate's build published and reads four package names off the bytes of the
# class files inside them. So every fixture carries a REAL `jars/` directory
# holding real zip archives with real class-file members, and the rehearsal
# shells out to the same unzip the scorer does. What is being rehearsed there is
# the counting, the four floors and the mean -- not javac's class-file format --
# so a synthesised constant pool is the honest fixture and a compiled one would
# only be a slower way to write the same bytes.
#
# What that still costs: nothing here says SourceScan's counts are right, only
# that the arithmetic over them is. The counts are verified separately and by a
# different instrument -- fixtures/src-rehearsal.sh, two stanzas down, compiles
# a smoke tree with the plugin attached and asserts every counter and every
# record shape against figures measured first and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files -- optbuckets.txt, sinks.txt, valuetypes.txt, timesites.txt, and step
# 04's lambdafiles.txt, samifaces.txt and collectmethods.txt that B-11 divides
# by -- so each half of each gate is rehearsed against the buckets it will
# actually be asked about. Fixtures with invented paths would agree with a gate
# pointed at any set at all. The departure side goes further and lifts the
# retnull, accum and millis records out of baseline/source.txt verbatim, so the
# residual population a fixture leaves standing is the real one.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The ones in between are what make either
# of those mean anything, and each is asserted to six decimals rather than to a
# band -- a gate that has drifted lands somewhere, just not here. The figures are
# computed by fixtures/mkfix05.py, an independent implementation of the same
# composition, and written into the rehearsal by hand: a rehearsal that computes
# its own expectations at run time agrees with the scorer by construction and
# asserts nothing. This image has no python3, which makes that separation
# structural rather than a matter of discipline.
#
# Seventeen of the forty carry a measured history or a load-bearing claim
# rather than a suspicion:
#
#   pinned must score familyC 0.000000 AND B-10 1.000000 AND B-11 1.000000. It
#      is the tree the candidate is handed, and the whole floor argument at the
#      foot of test.sh rests on the last two: the unmigrated tree collects family
#      B in full, and 0.30*1 + 0.70*(0.25*1 + 0.75*0) = 0.475 is what that comes
#      to against migration_prompt item 6's 0.5. It is the tightest floor on the
#      ladder and this row is where it is checked rather than argued.
#   golden must score all three 1.000000, and the third is the co-existence
#      claim: this step converts 23 of B-11's 24 collect buckets from a terminal
#      forEach(sink::add) to collect(Collectors...), and a converted bucket still
#      holds a stream producer and still holds a stream op, so B-11 reads it
#      exactly as full after the conversion as before.
#   K8 is the LinkedHashSet, and it is the reason C-2 is frozen as a map to a
#      FACTORY and not as a count. It collects 22 of the 23 sinks faithfully and
#      writes toSet() at DynamicServiceLoader#createInstances, whose sink is
#      constructed as a LinkedHashSet and whose iteration order is in the 935
#      recordings family A replays. It must read 0.983152 -- one bucket of 23
#      off full -- which is the only row that can tell a fidelity term that
#      checks the factory from one that checks that a collector was written.
#   K9 is the other side of it: every sink collected with toSet whatever it was
#      constructed as. 4 of 23 are accepted and 19 are not, and it reads
#      0.679891. Together with K11 -- twelve buckets migrated, all through
#      toCollection, 0.814674 -- these two are what pin the acceptance rule:
#      toCollection is accepted where toList or toSet is wanted, toSet is not
#      accepted where toCollection is. That rule is load-bearing on the golden
#      itself, which spells 7 of the 23 with toCollection.
#   K3 and K4 are the null MOVED rather than removed, which is the failure this
#      whole gate exists to charge. K3 consumes all 25 Optionals with get() and
#      reads 0.662500 -- C-1 whole is gone despite 16 correct signatures. K4
#      writes `if (o.isPresent()) return o.get()` and reads 0.831250, half of
#      C-1, because half of its operations are still get().
#   K6 and K10 are arrival in the WRONG BUCKETS, at C-1 and C-2. They are why
#      the four set files exist: a total that reaches 0 says the sites are gone,
#      not that they were migrated, and a tree that deleted them and wrote 16
#      Optionals or 23 collectors in a file of its own moves every count exactly
#      as far as one that did the work. Both read the full loss of their gate.
#   K18 is the per-bucket site count, and the reason timesites.txt carries one.
#      It converts one of the two currentTimeMillis calls in
#      ServletMethodExecutor#execute -- the loop condition, leaving the loop
#      bound as arithmetic on a primitive -- and reads 0.965625 rather than
#      full. A departure term keyed on the bucket alone cannot see it.
#   K19 AND K38 are why every leg of C-5 is a FLOOR and not a flag, taken from
#      the two ends. K19 is one Optional in one published class and no source
#      work anywhere: it reads 0.000000, because a leg nothing corroborates pays
#      nothing whatever its reach. K38 is that same short reach corroborated --
#      all four source gates full, and jars naming java/util/Optional in 2 of
#      the 5 published classes the floor asks for. Its Optional leg is 2/5 times
#      a C-1 of 1.000000, and family C reads 0.977500. A binary C-5 would hand
#      that tree the whole gate and could not tell the two rows apart.
#   K37 is the tree static review round 1 priced, and it is why C-5's legs are
#      scaled at all. It is this rung's INPUT -- the step-04 golden, not one
#      line migrated, all four source gates 0 -- carrying a dummy private method
#      in each of six classes that buys constant-pool reach equal to the
#      golden's: gate.cp reads 10 / 13 / 3 / 4, and the facts assert that the
#      references really are there. Under a C-5 that paid on its own reach that
#      tree took familyC 0.150000 and a reward of 0.553750, over migration_prompt
#      item 6's bar of 0.5 with nothing done. It now reads 0.000000. K37 fails
#      if the scaling is dropped and K38 fails if the floors are, which is why
#      both rows are here rather than either.
#   K20 is the CEILING anyone claimed for an automated migrator, and it is no
#      longer a prediction about one. The reasoning it encodes -- the static-
#      analysis catalogues all carry the equals/hashCode rewrite, so such a tree
#      takes C-3 whole and the one quarter of C-5 that C-3 puts in the constant
#      pools, 0.137500 of family C -- was tested in authoring/automigrate-
#      step05.sh against thirteen recipes from three catalogues, and the real
#      figure is 0.000000: none of the equals/hashCode recipes fires on an
#      Eclipse-generated nested-if equals. The row stays, because the scorer
#      still has to add 0.137500 for a tree in that shape and this is where that
#      is proved; what it no longer claims is that anything produces one.
#   K21, K22 and K23 are C-5's instrument failing three ways -- jars published
#      with no class files in them, no module jar published at all, and every
#      source gate full with the published jars carrying none of it. All three
#      read 0.850000, which is family C less exactly C-5's weight, and together
#      they say the leg is reading the artifact rather than the tree.
#   K24 and K25 are the anti-gaming penalty, which multiplies family C rather
#      than sitting beside it: five raw types added over a baseline of zero, and
#      six @SuppressWarnings sites added over the frozen 44. Both read 0.500000
#      on an otherwise perfect family C.
#   K26 is the instrument admitting it has no reading, and it is the row that
#      records a deliberate asymmetry. No compile produced a report, so the
#      source-AST block, B-10 and B-11 must all SAY so -- gate.src=broken, the
#      literal `0` for both held-gates -- rather than publish clean zeros that
#      read like a finding about the tree. C-5 is NOT inside that guard, because
#      it never needed the report: it reads the jars, and a tree whose sources
#      will not compile can still have published jars from an earlier build. So
#      C-5 still RUNS here and its four gate.cp counts are real readings; what
#      it cannot do is PAY, because the source legs it is scaled by are the
#      guard's untouched literal `0`. familyC reads 0.000000 -- and reads it as
#      a MEASURED zero, `0.000000`, beside the unmeasured literal `0` the two
#      held-gates publish, which is the asymmetry this row records. The
#      rehearsal asserts it structurally as well as numerically: it locates the
#      guard's closing `fi` and requires the C-5 stanza to be after it.
#   K27 through K30 are the ladder's ninth trap at step 03, one construct at a
#      time: the diamonds unwound (0.600000), the resource clauses unwound
#      (0.700000), the java.nio.file migration unwound (0.700000), and the
#      migration intact with its four recorded behaviours broken (0.850000).
#      Each does the whole of step 05 -- family C reads 1.000000 on all four --
#      while undoing a third of step 03. Under the mean a rung's regression is
#      otherwise a third of a step spread over thirteen, which is invisible.
#      K27 is also the one that had to be re-based: the golden holds 74 diamonds
#      and not 93, because 19 of the 93 were the sink constructions C-2 collects
#      away, so unwinding the golden's diamonds returns 74 and not 93 and the
#      fixture says 74.
#   K31 through K33 are the same trap at step 04: the lambdas and method
#      references unwound back to anonymous classes (0.800000), the
#      @FunctionalInterface annotations removed from all 24 (0.800000), and the
#      stream pipelines unwound out of all 24 buckets (0.400000). All three
#      score family C full.
#   K34 AND K35 ARE THE PAIR, and they are here because B-11 and C-2 read the
#      same 23 buckets from opposite ends and a rehearsal that scored either one
#      alone would be rehearsing a coincidence. K34 turns the 23 sinks back into
#      plain `for` loops: C-2 is lost (0.612500) and B-11 falls to 0.425000,
#      one collect bucket of 24 surviving. K35 does only C-2 and does it
#      correctly: family C reads 0.387500 -- C-2 whole, plus the quarter of C-5
#      that writing 23 Collectors calls puts in the constant pools -- and B-11
#      reads 1.000000, unmoved. Full-and-full beside zero-and-zero is what says
#      the two gates are independent readings of one population rather than two
#      spellings of one gate.
#   K36 is the instrument, not the tree: a report from a verifier built on a
#      gate jar too old to emit the `in=` field at all. Every bucket-scoped leg
#      then reads its `-` sentinel, which would be indistinguishable from a tree
#      that did nothing, so family C falls to 0.000000 -- C-5 is the only gate
#      still reading and, with every arrival leg at zero, it pays nothing --
#      and B-11 to its two set-scoped thirds (0.400000). This is the ladder's seventh trap
#      wearing family C's clothes, and the shape of the failure is what says the
#      gate noticed rather than scored.
#
# And the rehearsal proves it can fail before its clean runs are believed. It
# perturbs the golden's report in one place -- one collect record's `to=` moved
# from toCollection to toSet at the LinkedHashSet bucket -- asserts on the bytes
# that the edit landed, and requires the composed blocks to move familyC to
# exactly 0.983152 and the faithful count to 22. Four further meta-checks were
# run against the rehearsal itself during authoring, each producing a loud
# failure rather than a silent pass: a wrong table row, a renamed block banner,
# the measured-report guard relocated past C-5, and a fixture removed from disk.
# Its forty fixture directories are also required to be pairwise distinct by
# digest, so that two rows which had become byte-identical twins cannot look
# like forty assertions while being thirty-nine.
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

# The three baselines and the eight frozen set files this rung reads, checked
# against each other rather than against a number typed a second time here.
#
# Every threshold in family C, in B-10 and in B-11 comes out of
# baseline/source.txt and baseline/target.txt, and the scorer derives none of
# them from the tree it is grading. That is the right design and it moves the
# failure: a baseline with a wrong number in it now produces a confident wrong
# score instead of an error, and nothing else in the image would notice. So the
# files are required to agree with each other, which is a property no single
# typo can satisfy.
#
# baseline/source.txt AT THIS RUNG IS NOT THE PREVIOUS RUNG'S BASELINE. It is
# step 04's golden tree re-measured with the gate that ships HERE, and it had to
# be: step 04's SourceScan cannot see a retnull, an optret, an optop, an accum,
# a collect, an objects, a jtime or a millis record, because none of those
# record kinds existed below this step. Eight of the counters asserted below are
# therefore new measurements of an old tree, which is exactly the situation the
# ninth trap describes -- a step-local mean hiding an inherited figure that
# later requirements made unpassable -- and the assertions that relate them to
# target.txt are what make it visible if one of them drifted.
#
# THE LOAD-BEARING IDENTITY AT THIS RUNG IS THE DIAMOND PAIR, and it is the one
# figure that genuinely diverges between the two files: target.diamond is 74 and
# target.b10.diamond is 93. At steps 03 and 04 those were the same 93 counted
# from opposite ends. Here they cannot be, because 19 of the 93 diamonds are the
# SINK CONSTRUCTIONS of C-2's own buckets -- `List<X> xs = new ArrayList<>();`
# followed by a terminal forEach(xs::add) -- and `collect(Collectors.toList())`
# constructs nothing for a diamond to sit on. A candidate who does this step
# correctly therefore holds 74 diamonds and not 93, and a B-10 arrival half
# dividing by 93 would score the golden 0.796 on that third for doing the work
# the step asks for. So the two are asserted as a pair and as a difference: the
# 19 that go must be no more than the 23 sinks C-2 converts, and the departure
# denominator must stay at the tree's own 93.
#
# The other invariants are the things this rung must NOT move, each appearing in
# both files and required to be equal: the resource clauses (9), the manual
# close() count (0), the java.io floor (2), the non-anonymous explicit remainder
# (23), the anonymous functional residue (12), the @SuppressWarnings sites (44)
# and the raw-type count (0). A rung that moves one of them has either redone an
# earlier step's work or undone it, and B-10 and B-11 exist because the second
# is the likelier accident.
#
# THE FLOOR CLAIM, ASSERTED ON THE BASELINES rather than only through a fixture.
# The tree the candidate is handed already satisfies every requirement B-10 and
# B-11 state -- 93 diamonds, the 23 non-anonymous sites still explicit, 9
# resource clauses, no manual closes, java.io down to its 2 pinned sites, 60
# lambdas over 33 files, 24 annotated SAM interfaces, 24 stream buckets -- and
# NONE that family C states: no Optional-returning method anywhere (optret.size
# 0), no Optional operation (optop.size 0), no Collectors call (collect.size 0),
# no Objects delegation (objects.size 0) and no java.time reference (jtime.size
# 0), with the 42 null returns, 24 accumulate sites and 5 currentTimeMillis
# calls all still standing. Those are the two halves of the calibration table's
# `pinned` row -- familyC 0.0000 with B-10 AND B-11 at 1.0000 -- and that row is
# the floor argument at the foot of test.sh. It is the tightest floor on the
# whole ladder: 0.30*1 + 0.70*(0.25*1 + 0.75*0) = 0.475 against migration_prompt
# item 6's 0.5, a margin of 0.025. If any of the eight zeroes below stops being
# zero the floor moves, and it moves in the direction that breaks the item.
#
# The four NEW set files are not checked against a count here, they are DERIVED
# -- recomputed from source.txt's own per-site records and required to match:
#
#   optbuckets.txt   the <path>#<method> of the 16 PRIVATE null-returning
#                    lookup helpers, which must all be retnull buckets  (16/39)
#   sinks.txt        the 23 src/main accumulate buckets and the Collectors
#                    factory each one requires, which must all be accum
#                    buckets, leaving exactly one accum bucket behind   (23/24)
#   valuetypes.txt   the 3 classes that hand-roll equals+hashCode              
#   timesites.txt    the 4 millis buckets and the site count in each, which
#                    must be ALL of them and must sum to millis.size      (4, 5)
#
# The set files are what the gates are POINTED AT, and aim is the one property
# no count can check: a path that had drifted -- a file renamed, a record
# mis-parsed, a list hand-edited once -- would move a gate's target without
# moving any total, and every other check in this image would still pass. Round
# 1 of step 03's static review found exactly that in a hand-typed list of two
# paths. Here the sets are 16, 23, 3 and 4 entries over a tree of 322 files.
#
# optbuckets.txt is PRIVATE helpers and that is a constraint, not a description.
# B-6 scores the published API surface as an EXACT set of 1147 signatures, so
# widening a published method's return type from String to Optional<String> is
# charged exactly as hard as deleting it. retnull.api counts the null-returning
# methods that ARE on that surface -- 22 of the 42 -- and none of them may
# appear here. Asserted, because a set file that reached one would make C-1 and
# B-6 unsatisfiable together and no fixture in this image would notice: each
# gate would be correct on its own.
#
# valuetypes.txt is the one set with nothing to derive from. objects.size is 0
# at the baseline -- that is the point of the gate -- so there is no record to
# intersect it with. What is checked instead is that each line's path exists as
# a path in the tree's own records and that its basename is the class named in
# column 2, which is the drift a rename would cause.
#
# The FOUR INHERITED set files are step 04's, frozen: lambdafiles.txt,
# samifaces.txt and collectmethods.txt are B-11's aim, and streamfiles.txt is
# staged with them and deliberately NOT READ. That last one is asserted as
# unread rather than deleted, and it is a real check: B-11's third sub-gate asks
# its question per <path>#<method> BUCKET, and step 04's static review round 2
# found that the same question asked per FILE was satisfiable by one
# `xs.stream().count();` per file. A read of streamfiles.txt here would be that
# regression, spelled exactly as it was spelled the first time.
#
# baseline/nioids.txt names four recordings that must still exist in
# assets/ids.txt. It is read by family A as four of 935 ids and by B-10 as the
# proviso that halves its NIO third, so an id renamed out of the recording set
# would quietly stop protecting anything in both places at once.
RUN set -eux; \
    test "$(grep -c '^major=' /tests/baseline/level.txt)" = 1; \
    grep -qxF 'major=52' /tests/baseline/level.txt; \
    bsrc=/tests/baseline/source.txt; \
    btgt=/tests/baseline/target.txt; \
    anf=$(sed -n 's/^anonymousfunctional\.size=//p'        ${bsrc} | head -1); \
    con=$(sed -n 's/^anonymousfunctional\.convertible=//p' ${bsrc} | head -1); \
    lam=$(sed -n 's/^lambda\.size=//p'                     ${bsrc} | head -1); \
    lamf=$(sed -n 's/^lambda\.files=//p'                   ${bsrc} | head -1); \
    mrf=$(sed -n 's/^methodref\.size=//p'                  ${bsrc} | head -1); \
    fec=$(sed -n 's/^foreach\.collect=//p'                 ${bsrc} | head -1); \
    stp=$(sed -n 's/^stream\.producers=//p'                ${bsrc} | head -1); \
    sopm=$(sed -n 's/^stream\.opmethods=//p'               ${bsrc} | head -1); \
    blop=$(sed -n 's/^loop\.size=//p'                      ${bsrc} | head -1); \
    idc=$(sed -n 's/^iface\.decl=//p'                      ${bsrc} | head -1); \
    ism=$(sed -n 's/^iface\.sam=//p'                       ${bsrc} | head -1); \
    isa=$(sed -n 's/^iface\.samannotated=//p'              ${bsrc} | head -1); \
    exp=$(sed -n 's/^newgeneric\.explicit=//p'             ${bsrc} | head -1); \
    ean=$(sed -n 's/^newgeneric\.explicitanon=//p'         ${bsrc} | head -1); \
    dia=$(sed -n 's/^newgeneric\.diamond=//p'              ${bsrc} | head -1); \
    raw=$(sed -n 's/^newgeneric\.raw=//p'                  ${bsrc} | head -1); \
    res=$(sed -n 's/^try\.resources=//p'                   ${bsrc} | head -1); \
    clo=$(sed -n 's/^close\.manual=//p'                    ${bsrc} | head -1); \
    ion=$(sed -n 's/^iofile\.new=//p'                      ${bsrc} | head -1); \
    iod=$(sed -n 's/^iofile\.decl=//p'                     ${bsrc} | head -1); \
    ref=$(sed -n 's/^niofile\.ref=//p'                     ${bsrc} | head -1); \
    srf=$(sed -n 's/^niofile\.staticref=//p'               ${bsrc} | head -1); \
    sup=$(sed -n 's/^suppress\.sites=//p'                  ${bsrc} | head -1); \
    rn=$(sed -n  's/^retnull\.size=//p'                    ${bsrc} | head -1); \
    rnm=$(sed -n 's/^retnull\.methods=//p'                 ${bsrc} | head -1); \
    rna=$(sed -n 's/^retnull\.api=//p'                     ${bsrc} | head -1); \
    ort=$(sed -n 's/^optret\.size=//p'                     ${bsrc} | head -1); \
    oop=$(sed -n 's/^optop\.size=//p'                      ${bsrc} | head -1); \
    oog=$(sed -n 's/^optop\.get=//p'                       ${bsrc} | head -1); \
    acc=$(sed -n 's/^accum\.size=//p'                      ${bsrc} | head -1); \
    accm=$(sed -n 's/^accum\.methods=//p'                  ${bsrc} | head -1); \
    col=$(sed -n  's/^collect\.size=//p'                   ${bsrc} | head -1); \
    obj=$(sed -n  's/^objects\.size=//p'                   ${bsrc} | head -1); \
    jt=$(sed -n   's/^jtime\.size=//p'                     ${bsrc} | head -1); \
    mil=$(sed -n  's/^millis\.size=//p'                    ${bsrc} | head -1); \
    milm=$(sed -n 's/^millis\.methods=//p'                 ${bsrc} | head -1); \
    tanf=$(sed -n 's/^target\.anonfn=//p'            ${btgt} | head -1); \
    tdia=$(sed -n 's/^target\.diamond=//p'           ${btgt} | head -1); \
    tnon=$(sed -n 's/^target\.explicit\.nonanon=//p' ${btgt} | head -1); \
    tres=$(sed -n 's/^target\.try\.resources=//p'    ${btgt} | head -1); \
    tio=$(sed -n  's/^target\.iofile=//p'            ${btgt} | head -1); \
    bdia=$(sed -n 's/^target\.b10\.diamond=//p'      ${btgt} | head -1); \
    bclo=$(sed -n 's/^target\.b10\.close=//p'        ${btgt} | head -1); \
    bnio=$(sed -n 's/^target\.b10\.nio=//p'          ${btgt} | head -1); \
    elf=$(sed -n  's/^target\.b11\.lambdafiles=//p'  ${btgt} | head -1); \
    esam=$(sed -n 's/^target\.b11\.sam=//p'          ${btgt} | head -1); \
    ecm=$(sed -n  's/^target\.b11\.collectmethods=//p' ${btgt} | head -1); \
    topt=$(sed -n 's/^target\.optbuckets=//p'        ${btgt} | head -1); \
    tsnk=$(sed -n 's/^target\.sinks=//p'             ${btgt} | head -1); \
    tvt=$(sed -n  's/^target\.valuetypes=//p'        ${btgt} | head -1); \
    tts=$(sed -n  's/^target\.timesites=//p'         ${btgt} | head -1); \
    f5o=$(sed -n  's/^target\.c5\.optional\.floor=//p'   ${btgt} | head -1); \
    f5c=$(sed -n  's/^target\.c5\.collectors\.floor=//p' ${btgt} | head -1); \
    f5j=$(sed -n  's/^target\.c5\.objects\.floor=//p'    ${btgt} | head -1); \
    f5t=$(sed -n  's/^target\.c5\.jtime\.floor=//p'      ${btgt} | head -1); \
    # What the input tree HOLDS, and must go on holding: steps 03 and 04's work. \
    test "${dia}" = 93; \
    test "${exp}" = 36; \
    test "${ean}" = 13; \
    test "${res}" = 9; \
    test "${clo}" = 0; \
    test "${raw}" = 0; \
    test "${sup}" = 44; \
    test "${lam}" = 60; \
    test "${lamf}" = 33; \
    test "${mrf}" = 24; \
    test "${idc}" = 65; \
    test "${ism}" = 24; \
    test "${isa}" = 24; \
    test "${stp}" = 24; \
    test "${sopm}" = 24; \
    test "${anf}" = 12; \
    test "${blop}" = 115; \
    # ...and what it has NOT done, which is the whole of family C. Eight zeroes, \
    # and the floor is 0.475 against a 0.5 ceiling only while all eight hold. \
    test "${ort}" = 0; \
    test "${oop}" = 0; \
    test "${oog}" = 0; \
    test "${col}" = 0; \
    test "${obj}" = 0; \
    test "${jt}"  = 0; \
    test "${con}" = 0; \
    test "${fec}" = 0; \
    # The populations family C works on, present and non-trivial at the input. \
    test "${rn}"  = 42; \
    test "${rnm}" = 39; \
    test "${rna}" = 22; \
    test "${acc}" = 24; \
    test "${accm}" = 24; \
    test "${mil}" = 5; \
    test "${milm}" = 4; \
    # The two files agree about the things that must not move. \
    test "$(( exp - ean ))" = "${tnon}"; \
    test "${tnon}" = 23; \
    test "${anf}" = "${tanf}"; \
    test "${res}" = "${tres}"; \
    test "$(( ion + iod ))" = "${tio}"; \
    test "${tio}" = 2; \
    test "${ism}" = "${esam}"; \
    test "${isa}" = "${esam}"; \
    test "${sopm}" = "${ecm}"; \
    test "${elf}" = 20; \
    test "${elf}" -lt "${lamf}"; \
    # The diamond pair, which is the one figure that legitimately diverges. \
    test "${dia}" = "${bdia}"; \
    test "${tdia}" = 74; \
    test "${tdia}" -lt "${bdia}"; \
    test "$(( bdia - tdia ))" -le "${tsnk}"; \
    # B-10's other two denominators, related to the tree rather than restated. \
    test "${bclo}" = 11; \
    test "${bclo}" -gt "${clo}"; \
    test "${bclo}" -ge "${tres}"; \
    test "${bnio}" = 11; \
    test "${bnio}" -gt 0; \
    test "${bnio}" -le "$(( ref + srf ))"; \
    # C-5's four floors: real, and WELL below the golden's measured reach of \
    # 10/13/3/4, which is what makes the gate a floor rather than a transcript \
    # of one tree's spelling. Each is also at least 1, or the leg would be \
    # satisfied by a tree that published nothing at all. \
    test "${f5o}" = 5; test "${f5c}" = 6; test "${f5j}" = 2; test "${f5t}" = 2; \
    for f in "${f5o}" "${f5c}" "${f5j}" "${f5t}"; do test "${f}" -ge 1; done; \
    test "${f5o}" -lt 10; test "${f5c}" -lt 13; test "${f5j}" -lt 3; test "${f5t}" -lt 4; \
    # The per-site records are the instrument the set files are derived from, so \
    # they have to BE per-site: every record of every kind the gates bucket on \
    # must carry an in= field, or its bucket is the `-` sentinel and no \
    # candidate can ever fill it. This is the seventh trap applied to a \
    # baseline: assert the bytes, not the intention. \
    for kind in retnull accum millis stream streamop; do \
        n=$(sed -n "s/^${kind}\[[0-9]*\]=//p" ${bsrc} | awk '$0 !~ / in=/ { n++ } END { print n + 0 }'); \
        if [ "${n}" != 0 ]; then \
            echo "baseline/source.txt: ${n} ${kind} record(s) carry no in= field" >&2; exit 1; \
        fi; \
    done; \
    test "$(sed -n 's/^retnull\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${rn}"; \
    test "$(sed -n 's/^accum\[[0-9]*\]=//p'   ${bsrc} | grep -c .)" = "${acc}"; \
    test "$(sed -n 's/^millis\[[0-9]*\]=//p'  ${bsrc} | grep -c .)" = "${mil}"; \
    for kind in optret optop collect objects jtime; do \
        test "$(sed -n "s/^${kind}\[[0-9]*\]=//p" ${bsrc} | grep -c .)" = 0; \
    done; \
    # bucketise <kind> -- the <path>#<method> keys of one record kind, exactly \
    # as test.sh's buckets() builds them: field 1 up to the colon, joined to the \
    # LAST in= on the line. Written once here and used for all four sets. \
    bucketise() { \
        sed -n "s/^$1\[[0-9]*\]=//p" ${bsrc} \
          | awk '{ split($1, a, ":"); m = ""; \
                   for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                   if (m != "") print a[1] "#" m }' \
          | LC_ALL=C sort -u; \
    }; \
    bucketise retnull > /tmp/b-retnull.txt; \
    bucketise accum   > /tmp/b-accum.txt; \
    bucketise millis  > /tmp/b-millis.txt; \
    bucketise stream  > /tmp/b-stream.txt; \
    bucketise streamop > /tmp/b-streamop.txt; \
    for s in optbuckets sinks valuetypes timesites; do \
        grep -v '^#' "/tests/baseline/${s}.txt" | grep . > "/tmp/set-${s}.txt"; \
        awk '{print $1}' "/tmp/set-${s}.txt" | LC_ALL=C sort -u > "/tmp/key-${s}.txt"; \
    done; \
    test "$(grep -c . /tmp/set-optbuckets.txt)" = "${topt}"; \
    test "$(grep -c . /tmp/set-sinks.txt)"      = "${tsnk}"; \
    test "$(grep -c . /tmp/set-valuetypes.txt)" = "${tvt}"; \
    test "$(grep -c . /tmp/set-timesites.txt)"  = "${tts}"; \
    test "$(grep -c . /tmp/key-optbuckets.txt)" = "${topt}"; \
    test "$(grep -c . /tmp/key-sinks.txt)"      = "${tsnk}"; \
    test "$(grep -c . /tmp/key-timesites.txt)"  = "${tts}"; \
    test "${topt}" = 16; test "${tsnk}" = 23; test "${tvt}" = 3; test "${tts}" = 4; \
    # C-1's 16 are retnull buckets, a proper subset of the 39. \
    if [ "$(LC_ALL=C comm -23 /tmp/key-optbuckets.txt /tmp/b-retnull.txt | grep -c .)" != 0 ]; then \
        echo "baseline/optbuckets.txt names a bucket with no retnull record in source.txt:" >&2; \
        LC_ALL=C comm -23 /tmp/key-optbuckets.txt /tmp/b-retnull.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/b-retnull.txt)" = "${rnm}"; \
    test "${topt}" -lt "${rnm}"; \
    # ...and none of them is on the published surface B-6 scores as an exact \
    # set. The 22 API null-returners are named by the `api=yes` field of their \
    # own records; a set file that reached one would put C-1 and B-6 in direct \
    # contradiction. \
    sed -n 's/^retnull\[[0-9]*\]=//p' ${bsrc} \
      | awk '$0 ~ / api=yes/ { split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/b-retnull-api.txt; \
    if [ "$(LC_ALL=C comm -12 /tmp/key-optbuckets.txt /tmp/b-retnull-api.txt | grep -c .)" != 0 ]; then \
        echo "baseline/optbuckets.txt names a PUBLISHED null-returning method; B-6 would charge the widening:" >&2; \
        LC_ALL=C comm -12 /tmp/key-optbuckets.txt /tmp/b-retnull-api.txt >&2; exit 1; \
    fi; \
    # C-2's 23 are accum buckets, and exactly one accum bucket is left over -- \
    # the src/test site this step does not ask about. Stated as a difference so \
    # that a set file which quietly swallowed it would fail here. \
    if [ "$(LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/b-accum.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sinks.txt names a bucket with no accum record in source.txt:" >&2; \
        LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/b-accum.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/b-accum.txt)" = "$(( tsnk + 1 ))"; \
    # Three columns, and column 2 is a factory this step's acceptance rule knows. \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* (toList|toSet|toCollection) [A-Za-z][A-Za-z0-9_]*$' \
        /tmp/set-sinks.txt; then \
        echo "baseline/sinks.txt: a line is not <bucket> <factory> <constructed type>" >&2; exit 1; \
    fi; \
    test "$(awk '$2 == "toList" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 18; \
    test "$(awk '$2 == "toSet" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 4; \
    test "$(awk '$2 == "toCollection" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 1; \
    # The one toCollection site is the LinkedHashSet, and it is the reason this \
    # leg is a measurement rather than a spelling check: toSet would compile, \
    # pass every other gate, and change the iteration order family A replays. \
    grep -qE '^[^ ]*DynamicServiceLoader\.java#createInstances toCollection LinkedHashSet$' /tmp/set-sinks.txt; \
    # C-4's 4 are ALL the millis buckets, not a subset, and column 2 sums to the \
    # site count -- so a candidate who converts one of ServletMethodExecutor's \
    # two calls is seen to have converted one of two. \
    if ! cmp -s /tmp/key-timesites.txt /tmp/b-millis.txt; then \
        echo "baseline/timesites.txt is not exactly the millis buckets in source.txt:" >&2; \
        diff /tmp/key-timesites.txt /tmp/b-millis.txt >&2 || true; exit 1; \
    fi; \
    test "$(awk '{ s += $2 } END { print s + 0 }' /tmp/set-timesites.txt)" = "${mil}"; \
    test "${tts}" = "${milm}"; \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* [1-9][0-9]*$' \
        /tmp/set-timesites.txt; then \
        echo "baseline/timesites.txt: a line is not <bucket> <site count>" >&2; exit 1; \
    fi; \
    # C-3's 3 have NO record to intersect, and that is not an oversight in the \
    # checking -- it is the gate's premise. objects.size is 0 at the baseline, \
    # and a class whose only distinguishing feature is a hand-rolled \
    # equals/hashCode pair leaves no trace in any record kind SourceScan emits, \
    # so there is nothing here to derive the set FROM. What is checked instead \
    # is the pair of things a drifted line would break: the basename is the \
    # class column 2 names, and both members are on the published surface. \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java [A-Z][A-Za-z0-9_]*$' \
        /tmp/set-valuetypes.txt; then \
        echo "baseline/valuetypes.txt: a line is not a (path, class name) pair" >&2; exit 1; \
    fi; \
    while read -r pth cls; do \
        if [ "$(basename "${pth}")" != "${cls}.java" ]; then \
            echo "baseline/valuetypes.txt: ${pth} does not declare ${cls}" >&2; exit 1; \
        fi; \
    done < /tmp/set-valuetypes.txt; \
    # ...and the surface constraint, which is what makes C-3's rewrite a \
    # REWRITE rather than a redesign: all three classes publish both members, \
    # so `public boolean equals(Object)` and `public int hashCode()` are two of \
    # B-6's 1147 frozen signatures apiece. Objects.equals/Objects.hash go \
    # inside the bodies and the signatures do not move. A set file that had \
    # drifted onto a class which does NOT publish them would be asking for a \
    # change B-6 cannot see and family A cannot corroborate. \
    fqcn() { \
        sed -e 's#^.*/src/\(main\|test\)/java/##' -e 's#\.java$##' -e 's#/#.#g'; \
    }; \
    awk '{print $1}' /tmp/set-valuetypes.txt | fqcn | while read -r c; do \
        for m in 'public boolean equals(java.lang.Object);' 'public int hashCode();'; do \
            if ! grep -qF " ${c} :: ${m}" /tests/baseline/api.txt; then \
                echo "baseline/valuetypes.txt: ${c} does not publish ${m}" >&2; exit 1; \
            fi; \
        done; \
    done; \
    # EVERY PATH IN EVERY SET FILE RESOLVES TO A CLASS THIS TREE COMPILES. \
    # This is the check the regexes above cannot make and the counts cannot \
    # make either: a path that had drifted -- a file renamed upstream, a record \
    # mis-parsed, a list hand-edited once -- still LOOKS like a source path and \
    # still counts as one line, and it would move a gate's aim without moving \
    # any total in this image. baseline/classnames.txt is the 322 top-level \
    # FQCNs the inherited sources compile to, frozen at step 01 and already \
    # depended on by B-4 and B-5, so this ties all seven set files to a \
    # population three other gates would fail on first. \
    test "$(grep -c . /tests/baseline/classnames.txt)" = "$(sed -n 's/^files=//p' ${bsrc} | head -1)"; \
    { sed 's/#.*//' /tmp/key-optbuckets.txt; \
      sed 's/#.*//' /tmp/key-sinks.txt; \
      sed 's/#.*//' /tmp/key-timesites.txt; \
      awk '{print $1}' /tmp/set-valuetypes.txt; \
      cat /tmp/set-lambdafiles.txt; \
      awk '{print $1}' /tmp/set-samifaces.txt; \
      sed 's/#.*//' /tmp/set-collectmethods.txt; } \
      | LC_ALL=C sort -u | fqcn | LC_ALL=C sort -u > /tmp/set-fqcn.txt; \
    LC_ALL=C sort -u /tests/baseline/classnames.txt > /tmp/b-classnames.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/set-fqcn.txt /tmp/b-classnames.txt | grep -c .)" != 0 ]; then \
        echo "a baseline set file names a path that compiles to no class in classnames.txt:" >&2; \
        LC_ALL=C comm -23 /tmp/set-fqcn.txt /tmp/b-classnames.txt >&2; exit 1; \
    fi; \
    # B-11's three inherited sets, which are step 04's frozen aim. They cannot \
    # be re-derived from THIS baseline -- it is step 04's golden, where \
    # convertible and foreach.collect have already fallen to 0 -- so what is \
    # checked is that each still POINTS at something this tree has: the 20 \
    # lambda files are among the 33 files holding lambda records, the 24 SAM \
    # interfaces are exactly the abstract=1 interfacedecl records, and the 24 \
    # collect buckets are exactly the stream and streamop buckets. \
    grep '^[a-z]' /tests/baseline/lambdafiles.txt | LC_ALL=C sort -u > /tmp/set-lambdafiles.txt; \
    grep '^[a-z]' /tests/baseline/samifaces.txt   | LC_ALL=C sort -u > /tmp/set-samifaces.txt; \
    grep '^[a-z]' /tests/baseline/collectmethods.txt | LC_ALL=C sort -u > /tmp/set-collectmethods.txt; \
    test "$(grep -c . /tmp/set-lambdafiles.txt)"    = "${elf}"; \
    test "$(grep -c . /tmp/set-samifaces.txt)"      = "${esam}"; \
    test "$(grep -c . /tmp/set-collectmethods.txt)" = "${ecm}"; \
    sed -n 's/^lambda\[[0-9]*\]=//p' ${bsrc} | awk '{ split($1, a, ":"); print a[1] }' \
      | LC_ALL=C sort -u > /tmp/b-lampaths.txt; \
    test "$(grep -c . /tmp/b-lampaths.txt)" = "${lamf}"; \
    if [ "$(LC_ALL=C comm -23 /tmp/set-lambdafiles.txt /tmp/b-lampaths.txt | grep -c .)" != 0 ]; then \
        echo "baseline/lambdafiles.txt names a file holding no lambda in source.txt:" >&2; \
        LC_ALL=C comm -23 /tmp/set-lambdafiles.txt /tmp/b-lampaths.txt >&2; exit 1; \
    fi; \
    sed -n 's/^interfacedecl\[[0-9]*\]=//p' ${bsrc} \
      | awk '$3 == "abstract=1" { split($1, a, ":"); print a[1], $2 }' \
      | LC_ALL=C sort -u > /tmp/b-sam.txt; \
    if ! cmp -s /tmp/b-sam.txt /tmp/set-samifaces.txt; then \
        echo "baseline/samifaces.txt is not what source.txt's interfacedecl records say it is:" >&2; \
        diff /tmp/b-sam.txt /tmp/set-samifaces.txt >&2 || true; exit 1; \
    fi; \
    if ! cmp -s /tmp/b-streamop.txt /tmp/set-collectmethods.txt; then \
        echo "baseline/collectmethods.txt is not the streamop buckets in source.txt:" >&2; \
        diff /tmp/b-streamop.txt /tmp/set-collectmethods.txt >&2 || true; exit 1; \
    fi; \
    if ! cmp -s /tmp/b-stream.txt /tmp/set-collectmethods.txt; then \
        echo "baseline/collectmethods.txt is not the stream buckets in source.txt:" >&2; \
        diff /tmp/b-stream.txt /tmp/set-collectmethods.txt >&2 || true; exit 1; \
    fi; \
    # THE CO-EXISTENCE CLAIM, on the baselines: C-2's 23 buckets are a subset of \
    # B-11's 24, so the two gates really do read the same sites from opposite \
    # ends, and there is exactly one B-11 bucket C-2 does not touch. If this \
    # ever became a disjointness the step would be asking for two unrelated \
    # things and item 16's fixture pair would be rehearsing a coincidence. \
    if [ "$(LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/set-collectmethods.txt | grep -c .)" != 0 ]; then \
        echo "a C-2 sink bucket is not one of B-11's collect buckets:" >&2; \
        LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/set-collectmethods.txt >&2; exit 1; \
    fi; \
    test "$(( ecm - tsnk ))" = 1; \
    # Every path in every set file is a real source path in this repository's \
    # layout, which is the cheapest check that catches a hand-edit. \
    for k in optbuckets sinks timesites; do \
        if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]*$' "/tmp/key-${k}.txt"; then \
            echo "baseline/${k}.txt: a key is not a <src/main path>#<method> bucket" >&2; exit 1; \
        fi; \
        if grep -qF '#-' "/tmp/key-${k}.txt"; then \
            echo "baseline/${k}.txt: a bucket with no enclosing method can never be reached" >&2; exit 1; \
        fi; \
    done; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java$' /tmp/set-lambdafiles.txt; then \
        echo "baseline/lambdafiles.txt: a line is not a source path" >&2; exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]*$' \
        /tmp/set-collectmethods.txt; then \
        echo "baseline/collectmethods.txt: a line is not a <path>#<method> bucket" >&2; exit 1; \
    fi; \
    # streamfiles.txt is staged and must NOT be read: B-11's stream sub-gate is \
    # method-scoped, and the file-scoped form was step 04's review finding. \
    test -s /tests/baseline/streamfiles.txt; \
    if grep -qF 'baseline/streamfiles.txt' /tests/test.sh; then \
        echo "test.sh reads streamfiles.txt: B-11's stream question has gone back to file scope" >&2; \
        exit 1; \
    fi; \
    rm -f /tmp/b-retnull.txt /tmp/b-accum.txt /tmp/b-millis.txt /tmp/b-stream.txt \
          /tmp/b-streamop.txt /tmp/b-retnull-api.txt /tmp/b-lampaths.txt /tmp/b-sam.txt \
          /tmp/set-optbuckets.txt /tmp/set-sinks.txt /tmp/set-valuetypes.txt \
          /tmp/set-timesites.txt /tmp/set-lambdafiles.txt /tmp/set-samifaces.txt \
          /tmp/set-collectmethods.txt /tmp/key-optbuckets.txt /tmp/key-sinks.txt \
          /tmp/key-valuetypes.txt /tmp/key-timesites.txt /tmp/set-fqcn.txt \
          /tmp/b-classnames.txt; \
    test "$(grep -c '^[a-z]' /tests/baseline/nioids.txt)" = 4; \
    grep '^[a-z]' /tests/baseline/nioids.txt | while read -r id; do \
        if ! grep -qxF "${id}" /tests/assets/ids.txt; then \
            echo "baseline/nioids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; \
            exit 1; \
        fi; \
    done; \
    echo "step 05: the source, target, level, nioids and eight set-file baselines agree with each other"

# Family C's instrument, rehearsed on a real tree.
#
# fixtures/c-rehearsal.sh proves the arithmetic over SourceScan's counters is
# the arithmetic that was calibrated; it cannot prove the counters, because it
# feeds the blocks hand-written reports. This is the other half. fixtures/smoke
# is compiled here by this image's own javac with the plugin attached, and every
# count is asserted separately against a figure that was measured first and
# frozen afterwards -- not predicted from reading SourceScan.java, which is the
# way a rehearsal ends up asserting the bug.
#
# TWO FILES AND NOT ONE, which is the first way this rung's version differs from
# step 04's. Every gate at this step scores over a frozen set of <file>#<method>
# BUCKETS rather than over a count, and on a one-file tree the file half of
# every key is constant: a gate that dropped the path entirely would read
# exactly the same. `retnull.files`, `optret.files` and `jtime.files` would read
# 1 whatever distinctFiles() did. So smoke/ is Smoke.java plus Lib.java, and
# Lib.java also carries the near misses that need a NAME without an ELEMENT --
# a class of its own called Optional, another called Objects, a static method
# called toList -- which is the whole of what "resolved through javac's
# elements" buys and cannot be said in the file where the real imports are.
#
# EVERY BLOCK IS ASSERTED TWICE, which is the second. Step 04's gates could lean
# on totals, because `foreach.collect` falling from 24 to 0 is a fact about 24
# specific loops. Nothing here is like that: `retnull.size` falling by 16 is
# satisfied by deleting sixteen `return null;` statements and `optret.size`
# rising by 16 is satisfied by sixteen new methods in a file of one's own. So
# each of the eight record kinds this rung introduces -- retnull, optret, optop,
# accum, collect, objects, jtime, millis -- is checked once as a total and once
# as the specific records that make the total mean what the gate reads it as:
# the path, the enclosing method, and the discriminating field (`api=`, `to=`,
# `scope=`, `mods=`) that puts the record in the right bucket with the right
# answer.
#
# THE FLATTEN CONTROL IS A CROSS-RUNG CONTROL HERE. Smoke.java is step 04's
# Smoke.java with a section appended, verbatim, so cutting at the step-05 marker
# and dropping Lib.java must reproduce step 04's OWN frozen figures -- twenty-six
# of them, measured by a different rung's rehearsal on a different day, and
# asserted here rather than re-derived. A counter that drifted between the two
# rungs is then a disagreement between two measurements rather than two
# plausible numbers. The same control requires all twenty-four of this step's
# new keys to read 0 on the flattened tree: not "lower", zero, because every
# construct that moves one of them is in the half that was cut or in the file
# that was not copied, and a key wired to a constant reads the full-tree figure
# there.
#
# The -source 1.7 probe still runs and is still asserted -- four errors, every
# one of them about the language level, and the same probe required to say `yes`
# when the file is cut back to its step-03 half so that it cannot be a probe
# that answers "not supported" for every tree it is handed. But at THIS rung
# `gate.src.ok17` is published as a FACT and is a term in no gate. Step 04's C-4
# asked whether the tree requires Java 8 and B-4 asks the compiled form of that
# question at full weight; charging it again here would pay twice for one
# flip, and this step's must-flip is C-5 rather than the class-file version.
# The probe is rehearsed anyway, because a wrong fact in facts.txt is a fact the
# trajectory analysis will reason from.
#
# The classpath expression and srcgate() are lifted out of /tests/test.sh by
# anchor rather than copied, so what runs here is what the scorer runs. A shelf
# change that shortens the scorer's compile classpath fails this layer -- which
# is measured, not hoped for: the check was run against a deliberately shortened
# SRCCP and reported 76 jars where 77 were staged.
#
# It also asserts three absences that would each be a leak rather than a bug:
# arqplan.jar and Plan.java name every site the golden edits, and arqgate.Rewrite
# is the golden solution's own editor. None may ever be staged into a verifier,
# and this is the one place in the image that would notice if staging grew a
# line.
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
    # The twelve frozen baseline files this rung stages, each read from /tests \
    # and not from the tree. bv() and tv() name their paths as literals for \
    # exactly this. \
    # \
    # Eleven of the twelve are read. streamfiles.txt is staged and deliberately \
    # is not, and the baseline-agreement layer above asserts that absence as a \
    # positive claim rather than leaving it to be noticed: B-11's stream third \
    # is method-scoped on purpose, and a file-scoped set would answer a weaker \
    # question about the same tree. \
    grep -qF "bv() { sed -n \"s/^\$1=//p\" /tests/baseline/source.txt | head -1; }" /tests/test.sh; \
    grep -qF "tv() { sed -n \"s/^\$1=//p\" /tests/baseline/target.txt | head -1; }" /tests/test.sh; \
    grep -qF '/tests/baseline/nioids.txt' /tests/test.sh; \
    grep -qF 'sed -n '"'"'s/^major=//p'"'"' /tests/baseline/level.txt' /tests/test.sh; \
    # Family C's own four set files, each named by the gate that divides by it. \
    # Every one of these gates could be rewritten to divide by the matching \
    # target.txt scalar instead -- tgt_optbuckets, tgt_sinks and tgt_timesites \
    # are all in scope at the point of use -- and the result would still be a \
    # number in [0,1] that moves in the right direction. It would also be a \
    # count, and a count is what a tree that edited sixteen OTHER methods \
    # satisfies. The set is the whole difference between the two readings, so \
    # the read of it is asserted and not assumed. \
    grep -qF 'wantbuckets /tests/baseline/optbuckets.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/sinks.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/timesites.txt' /tests/test.sh; \
    grep -qF '/tests/baseline/sinks.txt "${WORK}/collect-to.txt"' /tests/test.sh; \
    grep -qF '/tests/baseline/valuetypes.txt "${WORK}/objects-in.txt"' /tests/test.sh; \
    grep -qF "grep -v '^#' /tests/baseline/valuetypes.txt | grep -c ." /tests/test.sh; \
    # B-11's three inherited sets, read at the scopes step 04 froze them at -- \
    # lambdafiles by FILE, samifaces by class, collectmethods by METHOD. The \
    # scope is carried in the helper's name, so asserting the call asserts the \
    # scope: covered() takes a file set and coveredmethods() takes a method set, \
    # and swapping them is a compile error's worth of obvious only because the \
    # two helpers exist separately. \
    grep -qF 'covered /tests/baseline/lambdafiles.txt lambda methodref' /tests/test.sh; \
    grep -qF 'coveredmethods /tests/baseline/collectmethods.txt stream streamop' /tests/test.sh; \
    grep -qF '/tests/baseline/samifaces.txt' /tests/test.sh; \
    # C-2's acceptance rule, which is load-bearing on the reference migration: 7 \
    # of the 23 recorded sinks are collected with toCollection, and a reading \
    # that demanded the exact factory named in sinks.txt would score the golden \
    # 16/23 and call the difference a defect in the tree. \
    # \
    # The asymmetry is the point and is asserted as one expression rather than \
    # as two greps for the two collector names: a bucket whose recorded want IS \
    # toCollection accepts nothing else, because that one is the LinkedHashSet \
    # site in DynamicServiceLoader#createInstances, and an ordinary toSet there \
    # changes the iteration order its caller reads the services in. \
    grep -qF 'if (!ok && (w == "toList" || w == "toSet")) ok = ((b SUBSEP "toCollection") in got)' /tests/test.sh; \
    # C-1 and C-2 are minima and not means, asserted at the head of each \
    # expression because a mean of the same legs pays half a gate for half a \
    # migration done in the wrong half. C-1 takes a third leg -- the share of \
    # Optional uses that are NOT get() -- because a tree that returns Optional \
    # and calls .get() at every call site has moved the null and not removed it, \
    # and both count-halves of this gate read 1.000000 on that tree. \
    grep -qF 'c1=$(minf "$(minf "$(clamp01 "$(frac "${opt_dep}" "${tgt_optbuckets:-0}")")"' /tests/test.sh; \
    grep -qF 'c2=$(minf "$(clamp01 "$(frac "${sink_dep}" "${tgt_sinks:-0}")")"' /tests/test.sh; \
    # C-5, this rung's must-flip, reads the PUBLISHED jars with a second \
    # instrument: the scorer's own unzip into the scorer's own scratch \
    # directory, then a binary grep for the constant-pool name. Four legs, all \
    # asserted -- the population it reads, the instrument it reads it with, the \
    # four package names, and the four floors it divides by. \
    # \
    # The floors come from target.txt through tv() and are not literals here, \
    # because a floor written into test.sh is a floor that cannot be re-measured \
    # when the reference migration moves. That they are FLOORS well below the \
    # golden's reach, rather than the golden's exact counts, is the baseline \
    # layer's claim and is checked there against the measured figures. \
    grep -qF 'done < "${WORK}/modulejars.txt"' /tests/test.sh; \
    grep -qF 'unzip -qo "${j}" '"'"'*.class'"'"' -d "${cpdir}/m${njar}"' /tests/test.sh; \
    grep -qF 'xargs -0 -r grep -lZ --binary-files=binary -F "$1"' /tests/test.sh; \
    for pkg in 'java/util/Optional' 'java/util/stream/Collectors' \
               'java/util/Objects' 'java/time/'; do \
        grep -qF "cpcount '${pkg}'" /tests/test.sh; \
    done; \
    test "$(grep -cE '^c5_(opt|col|obj|jt)_floor=\$\(tv ' /tests/test.sh)" = 4; \
    test "$(grep -cE 'target..c5..(optional|collectors|objects|jtime)..floor' /tests/test.sh)" = 4; \
    # ...and each of those four legs is MULTIPLIED by the source leg it \
    # corroborates before the mean is taken, which is the one line static review \
    # round 1 (R5) was about. A C-5 that paid on its own reach paid for a \
    # constant pool, and a constant pool is fifteen dummy references away from \
    # any tree at all: the step-04 input plus those references scored 0.553750, \
    # over migration_prompt item 6's 0.5, with nothing migrated. Scaled, the \
    # same tree scores 0.475000 -- the floor exactly. \
    # \
    # Anchored on the bytes of the expression and on the four pairings by name, \
    # because the failure this forecloses is not a missing gate, it is a `c5=` \
    # that quietly goes back to averaging four unscaled legs and still prints \
    # four legs, still publishes the four gate.cp counts, and still reads \
    # 1.000000 on the golden -- where every source leg is 1.0 and the scaling is \
    # invisible. K37 and K38 in the family-C rehearsal are the behavioural half \
    # of this: one fails if the scaling goes, the other if the floors do. \
    grep -qF 'BEGIN { printf "%.6f", (a * p + b * q + c * r + d * s) / 4 }' /tests/test.sh; \
    grep -qF ' -v p="${c1:-0}" -v q="${c2:-0}" -v r="${c3:-0}" -v s="${c4:-0}"' /tests/test.sh; \
    grep -qF "printf '    corroborated: %s*%s %s*%s %s*%s %s*%s -> %s" /tests/test.sh; \
    if grep -qF 'BEGIN { printf "%.6f", (a + b + c + d) / 4 }' /tests/test.sh; then \
        echo "C-5 averages its four legs unscaled again -- see R5" >&2; exit 1; \
    fi; \
    # ...and C-5 runs OUTSIDE the `if [ -z "${measured}" ]` guard the other four \
    # sit inside. That is not a stylistic detail. Every gate in the guard is one \
    # instrument's opinion of a compile the SCORER ran over whatever sources it \
    # found; this one asks a different question of a different artifact, and a \
    # tree whose sources the gate compiler cannot digest still publishes jars, \
    # and those jars still either name java/util/Optional or do not. Inside the \
    # guard the must-flip would read 0 for a reason that has nothing to do with \
    # whether the library ships. \
    # \
    # Asserted by indentation, because indentation is exactly what the guard \
    # does to every line inside it: C-1 to C-4's banners are indented three and \
    # C-5's is not, and the penalty -- the last thing inside the guard -- is \
    # published before it. The order check below pins that last part. \
    test "$(grep -cE '^   # -+ C-[1-4]$' /tests/test.sh)" = 4; \
    grep -qxF '# ---------------------------------------------------------------- C-5' /tests/test.sh; \
    grep -qxF 'echo "--- C-5: the published jars reference the Java 8 library"' /tests/test.sh; \
    grep -qF 'p * (0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e)' /tests/test.sh; \
    grep -qF '0.20*l + 0.20*s + 0.60*c' /tests/test.sh; \
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
    # was re-weighted for this step because C-1 and C-2 carry the two legs an \
    # automated migrator cannot do, and family B grew an ELEVENTH gate, which \
    # moved the other ten -- B-10 fell from 0.12 to 0.11 to make room for B-11. \
    # B-10's row is listed here by name for that reason: a step that adds a gate \
    # and leaves the rows below it alone is the shape this whole check exists \
    # for, and B-10's is the row that moved. \
    grep -qF "printf '    B10 java 7 held     %s  x0.11" /tests/test.sh; \
    grep -qF "printf '    B11 java 8 lang held %s  x0.12" /tests/test.sh; \
    grep -qF "printf '    C1 optional             %s  x0.30" /tests/test.sh; \
    grep -qF "printf '    C2 collectors           %s  x0.35" /tests/test.sh; \
    grep -qF "printf '    C3 objects              %s  x0.10" /tests/test.sh; \
    grep -qF "printf '    C4 java.time            %s  x0.10" /tests/test.sh; \
    grep -qF "printf '    C5 ships the library    %s  x0.15" /tests/test.sh; \
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
    # asserting seven literals by hand closes one instance of the hole and leaves \
    # the class of it open -- family B's eleven printfs and family B's awk are the \
    # same pair of spellings, and they agree today only by habit. For each family \
    # this pulls the weights out of the report rows in order and out of that \
    # family's awk expression in order, and requires the two sequences to be \
    # equal. A family added later is covered by adding its letter to the list. \
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B passed ten gates at the rung below, and a one-digit \
    # pattern reads nine rows against eleven coefficients and calls the \
    # difference a mismatch -- or worse, would have silently skipped B10 and B11 \
    # on both sides if the coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    # \
    # The row pattern needs one space before %s and this rung's family B has a \
    # row whose label is a character longer than the column ("B11 java 8 lang \
    # held"), so the columns no longer line up and the pattern must not depend \
    # on them. It matches ` .*  x0.NN` and not a fixed column for exactly that. \
    for fam in B:11:'0\.12\*b11$' C:5:'0\.15\*e[)]$'; do \
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
    for g in C1.optional C2.collectors C3.objects C4.jtime C5.ships-java8-library \
             B8.jupiter-held B9.junit4-held B10.java7-held B11.java8lang-held; do \
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
    # And step 04's family C the same way, one rung fresher and therefore the \
    # one actually at risk: this file was COPIED from step 04's, so every name \
    # in this list was present in it an edit ago and a missed rename leaves a \
    # gate that reads the right number under the wrong question. \
    # \
    # `0.60*c` is deliberately NOT in this list even though it was step 04's C-3 \
    # weight, because B-11 -- which holds step 04's work at this rung -- \
    # renormalises step 04's own three weights and lands on 0.20 / 0.20 / 0.60. \
    # The string is step 04's, still means step 04's third, and is asserted \
    # PRESENT thirty lines above. `0.15*a` is banned and `0.15*e` is required, \
    # which is the same coefficient moved to a different gate: the ban list has \
    # to distinguish a weight from its position, and these two are the pair that \
    # shows it does. \
    for dead in 'C1.lambdas' 'C2.functionalinterface' 'C3.streams' \
                'C4.requires-java8' 'requires-java8' '0.15*a'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 04's retired family-C gate reads are still in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # The order of the whole scorer, asserted rather than assumed. Two of these \
    # are hoists carried up from the rung below: the three srcgate compiles sit \
    # ABOVE family B so that B-10 and B-11 can read their counters, and both \
    # held gates sit between family B's own gates and family C. Under `set -u` an \
    # un-hoist is not a wrong number, it is a dead verifier -- which is the good \
    # case. The bad case is a hoist that lands after the reads and leaves the \
    # variables at their earlier values, and only the line numbers can tell those \
    # apart. \
    # \
    # New at this rung: C-5 is anchored between the anti-gaming penalty and \
    # family C's score. That is the structural half of the claim made above in \
    # prose -- the penalty is the last statement inside the `measured` guard, so \
    # a C-5 that appears before it is a C-5 that has been moved inside, and the \
    # must-flip would then read 0 on every tree whose sources the gate compiler \
    # declined. K26 in the family-C rehearsal is the behavioural half of the same \
    # claim, and one without the other is an assertion about a fixture. \
    awk '/^# -+ the source AST, compiled once$/{s=NR} \
         /^# -+ family B$/{b=NR} /^# -+ B-10: step 03, held$/{t=NR} \
         /^# -+ B-11: step 04, held$/{u=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^ *fact "gate.src.penalty=/{pen=NR} /^# -+ C-5$/{c5=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} /^fact "B11.java8lang-held=/{fb11=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || u == 0 || c == 0 || fc == 0 \
                   || pen == 0 || c5 == 0 || v == 0 || b9 == 0 || fb10 == 0 \
                   || fb11 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " B11=" u " Cbanner=" c " C=" fc " penalty=" pen " C5=" c5 \
                        " B-7=" v " B-9=" b9 " B10fact=" fb10 " B11fact=" fb11 \
                        " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so B-10 and B-11 cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < u && u < fb11 && fb11 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, B-11, family C, its score, the report"; exit 1 } \
               if (!(c < pen && pen < c5 && c5 < fc)) { \
                  print "C-5 must sit after the anti-gaming penalty, which is the last line inside the measured guard"; exit 1 } \
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
    echo "step 05: B-6 reads the same public surface through an anonymous class, a lambda and a method reference"; \
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
    echo "step 05: B-6 reads the same interface surface at class-file 50 and at 52, and still charges a default method the author wrote"; \
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
    echo "step 05: arqgate.Junit5 reads assertThrows at the call site, anonymous Executable or lambda"; \
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
    echo "step 05 verifier image: assertions passed"

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
                  /tests/baseline/optbuckets.txt /tests/baseline/sinks.txt \
                  /tests/baseline/valuetypes.txt /tests/baseline/timesites.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/gates/build/arqsource.jar \
                  /tests/gates/source-gate.sh /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh \
                  /tests/fixtures/b89-rehearsal.sh /tests/fixtures/c/golden/report.txt \
                  /tests/fixtures/src-rehearsal.sh /tests/fixtures/pv-rehearsal.sh \
                  /tests/fixtures/mkfix05.py /tests/fixtures/c/pinned/report.txt \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Lib.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk8 /tests/baseline \
                  /tests/runner /tests/gates /tests/warm /tests/warm2 \
                  /tests/fixtures /tests/fixtures/cl /tests/fixtures/c \
                  /tests/fixtures/c/golden /tests/fixtures/c/golden/jars \
                  /tests/fixtures/c/pinned /tests/fixtures/smoke; do \
        if $SP ls "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can list ${secret}" >&2; exit 1; \
        fi; \
    done; \
    if $SP ls /tests > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests" >&2; exit 1; fi; \
    if $SP ls /tests/assets > /dev/null 2>&1; then echo "LEAK: 65534 can list /tests/assets" >&2; exit 1; fi; \
    echo "step 05: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
