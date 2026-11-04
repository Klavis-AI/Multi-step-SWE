# Verifier image for step 07, built with steps/07-java11-jakarta/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so everything grading needs is copied in here. Harbor skips its usual
# tests upload and runs /tests/test.sh from this image.
#
# THE RUNG IS "JAVA 11 + JAKARTA": the class-file level goes 54 -> 55, the
# annotation and EJB packages go javax -> jakarta in the two modules that can
# move, and ServletMethodExecutor's HttpURLConnection loop becomes java.net.http.
# Three flips, one era. javax.servlet is NOT one of the movers -- it is one of
# the six packages C-3 holds, against the 2011 Jetty this tree tests with.
#
# The class-file version is this step's must-flip and C-1 owns it, read by
# arqgate.Scan out of the jars the candidate's own build published -- an
# artifact, not a source text. B-4, which at steps 01 to 06 read that same
# number, is restated here as "one major, and not below 54" so that the flip
# is scored once rather than twice: 54 is what step 06 left behind and what a
# candidate who does nothing still has. `--release 11` is what emits 55, and a
# tree that reached 55 by any other route still passes, because the gate reads
# the artifact and not the build file.
#
# THE GRADING JDK DOES NOT MOVE AT THIS RUNG. Step 06 graded on JDK 11 and so
# does this one; what moves is the code era, 54 -> 55, and `--release 11` is
# expressible on the same compiler. The era directory names are code eras and
# not grading JDKs, which is the one thing about them that reads backwards:
# rec/jdk8-step06 and rec/jdk11-step07 were both replayed on this same JDK 11.
#
# The 935 transcripts under assets/rec are the 2011 probe shelf replayed
# against the candidate's published jars. Three of the 935 move at this rung,
# and for a reason inside this step's own work rather than inside its
# toolchain: C-5 replaces ServletMethodExecutor's HttpURLConnection loop with
# java.net.http.HttpClient, and HttpClient refuses a `file:` URL with
# IllegalArgumentException("invalid URI scheme file") where the hand-written
# loop threw IllegalStateException("Not an http connection! ..."), while
# HttpResponse has no getResponseMessage() so the two "Got <code>" messages
# lose their reason phrase. The era staged here is jdk11-step07 --
# rec/jdk8-step06 with exactly those three files replaced, asserted as an
# equality by rebuild-verifier.sh rather than left as a hope.
#
# JDK 11 is also the right compiler for what this rung READS. Every construct
# gate in family C reads a TYPED AST: `Optional` in a javadoc and `Optional`
# in a return type differ by nothing a grep can see, `var` is not a node in
# the AST at all but a type tree at NOPOS, and `isAccessible()` on an
# AccessibleObject is a different finding from a method of that name on
# something else. arqgate.SourceScan resolves every site through javac's own
# Elements, against a classpath the scorer owns, at the level the step
# targets. A JDK that could not express the level would be answering a
# different question with the same counter names.
#
# `unzip` is asserted present below rather than assumed, and B-12e is why. That
# gate reads the constant pools of the jars the candidate's build published --
# a different instrument on a different artifact from everything else in the
# file -- and a missing unzip makes it report "the published jars carry no
# class files", which reads as a finding about the tree rather than as a broken
# image. That would be the ladder's sixth trap sprung in the one place where
# nothing else could corroborate it.
#
# What this rung does NOT carry forward is step 05's own source.txt and
# target.txt. Every rung below this one staged its predecessor's pair beside
# its own; here they would both land as baseline/source.txt and
# baseline/target.txt and which one a gate read would come down to the order of
# a staging loop. What step 05 needs carried forward is carried instead as
# target.b12.* keys inside step 06's target.txt, and its four population files
# are staged under their own names.
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
FROM eclipse-temurin:11-jdk

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
# Gradle 9 needs Java 17+ to run and this image has only JDK 11 -- the same
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
# under per-step names (fixtures-07/, baseline-07/, assets/STAGED-07.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-07; \
    test -d /tests/baseline-07; \
    test -f /tests/assets/STAGED-07.txt; \
    mv /tests/fixtures-07 /tests/fixtures; \
    mv /tests/baseline-07 /tests/baseline; \
    mv /tests/assets/STAGED-07.txt /tests/assets/STAGED.txt
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
RUN echo step07 > /tests/STEP_ID

# The bytecode gate. Built here, from source, with this image's own JDK 11
# javac -- the same jar the JDK 25 images of steps 12-13 build from the same
# source. arqgate.SourceScan is the javac plugin the whole of family C reads,
# so at this rung this layer builds the step's principal instrument and not
# only B-4's and C-1's. gates/build.sh compiles it at the image's default
# level, deliberately: the plugin runs INSIDE this image's javac and has to
# match it, while the tree it inspects is compiled at -source 11 by
# source-gate.sh. Two levels, two purposes, and conflating them is how a
# plugin comes to be built for a compiler that will not load it.
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
        case "${v}" in 11.*) found=$((found + 1));; \
            *) echo "RULE-6 VIOLATION: step 07 grades on JDK 11 only, found ${v} at ${j}" >&2; exit 1;; \
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
    grep -q '^staged.era=jdk11-step07$' /tests/assets/STAGED.txt; \
    grep -q '^staged.orderdep=1$'      /tests/assets/STAGED.txt; \
    test "$(grep -c '^as_untrusted timeout -s KILL' /tests/runner/score.sh)" = 2; \
    test "$(grep -vc '^#' /tests/baseline/orderdep.txt)" = 1; \
    grep -q '^staged.ids=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.rec=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.problib.jars=80$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/assets/ids.txt)" = 935; \
    test "$(find /tests/assets/rec/jdk11-step07 -name '*.txt' | wc -l)" = 935; \
    test ! -d /tests/assets/rec/jdk8-step06; \
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
    grep -q '^staged.testlib.jars=78$'  /tests/assets/STAGED.txt; \
    grep -q '^staged.baseline.files=34$' /tests/assets/STAGED.txt; \
    grep -q '^staged.fixtures=169$'     /tests/assets/STAGED.txt; \
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
    test "$(find /tests/assets/testlib -name '*.jar' | wc -l)" = 78; \
    test -f /tests/assets/testlib/mockito-core-4.11.0.jar; \
    test -f /tests/assets/testlib/mockito-junit-jupiter-4.11.0.jar; \
    test -f /tests/assets/testlib/testng-5.14.6.jar; \
    test -f /tests/assets/testlib/jboss-vfs-3.0.0.CR5.jar; \
    test "$(find /tests/assets/testlib -name 'junit-jupiter*.jar' | wc -l)" = 0; \
    test "$(find /tests/assets/testlib -name 'junit-platform*.jar' | wc -l)" = 0; \
    test "$(find /tests/assets/testlib -name 'mockito-core-1*.jar' | wc -l)" = 0; \
    test "$(find /tests/baseline -type f | wc -l)" = 34; \
    test "$(find /tests/fixtures -type f | wc -l)" = 169; \
    # And the thirty-three by NAME, because a count is satisfied by any \
    # thirty-three files and this rung's recipe does two different things to \
    # reach the number. It ADDS five. Four of those -- nsmove, nshold, \
    # toarray and http -- are the populations C-2..C-5 are measured against, \
    # and the fifth, optarr, is B-13.6's ARRIVAL population, which exists \
    # because step 06's blind run arrived at only two of the three buckets \
    # optops.txt names. And it REPLACES three the rung below already staged: \
    # source.txt, target.txt and level.txt are re-derived here, because \
    # level.txt carries this step's must-flip (class-file major 55) and the \
    # other two describe the tree step 06 left behind rather than the tree \
    # step 06 was handed. A replacement is invisible to a count -- \
    # twenty-eight plus six is thirty-four whether or not source.txt is the \
    # one this step meant -- which is why the names are listed and not just \
    # tallied. Thirteen of the thirty-four are frozen SETS: step 06's seven, \
    # carried forward unchanged, plus the five above. A set that failed to \
    # stage does not make the scorer fail -- wantbuckets() over a missing \
    # file yields an empty want, frac() over an empty denominator yields the \
    # scorer's zero-denominator answer, and the gate pays the tree for a \
    # migration nobody checked. \
    for b in accessible api artifacts boxedctor builds classnames \
             collectmethods compat http httpids junit5 lambdafiles level \
             newinstance nioids nshold nsmove optarr optbuckets optops orderdep \
             samifaces sinks source streamfiles target testng-cased \
             testresults tests timesites toarray valuetypes varbuckets \
             views; do \
        if [ ! -s "/tests/baseline/${b}.txt" ]; then \
            echo "baseline/${b}.txt did not stage, or staged empty" >&2; exit 1; \
        fi; \
    done; \
    # ...and nothing else, so that a name this list does not know about cannot \
    # arrive unnoticed -- which is exactly how a stale copy of step 06's \
    # source.txt or level.txt would arrive if a future edit dropped this \
    # rung's three re-derivations from the staging recipe and left the \
    # inherited files in place. The count would not move. \
    ls /tests/baseline | sed 's/\.txt$//' | LC_ALL=C sort > /tmp/blist.txt; \
    printf '%s\n' accessible api artifacts boxedctor builds classnames \
             collectmethods compat http httpids junit5 lambdafiles level \
             newinstance nioids nshold nsmove optarr optbuckets optops orderdep \
             samifaces sinks source streamfiles target testng-cased \
             testresults tests timesites toarray valuetypes varbuckets \
             views | LC_ALL=C sort > /tmp/bwant.txt; \
    if ! cmp -s /tmp/blist.txt /tmp/bwant.txt; then \
        echo "baseline/ is not the thirty-four files this step stages:" >&2; \
        diff /tmp/bwant.txt /tmp/blist.txt >&2 || true; exit 1; \
    fi; \
    rm -f /tmp/blist.txt /tmp/bwant.txt; \
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

# The six blocks that read the typed AST, on twenty-two source reports.
#
# fixtures/c-rehearsal.sh lifts the hoisted source-AST block, B-10, B-11, B-12,
# B-13 and the whole of family C out of the SHIPPED test.sh by their own
# banners, composes them in file order behind a srcgate() that hands them a
# fixture's report instead of compiling anything, and runs them once per
# fixture. Two of the twenty-two are measurements -- `pinned`, the tree this
# step is handed, which is step 06's blind snapshot, and `golden`, this step's
# own solution -- and twenty are stated edits of a measurement.
#
# SIX blocks and not five, which is the way this rehearsal differs from step
# 06's. The compiles were hoisted above family B at step 04 so B-10 could read
# them, step 05 put B-11 beside it, step 06 added B-12, and this rung adds
# B-13: the counters are read in one place and spent in FIVE others. The
# extraction asserts all six are disjoint and in that order in the file, which
# is not decoration -- a hoist that silently un-hoisted would leave a held gate
# reading variables that do not exist yet, and under `set -u` that is a dead
# verifier rather than a wrong number. It also asserts srcgate is called three
# times in the source block and not once anywhere else, and that no block runs
# javac, java, docker or gradlew: the rehearsal is only honest if the blocks it
# composes really are pure arithmetic over a report.
#
# It asserts FIVE figures per fixture and not four: familyC, B10.java7-held,
# B11.java8lang-held, B12.java8-library AND B13.java9-10. A rehearsal that
# scored only family C would pass a held gate that had drifted, and those four
# hold four closed rungs in place.
#
# All five are compared AS STRINGS, because the shell's spelling of a gate is
# part of what is being pinned. `b10=0` is a literal a guarded block assigns
# and never reaches; a gate that computed a zero prints `0.000000`. They are
# the same number and not the same fact, and normalising them would lose the
# difference between a gate that scored nothing and one that never ran. At this
# rung the distinction carries three separate findings at once:
#
#   gate.c1.reach=0        the scan is missing, or found more than one major
#   gate.c1.reach=0.000000 the scan was read and found the wrong major
#   gate.src.penalty=1     no anti-gaming term fired
#   gate.src.penalty=1.000000  one fired and the product came back to unity
#   B12e.ships-java8-library=0         no jars file: never divided
#   B12e.ships-java8-library=0.000000  jars unzipped, none of the four names
#
# And a THIRD SPELLING, new at this step and asserted the same way: a fact that
# is not published AT ALL. B-13's six sub-facts and family C's C-2..C-5 sit
# inside their blocks' measured-report guard, so a tree the scorer could not
# compile publishes their totals -- computed by awk outside the guard -- and
# none of the parts. Six clean zeros there would read like six findings about
# the tree. The rehearsal asserts the ABSENCE, spelled MISSING rather than
# collapsed into the empty string, so a future edit that tidied the parts back
# outside the guard would have to change this file too. It also checks the
# guard structurally, by line number: B-12e after its block's guard closes,
# C-1 before family C's opens, and all six B13.* parts and C2..C5 inside.
#
# Reports and not trees, with TWO exceptions, because two of the gates scored
# in this file are not arithmetic over a report at all:
#
#   B-12e unzips the jars the candidate's build published and reads four
#   package names off the bytes of the class files inside them. Step 05's
#   must-flip, held.
#   C-1 reads the class-file major versions out of the scan arqgate.Scan ran
#   over those same jars. This step's must-flip.
#
# So the fixtures carry real jars holding real zip members and a real scan
# report, the rehearsal writes both of the files those blocks read, and it
# shells out to the same unzip the scorer does. What is rehearsed there is the
# counting, the floors and the means -- not javac's class-file format -- so a
# synthesised constant pool is the honest fixture. A missing unzip would score
# every fixture's B-12e zero, so the rehearsal names that before it starts
# rather than reporting it as twenty-odd arithmetic failures.
#
# THE TWO ARTIFACT CHANNELS ARE TWO CHANNELS, and three fixtures say so by
# failing them on different routes: K3 publishes jars and no scan, K19 a full
# clean scan and jars that name none of the four packages, K20 neither file at
# all. Without K19 and K20 the `[ -s modulejars.txt ]` else-arm never runs and
# `gate.cp=no-jars` is a line no rehearsal has executed.
#
# What that still costs: nothing here says SourceScan's counts are right, only
# that the arithmetic over them is. The counts are verified separately and by a
# different instrument -- fixtures/src-rehearsal.sh, further down, compiles a
# smoke tree with the plugin attached and asserts every counter and every
# record shape against figures measured first and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files this rung reads, so each half of each gate is rehearsed against the
# buckets it will actually be asked about. Fixtures with invented paths would
# agree with a gate pointed at any set at all.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The ones in between are what make
# either of those mean anything, and each is asserted to six decimals rather
# than to a band -- a gate that has drifted lands somewhere, just not here. The
# figures are computed by fixtures/mkfix07.py, an independent implementation of
# the same composition, and written into the rehearsal BY HAND. This image has
# no python3, which makes that separation structural rather than a matter of
# discipline: a rehearsal that computed its own expectations at run time would
# agree with the scorer by construction and assert nothing.
#
# Under the five totals sit 177 assertions on the FACTS behind them, which is
# what tells apart the fixtures that share a total. Row by row, where the row
# is load-bearing rather than the plain half of a gate:
#
#   pinned must score familyC 0.000000 AND B-10, B-11, B-12 and B-13 all
#      1.000000. It is the tree the candidate is handed, and the floor argument
#      at the foot of test.sh rests on the last four: the unmigrated tree
#      collects family B in full, and 0.30*(932/935) + 0.70*0.25 = 0.474 is
#      what that comes to against migration_prompt item 6's bar of 0.5.
#   golden must score all five 1.000000. It is also where the held gates are
#      proved not to punish this step's own work.
#   K1, K2 and K3 all read 0.900000 and are three different findings about a
#      build: every class still at major 54, one class at 55 and 756 at 54, and
#      no scan at all. The SPELLING separates them -- a measured 0.000000, the
#      literal `0` for a set with two majors in it, and `gate.c1=no-scan` --
#      which is why these are string compares and not numeric ones. Every
#      source leg reads full in all three, which is what makes them findings
#      about the BUILD and not about the tree.
#   K4, K5 and K6 are C-2's three ways to be incomplete: a bucket that did not
#      move, a bucket that moved with a javax import left standing beside it,
#      and every bucket holding a jakarta reference with the javax ones not
#      gone. C-3 falls with C-2 in all three, because C-3 is SCALED by C-2 --
#      a tree that has not finished the move cannot be paid for having held the
#      packages Jakarta did not take.
#   K7 and K8 are C-3's two ends. K7 loses one of the 43 held (package,
#      bucket) pairs. K8 is the row this leg exists for: javax.management
#      renamed to jakarta.management, which is what a broad package-scoped
#      rename recipe does to this tree. C-2 reads 1.000000 -- the six buckets
#      that WERE asked to move did move -- and C-3 reads 0.642857.
#   K9 and K10 are C-4's two legs pulled apart. K9 reverts all eight sites to
#      sizing the array at the call site, which is what the input tree does,
#      and reads 0.000000. K10 converts two of the five buckets and reads
#      0.375000: the bucket leg binds below the site leg, which is why C-4
#      counts both.
#   K11, K12 and K13 are C-5, the heaviest leg in the family. K11 completes the
#      migration but names no redirect policy; K12 does half of it; K13 spells
#      every java.net.http reference so that the fourth segment truncates to a
#      type this gate has already counted -- three arrival buckets from five
#      recorded names, which is the row that pins the truncation rule.
#   K14, K15 and K16 are the three penalty arms, one each. K16 is the one the
#      other two cannot see: `Class[]::new` where `Class<?>[]::new` was meant.
#      newgeneric.raw cannot see it, because a member reference is not a `new`
#      expression, and toarray.raw is the channel that can. All three score
#      every leg of family C in full and are charged anyway.
#   K17 is the instrument admitting it has no reading, and it is the row the
#      third spelling exists for. Nothing compiled, so B-10, B-11 and B-12a..d
#      publish the literal `0`; B-12e, B-12, B-13 and familyC publish
#      0.000000, because their totals are computed by an awk outside the guard;
#      and B-13.2..7, C-2..C-5 and the penalty publish NOTHING. gate.c1.reach
#      is still a measured 1.000000, because C-1 reads the scan from outside
#      the guard -- and C-1 still pays nothing, because the mean it is scaled
#      by is zero. That is step 05's R5 finding two rungs on, and it is the
#      whole reason C-1 carries only what the source corroborates.
#   K18 is the ladder's ninth trap at step 06: both frozen Class.newInstance
#      buckets calling the legacy form again. B-13 falls to 0.866667 and family
#      C reads 1.000000, which is the separation the trap asks for.
#   K19 and K20 are the two artifact channels failed apart, above.
#
# And the rehearsal proves it can fail before its clean runs are believed. It
# takes the golden, re-points ONE field -- the `in=` on toarray[2], the sole
# record in C-4's ContainerRegistry#create bucket -- at a method that is not
# one of toarray.txt's five, and touches nothing else. It asserts on the BYTES
# that exactly one line changed, that the old spelling is gone and that the
# summary line `toarray.ctorref=8` still says eight, because an `in=` is an
# attribution and not a count. Then it requires the composed blocks to notice,
# and to notice in one place: C-4's BUCKET leg has to fall to 4 of 5 while its
# SITE leg still reads 8 of 8, C-4 to 0.800000 and no further, C-1 to
# (0.07 + 0.17 + 0.12*0.8 + 0.54)/0.90 = 0.973333 because it is scaled by the
# four source legs, and familyC to 0.973333 -- a row no fixture in the table
# produces. C-2, C-3, C-5 and all four held gates have to stay exactly where
# the golden left them.
#
# Its twenty-two fixture directories are required to be pairwise distinct by
# digest, so that two rows which had become byte-identical twins cannot look
# like twenty-two assertions while being twenty-one; and the table and the
# directory listing are required to name the same set, which is the ladder's
# third trap in miniature -- a fixture that stopped being scored would
# otherwise disappear without changing a line of output.
RUN bash /tests/fixtures/c-rehearsal.sh

