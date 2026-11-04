# Verifier image for step 08, built with steps/08-jpms/tests/ as the Docker
# build context: it cannot reach the task root, environment/, or another step,
# so everything grading needs is copied in here. Harbor skips its usual tests
# upload and runs /tests/test.sh from this image.
#
# THE RUNG IS "JPMS READINESS": every published jar declares an
# Automatic-Module-Name, the names are the ones the package tree already
# implies, and the two places that hand-roll a service lookup are replaced by
# java.util.ServiceLoader. Two flips, one era. What this rung does NOT do is
# write module-info.java, and the reason is in the tree rather than in the
# ladder's taste -- see below.
#
# THIS RUNG MOVES NO CLASS-FILE VERSION. Step 07 took the tree to 55 and 55 is
# where it stays; `--release 11` is already in the build file and neither of
# this step's two flips needs a language feature newer than that. B-4 is
# therefore a HOLD, "one major, and not below 55", and it is the only gate in
# the file that still reads baseline/level.txt. A rung whose must-flip is a
# manifest attribute has no business asserting a bytecode level it did not
# ask for, and a rung that silently let 55 slide back to 54 would be handing
# step 07's work back to the candidate to lose.
#
# THE GRADING JDK DOES NOT MOVE EITHER. Steps 06 and 07 graded on JDK 11 and
# so does this one. The era directory names are code eras and not grading
# JDKs, which is the one thing about them that reads backwards: rec/jdk8-step06,
# rec/jdk11-step07 and rec/jdk11-step08 were all replayed on this same JDK 11.
#
# The 935 transcripts under assets/rec are the 2011 probe shelf replayed
# against the candidate's published jars. TWENTY-EIGHT of the 935 move at this
# rung, all of them under the svc/ prefix, and all for one reason inside this
# step's own work: java.util.ServiceLoader is not bug-compatible with the two
# hand-written provider-file parsers it replaces.
#
#   On a MALFORMED file the platform loader is STRICTER. Twenty of the
#   twenty-eight change only the SHAPE of a failure -- ServiceConfigurationError
#   where the hand parser threw ClassNotFoundException or returned an empty
#   list -- and two of them LOSE a provider the hand parser had returned,
#   because the platform loader abandons the whole file at the first bad entry
#   instead of skipping the bad line: svc/sl-mixed-good-and-bad and
#   svc/sl-package-private-class.
#
#   On a WELL-FORMED file it is MORE CORRECT. Six GAIN a provider, because the
#   platform loader accepts what the hand-rolled parsers rejected: surrounding
#   whitespace, an inline comment, a leading space, a trailing comment, and a
#   class named twice in one file. Three of those six go FAILED -> PASSED.
#
# THE ERA IS THE REQUIREMENT, NOT A TOLERANCE. Those twenty-eight files are
# not noise the scorer is being asked to forgive; they are the observable
# difference between a tree that did the work and a tree that kept its own
# parser and wrapped it in a ServiceLoader-shaped method. The era staged here
# is jdk11-step08 -- rec/jdk11-step07 with exactly those twenty-eight files
# replaced, asserted as an equality by rebuild-verifier.sh (935 files, 28
# differing, and each of the 28 named) rather than left as a hope.
#
# AUTOMATIC-MODULE-NAME AND NOT module-info.java. This tree cannot be modularised
# at this rung and the reason is measurable: 152 of the 935 probes go through
# ShrinkWrap, whose descriptor layer resolves implementations by scanning the
# classpath (88 of them through Descriptors.importAs), and a module-info.java
# on the arquillian jars puts a ModuleFinder in the middle of a lookup that was
# written for a flat classpath. The Automatic-Module-Name attribute is the whole
# of the JPMS readiness that this tree can actually carry: it fixes the module
# name so that a downstream consumer's `requires` does not depend on a jar's
# file name, and it changes nothing about how the classes are loaded. Authoring
# `requires` and `exports` goes untested here, deliberately.
#
# THE TWO LOOKUPS THAT MOVE AND THE ELEVEN THAT DO NOT. C-3 scores exactly two
# departures -- impl-base DynamicServiceLoader#load and spi ServiceLoader#reload,
# the two methods that read META-INF/services/ by hand -- and two arrivals at
# java.util.ServiceLoader. Everything else that calls Class.forName or
# loadClass in this tree is a BY-NAME load of a known class, not a service
# lookup, and C-3 holds all eleven of them: a candidate that "modernises" the
# two SecurityActions probes or the JMX runner's test-class load has broken the
# tree, not improved it. The META-INF/services resource that names the
# descriptor implementation stays exactly where it is, for the same reason.
#
# THREE INSTRUMENTS, NOT ONE. The typed AST (arqgate.SourceScan) answers the
# construct questions in B-10 through B-14 and in C-3; the jars the candidate's
# build PUBLISHED are read twice, by arqgate.Scan for B-4's class-file major
# and by arqgate.Modules for the Automatic-Module-Name manifest attribute; and
# javac's own acceptance of the tree (source-gate.sh) is the control on both.
# A manifest attribute is not visible in the AST and a construct is not visible
# in a manifest, so no gate in this file can be satisfied by moving work from
# one instrument's reach into another's.
#
# The class-file level is read by arqgate.Scan out of the jars the candidate's
# own build published -- an artifact, not a source text -- exactly as it was at
# step 07, and the module names are read by arqgate.Modules out of those same
# jars' manifests. A tree that reaches an Automatic-Module-Name by any route,
# including writing it into each project's jar task by hand, still passes,
# because the gate reads the artifact and not the build file.
#
# JDK 11 is also the right compiler for what this rung READS. Every construct
# gate reads a TYPED AST: `Optional` in a javadoc and `Optional` in a return
# type differ by nothing a grep can see, `var` is not a node in the AST at all
# but a type tree at NOPOS, and a ServiceLoader.load(Class) call is a different
# finding from a method of that name on something else. arqgate.SourceScan
# resolves every site through javac's own Elements, against a classpath the
# scorer owns, at the level the step targets. A JDK that could not express the
# level would be answering a different question with the same counter names.
#
# `unzip` is asserted present below rather than assumed, and B-12e is why. That
# gate reads the constant pools of the jars the candidate's build published --
# a different instrument on a different artifact from everything else in the
# file -- and a missing unzip makes it report "the published jars carry no
# class files", which reads as a finding about the tree rather than as a broken
# image. That would be the ladder's sixth trap sprung in the one place where
# nothing else could corroborate it.
#
# What this rung does NOT carry forward is step 06's own source.txt and
# target.txt. Every rung below step 07 staged its predecessor's pair beside its
# own; here they would both land as baseline/source.txt and baseline/target.txt
# and which one a gate read would come down to the order of a staging loop.
# What step 07 needs carried forward is carried instead as target.b14.* keys
# inside step 08's target.txt, and step 07's population files are staged under
# their own names.
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
# under per-step names (fixtures-08/, baseline-08/, assets/STAGED-08.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-08; \
    test -d /tests/baseline-08; \
    test -f /tests/assets/STAGED-08.txt; \
    mv /tests/fixtures-08 /tests/fixtures; \
    mv /tests/baseline-08 /tests/baseline; \
    mv /tests/assets/STAGED-08.txt /tests/assets/STAGED.txt
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
RUN echo step08 > /tests/STEP_ID

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
            *) echo "RULE-6 VIOLATION: step 08 grades on JDK 11 only, found ${v} at ${j}" >&2; exit 1;; \
        esac; \
    done; \
    test "${found}" -gt 0; \
    if command -v mvn    >/dev/null 2>&1; then echo "RULE-6 VIOLATION: Maven is reachable from the scorer" >&2; exit 1; fi; \
    if command -v gradle >/dev/null 2>&1; then echo "RULE-6 VIOLATION: a gradle on PATH would bypass the candidate's wrapper" >&2; exit 1; fi; \
    test ! -e /opt/gradle; \
    javap -version; \
    # B-12e shells out to unzip, and it is a SCORING dependency and not a \
    # build-time one: step 05's held gate unzips the candidate's published jars \
    # to read four package names off their constant pools. Named here because \
    # the failure is silent -- no unzip means 0 classes extracted, which B-12e \
    # prints as "the published jars carry no class files", which is a sentence \
    # about the tree. This rung reads those same jars a second time, through \
    # java.lang.module rather than through unzip: family C's C-1 and C-2 run \
    # gates/build/arqmodules.jar over them. That channel needs no unzip and is \
    # checked by the JVM sweep above, which is why the two are named apart. \
    command -v unzip >/dev/null; \
    unzip -v | head -1

# The payload landed. Counts, not directory existence: a COPY that arrived empty
# and a COPY that arrived whole both leave the directory there. The numbers are
# repeated from assets/STAGED.txt rather than read out of it, so that a staging
# run against the wrong era or the wrong probe-lib pin fails the image build.
RUN set -eux; \
    grep -q '^staged.era=jdk11-step08$' /tests/assets/STAGED.txt; \
    grep -q '^staged.orderdep=1$'      /tests/assets/STAGED.txt; \
    test "$(grep -c '^as_untrusted timeout -s KILL' /tests/runner/score.sh)" = 2; \
    test "$(grep -vc '^#' /tests/baseline/orderdep.txt)" = 1; \
    grep -q '^staged.ids=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.rec=935$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.problib.jars=80$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/assets/ids.txt)" = 935; \
    test "$(find /tests/assets/rec/jdk11-step08 -name '*.txt' | wc -l)" = 935; \
    test ! -d /tests/assets/rec/jdk8-step06; \
    test ! -d /tests/assets/rec/jdk11-step07; \
    test "$(ls /tests/assets/rec | grep -c .)" = 1; \
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
    grep -q '^staged.baseline.files=37$' /tests/assets/STAGED.txt; \
    test "$(ls /tests/baseline | grep -c '\.txt$')" = 37; \
    grep -q '^staged.fixtures=455$'     /tests/assets/STAGED.txt; \
    grep -q '^staged.warm2.deps=6$'     /tests/assets/STAGED.txt; \
    grep -q '^staged.gates=Junit5.java Modules.java Scan.java SelfStop.java SourceScan.java Tests.java $' \
        /tests/assets/STAGED.txt; \
    test -f /tests/gates/src/arqgate/Modules.java; \
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
    test "$(find /tests/baseline -type f | wc -l)" = 37; \
    # 455, up from step 07's 169, and the whole of the increase is family C's \
    # jar shelf. The arithmetic is asserted rather than the total alone, \
    # because a fixture tree that lost a whole configuration and gained \
    # sixteen loose files elsewhere would satisfy a count. \
    # \
    #   8   at the top: the five rehearsals, mkfix.sh, mkfix08.py and the two \
    #       j5 answer files -- j5-pinned.txt and j5-golden.txt -- which is 8 \
    #       once mkfix.sh and mkfix08.py are counted as the two they are. \
    #   9   cl/: step 02's self-stop cases, unchanged since it. \
    #   2   smoke/: Smoke.java and Lib.java, the source-gate rehearsal's tree. \
    # 436   c/: 28 configurations. 27 of them hold twelve published jars and \
    #       four files of their own -- answers.txt, behavior.summary, \
    #       report.txt, why.txt -- and K20 holds the four and NO jars, which \
    #       is what makes it the configuration where nothing resolves. \
    #       27*16 + 4 = 436. \
    test "$(find /tests/fixtures -type f | wc -l)" = 455; \
    test "$(find /tests/fixtures -maxdepth 1 -type f | wc -l)" = 8; \
    test "$(find /tests/fixtures/cl -type f | wc -l)" = 9; \
    test "$(find /tests/fixtures/smoke -type f | wc -l)" = 2; \
    test "$(find /tests/fixtures/c -type f | wc -l)" = 436; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d | wc -l)" = 28; \
    test "$(find /tests/fixtures/c -name '*.jar' | wc -l)" = 324; \
    test "$(find /tests/fixtures/c/K20 -name '*.jar' | wc -l)" = 0; \
    for cfg in $(ls /tests/fixtures/c); do \
        n=$(find "/tests/fixtures/c/${cfg}" -maxdepth 1 -type f | wc -l); \
        if [ "${n}" != 4 ]; then \
            echo "fixtures/c/${cfg} holds ${n} top-level files, not the four every configuration has" >&2; \
            exit 1; \
        fi; \
    done; \
    # And the thirty-seven by NAME, because a count is satisfied by any \
    # thirty-seven files and this rung's recipe does two different things to \
    # reach the number. It ADDS three: modnames.txt and svc.txt are the \
    # populations C-1..C-3 are measured against, and svcids.txt is the subset \
    # of the recordings C-3's execution term is scored over. And it REPLACES \
    # four the rung below already staged -- source.txt, target.txt, level.txt \
    # and api.txt are re-derived here. source.txt because it must be step 07's \
    # OUTPUT measured by the gate that ships here, which is the only tool that \
    # can see an svcload record; target.txt because five keys are new and four \
    # moved; api.txt because the published surface this rung holds is the one \
    # step 07's renames produced; and level.txt because a HOLD needs its own \
    # copy of the number it is holding, not a reference to the rung that \
    # raised it. A replacement is invisible to a count -- thirty-four plus \
    # three is thirty-seven whether or not source.txt is the one this step \
    # meant -- which is why the names are listed and not just tallied. \
    # Fifteen of the thirty-seven are frozen SETS: step 06's seven and step \
    # 07's five, carried forward unchanged and read in the ARRIVED state, plus \
    # modnames and svc. A set that failed to stage does not make the scorer \
    # fail -- wantbuckets() over a missing file yields an empty want, frac() \
    # over an empty denominator yields the scorer's zero-denominator answer, \
    # and the gate pays the tree for a migration nobody checked. \
    for b in accessible api artifacts boxedctor builds classnames \
             collectmethods compat http httpids junit5 lambdafiles level \
             modnames newinstance nioids nshold nsmove optarr optbuckets optops \
             orderdep samifaces sinks source streamfiles svc svcids target \
             testng-cased testresults tests timesites toarray valuetypes \
             varbuckets views; do \
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
             modnames newinstance nioids nshold nsmove optarr optbuckets optops \
             orderdep samifaces sinks source streamfiles svc svcids target \
             testng-cased testresults tests timesites toarray valuetypes \
             varbuckets views | LC_ALL=C sort > /tmp/bwant.txt; \
    if ! cmp -s /tmp/blist.txt /tmp/bwant.txt; then \
        echo "baseline/ is not the thirty-seven files this step stages:" >&2; \
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

