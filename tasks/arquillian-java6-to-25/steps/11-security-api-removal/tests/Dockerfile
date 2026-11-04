# Verifier image for step 11, built with steps/11-security-api-removal/tests/
# as the Docker build context: it cannot reach the task root, environment/, or
# another step, so everything grading needs is copied in here. Harbor skips its
# usual tests upload and runs /tests/test.sh from this image.
#
# THE RUNG IS "SECURITY API REMOVAL": the class files move from 65 to 66, the
# sixty-seven uses of the Security Manager API go, the catch clauses that
# declare a parameter nothing reads take `_` instead of a name, and four of the
# fourteen `setAccessible` field sites become VarHandles. One level flip and
# three moves, and for the first time on this ladder one of them is a DELETION
# rather than a conversion -- which is what most of the shape below exists to
# price.
#
# THIS RUNG HAS TWO MUST-FLIPS AND ONE OF THEM IS THE COMPILER'S. The first is
# the class-file version: 65 -> 66, because `catch (X _)` was preview through
# 21 and standard only at 22, so a tree that did the language work without
# moving the level does not compile. The second is not a choice the candidate
# can get wrong quietly, as it was at every rung from 05 to 10:
# `-Xlint:removal -Werror` is already in the build file this rung is handed,
# JEP 486 terminally deprecates `AccessController.doPrivileged` and
# `PrivilegedAction`, and THE TREE DOES NOT BUILD until C-2 is done. Measured
# in both directions before anything was written -- the seed fails the build
# with the flag, the golden passes it (NOTES 16.3, 16.4) -- so family A, C-1
# and C-2 read zero together on a tree that has not started, and a release bump
# on its own buys nothing.
#
# C-1 OWNS THE LEVEL AND B-4 IS FORBIDDEN TO READ IT, which is why
# baseline/level.txt carries two numbers here as it did at steps 09 and 10.
# `major=66` is what C-1 wants; `was=65` is what B-4 holds -- the level the
# tree ARRIVES at, step 10's target. A B-4 that read C-1's number would be a
# regression gate that fails on the tree it is supposed to be protecting, which
# is the ladder's ninth trap. AND AT THIS RUNG THE LEVEL IS TWO NUMBERS IN THE
# BUILD FILE: `options.release` and the explicit TARGET_JVM_VERSION_ATTRIBUTE
# step 01 pinned on four resolvable classpaths. Move one and not the other and
# a 16-project reactor stops resolving. C-1 greps for neither; it reads the
# class files the build actually emitted, so the step does not dictate whether
# the candidate moves the daemon to 25 or declares a toolchain. NOTES 16.5,
# 16.6.
#
# THE GRADING JDK MOVES TOO: eclipse-temurin:25-jdk, from 21. Not because the
# scorer wants a newer shell, but because arqgate.SourceScan runs inside this
# image's javac and has to see what this rung asks for AS LANGUAGE. An unnamed
# catch parameter is a `VariableTree` whose name is `_`, and a javac older than
# 22 reports `catch (IOException _)` as a syntax error rather than as a
# converted site -- the whole population would read as a broken file. An era
# directory is named for the code era it records and for the JDK it was
# recorded under, and this rung's is rec/jdk25-step11, so both halves of the
# name agree with the image above for the third time on this ladder.
#
# THE MIGRATION MOVES THIRTY-SEVEN TRANSCRIPTS AND THE SUITE STAYS AT 939.
# This is the first era on this ladder that re-records rather than only adding,
# and rebuild-verifier.sh asserts the shape against bytes: `diff -rq` between
# rec/jdk21-step10 and rec/jdk25-step11 reports THIRTY-SEVEN `differ` lines and
# not one `Only in`. A transcript added or removed here would mean the suite
# moved under a rung that claims not to move it, and the verifier refuses to
# stage that.
#
# WHY A PACKAGING TRANSCRIPT HAD TO MOVE, which is the finding this rung's era
# exists to record. `SecurityActions$GetTcclAction` is a private enum
# implementing `PrivilegedAction<ClassLoader>`. `-Xlint:removal -Werror`
# refuses to build while it exists whether or not anything calls it, so C-2
# deletes it -- and it is a CLASS FILE inside arquillian-protocol.jar. Thirty-
# six `servlet/pack*` and `servlet/append*` transcripts enumerate that archive
# entry by entry; one class file fewer shortens the list, renumbers everything
# after it and decrements the `entries=` figure the deployment processors log.
# The thirty-seventh is desc/adv-nesting-over-jaxp-limit, which moves for the
# JDK rather than for the migration -- jdk.xml.maxElementDepth defaults to 100
# from JDK 24 -- and its re-recorded copy is byte-identical to rec/jdk25's, so
# the input tree still matches on it. The 36 are enumerated in
# authoring/ids-step11-moved.txt with the cause of each.
#
# SO FAMILY A DOES NOT READ 1.000000 ON THE TREE THIS RUNG IS HANDED, for the
# first time on this ladder. It reads 903/939 = 0.961661. Every earlier rung
# could write its floor as 0.30 + 0.175 = 0.475 without measuring. This one
# cannot, and neither can it write the 0.175: B-6's expectation is cut from the
# FINISHED tree and the input tree still publishes the 42 signatures C-2
# removes, so family B reads 0.997882 there too. The floor is
# 0.30 x 0.961661 + 0.70 x 0.25 x 0.997882 = 0.463128, with 0.036872 of room
# under the 0.5 a step is required to leave on the table. NOTES 16.17, 16.18.
#
# THIS RUNG'S THREE LEGS ARE THREE FROZEN POPULATIONS, and unlike every rung
# below they are stored as RAW PLUGIN RECORDS -- `<file>:<line> key=value ...
# in=<method>` -- rather than pre-bucketed. The line number is kept so a reader
# of a failed run can find the site, and the bucket is derived on both the
# frozen side and the measured side by the same three lines. test.sh's
# pbuckets/ptot/psum/pdep helpers are that derivation and nothing else reads
# these files:
#
#   baseline/priv.txt        C-2. 67 sites in 27 methods across 7 files -- the
#                            seven SecurityActions classes. 20 doPrivileged
#                            CALLS and 47 TYPE REFERENCES to AccessController,
#                            PrivilegedAction, PrivilegedExceptionAction or
#                            PrivilegedActionException. THE CLASSES ARE NOT
#                            DELETED: the hold half is the 13 reflective sites
#                            those same seven files must still contain
#                            afterwards, 7 constructor.newInstance calls
#                            counted per FILE and 6 scanner setAccessible reads
#                            counted per bucket.
#   baseline/catchparam.txt  C-3. 93 catch clauses in main sources: 35 declare
#                            a parameter the body never reads and become
#                            `catch (X _)`; 58 read theirs and must keep their
#                            names, because `_` is not a name and cannot be
#                            referenced. THE DENOMINATOR IS 28 AND NOT 35 --
#                            seven of the 35 and seven of the 58 sit in the
#                            getConstructor ladders C-2 deletes, so the golden
#                            has 79 clauses = 28 unnamed + 51 read + 0 unread.
#   baseline/varhandle.txt   C-4. 14 setAccessible sites in main sources and a
#                            measured ZERO VarHandles. 4 are plain private-
#                            FIELD reads and writes and convert to
#                            privateLookupIn().unreflectVarHandle() -- two
#                            calls per site, 8 in all, which is what the
#                            arrival counts. 10 must not: 6 in the annotation
#                            scanners, which hand Field and Method OBJECTS back
#                            to their callers, and 4 constructor or setter
#                            INVOCATION sites where reflection reports
#                            inaccessibility as a checked IllegalAccessException
#                            and a Lookup does not.
#
# Each leg is scored as a min() over halves stated on the same buckets, so
# deleting the code pays nothing, and C-4 is multiplied on top of that by the
# fraction of baseline/vhids.txt still matching -- the four recordings whose
# outcome IS that IllegalAccessException, asserted to be every recording in the
# era where THIS TREE raised one. A fifth carries the same exception out of
# java.util.ServiceLoader inside java.base and is B-15.3's, not C-4's.
#
# STEP 10'S FAMILY C ARRIVES AS B-17, MINUS ITS FIRST LEG. The ladder's weight
# rule gives the newest inherited leg 0.12 and scales everything before it by
# 0.88. What is inherited is step 10's C-2 through C-4 -- the reflective type
# tests, the pattern switches and the sequenced first-element reads -- under
# the sub-leg numbers step 10 gave them, B-17.2 to B-17.4, so a finding carries
# back to the rung that stated it without a translation table. There is no
# B-17.1 because step 10's C-1 was its class-file level, and 65 is what B-4
# holds here. Its four population files stage under their own names --
# reftest.txt, chain.txt, seqfirst.txt, swids.txt -- and so does apinorm.txt,
# B-6's normalisation list.
#
# AND TWO INHERITED POPULATIONS HAD TO BE RE-CUT, WHICH IS NEW AND IS THE
# NINTH TRAP ARRIVING THROUGH THE BASELINE. Step 09's ipattern.txt named 18
# conv buckets over 21 sites; C-2 deletes seven of them with the getConstructor
# ladders, so a CORRECT tree could have read at most 0.666667 on B-16.2. Step
# 06's accessible.txt named 12 setAccessible buckets; C-4 converts 4 of the 14
# sites, three of them in those buckets, so a CORRECT tree could have read at
# most 0.714286 on B-13.3. Both are re-cut against what survives -- ipattern.txt
# to 14 conv buckets over 17 sites, accessible.txt split into a `guard` half
# held verbatim at 12 and a `setacc` half re-stated over the 10 calls that
# outlive C-4 -- and both are re-cut from the GOLDEN, because a seed
# re-measurement would have reproduced the damage rather than found it. The
# stages below assert that split rather than trusting it. NOTES 16.16.
#
# THREE INSTRUMENTS, NOT ONE. The typed AST (arqgate.SourceScan) answers the
# construct questions in B-10 through B-17 and in three of family C's four
# legs; the jars the candidate's build PUBLISHED are read twice, by
# arqgate.Scan for the class-file major that C-1 grades and B-4 holds, and by
# arqgate.Modules for the Automatic-Module-Name manifests B-15 holds; and
# javac's own acceptance of the tree (source-gate.sh) is the control on both.
# A manifest attribute is not visible in the AST and a construct is not
# visible in a manifest, so no gate in this file can be satisfied by moving
# work from one instrument's reach into another's.
#
# THE SOURCE GATE COMPILES AT -source 22 AND -source 21 HERE, and the control
# is expected to read `no` -- for the third time on this ladder and for the
# narrowest reason yet. Everything else this tree carries is standard at 21:
# pattern switches at 21, sealed types at 17, type patterns at 16. Sequenced
# collections and VarHandles are API and not language, and the deletion of a
# deprecated API is not a language level at all. The unnamed catch parameter
# alone is what a 21 compile rejects, so `no` here is an unscored signal about
# C-3 specifically. Nothing scores the control in either direction; it is
# recorded so that a broken compile at 22 can be told apart from a tree that
# does not compile at all.
#
# WHAT THIS RUNG DOES NOT ASK FOR, and each omission is a decision rather than
# an oversight. NOT the Foreign Function and Memory API: sun.misc.Unsafe is not
# used anywhere in this tree, so there is nothing for java.lang.foreign to
# replace. NOT a Gatherer: no stream here has a custom intermediate operation.
# NOT ScopedValue, which is preview at this level and has no inheritable-
# ThreadLocal population to convert. NOT `Thread.ofVirtual`: the four `Thread`
# uses are the JMX test runner's own and a platform thread is what they mean.
# NOT `Subject.callAs`: nothing in this tree runs as a Subject, and replacing
# a doPrivileged with one would be inventing a security model rather than
# removing one. NOT a module descriptor: step 08 measured why. NOTES 16.1
# carries the measurement for each.
#
# `unzip` is asserted present below rather than assumed, and B-12e is why. That
# gate reads the constant pools of the jars the candidate's build published --
# a different instrument on a different artifact from everything else in the
# file -- and a missing unzip makes it report "the published jars carry no
# class files", which reads as a finding about the tree rather than as a broken
# image. That would be the ladder's sixth trap sprung in the one place where
# nothing else could corroborate it.
#
# What this rung does NOT carry forward is step 10's own source.txt,
# target.txt, level.txt and api.txt. Every rung below step 07 staged its
# predecessor's pair beside its own; here they would both land as
# baseline/source.txt and baseline/target.txt and which one a gate read would
# come down to the order of a staging loop. What step 10 needs carried forward
# is carried instead as the seventeen target.b17.* keys inside step 11's
# target.txt -- RE-MEASURED ON THIS STEP'S SEED rather than copied, because
# step 10's own work rewrote source this rung is handed -- and step 10's four
# files are staged under their own names.
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
FROM eclipse-temurin:25-jdk

# util-linux for setpriv (run_unprivileged). unzip for the gate build's jar
# assertions and for the Gradle distribution. The rest is what any
# diff-and-count verifier needs; nothing here comes from the agent's image, so a
# candidate that breaks its own toolchain does not change how it is graded.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash ca-certificates coreutils curl diffutils gawk unzip util-linux \
    && rm -rf /var/lib/apt/lists/*

# Gradle, present only long enough to generate a wrapper and pull the two caches
# below; removed before the image is finished. 8.14.5, UNCHANGED, and the
# reason it does not move with the JDK is that it is not this rung's to move.
# Steps 06 to 08 pinned 8.14.5 because Gradle 9 needs Java 17+ and their images
# had only JDK 11; this image has 17 and could run 9. It still does not.
# warm/ is byte-identical in every step's context and is shared as one layer,
# B-2 holds the wrapper version step 01 established, and a build-tool upgrade
# is a migration in its own right rather than a side effect of a language
# level. A rung that changed it here would be grading two things and
# attributing both to one.
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
# under per-step names (fixtures-11/, baseline-11/, assets/STAGED-11.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-11; \
    test -d /tests/baseline-11; \
    test -f /tests/assets/STAGED-11.txt; \
    mv /tests/fixtures-11 /tests/fixtures; \
    mv /tests/baseline-11 /tests/baseline; \
    mv /tests/assets/STAGED-11.txt /tests/assets/STAGED.txt
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
RUN echo step11 > /tests/STEP_ID

# The bytecode gate. Built here, from source, with this image's own JDK 25
# javac -- the same jar the JDK 25 images of steps 12-13 build from the same
# source. arqgate.SourceScan is the javac plugin three of family C's four legs
# read, and three of B-17's, so this layer builds the step's principal
# instrument and not only B-4's and C-1's. gates/build.sh compiles it at the
# image's default level, deliberately: the plugin runs INSIDE this image's
# javac and has to match it, while the tree it inspects is compiled at
# -source 22 by source-gate.sh. Two levels, two purposes, and conflating them
# is how a plugin comes to be built for a compiler that will not load it.
#
# AND THE DEFAULT LEVEL IS WHY THIS LAYER HAD TO WAIT FOR THE IMAGE TO MOVE --
# ONCE, AT 17, AND NOT AGAIN HERE OR AT THE RUNG BELOW. The plugin step 09
# shipped asks javac about Modifier.SEALED, ClassTree.getPermitsClause() and
# InstanceOfTree.getPattern(); none of those is in JDK 11's com.sun.source, so
# that source does not compile in step 08's image at all. NOTES 14.5 calls that
# the one-way door and puts it at 17, and this rung deliberately does not move
# it either: the three readings this step ADDS are written to stay behind it,
# and the two step-06 readings its family C newly leans on are unchanged.
#
#   priv         a RESOLVED reference to one of the four java.security types,
#                through Trees.getElement and TypeElement.getQualifiedName, and
#                a resolved AccessController.doPrivileged call site. JEP 486
#                deprecates those types terminally; it does not remove them, so
#                under JDK 25 they are still on the class path and still
#                resolve, and the reading is a reading of what the source SAYS
#                rather than of what the platform still has.
#   catchparam   CatchTree.getParameter(), which is JDK 6. THE UNNAMED CASE IS
#                RECOGNISED BY THE EMPTY NAME AND BY `_` BOTH, because javac 22
#                binds `catch (Foo _)` to a declaration whose name is the EMPTY
#                name rather than the underscore -- a scanner testing only for
#                the underscore spelling reads every converted site as an
#                ordinary unread parameter, which is the bucket the arrival
#                half is being distinguished FROM. Measured, not assumed.
#                Parsing the construct needs javac 22, which is what the image
#                above is for; reading the tree it produces needs no
#                com.sun.source this plugin did not already have.
#   varhandle    a resolved MethodHandles.privateLookupIn call and a resolved
#                MethodHandles.Lookup.unreflectVarHandle call, by owner and not
#                by spelling, which is why an `unreflectVarHandle` on something
#                that is not a Lookup cannot pay for the arrival.
#
# The two it leans on are step 06's `accessible` and `newinstance`, both
# UNCHANGED in the plugin. What changed for them is on the baseline side:
# accessible.txt is now two halves -- 12 `guard` buckets held verbatim and 10
# `setacc` buckets re-stated over what outlives C-4 -- and the `newinstance`
# records are what C-2's hold half counts. A plugin reading that moved with the
# baseline would have made the re-cut unfalsifiable.
#
# So the same SourceScan.java still builds in step 09's image, and a plugin
# built here is one step 09's javac could also have built. The door is still a
# door -- a re-stage of step 08 or below would hand it a plugin its own javac
# cannot build -- authoring/rebuild-verifier.sh refuses to re-stage a step
# whose frozen SourceScan.java differs from the authoring master for that
# reason, and NOTES 14.2 records it as a known-open hazard rather than a fixed
# one.
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
        case "${v}" in 25.*) found=$((found + 1));; \
            *) echo "RULE-6 VIOLATION: step 11 grades on JDK 25 only, found ${v} at ${j}" >&2; exit 1;; \
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
    # java.lang.module rather than through unzip: B-15 runs \
    # gates/build/arqmodules.jar over them. That channel needs no unzip and is \
    # checked by the JVM sweep above, which is why the two are named apart. \
    # B-15 holds that same reading at this rung; family C reads the class-file \
    # major out of the same jars through arqgate.Scan, a third channel again. \
    command -v unzip >/dev/null; \
    unzip -v | head -1

# The payload landed. Counts, not directory existence: a COPY that arrived empty
# and a COPY that arrived whole both leave the directory there. The numbers are
# repeated from assets/STAGED.txt rather than read out of it, so that a staging
# run against the wrong era or the wrong probe-lib pin fails the image build.
RUN set -eux; \
    grep -q '^staged.era=jdk25-step11$' /tests/assets/STAGED.txt; \
    grep -q '^staged.orderdep=1$'      /tests/assets/STAGED.txt; \
    test "$(grep -c '^as_untrusted timeout -s KILL' /tests/runner/score.sh)" = 2; \
    test "$(grep -vc '^#' /tests/baseline/orderdep.txt)" = 1; \
    grep -q '^staged.ids=939$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.rec=939$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.problib.jars=80$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/assets/ids.txt)" = 939; \
    test "$(find /tests/assets/rec/jdk25-step11 -name '*.txt' | wc -l)" = 939; \
    test ! -d /tests/assets/rec/jdk8-step06; \
    test ! -d /tests/assets/rec/jdk11-step07; \
    test ! -d /tests/assets/rec/jdk11-step08; \
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
    # B-6's surface is THIS rung's and not step 10's: 1105 signatures over \
    # 272 types, down from 1147 over 279, because C-2 withdraws the seven \
    # SecurityActions$GetTcclAction enums along with the doPrivileged calls \
    # they wrapped. Staging step 10's api.txt would score a correct tree \
    # 1105/1147 on a gate it satisfies exactly, so rebuild-verifier.sh stages \
    # baseline-step11-api.txt and this is where a slip back would be caught. \
    grep -q '^staged.api=1105$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/baseline/api.txt)" = 1105; \
    test "$(grep -c ' TYPE ' /tests/baseline/api.txt)" = 272; \
    test "$(grep -c 'GetTcclAction' /tests/baseline/api.txt)" = 0; \
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
    grep -q '^staged.baseline.files=51$' /tests/assets/STAGED.txt; \
    test "$(ls /tests/baseline | grep -c '\.txt$')" = 51; \
    grep -q '^staged.fixtures=1230$'    /tests/assets/STAGED.txt; \
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
    test "$(find /tests/baseline -type f | wc -l)" = 51; \
    # 1230, up from step 10's 975, and the increase is one thing rather \
    # than two: fifteen more configurations, each carrying the same \
    # seventeen files every other one carries. 15*17 = 255 and 975 + 255 = \
    # 1230. The arithmetic is asserted rather than the total alone, because a \
    # fixture tree that lost a whole configuration and gained seventeen \
    # loose files elsewhere would satisfy a count. \
    # \
    #   8   at the top: the four rehearsals -- b89, c, pv and src -- plus \
    #       mkfix.sh, mkfix11.py and the two j5 answer files, j5-pinned.txt \
    #       and j5-golden.txt. No __pycache__: mkfix11.py is imported by the \
    #       generator as well as run, and the .pyc it leaves behind staged \
    #       once before this line was written. \
    #   9   cl/: step 02's self-stop cases, unchanged since it. \
    #   2   smoke/: Smoke.java and Lib.java, the source-gate rehearsal's tree. \
    # 1211  c/: 72 configurations. 70 of them hold twelve published jars and \
    #       FIVE files of their own -- answers.txt, behavior.summary, \
    #       report.txt, scan.txt, why.txt. The fifteen this rung adds are \
    #       P1 through P15 and they are its three new legs of family C: \
    #       P1..P5 the seven `doPrivileged` wrappers C-2 withdraws, P6..P9 \
    #       the catch parameters C-3 renames, P10..P15 the four fields C-4 \
    #       reaches through a VarHandle instead of forcing open. \
    #       Three of the fifteen are rows about an ARITHMETIC rather than \
    #       about an edit, and they are the reason the table is a table: P9 \
    #       moves all three of C-3's terms at once, because a leg that \
    #       min()ed two of them or averaged any pair would read that tree \
    #       above its worst and no single-term row would show it; P13 builds \
    #       all four handles and leaves all four `setAccessible` calls \
    #       beside them, which is the one row that says C-4's departure is \
    #       read off the `accessible` records and never off `varhandle`; and \
    #       P15 moves the structure and the behaviour scaling together, \
    #       which is the only place a product can be told from a min() or a \
    #       sum. \
    #       Each carries a scan.txt and twelve jars like every other \
    #       configuration, because the columns that must NOT move are as \
    #       much of the assertion as the ones that must: a C-2 that had \
    #       noticed a class-file major, or a B-15 that had noticed a \
    #       VarHandle, shows up in the table as a moved cell in a column the \
    #       fixture never touched. Four rows move an inherited column ON \
    #       PURPOSE and say so in their why.txt, which is new at this rung \
    #       and is C-2 and C-4 editing records two held gates already read: \
    #       P2 and P5 move B-13's `newinstance.ctor` in opposite directions, \
    #       10 against a want of 13 and 14 against the same want, and P3 and \
    #       P12 move B-13.3's surviving `setAccessible` calls, 8 of 10 and 9 \
    #       of 10. \
    #       The two exceptions are the two ABSENCES the rungs below test, \
    #       carried forward because both are still live here, and they \
    #       are absences of different things. K20 holds the five files and NO \
    #       jars -- the configuration where nothing resolves, where \
    #       `gate.cp=no-jars` and `gate.b15.mod=no-module-jars` are the two \
    #       sentinels. L2 holds the twelve jars and only FOUR files: it is the \
    #       tree whose build left no class files behind, so there is no \
    #       scan.txt to read and `gate.c1=no-scan` is the third. None of the \
    #       three is spelled 0.000000, which is the distinction those fixtures \
    #       exist to hold open. \
    #       70*17 + 5 + 16 = 1211. \
    test "$(find /tests/fixtures -type f | wc -l)" = 1230; \
    test "$(find /tests/fixtures -maxdepth 1 -type f | wc -l)" = 8; \
    test -f /tests/fixtures/mkfix11.py; \
    test ! -e /tests/fixtures/mkfix10.py; \
    test ! -e /tests/fixtures/__pycache__; \
    test "$(find /tests/fixtures -name '*.pyc' | wc -l)" = 0; \
    test "$(find /tests/fixtures/cl -type f | wc -l)" = 9; \
    test "$(find /tests/fixtures/smoke -type f | wc -l)" = 2; \
    test "$(find /tests/fixtures/c -type f | wc -l)" = 1211; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d | wc -l)" = 72; \
    test "$(find /tests/fixtures/c -name '*.jar' | wc -l)" = 852; \
    test "$(find /tests/fixtures/c/K20 -name '*.jar' | wc -l)" = 0; \
    test ! -e /tests/fixtures/c/L2/scan.txt; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'scan.txt' | wc -l)" = 71; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'N*' | wc -l)" = 15; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'P*' | wc -l)" = 15; \
    for cfg in $(ls /tests/fixtures/c); do \
        n=$(find "/tests/fixtures/c/${cfg}" -maxdepth 1 -type f | wc -l); \
        case "${cfg}" in L2) want=4 ;; *) want=5 ;; esac; \
        if [ "${n}" != "${want}" ]; then \
            echo "fixtures/c/${cfg} holds ${n} top-level files, not the ${want} it should" >&2; \
            exit 1; \
        fi; \
    done; \
    # And the fifty-one by NAME, because a count is satisfied by any fifty-one \
    # files and this rung's recipe does two different things to reach the \
    # number. It ADDS four, one per leg of family C after the first -- C-1 is \
    # the level flip and level.txt already carries it: priv.txt is the 67 \
    # `doPrivileged` and `AccessController` records over 27 buckets in 7 files \
    # that C-2 measures a departure against, plus the 13-site payload the same \
    # leg min()s that departure against; catchparam.txt is all three halves of \
    # C-3 in one file -- the 93 main-scope catch parameters, the 79 that must \
    # still be there afterwards with 28 of them unnamed, and the 14 that leave \
    # with the `getConstructor` ladders; varhandle.txt is C-4's arrival \
    # measured against a zero plus the verdict on each of the 14 \
    # `setAccessible` sites that arrival is carved out of; and vhids.txt the 4 \
    # recorded transcripts C-4's behaviour term is scaled by. \
    # And it REPLACES six the rung below already staged. source.txt because it \
    # must be step 10's OUTPUT measured by the gate that ships here, which is \
    # the only tool that emits the four record kinds this rung reads -- step \
    # 10's frozen SourceScan.java contains the string `catchparam` nowhere at \
    # all, so its source.txt would report this rung's C-3 population as an \
    # argued zero rather than a measured one; target.txt because it carries \
    # step 10's denominators forward under target.b17.* spellings and because \
    # this rung's own keys had to be MEASURED on a tree where step 10's work \
    # is already done; api.txt because the published surface this rung holds \
    # is the one step 10 left, byte-identical and re-derived rather than \
    # inherited so that the claim is measured; and level.txt because its two \
    # numbers both move -- `major=66` for C-1 and `was=65` for B-4, where the \
    # rung below wrote 65 and 61. \
    # THE LAST TWO OF THE SIX ARE NEW IN KIND AND ARE THE REASON THIS LIST IS \
    # NAMES AND NOT A TALLY. accessible.txt and ipattern.txt are FROZEN SETS \
    # that every rung since 06 and since 09 has staged byte for byte, and this \
    # is the first rung that cannot. C-4 converts four of accessible.txt's 14 \
    # `setAccessible` sites, so its setacc half drops to 10 while its 12 \
    # guards stay; and C-2 deletes the seven `SecurityActions#getConstructor` \
    # ladders, so seven of step 09's 18 conv buckets go with the methods that \
    # held them. A stale copy of either stages, counts, and reads -- it simply \
    # asks a correctly migrated tree about code it was told to remove. \
    # A replacement is invisible to a count -- forty-one plus ten is fifty-one \
    # whether or not source.txt is the one this step meant -- which is why the \
    # names are listed and not just tallied, and why the two that changed \
    # SHAPE rather than arriving are called out here by name. \
    # Forty-six of the fifty-one are record POPULATIONS rather than key=value \
    # facts, and most of those are read in the ARRIVED state. That is the \
    # failure this loop exists for: a population that failed to stage does not \
    # make the scorer fail -- wantbuckets() over a missing file yields an \
    # empty want, frac() over an empty denominator yields the scorer's \
    # zero-denominator answer, and the gate pays the tree for a migration \
    # nobody checked. Every one of the fifty-one is asserted non-empty here by \
    # name, so a set that did not stage is a build failure and not a free leg. \
    for b in accessible api apinorm artifacts boxedctor builds catchparam \
             chain classnames collectmethods compat http httpids ipattern \
             junit5 lambdafiles level modnames newinstance nioids nshold \
             nsmove optarr optbuckets optops orderdep priv reftest samifaces \
             sealed seqfirst sinks source streamfiles strnl svc svcids \
             swarrow swids target tbids testng-cased testresults tests \
             timesites toarray valuetypes varbuckets varhandle vhids views; do \
        if [ ! -s "/tests/baseline/${b}.txt" ]; then \
            echo "baseline/${b}.txt did not stage, or staged empty" >&2; exit 1; \
        fi; \
    done; \
    # ...and nothing else, so that a name this list does not know about cannot \
    # arrive unnoticed -- which is exactly how a stale copy of step 10's \
    # accessible.txt or ipattern.txt would arrive if a future edit dropped \
    # this rung's ten re-derivations from the staging recipe and left the \
    # inherited files in place. The count would not move. \
    ls /tests/baseline | sed 's/\.txt$//' | LC_ALL=C sort > /tmp/blist.txt; \
    printf '%s\n' accessible api apinorm artifacts boxedctor builds catchparam \
             chain classnames collectmethods compat http httpids ipattern \
             junit5 lambdafiles level modnames newinstance nioids nshold \
             nsmove optarr optbuckets optops orderdep priv reftest samifaces \
             sealed seqfirst sinks source streamfiles strnl svc svcids \
             swarrow swids target tbids testng-cased testresults tests \
             timesites toarray valuetypes varbuckets varhandle vhids views \
             | LC_ALL=C sort > /tmp/bwant.txt; \
    if ! cmp -s /tmp/blist.txt /tmp/bwant.txt; then \
        echo "baseline/ is not the fifty-one files this step stages:" >&2; \
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

# The ten blocks that read the typed AST, the published jars and the
# class-file scan, on seventy-two trees.
#
# fixtures/c-rehearsal.sh lifts the hoisted source-AST block, B-10, B-11, B-12,
# B-13, B-14, B-15, B-16, B-17 and the whole of family C out of the SHIPPED
# test.sh by their own banners, composes them in file order behind a srcgate()
# that hands them a fixture's report instead of compiling anything, and runs
# them once per fixture. Two of the seventy-two are measurements -- `pinned`,
# the tree this step is handed, which is step 10's golden, and `golden`, this
# step's own solution -- and seventy are stated edits of a measurement.
#
# TEN blocks and not nine, which is the way this rehearsal differs from step
# 10's. The compiles were hoisted above family B at step 04 so B-10 could read
# them, step 05 put B-11 beside it, step 06 added B-12, step 07 added B-13,
# step 08 added B-14, step 09 added B-15, step 10 added B-16 and this rung adds
# B-17: the counters are read in one place and spent in NINE others. The
# extraction asserts all ten -- and the eleventh piece described below -- are
# disjoint and in that order in the file, which is not decoration --
# a hoist that silently un-hoisted would leave a held gate reading variables
# that do not exist yet, and under `set -u` that is a dead verifier rather than
# a wrong number. It also asserts srcgate is called three times in the source
# block and not once anywhere else, and that no block runs javac, docker or
# gradlew.
#
# AND AN ELEVENTH PIECE THAT IS NOT A BLOCK. Five bucket helpers -- cbuckets,
# fcount, capsum, capwant and cnames -- sit in the gap between B-15's last fact
# and B-16's banner, hoisted there by the rung below because B-16 reads four of
# them; this rung's B-17 reads four as well and its own family C reads two, so
# the one definition now serves three families and a copy that drifted would
# be three gates disagreeing. At step 09 they sat INSIDE family C, so lifting
# family C lifted them and there was nothing to notice; here they are in no
# block at all, and the rung below's first run of this rehearsal composed its
# nine blocks without them. Under `set -uo pipefail` with no `-e` that is not
# a crash. Every call is `command not found`, no ${WORK}/cnt-*.txt or
# nam-*.txt is ever written, and every leg that reads those files reads 0 on
# the GOLDEN while every leg that calls none of the five reads 1. That table
# printed B16 0.400000 -- B-16.4 alone, 0.36 of the 0.90 -- and familyC
# 0.040000, which is C-1's weight and the very figure K17 prints here off a
# different cause entirely: one missing `cat` wearing the costume of
# fifty-five broken gates, and the reason the sixth trap says verify the
# instrument before the finding. The gap is now lifted by name, asserted to
# hold exactly those five definitions and to end at a closing brace, asserted
# to sit strictly between B-15's fact and B-16's banner, and asserted to be
# the ONLY copy -- no block may define any of the five, because two copies
# that drifted would be two gates agreeing about a tree for different reasons.
#
# B-17 IS STEP 10's FAMILY C MINUS ITS C-1, renormalised over the 0.96 that is
# left, and the subtraction is this rung's own must-flip protecting itself.
# Step 10's C-1 was the class-file major, and the major is what THIS rung
# flips -- 66 where step 10 wanted 65 -- so carrying that leg forward would
# grade one artefact twice against two contradictory thresholds. B-16 is step
# 09's family C over the 0.90 that was left of it for the same reason one rung
# earlier, so this is now a rule of the ladder rather than a decision taken
# twice. K17 is the row where the consequence is visible: family C reads
# 0.040000 off an intact class-file scan while B-16 and B-17 both read
# 0.000000, because neither has a class-file leg left to be full.
#
# B-15 REALLY RUNS java, and it is the only block that does. Two of step 08's
# three legs asked java.lang.module what the published artifacts call
# themselves; one of those two survives as B-15.2, and there is no way to
# rehearse it except by running it. So the fixtures carry real jars with real
# manifests, wrapped at 72 bytes the way the jar tool wraps them, and the
# extraction asserts `java` is run EXACTLY ONCE across all ten blocks, on a
# line naming arqgate.Modules -- a second invocation anywhere would mean a gate
# the fixtures do not drive.
#
# ...and the instrument is read before the finding. Before the table runs at
# all, the rehearsal points arqgate.Modules at the golden fixture's own twelve
# jars and requires total=12, declared=12, derivable=11. If that says anything
# else, every B-15.2 figure below is being compared against a broken instrument
# and the seventy-two failures that follow would all be one fault wearing
# seventy-two faces.
#
# THE TWO ARTIFACT CHANNELS ARE STILL TWO, which is the shape step 08 lost and
# step 09 got back. At step 07 C-1 read ${WORK}/scan.txt and B-12e read the
# jars, so a fixture could carry one and not the other; at step 08 both read
# ${WORK}/modulejars.txt, and `no jars` was ONE absence with TWO consequences.
# C-1 has been on the class-file scan since step 09 -- major 66 over the class
# files the build left behind -- so the jars feed B-12e and B-15 and nothing
# else. Four fixtures sit on the split: L1 and L2 move the scan and leave the
# jars alone, K19 and K20 move the jars and leave the scan alone. The
# extraction asserts the same separation structurally: only family C may name
# WANT_MAJOR or ${WORK}/scan.txt, and neither B-16, B-17 nor family C may name
# modulejars.txt.
#
# SO THIS REHEARSAL STAGES A scan.txt. run_one copies the fixture's own into
# ${WORK} before the blocks run, the way the verifier copies the one
# arqgate.Scan writes, and a fixture with no scan.txt is a tree whose build
# produced no class files at all. That is L2, and it is the row that says C-1's
# absence spelling is the shell literal `0` and not `0.000000`.
#
# It asserts THIRTEEN figures per fixture and not six, because at this rung
# neither family's legs are interchangeable and the totals hide which one
# moved: B10.java7-held, B11.java8lang-held, B12.java8-library, B13.java9-10,
# B14.java11-jakarta, B15.jpms-services, B16.java17-constructs and
# B17.java21-constructs, then C-1 through C-4 separately and familyC. EIGHT
# held gates and FOUR own legs, where the rung below had seven and four: the
# column count held at twelve for two rungs by coincidence and moves here
# because the held side grew and the own side did not. K2 and K20 both read B15 0.562500 and they
# are different trees -- one published twelve jars and named none of them, one
# published nothing at all, and only gate.b15.mod.total tells those apart. L12
# and L13 both read B16 0.955556 and they are different trees -- one reverted a
# file's switch, the other left it colon and added an arrow switch beside it,
# and only gate.b16.5.arrow tells those apart. L6 and L14 both read B16
# 0.711111 -- one deleted the text block, one left one in the file but hoisted
# to class scope, outside the method that had to write the document -- and only
# gate.b16.3.textblocks tells those apart. N1 and N2 both read B17 0.959135
# and N10 and N11 both read B17 0.958333 -- each pair a departure half against
# an arrival half of one leg -- and P10 and P11 both read C4 0.750000 the same
# way, this rung's own. The eight held gates hold eight closed rungs in place,
# and B-14, B-15, B-16 and B-17 are the largest of them: they ARE step 07's,
# step 08's, step 09's and step 10's whole families C, renormalised over the
# legs that survive, so a drift in any one of them is a one- to four-rungs-back
# regression reported at step 11. That is the ladder's ninth trap and this
# table is where it is caught.
#
# All thirteen are compared AS STRINGS, because the shell's spelling of a gate is
# part of what is being pinned. `b10=0` is a literal a guarded block assigns and
# never reaches; a gate that computed a zero prints `0.000000`. They are the
# same number and not the same fact, and normalising them would lose the
# difference between a gate that scored nothing and one that never ran. There
# are THREE spellings of zero in this file and K17 carries all three at once:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                C-2 and C-3, B-16.2, B-16.4, B-16.5, B-17.2 and B-17.4 -- and
#                the FRACTIONS of the four legs in the next bullet, which are a
#                different thing from those legs' totals: gate.b15.departed,
#                .arrived and .held, gate.b16.3.movefrac and .holdfrac,
#                gate.b17.3.depfrac, .arrfrac and .holdfrac, and
#                gate.c4.depfrac, .arrfrac and .holdfrac are all initialised
#                above their guard and published below it. So does C-1 when
#                there is no scan file at all -- which is a different tree from
#                one that compiled nothing, and L2 and K17 are the two rows
#                that are not the same row.
#   `0.000000`   B-12, B-13, B-14, B-15, B-16 and B-17 are family totals
#                computed by an awk that sits OUTSIDE the guard, so a tree
#                nobody could compile gets a number computed from parts that
#                never ran. FOUR LEG totals join them, and it is not their
#                family that decides it: B-15.3, B-16.3, B-17.3 and C-4 are the
#                legs that MULTIPLY by a scaling read from behavior.summary, so
#                each computes its own total below its guard out of fractions
#                initialised above it. Five legs scale by a list at this rung;
#                the fifth is B-14.5, which sits behind the compile guard and
#                publishes nothing at all, which is why four print a spelled
#                zero and seven print a literal one. B-15.3 is the one that is
#                easy to miscount, because its three fractions are not spelled
#                `gate.b15.3.*`.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so that tree has no penalty fact rather
#                than an unfired `1`. So are every gate.b16.* and gate.b17.*
#                counter, every gate.c2.*, the gate.c3 counters and the
#                gate.c4.* measurements. gate.src.ok21 is on the list for a
#                reason of its own and not the guard's: the control compile
#                only runs when the measurement compile succeeded, so a tree
#                that will not compile at 22 is never asked about 21 -- the
#                absence IS the answer, and it is not the same answer as `no`.
#                Spelled MISSING and not collapsed into the empty string, so
#                `want MISSING` is an assertion.
#
# AND THREE ABSENCES THAT ARE NOT ZERO AT ALL, each with its own sentinel, which
# is the other half of the same argument. `gate.cp=no-jars` and
# `gate.b15.mod=no-module-jars` are K20's two else-arms -- a tree that published
# nothing, reached through `[ -s ]` and not `[ -f ]` in each block -- and
# `gate.c1=no-scan` is L2's. None of the three is spelled `0.000000`, because a
# gate that measured nothing and a gate that measured zero are different
# findings and a trajectory that could not tell them apart would send a reader
# looking for the wrong bug.
#
# WHERE EACH LEG SITS RELATIVE TO ITS GUARD is checked by line number, because
# it is the property that decides what a tree that will not compile scores. C-1
# reads the class-file scan and sits outside the source-report guard entirely --
# its own guard is about whether scan.txt exists, not about whether the sources
# parsed, and it is asserted not to read `measured` at all -- while C-2, C-3 and
# C-4 read the source AST and must sit INSIDE it, because gone() over an empty
# bucket file reads as "everything departed", the ladder's seventh trap. Each of
# the three opens its own guard naming its own baseline population, so a missing
# set file costs ONE leg rather than three, and B-16's four legs and B-17's
# three do the same over step 09's five set files and step 10's four. B-15 is
# split across its guard on purpose: the name leg reads the jars and sits
# above, the service leg reads the report and sits inside. K17 is that stated
# as a number -- nothing compiled, yet C-1 still reads a measured 1.000000 from
# a scan that exists, and familyC comes to exactly 0.040000, which is C-1's
# weight and nothing else.
#
# What that still costs: nothing here says SourceScan's counts are right, only
# that the arithmetic over them is. The counts are verified separately and by a
# different instrument -- fixtures/src-rehearsal.sh, further down, compiles a
# smoke tree with the plugin attached and asserts every counter and every record
# shape, including this rung's four new kinds, against figures measured first
# and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files this rung reads -- this step's own baseline/priv.txt, catchparam.txt,
# varhandle.txt and vhids.txt, step 10's reftest.txt, chain.txt, seqfirst.txt
# and swids.txt, step 09's ipattern.txt, strnl.txt, sealed.txt, swarrow.txt and
# tbids.txt, step 08's modnames.txt, svc.txt and svcids.txt, and step 07's
# six -- so each half of each gate is rehearsed against the buckets it will
# actually be asked about. Fixtures with invented paths would agree with a gate
# pointed at any set at all. TWO OF THOSE INHERITED FILES ARE RE-CUT HERE
# RATHER THAN CARRIED, and both for the same cause: deleting the seven
# `SecurityActions` ladders takes sites out of populations two and five rungs
# old. ipattern.txt is 24 sites where step 09 froze 28, and accessible.txt's
# setacc half is 10 calls where step 06 froze 14. A rung that staged either
# file unchanged would charge this candidate for work its own requirements
# asked it to delete.
#
# Both ends are in the table, because a gate stuck at 1 passes "golden = 1" and
# a gate stuck at 0 passes "pinned = 0". The ones in between are what make
# either of those mean anything, and each is asserted to six decimals rather
# than to a band -- a gate that has drifted lands somewhere, just not here. The
# figures are computed by fixtures/mkfix11.py, an independent implementation of
# the same composition, and written into the rehearsal BY HAND. This image has
# no python3, which makes that separation structural rather than a matter of
# discipline: a rehearsal that computed its own expectations at run time would
# agree with the scorer by construction and assert nothing. The separation is
# sharpest on B-15.2, which mkfix11.py predicts from the jar PLAN while the
# rehearsal measures the platform's reading of the bytes that plan produced;
# the two agreeing is a fact about java.lang.module and not about either
# program.
#
# The table is 72 rows of 13 figures, and beside those 936 totals stand 1091
# factck assertions on the FACTS behind them -- and 548 more on the twenty
# perturbations below -- which is what tells apart the fixtures that share a
# total. Row by row, where the row is load-bearing rather than the plain half
# of a gate:
#
#   pinned must score all four family-C legs 0.000000 and all eight held gates
#      IN THIS TABLE 1.000000. It is B-6, which is not in this table at all,
#      that makes this rung's floor what it is, and it reads a measured
#      0.961991 THAT IS NOT A FAILURE.
#      The input tree is step 10's finished answer, so B-16 and the newly held
#      B-17 both start full, and an inherited family that starts full gives the
#      floor no help. B-6 is the exception and the reason this rung's floor had
#      to be computed rather than written down: its expectation is cut from the
#      FINISHED tree, the input tree still publishes the 42 signatures C-2
#      withdraws, and family B therefore reads 0.997882 rather than 1.000000
#      on the unmigrated tree. Family A is not flat either -- 903/939 =
#      0.961661, the 36 packaging transcripts C-2's deletion moves -- so the
#      floor is 0.30 x 0.961661 + 0.70 x 0.25 x 0.997882 = 0.463128 against
#      migration_prompt item 6's bar of 0.5, a margin of 0.036872 argued in
#      NOTES 16.17 and 16.18 rather than assumed. C-1 is a MUST-FLIP and not a
#      report: the input tree reports its own major honestly and still scores
#      0.000000, because 65 is not the 66 baseline/level.txt asks for. A tree
#      that raised the level and did nothing else is a different arm --
#      `levelonly`, familyC 0.040000, reward 0.484128 -- and it is measured
#      separately. That arm lands under item 6's bar as it first did one rung
#      below, and lands lower here than there for the two reasons above.
#   golden must score all thirteen 1.000000. It is also where the held gates
#      are proved not to punish this step's own work -- the rung where that
#      matters most, because deleting the seven `SecurityActions` ladders takes
#      seven `instanceof` sites of the kind B-16.2 counts out of the tree along
#      with them, and converting a `setAccessible` to a VarHandle moves the
#      calls B-13.3 counts. Neither may drift, and neither could be asserted
#      against a carried-forward population, which is why both were re-cut.
#   L1 and L2 are C-1's two failures and they are failures of different kinds.
#      L1 is a partial migration: 700 of 739 class files at major 66, the other
#      39 still at 65 because one project's build file did not move, C-1
#      0.947226 and familyC 0.997889. THE DENOMINATOR IS 739 AND NOT THE INPUT
#      TREE'S 746, because the seven `SecurityActions` ladders and their seven
#      nested privileged-action classes leave the tree at this rung. A fixture
#      carried across unchanged would have divided by 746, published 0.938338,
#      and been wrong in a direction no total on the row would have shown.
#      L2 is no scan file at all -- a build that produced no class files --
#      C-1 the literal `0`, familyC 0.960000, and `gate.c1=no-scan` rather than
#      a zero.
#   L3 through L14 are step 09's own family C, failed one leg at a time and now
#      read through B-16: the `instanceof` pattern (L3 0.843137, L4 0.777778,
#      L5 0.904762), the text block (L6 0.711111, L7 0.876191, L8 0.884444,
#      L14 0.711111), the sealed hierarchies (L9 0.818182, L10 0.600000,
#      L11 0.963636) and the arrow switches (L12 and L13 both 0.955556). Twelve
#      rows of a closed rung, still scored on every tree, which is the ninth
#      trap answered rather than argued: the leg that would quietly become
#      unpassable at step 11 is the one nothing at step 11 would notice. L3's
#      figure is the one that MOVED, from step 10's 0.873016 to 0.843137, and
#      not because the gate changed: ipattern.txt is re-cut at 24 sites here
#      where step 09 froze 28, so the same three unconverted sites are three
#      of a smaller population. A row copied forward with its old figure would
#      have failed for the right reason and been read as a scorer bug.
#   N1 through N4 are B-17.2's three halves, one at a time and then together.
#      N1 leaves the three converted reflective tests standing beside their new
#      patterns (departures 10/13, B-17.2 0.769231, B17 0.959135); N2 removes
#      the patterns and leaves the tests gone, which is the SAME two figures off
#      the other half -- gate.b17.2.depfrac against gate.b17.2.arrfrac is the
#      whole difference, and a leg that scored arrivals alone would collapse
#      them onto one message. N2 ALSO MOVES B16, to 0.960784, and it did not
#      one rung ago: this rung re-cut ipattern.txt at 24 sites, the three
#      patterns step 10 wrote are among the survivors, and deleting them is a
#      missing conversion in two held families at once. N3 converts the four
#      ManagerImpl tests that compare a Class VALUE and cannot be a pattern at
#      all (hold 10/14, B-17.2 0.714286, B17 0.949405). N4 deletes every
#      reflective test in the population, both halves full and the hold half
#      0/14: B-17.2 0.000000, B17 0.822917, DELETION SCORING AS BADLY AS
#      LEAVING IT -- and this rung enforces that from a HELD gate, where the
#      rung below enforced it from its own.
#   N5 through N9 are B-17.3's. N5 gives the v_2_5 packager's switch two arms
#      where the chain had three, so the whole three-arm chain is lost
#      (0.700000) with the type set still covered, B17 0.796875; N6 names
#      `case Archive` where the chain tested WebArchive, which moves the
#      coverage condition and NOT the arm count, and moves B-17.2's arrival
#      half with it because both legs read the same `swpat` types (B-17.3
#      0.800000, B-17.2 0.923077, B17 0.850962); N7 converts a dispatch the
#      baseline says must stay a chain because its arms are not disjoint
#      (hold 1/2, B-17.3 0.500000, B17 0.661458); N8 leaves the source shape
#      perfect and breaks two of the five recorded behaviours, which is
#      gate.b17.3.exec at 0.600000 rather than gate.b17.3.holdfrac at
#      0.500000 -- the same shape of loss off a different channel, and at B17
#      0.729167 against N7's 0.661458 THE PAIR NO LONGER SHARES A TOTAL, which
#      it did when these were family C one rung below and 0.65/0.96 was 0.65;
#      N9 puts the switch in the right FILE and the wrong METHOD, which costs
#      the chain and the three arrivals in that method at once (B-17.3
#      0.700000, B-17.2 0.769231, B17 0.756010) and is L14's lesson one gate
#      over.
#   N14 is the row the departure half of B-17.3 exists for, and it is the rung
#      below's round-1 rubric Blocker written as a tree, still scored here.
#      Four correct pattern switches are added BESIDE four untouched
#      `X.class.isInstance(v)` chains: every arm, every type and the
#      `case null` are there, so the arrival half and the hold half are both
#      full and all five recordings still match. Nothing departed -- 0/10 --
#      and B-17.3 is the min() of the three, which is 0.000000. B-17.2 falls
#      with it because ten of its thirteen conv sites are the chains' own
#      reflective tests (3/13 = 0.230769), and B17 is 0.186699. A leg that
#      scored arrivals alone read this tree at full credit for converting
#      nothing.
#   N15 IS THE SAME ATTACK ONE REWRITE FURTHER ON, and it is round 2's
#      rubric Warn written as a tree. Here the four chains ARE converted --
#      every `X.class.isInstance(v)` arm becomes `v instanceof X x`, which is
#      exactly what step 10's requirement 4 asked for at those ten sites -- and
#      the four
#      correct pattern switches are planted beside the `instanceof` chains
#      rather than beside the reflective ones. The reflective calls really
#      have gone, so a departure half that counted only those would read
#      10/10 and hand this tree the whole leg. Counting a chain's own types
#      where an `instanceof` in the same method still names them one at a
#      time reads 0/10 instead, and `gate.b17.3.ipleft=10` says which of the
#      two spellings the residual is: `departed` says how much of the dispatch
#      left, `ipleft` says that what stayed stayed as a type test in the
#      language rather than as a reflective call. B-17.2 is FULL here and not
#      N14's 0.230769 -- the thirteen sites did convert -- so B17 is 0.322917
#      rather than 0.186699, and the two rows are a pair that differ by one
#      fact name and two figures. THE COLUMN TO READ THEM TOGETHER BY IS
#      B-17.2: the same four useless switches, 0.230769 against 1.000000, and
#      the whole burden of saying the DISPATCH did not move falls on B-17.3,
#      where it belongs.
#   N10 through N13 are B-17.4's. N10 leaves two index reads beside the
#      getFirst() that replaced them and N11 removes the getFirst() and leaves
#      the reads gone -- B-17.4 0.714286 and B17 0.958333 from both directions,
#      with gate.b17.4.depfrac and gate.b17.4.arrfrac swapped. N12 deletes
#      three of the nine head reads that must NOT move (hold 6/9, B-17.4
#      0.666667, B17 0.951389). N13 is the sharpest: the sequenced access
#      arrived and reads the WRONG END, getLast() where the code read index 0,
#      so the arrival half keys on `op=getFirst` and reads 6/7 while
#      gate.b17.4.sequenced.read still reports 7 -- reported and not scored,
#      and this row is why both of those are true at once (B-17.4 0.857143,
#      B17 0.979167). NONE OF THE FOUR MOVES THIS RUNG'S C-4, which every one
#      of them asserts in the familyC column: B-17.4 converts index-0 reads to
#      getFirst() and counts `seqfirst` records, C-4 converts `setAccessible`
#      to a VarHandle and counts `varhandle` records, and the two share a
#      JEP-era heading and nothing else.
#   P1 through P5 are this rung's C-2, the `doPrivileged` ladder, and the leg
#      is two halves under a min() because the ladder is not empty. JEP 486
#      left `AccessController.doPrivileged` nothing to do and all 67 call sites
#      must go; but each `SecurityActions` wrapper exists in order to reach a
#      `Constructor.newInstance` or a `setAccessible`, and those 13 calls are
#      the work it was wrapping and must survive the unwrapping. P1 and P4 take
#      the departure down and P2, P3 and P5 the hold, and no row takes both --
#      the only way to show a min() is over two things that move separately.
#      P1 leaves one of the seven wrapper files alone (57/67, C-2 0.850746,
#      familyC 0.931343); P4 unwraps every CALL and leaves all seven
#      `implements PrivilegedAction` declarations standing inside no method at
#      all, which pbuckets() keys `<file>#-` and counts as 7 of the 67
#      (0.895522, familyC 0.951940). P2 DELETES THREE LADDERS WHOLE: the
#      departure is perfect -- which is exactly why it cannot be the whole
#      leg -- the hold reads 10/13 for 0.769231, and B13 falls to 0.969231
#      beside it, because step 06's own `newinstance.ctor` floor asked for the
#      call this tree deleted. P3 IS WHERE ONE RECORD KIND MOVES THREE COLUMNS:
#      two annotation scanners go with their wrappers, so C-2's hold reads
#      11/13 (0.846154), C-4's hold reads 8 of its 10 surviving `setAccessible`
#      sites (0.800000) and B13 reads 0.960000, one tree and three findings
#      that are not redundant -- C-2 asks over the 6 calls inside the wrapper
#      files, B-13.3 and C-4 over all 10 that survive. P5 says the hold is
#      CAPPED PER FILE: one wrapper file loses its construction and another
#      grows two extra, C-2 reads 12/13 for 0.923077, and B13 reads 1.000000 on
#      the same tree because its own term is a bare count over the whole tree
#      that clamps at 14 against a want of 13. Two arithmetics over one record
#      kind, disagreeing on purpose.
#   P6 through P9 are C-3, the unnamed catch parameter, and it is THREE terms
#      under a min() over TWO denominators that differ by seven. 35 unread
#      parameters exist in the input tree and 28 survive C-2, the other seven
#      being inside the ladders C-2 deletes: they cannot be asked to arrive as
#      `_` AND to still be there to arrive. P6 charges both halves for the same
#      four parameters and they charge different prices -- 31 of 35 departing,
#      24 of 28 arriving, min() takes 0.857143, familyC 0.962857 -- and a leg
#      stated over one population would print the same figure in both cells and
#      the pair would not show. P7 DELETES six catch blocks rather than
#      renaming their parameters: the departure reads 1.000000, because a
#      parameter that is not there is not unread, and the arrival reads
#      0.785714. P8 takes the hold and nothing else -- five parameters that
#      WERE read are `_` now, so the diagnostic went with the name -- 46 of 51
#      for 0.901961, and the arrival does NOT rise to meet it, because psum()
#      caps each bucket at what it asked for. P9 fails all three at once and
#      the min() takes the worst, 0.764706: a leg that averaged any two of the
#      three would read this tree higher, and the table would show it in one
#      cell.
#   P10 through P15 are C-4, the VarHandle conversion, and it is the one leg of
#      this family with a fourth term that MULTIPLIES rather than joining the
#      min(). P10 and P11 are the same 0.750000 from two sides -- P10 left one
#      of the four fields unconverted, P11 built two lookups it never
#      unreflected -- and the arrival denominator is TWICE the departure's,
#      two records per converted bucket, `privateLookupIn` and
#      `unreflectVarHandle`. P12 converts a field that must NOT move (9 of 10
#      held, 0.900000, B13 0.980000 on the same tree, familyC 0.976000).
#      P13 IS THE ONLY ROW THAT SAYS THE DEPARTURE IS NOT READ OFF
#      `varhandle`: all four handles built AND all four `setAccessible` calls
#      left standing beside them, arrival 8 of 8, and the leg 0.000000 anyway,
#      because the departure is read off the candidate's own `accessible`
#      records and reads 0 of 4. P14 and P15 are the fourth term itself --
#      P14's source shape is perfect and two of the four recorded access
#      failures answer differently (0.500000, familyC 0.880000), and P15 is the
#      product of P12's 0.900000 with a scaling of 0.750000, which is 0.675000
#      and not the 0.750000 a leg that min()ed the scaling in would print.
#   K1, K2 and K20 are the three ways to lose B-15.2, and they lose it
#      differently: eleven of twelve jars declaring a name (0.840278, the SQUARE
#      of 11/12 and not 11/12 -- step 08's C-1 scaling survives the drop of C-1
#      as the square), none declaring one (0.000000, B-15 falling to its service
#      leg alone at 0.45/0.80 = 0.562500), and no jars published at all.
#   K3 names all twelve and gets ten of them right: B-15.2 0.694444, B-15
#      0.866319. The two it gets wrong are the two whose module name is NOT the
#      artifact id with dots -- arquillian-impl-base and
#      arquillian-protocol-jmx-osgi -- which is the exact failure an agent that
#      derived the names from the artifact ids produces.
#   M1 is the jar the finder cannot read at all: declared and correctly named
#      but not derivable. B-15 reads 1.000000 ON THAT TREE, and that is step
#      09's change rather than an error -- derivability was step 08's C-1 and is
#      REPORTED here, through gate.b15.mod.derivable, and scored by nothing.
#      The row exists to assert exactly that.
#   M2 through M6 are B-15.3's three halves failed one at a time: the two
#      buckets that must move to java.util.ServiceLoader half-moved (M2), one
#      of the eleven by-name loads moved when it must not have been (M3),
#      neither arrival present (M4), and the behaviour scaling at its two ends
#      -- all sixty-nine recorded service lookups broken (M5) and nine of them
#      (M6).
#   K4, K5 and K6 are B-14's C-2 leg; K7 and K8 its C-3 leg, including the
#      broad-rename row -- javax.management renamed to jakarta.management, what
#      a package-scoped recipe does to this tree; K9 and K10 its C-4 leg pulled
#      apart; K11 through K13 its C-5; and K14, K15 and K16 the three penalty
#      arms one each, including the one the other two cannot see, `Class[]::new`
#      where `Class<?>[]::new` was meant. All three penalty rows score every leg
#      of family C in full and are charged anyway.
#   K17 is the instrument admitting it has no reading, and it is the row the
#      three spellings exist for. Nothing compiled, so B-10 and B-11 publish the
#      literal `0` and so do C-2, C-3, B-16.2, B-16.4, B-16.5, B-17.2, B-17.4
#      and the fractions gate.b15.departed/arrived/held,
#      gate.b16.3.movefrac/holdfrac, gate.b17.3.* and gate.c4.*; B-12, B-13,
#      B-14, B-16, B-17, B-15.3, B-16.3, B-17.3 and C-4 publish 0.000000; and
#      every gate.b14.*, gate.b16.* and gate.b17.* counter, every gate.c2.*,
#      the gate.c3 counters and the gate.c4 measurements publish NOTHING.
#      WHICH SPELLING A LEG GETS IS NOT DECIDED BY ITS FAMILY, which is the
#      whole point of reading the four together: C-3 prints `0` here and C-4
#      prints `0.000000`, and one rung below it was the other way round,
#      because the scaled leg of family C changed hands when an unnamed catch
#      parameter -- which has no run-time signature to record -- took C-3 and
#      the VarHandle conversion took C-4. B-15 publishes 0.437500
#      rather than any spelling of zero, because its name leg sits above the
#      guard and reads twelve intact jars. C-1 still reads a
#      measured 1.000000 over all 739 class files, because the class files are
#      there even though the scorer's own javac run is not -- and familyC lands
#      on exactly 0.040000, which is C-1's weight, while B-16 and B-17 both
#      land on 0.000000, because every leg either of them has is on the source
#      channel. That is the guard split stated as three
#      numbers.
#   K18 and K19 are two held gates failed on their own channels: step 06's
#      Class.newInstance buckets calling the legacy form again (B-13 0.866667)
#      and jars naming none of step 05's four packages (B-12 0.850000). Family
#      C reads 1.000000 in both.
#
# And the rehearsal proves it can fail before its clean runs are believed --
# TWENTY times, on EIGHT channels. The eight are the point of the count: the
# source report, the class-file scan, the published jars, and the FIVE id lists
# a perturbation can point at -- httpids, svcids, tbids, swids and vhids -- of
# the six the one behavior.summary is read against. The sixth, nioids, moves
# only under GONE, because B-10's nio third is the one scaling with nothing on
# either side of it to be crossed with. A rung with three artifact channels and
# six behaviour lists can get any one of them crossed with any other, and the
# only way to see a crossing is to move one and assert the others did not. The
# fifth id list is this rung's own -- four recorded access failures, on the one
# artefact that already carried ninety-seven ids for four other legs -- and
# step 10 needed seventeen over seven.
#
# FIVE OF THE TWELVE DOCTORED-SUMMARY FIGURES LAND ON A FIGURE SOME TABLE ROW
# ALSO PRODUCES, which is where step 09's version of this section could still
# say none of them did. SVCALL's 0.437500 is K17's, M4's and M5's B-15;
# TBALL's 0.711111 is L6's and L14's B-16; TBTWO's 0.884444 is L8's B-16;
# SWALL's 0.322917 is N15's B-17; SWTWO's 0.729167 is N8's B-17. The VH* trio
# lands on none: 0.760000, 0.880000 and 0.940000 are family C figures no source
# perturbation in the table reaches.
#
# THE COUNT IS FIVE AND NOT FOUR BECAUSE OF A REPAIR, which is the argument for
# re-deriving this list from the table rather than carrying it. N15 read
# 1.000000 across the board while it was restoring a construct out of an input
# tree that no longer had one -- see NOTES 16.22 -- and the row that had no
# figure to collide with acquired one the moment it started working. A list of
# coincidences is a claim about a table, so it is grepped out of the table each
# time the table moves and not remembered from the rung below.
#
# Every one of those table rows reaches the number through a SOURCE
# fact on a summary that reads clean, and every run here reaches it through a
# broken summary over source that is whole, so the `gate.*.exec.broken` facts
# are asserted alongside each total and are what make the coincidence a
# coincidence rather than a collision.
#
#   PERT     takes the golden and re-points ONE field, the `in=` on toarray[2],
#            at a method that is not one of toarray.txt's five. Asserted on the
#            BYTES that exactly one line changed and that `toarray.ctorref=8`
#            still says eight, because an `in=` is an attribution and not a
#            count. B-14's C-4 bucket leg falls to 4 of 5 while its site leg
#            still reads 8 of 8 -- B-14.4 to 0.800000 and B-14 to 0.973333 --
#            with every family-C leg exactly where the golden left them.
#   SCANPERT moves 46 of the 739 class files back to major 65 in scan.txt and
#            touches nothing else: not one line of report.txt, not one jar.
#            C-1 falls to 0.937754 and familyC to 0.997510 -- which is a
#            SMALLER dent in the total than the same 46 files made one rung
#            below, because C-1's weight fell from 0.10 to 0.04 when the third
#            source leg arrived -- and B-15, B-16 and B-17 hold at 1.000000.
#   MODPERT  deletes ONE of the golden's twelve jars -- arquillian-testng,
#            neither the poisoned artifact nor a class slot carrying a B-12e
#            marker. B-15.2 falls to 0.840278 and B-15 to 0.930122, while
#            B-12 still reads 1.000000 from eleven jars and C-1 reads 1.000000
#            over all 739 class files of an untouched scan, with B-16 and B-17
#            full off an untouched report. Three channels proved separate from
#            every side at once.
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
#   SVCALL, SVCHALF, SVCONE; TBALL, TBTWO, TBONE; SWALL, SWTWO, SWONE;
#            VHALL, VHTWO, VHONE are the same instrument pointed at the other
#            four lists, in the same three shapes each -- the whole list, part
#            of it, one id. Sixty-nine service lookups give B-15 0.437500,
#            0.714674 and 0.991848; five text blocks give B-16 0.711111,
#            0.884444 and 0.942222; five pattern dispatches give B-17 0.322917,
#            0.729167 and 0.864583; four access failures give familyC 0.760000,
#            0.880000 and 0.940000. NO FIGURE IN ONE COLUMN IS REACHABLE FROM
#            ANOTHER LIST'S LENGTH -- sixty-nine cannot produce 0.600000 and
#            five cannot produce 0.985507 -- so a crossed pair does not merely
#            score wrongly, it scores impossibly. TBONE and SWONE are each
#            other's control: the two lists this rung is one rename apart from
#            getting backwards, since the leg called C-3 at step 09 is B-16.3
#            here and a NEW leg took the name. Their sub-gates are the same
#            0.800000 -- five ids each -- so what separates them is the fact
#            name and the column, never the figure. VHONE reads 0.750000
#            instead, which is a side effect of the repair recorded below and
#            not a separation to lean on.
#   GONE     deletes behavior.summary outright. ONE ABSENCE, SIX CONSEQUENCES,
#            asserted together because there is no tree in which the file is
#            missing for one gate and present for another: B-10's nio third is
#            halved, B-14.5 goes to zero with its redirect quarter, B-15.3 goes
#            to zero, B-16.3 goes to zero, B-17.3 goes to zero and C-4 goes to
#            zero with it. The sixth is this rung's and it is the reason GONE
#            could not be carried forward with a renamed key: a GONE that
#            asserted step 10's five numbers with `C3.pattern-switch` spelled
#            `B17.3.pattern-switch` would pass while saying nothing at all
#            about the leg this rung added. The six lists -- four nio ids,
#            eighteen http ids, sixty-nine service ids, five text-block ids,
#            five dispatch ids and four access-failure ids -- are asserted
#            pairwise disjoint first, fifteen pairs, a hundred and five ids and
#            no repeats, which is the precondition every "unmoved" row in the
#            other nineteen perturbations rests on.
#
#            THAT PRECONDITION IS THE ONE THIS RUNG BROKE AND HAD TO REPAIR.
#            A first cut of vhids.txt took every transcript in the era carrying
#            IllegalAccessException, which is five, and the fifth was already
#            svcids.txt's -- so a run that broke service loading lost C-4 as
#            well. A submission whose ONLY behaviour regression was that one
#            service paid 0.048 of family C for it, 0.025200 of reward, on top
#            of the 0.000133 B-15.3 charges: 191 times the price the leg that
#            owns the behaviour sets. The calibration table's own M5 row read
#            C4 0.800000 where it had to read 1.000000. The fifth transcript records
#            java.util.ServiceLoader refusing INSIDE java.base and the split is
#            now derived from that -- IllegalAccessException AND NOT
#            ServiceConfigurationError -- rather than from the string alone.
#            The text-block list, the dispatch list and the access-failure list
#            are within one of each other in LENGTH, which is why the
#            disjointness is asserted over the ids and the rehearsal's rows are
#            separated by their fact names rather than by their figures.
#
# Its seventy-two fixture directories are required to be pairwise distinct by
# digest -- scan.txt included, which is what separates L1 and L2 from the golden
# they are built out of -- so that two rows which had become byte-identical
# twins cannot look like seventy-two assertions while being seventy-one; and the
# table and the directory listing are required to name the same set, which is
# the ladder's third trap in miniature -- a fixture that stopped being scored
# would otherwise disappear without changing a line of output.
RUN bash /tests/fixtures/c-rehearsal.sh

# The three baselines and the twenty-two inherited frozen set files this rung
# reads -- with its own four populations beside them -- checked against each
# other rather than against a number typed a second time here.
#
# Every threshold in family C, in B-10, in B-11, in B-12, in B-13 and in B-16
# comes out of baseline/source.txt and baseline/target.txt, and the scorer
# derives none of them from the tree it is grading. That is the right design and
# it moves the failure: a baseline with a wrong number in it now produces a
# confident wrong score instead of an error, and nothing else in the image would
# notice. So the files are required to agree with each other, which is a
# property no single typo can satisfy.
#
# baseline/source.txt AT THIS RUNG IS NOT THE PREVIOUS RUNG'S BASELINE. It is
# step 09's golden tree re-measured with the gate that ships HERE, and it had
# to be: step 09's SourceScan cannot see a `reftest`, a `seqfirst`, a `seqnew`
# or a `swpat` record, because not one of those four record kinds existed below
# this step -- the string `swpat` appears nowhere in step 09's frozen
# SourceScan.java, which is a fact about that file and not about the JDK it
# ran under: this rung's plugin reads pattern labels as characters and stays
# behind NOTES 14.5's one-way door at 17 on purpose, so step 09's javac could
# have built it. What step 09 lacked was the code, not the API.
# Four record kinds and the aggregates that tally them are therefore first
# measurements of an old tree, and eight inherited counters were re-measured
# beside them rather than carried: target.diamond, target.explicit.nonanon,
# target.try.resources, target.iofile, target.b13.candidatenew, target.b13.want,
# target.b15.svc.arrive and target.b15.svc.keep. That is exactly the situation
# the ninth trap describes -- a step-local mean hiding an inherited figure that
# later requirements made unpassable -- and the assertions that relate them to
# target.txt are what make it visible if one of them drifted.
#
# ALL EIGHT CAME BACK AT THE VALUE THE RUNG BELOW FROZE, which is a result and
# not an assumption. Nothing this rung asks for constructs or destroys any of
# the shapes they count: a reflective type test becoming a type pattern deletes
# a cast and sometimes a local declaration, and every such local on this tree
# already has an explicit type so no `var` goes with it; a first-element read
# becoming `getFirst()` deletes one method call and adds one; a pattern switch
# deletes `if` statements. The re-measurement is here so that the claim is
# checked rather than stated.
#
# FOUR COUNTERS CHANGED THEIR SPELLING RATHER THAN THEIR VALUE, which is the
# eleventh trap seen from the other side. ns.ref.javax.annotation and
# ns.ref.javax.ejb are gone from this file and ns.ref.jakarta.annotation and
# ns.ref.jakarta.ejb hold their 17 and 5, because step 07 did the rename and
# this tree is three rungs past it. Every derivation below that was written
# against the javax spelling is re-keyed here, in the same direction the scorer
# re-keys it, and the four that are NOT re-keyed are the four this rung must
# still hold.
#
# STEP 09'S OWN source.txt AND target.txt ARE NOT STAGED, for the reason step
# 09 gave about step 08's: both would land as baseline/source.txt and
# baseline/target.txt, the same two names this step's pair lands as, and which
# one a gate read would come down to the order of a staging loop. What the
# rungs below need carried forward is carried inside THIS step's target.txt,
# under spellings that name the gate rather than the rung that cut them: step
# 07's four denominators as target.b14.c2.* through target.b14.c5.*, step 08's
# as target.b15.*, step 09's as target.b16.*. B-14, B-15 and B-16 read them
# under those spellings deliberately, because renaming a key is how a
# carry-forward gate quietly stops grading the thing it was named for. Step
# 06's seven population files -- newinstance.txt, accessible.txt, views.txt,
# builds.txt, optops.txt, boxedctor.txt, varbuckets.txt -- are staged under
# their own names and checked below against this tree's own records, and so
# are step 09's six.
#
# THE DIAMOND PAIR IS STILL AN EQUALITY HERE, as it became at step 09. At steps
# 03 and 04 target.diamond and target.b10.diamond were the same 93 counted from
# opposite ends; step 05 spent 19 of them on its sinks, step 06 spent the
# twentieth on Set.of, step 07 spent none, step 08 spent TWO when its C-3
# deleted the two hand-rolled META-INF/services readers whole, and steps 09 and
# 10 spent none. This rung spends none either, AND THAT WAS WORTH MEASURING
# RATHER THAN ASSUMING, because unlike its four predecessors this one DELETES
# code. What C-2 removes is `AccessController.doPrivileged(...)` calls, four
# imports, seven `GetTcclAction` enum bodies and the `getConstructor` unwrap
# ladders around them, and not one of those carries a `new X<>()`: the enums
# are enums, the ladders construct through `Constructor.newInstance` -- which
# C-2's own hold half counts and requires to survive -- and the actions are
# singleton enum constants rather than generic instantiations. A renamed catch
# parameter is a name and a VarHandle replaces a Field, neither of them a
# constructor call. target.diamond is 71 and the tree holds 71.
#
# The other invariants are the things this rung must NOT move, each appearing in
# both files and required to be equal: the manual close() count (0), the
# non-anonymous explicit remainder (23), the anonymous functional residue (12),
# the @SuppressWarnings sites (44), the raw-type count (0) and the seven
# resource clauses step 08 left behind. A rung that moves one of them has either
# redone an earlier step's work or undone it, and B-10, B-11, B-12, B-13 and
# B-16 exist because the second is the likelier accident.
#
# NOT ONE FIGURE IN THAT LIST MOVED UNDER THE RUNG BELOW, and java.io is the one
# worth saying out loud, because it is the one that looks as though it should
# have. Step 10's family C was type patterns, a pattern switch and sequenced-
# collection reads, and all three are shapes that can swallow a java.io
# declaration on the way past; none of them did. The floor was 4 for seven rungs
# and is 4 on the tree this one is handed, so mk-baseline-step11.sh asserts it
# the way every rung below asserted it -- `want`, against step 10's own
# target.iofile -- rather than recording it. B-10 does not read the figure as an
# equality in any case: it reads it as a FLOOR, the number of java.io sites that
# may not come BACK. The eleven that moved at step 03 are still the denominator
# of both halves of B-10's NIO.2 third, as they have been since step 03, and 11
# is what target.b10.nio still says.
#
# THE FLOOR CLAIM, ASSERTED ON THE BASELINES rather than only through a fixture.
# The tree the candidate is handed satisfies every requirement B-10, B-11, B-12,
# B-13, B-14, B-15, B-16 and B-17 state -- 71 diamonds, the 23 non-anonymous
# sites still explicit, 7 resource clauses, no manual closes, java.io still at
# its 4 sites, 62 lambdas over 34 files, 24 annotated SAM interfaces, 16
# Optional-returning helpers, 25 Collectors calls, 6 Objects delegations, 11
# java.time references, not one currentTimeMillis, both Class.newInstance calls
# migrated, no isAccessible guard with all 14 setAccessible calls still there,
# no wrapper constructor, seven List/Set/Map factory calls with the seven views
# untouched, no Optional.isPresent, 78 `var` declarations with step 06's one
# exemption spent, all 22 annotation references already jakarta, all 8 toArray
# sites converted, the 14 HTTP records already moved, twelve jars each declaring
# the Automatic-Module-Name its packages imply, both hand-rolled
# META-INF/services readers already through java.util.ServiceLoader, 17
# `instanceof` sites across 14 methods already binding a pattern variable with
# the 7 that must not still plain, the one text block written and the 7 held
# fragments still concatenated, 11 hierarchies sealed over 35 permitted names,
# both switches already in arrow form, and -- new at this rung, because step
# 10's family C arrives here as B-17 -- 13 reflective type tests across 7
# methods already reading as type patterns with the 14 that must not still
# plain, 4 dispatch chains already pattern switches over 10 arms with the 2 that
# must not still chains, and 7 sequenced first-element reads already calling
# getFirst() with the 9 that must not.
#
# AND IT SATISFIES NONE THAT FAMILY C STATES: every class file is major 65 and
# not one is 66, all 67 privileged-API sites are present across 27 buckets in 7
# files, there are 35 unread catch parameters and not one unnamed, and there are
# 0 privateLookupIn calls and 0 unreflectVarHandle calls with all 14
# setAccessible sites in place.
#
# Those are the two halves of the calibration table's `pinned` row -- familyC
# 0.0000 with every held gate reading what it reads on this tree -- and that row
# is the floor argument at the foot of test.sh. WHAT IS NOT TRUE HERE, AND WAS
# TRUE AT EVERY RUNG FROM 08 TO 10, IS THAT THE OTHER TWO FAMILIES ARE FLAT 1s.
# Both fall short on the input tree, for two unrelated reasons, and both were
# MEASURED rather than assumed:
#
#   family A   903 of 939, or 0.961661. The era replayed here is rec/jdk25-
#              step11, recorded on the GOLDEN under the Temurin 25 that grades
#              here, and 36 of its transcripts -- every `servlet/pack*` and
#              `servlet/append*` -- enumerate the entries of
#              arquillian-protocol.jar. C-2 deletes a class file from that jar,
#              which shortens the enumeration and decrements the `entries=`
#              figure, so those 36 record the SHORTER list and fail on the input
#              tree, where the class is still there. They are named one by one
#              in authoring/ids-step11-moved.txt. That is not a defect in the
#              era: it is the one thing this rung's work is visible as from
#              outside, and a family A term is what a step's work being visible
#              from outside looks like.
#   family B   0.997882. B-6's published-API expectation is re-cut at this rung,
#              1147 signatures to 1105, because C-2 withdraws the 42
#              `SecurityActions$GetTcclAction` members whose enum bodies it
#              deletes. The input tree still publishes all 42, so B-6 reads
#              (1105 - 42) / 1105 = 0.961991 and costs family B 0.002118. Every
#              other held gate reads 1.0000 on this tree.
#
# So the floor is 0.30 x 0.961661 + 0.70 x 0.25 x 0.997882 = 0.288498 + 0.174629
# = 0.463128 against migration_prompt item 6's 0.5, and it clears it by
# 0.036872. That margin is LARGER than the 0.025 the rung below left, and not
# one part of it is slack: it is the room the two shortfalls above buy, and if
# either of them were closed -- an era recorded on the input tree, or an
# allowlist in place of a re-cut B-6 expectation -- the floor would climb back
# toward 0.475 and the margin would shrink to what step 10 had. Family C's
# zeroes are still the thing that must stay zero; if any of them stops being
# zero the floor moves, and it moves in the direction that breaks the item.
#
# THIS RUNG'S FOUR NEW SET FILES ARE NOT CHECKED AGAINST A COUNT TYPED HERE.
# They are held against the tally keys that stage beside them -- seventeen of
# the thirty staged-and-unread target.* keys named below, each one the same
# population counted a second way, so that a list which quietly lost a line
# moves its own denominator with it and is caught here rather than reading full
# on a smaller question:
#
#   reftest.txt   27 lines, 7 conv buckets holding 13 sites and 11 hold buckets
#                 holding 14, against target.rt.conv.*, target.rt.hold.* and
#                 target.rt.total; target.rt.scanned is the 35 the scan saw
#                 across the whole tree, main sources and tests together
#   chain.txt     6 lines, 4 conv covering 10 arms and 2 hold, against
#                 target.ch.conv, target.ch.hold and target.ch.arms -- and
#                 derived from reftest.txt's own rows, so the two files cannot
#                 disagree about what is in this tree
#   seqfirst.txt  16 lines, 7 conv buckets holding 7 sites and 7 hold buckets
#                 holding 9, against target.sq.*; target.sq.scanned is the 26
#                 the scan saw and target.sq.new is the measured 0 that makes
#                 C-4's arrival half a presence test
#   swids.txt     5 recording ids, each required to be in assets/ids.txt AND to
#                 have a transcript in this rung's era, against target.ch.ids
#
# THE SIX SET FILES STEP 09 CUT are inherited now -- ipattern.txt, strnl.txt,
# sealed.txt, swarrow.txt, tbids.txt and apinorm.txt -- and read the way every
# other inherited set is read here: against this tree, in the state the rung
# below left them in. That is a real change of question and not a copied block.
# ipattern.txt's 14 conv buckets named 17 sites that were `kind=plain` when
# step 09 read them and must be `kind=bound` now; its 7 skip buckets must still
# be plain. 14 and not step 09's 18, because this is the one file in the six
# that is RE-CUT rather than carried -- seven of step 09's buckets leave with
# the methods C-2 deletes and three arrive from step 10's own conversions. strnl.txt's one move bucket held 16 literals there and must hold none
# here, with the one textblock record in its place, while its two hold buckets
# must still hold their 7. sealed.txt's 11 roots must now carry a `sealed`
# record naming 35 permitted subtypes between them and its 11 open types must
# still carry none. swarrow.txt's two files must now hold arrow switches and no
# colon one. Their tallies are the eleven target.b16.* keys that stage unread,
# and tbids.txt's five ids are checked the way swids.txt's five are.
#
# THE THREE SET FILES STEP 08 CUT -- modnames.txt, svc.txt and svcids.txt --
# are read the same way, against this tree in the state step 08 left them in.
#
# THE FIVE SET FILES STEP 07 CUT ARE READ IN THE ARRIVED STATE, which is the
# shape step 08 gave this block and the thing a copied agreement block would
# get wrong in five places at once. nsmove.txt, nshold.txt, toarray.txt,
# http.txt and httpids.txt were all cut from step 07's INPUT tree; the
# source.txt they are read against here is step 09's OUTPUT, three rungs further
# on. Derived verbatim they would compare the file to a tree that has already
# moved past it and fail on a CORRECT input. So each is read as the arrival it
# now describes: nsmove.txt through the same jakarta-to-javax re-keying the
# scorer performs, and with the javax spellings required ABSENT; toarray.txt
# against form=ctorref with form=sized required to be zero; http.txt's hold
# half verbatim in its own file and its move half as a proven departure. That
# is a stronger check than step 07's, not a weaker one -- verbatim, at this
# rung, would have compared the file to a tree it never saw.
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
# step 06 migrated, and this tree is step 06's output with steps 07, 08 and 09
# on top of it. newinstance.txt's two legacy buckets must now hold a form=ctor
# record and no form=legacy one; accessible.txt's twelve guard buckets must hold
# no isAccessible call while all fourteen setacc buckets still hold theirs;
# builds.txt's seven buckets must hold a factory call and no construction;
# boxedctor.txt's twelve records must be gone from the tree entirely;
# varbuckets.txt's 81 must be down to 78 with step 06's one exemption spent --
# and its 81 is NOT target.b13.candidatenew, because three of the 81 went with
# step 08's C-3, so the file's count and the key differ by an asserted 3.
# views.txt is the exception and is still DERIVED, because it is the one of the
# seven step 06 was told to leave alone. If B-13 is a regression gate, its
# populations have to be in the state it is holding, and this is where that is
# established rather than assumed.
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
# assets/ids.txt. It is read by family A as four of 939 ids and by B-10 as the
# proviso that halves its NIO third, so an id renamed out of the recording set
# would quietly stop protecting anything in both places at once.
# baseline/level.txt CARRIES TWO NUMBERS HERE AND AT THE RUNG BELOW, and the
# pair is the first thing this block establishes because two separate gates
# divide by it. `major=66` is C-1's must-flip; `was=65` is B-4's regression
# floor, the level the tree arrives at -- step 10's target, which is what makes
# the pair move together rather than one of them. Every rung from 03 to 08 could
# write one number because the level it held and the level it wanted were the
# same figure or B-4 did not exist yet. A file that carried only one here would
# leave HOLD_MAJOR empty, test.sh's own FATAL would catch that, and the build
# would ship a verifier that dies on every tree -- so it is caught here instead,
# and the ORDER of the two is asserted as well as their values: a level.txt
# whose `was` sat above its `major` would ask a candidate to go backwards.
RUN set -eux; \
    test "$(grep -c '^major=' /tests/baseline/level.txt)" = 1; \
    test "$(grep -c '^was=' /tests/baseline/level.txt)" = 1; \
    grep -qxF 'major=66' /tests/baseline/level.txt; \
    grep -qxF 'was=65'   /tests/baseline/level.txt; \
    want=$(sed -n 's/^major=//p' /tests/baseline/level.txt | head -1); \
    held=$(sed -n 's/^was=//p'   /tests/baseline/level.txt | head -1); \
    test "${want}" = 66; \
    test "${held}" = 65; \
    test "${held}" -lt "${want}"; \
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
    c2b=$(sed -n  's/^target\.b14\.c2\.buckets=//p'  ${btgt} | head -1); \
    c2r=$(sed -n  's/^target\.b14\.c2\.refs=//p'     ${btgt} | head -1); \
    c3b=$(sed -n  's/^target\.b14\.c3\.buckets=//p'  ${btgt} | head -1); \
    c3r=$(sed -n  's/^target\.b14\.c3\.refs=//p'     ${btgt} | head -1); \
    c4b=$(sed -n  's/^target\.b14\.c4\.buckets=//p'  ${btgt} | head -1); \
    c4s=$(sed -n  's/^target\.b14\.c4\.sized=//p'    ${btgt} | head -1); \
    c5f=$(sed -n  's/^target\.b14\.c5\.file=//p'     ${btgt} | head -1); \
    c5l=$(sed -n  's/^target\.b14\.c5\.legacy=//p'   ${btgt} | head -1); \
    c5t=$(sed -n  's/^target\.b14\.c5\.types=//p'    ${btgt} | head -1); \
    c5s=$(sed -n  's/^target\.b14\.c5\.cdistub=//p'  ${btgt} | head -1); \
    tmt=$(sed -n  's/^target\.b15\.mod\.total=//p'   ${btgt} | head -1); \
    svl=$(sed -n  's/^target\.b15\.svc\.legacy=//p'  ${btgt} | head -1); \
    sva=$(sed -n  's/^target\.b15\.svc\.arrive=//p'  ${btgt} | head -1); \
    svk=$(sed -n  's/^target\.b15\.svc\.keep=//p'    ${btgt} | head -1); \
    tlvl=$(sed -n 's/^target\.level=//p'             ${btgt} | head -1); \
    tlvw=$(sed -n 's/^target\.level\.was=//p'        ${btgt} | head -1); \
    ipb=$(sed -n  's/^target\.b16\.ip\.buckets=//p'       ${btgt} | head -1); \
    ips=$(sed -n  's/^target\.b16\.ip\.sites=//p'         ${btgt} | head -1); \
    ipk=$(sed -n  's/^target\.b16\.ip\.skip=//p'          ${btgt} | head -1); \
    ipt=$(sed -n  's/^target\.b16\.ip\.total=//p'         ${btgt} | head -1); \
    tbmb=$(sed -n 's/^target\.b16\.tb\.move\.buckets=//p' ${btgt} | head -1); \
    tbms=$(sed -n 's/^target\.b16\.tb\.move\.sites=//p'   ${btgt} | head -1); \
    tbhb=$(sed -n 's/^target\.b16\.tb\.hold\.buckets=//p' ${btgt} | head -1); \
    tbhs=$(sed -n 's/^target\.b16\.tb\.hold\.sites=//p'   ${btgt} | head -1); \
    tbid=$(sed -n 's/^target\.b16\.tb\.ids=//p'           ${btgt} | head -1); \
    slr=$(sed -n  's/^target\.b16\.sealed\.roots=//p'     ${btgt} | head -1); \
    slp=$(sed -n  's/^target\.b16\.sealed\.permits=//p'   ${btgt} | head -1); \
    slt=$(sed -n  's/^target\.b16\.sealed\.types=//p'     ${btgt} | head -1); \
    slo=$(sed -n  's/^target\.b16\.sealed\.open=//p'      ${btgt} | head -1); \
    swf=$(sed -n  's/^target\.b16\.sw\.files=//p'         ${btgt} | head -1); \
    swt=$(sed -n  's/^target\.b16\.sw\.total=//p'         ${btgt} | head -1); \
    # B-17'S SEVENTEEN, RENAMED FROM STEP 10'S BARE target.rt.*, target.ch.* \
    # AND target.sq.*. The figures are step 10's own and every one of them is \
    # unchanged; the prefix moved because this rung's target.txt carries them \
    # beside its own priv, cp and vh keys, and a bare `target.rt.` there would \
    # read as this rung's work rather than the rung below's. Every held family \
    # above the second is spelled that way already -- b14, b15, b16 -- so B-17 \
    # is spelled that way too, and the variable names here are left alone so a \
    # finding carries back to step 10 without a translation table. \
    rtcb=$(sed -n 's/^target\.b17\.rt\.conv\.buckets=//p' ${btgt} | head -1); \
    rtcs=$(sed -n 's/^target\.b17\.rt\.conv\.sites=//p'   ${btgt} | head -1); \
    rthb=$(sed -n 's/^target\.b17\.rt\.hold\.buckets=//p' ${btgt} | head -1); \
    rths=$(sed -n 's/^target\.b17\.rt\.hold\.sites=//p'   ${btgt} | head -1); \
    rtt=$(sed -n  's/^target\.b17\.rt\.total=//p'         ${btgt} | head -1); \
    rtsc=$(sed -n 's/^target\.b17\.rt\.scanned=//p'       ${btgt} | head -1); \
    chc=$(sed -n  's/^target\.b17\.ch\.conv=//p'          ${btgt} | head -1); \
    chh=$(sed -n  's/^target\.b17\.ch\.hold=//p'          ${btgt} | head -1); \
    cha=$(sed -n  's/^target\.b17\.ch\.arms=//p'          ${btgt} | head -1); \
    chid=$(sed -n 's/^target\.b17\.ch\.ids=//p'           ${btgt} | head -1); \
    sqcb=$(sed -n 's/^target\.b17\.sq\.conv\.buckets=//p' ${btgt} | head -1); \
    sqcs=$(sed -n 's/^target\.b17\.sq\.conv\.sites=//p'   ${btgt} | head -1); \
    sqhb=$(sed -n 's/^target\.b17\.sq\.hold\.buckets=//p' ${btgt} | head -1); \
    sqhs=$(sed -n 's/^target\.b17\.sq\.hold\.sites=//p'   ${btgt} | head -1); \
    sqn=$(sed -n  's/^target\.b17\.sq\.new=//p'           ${btgt} | head -1); \
    sqsc=$(sed -n 's/^target\.b17\.sq\.scanned=//p'       ${btgt} | head -1); \
    sqt=$(sed -n  's/^target\.b17\.sq\.total=//p'         ${btgt} | head -1); \
    # B-16'S FOUR NEW ONES. On the tree this rung is HANDED, C-2's population \
    # is 31 sites over 28 buckets, 24 of those sites bound over 21 buckets, and \
    # seven of the 21 are the SecurityActions ladders this rung's own C-2 \
    # deletes; so ipattern.txt was re-cut to 24 sites over 21 buckets, 17 bound \
    # over 14, and the figures the SEED would have asked for are carried beside \
    # the re-cut ones under `was`. `was` is this rung's input and not step 09's \
    # statement -- step 09 froze 18 conv buckets over 21 sites, 28 in all, and \
    # the difference is three buckets step 10 added. Both are asserted against \
    # the file below. \
    ipkb=$(sed -n 's/^target\.b16\.ip\.skip\.buckets=//p' ${btgt} | head -1); \
    ipwb=$(sed -n 's/^target\.b16\.ip\.was\.buckets=//p'  ${btgt} | head -1); \
    ipws=$(sed -n 's/^target\.b16\.ip\.was\.sites=//p'    ${btgt} | head -1); \
    ipwt=$(sed -n 's/^target\.b16\.ip\.was\.total=//p'    ${btgt} | head -1); \
    # B-13.3'S TWO NEW ONES, for the same reason and the other direction: the \
    # 14 `setAccessible` sites step 06 reached are still 14 on the way in, and \
    # 10 on the way out, because this rung's own C-4 converts four of them. The \
    # leg that HOLDS them therefore has to be stated over the 10 that survive, \
    # and `was` records the 14 it was stated over for five rungs. \
    g3sb=$(sed -n 's/^target\.b13\.setacc\.buckets=//p'   ${btgt} | head -1); \
    g3sw=$(sed -n 's/^target\.b13\.setacc\.was=//p'       ${btgt} | head -1); \
    # THIS RUNG'S OWN, in the four groups its family C is scored in. \
    tids=$(sed -n 's/^target\.ids=//p'                 ${btgt} | head -1); \
    pvs=$(sed -n  's/^target\.priv\.sites=//p'         ${btgt} | head -1); \
    pvc=$(sed -n  's/^target\.priv\.calls=//p'         ${btgt} | head -1); \
    pvt=$(sed -n  's/^target\.priv\.typerefs=//p'      ${btgt} | head -1); \
    pvb=$(sed -n  's/^target\.priv\.buckets=//p'       ${btgt} | head -1); \
    pvf=$(sed -n  's/^target\.priv\.files=//p'         ${btgt} | head -1); \
    pvw=$(sed -n  's/^target\.priv\.want=//p'          ${btgt} | head -1); \
    pvwf=$(sed -n 's/^target\.priv\.want\.files=//p'   ${btgt} | head -1); \
    pvh=$(sed -n  's/^target\.priv\.hold=//p'          ${btgt} | head -1); \
    pvhc=$(sed -n 's/^target\.priv\.hold\.ctor=//p'    ${btgt} | head -1); \
    pvha=$(sed -n 's/^target\.priv\.hold\.acc=//p'     ${btgt} | head -1); \
    cpu=$(sed -n  's/^target\.cp\.unnamed=//p'         ${btgt} | head -1); \
    cpr=$(sed -n  's/^target\.cp\.read=//p'            ${btgt} | head -1); \
    cpx=$(sed -n  's/^target\.cp\.unread=//p'          ${btgt} | head -1); \
    cpt=$(sed -n  's/^target\.cp\.total=//p'           ${btgt} | head -1); \
    cpb=$(sed -n  's/^target\.cp\.buckets=//p'         ${btgt} | head -1); \
    cpf=$(sed -n  's/^target\.cp\.files=//p'           ${btgt} | head -1); \
    cpsc=$(sed -n 's/^target\.cp\.scanned=//p'         ${btgt} | head -1); \
    cpwu=$(sed -n 's/^target\.cp\.want\.unnamed=//p'   ${btgt} | head -1); \
    cpwr=$(sed -n 's/^target\.cp\.want\.read=//p'      ${btgt} | head -1); \
    cpwx=$(sed -n 's/^target\.cp\.want\.unread=//p'    ${btgt} | head -1); \
    cpwt=$(sed -n 's/^target\.cp\.want\.total=//p'     ${btgt} | head -1); \
    cpwb=$(sed -n 's/^target\.cp\.want\.buckets=//p'   ${btgt} | head -1); \
    cpwf=$(sed -n 's/^target\.cp\.want\.files=//p'     ${btgt} | head -1); \
    vhs=$(sed -n  's/^target\.vh\.sites=//p'           ${btgt} | head -1); \
    vha=$(sed -n  's/^target\.vh\.arrival=//p'         ${btgt} | head -1); \
    vhb=$(sed -n  's/^target\.vh\.buckets=//p'         ${btgt} | head -1); \
    vhw=$(sed -n  's/^target\.vh\.want=//p'            ${btgt} | head -1); \
    vhwb=$(sed -n 's/^target\.vh\.want\.buckets=//p'   ${btgt} | head -1); \
    vhwf=$(sed -n 's/^target\.vh\.want\.files=//p'     ${btgt} | head -1); \
    tact=$(sed -n 's/^target\.acc\.total=//p'          ${btgt} | head -1); \
    tacv=$(sed -n 's/^target\.acc\.conv=//p'           ${btgt} | head -1); \
    tach=$(sed -n 's/^target\.acc\.hold=//p'           ${btgt} | head -1); \
    tacf=$(sed -n 's/^target\.acc\.files=//p'          ${btgt} | head -1); \
    # Every key test.sh reads is a key target.txt carries -- and at this rung \
    # the converse is deliberately NOT true, which is why the two directions \
    # are asserted separately instead of with one cmp. 118 keys stage and 54 \
    # are read through tv(). A key the scorer reads and the file does not carry \
    # reads as the empty string and defaults to 0, so that direction is an \
    # error with no exceptions. The other direction is sixty-four keys wide \
    # here, and sixty-one of the sixty-four are a population counted a second \
    # way. \
    # \
    # WHY SIXTY-THREE TARGET KEYS ARE STAGED AND UNREAD, up from step 10's \
    # thirty. Nothing about the scoring changed: neither family C's four legs \
    # nor B-16's and B-17's seven divides by a count in target.txt. They divide \
    # by the POPULATION FILES -- priv.txt, catchparam.txt, varhandle.txt and \
    # accessible.txt for this rung's own work, reftest.txt, chain.txt, \
    # seqfirst.txt and swids.txt for B-17, ipattern.txt, strnl.txt, sealed.txt, \
    # swarrow.txt and tbids.txt for B-16 -- because each leg is scored per \
    # bucket and a bucket list is not a number. What grew is the number of \
    # populations: this rung freezes four of its own, and it states two of its \
    # three legs TWICE, once on the tree it is handed and once on the golden, \
    # so each of those states is a row of counts rather than a figure. The \
    # sixty are those populations counted, frozen beside them so that this \
    # image can check a list against its own tally: `target.priv.buckets=27` is \
    # what priv.txt's 67 rows must fall into, `target.cp.want.unnamed=28` is \
    # how many of catchparam.txt's 35 declared-and-unread rows can arrive as \
    # `_`, `target.vh.want=8` is the two calls apiece that varhandle.txt's four \
    # `convert` rows owe, and so on. A population file that lost a line would \
    # move its own denominator with it and the gate would read full on a \
    # smaller question -- the ninth trap, and the reason the tally lives in a \
    # second file. Every one of the sixty is asserted below against the \
    # list it counts; none is merely staged. \
    # \
    # THE OTHER THREE ARE target.level, target.level.was AND target.ids. The \
    # level pair is unread for the reason it was unread at 09 and 10: C-1 reads \
    # the major it wants out of baseline/level.txt, which is the file the \
    # scorer parses for both numbers, and the target.txt pair exists only so \
    # that this image can hold the two files against each other -- the \
    # assertion at the foot of this stanza. target.ids is new, and it is the \
    # count of transcripts this rung's era MOVES rather than adds: 37 of 939. \
    # Family A divides by the whole suite and never by that number, so the key \
    # is staged as a claim about ids-step11-moved.txt and checked against that \
    # file where the era is checked, not read by the scorer. \
    # \
    # TWELVE KEYS OF THESE FAMILIES ARE READ, and they split in two. Seven are \
    # SCORED: target.b16.ip.total, target.b16.sealed.roots, \
    # target.b16.sealed.open, target.b16.sw.files and target.b13.setacc, each a \
    # term that is a count rather than a list, target.b17.ch.ids, which \
    # divides B-17.3's execution scaling, and target.b13.setacc.buckets, which \
    # is new at this rung and is the one key here that exists because C-4 \
    # MOVED an inherited denominator: B-13.3's arrival half used to be asked \
    # over the same twelve guard buckets as its departure half, and cannot be \
    # now, because three of the four calls C-4 converts sit in guard buckets \
    # and a correct tree would cap that half at 9/12. The other five -- \
    # target.b17.rt.scanned, target.b17.sq.scanned, target.b17.sq.new, \
    # target.priv.sites and target.acc.total -- are read only to be PRINTED, as \
    # the `.was` half of a fact line, so that a trajectory shows what a figure \
    # stood at before the rung that moved it. Nothing multiplies or divides by \
    # those five, and a wrong value in any of them would misreport the run \
    # without misgrading it -- which is exactly why they are checked here too. \
    test "$(grep -c '^target\.' ${btgt})" = 118; \
    grep -o '^target\.[a-z0-9.]*' ${btgt} | LC_ALL=C sort > /tmp/tkeys.txt; \
    sed -n "s/.*tv 'target\\(.*\\)').*/target\\1/p" /tests/test.sh \
      | tr -d '\\' | LC_ALL=C sort -u > /tmp/rkeys.txt; \
    test "$(grep -c . /tmp/rkeys.txt)" = 55; \
    if [ "$(LC_ALL=C comm -13 /tmp/tkeys.txt /tmp/rkeys.txt | grep -c .)" != 0 ]; then \
        echo "test.sh reads a target.* key baseline/target.txt does not carry:" >&2; \
        LC_ALL=C comm -13 /tmp/tkeys.txt /tmp/rkeys.txt >&2; exit 1; \
    fi; \
    LC_ALL=C comm -23 /tmp/tkeys.txt /tmp/rkeys.txt > /tmp/ukeys.txt; \
    printf '%s\n' target.acc.conv target.acc.files target.acc.hold \
                  target.b13.setacc.was \
                  target.b16.ip.buckets target.b16.ip.sites target.b16.ip.skip \
                  target.b16.ip.skip.buckets \
                  target.b16.ip.was.buckets target.b16.ip.was.sites \
                  target.b16.ip.was.total \
                  target.b16.sealed.permits target.b16.sealed.types \
                  target.b16.sw.total \
                  target.b16.tb.hold.buckets target.b16.tb.hold.sites \
                  target.b16.tb.ids \
                  target.b16.tb.move.buckets target.b16.tb.move.sites \
                  target.b17.ch.arms target.b17.ch.conv target.b17.ch.hold \
                  target.b17.rt.conv.buckets target.b17.rt.conv.sites \
                  target.b17.rt.hold.buckets target.b17.rt.hold.sites \
                  target.b17.rt.total \
                  target.b17.sq.conv.buckets target.b17.sq.conv.sites \
                  target.b17.sq.hold.buckets target.b17.sq.hold.sites \
                  target.b17.sq.total \
                  target.cp.buckets target.cp.files target.cp.read \
                  target.cp.scanned target.cp.total target.cp.unnamed \
                  target.cp.unread \
                  target.cp.want.buckets target.cp.want.files \
                  target.cp.want.read target.cp.want.total \
                  target.cp.want.unnamed target.cp.want.unread \
                  target.ids target.level target.level.was \
                  target.priv.buckets target.priv.calls target.priv.files \
                  target.priv.hold target.priv.hold.acc target.priv.hold.ctor \
                  target.priv.typerefs target.priv.want target.priv.want.files \
                  target.vh.arrival target.vh.buckets target.vh.sites \
                  target.vh.want target.vh.want.buckets target.vh.want.files \
      | LC_ALL=C sort > /tmp/uwant.txt; \
    test "$(grep -c . /tmp/uwant.txt)" = 63; \
    if ! cmp -s /tmp/ukeys.txt /tmp/uwant.txt; then \
        echo "the set of staged-and-unread target.* keys is not the sixty-three this rung means:" >&2; \
        diff /tmp/uwant.txt /tmp/ukeys.txt >&2 || true; exit 1; \
    fi; \
    # 117 of the 118 are numbers. target.b14.c5.file is the one that is not, and \
    # it is the only key on the ladder whose value is a path: B-14.5 is scoped \
    # by FILE, for the reason test.sh gives where it reads it, so the key names \
    # the file rather than counting anything. It is exempted here BY NAME and \
    # then checked as a path, which is stricter than the numeric loop it \
    # escapes -- a typo in it would aim the whole leg at a file that does not \
    # exist and every count in this image would still agree. \
    for k in $(cat /tmp/tkeys.txt); do \
        [ "${k}" = "target.b14.c5.file" ] && continue; \
        v=$(sed -n "s/^${k}=//p" ${btgt} | head -1); \
        case "${v}" in ''|*[!0-9]*) echo "${k} is not a number: '${v}'" >&2; exit 1;; esac; \
    done; \
    rm -f /tmp/tkeys.txt /tmp/rkeys.txt /tmp/ukeys.txt /tmp/uwant.txt; \
    case "${c5f}" in *"/src/main/java/"*.java) :;; *) echo "target.b14.c5.file is not a src/main source path: ${c5f}" >&2; exit 1;; esac; \
    # This rung's own two level keys, and the pair they must equal. \
    # baseline/level.txt is what the SCORER reads and target.txt is what this \
    # image checks it against; the two files are written by different halves of \
    # mk-baseline-step11.sh and a rung that moved one and not the other would \
    # grade a level nothing here asked for. \
    test "${tlvl}" = "${want}"; \
    test "${tlvw}" = "${held}"; \
    # What the input tree HOLDS, and must go on holding: steps 03, 04, 05 and 06. \
    # \
    # ALL FIFTY-ONE ARE ASSERTED AT THE VALUE STEPS 09 AND 10 ASSERT THEM AT, \
    # because nothing in step 09, step 10 or this rung touches any of them. \
    # That is the whole strength of the check: a tree that had drifted anywhere \
    # would have moved one, and the rung below's own staged source.txt is the \
    # reference every one of them is read against. \
    # \
    # SIX OF THEM STOOD AT THE GOLDEN'S VALUES HERE until this was re-measured \
    # (NOTES 16.43), and they are worth naming because the mistake is the \
    # instructive kind. Step 10's ORACLE rewrote seven methods that had searched \
    # a collection with a for-each loop into stream pipelines -- \
    # ManagerImpl.getScopedContext, ContainerRegistry.findDefaultContainer and \
    # findMatchingContainer, ProtocolRegistry.findDefaultProtocol and \
    # findSpecificProtocol, ArquillianResourceTestEnricher.getResourceAnnotation \
    # and DeploymentScenario.findMatchingDeployment. That is what moves loop.size \
    # 110 -> 103, lambda.size 62 -> 82, lambda.files 34 -> 41 and \
    # stream.opmethods 27 -> 33, with anonymousfunctional.convertible 1 -> 0 at \
    # RemoteTestExecuterTestCase:68 and http.clientref 11 -> 9 where the static \
    # HttpClient field in ServletMethodExecutor was inlined into execute(). \
    # \
    # EVERY ONE OF THOSE SIX IS A FACT ABOUT THE GOLDEN AND NONE IS A FACT ABOUT \
    # THIS RUNG'S INPUT. The blind agent left all seven methods as loops: \
    # findDefaultProtocol in the snapshot is still the early-return ladder, and \
    # the `.or(` the golden's version carries is precisely the third arrival that \
    # target.b13.opt.arrived declines to ask for. The text that stood here \
    # explained the six as a blind agent's free rewriting -- the right reasoning \
    # aimed at the wrong tree, because the report it was written from had been \
    # cut against step 10's golden rather than its blind snapshot. The other \
    # forty-five passed either way, which is why this file did not catch it and a \
    # blind run did. \
    test "${dia}" = 71; \
    test "${exp}" = 36; \
    test "${ean}" = 13; \
    test "${res}" = 7; \
    test "${clo}" = 0; \
    test "${raw}" = 0; \
    test "${sup}" = 44; \
    test "${lam}" = 62; \
    test "${lamf}" = 34; \
    test "${idc}" = 65; \
    test "${ism}" = 24; \
    test "${isa}" = 24; \
    test "${sopm}" = 27; \
    test "${anf}" = 12; \
    test "${blop}" = 110; \
    test "${con}" = 1; \
    test "${fec}" = 0; \
    test "${ort}" = 16; \
    test "${oog}" = 0; \
    test "${acc}" = 1; \
    test "${col}" = 25; \
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
    test "${vsz}" = 78; \
    test "${vcn}" = 0; \
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
    # ...and step 08's own work, in the ARRIVED state, because B-15 is where it \
    # now lives. Step 08 asked for the platform's ServiceLoader in place of two \
    # hand-rolled provider scanners and it got them: platformload stands at 2, \
    # platformtype at 2, providertype at 2, and the two scanners that remain \
    # are the two svc.txt never named -- legacyscan 2, loadclass 11. NONE OF \
    # THESE SIX MOVED UNDER STEP 10, whose family C asked for type patterns, a \
    # pattern switch and sequenced-collection reads, and touched no service \
    # lookup at all. These are the figures test.sh's B-15 header re-measured on \
    # this rung's seed, asserted here so that the header is a claim with a \
    # check under it -- re-measured and unmoved is a result, not a reason to \
    # drop the check. \
    test "$(sed -n 's/^svc\.platformload=//p'  ${bsrc} | head -1)" = 2; \
    test "$(sed -n 's/^svc\.platformtype=//p'  ${bsrc} | head -1)" = 2; \
    test "$(sed -n 's/^svc\.providertype=//p'  ${bsrc} | head -1)" = 2; \
    test "$(sed -n 's/^svcload\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=platform')" = 4; \
    test "$(sed -n 's/^svc\.legacyscan=//p'    ${bsrc} | head -1)" = 2; \
    test "$(sed -n 's/^svc\.loadclass=//p'     ${bsrc} | head -1)" = 11; \
    # ...and step 09's own work, in the ARRIVED state, because B-16 is where it \
    # now lives. The rung below asked for pattern `instanceof`, a text block, \
    # sealed hierarchies and arrow switches, and it got all four -- and then \
    # step 10 ADDED to two of those populations, which is why three of these \
    # figures are not step 09's. 31 instanceof sites are scanned where step 09 \
    # left 28, because step 10's C-2 turned three reflective type tests into \
    # `instanceof` patterns; 24 are bound where step 09 left 21; the 7 plain \
    # ones are the same 7, and not one is convertible. The one HTML document \
    # is a text block, the XML builder's newline literals are down to 7 in one \
    # file, eleven roots are sealed over 35 permitted subtypes between them, \
    # and the switch count is 6 rather than step 09's 2 -- the four step 10 \
    # wrote are pattern switches and they are arrow form too, so `colon` is \
    # still 0. These are the figures test.sh's B-16 header says were \
    # re-measured on this rung's seed, asserted here so that the header is a \
    # claim with a check under it. \
    test "$(sed -n 's/^ipattern\.size=//p'        ${bsrc} | head -1)" = 31; \
    test "$(sed -n 's/^ipattern\.bound=//p'       ${bsrc} | head -1)" = 24; \
    test "$(sed -n 's/^ipattern\.plain=//p'       ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^ipattern\.convertible=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^ipattern\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=bound')" = 24; \
    test "$(sed -n 's/^ipattern\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 31; \
    test "$(sed -n 's/^textblock\.size=//p'  ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^textblock\.files=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^textblock\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 1; \
    test "$(sed -n 's/^strnl\.size=//p'  ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^strnl\.files=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^sealed\.decl=//p'      ${bsrc} | head -1)" = 11; \
    test "$(sed -n 's/^sealed\.nonsealed=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^sealed\.permits=//p'   ${bsrc} | head -1)" = 35; \
    test "$(sed -n 's/^sealed\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 11; \
    test "$(sed -n 's/^switch\.arrow=//p' ${bsrc} | head -1)" = 6; \
    test "$(sed -n 's/^switch\.colon=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^switch\.size=//p'  ${bsrc} | head -1)" = 6; \
    test "$(sed -n 's/^switch\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=arrow')" = 6; \
    # ...and step 10's own work, in the ARRIVED state, because B-17 is where \
    # it now lives and this is the first rung at which that block exists. The \
    # rung below asked for type patterns in place of reflective type tests, \
    # pattern switches in place of dispatch chains, and sequenced-collection \
    # calls in place of index-0 reads, and it got all three. Every one of \
    # these figures is a POPULATION THAT SHRANK or an arrival that appeared, \
    # and the shrinkage is the proof rather than a side effect: reftest 35 -> \
    # 22, the 13 converted sites no longer being reflective type tests at all; \
    # seqfirst 26 -> 19 with seqnew standing at the 7 that replaced them, and \
    # `guarded` down from 8 to 1 because ALL SEVEN converted reads were guarded \
    # ones -- the single guard left is DynamicServiceLoader.onlyOne, an \
    # iterator-next over a Set rather than an index-0 read, which is why C-4 \
    # never asked about it; swpat at 4 switches over 10 type-pattern labels with \
    # one `case null`, where the same scan measured 0, 0 and 0 one rung ago. \
    test "$(sed -n 's/^reftest\.size=//p'    ${bsrc} | head -1)" = 22; \
    test "$(sed -n 's/^reftest\.literal=//p' ${bsrc} | head -1)" = 12; \
    test "$(sed -n 's/^reftest\.var=//p'     ${bsrc} | head -1)" = 10; \
    test "$(sed -n 's/^reftest\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 22; \
    test "$(sed -n 's/^seqfirst\.size=//p'    ${bsrc} | head -1)" = 19; \
    test "$(sed -n 's/^seqfirst\.guarded=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^seqfirst\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 19; \
    test "$(sed -n 's/^seqnew\.size=//p' ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^seqnew\.read=//p' ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^seqnew\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 7; \
    test "$(sed -n 's/^swpat\.size=//p'     ${bsrc} | head -1)" = 4; \
    test "$(sed -n 's/^swpat\.patterns=//p' ${bsrc} | head -1)" = 10; \
    test "$(sed -n 's/^swpat\.nullcase=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^swpat\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 4; \
    # ...and what THIS rung has not done, which is the whole of ITS family C. \
    # Three of the four legs are visible in a source scan and every one of \
    # them reads a measured zero on the tree the candidate is handed: not one \
    # of the 67 privileged-API sites has gone, not one of the 104 catch \
    # parameters is written `_`, and there is no VarHandle anywhere in the \
    # tree. C-1 is the fourth and it is not here: a class-file major is a \
    # property of a build and there is no build in this layer, so it is \
    # asserted in the calibration run instead. \
    # \
    # THE POPULATIONS ARE ASSERTED BESIDE THE ZEROES, which is the half that \
    # matters. A zero on its own is also what an empty tree reads, and three \
    # legs reading zero because the scan found nothing to score would make the \
    # floor look right for the wrong reason. 67 privileged sites in 27 methods \
    # across the 7 SecurityActions files, 20 of them doPrivileged CALLS and 47 \
    # of them type references; 104 catch parameters of which 39 name something \
    # nothing reads; 14 setAccessible calls with not one isAccessible guard in \
    # front of them: the work is there and undone. \
    # \
    # THE CATCH COUNT HERE IS 104 AND THE FROZEN FILE HOLDS 93. The scan sees \
    # the whole tree; baseline/catchparam.txt is cut from main sources alone, \
    # and the 11 records in src/test fall outside every frozen bucket and \
    # therefore outside the leg. Both numbers are asserted, here and in the \
    # stage that reads the file, so that a file which had quietly acquired a \
    # test-scope row, or a scan which had quietly stopped seeing one, moves \
    # one of them and not the other. \
    # \
    # THE TWO ZEROES BELOW ARE ARRIVAL COUNTERS AND NOT POPULATIONS. \
    # catchparam.unnamed records a parameter written `_`, which was not a name \
    # a Java 6 tree could have used, and varhandle records two calls that only \
    # exist from Java 9; both are what this rung asks the candidate to \
    # produce, so both must read empty on the input tree, and a non-zero here \
    # would mean the scan had been taken on something other than the seed. \
    test "$(sed -n 's/^priv\.size=//p'    ${bsrc} | head -1)" = 67; \
    test "$(sed -n 's/^priv\.methods=//p' ${bsrc} | head -1)" = 27; \
    test "$(sed -n 's/^priv\.files=//p'   ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 67; \
    test "$(sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=call ')" = 20; \
    test "$(sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=typeref ')" = 47; \
    test "$(sed -n 's/^catchparam\.size=//p'    ${bsrc} | head -1)" = 103; \
    test "$(sed -n 's/^catchparam\.read=//p'    ${bsrc} | head -1)" = 64; \
    test "$(sed -n 's/^catchparam\.unread=//p'  ${bsrc} | head -1)" = 39; \
    test "$(sed -n 's/^catchparam\.unnamed=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 103; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=unnamed')" = 0; \
    test "$(sed -n 's/^varhandle\.size=//p'    ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^varhandle\.arrival=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^accessible\.setaccessible=//p' ${bsrc} | head -1)" = 14; \
    test "$(sed -n 's/^accessible\.isaccessible=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^newinstance\.ctor=//p'   ${bsrc} | head -1)" = 13; \
    test "$(sed -n 's/^newinstance\.legacy=//p' ${bsrc} | head -1)" = 0; \
    # THE FLOOR THIS ADDS UP TO, stated here because this is where its inputs \
    # are, and it is the first floor on this ladder that is not 0.475000. \
    # Family A is the jdk25-step11 recording -- step 10's with 36 packaging \
    # transcripts RE-RECORDED, because C-2 deletes a class file out of \
    # arquillian-protocol.jar and those 36 enumerate that jar entry by entry -- \
    # so it reads 903/939 = 0.961661 on the input tree rather than 1.0; \
    # family B is the inherited families on the tree that satisfied them, all \
    # of them flat EXCEPT B-6, whose 1105-signature expectation is cut from \
    # the finished tree while the input tree still publishes the 42 signatures \
    # C-2 withdraws -- (1105-42)/1105 = 0.961991, family B 0.997882; \
    # family C reads ZERO, on every one of its four legs, because C-1 is a \
    # must-flip and not a report -- the tree reports its own major honestly, \
    # and 65 honestly reported is still not the 66 level.txt asks for. \
    # 0.30*0.961661 + 0.70*(0.25*0.997882 + 0.75*0) = 0.463128, against \
    # migration_prompt item 6's 0.5, and 0.036872 of margin. NOTES 16.17 and \
    # 16.18 record how both shortfalls were measured; neither is slack, and \
    # family C is still the only family that can move under a candidate. \
    # The two files agree about the things that must not move. \
    test "$(( exp - ean ))" = "${tnon}"; \
    test "${tnon}" = 23; \
    test "${anf}" = "${tanf}"; \
    # try-with-resources is one of the four denominators step 08 re-based, and \
    # three rungs on it is still the place a copied baseline would show. Step \
    # 08's C-3 deleted the two `try (InputStream is = url.openStream(); \
    # BufferedReader r = ...)` clauses along with the loops they fed, so its \
    # target said 7 against a tree that held 9. Every rung since has been \
    # handed the tree that did it: the hold IS 7, the target stays 7, and the \
    # difference this assertion states is ZERO. Nothing this rung asks for \
    # touches a resource clause either -- C-2 deletes doPrivileged calls, \
    # imports and enum bodies out of seven classes that open nothing, C-3 \
    # renames catch parameters, and C-4 replaces a Field with a VarHandle -- so \
    # a non-zero difference here means the baseline was measured against some \
    # earlier rung's input by mistake, which is the exact error NOTES 14.3 \
    # records being caught once already. \
    test "${res}" = 7; \
    test "${tres}" = 7; \
    test "$(( res - tres ))" = 0; \
    # java.io has NOT moved: the 4 sites step 09 was handed are the 4 this rung \
    # is handed, because nothing in step 10's family C touched a File cast or a \
    # stream declaration. B-10 reads the number as a floor -- java.io may not \
    # come BACK -- so the equality asserted here is between the scan and the \
    # target, and the target is re-derived from this rung's own seed rather \
    # than carried forward from step 10's. \
    test "$(( ion + iod ))" = "${tio}"; \
    test "${tio}" = 4; \
    test "${ism}" = "${esam}"; \
    test "${isa}" = "${esam}"; \
    # B-11.1's population is the ONE inherited denominator in this block that \
    # THIS rung moves, and it moves DOWN: 20 files at step 04, 13 here. Seven \
    # of step 04's twenty are SecurityActions classes whose only anonymous \
    # functional body was the `PrivilegedAction` step 04 converted to a lambda; \
    # C-2 unwraps the `doPrivileged` call and the lambda leaves with the \
    # wrapper it was an argument to. The seven FILES survive -- they still hold \
    # the reflective helpers their callers use -- so nothing looking for a \
    # deleted file would see this; what leaves is the construct B-11.1 counts. \
    # Staged forward at 20 it would have capped a CORRECT tree at 13/20, \
    # B-11.1 = 0.650000 and B-11 = 0.930000 on the golden itself, which is how \
    # it was found: the golden row of this rung's own calibration table. \
    # NOTES 16.20. B-11's other two denominators do NOT move -- C-2 deletes no \
    # interface and no collector -- so they stand at step 04's figures on \
    # either side of it, and a re-cut that had caught more than it meant to \
    # would have moved one of them too. \
    test "${elf}" = 13; \
    test "${elf}" -lt "${lamf}"; \
    test "${ecm}" = 24; \
    test "${ecm}" -le "${sopm}"; \
    # The diamond pair, and by the same argument the difference is ZERO. Both \
    # of the `new LinkedHashSet<>()` sites step 08's C-3 deleted -- \
    # DynamicServiceLoader:196 and spi ServiceLoader:142 -- are gone from the \
    # tree this rung is handed, so hold and target are both 71. B-10's own \
    # denominator b10.diamond stays at 93 regardless: it counts the step-03 \
    # population, which is a history and not a census of this tree. \
    test "${tdia}" = 71; \
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
    # that has a second source should have one -- AND ONE OF THEM NOW HAS A \
    # DIFFERENT SECOND SOURCE, because this rung is what moved it. B-13.3's \
    # setacc half asked for all 14 setAccessible calls up to the rung below; \
    # C-4 converts 4 of them to a VarHandle, so a CORRECT tree holds 10 and a \
    # leg still asking 14 would be an inherited gate nothing correct could \
    # pass. target.b13.setacc is therefore the GOLDEN's figure and not this \
    # tree's; the tree's own 14 is staged beside it as target.b13.setacc.was, \
    # and THAT is what accessible.setaccessible is checked against here, with \
    # the difference between the two being C-4's four. target.b13.guards does \
    # NOT move with it: all twelve guarded methods are still here, three of \
    # them reaching their field through a VarHandle now, and a guard coming \
    # back is still a failure. The restatement itself is re-derived from \
    # varhandle.txt's verdict column further down; what is checked here is the \
    # arithmetic between the three keys. NOTES 16.16. \
    test "${g2l}" = 2; \
    test "${g2f}" = 13; \
    test "${g2f}" = "${nis}"; \
    test "${g3g}" = 12; \
    test "${g3s}" = 10; \
    test "${g3sw}" = 14; \
    test "${g3sw}" = "${acst}"; \
    test "$(( g3sw - g3s ))" = 4; \
    test "${g3g}" -lt "${g3sw}"; \
    test "${g4b}" = 12; \
    test "${g5b}" = 7; \
    test "${g5v}" = "${lcvf}"; \
    test "${g5v}" = 7; \
    # B-13.6's pair, unchanged from the four rungs below it. The arrival \
    # denominator has been 2 against a departure set of 3 since step 07, \
    # because step 06's blind agent closed all three `isPresent()` branches and \
    # arrived at a Java 9 method in only two of them. Step 10 was asked for type \
    # patterns, a pattern switch and sequenced reads, and left the third alone, \
    # so the seed this rung hands out holds the same shortfall and the strict \
    # inequality still stands. The derivation layer below says WHICH bucket is \
    # missing rather than trusting the count. NOTES 16.20. \
    test "${g6d}" = 3; \
    test "${g6a}" = 2; \
    test "${g6a}" -lt "${g6d}"; \
    # The `var` pair, and the ONE denominator this rung re-bases on its own \
    # account. want is 78 and the tree holds 78, as above. candidatenew has \
    # been want+1 on every rung since step 06, because step 06 exempted one of \
    # its 81 candidates -- the diamond initialiser at DynamicServiceLoader:196, \
    # which would have written its type argument back out -- and the departure \
    # numerator `candidatenew - var.candidatenew` had to leave room for it. \
    # Step 08's C-3 deleted that line. The tree this rung is handed holds \
    # var.candidatenew=0, so a carried 79 over a want of 78 would clamp to 1.0 \
    # with one regression still to spare: an inherited leg loosened by the work \
    # of the rung below, the ninth trap in its exact shape. candidatenew is \
    # therefore re-derived here as want plus the redundant declarations the \
    # input tree actually still holds -- 78 + 0 -- and the pair is now EQUAL \
    # rather than off by one. If a later rung exempts a candidate again the \
    # allowance comes back by itself, because the generator adds what it \
    # measures instead of a constant. \
    test "${g7c}" = 78; \
    test "${g7w}" = 78; \
    test "${g7c}" = "$(( g7w + vcn ))"; \
    test "${vsz}" = 78; \
    test "${g7w}" = "${vsz}"; \
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
    # STEP 07'S MISSING THIRD IS STILL MISSING, and the relation below says so \
    # in the only way that survives a re-cut. optops.txt names three methods \
    # that had to lose an `isPresent()` branch -- optop.ispresent is 0 above, so \
    # all three lost it -- and step 06's blind agent reached for a Java 9 method \
    # at only two of them: at ProtocolRegistry#findDefaultProtocol it wrote an \
    # early-return ladder over `Optional.of` / `Optional.empty()`, which departs \
    # correctly and arrives at nothing this gate can see. That was adjudicated \
    # an agent miss and not a scorer fault (NOTES 11.18), and steps 07, 08, 09 \
    # and 10 all scored the arrival half over two. So does this rung: step 10's \
    # family C asked for type patterns, a pattern switch and sequenced reads, \
    # and left that method alone, so the seed it hands on still holds the \
    # ladder -- its blind agent met all four legs in full and still did not \
    # touch this one. \
    # \
    # THE RELATION IS AN INTERSECTION WITH A REMAINDER ON EACH SIDE, and both \
    # remainders are NAMED rather than counted. A file re-cut by dropping the \
    # wrong row and adding another would satisfy every count above; the greps \
    # below say WHICH bucket is in optops.txt and did not arrive, and WHICH \
    # arrived from outside optops.txt, so a re-measure that lost either one \
    # fails here and the denominator moves deliberately instead of drifting. \
    # NOTES 16.20. \
    test "$(LC_ALL=C comm -23 /tmp/s-optopsk.txt /tmp/d-optarr.txt | grep -c .)" = 1; \
    LC_ALL=C comm -23 /tmp/s-optopsk.txt /tmp/d-optarr.txt \
      | grep -qE '^impl-base/src/main/java/.*/ProtocolRegistry\.java#findDefaultProtocol$'; \
    # ...and the bucket that arrives from OUTSIDE step 06's three is named too. \
    # ToolingDeploymentFormatter#formatNode holds a Java 9 `stream` on this \
    # tree, as it did on step 10's seed. It was never one of step 06's three, \
    # so what keeps it out of the population is the intersection and not a rule \
    # about it -- and the record it does leave is asserted, so "outside the \
    # population" cannot quietly become "unscanned". \
    test "$(LC_ALL=C comm -23 /tmp/d-optarr.txt /tmp/s-optopsk.txt | grep -c .)" = 1; \
    LC_ALL=C comm -23 /tmp/d-optarr.txt /tmp/s-optopsk.txt \
      | grep -qE '^impl-base/src/main/java/.*/ToolingDeploymentFormatter\.java#formatNode$'; \
    test "$(grep -c 'ToolingDeploymentFormatter' /tmp/s-optarr.txt || true)" = 0; \
    test "$(sed -n 's/^optop\[[0-9]*\]=//p' ${bsrc} \
              | grep -c 'ToolingDeploymentFormatter\.java:[0-9]* use stream in=formatNode$')" = 1; \
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
    # --- accessible.txt, step 06's, RE-CUT: the two halves part company here -- \
    # STEP 06 CUT THIS FILE AND EVERY RUNG SINCE STAGED IT UNCHANGED. This rung \
    # is the first that cannot. Its own C-4 converts four of the 14 \
    # `setAccessible` calls to a VarHandle, and THREE OF THE FOUR SIT INSIDE \
    # STEP 06'S OWN TWELVE GUARD BUCKETS -- so the subset relation step 06 \
    # built the file on ("every guard bucket is also a setacc bucket") is \
    # FALSE here, and it is false on the golden rather than on some failure. \
    # Staged forward at 12 and 14 the leg would read, on a correctly migrated \
    # tree, 12 of 12 guards departed but only 10 of 14 setAccessible calls \
    # arrived -- the arrival half capped at 0.714286 by work this rung was \
    # ASKED to do. An inherited gate unpassable on a correct tree is the trap \
    # this whole ladder is built to catch, and this is the ninth rung's turn \
    # to catch it. So the DEPARTURE half still asks all 12 -- every one of \
    # those methods is still in the tree, three of them now reaching their \
    # field through a handle, and a guard coming back is a failure however the \
    # field is read -- and the ARRIVAL half is restated over the 10 that \
    # survive. target.b13.guards is UNCHANGED at 12; target.b13.setacc moves \
    # to 10 and its old value is kept beside it as target.b13.setacc.was. \
    # \
    # THE FOUR THAT LEAVE ARE NOT TYPED INTO THE FILE EITHER. accessible.txt's \
    # setacc half is the frozen survivor list and varhandle.txt carries the \
    # verdict on each of the 14; the three lines below are the whole \
    # derivation, and the two files agreeing with each other and with this \
    # tree is what makes the restatement a measurement rather than an edit. \
    fbucketise accessible call=isAccessible  > /tmp/d-guard.txt; \
    fbucketise accessible call=setAccessible > /tmp/d-acc-all.txt; \
    setpart /tests/baseline/accessible.txt guard  > /tmp/s-guard.txt; \
    setpart /tests/baseline/accessible.txt setacc > /tmp/s-setacc.txt; \
    # The setacc half as DERIVED from varhandle.txt's own verdicts: every one \
    # of the 14 rows that is not `verdict=convert`. A row's bucket is its \
    # `in=`, so this is the same key the scorer builds, and the departure from \
    # it is exactly the four this rung's C-4 moves. \
    awk '$0 !~ /^#/ && $0 != "" { split($1, a, ":"); m = ""; vd = ""; \
             for (i = 2; i <= NF; i++) { \
                if (substr($i, 1, 3) == "in=")     m  = substr($i, 4); \
                if (substr($i, 1, 8) == "verdict=") vd = substr($i, 9) } \
             if (m != "" && vd != "convert") print a[1] "#" m }' \
      /tests/baseline/varhandle.txt | LC_ALL=C sort -u > /tmp/d-acc-left.txt; \
    test "$(grep -c . /tmp/s-guard.txt)"   = "${g3g}"; \
    test "$(grep -c . /tmp/s-setacc.txt)"  = "${g3s}"; \
    test "$(grep -c . /tmp/d-acc-left.txt)" = "${g3s}"; \
    test "${g3g}" = 12; \
    test "${g3s}" = 10; \
    test "${g3sw}" = 14; \
    test "${g3sb}" = "${g3s}"; \
    if ! cmp -s /tmp/d-acc-left.txt /tmp/s-setacc.txt; then \
        echo "baseline/accessible.txt's setacc half is not varhandle.txt's 14 rows minus the 4 it converts:" >&2; \
        diff /tmp/s-setacc.txt /tmp/d-acc-left.txt >&2 || true; exit 1; \
    fi; \
    # NOT ONE GUARD BUCKET IS MISSING ITS CALL, which is the stronger half and \
    # the one that would stay true if the restatement were wrong in the other \
    # direction: the three guards that left the setacc half are called out \
    # below as exactly the guard buckets varhandle.txt marks convert, so a \
    # tree that DELETED one of the other nine is a diff here. \
    test "$(LC_ALL=C comm -12 /tmp/s-guard.txt /tmp/s-setacc.txt | grep -c .)" = 9; \
    test "$(LC_ALL=C comm -23 /tmp/s-guard.txt /tmp/s-setacc.txt | grep -c .)" = 3; \
    awk '$0 !~ /^#/ && $0 != "" { split($1, a, ":"); m = ""; vd = ""; \
             for (i = 2; i <= NF; i++) { \
                if (substr($i, 1, 3) == "in=")     m  = substr($i, 4); \
                if (substr($i, 1, 8) == "verdict=") vd = substr($i, 9) } \
             if (m != "" && vd == "convert") print a[1] "#" m }' \
      /tests/baseline/varhandle.txt | LC_ALL=C sort -u > /tmp/d-acc-gone.txt; \
    # THE THREE GUARD BUCKETS THAT LEFT ARE THREE OF THE FOUR THAT CONVERT, \
    # and the fourth converted bucket is a file that never had a guard at all. \
    # Asserting the containment rather than the equality is what keeps a \
    # scanner's bucket from being quietly counted as converted: those six rows \
    # are `verdict=scanner` and none of them may appear here. \
    LC_ALL=C comm -12 /tmp/d-acc-gone.txt /tmp/s-guard.txt > /tmp/d-gconv.txt; \
    test "$(grep -c . /tmp/d-gconv.txt)" = 3; \
    LC_ALL=C comm -23 /tmp/s-guard.txt /tmp/s-setacc.txt > /tmp/d-guard-gone.txt; \
    if ! cmp -s /tmp/d-guard-gone.txt /tmp/d-gconv.txt; then \
        echo "the guard buckets missing from the setacc half are not the guard buckets varhandle.txt converts:" >&2; \
        diff /tmp/d-guard-gone.txt /tmp/d-gconv.txt >&2 || true; exit 1; \
    fi; \
    # ON THE INPUT TREE THIS RUNG IS HANDED -- and that is the tree source.txt \
    # is a census of -- there are still 14 `setAccessible` calls and 0 \
    # `isAccessible` calls in 12 files: step 06's work is done and this rung's \
    # C-4 has not started. A file that had put a guard back would show up here \
    # as a non-zero isaccessible, and the four figures target.acc.* carries \
    # partition the same 14. \
    test "${acst}" = "${g3sw}"; \
    test "$(sed -n 's/^accessible\.isaccessible=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^accessible\.files=//p' ${bsrc} | head -1)" = "${tacf}"; \
    test "${tacf}" = 12; \
    test "$(grep -c . /tmp/d-guard.txt)" = 0; \
    # AND THE FOUR FIGURES target.txt CARRIES FOR THIS HALF PARTITION THE SAME 14. \
    test "${tact}" = "${g3sw}"; \
    test "${tacv}" = 4; \
    test "${tach}" = "${g3s}"; \
    test "$(( tacv + tach ))" = "${tact}"; \
    rm -f /tmp/d-guard.txt /tmp/d-acc-all.txt /tmp/d-acc-left.txt \
          /tmp/d-acc-gone.txt /tmp/d-guard-gone.txt /tmp/d-gconv.txt; \
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
    # AND THE DENOMINATOR THE RUNG BELOW RE-BASED, which is the one the frozen \
    # file makes visible rather than only arithmetical. varbuckets.txt is step \
    # 06's population and it is FROZEN: 81 records, verbatim, three rungs on, \
    # and step 09 does not get to edit a file cut three rungs back. Step 08's \
    # C-3 deleted THREE of the 81 when it replaced the two hand-rolled \
    # META-INF/services readers with the platform's ServiceLoader -- the two \
    # BufferedReader resource declarations and, in the same two methods, the \
    # LinkedHashSet at DynamicServiceLoader:196 -- so the tree this rung is \
    # handed holds 78, target.b13.want says 78 and target.b13.candidatenew now \
    # says 78 as well. Comparing the file's line count to ${g7c} directly would \
    # fail on a CORRECT tree, which is the ninth trap wearing this ladder's \
    # clothes. The subtraction is asserted instead, so that neither side can \
    # drift alone: \
    test "$(grep -c . /tmp/s-var.txt)" = 81; \
    test "$(( $(grep -c . /tmp/s-var.txt) - g7c ))" = 3; \
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
    # NEITHER CONDITION ALONE PICKS THE THREE, and the two halves of the \
    # subtraction are not the same two. Restricted to the two legacy methods \
    # the frozen file holds THREE records: the two readers -- which came out of \
    # the 80 target.b13.want counts, taking it to 78 -- plus the LinkedHashSet \
    # at DynamicServiceLoader:196, which was step 06's EXEMPTION and was never \
    # in the 80 at all. All three went with step 08's rewrite, which is why the \
    # line count falls by 3 while want falls by 2, and why candidatenew stops \
    # carrying its +1 here: the line it was carrying it for is gone. \
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
    # THE EXEMPTION IS SPENT, and that is asserted as an absence rather than \
    # left to the arithmetic above. 81 candidates at step 06, 80 asked for, and \
    # the one that was not was the diamond initialiser at DynamicServiceLoader: \
    # converting it would have written the type argument back out and undone a \
    # step-03 gain. Step 08 deleted the line. The tree this rung is handed \
    # holds NO ctor=yes varcand record at all -- not one at a different site, \
    # which would mean a candidate had written a new redundant declaration, and \
    # not the old one, which would mean the wrong tree was measured. \
    sed -n 's/^varcand\[[0-9]*\]=//p' ${bsrc} | awk '$0 ~ /ctor=yes/' \
      | LC_ALL=C sort > /tmp/d-varleft.txt; \
    test "$(grep -c . /tmp/d-varleft.txt)" = "${vcn}"; \
    test "${vcn}" = 0; \
    test "${vcn}" = "$(( g7c - g7w ))"; \
    if grep -q 'DynamicServiceLoader\.java' /tmp/d-varleft.txt; then \
        echo "DynamicServiceLoader still holds step 06's exempt declaration; step 08's C-3 deletes it" >&2; \
        exit 1; \
    fi; \
    # ...and the site itself is gone from the tree, not merely re-spelled. The \
    # arithmetic above would read the same if the declaration had become a \
    # `var` instead of being deleted, and those are different trees: one did \
    # step 08's rewrite and one undid step 06's exemption. \
    if sed -n 's/^var\[[0-9]*\]=//p' ${bsrc} \
         | grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java:[0-9]+ .*in=load$'; then \
        echo "DynamicServiceLoader#load still declares a local; step 08's C-3 deletes the LinkedHashSet" >&2; \
        exit 1; \
    fi; \
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
    # ...and the tree now holds TWO MORE collect buckets than sinks.txt names, \
    # which is an inequality and was an equality one rung ago. Step 08's C-3 \
    # replaced two hand-rolled provider scanners with the platform's \
    # ServiceLoader and both rewrites end in a Collectors call, so \
    # collect.size went 23 -> 25 without a single one of step 05's 23 sinks \
    # moving. Asserting equality against ${tsnk} here would fail on the correct \
    # tree; asserting only the subset above would let a rung delete a sink and \
    # add a new one unnoticed. The count, the difference and the NAMES of the \
    # two arrivals are all three stated. \
    test "$(grep -c . /tmp/b-collect.txt)" = 25; \
    test "$(( $(grep -c . /tmp/b-collect.txt) - tsnk ))" = 2; \
    LC_ALL=C comm -13 /tmp/key-sinks.txt /tmp/b-collect.txt > /tmp/d-newcol.txt; \
    test "$(grep -c . /tmp/d-newcol.txt)" = 2; \
    grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java#load$' /tmp/d-newcol.txt; \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java#reload$' /tmp/d-newcol.txt; \
    rm -f /tmp/d-newcol.txt; \
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
    # bucket. toCollection is accepted where toList or toSet is wanted, which \
    # is the scorer's own rule, and this tree reaches it NINE times: 7 of step \
    # 05's 23 sinks -- one fewer than step 06 was handed, because step 06's own \
    # C-5 turned MethodParameterInjectionPoint#getAnnotations into a Set.of \
    # call and the toCollection that had been in it went with the construction \
    # -- plus the two step 08's C-3 wrote, which are the same two buckets the \
    # count above named. The 7 is asserted INSIDE sinks.txt's population and \
    # the 2 outside it, because a single total of 9 would go on reading 9 if a \
    # sink lost its collector and a new site gained one. \
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
    awk '$2 == "toCollection" { print $1 }' /tmp/b-collect-to.txt \
      | LC_ALL=C sort -u > /tmp/d-tocoll.txt; \
    test "$(grep -c . /tmp/d-tocoll.txt)" = 9; \
    test "$(LC_ALL=C comm -12 /tmp/d-tocoll.txt /tmp/key-sinks.txt | grep -c .)" = 7; \
    LC_ALL=C comm -23 /tmp/d-tocoll.txt /tmp/key-sinks.txt > /tmp/d-tocnew.txt; \
    test "$(grep -c . /tmp/d-tocnew.txt)" = 2; \
    grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java#load$' /tmp/d-tocnew.txt; \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java#reload$' /tmp/d-tocnew.txt; \
    rm -f /tmp/d-tocoll.txt /tmp/d-tocnew.txt; \
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
    # --- modnames.txt: the twelve names B-15.2 grades against ---------------- \
    # STEP 08'S OWN BASELINE, inherited whole. It gets the same treatment the \
    # other inherited set files got: derived from a second source wherever a \
    # second source exists, and tied to the target keys that divide by it, so \
    # that no number in this image is only a number in one file. \
    # \
    # ONE TARGET KEY WENT WITH THE INHERITANCE. Step 08's C-1 scored whether a \
    # name was DERIVABLE from the packages, and target.mod.derivable stated the \
    # 11 of 12 that were. B-15 keeps only the naming and service legs, \
    # derivability is reported by the module gate and scored by nothing, and \
    # this rung's target.txt therefore carries no b15.mod.derivable at all, \
    # for the second rung running. \
    # Asserting it here would read the empty string; the absence is asserted \
    # instead, once, so that a later rung cannot quietly re-introduce the key \
    # without also re-introducing the leg that reads it. \
    grep '^[a-z]' /tests/baseline/modnames.txt > /tmp/s-modn.txt; \
    test "$(grep -c . /tmp/s-modn.txt)" = "${tmt}"; \
    test "${tmt}" = 12; \
    test "$(grep -c '^target\.b15\.mod\.derivable=' ${btgt})" = 0; \
    test "$(grep -c 'mod\.derivable' ${btgt})" = 0; \
    if grep -q "tv 'target\\.b15\\.mod\\.derivable'" /tests/test.sh; then \
        echo "test.sh reads target.b15.mod.derivable; B-15 does not score derivability" >&2; \
        exit 1; \
    fi; \
    grep -qF 'gate.b15.mod.derivable' /tests/test.sh; \
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
    # ...and the buckets are source.txt's and not a hand-written list -- but \
    # the relation between the two files is not the one step 08 asserted, \
    # because the tree moved between them. NINETEEN svcload records now, in \
    # five forms: the 2 legacyscan and 11 loadclass that stayed, and the three \
    # forms step 08's C-3 brought in -- 2 platformload, 2 platformtype, 2 \
    # providertype, spread over DynamicServiceLoader#load and spi \
    # ServiceLoader#reload. The five counts are asserted separately rather than \
    # as a total of 19: a rewrite that reached ServiceLoader.load() without \
    # using its Provider type would move two of them and leave the total alone. \
    # \
    # PLATFORMLOAD AND PLATFORMTYPE ARE BOTH TWO AND THAT IS NOT A REPETITION: \
    # the first counts load() calls and the second counts the spellings of the \
    # `java.util.ServiceLoader` name that reach them, and this tree names the \
    # type exactly once per call. They are asserted as two separate twos rather \
    # than as one figure used twice, because a rewrite that imported the type \
    # and dropped the qualified name would move the second and leave the first. \
    sed -n 's/^svcload\[[0-9]*\]=//p' ${bsrc} | LC_ALL=C sort > /tmp/d-svcload.txt; \
    test "$(grep -c . /tmp/d-svcload.txt)" = 19; \
    test "$(awk '$2 == "form=legacyscan"'   /tmp/d-svcload.txt | grep -c .)" = 2; \
    test "$(awk '$2 == "form=loadclass"'    /tmp/d-svcload.txt | grep -c .)" = 11; \
    test "$(awk '$2 == "form=platformload"' /tmp/d-svcload.txt | grep -c .)" = 2; \
    test "$(awk '$2 == "form=platformtype"' /tmp/d-svcload.txt | grep -c .)" = 2; \
    test "$(awk '$2 == "form=providertype"' /tmp/d-svcload.txt | grep -c .)" = 2; \
    svcbucket() { \
        awk -v want="$1" -v nm="$2" \
            '$2 == ("form=" want) && (nm == "" || $3 == ("name=" nm)) { \
                k = $1; sub(/:[0-9]*$/, "", k); m = ""; \
                for (i = 3; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                if (m != "") print k "#" m }' /tmp/d-svcload.txt | LC_ALL=C sort -u; \
    }; \
    svcbucket legacyscan   '' > /tmp/d-svcscan.txt; \
    svcbucket loadclass    '' > /tmp/d-svcload-b.txt; \
    svcbucket platformload '' > /tmp/d-svcplat.txt; \
    test "$(grep -c . /tmp/d-svcscan.txt)"   = 1; \
    test "$(grep -c . /tmp/d-svcload-b.txt)" = 10; \
    test "$(grep -c . /tmp/d-svcplat.txt)"   = 2; \
    # SVC.TXT'S TWO HALVES NOW SIT ON DIFFERENT FORMS, and that separation is \
    # the whole of what the rung below did. At step 08 the legacy two and the \
    # keep eleven were both drawn from legacyscan+loadclass, thirteen buckets \
    # over one union, and DynamicServiceLoader#load carried one record of each \
    # form. Here the legacy two ARE the platformload two and nothing else is, \
    # while the keep eleven are exactly the legacyscan+loadclass union -- which \
    # is now eleven and not thirteen, because the two that moved took their \
    # legacyscan records with them. \
    # \
    # ALL THREE RELATIONS ARE ASSERTED, not just the two equalities. The \
    # disjointness is the one that catches the failure this rung could \
    # actually produce: a tree that called ServiceLoader.load() and left the \
    # hand-rolled scan beside it would satisfy both equalities as SUPERSETS \
    # and be caught only by the intersection. \
    cat /tmp/d-svcscan.txt /tmp/d-svcload-b.txt | LC_ALL=C sort -u > /tmp/d-svcall.txt; \
    cat /tmp/s-svcleg.txt  /tmp/s-svckeep.txt   | LC_ALL=C sort -u > /tmp/s-svcall.txt; \
    test "$(grep -c . /tmp/d-svcall.txt)" = 11; \
    test "$(grep -c . /tmp/d-svcall.txt)" = "${svk}"; \
    test "$(grep -c . /tmp/s-svcall.txt)" = "$(( svl + svk ))"; \
    if ! cmp -s /tmp/d-svcall.txt /tmp/s-svckeep.txt; then \
        echo "baseline/svc.txt's keep half is not this tree's legacyscan+loadclass buckets:" >&2; \
        diff /tmp/d-svcall.txt /tmp/s-svckeep.txt >&2 || true; exit 1; \
    fi; \
    if ! cmp -s /tmp/d-svcplat.txt /tmp/s-svcleg.txt; then \
        echo "baseline/svc.txt's legacy half is not this tree's platform ServiceLoader.load() buckets:" >&2; \
        diff /tmp/d-svcplat.txt /tmp/s-svcleg.txt >&2 || true; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -12 /tmp/d-svcall.txt /tmp/s-svcleg.txt | grep -c .)" != 0 ]; then \
        echo "a method that moved to ServiceLoader still holds a hand-rolled scan or lookup:" >&2; \
        LC_ALL=C comm -12 /tmp/d-svcall.txt /tmp/s-svcleg.txt >&2; exit 1; \
    fi; \
    test "$(sed 's/#.*//' /tmp/s-svcall.txt | LC_ALL=C sort -u | grep -c .)" = 11; \
    test "$(sed 's/#.*//' /tmp/s-svcall.txt | LC_ALL=C sort -u | grep -c .)" \
       = "$(sed -n 's/^svc\.files=//p' ${bsrc} | head -1)"; \
    # WHICH TWO OF THE THREE SCANNERS MOVED, readable in the records rather \
    # than taken on the file's word. java.util.ServiceLoader answers one \
    # question: give me every provider of this interface. The two sites that \
    # called getResources() -- plural, the whole classpath, a provider list -- \
    # were the two it could replace, and both are gone: not one record in this \
    # tree carries name=getResources, in any form. FilteredURLClassLoader's \
    # getResource() -- singular, one named resource, delegated to a parent \
    # loader -- asks something ServiceLoader has no method for, so it stayed, \
    # and it is the only legacyscan bucket left. A rule written on the form \
    # alone would have moved it too and broken deployment filtering; the \
    # evidence that it did not is that this bucket is still here, one rung on. \
    svcbucket legacyscan getResources > /tmp/d-svcplural.txt; \
    svcbucket legacyscan getResource  > /tmp/d-svcsingular.txt; \
    test "$(grep -c . /tmp/d-svcplural.txt)"   = 0; \
    test "$(grep -c . /tmp/d-svcsingular.txt)" = 1; \
    test "$(grep -c 'name=getResources ' /tmp/d-svcload.txt)" = 0; \
    if ! cmp -s /tmp/d-svcsingular.txt /tmp/d-svcscan.txt; then \
        echo "the one surviving legacyscan bucket is not FilteredURLClassLoader#getResource:" >&2; \
        diff /tmp/d-svcsingular.txt /tmp/d-svcscan.txt >&2 || true; exit 1; \
    fi; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-svcsingular.txt /tmp/s-svckeep.txt | grep -c .)" != 0 ]; then \
        echo "FilteredURLClassLoader#getResource is a legacyscan that svc.txt does not keep" >&2; \
        exit 1; \
    fi; \
    # ...and the two that DID move are named, at the methods svc.txt names, \
    # with the platform type in hand. `ServiceLoader.load()` without \
    # `ServiceLoader.Provider` is the rewrite that instantiates every provider \
    # eagerly, which is the behaviour step 08's C-3 was written to avoid, so \
    # the provider type is asserted at both sites and not merely counted. \
    grep -qE '^impl-base/src/main/java/.*/DynamicServiceLoader\.java#load$'  /tmp/d-svcplat.txt; \
    grep -qE '^spi/src/main/java/.*/util/ServiceLoader\.java#reload$'        /tmp/d-svcplat.txt; \
    svcbucket providertype '' | LC_ALL=C sort > /tmp/d-svcprov.txt; \
    if ! cmp -s /tmp/d-svcprov.txt /tmp/d-svcplat.txt; then \
        echo "a ServiceLoader.load() site does not use ServiceLoader.Provider:" >&2; \
        diff /tmp/d-svcplat.txt /tmp/d-svcprov.txt >&2 || true; exit 1; \
    fi; \
    rm -f /tmp/d-svcprov.txt; \
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
        if [ ! -f "/tests/assets/rec/jdk25-step11/${f}.txt" ]; then \
            echo "baseline/svcids.txt names an id with no jdk25-step11 transcript: ${id}" >&2; \
            exit 1; \
        fi; \
    done < /tmp/s-svcids.txt; \
    # ======================================================================== \
    # STEP 09'S SIX BASELINES, INHERITED, RE-DERIVED IN THE ARRIVED STATE. \
    # ======================================================================== \
    # They get the treatment every inherited set file gets -- derived from a \
    # second source wherever a second source exists, and tied to the target \
    # keys that count it, so that no number in this image is only a number in \
    # one file -- with one difference that belongs to this rung alone. The tree \
    # underneath these six has MOVED: the rung below converted exactly what \
    # they name, so a re-derivation that expected step 09's own split back \
    # would fail on every one of them. What is re-derived here is the same \
    # bucket list against the ARRIVED records, which is a stronger check than \
    # step 09 could make -- it says both that the file still names real methods \
    # and that the work the rung below asked for landed in exactly those \
    # methods and nowhere else. \
    # \
    # Eleven of the thirty staged-and-unread target keys are the b16 tallies \
    # these six files are held against -- see the key-set block above -- and \
    # this is where all eleven are spent. \
    # \
    # --- ipattern.txt: B-16.2's population, RE-CUT ON THIS TREE ------------- \
    # STEP 10 STAGED THIS FILE BYTE FOR BYTE AND THIS RUNG CANNOT, WHICH MAKES \
    # IT THE THIRD POPULATION ON THE LADDER THAT HAD TO BE RE-CUT. Step 06's \
    # accessible.txt was the first and step 09's ipattern.txt the second, and \
    # the cause is the same both times: the rung inheriting it DELETES methods \
    # the file names. This rung's C-2 removes the seven \
    # `SecurityActions#getConstructor` ladders -- one per class -- and every one \
    # of them held exactly one bound pattern over a `NoSuchMethodException`. A \
    # file naming them would ask a CORRECTLY migrated tree for patterns inside \
    # methods it was told to remove, and capsum() takes a per-bucket min, so \
    # each departed bucket contributes zero and a correct tree caps at \
    # 14/21 = 0.666667. \
    # \
    # BUT THE POPULATION MOVED IN BOTH DIRECTIONS, WHICH IS WHY 18 - 7 IS NOT \
    # 14. THREE more methods on this tree hold a bound pattern than step 09 \
    # saw: DeploymentGenerator#buildTestableDeployments, OSGiDeploymentPackager \
    # #generateDeployment and ResourceInjectionEnricher#isPrimitiveNull, one \
    # pattern each. Step 10's requirement 4 rewrote 13 \
    # `X.class.isInstance(value)` tests over 7 methods, and the split is the \
    # whole reason only three arrive here: 10 of those 13 sites, over the other \
    # 4 methods, became `case` labels in the pattern switches its requirement 5 \
    # asked for, and a `case` label is not an `instanceof` and leaves no \
    # ipattern record at all. Only the 3 that became bound `instanceof` \
    # patterns enter this census. All three are `conv litinstance` rows in step \
    # 10's own baseline/reftest.txt, which is where mk-baseline-step11.sh \
    # checks them rather than taking the names on trust. \
    # \
    # SO THE RE-CUT IS 21 BUCKETS AND NOT THE 25 THE INHERITED FILE NAMES OR \
    # THE 28 THE RUNG BELOW WAS HANDED. 14 `conv` hold 17 bound patterns that \
    # must still read kind=bound; 7 `skip` hold 7 sites that had no cast after \
    # the test and could never become one, and must still read kind=plain. 24 \
    # sites over 21 buckets, and the 24 is what target.b16.ip.total divides. \
    # The file is SHORTER than source.txt's own ipattern count of 31, and the \
    # difference is the 7 that DEPART -- the ladder sites, still standing on \
    # the seed this scan censused and gone from the golden the file is cut on. \
    # The three arrivals are on both sides of that subtraction and move it by \
    # nothing: the scan is a census of the tree this rung is handed and the \
    # file is what B-16.2 asks of the tree it hands back. \
    # \
    # THE VERDICT COLUMN IS RE-DERIVED, NOT TAKEN ON TRUST, and it is the same \
    # rule step 10 restated: a bound pattern is one whose record says \
    # kind=bound, which the plugin sets exactly when the cast follows the test. \
    grep -v '^#' /tests/baseline/ipattern.txt | grep . > /tmp/s-ip.txt; \
    test "$(grep -c . /tmp/s-ip.txt)" = 21; \
    test "$(awk '$1 == "conv"' /tmp/s-ip.txt | grep -c .)" = "${ipb}"; \
    test "$(awk '$1 == "skip"' /tmp/s-ip.txt | grep -c .)" = "${ipk}"; \
    test "${ipb}" = 14; \
    test "${ipk}" = 7; \
    test "$(( ipb + ipk ))" = "$(( $(sed -n 's/^target\.b16\.ip\.buckets=//p' ${btgt} | head -1) \
                                  + $(sed -n 's/^target\.b16\.ip\.skip\.buckets=//p' ${btgt} | head -1) ))"; \
    test "$(awk '$1 == "conv" { n += $3 } END { print n + 0 }' /tmp/s-ip.txt)" = "${ips}"; \
    test "$(awk '$1 == "skip" { n += $3 } END { print n + 0 }' /tmp/s-ip.txt)" = "${ipk}"; \
    test "${ips}" = 17; \
    test "$(( ips + ipk ))" = "${ipt}"; \
    test "${ipt}" = 24; \
    # AND THE FIGURES THE SEED WOULD HAVE ASKED FOR ARE CARRIED BESIDE THE \
    # RE-CUT ONES. 21 conv buckets, 24 conv sites, 31 sites in all, of which \
    # this rung asks about 14 and 17 and 24. THOSE ARE THE SEED'S AND NOT STEP \
    # 09's, which is what the `was` in the key names means: `was` is the tree \
    # at the start of THIS rung, where the seven ladders still stand and the \
    # three arrivals are already in place. Step 09 itself froze 18 conv buckets \
    # over 21 sites, 7 skip, 28 in all, and no key carries that. The pair is a \
    # closed loop: 21 - 7 = 14 conv buckets and 24 - 7 = 17 conv sites, and \
    # against step 09's frozen file 18 - 7 + 3 and 21 - 7 + 3 land on the same \
    # two. A staged target.txt that had drifted onto the wrong one of those \
    # would agree with itself and say nothing. \
    test "$(sed -n 's/^target\.b16\.ip\.was\.buckets=//p' ${btgt} | head -1)" = 21; \
    test "$(sed -n 's/^target\.b16\.ip\.was\.sites=//p' ${btgt} | head -1)" = 24; \
    test "$(sed -n 's/^target\.b16\.ip\.was\.total=//p' ${btgt} | head -1)" = 31; \
    test "$(sed -n 's/^ipattern\.size=//p' ${bsrc} | head -1)" = 31; \
    # AND THE SCAN'S OWN BOUND/PLAIN SPLIT IS THE `was` PAIR AND NOT THE RE-CUT \
    # ONE. `ipattern.bound` is 24 -- the seed's convertible sites, the figure \
    # target.b16.ip.was.sites carries -- while ${ips} is 17, what this rung \
    # actually asks for; the 7 between them are the one bound pattern inside \
    # each departing ladder. `ipattern.plain` is the only one of the three that \
    # the re-cut leaves alone, because every departing site was bound, so it \
    # can still be read against ${ipk} directly. \
    test "$(sed -n 's/^ipattern\.bound=//p' ${bsrc} | head -1)" = 24; \
    test "$(( $(sed -n 's/^ipattern\.bound=//p' ${bsrc} | head -1) - ${ips} ))" = 7; \
    test "$(sed -n 's/^ipattern\.plain=//p' ${bsrc} | head -1)" = "${ipk}"; \
    test "$(sed -n 's/^ipattern\.convertible=//p' ${bsrc} | head -1)" = 0; \
    # SEVEN MORE BUCKETS IN THE SEED'S OWN SCAN THAN IN THE RE-CUT FILE, which \
    # is the DEPARTURE half of the re-cut stated against the census rather than \
    # against step 09: `ipattern.methods` is 28 on this tree and the file names \
    # 21, and the seven between them are the ladders C-2 deletes. Not the \
    # arrival half -- that one is 3 and is checked against step 10's \
    # reftest.txt, where it came from, in mk-baseline-step11.sh. \
    test "$(( $(sed -n 's/^ipattern\.methods=//p' ${bsrc} | head -1) \
             - ${ipb} - ${ipk} ))" = 7; \
    if grep -vE '^(conv|skip) [a-z][^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* [1-9][0-9]*$' \
        /tmp/s-ip.txt; then \
        echo "baseline/ipattern.txt: a line is not <conv|skip> <bucket> <count>" >&2; exit 1; \
    fi; \
    sed -n 's/^ipattern\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; c = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 3) == "in=")   m = substr($i, 4); \
                  if (substr($i, 1, 5) == "kind=") c = substr($i, 6) } \
               if (m != "") print (c == "bound" ? "conv" : "skip"), f[1] "#" m }' \
      | LC_ALL=C sort | uniq -c \
      | awk '{ print $2, $3, $1 }' | LC_ALL=C sort > /tmp/d-ip-all.txt; \
    # ONLY THE BUCKETS THE FILE NAMES ARE COMPARED, and that is the departure \
    # the re-cut buys: the seven buckets whose methods C-2 deletes are still in \
    # the scan and must NOT be in the file. Comparing the two whole would fail \
    # on a correctly migrated tree. \
    LC_ALL=C sort /tmp/s-ip.txt > /tmp/s-ip-sorted.txt; \
    awk 'FILENAME == ARGV[1] { w[$2] = 1; next } { if ($2 in w) print }' \
        /tmp/s-ip.txt /tmp/d-ip-all.txt > /tmp/d-ip.txt; \
    if ! cmp -s /tmp/d-ip.txt /tmp/s-ip-sorted.txt; then \
        echo "baseline/ipattern.txt is not source.txt's ipattern records re-bucketed:" >&2; \
        diff /tmp/s-ip-sorted.txt /tmp/d-ip.txt >&2 || true; exit 1; \
    fi; \
    test "$(( $(grep -c . /tmp/d-ip-all.txt) - $(grep -c . /tmp/d-ip.txt) ))" = 7; \
    # NO BUCKET IS ON BOTH LISTS, which the diff above already implies and this \
    # states, because it is the property C-2's two halves depend on: a method \
    # that holds a convertible site AND one with no cast after it would be \
    # asked to convert and to leave alone in the same breath. The scan says \
    # there is no such method on this tree. \
    test "$(awk '{ print $2 }' /tmp/s-ip.txt | LC_ALL=C sort | uniq -d | grep -c .)" = 0; \
    # --- strnl.txt: B-16.3's population --------------------------------------- \
    # Three buckets and 23 literals when step 09 cut the file; 7 literals in \
    # two buckets on the tree this rung is handed. The move bucket is GONE from \
    # the scan -- ServletTestRunner#doGet carries no newline-bearing literal at \
    # all now, because the document it used to build by concatenation is a text \
    # block -- and that ABSENCE is what the re-derivation checks here. The hold \
    # buckets are the other half: they are in the other project, the rung below \
    # never asked for them, and they must still carry their 3 and their 4. \
    grep -v '^#' /tests/baseline/strnl.txt | grep . > /tmp/s-tb.txt; \
    test "$(grep -c . /tmp/s-tb.txt)" = 3; \
    test "$(awk '$1 == "move"' /tmp/s-tb.txt | grep -c .)" = "${tbmb}"; \
    test "$(awk '$1 == "hold"' /tmp/s-tb.txt | grep -c .)" = "${tbhb}"; \
    test "${tbmb}" = 1; \
    test "${tbhb}" = 2; \
    test "$(awk '$1 == "move" { n += $3 } END { print n + 0 }' /tmp/s-tb.txt)" = "${tbms}"; \
    test "$(awk '$1 == "hold" { n += $3 } END { print n + 0 }' /tmp/s-tb.txt)" = "${tbhs}"; \
    test "${tbms}" = 16; \
    test "${tbhs}" = 7; \
    test "${tbhs}" = "$(sed -n 's/^strnl\.size=//p' ${bsrc} | head -1)"; \
    test "${tbhb}" = "$(sed -n 's/^strnl\.methods=//p' ${bsrc} | head -1)"; \
    test "$(( tbms + tbhs ))" = 23; \
    if grep -vE '^(move|hold) [a-z][^ ]*\.java#[A-Za-z_$<][A-Za-z0-9_$>]* [1-9][0-9]*$' \
        /tmp/s-tb.txt; then \
        echo "baseline/strnl.txt: a line is not <move|hold> <bucket> <count>" >&2; exit 1; \
    fi; \
    sed -n 's/^strnl\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort | uniq -c | awk '{ print $2, $1 }' | LC_ALL=C sort > /tmp/d-tb.txt; \
    awk '$1 == "hold" { print $2, $3 }' /tmp/s-tb.txt | LC_ALL=C sort > /tmp/s-tb-keys.txt; \
    if ! cmp -s /tmp/d-tb.txt /tmp/s-tb-keys.txt; then \
        echo "baseline/strnl.txt's hold buckets are not source.txt's strnl records re-bucketed:" >&2; \
        diff /tmp/s-tb-keys.txt /tmp/d-tb.txt >&2 || true; exit 1; \
    fi; \
    awk '$1 == "move" { print $2 }' /tmp/s-tb.txt | LC_ALL=C sort > /tmp/s-tb-move.txt; \
    while read -r bk; do \
        if awk -v k="${bk}" '$1 == k' /tmp/d-tb.txt | grep -q .; then \
            echo "baseline/strnl.txt's move bucket still holds newline literals on this rung's seed: ${bk}" >&2; \
            exit 1; \
        fi; \
    done < /tmp/s-tb-move.txt; \
    # THE ONE THAT MOVED IS THE ONE THE RECORDINGS COVER, and the two that hold \
    # are in the other project. B-16.3 multiplies its structural halves by the \
    # fraction of tbids.txt that still matches, and those five recordings drive \
    # ServletTestRunner; if the move bucket were in a file no recording \
    # reaches, the behaviour scaling would be a factor of 1 by construction and \
    # the leg would be structure-only without saying so. \
    grep -qE '^move protocols/servlet/src/main/java/.*/ServletTestRunner\.java#doGet 16$' /tmp/s-tb.txt; \
    test "$(awk '$1 == "hold" { print $2 }' /tmp/s-tb.txt \
              | sed 's/#.*//' | LC_ALL=C sort -u | grep -c .)" = 1; \
    grep -qE '^hold impl-base/src/main/java/.*/ToolingDeploymentFormatter\.java#' /tmp/s-tb.txt; \
    # --- tbids.txt: B-16.3's behaviour scaling -------------------------------- \
    test "$(grep -c '^[a-z]' /tests/baseline/tbids.txt)" = "${tbid}"; \
    test "${tbid}" = 5; \
    grep '^[a-z]' /tests/baseline/tbids.txt | LC_ALL=C sort > /tmp/s-tbids.txt; \
    test "$(grep -c . /tmp/s-tbids.txt)" = "$(LC_ALL=C sort -u /tmp/s-tbids.txt | grep -c .)"; \
    while read -r id; do \
        if ! grep -qxF "${id}" /tests/assets/ids.txt; then \
            echo "baseline/tbids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; \
            exit 1; \
        fi; \
        f=$(echo "${id}" | tr / .); \
        if [ ! -f "/tests/assets/rec/jdk25-step11/${f}.txt" ]; then \
            echo "baseline/tbids.txt names a recording with no transcript: ${id}" >&2; \
            exit 1; \
        fi; \
    done < /tmp/s-tbids.txt; \
    # ...and they are all in the ONE project whose output the text block is. \
    test "$(grep -c '^webrunner/' /tmp/s-tbids.txt)" = "${tbid}"; \
    # THE FIVE ID LISTS ARE DISJOINT, which is the property that lets five legs \
    # read five scalings out of one behavior.summary without any of them \
    # standing in for another. httpids 18, svcids 69, tbids 5, nioids 4 and \
    # this rung's swids 5: no id appears on two of them. The newest list is the \
    # one most likely to collide -- three of its five ids are era additions and \
    # a recording added for one leg would score for another, and the other two \
    # are drawn from the inherited 935, where every other list drew from too. \
    # tbids and swids being the same length now is the pair to read twice. \
    cat /tests/baseline/httpids.txt /tests/baseline/svcids.txt \
        /tests/baseline/tbids.txt  /tests/baseline/nioids.txt \
        /tests/baseline/swids.txt \
      | grep '^[a-z]' | LC_ALL=C sort > /tmp/d-allids.txt; \
    test "$(grep -c . /tmp/d-allids.txt)" = 101; \
    test "$(LC_ALL=C sort -u /tmp/d-allids.txt | grep -c .)" = 101; \
    # --- sealed.txt: B-16.4's population -------------------------------------- \
    # 51 lines in three kinds: 11 roots that must be closed, 29 leaves that \
    # must be named in a permits clause, and 11 open types that must NOT be \
    # closed. The open eleven are the control, and they are the half this rung \
    # would get wrong by being thorough: the tree's own test doubles and two \
    # functional interfaces are extended by code outside their file, so sealing \
    # them does not compile. \
    grep -v '^#' /tests/baseline/sealed.txt | grep . > /tmp/s-sl.txt; \
    test "$(grep -c . /tmp/s-sl.txt)" = 51; \
    test "$(awk '$1 == "root"' /tmp/s-sl.txt | grep -c .)" = "${slr}"; \
    test "$(awk '$1 == "leaf"' /tmp/s-sl.txt | grep -c .)" = 29; \
    test "$(awk '$1 == "open"' /tmp/s-sl.txt | grep -c .)" = "${slo}"; \
    test "${slr}" = 11; \
    test "${slo}" = 11; \
    test "$(( slr + 29 ))" = "${slt}"; \
    test "${slt}" = 40; \
    awk '$1 == "root" { n = split($5, a, ";"); t += n } END { print t + 0 }' /tmp/s-sl.txt \
      > /tmp/d-permn.txt; \
    test "$(cat /tmp/d-permn.txt)" = "${slp}"; \
    test "${slp}" = 35; \
    rm -f /tmp/d-permn.txt; \
    if grep -E '^root ' /tmp/s-sl.txt | grep -vE '^root [a-z][^ ]*\.java [A-Z][A-Za-z0-9_]* (class|interface) [A-Z][A-Za-z0-9_;]*$'; then \
        echo "baseline/sealed.txt: a root line is not <file> <name> <class|interface> <permits;...>" >&2; exit 1; \
    fi; \
    if grep -E '^open ' /tmp/s-sl.txt | grep -vE '^open [a-z][^ ]*\.java [A-Z][A-Za-z0-9_]*$'; then \
        echo "baseline/sealed.txt: an open line is not <file> <name>" >&2; exit 1; \
    fi; \
    if grep -E '^leaf ' /tmp/s-sl.txt | grep -vE '^leaf [A-Z][A-Za-z0-9_]*$'; then \
        echo "baseline/sealed.txt: a leaf line is not a bare simple name" >&2; exit 1; \
    fi; \
    # EVERY PERMITTED NAME IS A TYPE THE FILE KNOWS, and the five that nothing \
    # permits are the five tops of the five hierarchies. 29 leaves plus the 6 \
    # roots that are themselves subtypes of another root make the 35 permitted \
    # names; the remaining 5 roots -- ContainerControlEvent, \
    # ContainerMultiControlEvent, ContainerEvent, EnrichmentEvent, SuiteEvent \
    # -- are permitted by nobody because nothing is above them. A permits \
    # clause naming a type this file does not carry would be a name the \
    # candidate cannot satisfy and the gate would charge for forever. \
    awk '$1 == "root" { split($5, a, ";"); for (i in a) print a[i] }' /tmp/s-sl.txt \
      | LC_ALL=C sort -u > /tmp/d-perm.txt; \
    awk '$1 == "leaf" { print $2 } $1 == "root" { print $3 }' /tmp/s-sl.txt \
      | LC_ALL=C sort -u > /tmp/d-types.txt; \
    test "$(grep -c . /tmp/d-perm.txt)"  = "${slp}"; \
    test "$(grep -c . /tmp/d-types.txt)" = "${slt}"; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-perm.txt /tmp/d-types.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sealed.txt: a permits clause names a type the file does not carry:" >&2; \
        LC_ALL=C comm -23 /tmp/d-perm.txt /tmp/d-types.txt >&2; exit 1; \
    fi; \
    awk '$1 == "leaf" { print $2 }' /tmp/s-sl.txt | LC_ALL=C sort -u > /tmp/d-leaf.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-leaf.txt /tmp/d-perm.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sealed.txt: a leaf is in no permits clause and could never be reached:" >&2; \
        LC_ALL=C comm -23 /tmp/d-leaf.txt /tmp/d-perm.txt >&2; exit 1; \
    fi; \
    awk '$1 == "root" { print $3 }' /tmp/s-sl.txt | LC_ALL=C sort -u > /tmp/d-root.txt; \
    LC_ALL=C comm -23 /tmp/d-root.txt /tmp/d-perm.txt > /tmp/d-tops.txt; \
    test "$(grep -c . /tmp/d-tops.txt)" = 5; \
    for t in ContainerControlEvent ContainerEvent ContainerMultiControlEvent \
             EnrichmentEvent SuiteEvent; do \
        grep -qxF "${t}" /tmp/d-tops.txt \
          || { echo "baseline/sealed.txt: ${t} is not a hierarchy top" >&2; exit 1; }; \
    done; \
    # ...and every one of the 40 is a class this tree actually compiles, by \
    # simple name, out of classnames.txt. sealed.txt is the one baseline on \
    # this rung cut from `javap -p` rather than from the source scan -- NOTES \
    # 14.3 records a regex sweep of the sources giving 39 roots against the \
    # dump's 22, because it collapsed duplicate simple names -- so tying it \
    # back to a source-derived file is the cross-instrument check. \
    awk '{ n = split($0, a, "."); print a[n] }' /tests/baseline/classnames.txt \
      | LC_ALL=C sort -u > /tmp/d-simplenames.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-types.txt /tmp/d-simplenames.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sealed.txt names a type this tree does not compile:" >&2; \
        LC_ALL=C comm -23 /tmp/d-types.txt /tmp/d-simplenames.txt >&2; exit 1; \
    fi; \
    # ROOTS AND OPEN TYPES ARE DISJOINT, and the open eleven are checked the \
    # same way. An overlap would ask for a type to be sealed and left open. \
    # \
    # THE OPEN HALF IS KEYED BY FILE AND NAME AND THE CLOSED HALF BY NAME, and \
    # that asymmetry is a measurement rather than a style. `Service` is TWO \
    # types on this tree -- one nested in impl-base's \
    # DynamicServiceLoaderTestCase and one top-level in the CDI enricher's \
    # beans package -- so the eleven open types carry only ten distinct simple \
    # names, and a list deduplicated on the name would quietly become ten. \
    # This is the same collapse NOTES 14.3 records the source sweep making, \
    # and it is the reason the closed half can be named-keyed at all: its 40 \
    # names were checked distinct above. \
    awk '$1 == "open" { print $2, $3 }' /tmp/s-sl.txt | LC_ALL=C sort -u > /tmp/d-open.txt; \
    test "$(grep -c . /tmp/d-open.txt)" = "${slo}"; \
    awk '$1 == "open" { print $3 }' /tmp/s-sl.txt | LC_ALL=C sort -u > /tmp/d-opennm.txt; \
    test "$(grep -c . /tmp/d-opennm.txt)" = 10; \
    test "$(awk '$1 == "open" { print $3 }' /tmp/s-sl.txt \
              | LC_ALL=C sort | uniq -d)" = Service; \
    test "$(awk '$1 == "open" && $3 == "Service" { print $2 }' /tmp/s-sl.txt \
              | LC_ALL=C sort -u | grep -c .)" = 2; \
    test "$(LC_ALL=C comm -12 /tmp/d-opennm.txt /tmp/d-types.txt | grep -c .)" = 0; \
    # ...and the open half is tied back to the tree through its FILE and not \
    # its name, for the same reason. classnames.txt holds the 322 top-level \
    # compilation units and no nested type at all, so three of the open eleven \
    # -- ResultCallback, Service and Service2 -- are not in it under their own \
    # names and never will be. What is checked is that each open line's file \
    # is a compilation unit this tree compiles, which covers the nested three \
    # through their enclosing class and is the same relation the closed 40 get \
    # by name. \
    awk '$1 == "open" { print $2 }' /tmp/s-sl.txt | LC_ALL=C sort -u \
      | fqcn | LC_ALL=C sort -u > /tmp/d-openfq.txt; \
    LC_ALL=C sort -u /tests/baseline/classnames.txt > /tmp/d-cn.txt; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-openfq.txt /tmp/d-cn.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sealed.txt's open half names a file this tree does not compile:" >&2; \
        LC_ALL=C comm -23 /tmp/d-openfq.txt /tmp/d-cn.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/d-openfq.txt)" = 10; \
    # ...and the closed roots the same way, which also proves the 11 root files \
    # are 11 distinct compilation units rather than one file named twice. \
    awk '$1 == "root" { print $2 }' /tmp/s-sl.txt | LC_ALL=C sort -u \
      | fqcn | LC_ALL=C sort -u > /tmp/d-rootfq.txt; \
    test "$(grep -c . /tmp/d-rootfq.txt)" = "${slr}"; \
    if [ "$(LC_ALL=C comm -23 /tmp/d-rootfq.txt /tmp/d-cn.txt | grep -c .)" != 0 ]; then \
        echo "baseline/sealed.txt names a root file this tree does not compile:" >&2; \
        LC_ALL=C comm -23 /tmp/d-rootfq.txt /tmp/d-cn.txt >&2; exit 1; \
    fi; \
    # EVERY ROOT FILE EXISTS AND DECLARES ITS OWN NAME; an open type need not, \
    # because three of the eleven are nested -- ResultCallback inside \
    # ContainerDeploymentContextHandler, Service and Service2 inside \
    # DynamicServiceLoaderTestCase -- and a nested type is exactly the kind \
    # whose enclosing file has a different name. \
    awk '$1 == "root" { n = split($2, a, "/"); \
                        if (a[n] != $3 ".java") print $2, $3 }' /tmp/s-sl.txt > /tmp/d-rootbad.txt; \
    if [ "$(grep -c . /tmp/d-rootbad.txt)" != 0 ]; then \
        echo "baseline/sealed.txt: a root file does not declare the root:" >&2; \
        cat /tmp/d-rootbad.txt >&2; exit 1; \
    fi; \
    test "$(awk '$1 == "open" { n = split($2, a, "/"); \
                                if (a[n] != $3 ".java") print }' /tmp/s-sl.txt | grep -c .)" = 3; \
    rm -f /tmp/d-rootbad.txt; \
    # --- apinorm.txt: the 40 names B-6 must not charge for -------------------- \
    # EXACTLY THE 40 TYPES sealed.txt carries, and that identity is the whole \
    # reason the file exists. A published class that becomes `final` changes \
    # its javap header and therefore its B-6 lines; `sealed`, `non-sealed` and \
    # `permits` do not. apinorm.txt is the normalisation list that keeps B-6 \
    # from charging for the modifier anyway, and if it held one name more or \
    # fewer than the hierarchy it would either mask a real API change or \
    # charge for one this rung required. \
    grep -v '^#' /tests/baseline/apinorm.txt | grep . | LC_ALL=C sort > /tmp/d-apinorm.txt; \
    test "$(grep -c . /tmp/d-apinorm.txt)" = "${slt}"; \
    test "$(LC_ALL=C sort -u /tmp/d-apinorm.txt | grep -c .)" = "${slt}"; \
    if ! cmp -s /tmp/d-apinorm.txt /tmp/d-types.txt; then \
        echo "baseline/apinorm.txt is not sealed.txt's 40 types:" >&2; \
        diff /tmp/d-types.txt /tmp/d-apinorm.txt >&2 || true; exit 1; \
    fi; \
    if grep -vE '^[A-Z][A-Za-z0-9_]*$' /tmp/d-apinorm.txt; then \
        echo "baseline/apinorm.txt: a line is not a bare simple name" >&2; exit 1; \
    fi; \
    grep -qF 'baseline/apinorm.txt' /tests/test.sh; \
    # --- swarrow.txt: B-16.5's population -------------------------------------- \
    # Two files, one switch each, and the file is by FILE because B-16.5 asks \
    # two things per file -- enough arrows AND no colon left -- which a \
    # per-site list could not express. Both were colon-form when step 09 cut \
    # the file and both are ARROW-form on the tree this rung is handed, which \
    # is the switch.colon=0 asserted further up. \
    # \
    # THE FILE IS STEP 10'S BYTE FOR BYTE AND THE SCAN AROUND IT IS NOT, which \
    # is the mirror image of the ipattern case above. There the population moved \
    # and the file had to be re-cut; here the population is exactly the same two \
    # switches -- nothing this rung does goes near either -- and it is the TREE \
    # around them that grew. FOUR MORE arrow switches stand on this seed than on \
    # step 09's, and they are step 10's own C-3: the four pattern switches it \
    # wrote in place of instanceof-else dispatch chains, arrow-form because a \
    # pattern switch has no other form. swpat names all four at the same file \
    # and line the switch scan does, which is how the identity is checked below \
    # rather than asserted. \
    # \
    # SO switch.size IS 6 AGAINST A FILE OF 2 AND THE TWO ARE ASSERTED APART. \
    # Step 10 could read them as equal and this rung cannot, and the difference \
    # is not slack: it is pinned at 4, and the 4 are pinned to swpat's files. A \
    # tree that turned one of these two switches back to colon form would still \
    # leave switch.size at 6, so the equality would not have caught it either -- \
    # what catches it is the per-file comparison, which is now restricted to the \
    # two buckets the file names for the same reason ipattern's is. \
    grep -v '^#' /tests/baseline/swarrow.txt | grep . > /tmp/s-sw.txt; \
    test "$(grep -c . /tmp/s-sw.txt)" = "${swf}"; \
    test "${swf}" = 2; \
    test "$(awk '{ n += $2 } END { print n + 0 }' /tmp/s-sw.txt)" = "${swt}"; \
    test "${swt}" = 2; \
    test "$(sed -n 's/^switch\.size=//p'  ${bsrc} | head -1)" = 6; \
    test "$(sed -n 's/^switch\.files=//p' ${bsrc} | head -1)" = 6; \
    test "$(( $(sed -n 's/^switch\.size=//p'  ${bsrc} | head -1) - ${swt} ))" = 4; \
    test "$(( $(sed -n 's/^switch\.files=//p' ${bsrc} | head -1) - ${swf} ))" = 4; \
    test "$(sed -n 's/^swpat\.size=//p' ${bsrc} | head -1)" = 4; \
    if grep -vE '^[a-z][^ ]*\.java [1-9][0-9]*$' /tmp/s-sw.txt; then \
        echo "baseline/swarrow.txt: a line is not <file> <count>" >&2; exit 1; \
    fi; \
    sed -n 's/^switch\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); print f[1] }' \
      | LC_ALL=C sort | uniq -c | awk '{ print $2, $1 }' | LC_ALL=C sort > /tmp/d-sw-all.txt; \
    LC_ALL=C sort /tmp/s-sw.txt > /tmp/s-sw-sorted.txt; \
    awk 'FILENAME == ARGV[1] { w[$1] = 1; next } { if ($1 in w) print }' \
        /tmp/s-sw.txt /tmp/d-sw-all.txt > /tmp/d-sw.txt; \
    if ! cmp -s /tmp/d-sw.txt /tmp/s-sw-sorted.txt; then \
        echo "baseline/swarrow.txt is not source.txt's switch records by file:" >&2; \
        diff /tmp/s-sw-sorted.txt /tmp/d-sw.txt >&2 || true; exit 1; \
    fi; \
    # AND THE FOUR THE FILE DOES NOT NAME ARE EXACTLY THE FOUR swpat NAMES, \
    # which is what turns "switch.size is 6" from an excuse into a statement. \
    # If step 10's blind agent had written a fifth arrow switch somewhere else, \
    # or if one of its four had been unwound between the rungs, the remainder \
    # and swpat's file list would part company here. \
    LC_ALL=C comm -23 /tmp/d-sw-all.txt /tmp/d-sw.txt | awk '{ print $1 }' > /tmp/d-swrest.txt; \
    test "$(grep -c . /tmp/d-swrest.txt)" = 4; \
    sed -n 's/^swpat\[[0-9]*\]=//p' ${bsrc} | awk '{ split($1, f, ":"); print f[1] }' \
      | LC_ALL=C sort -u > /tmp/d-swpatf.txt; \
    if ! cmp -s /tmp/d-swrest.txt /tmp/d-swpatf.txt; then \
        echo "the arrow switches swarrow.txt does not name are not step 10's pattern switches:" >&2; \
        diff /tmp/d-swpatf.txt /tmp/d-swrest.txt >&2 || true; exit 1; \
    fi; \
    # ONE OF THE TWO IS A TEST SOURCE, and that is deliberate rather than an \
    # oversight to be tidied away: C-5 is a gate on whether the candidate \
    # noticed the form exists, and a tree that converted only what it shipped \
    # would leave the test one behind. Both halves are asserted so that a later \
    # edit narrowing the file to src/main is visible here. \
    test "$(grep -c '/src/main/java/' /tmp/s-sw.txt)" = 1; \
    test "$(grep -c '/src/test/java/' /tmp/s-sw.txt)" = 1; \
    # ======================================================================== \
    # B-17'S FOUR POPULATIONS, STAGED BYTE FOR BYTE FROM STEP 10. \
    # ======================================================================== \
    # Step 10 cut reftest.txt, chain.txt, seqfirst.txt and swids.txt out of the \
    # tree it was handed, and this rung stages all four unchanged. FOUR OTHER \
    # RUNG-BELOW POPULATION FILES HAD TO BE RE-CUT BEFORE THEM AND THIS TRIO \
    # DID NOT, and the reason is worth stating because the test is the same one \
    # every rung above the second applies: does the inheriting rung's own work \
    # remove or move a site the file names? Step 06's accessible.txt failed it \
    # here -- this rung's C-4 converts four `setAccessible` calls away -- and so \
    # did step 09's ipattern.txt, seven of whose buckets leave with the \
    # SecurityActions ladders C-2 deletes. These four pass it, and the pass is \
    # a MEASUREMENT rather than a reading: mk-baseline-step11.sh runs step 10's \
    # files against this rung's golden and finds every one of B-17's nine \
    # halves reading full there -- 14 hold sites with 0 reflective tests still \
    # in the 7 conv buckets, 10 pattern-switch arms with 0 chain types still \
    # spelled one at a time, 9 hold sites with 7 sequenced reads beside them. \
    # Step 10's populations name no bucket in any file this rung touches. \
    # \
    # --- B-17's four staged populations, read in the ARRIVED state ----------- \
    # reftest.txt, chain.txt, seqfirst.txt and swids.txt were cut by step 10 \
    # from the tree step 10 was handed. This rung stages all four byte for byte \
    # and READS THEM AGAINST A TREE THAT HAS ALREADY DONE THAT WORK, which is a \
    # different question and the one place a copied block would be silently \
    # wrong. A conv bucket in step 10's file named a method that HAD a \
    # reflective type test and must have a pattern instead; here that method \
    # has no reflective type test left at all, so `derived from source.txt` \
    # cannot mean `equal to source.txt's records` the way it did one rung down. \
    # It means the complement: the conv buckets are gone from the scan and the \
    # hold buckets are still in it, one row for one row. \
    # \
    # THE TALLY KEYS ARE THE SEVENTEEN target.b17.* AND THEY STAGE UNREAD BY \
    # test.sh. Thirteen of them are counted out of these four files here. THE \
    # OTHER FOUR SPLIT TWO AND TWO, and the split is the thing a copied block \
    # would get wrong. ch.ids is step 10's own figure carried forward -- five \
    # recordings, and the ids are step 10's. rt.scanned, sq.scanned and sq.new \
    # are NOT: mk-baseline-step11.sh re-measures all three on THIS rung's seed, \
    # so they read 22, 19 and 7 where step 10 published 35, 26 and 0. They are \
    # the `.was` half of a gate pair, and a `.was` carrying step 10's starting \
    # figures would tell a reader of a step 11 run that 13 reference \
    # comparisons and 7 first-element reads departed during THIS rung. They \
    # departed during step 10; this rung only holds the result. NOTES 16.20. \
    btgt17=/tests/baseline/target.txt; \
    k17() { sed -n "s/^target\.b17\.$1=//p" ${btgt17} | head -1; }; \
    # THE SHAPE IS CHECKED BEFORE THE COLUMNS ARE TRUSTED, and that is not \
    # ceremony: the verdict is the SECOND field of these three files and the \
    # bucket the first, so an awk that reached for the wrong one would count \
    # zero `conv` rows and say so with a number rather than an error. The regex \
    # pins both columns, and the projection below puts the verdict first so \
    # every reader after it is a plain `$1 == "conv"`. \
    if grep -v '^#' /tests/baseline/reftest.txt | grep . \
       | grep -vE '^[a-z][^ ]*\.java#[A-Za-z_$][A-Za-z0-9_$]* (conv|hold) [a-z]+ sel=[^ ]+ type=[^ ]+  # .+$'; then \
        echo "baseline/reftest.txt: a line is not <bucket> <conv|hold> <kind> sel= type=  # why" >&2; exit 1; \
    fi; \
    awk '$0 !~ /^#/ && $0 != "" { print $2, $1 }' /tests/baseline/reftest.txt \
      | LC_ALL=C sort > /tmp/s-rt.txt; \
    test "$(grep -c . /tmp/s-rt.txt)" = "$(k17 rt.total)"; \
    test "$(k17 rt.total)" = 27; \
    test "$(awk '$1 == "conv"' /tmp/s-rt.txt | grep -c .)" = "$(k17 rt.conv.sites)"; \
    test "$(awk '$1 == "hold"' /tmp/s-rt.txt | grep -c .)" = "$(k17 rt.hold.sites)"; \
    test "$(awk '$1 == "conv" { print $2 }' /tmp/s-rt.txt | LC_ALL=C sort -u \
              | grep -c .)" = "$(k17 rt.conv.buckets)"; \
    test "$(awk '$1 == "hold" { print $2 }' /tmp/s-rt.txt | LC_ALL=C sort -u \
              | grep -c .)" = "$(k17 rt.hold.buckets)"; \
    test "$(k17 rt.conv.sites)" = 13; \
    test "$(k17 rt.hold.sites)" = 14; \
    test "$(k17 rt.conv.buckets)" = 7; \
    test "$(k17 rt.hold.buckets)" = 11; \
    # NO METHOD IS IN BOTH HALVES, which is what lets the arrived reading be a \
    # presence test per bucket rather than a count per site. \
    test "$(awk '{ print $2 }' /tmp/s-rt.txt | LC_ALL=C sort -u | grep -c .)" \
       = "$(( $(k17 rt.conv.buckets) + $(k17 rt.hold.buckets) ))"; \
    # THE ARRIVED READING. source.txt's own main-source reftest records, \
    # bucketed the way test.sh buckets them: not one of the 7 conv buckets may \
    # still appear, every one of the 11 hold buckets must, and the main-source \
    # total must be the 14 hold sites exactly -- so a tree that had converted \
    # the holds too, or had put a reflective test back, moves this number. \
    sed -n 's/^reftest\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); if (f[1] !~ /\/src\/main\/java\//) next; \
               m = ""; for (i = 2; i <= NF; i++) \
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort > /tmp/d-rtbk.txt; \
    test "$(grep -c . /tmp/d-rtbk.txt)" = "$(k17 rt.hold.sites)"; \
    LC_ALL=C sort -u /tmp/d-rtbk.txt > /tmp/d-rtbku.txt; \
    awk '$1 == "conv" { print $2 }' /tmp/s-rt.txt | LC_ALL=C sort -u > /tmp/d-rtconv.txt; \
    awk '$1 == "hold" { print $2 }' /tmp/s-rt.txt | LC_ALL=C sort -u > /tmp/d-rthold.txt; \
    test "$(LC_ALL=C comm -12 /tmp/d-rtconv.txt /tmp/d-rtbku.txt | grep -c .)" = 0; \
    test "$(LC_ALL=C comm -23 /tmp/d-rthold.txt /tmp/d-rtbku.txt | grep -c .)" = 0; \
    # The eight the population never named are in test sources, and the scan \
    # still sees them: 22 tree-wide against 14 in main. Rule 6 leaves test \
    # sources alone, so that difference is a constant and not a candidate's \
    # business -- and asserting it here is what would catch a scan that had \
    # quietly started or stopped crossing into src/test. \
    test "$(sed -n 's/^reftest\.size=//p' ${bsrc} | head -1)" \
       = "$(( $(k17 rt.hold.sites) + 8 ))"; \
    # --- chain.txt, ARRIVED: the four that became switches and the two that could not \
    # 6 rows derived by step 10 from reftest.txt's own conv rows. Here the four \
    # conv buckets must be exactly the four swpat records the scan finds, and \
    # the two hold buckets must be exactly two of reftest.txt's hold buckets -- \
    # a chain that stayed a chain is still made of reflective type tests. \
    if grep -v '^#' /tests/baseline/chain.txt | grep . \
       | grep -vE '^[a-z][^ ]*\.java#[A-Za-z_$][A-Za-z0-9_$]* (conv|hold) arms=[1-9][0-9]* sel=[^ ]+ types=[^ ]+$'; then \
        echo "baseline/chain.txt: a line is not <bucket> <conv|hold> arms=N sel= types=" >&2; exit 1; \
    fi; \
    awk '$0 !~ /^#/ && $0 != "" { print $2, $1, substr($3, 6) }' /tests/baseline/chain.txt \
      | LC_ALL=C sort > /tmp/s-ch.txt; \
    test "$(grep -c . /tmp/s-ch.txt)" = "$(( $(k17 ch.conv) + $(k17 ch.hold) ))"; \
    test "$(awk '$1 == "conv"' /tmp/s-ch.txt | grep -c .)" = "$(k17 ch.conv)"; \
    test "$(awk '$1 == "hold"' /tmp/s-ch.txt | grep -c .)" = "$(k17 ch.hold)"; \
    test "$(awk '$1 == "conv" { s += $3 } END { print s + 0 }' /tmp/s-ch.txt)" \
       = "$(k17 ch.arms)"; \
    test "$(k17 ch.conv)" = 4; \
    test "$(k17 ch.hold)" = 2; \
    test "$(k17 ch.arms)" = 10; \
    sed -n 's/^swpat\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; for (i = 2; i <= NF; i++) \
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/d-chbk.txt; \
    awk '$1 == "conv" { print $2 }' /tmp/s-ch.txt | LC_ALL=C sort -u > /tmp/d-chconv.txt; \
    if ! cmp -s /tmp/d-chbk.txt /tmp/d-chconv.txt; then \
        echo "chain.txt's conv buckets are not this tree's pattern switches:" >&2; \
        diff /tmp/d-chbk.txt /tmp/d-chconv.txt >&2 || true; \
        exit 1; \
    fi; \
    awk '$1 == "hold" { print $2 }' /tmp/s-ch.txt | LC_ALL=C sort -u > /tmp/d-chhold.txt; \
    test "$(LC_ALL=C comm -23 /tmp/d-chhold.txt /tmp/d-rthold.txt | grep -c .)" = 0; \
    # ONE `case null` ACROSS THE FOUR, which is a fact about this tree and the \
    # thing B-17.3's arrival half would otherwise let a candidate un-write. \
    test "$(sed -n 's/^swpat\.nullcase=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^swpat\.patterns=//p' ${bsrc} | head -1)" = "$(k17 ch.arms)"; \
    # --- swids.txt, ARRIVED: five recordings that must still exist ------------ \
    # The ids are step 10's, the era is this rung's, and the check is the one \
    # step 10 made: every id is in assets/ids.txt and has a transcript under \
    # rec/jdk25-step11. A dispatch behaviour that stopped being recorded here \
    # would leave B-17.3's execution scaling dividing by five recordings that \
    # are not there. \
    grep -v '^#' /tests/baseline/swids.txt | grep -v '^$' \
      | LC_ALL=C sort -u > /tmp/s-swids.txt; \
    test "$(grep -c . /tmp/s-swids.txt)" = "$(k17 ch.ids)"; \
    test "$(k17 ch.ids)" = 5; \
    while read -r id; do \
        grep -qxF "${id}" /tests/assets/ids.txt \
          || { echo "swids.txt id not in assets/ids.txt: ${id}" >&2; exit 1; }; \
        test -s "/tests/assets/rec/jdk25-step11/$(echo "${id}" | tr / .).txt" \
          || { echo "swids.txt id has no transcript in this rung's era: ${id}" >&2; exit 1; }; \
    done < /tmp/s-swids.txt; \
    # --- seqfirst.txt, ARRIVED: seven gone, nine still there ------------------ \
    # 16 rows: 7 sites in 7 methods that step 10 converted and 9 in 7 that must \
    # not move. The arrived reading is the same complement as reftest's, with \
    # one addition that reftest has no counterpart for: seqnew records the \
    # calls that REPLACED the seven, and they must be in those same seven \
    # buckets. A tree that converted seven first-element reads SOMEWHERE ELSE \
    # would pass the departure half and fail this. \
    if grep -v '^#' /tests/baseline/seqfirst.txt | grep . \
       | grep -vE '^[a-z][^ ]*\.java#[A-Za-z_$][A-Za-z0-9_$]* (conv|hold) [a-z0-9]+ recv=[^ ]+ guard=[^ ]+ expr=[^ ]+  # .+$'; then \
        echo "baseline/seqfirst.txt: a line is not <bucket> <conv|hold> <op> recv= guard= expr=  # why" >&2; exit 1; \
    fi; \
    awk '$0 !~ /^#/ && $0 != "" { print $2, $1 }' /tests/baseline/seqfirst.txt \
      | LC_ALL=C sort > /tmp/s-sq.txt; \
    test "$(grep -c . /tmp/s-sq.txt)" = "$(k17 sq.total)"; \
    test "$(k17 sq.total)" = 16; \
    test "$(awk '$1 == "conv"' /tmp/s-sq.txt | grep -c .)" = "$(k17 sq.conv.sites)"; \
    test "$(awk '$1 == "hold"' /tmp/s-sq.txt | grep -c .)" = "$(k17 sq.hold.sites)"; \
    test "$(awk '$1 == "conv" { print $2 }' /tmp/s-sq.txt | LC_ALL=C sort -u \
              | grep -c .)" = "$(k17 sq.conv.buckets)"; \
    test "$(awk '$1 == "hold" { print $2 }' /tmp/s-sq.txt | LC_ALL=C sort -u \
              | grep -c .)" = "$(k17 sq.hold.buckets)"; \
    test "$(k17 sq.conv.sites)" = 7; \
    test "$(k17 sq.hold.sites)" = 9; \
    test "$(k17 sq.conv.buckets)" = 7; \
    test "$(k17 sq.hold.buckets)" = 7; \
    sed -n 's/^seqfirst\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); if (f[1] !~ /\/src\/main\/java\//) next; \
               m = ""; for (i = 2; i <= NF; i++) \
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort > /tmp/d-sq.txt; \
    test "$(grep -c . /tmp/d-sq.txt)" = "$(k17 sq.hold.sites)"; \
    LC_ALL=C sort -u /tmp/d-sq.txt > /tmp/d-squ.txt; \
    awk '$1 == "conv" { print $2 }' /tmp/s-sq.txt | LC_ALL=C sort -u > /tmp/d-sqconv.txt; \
    awk '$1 == "hold" { print $2 }' /tmp/s-sq.txt | LC_ALL=C sort -u > /tmp/d-sqhold.txt; \
    test "$(LC_ALL=C comm -12 /tmp/d-sqconv.txt /tmp/d-squ.txt | grep -c .)" = 0; \
    test "$(LC_ALL=C comm -23 /tmp/d-sqhold.txt /tmp/d-squ.txt | grep -c .)" = 0; \
    sed -n 's/^seqnew\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); m = ""; for (i = 2; i <= NF; i++) \
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort -u > /tmp/d-sqnew.txt; \
    if ! cmp -s /tmp/d-sqnew.txt /tmp/d-sqconv.txt; then \
        echo "seqnew records are not in seqfirst.txt's conv buckets:" >&2; \
        diff /tmp/d-sqnew.txt /tmp/d-sqconv.txt >&2 || true; \
        exit 1; \
    fi; \
    # AND THE THREE RE-MEASURED KEYS ARE ASSERTED AGAINST THE SCAN THEY WERE \
    # CUT FROM, which is the only way round that proves anything. sq.new was 0 \
    # when step 10 measured it and is 7 here, and the 7 are the sequenced reads \
    # step 10 wrote -- seqnew.size says so independently. sq.scanned falls 26 -> \
    # 19 and rt.scanned 35 -> 22, and both drops are step 10's own conv sites, 7 \
    # and 13, which is what the two differences below state. A target.txt \
    # regenerated against the WRONG tree would still agree with itself; it \
    # cannot also agree with this tree's scan and with step 10's conv counts at \
    # the same time. \
    test "$(k17 sq.new)" = 7; \
    test "$(k17 sq.new)" = "$(sed -n 's/^seqnew\.size=//p' ${bsrc} | head -1)"; \
    test "$(k17 sq.scanned)" = 19; \
    test "$(k17 sq.scanned)" = "$(sed -n 's/^seqfirst\.size=//p' ${bsrc} | head -1)"; \
    test "$(k17 rt.scanned)" = 22; \
    test "$(k17 rt.scanned)" = "$(sed -n 's/^reftest\.size=//p' ${bsrc} | head -1)"; \
    test "$(( 26 - $(k17 sq.scanned) ))" = "$(k17 sq.conv.sites)"; \
    test "$(( 35 - $(k17 rt.scanned) ))" = "$(k17 rt.conv.sites)"; \
    rm -f /tmp/s-rt.txt /tmp/d-rtbk.txt /tmp/d-rtbku.txt \
          /tmp/d-rtconv.txt /tmp/d-rthold.txt \
          /tmp/s-ch.txt /tmp/d-chbk.txt /tmp/d-chconv.txt /tmp/d-chhold.txt \
          /tmp/s-swids.txt \
          /tmp/s-sq.txt /tmp/d-sq.txt /tmp/d-squ.txt /tmp/d-sqnew.txt \
          /tmp/d-sqconv.txt /tmp/d-sqhold.txt; \
    # ======================================================================== \
    # THIS RUNG'S OWN FOUR BASELINES, read by nothing before step 11. \
    # ======================================================================== \
    # Three populations and one id list, and all four get the treatment the \
    # inherited set files get: every one derived from a second source, and \
    # every one tied to the target keys that count it, so that no number in \
    # this image is only a number in one file. Thirty of the sixty-four \
    # staged-and-unread target keys are these four files' tallies -- ten priv, \
    # thirteen catchparam, six varhandle and target.ids -- and this is where \
    # all thirty are spent. \
    # \
    # THE SECOND SOURCE IS source.txt's OWN RECORD ARRAY IN ALL THREE CASES, \
    # so each derivation below is a `cmp` and not a count. Step 10 could \
    # compare its populations line for line because the scan that cuts them \
    # emits the same rows, and that holds here too, with one twist worth \
    # naming: all three files are the MAIN-SOURCE records, and catchparam is \
    # the one place where that makes a visible difference -- the scan found \
    # 104 catch parameters and this rung scores 93. \
    # \
    # AND THE VERDICT COLUMN IS RE-DERIVED AND NOT TAKEN ON TRUST. \
    # varhandle.txt's three-way split is a function of two fields the scan \
    # already carries -- the file and the receiver type -- so the rule is \
    # restated below against those fields and mk-baseline-step11.sh and this \
    # image have to agree about all 14 rows or the build stops. \
    # \
    # --- priv.txt: C-2's departure population and its payload ---------------- \
    # 67 rows in 27 methods across 7 files: 20 CALLS to \
    # AccessController.doPrivileged and 47 TYPE REFERENCES to the four names \
    # that go with them. ALL SEVEN FILES ARE NAMED SecurityActions.java and \
    # that is the shape of the whole leg, so it is MEASURED here rather than \
    # read off the paths -- a file joining this population from anywhere else \
    # would mean the deletion C-2 scores is not a deletion of seven classes. \
    grep -v '^#' /tests/baseline/priv.txt | grep . > /tmp/s-pv.txt; \
    test "$(grep -c . /tmp/s-pv.txt)" = "${pvs}"; \
    test "${pvs}" = 67; \
    test "$(awk '$2 == "form=call"'    /tmp/s-pv.txt | grep -c .)" = "${pvc}"; \
    test "$(awk '$2 == "form=typeref"' /tmp/s-pv.txt | grep -c .)" = "${pvt}"; \
    test "${pvc}" = 20; \
    test "${pvt}" = 47; \
    test "$(( pvc + pvt ))" = "${pvs}"; \
    if grep -vE '^[a-z][^ ]*\.java:[0-9]+ form=(call|typeref) name=[A-Za-z][A-Za-z0-9_]* in=(-|[A-Za-z_$<][A-Za-z0-9_$>]*)$' \
        /tmp/s-pv.txt; then \
        echo "baseline/priv.txt: a line is not <file>:<line> form= name= in=" >&2; \
        exit 1; \
    fi; \
    # EVERY CALL IS A doPrivileged AND EVERY OTHER ROW IS ONE OF FOUR NAMES. \
    # The two halves are not independent and the cmp below says so: the 20 \
    # `form=call` rows and the 20 `name=doPrivileged` rows are the same 20, \
    # which is what makes `calls` a count of the CONSTRUCT and not of a name \
    # that happens to appear in the same file. \
    awk '$2 == "form=call"'         /tmp/s-pv.txt > /tmp/d-pv.txt; \
    awk '$3 == "name=doPrivileged"' /tmp/s-pv.txt > /tmp/d-pvni.txt; \
    if ! cmp -s /tmp/d-pv.txt /tmp/d-pvni.txt; then \
        echo "baseline/priv.txt: the form=call rows are not the doPrivileged rows:" >&2; \
        diff /tmp/d-pv.txt /tmp/d-pvni.txt >&2 || true; exit 1; \
    fi; \
    test "$(awk '{ print $3 }' /tmp/s-pv.txt | LC_ALL=C sort -u | grep -c .)" = 5; \
    test "$(awk '$2 == "form=typeref" { print $3 }' /tmp/s-pv.txt \
              | LC_ALL=C sort -u | grep -c .)" = 4; \
    # PrivilegedActionException AND PrivilegedExceptionAction ARE TWO NAMES AND \
    # BOTH MUST REACH ZERO. Seven rows each, one pair per file; JEP 486 removes \
    # the classes that declare them, so a tree that unwrapped the action but \
    # kept the checked-exception wrapper still has 7 of these standing. \
    test "$(awk '$3 == "name=PrivilegedActionException"' /tmp/s-pv.txt \
              | grep -c .)" = 7; \
    test "$(awk '$3 == "name=PrivilegedExceptionAction"' /tmp/s-pv.txt \
              | grep -c .)" = 7; \
    awk '{ split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-pv.txt | LC_ALL=C sort -u > /tmp/s-pvb.txt; \
    awk '{ split($1, a, ":"); print a[1] }' /tmp/s-pv.txt \
      | LC_ALL=C sort -u > /tmp/s-pvfile.txt; \
    test "$(grep -c . /tmp/s-pvb.txt)"    = "${pvb}"; \
    test "$(grep -c . /tmp/s-pvfile.txt)" = "${pvf}"; \
    test "${pvb}" = 27; \
    test "${pvf}" = 7; \
    test "$(grep -vc 'SecurityActions\.java$' /tmp/s-pvfile.txt)" = 0; \
    # THE SEVEN `in=-` ROWS ARE THE `implements PrivilegedAction<ClassLoader>` \
    # CLAUSE ON THE GetTcclAction ENUM each of the seven files declares -- a \
    # class-level reference with no enclosing method, which is why the scan \
    # wrote a bare dash there. test.sh's pbuckets() keys on the record's own \
    # `in=` field and does not skip them, so they key to `<file>#-`, one per \
    # file, and they are 7 of the 27 buckets. Dropping them would not change \
    # the departure -- a type reference that survives is a type reference that \
    # survives -- but it WOULD change C-3's arrival denominator through the \
    # bucket subtraction below, so the count is frozen here. \
    test "$(awk '$4 == "in=-"' /tmp/s-pv.txt | grep -c .)" = "${pvf}"; \
    test "$(awk '$4 == "in=-" { split($1, a, ":"); print a[1] }' /tmp/s-pv.txt \
              | LC_ALL=C sort -u | grep -c .)" = "${pvf}"; \
    test "$(awk '$4 == "in=-" { print $3 }' /tmp/s-pv.txt \
              | LC_ALL=C sort -u | grep -c .)" = 1; \
    test "$(grep -c '#-$' /tmp/s-pvb.txt)" = "${pvf}"; \
    # ...AND THE ROWS ARE source.txt's OWN RECORDS. There is no test-source \
    # half to subtract here -- nothing under src/test touches the security API \
    # -- so the three source keys and the three file tallies are the same three \
    # numbers, and the cmp is over all 67 rows verbatim. \
    sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); if (f[1] !~ /\/src\/main\/java\//) next; print }' \
      | LC_ALL=C sort > /tmp/d-pv.txt; \
    LC_ALL=C sort /tmp/s-pv.txt > /tmp/d-pvni.txt; \
    if ! cmp -s /tmp/d-pv.txt /tmp/d-pvni.txt; then \
        echo "baseline/priv.txt is not source.txt's main-source priv records:" >&2; \
        diff /tmp/d-pvni.txt /tmp/d-pv.txt >&2 || true; exit 1; \
    fi; \
    test "$(sed -n 's/^priv\.size=//p'    ${bsrc} | head -1)" = "${pvs}"; \
    test "$(sed -n 's/^priv\.methods=//p' ${bsrc} | head -1)" = "${pvb}"; \
    test "$(sed -n 's/^priv\.files=//p'   ${bsrc} | head -1)" = "${pvf}"; \
    # WHAT THE FILE HAS TO GET RIGHT IS THAT `want` IS ZERO. C-2 is a pure \
    # departure: nothing arrives in its place that a scan can see, because the \
    # privileged block's body becomes the method that held it. A `want` that \
    # had drifted above zero would make a correct migration unscorable, so both \
    # want keys are asserted against 0 and not merely against each other. \
    test "${pvw}"  = 0; \
    test "${pvwf}" = 0; \
    # THE PAYLOAD, DERIVED FROM THE SEVEN FILES THIS FILE'S OWN ROWS NAME. \
    # A departure scored alone pays deletion, so C-2 is min()'d against 13 \
    # sites that have to SURVIVE the deletion, and neither half of the 13 is a \
    # typed list: the construction half is every `newinstance form=ctor` record \
    # inside those seven files, and the access half is every `setacc` bucket \
    # accessible.txt carries inside them. \
    # \
    # THE TWO HALVES SIT DIFFERENTLY AGAINST THE DEPARTURE AND THAT ASYMMETRY \
    # IS THE POINT. Not one of the 7 construction buckets is a priv bucket -- \
    # `newInstance` is the method the unwrap ladders fed, never a ladder itself \
    # -- while all 6 access buckets are, because each of those six \
    # setAccessible reads sits in a method that also held a doPrivileged. So a \
    # tree that deletes the ladder and keeps the work reads 7 and 6, and a tree \
    # that deletes the file reads 0 and 0. \
    # \
    # THE CONSTRUCTION HALF IS COUNTED PER FILE AND NOT PER SITE, which is a \
    # deliberate weakening test.sh states in the leg and this block mirrors: \
    # the seven buckets are in seven distinct files, one apiece, so the \
    # per-file key and the per-bucket key are the same seven keys today and the \
    # weakening costs nothing on this tree. \
    fbucketise newinstance form=ctor > /tmp/d-pvctorb.txt; \
    awk 'FILENAME == ARGV[1] { p[$0] = 1; next } \
         { split($0, b, "#"); if (b[1] in p) print }' \
      /tmp/s-pvfile.txt /tmp/d-pvctorb.txt | LC_ALL=C sort -u > /tmp/s-pvctor.txt; \
    test "$(grep -c . /tmp/s-pvctor.txt)" = "${pvhc}"; \
    test "${pvhc}" = 7; \
    test "${pvhc}" = "${pvf}"; \
    test "$(sed 's/#.*//' /tmp/s-pvctor.txt | LC_ALL=C sort -u | grep -c .)" = "${pvf}"; \
    test "$(LC_ALL=C comm -12 /tmp/s-pvctor.txt /tmp/s-pvb.txt | grep -c .)" = 0; \
    setpart /tests/baseline/accessible.txt setacc > /tmp/d-pvni.txt; \
    awk 'FILENAME == ARGV[1] { p[$0] = 1; next } \
         { split($0, b, "#"); if (b[1] in p) print }' \
      /tmp/s-pvfile.txt /tmp/d-pvni.txt | LC_ALL=C sort -u > /tmp/s-pvacc.txt; \
    test "$(grep -c . /tmp/s-pvacc.txt)" = "${pvha}"; \
    test "${pvha}" = 6; \
    test "$(LC_ALL=C comm -23 /tmp/s-pvacc.txt /tmp/s-pvb.txt | grep -c .)" = 0; \
    test "$(( pvhc + pvha ))" = "${pvh}"; \
    test "${pvh}" = 13; \
    # AND THE SIX ARE varhandle.txt's SIX `verdict=scanner` ROWS, WHICH IS THE \
    # ONE PLACE THIS RUNG'S THREE NEW FILES ALL HAVE TO AGREE. A scanner row is \
    # a setAccessible C-4 will not convert; a priv file is a file C-2 deletes \
    # the ladders out of; and the six that are both are the six sites that must \
    # come through this rung untouched. The two derivations share no input \
    # beyond the tree, and the diff below is the check. \
    awk '$0 !~ /^#/ && $0 != "" { split($1, a, ":"); m = ""; vd = ""; \
             for (i = 2; i <= NF; i++) { \
                if (substr($i, 1, 3) == "in=")      m  = substr($i, 4); \
                if (substr($i, 1, 8) == "verdict=") vd = substr($i, 9) } \
             if (m != "" && vd == "scanner") print a[1] "#" m }' \
      /tests/baseline/varhandle.txt | LC_ALL=C sort -u > /tmp/d-vhscan.txt; \
    if ! cmp -s /tmp/s-pvacc.txt /tmp/d-vhscan.txt; then \
        echo "the setAccessible reads inside priv.txt's files are not varhandle.txt's scanner rows:" >&2; \
        diff /tmp/s-pvacc.txt /tmp/d-vhscan.txt >&2 || true; exit 1; \
    fi; \
    # --- catchparam.txt: C-3's population, all three halves in one file ------ \
    # 93 rows in 68 methods across 43 files: 58 catch parameters whose body \
    # READS the exception and 35 that never mention it again. The 35 are what \
    # `_` replaces and the 58 are what it must not, and THE SAME FILE CARRIES \
    # BOTH because this is the one leg on the ladder whose departure, arrival \
    # and hold all come out of a single census. \
    # \
    # `unnamed` IS ZERO ON THE WAY IN and that zero is the arrival's floor: `_` \
    # is a keyword only from release 22 and the tree is at 21, so a row could \
    # not have been written unnamed even by accident. \
    grep -v '^#' /tests/baseline/catchparam.txt | grep . > /tmp/s-cp.txt; \
    test "$(grep -c . /tmp/s-cp.txt)" = "${cpt}"; \
    test "${cpt}" = 93; \
    test "$(awk '$3 == "kind=read"'    /tmp/s-cp.txt | grep -c .)" = "${cpr}"; \
    test "$(awk '$3 == "kind=unread"'  /tmp/s-cp.txt | grep -c .)" = "${cpx}"; \
    test "$(awk '$3 == "kind=unnamed"' /tmp/s-cp.txt | grep -c .)" = "${cpu}"; \
    test "${cpr}" = 58; \
    test "${cpx}" = 35; \
    test "${cpu}" = 0; \
    test "$(( cpr + cpx + cpu ))" = "${cpt}"; \
    test "${cpsc}" = "${cpt}"; \
    if grep -vE '^[a-z][^ ]*\.java:[0-9]+ name=[A-Za-z_][A-Za-z0-9_]* kind=(read|unread|unnamed) type=[A-Za-z][A-Za-z0-9_.]* in=[A-Za-z_$<][A-Za-z0-9_$>]*$' \
        /tmp/s-cp.txt; then \
        echo "baseline/catchparam.txt: a line is not <file>:<line> name= kind= type= in=" >&2; \
        exit 1; \
    fi; \
    # EVERY ROW HAS AN ENCLOSING METHOD -- a catch clause cannot sit at class \
    # level -- so unlike priv.txt there is no `#-` bucket here. The two \
    # populations keying the same way is what makes the subtraction below a \
    # subtraction and not a coincidence of format. \
    awk '{ split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-cp.txt | LC_ALL=C sort -u > /tmp/s-cpb.txt; \
    test "$(grep -c . /tmp/s-cpb.txt)" = "${cpb}"; \
    test "${cpb}" = 68; \
    test "$(grep -c '#-$' /tmp/s-cpb.txt)" = 0; \
    test "$(awk '{ split($1, a, ":"); print a[1] }' /tmp/s-cp.txt \
              | LC_ALL=C sort -u | grep -c .)" = "${cpf}"; \
    test "${cpf}" = 43; \
    # THE TEN ROWS THIS FILE DOES NOT CARRY ARE THE TEST-SOURCE ONES, and the \
    # ten is computed rather than typed: the all-scope scan found 103 and \
    # catchparam.txt carries the 93 that rule 6 leaves in scope. A census that \
    # had quietly swept the test tree would equalise the two figures and put \
    # C-3's denominator ten rows out. \
    cpall=$(sed -n 's/^catchparam\.size=//p' ${bsrc} | head -1); \
    test "${cpall}" = 103; \
    test "${cpt}" -lt "${cpall}"; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${cpall}"; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
              | grep -vc '/src/main/java/')" = "$(( cpall - cpt ))"; \
    test "$(sed -n 's/^catchparam\.unnamed=//p' ${bsrc} | head -1)" = "${cpu}"; \
    sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); if (f[1] !~ /\/src\/main\/java\//) next; print }' \
      | LC_ALL=C sort > /tmp/d-cp.txt; \
    LC_ALL=C sort /tmp/s-cp.txt > /tmp/d-cpin.txt; \
    if ! cmp -s /tmp/d-cp.txt /tmp/d-cpin.txt; then \
        echo "baseline/catchparam.txt is not source.txt's main-source catchparam records:" >&2; \
        diff /tmp/d-cpin.txt /tmp/d-cp.txt >&2 || true; exit 1; \
    fi; \
    # THE SUBTRACTION C-3's ARRIVAL AND HOLD ARE STATED THROUGH, done here the \
    # way test.sh does it: pbuckets() takes priv.txt's 27 buckets away from \
    # catchparam.txt before counting. THE TWO POPULATIONS OVERLAP BY \
    # CONSTRUCTION. Each of the seven SecurityActions classes catches a \
    # PrivilegedActionException it reads and a ClassCastException it does not, \
    # both inside getConstructor, and C-2 DELETES that method. A candidate who \
    # does this rung's work correctly therefore CANNOT rename those seven \
    # unread parameters to `_`, because they are gone; an arrival denominator \
    # of 35 rather than 28 would cap a complete migration at 0.8. \
    # \
    # SO THE FOURTEEN ROWS THAT LEAVE ARE NAMED AND COUNTED, not assumed: \
    # seven `pae kind=read` and seven `cce kind=unread`, all fourteen \
    # `in=getConstructor`, one pair per file, seven buckets across the same \
    # seven files priv.txt names. \
    awk 'FILENAME == ARGV[1] { p[$0] = 1; next } \
         { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           b = a[1] "#" m; if (!(b in p)) print }' \
      /tmp/s-pvb.txt /tmp/s-cp.txt > /tmp/d-cpout.txt; \
    awk 'FILENAME == ARGV[1] { p[$0] = 1; next } \
         { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           b = a[1] "#" m; if (b in p) print }' \
      /tmp/s-pvb.txt /tmp/s-cp.txt > /tmp/d-cpgone.txt; \
    test "$(grep -c . /tmp/d-cpout.txt)"  = "${cpwt}"; \
    test "$(grep -c . /tmp/d-cpgone.txt)" = "$(( cpt - cpwt ))"; \
    test "$(awk '$3 == "kind=unread"' /tmp/d-cpout.txt | grep -c .)" = "${cpwu}"; \
    test "$(awk '$3 == "kind=read"'   /tmp/d-cpout.txt | grep -c .)" = "${cpwr}"; \
    test "${cpwu}" = 28; \
    test "${cpwr}" = 51; \
    test "${cpwx}" = 0; \
    test "${cpwt}" = 79; \
    test "$(( cpwu + cpwr ))" = "${cpwt}"; \
    test "$(awk '$2 == "name=pae" && $3 == "kind=read"' /tmp/d-cpgone.txt \
              | grep -c .)" = "${pvf}"; \
    test "$(awk '$2 == "name=cce" && $3 == "kind=unread"' /tmp/d-cpgone.txt \
              | grep -c .)" = "${pvf}"; \
    test "$(awk '$4 == "type=PrivilegedActionException"' /tmp/d-cpgone.txt \
              | grep -c .)" = "${pvf}"; \
    test "$(awk '$4 == "type=ClassCastException"' /tmp/d-cpgone.txt \
              | grep -c .)" = "${pvf}"; \
    test "$(awk '$5 != "in=getConstructor"' /tmp/d-cpgone.txt | grep -c .)" = 0; \
    awk '{ split($1, a, ":"); print a[1] }' /tmp/d-cpgone.txt \
      | LC_ALL=C sort -u > /tmp/d-cpin.txt; \
    if ! cmp -s /tmp/d-cpin.txt /tmp/s-pvfile.txt; then \
        echo "the catch parameters priv.txt's buckets remove are not one pair per SecurityActions file:" >&2; \
        diff /tmp/s-pvfile.txt /tmp/d-cpin.txt >&2 || true; exit 1; \
    fi; \
    # AND THE BUCKETS AND FILES THE SUBTRACTION LEAVES. Seven buckets go and \
    # the file count does NOT change: every one of the seven files still has \
    # catch parameters outside getConstructor, so a leg stated over 43 files \
    # before the subtraction is stated over the same 43 after it. \
    awk '{ split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/d-cpout.txt | LC_ALL=C sort -u > /tmp/d-cp.txt; \
    test "$(grep -c . /tmp/d-cp.txt)" = "${cpwb}"; \
    test "${cpwb}" = 61; \
    test "$(awk '{ split($1, a, ":"); m = ""; \
                   for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                   print a[1] "#" m }' /tmp/d-cpgone.txt \
              | LC_ALL=C sort -u | grep -c .)" = "$(( cpb - cpwb ))"; \
    test "$(awk '{ split($1, a, ":"); print a[1] }' /tmp/d-cpout.txt \
              | LC_ALL=C sort -u | grep -c .)" = "${cpwf}"; \
    test "${cpwf}" = "${cpf}"; \
    test "${cpwf}" = 43; \
    # --- varhandle.txt: C-4's arrival, its hold, and the verdict on each ----- \
    # Fourteen rows, one per `setAccessible` site, and THE POPULATION THE LEG \
    # SCORES IS EMPTY ON THE WAY IN: 0 privateLookupIn calls, 0 \
    # unreflectVarHandle calls, 0 methods, 0 files. That measured zero is what \
    # makes C-4 an arrival rather than a threshold, and it is read out of \
    # source.txt's own varhandle keys rather than out of this file, which \
    # carries only the sites the arrival is carved out of. \
    test "${vhs}" = 0; \
    test "${vha}" = 0; \
    test "${vhb}" = 0; \
    test "$(sed -n 's/^varhandle\.size=//p'    ${bsrc} | head -1)" = "${vhs}"; \
    test "$(sed -n 's/^varhandle\.arrival=//p' ${bsrc} | head -1)" = "${vha}"; \
    test "$(sed -n 's/^varhandle\.methods=//p' ${bsrc} | head -1)" = "${vhb}"; \
    test "$(sed -n 's/^varhandle\.files=//p'   ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${vhs}"; \
    grep -v '^#' /tests/baseline/varhandle.txt | grep . > /tmp/s-vh.txt; \
    test "$(grep -c . /tmp/s-vh.txt)" = "${tact}"; \
    if grep -vE '^[a-z][^ ]*\.java:[0-9]+ call=setAccessible recv=(Field|Method|Constructor) in=[A-Za-z_$<][A-Za-z0-9_$>]* verdict=(convert|scanner|behaviour)$' \
        /tmp/s-vh.txt; then \
        echo "baseline/varhandle.txt: a line is not <file>:<line> call= recv= in= verdict=" >&2; \
        exit 1; \
    fi; \
    # THE VERDICT IS A FUNCTION OF TWO FIELDS THE SCAN ALREADY CARRIES, and the \
    # rule is restated rather than trusted: a site in a SecurityActions class \
    # hands a Field or Method OBJECT back to its caller and has no handle to \
    # route, so it is `scanner`; of the rest a Field receiver is a private read \
    # or write and converts, and a Constructor or Method receiver is an \
    # invocation whose exception wrapping four transcripts observe, so it is \
    # `behaviour`. THE FILE TEST COMES FIRST AND THE LINE IS STRIPPED BEFORE \
    # IT, which is the one ordering that matters: four of the six scanner rows \
    # have a Field receiver and would read `convert` the other way round. \
    awk '{ f = $1; sub(/:[0-9]+$/, "", f); \
           recv = "-"; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 5) == "recv=") recv = substr($i, 6); \
           v = (f ~ /SecurityActions\.java$/) ? "scanner" \
                 : (recv == "Field" ? "convert" : "behaviour"); \
           print $1, $2, $3, $4, "verdict=" v }' /tmp/s-vh.txt > /tmp/d-vh.txt; \
    if ! cmp -s /tmp/d-vh.txt /tmp/s-vh.txt; then \
        echo "baseline/varhandle.txt's verdict column is not its own file-and-receiver rule:" >&2; \
        diff /tmp/s-vh.txt /tmp/d-vh.txt >&2 || true; exit 1; \
    fi; \
    test "$(awk '$5 == "verdict=convert"'   /tmp/s-vh.txt | grep -c .)" = "${tacv}"; \
    test "$(awk '$5 == "verdict=scanner"'   /tmp/s-vh.txt | grep -c .)" = "${pvha}"; \
    test "$(awk '$5 == "verdict=behaviour"' /tmp/s-vh.txt | grep -c .)" = 4; \
    test "$(awk '$3 == "recv=Field"'        /tmp/s-vh.txt | grep -c .)" = 8; \
    test "$(awk '$3 == "recv=Method"'       /tmp/s-vh.txt | grep -c .)" = 3; \
    test "$(awk '$3 == "recv=Constructor"'  /tmp/s-vh.txt | grep -c .)" = 3; \
    # THE EIGHT Field SITES SPLIT FOUR AND FOUR, which is the whole reason the \
    # file test runs first and the reason `recv=Field` alone is not the rule. \
    test "$(awk '$3 == "recv=Field" && $5 == "verdict=convert"' /tmp/s-vh.txt \
              | grep -c .)" = "${tacv}"; \
    test "$(awk '$3 == "recv=Field" && $5 == "verdict=scanner"' /tmp/s-vh.txt \
              | grep -c .)" = "${tacv}"; \
    test "$(awk '{ split($1, a, ":"); print a[1] }' /tmp/s-vh.txt \
              | LC_ALL=C sort -u | grep -c .)" = "${tacf}"; \
    # FOURTEEN ROWS, FOURTEEN BUCKETS: no method holds two of these sites, so \
    # the per-bucket min() test.sh takes over the hold half is a per-site test \
    # on this tree and the leg cannot be half-passed inside one method. \
    awk '$5 == "verdict=convert" { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-vh.txt | LC_ALL=C sort -u > /tmp/d-vhconv.txt; \
    awk '$5 != "verdict=convert" { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-vh.txt | LC_ALL=C sort -u > /tmp/d-vhhold.txt; \
    test "$(grep -c . /tmp/d-vhconv.txt)" = "${vhwb}"; \
    test "$(grep -c . /tmp/d-vhhold.txt)" = "${tach}"; \
    test "${vhwb}" = "${tacv}"; \
    test "$(LC_ALL=C comm -12 /tmp/d-vhconv.txt /tmp/d-vhhold.txt | grep -c .)" = 0; \
    test "$(( vhwb + tach ))" = "${tact}"; \
    test "$(sed 's/#.*//' /tmp/d-vhconv.txt | LC_ALL=C sort -u | grep -c .)" = "${vhwf}"; \
    test "${vhwf}" = "${tacv}"; \
    # THE ARRIVAL IS TWO CALLS PER CONVERTED SITE AND THE GATE READS BOTH. \
    # privateLookupIn returns a Lookup and unreflectVarHandle turns the Field \
    # into a VarHandle; neither alone is a conversion, and only the Lookup that \
    # privateLookupIn returns can see a private field, so an unreflectVarHandle \
    # without one cannot be written at all. `want` is therefore twice the \
    # converted count and is asserted as such rather than as the literal 8. \
    test "${vhw}" = "$(( tacv * 2 ))"; \
    test "${vhw}" = 8; \
    # --- vhids.txt: C-4's behaviour scaling ---------------------------------- \
    # Four recordings, and they are the reason the four `behaviour` rows are \
    # not converted. Constructor.newInstance wraps whatever the constructor \
    # throws in InvocationTargetException and reports inaccessibility as \
    # IllegalAccessException; MethodHandle.invoke wraps nothing and a Lookup \
    # reports inaccessibility differently. These four transcripts observe the \
    # old wrapping AS THEIR OUTCOME, so C-4 is multiplied by the fraction of \
    # them that still matches and a tree that converted the invocation sites \
    # loses the leg in proportion rather than failing a gate. \
    grep -v '^#' /tests/baseline/vhids.txt | grep . \
      | LC_ALL=C sort -u > /tmp/s-vhids.txt; \
    test "$(grep -c . /tmp/s-vhids.txt)" = 4; \
    test "$(grep -c . /tmp/s-vhids.txt)" \
       = "$(grep -v '^#' /tests/baseline/vhids.txt | grep -c .)"; \
    while read -r id; do \
        grep -qxF "${id}" /tests/assets/ids.txt \
          || { echo "vhids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; exit 1; }; \
        test -s "/tests/assets/rec/jdk25-step11/$(echo "${id}" | tr / .).txt" \
          || { echo "vhids.txt id has no transcript in this rung's era: ${id}" >&2; exit 1; }; \
    done < /tmp/s-vhids.txt; \
    # AND THE FOUR ARE A CLOSURE AND NOT A SELECTION, with one exclusion that is \
    # itself derived. Five transcripts in this rung's era mention \
    # IllegalAccessException; the fifth is svc/sl-package-private-class, where \
    # `java.util.ServiceLoader$ProviderImpl` INSIDE java.base refuses to reach a \
    # probe provider class -- the JDK's own reflection, not this tree's, which \
    # is why it arrives wrapped in the java.util.ServiceConfigurationError that \
    # says so and the other four do not. So the rule is IllegalAccessException \
    # AND NOT ServiceConfigurationError, and the closure still runs in the \
    # direction that matters: a sixth recording of this tree's own failure \
    # landing later and not being added here would leave C-4's exec term \
    # reading full on a tree that had broken it. \
    grep -rlF 'java.lang.IllegalAccessException' /tests/assets/rec/jdk25-step11 \
      | sed 's|.*/||; s|\.txt$||' | tr . / | LC_ALL=C sort -u > /tmp/d-vhall.txt; \
    grep -rlF 'java.util.ServiceConfigurationError' /tests/assets/rec/jdk25-step11 \
      | sed 's|.*/||; s|\.txt$||' | tr . / | LC_ALL=C sort -u > /tmp/d-vhsce.txt; \
    test "$(grep -c . /tmp/d-vhall.txt)" = 5; \
    LC_ALL=C comm -12 /tmp/d-vhall.txt /tmp/d-vhsce.txt > /tmp/d-vhjdk.txt; \
    LC_ALL=C comm -23 /tmp/d-vhall.txt /tmp/d-vhsce.txt > /tmp/d-vhids.txt; \
    test "$(grep -c . /tmp/d-vhjdk.txt)" = 1; \
    if ! cmp -s /tmp/d-vhids.txt /tmp/s-vhids.txt; then \
        echo "baseline/vhids.txt is not the transcripts where THIS TREE raised an IllegalAccessException:" >&2; \
        diff /tmp/s-vhids.txt /tmp/d-vhids.txt >&2 || true; exit 1; \
    fi; \
    # AND THE EXCLUDED ONE IS B-15.3'S, which is what makes this a partition of \
    # the evidence rather than a hole in it. It still scales a leg; it scales \
    # the leg whose reflection actually failed. \
    grep -v '^#' /tests/baseline/svcids.txt | grep . \
      | LC_ALL=C sort -u > /tmp/d-vhsvc.txt; \
    test "$(LC_ALL=C comm -12 /tmp/d-vhjdk.txt /tmp/d-vhsvc.txt | grep -c .)" = 1; \
    # --- the six behaviour-id lists are pairwise disjoint --------------------- \
    # SIX LEGS SCALE BY A NAMED SET OF RECORDED FIXTURES and all six sets ride \
    # on the one era: B-13's httpids, B-14's nioids, B-15.3's svcids, B-16.3's \
    # tbids, B-17.3's swids and C-4's vhids. An id on two of them is ONE \
    # regression charged to TWO legs, which is a candidate losing 0.46 of \
    # family C for a fault worth 0.12 of it. Fifteen pairs, every one empty, \
    # and the loop is written over the list of files so a seventh list is one \
    # more word rather than five more tests. \
    for f in http nio svc tb sw vh; do \
        grep -v '^#' "/tests/baseline/${f}ids.txt" | grep . \
          | LC_ALL=C sort -u > "/tmp/d-bid-${f}.txt"; \
        test -s "/tmp/d-bid-${f}.txt"; \
    done; \
    bidpairs=0; \
    for a in http nio svc tb sw vh; do \
        for b in http nio svc tb sw vh; do \
            [ "${a}" \< "${b}" ] || continue; \
            bidpairs=$(( bidpairs + 1 )); \
            if LC_ALL=C comm -12 "/tmp/d-bid-${a}.txt" "/tmp/d-bid-${b}.txt" \
                 | grep -q .; then \
                echo "behaviour-id lists ${a} and ${b} share a recording -- one regression would be priced twice:" >&2; \
                LC_ALL=C comm -12 "/tmp/d-bid-${a}.txt" "/tmp/d-bid-${b}.txt" >&2; \
                exit 1; \
            fi; \
        done; \
    done; \
    test "${bidpairs}" = 15; \
    test "$(cat /tmp/d-bid-http.txt /tmp/d-bid-nio.txt /tmp/d-bid-svc.txt \
                /tmp/d-bid-tb.txt /tmp/d-bid-sw.txt /tmp/d-bid-vh.txt \
              | LC_ALL=C sort -u | grep -c .)" = 105; \
    # --- target.ids: the thirty-seven recordings this rung's era re-cuts ----- \
    # Not a population and not a set file: the count of transcripts whose \
    # output changes because of this rung's work, and the one target key that \
    # is about the ERA rather than about the tree. Thirty-six of the \
    # thirty-seven are stack traces through the SecurityActions ladders C-2 \
    # deletes -- a frame named GetTcclAction appears in 36 of step 10's \
    # recordings -- and the thirty-seventh is desc/adv-nesting-over-jaxp-limit. \
    # \
    # THE LIST ITSELF IS ids-step11-moved.txt AND IT IS NOT STAGED, because it \
    # is checked where the era is cut. What this image can see is the other \
    # end of the same fact: the era in /tests/assets is the FINISHED one, so \
    # not a single recording in it still walks through a GetTcclAction frame. \
    test "${tids}" = 37; \
    test "${tids}" -lt "$(grep -c . /tests/assets/ids.txt)"; \
    test "$(grep -rlF 'GetTcclAction' /tests/assets/rec/jdk25-step11 \
              | grep -c .)" = 0; \
    rm -f /tmp/s-pv.txt /tmp/s-pvb.txt /tmp/s-pvfile.txt /tmp/s-pvctor.txt \
          /tmp/s-pvacc.txt /tmp/d-pv.txt /tmp/d-pvni.txt /tmp/d-pvctorb.txt \
          /tmp/d-vhscan.txt \
          /tmp/s-cp.txt /tmp/s-cpb.txt /tmp/d-cp.txt /tmp/d-cpin.txt \
          /tmp/d-cpout.txt /tmp/d-cpgone.txt \
          /tmp/s-vh.txt /tmp/d-vh.txt /tmp/d-vhconv.txt /tmp/d-vhhold.txt \
          /tmp/s-vhids.txt /tmp/d-vhids.txt /tmp/d-vhall.txt /tmp/d-vhsce.txt \
          /tmp/d-vhjdk.txt /tmp/d-vhsvc.txt \
          /tmp/d-bid-http.txt /tmp/d-bid-nio.txt /tmp/d-bid-svc.txt \
          /tmp/d-bid-tb.txt /tmp/d-bid-sw.txt /tmp/d-bid-vh.txt; \
    rm -f /tmp/s-ip.txt /tmp/d-ip.txt /tmp/d-ip-all.txt /tmp/s-ip-sorted.txt \
          /tmp/s-tb.txt /tmp/d-tb.txt /tmp/s-tb-keys.txt /tmp/s-tbids.txt \
          /tmp/d-allids.txt /tmp/s-sl.txt /tmp/d-perm.txt /tmp/d-types.txt \
          /tmp/d-leaf.txt /tmp/d-root.txt /tmp/d-tops.txt /tmp/d-open.txt \
          /tmp/d-opennm.txt /tmp/d-openfq.txt /tmp/d-rootfq.txt /tmp/d-cn.txt \
          /tmp/d-simplenames.txt /tmp/d-apinorm.txt \
          /tmp/s-sw.txt /tmp/d-sw.txt /tmp/s-sw-sorted.txt \
          /tmp/d-sw-all.txt /tmp/d-swrest.txt /tmp/d-swpatf.txt; \
    rm -f /tmp/s-modn.txt /tmp/d-modart.txt /tmp/s-modart.txt /tmp/d-mododd.txt \
          /tmp/s-svc.txt /tmp/s-svcleg.txt /tmp/s-svcarr.txt /tmp/s-svckeep.txt \
          /tmp/d-svcload.txt /tmp/d-svcscan.txt /tmp/d-svcload-b.txt \
          /tmp/d-svcall.txt /tmp/s-svcall.txt /tmp/d-svcplural.txt \
          /tmp/d-svcsingular.txt /tmp/s-svcids.txt /tmp/s-svclegf.txt /tmp/s-svckeepf.txt \
          /tmp/d-varmeth.txt /tmp/d-varres.txt /tmp/d-svclegacy.txt \
          /tmp/d-svcplat.txt; \
    echo "step 11: the source, target, level, priv, catchparam, varhandle, vhids, accessible, ipattern, reftest, chain, seqfirst, swids and twenty-nine inherited baselines agree with each other"

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
# WHAT THIS RUNG ADDS, AND WHY EVERYTHING BELOW THIS PARAGRAPH IS STEP 06'S.
# This narration was written when the layer was, at step 06. Steps 07, 08, 09,
# 10 and this one have each added blocks to fixtures/src-rehearsal.sh -- 71194
# bytes at step 06, 179359 here -- without moving a word of it. The blocks it
# describes are all still there and still asserted, so it is kept; but where it
# says "this rung" it is step 06 speaking, and what holds at THIS rung is:
#
#   THREE NEW RECORD BLOCKS, AND THEY ARE NEW TO THE PLUGIN. priv, catchparam
#   and varhandle are methods that did not exist in the SourceScan jar the rung
#   below built, so until this layer runs nothing in the image has ever driven
#   them over a compiler. The reports under fixtures/c/ carry all three and say
#   whatever they were written to say, which is the whole reason this layer is
#   not optional. Measured on the smoke tree and then frozen: 13 privileged
#   records over 2 files and 5 buckets, two of those buckets being the dash
#   that an `implements` clause outside any method produces; 8 catch clauses in
#   8 buckets reading 1 unnamed, 5 read and 2 unread; and 4 VarHandle arrivals
#   in 2 methods, two records apiece, because privateLookupIn and
#   unreflectVarHandle are separate calls and C-4 counts them separately.
#
#   THE SAME THREE SPELLINGS AS EVER, ONE LEVEL UP: `22 22`, `21 21` and
#   `def ""`. The rehearsal asserts those three are still in test.sh before
#   driving them, so a rung that moved its level fails here rather than
#   measuring a different instrument quietly. The whole tree compiles at 22 and
#   REFUSES 21, and the refusal is asserted as an identity rather than a count:
#   ONE error, in javac's own wording -- `unnamed variables are not supported
#   in -source 21` -- drawn by the single `catch (IllegalStateException _)` in
#   catchUnnamed(). That is the narrowest refusal any rung has had. Step 10's
#   was three errors in two spellings across two files, and the narrowing is
#   the point: there is exactly one place on this tree where the level can be
#   felt, so a count of anything but 1 means the construct is gone or something
#   unrelated has stopped compiling one level down. C-2 and C-4 cannot
#   contribute to it -- AccessController has been there since 1.2 and
#   MethodHandles.privateLookupIn since 9 -- so the level is C-3's alone. The
#   byte agreement therefore holds between the two that DO compile, -source 22
#   and the image default, which is the pair srcgate()'s own fallback chooses
#   between and therefore the pair whose disagreement would change a score.
#
#   AND THE RETIRED LIST GROWS BY ONE, WHICH IS NEW. Every rung through step 10
#   retired BOTH of the spellings it had been asking about; this rung retires
#   `ok20=$(srcgate 20 20)` alone and CANNOT retire `ok21=$(srcgate 21 21)`,
#   because it still makes that exact call -- with the opposite expected answer.
#   That is why the list is written as spellings rather than as levels, and this
#   is the first rung at which the distinction costs anything.
#
#   SEVEN CUTS AND NOT SIX. The step-11 cut is step 10's own tree compiled at
#   -source 21, clean, at step 10's 45 classes, with all of step 10's record
#   blocks at step 10's own frozen figures and every counter this rung adds at
#   zero. THREE INHERITED COUNTERS MOVED ON THE WHOLE TREE AND NONE IS SCORED:
#   try.size 3 -> 11, var.candidates 85 -> 90, var.candidatemethods 43 -> 46,
#   because this rung's section writes eight try statements and five convertible
#   locals. test.sh reads none of the three, and the step-11 cut asserts all
#   three at step 10's values so the movement is recorded here rather than found
#   later by somebody reconciling it with an edit. Below it the step-10 cut is
#   step 09's tree at -source 20 at 42 classes, the step-09 cut is step 08's at
#   -source 16 at 31 classes, the step-07 cut takes all sixteen step-07 and
#   step-08 counters to zero, and the step-06, step-05 and step-04 cuts are the
#   three the paragraphs below describe. Six rungs of frozen figures, each
#   measured by a different rung's rehearsal on a different day and asserted
#   here rather than re-derived.
#
#   THE NEAR MISSES ARE OWNER MISSES AT THIS RUNG, which is a different shape
#   from the rungs below and the reason Lib.java grew three impostors rather
#   than a look-alike method name. `Guarded.doPrivileged`, `Handles
#   .privateLookupIn` and `Handles.unreflectVarHandle` are types of this
#   project's own carrying the exact names C-2 and C-4 search for; every one is
#   called from Lib.java and none may produce a record. A channel that compared
#   simple names instead of resolved owners passes every total in this file and
#   fails only these three. varhandle's second owner is a NESTED type --
#   java.lang.invoke.MethodHandles.Lookup -- so the check is on a qualified name
#   with three dots in it.
#
#   AND THE PERTURBATION CHECK IS THIS RUNG'S OWN. authoring/perturb-src11.sh
#   makes ten single-edit breakages of the staged tree and requires each to come
#   back red on the named assertion; three of them are new and cover this rung's
#   three new blocks, one apiece. The seven inherited from perturb-src06.sh were
#   re-run here rather than carried forward on trust, and two of the seven were
#   stale -- which is the more useful half of this note. The var edit was still
#   red, but on the WRONG assertion: var.candidates on the step-11 cut is
#   reached long before var.size on the whole tree, and the rehearsal exits on
#   its first failure, so a step-06 expectation named a line that no longer ran.
#   The scorer edit was worse, and worse than it first looked. It sed'd for
#   `^ok10=$(srcgate 10 10)` -- ANCHORED AT COLUMN 0 -- and that spelling sits
#   at column 0 in step 06's test.sh and nowhere else on the ladder: 07 and 08
#   carry the call indented three spaces, and 09, 10 and this rung retired it
#   altogether. So the edit landed at exactly one rung and matched nothing at
#   FIVE, not at the three an unanchored grep suggests. What saved it is that
#   failing to land is not the same as passing: the grep-that-the-edit-landed
#   guard sets bad=1, and the unperturbed tree then comes back green, which
#   `run` reports as STILL GREEN -- the check is blind. It fails loudly at both
#   ends rather than certifying anything. Both are re-pointed, and this rung's
#   anchor, `^ok22=$(srcgate 22 22)`, was checked at column 0 in test.sh before
#   it was written.
#
#   What this layer proves about the three new blocks is now sharper, and still
#   bounded: priv.size, catchparam.read and varhandle.size are each separately
#   load-bearing, because one edit moves one of them and the rehearsal names
#   which. The other frozen figures in those blocks -- the .files and .methods
#   totals, the field sets, the individual record assertions -- are still what
#   they were, measured on a real compile and frozen, not separately perturbed.
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
# verified before a finding is taken from it: authoring/perturb-src11.sh makes
# ten single-edit breakages of the staged tree -- one var declaration removed,
# the legacy newInstance converted, a view over a local turned into a view over
# a call, Lib.java's section marker renamed, the scorer's scored probe moved off
# this rung's level, the answer key staged, the plugin's service registration
# deleted, one PrivilegedAction type reference respelled as the nested class
# that implements it, the unread catch parameter read, and the VarHandle got
# from arrayElementVarHandle instead of unreflectVarHandle -- and requires each
# to come back RED on the named assertion rather than on some other one. The
# last three are this rung's own, one per new block, and they land on priv.size,
# catchparam.read and varhandle.size: each is the FIRST assertion of its block
# that the edit moves, because the rehearsal stops at its first failure and an
# edit that trips a later line proves nothing about the earlier ones. Each also
# leaves the tree COMPILING, which is why the red is a counter and not javac.
# All ten do, and the unperturbed tree is green.
#
# The script is this rung's own rather than step 06's because step 06's was run
# against this image and came back bad=1: one of its seven was red on a counter
# belonging to a later rung, and one had been silently inert since step 09
# retired the call it edits. Seven inherited on faith read as ten do not.
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
    test "$(grep -cE '^ *ok(22|21|def)=\$\(srcgate ' /tests/test.sh)" = 3; \
    # ...and all three by their exact bytes, because the tag and the level are \
    # separate arguments to srcgate(): a probe TAGGED 10 but compiled at 11 \
    # writes its report into the file the scorer then reads as the Java 10 \
    # answer, and every count in family C would be an answer to a different \
    # question. \
    # This rung's level is 22, and THE CONTROL IS EXPECTED TO ANSWER `no` \
    # AGAIN -- the third rung in a row, after eight where the expected answer \
    # was yes. At every rung from 04 to 08 the control compile was a CONTROL \
    # whose expected answer was yes, because the must-flip was a library, a \
    # namespace or a manifest attribute and no language construct was \
    # load-bearing. Step 09 was the first to flip it, on `sealed`; step 10 \
    # flipped it on the pattern switch. Here it flips on ONE leg and one only: \
    # C-3 asks for `_` as the name of a catch parameter nothing reads, which is \
    # JEP 456's unnamed variable -- preview at 21, standard at 22 -- so a tree \
    # that has done C-3's work cannot compile at -source 21. The other three \
    # legs would all leave it at yes on their own, and at this rung that is \
    # unusually clear-cut, because two of them are not language at all: C-2 \
    # removes calls to `AccessController.doPrivileged`, which is API and \
    # terminally deprecated rather than gone from the grammar, and C-4 replaces \
    # `setAccessible` with a `VarHandle`, which is API that has compiled since \
    # 9. C-1 is the class-file major, a property of the artifact rather than of \
    # the source level. That is what makes `no` a statement about the unnamed \
    # catch parameters specifically and not about the rung in general -- and it \
    # is why the whole tree throws exactly ONE javac error at -source 21 rather \
    # than a spread of them. It is still scored by nothing, in either \
    # direction: a `yes` on a tree that did everything else is a true statement \
    # about an absence C-3 already charges, and charging it twice would pay the \
    # same leg out of two families. The level assertion this rung DOES make is \
    # C-1's, read out of a published class-file header via ${WORK}/scan.txt, \
    # and B-4's beside it reading the number this rung HOLDS at -- which is why \
    # level.txt carries two numbers here and one everywhere below step 09. The \
    # third probe is the compiler's own default, which is where test.sh falls \
    # back when ok22 reads no. \
    # \
    # fixtures/src-rehearsal.sh lifts SRCCP and srcgate() out of this same file \
    # by anchor and drives these same three calls, and it asserts these same \
    # three spellings before it asserts any number -- so a rename here fails the \
    # rehearsal on the rename rather than three hundred lines later on an \
    # unexplained zero. \
    grep -qxF 'ok22=$(srcgate 22 22)' /tests/test.sh; \
    grep -qxF '   ok21=$(srcgate 21 21)' /tests/test.sh; \
    grep -qxF '   okdef=$(srcgate def "")' /tests/test.sh; \
    # The frozen baseline files, each read from /tests and not from the tree. \
    # bv() and tv() name their paths as literals for exactly that reason. \
    # \
    # Fifty-one stage and forty-seven are read. The four that are not are \
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
    # THE FOUR FILES THIS RUNG ADDED ARE ALL READ, which is worth stating \
    # because it is the property that would have made them worth adding and the \
    # one a staging mistake breaks silently. priv.txt is C-2's whole \
    # denominator -- 67 records over 27 buckets -- and its 13-site payload in \
    # the same file, so the departure and the hold it is min()'d against cannot \
    # disagree about which tree they describe; catchparam.txt is C-3's, all \
    # three halves at once, 93 records of which 79 must survive and 14 must \
    # leave with the methods that hold them; varhandle.txt is C-4's, 14 rows \
    # carrying the verdict that says which 4 convert, derived from each \
    # record's own file and receiver type rather than typed; and vhids.txt is \
    # C-4's execution scale, the four recordings that pin what a converted site \
    # does when the access fails. None of the four has a count in target.txt \
    # that could stand in for it, so a rung that staged them and forgot to read \
    # them would score three quarters of family C over an empty set and take \
    # C-2, C-3 and C-4 whole. They are asserted read, by path, below -- and so \
    # are the thirteen inherited ones, because B-17 reads all four of step 10's, \
    # B-16 reads five of step 09's six, B-6 reads apinorm.txt, B-15 still reads \
    # all three of step 08's, and a rung that stopped reading sealed.txt or \
    # modnames.txt would take an inherited family whole in exactly the same way. \
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
    test "$(ls /tests/baseline | grep -c .)" = 51; \
    for unread in streamfiles junit5 varbuckets http; do \
        if grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${unread}.txt"; then \
            echo "baseline/${unread}.txt is staged as deliberately unread and test.sh now reads it" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qF 'baseline/varbuckets.txt' /tests/test.sh; \
    grep -qF 'baseline/http.txt' /tests/test.sh; \
    # ...and the four this rung added plus the thirteen it inherited, asserted \
    # read from CODE and not from a comment, which is the opposite test from the \
    # four above and is why the grep is piped through the same comment strip. \
    for readf in priv catchparam varhandle vhids \
                 reftest chain seqfirst swids \
                 ipattern strnl sealed swarrow tbids apinorm modnames svc svcids; do \
        if ! grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${readf}.txt"; then \
            echo "baseline/${readf}.txt is staged for a scored family and test.sh does not read it" >&2; \
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
        test -r "/tests/assets/rec/jdk25-step11/$(echo "${id}" | tr / .).txt" || \
            { echo "baseline/httpids.txt names ${id}, which has no recording" >&2; exit 1; }; \
    done; \
    grep -l 'log\[0\]=request:' /tests/assets/rec/jdk25-step11/*.txt > /tmp/withreq.txt; \
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
    # ...and accessible.txt is asked TWICE, which no rung below this one did. \
    # Its two halves are two populations now: departure over the 12 `guard` \
    # buckets step 06 froze, arrival over the 10 `setacc` calls that outlive \
    # this rung's C-4. A block copied forward from step 10 asks both halves \
    # over the guard list, and since C-4 converts three guard buckets to \
    # VarHandle that reads 9 of 12 on a correctly migrated tree -- the ninth \
    # trap, in the one shape that survives a re-cut baseline, because the file \
    # was fixed and the reader was not. Both calls and the denominator the \
    # arrival half divides by are asserted, since either one alone still runs. \
    grep -qF 'setwant /tests/baseline/accessible.txt setacc' /tests/test.sh; \
    grep -qF 'frac "${b13c3arr}" "${b13_saccb:-0}"' /tests/test.sh; \
    grep -qF "b13_saccb=\$(tv 'target\\.b13\\.setacc\\.buckets')" /tests/test.sh; \
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
    # EVERY BAN HERE IS A LEAF, WITH ONE PAIR OF PREFIXES AMONG THEM, and the \
    # exception is the whole story of what this rung did to the rung below. Step \
    # 08 had a family C of three legs and could ban `fact "C4.` and `fact "C5.` \
    # whole; step 09 had five and could ban neither; step 10 had four and could \
    # ban `fact "C5.` and `fact "gate.c5.` whole again. THIS RUNG HAS FOUR TOO, \
    # so that same pair stays a whole-prefix ban and every \
    # other dead spelling is banned leaf by leaf. A prefix ban is the stronger \
    # check and is used wherever the prefix is genuinely empty here; the leaves \
    # are enumerated one by one wherever the prefix above them is still live. \
    # \
    # FOUR RUNGS' WORTH OF DEAD LEAVES, not three, because this rung inherits \
    # step 07's family C as B-14, step 08's as B-15, step 09's as B-16 and step \
    # 10's as B-17, and all four sets were spelled as family C when they were \
    # written. Step 07's five are `C1.major`, `C2.jakarta`, `C3.javax-held`, \
    # `C4.toarray`, `C5.httpclient`; step 08's three are `C1.modules-named`, \
    # `C2.module-names`, `C3.platform-lookup`; step 09's four are \
    # `C2.pattern-instanceof`, `C3.text-blocks`, `C4.sealed-hierarchies` and \
    # `C5.arrow-switch`; step 10's three are `C2.type-patterns`, \
    # `C3.pattern-switch` and `C4.sequenced-access`. Each has a live successor \
    # under a B14, B15, B16 or B17 prefix, which is exactly what makes the old \
    # spelling survivable -- it would read the right number under the wrong \
    # question -- and therefore worth banning. \
    # \
    # STEP 10'S C-1 IS NOT IN THAT LIST AND THE ABSENCE IS NOT AN OMISSION. Its \
    # name was `C1.level-${WANT_MAJOR}` and this rung's is the same bytes, \
    # because both rungs ask what fraction of the tree reached the level \
    # level.txt names. The LEVEL moved, 65 to 66, and it moved inside a file the \
    # scorer reads rather than inside the scorer, so there is no spelling here to \
    # retire. That is the eleventh trap paid off in advance: a leg whose name \
    # carries its constant as a parameter needs no sweep when the constant moves. \
    # \
    # `fact "gate.c1.`, `fact "gate.c2.`, `fact "gate.c3.` and `fact "gate.c4.` \
    # are NOT banned at any depth, because all four are live prefixes here. The \
    # dead gate spellings under them are step 08's, step 09's and step 10's \
    # LEAVES, and only the leaves this rung does not itself republish. THREE \
    # FAMILIES OF LEAF ARE DELIBERATELY ABSENT from the list and each absence is \
    # a ban that would have failed on a correct file: step 10's `departed`, \
    # `departed.want`, `held`, `held.want`, `depfrac` and `holdfrac` under c2, c3 \
    # and c4, which this rung republishes under all three, because all three of \
    # its legs are departure-and-hold gates; step 10's `arrived`, `arrived.want` \
    # and `arrfrac` under c3 and c4, which this rung's C-3 and C-4 republish \
    # because both grew an arrival half; and step 09's `gate.c2.sites`, which is \
    # the one absence here that is not a republication at all. Step 09 published \
    # that name as a count of instanceof SITES; this rung publishes the same \
    # bytes as a count of privileged-API sites. A ban on it would fail on a \
    # correct file, so the spelling has to survive, and what keeps the two apart \
    # is the suffix beside it: step 09 wrote `gate.c2.sites.want`, a target, and \
    # this rung writes `gate.c2.sites.was`, a before-count. \
    # \
    # C-2's arrival names are the counter-case and they ARE banned, because this \
    # rung's C-2 is a removal with no arrival half at all -- `gate.c2.arrived` \
    # and `gate.c2.arrived.want` in the list below, and `gate.c2.arrfrac` by a \
    # dedicated refusal in the report-row block further down rather than here, \
    # because that one's message has to say why a departure-and-hold gate cannot \
    # publish an arrival fraction and this loop's message cannot. \
    # \
    # ONE FAMILY MOVED THE OTHER WAY AND IS NOW BANNED, which is what a term \
    # changing legs looks like from here. Step 08's `gate.c3.exec`, \
    # `gate.c3.exec.want` and `gate.c3.exec.broken` were deliberately ABSENT from \
    # the rung below's list, because step 10's C-3 republished them verbatim. \
    # This rung's C-3 has no execution term at all -- it is a catch-parameter \
    # rename, and a renamed parameter changes no behaviour that a test could \
    # observe -- while its C-4 does, because a VarHandle bound to the wrong field \
    # compiles and then fails at run time. So those three leaves move from the \
    # absent list to the ban list and come back one prefix over as \
    # `gate.c4.exec`, `gate.c4.exec.want` and `gate.c4.exec.broken`. A name that \
    # is dead under c3 and live under c4 is exactly the case a prefix ban gets \
    # wrong, which is why this sweep is over leaves. \
    # \
    # `converted`, `converted.want` and `convfrac` are dead in both spellings -- \
    # step 09 wrote them under c2 and step 10's first draft wrote them under c3 \
    # -- so both forms stay banned below, and `gate.b16.2.convfrac` is a \
    # different leaf under a live prefix that no ban reaches. \
    grep -v '^ *#' /tests/test.sh > /tmp/ts-code.txt; \
    for dead in 'tgt_c2_' 'tgt_c3_' 'tgt_c4_' 'tgt_c5_' \
                'tgt_ip_total' 'tgt_sealed_open' 'tgt_sealed_roots' \
                'tgt_sw_files' \
                'fact "C1.major' 'fact "C2.jakarta' 'fact "C3.javax-held' \
                'fact "C4.toarray' \
                'fact "C1.modules-named' 'fact "C2.module-names' \
                'fact "C3.platform-lookup' \
                'fact "C2.pattern-instanceof' 'fact "C3.text-blocks' \
                'fact "C4.sealed-hierarchies' \
                'fact "C2.type-patterns' 'fact "C3.pattern-switch' \
                'fact "C4.sequenced-access' \
                'fact "C5.' 'fact "gate.c5.' \
                'fact "gate.c1.declared' 'fact "gate.c1.readable' \
                'fact "gate.c1.right' 'fact "gate.c2.names' \
                'fact "gate.c3.converted' 'fact "gate.c3.convfrac' \
                'fact "gate.c2.converted' 'fact "gate.c2.convfrac' \
                'fact "gate.c2.totfrac' \
                'fact "gate.c3.moved' 'fact "gate.c3.movefrac' \
                'fact "gate.c3.textblocks' \
                'fact "gate.c4.decl' 'fact "gate.c4.nonsealed' \
                'fact "gate.c4.permits' 'fact "gate.c4.roots' \
                'fact "gate.c4.open' 'fact "gate.c4.closedfrac' \
                'fact "gate.c2.arrived' 'fact "gate.c2.patterns' \
                'fact "gate.c2.reftests' 'fact "gate.c2.swpatterns' \
                'fact "gate.c3.exec' 'fact "gate.c3.ipleft' \
                'fact "gate.c3.nullcase' 'fact "gate.c3.patterns' \
                'fact "gate.c3.switches' \
                'fact "gate.c4.firstreads' 'fact "gate.c4.sequenced'; do \
        if grep -qF "${dead}" /tmp/ts-code.txt; then \
            echo "test.sh still carries an inherited family-C spelling: ${dead}" >&2; \
            grep -nF "${dead}" /tmp/ts-code.txt | head -3 >&2; exit 1; \
        fi; \
    done; \
    # ...and the four leaves this rung's own family C does publish, asserted \
    # present in the same sweep's input, so the bans above cannot be satisfied by \
    # a file that publishes no family-C gate names at all. C-1's is matched \
    # without its value because the level is a parameter -- `C1.level-${WANT_MAJOR}` \
    # -- read out of level.txt rather than written here, which is the same reason \
    # B-4's name is matched as a prefix in the roll-call below. \
    for live in 'fact "C1.level-' 'fact "C2.doprivileged-removal=' \
                'fact "C3.unnamed-catch=' 'fact "C4.varhandle='; do \
        if ! grep -qF "${live}" /tmp/ts-code.txt; then \
            echo "this rung's family C does not publish ${live}" >&2; exit 1; \
        fi; \
    done; \
    # ...and all FOUR inherited families under their new prefixes, in the same \
    # sweep's input, so the bans above cannot be satisfied by a file that dropped \
    # the legs instead of renaming them. \
    for live in 'fact "B14.2.jakarta=' 'fact "B14.3.javax-held=' \
                'fact "B14.4.toarray=' 'fact "B14.5.httpclient=' \
                'fact "B15.2.module-names=' 'fact "B15.3.platform-lookup=' \
                'fact "B16.2.pattern-instanceof=' 'fact "B16.3.text-blocks=' \
                'fact "B16.4.sealed-hierarchies=' 'fact "B16.5.arrow-switch=' \
                'fact "B17.2.type-patterns=' 'fact "B17.3.pattern-switch=' \
                'fact "B17.4.sequenced-access='; do \
        if ! grep -qF "${live}" /tmp/ts-code.txt; then \
            echo "an inherited family-C leg was banned but not renamed: ${live}" >&2; exit 1; \
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
    # FAMILY C'S FOUR LEGS ARE UNINDENTED, and not because they escaped a guard: \
    # each one opens its OWN. C-1 reads ${WORK}/scan.txt behind `[ -s \
    # ${WORK}/scan.txt ]`; C-2, C-3 and C-4 each open `[ -n ${measured} ] && \
    # [ -s ${measured} ] && [ -r baseline/<population>.txt ]` over the source \
    # report and the one population file that leg divides by -- reftest.txt, \
    # chain.txt and seqfirst.txt, in that order. Four legs, four guards, four \
    # banners at column zero -- where B-14's four legs share ONE guard and are \
    # indented three inside it. A leg whose population file failed to stage reads \
    # zero on its own and does not take the other three with it, which is what \
    # four guards buy and one would not. \
    # \
    # This is the inversion worth stating, because it is the opposite of step \
    # 08: there family C read the published jars and every leg was jar-shaped, \
    # which is why that rung could leave all of them outside a source guard. \
    # Here three of the four count RECORDS the gate compiler wrote, so each had \
    # to acquire a guard of its own, and C-1 -- the one leg that is not \
    # source-shaped -- reads the same class-file scan B-4 reads. Counted rather \
    # than listed, for the reason indentation is always counted here: a leg that \
    # drifts into or out of the guard changes its indentation before it changes \
    # anything else, and nothing else about it changes at all. \
    test "$(grep -cE '^   # -+ B-14\.[2-5]$' /tests/test.sh)" = 4; \
    test "$(grep -cE '^# -+ B-15\.[23]$' /tests/test.sh)" = 2; \
    test "$(grep -cE '^# -+ B-16\.[2-5]$' /tests/test.sh)" = 4; \
    test "$(grep -cE '^# -+ C-[1-4]$' /tests/test.sh)" = 4; \
    # B-15'S TWO LEG BANNERS AND B-16'S FOUR ARE UNINDENTED WHERE B-14'S FOUR \
    # ARE INDENTED, and the difference is the guard each family sits behind \
    # rather than a formatting slip. B-14's four legs read the source scan and \
    # live inside the `if the scan parsed` guard; B-15's two read the published \
    # jars and the behaviour transcripts, neither of which the scan produces; and \
    # B-16's four each open a guard of their OWN over the source report and the \
    # one population file that leg divides by, exactly as family C's three source \
    # legs do -- because B-16 IS step 09's family C and that is the shape it had \
    # when it was one. All six sit at column zero beside family C. Counted \
    # separately for exactly that reason: a B-15 leg that drifted inside the scan \
    # guard would stop scoring on every tree whose source failed to parse and \
    # would look like a scan problem, and a B-16 leg that drifted inside it would \
    # do the same to the four legs this rung's candidate must HOLD, where a zero \
    # is indistinguishable from work undone. \
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
    grep -qF '0.04*a + 0.46*b + 0.26*c + 0.24*d' /tests/test.sh; \
    grep -qF '(0.20*a + 0.26*b + 0.36*c + 0.08*d) / 0.90' /tests/test.sh; \
    grep -qF '(0.17*a + 0.65*b + 0.14*c) / 0.96' /tests/test.sh; \
    grep -qF '(0.35*b + 0.45*c) / 0.80' /tests/test.sh; \
    # B-16'S AND B-17'S OWN COMPOSITES, which are step 09's family C and step \
    # 10's, each with ITS C-1 removed and the remainder renormalised -- the \
    # fourth and fifth times this ladder has performed that subtraction, and \
    # the second and third times the dropped leg WAS a class-file major. Step \
    # 09's C-1 asked what fraction of the tree had reached major 61 and step \
    # 10's asked the same of 65. This rung asks it of 66, out of the same scan, \
    # under the same fact name, and a tree at 66 is a tree past both by \
    # arithmetic -- so carrying either leg forward would charge twice for one \
    # reading and would charge it on a reading that cannot be failed \
    # independently. Both legs go: 0.20 + 0.26 + 0.36 + 0.08 = 0.90 is B-16's \
    # divisor and 0.17 + 0.65 + 0.14 = 0.96 is B-17's. \
    # \
    # WHAT DOES NOT SURVIVE MATTERS AS MUCH AS WHAT DOES. Neither step 09's C-1 \
    # nor step 10's carried a scaling into any other leg -- both were plain \
    # weighted terms, unlike step 08's, which multiplied its own C-2 and \
    # therefore left a SQUARE behind when it went. B-16 is four legs and a \
    # divisor, B-17 is three legs and a divisor, and a rung that squared one of \
    # them would be importing step 08's shape into a family that never had it. \
    # ONE SCALING DOES SURVIVE INTO B-17 AND IT IS NOT A SQUARE: step 10's C-3 \
    # multiplied its source fraction by the fraction of its five recorded \
    # dispatches that still matched, and B-17.3 keeps that factor, because the \
    # recordings are still in this rung's era and a pattern switch that stopped \
    # answering would otherwise cost nothing. \
    if grep -qF '0.10*a + 0.20*b + 0.26*c + 0.36*d + 0.08*e' /tests/test.sh; then \
        echo "step 09's five-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
    if grep -qF '0.04*a + 0.17*b + 0.65*c + 0.14*d' /tests/test.sh; then \
        echo "step 10's four-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
    # B-15's own composite, which is step 08's family C with ITS C-1 removed and \
    # the remaining two renormalised by dividing by 0.80 -- the third time this \
    # ladder has performed that subtraction and the first time the dropped leg \
    # was not a class-file major. Step 08's C-1 asked whether the twelve \
    # published artifacts declared a module name the platform could read back. \
    # The manifests are written once; re-charging for them would pay twice for a \
    # line in a build file, so the leg goes and 0.35 + 0.45 = 0.80 is the \
    # divisor. \
    # \
    # WHAT SURVIVES OF STEP 08'S C-1 IS ITS SCALING, and that is why B-15.2 is a \
    # SQUARE. Step 08 paid C-2 as `names x C-1`, and on a tree where the names \
    # and the readback are the same twelve artifacts those two fractions are the \
    # same number, so dropping C-1 as a separate leg leaves the product as \
    # `names x names`. Eleven of twelve names therefore pays 0.840278 and not \
    # 0.916667: the last name still costs more than the first ten, which is the \
    # property step 08 bought with the scaling and this rung keeps without \
    # paying for the leg twice. \
    if grep -qF '0.20*a + 0.35*b + 0.45*c' /tests/test.sh; then \
        echo "step 08's three-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
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
    # The weights each family DIVIDES BY, summed. test.sh claims five of these \
    # sums in prose -- family B's SEVENTEEN to 1.00000000000000, B-14's four to \
    # 0.90, B-15's two to 0.80, B-16's four to 0.90 and B-17's three to 0.96 -- \
    # and a claim in prose that nothing checks is the same object as no claim. \
    # Family C's four and B-12's five are summed too: B-12's are step 05's own \
    # internal weights carried forward unrenormalised, and the whole argument \
    # for not renormalising them is that they already sum to one. \
    # \
    # FAMILY C'S ROW WAS WRONG AT THE RUNG BELOW AND THIS LOOP DID NOT SAY SO, \
    # which is why it is stated here rather than quietly corrected. Step 09's \
    # five family-C weights were 0.10, 0.20, 0.26, 0.36 and 0.08; the list this \
    # loop summed for them was `0.20 0.30 0.20 0.22 0.08`, five numbers from no \
    # rung at all that happen to total 1.000000. The check passed and measured \
    # nothing. It did no damage, because step 09's five real weights were pinned \
    # twice over -- by the literal report-row greps below and by the family \
    # loop's positional comparison against the awk expression -- so the only \
    # claim that went unchecked was "they sum to one". NOTES 15.8 records it \
    # against step 09. The sixth trap, in the small: the instrument was verified \
    # here before the finding it produced was believed, and what it was actually \
    # measuring was arithmetic on numbers from nowhere. \
    # \
    # FAMILY B'S ROW IS FOURTEEN DECIMAL PLACES WIDE AT THIS RUNG and that is \
    # not cosmetic. Step 07's thirteen weights were exact at six; every rung \
    # since has scaled the whole column by 0.88 to make room for a new gate's \
    # 0.12, and four such scalings take 0.046464 to 0.02786424520704, which \
    # needs fourteen. A rung that rounded them back to twelve would print \
    # sixteen weights that no longer sum to 0.88 and a family B that no longer \
    # sums to one -- and this loop is the only thing in the tree that would say \
    # so. The two-step check below pins each printed row to the awk term it was \
    # copied from, so a rounded row and a rounded term would still agree with \
    # each other; only the sum catches the pair. \
    # \
    # B-17'S DIVISOR IS THE ONE WORTH STATING TWICE at this rung, for the reason \
    # B-16'S was at the rung below. Step 10's three remaining legs carried 0.96 \
    # of step 10's family C between them, so the renormalisation has to divide \
    # by 0.96; divide by 1.00 instead and a tree that held every one of step \
    # 10's legs scores B-17 at 0.96 and is charged a twenty-fifth of the gate \
    # for work it did not fail to do. That is 0.04 x 0.12 x 0.175 = 0.00084 of \
    # the reward, on a floor that has 0.036872 of room between 0.463128 and the \
    # half this step is held under -- smaller than the rung below's slip, and in \
    # the same direction, the one that makes the floor look better than it is, \
    # which is the direction nothing else here would catch. \
    for sum in "0.02786424520704 0.01393212260352 0.05572849041408 0.05572849041408 0.04644040867840 0.05572849041408 0.02786424520704 0.04644040867840 0.02786424520704 0.05108444954624 0.05572849041408 0.06332783001600 0.07196344320000 0.08177664000000 0.09292800000000 0.10560000000000 0.12000000000000:1.000000:family B" \
               "0.04 0.46 0.26 0.24:1.000000:family C" \
               "0.30 0.35 0.10 0.10 0.15:1.000000:B-12" \
               "0.12 0.18 0.03 0.26 0.23 0.08:0.900000:B-13's six, before its divisor" \
               "0.07 0.17 0.12 0.54:0.900000:B-14's four, before its divisor" \
               "0.35 0.45:0.800000:B-15's two, before its divisor" \
               "0.20 0.26 0.36 0.08:0.900000:B-16's four, before its divisor" \
               "0.17 0.65 0.14:0.960000:B-17's three, before its divisor"; do \
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
    # family C; B-14's four, B-16's four, B-17's three and B-12's five have no \
    # printed rows to compare against, so they are pinned here by the whole \
    # expression instead. B-17's entry is the new one, and it is the only one of \
    # the three whose terms are ALSO this rung's own family-C weights one rung \
    # ago -- 0.17, 0.65 and 0.14 are step 10's C-2, C-3 and C-4 -- so a rung \
    # that had left family C alone would satisfy it with step 10's arithmetic. \
    # That is why the sum above states 0.96 for it and 1.000000 for family C: \
    # the two lists cannot both be right unless family C was rewritten. \
    grep -qF '0.07*b + 0.17*c + 0.12*d + 0.54*e' /tests/test.sh; \
    grep -qF '0.20*a + 0.26*b + 0.36*c + 0.08*d' /tests/test.sh; \
    grep -qF '0.17*a + 0.65*b + 0.14*c' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when step 03's C-6 \
    # was added the arithmetic was changed and the report was not -- so every run \
    # printed five gates at the old weights while scoring six at the new ones. A \
    # number that is right, described wrongly, is the one defect a reviewer \
    # cannot see. Every weight in this rung's two families is a fresh literal: \
    # family C was re-weighted from scratch because this step's migration has \
    # FOUR gates and no two of them cost the same, and family B grew a \
    # SEVENTEENTH gate, which moved all sixteen others -- every step-10 weight \
    # was scaled by 0.88 to make room for B-17's 0.12, and the scaling is exact \
    # at FOURTEEN places, which is why these rows print fourteen decimals where \
    # the rung below printed twelve. \
    # \
    # ALL SIXTEEN INHERITED ROWS MOVED AT THIS RUNG, not just the ones nearest \
    # the new gate, so listing "the rows that moved" by name would be listing the \
    # whole family. The seven nearest are listed anyway, because they are the \
    # ones whose OLD spelling is a plausible survivor -- 0.058050510848, \
    # 0.063327830016, 0.071963443200, 0.081776640000, 0.092928000000, \
    # 0.105600000000 and 0.120000000000 are all real numbers that appeared in \
    # this file an edit ago. \
    # \
    # AND THIS RUNG'S SCALING KEEPS THE TRAP THE RUNG BELOW FOUND, one position \
    # further along. Step 10's twelve-decimal 0.105600000000 and 0.120000000000 \
    # are PREFIXES of this rung's fourteen-decimal 0.10560000000000 and \
    # 0.12000000000000, because adding a gate scaled the column by 0.88 and \
    # 0.88 x 0.12 = 0.1056 exactly, so the trailing zeros simply grew. What is \
    # different here is WHERE those two spellings now sit: 0.1056 was step 10's \
    # B-15 and is this rung's B-16, and 0.12 was step 10's B-16 and is this \
    # rung's B-17, so a stale row is a correct weight under the PREVIOUS row's \
    # label -- which is why the ban list below carries both of those rows \
    # label-and-weight together, and why a ban on either label alone would fail \
    # on a correct file. That is the eleventh trap with the escaping removed and \
    # the padding put in its place, and it is why every grep here matches the \
    # LABEL AND THE WEIGHT together and none of them matches a weight alone. The \
    # family loop below covers the other nine positionally, and it compares \
    # whole fields rather than substrings. \
    grep -qF "printf '    B10 java 7 held      %s  x0.05108444954624" /tests/test.sh; \
    grep -qF "printf '    B11 java 8 lang held %s  x0.05572849041408" /tests/test.sh; \
    grep -qF "printf '    B12 java 8 lib held  %s  x0.06332783001600" /tests/test.sh; \
    grep -qF "printf '    B13 java 9-10 held   %s  x0.07196344320000" /tests/test.sh; \
    grep -qF "printf '    B14 java 11 held     %s  x0.08177664000000  (penalty %s)" /tests/test.sh; \
    grep -qF "printf '    B15 jpms + services  %s  x0.09292800000000" /tests/test.sh; \
    grep -qF "printf '    B16 java 17 held     %s  x0.10560000000000" /tests/test.sh; \
    grep -qF "printf '    B17 java 21 held     %s  x0.12000000000000" /tests/test.sh; \
    grep -qF "printf '    C1 level %-3s            %s  x0.04  (%s of %s class files)" /tests/test.sh; \
    grep -qF "printf '    C2 doPrivileged gone    %s  x0.46  (departed %s, held %s)" /tests/test.sh; \
    grep -qF "printf '    C3 unnamed catch        %s  x0.26  (departed %s, arrived %s, held %s)" /tests/test.sh; \
    grep -qF "printf '    C4 varhandle            %s  x0.24  (departed %s, arrived %s, held %s, exec %s)" /tests/test.sh; \
    # And the step-10 rows asserted GONE, because the twelve asserted present \
    # above are twelve of the twenty-one weighted rows this report prints, and a \
    # row this rung forgot to rewrite is a row describing the rung below. \
    # \
    # B-4 IS NOT IN THIS LIST AND HAS NOT BEEN SINCE STEP 08, which is a removal \
    # worth restating. Step 07 printed `>=54` as a literal and step 08 changed \
    # the row to `>=%-3s` from HOLD_MAJOR, so the old spelling was bannable text. \
    # This rung prints the same `%-3s` form -- HOLD_MAJOR is 65 here and 55 there \
    # -- and the ROW is byte-identical apart from its weight, which the family \
    # loop below covers positionally. A ban on the row prefix would now fail on a \
    # correct file. \
    # \
    # C-1'S ROW IS NOT IN THIS LIST EITHER, and that is the sharper omission. \
    # Step 10 printed `    C1 level %-3s            %s  x0.04  (%s of %s class \
    # files)` and this rung prints the same bytes: the label, the weight, the \
    # conversion and the tail are all unchanged, because both rungs ask what \
    # fraction of the tree reached the major level.txt names and both price it at \
    # a twenty-fifth. The row is required PRESENT above. Banning it as a step-10 \
    # row would fail on a correct file, and that is the second place at this rung \
    # where a leg's name outliving its constant is the right answer rather than \
    # an oversight -- the fact name `C1.level-${WANT_MAJOR}` is the first. \
    # \
    # The three dead C rows are banned by LABEL ALONE and carry no weight text, \
    # because step 10's three dead weights are not dead numbers: 0.17, 0.65 and \
    # 0.14 all survive intact one family over as B-17's three, and this file \
    # asserts that expression present a hundred lines above. A ban written on the \
    # label-and-weight pair would therefore be banning live arithmetic under a \
    # dead label, and would still miss the failure it is for. It is the labels \
    # `C2 type patterns`, `C3 pattern switch` and `C4 sequenced access` that are \
    # dead here, not the coefficients beside them, and a ban written on the pair \
    # would have missed a row re-weighted but never re-labelled. \
    # \
    # THE TWO B ROWS IN THIS LIST ARE THE PREFIX PAIR and they are the reason it \
    # grew by one. `    B15 jpms + services  %s  x0.105600000000` and \
    # `    B16 java 17 held     %s  x0.120000000000` are step 10's, and both \
    # labels are LIVE at this rung under new weights -- B-15 now prints \
    # 0.09292800000000 and B-16 now prints 0.10560000000000. Banning either \
    # label alone would fail on a correct file; banning the pair catches exactly \
    # the row that was never rescaled. \
    for oldrow in "    B1 no-maven          %s  x0.031663915008" \
                  "    B8 jupiter held      %s  x0.052773191680" \
                  "    B15 jpms + services  %s  x0.105600000000" \
                  "    B16 java 17 held     %s  x0.120000000000" \
                  "    C2 type patterns" "    C3 pattern switch" \
                  "    C4 sequenced access"; do \
        if grep -qF "printf '${oldrow}" /tests/test.sh; then \
            echo "the report still prints a step-10 row: ${oldrow}" >&2; \
            exit 1; \
        fi; \
    done; \
    # THE PENALTY IS PRINTED ON B-14'S ROW rather than on a row of its own, and \
    # that is the inheritance made visible. At step 07 it multiplied family C and \
    # earned its own unweighted line; here it multiplies exactly one gate, so it \
    # belongs in that gate's parenthesis and nowhere else. A rung that kept the \
    # standalone line would be telling a reader the penalty multiplies family C, \
    # which at this rung it does not -- family C is the language level and the \
    # penalty counts raw types in held code. \
    if grep -qF 'anti-gaming penalty     %s  (multiplies family C)' /tests/test.sh; then \
        echo "the report still prints the penalty as a family-C multiplier" >&2; \
        exit 1; \
    fi; \
    # ALL FOUR FAMILY-C ROWS PRINT THEIR RAW READINGS beside their paid ones, \
    # and that is the only place a reader of the report can see the two apart. \
    # C-1's tail is a CENSUS -- `746 of 746 class files` -- and the other three \
    # are the sub-fractions each of those legs min()s together, printed before \
    # the min takes them. They are not the same COUNT of terms at this rung, \
    # which is new: C-3 and C-4 print three, C-2 prints two, because C-2's \
    # arrival is a deletion and a deletion has no population to count arriving \
    # into. Asserted as claims distinct from those rows' \
    # weights, because on the golden every one of those parentheses reads \
    # 1.000000, or reads its own denominator back, so a report that dropped a \
    # parenthesis would look identical on every tree that finished the step and \
    # would differ only on the trees the rehearsal is about. \
    # \
    # THE SUB-FRACTIONS ARE THE ROW'S WHOLE MEANING AND NOT DECORATION. A min() \
    # prints one number and hides which of its inputs bound it, and at this rung \
    # the three inputs are three different failures: `departed` low means the old \
    # form is still in the tree, `arrived` low means the new form was never \
    # written, `held` low means a site that must NOT move did. A candidate that \
    # converted all fourteen `setAccessible` sites to VarHandle instead of the \
    # four C-4 asks for reads departed 1.000000, arrived 1.000000 and held \
    # 0.000000; a candidate that deleted the four outright reads departed \
    # 1.000000 and arrived 0.000000. Without the tail both rows are a bare \
    # 0.000000, which is also what doing nothing at all reads, and the three are \
    # different mistakes from each other. \
    # \
    # NO TWO OF THIS RUNG'S THREE TAILS ARE THE SAME, which is a break from \
    # every rung below and is the whole reason the counts are asserted rather \
    # than the strings alone. C-3 is a departure, an arrival and a hold; C-4 is \
    # those three and then an execution fraction; C-2 is a departure and a hold \
    # with NO arrival, because what arrives when `doPrivileged` leaves is \
    # ordinary code with no shape to count. A reader who has learnt C-3's row \
    # can read C-4's and must NOT read C-2's the same way, and the report is \
    # spelled so that the difference is visible without the scorer. \
    # \
    # AND THAT IS WHY BOTH COUNTS BELOW ARE 1. C-4's tail carries a comma where \
    # C-3's closes its bracket, so the three-term spelling still matches C-3 \
    # alone -- the rung below's argument, one row along. What is new is C-2's: \
    # `(departed %s, held %s)` is what C-3's tail becomes if an edit drops its \
    # ARRIVAL term, so the ban list below cannot carry that spelling as a \
    # truncation -- it is a live row here. The count is what catches it. A C-3 \
    # trimmed to two terms reads 1 for the three-term string and 2 for the \
    # two-term one, and the second assertion fails. \
    grep -qF '(%s of %s class files)' /tests/test.sh; \
    grep -qF '(departed %s, held %s)' /tests/test.sh; \
    grep -qF '(departed %s, arrived %s, held %s)' /tests/test.sh; \
    grep -qF '(departed %s, arrived %s, held %s, exec %s)' /tests/test.sh; \
    test "$(grep -cF '(departed %s, arrived %s, held %s)' /tests/test.sh)" = 1; \
    test "$(grep -cF '(departed %s, held %s)' /tests/test.sh)" = 1; \
    grep -qF 'fact "gate.c1.at${WANT_MAJOR}=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.classes=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.depfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.holdfrac=' /tests/test.sh; \
    # ...and C-2 publishes NO arrival fraction, asserted as an absence rather \
    # than left to the eye. Every rung from 07 to 10 published `gate.cN.arrfrac` \
    # for every leg of its family C, so the name is one an edit would add back \
    # out of habit -- and a leg that published an arrival fraction while scoring \
    # min(departed, held) would be a report describing arithmetic the scorer \
    # does not do. \
    if grep -qF 'fact "gate.c2.arrfrac=' /tests/test.sh; then \
        echo "C-2 publishes an arrival fraction and its score has no arrival term" >&2; \
        exit 1; \
    fi; \
    grep -qF 'fact "gate.c3.depfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c3.arrfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c3.holdfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.depfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.arrfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.holdfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.exec=' /tests/test.sh; \
    # ...and every retired or TRUNCATED tail asserted gone in one sweep. The \
    # entries are older rungs' tails and this rung's own tails with a term \
    # chopped off, which is the exact shape an edit that trimmed a row to fit \
    # the column would leave behind. A three-term tail cut to two still prints, \
    # still lines up, and still says nothing about the term that was removed -- \
    # and on the golden, where all three terms read 1.000000, it reads correct. \
    # \
    # NOTHING FROM THE RUNG BELOW IS IN THIS LIST AND THAT IS NOT AN OVERSIGHT. \
    # Step 10's three family-C tails were `(%s of %s class files)`, `(departed \
    # %s, arrived %s, held %s)` and `(departed %s, arrived %s, held %s, exec \
    # %s)`, and all three are LIVE here -- C-1, C-3 and C-4 print them verbatim, \
    # asserted present above. A rung whose report rows all changed label and \
    # weight while three of its four tails stayed byte-identical is exactly the \
    # shape that makes a stale-row ban tempting and wrong, so the staleness is \
    # caught by the label-and-weight bans instead, which is where it lives. \
    # \
    # Each truncated form is written with its closing parenthesis so that the \
    # full tail cannot satisfy its own ban: `(departed %s, arrived %s)` is not a \
    # substring of `(departed %s, arrived %s, held %s)`, because the live row has \
    # a comma where the ban has a bracket. That is the whole reason these are \
    # fgrep strings ending in `)` rather than prefixes. And it is why the ONE \
    # truncation this rung cannot ban -- C-3 with its arrival term dropped, \
    # which lands exactly on C-2's live `(departed %s, held %s)` -- is left to \
    # the count assertion above rather than added here, where it would refuse a \
    # correct file. \
    for deadtail in '(converted %s, held plain %s)' '(exec %s)' \
                    '(closed %s of %s, open %s of %s)' '(%s of %s files)' \
                    'readable %s)' \
                    '(departed %s)' '(departed %s, arrived %s)' \
                    '(converted %s, held %s)' \
                    '(converted %s, held %s, exec %s)'; do \
        if grep -qF "${deadtail}" /tests/test.sh; then \
            echo "the report prints a retired or truncated family-C tail: ${deadtail}" >&2; \
            exit 1; \
        fi; \
    done; \
    nrep=$(grep -cE "^printf '    C[0-9][0-9]* " /tests/test.sh); \
    nfact=$(grep -cE '^ *fact "C[0-9][0-9]*\.' /tests/test.sh); \
    if [ "${nrep}" != "${nfact}" ]; then \
        echo "the report prints ${nrep} family-C gates and the scorer publishes ${nfact}" >&2; \
        exit 1; \
    fi; \
    test "${nrep}" = 4; \
    # The fact-line pattern stays anchored at `^ *fact` and not at `^fact` even \
    # though all four of this rung's family-C facts are unindented, which is what \
    # the rung below settled on once its C-1 stopped being read before its guard. \
    # The tolerant anchor is kept deliberately: it is the anchor that keeps \
    # counting correctly if a later rung puts family C back inside a guard, and a \
    # pattern that has to be edited to stay true is a pattern that will be edited \
    # wrongly. The count is asserted against a literal 4 as well, so the \
    # tolerance cannot hide a leg that grew or vanished on both sides at once. \
    # \
    # And the general form of the same check, for every family at once, because \
    # asserting eleven literals by hand closes one instance of the hole and \
    # leaves the class of it open -- family B's sixteen printfs and family B's \
    # awk are the same pair of spellings, and they agree today only by habit. For \
    # each family this pulls the weights out of the report rows in order and out \
    # of that family's awk expression in order, and requires the two sequences to \
    # be equal. A family added later is covered by adding its letter to the list. \
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B passed ten gates six rungs below, and a one-digit \
    # pattern reads nine rows against seventeen coefficients and calls the \
    # difference a mismatch -- or worse, would have silently skipped B10 through \
    # B17 on both sides if the coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    # \
    # THE WEIGHT PATTERN IS 0\.[0-9][0-9]* AND IT HAS TO STAY OPEN-ENDED, which \
    # matters more at this rung than at any before it. Family B's weights are \
    # fourteen decimals wide and family C's are two, on rows a dozen lines apart \
    # in one report; a pattern that pinned a width would read one family and \
    # silently skip the other, and silently skipping a family is the whole of \
    # what this loop exists to catch. \
    # \
    # The row pattern needs one space before %s and this rung's family B has rows \
    # whose labels are a character longer than the column, so the columns no \
    # longer line up and the pattern must not depend on them. It matches \
    # ` .*  x0.NN` and not a fixed column for exactly that. THREE rows in this \
    # file are the hard cases and all three are covered by the same pattern: B-4 \
    # carries a `%-3s` conversion in its LABEL, B-14 carries a `(penalty %s)` \
    # after its weight, and C-1 carries both -- a `%-3s` in its label and a \
    # `(%s of %s class files)` tail after its weight -- so on none of the three \
    # is the weight the first or the last field on the line. \
    # \
    # Family C's anchor is `0\.24\*d$` and family B's is \
    # `0\.12000000000000\*b17$`, both anchored at end of line. The `$` does real \
    # work on the C side at this rung, for the reason it did at the rung below: \
    # B-16's own expression is `(0.20*a + 0.26*b + 0.36*c + 0.08*d) / 0.90`, \
    # which carries a `*d` term too, and only the end-of-line anchor tells the \
    # two apart -- B-16's line runs on into `) / 0.90` where family C's stops at \
    # the `d`. B-17's `(0.17*a + 0.65*b + 0.14*c) / 0.96` ends in a `*c` and is \
    # not a candidate, which is why the anchor could move to family C's new \
    # fourth weight without another term being distinguished. The count below \
    # still asserts that each anchor picked exactly ONE line, which is the check \
    # that would say so if a later rung's arithmetic ever ended in the same \
    # bytes. \
    for fam in B:17:'0\.12000000000000\*b17$' C:4:'0\.24\*d$'; do \
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
    # THREE RUNGS' FAMILY-C NAMES ARE IN THIS LIST UNDER B-14, B-15 AND B-16 \
    # PREFIXES, which is the whole inheritance in fourteen lines. The leaf of \
    # each name is unchanged -- jakarta, javax-held, toarray, httpclient, \
    # module-names, platform-lookup, pattern-instanceof, text-blocks, \
    # sealed-hierarchies, arrow-switch, and now type-patterns, pattern-switch \
    # and sequenced-access -- because the question each leg asks is unchanged; \
    # only the family that asks it moved. The LEG NUMBER is unchanged too, so \
    # `sed 's/^B1[4567]\.\([0-9]\)\./C\1./'` over this rung's facts.txt turns \
    # the inherited names back into the names steps 07, 08, 09 and 10 printed, \
    # and all four files line up against it. That is what makes the claims \
    # "B-14 is step 07's family C", "B-15 is step 08's", "B-16 is step 09's" \
    # and "B-17 is step 10's" checkable rather than decorative. \
    # \
    # B-15 AND B-16 EACH HAVE NO LEG 1 AND THE TWO GAPS HAVE DIFFERENT REASONS. \
    # Step 08's C-1 asked whether the twelve artifacts declared a module name \
    # the platform could read back; B-15 drops it rather than carrying it, \
    # because the manifests are written once and a rung that re-charged for them \
    # would be paying twice for a line in a build file. What survives of it is \
    # the SCALING: B-15.2 is the square of the name fraction, which is what step \
    # 08's C-1-times-C-2 collapses to once C-1 is gone. \
    # \
    # B-16 AND B-17 EACH HAVE THE SAME GAP, AND IT IS THE NINTH TRAP RATHER THAN \
    # A MATTER OF TASTE. Step 09's C-1 read the fraction of this tree's class \
    # files that stood at major 61, step 10's read the same fraction at 65, and \
    # both are spelled `C1.level-${WANT_MAJOR}` because the major in that name \
    # is a parameter and not a literal. Carrying either leg would have been \
    # wrong whichever way round it was done. Pinned at its own major it reads \
    # 0.000000 on a CORRECT tree one rung later, because a tree compiled at 66 \
    # has no class files left at 65 or 61 -- an inherited leg that no right \
    # answer can pass, which is the ninth trap exactly. Parameterised at 66 it \
    # is this rung's own C-1 under a second name, charging twice for one \
    # class-file scan. So B-16 is step 09's family C with its C-1 removed and \
    # the remaining four renormalised over 0.90, B-17 is step 10's with the \
    # same subtraction and the remaining three renormalised over 0.96, and the \
    # level question is asked once, by C-1. The absent legs are asserted below \
    # beside B-13's and B-14's. \
    for g in B14.2.jakarta B14.3.javax-held B14.4.toarray B14.5.httpclient \
             B14.java11-jakarta \
             B15.2.module-names B15.3.platform-lookup B15.jpms-services \
             B16.2.pattern-instanceof B16.3.text-blocks \
             B16.4.sealed-hierarchies B16.5.arrow-switch B16.java17-constructs \
             B17.2.type-patterns B17.3.pattern-switch B17.4.sequenced-access \
             B17.java21-constructs \
             B4.one-major-ge- \
             B8.jupiter-held B9.junit4-held B10.java7-held B11.java8lang-held \
             B12a.optional B12b.collectors B12c.objects B12d.jtime \
             B12e.ships-java8-library B12.java8-library \
             B13.2.newinstance B13.3.isaccessible B13.4.boxedctor \
             B13.5.factories B13.6.optional9 B13.7.var B13.java9-10 \
             C1.level- C2.doprivileged-removal C3.unnamed-catch \
             C4.varhandle; do \
        grep -qF "fact \"${g}" /tests/test.sh; \
    done; \
    # The four family-C leaves are named for what they ASK, not for what they \
    # read, and the distinction is the one this rung is most likely to blur. \
    # `C1.level-66` is not "the build file says 22": it is the fraction of the \
    # tree's own class files that a separate instrument found at major 66, so a \
    # release setting that never reached javac pays nothing. \
    # `C2.doprivileged-removal` is not "the word doPrivileged is gone": it is \
    # 67 privileged-API sites leaving the 27 methods that hold them AND 13 \
    # reflective sites still there afterwards -- one reflective construction \
    # per FILE across the seven, and the six scanner methods still calling \
    # setAccessible -- because a candidate who deletes the class rather than \
    # unwrapping the call passes the first half and fails the second. `C3.unnamed-catch` is not "an \
    # underscore appeared": it is 28 unread catch parameters becoming `_` in \
    # the methods that hold them AND the 51 parameters something still READS \
    # left with their names, because a tree that renames those does not \
    # compile. `C4.varhandle` is not "privateLookupIn appeared": it is 4 \
    # setAccessible sites departing AND both halves of the lookup arriving in \
    # each of those 4 methods AND the 10 sites that cannot convert -- the \
    # scanners that hand a Field back to their caller and the four whose \
    # exception wrapping four transcripts record -- left as they are. \
    # \
    # THREE OF THE FOUR ARE A MINIMUM OVER THEIR HALVES AND NOT AN AVERAGE. C-3 \
    # and C-4 take min(departed, arrived, held), C-2 takes min(departed, held) \
    # because its arrival is a deletion and there is nothing to count arriving, \
    # and C-4 alone is then scaled by its execution fraction -- so no half can \
    # be bought with another. \
    # That is why the report prints the sub-fractions beside each leg: the leg's \
    # number IS one of them, and a reader has no way to tell which without them. \
    # \
    # And the measurements underneath them, which are what a reviewer reads when \
    # a leg comes back short. These are asserted by name because a leg that \
    # scores correctly while publishing nothing is a leg no one can debug: at \
    # this rung C-1 reads 0 on a tree whose class files are all at major 65 and \
    # publishes no number at all on a tree that produced no scan, and only \
    # `gate.c1=no-scan` tells those apart. \
    # \
    # THE MODULE MEASUREMENTS MOVED PREFIX WITH THEIR FAMILY. Step 08 published \
    # them as `gate.mod.*` when the module graph was its family C; here they are \
    # `gate.b15.mod.*`, under the gate that now asks the question. The bare \
    # `gate.mod.` spelling is asserted gone below for the usual reason -- it \
    # would read the right number under the wrong family -- and the five leaves \
    # are unchanged so the two files still diff. \
    # \
    # AND TWO RUNGS' FAMILY-C MEASUREMENTS MOVED THE SAME WAY, WITH ONE \
    # DIFFERENCE THAT DECIDES HOW THEY ARE POLICED. Step 09 published its four \
    # under `gate.c2.*` through `gate.c5.*` and they are `gate.b16.2.*` through \
    # `gate.b16.5.*` here; step 10 published its three under `gate.c2.*` through \
    # `gate.c4.*` and they are `gate.b17.2.*` through `gate.b17.4.*` here. Both \
    # sets now sit under the gate that asks the question. But the vacated \
    # prefixes are not vacant: this rung's own C-2, C-3 and C-4 publish under \
    # `gate.c2.`, `gate.c3.` and `gate.c4.` again, asking different questions \
    # with different leaves. So the bare prefix CANNOT be asserted gone the way \
    # `gate.mod.` can, and the retired leaves are banned one at a time in the \
    # dead-name loop above instead -- `gate.c2.converted` and `gate.c3.moved` \
    # from step 09, `gate.c2.patterns` and `gate.c4.sequenced` from step 10, and \
    # their neighbours. Only `gate.c5.` is bannable whole, because this rung has \
    # no fifth family-C leg for the name to mean anything else. \
    # \
    # TWENTY-FOUR LEAVES SURVIVE THE MOVE FROM THE RUNG BELOW IN PLACE, AND A \
    # TWENTY-FIFTH FROM TWO RUNGS BELOW. `departed`, `departed.want`, `depfrac`, \
    # `held`, `held.want` and `holdfrac` are live under all three of `gate.c2.`, \
    # `gate.c3.` and `gate.c4.`, because every leg on both rungs has a departure \
    # half and a hold half and the halves are spelled the same; `arrived`, \
    # `arrived.want` and `arrfrac` are live under `gate.c3.` and `gate.c4.` for \
    # the same reason, and dead under `gate.c2.` because this rung's C-2 is a \
    # removal. Six plus nine plus nine is twenty-four, and step 09's \
    # `gate.c2.sites` is the twenty-fifth. Those twenty-five are the reason the \
    # ban list above is a list of leaves and not a list of prefixes: a prefix ban \
    # would take every one of them with it. \
    for g in gate.b15.mod.total gate.b15.mod.declared gate.b15.mod.derivable \
             gate.b15.mod.resolved gate.b15.mod.resolve \
             gate.b15.names gate.b15.departed gate.b15.arrived gate.b15.held \
             gate.b15.exec.want gate.b15.exec.broken gate.b15.exec \
             gate.b16.2.converted gate.b16.2.converted.want gate.b16.2.held \
             gate.b16.2.held.want gate.b16.2.sites gate.b16.2.sites.want \
             gate.b16.2.convfrac gate.b16.2.holdfrac gate.b16.2.totfrac \
             gate.b16.3.moved gate.b16.3.moved.want gate.b16.3.held \
             gate.b16.3.held.want gate.b16.3.movefrac gate.b16.3.holdfrac \
             gate.b16.3.textblocks \
             gate.b16.3.exec.want gate.b16.3.exec.broken gate.b16.3.exec \
             gate.b16.4.decl gate.b16.4.nonsealed gate.b16.4.permits \
             gate.b16.4.roots gate.b16.4.roots.want \
             gate.b16.4.open gate.b16.4.open.want \
             gate.b16.4.closedfrac gate.b16.4.openfrac \
             gate.b16.5.arrow gate.b16.5.colon gate.b16.5.expr gate.b16.5.size \
             gate.b16.5.files gate.b16.5.files.want \
             gate.b17.2.departed gate.b17.2.departed.want \
             gate.b17.2.arrived gate.b17.2.arrived.want \
             gate.b17.2.held gate.b17.2.held.want \
             gate.b17.2.depfrac gate.b17.2.arrfrac gate.b17.2.holdfrac \
             gate.b17.2.patterns gate.b17.2.reftests gate.b17.2.reftests.was \
             gate.b17.2.swpatterns \
             gate.b17.3.departed gate.b17.3.departed.want gate.b17.3.ipleft \
             gate.b17.3.arrived gate.b17.3.arrived.want \
             gate.b17.3.held gate.b17.3.held.want \
             gate.b17.3.depfrac gate.b17.3.arrfrac gate.b17.3.holdfrac \
             gate.b17.3.patterns gate.b17.3.switches gate.b17.3.nullcase \
             gate.b17.3.exec.want gate.b17.3.exec.broken gate.b17.3.exec \
             gate.b17.4.departed gate.b17.4.departed.want \
             gate.b17.4.arrived gate.b17.4.arrived.want \
             gate.b17.4.held gate.b17.4.held.want \
             gate.b17.4.depfrac gate.b17.4.arrfrac gate.b17.4.holdfrac \
             gate.b17.4.firstreads gate.b17.4.firstreads.was \
             gate.b17.4.sequenced gate.b17.4.sequenced.read \
             gate.b17.4.sequenced.was \
             gate.c1 gate.c1.classes \
             gate.c2.departed gate.c2.departed.want \
             gate.c2.held gate.c2.held.want \
             gate.c2.held.ctor gate.c2.held.ctor.want \
             gate.c2.held.setacc gate.c2.held.setacc.want \
             gate.c2.depfrac gate.c2.holdfrac \
             gate.c2.sites gate.c2.sites.was \
             gate.c3.departed gate.c3.departed.want \
             gate.c3.arrived gate.c3.arrived.want \
             gate.c3.held gate.c3.held.want \
             gate.c3.depfrac gate.c3.arrfrac gate.c3.holdfrac \
             gate.c3.read gate.c3.unnamed gate.c3.unread \
             gate.c4.departed gate.c4.departed.want \
             gate.c4.arrived gate.c4.arrived.want \
             gate.c4.held gate.c4.held.want \
             gate.c4.depfrac gate.c4.arrfrac gate.c4.holdfrac \
             gate.c4.handles gate.c4.setaccessible gate.c4.setaccessible.was \
             gate.c4.exec.want gate.c4.exec.broken gate.c4.exec \
             familyA familyB familyC migrated reward; do \
        grep -qF "fact \"${g}=" /tests/test.sh; \
    done; \
    # ...and step 08's bare `gate.mod.` prefix asserted gone, over code rather \
    # than comments: the paragraph above names it in prose to explain the move. \
    if grep -v '^ *#' /tests/test.sh | grep -qF 'fact "gate.mod.'; then \
        echo "test.sh still publishes the module measurements as gate.mod.*" >&2; \
        exit 1; \
    fi; \
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
    # AND B-15 IS MISSING ITS LEG 1 FOR A DIFFERENT REASON, one rung later \
    # again, which is why it gets its own check rather than a third line in the \
    # comment above. B-13's and B-14's absent legs were class-file majors, \
    # dropped because a major that cannot move is not work. B-15's absent leg is \
    # step 08's C-1, the module-name READBACK -- and the readback still happens \
    # here, it is still measured, and `gate.b15.mod.derivable` still publishes \
    # it. What was dropped is the CHARGE for it. A `fact "B15.1.` line would mean \
    # this rung pays for the manifests a second time. \
    if grep -qF 'fact "B15.1.' /tests/test.sh; then \
        echo "B-15 has a leg 1: step 08's C-1 is reported here, not scored" >&2; \
        exit 1; \
    fi; \
    # THE CLASS-FILE MAJOR IS A FAMILY-C QUESTION AGAIN AT THIS RUNG, and that \
    # is a reversal worth writing down because the two rungs below asserted the \
    # opposite. Step 08's family C was the module graph and read no major at all; \
    # this rung's C-1 is the level itself, 61, read out of ${WORK}/scan.txt by a \
    # separate instrument over the class files the build actually produced. \
    # \
    # SO THE LEVEL IS READ TWICE AT THIS RUNG, by two gates, against two \
    # numbers, and that is deliberate rather than duplicated work: B-4 is a HELD \
    # gate reading `was=` out of level.txt -- major 55, the floor the input tree \
    # already stands on and must not fall below -- and C-1 is a MOVE gate reading \
    # `major=` -- 61, where this step has to arrive. A rung that pointed both at \
    # the same number would either forgive a regression below 55 or charge the \
    # candidate twice for one javac flag. Both reads are asserted, by their \
    # distinct fact names, and level.txt's two keys are asserted distinct and \
    # ordered in the baseline block far above. \
    grep -qF 'fact "B4.one-major-ge-${HOLD_MAJOR}=' /tests/test.sh; \
    grep -qF 'fact "C1.level-${WANT_MAJOR}=' /tests/test.sh; \
    if grep -qF 'fact "C1.major' /tests/test.sh; then \
        echo "family C publishes the major under step 07's name" >&2; \
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
    # `ok16=` was in this list at the rung below and is deliberately NOT in it \
    # here. Step 03 measured at `srcgate 6 6` with `srcgate 16 16` as a probe it \
    # did not keep, and every rung from 04 to 08 could ban the spelling because \
    # none of them compiled at 16. THIS RUNG RUNS `ok16=$(srcgate 16 16)` FOR \
    # REAL, as its published-unscored second channel, and it has to: `sealed` was \
    # a preview feature in 15 and 16 and standard in 17, so 16 is the level at \
    # which this step's own must-flip stops compiling, and a probe that answers \
    # `no` there is the evidence the flip is genuinely a 17 flip. It is the first \
    # rung on this ladder where the second channel is EXPECTED to fail. \
    for dead in 'C1.diamond' 'C2.try-with-resources' 'C3.nio-file' \
                'C4.requires-java7' 'requires-java7' '0.35*a' \
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
    # `ok17=` was in this list at the rung below and is deliberately NOT in it \
    # here, for the mirror of the reason `ok16=` left step 03's list above: 17 is \
    # the level THIS rung grades at, so `ok17=$(srcgate 17 17)` is its primary \
    # source-gate probe and the spelling is required rather than banned. `ok18=` \
    # stays banned -- nothing on this ladder compiles at 18 yet. \
    for dead in 'C1.optional' 'C2.collectors' 'C3.objects' 'C4.jtime' \
                'C5.ships-java8-library' 'C5 ships the library' \
                'c5_opt_floor=' 'c5_col_floor=' 'c5_obj_floor=' 'c5_jt_floor=' \
                'ok18='; do \
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
    # seven family-C rows and this one prints five; a leftover row would \
    # reference a variable that family C no longer assigns, and under `set -u` \
    # that is a verifier that dies between computing the reward and reporting it. \
    # \
    # `${c4}` AND `${c5}` WERE IN THIS LIST AT THE RUNG BELOW AND ARE NOT IN IT \
    # HERE, which is the second removal on this page and the one most likely to \
    # be undone by a careless merge. Step 08's family C had three legs and \
    # assigned neither variable, so banning them caught a surviving step-07 row. \
    # This rung's family C has five and assigns both -- C-4 is the sealed \
    # hierarchies and C-5 the arrow switches -- so the bans would now fail on a \
    # correct file. What replaces them is the row-and-fact count below, which \
    # asserts five against five and would catch a sixth row just as well. \
    # \
    # The last three are the compile probes rather than a gate. Step 06 measured \
    # at `srcgate 10 10` with `srcgate 8 8` as its published-unscored second \
    # channel; this rung measures at 17 with 16 as the second channel. A file \
    # that still ran `srcgate 8 8` would be compiling this tree at a level nine \
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
    # FAMILY C IS BELOW THE LAST GUARD AND INSIDE NO GUARD AT ALL, and at this \
    # rung that is a claim about four legs with two channels behind them. C-1 \
    # reads ${WORK}/scan.txt, written by arqgate.Scan out of the constant pools \
    # of everything the build published; C-2, C-3 and C-4 read the source report \
    # the hoisted srcgate compiles left in ${measured}. Neither channel sits \
    # inside the held gates' guard, and each source leg opens with its own \
    # `[ -n "${measured}" ] && [ -s "${measured}" ]` test, so a tree whose \
    # sources the gate compiler declined does not make them unmeasurable -- it \
    # makes them zero, correctly, by recording no sites to divide into 13, 4 and \
    # 7. `g < c` is asserted for exactly this: it is what says the `g` awk kept \
    # is B-14's and not one family C grew. \
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
         /^# =+ B-15$/{b15=NR} /^fact "B15.jpms-services=/{fb15=NR} \
         /^# -+ bucket helpers, shared by B-16, B-17 and C$/{bh=NR} \
         /^# =+ B-16$/{b16=NR} /^fact "B16.java17-constructs=/{fb16=NR} \
         /^# =+ B-17$/{b17=NR} /^fact "B17.java21-constructs=/{fb17=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^if \[ -z /{g=NR} \
         /^ *fact "gate.src.penalty=/{pen=NR} \
         /^# -+ C-1$/{c1=NR} /^# -+ C-2$/{c2=NR} /^# -+ C-3$/{c3=NR} \
         /^# -+ C-4$/{c4=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} /^fact "B11.java8lang-held=/{fb11=NR} \
         /^fact "B12.java8-library=/{fb12=NR} /^fact "B13.java9-10=/{fb13=NR} \
         /^fact "B14.java11-jakarta=/{fb14=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || u == 0 || w == 0 || b12e == 0 \
                   || b13 == 0 || fb13 == 0 || b14 == 0 || fb14 == 0 \
                   || b15 == 0 || fb15 == 0 || bh == 0 \
                   || b16 == 0 || fb16 == 0 || b17 == 0 || fb17 == 0 \
                   || c == 0 || fc == 0 || g == 0 || pen == 0 \
                   || c1 == 0 || c2 == 0 || c3 == 0 || c4 == 0 \
                   || v == 0 || b9 == 0 || fb10 == 0 || fb11 == 0 \
                   || fb12 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " B11=" u " B12=" w " B12e=" b12e " B13=" b13 \
                        " B13fact=" fb13 " B14=" b14 " B14fact=" fb14 \
                        " B15=" b15 " B15fact=" fb15 \
                        " bhelp=" bh " B16=" b16 " B16fact=" fb16 \
                        " B17=" b17 " B17fact=" fb17 \
                        " Cbanner=" c " C=" fc \
                        " guard=" g " penalty=" pen \
                        " C1=" c1 " C2=" c2 " C3=" c3 " C4=" c4 \
                        " B-7=" v " B-9=" b9 " B10fact=" fb10 " B11fact=" fb11 \
                        " B12fact=" fb12 " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so the held gates cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < u && u < fb11 && fb11 < w \
                     && w < b12e && b12e < fb12 && fb12 < b13 && b13 < fb13 \
                     && fb13 < b14 && b14 < fb14 && fb14 < b15 && b15 < fb15 \
                     && fb15 < bh && bh < b16 && b16 < fb16 \
                     && fb16 < b17 && b17 < fb17 \
                     && fb17 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, B-11, B-12 (jars last), B-13, B-14, B-15, the hoisted bucket helpers, B-16, B-17, family C, its score, the report"; exit 1 } \
               if (!(b14 < g && g < pen && pen < fb14)) { \
                  print "the penalty must be the last line inside B-14s guard and B-14 must be paid after it closes"; exit 1 } \
               if (!(g < c)) { \
                  print "family C opens after the last measured guard closes"; exit 1 } \
               if (!(c < c1 && c1 < c2 && c2 < c3 && c3 < c4 && c4 < fc)) { \
                  print "family Cs four legs must be read in order between its banner and its score"; exit 1 } \
               if (!(fc < fb)) { \
                  print "family B is summed before family C has finished scoring"; exit 1 } }' \
        /tests/test.sh; \
    # NO C-5 AT THIS RUNG, AND THE ASSERTION POINTS THE OTHER WAY. The awk \
    # above required a `# --- C-5` banner for as long as family C had five \
    # legs. Step 10's has four, and the weight check forty lines above reads \
    # exactly four numbers, so the requirement made a correct file fail. What \
    # replaces it is the negative: a fifth banner would be a leg nothing here \
    # weighs and nothing in the rehearsal scores. `C-5` still appears in \
    # test.sh nine times in prose, naming the legs of the rungs below -- it is \
    # the BANNER at column zero that must not exist. \
    if grep -qE '^# -+ C-5$' /tests/test.sh; then \
        echo "test.sh carries a C-5 banner; this rungs family C has four legs" >&2; \
        exit 1; \
    fi; \
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
    # THREE OF THESE FOUR ANCHORS TAKE THE FIRST MATCH AND NOT THE LAST, which \
    # is the opposite of the block above and is a change this rung forced. A \
    # gate's section banner is `# B-4  ` with TWO spaces, at column zero; but \
    # family C's C-1 opens with a paragraph that names B-4 in prose -- it has to, \
    # because this is the first rung since step 07 where the class-file level is \
    # read by two gates at once and the comment that explains which reads `was=` \
    # and which reads `major=` cannot avoid saying "B-4". That paragraph sits \
    # AFTER B-7, so a last-match anchor put b4 past b5 and the ordering assertion \
    # failed on a correct file. \
    # \
    # The two-space form is asserted unique as well as anchored, because \
    # first-match is only sound if the first match is the banner. A second \
    # `# B-4  ` line anywhere would mean two sections claim the gate, and that is \
    # a defect the ordering check cannot see from inside. \
    for banner in 'B-4' 'B-5' 'B-7'; do \
        n=$(grep -c "^# ${banner}  " /tests/test.sh); \
        if [ "${n}" != 1 ]; then \
            echo "${n} section banners for ${banner}, want exactly 1" >&2; exit 1; \
        fi; \
    done; \
    awk '/^# ---.*the inherited suite/{s=NR} \
         /^# B-4  /{if (b4 == 0) b4 = NR} /^# B-5  /{if (b5 == 0) b5 = NR} \
         /^# B-7  /{if (b7 == 0) b7 = NR} \
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
# So: a two-interface hierarchy compiled twice, once with the bridge and once
# without, and the requirement is that apisig.sh reads the SAME surface off
# both. The faces are proved different first -- one must NOT carry the member
# and the other must, with ACC_BRIDGE on it -- or the clean comparison below is
# a javac that never emitted the bridge rather than a filter that dropped it.
#
# THE BRIDGE-FREE FACE IS BUILT BY SPLIT COMPILATION AT THIS RUNG, WHERE EVERY
# RUNG BELOW IT USED A LOW CLASS-FILE VERSION, and the reason is the verifier
# image rather than anything about B-6. Steps 05 to 08 built it at
# `-source 1.6 -target 1.6` and step 09 at `-source 7 -target 7`, on the
# argument that a class file below 52 forbids a method body in an interface so
# the bridge cannot be written there at all. Neither spelling survives the move
# to JDK 21: this javac refuses both -- "Source option 7 is no longer
# supported. Use 8 or later." -- and 8 is the lowest it will accept, which is
# the first version that DOES carry the bridge. The low face as the rungs below
# built it is unreachable in this image, and the inherited block does not build
# here at all.
#
# WHAT REPLACES IT REACHES THE SAME CLASS FILE BY A DIFFERENT ROUTE. IDeep is
# compiled against a STUB IBase that declares no `self()` at all, so there is
# nothing to bridge and javac writes none; the stub is then discarded and the
# REAL IBase.class is packaged beside it. The jar that results declares
# `public interface IDeep extends IBase` with one abstract `IDeep self()` and
# no default member -- the surface every rung below measured at class-file 50
# -- while being an ordinary class-file 52. Measured on this image: javap
# prints two members for the split build and three for the straight one, the
# third being `public default IBase self();`.
#
# AND IT IS A BETTER CONTROL AND NOT ONLY A SURVIVING ONE, because it moves one
# thing. The old pair moved the class-file version AND the bridge together, so
# a filter keyed on the version rather than on the flags would have passed it
# just as cleanly. Here both faces are major 52 and the only difference between
# them is the member, which is the difference the filter exists to read. The
# two explicit major assertions below say so: both faces are 52, and 52 is
# neither 61 nor 65 -- neither the major this image writes by default nor the
# one C-1 credits -- so a bug that hard-wired this rung's constant still cannot
# pass through a block that is not about C-1 at all.
#
# NOTES 15.8 records the change and records that step 09's own block would not
# build in this image.
#
# And the paired failure, because the filter reads flags for a reason: a third
# face adds a default method the AUTHOR wrote. That is a widened surface, it
# lands in exactly the place the filter drops from, and it must still be
# charged. A filter written as "an interface method that is not abstract" would
# pass the first half of this block and go blind to the second.
RUN set -eux; \
    mkdir -p /tmp/iface/src /tmp/iface/stubsrc; cd /tmp/iface; \
    printf 'public interface IBase { IBase self(); }\n' > src/IBase.java; \
    printf 'public interface IDeep extends IBase { IDeep self(); }\n' > src/IDeep.java; \
    cp -r src src-def; \
    printf 'public interface IDeep extends IBase { IDeep self(); default void authored() { } }\n' \
        > src-def/IDeep.java; \
    printf 'public interface IBase { }\n' > stubsrc/IBase.java; \
    mkdir -p stub bnb b8 bdef jnb j8 jdef; \
    javac -nowarn --release 8 -d stub stubsrc/IBase.java; \
    javac -nowarn --release 8 -d b8 src/*.java; \
    javac -nowarn --release 8 -d bdef src-def/*.java; \
    javac -nowarn --release 8 -cp stub -d bnb src/IDeep.java; \
    cp b8/IBase.class bnb/IBase.class; \
    for f in nb 8 def; do \
        ( cd "b${f}" && jar cf "../j${f}/arquillian-iface-1.0.0-SNAPSHOT.jar" . ); \
    done; \
    test "$(od -An -tu1 -j6 -N2 bnb/IDeep.class | awk '{ print $1 * 256 + $2 }')" = 52; \
    test "$(od -An -tu1 -j6 -N2 b8/IDeep.class | awk '{ print $1 * 256 + $2 }')" = 52; \
    if javap -protected -classpath bnb IDeep | grep -qE 'public (default )?IBase self\(\);'; then \
        echo "CONTROL FAILED: the split-compiled interface already carries the bridge" >&2; \
        exit 1; \
    fi; \
    javap -protected -classpath b8 IDeep | grep -qE 'public (default )?IBase self\(\);'; \
    javap -v -protected -classpath b8 IDeep | grep -q 'ACC_BRIDGE'; \
    for f in nb 8 def; do \
        bash /tests/gates/apisig.sh "/tmp/iface/api-${f}.txt" \
            "/tmp/iface/j${f}/arquillian-iface-1.0.0-SNAPSHOT.jar"; \
    done; \
    cat /tmp/iface/api-8.txt; \
    test "$(grep -c . /tmp/iface/api-nb.txt)" = 4; \
    if ! cmp -s /tmp/iface/api-nb.txt /tmp/iface/api-8.txt; then \
        echo "B-6 reads a different public surface with the bridge than without it:" >&2; \
        diff /tmp/iface/api-nb.txt /tmp/iface/api-8.txt >&2 || true; \
        exit 1; \
    fi; \
    test "$(grep -c . /tmp/iface/api-8.txt.dropped)" = 1; \
    test "$(grep -c . /tmp/iface/api-nb.txt.dropped)" = 0; \
    grep -qE ':: public (default )?IBase self\(\);' /tmp/iface/api-8.txt.dropped; \
    test "$(grep -c . /tmp/iface/api-def.txt)" = 5; \
    test "$(diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep -c '^> ')" = 1; \
    diff /tmp/iface/api-8.txt /tmp/iface/api-def.txt | grep '^> ' \
        | grep -qE ':: public (default )?void authored\(\);'; \
    echo "step 10: B-6 reads the same interface surface with the covariant bridge and without it, and still charges a default method the author wrote"; \
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
# pin it at 60, and at THIS rung that number has to be asked for rather than
# inherited.
#
# EVERY RUNG BELOW STEP 09 LET THE FACES TAKE THE IMAGE DEFAULT. That worked
# because the image's javac was a JDK 11 and wrote 55, which was a value no gate
# in those images ever credited. Step 09's image was a JDK 17 writing 61 and
# step 10's a JDK 21 writing 65, each of them exactly what that rung's C-1
# wanted, so both pinned the faces. THIS IMAGE IS THE FIRST SINCE STEP 08 WHERE
# THE DEFAULT WOULD NOT HAVE COLLIDED: a JDK 25 writes major 69 and no gate here
# credits 69. The pin stays anyway, and it stays for the reason it was put in
# rather than for the collision it happened to avoid -- a face compiled at the
# image default states the image's javac, which is a fact about the base image
# and moves whenever the base image moves, while a face compiled `--release 16`
# states a number this file chose. The assertion under it is the same either
# way, but only one of the two keeps saying something after an image bump.
#
# So the faces are compiled `--release 16`, giving major 60: not 66, which C-1
# credits, not 65, which B-4 holds as its floor, and not 69, which this image's
# javac writes when nobody says otherwise. A hard-wired 50, 52, 55, 61, 65, 66
# or 69 all fail here. It remains the clearest statement in the image that the
# GRADING JDK and the TARGET LEVEL are two different numbers at this step -- and
# at this rung they are three, because the grading JDK is 25, the level asked
# for is 22, and the level held is 21.
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
    javac -nowarn --release 16 -cp "${H}" -d /tmp/j5s/j5 /tmp/j5s/J5Smoke.java; \
    javac -nowarn --release 16 -cp "${H}" -d /tmp/j5s/j4 /tmp/j5s/J4Smoke.java; \
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
    grep -qxF 'j5.major=J5Smoke|60'      /tmp/j5s/a.txt; \
    grep -qxF 'j5.major=J5Smoke$1|60'    /tmp/j5s/a.txt; \
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
    grep -qxF 'j5.major=J4Smoke|60'      /tmp/j5s/b.txt; \
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
    javac -nowarn --release 16 -cp "${H}" -d /tmp/j5s/lam /tmp/j5s/J5Lambda.java; \
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
    grep -qxF 'j5.major=J5Lambda|60'     /tmp/j5s/d.txt; \
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
    echo "step 11 verifier image: assertions passed"

# Both directions, because a grant that is too wide is indistinguishable from a
# correct one right up until something reads the answers. Every path 65534 must
# reach is read as 65534, and every path it must not reach is proven unreadable
# through the identical construction -- so the negative arm is a measurement and
# not an assumption about which paths happen to exist.
#
# The file list is the WHOLE of /tests/baseline -- all 51 staged files, not the
# 33 step 10 happened to name. Eighteen were added here and only four of them
# are this rung's own: priv, catchparam, varhandle and vhids. The other fourteen
# -- apinorm, chain, httpids, ipattern, modnames, reftest, sealed, seqfirst,
# strnl, svc, svcids, swarrow, swids and tbids -- were staged by steps 07
# through 10 and had never reached this list, because the list had been extended
# by reading it rather than by counting it against `ls baseline`. A staged file
# that is never probed looks exactly like a file that is not staged at all,
# which is why the count is the check and not the reading.
#
# FOUR OF THE 51 ARE READ BY THIS IMAGE AND BY NO PART OF THE SCORER --
# http.txt, junit5.txt, streamfiles.txt and varbuckets.txt. Those are the ones
# whose leak test.sh itself would never notice, so they are the ones the
# enumeration must not skip. /tests/baseline is 0700 and the directory arm below
# already proves 65534 cannot traverse it, so this was never a live leak; the
# per-file arm exists so that a single wrong mode cannot become one, and a
# per-file arm that enumerates 33 of 51 does not do that job.
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
                  /tests/baseline/httpids.txt /tests/baseline/optarr.txt \
                  /tests/baseline/modnames.txt /tests/baseline/svc.txt \
                  /tests/baseline/svcids.txt \
                  /tests/baseline/apinorm.txt /tests/baseline/ipattern.txt \
                  /tests/baseline/strnl.txt /tests/baseline/tbids.txt \
                  /tests/baseline/sealed.txt /tests/baseline/swarrow.txt \
                  /tests/baseline/reftest.txt /tests/baseline/chain.txt \
                  /tests/baseline/seqfirst.txt /tests/baseline/swids.txt \
                  /tests/baseline/priv.txt /tests/baseline/catchparam.txt \
                  /tests/baseline/varhandle.txt /tests/baseline/vhids.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/gates/build/arqsource.jar \
                  /tests/gates/source-gate.sh /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh \
                  /tests/fixtures/b89-rehearsal.sh /tests/fixtures/c/golden/report.txt \
                  /tests/fixtures/src-rehearsal.sh /tests/fixtures/pv-rehearsal.sh \
                  /tests/fixtures/mkfix11.py /tests/fixtures/c/pinned/report.txt \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Lib.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk25-step11 /tests/baseline \
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
    echo "step 11: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