# The three baselines and the nineteen frozen set files this rung reads, checked
# against each other rather than against a number typed a second time here.
#
# Every threshold in family C, in B-10, in B-11, in B-12 and in B-13 comes out
# of baseline/source.txt and baseline/target.txt, and the scorer derives none of
# them from the tree it is grading. That is the right design and it moves the
# failure: a baseline with a wrong number in it now produces a confident wrong
# score instead of an error, and nothing else in the image would notice. So the
# files are required to agree with each other, which is a property no single
# typo can satisfy.
#
# baseline/source.txt AT THIS RUNG IS NOT THE PREVIOUS RUNG'S BASELINE. It is
# step 06's golden tree re-measured with the gate that ships HERE, and it had to
# be: step 06's SourceScan cannot see an `ns`, a `toarray` or an `http` record,
# because none of those record kinds existed below this step. Nineteen of the
# counters asserted below are therefore first measurements of an old tree, which
# is exactly the situation the ninth trap describes -- a step-local mean hiding
# an inherited figure that later requirements made unpassable -- and the
# assertions that relate them to target.txt are what make it visible if one of
# them drifted.
#
# STEP 06'S OWN source.txt AND target.txt ARE NOT STAGED. Every rung below this
# one carried its predecessor's pair forward beside its own; here both would
# land as baseline/source.txt and baseline/target.txt, the same two names this
# step's pair lands as, and which one a gate read would come down to the order
# of a staging loop. What step 06 needs carried forward is carried as
# target.b13.* keys inside this step's target.txt, and its seven population
# files -- newinstance.txt, accessible.txt, views.txt, builds.txt, optops.txt,
# boxedctor.txt, varbuckets.txt -- are staged under their own names and checked
# below against this tree's own records.
#
# THE DIAMOND PAIR STOPS MOVING HERE, and that is itself an assertion. At steps
# 03 and 04 target.diamond and target.b10.diamond were the same 93 counted from
# opposite ends; step 05 spent 19 of them on its sinks and step 06 spent the
# twentieth on Set.of, leaving the 73 this tree holds. Nothing family C asks for
# at this rung constructs or destroys a generic instance: an annotation package
# rename does not, `toArray(T[]::new)` replaces an ARRAY creation and not a
# `new Foo<>()`, and HttpClient.newBuilder() is a factory. So target.diamond is
# required to be EQUAL to the tree's own count rather than one below it, which
# is the one shape of this assertion that says "this rung spends none".
#
# The other invariants are the things this rung must NOT move, each appearing in
# both files and required to be equal: the resource clauses (9), the manual
# close() count (0), the java.io floor (4), the non-anonymous explicit remainder
# (23), the anonymous functional residue (12), the @SuppressWarnings sites (44)
# and the raw-type count (0). A rung that moves one of them has either redone an
# earlier step's work or undone it, and B-10, B-11, B-12 and B-13 exist because
# the second is the likelier accident.
#
# THE FLOOR CLAIM, ASSERTED ON THE BASELINES rather than only through a fixture.
# The tree the candidate is handed already satisfies every requirement B-10,
# B-11, B-12 and B-13 state -- 73 diamonds, the 23 non-anonymous sites still
# explicit, 9 resource clauses, no manual closes, java.io at its 4 pinned sites,
# 62 lambdas over 34 files, 24 annotated SAM interfaces, 16 Optional-returning
# helpers, 23 Collectors calls, 6 Objects delegations, 11 java.time references,
# not one currentTimeMillis, both Class.newInstance calls migrated, no
# isAccessible guard with all 14 setAccessible calls still there, no wrapper
# constructor, seven List/Set/Map factory calls with the seven views untouched,
# no Optional.isPresent and 80 of 81 declarations written `var` -- and NONE that
# family C states: class file 54 and not 55, all 22 javax.annotation and
# javax.ejb references still javax, all 8 toArray sites still sizing their own
# array, all 14 legacy HTTP records still in ServletMethodExecutor and no
# java.net.http type anywhere.
#
# Those are the two halves of the calibration table's `pinned` row -- familyC
# 0.0000 with B-10, B-11, B-12 AND B-13 at 1.0000 -- and that row is the floor
# argument at the foot of test.sh. It is the tightest floor on the ladder:
# 0.30*(932/935) + 0.70*(0.25*0.999838 + 0.75*0) = 0.474009 measured -- family B
# is not a flat 1 because B-6 reads this rung's own API baseline and the step-06
# tree has not made the jakarta renames that baseline records -- and 0.475 even
# if both A and B read their caps, against migration_prompt item 6's 0.5 -- a
# margin of 0.025 at that cap. If any of family C's five zeroes below stops
# being zero the floor moves, and it moves in the direction that breaks the
# item.
#
# THE FIVE NEW SET FILES ARE NOT CHECKED AGAINST A COUNT HERE, THEY ARE DERIVED
# -- recomputed from source.txt's own per-site records by the same awk the
# scorer uses, and required to match line for line:
#
#   nsmove.txt    6 (package, bucket) pairs holding 22 references, from
#                 pkg=javax.annotation or pkg=javax.ejb
#   nshold.txt   43 (package, bucket) pairs holding 112 references, from every
#                 OTHER javax package -- and the two sets are required to be
#                 disjoint by construction, because the filters that build them
#                 are each other's complement
#   toarray.txt   5 buckets with their site counts, from form=sized
#   http.txt     14 move records in one file plus 3 hold records in another,
#                 verbatim, and the split is by PATH because C-5 is file-scoped
#   optarr.txt    2 buckets, from the or/ifPresentOrElse/stream arm of optop
#                 intersected with step 06's own optops.txt
#   level.txt    55, and above the 54 the rung below ended on
#   target.txt   40 keys, every one of them divided by something
#
# A BUCKET SPELLED `<path>#-` IS LEGITIMATE FOR AN ns RECORD and is asserted by
# count rather than rejected. An import, and a field declaration at class scope,
# have no enclosing method to name; SourceScan writes `in=-` and nsbuckets()
# keys on it exactly as the want file does, so the bucket is one a candidate can
# empty. Seven of the 49 pairs are of this shape -- one in nsmove, six in nshold
# -- and they are counted here so that a record kind which silently LOST its
# `in=` field, and would therefore collapse every bucket to `#-`, cannot pass.
#
# THE SEVEN INHERITED STEP-06 SET FILES are checked against THIS tree in the
# ARRIVED state, which is what step 06 itself could not do: they name the sites
# step 06 migrated and this tree is step 06's output. newinstance.txt's two
# legacy buckets must now hold a form=ctor record and no form=legacy one;
# accessible.txt's twelve guard buckets must hold no isAccessible call while all
# fourteen setacc buckets still hold theirs; builds.txt's seven buckets must
# hold a factory call and no construction; boxedctor.txt's twelve records must
# be gone from the tree entirely; varbuckets.txt's 81 must be down to the one
# exemption. views.txt is the exception and is still DERIVED, because it is the
# one of the seven step 06 was told to leave alone. If B-13 is a regression
# gate, its populations have to be in the state it is holding, and this is where
# that is established rather than assumed.
#
# optops.txt AND optarr.txt ARE THE NINTH TRAP WRITTEN DOWN. optops.txt is step
# 06's departure population, three buckets; optarr.txt is this rung's arrival
# population and holds TWO of them, because step 06's blind agent reached for a
# Java 9 method at only two. B-13.6's arrival denominator is target.b13.opt.
# arrived and it is required below to equal optarr.txt's line count and NOT
# optops.txt's -- a hold gate whose denominator is what the previous rung ASKED
# for, measured against the tree that rung's agent actually produced, charges
# this rung's candidate for the last one's miss and can never read full.
#
# THE FOUR INHERITED STEP-05 SET FILES and THE THREE INHERITED STEP-04 SET FILES
# are carried forward unchanged and checked the same way step 06 checked them:
# in the arrived state for B-12's four, as B-11's aim for step 04's three.
# streamfiles.txt is staged with them and deliberately NOT READ -- B-11's third
# sub-gate asks its question per <path>#<method> BUCKET, and step 04's static
# review round 2 found that the same question asked per FILE was satisfiable by
# one `xs.stream().count();` per file. A read of streamfiles.txt here would be
# that regression, spelled exactly as it was spelled the first time.
#
# baseline/nioids.txt names four recordings that must still exist in
# assets/ids.txt. It is read by family A as four of 935 ids and by B-10 as the
# proviso that halves its NIO third, so an id renamed out of the recording set
# would quietly stop protecting anything in both places at once.
RUN set -eux; \
    test "$(grep -c '^major=' /tests/baseline/level.txt)" = 1; \
    grep -qxF 'major=55' /tests/baseline/level.txt; \
    want=$(sed -n 's/^major=//p' /tests/baseline/level.txt | head -1); \
    test "${want}" -gt 54; \
    bsrc=/tests/baseline/source.txt; \
    btgt=/tests/baseline/target.txt; \
    dia=$(sed -n  's/^newgeneric\.diamond=//p'             ${bsrc} | head -1); \
    exp=$(sed -n  's/^newgeneric\.explicit=//p'            ${bsrc} | head -1); \
    ean=$(sed -n  's/^newgeneric\.explicitanon=//p'        ${bsrc} | head -1); \
    raw=$(sed -n  's/^newgeneric\.raw=//p'                 ${bsrc} | head -1); \
    res=$(sed -n  's/^try\.resources=//p'                  ${bsrc} | head -1); \
    clo=$(sed -n  's/^close\.manual=//p'                   ${bsrc} | head -1); \
    ion=$(sed -n  's/^iofile\.new=//p'                     ${bsrc} | head -1); \
    iod=$(sed -n  's/^iofile\.decl=//p'                    ${bsrc} | head -1); \
    ref=$(sed -n  's/^niofile\.ref=//p'                    ${bsrc} | head -1); \
    srf=$(sed -n  's/^niofile\.staticref=//p'              ${bsrc} | head -1); \
    sup=$(sed -n  's/^suppress\.sites=//p'                 ${bsrc} | head -1); \
    anf=$(sed -n  's/^anonymousfunctional\.size=//p'       ${bsrc} | head -1); \
    con=$(sed -n  's/^anonymousfunctional\.convertible=//p' ${bsrc} | head -1); \
    lam=$(sed -n  's/^lambda\.size=//p'                    ${bsrc} | head -1); \
    lamf=$(sed -n 's/^lambda\.files=//p'                   ${bsrc} | head -1); \
    idc=$(sed -n  's/^iface\.decl=//p'                     ${bsrc} | head -1); \
    ism=$(sed -n  's/^iface\.sam=//p'                      ${bsrc} | head -1); \
    isa=$(sed -n  's/^iface\.samannotated=//p'             ${bsrc} | head -1); \
    fec=$(sed -n  's/^foreach\.collect=//p'                ${bsrc} | head -1); \
    sopm=$(sed -n 's/^stream\.opmethods=//p'               ${bsrc} | head -1); \
    blop=$(sed -n 's/^loop\.size=//p'                      ${bsrc} | head -1); \
    rn=$(sed -n   's/^retnull\.size=//p'                   ${bsrc} | head -1); \
    rnm=$(sed -n  's/^retnull\.methods=//p'                ${bsrc} | head -1); \
    rna=$(sed -n  's/^retnull\.api=//p'                    ${bsrc} | head -1); \
    ort=$(sed -n  's/^optret\.size=//p'                    ${bsrc} | head -1); \
    oop=$(sed -n  's/^optop\.size=//p'                     ${bsrc} | head -1); \
    oog=$(sed -n  's/^optop\.get=//p'                      ${bsrc} | head -1); \
    oip=$(sed -n  's/^optop\.ispresent=//p'                ${bsrc} | head -1); \
    acc=$(sed -n  's/^accum\.size=//p'                     ${bsrc} | head -1); \
    col=$(sed -n  's/^collect\.size=//p'                   ${bsrc} | head -1); \
    obj=$(sed -n  's/^objects\.size=//p'                   ${bsrc} | head -1); \
    jt=$(sed -n   's/^jtime\.size=//p'                     ${bsrc} | head -1); \
    mil=$(sed -n  's/^millis\.size=//p'                    ${bsrc} | head -1); \
    nis=$(sed -n  's/^newinstance\.size=//p'               ${bsrc} | head -1); \
    nil=$(sed -n  's/^newinstance\.legacy=//p'             ${bsrc} | head -1); \
    nic=$(sed -n  's/^newinstance\.ctor=//p'               ${bsrc} | head -1); \
    bxs=$(sed -n  's/^boxedctor\.size=//p'                 ${bsrc} | head -1); \
    fac=$(sed -n  's/^factory\.size=//p'                   ${bsrc} | head -1); \
    fof=$(sed -n  's/^factory\.of=//p'                     ${bsrc} | head -1); \
    fcp=$(sed -n  's/^factory\.copyof=//p'                 ${bsrc} | head -1); \
    lcv=$(sed -n  's/^legacycoll\.view=//p'                ${bsrc} | head -1); \
    lcvf=$(sed -n 's/^legacycoll\.viewfield=//p'           ${bsrc} | head -1); \
    lcb=$(sed -n  's/^legacycoll\.build=//p'               ${bsrc} | head -1); \
    acs=$(sed -n  's/^accessible\.size=//p'                ${bsrc} | head -1); \
    aci=$(sed -n  's/^accessible\.isaccessible=//p'        ${bsrc} | head -1); \
    acc9=$(sed -n 's/^accessible\.canaccess=//p'           ${bsrc} | head -1); \
    acst=$(sed -n 's/^accessible\.setaccessible=//p'       ${bsrc} | head -1); \
    vsz=$(sed -n  's/^var\.size=//p'                       ${bsrc} | head -1); \
    vcn=$(sed -n  's/^var\.candidatenew=//p'               ${bsrc} | head -1); \
    vmd=$(sed -n  's/^var\.multideclarator=//p'            ${bsrc} | head -1); \
    nsf=$(sed -n  's/^ns\.files=//p'                       ${bsrc} | head -1); \
    tas=$(sed -n  's/^toarray\.sized=//p'                  ${bsrc} | head -1); \
    tac=$(sed -n  's/^toarray\.ctorref=//p'                ${bsrc} | head -1); \
    tar=$(sed -n  's/^toarray\.raw=//p'                    ${bsrc} | head -1); \
    taf=$(sed -n  's/^toarray\.files=//p'                  ${bsrc} | head -1); \
    hlr=$(sed -n  's/^http\.legacyref=//p'                 ${bsrc} | head -1); \
    hlc=$(sed -n  's/^http\.legacycall=//p'                ${bsrc} | head -1); \
    hcr=$(sed -n  's/^http\.clientref=//p'                 ${bsrc} | head -1); \
    hrd=$(sed -n  's/^http\.redirects=//p'                 ${bsrc} | head -1); \
    hff=$(sed -n  's/^http\.files=//p'                     ${bsrc} | head -1); \
    tdia=$(sed -n 's/^target\.diamond=//p'           ${btgt} | head -1); \
    tnon=$(sed -n 's/^target\.explicit\.nonanon=//p' ${btgt} | head -1); \
    tres=$(sed -n 's/^target\.try\.resources=//p'    ${btgt} | head -1); \
    tio=$(sed -n  's/^target\.iofile=//p'            ${btgt} | head -1); \
    bdia=$(sed -n 's/^target\.b10\.diamond=//p'      ${btgt} | head -1); \
    bclo=$(sed -n 's/^target\.b10\.close=//p'        ${btgt} | head -1); \
    bnio=$(sed -n 's/^target\.b10\.nio=//p'          ${btgt} | head -1); \
    tanf=$(sed -n 's/^target\.anonfn=//p'            ${btgt} | head -1); \
    elf=$(sed -n  's/^target\.b11\.lambdafiles=//p'  ${btgt} | head -1); \
    esam=$(sed -n 's/^target\.b11\.sam=//p'          ${btgt} | head -1); \
    ecm=$(sed -n  's/^target\.b11\.collectmethods=//p' ${btgt} | head -1); \
    topt=$(sed -n 's/^target\.b12\.optbuckets=//p'   ${btgt} | head -1); \
    tsnk=$(sed -n 's/^target\.b12\.sinks=//p'        ${btgt} | head -1); \
    tvt=$(sed -n  's/^target\.b12\.valuetypes=//p'   ${btgt} | head -1); \
    tts=$(sed -n  's/^target\.b12\.timesites=//p'    ${btgt} | head -1); \
    f5o=$(sed -n  's/^target\.b12\.optional\.floor=//p'   ${btgt} | head -1); \
    f5c=$(sed -n  's/^target\.b12\.collectors\.floor=//p' ${btgt} | head -1); \
    f5j=$(sed -n  's/^target\.b12\.objects\.floor=//p'    ${btgt} | head -1); \
    f5t=$(sed -n  's/^target\.b12\.jtime\.floor=//p'      ${btgt} | head -1); \
    g2l=$(sed -n  's/^target\.b13\.legacy=//p'       ${btgt} | head -1); \
    g2f=$(sed -n  's/^target\.b13\.ctor\.floor=//p'  ${btgt} | head -1); \
    g3g=$(sed -n  's/^target\.b13\.guards=//p'       ${btgt} | head -1); \
    g3s=$(sed -n  's/^target\.b13\.setacc=//p'       ${btgt} | head -1); \
    g4b=$(sed -n  's/^target\.b13\.boxed=//p'        ${btgt} | head -1); \
    g5b=$(sed -n  's/^target\.b13\.builds=//p'       ${btgt} | head -1); \
    g5v=$(sed -n  's/^target\.b13\.views=//p'        ${btgt} | head -1); \
    g6d=$(sed -n  's/^target\.b13\.opt\.departed=//p' ${btgt} | head -1); \
    g6a=$(sed -n  's/^target\.b13\.opt\.arrived=//p'  ${btgt} | head -1); \
    g7c=$(sed -n  's/^target\.b13\.candidatenew=//p' ${btgt} | head -1); \
    g7w=$(sed -n  's/^target\.b13\.want=//p'         ${btgt} | head -1); \
    c2b=$(sed -n  's/^target\.c2\.buckets=//p'       ${btgt} | head -1); \
    c2r=$(sed -n  's/^target\.c2\.refs=//p'          ${btgt} | head -1); \
    c3b=$(sed -n  's/^target\.c3\.buckets=//p'       ${btgt} | head -1); \
    c3r=$(sed -n  's/^target\.c3\.refs=//p'          ${btgt} | head -1); \
    c4b=$(sed -n  's/^target\.c4\.buckets=//p'       ${btgt} | head -1); \
    c4s=$(sed -n  's/^target\.c4\.sized=//p'         ${btgt} | head -1); \
    c5f=$(sed -n  's/^target\.c5\.file=//p'          ${btgt} | head -1); \
    c5l=$(sed -n  's/^target\.c5\.legacy=//p'        ${btgt} | head -1); \
    c5t=$(sed -n  's/^target\.c5\.types=//p'         ${btgt} | head -1); \
    c5s=$(sed -n  's/^target\.c5\.cdistub=//p'       ${btgt} | head -1); \
    # Every key target.txt carries is a key test.sh reads, and every key test.sh \
    # reads is a key target.txt carries. 40 both ways: a key added to the file \
    # and not to the scorer is a frozen number nothing divides by, which is the \
    # kind that goes wrong unnoticed, and a key the scorer reads and the file \
    # does not carry reads as the empty string and defaults to 0. \
    test "$(grep -c '^target\.' ${btgt})" = 40; \
    grep -o '^target\.[a-z0-9.]*' ${btgt} | LC_ALL=C sort > /tmp/tkeys.txt; \
    sed -n "s/.*tv 'target\\(.*\\)').*/target\\1/p" /tests/test.sh \
      | tr -d '\\' | LC_ALL=C sort -u > /tmp/rkeys.txt; \
    test "$(grep -c . /tmp/rkeys.txt)" = 40; \
    if ! cmp -s /tmp/tkeys.txt /tmp/rkeys.txt; then \
        echo "baseline/target.txt and test.sh disagree about which target.* keys exist:" >&2; \
        diff /tmp/tkeys.txt /tmp/rkeys.txt >&2 || true; exit 1; \
    fi; \
    # 39 of the 40 are numbers. target.c5.file is the one that is not, and it is \
    # the only key on the ladder whose value is a path: C-5 is scoped by FILE, \
    # for the reason test.sh gives where it reads it, so the key names the file \
    # rather than counting anything. It is exempted here BY NAME and then \
    # checked as a path, which is stricter than the numeric loop it escapes -- a \
    # typo in it would aim the whole leg at a file that does not exist and every \
    # count in this image would still agree. \
    for k in $(cat /tmp/tkeys.txt); do \
        [ "${k}" = "target.c5.file" ] && continue; \
        v=$(sed -n "s/^${k}=//p" ${btgt} | head -1); \
        case "${v}" in ''|*[!0-9]*) echo "${k} is not a number: '${v}'" >&2; exit 1;; esac; \
    done; \
    rm -f /tmp/tkeys.txt /tmp/rkeys.txt; \
    case "${c5f}" in *"/src/main/java/"*.java) :;; *) echo "target.c5.file is not a src/main source path: ${c5f}" >&2; exit 1;; esac; \
    # What the input tree HOLDS, and must go on holding: steps 03, 04, 05 and 06. \
    test "${dia}" = 73; \
    test "${exp}" = 36; \
    test "${ean}" = 13; \
    test "${res}" = 9; \
    test "${clo}" = 0; \
    test "${raw}" = 0; \
    test "${sup}" = 44; \
    test "${lam}" = 62; \
    test "${lamf}" = 34; \
    test "${idc}" = 65; \
    test "${ism}" = 24; \
    test "${isa}" = 24; \
    test "${sopm}" = 25; \
    test "${anf}" = 12; \
    test "${blop}" = 114; \
    test "${con}" = 1; \
    test "${fec}" = 0; \
    test "${ort}" = 16; \
    test "${oog}" = 0; \
    test "${acc}" = 1; \
    test "${col}" = 23; \
    test "${obj}" = 6; \
    test "${jt}"  = 11; \
    test "${mil}" = 0; \
    test "${rn}"  = 25; \
    test "${rnm}" = 23; \
    test "${rna}" = 22; \
    # ...and step 06's own five, in the ARRIVED state. These were the seven \
    # zeroes of the rung below, and every one of them has flipped: the tree this \
    # step is handed is the tree step 06's blind agent produced. B-13 holds them \
    # and the per-bucket work is done further down; what is asserted here is the \
    # counter, because a counter that had NOT flipped would mean the wrong tree \
    # was measured into this baseline and every bucket check below would still \
    # pass against it. \
    test "${nil}" = 0; \
    test "${nic}" = 13; \
    test "${nis}" = 13; \
    test "${aci}" = 0; \
    test "${acc9}" = 0; \
    test "${acst}" = 14; \
    test "${acs}" = 14; \
    test "${bxs}" = 0; \
    test "${fac}" = 7; \
    test "${fof}" = 6; \
    test "${fcp}" = 1; \
    test "${lcb}" = 11; \
    test "${lcv}" = 7; \
    test "${lcvf}" = 7; \
    test "${oip}" = 0; \
    test "${vsz}" = 80; \
    test "${vcn}" = 1; \
    test "${vmd}" = 0; \
    # C-6's arrival half at step 06 reached two of three, which is what \
    # optarr.txt records; here it is asserted as a count of BUCKETS and not of \
    # records, because the third bucket on this tree -- \
    # ToolingDeploymentFormatter#formatNode -- holds a Java 9 arrival that was \
    # never one of step 06's three. Three buckets hold one, two of them are \
    # step 06's, and optarr.txt is the intersection. \
    test "$(sed -n 's/^optop\[[0-9]*\]=//p' ${bsrc} \
              | awk '$2 == "use" && ($3 == "or" || $3 == "ifPresentOrElse" || $3 == "stream")' \
              | grep -c .)" -ge 3; \
    # ...and what it has NOT done, which is the whole of family C. Five gates, \
    # every one of them measured 0 on this tree, and the floor is 0.474009 -- \
    # 0.475 even with families A and B at their caps -- against a 0.5 ceiling \
    # that holds only while all five do. C-1 is not here -- it reads the \
    # class-file header of jars a build produces and there is no build in this \
    # layer -- and it is asserted in the calibration run instead. \
    test "$(sed -n 's/^ns\.ref\.jakarta\./&/p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^ns\.import\.jakarta\./&/p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c 'pkg=jakarta\.')" = 0; \
    test "${tas}" = 8; \
    test "${tac}" = 0; \
    test "${tar}" = 0; \
    test "${taf}" = 5; \
    test "${hcr}" = 0; \
    test "${hrd}" = 0; \
    test "$(sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=clienttype')" = 0; \
    test "$(sed -n 's/^ns\.ref\.javax\.annotation=//p' ${bsrc} | head -1)" = 17; \
    test "$(sed -n 's/^ns\.ref\.javax\.ejb=//p' ${bsrc} | head -1)" = 5; \
    # The two files agree about the things that must not move. \
    test "$(( exp - ean ))" = "${tnon}"; \
    test "${tnon}" = 23; \
    test "${anf}" = "${tanf}"; \
    test "${res}" = "${tres}"; \
    test "$(( ion + iod ))" = "${tio}"; \
    test "${tio}" = 4; \
    test "${ism}" = "${esam}"; \
    test "${isa}" = "${esam}"; \
    test "${elf}" = 20; \
    test "${elf}" -lt "${lamf}"; \
    test "${ecm}" = 24; \
    test "${ecm}" -le "${sopm}"; \
    # The diamond pair, and the difference is ZERO. See the note above: this is \
    # the first rung of the ladder that spends none of them. \
    test "${tdia}" = 73; \
    test "$(( dia - tdia ))" = 0; \
    test "${bdia}" = 93; \
    test "${dia}" -lt "${bdia}"; \
    # B-10's other two denominators, related to the tree rather than restated. \
    test "${bclo}" = 11; \
    test "${bclo}" -gt "${clo}"; \
    test "${bclo}" -ge "${tres}"; \
    test "${bnio}" = 11; \
    test "${bnio}" -gt 0; \
    test "${bnio}" -le "$(( ref + srf ))"; \
    # B-12's four constant-pool floors, carried forward from step 05 unchanged. \
    # Each is at least 1, or the leg would be satisfied by a tree that published \
    # nothing at all, and each is below the number of SOURCE sites that have to \
    # reference the class it counts -- which is the relation this layer can \
    # establish. What the golden's compiled constant pool actually reaches is a \
    # calibration figure measured on a real build and is not restated here. \
    test "${f5o}" = 5; test "${f5c}" = 6; test "${f5j}" = 2; test "${f5t}" = 2; \
    for f in "${f5o}" "${f5c}" "${f5j}" "${f5t}"; do test "${f}" -ge 1; done; \
    test "${f5o}" -le "${ort}"; test "${f5c}" -le "${col}"; \
    test "${f5j}" -le "${obj}"; test "${f5t}" -le "${jt}"; \
    # B-13's eleven denominators. Step 06's populations are GONE from this tree \
    # -- that is what B-13 holding means -- so each one is tied to the frozen set \
    # file it was cut from rather than to a counter here, and the set files are \
    # in turn tied to this tree in the arrived state further down. The two that \
    # CAN still be read off this tree are read off it, because a denominator \
    # that has a second source should have one. \
    test "${g2l}" = 2; \
    test "${g2f}" = 13; \
    test "${g2f}" = "${nis}"; \
    test "${g3g}" = 12; \
    test "${g3s}" = 14; \
    test "${g3s}" = "${acst}"; \
    test "${g3g}" -lt "${g3s}"; \
    test "${g4b}" = 12; \
    test "${g5b}" = 7; \
    test "${g5v}" = "${lcvf}"; \
    test "${g5v}" = 7; \
    test "${g6d}" = 3; \
    test "${g6a}" = 2; \
    test "${g6a}" -lt "${g6d}"; \
    test "${g7c}" = 81; \
    test "${g7w}" = "$(( g7c - 1 ))"; \
    test "${g7w}" = 80; \
    test "${g7w}" = "${vsz}"; \
    # Family C's own denominators, every one of them tied to a counter above or \
    # to a set file below. A denominator that is only a literal is a literal \
    # that gets copied forward; each of these is required to BE the population \
    # it divides. \
    test "${c2b}" = 6; \
    test "${c2r}" = 22; \
    test "${c2r}" = "$(( $(sed -n 's/^ns\.ref\.javax\.annotation=//p' ${bsrc} | head -1) \
                       + $(sed -n 's/^ns\.ref\.javax\.ejb=//p' ${bsrc} | head -1) ))"; \
    test "${c3b}" = 43; \
    test "${c3r}" = 112; \
    test "${c3b}" -gt "${c2b}"; \
    test "${c3r}" -gt "${c2r}"; \
    test "${c4b}" = 5; \
    test "${c4s}" = "${tas}"; \
    test "${c4s}" = 8; \
    test "${c4b}" -lt "${c4s}"; \
    test "${c5l}" = 14; \
    test "${c5t}" = 3; \
    test "${c5s}" = 3; \
    test "$(( c5l + c5s ))" = "$(( hlr + hlc ))"; \
    test "${hff}" = 2; \
    test "${nsf}" = 17; \
    # The per-site records are the instrument the set files are derived from, so \
    # they have to BE per-site: every record of every kind the gates bucket on \
    # must carry an in= field, or its bucket is the `-` sentinel and no \
    # candidate can ever fill it. This is the seventh trap applied to a \
    # baseline: assert the bytes, not the intention. \
    for kind in retnull optret accum collect objects jtime stream streamop \
                newinstance accessible legacycoll optop varcand factory \
                ns toarray http; do \
        n=$(sed -n "s/^${kind}\[[0-9]*\]=//p" ${bsrc} | awk '$0 !~ / in=/ { n++ } END { print n + 0 }'); \
        if [ "${n}" != 0 ]; then \
            echo "baseline/source.txt: ${n} ${kind} record(s) carry no in= field" >&2; exit 1; \
        fi; \
    done; \
    # ns is the one kind that is allowed a `-`, and 7 records have it across 7 \
    # buckets: imports and class-scope field declarations, which have no \
    # enclosing method to name. Both sides -- the want files here and the \
    # scorer's nsbuckets() -- build the key the same way, so `path#-` is a \
    # bucket that can be emptied and is therefore reachable. It is asserted as \
    # exactly seven rather than tolerated, because a record kind that had lost \
    # its in= field would collapse EVERY bucket to this shape. \
    test "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | awk '$0 ~ / in=-$/' | grep -c .)" -ge 7; \
    test "$(sed -n 's/^retnull\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${rn}"; \
    test "$(sed -n 's/^optret\[[0-9]*\]=//p'  ${bsrc} | grep -c .)" = "${ort}"; \
    test "$(sed -n 's/^optop\[[0-9]*\]=//p'   ${bsrc} | grep -c .)" = "${oop}"; \
    test "$(sed -n 's/^accum\[[0-9]*\]=//p'   ${bsrc} | grep -c .)" = "${acc}"; \
    test "$(sed -n 's/^collect\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${col}"; \
    test "$(sed -n 's/^objects\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${obj}"; \
    test "$(sed -n 's/^jtime\[[0-9]*\]=//p'   ${bsrc} | grep -c .)" = "${jt}"; \
    test "$(sed -n 's/^millis\[[0-9]*\]=//p'  ${bsrc} | grep -c .)" = "${mil}"; \
    test "$(sed -n 's/^newinstance\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${nis}"; \
    test "$(sed -n 's/^accessible\[[0-9]*\]=//p'  ${bsrc} | grep -c .)" = "${acs}"; \
    test "$(sed -n 's/^boxedctor\[[0-9]*\]=//p'   ${bsrc} | grep -c .)" = "${bxs}"; \
    test "$(sed -n 's/^factory\[[0-9]*\]=//p'     ${bsrc} | grep -c .)" = "${fac}"; \
    test "$(sed -n 's/^var\[[0-9]*\]=//p'         ${bsrc} | grep -c .)" = "${vsz}"; \
    test "$(sed -n 's/^toarray\[[0-9]*\]=//p'     ${bsrc} | grep -c .)" = "$(( tas + tac + tar ))"; \
    test "$(sed -n 's/^http\[[0-9]*\]=//p'        ${bsrc} | grep -c .)" = "$(( hlr + hlc + hcr + hrd ))"; \
    # bucketise <kind> -- the <path>#<method> keys of one record kind, exactly \
    # as test.sh's buckets() builds them: field 1 up to the colon, joined to the \
    # LAST in= on the line. Written once here and used for every set below. \
    bucketise() { \
        sed -n "s/^$1\[[0-9]*\]=//p" ${bsrc} \
          | awk '{ split($1, a, ":"); m = ""; \
                   for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                   if (m != "") print a[1] "#" m }' \
          | LC_ALL=C sort -u; \
    }; \
    # fbucketise <kind> <field> [<field>] -- the same, filtered on whole fields, \
    # exactly as test.sh's fbuckets() filters. Whole fields and not a substring \
    # match, because `form=ctor` is a substring of nothing here today and would \
    # be of `form=ctornew` tomorrow. \
    fbucketise() { \
        fk=$1; fw1=$2; fw2=${3:-}; \
        sed -n "s/^${fk}\[[0-9]*\]=//p" ${bsrc} \
          | awk -v w1="${fw1}" -v w2="${fw2}" \
              '{ m = ""; h1 = 0; h2 = (w2 == "" ? 1 : 0); \
                 for (i = 2; i <= NF; i++) { \
                    if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                    if ($i == w1) h1 = 1; \
                    if (w2 != "" && $i == w2) h2 = 1 } \
                 if (h1 && h2 && m != "") { split($1, a, ":"); print a[1] "#" m } }' \
          | LC_ALL=C sort -u; \
    }; \
    # nsbucketise <awk condition> -- the pkg|bucket pairs of the ns records the \
    # condition selects, exactly as test.sh's nsbuckets() builds them. \
    nsbucketise() { \
        sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} \
          | awk "{ p = substr(\$2, 5) } $1 { \
                   split(\$1, f, \":\"); m = \"\"; \
                   for (i = 2; i <= NF; i++) if (substr(\$i, 1, 3) == \"in=\") m = substr(\$i, 4); \
                   if (m != \"\") print p \"|\" f[1] \"#\" m }" \
          | LC_ALL=C sort -u; \
    }; \
    nsrefs() { \
        sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | awk "{ p = substr(\$2, 5) } $1" | grep -c . || true; \
    }; \
    nswanted() { grep -v '^#' "$1" | grep . | awk '{ print $1 "|" $2 }' | LC_ALL=C sort -u; }; \
    setpart() { grep -v '^#' "$1" | grep . | awk -v t="$2" '$1 == t { print $2 }' | LC_ALL=C sort -u; }; \
    wanted()  { grep -v '^#' "$1" | grep . | awk '{ print $1 }' | LC_ALL=C sort -u; }; \
    # --- nsmove.txt, derived -------------------------------------------------- \
    nsbucketise 'p == "javax.annotation" || p == "javax.ejb"' > /tmp/d-nsmove.txt; \
    nswanted /tests/baseline/nsmove.txt > /tmp/s-nsmove.txt; \
    if ! cmp -s /tmp/d-nsmove.txt /tmp/s-nsmove.txt; then \
        echo "baseline/nsmove.txt is not the javax.annotation/javax.ejb pairs in source.txt:" >&2; \
        diff /tmp/d-nsmove.txt /tmp/s-nsmove.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-nsmove.txt)" = "${c2b}"; \
    test "$(nsrefs 'p == "javax.annotation" || p == "javax.ejb"')" = "${c2r}"; \
    test "$(cut -d'|' -f1 /tmp/s-nsmove.txt | LC_ALL=C sort -u | grep -c .)" = 2; \
    test "$(grep -cF '#-' /tmp/s-nsmove.txt)" = 1; \
    # --- nshold.txt, derived, and the COMPLEMENT of nsmove -------------------- \
    nsbucketise 'p != "javax.annotation" && p != "javax.ejb" && p !~ /^jakarta\./' > /tmp/d-nshold.txt; \
    nswanted /tests/baseline/nshold.txt > /tmp/s-nshold.txt; \
    if ! cmp -s /tmp/d-nshold.txt /tmp/s-nshold.txt; then \
        echo "baseline/nshold.txt is not the other javax packages' pairs in source.txt:" >&2; \
        diff /tmp/d-nshold.txt /tmp/s-nshold.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-nshold.txt)" = "${c3b}"; \
    test "$(nsrefs 'p != "javax.annotation" && p != "javax.ejb" && p !~ /^jakarta\./')" = "${c3r}"; \
    test "$(grep -cF '#-' /tmp/s-nshold.txt)" = 6; \
    # DISJOINT AND EXHAUSTIVE. The two filters are each other's complement over \
    # a tree with no jakarta record, so the two want sets must partition every \
    # ns record's pair: no pair in both, and the two counts summing to the whole \
    # population. A bucket that fell out of both filters would be a javax \
    # reference C-2 does not ask to move and C-3 does not reward holding, and \
    # nothing else here would see it. \
    if [ "$(LC_ALL=C comm -12 /tmp/s-nsmove.txt /tmp/s-nshold.txt | grep -c .)" != 0 ]; then \
        echo "a pair is in BOTH nsmove.txt and nshold.txt:" >&2; \
        LC_ALL=C comm -12 /tmp/s-nsmove.txt /tmp/s-nshold.txt >&2; exit 1; \
    fi; \
    test "$(( c2b + c3b ))" = "$(nsbucketise '1' | grep -c .)"; \
    test "$(( c2r + c3r ))" = "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c .)"; \
    # The eight javax packages SourceScan counts and the eight this pair of \
    # files divides are the same eight, and the per-package reference totals in \
    # the ns.ref.javax.* counters are the per-package totals of the records. \
    sed -n 's/^ns\.ref\.javax\.\([a-z]*\)=.*/javax.\1/p' ${bsrc} | LC_ALL=C sort -u > /tmp/d-nspkg.txt; \
    test "$(grep -c . /tmp/d-nspkg.txt)" = 8; \
    { cut -d'|' -f1 /tmp/s-nsmove.txt; cut -d'|' -f1 /tmp/s-nshold.txt; } \
      | LC_ALL=C sort -u > /tmp/s-nspkg.txt; \
    if ! cmp -s /tmp/d-nspkg.txt /tmp/s-nspkg.txt; then \
        echo "the ns.ref.javax.* counters and the two set files name different packages:" >&2; \
        diff /tmp/d-nspkg.txt /tmp/s-nspkg.txt >&2 || true; exit 1; \
    fi; \
    while read -r p; do \
        c=$(sed -n "s/^ns\\.ref\\.${p}=//p" ${bsrc} | head -1); \
        r=$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | awk -v p="${p}" '{ q = substr($2, 5) } q == p' | grep -c . || true); \
        if [ "${c}" != "${r}" ]; then \
            echo "ns.ref.${p} says ${c} and source.txt holds ${r} record(s)" >&2; exit 1; \
        fi; \
    done < /tmp/d-nspkg.txt; \
    # --- toarray.txt, derived ------------------------------------------------- \
    fbucketise toarray form=sized > /tmp/d-tasized.txt; \
    wanted /tests/baseline/toarray.txt > /tmp/s-toarray.txt; \
    if ! cmp -s /tmp/d-tasized.txt /tmp/s-toarray.txt; then \
        echo "baseline/toarray.txt is not the form=sized buckets in source.txt:" >&2; \
        diff /tmp/d-tasized.txt /tmp/s-toarray.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-toarray.txt)" = "${c4b}"; \
    test "$(grep -v '^#' /tests/baseline/toarray.txt | grep . \
              | awk '{ sub(/^sites=/, "", $2); s += $2 } END { print s + 0 }')" = "${c4s}"; \
    # Column 2 is a per-bucket site count and it is required to BE the per-bucket \
    # record count, bucket by bucket rather than only in total. C-4's departure \
    # half is scored per bucket and its `sized` half per site; a file whose \
    # column 2 summed correctly while attributing the sites to the wrong buckets \
    # would make the two halves disagree about which method is finished. \
    sed -n 's/^toarray\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort | uniq -c | awk '{ print $2, "sites=" $1 }' | LC_ALL=C sort > /tmp/d-tacount.txt; \
    grep -v '^#' /tests/baseline/toarray.txt | grep . | LC_ALL=C sort > /tmp/s-tacount.txt; \
    if ! cmp -s /tmp/d-tacount.txt /tmp/s-tacount.txt; then \
        echo "baseline/toarray.txt: a bucket's site count is not its record count:" >&2; \
        diff /tmp/d-tacount.txt /tmp/s-tacount.txt >&2 || true; exit 1; \
    fi; \
    # Two of the five hold one site and three hold two, which is why the leg has \
    # a per-site half at all: a purely per-bucket score would pay the same for \
    # converting one of DeployableTestBuilder#build's two calls as for both. \
    test "$(awk '$2 != "sites=1"' /tmp/s-tacount.txt | grep -c .)" = 3; \
    test "$(sed 's/#.*//' /tmp/s-toarray.txt | LC_ALL=C sort -u | grep -c .)" = "${taf}"; \
    if grep -v '/src/main/java/' /tmp/s-toarray.txt; then \
        echo "baseline/toarray.txt: a bucket is not under src/main" >&2; exit 1; \
    fi; \
    # --- http.txt, verbatim, and split by PATH -------------------------------- \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] == f' \
      | LC_ALL=C sort > /tmp/d-httpmove.txt; \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] != f' \
      | LC_ALL=C sort > /tmp/d-httphold.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "move" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httpmove.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "hold" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httphold.txt; \
    if ! cmp -s /tmp/d-httpmove.txt /tmp/s-httpmove.txt; then \
        echo "baseline/http.txt's move records are not target.c5.file's records in source.txt:" >&2; \
        diff /tmp/d-httpmove.txt /tmp/s-httpmove.txt >&2 || true; exit 1; \
    fi; \
    if ! cmp -s /tmp/d-httphold.txt /tmp/s-httphold.txt; then \
        echo "baseline/http.txt's hold records are not the other file's records in source.txt:" >&2; \
        diff /tmp/d-httphold.txt /tmp/s-httphold.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-httpmove.txt)" = "${c5l}"; \
    test "$(grep -c . /tmp/s-httphold.txt)" = "${c5s}"; \
    # THE HOLD RECORDS ARE NOT IN target.c5.file AND ARE NOT SCORED. C-5's \
    # legacy half counts records in ONE file; the three the CDI test holds are \
    # an anonymous `new URLConnection(u){...}` used as an injection fixture, and \
    # a gate that asked for http.legacyref == 0 tree-wide would score the \
    # finished migration zero. Asserted as being in a DIFFERENT file, and in \
    # src/test, which is what makes them out of C-5's reach by construction \
    # rather than by the scorer remembering to exclude them. \
    if grep -qF "${c5f}" /tmp/s-httphold.txt; then \
        echo "baseline/http.txt: a hold record is inside target.c5.file after all" >&2; exit 1; \
    fi; \
    test "$(awk '{ split($1, p, ":"); print p[1] }' /tmp/s-httphold.txt | LC_ALL=C sort -u | grep -c .)" = 1; \
    if grep -v '/src/test/java/' /tmp/s-httphold.txt; then \
        echo "baseline/http.txt: a hold record is not under src/test" >&2; exit 1; \
    fi; \
    test "$(awk '{ split($1, p, ":"); print p[1] }' /tmp/s-httpmove.txt | LC_ALL=C sort -u | grep -c .)" = 1; \
    test "$(awk '$2 == "form=legacycall" || $2 == "form=legacytype"' /tmp/s-httpmove.txt | grep -c .)" = "${c5l}"; \
    # The two files in http.txt are http.files, and nothing else in the tree \
    # holds an http record. \
    test "$(sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk '{ split($1, p, ":"); print p[1] }' \
              | LC_ALL=C sort -u | grep -c .)" = "${hff}"; \
    # --- optarr.txt, derived and INTERSECTED with step 06's optops.txt -------- \
    sed -n 's/^optop\[[0-9]*\]=//p' ${bsrc} \
      | awk '$2 == "use" && ($3 == "or" || $3 == "ifPresentOrElse" || $3 == "stream") { \
               split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/d-optarr.txt; \
    wanted /tests/baseline/optops.txt > /tmp/s-optopsk.txt; \
    wanted /tests/baseline/optarr.txt > /tmp/s-optarr.txt; \
    LC_ALL=C comm -12 /tmp/d-optarr.txt /tmp/s-optopsk.txt > /tmp/d-optarr-in.txt; \
    if ! cmp -s /tmp/d-optarr-in.txt /tmp/s-optarr.txt; then \
        echo "baseline/optarr.txt is not the Java 9 arrivals inside step 06's optops.txt:" >&2; \
        diff /tmp/d-optarr-in.txt /tmp/s-optarr.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-optarr.txt)" = "${g6a}"; \
    test "$(grep -c . /tmp/s-optopsk.txt)" = "${g6d}"; \
    # THE MISSING THIRD IS NAMED. optops.txt's three buckets all lost their \
    # isPresent branch -- optop.ispresent is 0 above -- and exactly one of them \
    # arrived at nothing B-13.6 can see. Asserting WHICH one, rather than just \
    # that there is one, is what stops this file quietly becoming a tolerance: \
    # if a future re-measure made that bucket arrive, this line fails and the \
    # denominator gets raised deliberately instead of drifting. \
    test "$(LC_ALL=C comm -23 /tmp/s-optopsk.txt /tmp/d-optarr.txt | grep -c .)" = 1; \
    LC_ALL=C comm -23 /tmp/s-optopsk.txt /tmp/d-optarr.txt \
      | grep -qE '^impl-base/src/main/java/.*/ProtocolRegistry\.java#findDefaultProtocol$'; \
    # ...and the bucket that arrived but was never asked for is named too. \
    test "$(LC_ALL=C comm -23 /tmp/d-optarr.txt /tmp/s-optopsk.txt | grep -c .)" = 1; \
    LC_ALL=C comm -23 /tmp/d-optarr.txt /tmp/s-optopsk.txt \
      | grep -qE '^impl-base/src/main/java/.*/ToolingDeploymentFormatter\.java#formatNode$'; \
    # --- newinstance.txt, step 06's, IN THE ARRIVED STATE --------------------- \
    fbucketise newinstance form=legacy > /tmp/d-nileg.txt; \
    fbucketise newinstance form=ctor   > /tmp/d-nictor.txt; \
    setpart /tests/baseline/newinstance.txt legacy > /tmp/s-nileg.txt; \
    setpart /tests/baseline/newinstance.txt ctor   > /tmp/s-nictor.txt; \
    test "$(grep -c . /tmp/s-nileg.txt)" = "${g2l}"; \
    test "$(grep -c . /tmp/s-nictor.txt)" = 11; \
    test "$(grep -vc '^#' /tests/baseline/newinstance.txt)" = "${g2f}"; \
    test "$(grep -c . /tmp/d-nileg.txt)" = 0; \
    if [ "$(LC_ALL=C comm -23 /tmp/s-nileg.txt /tmp/d-nictor.txt | grep -c .)" != 0 ]; then \
        echo "baseline/newinstance.txt: a legacy bucket has no Class.getDeclaredConstructor in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/s-nileg.txt /tmp/d-nictor.txt >&2; exit 1; \
    fi; \
    cat /tmp/s-nileg.txt /tmp/s-nictor.txt | LC_ALL=C sort -u > /tmp/s-niall.txt; \
    if ! cmp -s /tmp/s-niall.txt /tmp/d-nictor.txt; then \
        echo "baseline/newinstance.txt's 13 buckets are not this tree's 13 form=ctor buckets:" >&2; \
        diff /tmp/s-niall.txt /tmp/d-nictor.txt >&2 || true; exit 1; \
    fi; \
    if grep -v '/src/main/java/' /tmp/s-nileg.txt; then \
        echo "baseline/newinstance.txt: a legacy site is not under src/main" >&2; exit 1; \
    fi; \
    # --- accessible.txt, step 06's, IN THE ARRIVED STATE ---------------------- \
    fbucketise accessible call=isAccessible  > /tmp/d-guard.txt; \
    fbucketise accessible call=setAccessible > /tmp/d-setacc.txt; \
    setpart /tests/baseline/accessible.txt guard  > /tmp/s-guard.txt; \
    setpart /tests/baseline/accessible.txt setacc > /tmp/s-setacc.txt; \
    test "$(grep -c . /tmp/s-guard.txt)" = "${g3g}"; \
    test "$(grep -c . /tmp/s-setacc.txt)" = "${g3s}"; \
    test "$(grep -c . /tmp/d-guard.txt)" = 0; \
    if ! cmp -s /tmp/d-setacc.txt /tmp/s-setacc.txt; then \
        echo "baseline/accessible.txt: the setacc list is not call=setAccessible in this tree:" >&2; \
        diff /tmp/d-setacc.txt /tmp/s-setacc.txt >&2 || true; exit 1; \
    fi; \
    # THE SUBSET CLAIM, which is what made C-3 satisfiable at step 06 and is \
    # what makes B-13.3 a hold rather than a second copy of the same question. \
    # Every guard bucket is a setacc bucket, so the 12 that had to lose a guard \
    # are 12 of the 14 that must still be calling setAccessible, and the gate \
    # reads both halves off the same sites. \
    if [ "$(LC_ALL=C comm -23 /tmp/s-guard.txt /tmp/s-setacc.txt | grep -c .)" != 0 ]; then \
        echo "baseline/accessible.txt: a guard bucket holds no setAccessible call:" >&2; \
        LC_ALL=C comm -23 /tmp/s-guard.txt /tmp/s-setacc.txt >&2; exit 1; \
    fi; \
    test "$(( g3s - g3g ))" = 2; \
    # --- views.txt held and DERIVED, builds.txt migrated ---------------------- \
    sed -n 's/^legacycoll\[[0-9]*\]=//p' ${bsrc} \
      | awk '$0 ~ /src\/main/ && $0 ~ /kind=view/ && $0 ~ /arg=field/ { \
               split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/d-views.txt; \
    sed -n 's/^legacycoll\[[0-9]*\]=//p' ${bsrc} \
      | awk '$0 ~ /src\/main/ && ($0 ~ /kind=build/ || $0 ~ /arg=local/) { \
               split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/d-builds.txt; \
    grep -v '^#' /tests/baseline/views.txt  | grep . | LC_ALL=C sort -u > /tmp/s-views.txt; \
    grep -v '^#' /tests/baseline/builds.txt | grep . | LC_ALL=C sort -u > /tmp/s-builds.txt; \
    if ! cmp -s /tmp/d-views.txt /tmp/s-views.txt; then \
        echo "baseline/views.txt is not the src/main kind=view arg=field buckets:" >&2; \
        diff /tmp/d-views.txt /tmp/s-views.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-views.txt)" = "${g5v}"; \
    test "$(grep -c . /tmp/s-builds.txt)" = "${g5b}"; \
    # builds.txt's seven are GONE as constructions and PRESENT as factory calls. \
    # Step 06 converted them; this is the state B-13.5 holds, and it is the \
    # check that could not be made at step 06 because the tree it shipped still \
    # held them. \
    if [ "$(LC_ALL=C comm -12 /tmp/s-builds.txt /tmp/d-builds.txt | grep -c .)" != 0 ]; then \
        echo "baseline/builds.txt names a bucket that STILL constructs by hand:" >&2; \
        LC_ALL=C comm -12 /tmp/s-builds.txt /tmp/d-builds.txt >&2; exit 1; \
    fi; \
    bucketise factory > /tmp/d-factory.txt; \
    if ! cmp -s /tmp/d-factory.txt /tmp/s-builds.txt; then \
        echo "baseline/builds.txt is not this tree's List/Set/Map factory buckets:" >&2; \
        diff /tmp/d-factory.txt /tmp/s-builds.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/d-factory.txt)" = "${fac}"; \
    # DISJOINT, asserted. B-13.5 holds one list converted and the other left \
    # alone; a bucket in both would be held in two contradictory states at once. \
    if [ "$(LC_ALL=C comm -12 /tmp/s-views.txt /tmp/s-builds.txt | grep -c .)" != 0 ]; then \
        echo "a bucket is in BOTH views.txt and builds.txt:" >&2; \
        LC_ALL=C comm -12 /tmp/s-views.txt /tmp/s-builds.txt >&2; exit 1; \
    fi; \
    # ONE FILE IS ON BOTH LISTS, under two different methods, and it is still \
    # the clearest statement of why the split is by bucket and not by file: \
    # DeploymentScenario#getDeployments wraps a field and was left alone, \
    # DeploymentScenario#getStartupDeploymentsFor wrapped a local the method \
    # built two lines earlier and was converted. A file-scoped rule would have \
    # had to pick one of them, and this tree is the proof that it did not: the \
    # first is still a view here and the second is now a factory call. \
    grep -qE '^spi/src/main/java/.*/DeploymentScenario\.java#getDeployments$' /tmp/s-views.txt; \
    grep -qE '^spi/src/main/java/.*/DeploymentScenario\.java#getStartupDeploymentsFor$' /tmp/s-builds.txt; \
    grep -qE '^spi/src/main/java/.*/DeploymentScenario\.java#getStartupDeploymentsFor$' /tmp/d-factory.txt; \
    grep -qE '^spi/src/main/java/.*/ServiceLoader\.java#getProviders$' /tmp/s-views.txt; \
    sed 's/#.*//' /tmp/s-views.txt  | LC_ALL=C sort -u > /tmp/s-viewf.txt; \
    sed 's/#.*//' /tmp/s-builds.txt | LC_ALL=C sort -u > /tmp/s-buildf.txt; \
    test "$(LC_ALL=C comm -12 /tmp/s-viewf.txt /tmp/s-buildf.txt | grep -c .)" = 1; \
    test "$(LC_ALL=C comm -12 /tmp/s-viewf.txt /tmp/s-buildf.txt | grep -c 'DeploymentScenario\.java$')" = 1; \
    test "$(cat /tmp/s-viewf.txt /tmp/s-buildf.txt | LC_ALL=C sort -u | grep -c .)" = 10; \
    # --- optops.txt, step 06's, IN THE ARRIVED STATE -------------------------- \
    grep -v '^#' /tests/baseline/optops.txt | grep . | LC_ALL=C sort > /tmp/s-optops.txt; \
    test "$(grep -c . /tmp/s-optops.txt)" = "${g6d}"; \
    test "$(awk '{ sub(/^sites=/, "", $2); s += $2 } END { print s + 0 }' /tmp/s-optops.txt)" = 6; \
    test "$(awk '$2 != "sites=1"' /tmp/s-optops.txt | grep -c .)" = 2; \
    fbucketise optop isPresent use > /tmp/d-ispresent.txt; \
    test "$(grep -c . /tmp/d-ispresent.txt)" = 0; \
    # --- boxedctor.txt, step 06's, IN THE ARRIVED STATE ----------------------- \
    grep -v '^#' /tests/baseline/boxedctor.txt | grep . | LC_ALL=C sort > /tmp/s-boxed.txt; \
    test "$(grep -c . /tmp/s-boxed.txt)" = "${g4b}"; \
    test "${bxs}" = 0; \
    # The per-type census, which is the second instrument on this population: \
    # javac's own -Xlint:deprecation at --release 10 counted 6 Integer, 2 Double, \
    # 2 Boolean, 1 Float and 1 Byte over the same 322 sources at step 06 (NOTES \
    # 11.6.4). Two tools, no coordination, and the split is asserted here \
    # against the FROZEN FILE rather than against the tree, because the tree no \
    # longer holds one of them -- which is the point. \
    test "$(awk '$2 == "java.lang.Integer"' /tmp/s-boxed.txt | grep -c .)" = 6; \
    test "$(awk '$2 == "java.lang.Double"'  /tmp/s-boxed.txt | grep -c .)" = 2; \
    test "$(awk '$2 == "java.lang.Boolean"' /tmp/s-boxed.txt | grep -c .)" = 2; \
    test "$(awk '$2 == "java.lang.Float"'   /tmp/s-boxed.txt | grep -c .)" = 1; \
    test "$(awk '$1 ~ /src\/main/' /tmp/s-boxed.txt | grep -c .)" = 1; \
    test "$(awk '{ split($1, a, ":"); m = ""; \
                   for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                   if (m != "") print a[1] "#" m }' /tmp/s-boxed.txt \
              | LC_ALL=C sort -u | grep -c .)" = 6; \
    test "$(awk '$0 ~ / in=-$/' /tmp/s-boxed.txt | grep -c .)" = 1; \
    # --- varbuckets.txt, step 06's, IN THE ARRIVED STATE ---------------------- \
    grep -v '^#' /tests/baseline/varbuckets.txt | grep . | LC_ALL=C sort > /tmp/s-var.txt; \
    test "$(grep -c . /tmp/s-var.txt)" = "${g7c}"; \
    if awk '{ t = ""; i2 = ""; \
              for (i = 2; i <= NF; i++) { \
                 if (substr($i, 1, 5) == "type=") t = substr($i, 6); \
                 if (substr($i, 1, 5) == "init=") i2 = substr($i, 6) } \
              if (t == "" || t != i2) print }' /tmp/s-var.txt | grep -q .; then \
        echo "baseline/varbuckets.txt: a record's written type is not its initialiser type" >&2; \
        exit 1; \
    fi; \
    # THE ONE EXEMPTION, now readable off the tree rather than only argued for. \
    # 81 candidates, 80 asked for, and the one that was not is the diamond \
    # initialiser at DynamicServiceLoader: converting it would write the type \
    # argument back out and undo a step-03 gain. This tree holds exactly one \
    # ctor=yes varcand record, it is that one, and it is in the frozen file. \
    sed -n 's/^varcand\[[0-9]*\]=//p' ${bsrc} | awk '$0 ~ /ctor=yes/' \
      | LC_ALL=C sort > /tmp/d-varleft.txt; \
    test "$(grep -c . /tmp/d-varleft.txt)" = "${vcn}"; \
    test "${vcn}" = "$(( g7c - g7w ))"; \
    grep -qF 'DynamicServiceLoader.java' /tmp/d-varleft.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-varleft.txt /tmp/s-var.txt | grep -c .)" != 0 ]; then \
        echo "the varcand record this tree still holds is not one of varbuckets.txt's 81:" >&2; \
        LC_ALL=C comm -23 /tmp/d-varleft.txt /tmp/s-var.txt >&2; exit 1; \
    fi; \
    # --- the four inherited step-05 set files, against THIS tree -------------- \
    bucketise retnull > /tmp/b-retnull.txt; \
    bucketise optret  > /tmp/b-optret.txt; \
    bucketise accum   > /tmp/b-accum.txt; \
    bucketise collect > /tmp/b-collect.txt; \
    bucketise millis  > /tmp/b-millis.txt; \
    bucketise jtime   > /tmp/b-jtime.txt; \
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
    # B-12a's 16, IN THE ARRIVED STATE. Step 05 migrated them, this tree is two \
    # rungs downstream of step 05, and B-12 still holds them: so none of the 16 \
    # may hold a retnull record and all 16 must hold an optret one. The gate \
    # computes exactly these two figures at grading time; if they were not \
    # already true here, B-12 would be scoring the tree down for work step 05 \
    # did. \
    if [ "$(LC_ALL=C comm -12 /tmp/key-optbuckets.txt /tmp/b-retnull.txt | grep -c .)" != 0 ]; then \
        echo "baseline/optbuckets.txt names a bucket that STILL returns null in this tree:" >&2; \
        LC_ALL=C comm -12 /tmp/key-optbuckets.txt /tmp/b-retnull.txt >&2; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -23 /tmp/key-optbuckets.txt /tmp/b-optret.txt | grep -c .)" != 0 ]; then \
        echo "baseline/optbuckets.txt names a bucket with no Optional return in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/key-optbuckets.txt /tmp/b-optret.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/b-optret.txt)" = "${topt}"; \
    # ...and none of the 16 is on the published surface B-6 scores as an exact \
    # set. That was step 05's constraint and it is still load-bearing: widening \
    # a published return type from T to Optional<T> is charged by B-6 exactly \
    # as hard as deleting the method, so a set file that reached one would make \
    # B-12a and B-6 unsatisfiable together while each stayed correct on its own. \
    fqcn() { \
        sed -e 's#^.*/src/\(main\|test\)/java/##' -e 's#\.java$##' -e 's#/#.#g'; \
    }; \
    sed 's/ .*//' /tmp/set-optbuckets.txt | while read -r b; do \
        c=$(echo "${b}" | sed 's/#.*//' | fqcn); \
        m=$(echo "${b}" | sed 's/^[^#]*#//'); \
        if grep -F " ${c} :: " /tests/baseline/api.txt | grep -qE "[ >]${m}\("; then \
            echo "baseline/optbuckets.txt: ${c}#${m} is PUBLISHED; B-6 would charge the widening" >&2; \
            exit 1; \
        fi; \
    done; \
    if grep -vE '^[a-z][^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* [A-Za-z][A-Za-z0-9_.<>?, ]*$' \
        /tmp/set-optbuckets.txt; then \
        echo "baseline/optbuckets.txt: a line is not <bucket> <original return type>" >&2; exit 1; \
    fi; \
    # B-12b's 23, IN THE ARRIVED STATE, and the one accum bucket left over is \
    # the src/test site step 05 deliberately did not ask about. \
    if [ "$(LC_ALL=C comm -12 /tmp/key-sinks.txt /tmp/b-accum.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sinks.txt names a bucket that STILL accumulates by hand:" >&2; \
        LC_ALL=C comm -12 /tmp/key-sinks.txt /tmp/b-accum.txt >&2; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/b-collect.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sinks.txt names a bucket with no Collectors call in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/b-collect.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/b-accum.txt)" = 1; \
    test "$(grep -c . /tmp/b-collect.txt)" = "${tsnk}"; \
    if grep -v '/src/test/java/' /tmp/b-accum.txt; then \
        echo "the one surviving accumulate bucket is not the src/test one" >&2; exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* (toList|toSet|toCollection) [A-Za-z][A-Za-z0-9_]*$' \
        /tmp/set-sinks.txt; then \
        echo "baseline/sinks.txt: a line is not <bucket> <factory> <constructed type>" >&2; exit 1; \
    fi; \
    test "$(awk '$2 == "toList" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 18; \
    test "$(awk '$2 == "toSet" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 4; \
    test "$(awk '$2 == "toCollection" { n++ } END { print n + 0 }' /tmp/set-sinks.txt)" = 1; \
    grep -qE '^[^ ]*DynamicServiceLoader\.java#createInstances toCollection LinkedHashSet$' /tmp/set-sinks.txt; \
    # ...and the collectors this tree actually reached agree with the file, per \
    # bucket. toCollection is accepted where toList or toSet is wanted, which is \
    # the scorer's own rule, and this tree uses it at 7 of the 23 -- one fewer \
    # than step 06 was handed, because step 06's own C-5 turned \
    # MethodParameterInjectionPoint#getAnnotations into a Set.of call and the \
    # toCollection that had been in it went with the construction. \
    sed -n 's/^collect\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; to = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 3) == "to=") to = substr($i, 4); \
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4) } \
               if (m != "" && to != "") print f[1] "#" m, to }' \
      | LC_ALL=C sort -u > /tmp/b-collect-to.txt; \
    awk 'FILENAME == ARGV[1] { if ($0 ~ /^#/ || $0 == "") next; want[$1] = $2; next } \
         { got[$1 SUBSEP $2] = 1 } \
         END { bad = 0; \
               for (b in want) { w = want[b]; \
                  ok = ((b SUBSEP w) in got); \
                  if (!ok && (w == "toList" || w == "toSet")) ok = ((b SUBSEP "toCollection") in got); \
                  if (!ok) { print "    sinks.txt wants " w " at " b " and this tree has none" > "/dev/stderr"; bad++ } } \
               exit (bad > 0) }' \
        /tests/baseline/sinks.txt /tmp/b-collect-to.txt; \
    test "$(awk '$2 == "toCollection"' /tmp/b-collect-to.txt | grep -c .)" = 7; \
    # B-12c's 3, IN THE ARRIVED STATE: each class's equals bucket holds an \
    # Objects.equals and its hashCode bucket an Objects.hash, which is exactly \
    # the pair the gate's awk looks for. objects.size is 6 and these are the 6. \
    sed -n 's/^objects\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m, $2 }' \
      | LC_ALL=C sort -u > /tmp/b-objects.txt; \
    test "$(grep -c . /tmp/b-objects.txt)" = "${obj}"; \
    while read -r pth cls; do \
        if [ "$(basename "${pth}")" != "${cls}.java" ]; then \
            echo "baseline/valuetypes.txt: ${pth} does not declare ${cls}" >&2; exit 1; \
        fi; \
        grep -qxF "${pth}#equals equals" /tmp/b-objects.txt \
          || { echo "baseline/valuetypes.txt: ${pth} has no Objects.equals in equals()" >&2; exit 1; }; \
        grep -qxF "${pth}#hashCode hash" /tmp/b-objects.txt \
          || { echo "baseline/valuetypes.txt: ${pth} has no Objects.hash in hashCode()" >&2; exit 1; }; \
    done < /tmp/set-valuetypes.txt; \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java [A-Z][A-Za-z0-9_]*$' \
        /tmp/set-valuetypes.txt; then \
        echo "baseline/valuetypes.txt: a line is not a (path, class name) pair" >&2; exit 1; \
    fi; \
    awk '{print $1}' /tmp/set-valuetypes.txt | fqcn | while read -r c; do \
        for m in 'public boolean equals(java.lang.Object);' 'public int hashCode();'; do \
            if ! grep -qF " ${c} :: ${m}" /tests/baseline/api.txt; then \
                echo "baseline/valuetypes.txt: ${c} does not publish ${m}" >&2; exit 1; \
            fi; \
        done; \
    done; \
    # B-12d's 4, IN THE ARRIVED STATE: no millis record anywhere in the tree, \
    # and every one of the four buckets holds a java.time reference. Column 2 \
    # still sums to 5, which is STEP 05'S site count and not this tree's -- the \
    # file records how many calls each bucket held before the migration, which \
    # is what makes "a bucket where BOTH went" a statement with content. \
    test "${mil}" = 0; \
    test "$(grep -c . /tmp/b-millis.txt)" = 0; \
    if [ "$(LC_ALL=C comm -23 /tmp/key-timesites.txt /tmp/b-jtime.txt | grep -c .)" != 0 ]; then \
        echo "baseline/timesites.txt names a bucket with no java.time reference in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/key-timesites.txt /tmp/b-jtime.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/b-jtime.txt)" = "${tts}"; \
    test "$(awk '{ s += $2 } END { print s + 0 }' /tmp/set-timesites.txt)" = 5; \
    if grep -vE '^[a-z][^ ]*/src/main/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* [1-9][0-9]*$' \
        /tmp/set-timesites.txt; then \
        echo "baseline/timesites.txt: a line is not <bucket> <site count>" >&2; exit 1; \
    fi; \
    # --- the three inherited step-04 set files ------------------------------- \
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
    if [ "$(LC_ALL=C comm -23 /tmp/set-collectmethods.txt /tmp/b-streamop.txt | grep -c .)" != 0 ]; then \
        echo "baseline/collectmethods.txt names a bucket with no stream operation in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/set-collectmethods.txt /tmp/b-streamop.txt >&2; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -23 /tmp/set-collectmethods.txt /tmp/b-stream.txt | grep -c .)" != 0 ]; then \
        echo "baseline/collectmethods.txt names a bucket with no stream producer in this tree:" >&2; \
        LC_ALL=C comm -23 /tmp/set-collectmethods.txt /tmp/b-stream.txt >&2; exit 1; \
    fi; \
    # THE CO-EXISTENCE CLAIM, carried forward: B-12b's 23 buckets are a subset \
    # of B-11's 24, so the two gates read the same sites from opposite ends and \
    # there is exactly one B-11 bucket B-12b does not touch. \
    if [ "$(LC_ALL=C comm -23 /tmp/key-sinks.txt /tmp/set-collectmethods.txt | grep -c .)" != 0 ]; then \
        echo "a B-12b sink bucket is not one of B-11's collect buckets:" >&2; \
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
    for k in nileg nictor guard setacc views builds toarray optarr optopsk; do \
        if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]*$' "/tmp/s-${k}.txt"; then \
            echo "set file ${k}: a key is not a <path>#<method> bucket" >&2; exit 1; \
        fi; \
        if grep -qF '#-' "/tmp/s-${k}.txt"; then \
            echo "set file ${k}: a bucket with no enclosing method can never be reached" >&2; exit 1; \
        fi; \
    done; \
    for k in nsmove nshold; do \
        if grep -vE '^javax\.[a-z]+\|[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java#([A-Za-z_$<][A-Za-z0-9_$>]*|-)$' "/tmp/s-${k}.txt"; then \
            echo "baseline/${k}.txt: a line is not <javax package>|<path>#<method-or-dash>" >&2; exit 1; \
        fi; \
    done; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java$' /tmp/set-lambdafiles.txt; then \
        echo "baseline/lambdafiles.txt: a line is not a source path" >&2; exit 1; \
    fi; \
    if grep -vE '^[a-z][^ ]*/src/(main|test)/java/[^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]*$' \
        /tmp/set-collectmethods.txt; then \
        echo "baseline/collectmethods.txt: a line is not a <path>#<method> bucket" >&2; exit 1; \
    fi; \
    # EVERY PATH IN EVERY SET FILE RESOLVES TO A CLASS THIS TREE COMPILES. \
    # This is the check the regexes above cannot make and the counts cannot make \
    # either: a path that had drifted -- a file renamed upstream, a record \
    # mis-parsed, a list hand-edited once -- still LOOKS like a source path and \
    # still counts as one line, and it would move a gate's aim without moving \
    # any total in this image. baseline/classnames.txt is the 322 top-level \
    # FQCNs the inherited sources compile to, frozen at step 01 and already \
    # depended on by B-4 and B-5, so this ties all nineteen set files to a \
    # population three other gates would fail on first. \
    test "$(grep -c . /tests/baseline/classnames.txt)" = "$(sed -n 's/^files=//p' ${bsrc} | head -1)"; \
    { sed 's/#.*//' /tmp/key-optbuckets.txt; \
      sed 's/#.*//' /tmp/key-sinks.txt; \
      sed 's/#.*//' /tmp/key-timesites.txt; \
      awk '{print $1}' /tmp/set-valuetypes.txt; \
      cat /tmp/set-lambdafiles.txt; \
      awk '{print $1}' /tmp/set-samifaces.txt; \
      sed 's/#.*//' /tmp/set-collectmethods.txt; \
      sed 's/#.*//' /tmp/s-nileg.txt; \
      sed 's/#.*//' /tmp/s-nictor.txt; \
      sed 's/#.*//' /tmp/s-guard.txt; \
      sed 's/#.*//' /tmp/s-setacc.txt; \
      sed 's/#.*//' /tmp/s-views.txt; \
      sed 's/#.*//' /tmp/s-builds.txt; \
      sed 's/#.*//' /tmp/s-optopsk.txt; \
      sed 's/#.*//' /tmp/s-optarr.txt; \
      sed 's/#.*//' /tmp/s-toarray.txt; \
      cut -d'|' -f2 /tmp/s-nsmove.txt | sed 's/#.*//'; \
      cut -d'|' -f2 /tmp/s-nshold.txt | sed 's/#.*//'; \
      awk '{print $1}' /tmp/s-httpmove.txt | sed 's/:.*//'; \
      awk '{print $1}' /tmp/s-httphold.txt | sed 's/:.*//'; \
      awk '{print $1}' /tmp/s-boxed.txt | sed 's/:.*//'; \
      awk '{print $1}' /tmp/s-var.txt   | sed 's/:.*//'; } \
      | LC_ALL=C sort -u | fqcn | LC_ALL=C sort -u > /tmp/set-fqcn.txt; \
    LC_ALL=C sort -u /tests/baseline/classnames.txt > /tmp/b-classnames.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/set-fqcn.txt /tmp/b-classnames.txt | grep -c .)" != 0 ]; then \
        echo "a baseline set file names a path that compiles to no class in classnames.txt:" >&2; \
        LC_ALL=C comm -23 /tmp/set-fqcn.txt /tmp/b-classnames.txt >&2; exit 1; \
    fi; \
    # 114 distinct classes over the nineteen files, out of the 322 this tree \
    # compiles. The set files between them name a third of the repository and \
    # every one of those names resolves; asserted exactly, because a set file \
    # that gained or lost a FILE is a change of aim even when its line count is \
    # unchanged. \
    test "$(grep -c . /tmp/set-fqcn.txt)" = 114; \
    test "$(grep -c . /tmp/set-fqcn.txt)" -lt "$(grep -c . /tests/baseline/classnames.txt)"; \
    # streamfiles.txt is staged and must NOT be read: B-11's stream sub-gate is \
    # method-scoped, and the file-scoped form was step 04's review finding. \
    test -s /tests/baseline/streamfiles.txt; \
    if grep -qF 'baseline/streamfiles.txt' /tests/test.sh; then \
        echo "test.sh reads streamfiles.txt: B-11's stream question has gone back to file scope" >&2; \
        exit 1; \
    fi; \
    # Step 06's own pair must not have been staged under this step's names. If \
    # both ever landed, the one a gate read would be whichever the staging loop \
    # copied last, and every number above would be checked against the wrong \
    # file while still agreeing with itself. \
    test ! -e /tests/baseline/source-step06.txt; \
    test ! -e /tests/baseline/target-step06.txt; \
    test "$(ls /tests/baseline | grep -c '^source')" = 1; \
    test "$(ls /tests/baseline | grep -c '^target')" = 1; \
    rm -f /tmp/b-retnull.txt /tmp/b-optret.txt /tmp/b-accum.txt /tmp/b-collect.txt \
          /tmp/b-millis.txt /tmp/b-jtime.txt /tmp/b-stream.txt /tmp/b-streamop.txt \
          /tmp/b-objects.txt /tmp/b-collect-to.txt /tmp/b-lampaths.txt /tmp/b-sam.txt \
          /tmp/b-classnames.txt /tmp/set-fqcn.txt \
          /tmp/set-optbuckets.txt /tmp/set-sinks.txt /tmp/set-valuetypes.txt \
          /tmp/set-timesites.txt /tmp/set-lambdafiles.txt /tmp/set-samifaces.txt \
          /tmp/set-collectmethods.txt /tmp/key-optbuckets.txt /tmp/key-sinks.txt \
          /tmp/key-valuetypes.txt /tmp/key-timesites.txt \
          /tmp/d-nileg.txt /tmp/d-nictor.txt /tmp/s-nileg.txt /tmp/s-nictor.txt \
          /tmp/s-niall.txt /tmp/d-guard.txt /tmp/d-setacc.txt /tmp/s-guard.txt \
          /tmp/s-setacc.txt /tmp/d-views.txt /tmp/d-builds.txt /tmp/s-views.txt \
          /tmp/s-builds.txt /tmp/d-factory.txt /tmp/s-optops.txt /tmp/d-ispresent.txt \
          /tmp/s-boxed.txt /tmp/s-var.txt /tmp/d-varleft.txt \
          /tmp/s-viewf.txt /tmp/s-buildf.txt \
          /tmp/d-nsmove.txt /tmp/s-nsmove.txt /tmp/d-nshold.txt /tmp/s-nshold.txt \
          /tmp/d-nspkg.txt /tmp/s-nspkg.txt \
          /tmp/d-tasized.txt /tmp/s-toarray.txt /tmp/d-tacount.txt /tmp/s-tacount.txt \
          /tmp/d-httpmove.txt /tmp/d-httphold.txt /tmp/s-httpmove.txt /tmp/s-httphold.txt \
          /tmp/d-optarr.txt /tmp/d-optarr-in.txt /tmp/s-optarr.txt /tmp/s-optopsk.txt; \
    test "$(grep -c '^[a-z]' /tests/baseline/nioids.txt)" = 4; \
    grep '^[a-z]' /tests/baseline/nioids.txt | while read -r id; do \
        if ! grep -qxF "${id}" /tests/assets/ids.txt; then \
            echo "baseline/nioids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; \
            exit 1; \
        fi; \
    done; \
    echo "step 07: the source, target, level, nioids and nineteen set-file baselines agree with each other"

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
# TWO FILES AND NOT ONE, as at the rung below. Every gate from step 05 up scores
# over a frozen set of <file>#<method> BUCKETS rather than over a count, and on
# a one-file tree the file half of every key is constant: a gate that dropped
# the path entirely would read exactly the same, and every `.files` counter
# would read 1 whatever distinctFiles() did. Lib.java also carries the near
# misses that need a NAME without an ELEMENT, and this rung adds six of its own
# to step 05's three: a wrapper class called Integer, a newInstance() in two
# arities, a Slot with setAccessible/isAccessible/canAccess on it, a Bag with
# static of() and copyOf(), and a Collections that is not java.util's.
#
# EVERY BLOCK IS ASSERTED THREE WAYS, which is where this rung's version differs
# from step 05's. Step 05 needed two -- a total, and the specific records that
# make the total mean what the gate reads it as -- because a bucket count is
# satisfiable by moving code. At this rung each record ALSO carries a field the
# total cannot express and the gate reads it: form=legacy against form=ctor,
# where the method name is the same word in both; recv=<Owner>, which is the
# resolved owner and not the static type of the receiver; kind=view against
# kind=build with arg= beside it, which is what lets C-5 convert the builds and
# hold the views; args=<n>, which is all that tells Map.of from Map.ofEntries;
# and ctor=yes against ctor=no. So each of the seven record kinds this rung
# introduces -- newinstance, accessible, boxedctor, legacycoll, factory,
# getpackage, var -- is checked as a total, as records, and as the SET of values
# its discriminating field takes across the tree. A counter that hard-wired the
# field passes the first two and fails the third.
#
# THREE PROBES, AND THEY MUST DISAGREE. The scorer calls srcgate three times --
# `10 10`, `8 8`, `def ""` -- and the rehearsal asserts those three spellings
# are still in test.sh before driving them, so a rung that moved its level to 11
# fails here rather than measuring a different instrument quietly. yes/no/yes is
# the required answer. The no is then asserted as an IDENTITY rather than a
# count: six errors, every one of them javac looking for a class called `var`,
# and the six source lines they name equal the six lines the var block records.
# ok8 is a term in B-4, and a tree that failed at 8 for an unrelated reason
# would satisfy ok8=no and would mean nothing. The def probe's report is
# required to be BYTE-IDENTICAL to the -source 10 one, which is the cheap strong
# form of "these counters are properties of the source text and not of the level
# javac was asked for" -- and that is the whole justification for test.sh
# falling back to `srcgate def ""` when ok10 says no.
#
# THE FLATTEN CONTROL IS THREE RUNGS DEEP HERE. Smoke.java is step 05's
# Smoke.java with a section appended and Lib.java is step 05's Lib.java with two
# top-level classes appended, both verbatim, so cutting BOTH at the step-06
# marker must reproduce step 05's OWN frozen figures -- about forty of them,
# measured by a different rung's rehearsal on a different day and asserted here
# rather than re-derived. Cutting Smoke.java at the step-05 marker and dropping
# Lib.java must then reproduce step 04's twenty-six. A counter that drifted
# between any two rungs is a disagreement between two measurements rather than
# two separately plausible numbers. The step-06 cut is also this rung's
# answer-can-be-yes control and it replaces step 05's: the tree that javac
# rejects at -source 8 must be the SAME tree that passes at -source 8 once its
# six var declarations are gone, by the same probe at the same level.
#
# Thirty-one of this step's keys must read 0 on that flattened tree -- not
# "lower", zero. `var.candidates` is deliberately NOT among them: it counts the
# declarations that COULD be written with var, which is a property of ordinary
# code, so the step-05 tree has fifteen of them and the step-03 half alone has
# four. It is asserted at those two figures instead, because that non-zero
# denominator is exactly why C-7 scores a departure against a count frozen at
# authoring time rather than as a ratio of candidates a candidate can move by
# writing an unrelated method.
#
# The -source 1.7 probe survives as a control and nothing else. test.sh no
# longer runs it at any rung above step 04 and `gate.src.ok17` is published
# nowhere, but it is the cheapest statement in the image that the step-03 half
# of this file has not quietly acquired a Java 8 construct -- and if it ever
# does, three rungs' worth of frozen figures above it become claims about a
# different tree.
#
# The classpath expression and srcgate() are lifted out of /tests/test.sh by
# anchor rather than copied, so what runs here is what the scorer runs. A shelf
# change that shortens the scorer's compile classpath fails this layer -- which
# is measured, not hoped for: the check was run against a deliberately shortened
# SRCCP and reported 76 jars where 77 were staged.
#
# And the layer was verified the way the rubrics ask an instrument to be
# verified before a finding is taken from it: authoring/perturb-src06.sh makes
# seven single-edit breakages of the staged tree -- one var declaration removed,
# the legacy newInstance converted, a view over a local turned into a view over
# a call, Lib.java's section marker renamed, test.sh moved to -source 11, the
# answer key staged, the plugin's service registration deleted -- and requires
# each to come back RED on the named assertion rather than on some other one.
# All seven do, and the unperturbed tree is green.
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
    test "$(grep -cE '^ *ok(11|10|def)=\$\(srcgate ' /tests/test.sh)" = 3; \
    # ...and all three by their exact bytes, because the tag and the level are \
    # separate arguments to srcgate(): a probe TAGGED 10 but compiled at 11 \
    # writes its report into the file the scorer then reads as the Java 10 \
    # answer, and every count in family C would be an answer to a different \
    # question. \
    # This rung's level is 11. THE SECOND PROBE IS NOT A REFUSAL HERE, and that \
    # is the one structural difference from every rung below: Java 11 adds \
    # exactly one language construct over Java 10 -- `var` in a lambda \
    # parameter -- and nothing this step asks for is a language feature, so a \
    # correct tree still compiles at -source 10. `srcgate 10 10` is therefore a \
    # CONTROL whose expected answer is yes, and its reading is itself the \
    # datum: an ok10 of no says the candidate reached for a construct nothing \
    # here asked for. The level assertion this rung DOES make is C-1's, read \
    # out of a published class-file header, which a tree that only compiled at \
    # 10 cannot produce. The third probe is the compiler's own default, which \
    # is where test.sh falls back when ok11 reads no. \
    # \
    # fixtures/src-rehearsal.sh lifts SRCCP and srcgate() out of this same file \
    # by anchor and drives these same three calls, and it asserts these same \
    # three spellings before it asserts any number -- so a rename here fails the \
    # rehearsal on the rename rather than three hundred lines later on an \
    # unexplained zero. \
    grep -qxF 'ok11=$(srcgate 11 11)' /tests/test.sh; \
    grep -qxF '   ok10=$(srcgate 10 10)' /tests/test.sh; \
    grep -qxF '   okdef=$(srcgate def "")' /tests/test.sh; \
    # The frozen baseline files, each read from /tests and not from the tree. \
    # bv() and tv() name their paths as literals for exactly that reason. \
    # \
    # Thirty-four stage and thirty are read. The four that are not are \
    # asserted absent as a positive claim rather than left to be noticed, \
    # because a set file that is staged and silently unread is the shape a \
    # gate-that-does-not-gate takes: \
    # \
    #   streamfiles.txt  B-11's stream third is method-scoped on purpose, and a \
    #                    file-scoped set answers a weaker question about the \
    #                    same tree \
    #   junit5.txt       step 02's own family C retired at step 03; B-8 and B-9 \
    #                    read baseline/compat.txt instead \
    #   varbuckets.txt   B-13.7 is the one leg in this file scored over COUNTS \
    #                    and not over buckets, so the scorer has nothing to \
    #                    divide by it \
    #   http.txt         C-5 is scoped by FILE and divides by two counts -- the \
    #                    14 legacy records inside target.c5.file and the 3 \
    #                    java.net.http types that must arrive there -- so it \
    #                    filters the candidate's OWN records to that path and \
    #                    never needs the recorded list \
    # \
    # The last two stage anyway, and that is deliberate rather than an \
    # oversight. varbuckets.txt enumerates all 81 redundant declarations with \
    # the one exemption marked; http.txt enumerates the 14 records that must \
    # move beside the 3 in another file that must not, which is the distinction \
    # the whole gate turns on. The baseline-consistency layer above checks both \
    # against this tree -- the 81 against its single surviving candidate, the 17 \
    # against its own http records -- and they are the files a human reads \
    # instruction.md's counts against. So their absence from the scorer is \
    # asserted, and a comment explaining each absence is asserted present: an \
    # unread file has to be unread ON PURPOSE, and the purpose has to be \
    # written down next to it. \
    test "$(ls /tests/baseline | grep -c .)" = 34; \
    for unread in streamfiles junit5 varbuckets http; do \
        if grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${unread}.txt"; then \
            echo "baseline/${unread}.txt is staged as deliberately unread and test.sh now reads it" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qF 'baseline/varbuckets.txt' /tests/test.sh; \
    grep -qF 'baseline/http.txt' /tests/test.sh; \
    grep -qF "bv() { sed -n \"s/^\$1=//p\" /tests/baseline/source.txt | head -1; }" /tests/test.sh; \
    grep -qF "tv() { sed -n \"s/^\$1=//p\" /tests/baseline/target.txt | head -1; }" /tests/test.sh; \
    grep -qF '/tests/baseline/nioids.txt' /tests/test.sh; \
    # ...and httpids.txt beside it, which is the same construction one rung \
    # later: a source gate scaled by the recorded behaviours it claims to be \
    # about. Round 1 of this step's static review found C-5 payable by a tree \
    # that deleted the method and kept the shape, at 0.2835 against the \
    # 0.005775 those eighteen ids cost in family A. The list closes it. \
    # \
    # The header's load-bearing claim is that these eighteen are ALL of the \
    # exchange, and it is asserted here rather than believed: exactly fifteen \
    # recordings in this era log an outgoing request, every one of the fifteen \
    # is on the list, and the eighteenth minus fifteenth are the three that \
    # reach the method and fail before a request goes out. A nineteenth id \
    # appearing in the suite, or a sixteenth request-logging recording, fails \
    # the build here instead of quietly shrinking the denominator. \
    grep -qF '/tests/baseline/httpids.txt' /tests/test.sh; \
    test "$(grep -c '^[a-z]' /tests/baseline/httpids.txt)" = 18; \
    for id in $(grep '^[a-z]' /tests/baseline/httpids.txt); do \
        grep -qxF "${id}" /tests/assets/ids.txt || \
            { echo "baseline/httpids.txt names ${id}, which the id list does not" >&2; exit 1; }; \
        test -r "/tests/assets/rec/jdk11-step07/$(echo "${id}" | tr / .).txt" || \
            { echo "baseline/httpids.txt names ${id}, which has no recording" >&2; exit 1; }; \
    done; \
    grep -l 'log\[0\]=request:' /tests/assets/rec/jdk11-step07/*.txt > /tmp/withreq.txt; \
    test "$(grep -c . /tmp/withreq.txt)" = 15; \
    while read -r f; do \
        id=$(basename "${f}" .txt | sed 's/\./\//'); \
        grep -qxF "${id}" /tests/baseline/httpids.txt || \
            { echo "${id} records an outgoing request and is not in baseline/httpids.txt" >&2; exit 1; }; \
    done < /tmp/withreq.txt; \
    rm -f /tmp/withreq.txt; \
    grep -qF 'sed -n '"'"'s/^major=//p'"'"' /tests/baseline/level.txt' /tests/test.sh; \
    # B-13's six frozen set files, each named by the gate that divides by it, \
    # and family C's four, the same way. Every one of these legs could be \
    # rewritten to divide by the matching target.txt scalar instead -- \
    # b13_legacy, b13_guards, b13_builds, tgt_c2_buckets and tgt_c3_buckets are \
    # all in scope at the point of use -- and the result would still be a number \
    # in [0,1] that moves in the right direction. It would also be a COUNT, and \
    # a count is what a tree that migrated six OTHER methods satisfies. The set \
    # is the whole difference between the two readings, so the read of it is \
    # asserted and not assumed. \
    # \
    # B-13.4 is the one leg whose want-set is derived in place rather than by a \
    # helper, because boxedctor.txt is the only one of these files whose records \
    # carry twelve sites in six buckets and the gate needs both numbers; its \
    # derivation is asserted by the filter that performs it. B-13.7 has no set \
    # file at all, and C-5 has one it does not read -- see the two notes above. \
    grep -qF 'setwant /tests/baseline/newinstance.txt legacy' /tests/test.sh; \
    grep -qF 'setwant /tests/baseline/accessible.txt guard' /tests/test.sh; \
    grep -qF "grep -v '^#' /tests/baseline/boxedctor.txt | grep ." /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/builds.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/views.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/optops.txt' /tests/test.sh; \
    # ...and optarr.txt beside it, which is the ninth trap written into the \
    # scorer rather than into a note. B-13.6's ARRIVAL half divides by the \
    # population step 06's blind agent actually reached, not by the population \
    # step 06 asked for: three buckets lost their isPresent branch and only two \
    # of the three arrived at a Java 9 Optional method. A leg that divided both \
    # halves by optops.txt would charge this rung's candidate for the last \
    # rung's miss, and could never read full on any tree this rung can produce. \
    grep -qF 'wantbuckets /tests/baseline/optarr.txt' /tests/test.sh; \
    # Family C's four. nsmove and nshold are read through nswant() rather than \
    # wantbuckets() because their key is pkg|bucket and not bucket: one method \
    # can reference two different javax packages, and a bucket-only key would \
    # let a tree that moved javax.ejb out of a method still holding \
    # javax.annotation count as having moved both -- and would then count the \
    # same bucket as held for C-3, which is the contradiction the pkg in the key \
    # makes unreachable. \
    grep -qF 'nswant /tests/baseline/nsmove.txt' /tests/test.sh; \
    grep -qF 'nswant /tests/baseline/nshold.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/toarray.txt' /tests/test.sh; \
    # ...and the record filters those sets are compared against, asserted by the \
    # bytes of the filter. C-2 reads the javax side and the jakarta side with \
    # two SEPARATE nsbuckets calls over complementary conditions, because one \
    # call over "any annotation package" cannot tell a move from a tree that \
    # never held the import. C-3's condition is the complement of C-2's with \
    # jakarta excluded, so a bucket C-2 has moved cannot also be counted as \
    # held -- drop that arm and a finished migration scores C-3 on the very \
    # buckets it was paid for emptying. \
    grep -qF "nsbuckets jxmove 'p == \"javax.annotation\" || p == \"javax.ejb\"'" /tests/test.sh; \
    grep -qF "nsbuckets jkmove 'p == \"jakarta.annotation\" || p == \"jakarta.ejb\"'" /tests/test.sh; \
    grep -qF "nsbuckets nshold 'p != \"javax.annotation\" && p != \"javax.ejb\" && p !~ /^jakarta" /tests/test.sh; \
    grep -qF 'fbuckets toarray tasized  form=sized' /tests/test.sh; \
    grep -qF 'fbuckets toarray tactorb  form=ctorref' /tests/test.sh; \
    # B-12's four inherited sets, read at the scopes step 05 froze them at. The \
    # same argument one rung older: these are the sets step 05's family C divided \
    # by, and holding step 05's work means holding it against step 05's own \
    # denominators rather than against a fresh count of this tree. \
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
    # B-12b's acceptance rule, which was load-bearing on step 05's reference \
    # migration and is load-bearing on this one's inheritance of it: 7 of the 23 \
    # recorded sinks are collected with toCollection, and a reading that demanded \
    # the exact factory named in sinks.txt would score the step-05 golden 16/23 \
    # and charge the difference to a step-06 candidate who touched none of it. \
    # \
    # The asymmetry is the point and is asserted as one expression rather than \
    # as two greps for the two collector names: a bucket whose recorded want IS \
    # toCollection accepts nothing else, because that one is the LinkedHashSet \
    # site in DynamicServiceLoader#createInstances, and an ordinary toSet there \
    # changes the iteration order its caller reads the services in. \
    grep -qF 'if (!ok && (w == "toList" || w == "toSet")) ok = ((b SUBSEP "toCollection") in got)' /tests/test.sh; \
    # Every leg of B-13 and of family C is a MINIMUM and not a mean, asserted at \
    # the head of each expression, because a mean of the same legs pays half a \
    # gate for half a migration done in the wrong half: a tree that removed the \
    # twelve `isAccessible()` guards and the fourteen `setAccessible` calls with \
    # them has departed perfectly and arrived nowhere, and B-13.3 as a mean pays \
    # it 0.5 for deleting reflection this step is explicitly not asking it to \
    # touch. \
    # \
    # B-13 IS STEP 06'S FAMILY C INHERITED AS A REGRESSION GATE, and its six \
    # legs keep the shapes that rung gave them -- including the two third legs \
    # that are not bucket questions at all: \
    # \
    #   B-13.2  the tree makes 13 `Constructor.newInstance` calls and must still \
    #           make 13. Without that floor the leg is passed by deleting both \
    #           migrated sites, which departs perfectly and arrives nowhere the \
    #           two bucket halves can see -- they are asked of the SAME two \
    #           buckets, so an emptied bucket satisfies the departure half and \
    #           the arrival half is asked of a bucket that no longer exists \
    #   B-13.4  the bucket half says the work stayed done in the six buckets it \
    #           was asked in; the total half says none of the twelve wrapper \
    #           constructions came back somewhere else. `b13_boxed - now_boxed` \
    #           over `b13_boxed` is a whole-tree departure, and is the only half \
    #           of the six legs that cannot be satisfied bucket by bucket \
    grep -qF 'b13_2=$(minf "$(minf "$(clamp01 "$(frac "${b13c2dep}" "${b13_legacy:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${now_nictor}" "${b13_ctorfloor:-0}")")")' /tests/test.sh; \
    grep -qF 'b13_3=$(minf "$(minf "$(clamp01 "$(frac "${b13c3dep}" "${b13_guards:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${now_setacc}" "${b13_setacc:-0}")")")' /tests/test.sh; \
    grep -qF 'b13_4=$(minf "$(clamp01 "$(frac "${b13c4dep}" "${b13c4n}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "$(( ${b13_boxed:-0} - now_boxed ))" "${b13_boxed:-0}")")")' /tests/test.sh; \
    grep -qF 'b13_5=$(minf "$(minf "$(clamp01 "$(frac "${b13c5dep}" "${b13_builds:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${b13c5held}" "${b13_views:-0}")")")' /tests/test.sh; \
    grep -qF 'b13_6=$(minf "$(clamp01 "$(frac "${b13c6dep}" "${b13_optdep:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${b13c6arr}" "${b13_optarr:-0}")")")' /tests/test.sh; \
    # B-13.5's departure population is a UNION of two record shapes and not one, \
    # and that union is the leg's whole difficulty. The seven buckets asked for \
    # were hand-rolled CONSTRUCTIONS (kind=build); the seven buckets held are \
    # unmodifiable VIEWS over a field (kind=view arg=field); and a view over a \
    # LOCAL is a construction wearing a view's spelling -- the local is filled \
    # line by line and then wrapped on the way out, which is exactly the shape \
    # `List.of(...)` replaced. So `kind=view arg=local` joins the departure set \
    # and `kind=view arg=field` does not. \
    # \
    # Asserted by the three fbuckets calls and by the sort -u that joins two of \
    # them, because dropping the arg= qualifier from either one silently moves \
    # seven buckets between "must have gone" and "must still be here" and the \
    # leg still reads a number. \
    grep -qF 'fbuckets legacycoll lcbuild0 kind=build' /tests/test.sh; \
    grep -qF 'fbuckets legacycoll lcviewl  kind=view arg=local' /tests/test.sh; \
    grep -qF 'fbuckets legacycoll lcviewf  kind=view arg=field' /tests/test.sh; \
    grep -qF 'LC_ALL=C sort -u "${WORK}/buk-lcbuild0.txt" "${WORK}/buk-lcviewl.txt"' /tests/test.sh; \
    grep -qF 'b13c5held=$(hit lcviewf)' /tests/test.sh; \
    # B-13.7 is the one leg scored over COUNTS rather than buckets, and its two \
    # halves read opposite ends of the same population: departures are \
    # `candidatenew` holding at 1 rather than climbing back towards 81, arrivals \
    # are `var` holding at 80. Both are asserted, because a B-13.7 that kept \
    # only the arrival half is passed by adding eighty NEW `var` declarations \
    # and leaving all 81 redundant ones in place. \
    grep -qF 'b13c7dep=$(clamp01 "$(frac "$(( ${b13_cand:-0} - now_varnew ))" "${b13_want:-0}")")' /tests/test.sh; \
    grep -qF 'b13c7arr=$(clamp01 "$(frac "${now_var}" "${b13_want:-0}")")' /tests/test.sh; \
    grep -qF 'b13_7=$(minf "${b13c7dep}" "${b13c7arr}")' /tests/test.sh; \
    # THIS RUNG'S OWN FAMILY C, four gates and four shapes, and the shapes are \
    # not interchangeable: \
    # \
    #   C-2  four legs under two nested minima -- buckets departed, buckets \
    #        arrived, references departed, references arrived. Both pairs are \
    #        needed because six buckets hold 22 references: a tree that rewrote \
    #        one import per bucket empties six buckets on the javax side while \
    #        leaving sixteen references behind, and a tree that rewrote 22 \
    #        references inside three buckets moves the reference count without \
    #        finishing a single method \
    #   C-3  a HOLD, and the only gate in the family that is not a min at its \
    #        head: it is the min of held-buckets and held-references SCALED BY \
    #        C-2, because holding javax.naming is worth nothing on a tree that \
    #        moved nothing. The unscaled value is published beside it so the \
    #        scaling is visible in the facts and not only in the arithmetic \
    #   C-4  the same four-leg shape as C-2, over buckets and over sites, \
    #        because five buckets hold eight sized toArray calls and three of \
    #        the five hold more than one \
    #   C-5  a min of departure and arrival, multiplied by 0.75 + 0.25*red: a \
    #        candidate that moves every record and never names a redirect policy \
    #        has changed the behaviour its caller depended on, and loses a \
    #        quarter of the gate rather than all of it or none of it -- and the \
    #        whole of it scaled by the fraction of baseline/httpids.txt that \
    #        still matches, because every term before that one is SHAPE and a \
    #        deleted method body has the shape of a finished migration \
    grep -qF 'c2=$(minf "$(minf "$(clamp01 "$(frac "${c2dep}" "${tgt_c2_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(minf "$(clamp01 "$(frac "$(( ${tgt_c2_refs:-0} - c2jx ))" "${tgt_c2_refs:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${c2jk}" "${tgt_c2_refs:-0}")")")")' /tests/test.sh; \
    grep -qF 'c3raw=$(minf "$(clamp01 "$(frac "${c3held}" "${tgt_c3_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${c3refs}" "${tgt_c3_refs:-0}")")")' /tests/test.sh; \
    grep -qF 'c3=$(awk -v a="${c3raw}" -v b="${c2}" '"'"'BEGIN { printf "%.6f", a * b }'"'"')' /tests/test.sh; \
    grep -qF 'fact "gate.c3.unscaled=${c3raw}"' /tests/test.sh; \
    grep -qF 'c4=$(minf "$(minf "$(clamp01 "$(frac "${c4dep}" "${tgt_c4_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(minf "$(clamp01 "$(frac "$(( ${tgt_c4_sized:-0} - nw_tasized ))" "${tgt_c4_sized:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${nw_tactor}" "${tgt_c4_sized:-0}")")")")' /tests/test.sh; \
    grep -qF 'c5=$(awk -v d="$(clamp01 "$(frac "$(( ${tgt_c5_legacy:-0} - c5leg ))" "${tgt_c5_legacy:-0}")")"' /tests/test.sh; \
    grep -qF -- '-v a="$(clamp01 "$(frac "${c5arr}" "${tgt_c5_types:-0}")")"' /tests/test.sh; \
    grep -qF -- '-v r="${c5redok}" -v x="${c5exec}" \' /tests/test.sh; \
    grep -qF -- ''"'"'BEGIN { m = (d < a) ? d : a; printf "%.6f", x * m * (0.75 + 0.25 * r) }'"'"')' /tests/test.sh; \
    # The scaling term itself: the list read, the two zeroes spelled apart, and \
    # the fail-closed arm. A summary that cannot be read is family A not having \
    # run, and B-10 already settled what that means -- a proviso that cannot be \
    # checked is not satisfied. The arm is asserted because the tempting edit \
    # is the other one: leave the shape standing when there is no evidence, and \
    # a tree whose build fails collects 0.2835 for a file it never compiled. \
    grep -qF 'c5exec=$(clamp01 "$(frac "$(( c5execn - c5execbad ))" "${c5execn}")")' /tests/test.sh; \
    grep -qF 'c5execbad="${c5execn}"' /tests/test.sh; \
    grep -qF 'fact "gate.c5.exec.want=${c5execn}"' /tests/test.sh; \
    grep -qF 'fact "gate.c5.exec.broken=${c5execbad}"' /tests/test.sh; \
    grep -qF 'fact "gate.c5.exec=${c5exec}"' /tests/test.sh; \
    # And the redirect proviso, which is the OTHER run-time factor and the one \
    # that is easy to delete by accident, because deleting it moves no fixture \
    # in the table -- every source-tree fixture keeps all eighteen recordings, \
    # so the quarter is paid either way there. It moves BEHAVRED alone. The \
    # reason it exists: gate.c5.redirects reads that a followRedirects policy \
    # is NAMED, not which one, and HttpClient's default is NEVER. Naming NEVER \
    # therefore changes nothing at run time and, unprovisoed, paid a quarter of \
    # C-5 more than naming nothing at all. Pin the id as well as the shape -- a \
    # proviso pointed at the wrong recording is a proviso that never fires. \
    grep -qF 'c5redid=servlet/exec-redirect' /tests/test.sh; \
    grep -qF 'c5redok=1' /tests/test.sh; \
    grep -qF 'fact "gate.c5.redirects.honoured=${c5redok}"' /tests/test.sh; \
    grep -qF 'elif [ ! -r "${summary}" ]; then' /tests/test.sh; \
    grep -qF 'elif grep -qE "^behavior\.(differ|missing)\[[0-9]+\]=${c5redid}\$" "${summary}"; then' /tests/test.sh; \
    grep -qxF 'servlet/exec-redirect' /tests/baseline/httpids.txt; \
    test -r /tests/assets/rec/jdk11-step07/servlet.exec-redirect.txt; \
    # C-5 reads its records out of the candidate's own scan filtered to \
    # target.c5.file, which is why http.txt stages unread, and it publishes the \
    # CDI stub's three legacy records as a REPORTED count that no leg divides \
    # by. A gate that had asked for the tree-wide legacy count to reach zero \
    # would score the finished migration at zero too, because those three are an \
    # anonymous URLConnection subclass standing in as an injection fixture in \
    # another module, and this rung is not asking for it to be rewritten. \
    grep -qF 'c5stub=$(( ${nw_hlegref:-0} + ${nw_hlegcall:-0} - c5leg ))' /tests/test.sh; \
    # B-12e, step 05's must-flip inherited as this rung's regression floor, reads \
    # the PUBLISHED jars with a second instrument: the scorer's own unzip into the \
    # scorer's own scratch directory, then a binary grep for the constant-pool \
    # name. Four legs, all asserted -- the population it reads, the instrument it \
    # reads it with, the four package names, and the four floors it divides by. \
    # \
    # The floors come from target.txt through tv() and are not literals here, \
    # because a floor written into test.sh is a floor that cannot be re-measured \
    # when the reference migration moves. They are republished under target.b12.* \
    # names at this rung rather than kept at step 05's target.c5.*.floor \
    # spellings, and the step-05 spellings are asserted gone below: a gate that \
    # changed families and kept its old key reads correctly today and tells a \
    # reviewer this step grades java.time. \
    grep -qF 'done < "${WORK}/modulejars.txt"' /tests/test.sh; \
    grep -qF 'unzip -qo "${j}" '"'"'*.class'"'"' -d "${cpdir}/m${njar}"' /tests/test.sh; \
    grep -qF 'xargs -0 -r grep -lZ --binary-files=binary -F "$1"' /tests/test.sh; \
    for pkg in 'java/util/Optional' 'java/util/stream/Collectors' \
               'java/util/Objects' 'java/time/'; do \
        grep -qF "cpcount '${pkg}'" /tests/test.sh; \
    done; \
    test "$(grep -cE '^b12_(opt|col|obj|jt)_floor=\$\(tv ' /tests/test.sh)" = 4; \
    test "$(grep -cE 'target..b12..(optional|collectors|objects|jtime)..floor' /tests/test.sh)" = 4; \
    # ...and each of those four legs is MULTIPLIED by the source leg it \
    # corroborates before the mean is taken, which is the one line step 05's \
    # static review round 1 (R5) was about. A constant-pool leg that paid on its \
    # own reach paid for a constant pool, and a constant pool is fifteen dummy \
    # references away from any tree at all: the step-04 input plus those \
    # references scored 0.553750 at step 05, over migration_prompt item 6's 0.5, \
    # with nothing migrated. \
    # \
    # Inherited unchanged, and the reason it must stay is stronger here than it \
    # was there: at step 05 this gate was a must-flip and its scaling protected \
    # the step's own floor, whereas here it is a REGRESSION gate worth 0.12 of \
    # family B that the unmigrated input collects in full. An unscaled version \
    # would hand this rung's candidate 0.12 of family B for fifteen references, \
    # on top of a floor that already has only 0.025 of room. \
    # \
    # Anchored on the bytes of the expression and on the four pairings by name, \
    # because the failure this forecloses is not a missing gate, it is a \
    # `b12_lib=` that quietly goes back to averaging four unscaled legs and still \
    # prints four legs, still publishes the four gate.cp counts, and still reads \
    # 1.000000 on the golden -- where every source leg is 1.0 and the scaling is \
    # invisible. \
    grep -qF 'BEGIN { printf "%.6f", (a * p + b * q + c * r + d * s) / 4 }' /tests/test.sh; \
    grep -qF ' -v p="${b12_opt:-0}" -v q="${b12_col:-0}" -v r="${b12_obj:-0}" -v s="${b12_jt:-0}"' /tests/test.sh; \
    grep -qF "printf '    corroborated: %s*%s %s*%s %s*%s %s*%s -> %s" /tests/test.sh; \
    if grep -qF 'BEGIN { printf "%.6f", (a + b + c + d) / 4 }' /tests/test.sh; then \
        echo "B-12e averages its four legs unscaled again -- see step 05's R5" >&2; exit 1; \
    fi; \
    # ...and B-12e runs OUTSIDE the `if [ -z "${measured}" ]` guard that B-12a to \
    # B-12d sit inside. That is not a stylistic detail. Those four are one \
    # instrument's opinion of a compile the SCORER ran over whatever sources it \
    # found; this one asks a different question of a different artifact, and a \
    # tree whose sources the gate compiler cannot digest still publishes jars, \
    # and those jars still either name java/util/Optional or do not. Inside the \
    # guard the regression floor would read 0 for a reason that has nothing to do \
    # with whether the library ships. \
    # \
    # Asserted by indentation, because indentation is exactly what the guard does \
    # to every line inside it. \
    grep -qxF '# ---------------------------------------------------------------- B-12e' /tests/test.sh; \
    grep -qxF 'echo "--- B-12e: the published jars reference the Java 8 library"' /tests/test.sh; \
    # Family C's own guard has the same shape with the roles swapped, and this \
    # rung is where that shape earns its keep. C-2 to C-7 sit INSIDE the measured \
    # guard and are indented three; C-1 -- the must-flip -- is read OUTSIDE it and \
    # is not, because the class-file level is a fact about the published jars and \
    # a tree the gate compiler declined still published them. C-1 is then PAID \
    # after the guard closes, from the six source legs the guard produced, which \
    # is why there are two C-1 banners and only one of them is a reading. \
    # \
    # Six indented banners and two unindented ones, counted rather than listed: a \
    # gate that drifts into or out of the guard changes its indentation before it \
    # changes anything else, and nothing else about it changes at all. \
    test "$(grep -cE '^   # -+ C-[2-5]$' /tests/test.sh)" = 4; \
    grep -qxF '# ---------------------------------------------------------------- C-1' /tests/test.sh; \
    grep -qxF '# ---------------------------------------------------------------- C-1, paid' /tests/test.sh; \
    test "$(grep -cE '^   fact "gate.src.penalty=' /tests/test.sh)" = 1; \
    # The arithmetic of every composite score in the file, by its bytes. \
    grep -qF '0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e' /tests/test.sh; \
    grep -qF '0.20*l + 0.20*s + 0.60*c' /tests/test.sh; \
    grep -qF '0.4*d + 0.3*r + 0.3*n' /tests/test.sh; \
    grep -qF '(0.30*a + 0.175*b) / 0.475' /tests/test.sh; \
    grep -qxF 'mig="${c_score}"' /tests/test.sh; \
    grep -qF 'printf "%.6f", t1 * t2' /tests/test.sh; \
    grep -qF 'r * ((0.07*b + 0.17*c + 0.12*d + 0.54*e) / 0.90)' /tests/test.sh; \
    grep -qF 'p * (0.10*a + 0.07*b + 0.17*c + 0.12*d + 0.54*e)' /tests/test.sh; \
    # B-13's own composite, which is step 06's family C with its C-1 removed and \
    # the remaining six renormalised by dividing by 0.90 rather than rescaled. \
    # Dividing keeps step 06's proportions between the six exactly; rescaling \
    # would have had to pick new literals and the proportions would be a claim \
    # instead of an identity. \
    grep -qF '(0.12*b + 0.18*c + 0.03*d + 0.26*e + 0.23*f + 0.08*g) / 0.90' /tests/test.sh; \
    # ...and B-12 is NOT multiplied by the anti-gaming penalty at this rung, \
    # which is the one arithmetic change the inheritance made. At step 05 that \
    # multiplier stood in front of this expression, because the raw types and \
    # suppressions it counts were the ones step 05's candidate added. Here they \
    # are the ones step 06's candidate adds, so the multiplier moved down into \
    # family C where that candidate's work is scored. Leaving it here would \
    # charge a step-06 candidate's raw type twice and would put a penalty on a \
    # regression floor, where its only effect is to make holding step 05's work \
    # cost something. \
    if grep -qF 'p * (0.30*a + 0.35*b' /tests/test.sh; then \
        echo "B-12 still multiplies step 05's family C by this step's anti-gaming penalty" >&2; \
        exit 1; \
    fi; \
    # The weights each family DIVIDES BY, summed. test.sh claims three of these \
    # sums in prose -- family B's twelve to 1.000000, family C's seven to 1.00, \
    # and C-1's corroborating six to 0.90 -- and a claim in prose that nothing \
    # checks is the same object as no claim. B-12's five are summed too: they are \
    # step 05's own internal weights carried forward unrenormalised, and the \
    # whole argument for not renormalising them is that they already sum to one. \
    # \
    # C-1's divisor is the one worth stating twice. The six source legs carry \
    # 0.90 of family C between them, so the mean has to divide by 0.90; divide by \
    # 1.00 instead and a tree that finished every source leg corroborates at 0.9 \
    # and loses a tenth of its must-flip for completing the step. Divide by the \
    # wrong number the other way and C-1 pays more than its reach. \
    for sum in "0.046464 0.023232 0.092928 0.092928 0.077440 0.092928 0.046464 0.077440 0.046464 0.085184 0.092928 0.105600 0.120000:1.000000:family B" \
               "0.10 0.07 0.17 0.12 0.54:1.000000:family C" \
               "0.30 0.35 0.10 0.10 0.15:1.000000:B-12" \
               "0.12 0.18 0.03 0.26 0.23 0.08:0.900000:B-13's six, before its divisor" \
               "0.07 0.17 0.12 0.54:0.900000:C-1's corroborating legs"; do \
        terms="${sum%%:*}"; rest="${sum#*:}"; want="${rest%%:*}"; who="${rest#*:}"; \
        got=$(printf '%s\n' ${terms} | awk '{ t += $1 } END { printf "%.6f", t }'); \
        if [ "${got}" != "${want}" ]; then \
            echo "${who}: weights sum to ${got}, want ${want}" >&2; \
            exit 1; \
        fi; \
    done; \
    # ...and those literal sums are only arithmetic unless the terms are the \
    # terms the scorer uses, so each list is pinned back to the expression it was \
    # copied from. The family loop below does this positionally for family B and \
    # family C; C-1's six and B-12's five have no printed rows to compare \
    # against, so they are pinned here by the whole expression instead. \
    grep -qF '0.07*b + 0.17*c + 0.12*d + 0.54*e' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when step 03's C-6 \
    # was added the arithmetic was changed and the report was not -- so every run \
    # printed five gates at the old weights while scoring six at the new ones. A \
    # number that is right, described wrongly, is the one defect a reviewer cannot \
    # see. Every weight in this rung's two families is a fresh literal: family C \
    # was re-weighted from scratch because this step's migration has seven gates \
    # and no two of them cost the same, and family B grew a TWELFTH gate, which \
    # moved all eleven others -- every step-05 weight was scaled by 0.88 to make \
    # room for B-12's 0.12, and the scaling is exact at four places, which is why \
    # these rows print four decimals where every rung below printed two. \
    # \
    # B-10's and B-11's rows are listed here by name for the same reason B-10's \
    # was at the rung below: a step that adds a gate and leaves the rows below it \
    # alone is the shape this whole check exists for, and these are the rows that \
    # moved. \
    grep -qF "printf '    B10 java 7 held      %s  x0.085184" /tests/test.sh; \
    grep -qF "printf '    B11 java 8 lang held %s  x0.092928" /tests/test.sh; \
    grep -qF "printf '    B12 java 8 lib held  %s  x0.105600" /tests/test.sh; \
    grep -qF "printf '    B13 java 9-10 held   %s  x0.120000" /tests/test.sh; \
    grep -qF "printf '    C1 major %-3s            %s  x0.10  (reach %s)" /tests/test.sh; \
    grep -qF "printf '    C2 jakarta arrived      %s  x0.07" /tests/test.sh; \
    grep -qF "printf '    C3 javax held           %s  x0.17  (scaled by C2)" /tests/test.sh; \
    grep -qF "printf '    C4 toArray T[]::new     %s  x0.12" /tests/test.sh; \
    grep -qF "printf '    C5 java.net.http        %s  x0.54" /tests/test.sh; \
    # C-1's row prints its RAW reading beside its paid one, and that is the only \
    # place a reader of the report can see the two apart. Asserted as a distinct \
    # claim from the row's weight, because the scaling introduced at this rung is \
    # invisible on the golden -- where the reach is 1.0 and the corroborated \
    # value is 1.0 -- and a report that dropped the parenthesis would look \
    # identical on every tree that finished the step. \
    grep -qF '(reach %s)' /tests/test.sh; \
    grep -qF 'fact "gate.c1.reach=' /tests/test.sh; \
    # The multiplier is printed too, and deliberately NOT as an `x0.NN` row: it \
    # is not a weighted term, and a report that listed it as one would be \
    # describing a different composition than the one being scored. \
    grep -qF "printf '    anti-gaming penalty     %s  (multiplies family C)" /tests/test.sh; \
    nrep=$(grep -cE "^printf '    C[0-9][0-9]* " /tests/test.sh); \
    nfact=$(grep -cE '^ *fact "C[0-9][0-9]*\.' /tests/test.sh); \
    if [ "${nrep}" != "${nfact}" ]; then \
        echo "the report prints ${nrep} family-C gates and the scorer publishes ${nfact}" >&2; \
        exit 1; \
    fi; \
    # The fact-line pattern is anchored at `^ *fact` and not at `^fact`, which at \
    # this rung is load-bearing rather than tolerant: C-1's fact is published \
    # outside the measured guard and is unindented, C-2 to C-7's are published \
    # inside it and are indented three. A `^fact` anchor counts 1 against 7 \
    # printed rows and reports a drift that is not there. \
    # \
    # And the general form of the same check, for every family at once, because \
    # asserting ten literals by hand closes one instance of the hole and leaves \
    # the class of it open -- family B's twelve printfs and family B's awk are the \
    # same pair of spellings, and they agree today only by habit. For each family \
    # this pulls the weights out of the report rows in order and out of that \
    # family's awk expression in order, and requires the two sequences to be \
    # equal. A family added later is covered by adding its letter to the list. \
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B passed ten gates two rungs below, and a one-digit \
    # pattern reads nine rows against twelve coefficients and calls the \
    # difference a mismatch -- or worse, would have silently skipped B10, B11 and \
    # B12 on both sides if the coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    # \
    # The row pattern needs one space before %s and this rung's family B has rows \
    # whose labels are a character longer than the column, so the columns no \
    # longer line up and the pattern must not depend on them. It matches \
    # ` .*  x0.NN` and not a fixed column for exactly that. C-1's row is the \
    # hardest case in the file and is covered by the same pattern: its label \
    # carries a `%-3s` conversion and its tail carries a `(reach %s)`, so the \
    # weight is neither the first nor the last field on the line. \
    # \
    # Family C's anchor is `0\.54\*e[)]$` and not `0\.54\*e`, because two \
    # expressions in this file end in that term: the family-C weighting, which \
    # closes its parenthesis at end of line, and C-1's corroborating mean, which \
    # continues ` / 0.90)`. The anchor picks the first and the count below \
    # asserts that it picked exactly one. \
    for fam in B:13:'0\.120000\*b13$' C:5:'0\.54\*e[)]$'; do \
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
    # Every gate name this rung publishes, by name. C-1's carries the wanted \
    # major as a parameter rather than a literal -- `C1.major${WANT_MAJOR}` -- so \
    # it is matched as a prefix; every other one is exact. \
    for g in C2.jakarta C3.javax-held C4.toarray C5.httpclient \
             B8.jupiter-held B9.junit4-held B10.java7-held B11.java8lang-held \
             B12a.optional B12b.collectors B12c.objects B12d.jtime \
             B12e.ships-java8-library B12.java8-library \
             B13.2.newinstance B13.3.isaccessible B13.4.boxedctor \
             B13.5.factories B13.6.optional9 B13.7.var B13.java9-10; do \
        grep -qF "fact \"${g}=" /tests/test.sh; \
    done; \
    # B-13's six legs keep step 06's NUMBERING -- 2 to 7, with no 1 -- and the \
    # gap is asserted rather than tidied away. Renumbering them 1 to 6 would \
    # break the one property that makes B-13 auditable: every leg is the step-06 \
    # leg of the same number, read against the same frozen set file, and a \
    # reviewer can lay the two files side by side. The missing B13.1 is step \
    # 06's C-1, which asserted major 54 and which this rung's C-1 replaces with \
    # 55; a B-13 that grew a leg 1 back would be the ninth trap walking in \
    # through the front door. \
    if grep -qF 'fact "B13.1.' /tests/test.sh; then \
        echo "B-13 has a leg 1: step 06's C-1 must not be carried forward" >&2; \
        exit 1; \
    fi; \
    grep -qF 'fact "C1.major${WANT_MAJOR}=' /tests/test.sh; \
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
    # And step 04's, one rung fresher. \
    # \
    # `0.60*c` is deliberately NOT in this list even though it was step 04's C-3 \
    # weight, because B-11 -- which holds step 04's work at this rung -- \
    # renormalises step 04's own three weights and lands on 0.20 / 0.20 / 0.60. \
    # The string is step 04's, still means step 04's third, and is asserted \
    # PRESENT above. `0.15*a` is banned and `0.15*e` is required, which is the \
    # same coefficient moved to a different gate: the ban list has to distinguish \
    # a weight from its position, and these two are the pair that shows it does. \
    for dead in 'C1.lambdas' 'C2.functionalinterface' 'C3.streams' \
                'C4.requires-java8' 'requires-java8' '0.15*a'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 04's retired family-C gate reads are still in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # And step 05's, which is the list actually at risk: this file was COPIED \
    # from step 05's, so every name here was present in it an edit ago and a \
    # missed rename leaves a gate that reads the right number under the wrong \
    # question. Step 05's family C did not retire -- it became B-12 -- so each of \
    # these names has a live successor, which is exactly what makes the old \
    # spelling survivable and therefore worth banning. \
    # \
    # `target.c5.*.floor` is banned through its four variable names rather than \
    # through the key text, because test.sh's own comment at the head of the \
    # target block names the step-05 spellings in prose to explain what was \
    # renamed and why, and a sweep that banned the key would ban the explanation. \
    # `0.30*a`, `0.35*b`, `0.10*c`, `0.10*d` and `0.15*e` are NOT banned: those \
    # are B-12's five internal weights, carried forward deliberately unchanged, \
    # and are asserted present above. \
    # \
    # `c1=$(minf` is in the list and is the sharpest entry in it. Step 05's C-1 \
    # was a MINIMUM of two bucket legs; this rung's C-1 is a product of a reach \
    # and a corroborating mean. Both spellings assign to `c1`, both produce a \
    # number in [0,1], and both read 1.000000 on the golden -- so the only thing \
    # that distinguishes a C-1 that silently reverted is the operator. \
    for dead in 'C1.optional' 'C2.collectors' 'C3.objects' 'C4.jtime' \
                'C5.ships-java8-library' 'C5 ships the library' \
                'c5_opt_floor=' 'c5_col_floor=' 'c5_obj_floor=' 'c5_jt_floor=' \
                'ok18=' 'ok17=' 'c1=$(minf'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 05's family C is still spelled as family C in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # And step 06's, which is the list actually at risk THIS time: this file was \
    # COPIED from step 06's, so every name below was present in it an edit ago \
    # and a missed rename leaves a gate that reads the right number under the \
    # wrong question. Step 06's family C did not retire either -- it became B-13 \
    # -- so each of these names has a live successor under a B13 prefix, which is \
    # exactly what makes the old spelling survivable and therefore worth banning. \
    # \
    # `target.c6.methods` and `target.c2..c7` are NOT banned through the key text: \
    # test.sh names both in prose, at the head of the target block and at \
    # target.b13.opt.arrived, to explain what was renamed and why step 06's \
    # arrival denominator of 3 became 2. A sweep that banned the key would ban \
    # the explanation. The variable names are banned instead, and a variable name \
    # cannot appear in prose by accident. \
    # \
    # `${c6}` and `${c7}` are the sharpest two entries. Step 06's report printed \
    # seven family-C rows and this one prints five; a leftover row would reference \
    # a variable that family C no longer assigns, and under `set -u` that is a \
    # verifier that dies between computing the reward and reporting it. \
    # \
    # The last three are the compile probes rather than a gate. Step 06 measured \
    # at `srcgate 10 10` with `srcgate 8 8` as its published-unscored second \
    # channel; this rung measures at 11 with 10 as the second channel. A file \
    # that still ran `srcgate 8 8` would be compiling this tree at a level three \
    # rungs below the one it is being graded at, and the counters it produced \
    # would be the counters of a different language. \
    for dead in 'C2.newinstance' 'C3.isaccessible' 'C4.boxedctor' \
                'C5.factories' 'C6.optional9' 'C7.var' \
                'tgt_c2_legacy' 'tgt_c2_ctorfloor' 'tgt_c3_guards' \
                'tgt_c3_setacc' 'tgt_c4_boxed' 'tgt_c5_builds' 'tgt_c5_views' \
                'tgt_c6_methods' 'tgt_c7_cand' 'tgt_c7_want' \
                '${c6}' '${c7}' 'C6 optional 9' 'C7 var' \
                'gate.src.ok8' 'ok8=' 'srcgate 8 8'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 06's family C is still spelled as family C in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # The order of the whole scorer, asserted rather than assumed. Three of these \
    # are hoists carried up from the rungs below: the three srcgate compiles sit \
    # ABOVE family B so that B-10, B-11, B-12 and family C can all read their \
    # counters, and the three held gates sit between family B's own gates and \
    # family C. Under `set -u` an un-hoist is not a wrong number, it is a dead \
    # verifier -- which is the good case. The bad case is a hoist that lands after \
    # the reads and leaves the variables at their earlier values, and only the \
    # line numbers can tell those apart. \
    # \
    # New at this rung, and the structural half of the prose above: C-1 is read \
    # BEFORE family C's measured guard opens and PAID after it closes, with the \
    # anti-gaming penalty -- the last statement inside the guard -- between \
    # them. The guard is anchored by its opening `if [ -z` at column zero: six \
    # lines in the file carry that spelling, all six are the same guard around a \
    # different block, and awk keeps the last, which is family C's. B-13's is the \
    # fifth and sits immediately above it, which is why the count is stated here \
    # rather than left implicit -- a B-13 that lost its guard would move family \
    # C's anchor without changing the regex. A C-1 reading that slid inside it \
    # would read 0 on every tree \
    # whose sources the gate compiler declined, and a C-1 payment that slid inside \
    # it would multiply the must-flip by six legs that had not been computed yet. \
    awk '/^# -+ the source AST, compiled once$/{s=NR} \
         /^# -+ family B$/{b=NR} /^# -+ B-10: step 03, held$/{t=NR} \
         /^# -+ B-11: step 04, held$/{u=NR} /^# -+ B-12: step 05, held$/{w=NR} \
         /^# -+ B-12e$/{b12e=NR} /^# =+ B-13$/{b13=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^if \[ -z /{g=NR} \
         /^ *fact "gate.src.penalty=/{pen=NR} \
         /^# -+ C-1$/{c1r=NR} /^# -+ C-1, paid$/{c1p=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} /^fact "B11.java8lang-held=/{fb11=NR} \
         /^fact "B12.java8-library=/{fb12=NR} /^fact "B13.java9-10=/{fb13=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || u == 0 || w == 0 || b12e == 0 \
                   || b13 == 0 || fb13 == 0 \
                   || c == 0 || fc == 0 || g == 0 || pen == 0 || c1r == 0 \
                   || c1p == 0 || v == 0 || b9 == 0 || fb10 == 0 || fb11 == 0 \
                   || fb12 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " B11=" u " B12=" w " B12e=" b12e " B13=" b13 \
                        " B13fact=" fb13 " Cbanner=" c " C=" fc \
                        " guard=" g " penalty=" pen " C1read=" c1r " C1paid=" c1p \
                        " B-7=" v " B-9=" b9 " B10fact=" fb10 " B11fact=" fb11 \
                        " B12fact=" fb12 " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so the held gates cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < u && u < fb11 && fb11 < w \
                     && w < b12e && b12e < fb12 && fb12 < b13 && b13 < fb13 \
                     && fb13 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, B-11, B-12 (jars last), B-13, family C, its score, the report"; exit 1 } \
               if (!(c < c1r && c1r < g)) { \
                  print "C-1 must be read before family C opens its measured guard"; exit 1 } \
               if (!(g < pen && pen < c1p && c1p < fc)) { \
                  print "the penalty must be the last line inside the guard and C-1 must be paid after it"; exit 1 } \
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
    echo "step 06: B-6 reads the same public surface through an anonymous class, a lambda and a method reference"; \
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
    if javap -protected -classpath b6 IDeep | grep -qE 'public (default )?IBase self\(\);'; then \
        echo "CONTROL FAILED: a class-file 50 interface already carries the bridge" >&2; \
        exit 1; \
    fi; \
    javap -protected -classpath b8 IDeep | grep -qE 'public (default )?IBase self\(\);'; \
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
    grep -qE ':: public (default )?IBase self\(\);' /tmp/iface/api-8.txt.dropped; \
    test "$(grep -c . /tmp/iface/api-def.txt)" = 5; \
    test "$(diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep -c '^> ')" = 1; \
    diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep '^> ' \
        | grep -qE ':: public (default )?void authored\(\);'; \
    echo "step 06: B-6 reads the same interface surface at class-file 50 and at 52, and still charges a default method the author wrote"; \
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
# pin it at 55 -- the image's javac is JDK 11 and the smoke sources carry no
# -source/-target or --release -- which is a value neither gate ever credits
# (this rung's own level is 54), so a hard-wired 50 or 52 would fail here. It
# is also the clearest statement in the image that the GRADING JDK and the
# TARGET LEVEL are two different numbers at this step.
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
    grep -qxF 'j5.major=J5Smoke|55'      /tmp/j5s/a.txt; \
    grep -qxF 'j5.major=J5Smoke$1|55'    /tmp/j5s/a.txt; \
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
    grep -qxF 'j5.major=J4Smoke|55'      /tmp/j5s/b.txt; \
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
    grep -qxF 'j5.major=J5Lambda|55'     /tmp/j5s/d.txt; \
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
    echo "step 06: arqgate.Junit5 reads assertThrows at the call site, anonymous Executable or lambda"; \
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
    echo "step 07 verifier image: assertions passed"

# Both directions, because a grant that is too wide is indistinguishable from a
# correct one right up until something reads the answers. Every path 65534 must
# reach is read as 65534, and every path it must not reach is proven unreadable
# through the identical construction -- so the negative arm is a measurement and
# not an assumption about which paths happen to exist.
#
# The file list is the WHOLE of /tests/baseline -- all 28 staged files, not the
# 19 step 05 happened to name. The nine this rung adds (api, artifacts, views,
# builds, newinstance, boxedctor, accessible, optops, varbuckets) were missing
# until the catalogue was counted against `ls baseline`, and varbuckets.txt is
# one of the three files this step stages DELIBERATELY UNREAD -- the one kind of
# baseline whose leak the scorer itself would never notice. /tests/baseline is
# 0700 and the directory arm below already proves 65534 cannot traverse it, so
# this was never a live leak; the per-file arm exists so that a single wrong
# mode cannot become one, and a per-file arm that enumerates 19 of 28 does not
# do that job.
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
                  /tests/baseline/api.txt /tests/baseline/artifacts.txt \
                  /tests/baseline/views.txt /tests/baseline/builds.txt \
                  /tests/baseline/newinstance.txt /tests/baseline/boxedctor.txt \
                  /tests/baseline/accessible.txt /tests/baseline/optops.txt \
                  /tests/baseline/varbuckets.txt \
                  /tests/baseline/nsmove.txt /tests/baseline/nshold.txt \
                  /tests/baseline/toarray.txt /tests/baseline/http.txt \
                  /tests/baseline/optarr.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/gates/build/arqsource.jar \
                  /tests/gates/source-gate.sh /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh \
                  /tests/fixtures/b89-rehearsal.sh /tests/fixtures/c/golden/report.txt \
                  /tests/fixtures/src-rehearsal.sh /tests/fixtures/pv-rehearsal.sh \
                  /tests/fixtures/mkfix07.py /tests/fixtures/c/pinned/report.txt \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Lib.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk11-step07 /tests/baseline \
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
    echo "step 07: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