# The seven blocks that read the typed AST, on twenty-eight source reports.
#
# fixtures/c-rehearsal.sh lifts the hoisted source-AST block, B-10, B-11, B-12,
# B-13, B-14 and the whole of family C out of the SHIPPED test.sh by their own
# banners, composes them in file order behind a srcgate() that hands them a
# fixture's report instead of compiling anything, and runs them once per
# fixture. Two of the twenty-eight are measurements -- `pinned`, the tree this
# step is handed, which is step 07's blind snapshot, and `golden`, this step's
# own solution -- and twenty-six are stated edits of a measurement.
#
# SEVEN blocks and not six, which is the way this rehearsal differs from step
# 07's. The compiles were hoisted above family B at step 04 so B-10 could read
# them, step 05 put B-11 beside it, step 06 added B-12, step 07 added B-13 and
# this rung adds B-14: the counters are read in one place and spent in SIX
# others. The extraction asserts all seven are disjoint and in that order in
# the file, which is not decoration -- a hoist that silently un-hoisted would
# leave a held gate reading variables that do not exist yet, and under `set -u`
# that is a dead verifier rather than a wrong number. It also asserts srcgate
# is called three times in the source block and not once anywhere else, and
# that no block runs javac, docker or gradlew.
#
# FAMILY C REALLY RUNS java, and that is the one thing this rehearsal does that
# no earlier one did. C-1 and C-2 are arithmetic over what
# `java -cp gates/build/arqmodules.jar arqgate.Modules` reports about the jars
# the candidate's build published, not over a source report, so the rehearsal
# cannot stub it and stay honest. The extraction therefore asserts `java` is
# run EXACTLY ONCE across all seven blocks, on a line naming arqgate.Modules --
# a second invocation anywhere would mean a gate the fixtures do not drive.
#
# ...and the instrument is read before the finding. Before the table runs at
# all, the rehearsal points arqgate.Modules at the golden fixture's own twelve
# jars and requires total=12, declared=12, derivable=11. If that says anything
# else, every C-1 and C-2 figure below is being compared against a broken
# instrument and the twenty-eight failures that follow would all be one fault
# wearing twenty-eight faces.
#
# TWO OF STEP 07's CHANNELS ARE GONE, and their absence is asserted positively
# rather than left to the arithmetic. C-1 no longer reads a class-file major
# out of ${WORK}/scan.txt -- family C has no class-file leg at this rung at all,
# which is the first time that has been true on this ladder -- and no block
# reads WANT_MAJOR. A block that still reached for WANT_MAJOR would read an
# unset variable under `set -u` and take the whole composed run down; one that
# still reached for the scan would find nothing and score a clean, wrong zero.
#
# It asserts NINE figures per fixture and not five: familyC, its three legs
# C1.modules-named, C2.module-names and C3.platform-lookup separately, and
# B10.java7-held, B11.java8lang-held, B12.java8-library, B13.java9-10 and
# B14.java11-jakarta. The three legs are broken out because at this rung the
# total hides which one moved -- K2 and K20 both read familyC 0.450000 and they
# are different trees, one that published twelve jars and named none of them
# and one that published nothing at all. The five held gates hold five closed
# rungs in place, and B-14 is the largest of them: it IS step 07's whole family
# C, renormalised over the four legs that survive, so a drift there is a step
# 07 regression reported at step 08.
#
# All nine are compared AS STRINGS, because the shell's spelling of a gate is
# part of what is being pinned. `b10=0` is a literal a guarded block assigns
# and never reaches; a gate that computed a zero prints `0.000000`. They are
# the same number and not the same fact, and normalising them would lose the
# difference between a gate that scored nothing and one that never ran. There
# are THREE spellings of zero in this file and the rehearsal pins each:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                gate.c3.departed, arrived and held, which are initialised
#                above family C's guard and published below it.
#   `0.000000`   B-12, B-13, B-14 and C-3 are totals computed by an awk that
#                sits OUTSIDE the guard, so a tree nobody could compile gets a
#                number computed from parts that never ran.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so that tree has no penalty fact rather
#                than an unfired `1`. Spelled MISSING and not collapsed into
#                the empty string, so `want MISSING` is an assertion.
#
# THE GUARD IS INVERTED AT THIS RUNG, and that is the structural story the
# rehearsal checks by line number. At step 07 the whole of family C sat inside
# the measured-report guard. Here only C-3 does. C-1 and C-2 read the published
# JARS, which exist whether or not the sources compiled, so they sit ABOVE the
# guard and a tree that fails to compile still gets told what its jars are
# named. C-3 reads the source AST and must sit INSIDE it, because gone() over
# an empty bucket file reads as "both buckets departed" -- the ladder's seventh
# trap, and the reason C-3's three parts are asserted inside the guard while
# the facts they publish are asserted below it.
#
# THE TWO ARTIFACT CHANNELS NOW SHARE ONE FILE. B-12e unzips the published jars
# and reads four package names off the class bytes inside them; C-1 and C-2 run
# the module finder over the same jars. Both read ${WORK}/modulejars.txt, and
# the rehearsal writes exactly that one file per fixture -- so a fixture with
# no jars/ directory leaves it empty and reaches both blocks the way a build
# that published nothing would, through `[ -s ]` and not `[ -f ]` in each. K19,
# K20 and MODPERT are the three rows that keep the channels apart: K19 has
# twelve clean jars naming none of the four packages (B-12 falls, family C does
# not), K20 has no jars at all (`gate.mod=no-module-jars` AND `gate.cp=no-jars`,
# two else-arms no other row executes), and MODPERT deletes one jar and requires
# family C to fall while B-12 holds. A missing unzip would score every fixture's
# B-12e zero, so the rehearsal names that before it starts rather than reporting
# it as twenty-odd arithmetic failures.
#
# What that still costs: nothing here says SourceScan's counts are right, only
# that the arithmetic over them is. The counts are verified separately and by a
# different instrument -- fixtures/src-rehearsal.sh, further down, compiles a
# smoke tree with the plugin attached and asserts every counter and every
# record shape against figures measured first and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files this rung reads -- baseline/modnames.txt, baseline/svc.txt, and step
# 07's six -- so each half of each gate is rehearsed against the buckets it
# will actually be asked about. Fixtures with invented paths would agree with a
# gate pointed at any set at all.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The ones in between are what make
# either of those mean anything, and each is asserted to six decimals rather
# than to a band -- a gate that has drifted lands somewhere, just not here. The
# figures are computed by fixtures/mkfix08.py, an independent implementation of
# the same composition, and written into the rehearsal BY HAND. This image has
# no python3, which makes that separation structural rather than a matter of
# discipline: a rehearsal that computed its own expectations at run time would
# agree with the scorer by construction and assert nothing.
#
# The table is 28 rows of 9 figures, and under those 252 totals sit 307
# assertions on the FACTS behind them, which is what tells apart the fixtures
# that share a total. Row by row, where the row is load-bearing rather than the
# plain half of a gate:
#
#   pinned must score all three family-C legs 0.000000 AND B-10, B-11, B-12,
#      B-13 and B-14 all 1.000000. It is the tree the candidate is handed, and
#      the floor argument at the foot of test.sh rests on the last five: the
#      unmigrated tree collects family B in full, and
#      0.30*(907/935) + 0.70*0.25 = 0.466 is what that comes to against
#      migration_prompt item 6's bar of 0.5.
#   golden must score all nine 1.000000. It is also where the held gates are
#      proved not to punish this step's own work, and where gate.mod.resolved
#      is pinned at 0 ON THE CORRECT ANSWER: arquillian-impl-base ships a
#      META-INF/services entry naming a provider that is not in the module, so
#      ModuleFinder refuses to derive a descriptor for that one jar and the
#      whole configuration fails to resolve. Nothing in family C reads
#      mod.resolved; it is published so a trajectory can see why. The subject
#      of the refusal is asserted by name, not the message, because the message
#      embeds an absolute path from a mktemp directory.
#   K1, K2 and K20 are the three ways to lose C-1 and C-2, and they lose them
#      differently: eleven of twelve jars declaring a name, none declaring one,
#      and no jars published at all. C-2 is SCALED by C-1 -- a tree that has
#      not named its modules cannot be paid for the names being right -- which
#      is why K1 reads C-1 0.916667 and C-2 0.840278 rather than 0.916667 twice.
#   K3 names all twelve and gets ten of them right: C-1 0.833333, C-2 0.694444,
#      familyC 0.859722. The pairwise comm against baseline/modnames.txt is the
#      whole of C-2 AND, since round 1 of this step's review, C-1's third min()
#      term -- so a wrong name is charged in both legs and not only in the
#      second. The two it gets wrong are the two whose module name is NOT the
#      artifact id with dots -- arquillian-impl-base and
#      arquillian-protocol-jmx-osgi. Before the third term this row read C-1
#      1.000000 and familyC 0.941667, and twelve arbitrary legal names on the
#      step-07 tree took the whole 0.20 for reward 0.571016 against a floor of
#      0.466016. K3 and M1 are now a matched pair: each holds two of C-1's three
#      terms at 1.000000 and drops a different third.
#   M1 is the jar the finder cannot read at all: declared and correctly named
#      but not derivable, so C-1 is min(12/12, 10/11, 12/12) = 0.909091. C-1
#      counts all three channels because a manifest attribute is a claim, the
#      finder's answer is the reading, and the baseline's twelve names are
#      whether the claim was the right one.
#   M2, M3, M4, M5 and M6 are C-3's three halves failed one at a time: the two
#      buckets that must move to java.util.ServiceLoader half-moved (M2), one
#      of the eleven by-name loads moved when it must not have been (M3),
#      neither arrival present (M4), and the behaviour scaling at its two ends
#      -- all sixty-nine recorded service lookups broken (M5) and nine of them
#      (M6). C-3 is min(departed, arrived, held) TIMES the recorded behaviour,
#      and those five rows are what say the min is a min and the scaling is a
#      fraction of its own list.
#   K4, K5 and K6 are B-14's C-2 leg: a bucket that did not move, a bucket that
#      moved with a javax import left standing beside it, and every bucket
#      holding a jakarta reference with the javax ones not gone. Step 07's
#      findings, still findings.
#   K7 and K8 are B-14's C-3 leg: one of the 43 held (package, bucket) pairs
#      lost, and the broad-rename row -- javax.management renamed to
#      jakarta.management, which is what a package-scoped rename recipe does to
#      this tree and which no amount of step-08 work should be allowed to hide.
#   K9 and K10 are B-14's C-4 leg pulled apart, K11 through K13 its C-5, and
#      K14, K15 and K16 the three penalty arms one each -- including the one
#      the other two cannot see, `Class[]::new` where `Class<?>[]::new` was
#      meant, which toarray.raw catches and newgeneric.raw cannot. All three
#      score every leg of family C in full and are charged anyway.
#   K17 is the instrument admitting it has no reading, and it is the row the
#      three spellings exist for. Nothing compiled, so B-10 and B-11 publish
#      the literal `0` and so do gate.c3.departed, arrived and held; B-12,
#      B-13, B-14 and C-3 publish 0.000000; and every gate.b14.* fact and the
#      penalty publish NOTHING. C-1 and C-2 still read a measured 1.000000,
#      because they are above the guard and the jars are still there -- and
#      family C still falls to 0.550000, because C-3's 0.45 is gone. That is
#      the guard inversion stated as a number.
#   K18 and K19 are two held gates failed on their own channels: step 06's
#      Class.newInstance buckets calling the legacy form again (B-13 0.866667)
#      and jars naming none of step 05's four packages (B-12 0.850000). Family
#      C reads 1.000000 in both.
#
# And the rehearsal proves it can fail before its clean runs are believed --
# eight times, on eight different routes, none of which lands on a number any
# fixture in the table produces.
#
#   PERT     takes the golden and re-points ONE field, the `in=` on toarray[2],
#            at a method that is not one of toarray.txt's five. Asserted on the
#            BYTES that exactly one line changed and that `toarray.ctorref=8`
#            still says eight, because an `in=` is an attribution and not a
#            count. B-14's C-4 bucket leg has to fall to 4 of 5 while its site
#            leg still reads 8 of 8, and B-14 to 0.973333 and no further, with
#            all three family-C legs exactly where the golden left them.
#   MODPERT  deletes ONE of the golden's twelve jars -- arquillian-testng,
#            chosen because it is neither the poisoned artifact nor one of the
#            class slots carrying a B-12e marker. C-1 falls to 0.909091 on its
#            READABLE term -- the eleven jars that remain are all named
#            correctly, so the third term reads 11/12 and is not the minimum --
#            C-2 to 0.833334 and familyC to 0.923485, and B-12 must still read
#            1.000000 from eleven jars, which is the two channels proved
#            separate from the family-C side. That 0.833334 used to sit one ULP
#            from K3's C-2 of 0.833333, and the rehearsal's narrative turned on
#            the distinction; C-1's third term has since moved K3 to 0.694444,
#            so the two rows are 0.138890 apart and the six-decimal assertion is
#            belt and braces now rather than the only thing telling them apart.
#   BEHAVALL, BEHAVHALF break eighteen and nine of the eighteen recorded
#            exchanges B-14.5 is scaled by -- nine as `differ` and nine as
#            `missing`, because a rehearsal that only ever sent one channel
#            would agree with a gate that read one. B-14 reads 0.400000 and
#            0.700000; a gate that halved on any breakage would read 0.500000
#            in both.
#   BEHAVRED, BEHAVONE break exactly ONE exchange each and differ only in
#            WHICH. B-14.5's redirect quarter is withdrawn for
#            servlet/exec-redirect and kept for the other, so the two land
#            0.141666 of B-14 apart on the same count.
#   SVCONE   breaks one of the SIXTY-NINE recorded service lookups C-3 is
#            scaled by. C-3 reads 0.985507 and B-14 stays at 1.000000: two
#            scalings, two lists, one file.
#   GONE     deletes behavior.summary outright. ONE ABSENCE, THREE
#            CONSEQUENCES, asserted together because there is no tree in which
#            the file is missing for one gate and present for another: B-10's
#            nio third is halved (0.850000), B-14.5 goes to zero with its
#            redirect quarter (B-14 0.400000), and C-3 goes to zero (familyC
#            0.550000). The three lists -- four nio ids, eighteen http ids,
#            sixty-nine service ids -- are asserted pairwise disjoint first,
#            which is the precondition every "unmoved" row in the other seven
#            perturbations rests on.
#
# Its twenty-eight fixture directories are required to be pairwise distinct by
# digest, so that two rows which had become byte-identical twins cannot look
# like twenty-eight assertions while being twenty-seven; and the table and the
# directory listing are required to name the same set, which is the ladder's
# third trap in miniature -- a fixture that stopped being scored would
# otherwise disappear without changing a line of output.
RUN bash /tests/fixtures/c-rehearsal.sh

# The three baselines and the twenty-two frozen set files this rung reads,
# checked against each other rather than against a number typed a second time
# here.
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
# step 07's golden tree re-measured with the gate that ships HERE, and it had to
# be: step 07's SourceScan cannot see an `svcload` record, because that record
# kind did not exist below this step. Six aggregates and sixteen records are
# therefore first measurements of an old tree, which is exactly the situation
# the ninth trap describes -- a step-local mean hiding an inherited figure that
# later requirements made unpassable -- and the assertions that relate them to
# target.txt are what make it visible if one of them drifted.
#
# FOUR COUNTERS CHANGED THEIR SPELLING RATHER THAN THEIR VALUE, which is the
# same trap wearing the eleventh's clothes. ns.ref.javax.annotation and
# ns.ref.javax.ejb are gone from this file and ns.ref.jakarta.annotation and
# ns.ref.jakarta.ejb hold their 17 and 5, because step 07 did the rename and
# this is its output. Every derivation below that was written against the javax
# spelling is re-keyed here, in the same direction the scorer re-keys it, and
# the four that are NOT re-keyed are the four this rung must still hold.
#
# STEP 07'S OWN source.txt AND target.txt ARE NOT STAGED, for the reason step 07
# gave about step 06's: both would land as baseline/source.txt and
# baseline/target.txt, the same two names this step's pair lands as, and which
# one a gate read would come down to the order of a staging loop. What step 07
# needs carried forward is carried as the target.c2.* through target.c5.* keys
# inside this step's target.txt -- B-14 reads them under those step-07
# spellings, deliberately, because renaming a key is how a carry-forward gate
# quietly stops grading the thing it was named for. Step 06's seven population
# files -- newinstance.txt, accessible.txt, views.txt, builds.txt, optops.txt,
# boxedctor.txt, varbuckets.txt -- are staged under their own names and checked
# below against this tree's own records.
#
# THE DIAMOND PAIR STARTS MOVING AGAIN HERE, and that is itself an assertion.
# At steps 03 and 04 target.diamond and target.b10.diamond were the same 93
# counted from opposite ends; step 05 spent 19 of them on its sinks, step 06
# spent the twentieth on Set.of, and step 07 spent none -- an annotation package
# rename constructs nothing, `toArray(T[]::new)` replaces an ARRAY creation and
# HttpClient.newBuilder() is a factory. This rung spends TWO. C-3 deletes the
# two hand-rolled META-INF/services readers whole, and the generic instances
# those loops built go with them, so target.diamond is 71 against a tree that
# holds 73 and the difference is asserted below as a SUBTRACTION rather than
# written as a literal. Three of the four denominators this step re-bases move
# for that one edit and the fourth moves with them; a denominator copied
# forward would charge a correct tree for doing exactly what this rung asked.
#
# The other invariants are the things this rung must NOT move, each appearing in
# both files and required to be equal: the manual close() count (0), the java.io
# floor (4), the non-anonymous explicit remainder (23), the anonymous functional
# residue (12), the @SuppressWarnings sites (44) and the raw-type count (0). A
# rung that moves one of them has either redone an earlier step's work or undone
# it, and B-10, B-11, B-12 and B-13 exist because the second is the likelier
# accident. The resource clauses are no longer on that list: they were 9 at
# every rung from step 03 to step 07 and they are 7 here, because two of the
# nine are the readers C-3 deletes.
#
# THE FLOOR CLAIM, ASSERTED ON THE BASELINES rather than only through a fixture.
# The tree the candidate is handed already satisfies every requirement B-10,
# B-11, B-12, B-13 and B-14 state -- 73 diamonds, the 23 non-anonymous sites
# still explicit, 9 resource clauses, no manual closes, java.io at its 4 pinned
# sites, 62 lambdas over 34 files, 24 annotated SAM interfaces, 16
# Optional-returning helpers, 23 Collectors calls, 6 Objects delegations, 11
# java.time references, not one currentTimeMillis, both Class.newInstance calls
# migrated, no isAccessible guard with all 14 setAccessible calls still there,
# no wrapper constructor, seven List/Set/Map factory calls with the seven views
# untouched, no Optional.isPresent, 80 of 81 declarations written `var`, all 22
# annotation references already jakarta, all 8 toArray sites converted, the 14
# HTTP records already moved and class file 55 -- and NONE that family C states:
# not one of the twelve jars declares an Automatic-Module-Name, and both
# hand-rolled META-INF/services readers are still scanning by hand.
#
# Those are the two halves of the calibration table's `pinned` row -- familyC
# 0.0000 with every held gate at 1.0000, B-14 included -- and that row is the
# floor argument at the foot of test.sh. It is looser than step 07's, and it is
# looser in the safe direction: 0.30*(907/935) + 0.70*(0.25*1 + 0.75*0) =
# 0.466016 measured, and 0.475000 even if A read its cap, against
# migration_prompt item 6's 0.5. Family B IS a flat 1 here, unlike step 07,
# because every gate it holds reads a baseline cut from the tree it is holding.
# Family A is not: 28 of the 935 recorded behaviours differ under the
# jdk11-step08 era, every one of them a difference between the hand-rolled
# parsers and java.util.ServiceLoader, and the input tree still has the hand-
# rolled parsers. They are not a tolerance: they are the 28 the candidate is
# being asked to earn.
# If any of family C's zeroes below stops being zero the floor moves, and it
# moves in the direction that breaks the item.
#
# THE THREE NEW SET FILES ARE NOT CHECKED AGAINST A COUNT HERE, THEY ARE
# DERIVED -- recomputed from a file cut for another purpose, by the same rule
# that built them, and required to match line for line:
#
#   modnames.txt 12 (artifact, module name) pairs, and the twelve artifacts are
#                 derived from step 01's maven-local listing: exactly the
#                 artifact ids that publish a main jar, which keeps the pom-only
#                 arquillian-build out and would catch a thirteenth appearing
#   svc.txt      13 buckets partitioned 2 legacy / 2 arrive / 11 keep, and the
#                 thirteen are derived from source.txt's sixteen svcload
#                 records; which two move is derived too, from getResources()
#                 against getResource()
#   svcids.txt   69 recording ids, each required to be in assets/ids.txt AND to
#                 have a transcript in this rung's era
#   level.txt    55, unchanged, and this rung is the first that HOLDS it rather
#                 than raising it
#   target.txt   45 keys, every one of them divided by something
#
# THE FIVE SET FILES STEP 07 CUT ARE READ IN THE ARRIVED STATE, which is the
# structural repair this rung's Dockerfile needed and the thing a copied
# agreement block would have got wrong in five places at once. nsmove.txt,
# toarray.txt, http.txt and httpids.txt were cut from step 07's INPUT tree;
# source.txt here is step 07's OUTPUT. Derived verbatim they would compare the
# file to a tree that has already moved past it and fail on a CORRECT input. So
# each is read as the arrival it now describes: nsmove.txt through the same
# jakarta-to-javax re-keying the scorer performs, and with the javax spellings
# required ABSENT; toarray.txt against form=ctorref with form=sized required to
# be zero; http.txt's hold half verbatim in its own file and its move half as a
# proven departure. That is a stronger check than step 07's, not a weaker one --
# verbatim, at this rung, would have compared the file to itself.
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
# exemption -- and its 81 is NOT target.b13.candidatenew any more, because two
# of the 81 are the resource declarations C-3 deletes, so the file's count and
# the key differ by an asserted 2. views.txt is the exception and is still
# DERIVED, because it is the one of the seven step 06 was told to leave alone. If B-13 is a regression
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
    tmt=$(sed -n  's/^target\.mod\.total=//p'        ${btgt} | head -1); \
    tmd=$(sed -n  's/^target\.mod\.derivable=//p'    ${btgt} | head -1); \
    svl=$(sed -n  's/^target\.svc\.legacy=//p'       ${btgt} | head -1); \
    sva=$(sed -n  's/^target\.svc\.arrive=//p'       ${btgt} | head -1); \
    svk=$(sed -n  's/^target\.svc\.keep=//p'         ${btgt} | head -1); \
    # Every key target.txt carries is a key test.sh reads, and every key test.sh \
    # reads is a key target.txt carries. 45 both ways: a key added to the file \
    # and not to the scorer is a frozen number nothing divides by, which is the \
    # kind that goes wrong unnoticed, and a key the scorer reads and the file \
    # does not carry reads as the empty string and defaults to 0. \
    test "$(grep -c '^target\.' ${btgt})" = 45; \
    grep -o '^target\.[a-z0-9.]*' ${btgt} | LC_ALL=C sort > /tmp/tkeys.txt; \
    sed -n "s/.*tv 'target\\(.*\\)').*/target\\1/p" /tests/test.sh \
      | tr -d '\\' | LC_ALL=C sort -u > /tmp/rkeys.txt; \
    test "$(grep -c . /tmp/rkeys.txt)" = 45; \
    if ! cmp -s /tmp/tkeys.txt /tmp/rkeys.txt; then \
        echo "baseline/target.txt and test.sh disagree about which target.* keys exist:" >&2; \
        diff /tmp/tkeys.txt /tmp/rkeys.txt >&2 || true; exit 1; \
    fi; \
    # 44 of the 45 are numbers. target.c5.file is the one that is not, and it is \
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
    # ...and step 07's own work, in the ARRIVED state, because B-14 is where it \
    # now lives. The tree this step is handed is the tree step 07's blind agent \
    # produced: every javax.annotation and javax.ejb reference has become a \
    # jakarta one, every sized toArray has become a constructor reference, and \
    # the HttpURLConnection loop in ServletMethodExecutor has become HttpClient. \
    # BOTH DIRECTIONS ARE ASSERTED -- the departed packages absent and the \
    # arrived ones present at the right counts -- and that is not belt and \
    # braces here. The four B-14 set files below were cut from step 07's INPUT \
    # tree by construction, so they name buckets this tree has already moved; \
    # a source.txt that had accidentally been measured on step 07's input would \
    # satisfy every one of those set checks and fail only here. \
    test "$(sed -n 's/^ns\.ref\.javax\.annotation=//p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^ns\.ref\.javax\.ejb=//p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c 'pkg=javax\.annotation')" = 0; \
    test "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c 'pkg=javax\.ejb')" = 0; \
    test "$(sed -n 's/^ns\.ref\.jakarta\.annotation=//p' ${bsrc} | head -1)" = 17; \
    test "$(sed -n 's/^ns\.ref\.jakarta\.ejb=//p' ${bsrc} | head -1)" = 5; \
    test "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c 'pkg=jakarta\.')" -gt 0; \
    test "${tas}" = 0; \
    test "${tac}" = 8; \
    test "${tar}" = 0; \
    test "${taf}" = 5; \
    test "${hcr}" = 11; \
    test "${hrd}" = 1; \
    test "$(sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=clienttype')" -gt 0; \
    test "$(sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=legacycall')" = 0; \
    # ...and what THIS rung has not done, which is the whole of ITS family C. \
    # Only one of the three legs is visible in a source scan at all: C-1 and \
    # C-2 read a manifest attribute off jars a build produces and there is no \
    # build in this layer, so they are asserted in the calibration run instead. \
    # C-3 is here, and it reads 0 in the only direction that decides the leg -- \
    # not one platform ServiceLoader call anywhere in the tree -- while both \
    # hand-rolled scanners are still where they were. The floor that follows is \
    # 0.30*(907/935) + 0.70*(0.25*1 + 0.75*0) = 0.466016 measured, and 0.475000 \
    # even with family A at its cap, against migration_prompt item 6's 0.5. \
    test "$(sed -n 's/^svc\.platformload=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^svc\.platformtype=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^svc\.providertype=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^svcload\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=platform')" = 0; \
    test "$(sed -n 's/^svc\.legacyscan=//p'    ${bsrc} | head -1)" = 4; \
    test "$(sed -n 's/^svc\.loadclass=//p'     ${bsrc} | head -1)" = 12; \
    # The two files agree about the things that must not move. \
    test "$(( exp - ean ))" = "${tnon}"; \
    test "${tnon}" = 23; \
    test "${anf}" = "${tanf}"; \
    # try-with-resources is one of the FOUR inherited denominators this rung \
    # re-bases rather than copies forward. The two stream rewrites C-3 asks for \
    # delete two `try (InputStream is = url.openStream(); BufferedReader r = \
    # ...)` clauses along with the loops they fed, so a correct tree ends at 7 \
    # and a target of 9 would charge it for its own work. The subtraction is \
    # asserted AS a subtraction -- the tree holds 9, the target states 7, the \
    # difference is exactly C-3's two moves -- and not as two loose literals. \
    test "${res}" = 9; \
    test "${tres}" = 7; \
    test "$(( res - tres ))" = 2; \
    test "$(( ion + iod ))" = "${tio}"; \
    test "${tio}" = 4; \
    test "${ism}" = "${esam}"; \
    test "${isa}" = "${esam}"; \
    test "${elf}" = 20; \
    test "${elf}" -lt "${lamf}"; \
    test "${ecm}" = 24; \
    test "${ecm}" -le "${sopm}"; \
    # The diamond pair, and the difference is TWO -- the second of the four \
    # re-based denominators. Both of the `new LinkedHashSet<>()` sites C-3's \
    # rewrites delete, DynamicServiceLoader:196 and spi ServiceLoader:142, are \
    # inside the two methods this rung moves, so the hold is re-based from 73 \
    # to 71. A gate that kept 73 would score a correct tree 71/73 on B-10's \
    # diamond leg for doing exactly what C-3 required of it. \
    test "${tdia}" = 71; \
    test "$(( dia - tdia ))" = 2; \
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
    # The last two re-based denominators, and they move together. The two \
    # BufferedReader resource variables that go with C-3's two try clauses are \
    # two of this tree's 80 `var` declarations, so the population B-13.7 holds \
    # is 78 and not 80. candidatenew follows it by the same algebra it has \
    # always followed -- want plus the one candidate step 06 was told to leave \
    # alone -- so 81 - 2 = 79. A numerator of 78-1 over a denominator of 80 \
    # would charge a correct tree for two declarations this rung asked it to \
    # delete; a denominator of 78 with candidatenew left at 81 would charge it \
    # for three. \
    test "${g7c}" = 79; \
    test "${g7w}" = "$(( g7c - 1 ))"; \
    test "${g7w}" = 78; \
    test "${vsz}" = 80; \
    test "${g7w}" = "$(( vsz - 2 ))"; \
    # Family C's own denominators, every one of them tied to a counter above or \
    # to a set file below. A denominator that is only a literal is a literal \
    # that gets copied forward; each of these is required to BE the population \
    # it divides. \
    test "${c2b}" = 6; \
    test "${c2r}" = 22; \
    # target.c2.refs is B-14's denominator and it is read off the ARRIVED \
    # packages here, because the departed ones are gone from this tree: the 22 \
    # that were javax.annotation + javax.ejb at step 07 are jakarta.annotation \
    # + jakarta.ejb now, and B-14.2 divides its arrival half by exactly that. \
    test "${c2r}" = "$(( $(sed -n 's/^ns\.ref\.jakarta\.annotation=//p' ${bsrc} | head -1) \
                       + $(sed -n 's/^ns\.ref\.jakarta\.ejb=//p' ${bsrc} | head -1) ))"; \
    test "${c3b}" = 43; \
    test "${c3r}" = 112; \
    test "${c3b}" -gt "${c2b}"; \
    test "${c3r}" -gt "${c2r}"; \
    test "${c4b}" = 5; \
    test "${c4s}" = "${tac}"; \
    test "${c4s}" = 8; \
    test "${c4b}" -lt "${c4s}"; \
    test "${c5l}" = 14; \
    test "${c5t}" = 3; \
    test "${c5s}" = 3; \
    # At step 07 this read `c5l + c5s == hlr + hlc`: fourteen legacy records in \
    # the target file plus three CDI-stub ones elsewhere were the whole legacy \
    # population. The fourteen are gone from this tree -- that is what step 07 \
    # did -- so what is left is the three, and the denominator 14 survives only \
    # as the figure B-14.5 divides by. \
    test "${c5s}" = "$(( hlr + hlc ))"; \
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
    # --- nsmove.txt, derived through the SAME re-keying the scorer does ------- \
    # FOUR SET FILES BELOW ARE READ AGAINST A TREE THAT HAS ALREADY MOVED PAST \
    # THEM. nsmove.txt, toarray.txt, http.txt and httpids.txt were cut from \
    # step 07's INPUT and are B-14's populations now; the tree this baseline \
    # was measured on is step 07's OUTPUT. So each one is checked in its \
    # ARRIVED state rather than verbatim, which is a stronger check and not a \
    # weaker one: verbatim would have compared the file to itself. \
    # \
    # nsmove.txt is frozen in the javax.annotation/javax.ejb spelling and this \
    # tree has neither, so the derivation runs the way B-14.2's arrival half \
    # runs and then maps the key back: take the jakarta pairs off this tree, \
    # rewrite the prefix, compare to the file. Round-tripping the key is the \
    # point -- the two spellings differ in nothing but the first component, so \
    # a bucket that never moved is missing from one side and a bucket the \
    # scorer could never match is missing from the other. \
    nsbucketise 'p == "jakarta.annotation" || p == "jakarta.ejb"' \
      | sed 's/^jakarta\./javax./' | LC_ALL=C sort -u > /tmp/d-nsmove.txt; \
    nswanted /tests/baseline/nsmove.txt > /tmp/s-nsmove.txt; \
    if ! cmp -s /tmp/d-nsmove.txt /tmp/s-nsmove.txt; then \
        echo "baseline/nsmove.txt is not the jakarta.annotation/jakarta.ejb pairs in source.txt:" >&2; \
        diff /tmp/d-nsmove.txt /tmp/s-nsmove.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-nsmove.txt)" = "${c2b}"; \
    test "$(nsrefs 'p == "jakarta.annotation" || p == "jakarta.ejb"')" = "${c2r}"; \
    test "$(nsrefs 'p == "javax.annotation" || p == "javax.ejb"')" = 0; \
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
    # this tree's ns records -- one selects the two jakarta packages, the other \
    # everything that is neither those nor the javax spellings they came from \
    # -- so the two want sets must partition every ns record's pair: no pair in \
    # both, and the two counts summing to the whole population. A bucket that \
    # fell out of both filters would be a reference B-14.2 does not ask to have \
    # moved and B-14.3 does not reward holding, and nothing else here would \
    # see it. \
    if [ "$(LC_ALL=C comm -12 /tmp/s-nsmove.txt /tmp/s-nshold.txt | grep -c .)" != 0 ]; then \
        echo "a pair is in BOTH nsmove.txt and nshold.txt:" >&2; \
        LC_ALL=C comm -12 /tmp/s-nsmove.txt /tmp/s-nshold.txt >&2; exit 1; \
    fi; \
    test "$(( c2b + c3b ))" = "$(nsbucketise '1' | grep -c .)"; \
    test "$(( c2r + c3r ))" = "$(sed -n 's/^ns\[[0-9]*\]=//p' ${bsrc} | grep -c .)"; \
    # The eight packages SourceScan counts and the eight this pair of files \
    # divides are the same eight, and the per-package reference totals in the \
    # ns.ref.* counters are the per-package totals of the records. Two of the \
    # eight are counted under `jakarta.` on this tree and written `javax.` in \
    # nsmove.txt, so the derived list is re-keyed the same way the set was, and \
    # the per-package loop below reads each counter under the prefix the tree \
    # actually carries. \
    { sed -n 's/^ns\.ref\.javax\.\([a-z]*\)=.*/javax.\1/p'   ${bsrc}; \
      sed -n 's/^ns\.ref\.jakarta\.\([a-z]*\)=.*/javax.\1/p' ${bsrc}; } \
      | LC_ALL=C sort -u > /tmp/d-nspkg.txt; \
    { sed -n 's/^ns\.ref\.\(javax\.[a-z]*\)=.*/\1/p'   ${bsrc}; \
      sed -n 's/^ns\.ref\.\(jakarta\.[a-z]*\)=.*/\1/p' ${bsrc}; } \
      | LC_ALL=C sort -u > /tmp/d-nspkgreal.txt; \
    test "$(grep -c . /tmp/d-nspkg.txt)" = 8; \
    test "$(grep -c . /tmp/d-nspkgreal.txt)" = 8; \
    test "$(grep -c '^jakarta\.' /tmp/d-nspkgreal.txt)" = 2; \
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
    done < /tmp/d-nspkgreal.txt; \
    # --- toarray.txt, derived IN THE ARRIVED STATE ---------------------------- \
    # The file names the five buckets step 07 was asked to convert, and this \
    # tree is step 07's output, so the form to derive on is `ctorref` and not \
    # `sized`. Both halves are checked: the five buckets hold ctorref records \
    # and the tree holds no sized record anywhere, which together are what \
    # B-14.4's two halves will read on an unregressed candidate. \
    fbucketise toarray form=ctorref > /tmp/d-tasized.txt; \
    wanted /tests/baseline/toarray.txt > /tmp/s-toarray.txt; \
    if ! cmp -s /tmp/d-tasized.txt /tmp/s-toarray.txt; then \
        echo "baseline/toarray.txt is not the form=ctorref buckets in source.txt:" >&2; \
        diff /tmp/d-tasized.txt /tmp/s-toarray.txt >&2 || true; exit 1; \
    fi; \
    test "$(fbucketise toarray form=sized | grep -c .)" = 0; \
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
    # --- http.txt: the HOLD half verbatim, the MOVE half as a departure ------- \
    # This is the one B-14 set file whose move half cannot be re-derived from \
    # this tree at all, and saying so is more useful than pretending otherwise. \
    # Its fourteen move records ARE the population step 07 deleted: they are \
    # gone from source.txt by construction, so the check here is that they are \
    # gone, that they were all in target.c5.file and all of the two legacy \
    # forms, and that what stands in their place is what step 07 was asked to \
    # arrive at -- eleven java.net.http references and exactly one redirect \
    # policy, in that same file. The hold half is a different matter: those \
    # three records were never asked to move and are still here, so they are \
    # compared verbatim, which is what makes `c5s` a live figure rather than a \
    # remembered one. \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] == f' \
      | LC_ALL=C sort > /tmp/d-httpmove.txt; \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] != f' \
      | LC_ALL=C sort > /tmp/d-httphold.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "move" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httpmove.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "hold" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httphold.txt; \
    if ! cmp -s /tmp/d-httphold.txt /tmp/s-httphold.txt; then \
        echo "baseline/http.txt's hold records are not the other file's records in source.txt:" >&2; \
        diff /tmp/d-httphold.txt /tmp/s-httphold.txt >&2 || true; exit 1; \
    fi; \
    if [ "$(awk '$2 == "form=legacycall" || $2 == "form=legacytype"' /tmp/d-httpmove.txt | grep -c .)" != 0 ]; then \
        echo "target.c5.file still holds a legacy HttpURLConnection record after step 07:" >&2; \
        awk '$2 == "form=legacycall" || $2 == "form=legacytype"' /tmp/d-httpmove.txt >&2; exit 1; \
    fi; \
    test "$(awk '$2 == "form=clienttype"' /tmp/d-httpmove.txt | grep -c .)" = "${hcr}"; \
    test "$(awk '$2 == "form=redirects"'  /tmp/d-httpmove.txt | grep -c .)" = "${hrd}"; \
    test "$(grep -c . /tmp/s-httpmove.txt)" = "${c5l}"; \
    test "$(grep -c . /tmp/s-httphold.txt)" = "${c5s}"; \
    test "$(awk -v f="${c5f}" '{ split($1, p, ":") } p[1] != f' /tmp/s-httpmove.txt | grep -c .)" = 0; \
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
    # AND THE FOURTH RE-BASED DENOMINATOR, which is the one the frozen file \
    # makes visible rather than only arithmetical. varbuckets.txt is step 06's \
    # population and it is FROZEN: 81 records, verbatim, and this rung does not \
    # get to edit a file cut two rungs back. What this rung does is delete two \
    # of the 81 -- the two BufferedReader resource declarations of the two \
    # hand-rolled META-INF/services readers C-3 rewrites -- so a correct tree \
    # ends this step holding 79 of them, not 81, and target.b13.candidatenew \
    # says 79. Comparing the file's line count to ${g7c} directly would fail on \
    # a CORRECT tree, which is the ninth trap wearing this rung's clothes. The \
    # subtraction is asserted instead, so that neither side can drift alone: \
    test "$(grep -c . /tmp/s-var.txt)" = 81; \
    test "$(( $(grep -c . /tmp/s-var.txt) - g7c ))" = 2; \
    # ...and the two that leave are NAMED, in the file, by the type that makes \
    # them resource variables and by the two methods svc.txt marks `legacy'. A \
    # denominator that moved by two without being able to point at the two \
    # would be a literal with a story attached. \
    awk '$1 == "legacy" { print $2 }' /tests/baseline/svc.txt \
      | LC_ALL=C sort -u > /tmp/d-svclegacy.txt; \
    test "$(grep -c . /tmp/d-svclegacy.txt)" = 2; \
    awk 'NR == FNR { leg[$0] = 1; next } \
         { k = $1; sub(/:[0-9]*$/, "", k); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           if (m != "" && (k "#" m) in leg) print }' \
        /tmp/d-svclegacy.txt /tmp/s-var.txt | LC_ALL=C sort > /tmp/d-varmeth.txt; \
    grep 'type=java\.io\.BufferedReader ' /tmp/d-varmeth.txt \
      | LC_ALL=C sort > /tmp/d-varres.txt; \
    test "$(grep -c . /tmp/d-varres.txt)" = 2; \
    # NEITHER CONDITION ALONE PICKS THE TWO, and that is the whole reason the \
    # subtraction is 2 and not 3. Restricted to the two legacy methods the \
    # frozen file holds THREE records: the two readers plus the LinkedHashSet \
    # at DynamicServiceLoader:196, which C-3 deletes as well -- but :196 is the \
    # one candidate step 06 EXEMPTED, so it was never in the 80 and cannot come \
    # out of them twice. target.b13.candidatenew keeps its +1 for exactly that \
    # line, as the frozen file's own header and step 08's target both say. \
    test "$(grep -c . /tmp/d-varmeth.txt)" = 3; \
    grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java:[0-9]+ type=java\.util\.LinkedHashSet .* in=load$' /tmp/d-varmeth.txt; \
    grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java:[0-9]+ .* in=load$' /tmp/d-varres.txt; \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java:[0-9]+ .* in=reload$' /tmp/d-varres.txt; \
    # AND THE CONTROL, which is why the two are selected by METHOD and not by \
    # type: the frozen file holds a THIRD BufferedReader candidate, at \
    # ContainerRegistryCreator#getActivatedConfiguration, and that one reads a \
    # configuration file rather than a provider list. It is not a service \
    # lookup, svc.txt does not name it, C-3 does not touch it, and it must \
    # still be here at the end of this rung. A rule written on the type would \
    # have taken all three and made the denominator 78. \
    test "$(grep -c 'type=java\.io\.BufferedReader ' /tmp/s-var.txt)" = 3; \
    grep -qE '^impl-base/src/main/java/.*/ContainerRegistryCreator\.java:[0-9]+ type=java\.io\.BufferedReader .* in=getActivatedConfiguration$' /tmp/s-var.txt; \
    if awk '$1 == "legacy" || $1 == "arrive" { print $2 }' /tests/baseline/svc.txt \
         | grep -q 'ContainerRegistryCreator'; then \
        echo "svc.txt claims ContainerRegistryCreator is a service lookup; it reads a configuration file" >&2; \
        exit 1; \
    fi; \
    if awk '{ t = ""; i2 = ""; \
              for (i = 2; i <= NF; i++) { \
                 if (substr($i, 1, 5) == "type=") t = substr($i, 6); \
                 if (substr($i, 1, 5) == "init=") i2 = substr($i, 6) } \
              if (t == "" || t != i2) print }' /tmp/s-var.txt | grep -q .; then \
        echo "baseline/varbuckets.txt: a record's written type is not its initialiser type" >&2; \
        exit 1; \
    fi; \
    # THE ONE EXEMPTION, now readable off the tree rather than only argued for. \
    # 81 candidates at step 06, 80 asked for, and the one that was not is the \
    # diamond \
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
    # --- modnames.txt: the twelve names C-1 and C-2 grade against ------------ \
    # THIS RUNG'S OWN THREE BASELINES, read by nothing before it. They get the \
    # same treatment the inherited set files got: derived from a second source \
    # wherever a second source exists, and tied to the target keys that divide \
    # by them, so that no number in this image is only a number in one file. \
    grep '^[a-z]' /tests/baseline/modnames.txt > /tmp/s-modn.txt; \
    test "$(grep -c . /tmp/s-modn.txt)" = "${tmt}"; \
    test "${tmt}" = 12; \
    test "${tmd}" = "$(( tmt - 1 ))"; \
    test "${tmd}" = 11; \
    test "$(awk 'NF == 2' /tmp/s-modn.txt | grep -c .)" = "${tmt}"; \
    test "$(awk '{ print $1 }' /tmp/s-modn.txt | LC_ALL=C sort -u | grep -c .)" = "${tmt}"; \
    test "$(awk '{ print $2 }' /tmp/s-modn.txt | LC_ALL=C sort -u | grep -c .)" = "${tmt}"; \
    # (1) EVERY NAME IS A LEGAL MODULE NAME. Automatic-Module-Name is read by \
    # ModuleFinder and not by the build tool that writes it, so a name with a \
    # hyphen in it packages cleanly and fails at the first module that requires \
    # it. One line here, and the ladder never has to find that out at run time. \
    if awk '$2 !~ /^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/' /tmp/s-modn.txt \
         | grep -q .; then \
        echo "baseline/modnames.txt names something that is not a legal module name:" >&2; \
        awk '$2 !~ /^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$/' /tmp/s-modn.txt >&2; \
        exit 1; \
    fi; \
    # (2) THE TWELVE ARE THE TWELVE THAT PUBLISH A JAR, and that is derived from \
    # a file cut at step 01 for another purpose entirely. artifacts.txt is the \
    # maven-local listing; the artifact ids with a main jar in it are exactly \
    # the artifact ids in modnames.txt. This is the check that keeps \
    # arquillian-build out -- a pom-only project that looks like a thirteenth \
    # module in every directory listing on the tree and ships no classes at all. \
    grep -oE '[a-z0-9-]+-1\.0\.0-SNAPSHOT\.jar$' /tests/baseline/artifacts.txt \
      | sed 's/-1\.0\.0-SNAPSHOT\.jar$//' | LC_ALL=C sort -u > /tmp/d-modart.txt; \
    awk '{ print $1 }' /tmp/s-modn.txt | LC_ALL=C sort > /tmp/s-modart.txt; \
    if ! cmp -s /tmp/d-modart.txt /tmp/s-modart.txt; then \
        echo "baseline/modnames.txt is not the set of artifacts that publish a jar:" >&2; \
        diff /tmp/d-modart.txt /tmp/s-modart.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c 'arquillian-build' /tmp/s-modart.txt)" = 0; \
    grep -q '^arquillian-build/' /tests/baseline/artifacts.txt; \
    # (3) NO NAME IS A PACKAGE PREFIX OF ANOTHER. Two automatic modules that \
    # share a package do not resolve, and the cheapest way to ship a shared \
    # package is to give one module a name the other's packages live under. \
    # mod.resolved reads 0 on the input tree because nothing declares a name \
    # yet; this is the half of the same question that can be answered here, \
    # without building anything, against the names themselves. \
    if awk '{ n[NR] = $2 } \
            END { for (i = 1; i <= NR; i++) for (j = 1; j <= NR; j++) \
                    if (i != j && index(n[j], n[i] ".") == 1) \
                       print n[i] " is a package prefix of " n[j] }' /tmp/s-modn.txt \
         | grep .; then \
        echo "baseline/modnames.txt: one module name is a package prefix of another" >&2; \
        exit 1; \
    fi; \
    # (4) AND THE TWO THAT ARE NOT THE ARTIFACT ID. The rule instruction.md \
    # states is the longest package prefix each project's own main sources \
    # share -- NOT the artifact id with its hyphens turned into dots, which \
    # agrees on ten of the twelve and is wrong on two. Both exceptions are \
    # asserted BY NAME, because C-2 is scored pairwise and a candidate who \
    # reaches for the cheap rule loses exactly these two and nothing else: \
    # arquillian-impl-base publishes org.jboss.arquillian.impl (there is no \
    # `base' package) and arquillian-protocol-jmx-osgi publishes \
    # org.jboss.arquillian.packager.osgi (its sources are not under protocol). \
    awk '{ id = $1; gsub(/-/, ".", id); if ($2 != "org.jboss." id) print $1 }' /tmp/s-modn.txt \
      | LC_ALL=C sort > /tmp/d-mododd.txt; \
    test "$(grep -c . /tmp/d-mododd.txt)" = 2; \
    grep -qxF 'arquillian-impl-base' /tmp/d-mododd.txt; \
    grep -qxF 'arquillian-protocol-jmx-osgi' /tmp/d-mododd.txt; \
    grep -qxF 'arquillian-impl-base org.jboss.arquillian.impl' /tmp/s-modn.txt; \
    grep -qxF 'arquillian-protocol-jmx-osgi org.jboss.arquillian.packager.osgi' /tmp/s-modn.txt; \
    # --- svc.txt: C-3's population, and the partition is DERIVED -------------- \
    grep '^[a-z]' /tests/baseline/svc.txt > /tmp/s-svc.txt; \
    test "${svl}" = 2; test "${sva}" = 2; test "${svk}" = 11; \
    test "$(grep -c . /tmp/s-svc.txt)" = "$(( svl + sva + svk ))"; \
    test "$(awk '$1 == "legacy"' /tmp/s-svc.txt | grep -c .)" = "${svl}"; \
    test "$(awk '$1 == "arrive"' /tmp/s-svc.txt | grep -c .)" = "${sva}"; \
    test "$(awk '$1 == "keep"'   /tmp/s-svc.txt | grep -c .)" = "${svk}"; \
    test "$(awk '$1 != "legacy" && $1 != "arrive" && $1 != "keep"' /tmp/s-svc.txt | grep -c .)" = 0; \
    test "$(awk 'NF == 2 && $2 ~ /\.java#/' /tmp/s-svc.txt | grep -c .)" = "$(( svl + sva + svk ))"; \
    awk '$1 == "legacy" { print $2 }' /tmp/s-svc.txt | LC_ALL=C sort > /tmp/s-svcleg.txt; \
    awk '$1 == "arrive" { print $2 }' /tmp/s-svc.txt | LC_ALL=C sort > /tmp/s-svcarr.txt; \
    awk '$1 == "keep"   { print $2 }' /tmp/s-svc.txt | LC_ALL=C sort > /tmp/s-svckeep.txt; \
    # THE DEPARTURE AND THE ARRIVAL ARE THE SAME TWO METHODS. That is what makes \
    # C-3 a MOVE and not two gates that happen to be adjacent: a tree that \
    # deleted the two scanners and called nothing would satisfy the first half \
    # and fail the second, and a tree that added two ServiceLoader.load() calls \
    # beside the scanners it kept would do the reverse. Neither can be worth \
    # half of C-3, so the two lists are asserted equal here and the gate divides \
    # each half by the same 2. \
    if ! cmp -s /tmp/s-svcleg.txt /tmp/s-svcarr.txt; then \
        echo "baseline/svc.txt's legacy and arrive halves are not the same two methods:" >&2; \
        diff /tmp/s-svcleg.txt /tmp/s-svcarr.txt >&2 || true; exit 1; \
    fi; \
    test "$(LC_ALL=C comm -12 /tmp/s-svcleg.txt /tmp/s-svckeep.txt | grep -c .)" = 0; \
    # ...and the thirteen buckets are source.txt's, not a hand-written list. \
    # Sixteen svcload records over thirteen distinct methods: DynamicServiceLoader \
    # #load holds one of each form, which is why the two forms' bucket counts \
    # (3 and 11) sum to fourteen and the union is thirteen. \
    sed -n 's/^svcload\[[0-9]*\]=//p' ${bsrc} | LC_ALL=C sort > /tmp/d-svcload.txt; \
    test "$(grep -c . /tmp/d-svcload.txt)" = 16; \
    test "$(awk '$2 == "form=legacyscan"' /tmp/d-svcload.txt | grep -c .)" = 4; \
    test "$(awk '$2 == "form=loadclass"'  /tmp/d-svcload.txt | grep -c .)" = 12; \
    svcbucket() { \
        awk -v want="$1" -v nm="$2" \
            '$2 == ("form=" want) && (nm == "" || $3 == ("name=" nm)) { \
                k = $1; sub(/:[0-9]*$/, "", k); m = ""; \
                for (i = 3; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                if (m != "") print k "#" m }' /tmp/d-svcload.txt | LC_ALL=C sort -u; \
    }; \
    svcbucket legacyscan '' > /tmp/d-svcscan.txt; \
    svcbucket loadclass  '' > /tmp/d-svcload-b.txt; \
    test "$(grep -c . /tmp/d-svcscan.txt)"   = 3; \
    test "$(grep -c . /tmp/d-svcload-b.txt)" = 11; \
    cat /tmp/d-svcscan.txt /tmp/d-svcload-b.txt | LC_ALL=C sort -u > /tmp/d-svcall.txt; \
    cat /tmp/s-svcleg.txt  /tmp/s-svckeep.txt   | LC_ALL=C sort -u > /tmp/s-svcall.txt; \
    test "$(grep -c . /tmp/d-svcall.txt)" = 13; \
    test "$(grep -c . /tmp/s-svcall.txt)" = "$(( svl + svk ))"; \
    if ! cmp -s /tmp/d-svcall.txt /tmp/s-svcall.txt; then \
        echo "baseline/svc.txt is not source.txt's svcload buckets:" >&2; \
        diff /tmp/d-svcall.txt /tmp/s-svcall.txt >&2 || true; exit 1; \
    fi; \
    test "$(sed 's/#.*//' /tmp/s-svcall.txt | LC_ALL=C sort -u | grep -c .)" = 11; \
    test "$(sed 's/#.*//' /tmp/s-svcall.txt | LC_ALL=C sort -u | grep -c .)" \
       = "$(sed -n 's/^svc\.files=//p' ${bsrc} | head -1)"; \
    # WHICH TWO OF THE THREE SCANNERS MOVE, readable in the records rather than \
    # decided in the file. java.util.ServiceLoader answers one question: give me \
    # every provider of this interface. The two sites that call getResources() \
    # -- plural, the whole classpath, a provider list -- are the two it can \
    # replace. FilteredURLClassLoader's getResource() -- singular, one named \
    # resource, delegated to a parent loader -- asks something ServiceLoader has \
    # no method for, so it is a legacyscan record that KEEPS. A rule written on \
    # the form alone would have moved it and broken deployment filtering. \
    svcbucket legacyscan getResources > /tmp/d-svcplural.txt; \
    svcbucket legacyscan getResource  > /tmp/d-svcsingular.txt; \
    test "$(grep -c . /tmp/d-svcplural.txt)"   = 2; \
    test "$(grep -c . /tmp/d-svcsingular.txt)" = 1; \
    if ! cmp -s /tmp/d-svcplural.txt /tmp/s-svcleg.txt; then \
        echo "baseline/svc.txt's legacy half is not the getResources() call sites:" >&2; \
        diff /tmp/d-svcplural.txt /tmp/s-svcleg.txt >&2 || true; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-svcsingular.txt /tmp/s-svckeep.txt | grep -c .)" != 0 ]; then \
        echo "FilteredURLClassLoader#getResource is a legacyscan that svc.txt does not keep" >&2; \
        exit 1; \
    fi; \
    # ONE CLASS IS ON BOTH LISTS, under two of its own methods, and it is the \
    # clearest statement of why C-3 is scoped by METHOD: spi's ServiceLoader \
    # #reload scans META-INF/services by hand and must stop, while \
    # #createInstance loads one class by the name that scan produced and must \
    # not -- and if it did stop, nothing would construct the providers \
    # java.util.ServiceLoader just found. A file-scoped rule would have had to \
    # pick one of them. \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java#reload$'         /tmp/s-svcleg.txt; \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java#createInstance$' /tmp/s-svckeep.txt; \
    sed 's/#.*//' /tmp/s-svcleg.txt  | LC_ALL=C sort -u > /tmp/s-svclegf.txt; \
    sed 's/#.*//' /tmp/s-svckeep.txt | LC_ALL=C sort -u > /tmp/s-svckeepf.txt; \
    test "$(LC_ALL=C comm -12 /tmp/s-svclegf.txt /tmp/s-svckeepf.txt | grep -c .)" = 1; \
    test "$(LC_ALL=C comm -12 /tmp/s-svclegf.txt /tmp/s-svckeepf.txt \
              | grep -c '/util/ServiceLoader\.java$')" = 1; \
    # ...and the other repeated file is repeated INSIDE one list, which is the \
    # same point from the other side: FilteredURLClassLoader keeps two methods, \
    # and neither is a provider lookup. \
    test "$(sed 's/#.*//' /tmp/s-svckeep.txt | LC_ALL=C sort | uniq -d | grep -c .)" = 1; \
    test "$(grep -c 'FilteredURLClassLoader\.java#' /tmp/s-svckeep.txt)" = 2; \
    test "$(grep -c '/SecurityActions\.java#isClassPresent$' /tmp/s-svckeep.txt)" = 4; \
    # --- svcids.txt: the recordings C-3's exec term is scored over ------------ \
    # Sixty-nine of the 935, named rather than counted, because C-3's third leg \
    # multiplies by the fraction of THESE that still pass and a subset chosen by \
    # prefix at grading time would silently change size the first time a \
    # recording was added. Each one must be a real id and each one must have a \
    # transcript in THIS rung's era -- the second half is the check that would \
    # catch an id that survived a rename in ids.txt and lost its recording. \
    grep '^[a-z]' /tests/baseline/svcids.txt > /tmp/s-svcids.txt; \
    test "$(grep -c . /tmp/s-svcids.txt)" = 69; \
    test "$(LC_ALL=C sort -u /tmp/s-svcids.txt | grep -c .)" = 69; \
    test "$(grep -c '^svc/' /tmp/s-svcids.txt)" = 69; \
    while read -r id; do \
        if ! grep -qxF "${id}" /tests/assets/ids.txt; then \
            echo "baseline/svcids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; \
            exit 1; \
        fi; \
        f=$(echo "${id}" | tr '/' '.'); \
        if [ ! -f "/tests/assets/rec/jdk11-step08/${f}.txt" ]; then \
            echo "baseline/svcids.txt names an id with no jdk11-step08 transcript: ${id}" >&2; \
            exit 1; \
        fi; \
    done < /tmp/s-svcids.txt; \
    rm -f /tmp/s-modn.txt /tmp/d-modart.txt /tmp/s-modart.txt /tmp/d-mododd.txt \
          /tmp/s-svc.txt /tmp/s-svcleg.txt /tmp/s-svcarr.txt /tmp/s-svckeep.txt \
          /tmp/d-svcload.txt /tmp/d-svcscan.txt /tmp/d-svcload-b.txt \
          /tmp/d-svcall.txt /tmp/s-svcall.txt /tmp/d-svcplural.txt \
          /tmp/d-svcsingular.txt /tmp/s-svcids.txt /tmp/s-svclegf.txt /tmp/s-svckeepf.txt \
          /tmp/d-varmeth.txt /tmp/d-varres.txt /tmp/d-svclegacy.txt; \
    echo "step 08: the source, target, level, nioids, svcids and twenty-two set-file baselines agree with each other"

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
    # here asked for. The level assertion this rung DOES make is B-4's, read \
    # out of a published class-file header, which a tree that only compiled at \
    # 10 cannot produce -- and it is a HELD gate rather than a must-flip, \
    # because 55 was reached at the rung below and nothing here moves it. The \
    # third probe is the compiler's own default, which is where test.sh falls \
    # back when ok11 reads no. \
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
    # Thirty-seven stage and thirty-three are read. The four that are not are \
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
    #   http.txt         B-14.5 is scoped by FILE and divides by two counts -- \
    #                    the 14 legacy records inside target.c5.file and the 3 \
    #                    java.net.http types that must arrive there -- so it \
    #                    filters the candidate's OWN records to that path and \
    #                    never needs the recorded list \
    # \
    # THE THREE FILES THIS RUNG ADDED ARE ALL READ, which is worth stating \
    # because it is the property that would have made them worth adding and the \
    # one a staging mistake breaks silently. modnames.txt is C-2's whole \
    # denominator, svc.txt is C-3's three-way partition, and svcids.txt is C-3's \
    # execution scale. None of the three has a count in target.txt that could \
    # stand in for it, so a rung that staged them and forgot to read them would \
    # score family C over an empty set and take C-2 and C-3 whole. They are \
    # asserted read, by path, below. \
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
    test "$(ls /tests/baseline | grep -c .)" = 37; \
    for unread in streamfiles junit5 varbuckets http; do \
        if grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${unread}.txt"; then \
            echo "baseline/${unread}.txt is staged as deliberately unread and test.sh now reads it" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qF 'baseline/varbuckets.txt' /tests/test.sh; \
    grep -qF 'baseline/http.txt' /tests/test.sh; \
    # ...and the three this rung added, asserted read from CODE and not from a \
    # comment, which is the opposite test from the four above and is why the \
    # grep is piped through the same comment strip. \
    for readf in modnames svc svcids; do \
        if ! grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${readf}.txt"; then \
            echo "baseline/${readf}.txt is staged for family C and test.sh does not read it" >&2; \
            exit 1; \
        fi; \
    done; \
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
        test -r "/tests/assets/rec/jdk11-step08/$(echo "${id}" | tr / .).txt" || \
            { echo "baseline/httpids.txt names ${id}, which has no recording" >&2; exit 1; }; \
    done; \
    grep -l 'log\[0\]=request:' /tests/assets/rec/jdk11-step08/*.txt > /tmp/withreq.txt; \
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
    # STEP 07'S FAMILY C, NOW B-14, four legs and four shapes, and the shapes \
    # are not interchangeable. They are asserted here in their B-14 spellings, \
    # because renaming a carry-forward gate's variables is exactly how one \
    # stops grading the thing it was named for and keeps printing a number: \
    # \
    #   B-14.2  four legs under two nested minima -- buckets departed, buckets \
    #        arrived, references departed, references arrived. Both pairs are \
    #        needed because six buckets hold 22 references: a tree that \
    #        reverted one import per bucket refills six buckets on the javax \
    #        side while leaving sixteen jakarta references behind \
    #   B-14.3  a HOLD, and the only leg that is not a min at its head: it is \
    #        the min of held-buckets and held-references SCALED BY B-14.2, \
    #        because holding javax.naming is worth nothing on a tree that \
    #        un-renamed the annotations. The unscaled value is published beside \
    #        it so the scaling is visible in the facts and not only in the \
    #        arithmetic \
    #   B-14.4  the same four-leg shape as B-14.2, over buckets and over sites, \
    #        because five buckets hold eight converted toArray calls and three \
    #        of the five hold more than one \
    #   B-14.5  a min of departure and arrival, multiplied by 0.75 + 0.25*red, \
    #        the whole scaled by the fraction of baseline/httpids.txt that \
    #        still matches, because every term before that one is SHAPE and a \
    #        deleted method body has the shape of a finished migration \
    grep -qF 'b14_2=$(minf "$(minf "$(clamp01 "$(frac "${b14_2dep}" "${tgt_b14_2_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(minf "$(clamp01 "$(frac "$(( ${tgt_b14_2_refs:-0} - b14_2jx ))" "${tgt_b14_2_refs:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${b14_2jk}" "${tgt_b14_2_refs:-0}")")")")' /tests/test.sh; \
    grep -qF 'b14_3raw=$(minf "$(clamp01 "$(frac "${b14_3held}" "${tgt_b14_3_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${b14_3refs}" "${tgt_b14_3_refs:-0}")")")' /tests/test.sh; \
    grep -qF 'b14_3=$(awk -v a="${b14_3raw}" -v b="${b14_2}" '"'"'BEGIN { printf "%.6f", a * b }'"'"')' /tests/test.sh; \
    grep -qF 'fact "gate.b14.c3.unscaled=${b14_3raw}"' /tests/test.sh; \
    grep -qF 'b14_4=$(minf "$(minf "$(clamp01 "$(frac "${b14_4dep}" "${tgt_b14_4_buckets:-0}")")"' /tests/test.sh; \
    grep -qF '"$(minf "$(clamp01 "$(frac "$(( ${tgt_b14_4_sized:-0} - nw_tasized ))" "${tgt_b14_4_sized:-0}")")"' /tests/test.sh; \
    grep -qF '"$(clamp01 "$(frac "${nw_tactor}" "${tgt_b14_4_sized:-0}")")")")' /tests/test.sh; \
    grep -qF 'b14_5=$(awk -v d="$(clamp01 "$(frac "$(( ${tgt_b14_5_legacy:-0} - b14_5leg ))" "${tgt_b14_5_legacy:-0}")")"' /tests/test.sh; \
    grep -qF -- '-v a="$(clamp01 "$(frac "${b14_5arr}" "${tgt_b14_5_types:-0}")")"' /tests/test.sh; \
    grep -qF -- '-v r="${b14_5redok}" -v x="${b14_5exec}" \' /tests/test.sh; \
    grep -qF -- ''"'"'BEGIN { m = (d < a) ? d : a; printf "%.6f", x * m * (0.75 + 0.25 * r) }'"'"')' /tests/test.sh; \
    # AND THE FOUR ARE RENORMALISED OVER 0.90, NOT OVER 1.00. Step 07's C-1 was \
    # its class-file leg and this rung raises no class file, so it is dropped \
    # rather than carried at a weight that would grade one hold twice -- B-4 \
    # already holds major 55, and holds it against every jar rather than one. \
    # What is left is 0.07 + 0.17 + 0.12 + 0.54 = 0.90, and dividing by it \
    # restores the proportions the four had to each other without inventing a \
    # fifth. The divisor is asserted as a literal because an edit that drops a \
    # leg and forgets the divisor reads HIGHER, not lower, and nothing else in \
    # this image would complain. \
    grep -qF 'printf "%.6f", p * ((0.07*b + 0.17*c + 0.12*d + 0.54*e) / 0.90)' /tests/test.sh; \
    grep -qF 'fact "B14.java11-jakarta=${b14}"' /tests/test.sh; \
    test "$(grep -c 'fact "B14\.[0-9]\.' /tests/test.sh)" = 4; \
    # ...and step 07's own spellings are asserted GONE from the CODE, for the \
    # same reason the step-05 target.c5.*.floor spellings are asserted gone \
    # below: a gate that changed families and kept its old variable names reads \
    # correctly today and tells a reviewer this step grades the jakarta rename. \
    # The comments keep the old names where they are quoting history, so the \
    # sweep is over non-comment lines -- the eleventh trap, in the small. \
    # \
    # THE FIRST FOUR ARE BANNED BY PREFIX AND THE NEXT FOUR BY WHOLE NAME, and \
    # the difference is that this rung HAS a C-1, a C-2 and a C-3 of its own. \
    # `fact "C2.` would match this file's `fact "C2.module-names=` and a sweep \
    # that banned it would fail on a correct file, so what is banned is step \
    # 07's LEAF -- `C2.jakarta`, `C3.javax-held` -- which is the half of the \
    # name that says which question the leg asks. C-4 and C-5 are banned whole, \
    # because a family C of three legs has no fourth or fifth under any leaf. \
    # \
    # `fact "gate.c1.`, `fact "gate.c2.` and `fact "gate.c3.` are NOT banned at \
    # all, for the same reason: this rung publishes gate.c1.declared, \
    # gate.c2.names and gate.c3.departed under its own family C. Only gate.c4 \
    # and gate.c5 are dead spellings here. \
    grep -v '^ *#' /tests/test.sh > /tmp/ts-code.txt; \
    for dead in 'tgt_c2_' 'tgt_c3_' 'tgt_c4_' 'tgt_c5_' \
                'fact "gate.c4.' 'fact "gate.c5.' \
                'fact "C2.jakarta' 'fact "C3.javax-held' \
                'fact "C4.' 'fact "C5.' \
                'fact "C1.major'; do \
        if grep -qF "${dead}" /tmp/ts-code.txt; then \
            echo "test.sh still carries step 07's family-C spelling: ${dead}" >&2; \
            grep -nF "${dead}" /tmp/ts-code.txt | head -3 >&2; exit 1; \
        fi; \
    done; \
    # ...and the three leaves this rung's own family C does publish, asserted \
    # present in the same sweep's input, so the ban above cannot be satisfied by \
    # a file that publishes no family-C gate names at all. \
    for live in 'fact "C1.modules-named=' 'fact "C2.module-names=' \
                'fact "C3.platform-lookup='; do \
        if ! grep -qF "${live}" /tmp/ts-code.txt; then \
            echo "this rung's family C does not publish ${live}" >&2; exit 1; \
        fi; \
    done; \
    rm -f /tmp/ts-code.txt; \
    # B-14.5 reads its records out of the candidate's own scan filtered to \
    # target.c5.file, which is why http.txt's move half is read as a departure \
    # here and its hold half verbatim, and it publishes the CDI stub's three \
    # legacy records as a REPORTED count that no leg divides by. A gate that \
    # had asked for the tree-wide legacy count to reach zero would score the \
    # finished migration at zero too, because those three are an anonymous \
    # URLConnection subclass standing in as an injection fixture in another \
    # module, and no rung on this ladder asks for it to be rewritten. \
    grep -qF 'b14_5stub=$(( ${nw_hlegref:-0} + ${nw_hlegcall:-0} - b14_5leg ))' /tests/test.sh; \
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
    # B-14 has the same shape, and at this rung the indentation is the whole \
    # story of which family reads what. B-14's four legs sit INSIDE the measured \
    # guard and are indented three, because every one of them counts records in \
    # the source baseline and a tree whose sources the gate compiler declined \
    # produced no records to count. \
    # \
    # FAMILY C'S THREE LEGS ARE UNINDENTED, and not because they escaped a guard: \
    # there is no source guard over them to escape. C-1 and C-2 read the PUBLISHED \
    # JARS through java.lang.module and C-3 reads them through the recorded \
    # behaviours, so the source scan is not an input to any of the three. A tree \
    # that fails to compile under the gate's own javac fails B-3 and publishes \
    # nothing, and family C then divides by twelve and reads zero -- which is the \
    # right answer arrived at through the jars rather than through a guard. \
    # \
    # This is the inversion worth stating, because it is the opposite of every \
    # rung from 02 to 07: down there family C was the source-shaped family and \
    # the held gates were the jar-shaped ones. Here the held gates are the \
    # source-shaped ones and family C is the jar-shaped one. Counted rather than \
    # listed, for the reason indentation is always counted here: a leg that \
    # drifts into or out of the guard changes its indentation before it changes \
    # anything else, and nothing else about it changes at all. \
    test "$(grep -cE '^   # -+ B-14\.[2-5]$' /tests/test.sh)" = 4; \
    test "$(grep -cE '^# -+ C-[1-3]$' /tests/test.sh)" = 3; \
    if grep -qE '^   # -+ C-[0-9]' /tests/test.sh; then \
        echo "a family-C leg banner is indented inside a measured guard" >&2; \
        exit 1; \
    fi; \
    if grep -qF 'C-1, paid' /tests/test.sh; then \
        echo "C-1 is still read before a guard and paid after it: this rung's C-1 is not a must-flip" >&2; \
        exit 1; \
    fi; \
    test "$(grep -cE '^   fact "gate.src.penalty=' /tests/test.sh)" = 1; \
    # The arithmetic of every composite score in the file, by its bytes. \
    grep -qF '0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e' /tests/test.sh; \
    grep -qF '0.20*l + 0.20*s + 0.60*c' /tests/test.sh; \
    grep -qF '0.4*d + 0.3*r + 0.3*n' /tests/test.sh; \
    grep -qF '(0.30*a + 0.175*b) / 0.475' /tests/test.sh; \
    grep -qxF 'mig="${c_score}"' /tests/test.sh; \
    grep -qF 'printf "%.6f", t1 * t2' /tests/test.sh; \
    grep -qF '0.20*a + 0.35*b + 0.45*c' /tests/test.sh; \
    # B-14's own composite, which is step 07's family C with ITS C-1 removed and \
    # the remaining four renormalised by dividing by 0.90 -- the same subtraction \
    # B-13 performed on step 06's, one rung later and for the same reason. Step \
    # 07's C-1 read the class-file major and asserted 55; this tree has been at \
    # 55 since the rung below and nothing here moves it, so holding a major that \
    # cannot move is not work and is not paid for. What changed from step 07's \
    # spelling is the multiplier in front: there it was `r`, step 07's own C-1 \
    # reach, and here it is `p`, the anti-gaming penalty that step 07's family C \
    # carried separately. The penalty stayed because raw types and suppressions \
    # ARE still reachable in held code; the reach went because there is nothing \
    # left for it to reach. \
    grep -qF 'p * ((0.07*b + 0.17*c + 0.12*d + 0.54*e) / 0.90)' /tests/test.sh; \
    if grep -qF 'r * ((0.07*b' /tests/test.sh; then \
        echo "B-14 still scales step 07's four legs by step 07's C-1 reach" >&2; \
        exit 1; \
    fi; \
    if grep -qF 'p * (0.10*a + 0.07*b' /tests/test.sh; then \
        echo "step 07's five-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
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
    # sums in prose -- family B's FOURTEEN to 1.00000000, family C's three to \
    # 1.00, and B-14's four to 0.90 -- and a claim in prose that nothing checks \
    # is the same object as no claim. B-12's five are summed too: they are step \
    # 05's own internal weights carried forward unrenormalised, and the whole \
    # argument for not renormalising them is that they already sum to one. \
    # \
    # FAMILY B'S ROW IS EIGHT DECIMAL PLACES WIDE AT THIS RUNG and that is not \
    # cosmetic. Step 07's thirteen weights were exact at six; adding a fourteenth \
    # gate scaled all thirteen by 0.88 to make room for B-14's 0.12, and 0.046464 \
    # x 0.88 is 0.04088832, which needs eight. A rung that rounded them back to \
    # six would print thirteen weights that no longer sum to 0.88 and a family B \
    # that no longer sums to one -- and this loop is the only thing in the tree \
    # that would say so. The two-step check below pins each printed row to the \
    # awk term it was copied from, so a rounded row and a rounded term would \
    # still agree with each other; only the sum catches the pair. \
    # \
    # B-14's divisor is the one worth stating twice, for the reason B-13's was at \
    # the rung below. Step 07's four remaining legs carried 0.90 of step 07's \
    # family C between them, so the renormalisation has to divide by 0.90; divide \
    # by 1.00 instead and a tree that held every one of step 07's legs scores \
    # B-14 at 0.9 and is charged a tenth of the gate for work it did not fail to \
    # do. Divide by the wrong number the other way and B-14 pays more than it is \
    # weighted. \
    for sum in "0.04088832 0.02044416 0.08177664 0.08177664 0.06814720 0.08177664 0.04088832 0.06814720 0.04088832 0.07496192 0.08177664 0.09292800 0.10560000 0.12000000:1.000000:family B" \
               "0.20 0.35 0.45:1.000000:family C" \
               "0.30 0.35 0.10 0.10 0.15:1.000000:B-12" \
               "0.12 0.18 0.03 0.26 0.23 0.08:0.900000:B-13's six, before its divisor" \
               "0.07 0.17 0.12 0.54:0.900000:B-14's four, before its divisor"; do \
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
    # family C; B-14's four and B-12's five have no printed rows to compare \
    # against, so they are pinned here by the whole expression instead. \
    grep -qF '0.07*b + 0.17*c + 0.12*d + 0.54*e' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when step 03's C-6 \
    # was added the arithmetic was changed and the report was not -- so every run \
    # printed five gates at the old weights while scoring six at the new ones. A \
    # number that is right, described wrongly, is the one defect a reviewer cannot \
    # see. Every weight in this rung's two families is a fresh literal: family C \
    # was re-weighted from scratch because this step's migration has three gates \
    # and no two of them cost the same, and family B grew a FOURTEENTH gate, \
    # which moved all thirteen others -- every step-07 weight was scaled by 0.88 \
    # to make room for B-14's 0.12, and the scaling is exact at EIGHT places, \
    # which is why these rows print eight decimals where the rung below printed \
    # six. \
    # \
    # ALL THIRTEEN INHERITED ROWS MOVED AT THIS RUNG, not just the two nearest \
    # the new gate, so listing "the rows that moved" by name would be listing the \
    # whole family. The four nearest are listed anyway, because they are the ones \
    # whose OLD spelling is a plausible survivor -- 0.085184, 0.092928, 0.105600 \
    # and 0.120000 are all real numbers that appeared in this file an edit ago -- \
    # and the family loop below covers the other nine positionally. \
    grep -qF "printf '    B10 java 7 held      %s  x0.07496192" /tests/test.sh; \
    grep -qF "printf '    B11 java 8 lang held %s  x0.08177664" /tests/test.sh; \
    grep -qF "printf '    B12 java 8 lib held  %s  x0.09292800" /tests/test.sh; \
    grep -qF "printf '    B13 java 9-10 held   %s  x0.10560000" /tests/test.sh; \
    grep -qF "printf '    B14 java 11 held     %s  x0.12000000  (penalty %s)" /tests/test.sh; \
    grep -qF "printf '    C1 modules named        %s  x0.20  (declared %s, readable %s, right %s)" /tests/test.sh; \
    grep -qF "printf '    C2 the right names      %s  x0.35  (scaled by C1)" /tests/test.sh; \
    grep -qF "printf '    C3 platform lookup      %s  x0.45  (exec %s)" /tests/test.sh; \
    # And the step-07 rows asserted GONE, because the four that survive above are \
    # four of thirteen and a row this rung forgot to rewrite is a row describing \
    # the rung below. B-4's entry bans the row's PREFIX rather than its weight, \
    # because two things changed on that row: step 07 printed `>=54` as a literal \
    # and this rung prints `>=%-3s` from WANT_MAJOR, which comes out of \
    # level.txt. A row that still read `>=54` would print the wrong major on a \
    # tree that had moved and would be describing the rung below twice over. \
    for oldrow in "    B1 no-maven          %s  x0.046464" \
                  "    B4 one major, >=54" \
                  "    B8 jupiter held      %s  x0.077440" \
                  "    C1 major %-3s" "    C5 java.net.http"; do \
        if grep -qF "printf '${oldrow}" /tests/test.sh; then \
            echo "the report still prints a step-07 row: ${oldrow}" >&2; \
            exit 1; \
        fi; \
    done; \
    # THE PENALTY IS PRINTED ON B-14'S ROW rather than on a row of its own, and \
    # that is the inheritance made visible. At step 07 it multiplied family C and \
    # earned its own unweighted line; here it multiplies exactly one gate, so it \
    # belongs in that gate's parenthesis and nowhere else. A rung that kept the \
    # standalone line would be telling a reader the penalty multiplies family C, \
    # which at this rung it does not -- family C is the module graph and the \
    # penalty counts raw types in held code. \
    if grep -qF 'anti-gaming penalty     %s  (multiplies family C)' /tests/test.sh; then \
        echo "the report still prints the penalty as a family-C multiplier" >&2; \
        exit 1; \
    fi; \
    # C-1's and C-3's rows print their RAW readings beside their paid ones, and \
    # that is the only place a reader of the report can see the two apart. \
    # Asserted as claims distinct from those rows' weights, because both scalings \
    # are invisible on the golden -- where declared, readable and exec all read \
    # 1.0 -- and a report that dropped either parenthesis would look identical on \
    # every tree that finished the step. \
    grep -qF '(declared %s, readable %s, right %s)' /tests/test.sh; \
    grep -qF '(exec %s)' /tests/test.sh; \
    grep -qF 'fact "gate.c1.declared=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.readable=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.right=' /tests/test.sh; \
    # C-1's THIRD reading is asserted in the row as well as in the facts, and \
    # the two-reading tail is asserted GONE. A C-1 that still printed `readable \
    # %s)` and stopped there would be a C-1 whose third term had been dropped \
    # from the report while the arithmetic above still carried it -- or dropped \
    # from both by an edit that only looked at the row. \
    if grep -qE 'readable %s[)]' /tests/test.sh; then \
        echo "C-1's report row still prints only two of its three readings" >&2; \
        exit 1; \
    fi; \
    grep -qF 'fact "gate.c3.exec=' /tests/test.sh; \
    nrep=$(grep -cE "^printf '    C[0-9][0-9]* " /tests/test.sh); \
    nfact=$(grep -cE '^ *fact "C[0-9][0-9]*\.' /tests/test.sh); \
    if [ "${nrep}" != "${nfact}" ]; then \
        echo "the report prints ${nrep} family-C gates and the scorer publishes ${nfact}" >&2; \
        exit 1; \
    fi; \
    test "${nrep}" = 3; \
    # The fact-line pattern stays anchored at `^ *fact` and not at `^fact` even \
    # though all three of this rung's family-C facts are unindented, which is the \
    # opposite of the rung below, where C-1's was unindented and C-2 to C-5's were \
    # not. The tolerant anchor is kept deliberately: it is the anchor that keeps \
    # counting correctly if a later rung puts family C back inside a guard, and a \
    # pattern that has to be edited to stay true is a pattern that will be edited \
    # wrongly. The count is asserted against a literal 3 as well, so the \
    # tolerance cannot hide a leg that grew or vanished on both sides at once. \
    # \
    # And the general form of the same check, for every family at once, because \
    # asserting ten literals by hand closes one instance of the hole and leaves \
    # the class of it open -- family B's fourteen printfs and family B's awk are \
    # the same pair of spellings, and they agree today only by habit. For each family \
    # this pulls the weights out of the report rows in order and out of that \
    # family's awk expression in order, and requires the two sequences to be \
    # equal. A family added later is covered by adding its letter to the list. \
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B passed ten gates four rungs below, and a one-digit \
    # pattern reads nine rows against fourteen coefficients and calls the \
    # difference a mismatch -- or worse, would have silently skipped B10 through \
    # B14 on both sides if the coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    # \
    # The row pattern needs one space before %s and this rung's family B has rows \
    # whose labels are a character longer than the column, so the columns no \
    # longer line up and the pattern must not depend on them. It matches \
    # ` .*  x0.NN` and not a fixed column for exactly that. THREE rows in this \
    # file are the hard cases and all three are covered by the same pattern: B-4 \
    # carries a `%-3s` conversion in its LABEL, B-14 carries a `(penalty %s)` \
    # after its weight, and C-1 carries both a wider weight column and a \
    # `(declared %s, readable %s, right %s)` tail -- so on none of the three is \
    # the weight \
    # the first or the last field on the line. \
    # \
    # Family C's anchor is `0\.45\*c$` and family B's is `0\.12000000\*b14$`, \
    # both anchored at end of line. At the rung below family C's anchor needed a \
    # trailing `[)]` to tell two expressions ending in the same term apart; here \
    # the two families end in different terms and the `$` alone is enough. The \
    # count below still asserts that each anchor picked exactly one line, which \
    # is the check that would have caught the ambiguity at the rung below and is \
    # kept for the rung above. \
    for fam in B:14:'0\.12000000\*b14$' C:3:'0\.45\*c$'; do \
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
    # Every gate name this rung publishes, by name. B-4's carries the wanted \
    # major as a parameter rather than a literal -- `B4.one-major-ge-${WANT_MAJOR}` \
    # -- so it is matched as a prefix; every other one is exact. \
    # \
    # STEP 07'S FOUR FAMILY-C NAMES ARE IN THIS LIST UNDER B-14 PREFIXES, which \
    # is the whole inheritance in five lines. The leaf of each name is unchanged \
    # -- jakarta, javax-held, toarray, httpclient -- because the question each \
    # leg asks is unchanged; only the family that asks it moved. Keeping the \
    # leaves lets a reviewer diff this rung's facts.txt against step 07's with a \
    # `sed 's/^B14\.//'` and get a file that lines up, and it is what makes the \
    # claim "B-14 is step 07's family C" checkable rather than decorative. \
    for g in B14.2.jakarta B14.3.javax-held B14.4.toarray B14.5.httpclient \
             B14.java11-jakarta \
             B4.one-major-ge- \
             B8.jupiter-held B9.junit4-held B10.java7-held B11.java8lang-held \
             B12a.optional B12b.collectors B12c.objects B12d.jtime \
             B12e.ships-java8-library B12.java8-library \
             B13.2.newinstance B13.3.isaccessible B13.4.boxedctor \
             B13.5.factories B13.6.optional9 B13.7.var B13.java9-10 \
             C1.modules-named C2.module-names C3.platform-lookup; do \
        grep -qF "fact \"${g}" /tests/test.sh; \
    done; \
    # The three family-C leaves are named for what they ASK, not for what they \
    # read, and the distinction is the one this rung is most likely to blur. \
    # `C1.modules-named` is not "modules declared": a jar can declare a name the \
    # platform then refuses to read back, and C-1 is the minimum of the two \
    # readings precisely so that declaring without being readable pays nothing. \
    # `C2.module-names` is not "module names correct": it is the count of \
    # artifacts whose published name is the name the packages imply, scaled by \
    # C-1, so an artifact that is unreadable cannot contribute a right name. \
    # `C3.platform-lookup` is not "services migrated": it is the platform lookup \
    # arriving AND the eleven by-name loads staying, scaled by whether the \
    # recorded behaviours still hold. \
    # \
    # And the measurements underneath them, which are what a reviewer reads when \
    # a leg comes back short. These are asserted by name because a leg that \
    # scores correctly while publishing nothing is a leg no one can debug: at \
    # this rung C-1 reads 0 on a tree with twelve unnamed jars and 0 on a tree \
    # with no jars at all, and only gate.mod.total tells those apart. \
    for g in gate.mod.total gate.mod.declared gate.mod.derivable \
             gate.mod.resolved gate.mod.resolve \
             gate.c1.declared gate.c1.readable gate.c1.right \
             gate.c2.names \
             gate.c3.departed gate.c3.arrived gate.c3.held \
             gate.c3.exec.want gate.c3.exec.broken gate.c3.exec \
             familyA familyB familyC migrated reward; do \
        grep -qF "fact \"${g}=" /tests/test.sh; \
    done; \
    # B-13's six legs keep step 06's NUMBERING -- 2 to 7, with no 1 -- and the \
    # gap is asserted rather than tidied away. Renumbering them 1 to 6 would \
    # break the one property that makes B-13 auditable: every leg is the step-06 \
    # leg of the same number, read against the same frozen set file, and a \
    # reviewer can lay the two files side by side. The missing B13.1 is step \
    # 06's C-1, which asserted major 54; a B-13 that grew a leg 1 back would be \
    # the ninth trap walking in through the front door. \
    if grep -qF 'fact "B13.1.' /tests/test.sh; then \
        echo "B-13 has a leg 1: step 06's C-1 must not be carried forward" >&2; \
        exit 1; \
    fi; \
    # B-14 IS MISSING ITS LEG 1 FOR THE SAME REASON, one rung later, and the two \
    # gaps together are the shape of this ladder: step 06's C-1 asserted major \
    # 54, step 07's asserted 55, and each was dropped when its family was \
    # demoted to a held gate because a major that cannot move is not work. Both \
    # gaps are asserted rather than tidied. \
    if grep -qF 'fact "B14.1.' /tests/test.sh; then \
        echo "B-14 has a leg 1: step 07's C-1 must not be carried forward" >&2; \
        exit 1; \
    fi; \
    # THE CLASS-FILE MAJOR IS NOT A FAMILY-C QUESTION AT THIS RUNG AT ALL. From \
    # step 02 to step 07 every family C opened with a must-flip that javac \
    # performed, and the level was read there. Here it is read once, by B-4, out \
    # of the same level.txt -- and B-4 is a HELD gate, which is the correct \
    # statement of what the level is at this rung: something already reached that \
    # must not regress. A `fact "C1.major...` line would mean this rung believes \
    # the level still has somewhere to go. \
    grep -qF 'fact "B4.one-major-ge-${WANT_MAJOR}=' /tests/test.sh; \
    if grep -qF 'fact "C1.major' /tests/test.sh; then \
        echo "family C still publishes a class-file major: no major moves at this rung" >&2; \
        exit 1; \
    fi; \
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
    # `c1=$(minf` WAS in this list at the rung below and is deliberately NOT in \
    # it here, which is a removal worth explaining rather than doing quietly. \
    # Step 05's C-1 was a minimum of two bucket legs and step 07's was a product \
    # of a reach and a corroborating mean, so at step 07 the operator told the \
    # two apart and the ban was the sharpest entry in the list. THIS RUNG'S C-1 \
    # IS A MINIMUM AGAIN -- a nested `minf "$(minf "${c1decl}" "${c1read}")" \
    # "${c1right}"`, declared against readable against right -- so the operator \
    # no longer discriminates and a ban on it would \
    # fail on a correct file. The discrimination moved to the operands and to the \
    # name: `C1.optional` is banned above, `C1.modules-named` is required above, \
    # and the two cannot both be true. A reviewer who reaches for the operator \
    # here should reach for the gate name instead. \
    for dead in 'C1.optional' 'C2.collectors' 'C3.objects' 'C4.jtime' \
                'C5.ships-java8-library' 'C5 ships the library' \
                'c5_opt_floor=' 'c5_col_floor=' 'c5_obj_floor=' 'c5_jt_floor=' \
                'ok18=' 'ok17='; do \
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
    # seven family-C rows and this one prints three; a leftover row would \
    # reference a variable that family C no longer assigns, and under `set -u` \
    # that is a verifier that dies between computing the reward and reporting it. \
    # `${c4}` and `${c5}` are in the list for the first time at this rung for the \
    # same reason: step 07 printed five rows and assigned c4 and c5, this rung \
    # prints three and does not. \
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
                '${c4}' '${c5}' '${c6}' '${c7}' 'C6 optional 9' 'C7 var' \
                'gate.src.ok8' 'ok8=' 'srcgate 8 8'; do \
        if grep -qF -- "${dead}" /tests/test.sh; then \
            echo "step 06's family C is still spelled as family C in test.sh: ${dead}" >&2; \
            exit 1; \
        fi; \
    done; \
    # The order of the whole scorer, asserted rather than assumed. Three of these \
    # are hoists carried up from the rungs below: the three srcgate compiles sit \
    # ABOVE family B so that B-10 through B-14 can all read their counters, and \
    # the four held gates sit between family B's own gates and family C. Under \
    # `set -u` an un-hoist is not a wrong number, it is a dead verifier -- which \
    # is the good case. The bad case is a hoist that lands after the reads and \
    # leaves the variables at their earlier values, and only the line numbers can \
    # tell those apart. \
    # \
    # THE MEASURED GUARD MOVED DOWN A FAMILY AT THIS RUNG, which is the one \
    # structural change the inheritance made and the one this check exists to \
    # pin. At step 07 the last `if [ -z "${measured}"` in the file opened family \
    # C, C-1 was read above it and paid below it, and the penalty was the last \
    # statement inside. Here the last one opens B-14 -- step 07's family C, held \
    # -- the penalty is still the last statement inside it, and B-14's own fact \
    # is published after it closes. \
    # \
    # FAMILY C IS BELOW THE LAST GUARD AND INSIDE NO GUARD AT ALL. Its three \
    # legs read the published jars and the recorded behaviours, not the source \
    # baseline, so a tree whose sources the gate compiler declined does not make \
    # them unmeasurable -- it makes them zero, correctly, by publishing no jars \
    # to divide into twelve. `g < c` is asserted for exactly this: it is what \
    # says the `g` awk kept is B-14's and not one family C grew. \
    # \
    # The guard is anchored by its opening `if [ -z` at column zero and awk keeps \
    # the LAST, so the number of them is stated here rather than left implicit. \
    # Six lines in the file carry that spelling, all six are the same guard \
    # around a different block, and B-13's is the fifth. A B-14 that lost its \
    # guard would silently move the anchor up to B-13's without changing the \
    # regex, and the penalty would then appear to be inside the block it is \
    # inside -- which is why the count below is a separate assertion from the \
    # ordering. \
    test "$(grep -c '^if \[ -z ' /tests/test.sh)" = 6; \
    awk '/^# -+ the source AST, compiled once$/{s=NR} \
         /^# -+ family B$/{b=NR} /^# -+ B-10: step 03, held$/{t=NR} \
         /^# -+ B-11: step 04, held$/{u=NR} /^# -+ B-12: step 05, held$/{w=NR} \
         /^# -+ B-12e$/{b12e=NR} /^# =+ B-13$/{b13=NR} /^# =+ B-14$/{b14=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^if \[ -z /{g=NR} \
         /^ *fact "gate.src.penalty=/{pen=NR} \
         /^# -+ C-1$/{c1=NR} /^# -+ C-2$/{c2=NR} /^# -+ C-3$/{c3=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} /^fact "B11.java8lang-held=/{fb11=NR} \
         /^fact "B12.java8-library=/{fb12=NR} /^fact "B13.java9-10=/{fb13=NR} \
         /^fact "B14.java11-jakarta=/{fb14=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || u == 0 || w == 0 || b12e == 0 \
                   || b13 == 0 || fb13 == 0 || b14 == 0 || fb14 == 0 \
                   || c == 0 || fc == 0 || g == 0 || pen == 0 \
                   || c1 == 0 || c2 == 0 || c3 == 0 \
                   || v == 0 || b9 == 0 || fb10 == 0 || fb11 == 0 \
                   || fb12 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " B11=" u " B12=" w " B12e=" b12e " B13=" b13 \
                        " B13fact=" fb13 " B14=" b14 " B14fact=" fb14 \
                        " Cbanner=" c " C=" fc \
                        " guard=" g " penalty=" pen \
                        " C1=" c1 " C2=" c2 " C3=" c3 \
                        " B-7=" v " B-9=" b9 " B10fact=" fb10 " B11fact=" fb11 \
                        " B12fact=" fb12 " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so the held gates cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < u && u < fb11 && fb11 < w \
                     && w < b12e && b12e < fb12 && fb12 < b13 && b13 < fb13 \
                     && fb13 < b14 && b14 < fb14 && fb14 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, B-11, B-12 (jars last), B-13, B-14, family C, its score, the report"; exit 1 } \
               if (!(b14 < g && g < pen && pen < fb14)) { \
                  print "the penalty must be the last line inside B-14s guard and B-14 must be paid after it closes"; exit 1 } \
               if (!(g < c)) { \
                  print "family C opens a measured guard: its three legs read the jars, not the source baseline"; exit 1 } \
               if (!(c < c1 && c1 < c2 && c2 < c3 && c3 < fc)) { \
                  print "family Cs three legs must be read in order between its banner and its score"; exit 1 } \
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

# C-1's and C-2's instrument, rehearsed on four jars this image builds itself.
#
# arqgate.Modules is the only gate in this file that reads a MANIFEST, and the
# only one that asks java.lang.module anything, so the three answers it can
# give are produced here on purpose rather than waited for:
#
#   manifest  the jar carries Automatic-Module-Name and that is its name
#   derived   it does not, and the platform invents one from the FILE NAME
#   none      the platform cannot read it as a module at all, with a reason
#
# It is also a SEPARATE JAR from arqgate.jar and the first assertion below is
# why. arqgate.jar is built --release 8 so that one jar runs under every JDK
# this ladder grades on, and java.lang.module does not exist at 8; arqmodules.jar
# is built --release 11 and can therefore never join it. The two floors are
# asserted by reading the two jars' own constant pools with the gate that reads
# constant pools, which is the only instrument in the image that could tell
# them apart.
#
# THE WRAPPED MANIFEST IS WHY THIS GATE IS NOT A GREP, and the `wrap` face
# exists to prove it. A manifest value longer than 72 bytes is folded onto a
# continuation line beginning with a space, and every real module name in this
# tree is longer than that. The face declares a 68-character name, `jar` folds
# it, java.util.jar.Manifest reads it back whole, and a line scan for the same
# string finds nothing. Both directions are asserted, because the half that
# matters is the half that would have passed silently.
#
# THE DERIVED NAME IS NOT THE PACKAGE NAME, which is the whole reason C-2 is a
# leg of its own rather than a stricter C-1. The `beta` face holds one class in
# org.arq.mreh.b and no attribute; the platform calls it `mrehbeta`, after the
# jar file. A tree that declares nothing at all still reads derivable=12 and
# scores zero on C-2, and a gate that only counted readable modules would have
# handed this rung's must-flip to a candidate who did nothing.
#
# AND THE LADDER'S SIXTH TRAP, in the place it was actually waiting: `jar
# --describe-module` prints "Unable to derive module descriptor for:" and exits
# ZERO. A screen written the obvious way -- run the tool, check the status --
# reports the `top` face as a healthy module and would have reported this
# tree's impl-base jar as one too. The exit status is pinned here at 0 so that
# the reason this gate calls the API instead is on the record rather than in a
# comment.
RUN set -eux; \
    unzip -l /tests/gates/build/arqmodules.jar > /tmp/mj.txt; \
    grep -q 'arqgate/Modules\.class' /tmp/mj.txt; \
    rm -f /tmp/mj.txt; \
    SCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"; \
    java -cp "${SCP}" arqgate.Scan /tmp/modmaj.txt /tests/gates/build/arqmodules.jar; \
    grep -q '^major\.55=' /tmp/modmaj.txt; \
    if grep -q '^major\.52=' /tmp/modmaj.txt; then \
        echo "CONTROL FAILED: arqmodules.jar was built at arqgate.jar's floor" >&2; exit 1; \
    fi; \
    rm -f /tmp/modmaj.txt /tmp/modmaj.txt.pairs; \
    M=/tmp/mreh; mkdir -p "${M}"; cd "${M}"; \
    printf 'package org.arq.mreh.a;\npublic class A {}\n' > A.java; \
    printf 'package org.arq.mreh.b;\npublic class B {}\n' > B.java; \
    printf 'public class Top {}\n' > Top.java; \
    mkdir -p ca cb ct; \
    javac -d ca A.java; \
    javac -d cb B.java; \
    javac -d ct Top.java; \
    printf 'Manifest-Version: 1.0\nAutomatic-Module-Name: org.arq.mreh.a\n' > mf-a.txt; \
    N=org.jboss.arquillian.mreh.averylongmodulename.thatwraps.onthemanifest; \
    test "$(printf %s "${N}" | wc -c)" -gt 60; \
    printf 'Manifest-Version: 1.0\nAutomatic-Module-Name: %s\n' "${N}" > mf-w.txt; \
    jar cfm mrehalpha-1.0.0-SNAPSHOT.jar mf-a.txt -C ca .; \
    jar cf  mrehbeta-1.0.0-SNAPSHOT.jar          -C cb .; \
    jar cf  mrehtop-1.0.0-SNAPSHOT.jar           -C ct .; \
    jar cfm mrehwrap-1.0.0-SNAPSHOT.jar  mf-w.txt -C cb .; \
    printf '%s/mrehalpha-1.0.0-SNAPSHOT.jar\n%s/mrehbeta-1.0.0-SNAPSHOT.jar\n%s/mrehtop-1.0.0-SNAPSHOT.jar\n' \
        "${M}" "${M}" "${M}" > l1.txt; \
    java -cp /tests/gates/build/arqmodules.jar arqgate.Modules o1.txt l1.txt; \
    cat o1.txt; \
    grep -q '^mod\.total=3$'     o1.txt; \
    grep -q '^mod\.declared=1$'  o1.txt; \
    grep -q '^mod\.derivable=2$' o1.txt; \
    grep -q '^mod\[0\]=mrehalpha name=org\.arq\.mreh\.a source=manifest$' o1.txt; \
    grep -q '^mod\[1\]=mrehbeta name=mrehbeta source=derived$'            o1.txt; \
    grep -q '^mod\[2\]=mrehtop name=- source=none reason=Top\.class_found_in_top-level_directory' o1.txt; \
    echo "${M}/mrehwrap-1.0.0-SNAPSHOT.jar" > l2.txt; \
    java -cp /tests/gates/build/arqmodules.jar arqgate.Modules o2.txt l2.txt; \
    cat o2.txt; \
    grep -q '^mod\.declared=1$' o2.txt; \
    grep -q "^mod\[0\]=mrehwrap name=${N} source=manifest\$" o2.txt; \
    unzip -p mrehwrap-1.0.0-SNAPSHOT.jar META-INF/MANIFEST.MF | tr -d '\r' > mw.txt; \
    grep -q '^ ' mw.txt; \
    if grep -q "^Automatic-Module-Name: ${N}\$" mw.txt; then \
        echo "CONTROL FAILED: the manifest did not fold, so the line-scan argument is untested" >&2; exit 1; \
    fi; \
    if jar --describe-module --file mrehtop-1.0.0-SNAPSHOT.jar > jd.txt 2>&1; then \
        grep -q 'Unable to derive module descriptor' jd.txt \
            || { echo "CONTROL FAILED: the jar tool derived a descriptor for the top-level face" >&2; exit 1; }; \
    else \
        echo "CONTROL FAILED: jar --describe-module now reports an underivable jar in its exit status;" >&2; \
        echo "  arqgate.Modules is still correct, but this rehearsal's premise has changed" >&2; exit 1; \
    fi; \
    cd /; rm -rf /tmp/mreh; \
    echo "step 08: the module gate reads a folded manifest, names an underivable jar, and does not trust jar(1)'s exit status"

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
    echo "step 08 verifier image: assertions passed"

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
                  /tests/fixtures/mkfix08.py /tests/fixtures/c/pinned/report.txt \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Lib.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk11-step08 /tests/baseline \
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
    echo "step 08: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
