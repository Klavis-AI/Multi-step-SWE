# Verifier image for step 13, built with steps/13-static-closure/tests/ as the
# Docker build context: it cannot reach the task root, environment/, or another
# step, so everything grading needs is copied in here. Harbor skips its usual
# tests upload and runs /tests/test.sh from this image.
#
# THE RUNG IS "STATIC CLOSURE", AND IT IS THE ONLY ONE THAT MOVES NO LEVEL.
# migration-lines.md:116 gives it as static-analysis closure and names no
# release; steps/ ends at 13. The tree arrives at Java 25 and leaves at Java 25,
# and baseline/level.txt is the first and only one on this ladder whose `was=`
# equals its `major=` -- 69 both. What moves is what the COMPILER SAYS about the
# tree: 36 honest warnings under `-Xlint:all -Xlint:-deprecation` go to 0, the
# 44 @SuppressWarnings sites that hide 25 of them come down to a budget of four
# named (file, value) pairs, and the 7 open raw declarations are narrowed while
# the 4 TestNG forces stay raw and annotated. No level flip and no construct:
# three closures.
#
# THIS RUNG HAS NO MUST-FLIP, AND EVERY OTHER DECISION HANGS OFF THAT. Every
# rung from 03 to 12 had a construct that did not compile until a level moved,
# so its family C could be read off the language the tree was written in. Here
# the tree compiles before the work and after it, at the same release, and a
# candidate who does nothing at all still hands back a tree that builds, tests
# and publishes exactly like the one that did everything. The only instrument
# that tells the two apart is javac's console -- which is why GATE_LINT exists,
# why C-2 reads the console rather than the plugin report every rung below
# reads, and why the console's flags are read out of baseline/target.txt
# (`target.w.lint`) instead of being spelled a second time in test.sh.
#
# AND BECAUSE NOTHING MUST FLIP, C-1 IS LOAD-BEARING RATHER THAN DEFENSIVE. The
# cheapest way to read zero warnings is to annotate them away, and
# migration-lines.md:30 says in as many words that "a gate the agent can silence
# with a comment is not a gate". So C-1 is a BUDGET and not a count: four
# suppressions survive and are named by (file, value) in baseline/suppress.txt's
# allow rows -- two of the seed's own and two the work adds -- the other 46 of
# the seed's 48 values must go, and the 27 `unused` values must all go, because a
# suppression that silences nothing is the one kind whose removal is free. C-2 is
# then bound to C-1 by min(), so a tree that silenced its way to a clean console
# scores the clean console at whatever C-1 says the silencing was worth: for a
# tree that kept all 44 sites, zero.
#
# C-4 OWNS THE LEVEL AND B-4 STILL READS ITS OWN KEY, which is why
# baseline/level.txt carries three numbers for one gate and one for the other.
# `major=69`, `minor=0` and `classes=435` are C-4's; `was=69` is B-4's -- the
# level the tree ARRIVES at, step 12's target. That the two majors are EQUAL
# here is a property of this rung and not a licence to merge them: a gate that
# read the other's key would be reading a coincidence, and this is the only rung
# on which that read would go unpunished.
#
# minor=0 AND NOT "any minor", carried from step 12 for step 12's reason. 69
# alone is satisfiable with `--enable-preview`, which stamps 69.65535 -- a tree
# that reached a compiler flag rather than the target standard. arqgate.Scan
# reads bytes 4-5 as well as 6-7 and publishes `majorminor.<M>.<m>`,
# `majorminors.distinct` and `preview.classes`; C-4 asks for 69.0 with
# `preview.classes=0`. The read is ADDITIVE -- the `major.<N>` keys every closed
# rung's B-4 reads are unchanged -- which is what lets one gate source serve
# thirteen verifiers. NOTES 16.62 proved it in both directions: a normal compile
# reads `majorminor.69.0=839, preview.classes=0`, an `--enable-preview` compile
# reads `majorminor.69.65535=1, preview.classes=1`.
#
# AND classes=435 IS A REQUIREMENT AND NOT A STATISTIC, which is new here and is
# the whole reason C-4 multiplies rather than weighs. Static closure adds and
# removes no type. The cheapest wrong answer DELETES the two TestNG transformers
# whose raw parameters TestNG 5.14.6 forces: a tree without them has no forced
# raw declarations to hold and no `unused` suppressions to account for, and would
# read better on C-1, C-2 and C-3 than the tree that got them right. An inventory
# that multiplies stops that completely rather than partly.
#
# THE GRADING JDK DOES NOT MOVE: eclipse-temurin:25-jdk, as at step 11. That is
# the first time on this ladder the image has stayed put across a level flip,
# and it is because step 11 had already moved it: arqgate.SourceScan has to see
# `catch (X _)` as a converted site rather than as a syntax error, which needed
# a javac of at least 22, and 25 was taken then rather than 22 so that this rung
# would not have to move it again. An era directory is named for the code era it
# records and for the JDK it was recorded under; this rung's is rec/jdk25-step13,
# so both halves of the name agree with the image for the fourth time running --
# and the second half names THIS rung, because this rung re-cuts the era. Steps
# 12 and 13 both ran under jdk25; only one of them moved a transcript.
#
# THE MIGRATION MOVES EXACTLY ONE TRANSCRIPT, AND THAT IS MEASURED.
# rec/jdk25-step13 is rec/jdk25-step11 with ONE FILE REWRITTEN --
# webrunner/shape-servlet, 1 of 939 -- and authoring/ids-step13-moved.txt names
# that one id. rebuild-verifier.sh diffs the base era against what step 12
# staged, refuses to build on any `Only in` line, and requires the set of ids
# that differ to be EXACTLY the enumerated set. A re-cut with no check is how a
# rung silently inherits a recording someone moved for a different reason; a
# re-cut that enumerates nothing is indistinguishable from one that moved
# everything.
#
# WHAT MOVED IS ONE METHOD NAME. `writeObject` in a Serializable class is a name
# javac's `serial` lint reserves -- a private method by that name whose
# signature is not the hook's is exactly how a serialization hook that will
# never be called gets written -- and renaming it is the only answer that
# warning has which is not the suppression C-1 forbids. So the golden renames
# ServletTestRunner#writeObject to writeSerializedResult, the probe's shape()
# helper lists getDeclaredMethods() with privates included and sorted, and the
# new name lands in the transcript at `servlet.methods[1]`. THE INSTRUCTION
# NAMES THE REPLACEMENT for that reason: once family A records a name, the name
# is part of the answer.
#
# STEP 12 MOVED NOTHING AND THAT IS THE CONTRAST WORTH KEEPING. It changed four
# things a processor could in principle observe -- which import statements the
# sources carry, one ThreadLocal field, one `options.release`, one `jvmArgs`
# line -- and none of the four was observable in fact: imports leave no trace in
# a class file beyond constant-pool entries the code already needs, the
# ScopedValue conversion is confined to one private field of ManagerImpl, and
# the release bump moves the class-file major, which family A does not read and
# B-4 does. NOTES 16.61 ran step 11's ENTIRE verifier against step 12's golden
# and read `behavior.expected=939 behavior.produced=939 behavior.match=939
# familyA=1.000000`. A rename of a declared method is the fourth kind of change,
# and it is the kind a recording sees. NOTES 16.103.
#
# SO THE FLOOR IS 0.474681, AND IT IS THE TIGHTEST ON THE LADDER. The tree this
# rung is handed IS step 12's golden, so all nineteen inherited gates read
# 1.000000 -- B-19 included, which IS step 12's family C minus its C-1. B-4
# holds `was=69` and the seed's class files are at major 69. Family A is the one
# leg that does NOT read whole on that tree, and for the right reason: the era
# records writeSerializedResult and an untouched tree still declares
# writeObject, so 938 of 939 transcripts match and family A reads 0.998935.
#
#   0.30 x 0.998935 + 0.70 x (0.25 x 1.000000 + 0.75 x 0.0) = 0.474681
#
# against item 6's 0.5 bar, so the margin is 0.025319. Narrow, and narrow for
# the right reason: every rung whose floor was lower had something in its own
# family B reading short on its own input, which is a defect of the inheritance
# rather than a safety margin. Step 11's 0.463128 was B-6 charging its input
# tree for 42 signatures its own family C was about to remove. The 0.000319 here
# is the opposite of that -- family A telling a do-nothing tree, correctly, that
# it has not made the one edit on this rung a recording can see -- so it is
# signal and not slack. The constraint the floor imposes is stated rather than
# discovered: a do-nothing tree that collects family-C weight w in TOTAL must
# satisfy `0.474681 + 0.525 x w < 0.5`, i.e. **w < 0.048228** -- AND w IS THE
# SUM OVER EVERY LEG SUCH A TREE REACHES, NOT THE LARGEST OF THEM.
#
# AND THIS IS THE FIRST RUNG SINCE 05 WHERE THAT INEQUALITY IS NOT WHAT PRICES
# THE LEGS, because this rung has no cheap leg for it to price. Every rung from
# 05 up had at least one leg a tree could reach with a line of build script --
# step 12 had two, C-1 and C-5 at 0.02 apiece, and spent most of a page showing
# that their SUM of 0.04 fitted under a bound of 0.047619 while 0.04 each did
# not. Here the smallest weighted leg is C-1 at 0.15, and 0.525 x 0.15 =
# 0.078750 is over the bar on its own. What holds the floor down instead is that
# ALL THREE weighted legs read a hard zero on the tree the candidate is handed,
# each by a different mechanism and all three measured on the seed: C-1's extras
# half is (46-46)/46, C-2 is bound to C-1 by min() and goes to zero with it, and
# C-3's departure half is 0 of 7. The foot of test.sh argues each of the three.
# C-4 and C-5 take no weight at all, for the same arithmetic read the other way:
# both are holds, both read 1.000000 on the input tree, and a weight w on either
# would spend the 0.025319 directly. NOTES 16.64, 16.76, 16.100.
#
# THIS RUNG'S FIVE LEGS AND THE POPULATIONS EACH IS SCORED OVER. Three of the
# five have a frozen population file; the other two are scored against counts in
# baseline/target.txt, and in both cases that is a decision rather than a gap.
#
#   baseline/suppress.txt    C-1. 44 SITES OVER 26 FILES CARRYING 48 VALUES,
#                            plus four allow rows. TWO SECTIONS AND ONLY ONE IS
#                            SCORED: the census is published and graded by
#                            nothing -- a candidate who removes a different 40 of
#                            the 44 is not wrong -- while the allow rows are
#                            scored BY NAME, because a budget of "at most four
#                            suppressions anywhere" passes a tree that suppressed
#                            four of the fixable warnings and fixed none of the
#                            forced ones. NO LINE NUMBERS: the golden deletes
#                            annotation lines in 26 files and every survivor's
#                            own line moves, so rows are keyed by file and the
#                            match is on the (file, value) pair.
#   baseline/rawdecl.txt     C-3. 11 DECLARATIONS, 4 FORCED, 7 OPEN over 6
#                            buckets in 5 files, bucketed <file>#<method>. The
#                            four forced are the parameters of the two methods
#                            implementing TestNG 5.14.6's IAnnotationTransformer,
#                            which declares them raw: narrow them and the method
#                            stops overriding anything. They are the denominator
#                            of the hold half AND they are four of suppress.txt's
#                            allow rows -- the one place on this rung where two
#                            legs read the same four sites and have to agree
#                            about them. Four of the eleven declarations sit in
#                            three buckets ending in `#-`, because the
#                            declaration is a FIELD and enclosingMethod() yields
#                            `-` outside a method -- two raw fields of one class
#                            collapse into one bucket, which shrinks the
#                            denominator rather than inflating it.
#   baseline/level.txt       C-4. major=69 minor=0 classes=435 for C-4, was=69
#                            for B-4. Two gates, two keys, one file, and neither
#                            number is a literal anywhere in test.sh.
#   target.w.* (target.txt)  C-2. 36 honest warnings in six families -- 18
#                            unchecked, 11 rawtypes, 3 cast, 2 this-escape, 1
#                            serial, 1 dangling-doc-comments -- of which 6 are
#                            forced and 30 fixable, all measured by compiling the
#                            golden under target.w.lint. NO POPULATION FILE, AND
#                            THAT IS THE DECISION: a javac warning is a console
#                            line carrying a file and a LINE NUMBER, and every
#                            line number in it moves when the work is done. What
#                            can be frozen is the count, and the count is what
#                            C-2 divides. Which warnings they are is recorded as
#                            target.w.fam.* and scored by nothing.
#   target.be.* (target.txt) C-5. 160 ids in 53 JUnit XML files, the same XML
#                            B-7 parses, with 7 skipped counted as passed because
#                            that is B-7's own rule. NO POPULATION FILE for the
#                            reason C-5 had one rung ago: the ids are already
#                            named in baseline/tests.txt and baseline/testresults
#                            .txt, and a third file listing them would be a third
#                            source of truth that agrees with the other two only
#                            until someone edits one.
#
# Each weighted leg is a min() over halves stated on the same population, so
# deleting the code pays nothing and adding a construct beside it pays nothing.
# C-1 is min(budget, unused) and the budget is itself min(allow 4-of-4, extras
# 0-of-46); C-2 is min(its own fixable half, C-1); C-3 is min(narrowed 7-of-7,
# held 4-of-4), per bucket and capped, because two buckets hold two declarations
# each and a plain total would let one narrowing pay for the other. C-4 and C-5
# are multipliers and are min()s too -- inventory against level, ids against XML
# files -- and they take no weight at all, for the floor's arithmetic read the
# other way: both read 1.000000 on the tree the candidate is handed, so a weight
# w on either would spend the 0.025319 directly.
#
# STEP 12'S FAMILY C ARRIVES AS B-19, MINUS ITS FIRST LEG. The ladder's weight
# rule gives the newest inherited leg 0.12 and scales everything before it by
# 0.88. What is inherited is step 12's C-2 through C-5 -- the module import
# declarations, the subsumed single-type JDK imports, the ScopedValue conversion
# and the compact-object-headers fork argument -- under the sub-leg numbers step
# 12 gave them, B-19.2 to B-19.5, so a finding carries back to the rung that
# stated it without a translation table. There is no B-19 leg for step 12's C-1,
# because 69 is what B-4 holds here. The population files stage under their own
# names -- modimport.txt, jdkimport.txt, scopedvalue.txt -- and step 12's own
# weighting comes forward renormalised: (0.38b + 0.33c + 0.25d + 0.02e) / 0.98,
# because the 0.02 that was step 12's C-1 is the part B-4 holds instead.
#
# AND THE INHERITED POPULATIONS ARE NOT RE-CUT, WHICH AT THIS RUNG TAKES AN
# ARGUMENT RATHER THAN A MEASUREMENT. Nothing this rung's golden does moves a
# bucket: deleted annotation lines, narrowed declarations and one method rename
# in a file no inherited population keys. But this rung's own baseline/source.txt
# was cut from a tree a step-12 BLIND run produced, and that agent had extracted
# a `fireInScope` out of `ManagerImpl#fire` -- so the scan disagrees with the
# frozen ipattern.txt and catchparam.txt by exactly one bucket each. The files
# are NOT re-pointed: they are keyed on what the tree ALREADY holds, and moving
# them would forgive a refactor the reference solution never made. The cross-
# check below maps the scan back to the frozen spelling instead, and asserts both
# directions before it does. NOTES 16.98, 16.104.
#
# FOUR INSTRUMENTS, NOT ONE. The typed AST (arqgate.SourceScan) answers the
# construct questions in B-10 through B-19 and two of family C's five legs --
# C-1's @SuppressWarnings census (suppress.*) and C-3's raw declarations
# (rawdecl.*); the jars the candidate's build PUBLISHED are read twice, by
# arqgate.Scan for the class-file major, minor and count that C-4 grades and the
# major B-4 holds, and by arqgate.Modules for the Automatic-Module-Name
# manifests B-15 holds; javac's own console under GATE_LINT is C-2's whole
# population and is an instrument no other rung on this ladder reads; and the
# test run's JUnit XML is C-5's, which is the only one of the four that can say
# the tree still DOES what it did. A warning is not visible in the AST, a
# declaration is not visible in a console count, and neither is visible in a
# class file, so no gate in this file can be satisfied by moving work from one
# instrument's reach into another's.
#
# THE SOURCE GATE COMPILES AT -source 25 AND -source 24 HERE, and the control is
# expected to read `no` ON THE INPUT ALREADY, which is new and is the point.
# THIS RUNG MOVES NO LEVEL, so 24 is not a decrement of anything it did: the
# tree arrives carrying step 12's 188 `import module` declarations, which parse
# at no release below 25, and it still carries them when the work is done. `no`
# before and `no` after is therefore a statement about B-19.2 and not about
# family C, and `yes` after would say that the module imports have gone --
# which B-19.2 already charges, so scoring the control would charge the same
# absence twice. Nothing scores it in either direction; it is recorded so that a
# broken compile at 25 can be told apart from a tree that does not compile at
# all, and so that the shape of a partial answer is on the record.
#
# WHAT THIS RUNG DOES NOT ASK FOR, and each omission is a decision rather than
# an oversight. migration-lines.md:116 names four things; two of them are dead.
# NOT ERROR PRONE AND NOT OPENREWRITE AS TOOLS: neither ships in any step's
# verifier payload, `gates/build.sh`'s arqrewrite.jar branch has never been
# taken at any of the thirteen rungs, and scoring by RUNNING them would mean a
# 101 MB, 82-jar, network-free Java 17+ toolchain inside the grading image. The
# recipes the line means are `UseLambdaForFunctionalInterface` and the raw-type
# family, and their EFFECT is exactly what `-Xlint:rawtypes`, `-Xlint:unchecked`
# and SourceScan's raw-declaration records already measure with gates that do
# ship. Redirected, not dropped: score the effect. NOT `anonymous functional
# classes eliminated`: `anonymousfunctional.size=12`, identical on all three
# trees, and eleven of the twelve read `convertible=no why=members` -- bodies
# carrying fields beyond the single abstract method, for which no lambda form
# exists. The twelfth is a JUnit 5 Executable in a test file. SourceScan's own
# rule is that a site reported `convertible=no` is a site the step's arithmetic
# must not ask anyone to convert, so the population this half of the line would
# score is one. Unlike SecurityManager at step 12 this is not SPENT -- no
# earlier rung consumed it; these eleven were never convertible. NOT `-Werror`
# AS A BUILD-FILE FLAG: `-Werror` in build.gradle is a line a candidate writes
# and a scorer cannot distinguish from a tree that earned it, so the scorer
# compiles the tree itself, under its own flags, on its own classpath, and reads
# the count. What is left of line 116 -- raw types and a clean `-Xlint:all`
# console -- is 29 of the seed's 36 warnings, so the spine of the rung is the
# construct the line names by hand. NOTES 16.82, 16.91.
#
# `unzip` is asserted present below rather than assumed, and B-12e is why. That
# gate reads the constant pools of the jars the candidate's build published -- a
# different instrument on a different artifact from everything else in the file
# -- and a missing unzip makes it report "the published jars carry no class
# files", which reads as a finding about the tree rather than as a broken image.
# That would be the ladder's sixth trap sprung in the one place where nothing
# else could corroborate it. AND THE ASSERTION IS NOT INHERITED DECORATION HERE:
# eclipse-temurin:25-jdk ships without unzip, so the apt-get layer below installs
# it and this check is what proves the layer did.
#
# What this rung does NOT carry forward is step 12's own source.txt, target.txt,
# level.txt and api.txt. Every rung below step 07 staged its predecessor's pair
# beside its own; here they would both land as baseline/source.txt and
# baseline/target.txt and which one a gate read would come down to the order of a
# staging loop. What step 12 needs carried forward is carried instead as the
# target.b19.* keys inside THIS step's target.txt -- and they are RE-MEASURED ON
# THIS STEP'S SEED rather than copied, because step 12's own work rewrote the
# source this rung is handed -- while step 12's three population files stage
# under their own names.
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
# under per-step names (fixtures-13/, baseline-13/, assets/STAGED-13.txt)
# and are mapped back here, before anything reads them, so every in-image path
# below is unchanged. The build fails unless exactly one of each is present and
# it is this step's own.
RUN set -eux; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'fixtures*' | wc -l)" = 1; \
    test "$(find /tests -mindepth 1 -maxdepth 1 -name 'baseline*' | wc -l)" = 1; \
    test "$(find /tests/assets -mindepth 1 -maxdepth 1 -name 'STAGED*' | wc -l)" = 1; \
    test -d /tests/fixtures-13; \
    test -d /tests/baseline-13; \
    test -f /tests/assets/STAGED-13.txt; \
    mv /tests/fixtures-13 /tests/fixtures; \
    mv /tests/baseline-13 /tests/baseline; \
    mv /tests/assets/STAGED-13.txt /tests/assets/STAGED.txt
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
RUN echo step13 > /tests/STEP_ID

# The bytecode gate. Built here, from source, with this image's own JDK 25
# javac -- the same jar step 11's JDK 25 image builds from the same source, and
# the last one this task builds. arqgate.SourceScan is the javac plugin three
# of family C's five legs read, and three of B-18's four sub-legs, so this
# layer builds the step's principal instrument and not only B-4's and C-1's.
# gates/build.sh compiles it at the image's default level, deliberately: the
# plugin runs INSIDE this image's javac and has to match it, while the tree it
# inspects is compiled at -source 25, and again at -source 24, by
# source-gate.sh. Two levels, two purposes, and conflating them is how a plugin
# comes to be built for a compiler that will not load it.
#
# AND THE DEFAULT LEVEL IS WHY THIS LAYER HAD TO WAIT FOR THE IMAGE TO MOVE --
# ONCE, AT 17, AND NOT AGAIN AT ANY RUNG SINCE, THIS TOP ONE INCLUDED. The
# plugin step 09 shipped asks javac about Modifier.SEALED,
# ClassTree.getPermitsClause() and InstanceOfTree.getPattern(); none of those is
# in JDK 11's com.sun.source, so that source does not compile in step 08's image
# at all. NOTES 14.5 calls that the one-way door and puts it at 17. THIS RUNG IS
# THE ONE THAT HAD A REASON TO MOVE IT AND DID NOT: the first of its four new
# readings is of a com.sun.source method that did not exist before JDK 25.
#
#   modimport    every `import module M;` declaration -- C-2's arrival half, and
#                the whole of it. ImportTree.isModule() arrived in JDK 25 with
#                JEP 511, and this one source file is compiled inside every
#                verifier image from step 03 up, whose JDKs are 11, 17, 21 and
#                25. A direct call would make the entire scanner uncompilable on
#                three of those four for the sake of a block only the top rung
#                reads, so moduleImport() invokes the method REFLECTIVELY and
#                answers false when it is absent. False is the TRUE answer
#                there, not a degraded one: a javac with no isModule() is a
#                javac that cannot parse a module import, so no tree it compiled
#                has one. And no syntactic fallback either -- once parsed,
#                `import module M;` and an ordinary import differ only in a
#                flag, so a guess would have to decide that a dotted name is not
#                a type, and it would decide wrongly on the day someone writes a
#                type whose package looks like a module name.
#   jdkimport    every NON-module import whose qualified name starts with a
#                package the running JDK actually EXPORTS, classified by the
#                longest such prefix, carrying kind=type/nested/static/ondemand
#                and ambig=<n> -- the number of exported packages declaring a
#                public type of that simple name. C-3 reads it from both ends,
#                452 departures against 43 holds, and the holds are the three
#                shapes a module import cannot supply unaided: ambig>=2 (25
#                rows), a static member (16), a nested type (2). An import of
#                org.testng or org.jboss is not a JDK import and is SKIPPED
#                rather than classified, which is what keeps C-3's denominator
#                at the 495 rows a module import could have spoken to, instead
#                of at every import declaration 171 files happen to carry.
#   scopedvalue  C-4's two halves, published as two populations rather than as
#   threadlocal  one net figure, because the conversion this rung asks for is a
#                ThreadLocal field that left and a ScopedValue that arrived in
#                the same file -- and a single number cannot say that both
#                happened. Eight fields in five files, of which exactly one
#                converts and seven are held for reasons the baseline names.
#
# C-5 has no reading here and can have none: `-XX:+UseCompactObjectHeaders` is
# an argument to a JVM, never to javac, and does not appear in a typed AST at
# any level. It reads the fourth channel instead, which is the head of this file
# under THREE INSTRUMENTS, NOT ONE.
#
# ON THE BYTECODE SIDE arqgate.Scan gains a reading and loses none. It still
# prints `major.<M>` and `majors.distinct` exactly as every rung below read
# them, and now prints `majorminor.<M>.<m>`, `majorminors.distinct` and
# `preview.classes` beside them. ADDITIVE ON PURPOSE, and it is why a rung that
# moves the class-file level re-cuts no inherited population: B-4 holds `was=66`
# against rows whose shape has not changed, while C-1 is the only leg in the
# file permitted to read the minor. A Scan that had REPLACED `major.<M>` with
# the pair would have rewritten eleven rungs of baseline in one edit, silently,
# and every inherited hold would have failed at once for a reason no message
# would have named.
#
# So the same SourceScan.java still builds in step 09's image, and a plugin
# built here is one step 09's javac could also have built. The door is still a
# door -- a re-stage of step 08 or below would hand it a plugin its own javac
# cannot build -- authoring/rebuild-verifier.sh refuses to re-stage a step whose
# frozen SourceScan.java differs from the authoring master for that reason, and
# NOTES 14.2 records it as a known-open hazard rather than a fixed one.
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
            *) echo "RULE-6 VIOLATION: step 13 grades on JDK 25 only, found ${v} at ${j}" >&2; exit 1;; \
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
    grep -q '^staged.era=jdk25-step13$' /tests/assets/STAGED.txt; \
    grep -q '^staged.orderdep=1$'      /tests/assets/STAGED.txt; \
    test "$(grep -c '^as_untrusted timeout -s KILL' /tests/runner/score.sh)" = 2; \
    test "$(grep -vc '^#' /tests/baseline/orderdep.txt)" = 1; \
    grep -q '^staged.ids=939$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.rec=939$'         /tests/assets/STAGED.txt; \
    grep -q '^staged.problib.jars=80$' /tests/assets/STAGED.txt; \
    test "$(grep -c . /tests/assets/ids.txt)" = 939; \
    test "$(find /tests/assets/rec/jdk25-step13 -name '*.txt' | wc -l)" = 939; \
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
    # B-6's surface is STEP 11'S, and this rung leaves it exactly there: 1105 \
    # signatures over 272 types, down from 1147 over 279 when step 11's own \
    # C-2 withdrew the seven SecurityActions$GetTcclAction enums along with \
    # the doPrivileged calls they wrapped. NOTHING STEP 12 ASKS FOR MOVES IT. \
    # Deleting 452 import declarations and inserting 188 module imports \
    # changes no signature -- imports are not in a class file -- and the one \
    # ThreadLocal that converts is a private field of ManagerImpl. So \
    # rebuild-verifier.sh stages baseline-step11-api.txt verbatim, and this is \
    # where either slip would be caught: a fall back to step 10's 1147, or a \
    # re-cut on this rung's golden that nobody asked for. \
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
    grep -q '^staged.baseline.files=56$' /tests/assets/STAGED.txt; \
    test "$(ls /tests/baseline | grep -c '\.txt$')" = 56; \
    grep -q '^staged.fixtures=2357$'    /tests/assets/STAGED.txt; \
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
    test "$(find /tests/baseline -type f | wc -l)" = 56; \
    # 2357, up from step 12's 1662, and unlike that increase it is THREE \
    # things rather than one: twenty more configurations, three more files in \
    # every configuration that was already here, and one file in the smoke \
    # tree. \
    # \
    #   1662 + 276 + 418 + 1 = 2357 \
    # \
    # 276 is cases.txt, classfiles.txt and xml.txt arriving in each of the \
    # ninety-two configurations the rung below shipped; 418 is the twenty new \
    # ones, eighteen at twenty-one files apiece and R18 and R19 at twenty; \
    # the 1 is smoke/mod/src/main/java/smoke/Raw.java, the only fixture file \
    # anywhere outside c/ that this rung adds. The arithmetic is written out \
    # rather than the total alone, because a fixture tree that lost a whole \
    # configuration and gained twenty-one loose files elsewhere would satisfy \
    # a count. \
    # \
    #   8   at the top: the four rehearsals -- b89, c, pv and src -- plus \
    #       mkfix.sh, mkfix13.py and the two j5 answer files, j5-pinned.txt \
    #       and j5-golden.txt. No __pycache__: mkfix13.py is imported by the \
    #       generator as well as run, and the .pyc it leaves behind staged \
    #       ONCE AT THE RUNG BELOW, the first time that build was run against \
    #       a freshly generated tree. stage.sh prunes it, and the two \
    #       assertions below stay so that the prune is checked rather than \
    #       trusted. \
    #   9   cl/: step 02's self-stop cases, unchanged since it. \
    #   4   smoke/: the source-gate rehearsal's tree -- Smoke.java, Lib.java \
    #       and Mod.java carried up from the rung below, plus Raw.java, new \
    #       here. This rung moves no source level and introduces no \
    #       construct, so the three inherited files are not touched; the \
    #       fourth is here because C-3's channel, rawdecl, is a record block \
    #       this rehearsal had no population for at all, and nine of its \
    #       eleven rows are in that one file. It is a FILE rather than a \
    #       section of Smoke.java because flatten() writes Smoke.java and \
    #       Lib.java BY NAME: a file it has never heard of is off all eight \
    #       cut trees for free, which is how a record block was added without \
    #       adding a cut, and why every figure below the cut banner in \
    #       src-rehearsal.sh is still the figure step 12 measured. \
    # 2336  c/: 112 configurations -- the two ends, golden and pinned, and a \
    #       hundred and ten rows. A hundred and seven of the 112 hold twelve \
    #       published jars and NINE files of their own: answers.txt, \
    #       behavior.summary, cases.txt, classfiles.txt, jvmargs.txt, \
    #       report.txt, scan.txt, why.txt and xml.txt. Three of the nine are \
    #       new at this rung and each is one half of one leg: \
    #         classfiles.txt  C-4's INVENTORY half, and it is a single \
    #                         number. C-4 is minf(invfrac, levelfrac) and the \
    #                         level half was step 12's C-1, so the inventory \
    #                         is the half with no fixture anywhere below this \
    #                         rung. `major.69=730` over 400 class files reads \
    #                         FULL on the level and says nothing about a tree \
    #                         that deleted the code rather than migrating it. \
    #         cases.txt       C-5's FIRST half: one row per behaviour id with \
    #                         pass or fail. The denominator is target.be.ids, \
    #                         measured off the golden, so a run that passes \
    #                         everything it chose to run is not the same as \
    #                         one that passed. \
    #         xml.txt         C-5's SECOND half: the TEST-*.xml the build \
    #                         wrote, spelt as paths and counted with \
    #                         `grep -c .` rather than opened. Two halves and \
    #                         not one because a suite can pass every case it \
    #                         reports while reporting a third of them. \
    #       The twenty this rung adds are R1 through R20, its five legs of \
    #       family C in order. They are ONE series and not five because the \
    #       legs are chained -- C-2 is capped by C-1, and C-4 and C-5 \
    #       multiply what C-1..C-3 sum to -- so a row that moves one leg is \
    #       also the row that says what the chain does to the others: \
    #         R1..R5   C-1, the suppression budget. R2 is the census \
    #                  untouched, all 48 pairs, and it reads 0.000000 on C-1 \
    #                  AND on C-2, which is the cap doing its work. R1 is one \
    #                  survivor outside the allow list, R3 is one of the four \
    #                  forced ones removed, R4 is the right count of the \
    #                  wrong four, R5 is eighteen of the twenty-seven \
    #                  `unused` annotations left. \
    #         R6..R8   C-2, javac's own warning count. R6 leaves three \
    #                  fixable warnings, R7 leaves all thirty, and R8 is the \
    #                  tree that compiles under the default release: the \
    #                  lint block is never reached, and the leg reads the \
    #                  shell's `0` rather than awk's `0.000000`. Those are \
    #                  two different facts about a scorer, and the rehearsal \
    #                  compares this column as a STRING so that they stay \
    #                  two. \
    #         R9..R13  C-3, the raw declarations. R9 leaves three of the \
    #                  seven open ones raw and R10 leaves all seven; R11 and \
    #                  R12 narrow one and then both of the FORCED \
    #                  declarations, which must not narrow because TestNG \
    #                  5.14.6 declares those parameters raw; R13 deletes the \
    #                  two transformers outright, which is the tree that \
    #                  would score the held half by having nothing left to \
    #                  hold. \
    #         R14..R15 C-4, the inventory. R14 compiles 400 class files where \
    #                  the seed compiled 435 and reads 0.919540; R15 compiles \
    #                  500 and reads 1.000000. R15 is the control that says \
    #                  the leg is a floor and not an equality -- a tree is \
    #                  not penalised for compiling more than it inherited. \
    #         R16..R20 C-5, the tests. R16 fails 13 of the 160 ids, R17 \
    #                  passes every id that ran and writes thirteen XMLs \
    #                  short, R20 passes 200 where 160 were frozen and is \
    #                  capped to 1.000000 for R15's reason. \
    #       FIVE CONFIGURATIONS ARE ABSENCES, of five different files, with \
    #       four different verdicts. K20 holds the nine files and NO jars -- \
    #       the tree where nothing resolves, where `gate.cp=no-jars` and \
    #       `gate.b15.mod=no-module-jars` are the sentinels. L2 holds the \
    #       twelve jars and only EIGHT files: no scan.txt, so \
    #       `gate.c4=no-scan` and C-4 is 0.000000 -- and because C-4 is a \
    #       MULTIPLIER here where the same channel was 0.02 of a sum one \
    #       rung down, L2 fell from familyC 0.980000 to 0.000000 without a \
    #       line of its own being edited. Q20 is missing jvmargs.txt, which \
    #       is B-19.5 at this rung and not family C at all, so it reads B-19 \
    #       0.979592 and familyC 1.000000 -- the same absence, re-scored by \
    #       the inheritance. R18 is missing cases.txt and R19 xml.txt, and \
    #       both read C-5 0.000000 with no sentinel fact of their own: C-5 \
    #       has no `no-tests` spelling, because the two halves fail \
    #       separately and one sentinel would say which file was gone and \
    #       not which half went unanswered. \
    #       107*21 + 9 + 4*20 = 2336. \
    test "$(find /tests/fixtures -type f | wc -l)" = 2357; \
    test "$(find /tests/fixtures -maxdepth 1 -type f | wc -l)" = 8; \
    test -f /tests/fixtures/mkfix13.py; \
    test ! -e /tests/fixtures/mkfix12.py; \
    test ! -e /tests/fixtures/__pycache__; \
    test "$(find /tests/fixtures -name '*.pyc' | wc -l)" = 0; \
    test "$(find /tests/fixtures/cl -type f | wc -l)" = 9; \
    test "$(find /tests/fixtures/smoke -type f | wc -l)" = 4; \
    test -f /tests/fixtures/smoke/mod/src/main/java/smoke/Mod.java; \
    test -f /tests/fixtures/smoke/mod/src/main/java/smoke/Raw.java; \
    test "$(find /tests/fixtures/c -type f | wc -l)" = 2336; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d | wc -l)" = 112; \
    test "$(find /tests/fixtures/c -name '*.jar' | wc -l)" = 1332; \
    test "$(find /tests/fixtures/c/K20 -name '*.jar' | wc -l)" = 0; \
    test ! -e /tests/fixtures/c/L2/scan.txt; \
    test ! -e /tests/fixtures/c/Q20/jvmargs.txt; \
    test ! -e /tests/fixtures/c/R18/cases.txt; \
    test ! -e /tests/fixtures/c/R19/xml.txt; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'scan.txt' | wc -l)" = 111; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'jvmargs.txt' | wc -l)" = 111; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'cases.txt' | wc -l)" = 111; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'xml.txt' | wc -l)" = 111; \
    test "$(find /tests/fixtures/c -mindepth 2 -maxdepth 2 -name 'classfiles.txt' | wc -l)" = 112; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'K*' | wc -l)" = 20; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'L*' | wc -l)" = 14; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'M*' | wc -l)" = 6; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'N*' | wc -l)" = 15; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'P*' | wc -l)" = 15; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'Q*' | wc -l)" = 20; \
    test "$(find /tests/fixtures/c -mindepth 1 -maxdepth 1 -type d -name 'R*' | wc -l)" = 20; \
    for cfg in $(ls /tests/fixtures/c); do \
        n=$(find "/tests/fixtures/c/${cfg}" -maxdepth 1 -type f | wc -l); \
        case "${cfg}" in L2|Q20|R18|R19) want=8 ;; *) want=9 ;; esac; \
        if [ "${n}" != "${want}" ]; then \
            echo "fixtures/c/${cfg} holds ${n} top-level files, not the ${want} it should" >&2; \
            exit 1; \
        fi; \
    done; \
    # And the fifty-six by NAME, because a count is satisfied by any \
    # fifty-six files and this rung's recipe does two different things to \
    # reach the number. \
    # \
    # It ADDS two, one per leg of family C that needs a population of its \
    # own -- and that is two and not five because the other three legs read \
    # no set at all. C-2's denominator is a single count, target.w.fixable, \
    # and its numerator is javac's own warning total, so a file naming sites \
    # would be a second place for the same fact to live; C-4 reads level.txt, \
    # which the rung below staged and this one replaces, and one number, \
    # target.level.classes; C-5 reads two, target.be.ids and target.be.xmls. \
    #   suppress.txt  C-1: the census and the allow list, and they are two \
    #                 different kinds of row in one file on purpose. The \
    #                 census is 26 files, 44 sites, 48 values -- published \
    #                 and scored by nothing, because a candidate who removes \
    #                 a different 40 of the 44 is not wrong. The allow rows \
    #                 ARE scored, BY NAME: a budget of 4 spent on the wrong \
    #                 4 is the attack this leg exists for, and a budget \
    #                 checked as a count would pay it. No line numbers \
    #                 anywhere in the file -- the golden deletes annotation \
    #                 lines in 26 files and every survivor's own line moves. \
    #   rawdecl.txt   C-3: the raw declarations in capsum() shape. 6 `open` \
    #                 rows summing 7 that must narrow and 2 `forced` rows \
    #                 summing 4 that must NOT, because TestNG 5.14.6 \
    #                 declares those parameters raw and a tree that narrowed \
    #                 them would fail to compile against the pinned jar. \
    #                 Buckets and not files: capsum() caps each bucket at \
    #                 what the population froze for it, so two narrowings in \
    #                 one transformer and none in the other must not score \
    #                 as one in each. \
    # \
    # And it REPLACES three the rung below already staged, which are exactly \
    # the three this rung cuts for itself. source.txt because it must be step \
    # 12's OUTPUT measured by the gate that ships here, and step 12's frozen \
    # source-gate.sh has no GATE_LINT hook at all, so its source.txt carries \
    # no javac.warnings key and C-2 would read this rung's central figure as \
    # an absence; target.txt because it carries step 12's denominators \
    # forward under target.b19.* spellings AND because this rung's own su, \
    # rd, w and be keys had to be measured on a tree where step 12's work is \
    # already done; and level.txt because its three keys are now all the same \
    # number -- `major=69`, `minor=0` and `was=69`, where the rung below \
    # wrote was=66. That is what makes C-4 a multiplier rather than a leg: \
    # there is no level to gain here, only a level to not lose. \
    # \
    # WHAT IS NEW IN KIND AT THIS RUNG IS WHAT IS NOT IN EITHER LIST. Every \
    # rung from 09 on has had to RE-CUT at least one inherited population \
    # because its own work deleted part of what that population names -- step \
    # 11 re-cut three and found a fourth by calibration, and the note above \
    # its own copy of this loop is about accessible.txt and ipattern.txt \
    # going stale. Step 12 re-cut none and said so as a measurement. This \
    # rung re-cuts none either, and the second half of that is a measurement \
    # too -- one that took an oracle run to get right. \
    # \
    # ITS OWN WORK MOVES NOTHING, and that half is the same measurement step \
    # 12 made. The golden edits 32 files and its edits are deletions of \
    # annotation lines and narrowings of declarations: it adds no file, \
    # removes no file, and neither adds nor splits a method, so every \
    # population keyed by <file> or <file>#<method> is untouched. The one \
    # population that could have moved is jdkimport.txt, which names import \
    # statements -- and this rung touches no import line at all. \
    # \
    # THE TREE IT IS AUTHORED FROM MOVES TWO, AND THE TREE IT IS HANDED DOES \
    # NOT, WHICH ARE DIFFERENT TREES. Every rung since 09 has been AUTHORED \
    # from the rung below's BLIND workspace, not its golden, and step 12's \
    # blind agent did more than convert a ThreadLocal to a ScopedValue: it \
    # EXTRACTED A METHOD doing it. In that tree ManagerImpl#fire binds the \
    # scope and delegates to a new private ManagerImpl#fireInScope, and the \
    # two buckets keyed on the old method name travelled with the body -- \
    # B-16.2's `skip` site in ipattern.txt and B-18.3's read catch parameter \
    # in catchparam.txt. Step 12's own run priced that at gate.b16.2.held=6 \
    # of 7 and gate.b18.3.held=50 of 51, the whole of the 0.000628 between \
    # its 0.999372 and a clean sweep. \
    # \
    # BUT A SCORED RUN IS NOT HANDED THAT TREE. Harbor carries the workspace \
    # forward from rung to rung, so step 13's input is the candidate's own \
    # step 12 -- and in the arm that has to read 1.0, that is \
    # steps/12-java25/solution/solve.sh's output. That solve.sh extracts \
    # nothing. It binds in place, `ScopedValue.where(handledThrowables, new \
    # HashSet<>()).run(() -> fire(event, nonManagedObserver))`, so `fire` \
    # keeps its body and `fireInScope` never exists. A population re-pointed \
    # at `fireInScope` would be charging the REFERENCE SOLUTION for a \
    # refactor only a blind agent made -- which is what the first step-13 \
    # oracle run measured: gate.b16.2.held=6 of 7 and gate.b18.3.held=48 of \
    # 51 against a ManagerImpl.java that is the golden's (NOTES 16.98). \
    # \
    # SO BOTH FILES STAGE AS STEP 11 CUT THEM. The B-13 rule stated at step \
    # 08 -- a hold gate cannot charge this rung for the last one's miss -- is \
    # satisfied by the lineage rather than by an edit: along the reference \
    # chain there is no miss to carry. A candidate who DID extract the method \
    # at step 12 paid at step 12 and pays again here, because the bucket \
    # names a method their tree no longer has; that is what a population \
    # frozen on <file>#<method> means, and every rung from 09 on has offered \
    # the same bargain. \
    # \
    # EVERYTHING ELSE STAGES VERBATIM: modimport, jdkimport and scopedvalue \
    # as step 12 cut them, priv, varhandle and vhids as step 11 cut them, and \
    # accessible, lambdafiles, optarr and api as step 11 re-cut them. \
    # \
    # A replacement is invisible to a count -- fifty-one plus five is \
    # fifty-six whether or not source.txt is the one this step meant -- \
    # which is why the names are listed and not just tallied. \
    # \
    # FIFTY-TWO OF THE FIFTY-SIX ARE RECORD POPULATIONS rather than key=value \
    # tallies; only junit5.txt, level.txt, target.txt and tests.txt are \
    # facts, and the other fifty-two are sets the scorer reads row by row. \
    # That is the failure this loop exists for: a population that failed to \
    # stage does not make the scorer fail -- wantbuckets() over a missing \
    # file yields an empty want, frac() over an empty denominator yields the \
    # scorer's zero-denominator answer, and the gate pays the tree for a \
    # migration nobody checked. suppress.txt is the worst of the fifty-two \
    # to lose, worse than the set equality the rung below worried about: an \
    # empty allow list does not read as a leg that failed, it reads as a leg \
    # that permits nothing -- and since every tree that suppresses anything \
    # then scores 0.000000 on C-1 AND on the C-2 it caps, the loss would look \
    # like a hard task rather than a broken one. Every one of the fifty-six \
    # is asserted non-empty here by name. \
    for b in accessible api apinorm artifacts boxedctor builds catchparam \
             chain classnames collectmethods compat http httpids ipattern \
             jdkimport junit5 lambdafiles level modimport modnames \
             newinstance nioids nshold nsmove optarr optbuckets optops \
             orderdep priv rawdecl reftest samifaces scopedvalue sealed \
             seqfirst sinks source streamfiles strnl suppress svc svcids \
             swarrow swids target tbids testng-cased testresults tests \
             timesites toarray valuetypes varbuckets varhandle vhids views; do \
        if [ ! -s "/tests/baseline/${b}.txt" ]; then \
            echo "baseline/${b}.txt did not stage, or staged empty" >&2; exit 1; \
        fi; \
    done; \
    # ...and nothing else, so that a name this list does not know about \
    # cannot arrive unnoticed -- which is exactly how a stale copy of step \
    # 12's source.txt, target.txt or level.txt would arrive if a future edit \
    # dropped this rung's five own cuts from the staging recipe and left the \
    # inherited files in place. The count would not move: the recipe stages \
    # step 12's copies of all three under the same names first, and the five \
    # step-13 files are laid down over them by a LATER loop in the same \
    # script. Order is the whole mechanism, so a reordering is silent. \
    ls /tests/baseline | sed 's/\.txt$//' | LC_ALL=C sort > /tmp/blist.txt; \
    printf '%s\n' accessible api apinorm artifacts boxedctor builds catchparam \
             chain classnames collectmethods compat http httpids ipattern \
             jdkimport junit5 lambdafiles level modimport modnames \
             newinstance nioids nshold nsmove optarr optbuckets optops \
             orderdep priv rawdecl reftest samifaces scopedvalue sealed \
             seqfirst sinks source streamfiles strnl suppress svc svcids \
             swarrow swids target tbids testng-cased testresults tests \
             timesites toarray valuetypes varbuckets varhandle vhids views \
             | LC_ALL=C sort > /tmp/bwant.txt; \
    if ! cmp -s /tmp/blist.txt /tmp/bwant.txt; then \
        echo "baseline/ is not the fifty-six files this step stages:" >&2; \
        diff /tmp/bwant.txt /tmp/blist.txt >&2 || true; exit 1; \
    fi; \
    rm -f /tmp/blist.txt /tmp/bwant.txt; \
    # ...and the two files that COULD have been re-pointed were not. This is \
    # the assertion that changed direction after the first oracle run, and it \
    # is kept rather than deleted because the thing it now forbids is the \
    # thing the tree was shipped with once: ipattern.txt's B-16.2 `skip` site \
    # and catchparam.txt's B-18.3 read clause must still name \
    # `ManagerImpl#fire`, the method the REFERENCE step 12 leaves the body \
    # in, and must not name the `fireInScope` a blind agent split out of it. \
    # Neither spelling moves a count -- 24 sites over 21 buckets either way, \
    # 89 clauses either way -- so the method name in one row of each is the \
    # only thing there is to assert. Getting it wrong costs B-16.2 a seventh \
    # of its hold and B-18.3 a fifty-first, silently, on every arm including \
    # the oracle. The counts are taken over the ROWS -- `grep -v '^#'` first \
    # -- because the headers discuss both names in prose and a file must not \
    # assert against its own commentary. \
    grep -v '^#' /tests/baseline/ipattern.txt > /tmp/ipatrows.txt; \
    test "$(grep -c '#fire 1$' /tmp/ipatrows.txt)" = 1; \
    test "$(grep -c '#fireInScope 1$' /tmp/ipatrows.txt)" = 0 \
        || { echo "baseline/ipattern.txt was re-pointed at fireInScope, which the reference tree has no method called" >&2; exit 1; }; \
    grep -v '^#' /tests/baseline/catchparam.txt > /tmp/cparows.txt; \
    test "$(grep -c ' in=fire$' /tmp/cparows.txt)" = 1; \
    test "$(grep -c ' in=fireInScope$' /tmp/cparows.txt)" = 0 \
        || { echo "baseline/catchparam.txt was re-pointed at fireInScope, which the reference tree has no method called" >&2; exit 1; }; \
    rm -f /tmp/ipatrows.txt /tmp/cparows.txt; \
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

# The twelve blocks that read the typed AST, the published jars, the class-file
# scan and the build's own answer about its test forks, on a hundred and twelve
# trees.
#
# fixtures/c-rehearsal.sh lifts the hoisted source-AST block, B-10, B-11, B-12,
# B-13, B-14, B-15, B-16, B-17, B-18, B-19 and the whole of family C out of the
# SHIPPED test.sh by their own banners, composes them in file order behind a
# srcgate() that hands them a fixture's report instead of compiling anything
# and a run_unprivileged() that hands them a fixture's recorded probe output
# instead of running a build, and runs them once per fixture. Two of the
# hundred and twelve are measurements -- `pinned`, the tree this step is
# actually handed, which is step 12's BLIND round-2 workspace, and `golden`,
# this step's own solution -- and a hundred and ten are stated edits of a
# measurement.
#
# TWELVE BLOCKS AND NOT ELEVEN. The compiles were hoisted above family B at
# step 04 so B-10 could read them, step 05 put B-11 beside it, step 06 added
# B-12, step 07 added B-13, step 08 added B-14, step 09 added B-15, step 10
# added B-16, step 11 added B-17, step 12 added B-18 and this rung adds B-19:
# the counters are read in one place and spent in ELEVEN others. The extraction
# asserts all twelve -- and the thirteenth piece described below -- are disjoint
# and in that order in the file, which is not decoration: a hoist that silently
# un-hoisted would leave a held gate reading variables that do not exist yet,
# and under `set -u` that is a dead verifier rather than a wrong number. It also
# asserts srcgate is called three times in the source block and not once
# anywhere else -- 25, 24 and def, this rung's three tags -- and that no block
# anywhere runs javac or docker.
#
# TWO PROGRAMS ARE ALLOWED AND EACH IN EXACTLY ONE PLACE: java in B-15 and
# nowhere else, on a line that also names arqgate.Modules, which has been the
# rule since step 08; and ./gradlew in B-19 and nowhere else, exactly once, on
# a line that also names arqTestJvmArgs. THE SECOND RULE MOVED WITH ITS LEG
# THIS RUNG, and the move is the whole of the change. It was written one step
# ago to say `in family C`, because the probe was step 12's C-5 and a leg whose
# content is what the BUILD hands a fork cannot be written without a build.
# Here the probe is B-19.5, so the sweep says a HELD block may start one and
# the live family may not -- and family C at this rung reads no process output
# at all, on any of its five legs. A sweep carried across unchanged would have
# forbidden the build in the only block that still makes it and permitted it in
# the family that no longer does, which is the same rule pointed exactly
# backwards and would have passed every fixture.
#
# AND A THIRTEENTH PIECE THAT IS NOT A BLOCK. NINE bucket helpers -- cbuckets,
# fcount, capsum, capwant, cnames, pbuckets, ptot, psum and pdep -- sit in the
# gap between B-15's last fact and B-16's banner. Step 09 kept the first four
# INSIDE family C, so lifting family C lifted them and there was nothing to
# notice; step 10 hoisted them and added cnames; step 11 added the four `p`
# helpers for the bucket arithmetic its family C needed. All nine are still
# there and no block covers that gap.
#
# FOUR BLOCKS READ THEM AND ONE OF THE FOUR IS THIS RUNG'S, which reverses what
# the rung below had to say here. B-16 IS step 09's family C and reads four
# (cbuckets, fcount, capsum, capwant); B-17 IS step 10's and reads two
# (cbuckets, cnames); B-18 IS step 11's and reads six (cbuckets, fcount,
# pbuckets, ptot, psum, pdep); and THIS RUNG'S OWN FAMILY C READS THREE
# (cbuckets, capsum, capwant), because C-3 caps raw declarations per
# `file#method` and that is the key cbuckets() already builds and the cap
# capsum() and capwant() already apply. B-19 reads none: it is step 12's family
# C, which read none of the nine when it was live either. Only cbuckets() is
# read by all four, and one definition beats four that drift.
#
# THE COUNT IS ASSERTED AND SO IS THE READERSHIP, because the two can come
# apart in opposite directions. Nine definitions with a B-19 caller would mean
# an inherited family had grown a per-method leg on its way across; eight
# definitions with the same four readers would mean a helper was deleted out
# from under a call. Neither shows up in a total. ptot() IS A ONE-LINER AND THE
# COUNT HAS TO KNOW IT: eight of the nine are `name() {` on a line of their own
# and ptot() is `ptot() { awk ...; }` entire, so a count anchored on
# `^[a-z][a-z0-9_]*() {$` reads eight and calls a correct file wrong. The regex
# accepts either shape and a name loop under it says which nine.
#
# Composed WITHOUT them, under `set -uo pipefail` with no `-e`, every call is
# `command not found`, no ${WORK}/cnt-*.txt, nam-*.txt or set-*.txt is ever
# written, and every leg that reads those files reads 0. That is the shape step
# 10's first run of this rehearsal printed -- B16 0.400000 and familyC 0.100000
# on all fifty-five rows, one missing `cat` wearing the costume of fifty-five
# broken gates, and the reason the sixth trap says verify the instrument before
# the finding. (Both figures are as they were printed then, at a rung whose
# family C did read the helpers and whose C-1 still carried 0.10.) AT THIS RUNG
# THE SAME OMISSION HAS A DIFFERENT SILHOUETTE: it would drop B16, B17, B18 and
# familyC together and leave B19 alone. A reader who remembered the shape and
# not the cause would look in the wrong family.
#
# B-19 IS STEP 12's FAMILY C MINUS ITS C-1, renormalised over the 0.98 that is
# left -- 0.38, 0.33, 0.25 and 0.02 -- and this is the sixth rung in a row where
# a leg stayed behind, for the opposite reason to the five before it. At steps
# 09 through 12 the dropped leg was the one the LIVE rung had to flip, and
# carrying it forward would have graded one artefact against two contradictory
# thresholds. Step 12's C-1 was the class-file major and step 12 HAD to reach
# 69. This rung only has to HOLD 69 -- HOLD_MAJOR and WANT_MAJOR are the same
# number here -- so the leg did come across, as family C's own C-4, where it
# MULTIPLIES the family instead of adding two hundredths to it. Leaving a copy
# inside B-19 as well would grade one artefact twice: once as a term that is
# already full, and once as the gate on everything else.
#
# K17 IS THE ROW WHERE THAT IS VISIBLE AS ARITHMETIC. Nothing on it compiles,
# so C-1, C-2 and C-3 are all zero and familyC is 0.000000 -- off an INTACT
# class-file scan and an INTACT probe, with C-4 and C-5 both reading 1.000000,
# because a multiplier cannot lift a sum of zeros. B-19 on the same row reads
# 0.020408, which is 0.02 over 0.98: its probe leg is the one thing in either
# family that a tree nobody can compile still earns. Transcribing K17 from the
# rung below would have carried step 12's C-1 across as a full 1.000000 and
# left C-4 sitting at the literal `0` -- four cells wrong in one row, in both
# directions at once, and a family C of 0.040000 where this rung reads
# 0.000000.
#
# SEVEN FAMILIES ARE STACKED IN THE HELD HALF NOW -- step 06's as B-13, step
# 07's as B-14, step 08's as B-15, step 09's as B-16, step 10's as B-17, step
# 11's as B-18 and step 12's as B-19 -- and the count is the ninth trap's whole
# subject. Every one of them is scored on every row of the table, including
# rows written seven rungs ago, because a leg that would quietly become
# unpassable at step 13 is exactly the leg nothing at step 13 would otherwise
# look at.
#
# B-15 REALLY RUNS java, and it is still the only block that does. Two of step
# 08's three legs asked java.lang.module what the published artifacts call
# themselves; one of those two survives as B-15.2, and there is no way to
# rehearse it except by running it. So the fixtures carry real jars with real
# manifests, wrapped at 72 bytes the way the jar tool wraps them, and the
# extraction asserts `java` is run EXACTLY ONCE across all twelve blocks, on a
# line naming arqgate.Modules -- a second invocation anywhere would mean a gate
# the fixtures do not drive.
#
# ...and the instrument is read before the finding, twice, and the SECOND
# PREFLIGHT NOW POINTS AT A HELD BLOCK. Before the table runs at all the
# rehearsal points arqgate.Modules at the golden fixture's own twelve jars and
# requires total=12, declared=12, derivable=11 -- the eleven being the poisoned
# artifact, arquillian-impl-base, whose META-INF/services entry names a
# provider class the module does not contain, which ModuleFinder refuses to
# derive a descriptor for. Then it reads B-19.5's denominator: `target.co.
# projects`, a floor frozen off the tree, and `target.co.flag`, the string the
# probe greps for -- both out of baseline/target.txt through the same tv() the
# shipped block uses. baseline/modnames.txt is still asserted at twelve rows,
# but on B-15's behalf alone, and the two readings are separate lines so a
# future failure says which denominator moved.
#
# THAT SEPARATION IS A RECORDED FAULT AND NOT A PRECAUTION. The probe's
# denominator was keyed on modnames.txt when the leg was written, and
# modnames.txt holds PUBLISHED ARTIFACT IDS -- arquillian-core-api and its
# siblings -- while the Gradle probe answers with PROJECT NAMES off the
# settings file. The two sets do not intersect, so the leg read 0 of 12 on a
# tree that had done everything it asks. A leg that scores zero on a perfect
# tree is not a strict leg; it is a broken one. Either preflight failing would
# make every row below a comparison against a broken instrument, and the
# hundred and twelve failures that followed would all be one fault wearing a
# hundred and twelve faces.
#
# STILL THREE ARTIFACT CHANNELS, AND TWO OF THE THREE CHANGED HANDS. The
# published jars feed B-12e and B-15, as they have since step 08.
# ${WORK}/scan.txt has fed a leg since step 09 and now feeds C-4, where it
# gates the family rather than adding to it. ${WORK}/jvmargs.log, new one rung
# ago, now feeds B-19.5 rather than a live C-5 -- it is still the only leg in
# this task that reads what the BUILD would hand a fork rather than what the
# source says or what the artifacts contain, and it is now an inherited one.
# Each channel is separable in the fixtures and NO FIXTURE MOVES TWO, which was
# measured by comparing every fixture's three artefacts against the golden's:
# K1, K2, K3, K19, K20 and M1 move the jars; L1, L2, Q1 and Q2 move the scan;
# Q18, Q19 and Q20 move the probe; and every other fixture leaves all three
# alone and moves the report, the answers or the recorded behaviour instead.
#
# THE FIFTH ARTEFACT IS NO LONGER NEW AND run_one STAGES IT ANYWAY. The
# fixture's own jvmargs.txt becomes the probe's stdout, beside report.txt,
# answers.txt, behavior.summary and scan.txt, and a fixture with NO jvmargs.txt
# is a build the probe could not ask at all -- a configuration-time hang, a
# gradlew that will not execute, a reactor that refuses to evaluate. Q20 is
# that fixture and the only one; the stub returns non-zero for exactly it,
# which is how `gate.b19.5.probe` comes to read `failed` in a rehearsal that
# never started a JVM.
#
# It asserts SIXTEEN figures per fixture and not fifteen, because at this rung
# neither family's legs are interchangeable and the totals hide which one
# moved: B10.java7-held, B11.java8lang-held, B12.java8-library, B13.java9-10,
# B14.java11-jakarta, B15.jpms-services, B16.java17-constructs,
# B17.java21-constructs, B18.security-api-removal and B19.java25-constructs, then
# C-1 through C-5 separately and familyC. TEN held gates and FIVE own legs,
# where the rung below had nine and five.
#
# All sixteen are compared AS STRINGS, because the shell's spelling of a gate
# is part of what is being pinned. `b10=0` is a literal a guarded block assigns
# and never reaches; a gate that computed a zero prints `0.000000`. They are
# the same number and not the same fact, and normalising them would lose the
# difference between a gate that scored nothing and one that never ran. There
# are THREE spellings of zero in this file, and K17 and L2 between them carry
# all three:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                B-16.2, B-16.4, B-16.5, B-17.2, B-17.4, B-18.2 and B-18.3, and
#                gate.b15.departed, .arrived and .held. AND SO DO THREE OF
#                FAMILY C's FIVE AND NOT ALL FIVE, which is what changed at
#                this rung: C-1, C-2 and C-3 are each assigned inside the guard
#                that computes them, so K17 prints the literal three times in
#                one row. C-4 AND C-5 NEVER PRINT IT AT ALL -- each ends in a
#                minf() that runs whichever arm its guard took, and minf()
#                always spells six decimals. That is why L2, which has no scan
#                file whatsoever, reads C-4 `0.000000` and not `0`, and why
#                R18 and R19 read C-5 the same way.
#   `0.000000`   B-12 through B-19 are family totals computed by an awk that
#                sits OUTSIDE the guard, so a tree nobody could compile gets a
#                number computed from parts that never ran. THREE sub-legs join
#                them and their siblings do not: B-16.3, B-17.3 and B-18.4 each
#                multiply by a run-time scaling read from behavior.summary and
#                so compute their own total below their own `fi`, while the
#                fractions they multiply -- gate.b16.3.movefrac and .holdfrac,
#                gate.b17.3.depfrac, .arrfrac and .holdfrac, gate.b18.4.depfrac
#                and .arrfrac -- are initialised above it and print the literal.
#
#                NO LEG OF FAMILY C IS BEHAVIOUR-SCALED HERE, AND AT EVERY RUNG
#                FROM 09 TO 12 ONE WAS. Step 09's C-3, step 10's C-3 and step
#                11's C-4 were each an awk below their guard; family C at this
#                rung does not read behavior.summary on any of its five legs,
#                because none of what it asks for has a run-time signature. A
#                suppression either survives by name or does not; a warning is
#                emitted or it is not; a raw declaration is raw or it is
#                parameterised; a class file carries major 69 or another major;
#                a test id passed before and passed again. R7 and R8 are that
#                distinction priced: both read familyC 0.430000, and C-2 is
#                `0.000000` on R7 -- a tree that compiled and left all thirty
#                fixable warnings standing -- and the literal `0` on R8, whose
#                tree does not compile at 25 at all.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so a tree nobody could compile has no
#                penalty fact rather than an unfired `1`. So are every
#                gate.b16.*, gate.b17.*, gate.b18.* and gate.b19.2/3/4
#                measurement, and every MEASUREMENT under gate.c1.*, gate.c2.*
#                and gate.c3.*.
#
#                THE WANTS ARE NOT, AND THAT IS THE LINE THIS RUNG DRAWS.
#                `gate.c1.allow.want`, `.extras.want`, `.unused.want`,
#                `gate.c2.fixable`, `gate.c2.lint`, `gate.c3.departed.want` and
#                `.held.want` are read off the frozen baseline before anything
#                runs, so K17 carries all seven while every number they would
#                have been compared against is absent -- which is the honest
#                shape, because what the rung ASKS FOR does not depend on
#                whether the tree compiled. Nor are gate.c4.*, gate.c5.* and
#                gate.b19.5.*: their guards are the class-file scan, the
#                recorded cases and the declared flag, and not one of the three
#                is about compiling, so K17 carries thirty of those with real
#                numbers in. gate.src.ok24 is on the MISSING list for a reason
#                of its own and not a guard's: the control compile only runs
#                when the measurement compile succeeded, so a tree that will
#                not compile at 25 is never asked about 24 -- the absence IS
#                the answer, and it is not the same answer as `no`. Spelled
#                MISSING and not collapsed into the empty string, so
#                `want MISSING` is an assertion.
#
# AND FOUR ABSENCES THAT ARE NOT ZERO AT ALL, each with its own sentinel, which
# is the other half of the same argument. `gate.cp=no-jars` and
# `gate.b15.mod=no-module-jars` are K20's two else-arms -- a tree that
# published nothing, reached through `[ -s ]` and not `[ -f ]` in each block --
# `gate.c4=no-scan` is L2's, and `gate.b19.5.probe=failed` is Q20's, which is
# the only one of the four that is a report about a PROCESS rather than about a
# file. None is spelled `0.000000`, because a gate that measured nothing and a
# gate that measured zero are different findings and a trajectory that could
# not tell them apart would send a reader looking for the wrong bug.
#
# factck's SIGNATURE CHANGED THIS RUNG AND THE CHANGE IS LOAD-BEARING. `want`
# is `$*` after `shift 2` rather than `$3`, because one of the facts this rung
# asserts has A SPACE IN ITS VALUE: `gate.c2.lint` publishes the flag string
# C-2 was measured under, and that string is `-Xlint:all -Xlint:-deprecation`.
# Under the old signature `want` would have been `-Xlint:all` alone and every
# row would have failed on a fact that was right -- or, worse, a row whose lint
# string had silently become the single word `-Xlint:all` would have PASSED.
# The carve-out is measured rather than chosen (NOTES 16.89) and it is exactly
# the half of the string the old signature would have dropped. probeck moved
# for a smaller reason and would have failed more quietly: its key was
# `gate.c5.probe` one rung ago and is `gate.b19.5.probe` here, and a helper
# carried across unchanged would have found MISSING on all 112 rows and -- since
# MISSING is a string like any other -- been satisfied by writing MISSING into
# the table.
#
# WHERE EACH LEG SITS RELATIVE TO ITS GUARD is checked by line number, because
# it is the property that decides what a tree that will not compile scores.
# C-4 reads the class-file scan and sits outside the source-report guard
# entirely -- its own guard is about whether scan.txt exists, not about whether
# the sources parsed, and it is asserted not to read `measured` at all -- and
# C-5 sits outside it for the same kind of reason, because a JUnit result file
# is not a reading of the source either. C-1, C-2 and C-3 read the source AST
# and must sit INSIDE it, because a census counted over an empty population
# file reads as "everything departed", the ladder's seventh trap. Each opens
# its own guard naming its own baseline population -- suppress.txt for C-1, the
# target.w.* wants for C-2, rawdecl.txt for C-3 -- so a missing file costs ONE
# leg rather than three, and B-16's four legs, B-17's three, B-18's three and
# B-19's four do the same over step 09's five set files, step 10's four, step
# 11's four and step 12's three. B-15 is split across its guard on purpose: the
# name leg reads the jars and sits above, the service leg reads the report and
# sits inside.
#
# What that still costs: nothing here says SourceScan's counts are right, only
# that the arithmetic over them is. The counts are verified separately and by a
# different instrument -- fixtures/src-rehearsal.sh, further down, compiles a
# smoke tree with the plugin attached and asserts every counter and every
# record shape, including this rung's new `rawdecl` reading, against figures
# measured first and frozen after.
#
# The per-site records in the fixtures are generated from the REAL frozen set
# files this rung reads -- step 12's modimport.txt, jdkimport.txt and
# scopedvalue.txt, step 11's priv.txt, catchparam.txt, varhandle.txt and
# vhids.txt, step 10's reftest.txt, chain.txt, seqfirst.txt and swids.txt, step
# 09's ipattern.txt, strnl.txt, sealed.txt, swarrow.txt and tbids.txt, step
# 08's modnames.txt, svc.txt and svcids.txt, step 07's six -- and from this
# rung's own two, suppress.txt and rawdecl.txt, so each half of each gate is
# rehearsed against the buckets it will actually be asked about. Fixtures with
# invented paths would agree with a gate pointed at any set at all.
#
# AND NOT ONE INHERITED POPULATION IS RE-CUT HERE, which at this rung takes an
# ARGUMENT rather than a measurement, because one bucket key in two frozen
# files disagrees with the report this step's baseline was cut from. The golden
# renames ServletTestRunner#writeObject, and step 12's blind agent had already
# split ManagerImpl#fire into #fireInScope; baseline/ipattern.txt and
# baseline/catchparam.txt name #fire, which is what the oracle-lineage tree a
# scored run is handed actually calls the method. Re-pointing either file at
# #fireInScope is what the oracle arm of 2026-09-24 ran, and it cost that arm
# B-16 0.968254 and B-18 0.994690 -- a hold gate charging a candidate for a
# name no tree it will ever see carries. So the two files stay frozen and the
# cross-check maps the scan back to the frozen spelling instead, asserting both
# directions before it does. NOTES 16.98, 16.99 and 16.104.
#
# Both ends of the table are in it, because a gate stuck at 1 passes
# "golden = 1" and a gate stuck at 0 passes "pinned = 0". The rows in between
# are what make either of those mean anything, and each is asserted to six
# decimals rather than to a band -- a gate that has drifted lands somewhere,
# just not here. The figures are computed by authoring/fixtures-step13/
# mkfix13.py, an independent implementation of the same composition, and
# written into the rehearsal BY HAND. This image has no python3, which makes
# that separation structural rather than a matter of discipline: a rehearsal
# that computed its own expectations at run time would agree with the scorer by
# construction and assert nothing. The separation is sharpest on B-15.2, which
# mkfix13.py predicts from the jar PLAN -- twelve Mod records, each with a name
# or without one -- while the rehearsal measures the platform's reading of the
# bytes that plan produced; the two agreeing is a fact about java.lang.module
# and not about either program.
#
# The table is 112 rows of 16 figures, and beside those 1792 totals stand 1102
# factck and 7 probeck assertions on the FACTS behind them -- and 431 and 4
# more on the twenty-one perturbations below -- which is what tells apart the
# fixtures that share a total. Row by row, where the row is load-bearing rather
# than the plain half of a gate:
#
#   pinned must score C-1, C-2 and C-3 0.000000, C-4 and C-5 1.000000, familyC
#      0.000000, and all TEN held gates 1.000000. THE TWO MULTIPLIERS BEING
#      FULL IS THE POINT AND NOT AN OVERSIGHT: the tree this rung is handed
#      already compiles at 69.0 and already passes the suite, so the two legs
#      that gate the family are the two a nop tree has nothing to do about, and
#      a nop tree collects NOTHING -- not 0.04, not 0.02, zero. That is this
#      rung's answer to migration_prompt item 6, and it is the reason C-4 and
#      C-5 take no weight rather than a small one.
#   ...SO THE FLOOR IS 0.474681 AND THE MARGIN 0.025319 IS UNSPENT. Family B is
#      flat at 1.000000 on the input tree -- earned rather than assumed, since
#      it is what the frozen-population argument above is about -- and family A
#      reads 938/939 = 0.998935, one transcript, because the era was minted
#      against the GOLDEN and the golden renames ServletTestRunner#writeObject.
#      0.30 x 0.998935 + 0.70 x 0.25 x 1 = 0.474681, and item 6's bar is that
#      the nop must not reach 0.5. Any leg such a tree could collect is
#      admissible only while 0.474681 + 0.525w < 0.5, that is w < 0.048228;
#      here w is 0, and the whole of that headroom is left standing. NOTES
#      16.103.
#   golden must score all five family-C legs and familyC 1.000000 -- AND IT IS
#      THE FIRST GOLDEN ON THIS LADDER THAT DOES NOT READ 1.000000 ACROSS
#      FAMILY B. B14 reads 0.995607 and B18 0.989379, and the 0.001400 those
#      two cost family B is ONE METHOD RENAME: ServletTestRunner#writeObject is
#      the only answer javac's [serial] warning has that is not a suppression,
#      and it takes one frozen ns bucket (B-14.3, 42 of 43) and two frozen
#      catchparam holds (B-18.3, 49 of 51) with it. The row is here so that the
#      cost is a figure in a table rather than a claim in a note: 0.001400 x
#      0.175 = 0.000245 of reward, paid on purpose, priced in NOTES 16.103 and
#      16.104, and NOT re-keyed -- re-keying would ship a population asserting
#      that step 11's catch clauses live in a method step 13 invented.
#   K15 and K16 are the narrowest pair in the file: B14 0.497804 on both, every
#      other cell identical, and the whole difference is one published fact,
#      gate.src.taraw.
#   K17 is the tree nobody can compile, and it is the row that proves the
#      multipliers multiply: three literal zeros in family C, C-4 and C-5 full
#      off artefacts that survived, familyC 0.000000, and B19 0.020408 standing
#      alone.
#   K19 and K20 are the jar channel's two failures. K19 published twelve named
#      jars with nothing in them B-12e looks for, B12 0.850000. K20 published
#      NOTHING: one absence with two consequences and not three -- B-12e the
#      literal 0 under gate.cp=no-jars, B-15's name leg 0 under
#      gate.b15.mod=no-module-jars, B15 0.562500, and the service leg untouched
#      because it reads the report.
#   L1 and L2 are C-4's two failures and they are failures of different kinds.
#      L1 is a partial migration -- 700 of 730 class files at 69.0, the other
#      30 still at 24 because one subproject's build file was given
#      `release = 24` -- C-4 0.958904 and familyC 0.958904, THE MULTIPLIER
#      CARRYING THE WHOLE FAMILY DOWN, which is the shape no rung below this
#      has had. L2 is no scan file at all, a build that produced no class
#      files: C-4 0.000000 through minf(), familyC 0.000000, and
#      `gate.c4=no-scan` rather than a bare zero.
#   Q1 and Q2 are the same channel's other two, and Q1 is the one that could
#      not have been written before step 12: the whole tree compiled behind
#      `--enable-preview`, so every class file reads major 69 AND minor 65535,
#      `major.69` is FULL, and C-4 is 0.000000 anyway. Q2 is one subproject
#      doing it, 690 of 730, C-4 0.945205. The minor is part of C-4's one
#      channel and not a second one: `majorminor.69.0` and not `major.69`,
#      because a --enable-preview build satisfies a major-only reading
#      completely (JVMS 4.1).
#   L3 through L14 are step 09's own family C, failed one leg at a time and now
#      read through B-16; M1 through M6 are B-15's; N1 through N15 are step
#      10's through B-17 and P1 through P15 are step 11's through B-18. M1 is
#      worth reading even though every cell on it is 1.000000: it writes a
#      thirteenth malformed provider file and B-15 reports
#      gate.b15.mod.derivable=10 beside a full score, which is the evidence
#      that step 08's derivable term really is gone rather than merely
#      unmentioned.
#   Q3 through Q20 are step 12's five legs read through B-19, failed one at a
#      time, and they are priced over 0.98 rather than over 1: Q3 and Q6 both
#      0.997732 (each one file short of 171, one with a module missing and one
#      with the wrong module named), Q4 and Q7 both 0.612245 (Q4 wrote
#      `import module java.base;` into all 322 files, Q7 wrote none at all),
#      Q14, Q15 and Q16 all 0.744898 (the ScopedValue leg's min() falling three
#      different ways), Q18 and Q19 both 0.998299 (eleven of twelve projects
#      flagged, by hand and by one build file), and Q20 0.979592, the probe
#      that never answered.
#   R1 through R20 are THIS RUNG'S OWN five legs, and they are the rows that
#      show what a family with two multipliers in it actually does. R1 leaves
#      one suppression outside the allow list: C-1 0.978261 and C-2 0.978261
#      together, because C-2 is bound to C-1 by a min() and cannot be better
#      than the census underneath it, familyC 0.984348. R2 leaves the census
#      untouched at all 48 pairs, C-1 and C-2 both 0.000000, familyC 0.280000 --
#      C-3 alone. R6 leaves three of thirty fixable warnings, C-2 0.900000 and
#      familyC 0.943000. R7 and R8 both read 0.430000 by the two different
#      roads above. R9 through R13 are C-3: R9 0.571429 on the open half, R11
#      0.750000 and R12 0.500000 on the forced half, and R10 and R13 BOTH
#      0.000000 and both familyC 0.720000 by different routes -- R10 held all
#      four forced declarations and departed from none of the seven open ones,
#      R13 deleted the four forced ones instead, so `gate.c3.held` reads 4
#      there and 0 here. Deleting the population is not departing from it.
#      R14 and R15 are C-4's inventory, 400 class files against the wanted 435
#      (0.919540) and 500 against it (1.000000, because min(500, 435) is 435
#      and a tree is not charged for compiling more). R16 through R20 are C-5:
#      147 of 160 ids (0.918750), 40 of 53 XML files (0.754717), cases.txt
#      absent and xml.txt absent (both 0.000000 and both familyC 0.000000, by
#      opposite halves again), and 200 ids passing where the baseline froze 160
#      -- min(200, 160) is 160, C-5 1.000000, and a candidate who ADDED tests
#      neither gains nor loses.
#
# FOURTEEN GROUPS OF ROWS ARE IDENTICAL IN ALL SIXTEEN CELLS, thirty-two rows,
# and the reason is the SHAPE of this rung's family rather than the number of
# legs on the ladder. `(0.15*C1 + 0.57*C2 + 0.28*C3) * C4 * C5` has two
# multipliers in it, and a multiplier reading 1.000000 is invisible: every one
# of the six inherited families -- K through Q, step 07's through step 12's --
# leaves all five of this rung's legs full, so any two of them that agree on
# family B agree on all sixteen columns. To the table, two rows reading the
# same sixteen numbers are one row printed twice. So the rehearsal carries a
# THIRD HELPER: sep() reads TWO fixtures and asserts a named key does NOT read
# the same on both, which is what turns "these rows are identical on purpose"
# from a remark into a check. Thirty-one calls, and a key that stopped parting
# a pair -- because a population shrank, or because a fact line moved inside a
# guard -- fails there while every total still agrees.
#
# THE FIRST GROUP IS THE ONE THAT MATTERS MOST, and it is the group whose rows
# are 1.000000 in all sixteen: golden's own family C shares it with M1 and Q8.
# Both of those are ADVERSARIAL -- M1 writes a thirteenth malformed provider
# file, Q8 writes a 189th module import in a file whose set is already right --
# and both are non-events BY CONSTRUCTION, which is a thing a table of totals
# cannot say. An unbuilt fixture scores 1.000000 everywhere too.
# gate.b15.mod.derivable reading 10 against 11, and gate.b19.2.imports reading 189
# against 188, are what separate a non-event from an absence.
#
# AND THE PAIRS THAT SHARE A TOTAL THROUGH OPPOSITE HALVES ARE THIS RUNG'S OWN
# ADDITION TO THE SHAPE. R10 and R13 both read familyC 0.720000 with C-3
# 0.000000; one tree touched no declaration at all and the other deleted the
# only two buckets the forced half scores. R18 and R19 both read 0.000000 with
# C-5 0.000000; one has no cases.txt and the other no xml.txt. L2, Q1 and R18
# all lose the family outright through a multiplier, by three different routes
# -- no scan, a preview build, and a suite that left nothing to parse. The row
# loop parts some of these already, because two of them spell their zero
# differently; sep() is what parts the rest, and the reason it is worth the
# lines is that the TOTAL is what a reader of a scored run sees first, and
# 0.720000 twice over is exactly the reading that would send them to the wrong
# half of the wrong leg.
#
# ...and then the rehearsal proves it can fail. Twenty-one perturbations on
# NINE channels: FOUR ARTEFACT CHANNELS -- the source report, the class-file
# scan, the published jars, and the argument list Gradle would hand a test
# fork -- and the FIVE id lists the one behavior.summary is read against:
# httpids, svcids, tbids, swids, vhids. A rung with four artefact channels and
# five behaviour lists can get any one crossed with any other, and the only way
# to see a crossing is to move one and assert the other eight did not. Each takes the golden -- the one tree
# that scores every gate in full -- moves ONE thing, and derives the expected
# figure in the comment above it rather than transcribing it, because these
# trees are not in mkfix13.py's table.
#
# Its hundred and twelve fixture directories are required to be pairwise
# distinct by digest -- scan.txt and jvmargs.txt included, which is what
# separates L1, L2 and Q20 from the golden they are built out of -- so that two
# rows which had become byte-identical twins cannot look like a hundred and
# twelve assertions while being a hundred and eleven; and the table and the
# directory listing are required to name the same set, which is the ladder's
# third trap in miniature: a fixture that stopped being scored would otherwise
# disappear without changing a line of output.
RUN bash /tests/fixtures/c-rehearsal.sh

# The three baselines and the fifty-one inherited frozen set files this rung
# reads -- with its own two populations beside them -- checked against each
# other rather than against a number typed a second time here.
#
# Every threshold in family C and in B-10 through B-19 comes out of
# baseline/source.txt and baseline/target.txt, and the scorer derives none of
# them from the tree it is grading. That is the right design and it moves the
# failure: a baseline with a wrong number in it now produces a confident wrong
# score instead of an error, and nothing else in the image would notice. So the
# files are required to agree with each other, which is a property no single
# typo can satisfy.
#
# WHAT THIS RUNG ACTUALLY WROTE IS FIVE FILES AND NOT FIFTY-SIX, and the
# inheritance claim is a claim about BYTES here rather than about counts.
# mk-baseline-step13.sh emits level.txt and target.txt, cuts the two populations
# this rung owns -- suppress.txt and rawdecl.txt -- and copies source.txt in
# whole; the other fifty-one staged files are step 12's, byte for byte, and
# `cmp` across the two baseline directories says so: three differ, two are new,
# fifty-one are identical. Every re-reading below is therefore a NEW QUESTION
# asked of an OLD FILE, which is the only kind of inherited check worth running.
#
# TWO IS THE FEWEST FILES ANY RUNG ON THIS LADDER HAS ADDED, and it is not
# thrift. Step 12 added three -- modimport.txt, jdkimport.txt, scopedvalue.txt
# -- because its legs were new RECORD KINDS and a record kind needs a frozen
# population to be scored against. This rung's SourceScan gained exactly one,
# `rawdecl`, and it is the only one of that file's fifty-six emit keys step 12's
# frozen copy does not also print. C-1 and C-2 needed no record kind at all,
# because their subject is javac's own console output rather than the plugin's
# report, and the `suppress` records C-1 keys off have been emitted since step
# 03, on every rung -- what is new here is that a leg finally reads them.
#
# baseline/source.txt AT THIS RUNG IS NOT THE PREVIOUS RUNG'S BASELINE, and it
# is not a re-reading of the same tree either. It is STEP 12'S BLIND WORKSPACE
# -- the tree this rung's candidate is actually handed -- measured with the gate
# that ships HERE, at -source 25, ALL scope, with GATE_LINT set. Three things in
# that sentence had to be new, and THE LEVEL IS NOT ONE OF THEM:
#
#   THE TREE      step 12's source.txt described step 11's blind output. This
#                 one describes step 12's, which is four populations and a
#                 class-file level further on.
#   THE GATE      step 12's SourceScan cannot see a `rawdecl` record, because
#                 that record kind did not exist below this step -- the string
#                 appears nowhere in step 12's frozen SourceScan.java. C-3
#                 reads nothing else, and its held half is the forced rows of
#                 that same population.
#   THE FLAG      GATE_LINT, which step 12's source-gate.sh does not contain the
#                 name of. The javac line there is -nowarn and always was, and
#                 C-1 and C-2 are counted out of the console output -nowarn
#                 silences -- so below this rung the number those two legs need
#                 does not exist at any level or on any tree.
#
# THE LEVEL IS THE ONE THING THAT DID NOT MOVE, and saying so is the point.
# Step 12's report was already cut at -source 25, because `import module` parses
# at no lower release and step 12 is the rung where it arrived; this one
# compiles at whatever step 12 left. That is why level.txt here is the first in
# thirteen rungs whose `major` equals its `was` -- 69 and 69 -- and why it
# carries a third key, `classes=435`, that no earlier level.txt needed.
#
# GATE_LINT IS ADDITIVE, AND THAT IS A PROPERTY OF THE OTHER TWELVE RUNGS
# rather than of this one. Unset, it expands to the -nowarn the gate has always
# carried, so every rung staged below step 13 runs the identical javac command
# and reads the identical report, and `javac.warnings` is published
# unconditionally and reads 0 there. Step 13 sets it to
# `-Xlint:all -Xlint:-deprecation`, and the carve-out is measured rather than
# tidy: NOTES 16.89 found the deprecation family CHAIN-DEPENDENT, because the
# two dependency chains resolve junit:junit at 4.8.1 and 4.13.2 and the two
# versions deprecate different members. A family that is legal at two values is
# a family no baseline can name.
#
# AND THE REPORTS ARE REQUIRED TO BE REPORTS ABOUT TREES THAT COMPILED -- all
# three of them, not one. mk-baseline-step13.sh refuses to run unless the blind
# workspace, the STRIPPED ARM and the golden each say `javac.rc=0`,
# `javac.errors=0`, `source.ok=yes` and `javac.level=25`, and unless each has
# left a `.javac.log` beside its report. That last requirement is C-1's alone:
# C-1 IS javac's diagnostic count, so a report whose compile failed would hand
# it a denominator made of errors.
#
# THE STRIPPED ARM IS THE INPUT STEP 12'S SCRIPT HAD NO ANALOGUE FOR. It is the
# seed with every @SuppressWarnings removed, and it exists because C-1's
# denominator cannot be read off the tree the candidate is handed: 25 of the
# honest 36 warnings arrive PRE-SILENCED. target.w.honest is the stripped arm's
# 36, target.w.delivered is the seed's 11, target.w.want is the golden's 0, and
# each is asserted against the report it came from rather than typed. A rung
# that sized this leg on the delivered count would be scoring the annotations
# instead of the defects. NOTES 16.95 records the whole of it, 59 cross-checks,
# all ok.
#
# AND THE INPUT TREE IS REQUIRED TO BE AN ORACLE-CHAIN TREE, checked rather than
# trusted. Step 01's solve.sh renames the seed's `build` module to
# `build-config` and rewrites settings.gradle to match, and no tree descended
# from a blind run carries that line; the script greps for it and exits if it is
# absent. A LINEAGE that was in fact another blind snapshot would re-publish
# exactly the figures this input exists to replace.
#
# THE PLUGIN ITSELF STILL STOPS AT THE 17 DOOR, and this rung inherited the
# reason rather than finding it. ImportTree.isModule() arrived with JEP 511 in
# JDK 25 and B-19.2's whole arrival half is read through it -- but the same
# SourceScan.java is compiled inside every verifier image from step 03 up, under
# JDKs 11, 17, 21 and 25, so moduleImport() invokes the method REFLECTIVELY and
# answers false where it is absent. False is the TRUE answer there and not a
# degraded one: a javac with no isModule() is a javac that cannot parse a module
# import, so no tree it compiled has one.
#
# NOT ONE INHERITED COUNTER IS RE-MEASURED HERE, which is the rule this ladder
# settled into and is a result rather than a convenience.
# mk-baseline-step13.sh reads every inherited denominator out of step 12's
# staged target.txt and re-derives none of them -- target.diamond,
# target.explicit.nonanon, target.try.resources, target.iofile, target.anonfn,
# the whole of B-10's, B-11's and B-12's, and B-13 through B-18's. The carry is
# taken WHOLESALE and not key by key: every `target.` line of step 12's file
# except its three level keys, 132 of the 135, and the script asserts the count
# it carried equals 135 minus 3 rather than equals 132. Transcription was never
# the safer option -- it was the option whose errors were invisible.
#
# THAT IS STILL AN ASSERTION AND NOT A CONVENIENCE, because source.txt and
# target.txt here describe two DIFFERENT TREES: target.txt's inherited figures
# were cut against step 12's GOLDEN, and source.txt is step 12's blind
# workspace. The two agree only because that blind run scored full on every
# family-B leg, and every derivation below is what checks that they still do. A
# seed that fell short of step 12's golden anywhere would make the file this
# rung was handed disagree with the file it publishes, and it would disagree
# HERE rather than in a score.
#
# FOUR COUNTERS CHANGED THEIR SPELLING RATHER THAN THEIR VALUE, which is the
# eleventh trap seen from the other side. ns.ref.javax.annotation and
# ns.ref.javax.ejb are gone from this file and ns.ref.jakarta.annotation and
# ns.ref.jakarta.ejb hold their 17 and 5, because step 07 did the rename and
# this tree is six rungs past it. Every derivation below that was written
# against the javax spelling is re-keyed here, in the same direction the scorer
# re-keys it, and the javax counters that are NOT re-keyed -- .enterprise,
# .inject, .management, .naming, .servlet and .xml -- are the ones this rung
# must still hold, checked below against the set files rather than a literal.
#
# STEP 12'S OWN source.txt AND target.txt ARE NOT STAGED, for the reason step
# 09 gave about step 08's: both would land as baseline/source.txt and
# baseline/target.txt, the same two names this step's pair lands as, and which
# one a gate read would come down to the order of a staging loop. What the
# rungs below need carried forward is carried inside THIS step's target.txt,
# under spellings that name the gate rather than the rung that cut them: step
# 07's four denominators as target.b14.c2.* through target.b14.c5.*, step 08's
# as target.b15.*, step 09's as target.b16.*, step 10's as target.b17.*, step
# 11's as target.b18.* and step 12's as target.b19.*. B-14 through B-19 read
# them under those spellings deliberately, because renaming a key is how a
# carry-forward gate quietly stops grading the thing it was named for.
#
# STEP 12'S TWENTY-THREE ARRIVE TWICE, AND THE ALIAS IS THE COPY THAT IS NEW.
# target.mi.*, target.ji.*, target.sv.* and target.co.* are how step 12's own
# file spelled its four legs, and the wholesale carry above brings them across
# under those names because a rename is the one thing this file must never do
# to an inherited key -- it is how T12 itself still carries step 11's `acc`
# keys. target.b19.* is then DERIVED from those twenty-three rows by a single
# substitution, never re-measured, and the two copies are asserted value by
# value below. The duplication is safe for exactly that reason and would
# otherwise be a second place to be wrong.
#
# Step 06's seven population files -- newinstance.txt, accessible.txt,
# views.txt, builds.txt, optops.txt, boxedctor.txt, varbuckets.txt -- are staged
# under their own names and checked below against this tree's own records, and
# so are step 09's six, step 10's four, step 11's four and step 12's three.
#
# THE DIAMOND PAIR IS STILL AN EQUALITY HERE, as it became at step 09. At steps
# 03 and 04 target.diamond and target.b10.diamond were the same 93 counted from
# opposite ends; step 05 spent 19 of them on its sinks, step 06 spent the
# twentieth on Set.of, step 07 spent none, step 08 spent TWO when its C-3
# deleted the two hand-rolled META-INF/services readers whole, and steps 09
# through 12 spent none. This rung spends none either, and what makes that hard
# HERE is not a construct it converts, because IT CONVERTS NOTHING. Static
# closure adds and removes no type and rewrites no expression: its whole output
# is annotations added, annotations removed, and raw declarations either
# parameterised or excused. What threatens the count is the CHEAPEST WRONG
# ANSWER. Zero warnings and zero open raw declarations are both reachable by
# DELETING the two TestNG transformers whose raw parameters TestNG 5.14.6's own
# interface forces, and on a tree with those two files gone every other leg on
# this rung scores HIGHER than on the tree that suppressed them correctly.
# level.txt's classes=435 is what stands in front of that, and NOTES 16.95
# records why the figure belongs in level.txt rather than in a leg of its own: a
# leg whose cheapest satisfaction is a deletion is not a leg.
# target.diamond is 71 and the tree holds 71.
#
# The other invariants are the things this rung must NOT move, each read out of
# source.txt and required to match target.txt: the non-anonymous explicit
# remainder (23, as newgeneric.explicit minus newgeneric.explicitanon), the
# anonymous functional residue (12), the java.io pair (4, as iofile.new plus
# iofile.decl) and the seven resource clauses step 08 left behind. Two more are
# held against a literal zero instead, because zero is not a denominator and has
# no target key to match: the manual close() count, and `newgeneric.raw`, which
# counts raw NEW EXPRESSIONS and is NOT this rung's rawdecl population. Those
# two names are a syllable apart and measure opposite halves of the language --
# `new ArrayList()` against `List xs = ...` -- and C-3 is about the second only.
# A rung that moves one of these has either redone an earlier step's work or
# undone it, and B-10 through B-19 exist because the second is the likelier
# accident.
#
# THE @SuppressWarnings CENSUS IS NOT ON THAT LIST ANY MORE, and at the rung
# below it was. suppress.sites reads 44 here and is asserted at 44 -- but as a
# fact about the tree this rung is HANDED, not as a figure it must hold. C-1's
# whole job is to move it, to exactly four: two the seed already carried and two
# the golden added, target.su.budget=4 with budget.kept=2 and budget.added=2. A
# step-13 candidate that ended with 44 suppression sites has done nothing at
# all, and an invariant list that still named 44 would be asking it to.
#
# NOT ONE FIGURE IN THE INVARIANT LIST MOVED UNDER THE RUNG BELOW, and java.io
# is the one worth saying out loud, because it is the one that looks as though
# it could not possibly have survived. Step 12's family C deleted 452
# single-type JDK imports across 171 files and put 188 `import module`
# declarations where they had been, and THIRTY-FIVE OF THE 452 WERE java.io --
# File, InputStream, BufferedReader, InputStreamReader, IOException and the
# rest, spread over 20 files, with not a single java.io row held
# (authoring/baseline-step12-jdkimport.txt). The figure
# did not move anyway, and the reason is the one thing worth carrying out of this
# paragraph: B-10 COUNTS SITES, NOT IMPORTS. `iofile.new` is a `new
# java.io.File` or a `new File{Input,Output}Stream`, `iofile.decl` is a
# declaration of one of those types, and a name that arrives through a module
# import is the same name at the same site. Deleting the import moved 35 rows in
# C-3's population and 0 in B-10's. The floor was 4 for nine rungs and is 4 on
# the tree this one is handed, so
# mk-baseline-step13.sh carries it out of step 12's target.txt rather than
# re-deriving it -- and the image then holds that carried 4 against THIS tree's
# own `iofile.new` plus `iofile.decl`, which is the check the script cannot make
# for itself. B-10 does not read the figure as an equality in any case: it reads
# it as a FLOOR, the number of java.io sites that may not come BACK. The eleven
# that moved at step 03 are still the denominator of both halves of B-10's NIO.2
# third, as they have been since step 03, and 11 is what target.b10.nio still
# says.
#
# THE FLOOR CLAIM, ASSERTED ON THE BASELINES rather than only through a fixture.
# The tree the candidate is handed satisfies every requirement B-10 through B-19
# state -- 71 diamonds, the 23 non-anonymous sites still explicit, 7 resource
# clauses, no manual closes, java.io still at its 4 sites, 49 lambdas over 27
# files with all 13 of B-11.1's re-cut buckets still holding one, 24 annotated
# SAM interfaces, 16 Optional-returning helpers, 25 Collectors calls, 6 Objects
# delegations, 11 java.time references, not one
# currentTimeMillis, both Class.newInstance calls migrated, no isAccessible
# guard with the 10 setAccessible calls step 11's C-4 left still there, no
# wrapper constructor, seven List/Set/Map factory calls with the seven views
# untouched, no Optional.isPresent, 78 `var` declarations with step 06's one
# exemption spent, all 22 annotation references already jakarta, all 8 toArray
# sites converted, the 14 HTTP records already moved, twelve jars each declaring
# the Automatic-Module-Name its packages imply, both hand-rolled
# META-INF/services readers already through java.util.ServiceLoader, 17
# `instanceof` sites across 14 methods already binding a pattern variable with
# the 7 that must not still plain, the one text block written and the 7 held
# fragments still concatenated, 11 hierarchies sealed over 35 permitted names,
# both switches already in arrow form, 13 reflective type tests across 7 methods
# already reading as type patterns with the 14 that must not still plain, 4
# dispatch chains already pattern switches over 10 arms with the 2 that must not
# still chains, 7 sequenced first-element reads already calling getFirst() with
# the 9 that must not, all 67 privileged-API sites gone from all 7 files with
# the 13 held constructor and accessor calls still in place, 28 catch parameters
# already unnamed with the 51 that are read still named, 8 VarHandle sites
# across 4 buckets in 4 files, and -- new at this rung, because step 12's family
# C arrives here as B-19 -- 188 `import module` declarations across the 171 main
# sources that need one and none in the 151 that do not, 452 of the 495
# single-type JDK imports departed with all 43 holds still written out (25
# ambiguous, 16 static, 2 nested), one of the 8 ThreadLocal fields converted to
# a ScopedValue with the other 7 held and all 8 call sites moved with it, and
# all 12 projects forking their tests with -XX:+UseCompactObjectHeaders.
#
# AND IT SATISFIES NONE OF THE THREE LEGS FAMILY C WEIGHS: all 46 of C-1's extra
# @SuppressWarnings pairs are still written, the 27 `unused` ones among them
# included -- a value javac has no lint key for at all, so not one of the 27
# suppresses anything javac would ever emit; C-2's 11 delivered warnings of the
# 30 fixable are still 11, which is (30-11)/30 = 0.633333 read naively and
# 0.000000 read against the floor the seed already stands on, because that floor
# IS 11 and min() is what stands between the two numbers; and 0 of C-3's 7 open
# raw declarations are narrowed.
#
# BOTH MULTIPLIERS, THOUGH, READ 1.000000 ON THAT SAME TREE, and that is this
# rung's shape rather than a hole in it. C-4 wants major 69 over 435 class files
# and the tree it is handed is already at 69: static closure moves no level, and
# target.level.was equals target.level here for the first time in the migration.
# C-5 wants the behaviour unchanged and a tree that changed nothing has changed
# nothing -- 160 ids over 53 XML files, exactly what it was handed. A multiplier
# that reads full against a weighted sum of zero still gives zero.
#
# Those are the two halves of the calibration table's `pinned` row -- familyC
# 0.0000 with every held gate reading what it reads on this tree -- and that row
# is the floor argument at the foot of test.sh. WHAT IS TRUE HERE AND WAS TRUE
# AT NO RUNG SINCE 10 IS THAT FAMILY B IS A FLAT 1 ON THE TREE THE CANDIDATE IS
# HANDED, and family A is one id short of one for a reason that is the rung's
# own answer:
#
#   family A   938 of 939, or 0.998935 -- AND THE MISSING ONE IS THAT ANSWER,
#              not a defect. The era replayed here is rec/jdk25-step13, which is
#              step 11's era with exactly ONE transcript re-cut:
#              webrunner/shape-servlet, the only id of the 939 that records
#              getDeclaredMethods() on a class this rung renames a method of.
#              C-1 forbids suppressing javac's [serial] warning, the only other
#              answer that warning has is to rename
#              ServletTestRunner#writeObject, so the rename IS the rung's answer
#              and the recording names it. THE FIGURE IS THE MIRROR OF A
#              MEASUREMENT: the oracle arm of 2026-09-24 ran the renamed tree
#              against the UNRENAMED era and read familyA=0.998935 with
#              `behavior.differ[0]=webrunner/shape-servlet`, so an unrenamed
#              tree against the renamed era differs on that same single id and
#              reads the same 938. authoring/ids-step13-moved.txt is the whole
#              diff and it is one line long; the reference solution reads 939.
#   family B   1.000000, all nineteen legs, and this one is by construction
#              rather than by arm: the tree the candidate is handed IS step 12's
#              golden, so every population B-10 through B-19 freezes was frozen
#              off it. Eighteen of the nineteen are step 12's own inherited set.
#              The nineteenth is B-19, which IS step 12's C-2, C-3, C-4 and C-5
#              renormalised over 0.98 -- its family C with its C-1 taken out --
#              and a golden that scored 1.0 on those four legs reads them full
#              when they are asked again under other names.
#
# So the floor is 0.30 x 0.998935 + 0.70 x 0.25 x 1.000000 = 0.299681 + 0.175000
# = 0.474681 against migration_prompt item 6's 0.5, and it clears it by
# 0.025319. THAT IS THE NARROWEST MARGIN ON THE LADDER AND IT IS NARROW FOR THE
# RIGHT REASON. Every rung whose floor was lower had something in its own family
# B reading short on its own input -- step 11's 0.463128 was B-6 charging its
# input tree for 42 signatures step 11's own family C was about to remove, which
# is a defect of the inheritance and not a safety margin. Nothing here is
# charged for work it has not done. AND THE FLOOR IS NOT THE ROUND 0.475000 THE
# RUNG BELOW HAD, which is the one thing about this number worth reading twice:
# the 0.000319 under it is family A declining to pay a do-nothing tree for a
# rename it did not make, and that is signal rather than slack.
#
# TWO CONSEQUENCES FOLLOW FROM THAT NUMBER AND BOTH ARE LOAD-BEARING. The cap on
# the family-C weight a do-nothing tree collects is 0.474681 + 0.525*w < 0.5,
# that is w < 0.048228 -- AND w IS THE SUM OF EVERY LEG SUCH A TREE REACHES, not
# the largest of them. AT EVERY RUNG SINCE 05 THAT INEQUALITY IS WHAT PRICED THE
# CHEAP LEGS. AT THIS ONE IT PRICES NOTHING, because this rung has no cheap leg
# to price. The three legs that carry weight are C-1 at 0.15, C-2 at 0.57 and
# C-3 at 0.28, and 0.525 x 0.15 = 0.078750 is already over the bar on its own:
# the SMALLEST of the three cannot be afforded by a tree that has not earned it.
# No build-file edit reaches any of the three -- C-1 is 46 @SuppressWarnings
# pairs deleted by name, C-2 is 30 javac warnings fixed against a delivered
# floor of 11, C-3 is 7 raw declarations narrowed -- and the two legs that ARE
# cheap to read, C-4 and C-5, carry no weight at all. They are multipliers, they
# read 1.000000 on the tree the candidate is handed, and a multiplier cannot
# lift a sum of zero. The `levelonly` and `twoflag` calibration arms that priced
# this inequality at the rung below have no analogue here for that reason. And
# family C's zeroes are still the thing that must stay zero; if any of them
# stops being zero the floor moves, and it moves in the direction that breaks
# the item.
#
# THE THREE SET FILES STEP 12 CUT ARE NOT CHECKED AGAINST A COUNT TYPED HERE.
# modimport.txt, jdkimport.txt and scopedvalue.txt are inherited now and are
# read the way every other inherited set is read here -- against THIS tree, in
# the state the rung below left them in. They are held against the tally keys
# that stage beside them: thirteen of the hundred and twenty-seven
# staged-and-unread target.* keys named below, plus the ten that test.sh does
# read, plus the twenty-three b19 aliases of the same twenty-three rows. Each
# is the same population counted a second way, so that a list which quietly lost
# a line moves its own denominator with it and is caught here rather than
# reading full on a smaller question:
#
#   modimport.txt    322 rows, ONE PER MAIN-SOURCE CLASS FILE and not one per
#                    file that needs a module import, so the row set is
#                    classnames.txt read through a path-to-FQCN rewrite and is
#                    cmp'd against it. 171 rows carry `n>0` and are
#                    target.mi.files; the other 151 are target.mi.none;
#                    target.mi.imports is the 188 declarations the 171 add up
#                    to, target.mi.multi the 15 files needing more than one,
#                    target.mi.nobase the 4 needing a module that is not
#                    java.base, target.mi.mods the 6 distinct modules and
#                    target.mi.was the measured 0 on the way in
#   jdkimport.txt    495 rows, C-3's whole population and C-2's other face.
#                    Every row's verdict is re-derived here from the four-case
#                    rule the scanner applies -- static holds, nested holds,
#                    ambiguous holds, everything else departs subsumed -- and
#                    cmp'd back against the file: target.ji.total 495,
#                    target.ji.depart 452, target.ji.hold 43 splitting 25
#                    ambiguous / 16 static / 2 nested, target.ji.files 171 and
#                    target.ji.want the same 43 the hold half asks for. The
#                    per-file module sets modimport.txt states are rebuilt from
#                    THIS file's `kind=type` rows, so the two cannot disagree
#                    about what a file needs
#   scopedvalue.txt  8 rows, one per ThreadLocal FIELD declaration, cmp'd
#                    against the seed's own `decl=FIELD form=decl` threadlocal
#                    records: target.sv.fields 8, target.sv.conv the single 1
#                    whose extent is a method, target.sv.hold 7 over 6 distinct
#                    reasons, target.sv.was the measured 0. target.sv.calls is
#                    the one figure in this block with no in-image derivation --
#                    the call records carry no field name -- so it is asserted
#                    as the golden's 8 and said to be that, printed on a `fact`
#                    line and scaling nothing
#
# B-19.5 HAS NO FILE OF ITS OWN AND THAT IS THE POINT -- BUT WHAT IT READS IS A
# COUNT, NOT A LIST. "Every project that forks a test JVM" is a list step 08
# already cut and B-15.1 already scores against, so target.co.projects is held
# here against baseline/modnames.txt's 12 rows AND against target.b15.mod.total.
# What test.sh takes from it is the NUMBER and never the names: modnames.txt
# holds published artifact ids, a Gradle project path is not an artifact id --
# `:api` publishing `arquillian-api` is the ordinary arrangement and not a
# defect -- and a leg that matched the two spellings would read a tree with the
# flag on all twelve forks as a tree with none. So B-19.5 counts the projects
# its own probe found forking and floors that count at target.co.projects, which
# makes this key one of the TWO read keys inherited from the rung below that
# still score. target.co.was is the measured 0, and target.co.flag -- the other
# one -- is checked to be the only place the flag is spelled: five occurrences
# in test.sh, every one of them a comment.
#
# THIS RUNG'S OWN TWO SET FILES ARE CHECKED THE OTHER WAY ROUND, because the
# tally keys that stage beside THEM are read by nothing. Eleven `target.su.*`
# and nine `target.rd.*` keys ship in target.txt and the scorer divides by not
# one of the twenty -- C-1 counts out of suppress.txt and C-3 out of
# rawdecl.txt, which is capwant()'s rule and not an omission. So the second
# counting of suppress.txt's 44 sites over 26 files carrying 48 values, and of
# rawdecl.txt's 11 declarations in 6 open buckets over 5 files, happens in
# mk-baseline-step13.sh rather than in this image: the census columns are
# summed against the report they were cut from, the allow rows are cmp'd
# against the golden's own survivors, and the open rows are counted against the
# bucket total. NOTES 16.95 has the 59 cross-checks. What this image asserts
# about the twenty is that they are UNREAD -- they are on the staged-and-unread
# list below, and a leg that quietly started dividing by one of them would move
# off it and be caught.
#
# THE FOUR SET FILES STEP 11 CUT -- priv.txt, catchparam.txt, varhandle.txt and
# vhids.txt -- are inherited now and read the way every other inherited set is
# read here: against this tree, in the state the rung below left them in. That
# is a real change of question and not a copied block. priv.txt's 67 rows named
# sites that were PRESENT when step 11 read them and must be ABSENT here, so the
# file is read as a proven departure rather than verbatim; catchparam.txt's 93
# rows must now show 28 unnamed against 51 read and 0 unread; varhandle.txt's 14
# rows must show its 4 `verdict=convert` sites arrived and its 10 holds
# untouched; and vhids.txt's 4 ids are checked the way tbids.txt's and
# swids.txt's are. Their tallies are the twenty-two target.b18.* keys that stage
# unread, and the one that does not stage unread is B-18's single read key.
#
# THE FOUR SET FILES STEP 10 CUT -- reftest.txt's 27 rows, chain.txt's 6,
# seqfirst.txt's 16 and swids.txt's 5 ids -- are two rungs down now and are read
# in the arrived state against target.b17.*: 7 conv buckets holding 13 reflective
# type tests and 11 hold buckets holding 14, 4 pattern switches over 10 arms with
# 2 chains held, 7 conv buckets holding 7 first-element reads and 7 hold buckets
# holding 9. target.b17.rt.scanned and target.b17.sq.scanned are the 22 and 19
# the scan saw across the whole tree, and target.b17.sq.new is the 7 step 10's
# own conversions put there -- a figure that was 0 when step 10 cut the file and
# is not 0 on any tree since, which is why it is republished rather than carried.
#
# THE SIX SET FILES STEP 09 CUT are inherited too -- ipattern.txt, strnl.txt,
# sealed.txt, swarrow.txt, tbids.txt and apinorm.txt. ipattern.txt's 14 conv
# buckets named 17 sites that were `kind=plain` when step 09 read them and must
# be `kind=bound` now; its 7 skip buckets must still be plain. 14 and not step
# 09's 18 because STEP 11 RE-CUT THE FILE -- seven of step 09's buckets left with
# the methods step 11's C-2 deleted and three arrived from step 10's own
# conversions -- and this rung stages that re-cut byte for byte and re-cuts
# nothing. NOTES 16.61 is where the one C-4 shape that WOULD have moved a bucket
# was rejected for exactly that reason. strnl.txt's one move bucket held 16
# literals there and must hold none here, with the one textblock record in its
# place, while its two hold buckets must still hold their 7. sealed.txt's 11
# roots must now carry a `sealed` record naming 35 permitted subtypes between
# them and its 11 open types must still carry none. swarrow.txt's two files must
# now hold arrow switches and no colon one. Their tallies are the fifteen
# target.b16.* keys that stage unread, and tbids.txt's five ids are checked the
# way swids.txt's five are.
#
# THE THREE SET FILES STEP 08 CUT -- modnames.txt, svc.txt and svcids.txt --
# are read the same way, against this tree in the state step 08 left them in.
# modnames.txt is read twice now, once for B-15.1 and once as C-5's population,
# which is the first time on this ladder that an inherited set file is also a
# NEW leg's denominator.
#
# THE FIVE SET FILES STEP 07 CUT ARE READ IN THE ARRIVED STATE, which is the
# shape step 08 gave this block and the thing a copied agreement block would
# get wrong in five places at once. nsmove.txt, nshold.txt, toarray.txt,
# http.txt and httpids.txt were all cut from step 07's INPUT tree; the
# source.txt they are read against here is step 11's OUTPUT, five rungs further
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
# step 06 migrated, and this tree is step 06's output with steps 07 through 11
# on top of it. newinstance.txt's two legacy buckets must now hold a form=ctor
# record and no form=legacy one; builds.txt's seven buckets must hold a factory
# call and no construction; boxedctor.txt's twelve records must be gone from the
# tree entirely; varbuckets.txt's 81 must be down to 78 with step 06's one
# exemption spent -- and its 81 is NOT target.b13.candidatenew, because three of
# the 81 went with step 08's C-3, so the file's count and the key differ by an
# asserted 3. views.txt is the exception and is still DERIVED, because it is the
# one of the seven step 06 was told to leave alone.
#
# accessible.txt IS THE SECOND EXCEPTION AND IT IS NOT STEP 06'S FILE ANY MORE.
# Step 11's C-4 converted four of step 06's 14 `setAccessible` calls to
# VarHandle and three of those four sat inside step 06's 12 guard buckets, so
# step 11 re-cut the file and the two halves parted company for the first time:
# 12 guard buckets, UNCHANGED, every one of which must still hold no
# isAccessible call -- a guard coming back is a failure however the field is now
# read -- and 10 setacc buckets, down from 14, every one of which must still
# hold its call. This rung stages that re-cut byte for byte and touches no
# `setAccessible` call of its own. If B-13 is a regression gate, its populations
# have to be in the state it is holding, and this is where that is established
# rather than assumed.
#
# optops.txt AND optarr.txt ARE THE NINTH TRAP WRITTEN DOWN. optops.txt is step
# 06's departure population, three buckets; optarr.txt is step 07's arrival
# population and holds TWO of them, because step 06's blind agent reached for a
# Java 9 method at only two. B-13.6's arrival denominator is target.b13.opt.
# arrived and it is required below to equal optarr.txt's line count and NOT
# optops.txt's -- a hold gate whose denominator is what an earlier rung ASKED
# for, measured against the tree that rung's agent actually produced, charges
# this rung's candidate for that one's miss and can never read full.
#
# THE FOUR INHERITED STEP-05 SET FILES -- sinks.txt, optbuckets.txt,
# timesites.txt and valuetypes.txt -- and THE THREE INHERITED STEP-04 SET FILES
# are checked the same way step 06 checked them: in the arrived state for
# B-12's four, as B-11's aim for step 04's three. Two of step 04's three come
# forward unchanged; lambdafiles.txt does NOT, because step 11's C-2 emptied
# seven of step 04's twenty files of the only anonymous functional body their
# SecurityActions classes had, so step 11 re-cut it to 13 and this rung stages
# that 13.
# streamfiles.txt is staged with them and deliberately NOT READ -- B-11's third
# sub-gate asks its question per <path>#<method> BUCKET, and step 04's static
# review round 2 found that the same question asked per FILE was satisfiable by
# one `xs.stream().count();` per file. A read of streamfiles.txt here would be
# that regression, spelled exactly as it was spelled the first time. The image
# therefore asserts the file is PRESENT and that test.sh does not mention it,
# which is the only way a deliberate omission can be told from an accident.
#
# baseline/nioids.txt names four recordings that must still exist in
# assets/ids.txt. It is read by family A as four of 939 ids and by B-10 as the
# proviso that halves its NIO third, so an id renamed out of the recording set
# would quietly stop protecting anything in both places at once.
#
# baseline/level.txt CARRIES FOUR NUMBERS HERE AND THREE AT THE RUNG BELOW, and
# the quartet is the first thing this block establishes because three separate
# gates divide by it. `major=69` and `minor=0` are C-4's level half; `classes=435`
# is C-4's inventory half; `was=69` is B-4's regression floor, the level the tree
# arrives at -- step 12's target, which is what makes the set move together
# rather than one of them. Every rung from 03 to 08 could write one number
# because the level it held and the level it wanted were the same figure or B-4
# did not exist yet; steps 09 through 11 needed two; step 12 needed three.
#
# THE FOURTH IS `classes=` AND IT IS NEW HERE, because this is the first rung
# whose level gate can be satisfied by DELETING code. Steps 09 through 12 each
# asked for a level the tree had not reached, and a candidate who deleted a
# source file to reach it deleted the work it was being paid for. This rung asks
# the tree to HOLD a level it already has, while asking it to hold zero warnings
# and zero open raw declarations -- and the cheapest tree that satisfies both is
# the tree with the two TestNG transformers deleted. 435 is what makes that tree
# score zero on C-4 instead of 1.000000.
#
# `major` EQUALS `was` HERE AND THE ORDER ASSERTION CHANGES WITH IT. Every rung
# from 09 to 12 asserted `was` strictly BELOW `major`, because each moved the
# level and a level.txt whose `was` sat above its `major` would ask a candidate
# to go backwards. This rung moves no level, so the assertion is EQUALITY, and
# it is the stronger of the two: `-lt` would pass a level.txt that quietly asked
# for 70.
#
# THE MINOR IS STILL THE PREVIEW GUARD: a preview class file carries minor
# 65535, and a tree built with --enable-preview would satisfy a major-only C-4
# while being unbuildable by anyone without the flag. A file that carried too
# few numbers here would leave HOLD_MAJOR or WANT_MINOR empty, test.sh's own
# FATAL would catch that, and the build would ship a verifier that dies on every
# tree -- so it is caught here instead.
RUN set -eux; \
    test "$(grep -c '^major=' /tests/baseline/level.txt)" = 1; \
    test "$(grep -c '^minor=' /tests/baseline/level.txt)" = 1; \
    test "$(grep -c '^classes=' /tests/baseline/level.txt)" = 1; \
    test "$(grep -c '^was=' /tests/baseline/level.txt)" = 1; \
    grep -qxF 'major=69'    /tests/baseline/level.txt; \
    grep -qxF 'minor=0'     /tests/baseline/level.txt; \
    grep -qxF 'classes=435' /tests/baseline/level.txt; \
    grep -qxF 'was=69'      /tests/baseline/level.txt; \
    want=$(sed -n 's/^major=//p'   /tests/baseline/level.txt | head -1); \
    wmin=$(sed -n 's/^minor=//p'   /tests/baseline/level.txt | head -1); \
    wcls=$(sed -n 's/^classes=//p' /tests/baseline/level.txt | head -1); \
    held=$(sed -n 's/^was=//p'     /tests/baseline/level.txt | head -1); \
    test "${want}" = 69; \
    test "${wmin}" = 0; \
    test "${wcls}" = 435; \
    test "${held}" = 69; \
    test "${held}" -eq "${want}"; \
    test "${wmin}" -ne 65535; \
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
    tlvm=$(sed -n 's/^target\.level\.minor=//p'      ${btgt} | head -1); \
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
    # 14 `setAccessible` sites step 06 reached were still 14 for five rungs, \
    # and are 10 here, because step 11's C-4 converted four of them. The leg \
    # that HOLDS them is therefore stated over the 10 that survive, and `was` \
    # records the 14 it was stated over before that. \
    g3sb=$(sed -n 's/^target\.b13\.setacc\.buckets=//p'   ${btgt} | head -1); \
    g3sw=$(sed -n 's/^target\.b13\.setacc\.was=//p'       ${btgt} | head -1); \
    # THIS RUNG'S B-18, in the three groups its legs are scored in. EVERY KEY \
    # HERE CARRIES A `b18.` INFIX AND STEP 11'S DID NOT, which is the whole \
    # difference between a goal and a measurement: that rung published \
    # `target.priv.*`, `target.cp.*` and `target.vh.*` as the state its \
    # candidate had to reach, and this one publishes the state its seed is \
    # already in. The renaming is deliberate and asserted rather than \
    # tolerated -- a reader who found `target.cp.total` here would have no way \
    # to tell which tree it counted. \
    # \
    # AND THE FIGURES FLIPPED SIDES WITH THE NAME. Step 11's `target.cp.total` \
    # was 93, the census it was handed, and its `target.cp.want.total` was the \
    # 79 it wanted; here `target.b18.cp.total` IS 79 and the 93 has become \
    # `.was`. Same for the VarHandle leg, where `.was` and `.was.buckets` read \
    # 0 because that leg arrived out of nothing. \
    pvs=$(sed -n  's/^target\.b18\.priv\.sites=//p'      ${btgt} | head -1); \
    pvc=$(sed -n  's/^target\.b18\.priv\.calls=//p'      ${btgt} | head -1); \
    pvt=$(sed -n  's/^target\.b18\.priv\.typerefs=//p'   ${btgt} | head -1); \
    pvb=$(sed -n  's/^target\.b18\.priv\.buckets=//p'    ${btgt} | head -1); \
    pvf=$(sed -n  's/^target\.b18\.priv\.files=//p'      ${btgt} | head -1); \
    pvw=$(sed -n  's/^target\.b18\.priv\.want=//p'       ${btgt} | head -1); \
    pvwf=$(sed -n 's/^target\.b18\.priv\.want\.files=//p' ${btgt} | head -1); \
    pvh=$(sed -n  's/^target\.b18\.priv\.hold=//p'       ${btgt} | head -1); \
    pvhc=$(sed -n 's/^target\.b18\.priv\.hold\.ctor=//p' ${btgt} | head -1); \
    pvha=$(sed -n 's/^target\.b18\.priv\.hold\.acc=//p'  ${btgt} | head -1); \
    cpu=$(sed -n  's/^target\.b18\.cp\.unnamed=//p'      ${btgt} | head -1); \
    cpr=$(sed -n  's/^target\.b18\.cp\.read=//p'         ${btgt} | head -1); \
    cpx=$(sed -n  's/^target\.b18\.cp\.unread=//p'       ${btgt} | head -1); \
    cpt=$(sed -n  's/^target\.b18\.cp\.total=//p'        ${btgt} | head -1); \
    cpb=$(sed -n  's/^target\.b18\.cp\.buckets=//p'      ${btgt} | head -1); \
    cpf=$(sed -n  's/^target\.b18\.cp\.files=//p'        ${btgt} | head -1); \
    cpw=$(sed -n  's/^target\.b18\.cp\.was=//p'          ${btgt} | head -1); \
    cpws=$(sed -n 's/^target\.b18\.cp\.was\.scanned=//p' ${btgt} | head -1); \
    vhs=$(sed -n  's/^target\.b18\.vh\.sites=//p'        ${btgt} | head -1); \
    vhb=$(sed -n  's/^target\.b18\.vh\.buckets=//p'      ${btgt} | head -1); \
    vhf=$(sed -n  's/^target\.b18\.vh\.files=//p'        ${btgt} | head -1); \
    vhwas=$(sed -n 's/^target\.b18\.vh\.was=//p'         ${btgt} | head -1); \
    vhwasb=$(sed -n 's/^target\.b18\.vh\.was\.buckets=//p' ${btgt} | head -1); \
    tact=$(sed -n 's/^target\.acc\.total=//p'          ${btgt} | head -1); \
    tacv=$(sed -n 's/^target\.acc\.conv=//p'           ${btgt} | head -1); \
    tach=$(sed -n 's/^target\.acc\.hold=//p'           ${btgt} | head -1); \
    tacf=$(sed -n 's/^target\.acc\.files=//p'          ${btgt} | head -1); \
    # NONE OF THE TWENTY-THREE IS ALLOWED TO BE EMPTY, and that is checked here \
    # rather than left to the assertions that read them. A key target.txt does \
    # not carry binds to the empty string, and `test "" = 0` is false, so a \
    # misspelling WOULD be caught -- but by a line that says a population is \
    # the wrong size, which is the wrong diagnosis for a typo in a key name. \
    for v in "${pvs}" "${pvc}" "${pvt}" "${pvb}" "${pvf}" "${pvw}" "${pvwf}" \
             "${pvh}" "${pvhc}" "${pvha}" \
             "${cpu}" "${cpr}" "${cpx}" "${cpt}" "${cpb}" "${cpf}" \
             "${cpw}" "${cpws}" \
             "${vhs}" "${vhb}" "${vhf}" "${vhwas}" "${vhwasb}"; do \
        case "${v}" in \
            ''|*[!0-9]*) echo "a target.b18.* key is missing from target.txt" >&2; exit 1;; \
        esac; \
    done; \
    # AND B-19'S FOUR LEGS, twenty-two numbers and one string under the four \
    # prefixes step 12 cut for its own family C: `mi` for B-19.2's module \
    # imports, `ji` for B-19.3's single-type imports, `sv` for B-19.4's scoped \
    # value, `co` for B-19.5's compact headers. THEY ARE READ UNDER THOSE NAMES \
    # AND NOT UNDER target.b19., because an inherited key is never renamed -- \
    # the rule step 12 followed for step 11's `acc` keys, and the rule \
    # mk-baseline-step13.sh states where it writes the aliases. The b19 block \
    # is a SECOND publication of these same twenty-three values, checked \
    # against them a hundred lines down rather than standing in for them. \
    # \
    # The guard is repeated for the twenty-three because a prefix only one rung \
    # writes is exactly where a typo goes unnoticed: nothing else in target.txt \
    # begins `target.mi.`, so a misspelling binds the empty string and every \
    # count it feeds reads zero against a population of zero. target.co.flag, \
    # the one value here that is not a number, is guarded as the flag it has to \
    # be instead of being let through the numeric case. \
    tmif=$(sed -n  's/^target\.mi\.files=//p'           ${btgt} | head -1); \
    tmin=$(sed -n  's/^target\.mi\.none=//p'            ${btgt} | head -1); \
    tmii=$(sed -n  's/^target\.mi\.imports=//p'         ${btgt} | head -1); \
    tmim=$(sed -n  's/^target\.mi\.multi=//p'           ${btgt} | head -1); \
    tmib=$(sed -n  's/^target\.mi\.nobase=//p'          ${btgt} | head -1); \
    tmimo=$(sed -n 's/^target\.mi\.mods=//p'            ${btgt} | head -1); \
    tmiw=$(sed -n  's/^target\.mi\.was=//p'             ${btgt} | head -1); \
    tjit=$(sed -n  's/^target\.ji\.total=//p'           ${btgt} | head -1); \
    tjid=$(sed -n  's/^target\.ji\.depart=//p'          ${btgt} | head -1); \
    tjih=$(sed -n  's/^target\.ji\.hold=//p'            ${btgt} | head -1); \
    tjia=$(sed -n  's/^target\.ji\.hold\.ambiguous=//p' ${btgt} | head -1); \
    tjis=$(sed -n  's/^target\.ji\.hold\.static=//p'    ${btgt} | head -1); \
    tjin=$(sed -n  's/^target\.ji\.hold\.nested=//p'    ${btgt} | head -1); \
    tjif=$(sed -n  's/^target\.ji\.files=//p'           ${btgt} | head -1); \
    tjiw=$(sed -n  's/^target\.ji\.want=//p'            ${btgt} | head -1); \
    tsvf=$(sed -n  's/^target\.sv\.fields=//p'          ${btgt} | head -1); \
    tsvc=$(sed -n  's/^target\.sv\.conv=//p'            ${btgt} | head -1); \
    tsvh=$(sed -n  's/^target\.sv\.hold=//p'            ${btgt} | head -1); \
    tsvk=$(sed -n  's/^target\.sv\.calls=//p'           ${btgt} | head -1); \
    tsvw=$(sed -n  's/^target\.sv\.was=//p'             ${btgt} | head -1); \
    tcop=$(sed -n  's/^target\.co\.projects=//p'        ${btgt} | head -1); \
    tcow=$(sed -n  's/^target\.co\.was=//p'             ${btgt} | head -1); \
    tcof=$(sed -n  's/^target\.co\.flag=//p'            ${btgt} | head -1); \
    for v in "${tmif}" "${tmin}" "${tmii}" "${tmim}" "${tmib}" "${tmimo}" "${tmiw}" \
             "${tjit}" "${tjid}" "${tjih}" "${tjia}" "${tjis}" "${tjin}" \
             "${tjif}" "${tjiw}" \
             "${tsvf}" "${tsvc}" "${tsvh}" "${tsvk}" "${tsvw}" \
             "${tcop}" "${tcow}"; do \
        case "${v}" in \
            ''|*[!0-9]*) echo "a target.mi/ji/sv/co key is missing from target.txt" >&2; exit 1;; \
        esac; \
    done; \
    case "${tcof}" in -XX:+*) :;; *) echo "target.co.flag is not a JVM flag: '${tcof}'" >&2; exit 1;; esac; \
    # Every key test.sh reads is a key target.txt carries -- and at this rung \
    # the converse is deliberately NOT true, which is why the two directions \
    # are asserted separately instead of with one cmp. 197 keys stage and 70 \
    # are read through tv(). A key the scorer reads and the file does not carry \
    # reads as the empty string and defaults to 0, so that direction is an \
    # error with no exceptions. The other direction is a hundred and \
    # twenty-seven keys wide here, and it divides four ways. \
    # \
    # SIXTY-SEVEN ARE A POPULATION COUNTED A SECOND WAY -- the same sixty-seven \
    # the rung below checked in this place. Nothing about the scoring changed: \
    # neither family C's five legs nor B-16's, B-17's, B-18's or B-19's divides \
    # by a count in target.txt. They divide by the POPULATION FILES -- \
    # modimport.txt, jdkimport.txt and scopedvalue.txt for B-19, with B-19.5 \
    # the one exception on the ladder, dividing by a COUNT rather than a file \
    # because its population is Gradle projects and not rows of anything; \
    # priv.txt, catchparam.txt, varhandle.txt and accessible.txt for B-18; \
    # reftest.txt, chain.txt, seqfirst.txt and swids.txt for B-17; ipattern.txt, \
    # strnl.txt, sealed.txt, swarrow.txt and tbids.txt for B-16 -- because each \
    # leg is scored per bucket and a bucket list is not a number. The \
    # sixty-seven are those populations counted, frozen beside them so that \
    # this image can check a list against its own tally: `target.mi.none=151` \
    # is how many of modimport.txt's 322 rows must still say `n=0` when B-19.2 \
    # is done, `target.ji.hold=43` is how many of jdkimport.txt's 495 imports \
    # may not be deleted, `target.b18.priv.buckets=27` is what priv.txt's 67 \
    # rows must fall into, and `target.b18.cp.unnamed=28` is how many of \
    # catchparam.txt's 93 rows can arrive as `_`. A population file that lost a \
    # line would move its own denominator with it and the gate would read full \
    # on a smaller question -- the ninth trap, and the reason the tally lives in \
    # a second file. Every one of the sixty-seven is asserted below against the \
    # list it counts; none is merely staged. \
    # \
    # THREE ARE target.level, target.level.minor AND target.level.was, unread \
    # for the reason the pair was unread at 09, 10 and 11 and the trio at 12: \
    # C-4 reads the major it wants out of baseline/level.txt, which is the file \
    # the scorer parses, and the target.txt trio exists only so that this image \
    # can hold the two files against each other -- the assertion at the foot of \
    # this stanza. THE MINOR IS NOT NEW HERE and it is still not a constant: \
    # 65535 marks a preview class file, and a tree built with --enable-preview \
    # lands on the right major with the wrong minor. What IS new is that C-4 \
    # asks for no move at all. `was` and the major are both 69, so this is the \
    # one rung on the ladder where the level a candidate must not change is the \
    # level it was handed, and the trio is what lets this image say so out loud \
    # instead of leaving it to be inferred from two equal numbers. \
    # \
    # TWENTY-THREE ARE THE b19 ALIASES, and they are unread by construction. \
    # mk-baseline-step13.sh republishes step 12's mi, ji, sv and co keys under \
    # the prefix every rung has given the rung below it -- step 10's family C \
    # became b17, step 11's became b18 -- WITHOUT deleting the originals, \
    # because test.sh reads the originals and an inherited key is never \
    # renamed. That leaves twenty-three pairs which have to agree, and a pair \
    # that disagreed would be the worst shape this file can take: two published \
    # denominators for one population, with the scorer reading whichever of the \
    # two the alias is not. The pairing is asserted value by value below, which \
    # is the whole reason the duplication is allowed to exist. \
    # \
    # AND THIRTY-FOUR ARE THIS RUNG'S OWN, the block with no precedent below \
    # it. C-1 AND C-3 READ NOTHING FROM target.txt AT ALL: eleven `target.su.*` \
    # keys and nine `target.rd.*` keys stage and not one of the twenty is read, \
    # because both legs count out of the population file -- suppress.txt and \
    # rawdecl.txt -- which is capwant()'s rule and not an omission. C-2 reads \
    # two of its twelve and C-5 two of its six. All thirty-four are census: \
    # what the honest tree warns (36), what the delivered tree warns (11), what \
    # the golden warns (0), what the four allowed annotations force (6), the \
    # six lint families behind the 36, the 44 suppression sites over 26 files \
    # and the 48 values on them, rawdecl.txt's 11 rows in 6 buckets over 5 \
    # files, and the four behaviour totals. They are published because a reader \
    # of a scored run has to be able to check C-1's arithmetic without the \
    # tree, and they are unread because a leg that divided by one of them would \
    # be scoring a tree against a number the tree cannot move. \
    # \
    # TWO OF THE HUNDRED AND NINETY-SEVEN KEY NAMES ARE NOT [a-z0-9.], and that \
    # is why the pattern below carries a hyphen. javac names its own lint \
    # categories and target.w.fam.* spells them as javac does, so \
    # `dangling-doc-comments` and `this-escape` are key names with a `-` in \
    # them, the first on this ladder. A pattern of [a-z0-9.]* does not fail on \
    # those two -- it TRUNCATES them, to target.w.fam.dangling and \
    # target.w.fam.this. The inventory is then two keys short, the truncations \
    # are missing from the unread list, and the numeric loop reads the empty \
    # string for both and reports them absent from a file that carries them. \
    # Every one of those symptoms points somewhere other than a character \
    # class, which is why the class is named here rather than left to be read \
    # off the line. \
    # \
    # TWENTY-TWO KEYS OF THE INHERITED FAMILIES ARE READ, and they split in \
    # two. TWELVE are the ones the rung below checked here. Seven are SCORED: \
    # target.b16.ip.total, target.b16.sealed.roots, target.b16.sealed.open, \
    # target.b16.sw.files and target.b13.setacc, each a term that is a count \
    # rather than a list, target.b17.ch.ids, which divides B-17.3's execution \
    # scaling, and target.b13.setacc.buckets, which arrived at the rung below \
    # because step 12's C-4 MOVED an inherited denominator: B-13.3's arrival \
    # half used to be asked over the same twelve guard buckets as its departure \
    # half, and cannot be, because three of the four calls step 11 converted sit \
    # in guard buckets and a correct tree would cap that half at 9/12. The other \
    # five -- target.b17.rt.scanned, target.b17.sq.scanned, target.b17.sq.new, \
    # target.b18.priv.sites and target.acc.total -- are read only to be PRINTED, \
    # as the `.was` half of a fact line, so that a trajectory shows what a \
    # figure stood at before the rung that moved it. \
    # \
    # THE OTHER TEN ARE B-19'S, read as its own by the rung below and inherited \
    # here unchanged. TWO of them score. target.co.flag is the flag the leg \
    # greps the fork arguments for, and it is not a count at all: test.sh binds \
    # it to a shell variable and everything B-19.5 does is downstream of that \
    # string. target.co.projects is that leg's FLOOR: it counts the projects its \
    # own probe found forking and divides by the larger of that and this key, so \
    # a tree that deleted its Test tasks scores 0 out of twelve rather than 1 \
    # out of the empty set it left behind. It is a count and not a list on \
    # purpose -- see B-19.5 HAS NO FILE OF ITS OWN above. The remaining eight -- \
    # target.mi.was, target.mi.imports, target.ji.total and its three hold \
    # reasons, target.sv.calls and target.co.was -- are read ONLY to be printed, \
    # each on the right-hand side of a `fact` line. \
    # \
    # AND FIVE KEYS OF THIS RUNG'S OWN FIVE LEGS ARE READ, ALL FIVE OF WHICH \
    # SCORE. There is no printed-only key among them, and that absence is the \
    # difference between a rung that moves a figure and a rung that closes one: \
    # nothing here has a `.was` worth showing, because C-4's answer is that \
    # nothing moved and the other four legs are measured for the first and the \
    # last time in the same run. target.level.classes is C-4's inventory half, \
    # target.w.fixable is C-2's denominator, target.w.lint is the compile line \
    # C-2 requires the build to carry, and target.be.ids and target.be.xmls are \
    # C-5's two population counts. \
    # \
    # ALL OF THAT IS ASSERTED AND NOT JUST DESCRIBED. Each of the THIRTEEN \
    # fact-only keys occurs in test.sh exactly once, on a line that begins \
    # `fact "`; each of the SEVEN named here as scoring occurs exactly once on a \
    # line that does not. A leg that started dividing by one of the thirteen \
    # would be scoring against a figure this image has no derivation for -- \
    # target.sv.calls is counted on the golden and nowhere else -- and the loops \
    # below are what would notice. \
    test "$(grep -c '^target\.' ${btgt})" = 197; \
    grep -o '^target\.[a-z0-9.-]*' ${btgt} | LC_ALL=C sort > /tmp/tkeys.txt; \
    sed -n "s/.*tv 'target\\(.*\\)').*/target\\1/p" /tests/test.sh \
      | tr -d '\\' | LC_ALL=C sort -u > /tmp/rkeys.txt; \
    test "$(grep -c . /tmp/rkeys.txt)" = 70; \
    if [ "$(LC_ALL=C comm -13 /tmp/tkeys.txt /tmp/rkeys.txt | grep -c .)" != 0 ]; then \
        echo "test.sh reads a target.* key baseline/target.txt does not carry:" >&2; \
        LC_ALL=C comm -13 /tmp/tkeys.txt /tmp/rkeys.txt >&2; exit 1; \
    fi; \
    LC_ALL=C comm -23 /tmp/tkeys.txt /tmp/rkeys.txt > /tmp/ukeys.txt; \
    printf '%s\n' target.acc.conv target.acc.files target.acc.hold \
                  target.b13.setacc.was \
                  target.b16.ip.buckets target.b16.ip.sites \
                  target.b16.ip.skip target.b16.ip.skip.buckets \
                  target.b16.ip.was.buckets target.b16.ip.was.sites \
                  target.b16.ip.was.total \
                  target.b16.sealed.permits target.b16.sealed.types \
                  target.b16.sw.total \
                  target.b16.tb.hold.buckets target.b16.tb.hold.sites \
                  target.b16.tb.ids target.b16.tb.move.buckets \
                  target.b16.tb.move.sites \
                  target.b17.ch.arms target.b17.ch.conv target.b17.ch.hold \
                  target.b17.rt.conv.buckets target.b17.rt.conv.sites \
                  target.b17.rt.hold.buckets target.b17.rt.hold.sites \
                  target.b17.rt.total \
                  target.b17.sq.conv.buckets target.b17.sq.conv.sites \
                  target.b17.sq.hold.buckets target.b17.sq.hold.sites \
                  target.b17.sq.total \
                  target.b18.cp.buckets target.b18.cp.files \
                  target.b18.cp.read target.b18.cp.total \
                  target.b18.cp.unnamed target.b18.cp.unread \
                  target.b18.cp.was target.b18.cp.was.scanned \
                  target.b18.priv.buckets target.b18.priv.calls \
                  target.b18.priv.files target.b18.priv.hold \
                  target.b18.priv.hold.acc target.b18.priv.hold.ctor \
                  target.b18.priv.typerefs target.b18.priv.want \
                  target.b18.priv.want.files \
                  target.b18.vh.buckets target.b18.vh.files \
                  target.b18.vh.sites target.b18.vh.was \
                  target.b18.vh.was.buckets \
                  target.b19.co.flag target.b19.co.projects \
                  target.b19.co.was \
                  target.b19.ji.depart target.b19.ji.files \
                  target.b19.ji.hold target.b19.ji.hold.ambiguous \
                  target.b19.ji.hold.nested target.b19.ji.hold.static \
                  target.b19.ji.total target.b19.ji.want \
                  target.b19.mi.files target.b19.mi.imports \
                  target.b19.mi.mods target.b19.mi.multi \
                  target.b19.mi.nobase target.b19.mi.none \
                  target.b19.mi.was \
                  target.b19.sv.calls target.b19.sv.conv \
                  target.b19.sv.fields target.b19.sv.hold \
                  target.b19.sv.was \
                  target.be.errors target.be.failures target.be.skipped \
                  target.be.tests \
                  target.ji.depart target.ji.files target.ji.hold \
                  target.ji.want \
                  target.level target.level.minor target.level.was \
                  target.mi.files target.mi.mods target.mi.multi \
                  target.mi.nobase target.mi.none \
                  target.rd.buckets target.rd.files target.rd.forced \
                  target.rd.maxbucket target.rd.open \
                  target.rd.scanned.buckets target.rd.scanned.files \
                  target.rd.size target.rd.want \
                  target.su.budget target.su.budget.added \
                  target.su.budget.kept target.su.extras target.su.files \
                  target.su.pairs target.su.sites target.su.value.rawtypes \
                  target.su.value.unchecked target.su.value.unused \
                  target.su.want.unused \
                  target.sv.conv target.sv.fields target.sv.hold \
                  target.sv.was \
                  target.w.delivered target.w.fam.cast \
                  target.w.fam.dangling-doc-comments target.w.fam.rawtypes \
                  target.w.fam.serial target.w.fam.this-escape \
                  target.w.fam.unchecked target.w.forced target.w.honest \
                  target.w.want \
      | LC_ALL=C sort > /tmp/uwant.txt; \
    test "$(grep -c . /tmp/uwant.txt)" = 127; \
    if ! cmp -s /tmp/ukeys.txt /tmp/uwant.txt; then \
        echo "the set of staged-and-unread target.* keys is not the hundred and twenty-seven this rung means:" >&2; \
        diff /tmp/uwant.txt /tmp/ukeys.txt >&2 || true; exit 1; \
    fi; \
    # THE TWENTY-THREE b19 ALIASES AGAINST THE TWENTY-THREE THEY ALIAS. Every \
    # target.b19.X must carry the same value as target.X, and target.X must not \
    # be empty -- a sed that matched nothing would otherwise make two absent \
    # keys agree. This is the assertion the paragraph above promises, and it is \
    # the only thing that makes a second copy of a denominator safe. \
    for k in $(grep -o '^target\.b19\.[a-z0-9.]*' ${btgt}); do \
        o=$(printf '%s\n' "${k}" | sed 's/^target\.b19\./target./'); \
        av=$(sed -n "s/^${k}=//p" ${btgt} | head -1); \
        ov=$(sed -n "s/^${o}=//p" ${btgt} | head -1); \
        if [ -z "${ov}" ] || [ "${av}" != "${ov}" ]; then \
            echo "b19 alias ${k}='${av}' does not match ${o}='${ov}'" >&2; exit 1; \
        fi; \
    done; \
    test "$(grep -c '^target\.b19\.' ${btgt})" = 23; \
    # The thirteen fact-only keys and the seven that score. The patterns below \
    # spell the key with `.` where test.sh writes `\.`, because the dot in a \
    # tv() argument is escaped for sed and a literal match would have to carry \
    # the backslash through this file's own escaping twice. \
    for p in 'target..mi..was' 'target..mi..imports' 'target..ji..total' \
             'target..ji..hold..static' 'target..ji..hold..nested' \
             'target..ji..hold..ambiguous' 'target..sv..calls' \
             'target..co..was' 'target..acc..total' 'target..b18..priv..sites' \
             'target..b17..rt..scanned' 'target..b17..sq..scanned' \
             'target..b17..sq..new'; do \
        test "$(grep -c "tv '${p}'" /tests/test.sh)" = 1; \
        test "$(grep "tv '${p}'" /tests/test.sh | grep -c '^ *fact "')" = 1; \
    done; \
    for p in 'target..co..flag' 'target..co..projects' 'target..level..classes' \
             'target..w..fixable' 'target..w..lint' 'target..be..ids' \
             'target..be..xmls'; do \
        test "$(grep -c "tv '${p}'" /tests/test.sh)" = 1; \
        test "$(grep "tv '${p}'" /tests/test.sh | grep -c '^ *fact "')" = 0; \
    done; \
    # 193 of the 197 are numbers. FOUR are not, and they are the only keys on \
    # the ladder whose value is neither a count nor a level. target.b14.c5.file \
    # names a FILE, because B-14.5 is scoped by file for the reason test.sh \
    # gives where it reads it, so the key names the file rather than counting \
    # anything. target.co.flag names a JVM flag for the same kind of reason -- \
    # B-19.5 greps the fork arguments for whatever it says. target.b19.co.flag \
    # is that flag's alias, and it is exempted here BY ITS OWN NAME rather than \
    # by a pattern over the prefix, so that an alias which stopped being a flag \
    # would be caught by the pairing loop above and not quietly waved through \
    # by the exemption. And target.w.lint is C-2's REQUIRED COMPILE LINE, two \
    # options with a space between them: the first value in any target.txt on \
    # this ladder that is not a single token, which is why the numeric case \
    # would report it missing rather than malformed. All four are checked as \
    # what they are, which is stricter than the loop they escape: a typo in any \
    # of them would aim a whole leg at something that does not exist and every \
    # count in this image would still agree. \
    for k in $(cat /tmp/tkeys.txt); do \
        [ "${k}" = "target.b14.c5.file" ] && continue; \
        [ "${k}" = "target.co.flag" ] && continue; \
        [ "${k}" = "target.b19.co.flag" ] && continue; \
        [ "${k}" = "target.w.lint" ] && continue; \
        v=$(sed -n "s/^${k}=//p" ${btgt} | head -1); \
        case "${v}" in ''|*[!0-9]*) echo "${k} is not a number: '${v}'" >&2; exit 1;; esac; \
    done; \
    tlint=$(sed -n 's/^target\.w\.lint=//p' ${btgt} | head -1); \
    case "${tlint}" in \
        '-Xlint:all -Xlint:-deprecation') :;; \
        *) echo "target.w.lint is not the compile line C-2 requires: '${tlint}'" >&2; exit 1;; \
    esac; \
    rm -f /tmp/tkeys.txt /tmp/rkeys.txt /tmp/ukeys.txt /tmp/uwant.txt; \
    case "${c5f}" in *"/src/main/java/"*.java) :;; *) echo "target.b14.c5.file is not a src/main source path: ${c5f}" >&2; exit 1;; esac; \
    # This rung's own three level keys, and the three numbers they must equal. \
    # baseline/level.txt is what the SCORER reads and target.txt is what this \
    # image checks it against; the two files are written by different halves of \
    # mk-baseline-step12.sh and a rung that moved one and not the other would \
    # grade a level nothing here asked for. THE MINOR IS THE THIRD BECAUSE 25 \
    # IS THE FIRST RUNG THAT HAS ONE TO STATE: every release below it left the \
    # minor at 0 and nothing on the ladder had to say so, and here 0 is a claim \
    # -- that the tree was built without --enable-preview, which would write \
    # 65535 into the same two bytes and leave the major exactly where C-1 wants \
    # it. \
    test "${tlvl}" = "${want}"; \
    test "${tlvm}" = "${wmin}"; \
    test "${tlvw}" = "${held}"; \
    # What the input tree HOLDS, and must go on holding: steps 03, 04, 05 and 06. \
    # \
    # FORTY-EIGHT OF THE FIFTY-ONE ARE ASSERTED AT THE VALUE STEPS 09 THROUGH \
    # 12 ASSERT THEM AT, because nothing in those four rungs touches any of \
    # them. That is the whole strength of the check: a tree that had drifted \
    # anywhere would have moved one, and the rung below's own staged source.txt \
    # is the reference every one of them is read against. \
    # \
    # THE OTHER THREE MOVED, AND THEY MOVED BECAUSE THIS RUNG'S INPUT IS THE \
    # RUNG BELOW'S OUTPUT -- the same sentence the rung below had to write in \
    # this place, and it is written here rather than left to the reader because \
    # three stale numbers in a list of fifty-one look exactly like forty-eight \
    # correct ones. \
    # \
    # ALL THREE ARE ONE EDIT. B-19.4 converted the one convertible ThreadLocal \
    # in the tree -- ManagerImpl's handledThrowables, the single `verdict=convert` \
    # row in baseline/scopedvalue.txt -- and the field it replaced was declared \
    # as an anonymous subclass with its type argument written out: \
    # \
    #   new ThreadLocal<Set<Class<? extends Throwable>>>() { ... initialValue ... } \
    # \
    # became `ScopedValue.newInstance()`, with the extent opened by \
    # `ScopedValue.where(handledThrowables, new HashSet<>()).run(() -> fire(...))`. \
    # So ONE anonymous class carrying an explicit type argument left the tree -- \
    # newgeneric.explicitanon 13 -> 12, and newgeneric.explicit 36 -> 35 with it, \
    # because the anonymous count is a subset of the explicit one -- and ONE \
    # lambda expression arrived, lambda.size 49 -> 50. \
    # \
    # lambda.files DOES NOT MOVE WITH IT, and that is the assertion in this \
    # group worth reading twice. The new lambda landed in ManagerImpl.java, \
    # which already held lambdas, so 27 is the right answer and a file count \
    # that had ALSO gone up by one would mean the extent was opened in some \
    # other file than the one whose field it binds. Asserting the three at \
    # their old values would be asserting that B-19.4 did not run, so they are \
    # asserted at their new ones and the old ones are kept here in the prose, \
    # where the subtraction can be read. \
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
    test "${exp}" = 35; \
    test "${ean}" = 12; \
    test "${res}" = 7; \
    test "${clo}" = 0; \
    test "${raw}" = 0; \
    test "${sup}" = 44; \
    test "${lam}" = 50; \
    test "${lamf}" = 27; \
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
    test "${acst}" = 10; \
    test "${acs}" = 10; \
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
    # sealed hierarchies and arrow switches, and it got all four -- and then two \
    # LATER rungs moved one of those populations in OPPOSITE DIRECTIONS, which \
    # is why the two `instanceof` figures below are neither step 09's nor step \
    # 10's. Step 10's C-2 rewrote three reflective type tests as bound patterns \
    # and step 11's C-2 took seven away: the `SecurityActions#getConstructor` \
    # ladders, one bound pattern over a `NoSuchMethodException` each, which left \
    # with the privileged wrappers the classes no longer make. 28 + 3 - 7 = 24 \
    # sites are scanned here and 21 + 3 - 7 = 17 of them are bound. The 7 plain \
    # ones are step 09's same 7 and not one is convertible -- a deletion that \
    # had caught a plain `instanceof` on the way past would move that figure and \
    # not these two. The one HTML document is a text block, the XML builder's \
    # newline literals are down to 7 in one file, eleven roots are sealed over \
    # 35 permitted subtypes between them, and the switch count is 6 rather than \
    # step 09's 2 -- the four step 10 wrote are pattern switches and they are \
    # arrow form too, so `colon` is still 0. These are the figures test.sh's \
    # B-16 header says were re-measured on this rung's seed, asserted here so \
    # that the header is a claim with a check under it. \
    # \
    # THE 31 AND THE 24 ARE STILL IN THE FILE AND THEY ARE NOT THIS SCAN. \
    # target.b16.ip.was.* carries 21 / 24 / 31 -- the state the seed was in \
    # before step 11 deleted anything -- and the section that reads \
    # baseline/ipattern.txt further down asserts those three against the frozen \
    # file rather than against this tree. Getting the two confused is the whole \
    # of the eighth trap: a held leg whose denominator silently became the \
    # history of the tree instead of the tree. \
    test "$(sed -n 's/^ipattern\.size=//p'        ${bsrc} | head -1)" = 24; \
    test "$(sed -n 's/^ipattern\.bound=//p'       ${bsrc} | head -1)" = 17; \
    test "$(sed -n 's/^ipattern\.plain=//p'       ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^ipattern\.convertible=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^ipattern\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=bound')" = 17; \
    test "$(sed -n 's/^ipattern\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 24; \
    test "${ipt}" = 24; \
    test "${ipwt}" = 31; \
    test "$(( ipwt - ipt ))" = 7; \
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
    # ...and step 10's own work, in the ARRIVED state, because B-17 is where it \
    # now lives. Step 11 was the rung that made that block and this is the \
    # second rung to inherit it, which is the reason every figure below is \
    # asserted again rather than taken as settled: a population that survived \
    # one rung untouched is exactly the kind a second rung stops checking. Step \
    # 10 asked for type patterns in place of reflective type tests, pattern \
    # switches in place of dispatch chains, and sequenced-collection calls in \
    # place of index-0 reads, and it got all three. Every one of these figures \
    # is a POPULATION THAT SHRANK or an arrival that appeared, and the \
    # shrinkage is the proof rather than a side effect: reftest 35 -> 22, the 13 \
    # converted sites no longer being reflective type tests at all; seqfirst \
    # 26 -> 19 with seqnew standing at the 7 that replaced them, and `guarded` \
    # down from 8 to 1 because ALL SEVEN converted reads were guarded ones -- \
    # the single guard left is DynamicServiceLoader.onlyOne, an iterator-next \
    # over a Set rather than an index-0 read, which is why step 10's C-4 never \
    # asked about it; swpat at 4 switches over 10 type-pattern labels with one \
    # `case null`, where the same scan measured 0, 0 and 0 two rungs ago. \
    # Step 11 touched none of the four and neither does this rung. \
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
    # ...and step 11's own work, in the ARRIVED state, because B-18 is where it \
    # now lives and THIS is the first rung at which that block exists. The rung \
    # below asked for three things a source scan can see -- the privileged-API \
    # wrappers unwrapped, the unread catch parameters written `_`, and a \
    # reflective field write replaced by a VarHandle -- and it got all three. \
    # Its fourth leg was a class-file major and it is not here: it left family C \
    # as a level and arrived as B-4's HOLD_MAJOR, which is the subtraction this \
    # ladder has made at every rung and the reason B-18 is renormalised over the \
    # 0.96 its three surviving legs carried between them. \
    # \
    # THE THREE READINGS BELOW ARE THE THREE SHAPES AN ARRIVED LEG CAN TAKE, \
    # and they are worth naming because a single spelling of "done" would hide \
    # two of them. \
    # \
    #   A POPULATION THAT EMPTIED. priv.* reads 0 on every counter it has: 67 \
    #   sites in 27 methods across 7 files at step 11, none now. The departure \
    #   was total, so the leg's evidence on this tree is an ABSENCE, and an \
    #   absence is what an empty scan also looks like -- which is why the 67 is \
    #   asserted where it still exists, in target.b18.priv.sites, rather than \
    #   being left as a memory. \
    # \
    #   A POPULATION THAT PARTITIONED. catchparam.* still holds 89 clauses; what \
    #   moved is which kind they are. 28 are written `_` where the tree had zero, \
    #   and the 39 unread ones are down to 4. The 4 are not a shortfall: every \
    #   one of them is in src/test, outside the main-sources cut baseline/ \
    #   catchparam.txt was taken from and outside every bucket the leg scores, \
    #   and BOTH facts are asserted below because a test-scope clause quietly \
    #   becoming a main one would move the second and not the first. \
    # \
    #   A POPULATION THAT DID NOT EXIST. varhandle.* reads 8 where it read 0. \
    #   There is no departure half to see, because the tree never had a \
    #   VarHandle; the whole leg is an arrival, and the only check a source scan \
    #   can make is that the arrival is still standing in the same 4 methods. \
    # \
    # AND accessible.setaccessible IS THE ONE INHERITED COUNTER THIS MOVED. \
    # Step 06 counted 14 `setAccessible` calls; step 11's C-4 turned 4 of them \
    # into VarHandle writes, so the tree reads 10 and target.b13.setacc is 10 to \
    # match. 14 survives as target.b13.setacc.was and the difference between the \
    # two is exactly the 4 methods varhandle.* names. NOTES 16.16 and 16.61. \
    test "$(sed -n 's/^priv\.size=//p'    ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\.methods=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\.files=//p'   ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 0; \
    test "$(sed -n 's/^target\.b18\.priv\.sites=//p'    ${btgt} | head -1)" = 67; \
    test "$(sed -n 's/^target\.b18\.priv\.buckets=//p'  ${btgt} | head -1)" = 27; \
    test "$(sed -n 's/^target\.b18\.priv\.files=//p'    ${btgt} | head -1)" = 7; \
    test "$(sed -n 's/^target\.b18\.priv\.want=//p'     ${btgt} | head -1)" = 0; \
    test "$(sed -n 's/^target\.b18\.priv\.calls=//p'    ${btgt} | head -1)" = 20; \
    test "$(sed -n 's/^target\.b18\.priv\.typerefs=//p' ${btgt} | head -1)" = 47; \
    test "$(( $(sed -n 's/^target\.b18\.priv\.calls=//p' ${btgt} | head -1) \
            + $(sed -n 's/^target\.b18\.priv\.typerefs=//p' ${btgt} | head -1) ))" \
         = "$(sed -n 's/^target\.b18\.priv\.sites=//p' ${btgt} | head -1)"; \
    test "$(sed -n 's/^catchparam\.size=//p'    ${bsrc} | head -1)" = 89; \
    test "$(sed -n 's/^catchparam\.unnamed=//p' ${bsrc} | head -1)" = 28; \
    test "$(sed -n 's/^catchparam\.read=//p'    ${bsrc} | head -1)" = 57; \
    test "$(sed -n 's/^catchparam\.unread=//p'  ${bsrc} | head -1)" = 4; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 89; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=unnamed')" = 28; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
              | grep 'kind=unread' | grep -c '^src/test/\|/src/test/')" = 4; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
              | grep 'kind=unnamed' | grep -c '^src/test/\|/src/test/')" = 0; \
    test "$(sed -n 's/^target\.b18\.cp\.unnamed=//p' ${btgt} | head -1)" = 28; \
    test "$(sed -n 's/^target\.b18\.cp\.unread=//p'  ${btgt} | head -1)" = 0; \
    test "$(sed -n 's/^target\.b18\.cp\.read=//p'    ${btgt} | head -1)" = 51; \
    test "$(sed -n 's/^target\.b18\.cp\.total=//p'   ${btgt} | head -1)" = 79; \
    test "$(( $(sed -n 's/^target\.b18\.cp\.unnamed=//p' ${btgt} | head -1) \
            + $(sed -n 's/^target\.b18\.cp\.read=//p'    ${btgt} | head -1) \
            + $(sed -n 's/^target\.b18\.cp\.unread=//p'  ${btgt} | head -1) ))" \
         = "$(sed -n 's/^target\.b18\.cp\.total=//p' ${btgt} | head -1)"; \
    test "$(( $(sed -n 's/^catchparam\.size=//p' ${bsrc} | head -1) \
            - $(sed -n 's/^target\.b18\.cp\.total=//p' ${btgt} | head -1) ))" = 10; \
    test "$(sed -n 's/^varhandle\.size=//p'    ${bsrc} | head -1)" = 8; \
    test "$(sed -n 's/^varhandle\.arrival=//p' ${bsrc} | head -1)" = 8; \
    test "$(sed -n 's/^varhandle\.methods=//p' ${bsrc} | head -1)" = 4; \
    test "$(sed -n 's/^varhandle\.files=//p'   ${bsrc} | head -1)" = 4; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 8; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=arrival')" = 8; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c 'call=privateLookupIn')" = 4; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c 'call=unreflectVarHandle')" = 4; \
    test "$(sed -n 's/^target\.b18\.vh\.sites=//p'   ${btgt} | head -1)" = 8; \
    test "$(sed -n 's/^target\.b18\.vh\.buckets=//p' ${btgt} | head -1)" = 4; \
    test "$(sed -n 's/^target\.b18\.vh\.was=//p'     ${btgt} | head -1)" = 0; \
    test "$(sed -n 's/^accessible\.setaccessible=//p' ${bsrc} | head -1)" = 10; \
    test "$(sed -n 's/^accessible\.isaccessible=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^newinstance\.ctor=//p'   ${bsrc} | head -1)" = 13; \
    test "$(sed -n 's/^newinstance\.legacy=//p' ${bsrc} | head -1)" = 0; \
    # ...and what the RUNG BELOW HAS DONE, which this rung must hold. B-19 is \
    # step 12's family C and its work is IN THE SEED: this is the first block \
    # in this file where the inherited family's counters are read at their \
    # ARRIVED values rather than at zero, because step 13's input is step 12's \
    # blind workspace and that run scored 0.999372. \
    # \
    # THE SEED HAS REACHED EVERY ONE OF B-19's TARGETS, and each pair is \
    # asserted equal rather than each half asserted at a constant. That is the \
    # difference that matters: a constant would still agree if BOTH files were \
    # re-cut wrong together, and the equalities below are what make a re-cut of \
    # one without the other fail here. \
    # \
    #   modimport.size    188 == target.mi.imports    188 module imports written \
    #   modimport.files   171 == target.mi.files      over the 171 files \
    #   jdkimport.size     43 == target.ji.hold       43 single-type imports left \
    #   jdkimport.type     25 == target.ji.hold.ambiguous \
    #   jdkimport.static   16 == target.ji.hold.static \
    #   jdkimport.nested    2 == target.ji.hold.nested \
    #   scopedvalue.decls   1 == target.sv.conv       the one field that converts \
    #   threadlocal.decls   7 == target.sv.hold       the seven that do not \
    # \
    # 452 SINGLE-TYPE IMPORTS LEFT THE TREE, which is target.ji.total 495 minus \
    # the 43, and the 43 that remain are the leg and not an exception list. 25 \
    # are ambiguous -- three packages java.base exports declare a public \
    # `Annotation`, which is why every one of those 25 rows reads `ambig=3` -- \
    # 16 are static imports and 2 are nested-type imports, and a module import \
    # supplies neither a type member nor a type's member. All three reasons are \
    # counted separately here and in target.ji.hold.*, because a candidate can \
    # hold any one of them without holding the others and a tree that had lost \
    # three of the 43 should be readable as which three. \
    # \
    # `ambig=` IS A COUNT AND NOT A FLAG: it is how many packages the module \
    # exports that declare the simple name, so 1 is the ordinary case and \
    # anything above 1 is what makes the import unsubsumable. THAT IS WHY \
    # `ambig=1` IS ASSERTED AT ZERO HERE and was asserted at 452 at the rung \
    # below. Every row that read 1 is exactly a row a module import subsumes, \
    # and all 452 of them departed; what is left is 25 rows reading 3 and 18 \
    # -- the statics and the nested -- reading 0, because the question does not \
    # apply to them. An `ambig=1` row surviving in this seed would be a \
    # departure the rung below failed to make, and it would be invisible to \
    # every count in this block except this one. \
    # \
    # THE ARRIVAL COUNTERS NO LONGER READ ZERO AND THE `.was` KEYS STILL DO. \
    # modimport.* records an `import module` declaration and scopedvalue.* \
    # records a ScopedValue: at the rung below both were what the candidate had \
    # to produce and both read empty, and target.mi.was=0 and target.sv.was=0 \
    # are the measurements that said so. Those two keys are carried forward \
    # unchanged and asserted at 0 HERE as well, because they are a statement \
    # about the tree step 12 was handed and not about the tree this rung is \
    # handed -- a `.was` that had drifted to match the arrival would mean the \
    # carry-forward had been re-cut against the wrong tree, and every other \
    # number in this block would still agree. \
    test "$(sed -n 's/^modimport\.size=//p'  ${bsrc} | head -1)" = 188; \
    test "$(sed -n 's/^modimport\.files=//p' ${bsrc} | head -1)" = 171; \
    test "$(sed -n 's/^modimport\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 188; \
    test "$(sed -n 's/^modimport\.size=//p'      ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.mi\.imports=//p'  ${btgt} | head -1)"; \
    test "$(sed -n 's/^modimport\.files=//p'     ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.mi\.files=//p'    ${btgt} | head -1)"; \
    test "$(sed -n 's/^target\.mi\.was=//p'     ${btgt} | head -1)" = 0; \
    test "$(sed -n 's/^target\.mi\.files=//p'   ${btgt} | head -1)" = 171; \
    test "$(sed -n 's/^target\.mi\.none=//p'    ${btgt} | head -1)" = 151; \
    test "$(sed -n 's/^target\.mi\.imports=//p' ${btgt} | head -1)" = 188; \
    test "$(sed -n 's/^target\.mi\.multi=//p'   ${btgt} | head -1)" = 15; \
    test "$(sed -n 's/^target\.mi\.nobase=//p'  ${btgt} | head -1)" = 4; \
    test "$(sed -n 's/^target\.mi\.mods=//p'    ${btgt} | head -1)" = 6; \
    test "$(sed -n 's/^jdkimport\.size=//p'      ${bsrc} | head -1)" = 43; \
    test "$(sed -n 's/^jdkimport\.files=//p'     ${bsrc} | head -1)" = 43; \
    test "$(sed -n 's/^jdkimport\.type=//p'      ${bsrc} | head -1)" = 25; \
    test "$(sed -n 's/^jdkimport\.static=//p'    ${bsrc} | head -1)" = 16; \
    test "$(sed -n 's/^jdkimport\.nested=//p'    ${bsrc} | head -1)" = 2; \
    test "$(sed -n 's/^jdkimport\.ondemand=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^jdkimport\.ambiguous=//p' ${bsrc} | head -1)" = 25; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 43; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=static')" = 16; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=nested')" = 2; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} | grep -c 'kind=type')" = 25; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} | grep -c 'ambig=1')" = 0; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} \
              | awk '{ for (i = 1; i <= NF; i++) \
                         if (substr($i, 1, 6) == "ambig=" && substr($i, 7) + 0 > 1) n++ } \
                     END { print n + 0 }')" = 25; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} \
              | awk '{ for (i = 1; i <= NF; i++) \
                         if (substr($i, 1, 6) == "ambig=" && substr($i, 7) + 0 == 0) n++ } \
                     END { print n + 0 }')" = 18; \
    test "$(sed -n 's/^target\.ji\.total=//p'          ${btgt} | head -1)" = 495; \
    test "$(sed -n 's/^target\.ji\.depart=//p'         ${btgt} | head -1)" = 452; \
    test "$(sed -n 's/^target\.ji\.hold=//p'           ${btgt} | head -1)" = 43; \
    test "$(sed -n 's/^target\.ji\.hold\.ambiguous=//p' ${btgt} | head -1)" = 25; \
    test "$(sed -n 's/^target\.ji\.hold\.static=//p'   ${btgt} | head -1)" = 16; \
    test "$(sed -n 's/^target\.ji\.hold\.nested=//p'   ${btgt} | head -1)" = 2; \
    test "$(sed -n 's/^target\.ji\.files=//p'          ${btgt} | head -1)" = 171; \
    test "$(sed -n 's/^target\.ji\.want=//p'           ${btgt} | head -1)" = 43; \
    # The arithmetic relations the two legs' keys have to satisfy, stated so \
    # that a re-cut that moved one figure and not its partners fails here \
    # rather than quietly re-weighting a leg. The first two are relations \
    # WITHIN the target file and held at the rung below unchanged; the next \
    # four are the seed-against-target equalities the paragraph above lists, \
    # and they are the ones that are new. \
    test "$(( $(sed -n 's/^target\.ji\.total=//p'  ${btgt} | head -1) \
            - $(sed -n 's/^target\.ji\.hold=//p'   ${btgt} | head -1) ))" \
         = "$(sed -n 's/^target\.ji\.depart=//p'   ${btgt} | head -1)"; \
    test "$(( $(sed -n 's/^target\.ji\.hold\.ambiguous=//p' ${btgt} | head -1) \
            + $(sed -n 's/^target\.ji\.hold\.static=//p'    ${btgt} | head -1) \
            + $(sed -n 's/^target\.ji\.hold\.nested=//p'    ${btgt} | head -1) ))" \
         = "$(sed -n 's/^target\.ji\.hold=//p' ${btgt} | head -1)"; \
    test "$(sed -n 's/^jdkimport\.size=//p'   ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.ji\.hold=//p'  ${btgt} | head -1)"; \
    test "$(sed -n 's/^jdkimport\.type=//p'   ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.ji\.hold\.ambiguous=//p' ${btgt} | head -1)"; \
    test "$(sed -n 's/^jdkimport\.static=//p' ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.ji\.hold\.static=//p'    ${btgt} | head -1)"; \
    test "$(sed -n 's/^jdkimport\.nested=//p' ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.ji\.hold\.nested=//p'    ${btgt} | head -1)"; \
    test "$(( $(sed -n 's/^jdkimport\.type=//p'     ${bsrc} | head -1) \
            + $(sed -n 's/^jdkimport\.static=//p'   ${bsrc} | head -1) \
            + $(sed -n 's/^jdkimport\.nested=//p'   ${bsrc} | head -1) \
            + $(sed -n 's/^jdkimport\.ondemand=//p' ${bsrc} | head -1) ))" \
         = "$(sed -n 's/^jdkimport\.size=//p' ${bsrc} | head -1)"; \
    test "$(sed -n 's/^target\.mi\.files=//p' ${btgt} | head -1)" \
       = "$(sed -n 's/^target\.ji\.files=//p' ${btgt} | head -1)"; \
    # B-19.4's nine rows are one FIELD declaration and eight CALLS, all of them \
    # in ManagerImpl.java, which is why scopedvalue.files is 1 and not 7: the \
    # leg converted the one field whose extent is `fire`, and the other seven \
    # ThreadLocals are still ThreadLocals in the six other files. \
    # \
    # THE FIELD COUNT IS CONSERVED AND THAT IS THE ASSERTION WORTH HAVING. \
    # target.sv.fields=8 is how many ThreadLocal fields the tree step 12 was \
    # handed carried; the seed carries 7 ThreadLocal declarations and 1 \
    # ScopedValue declaration, and 7 + 1 = 8. A field that had simply been \
    # DELETED rather than converted would leave threadlocal.decls at 7 with \
    # scopedvalue.decls at 0, satisfy the `hold` half on its own, and be caught \
    # by nothing else in this block. \
    test "$(sed -n 's/^scopedvalue\.size=//p'  ${bsrc} | head -1)" = 9; \
    test "$(sed -n 's/^scopedvalue\.files=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^scopedvalue\.decls=//p' ${bsrc} | head -1)" = 1; \
    test "$(sed -n 's/^scopedvalue\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 9; \
    test "$(sed -n 's/^scopedvalue\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=decl')" = 1; \
    test "$(sed -n 's/^scopedvalue\[[0-9]*\]=//p' ${bsrc} | grep -c 'form=call')" = 8; \
    test "$(sed -n 's/^threadlocal\.decls=//p' ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^threadlocal\.files=//p' ${bsrc} | head -1)" = 7; \
    test "$(sed -n 's/^threadlocal\.size=//p'  ${bsrc} | head -1)" = 57; \
    test "$(sed -n 's/^target\.sv\.fields=//p' ${btgt} | head -1)" = 8; \
    test "$(sed -n 's/^target\.sv\.conv=//p'   ${btgt} | head -1)" = 1; \
    test "$(sed -n 's/^target\.sv\.hold=//p'   ${btgt} | head -1)" = 7; \
    test "$(sed -n 's/^target\.sv\.was=//p'    ${btgt} | head -1)" = 0; \
    test "$(sed -n 's/^threadlocal\.decls=//p' ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.sv\.hold=//p'   ${btgt} | head -1)"; \
    test "$(sed -n 's/^scopedvalue\.decls=//p' ${bsrc} | head -1)" \
       = "$(sed -n 's/^target\.sv\.conv=//p'   ${btgt} | head -1)"; \
    test "$(( $(sed -n 's/^threadlocal\.decls=//p' ${bsrc} | head -1) \
            + $(sed -n 's/^scopedvalue\.decls=//p' ${bsrc} | head -1) ))" \
         = "$(sed -n 's/^target\.sv\.fields=//p' ${btgt} | head -1)"; \
    test "$(( $(sed -n 's/^target\.sv\.conv=//p' ${btgt} | head -1) \
            + $(sed -n 's/^target\.sv\.hold=//p' ${btgt} | head -1) ))" \
         = "$(sed -n 's/^target\.sv\.fields=//p' ${btgt} | head -1)"; \
    # C-5 has no source counter and it still has a target, so what this layer \
    # can check is that the target is a shape and not a sentence: twelve \
    # projects, a flag spelled the way a JVM reads it, and a measured zero for \
    # how many carry it now. The twelve are baseline/modnames.txt's twelve and \
    # that identity is asserted in the modnames section further down. \
    test "$(sed -n 's/^target\.co\.projects=//p' ${btgt} | head -1)" = 12; \
    test "$(sed -n 's/^target\.co\.flag=//p'     ${btgt} | head -1)" \
       = '-XX:+UseCompactObjectHeaders'; \
    test "$(sed -n 's/^target\.co\.was=//p'      ${btgt} | head -1)" = 0; \
    # THE FLOOR THIS ADDS UP TO, stated here because this is where its inputs \
    # are. Family B is the nineteen inherited legs, not one of them re-cut at \
    # this rung, and the tree the candidate is handed is the tree step 12's own \
    # verifier scored full, so all nineteen read 1.000000. Family C reads ZERO \
    # on all three weighted legs, each by its own mechanism, and both \
    # multipliers read 1.000000 because both are holds. Family A reads 938 of \
    # 939: this rung's era is rec/jdk25-step13, which records \
    # ServletTestRunner#writeSerializedResult, and a tree nobody has touched \
    # still declares writeObject. \
    # \
    # 0.30*0.998935 + 0.70*(0.25*1 + 0.75*0) = 0.474681, against \
    # migration_prompt item 6's 0.5, and 0.025319 of margin. That is the \
    # NARROWEST margin on this ladder -- step 11 had 0.036872 under a floor of \
    # 0.463128, step 12 had 0.025000 under 0.475000 -- and it is narrow for the \
    # good reason rather than the bad one: every shortfall step 11 was carrying \
    # has been made up, so there is nothing left below family C to hold the \
    # total down. The consequence is a cap on what the family-C legs a \
    # do-nothing tree reaches may weigh BETWEEN THEM: 0.474681 + 0.525w < 0.5 \
    # requires w < 0.048228, AND w IS THEIR SUM. This rung has no leg a tree can \
    # reach without compiling anything -- its smallest weighted leg is C-1 at \
    # 0.15, and 0.525 x 0.15 = 0.078750 is over the bar on its own -- which is \
    # what makes the cap slack here for the first time since step 05. The rung \
    # below had two such legs, C-1 off class files already emitted and C-5 off \
    # the argument list the build would hand a test fork; priced at 0.04 apiece \
    # they summed to 0.08 and put a two-flag tree over the bar, and at 0.02 \
    # apiece they fitted. NOTES 16.64 and 16.75 record the weighting that rule \
    # produced at the rung below, and step 12's `levelonly` calibration is \
    # where its figure was measured rather than argued. THIS rung's own split \
    # -- 0.15 / 0.57 / 0.28 with C-4 and C-5 as multipliers -- is item 1 of \
    # authoring/checklists/step13-requirements.md, and xcheck confirms the \
    # report's printed weights against the arithmetic the block performs. \
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
    # touches a resource clause either -- C-1 deletes @SuppressWarnings lines, \
    # C-2 fixes the javac warnings those lines were hiding, and C-3 narrows \
    # seven raw declarations to their inferred arguments, none of which opens \
    # or closes anything -- so \
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
    # that has a second source should have one -- AND THE SECOND SOURCE OF ONE \
    # OF THEM CHANGED SIDES AT THE RUNG BELOW. B-13.3's setacc half asked for \
    # all 14 setAccessible calls up to step 10; step 11's C-4 converted 4 of \
    # them to a VarHandle, so a correct tree holds 10, and step 11 re-cut \
    # target.b13.setacc to 10 for exactly that reason -- a leg still asking 14 \
    # would have been an inherited gate nothing correct could pass. THE SIDE \
    # THAT CHANGED IS WHICH KEY THE SCAN AGREES WITH. At step 11 the tree still \
    # held 14 and accessible.setaccessible was checked against setacc.was; this \
    # rung is handed the tree that did the conversion, so the scan reads 10 and \
    # it is target.b13.setacc the scan now equals. 14 survives as setacc.was, \
    # matched by nothing on this tree, which is what a `.was` key is for. \
    # target.b13.guards did NOT move with it: all twelve guarded methods are \
    # still here, three of them reaching their field through a VarHandle now, \
    # and a guard coming back is still a failure. The restatement is re-derived \
    # from varhandle.txt's verdict column further down; what is checked here is \
    # the arithmetic between the three keys. NOTES 16.16 and 16.61. \
    test "${g2l}" = 2; \
    test "${g2f}" = 13; \
    test "${g2f}" = "${nis}"; \
    test "${g3g}" = 12; \
    test "${g3s}" = 10; \
    test "${g3sw}" = 14; \
    test "${g3s}" = "${acst}"; \
    test "${g3sw}" != "${acst}"; \
    test "$(( g3sw - g3s ))" = 4; \
    test "$(( g3sw - g3s ))" = "$(sed -n 's/^varhandle\.methods=//p' ${bsrc} | head -1)"; \
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
    # ns.files IS 16 AND NOT 17, and the one file that left is B-19.2's doing. \
    # SourceScan counts a file here if it holds an ns IMPORT or an ns \
    # REFERENCE, and api/.../ArquillianResource.java held only the import: its \
    # single javax.naming contact is `{@link InitialContext}` in a javadoc \
    # comment, which is not a reference the scanner records. Step 12 replaced \
    # `import javax.naming.Context` with `import module java.naming`, the named \
    # import stopped being an ns import, and the file dropped out of the set \
    # with nothing else to keep it in. \
    # \
    # THE THREE AGGREGATES DID NOT DROP TO ZERO -- THEY ARE ABSENT. SourceScan \
    # prints an `ns.import.<pkg>` line only for a package it counted, so with \
    # every javax.management, javax.naming and javax.xml import subsumed there \
    # is no key at all rather than a key reading 0. That is asserted directly \
    # below, because a `= 0` test would pass just as happily on a missing key \
    # and would then be asserting nothing. \
    # \
    # AND B-14 CANNOT SEE ANY OF IT. Its hold half counts ns RECORDS bucketed \
    # pkg|method, the 134 records are byte-identical to step 12's, and \
    # ns.ref.javax.management, .naming and .xml still read 40, 24 and 4. B-19.2 \
    # moved where a type is IMPORTED FROM and not where it is USED, so the \
    # counter that changed is the one keyed on imports and no other. \
    test "${nsf}" = 16; \
    for p in javax.management javax.naming javax.xml; do \
        if grep -q "^ns\.import\.${p}=" ${bsrc}; then \
            echo "ns.import.${p} is still published: B-19.2 no longer covers those files" >&2; \
            exit 1; \
        fi; \
    done; \
    test "$(grep -c '^ns\.import\.' ${bsrc})" = 5; \
    test "$(grep -c '^ns\[' ${bsrc})" = 134; \
    test "$(sed -n 's/^ns\.ref\.javax\.management=//p' ${bsrc} | head -1)" = 40; \
    test "$(sed -n 's/^ns\.ref\.javax\.naming=//p'     ${bsrc} | head -1)" = 24; \
    test "$(sed -n 's/^ns\.ref\.javax\.xml=//p'        ${bsrc} | head -1)" = 4; \
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
    # compared against the tree rather than remembered, which is what makes \
    # `c5s` a live figure. \
    # \
    # THE LINE NUMBERS ARE STRIPPED BEFORE THE COMPARE AT THIS RUNG AND AT NO \
    # RUNG BEFORE IT. http.txt is step 07's frozen set and is byte-identical in \
    # steps 09 through 13; its three hold records read :179, :181 and :181, and \
    # the same three records in this tree read :169, :171 and :171. B-19.2 is \
    # why. CDIInjectionEnricherTestCase.java carried eleven named JDK imports \
    # on lines 3 through 13 and now carries one `import module java.base;` on \
    # line 3, which is ten lines shorter, and everything below the import block \
    # moved up by exactly ten. \
    # \
    # STRIPPING THE LINE IS THE CORRECT COMPARE AND NOT A WEAKENING, because \
    # the line is the one field of the record NOTHING SCORES: B-14.5 buckets on \
    # <file>#<method>, the same shape every other hold gate uses, and \
    # mk-baseline-step13.sh refuses to write a line number into any file it \
    # derives for exactly this reason. What is compared is still the whole of \
    # the rest -- file, form, type and enclosing method, three records against \
    # three -- so a record that vanished, changed method, or stopped being a \
    # legacytype still fails here. \
    # \
    # THE SHIFT IS CHECKED RATHER THAN TOLERATED. It has to be ONE number for \
    # all three records -- a file's lines move together -- and that number has \
    # to be the ten lines the import collapse accounts for. A tolerance that \
    # just ignored the column would also ignore two records that had moved by \
    # different amounts, which is not a shift but an edit. \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] == f' \
      | LC_ALL=C sort > /tmp/d-httpmove.txt; \
    sed -n 's/^http\[[0-9]*\]=//p' ${bsrc} | awk -v f="${c5f}" '{ split($1, p, ":") } p[1] != f' \
      | LC_ALL=C sort > /tmp/d-httphold.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "move" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httpmove.txt; \
    grep -v '^#' /tests/baseline/http.txt | grep . | awk '$1 == "hold" { $1 = ""; sub(/^ /, ""); print }' \
      | LC_ALL=C sort > /tmp/s-httphold.txt; \
    sed 's/\.java:[0-9]*/.java/' /tmp/d-httphold.txt | LC_ALL=C sort > /tmp/d-httphn.txt; \
    sed 's/\.java:[0-9]*/.java/' /tmp/s-httphold.txt | LC_ALL=C sort > /tmp/s-httphn.txt; \
    if ! cmp -s /tmp/d-httphn.txt /tmp/s-httphn.txt; then \
        echo "baseline/http.txt's hold records are not the other file's records in source.txt:" >&2; \
        diff /tmp/d-httphn.txt /tmp/s-httphn.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/d-httphn.txt)" = 3; \
    # The three records are INDISTINGUISHABLE once the line is stripped -- same \
    # file, same form, same type, same method, and two of them share a line as \
    # well -- so the shift is paired off positionally against the sorted line \
    # numbers and not by matching record to record. There is nothing else to \
    # match on, and the cmp above has already established that it is the same \
    # three records on both sides. \
    sed 's/.*\.java:\([0-9]*\) .*/\1/' /tmp/s-httphold.txt | LC_ALL=C sort -n > /tmp/httpsl.txt; \
    sed 's/.*\.java:\([0-9]*\) .*/\1/' /tmp/d-httphold.txt | LC_ALL=C sort -n > /tmp/httpdl.txt; \
    awk 'NR == FNR { a[FNR] = $1; next } { print a[FNR] - $1 }' \
        /tmp/httpsl.txt /tmp/httpdl.txt | LC_ALL=C sort -u > /tmp/httpshift.txt; \
    if [ "$(grep -c . /tmp/httpshift.txt)" != 1 ]; then \
        echo "baseline/http.txt's three hold records moved by different amounts:" >&2; \
        cat /tmp/httpshift.txt >&2; exit 1; \
    fi; \
    test "$(cat /tmp/httpshift.txt)" = 10; \
    c5h=$(sed 's/:[0-9]* .*//' /tmp/d-httphold.txt | LC_ALL=C sort -u); \
    test "$(printf '%s\n' "${c5h}" | grep -c .)" = 1; \
    test "$(grep -c "^modimport\[[0-9]*\]=${c5h}:" ${bsrc})" = 1; \
    test "$(grep -c "^jdkimport\[[0-9]*\]=${c5h}:" ${bsrc})" = 0; \
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
    # --- accessible.txt, step 06's, RE-CUT BY STEP 11 AND ANSWERED HERE ------ \
    # STEP 06 CUT THIS FILE AND EVERY RUNG THROUGH STEP 10 STAGED IT UNCHANGED. \
    # STEP 11 WAS THE FIRST THAT COULD NOT. Its C-4 converted four of the 14 \
    # `setAccessible` calls to a VarHandle, and THREE OF THE FOUR SAT INSIDE \
    # STEP 06'S OWN TWELVE GUARD BUCKETS -- so the subset relation step 06 \
    # built the file on ("every guard bucket is also a setacc bucket") stopped \
    # holding, and it stopped holding on the golden rather than on some \
    # failure. Staged forward at 12 and 14 the leg would have read, on a \
    # correctly migrated tree, 12 of 12 guards departed but only 10 of 14 \
    # setAccessible calls arrived -- the arrival half capped at 0.714286 by \
    # work step 11 was ASKED to do. So step 11 restated the arrival half over \
    # the 10 that survive and left the departure half asking all 12, every one \
    # of those methods being still in the tree with three of them now reaching \
    # their field through a handle. target.b13.guards is still 12; \
    # target.b13.setacc is 10 with its old value beside it as \
    # target.b13.setacc.was. \
    # \
    # THIS RUNG IS THE FIRST HANDED A TREE THE RESTATED FILE DESCRIBES EXACTLY, \
    # and that makes the check one line stronger here than it was below. Step \
    # 11 could compare the setacc half only against its OWN derivation -- \
    # varhandle.txt's fourteen rows minus the four marked `convert` -- because \
    # its tree still made all 14 calls and a comparison against the tree would \
    # have been a comparison against the departure state. This tree makes 10, \
    # so the half is compared against BOTH, and the derivation goes first: it \
    # is the one that would still catch a mis-cut file on a tree that had \
    # drifted the same way the file was mis-cut. \
    # \
    # THE FOUR THAT LEFT ARE NOT TYPED INTO THE FILE EITHER. accessible.txt's \
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
    # AND THE SAME HALF AGAINST THE TREE, which is the comparison step 11 had \
    # no tree to make. d-acc-all is every bucket this tree still calls \
    # `setAccessible` from; a rung that had staged step 10's accessible.txt by \
    # mistake would pass every count above -- 14 rows is 14 rows -- and fail \
    # here on four buckets the tree no longer has. \
    if ! cmp -s /tmp/d-acc-all.txt /tmp/s-setacc.txt; then \
        echo "baseline/accessible.txt's setacc half is not the setAccessible calls this tree still makes:" >&2; \
        diff /tmp/s-setacc.txt /tmp/d-acc-all.txt >&2 || true; exit 1; \
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
    # is a census of -- there are 10 `setAccessible` calls and 0 `isAccessible` \
    # calls in 8 files: step 06's work is done, step 11's C-4 is done, and the \
    # 14 and the 12 survive only as the figures target.txt keeps beside the \
    # ones it grades. A file that had put a guard back would show up here as a \
    # non-zero isaccessible. \
    test "${acst}" = "${g3s}"; \
    test "$(sed -n 's/^accessible\.isaccessible=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^accessible\.files=//p' ${bsrc} | head -1)" = 8; \
    test "${tacf}" = 12; \
    test "$(grep -c . /tmp/d-guard.txt)" = 0; \
    # AND THE FOUR FIGURES target.txt CARRIES FOR THIS HALF STILL PARTITION THE \
    # SAME 14 OVER THE SAME 12 FILES, which is the one place on this rung the \
    # departed sites are still counted at all. \
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
        if [ ! -f "/tests/assets/rec/jdk25-step13/${f}.txt" ]; then \
            echo "baseline/svcids.txt names an id with no jdk25-step13 transcript: ${id}" >&2; \
            exit 1; \
        fi; \
    done < /tmp/s-svcids.txt; \
    # ------------------------------------------------------------------ \
    # AND HERE THE CROSS-CHECK IS CUT IN TWO, for a reason that belongs to the \
    # kernel and not to this file. A RUN in shell form is handed to `/bin/sh -c` \
    # as ONE argument, and Linux caps a single argument at MAX_ARG_STRLEN -- 32 \
    # pages, 131072 bytes -- a ceiling no ulimit raises and no shell can work \
    # around, because the failure happens in execve before a shell exists. With \
    # this rung's own six baselines added to the thirty-one it inherits, the \
    # composed command reached 137499 bytes and the build died with `exec \
    # /bin/sh: argument list too long` BEFORE the first assertion ran. That is \
    # the worst shape a failure can take: every check below was correct and not \
    # one of them executed, and the trace named no assertion because no \
    # assertion started. \
    # \
    # THE CUT IS HERE BECAUSE IT IS WHERE THE TWO HALVES STOP TOUCHING. \
    # Everything above is this rung's own six baselines and the inherited set \
    # files of steps 03 through 08; everything below is steps 09, 10 and 11's, \
    # the six behaviour-id lists and the era this rung does not re-cut. No \
    # baseline file is read on both sides of the line, and the /tmp working \
    # files the half above writes are read only by it -- the sweep at the very \
    # end removes them across the layer boundary, which a later RUN can do \
    # because a RUN's filesystem is the image's and not a sandbox's. \
    # \
    # WHAT THE HALVES DO SHARE IS THE HEADER, and it is carried as VALUES and \
    # not re-derived. Sixty-eight bindings and three shell functions cross the \
    # line. A second copy of the sixty-eight sed expressions would be a second \
    # thing to keep right and would drift the first time a key was renamed; a \
    # file of what the half above has already asserted cannot drift from it, \
    # because it is not a copy of the derivation but a copy of the result. The \
    # worst a dropped name can do is leave a variable empty, and every \
    # assertion below compares it against a number, so empty fails loudly. \
    { \
      echo "bsrc=${bsrc}"; echo "btgt=${btgt}"; echo "tmt=${tmt}"; echo "ipb=${ipb}"; \
      echo "ips=${ips}"; echo "ipk=${ipk}"; echo "ipt=${ipt}"; echo "tbmb=${tbmb}"; \
      echo "tbms=${tbms}"; echo "tbhb=${tbhb}"; echo "tbhs=${tbhs}"; echo "tbid=${tbid}"; \
      echo "slr=${slr}"; echo "slp=${slp}"; echo "slt=${slt}"; echo "slo=${slo}"; \
      echo "swf=${swf}"; echo "swt=${swt}"; echo "pvs=${pvs}"; echo "pvc=${pvc}"; \
      echo "pvt=${pvt}"; echo "pvb=${pvb}"; echo "pvf=${pvf}"; echo "pvw=${pvw}"; \
      echo "pvwf=${pvwf}"; echo "pvh=${pvh}"; echo "pvhc=${pvhc}"; echo "pvha=${pvha}"; \
      echo "cpu=${cpu}"; echo "cpr=${cpr}"; echo "cpx=${cpx}"; echo "cpt=${cpt}"; \
      echo "cpb=${cpb}"; echo "cpf=${cpf}"; echo "cpw=${cpw}"; echo "cpws=${cpws}"; \
      echo "vhs=${vhs}"; echo "vhb=${vhb}"; echo "vhf=${vhf}"; echo "vhwas=${vhwas}"; \
      echo "vhwasb=${vhwasb}"; echo "tact=${tact}"; echo "tacv=${tacv}"; echo "tach=${tach}"; \
      echo "tacf=${tacf}"; echo "tmif=${tmif}"; echo "tmin=${tmin}"; echo "tmii=${tmii}"; \
      echo "tmim=${tmim}"; echo "tmib=${tmib}"; echo "tmimo=${tmimo}"; echo "tmiw=${tmiw}"; \
      echo "tjit=${tjit}"; echo "tjid=${tjid}"; echo "tjih=${tjih}"; echo "tjia=${tjia}"; \
      echo "tjis=${tjis}"; echo "tjin=${tjin}"; echo "tjif=${tjif}"; echo "tjiw=${tjiw}"; \
      echo "tsvf=${tsvf}"; echo "tsvc=${tsvc}"; echo "tsvh=${tsvh}"; echo "tsvk=${tsvk}"; \
      echo "tsvw=${tsvw}"; echo "tcop=${tcop}"; echo "tcow=${tcow}"; echo "tcof=${tcof}"; \
    } > /tmp/consist-binds.sh; \
    test "$(grep -c . /tmp/consist-binds.sh)" = 68; \
    if grep -vE '^[a-z][a-z0-9]+=[^ ]+$' /tmp/consist-binds.sh; then \
        echo "the bindings carried across the split are not sixty-eight space-free name=value lines" >&2; \
        exit 1; \
    fi; \
    echo "step 13: the first half of the baseline cross-check agrees -- this rung's five, and the inherited set files of steps 03 through 08"

# The baseline cross-check, second half: steps 09, 10 and 11's inherited set
# files, this rung's own six, the six behaviour-id lists and the era it does
# not re-cut.
#
# This is one block with the layer above it and is a separate RUN for one
# reason, which the comment at the end of that layer states in full: a single
# argument to execve cannot exceed 131072 bytes and the undivided block came
# to 137499. Read the two as one.
#
# THE HEADER IS RE-ESTABLISHED FROM THE FILE THE HALF ABOVE WROTE, AND THE
# RE-ESTABLISHMENT IS CHECKED RATHER THAN TRUSTED. The file has to be
# sixty-eight space-free name=value lines before it is sourced -- a shape that
# is also what makes sourcing it safe, since a value with a space or a quote in
# it would be two words to the shell -- `bsrc` and `btgt` have to come back as
# the two paths they went out as, and thirteen of the sixty-eight, one from
# each of the thirteen key families the header binds, are read out of
# target.txt a second time and compared. A file that had lost a line would
# leave a variable empty and fail on the first assertion that used it; these
# thirteen fail at the top instead, where the cause is legible.
#
# The three functions are re-defined and not carried, because a shell function
# is not a value and cannot be written to a file and read back the way one can.
# They are the three this half calls -- fqcn at two sites, setpart at one and
# fbucketise at two -- and they are copied from the layer above unchanged.
RUN set -eux; \
    test "$(grep -c . /tmp/consist-binds.sh)" = 68; \
    if grep -vE '^[a-z][a-z0-9]+=[^ ]+$' /tmp/consist-binds.sh; then \
        echo "the bindings carried across the split are not sixty-eight space-free name=value lines" >&2; \
        exit 1; \
    fi; \
    . /tmp/consist-binds.sh; \
    test "${bsrc}" = /tests/baseline/source.txt; \
    test "${btgt}" = /tests/baseline/target.txt; \
    test -s "${bsrc}"; \
    test -s "${btgt}"; \
    test "$(sed -n 's/^target\.b15\.mod\.total=//p'   ${btgt} | head -1)" = "${tmt}"; \
    test "$(sed -n 's/^target\.b16\.ip\.sites=//p'    ${btgt} | head -1)" = "${ips}"; \
    test "$(sed -n 's/^target\.b16\.tb\.ids=//p'      ${btgt} | head -1)" = "${tbid}"; \
    test "$(sed -n 's/^target\.b16\.sealed\.roots=//p' ${btgt} | head -1)" = "${slr}"; \
    test "$(sed -n 's/^target\.b16\.sw\.files=//p'    ${btgt} | head -1)" = "${swf}"; \
    test "$(sed -n 's/^target\.b18\.priv\.sites=//p'  ${btgt} | head -1)" = "${pvs}"; \
    test "$(sed -n 's/^target\.b18\.cp\.total=//p'    ${btgt} | head -1)" = "${cpt}"; \
    test "$(sed -n 's/^target\.b18\.vh\.sites=//p'    ${btgt} | head -1)" = "${vhs}"; \
    test "$(sed -n 's/^target\.acc\.total=//p'        ${btgt} | head -1)" = "${tact}"; \
    test "$(sed -n 's/^target\.mi\.files=//p'         ${btgt} | head -1)" = "${tmif}"; \
    test "$(sed -n 's/^target\.ji\.total=//p'         ${btgt} | head -1)" = "${tjit}"; \
    test "$(sed -n 's/^target\.sv\.fields=//p'        ${btgt} | head -1)" = "${tsvf}"; \
    test "$(sed -n 's/^target\.co\.flag=//p'          ${btgt} | head -1)" = "${tcof}"; \
    fqcn() { \
        sed -e 's#^.*/src/\(main\|test\)/java/##' -e 's#\.java$##' -e 's#/#.#g'; \
    }; \
    setpart() { grep -v '^#' "$1" | grep . | awk -v t="$2" '$1 == t { print $2 }' | LC_ALL=C sort -u; }; \
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
    # Eleven of the hundred and twenty-seven staged-and-unread target keys \
    # are the b16 tallies these six files are held against -- see the key-set \
    # block above -- and this is where all eleven are spent. \
    # \
    # --- ipattern.txt: B-16.2's population, STAGED AND NOW EXACT -------------- \
    # STEP 11 RE-CUT THIS FILE AND THIS RUNG STAGES IT BYTE FOR BYTE, WHICH IS \
    # THE FIRST TIME ON THIS LADDER THAT A RE-CUT FILE HAS COME BACK ROUND TO \
    # AGREEING WITH ITS OWN TREE EXACTLY. The history is worth one paragraph \
    # because the numbers below only make sense against it. Step 09 froze 18 \
    # conv buckets over 21 bound sites and 7 skip, 28 in all. Step 10's \
    # requirement 4 rewrote 13 `X.class.isInstance(value)` tests over 7 methods, \
    # and only 3 of them arrived here as bound `instanceof` patterns -- the \
    # other 10, over the other 4 methods, became `case` labels in the pattern \
    # switches its requirement 5 asked for, and a `case` label leaves no \
    # ipattern record at all. Step 11's C-2 then deleted the seven \
    # `SecurityActions#getConstructor` ladders, one bound pattern each, so step \
    # 11 had to re-cut: 18 - 7 + 3 = 14 conv buckets over 17 sites, 7 skip, 21 \
    # buckets and 24 sites in all. \
    # \
    # AND THAT IS NOW A CENSUS OF THIS TREE RATHER THAN A FORECAST OF IT. Step \
    # 11 compared only the buckets the file names, because the seven departing \
    # ladders were still standing in the seed it scanned and comparing the two \
    # lists whole would have failed on a correctly migrated tree. This rung is \
    # handed the tree that did the deletion, so the two lists ARE the same list, \
    # and the comparison below is made WHOLE. That is the stronger check and it \
    # is available only here: a rung that kept the filter after the departure \
    # had happened would have gone on excusing seven buckets that no longer \
    # need excusing, and would not notice if a new one appeared. \
    # \
    # THE VERDICT COLUMN IS RE-DERIVED, NOT TAKEN ON TRUST, and it is the same \
    # rule steps 10 and 11 restated: a bound pattern is one whose record says \
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
    # THE SCAN AGREES WITH THE FILE ON ALL THREE COUNTERS NOW, and at the rung \
    # below it agreed with none of them. `ipattern.size` is 24 where step 11 \
    # scanned 31, `ipattern.bound` is 17 where step 11 scanned 24, and \
    # `ipattern.methods` is 21 where step 11 scanned 28: each is exactly seven \
    # lower, one per deleted ladder, and each now equals the figure the file \
    # asks for. `ipattern.plain` never moved, because every departing site was \
    # bound. THE DIFFERENCE THAT WAS SEVEN IS NOW ZERO, and it is asserted as \
    # zero rather than deleted -- a check that stops being interesting is \
    # exactly the one a later rung is glad to still have. \
    test "$(sed -n 's/^ipattern\.size=//p'        ${bsrc} | head -1)" = "${ipt}"; \
    test "$(sed -n 's/^ipattern\.bound=//p'       ${bsrc} | head -1)" = "${ips}"; \
    test "$(sed -n 's/^ipattern\.plain=//p'       ${bsrc} | head -1)" = "${ipk}"; \
    test "$(sed -n 's/^ipattern\.convertible=//p' ${bsrc} | head -1)" = 0; \
    test "$(( $(sed -n 's/^ipattern\.bound=//p' ${bsrc} | head -1) - ${ips} ))" = 0; \
    test "$(( $(sed -n 's/^ipattern\.methods=//p' ${bsrc} | head -1) \
             - ${ipb} - ${ipk} ))" = 0; \
    # AND THE FIGURES THE LADDER USED TO ASK FOR ARE CARRIED BESIDE THEM. 21 \
    # conv buckets, 24 conv sites, 31 sites in all: that is the tree at the \
    # START of step 11, where the seven ladders still stood and step 10's three \
    # arrivals were already in place. It is what `was` means in these key names \
    # and it is NOT step 09's frozen 18 / 21 / 28, which no key carries. The \
    # pair is a closed loop: 21 - 7 = 14 conv buckets, 24 - 7 = 17 conv sites, \
    # 31 - 7 = 24 sites in all, and this tree's own scan lands on the second \
    # member of every one of those three subtractions. A staged target.txt that \
    # had drifted onto the wrong side would agree with itself and say nothing; \
    # against the scan it cannot. \
    test "$(sed -n 's/^target\.b16\.ip\.was\.buckets=//p' ${btgt} | head -1)" = 21; \
    test "$(sed -n 's/^target\.b16\.ip\.was\.sites=//p' ${btgt} | head -1)" = 24; \
    test "$(sed -n 's/^target\.b16\.ip\.was\.total=//p' ${btgt} | head -1)" = 31; \
    test "$(( $(sed -n 's/^target\.b16\.ip\.was\.buckets=//p' ${btgt} | head -1) - 7 ))" = "${ipb}"; \
    test "$(( $(sed -n 's/^target\.b16\.ip\.was\.sites=//p'   ${btgt} | head -1) - 7 ))" = "${ips}"; \
    test "$(( $(sed -n 's/^target\.b16\.ip\.was\.total=//p'   ${btgt} | head -1) - 7 ))" = "${ipt}"; \
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
    # THE WHOLE LISTS ARE COMPARED, not the intersection -- and this is the \
    # first rung at which they are not identical. Step 11 filtered the scan \
    # down to the buckets the file names and asserted that the seven left over \
    # were the departing ladders; here exactly ONE bucket is spelt differently \
    # on the two sides, and it is named rather than tolerated. \
    # \
    # THE FROZEN FILE PUTS B-16.2's `skip` SITE IN `ManagerImpl#fire` AND THIS \
    # TREE'S SCAN FINDS IT IN `ManagerImpl#fireInScope`. The seed this rung was \
    # authored from is step 12's BLIND workspace, and that agent did not merely \
    # convert the ThreadLocal: it bound the ScopedValue in `fire` and moved the \
    # body to a new private method, carrying the bucket with it. ipattern.txt \
    # is NOT re-pointed, because the REFERENCE step 12 binds in place and the \
    # tree a scored run is handed still has the body in `fire`; re-pointing \
    # would charge the reference solution for a refactor only a blind agent \
    # made. NOTES 16.104. So the disagreement is real, one bucket wide, and \
    # permanent. \
    # \
    # SO THE SCAN SIDE IS MAPPED BACK BY THAT ONE NAME AND THEN COMPARED WHOLE, \
    # with both directions of the mapping asserted first: the scan carries the \
    # new spelling once and the old none, the file carries the old once and the \
    # new none. A mapping that matched nothing would turn the cmp below into a \
    # weaker test than step 11's and nothing else here would say so. \
    ipold='impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java#fire'; \
    ipnew='impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java#fireInScope'; \
    test "$(awk -v k="${ipnew}" '$2 == k' /tmp/d-ip-all.txt | grep -c .)" = 1; \
    test "$(awk -v k="${ipold}" '$2 == k' /tmp/d-ip-all.txt | grep -c .)" = 0; \
    test "$(awk -v k="${ipold}" '$2 == k' /tmp/s-ip.txt     | grep -c .)" = 1; \
    test "$(awk -v k="${ipnew}" '$2 == k' /tmp/s-ip.txt     | grep -c .)" = 0; \
    awk -v a="${ipnew}" -v b="${ipold}" '{ if ($2 == a) $2 = b; print }' \
        /tmp/d-ip-all.txt | LC_ALL=C sort > /tmp/d-ipref.txt; \
    test "$(grep -c . /tmp/d-ipref.txt)" = "$(grep -c . /tmp/d-ip-all.txt)"; \
    # ...and the filter is kept only to prove there is nothing left over: it \
    # must select every row of the mapped list. \
    LC_ALL=C sort /tmp/s-ip.txt > /tmp/s-ip-sorted.txt; \
    awk 'FILENAME == ARGV[1] { w[$2] = 1; next } { if ($2 in w) print }' \
        /tmp/s-ip.txt /tmp/d-ipref.txt > /tmp/d-ip.txt; \
    test "$(( $(grep -c . /tmp/d-ipref.txt) - $(grep -c . /tmp/d-ip.txt) ))" = 0; \
    if ! cmp -s /tmp/d-ipref.txt /tmp/s-ip-sorted.txt; then \
        echo "baseline/ipattern.txt is not source.txt's ipattern records re-bucketed:" >&2; \
        diff /tmp/s-ip-sorted.txt /tmp/d-ipref.txt >&2 || true; exit 1; \
    fi; \
    # NO BUCKET IS ON BOTH LISTS, which the comparison above already implies and \
    # this states, because it is the property B-16.2's two halves depend on: a \
    # method that holds a convertible site AND one with no cast after it would \
    # be asked to convert and to leave alone in the same breath. The scan says \
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
        if [ ! -f "/tests/assets/rec/jdk25-step13/${f}.txt" ]; then \
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
    # tree it was handed; step 11 staged all four unchanged and so does this \
    # rung. THE TEST EVERY RUNG ABOVE THE SECOND APPLIES IS THE SAME ONE: does \
    # the inheriting rung's own work remove or move a site the file names? Step \
    # 06's accessible.txt failed it at step 11, whose C-4 converted four \
    # `setAccessible` calls away, and so did step 09's ipattern.txt, seven of \
    # whose buckets left with the SecurityActions ladders step 11's C-2 \
    # deleted. THIS RUNG RE-CUTS NOTHING, and that is a measured answer rather \
    # than a claim about intent: C-2 and C-3 rewrite import blocks, C-4 \
    # replaces a carrier declaration and its calls, C-5 adds a JVM argument to \
    # twelve poms and C-1 moves a class-file major -- and the four files here \
    # name reflective type tests, instanceof chains, first-element reads and \
    # switch ids, none of which is any of those. The four are read in the \
    # ARRIVED state below against THIS rung's own scan, which is where the \
    # answer is actually checked. \
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
    # are NOT: step 11's mk-baseline script re-measured all three on the seed \
    # it was handed, so they read 22, 19 and 7 where step 10 published 35, 26 \
    # and 0, and this rung carries that forward rather than measuring it twice. \
    # They are the `.was` half of a gate pair, and a `.was` carrying step 10's \
    # starting figures would say that 13 reference comparisons and 7 \
    # first-element reads departed after step 10. They departed during it, and \
    # the two rungs since have only held the result. NOTES 16.20. \
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
    # rec/jdk25-step13. A dispatch behaviour that stopped being recorded here \
    # would leave B-17.3's execution scaling dividing by five recordings that \
    # are not there. \
    grep -v '^#' /tests/baseline/swids.txt | grep -v '^$' \
      | LC_ALL=C sort -u > /tmp/s-swids.txt; \
    test "$(grep -c . /tmp/s-swids.txt)" = "$(k17 ch.ids)"; \
    test "$(k17 ch.ids)" = 5; \
    while read -r id; do \
        grep -qxF "${id}" /tests/assets/ids.txt \
          || { echo "swids.txt id not in assets/ids.txt: ${id}" >&2; exit 1; }; \
        test -s "/tests/assets/rec/jdk25-step13/$(echo "${id}" | tr / .).txt" \
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
    # STEP 11'S FOUR BASELINES, INHERITED AND READ IN THE ARRIVED STATE. \
    # ======================================================================== \
    # Three populations and one id list, cut one rung down and staged forward \
    # byte for byte, and all four still get the treatment the inherited set \
    # files get: every one derived from a second source, and every one tied to \
    # the target keys that count it, so that no number in this image is only a \
    # number in one file. Twenty-two of the hundred and twenty-seven \
    # staged-and-unread target keys are these files' tallies -- nine priv, \
    # eight catchparam and five varhandle -- and this is where all twenty-two \
    # are spent. The id \
    # list's own key is not among them: step 11 published target.ids beside \
    # vhids.txt because the ids it moved were ids its own populations name, and \
    # this rung publishes none because the one id it moves is named by no \
    # population at all -- the era section below is where that is checked. \
    # \
    # THE SECOND SOURCE IS source.txt's OWN RECORD ARRAY IN ALL THREE CASES, \
    # so each derivation below is a `cmp` and not a count. Step 10 could \
    # compare its populations line for line because the scan that cuts them \
    # emits the same rows, and that holds here too, with one twist worth \
    # naming: all three files are the MAIN-SOURCE records, and catchparam is \
    # the one place where that makes a visible difference -- this rung's scan \
    # finds 89 catch clauses and 79 of them are in main sources. \
    # \
    # AND THE VERDICT COLUMN IS RE-DERIVED AND NOT TAKEN ON TRUST. \
    # varhandle.txt's three-way split is a function of two fields the scan \
    # already carries -- the file and the receiver type -- so the rule is \
    # restated below against those fields: step 11's cut and this image's \
    # own scan have to agree about all 14 rows or the build stops. \
    # \
    # --- priv.txt: B-18.1's departure population, ARRIVED --------------------- \
    # 67 rows in 27 methods across 7 files: 20 CALLS to \
    # AccessController.doPrivileged and 47 TYPE REFERENCES to the four names \
    # that go with them. ALL SEVEN FILES ARE NAMED SecurityActions.java and \
    # that is the shape of the whole leg, so it is MEASURED here rather than \
    # read off the paths -- a file joining this population from anywhere else \
    # would mean the deletion step 11's C-2 scored is not a deletion of seven \
    # classes. \
    # \
    # WHAT CHANGED AT THIS RUNG IS THE TENSE AND NOT THE FILE. Step 11 staged \
    # these 67 rows as the population its candidate had to remove; the blind \
    # run removed them; this rung's seed IS that tree. So B-18.1 reads the same \
    # 67 rows as a record of something that has already gone, and the file is \
    # byte for byte step 11's -- one of the staged extras the era hands \
    # forward, re-used and not re-cut. Everything from here down to the payload \
    # is an assertion about the FILE and holds unchanged. The one assertion \
    # that was about the TREE is the one that had to be turned around, and it \
    # is the block marked ...AND THE TREE below. \
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
    # ...AND THE TREE NO LONGER HAS ONE ROW OF IT. This is the assertion that \
    # reverses at this rung. Step 11 asserted that this file WAS source.txt's \
    # main-source priv records, verbatim and in order, because the population \
    # was still standing when its scan ran. On this seed the scan finds \
    # nothing: priv.size, priv.methods and priv.files all read 0, there is no \
    # main-source half left to compare against, and a cmp against an empty list \
    # would be an assertion about the file rather than about the migration. \
    # \
    # SO THE THREE SOURCE KEYS ARE ASSERTED AT ZERO, AND THE RECORD COUNT WITH \
    # THEM, because a counter can be written by hand and a record cannot. A \
    # seed that had kept even one doPrivileged ladder reads 1 here and fails, \
    # which is the direction that matters: `want` is 0 just below, and a want \
    # of 0 measured on a tree the scan still finds the population on would be a \
    # leg certifying work that was never done. \
    test "$(sed -n 's/^priv\.size=//p'    ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\.methods=//p' ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\.files=//p'   ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^priv\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = 0; \
    # AND THE SEVEN CLASSES ARE STILL THERE, which is the other half of the \
    # same fact and the reason the payload below can be counted at all. C-2 \
    # deleted seven LADDERS and not seven FILES: every one of the seven paths \
    # this file names is still a path source.txt writes records against, and a \
    # tree that had answered step 11 with `rm` would fail here instead. \
    sed -n 's/^[a-z][a-z]*\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); print f[1] }' \
      | LC_ALL=C sort -u > /tmp/d-pvseen.txt; \
    test "$(LC_ALL=C comm -23 /tmp/s-pvfile.txt /tmp/d-pvseen.txt | grep -c .)" = 0; \
    # WHAT THE FILE HAS TO GET RIGHT IS THAT `want` IS ZERO. C-2 is a pure \
    # departure: nothing arrives in its place that a scan can see, because the \
    # privileged block's body becomes the method that held it. A `want` that \
    # had drifted above zero would make a correct migration unscorable, so both \
    # want keys are asserted against 0 and not merely against each other. \
    test "${pvw}"  = 0; \
    test "${pvwf}" = 0; \
    # THE PAYLOAD, DERIVED FROM THE SEVEN FILES THIS FILE'S OWN ROWS NAME. \
    # A departure scored alone pays deletion, so B-18.1 keeps the min() step \
    # 11's C-2 was scored under: 13 sites that have to SURVIVE the deletion. \
    # Neither half of the 13 is a typed list -- the construction half is every \
    # `newinstance form=ctor` record \
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
    # ONE PLACE B-18's THREE POPULATION FILES ALL HAVE TO AGREE. A scanner row \
    # is a setAccessible step 11's C-4 did not convert; a priv file is a file \
    # its C-2 took the ladders out of; and the six that are both are the six \
    # sites that came through that rung untouched and must still be here now. \
    # The two derivations share no input beyond the tree, and the diff below is \
    # the check. \
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
    # --- catchparam.txt: B-18.2's census, and the tree that answered it ------ \
    # 93 rows in 68 methods across 43 files: 58 catch parameters whose body \
    # READS the exception and 35 that never mention it again. THAT IS THE STATE \
    # OF THE TREE STEP 11 WAS HANDED and not the state of this one. The file is \
    # inherited byte for byte and is the only record left of the `before`, \
    # which is why `target.b18.cp.was` reads 93 while every arrival figure \
    # beside it reads something smaller. \
    # \
    # `unnamed` IS ZERO IN THIS FILE and that zero was the arrival's floor: `_` \
    # is a keyword only from release 22 and the tree step 11 scanned was at 21, \
    # so a row could not have been written unnamed even by accident. The same \
    # counter on THIS tree reads 28, and the two twenty-eights are matched row \
    # for row at the end of this block. \
    # \
    # THE FILE'S OWN TALLIES ARE LITERALS HERE AND NOT KEYS, because target.txt \
    # no longer carries them. `target.b18.cp.*` publishes the ARRIVED figures, \
    # which are what B-18.2 is scored against; the only two it keeps from the \
    # old state are `was` and `was.scanned`. 58, 35, 0 and 68 are written out \
    # because the file is frozen and they cannot drift. \
    grep -v '^#' /tests/baseline/catchparam.txt | grep . > /tmp/s-cp.txt; \
    test "$(grep -c . /tmp/s-cp.txt)" = "${cpw}"; \
    test "${cpw}"  = 93; \
    test "${cpws}" = "${cpw}"; \
    test "$(awk '$3 == "kind=read"'    /tmp/s-cp.txt | grep -c .)" = 58; \
    test "$(awk '$3 == "kind=unread"'  /tmp/s-cp.txt | grep -c .)" = 35; \
    test "$(awk '$3 == "kind=unnamed"' /tmp/s-cp.txt | grep -c .)" = 0; \
    test "$(awk '$3 == "kind=read" || $3 == "kind=unread" || $3 == "kind=unnamed"' \
              /tmp/s-cp.txt | grep -c .)" = "${cpw}"; \
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
    test "$(grep -c . /tmp/s-cpb.txt)" = 68; \
    test "$(grep -c '#-$' /tmp/s-cpb.txt)" = 0; \
    test "$(awk '{ split($1, a, ":"); print a[1] }' /tmp/s-cp.txt \
              | LC_ALL=C sort -u | grep -c .)" = 43; \
    # THE TEN ROWS THIS FILE DOES NOT CARRY ARE THE TEST-SOURCE ONES, and the \
    # ten is computed rather than typed -- twice over, once on each tree. Step \
    # 11 scanned 103 in all scopes and froze the 93 rule 6 leaves in scope; \
    # this rung's scan finds 89 and 79 of them are in scope. The all-scope \
    # figure fell by 14 and the test half did not move at all, which is the \
    # right answer and not an accident: C-2 deleted seven getConstructor \
    # methods and every one of the fourteen clauses in them was under src/main. \
    cpall=$(sed -n 's/^catchparam\.size=//p' ${bsrc} | head -1); \
    test "${cpall}" = 89; \
    test "${cpt}" -lt "${cpall}"; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${cpall}"; \
    test "$(sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
              | grep -vc '/src/main/java/')" = "$(( cpall - cpt ))"; \
    test "$(( cpall - cpt ))" = 10; \
    test "$(( cpw - cpt ))"   = 14; \
    test "$(sed -n 's/^catchparam\.unnamed=//p' ${bsrc} | head -1)" = "${cpu}"; \
    # THE SUBTRACTION B-18.2's ARRIVAL AND HOLD ARE STATED THROUGH, done here \
    # the way test.sh does it: pbuckets() takes priv.txt's 27 buckets away from \
    # catchparam.txt before counting. THE TWO POPULATIONS OVERLAP BY \
    # CONSTRUCTION. Each of the seven SecurityActions classes caught a \
    # PrivilegedActionException it read and a ClassCastException it did not, \
    # both inside getConstructor, and step 11's C-2 DELETED that method. A \
    # candidate who did that rung's work correctly therefore COULD NOT rename \
    # those seven unread parameters to `_`, because they were gone; an arrival \
    # denominator of 35 rather than 28 would have capped a complete migration \
    # at 0.8. The subtraction is inherited along with the leg, which is why it \
    # is still done here rather than replaced by the count this tree gives. \
    # \
    # SO THE FOURTEEN ROWS THAT LEFT ARE NAMED AND COUNTED, not assumed: seven \
    # `pae kind=read` and seven `cce kind=unread`, all fourteen \
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
    test "$(grep -c . /tmp/d-cpout.txt)"  = "${cpt}"; \
    test "$(grep -c . /tmp/d-cpgone.txt)" = "$(( cpw - cpt ))"; \
    test "$(awk '$3 == "kind=unread"' /tmp/d-cpout.txt | grep -c .)" = "${cpu}"; \
    test "$(awk '$3 == "kind=read"'   /tmp/d-cpout.txt | grep -c .)" = "${cpr}"; \
    test "${cpu}" = 28; \
    test "${cpr}" = 51; \
    test "${cpx}" = 0; \
    test "${cpt}" = 79; \
    test "$(( cpu + cpr ))" = "${cpt}"; \
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
    test "$(grep -c . /tmp/d-cp.txt)" = "${cpb}"; \
    test "${cpb}" = 61; \
    test "$(awk '{ split($1, a, ":"); m = ""; \
                   for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
                   print a[1] "#" m }' /tmp/d-cpgone.txt \
              | LC_ALL=C sort -u | grep -c .)" = "$(( 68 - cpb ))"; \
    test "$(awk '{ split($1, a, ":"); print a[1] }' /tmp/d-cpout.txt \
              | LC_ALL=C sort -u | grep -c .)" = "${cpf}"; \
    test "${cpf}" = 43; \
    # AND HERE IS THE TREE, which is the half step 11 could not write. The 79 \
    # rows the subtraction leaves are that rung's PREDICTION of what its \
    # candidate would be scored over; the 79 rows this rung's scan finds under \
    # src/main are what the candidate actually left behind. The two cannot be \
    # compared line for line -- deleting seven methods moved every line number \
    # below them -- so they are compared the two ways a line number does not \
    # enter: by BUCKET, which is <file>#<method>, and by the triple \
    # <file>#<method>#<type> taken one kind at a time. \
    # \
    # THE KIND IS WHERE THE MIGRATION SHOWS. Every one of the 28 rows that was \
    # `unread` in the frozen census is `unnamed` on this tree and written `_`; \
    # every one of the 51 that was `read` is `read` still. A tree that had \
    # renamed a read parameter breaks the second comparison, one that had \
    # missed an unread parameter breaks the first, and one that had reshaped a \
    # method to dodge either breaks the bucket cmp underneath both. \
    sed -n 's/^catchparam\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, f, ":"); if (f[1] !~ /\/src\/main\/java\//) next; print }' \
      > /tmp/d-cparr.txt; \
    test "$(grep -c . /tmp/d-cparr.txt)" = "${cpt}"; \
    test "$(awk '$3 == "kind=unnamed"' /tmp/d-cparr.txt | grep -c .)" = "${cpu}"; \
    test "$(awk '$3 == "kind=read"'    /tmp/d-cparr.txt | grep -c .)" = "${cpr}"; \
    test "$(awk '$3 == "kind=unread"'  /tmp/d-cparr.txt | grep -c .)" = "${cpx}"; \
    test "$(awk '$3 == "kind=unnamed" && $2 != "name=_"' /tmp/d-cparr.txt \
              | grep -c .)" = 0; \
    # AND ONE ROW OF THIS TREE IS SPELT WITH A METHOD NAME THE FROZEN FILE \
    # DOES NOT CARRY, which is mapped back here rather than tolerated -- the \
    # same one bucket, for the same reason, as the ipattern block above. The \
    # seed this rung was cut from is step 12's BLIND workspace, whose agent \
    # bound the ScopedValue in ManagerImpl#fire and moved the body to a new \
    # private fireInScope; catchparam.txt is frozen on `fire` because the \
    # REFERENCE step 12 binds in place and the tree a scored run is handed \
    # still has the body there (NOTES 16.104). Every comparison below keys on \
    # <file>#<method>, so without the mapping all three would fail on that \
    # single clause and none of them would say which. \
    # \
    # BOTH DIRECTIONS ARE ASSERTED BEFORE THE MAPPING RUNS, because a mapping \
    # that matched nothing would quietly weaken all three cmps and a mapping \
    # that matched twice would hide a second moved method. \
    test "$(awk '$5 == "in=fireInScope"' /tmp/d-cparr.txt | grep -c .)" = 1; \
    test "$(awk '$5 == "in=fire"'        /tmp/d-cparr.txt | grep -c .)" = 0; \
    test "$(awk '$5 == "in=fire"'        /tmp/d-cpout.txt | grep -c .)" = 1; \
    test "$(awk '$5 == "in=fireInScope"' /tmp/d-cpout.txt | grep -c .)" = 0; \
    test "$(awk '$5 == "in=fireInScope" { split($1, a, ":"); print a[1] }' \
              /tmp/d-cparr.txt)" = impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl.java; \
    sed 's#^\(impl-base/src/main/java/org/jboss/arquillian/impl/core/ManagerImpl\.java:[0-9][0-9]*\) \(name=[^ ]* kind=[^ ]* type=[^ ]*\) in=fireInScope$#\1 \2 in=fire#' \
        /tmp/d-cparr.txt > /tmp/d-cpref.txt; \
    test "$(grep -c . /tmp/d-cpref.txt)" = "$(grep -c . /tmp/d-cparr.txt)"; \
    test "$(awk '$5 == "in=fireInScope"' /tmp/d-cpref.txt | grep -c .)" = 0; \
    test "$(awk '$5 == "in=fire"'        /tmp/d-cpref.txt | grep -c .)" = 1; \
    # cpkey <file> <kind> -- the rows of one kind reduced to the coordinates a \
    # deletion above them cannot move. Defined here rather than beside the \
    # other helpers because this rung is the only one with two trees to line up. \
    cpkey() { \
        awk -v k="$2" '$3 == k { split($1, a, ":"); m = ""; t = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 3) == "in=")   m = substr($i, 4); \
                  if (substr($i, 1, 5) == "type=") t = substr($i, 6) } \
               print a[1] "#" m "#" t }' "$1" | LC_ALL=C sort; \
    }; \
    cpkey /tmp/d-cpout.txt kind=unread  > /tmp/d-cpk1.txt; \
    cpkey /tmp/d-cpref.txt kind=unnamed > /tmp/d-cpk2.txt; \
    if ! cmp -s /tmp/d-cpk1.txt /tmp/d-cpk2.txt; then \
        echo "the 28 unread parameters step 11 froze are not the 28 this tree writes as _:" >&2; \
        diff /tmp/d-cpk1.txt /tmp/d-cpk2.txt >&2 || true; exit 1; \
    fi; \
    cpkey /tmp/d-cpout.txt kind=read > /tmp/d-cpk1.txt; \
    cpkey /tmp/d-cpref.txt kind=read > /tmp/d-cpk2.txt; \
    if ! cmp -s /tmp/d-cpk1.txt /tmp/d-cpk2.txt; then \
        echo "the 51 read parameters step 11 froze are not the 51 this tree still reads:" >&2; \
        diff /tmp/d-cpk1.txt /tmp/d-cpk2.txt >&2 || true; exit 1; \
    fi; \
    awk '{ split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/d-cpref.txt | LC_ALL=C sort -u > /tmp/d-cpk2.txt; \
    if ! cmp -s /tmp/d-cp.txt /tmp/d-cpk2.txt; then \
        echo "the 61 buckets the subtraction leaves are not this tree's 61 main-source buckets:" >&2; \
        diff /tmp/d-cp.txt /tmp/d-cpk2.txt >&2 || true; exit 1; \
    fi; \
    # --- varhandle.txt: B-18.3's arrival, its hold, and the tree that has both \
    # Fourteen rows, one per `setAccessible` site as step 11 found them, and \
    # THE POPULATION THE LEG SCORES WAS EMPTY ON THE WAY IN: 0 privateLookupIn \
    # calls, 0 unreflectVarHandle calls, 0 methods, 0 files. That measured zero \
    # is what made C-4 an arrival rather than a threshold, and it survives into \
    # this rung as `target.b18.vh.was` and `.was.buckets` -- the only two \
    # figures a leg that has already arrived still needs from the `before`. \
    # \
    # ON THIS TREE THE SAME SCAN READS EIGHT. Four sites converted, two calls \
    # apiece, in four methods across four files; the other ten are still \
    # setAccessible and accessible.txt's counters say so. Both halves are read \
    # out of source.txt and not out of this file, which carries only the \
    # fourteen sites the split was carved out of. \
    test "${vhwas}"  = 0; \
    test "${vhwasb}" = 0; \
    test "${vhs}" = 8; \
    test "${vhb}" = 4; \
    test "${vhf}" = 4; \
    test "$(sed -n 's/^varhandle\.size=//p'    ${bsrc} | head -1)" = "${vhs}"; \
    test "$(sed -n 's/^varhandle\.arrival=//p' ${bsrc} | head -1)" = "${vhs}"; \
    test "$(sed -n 's/^varhandle\.methods=//p' ${bsrc} | head -1)" = "${vhb}"; \
    test "$(sed -n 's/^varhandle\.files=//p'   ${bsrc} | head -1)" = "${vhf}"; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} | grep -c .)" = "${vhs}"; \
    # TWO CALLS PER CONVERTED SITE AND THE SCAN HAS TO SEE BOTH. \
    # privateLookupIn returns a Lookup and unreflectVarHandle turns the Field \
    # into a VarHandle; neither alone is a conversion, and only the Lookup that \
    # privateLookupIn returns can reach a private field, so an \
    # unreflectVarHandle without one cannot be written at all. Eight records, \
    # four of each call, four methods -- so `sites` is twice `buckets` here and \
    # is asserted as such rather than as the literal 8. \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} \
              | awk '$3 == "call=privateLookupIn"' | grep -c .)" = "${vhb}"; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} \
              | awk '$3 == "call=unreflectVarHandle"' | grep -c .)" = "${vhb}"; \
    test "$(( vhb * 2 ))" = "${vhs}"; \
    test "$(sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} \
              | awk '$2 != "form=arrival"' | grep -c .)" = 0; \
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
    # FOURTEEN ROWS, FOURTEEN BUCKETS: no method held two of these sites, so \
    # the per-bucket min() test.sh takes over the hold half was a per-site test \
    # on the tree step 11 was given and the leg could not be half-passed inside \
    # one method. \
    awk '$5 == "verdict=convert" { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-vh.txt | LC_ALL=C sort -u > /tmp/d-vhconv.txt; \
    awk '$5 != "verdict=convert" { split($1, a, ":"); m = ""; \
           for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
           print a[1] "#" m }' /tmp/s-vh.txt | LC_ALL=C sort -u > /tmp/d-vhhold.txt; \
    test "$(grep -c . /tmp/d-vhconv.txt)" = "${vhb}"; \
    test "$(grep -c . /tmp/d-vhhold.txt)" = "${tach}"; \
    test "${vhb}" = "${tacv}"; \
    test "$(LC_ALL=C comm -12 /tmp/d-vhconv.txt /tmp/d-vhhold.txt | grep -c .)" = 0; \
    test "$(( vhb + tach ))" = "${tact}"; \
    test "$(sed 's/#.*//' /tmp/d-vhconv.txt | LC_ALL=C sort -u | grep -c .)" = "${vhf}"; \
    test "${vhf}" = "${tacv}"; \
    # AND THE FOUR ARE THE FOUR. The buckets varhandle.txt marks `convert` are \
    # exactly the buckets this tree's varhandle records sit in; the ten it does \
    # not mark are exactly the buckets this tree's setAccessible calls still sit \
    # in. Step 11 named which four sites had to move and which ten had to stay, \
    # and these two comparisons are the tree agreeing site by site -- neither \
    # can be satisfied by converting the right NUMBER of the wrong sites. \
    sed -n 's/^varhandle\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ split($1, a, ":"); m = ""; \
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4); \
               print a[1] "#" m }' | LC_ALL=C sort -u > /tmp/d-vharr.txt; \
    if ! cmp -s /tmp/d-vhconv.txt /tmp/d-vharr.txt; then \
        echo "the four sites varhandle.txt marks convert are not the four this tree routes through a VarHandle:" >&2; \
        diff /tmp/d-vhconv.txt /tmp/d-vharr.txt >&2 || true; exit 1; \
    fi; \
    fbucketise accessible call=setAccessible > /tmp/d-vhstill.txt; \
    if ! cmp -s /tmp/d-vhhold.txt /tmp/d-vhstill.txt; then \
        echo "the ten sites varhandle.txt holds are not the ten setAccessible calls this tree still makes:" >&2; \
        diff /tmp/d-vhhold.txt /tmp/d-vhstill.txt >&2 || true; exit 1; \
    fi; \
    test "$(sed -n 's/^accessible\.setaccessible=//p' ${bsrc} | head -1)" = "${tach}"; \
    # AND FOUR FILES LEFT THE accessible POPULATION ALTOGETHER, which is the \
    # cheapest statement of the same fact: none of the four converted sites \
    # shared a file with a site that stayed, so 12 files minus 4 is the 8 this \
    # tree's scan reports. A conversion that had picked a Field site out of a \
    # file with two would read 9 here. \
    test "$(sed -n 's/^accessible\.files=//p' ${bsrc} | head -1)" \
       = "$(sed 's/#.*//' /tmp/d-vhhold.txt | LC_ALL=C sort -u | grep -c .)"; \
    test "$(sed -n 's/^accessible\.files=//p' ${bsrc} | head -1)" = "$(( tacf - vhf ))"; \
    # --- vhids.txt: B-18.3's behaviour scaling ------------------------------- \
    # Four recordings, and they are the reason the four `behaviour` rows were \
    # not converted at the rung below and must not be converted at this one. \
    # Constructor.newInstance wraps whatever the constructor throws in \
    # InvocationTargetException and reports inaccessibility as \
    # IllegalAccessException; MethodHandle.invoke wraps nothing and a Lookup \
    # reports inaccessibility differently. These four transcripts observe the \
    # old wrapping AS THEIR OUTCOME, so B-18.3 is multiplied by the fraction of \
    # them that still matches, and a tree that converts one of the four \
    # invocation sites here loses the leg in proportion rather than failing a \
    # gate. \
    grep -v '^#' /tests/baseline/vhids.txt | grep . \
      | LC_ALL=C sort -u > /tmp/s-vhids.txt; \
    test "$(grep -c . /tmp/s-vhids.txt)" = 4; \
    test "$(grep -c . /tmp/s-vhids.txt)" \
       = "$(grep -v '^#' /tests/baseline/vhids.txt | grep -c .)"; \
    while read -r id; do \
        grep -qxF "${id}" /tests/assets/ids.txt \
          || { echo "vhids.txt names a recording that is not in assets/ids.txt: ${id}" >&2; exit 1; }; \
        test -s "/tests/assets/rec/jdk25-step13/$(echo "${id}" | tr / .).txt" \
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
    grep -rlF 'java.lang.IllegalAccessException' /tests/assets/rec/jdk25-step13 \
      | sed 's|.*/||; s|\.txt$||' | tr . / | LC_ALL=C sort -u > /tmp/d-vhall.txt; \
    grep -rlF 'java.util.ServiceConfigurationError' /tests/assets/rec/jdk25-step13 \
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
    # --- modimport.txt, B-19.2's population, INHERITED FROM THE RUNG BELOW ---- \
    # THE FIRST OF THREE FILES THIS RUNG DID NOT CUT AND CANNOT RE-CUT. Step 12 \
    # cut modimport.txt for its C-2, jdkimport.txt for its C-3 and \
    # scopedvalue.txt for its C-4; B-19 is that family minus its C-1, so all \
    # three arrive here as the population of a HOLD. Nothing below leans on a \
    # helper: the buckets the rungs beneath share are `<path>#<method>` keys \
    # and none of these three is keyed that way, so each derivation is written \
    # out where it is used. \
    # \
    # 322 ROWS AND NOT 171. A module import is a property of a FILE, so the \
    # population is every file javac compiles, and the 151 that import no JDK \
    # type at all are rows that say `n=0` rather than an absence from the file. \
    # C-2 scores min(the 171 sets are right, the 151 are still empty): a tree \
    # that writes `import module java.base;` into all 322 files scores the 152 \
    # it was right about on the first half and nothing at all on the second. \
    # The empty rows have to be IN the file for that second half to have a \
    # denominator it can name, and that is the whole reason the count is 322. \
    # \
    # AND 322 IS A NUMBER THIS IMAGE CAN CHECK, because classnames.txt is the \
    # same population under a different key: step 08 cut it as the fully \
    # qualified name of every class in the tree. Stripping `src/<set>/java/` \
    # off a modimport path and turning the slashes into dots has to land on \
    # classnames.txt exactly -- same count, same members -- and a modimport.txt \
    # that had lost a row, or gained one for a file javac does not compile, is \
    # a diff here rather than a count that is merely smaller than it was. \
    grep -v '^#' /tests/baseline/modimport.txt | grep . > /tmp/s-mimp.txt; \
    test "$(grep -c . /tmp/s-mimp.txt)" = 322; \
    awk '{ print $1 }' /tmp/s-mimp.txt | LC_ALL=C sort -u > /tmp/d-mipath.txt; \
    test "$(grep -c . /tmp/d-mipath.txt)" = 322; \
    sed -e 's#^.*/src/[a-z]*/java/##' -e 's#\.java$##' -e 's#/#.#g' /tmp/d-mipath.txt \
      | LC_ALL=C sort -u > /tmp/d-micn.txt; \
    grep -v '^#' /tests/baseline/classnames.txt | grep . | LC_ALL=C sort -u > /tmp/s-micn.txt; \
    if ! cmp -s /tmp/d-micn.txt /tmp/s-micn.txt; then \
        echo "baseline/modimport.txt's 322 paths are not baseline/classnames.txt's 322 classes:" >&2; \
        diff /tmp/s-micn.txt /tmp/d-micn.txt >&2 || true; exit 1; \
    fi; \
    # THE SETS ARE DERIVED, NOT DECIDED, AND THE DERIVATION FLIPPED AT THIS \
    # RUNG. Step 12's image read the need: a file needed `import module M` \
    # exactly when it held a single-type import of a JDK type whose package M \
    # exports, so it grouped the seed's `kind=type` jdkimport records, which \
    # carry the module on every row. That reading is not available here and \
    # would not be true if it were. The imports it counted are the ones the \
    # rung below DELETED -- 495 single-type JDK imports went in, 43 came out -- \
    # so grouping them now yields 25 rows where the file wants 171. \
    # \
    # SO THIS RUNG READS THE HAVE AND NOT THE NEED. `modimport[n]` records the \
    # module imports the seed tree actually carries, one row per written \
    # statement with `name=` the module and `in=` the file, and grouping THOSE \
    # by file is the same set modimport.txt's positive rows hold -- the rung \
    # below's work, read back off the tree it produced. The header keys say the \
    # same thing in totals (modimport.size=188 over modimport.files=171, both \
    # equal to their target.mi.* twins), and the first half of this cross-check \
    # already asserted those; this is the same claim member by member. \
    # \
    # WHAT THE 43 SURVIVORS ARE IS CHECKED BELOW AND NOT HERE, because a \
    # leftover single-type import is no longer evidence of a missing module \
    # import in either direction: 25 of the 43 are `kind=type` and every one \
    # sits in a file that ALSO carries the module import for the same module, \
    # kept deliberately because the simple name is ambiguous across two \
    # modules (`ambig=3` on all 25). Reading them as unmet need would charge \
    # this rung for the rung below's correct decision. \
    # \
    # The `sort -u` before the grouping is doing two jobs: it puts one file's \
    # rows together so the group-by works on a stream, and it sorts the module \
    # names inside each group, which is the order modimport.txt writes them \
    # in. A set written in a different order would compare unequal here and \
    # the scorer would have the same trouble, so the ordering is part of the \
    # file's contract and not an accident of how it was generated. \
    sed -n 's/^modimport\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ md = ""; f = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 5) == "name=") md = substr($i, 6); \
                  if (substr($i, 1, 3) == "in=")   f  = substr($i, 4) } \
               if (md != "" && f != "") print f, md }' \
      | LC_ALL=C sort -u \
      | awk '{ if ($1 != p) { if (p != "") print p, s, n; p = $1; s = $2; n = 1 } \
               else { s = s "," $2; n++ } } \
             END { if (p != "") print p, s, n }' \
      | LC_ALL=C sort > /tmp/d-mineed.txt; \
    awk '$2 != "mods=-" { print $1, substr($2, 6), substr($3, 3) }' /tmp/s-mimp.txt \
      | LC_ALL=C sort > /tmp/d-mipos.txt; \
    if ! cmp -s /tmp/d-mipos.txt /tmp/d-mineed.txt; then \
        echo "baseline/modimport.txt's positive rows are not the module sets this tree's JDK imports need:" >&2; \
        diff /tmp/d-mineed.txt /tmp/d-mipos.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/d-mineed.txt)"                = "${tmif}"; \
    test "$(awk '$2 == "mods=-"' /tmp/s-mimp.txt | grep -c .)" = "${tmin}"; \
    test "$(( tmif + tmin ))"                            = 322; \
    test "$(awk '{ t += $3 } END { print t + 0 }' /tmp/d-mineed.txt)" = "${tmii}"; \
    test "$(awk '$3 >= 2' /tmp/d-mineed.txt | grep -c .)" = "${tmim}"; \
    test "$(awk '$2 !~ /(^|,)java\.base(,|$)/' /tmp/d-mineed.txt | grep -c .)" = "${tmib}"; \
    test "$(awk '{ split($2, m, ","); for (i in m) seen[m[i]] = 1 } \
                 END { print length(seen) }' /tmp/d-mineed.txt)" = "${tmimo}"; \
    test "${tmif}"  = 171; \
    test "${tmin}"  = 151; \
    test "${tmii}"  = 188; \
    test "${tmim}"  = 15; \
    test "${tmib}"  = 4; \
    test "${tmimo}" = 6; \
    # 188 IMPORTS ACROSS 171 FILES, which is 156 files needing one module, 13 \
    # needing two and 2 needing three. The distribution is asserted and not \
    # just the total because the total is the one number a wrong derivation \
    # is most likely to get right by accident: a set built by unioning across \
    # files instead of per file lands on the same 188 with the rows in the \
    # wrong places. \
    test "$(awk '$3 == 1' /tmp/d-mineed.txt | grep -c .)" = 156; \
    test "$(awk '$3 == 2' /tmp/d-mineed.txt | grep -c .)" = 13; \
    test "$(awk '$3 == 3' /tmp/d-mineed.txt | grep -c .)" = 2; \
    test "$(( 156 + 13 * 2 + 2 * 3 ))" = "${tmii}"; \
    # AND 152 OF THE 171 ARE `java.base` ALONE. That is the number a blanket \
    # edit scores, and it is worth having in the image because it is the \
    # distance between C-2's two halves: 152 right out of 171 on the sets, and \
    # 0 out of 151 on the files that were supposed to stay empty. \
    test "$(awk '$2 == "java.base"' /tmp/d-mineed.txt | grep -c .)" = 152; \
    # EVERY ROW'S OWN `n=` AND `base=` AGREE WITH ITS OWN `mods=`, including on \
    # the 151 empty ones, which the derivation above never looks at. `n` is the \
    # size of the set and `base` is java.base's membership in it; a row that \
    # said otherwise would be scored by a gate reading one field against a tree \
    # producing another. \
    awk '{ n = split(substr($2, 6), m, ","); if ($2 == "mods=-") n = 0; \
           if (substr($3, 3) + 0 != n) { \
              print "modimport.txt: n= is not the size of mods= on: " $0 > "/dev/stderr"; bad = 1 } \
           b = ($2 ~ /(^|=|,)java\.base(,|$)/) ? "base=yes" : "base=no"; \
           if ($4 != b) { \
              print "modimport.txt: base= is not java.base membership on: " $0 > "/dev/stderr"; bad = 1 } } \
         END { exit bad ? 1 : 0 }' /tmp/s-mimp.txt; \
    # AND `target.mi.was` STILL READS 0, WHICH IS NOT A CLAIM ABOUT THIS TREE. \
    # It is the held figure the gate reported beside the produced one AT THE \
    # RUNG BELOW: 0, because `import module` is a 25 construct and the tree \
    # STEP 12 was handed compiled at 22. Carrying it forward unchanged is what \
    # keeps B-19.2's arrival half on the denominator it was calibrated with. \
    # \
    # THE TREE THIS RUNG IS HANDED IS THE ONE STEP 12 PRODUCED, so its own \
    # counters read the finished figures and not `was` -- 188 module imports \
    # over 171 files, which are `target.mi.imports` and `target.mi.files`, the \
    # same two equalities the first half of this cross-check states from the \
    # header keys. Asserting them against `tmiw` would be asserting that the \
    # rung below did nothing, and it is worth writing out because `= 0` is the \
    # assertion an inherited stanza keeps by accident. \
    test "${tmiw}" = 0; \
    test "$(sed -n 's/^modimport\.size=//p'  ${bsrc} | head -1)" = "${tmii}"; \
    test "$(sed -n 's/^modimport\.files=//p' ${bsrc} | head -1)" = "${tmif}"; \
    test "$(sed -n 's/^modimport\.size=//p'  ${bsrc} | head -1)" -gt "${tmiw}"; \
    # --- jdkimport.txt, B-19.3's population, AND B-19.2'S OTHER FACE ---------- \
    # 495 ROWS, ONE PER SINGLE-TYPE JDK IMPORT THE RUNG BELOW WAS HANDED, and \
    # the verdict on each was a function of the row itself rather than a \
    # judgement typed into the file. The rule has four cases: a static import \
    # holds because a module import supplies package members and not type \
    # members; a nested-type import holds for the same reason one level down; \
    # an import whose simple name is exported by more than one package of the \
    # module holds because the module import cannot disambiguate it and javac \
    # says so; everything else departs, subsumed. 452 departed and 43 held. \
    # \
    # STEP 12 REPRODUCED ALL 495 VERDICTS FROM ITS SEED. This rung cannot and \
    # must not: 452 of the rows name import statements that are GONE, which is \
    # precisely what the rung below was scored for doing. What is checkable \
    # here is the other end of the same claim, and it is checked in both \
    # directions -- the 43 that were to hold are still there and still say the \
    # same six fields, and the 452 that were to depart are not there at all. \
    # \
    # THE 43 ARE COMPARED AS ROWS AND NOT AS A COUNT. Running this tree's own \
    # jdkimport records through the rule the file was cut with has to land on \
    # the file's 43 `verdict=hold` rows byte for byte: same file, same `q=`, \
    # same kind, same module, same `why=`. A tree that deleted a static import \
    # and kept a different ambiguous one holds 43 of 43 by count and fails \
    # here, which is the failure worth catching -- B-19.3's want is a SET. \
    # \
    # `ambig=` IS THE SEED'S OWN COUNT of how many exported packages offer that \
    # simple name, so `ambig > 1` is the ambiguity test and `ambig = 1` is the \
    # clean case. In the tree this rung is handed the 18 rows that read \
    # `ambig=0` are the 16 static and 2 nested ones, where the field is not \
    # asked, and the 25 that remain read `ambig=3` -- there is no surviving \
    # `ambig=1` row, because `ambig=1` is exactly the condition that made a row \
    # depart. \
    grep -v '^#' /tests/baseline/jdkimport.txt | grep . | LC_ALL=C sort > /tmp/s-jimp.txt; \
    grep ' verdict=hold ' /tmp/s-jimp.txt | LC_ALL=C sort > /tmp/s-jihold.txt; \
    sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ q = ""; k = ""; md = ""; am = 0; f = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 2) == "q=")     q  = substr($i, 3); \
                  if (substr($i, 1, 5) == "kind=")  k  = substr($i, 6); \
                  if (substr($i, 1, 4) == "mod=")   md = substr($i, 5); \
                  if (substr($i, 1, 6) == "ambig=") am = substr($i, 7) + 0; \
                  if (substr($i, 1, 3) == "in=")    f  = substr($i, 4) } \
               if      (k == "static") { v = "hold";   w = "static" } \
               else if (k == "nested") { v = "hold";   w = "nested" } \
               else if (am > 1)        { v = "hold";   w = "ambiguous" } \
               else                    { v = "depart"; w = "subsumed" } \
               print f, "q=" q, "kind=" k, "mod=" md, "verdict=" v, "why=" w }' \
      | LC_ALL=C sort > /tmp/d-jimp.txt; \
    if ! cmp -s /tmp/s-jihold.txt /tmp/d-jimp.txt; then \
        echo "this tree's remaining JDK imports are not baseline/jdkimport.txt's 43 held rows:" >&2; \
        diff /tmp/s-jihold.txt /tmp/d-jimp.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c ' verdict=depart ' /tmp/d-jimp.txt)" = 0; \
    # AND THE 452 ARE GONE BY NAME. The comparison above would already fail if \
    # a departed import had survived -- it would arrive as a 44th row -- but \
    # the message it printed would be about the hold set, which is the wrong \
    # thing to read in a log. `<file> q=<type>` is the key on both sides, so \
    # the intersection of the departed keys with the tree's surviving keys is \
    # the direct statement, and it is empty. \
    awk '$5 == "verdict=depart" { print $1, $2 }' /tmp/s-jimp.txt \
      | LC_ALL=C sort -u > /tmp/d-jidep.txt; \
    awk '{ print $1, $2 }' /tmp/d-jimp.txt | LC_ALL=C sort -u > /tmp/d-jicur.txt; \
    test "$(grep -c . /tmp/d-jidep.txt)" = "${tjid}"; \
    LC_ALL=C comm -12 /tmp/d-jidep.txt /tmp/d-jicur.txt > /tmp/d-jiback.txt; \
    if [ -s /tmp/d-jiback.txt ]; then \
        echo "single-type JDK imports baseline/jdkimport.txt marked depart are still in the tree:" >&2; \
        cat /tmp/d-jiback.txt >&2; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-jimp.txt)"                 = "${tjit}"; \
    test "$(grep -c ' verdict=depart ' /tmp/s-jimp.txt)" = "${tjid}"; \
    test "$(grep -c ' verdict=hold '   /tmp/s-jimp.txt)" = "${tjih}"; \
    test "$(grep -c ' why=subsumed$'   /tmp/s-jimp.txt)" = "${tjid}"; \
    test "$(grep -c ' why=ambiguous$'  /tmp/s-jimp.txt)" = "${tjia}"; \
    test "$(grep -c ' why=static$'     /tmp/s-jimp.txt)" = "${tjis}"; \
    test "$(grep -c ' why=nested$'     /tmp/s-jimp.txt)" = "${tjin}"; \
    test "$(awk '{ print $1 }' /tmp/s-jimp.txt | LC_ALL=C sort -u | grep -c .)" = "${tjif}"; \
    test "$(( tjid + tjih ))"               = "${tjit}"; \
    test "$(( tjia + tjis + tjin ))"        = "${tjih}"; \
    test "${tjit}" = 495; \
    test "${tjid}" = 452; \
    test "${tjih}" = 43; \
    test "${tjia}" = 25; \
    test "${tjis}" = 16; \
    test "${tjin}" = 2; \
    # `target.ji.want` IS THE HELD HALF UNDER ITS ARRIVAL NAME, and at this rung \
    # it is the ONLY half with anything to earn. Step 12's C-3 was min()ed the \
    # way step 04 and step 11 are -- 452 had to be gone AND 43 had to still be \
    # there -- because a tree that deleted the import block wholesale departs \
    # 495 of 452 and would otherwise read as a clean pass. Inherited as B-19.3 \
    # the min() still stands, but the departed half is already spent: this \
    # rung's candidate can only lose it, by writing an import back. \
    test "${tjiw}" = "${tjih}"; \
    # THE SEED'S OWN KIND TALLIES ARE NOW THE HELD TALLIES, and that is the \
    # shape of every inherited counter at this rung. `jdkimport.size` read 495 \
    # in the tree step 12 was handed and reads 43 in the tree step 13 is handed \
    # -- the same key, the same scanner, the migration in between -- so it is \
    # `target.ji.hold` it must equal here and not `target.ji.total`. \
    # \
    # `jdkimport.files` MOVES FURTHEST AND IS THE MOST INSTRUCTIVE. It read 171 \
    # before, which is why `target.ji.files` is 171, and reads 43 now: the 43 \
    # survivors are one apiece in 43 distinct files, so the key that used to \
    # be a file count is numerically a row count. Asserting it against 171 \
    # would be asserting that the migration did not happen. \
    # \
    # THE THREE KIND TALLIES DO NOT MOVE AT ALL, and that is the real content \
    # here: static was 16 and is 16, nested was 2 and is 2, ambiguous was 25 \
    # and is 25, because every row in each of those three classes was a row the \
    # rule said to hold. `jdkimport.type` is the one that moves within the \
    # kinds -- 477 type imports before, 25 after -- and the 25 are exactly the \
    # ambiguous ones, which is `tjih - tjis - tjin` and also `tjia`, asserted \
    # both ways because the two readings coinciding is the claim. \
    # \
    # `jdkimport.ondemand` is 0 and asserted at 0: the tree has no \
    # `import java.util.zip.*;` anywhere, so the fourth shape the scanner \
    # distinguishes is exercised only by the smoke tree, and a counter that \
    # started reading non-zero here would mean the population had gained a row \
    # shape jdkimport.txt has no verdict column for. \
    test "$(sed -n 's/^jdkimport\.size=//p'      ${bsrc} | head -1)" = "${tjih}"; \
    test "$(sed -n 's/^jdkimport\.files=//p'     ${bsrc} | head -1)" = "${tjih}"; \
    test "$(sed -n 's/^jdkimport\.static=//p'    ${bsrc} | head -1)" = "${tjis}"; \
    test "$(sed -n 's/^jdkimport\.nested=//p'    ${bsrc} | head -1)" = "${tjin}"; \
    test "$(sed -n 's/^jdkimport\.ambiguous=//p' ${bsrc} | head -1)" = "${tjia}"; \
    test "$(sed -n 's/^jdkimport\.ondemand=//p'  ${bsrc} | head -1)" = 0; \
    test "$(sed -n 's/^jdkimport\.type=//p'      ${bsrc} | head -1)" = "$(( tjih - tjis - tjin ))"; \
    test "$(sed -n 's/^jdkimport\.type=//p'      ${bsrc} | head -1)" = "${tjia}"; \
    test "$(awk '{ print $1 }' /tmp/d-jimp.txt | LC_ALL=C sort -u | grep -c .)" = "${tjih}"; \
    # AND THE 25 THAT REMAIN ARE DISAMBIGUATORS, NOT LEFTOVERS. The modimport \
    # section above said this check lives here. Every surviving `kind=type` row \
    # sits in a file that ALSO carries `import module <its own mod>`: the \
    # single-type import was kept BESIDE the module import, because the simple \
    # name is offered by three exported packages and javac will not choose. \
    # Reading a leftover type import as a missing module import -- which is \
    # what step 12's derivation would do if it were still wired up -- charges \
    # this rung for the rung below's correct decision, so the relation is \
    # asserted in the direction that is actually true. \
    awk '$3 == "kind=type" { print $1, substr($4, 5) }' /tmp/d-jimp.txt \
      | LC_ALL=C sort -u > /tmp/d-jityp.txt; \
    test "$(grep -c . /tmp/d-jityp.txt)" = "${tjia}"; \
    awk '{ n = split($2, m, ","); for (i = 1; i <= n; i++) print $1, m[i] }' /tmp/d-mineed.txt \
      | LC_ALL=C sort -u > /tmp/d-mipair.txt; \
    test "$(grep -c . /tmp/d-mipair.txt)" = "${tmii}"; \
    LC_ALL=C comm -23 /tmp/d-jityp.txt /tmp/d-mipair.txt > /tmp/d-jiorph.txt; \
    if [ -s /tmp/d-jiorph.txt ]; then \
        echo "a held single-type import has no module import beside it, so it is a leftover and not a disambiguator:" >&2; \
        cat /tmp/d-jiorph.txt >&2; exit 1; \
    fi; \
    test "$(sed -n 's/^jdkimport\[[0-9]*\]=//p' ${bsrc} \
              | grep 'kind=type' | grep -c 'ambig=3')" = "${tjia}"; \
    # AND B-19.2 IS THIS FILE READ THE OTHER WAY. The two legs are scored apart \
    # and were cut apart, but a module set is nothing but the modules of a \
    # file's `kind=type` rows, so jdkimport.txt has to regenerate \
    # modimport.txt's 171 positive rows on its own. This is the check that \
    # keeps the pair honest: a type import dropped from jdkimport.txt would \
    # quietly shrink a module set in modimport.txt too, and both files would \
    # still agree with their own counters. It runs over the FROZEN file's 495 \
    # rows, not over the tree -- the tree has 25 type imports left and could \
    # not rebuild 171 sets from them -- so what it proves is that the two \
    # inherited files were cut from one measurement, which is as true at this \
    # rung as it was at the one below. \
    awk '$3 == "kind=type" { print $1, substr($4, 5) }' /tmp/s-jimp.txt \
      | LC_ALL=C sort -u \
      | awk '{ if ($1 != p) { if (p != "") print p, s, n; p = $1; s = $2; n = 1 } \
               else { s = s "," $2; n++ } } \
             END { if (p != "") print p, s, n }' \
      | LC_ALL=C sort > /tmp/d-jimods.txt; \
    if ! cmp -s /tmp/d-jimods.txt /tmp/d-mineed.txt; then \
        echo "baseline/jdkimport.txt's type imports do not rebuild baseline/modimport.txt's module sets:" >&2; \
        diff /tmp/d-mineed.txt /tmp/d-jimods.txt >&2 || true; exit 1; \
    fi; \
    # --- scopedvalue.txt, B-19.4's population --------------------------------- \
    # EIGHT ROWS, ONE PER ThreadLocal FIELD THE RUNG BELOW WAS HANDED, and the \
    # population was not a judgement: it was exactly that tree's eight \
    # `decl=FIELD form=decl` threadlocal records, re-keyed to `<file> <field>`. \
    # What IS a judgement is the verdict column, one convert and seven holds, \
    # and the seven hold for six different reasons -- two of the Arquillian \
    # fields fail the same way in two different projects, which is why the \
    # reason count is 6 and not 7. \
    # \
    # AND THE VERDICT COLUMN IS NOW A PARTITION OF THE TREE. This is the \
    # cleanest inherited check in the block, because the rung below did the \
    # work and the tree records the result in two populations rather than one: \
    # the seven `verdict=hold` rows have to be the seven ThreadLocal field \
    # declarations that are still ThreadLocal, and the one `verdict=convert` \
    # row has to be the one ScopedValue field declaration that now exists. \
    # Step 12's image could only compare the eight against eight ThreadLocals; \
    # this one compares 7 against 7 AND 1 against 1, by file and field name, \
    # which is a strictly stronger statement -- it fails a tree that converted \
    # the WRONG field, where a count of eight declarations would not. \
    # \
    # THE VERDICTS THEMSELVES STILL CANNOT BE DERIVED IN THIS IMAGE and are \
    # still not asserted as if they could. Whether an extent exists is a \
    # question about who calls what, and the answer lives in the prose at the \
    # head of scopedvalue.txt and in NOTES 16.61. What the partition proves is \
    # that the tree AGREES with the verdicts, not that the verdicts are right. \
    grep -v '^#' /tests/baseline/scopedvalue.txt | grep . > /tmp/s-sval.txt; \
    awk '$3 == "verdict=hold"    { print $1, substr($2, 7) }' /tmp/s-sval.txt \
      | LC_ALL=C sort > /tmp/d-svhold.txt; \
    awk '$3 == "verdict=convert" { print $1, substr($2, 7) }' /tmp/s-sval.txt \
      | LC_ALL=C sort > /tmp/d-svconv.txt; \
    sed -n 's/^threadlocal\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ n = ""; d = ""; fm = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 5) == "name=") n  = substr($i, 6); \
                  if (substr($i, 1, 5) == "decl=") d  = substr($i, 6); \
                  if (substr($i, 1, 5) == "form=") fm = substr($i, 6) } \
               if (d == "FIELD" && fm == "decl" && n != "") { split($1, a, ":"); print a[1], n } }' \
      | LC_ALL=C sort > /tmp/d-tldecl.txt; \
    if ! cmp -s /tmp/d-svhold.txt /tmp/d-tldecl.txt; then \
        echo "baseline/scopedvalue.txt's seven holds are not this tree's remaining ThreadLocal field declarations:" >&2; \
        diff /tmp/d-svhold.txt /tmp/d-tldecl.txt >&2 || true; exit 1; \
    fi; \
    sed -n 's/^scopedvalue\[[0-9]*\]=//p' ${bsrc} \
      | awk '{ n = ""; d = ""; fm = ""; \
               for (i = 2; i <= NF; i++) { \
                  if (substr($i, 1, 5) == "name=") n  = substr($i, 6); \
                  if (substr($i, 1, 5) == "decl=") d  = substr($i, 6); \
                  if (substr($i, 1, 5) == "form=") fm = substr($i, 6) } \
               if (d == "FIELD" && fm == "decl" && n != "") { split($1, a, ":"); print a[1], n } }' \
      | LC_ALL=C sort > /tmp/d-svdecl.txt; \
    if ! cmp -s /tmp/d-svconv.txt /tmp/d-svdecl.txt; then \
        echo "baseline/scopedvalue.txt's one convert is not this tree's ScopedValue field declaration:" >&2; \
        diff /tmp/d-svconv.txt /tmp/d-svdecl.txt >&2 || true; exit 1; \
    fi; \
    test "$(grep -c . /tmp/s-sval.txt)"                    = "${tsvf}"; \
    test "$(grep -c ' verdict=convert ' /tmp/s-sval.txt)"  = "${tsvc}"; \
    test "$(grep -c ' verdict=hold '    /tmp/s-sval.txt)"  = "${tsvh}"; \
    test "$(( tsvc + tsvh ))"                              = "${tsvf}"; \
    test "$(awk '{ print $4 }' /tmp/s-sval.txt | grep -c '^why=')" = "${tsvf}"; \
    test "${tsvf}" = 8; \
    test "${tsvc}" = 1; \
    test "${tsvh}" = 7; \
    test "$(sed -n 's/^threadlocal\.decls=//p' ${bsrc} | head -1)" = "${tsvh}"; \
    test "$(grep ' verdict=hold ' /tmp/s-sval.txt | awk '{ print $4 }' \
              | LC_ALL=C sort -u | grep -c .)" = 6; \
    # FIVE FILES HOLD EIGHT FIELDS AND SEVEN FILES TOUCH A ThreadLocal. The two \
    # extra files call one they do not declare -- JUnitTestRunner and its test \
    # case, both reaching Arquillian's `public static` field, which is the \
    # exact reason that field is one of the seven holds. Asserting both numbers \
    # keeps the distinction in the image: C-4's population is DECLARATIONS, \
    # and the call sites are a larger set that a converted field has to keep \
    # working for. \
    test "$(awk '{ print $1 }' /tmp/s-sval.txt | LC_ALL=C sort -u | grep -c .)" = 5; \
    test "$(sed -n 's/^threadlocal\.files=//p' ${bsrc} | head -1)" = 7; \
    # AND `target.sv.was` STILL READS 0, WHICH IS NOT A CLAIM ABOUT THIS TREE. \
    # It is the count of ScopedValue in the tree the RUNG BELOW was handed, \
    # carried forward unchanged so that B-19.4's arrival half keeps the \
    # denominator it was calibrated with. The tree THIS rung is handed is the \
    # one step 12 produced, so it holds a ScopedValue -- exactly one field, in \
    # exactly one file -- and asserting the seed's counters against `tsvw` \
    # would be asserting that step 12 did nothing. \
    test "${tsvw}" = 0; \
    test "$(sed -n 's/^scopedvalue\.files=//p' ${bsrc} | head -1)" = "${tsvc}"; \
    test "$(sed -n 's/^scopedvalue\.decls=//p' ${bsrc} | head -1)" = "${tsvc}"; \
    test "$(sed -n 's/^scopedvalue\.size=//p'  ${bsrc} | head -1)" -gt "${tsvw}"; \
    # `target.sv.calls` IS THE ONE NUMBER HERE WITH NO BASELINE BEHIND IT, and \
    # it is asserted as a literal because that is honestly all this image can \
    # do with it. The gate reports `scopedvalue.size - scopedvalue.decls` off \
    # the PRODUCED tree and prints this beside it, so it is the golden's own \
    # count of the call sites the converted field ends up with -- a `get`, the \
    # `where(...).run(...)` that binds it, and the reads in between. It is a \
    # `fact` line and scales no leg: nothing in test.sh compares the two, and \
    # the section that proves that is the test.sh-shape block below. It is \
    # asserted at all so that a target.txt regenerated without the golden \
    # cannot leave it at 0 unnoticed. \
    # AT THIS RUNG IT HAS ONE. `target.sv.calls` was a literal in step 12's \
    # image because the tree it described did not exist yet; the tree this rung \
    # is handed IS that tree, so the key can be checked against it rather than \
    # trusted: `scopedvalue.size` counts every ScopedValue mention and \
    # `scopedvalue.decls` the declarations among them, so their difference is \
    # the call sites, and that difference has to be the published 8. \
    test "${tsvk}" = 8; \
    test "${tsvk}" -gt "${tsvc}"; \
    test "$(( $(sed -n 's/^scopedvalue\.size=//p'  ${bsrc} | head -1) \
            - $(sed -n 's/^scopedvalue\.decls=//p' ${bsrc} | head -1) ))" = "${tsvk}"; \
    # --- C-5's tally is step 08's modnames.txt, COUNTED --------------------- \
    # NO FILE OF ITS OWN, AND THAT IS THE POINT. C-5 asks that every project \
    # that forks a test JVM hands it `-XX:+UseCompactObjectHeaders`, and \
    # "every project" is a list step 08 already cut and step 11 already \
    # scores B-15.1 against. Cutting a second copy would give the two legs two \
    # populations that could drift apart; deriving the twelve from the same \
    # file gives them one, and target.co.projects and target.b15.mod.total are \
    # asserted equal here for the same reason. \
    # \
    # WHAT THE SCORER TAKES IS THE TWELVE AND NOT THE NAMES. modnames.txt holds \
    # published artifact ids; the probe C-5 runs reports Gradle project paths; \
    # `:api` publishing `arquillian-api` is the ordinary arrangement, so a leg \
    # that matched one against the other would read a tree with the flag on \
    # every fork as a tree with none. test.sh therefore reads this key as a \
    # FLOOR under its own tally of forking projects and reads modnames.txt for \
    # B-15 alone. The derivation below is this image's, not the scorer's. \
    test "$(grep -v '^#' /tests/baseline/modnames.txt | grep -c .)" = "${tcop}"; \
    test "${tcop}" = "${tmt}"; \
    test "${tcop}" = 12; \
    # AND NONE OF THE TWELVE PASSES IT TODAY. The flag is a 25 product flag -- \
    # JEP 519 -- and the tree this rung is handed builds at 22, so the held \
    # figure is 0 and the whole leg is arrival. \
    test "${tcow}" = 0; \
    # THE FLAG ITSELF LIVES IN target.txt AND NOWHERE ELSE. test.sh reads it \
    # with `tv 'target\.co\.flag'` and greps the fork arguments for whatever \
    # comes back, which is the only arrangement in which the flag can be \
    # corrected in one place. BOTH occurrences in test.sh are comments -- the \
    # scoring header's one-line summary of B-19.5 and the paragraph above that \
    # leg -- and that is asserted rather than assumed: a flag spelled out in an \
    # active line would be a second source of truth, and the one that disagreed \
    # would be the one in target.txt. \
    test "${tcof}" = '-XX:+UseCompactObjectHeaders'; \
    # The count is pinned to what test.sh CURRENTLY says and moves with its \
    # prose; what does not move is the line below it, which is the invariant \
    # the count is only a tripwire for. It was 5 while this leg was step 12's \
    # own C-5 and is 2 now that the leg is B-19.5 and its long-form paragraph \
    # went across as one line of the inherited-gate summary -- re-pin it when \
    # test.sh's header is rewritten (NOTES 16.98). \
    test "$(grep -cF -e "${tcof}" /tests/test.sh)" = 2; \
    test "$(grep -F -e "${tcof}" /tests/test.sh | grep -vc '^ *#')" = 0; \
    # --- the six behaviour-id lists are pairwise disjoint --------------------- \
    # SIX LEGS SCALE BY A NAMED SET OF RECORDED FIXTURES and all six sets ride \
    # on the one era: B-13's httpids, B-14's nioids, B-15.3's svcids, B-16.3's \
    # tbids, B-17.3's swids and B-18.3's vhids. THIS IS THE FIRST RUNG AT WHICH \
    # ALL SIX ARE FAMILY-B LISTS -- vhids scaled a family-C leg one rung ago \
    # and scales an inherited one now -- so an id landing on two of them is one \
    # regression charged twice inside the family that has to read flat, and the \
    # seed's 1.000000 would be the first thing it broke. Fifteen pairs, every \
    # one empty, and the loop is written over the list of files so a seventh \
    # list is one more word rather than five more tests. \
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
    # --- the era this rung re-cuts by exactly one ----------------------------- \
    # Step 11 had a `target.ids` key and an ids-step11-moved.txt beside it: the \
    # 37 transcripts whose output its family C changed, 36 of them stack traces \
    # through the SecurityActions ladders C-2 deleted. Step 12 had NEITHER, \
    # because its family C was a class-file level, two rewrites of import \
    # blocks, one ScopedValue conversion and a JVM flag and not one of the five \
    # changed what a program prints. THIS RUNG HAS ONE: ids-step13-moved.txt \
    # names webrunner/shape-servlet and nothing else, because the golden renames \
    # ServletTestRunner#writeObject -- the only answer javac's `serial` lint has \
    # that C-1 does not forbid -- and the probe records declared method names. \
    # 938 of the 939 transcripts are step 11's byte for byte. \
    # \
    # AND THERE IS STILL NO `target.ids` KEY, WHICH IS NOT THE SAME CLAIM. \
    # Step 11 published one because the ids it moved were ids its own frozen \
    # populations name, and a scorer had to be told which. NOT ONE FROZEN \
    # POPULATION IN THIS IMAGE NAMES webrunner/shape-servlet: the two id lists \
    # that carry a `webrunner/` prefix at all are step 09's tbids.txt, whose \
    # five are the text-block recordings, and step 11's vhids.txt, whose one is \
    # security-new-instance-non-public-class. So the move is a BUILD-TIME fact, \
    # checked by rebuild-verifier.sh against authoring/ids-step13-moved.txt \
    # before the image is written, and no gate needs to be told about it at \
    # grading time. A stray `target.ids` here would be a claim to the scorer \
    # that some population had to be forgiven, and none did. \
    # \
    # WHAT IS ASSERTED IS THEREFORE: no key, no transcript walking through a \
    # GetTcclAction frame, and all 939 ids present. 939 is the family-A \
    # denominator, and it is the denominator this rung's floor reads 938 over. \
    test "$(grep -c '^target\.ids=' ${btgt})" = 0; \
    test "$(grep -rlF 'webrunner/shape-servlet' /tests/baseline \
              | grep -c .)" = 0; \
    test "$(grep -rlF 'GetTcclAction' /tests/assets/rec/jdk25-step13 \
              | grep -c .)" = 0; \
    test "$(grep -c . /tests/assets/ids.txt)" = 939; \
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
    rm -f /tmp/s-ip.txt /tmp/d-ip.txt /tmp/d-ip-all.txt /tmp/d-ipref.txt /tmp/s-ip-sorted.txt \
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
    rm -f /tmp/s-mimp.txt /tmp/d-mipath.txt /tmp/d-micn.txt /tmp/s-micn.txt \
          /tmp/d-mineed.txt /tmp/d-mipos.txt /tmp/s-jimp.txt /tmp/d-jimp.txt \
          /tmp/d-jimods.txt /tmp/s-sval.txt /tmp/d-svrow.txt /tmp/d-tldecl.txt; \
    # ...and the file the split carried the header across in, which is not a \
    # working file of any one check and so is swept on its own. \
    rm -f /tmp/consist-binds.sh; \
    echo "step 13: the second half of the baseline cross-check agrees -- B-19's modimport, jdkimport and scopedvalue read as held, ipattern and catchparam still name fire and were not re-pointed, and the inherited set files of steps 09 through 12 still describe the tree"

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
# 10, 11 and this one have each added blocks to fixtures/src-rehearsal.sh --
# 71194 bytes at step 06, 214766 here -- without moving a word of it. The blocks
# it describes are all still there and still asserted, so it is kept; but where
# it says "this rung" it is step 06 speaking, and what holds at THIS rung is:
#
#   FOUR NEW RECORD BLOCKS, AND THEY ARE NEW TO THE PLUGIN. modimport,
#   jdkimport, scopedvalue and threadlocal are methods that did not exist in
#   the SourceScan jar the rung below built, so until this layer runs nothing
#   in the image has ever driven them over a compiler. Measured on the smoke
#   tree and then frozen: ONE module import, in the one file that has one; 42
#   JDK imports over all three files in the four retention kinds the gate
#   distinguishes -- 37 plain, 3 static, 1 nested, 1 on-demand -- of which
#   exactly one carries a name java.base exports from three packages and cannot
#   supply; 11 ScopedValue records, one declaration and ten calls; and 4
#   ThreadLocal ones, one declaration and three calls, because C-4 scores a
#   carrier for STAYING as surely as for going and a net figure would read the
#   same on a tree that converted one and on a tree that deleted one and
#   declared another.
#
#   A THIRD FILE IN THE SMOKE TREE, AND C-2 FORCES IT. Every cut in this file
#   is one sed address over a section marker, running from the marker to the end
#   of the file -- and C-2's construct is an IMPORT DECLARATION, which sits
#   above every marker there is. A step-12 section written into Smoke.java would
#   survive its own cut and the step-12 cut would read modimport.size=1 where it
#   has to read 0. Mod.java is cut by being ABSENT: flatten() writes the files it
#   knows about into a fresh directory, so a file it has never heard of is out of
#   all eight cut trees without a line of cut logic being spent on it, and the
#   cut asserts javac.sources=2 so that the exclusion is PROVED rather than
#   merely unmentioned. It is also why flatten() grew a third mode: `yes` cuts
#   Lib.java at the marker and `no` drops it, and this rung needs it WHOLE,
#   because step 12 appends nothing to Lib.java and a cut at a marker it does
#   not carry would trip flatten()'s own "nothing was cut" guard.
#
#   THE SPELLINGS MOVED THREE LEVELS AND NOT ONE: `25 25`, `24 24` and
#   `def ""`. The rehearsal asserts those three are still in test.sh before
#   driving them, so a rung that moved its level fails here rather than
#   measuring a different instrument quietly. The whole tree compiles at 25 --
#   53 classes from three sources -- and REFUSES 24, and the refusal is
#   asserted as an identity rather than a count: ONE error, in javac's own
#   wording, `module imports are not supported in -source 24`, drawn by the
#   single `import module java.base;` at the top of Mod.java.
#
#   AND THE LEVEL IS DERIVED AND NOT DECREMENTED, which is the sentence this
#   rung has that no rung below it did. Four of this rung's five legs move no
#   source level at all: ScopedValue is a class in java.lang and binding one is
#   an ordinary method call, -XX:+UseCompactObjectHeaders is an argument javac
#   never sees, the class-file major is a property of the emitted artifact
#   rather than of what javac will parse, and C-3's holds are import
#   declarations that have been legal since Java 1. What is left is C-2's
#   module import, which JEP 511 made standard in 25 after three releases of
#   preview -- so 24 is the one level at which exactly one of the five legs
#   stops compiling, and a second error of any kind is a broken smoke tree
#   rather than a stricter compiler.
#
#   AND THE RETIRED LIST GROWS BY TWO, which is what step 11 said would happen
#   and why it wrote the list as spellings rather than as levels. Step 11
#   retired `ok20` alone and kept `ok21` live, because test.sh still made that
#   call with the opposite expected answer. Here the rung's own level is 25, the
#   target itself, and BOTH of step 11's spellings go: `ok22=$(srcgate 22 22)`
#   and `ok21=$(srcgate 21 21)` join 8, 11, 17, 16 and 20 on the absent list, and
#   23 appears on neither list because no scorer in this task ever asked for it.
#   All seven retired spellings survive further down as the rehearsal's OWN
#   probes over the cut trees, which is a different claim about a different tree
#   and is labelled as one.
#
#   EIGHT CUTS AND NOT SEVEN, and the eighth says something none of the seven
#   below it could. The step-12 cut is step 11's own tree compiled at -source
#   24, clean, at step 11's 52 classes from TWO sources, with step 11's record
#   blocks at step 11's own frozen figures. TWENTY-TWO INHERITED COUNTERS MOVED
#   AND NONE IS SCORED: svc.platformtype 6 -> 7, svc.providertype 2 -> 3,
#   svc.files 2 -> 3 because Mod.java names java.util.ServiceLoader and its
#   Provider, and nineteen more -- lambdas, stream ops, for-eaches, try
#   statements, var candidates -- because a third file of ordinary Java is a
#   third file of ordinary Java. test.sh reads none of the twenty-two, and the
#   cut asserts every one of them at step 11's value so the movement is recorded
#   here rather than found later by somebody reconciling it with an edit.
#
#   AND THE FOURTH BLOCK DOES NOT READ ZERO, which is the part that had to be
#   written out rather than looped. On every rung below, the cut's claim was
#   "the counters this rung adds all read zero on the tree below it". Here
#   modimport, scopedvalue and threadlocal do and JDKIMPORT READS 37, because
#   step 11's tree already had 37 single-type JDK imports in it and this rung's
#   scanner counts them wherever it finds them. A rehearsal that asserted four
#   zeroes would fail on a correct tree.
#
#   THE NEAR MISSES ARE A SKIPPED IMPORT AND A CARRIER THAT STAYED. Mod.java
#   imports org.testng.annotations.Test and annotates a method with it;
#   org.testng is on the classpath rather than in a module the JDK ships, so
#   Jdk.packageOf() hands back null and imports() skips the declaration before
#   any retention kind is decided -- the one near miss in this file asserted by
#   the ABSENCE of a row rather than by a row with the wrong field. That is the
#   seed's own shape: C-3's population is 495 rows out of the several thousand
#   import declarations 171 files carry, and a scanner that counted the rest
#   would score the departure half against a denominator no migration can move.
#   The second is SAVED, the ThreadLocal that could not convert, counted rather
#   than converted; and the third is step 08's `smoke.ServiceLoader`, which is
#   why providerTypes() writes `java.util.ServiceLoader` out in full -- a type
#   in the current package beats a module import the way it beats an on-demand
#   one, silently, against the wrong type.
#
#   AND THE PERTURBATION CHECK IS THIS RUNG'S OWN. authoring/perturb-src12.sh
#   makes fourteen single-edit breakages of the staged tree and requires each to
#   come back red on the named assertion; four of them are new and cover this
#   rung's four new blocks, one apiece. TWO OF THE TEN INHERITED WERE STALE
#   BEFORE THE SUITE WAS RUN, both for reasons step 11 wrote down about step
#   06's copy and then reproduced. P1 named `var.candidates (cut11)`, which this
#   rung reaches AFTER the step-12 cut's movement table asserts the same counter
#   -- and the rehearsal exits on its first failure, so the expectation named a
#   line that no longer runs. P4 sed'd for `^ok22=$(srcgate 22 22)`, the
#   spelling step 11 anchored at column 0 and this rung retired, so the edit
#   would have landed nowhere and the tree gone unperturbed. Both are re-pointed,
#   and this rung's anchor, `^ok25=$(srcgate 25 25)`, was checked at column 0 in
#   test.sh before it was written. THE ANCHOR IS PER-RUNG; that is the third
#   rung in a row at which it has had to be re-pointed and the second at which
#   nobody was told by a failure.
#
#   THE MODIMPORT EDIT TRIPS THE REFUSAL AND NOT A COUNTER, which is the one
#   perturbation on this rung worth reading twice. With `import module
#   java.base` replaced by the three single-type imports it was standing in for,
#   the tree compiles at -source 24 and `ok24=no` fails three hundred lines
#   before modimport.size is read. That turns the paragraph above -- four of the
#   five legs move no level, so the refusal is C-2's alone -- from prose into an
#   assertion, and a tree whose refusal came from anywhere else would go red in
#   the wrong place.
#
#   What this layer proves about the four new blocks is the same bounded thing
#   it proved about step 11's three: modimport.size, jdkimport.size,
#   scopedvalue.size and threadlocal.size are each separately load-bearing,
#   because one edit moves one of them and the rehearsal names which. The other
#   frozen figures in those blocks -- the retention-kind split, the (file, q)
#   pair count, the decls, the field sets, the individual record assertions --
#   are still what they were, measured on a real compile and frozen, not
#   separately perturbed.
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
    test "$(grep -cE '^ *ok(25|24|def)=\$\(srcgate ' /tests/test.sh)" = 3; \
    # ...and all three by their exact bytes, because the tag and the level are \
    # separate arguments to srcgate(): a probe TAGGED 24 but compiled at 25 \
    # writes its report into the file the scorer then reads as the Java 24 \
    # answer, and every count in family C would be an answer to a different \
    # question. \
    # \
    # This rung's level is 25, and THE CONTROL IS EXPECTED TO ANSWER `no` AGAIN \
    # -- the fourth rung in a row, after eight where the expected answer was \
    # yes. At every rung from 04 to 08 the control compile was a CONTROL whose \
    # expected answer was yes, because the must-flip was a library, a namespace \
    # or a manifest attribute and no language construct was load-bearing. Step \
    # 09 was the first to flip it, on `sealed`; step 10 flipped it on the \
    # pattern switch; step 11 on the unnamed catch parameter. Here it flips on \
    # ONE leg of five, and the leg is C-2: the MODULE IMPORT DECLARATION, JEP \
    # 511, preview in 22, 23 and 24 and standard in 25. A tree that has done \
    # C-2's work cannot compile at -source 24 without --enable-preview, so the \
    # control reads `no`. \
    # \
    # THE OTHER FOUR LEGS WOULD ALL LEAVE IT AT YES ON THEIR OWN, and at this \
    # rung that is as clear-cut as it has ever been, because only one of the \
    # five is language at all. C-4 rebinds a ThreadLocal to a `ScopedValue`, \
    # which is a class in java.lang and moves no -source level. C-5 turns on \
    # `-XX:+UseCompactObjectHeaders`, which is an argument to a JVM that javac \
    # never sees. C-1 is the class-file major and minor, a property of the \
    # emitted artifact rather than of what javac will parse. And C-3's holds are \
    # ordinary import declarations that have been legal since Java 1 -- the leg \
    # asks for imports to be REMOVED, and removing a declaration lowers no \
    # level. Everything step 11 put in is standard at 24 as well: the unnamed \
    # catch parameters went standard at 22, and `privateLookupIn` and \
    # `unreflectVarHandle` are APIs that have compiled since 9. So `no` here is \
    # a statement about the module imports specifically and not about the rung \
    # in general. \
    # \
    # IT IS NOT THE PREVIEW CHECK AND MUST NOT BE READ AS ONE, which is the one \
    # way this control could mislead a reviewer at this rung and at no rung \
    # below. The scorer runs the compile WITHOUT --enable-preview, so a tree \
    # that reached the module imports by turning the flag on in its own build \
    # reads `no` here in exactly the same bytes as a tree that did the work \
    # properly. What separates those two is C-1's MINOR, read off the class-file \
    # headers the candidate's own build emitted -- 0 against 65535 -- and \
    # nothing in this compile. That is why level.txt carries a `minor=` at this \
    # rung and carried none at any rung below, and why the third of C-1's three \
    # readings is `preview.classes`. \
    # \
    # AND AT THIS RUNG THE MEASUREMENT LEVEL IS LOAD-BEARING FOR THE BASELINE \
    # TOO, which is new and is the reason -source 25 is asserted here rather \
    # than left to source-gate.sh. `import module` parses at no release below \
    # 25, so a report taken at 22 would carry `modimport.size=0` -- not because \
    # the tree has none but because the compiler refused to produce the figure. \
    # baseline/source.txt and every arrival count in baseline/target.txt were \
    # taken at -source 25 by mk-baseline-step12.sh for that reason, and this \
    # rung's two largest legs, C-2 at 0.38 and C-3 at 0.33, are scored against \
    # those denominators. A measurement level that drifted here would not read \
    # low; it would read zero over zero. \
    # \
    # The level assertion this rung DOES make is C-1's, read out of a published \
    # class-file header via ${WORK}/scan.txt, and B-4's beside it reading the \
    # major this rung HOLDS at -- 69 and 66, staged side by side in level.txt as \
    # `major=`/`minor=` and `was=`. The third probe is the compiler's own \
    # default, which is where test.sh falls back when ok25 reads no. \
    # \
    # fixtures/src-rehearsal.sh lifts SRCCP and srcgate() out of this same file \
    # by anchor and drives these same three calls, and it asserts these same \
    # three spellings before it asserts any number -- so a rename here fails the \
    # rehearsal on the rename rather than three hundred lines later on an \
    # unexplained zero. It also bans the seven retired calls by name, `srcgate \
    # 22 22` and `srcgate 21 21` among them, which is the same sweep the loop \
    # below makes from this side. \
    grep -qxF 'ok25=$(srcgate 25 25)' /tests/test.sh; \
    grep -qxF '   ok24=$(srcgate 24 24)' /tests/test.sh; \
    # ...and every level this ladder has probed below 24 asserted GONE, because \
    # a leftover `ok22=$(srcgate 22 22)` would compile the whole tree a fourth \
    # time, cost a minute of the verifier's budget and publish a fact no scorer \
    # reads. `gate.src.ok22` and `gate.src.ok21` go with them: a published \
    # control nobody reads is indistinguishable from a gate that was silently \
    # dropped. \
    for gonecall in 'srcgate 8 8' 'srcgate 11 11' 'srcgate 16 16' \
                    'srcgate 17 17' 'srcgate 20 20' 'srcgate 21 21' \
                    'srcgate 22 22' 'gate.src.ok21' 'gate.src.ok22'; do \
        if grep -qF "${gonecall}" /tests/test.sh; then \
            echo "a retired source probe is still in test.sh: ${gonecall}" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qxF '   okdef=$(srcgate def "")' /tests/test.sh; \
    # The frozen baseline files, each read from /tests and not from the tree. \
    # bv() and tv() name their paths as literals for exactly that reason. \
    # \
    # Fifty-six stage and fifty-two are read. The four that are not are \
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
    #                    the 14 legacy records inside target.b14.c5.file and \
    #                    the 3 java.net.http types that must arrive there -- so \
    #                    it filters the candidate's OWN records to that path \
    #                    and never needs the recorded list \
    # \
    # THE TWO FILES THIS RUNG ADDED ARE BOTH READ, and the reason they have to \
    # be is the reverse of the reason step 11's four did. suppress.txt carries \
    # 26 census rows -- 44 sites over 26 files, 48 (file, value) pairs between \
    # them -- and 4 allow rows, and C-1 takes its budget, its extras and its \
    # unused count off that file in three awk passes. rawdecl.txt carries 8 \
    # rows bucketing 11 declarations, 7 open and 4 forced over 6 buckets in 5 \
    # files, and C-3's departure half is capwant() over the open rows while its \
    # hold half is capwant() over the forced ones. target.txt carries a scalar \
    # for every one of those five figures -- target.su.budget, \
    # target.su.extras, target.su.value.unused, target.rd.open, \
    # target.rd.forced -- and the scorer reads not one of them. That is the \
    # point rather than an oversight: a budget of four is satisfied by a tree \
    # that suppressed four FIXABLE warnings and fixed none of the forced ones, \
    # and the allow rows are matched on the (file, value) PAIR, which is not a \
    # number that a scalar could have stood in for. The scalars stage anyway, \
    # and mk-baseline-step13.sh asserts each of them against the file it \
    # summarises, so a file edited on one side and not the other fails the \
    # build instead of quietly scoring the wrong tree. \
    # \
    # Both are asserted read, by path, below -- and so are the twenty this rung \
    # inherited, because dropping one of those costs an inherited family in \
    # exactly the same way: B-19 reads all three of step 12's, B-18 all four of \
    # step 11's, B-17 three, B-16 six, B-15 all three of step 08's, and B-6 \
    # reads apinorm.txt. A rung that stopped reading sealed.txt or modnames.txt \
    # takes the leg whole. \
    # \
    # The last two stage anyway, and that is deliberate rather than an \
    # oversight. varbuckets.txt enumerates all 81 redundant declarations with \
    # the one exemption marked; http.txt enumerates the 14 records that must \
    # move beside the 3 in another file that must not, which is the distinction \
    # the whole gate turns on. The baseline-consistency layer above checks both \
    # against this tree -- the 81 against its single surviving candidate, the \
    # 17 against its own http records -- and they are the files a human reads \
    # instruction.md's counts against. So their absence from the scorer is \
    # asserted, and a comment explaining each absence is asserted present: an \
    # unread file has to be unread ON PURPOSE, and the purpose has to be \
    # written down next to it. \
    test "$(ls /tests/baseline | grep -c .)" = 56; \
    for unread in streamfiles junit5 varbuckets http; do \
        if grep -v '^[[:space:]]*#' /tests/test.sh | grep -qF "/tests/baseline/${unread}.txt"; then \
            echo "baseline/${unread}.txt is staged as deliberately unread and test.sh now reads it" >&2; \
            exit 1; \
        fi; \
    done; \
    grep -qF 'baseline/varbuckets.txt' /tests/test.sh; \
    grep -qF 'baseline/http.txt' /tests/test.sh; \
    # ...and this rung's two plus the twenty it inherited, asserted read from \
    # CODE and not from a comment, which is the opposite test from the four \
    # above and is why the grep is piped through the same comment strip. \
    for readf in suppress rawdecl \
                 modimport jdkimport scopedvalue \
                 priv catchparam varhandle vhids \
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
        test -r "/tests/assets/rec/jdk25-step13/$(echo "${id}" | tr / .).txt" || \
            { echo "baseline/httpids.txt names ${id}, which has no recording" >&2; exit 1; }; \
    done; \
    grep -l 'log\[0\]=request:' /tests/assets/rec/jdk25-step13/*.txt > /tmp/withreq.txt; \
    test "$(grep -c . /tmp/withreq.txt)" = 15; \
    while read -r f; do \
        id=$(basename "${f}" .txt | sed 's/\./\//'); \
        grep -qxF "${id}" /tests/baseline/httpids.txt || \
            { echo "${id} records an outgoing request and is not in baseline/httpids.txt" >&2; exit 1; }; \
    done < /tmp/withreq.txt; \
    rm -f /tmp/withreq.txt; \
    grep -qF 'sed -n '"'"'s/^major=//p'"'"' /tests/baseline/level.txt' /tests/test.sh; \
    # B-13's six frozen set files, each named by the gate that divides by it, \
    # and B-14's four, the same way -- the fourth of those is http.txt, unread \
    # for the reason given above. Every one of these legs could be rewritten to \
    # divide by the matching target.txt scalar instead: b13_legacy, b13_guards \
    # and b13_builds are already in scope at the point of use, and \
    # target.b14.c2.buckets and target.b14.c3.buckets sit in target.txt one \
    # tv() call away. The result would still be a number in [0,1] that moves in \
    # the right direction. It would also be a COUNT, and a count is what a tree \
    # that migrated six OTHER methods satisfies. The set is the whole difference \
    # between the two readings, so the read of it is asserted and not assumed. \
    # \
    # B-13.4 is the one leg whose want-set is derived in place rather than by a \
    # helper, because boxedctor.txt is the only one of these files whose records \
    # carry twelve sites in six buckets and the gate needs both numbers; its \
    # derivation is asserted by the filter that performs it. B-13.7 has no set \
    # file at all, and B-14.5 has one it does not read -- see the two notes \
    # above. \
    grep -qF 'setwant /tests/baseline/newinstance.txt legacy' /tests/test.sh; \
    grep -qF 'setwant /tests/baseline/accessible.txt guard' /tests/test.sh; \
    # ...and accessible.txt is asked TWICE, which no rung below this one did. \
    # Its two halves are two populations now: departure over the 12 `guard` \
    # buckets step 06 froze, arrival over the 10 `setacc` calls that outlive \
    # B-18.4. A block copied forward from step 10 asks both halves over the \
    # guard list, and since B-18.4 converts three guard buckets to \
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
    # B-14's four. nsmove and nshold are read through nswant() rather than \
    # wantbuckets() because their key is pkg|bucket and not bucket: one method \
    # can reference two different javax packages, and a bucket-only key would \
    # let a tree that moved javax.ejb out of a method still holding \
    # javax.annotation count as having moved both -- and would then count the \
    # same bucket as held for B-14.3, which is the contradiction the pkg in the \
    # key makes unreachable. \
    grep -qF 'nswant /tests/baseline/nsmove.txt' /tests/test.sh; \
    grep -qF 'nswant /tests/baseline/nshold.txt' /tests/test.sh; \
    grep -qF 'wantbuckets /tests/baseline/toarray.txt' /tests/test.sh; \
    # ...and the record filters those sets are compared against, asserted by the \
    # bytes of the filter. B-14.2 reads the javax side and the jakarta side with \
    # two SEPARATE nsbuckets calls over complementary conditions, because one \
    # call over "any annotation package" cannot tell a move from a tree that \
    # never held the import. B-14.3's condition is the complement of B-14.2's \
    # with jakarta excluded, so a bucket B-14.2 has moved cannot also be counted \
    # as held -- drop that arm and a finished migration scores B-14.3 on the \
    # very buckets it was paid for emptying. \
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
    # asks for one major at or above 66 across everything the build compiled, \
    # which subsumes step 07's 55, and asks it of every jar rather than one. \
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
    # EVERY FACT NAME BANNED HERE IS A LEAF, WITH NO PREFIX AMONG THEM AT ALL, \
    # and that absence is the whole story of what this rung did to the rung \
    # below. Step 08 had a family C of three legs and could ban `fact "C4.` and \
    # `fact "C5.` whole; step 09 had five and could ban neither; steps 10 and 11 \
    # had four and could ban that same pair again; step 12 had five and could \
    # ban neither. THIS RUNG HAS FIVE TOO -- C-1 through C-5, two of them \
    # multipliers rather than weighted legs, which changes how they score and \
    # not how they are named -- so the pair stays out and every dead spelling is \
    # enumerated one by one. That makes two consecutive rungs with no prefix ban \
    # among the fact names, the first such pair on this ladder. A prefix ban is \
    # the stronger check and is still used wherever a prefix is genuinely empty \
    # here, which among the fact names is now nowhere; the four `tgt_c2_`, \
    # `tgt_c3_`, `tgt_c4_` and `tgt_c5_` variable stems below are prefix bans of \
    # a different kind, over shell names rather than fact names, and they are \
    # unaffected by any of this. \
    # \
    # SIX RUNGS' WORTH OF DEAD LEAVES, not five, because this rung inherits step \
    # 07's family C as B-14, step 08's as B-15, step 09's as B-16, step 10's as \
    # B-17, step 11's as B-18 and step 12's as B-19, and all six sets were \
    # spelled as family C when they were written. Step 07's five are `C1.major`, \
    # `C2.jakarta`, `C3.javax-held`, `C4.toarray`, `C5.httpclient`; step 08's \
    # three are `C1.modules-named`, `C2.module-names`, `C3.platform-lookup`; \
    # step 09's four are `C2.pattern-instanceof`, `C3.text-blocks`, \
    # `C4.sealed-hierarchies` and `C5.arrow-switch`; step 10's three are \
    # `C2.type-patterns`, `C3.pattern-switch` and `C4.sequenced-access`; step \
    # 11's three are `C2.doprivileged-removal`, `C3.unnamed-catch` and \
    # `C4.varhandle`; step 12's are `C2.module-imports`, `C3.import-subsumption`, \
    # `C4.scoped-value` and `C5.compact-headers`, plus a fifth with no successor \
    # that the next paragraph is entirely about. Each of the named ones has a \
    # live successor under a B14, B15, B16, B17, B18 or B19 prefix, which is \
    # exactly what makes the old spelling survivable -- it would read the right \
    # number under the wrong question -- and therefore worth banning. Step 12's \
    # four are the new entries, and they are the ones to read twice: \
    # `C2.module-imports` is banned as a family-C leaf in the first loop of the \
    # sweep below and required as `B19.2.module-imports` in the last, which is \
    # what a rung promoting the rung below's migration into its regression floor \
    # looks like from inside a single sweep. \
    # \
    # `C1.level-` DIES HERE, ONCE, FOR FOUR RUNGS AT A STROKE, and it is the \
    # entry that most repays reading slowly. Steps 09, 10, 11 and 12 all wrote \
    # `fact "C1.level-${WANT_MAJOR}"` -- the same bytes in all four files, the \
    # level itself arriving from level.txt rather than from the scorer -- and \
    # every rung until now republished those bytes, so there was never a \
    # spelling to retire. The rung below called that the eleventh trap paid off \
    # in advance: a leg whose name carries its constant as a parameter needs no \
    # sweep when the constant moves. It needs one when the LEG moves. This \
    # rung's class-file level is C-4, not C-1, and it is published as \
    # `C4.level-held` with no parameter in it at all, because the level is now \
    # half of a multiplier and the other half is an inventory count that has no \
    # major version to name. So one ban entry, `fact "C1.level-`, retires four \
    # rungs' leaf facts together, and nothing under a B prefix brings any of \
    # them back: B-16, B-17, B-18 and B-19 all inherit their rung's family C \
    # MINUS its first leg, which is what the four `B1n.1.` absence checks near \
    # the end of this file are for. \
    # \
    # `gate.c1.at${WANT_MAJOR}` BECOMES BANNABLE AT THE RUNG AFTER THE ONE THAT \
    # COULD NOT BAN IT, which is the same trap seen from the far side. Step 11 \
    # published `gate.c1.at${WANT_MAJOR}` and step 12 published \
    # `gate.c1.at${WANT_MAJOR}.${WANT_MINOR}`: the older name was a proper PREFIX \
    # of the newer one, so the rung below could not ban it without failing on its \
    # own correct file, and it recorded the name as dead and unbannable in the \
    # same breath. Here the whole counter moved to C-4 and is published as \
    # `gate.c4.at${WANT_MAJOR}.${WANT_MINOR}`, so `fact "gate.c1.at${WANT_MAJOR}` \
    # matches nothing live and retires BOTH earlier spellings in one entry -- the \
    # prefix relation that made it unbannable below is what makes one entry \
    # enough here. Its live counterpart is asserted present, whole, in the \
    # measurement roll-call further down, where it has to be written \
    # single-quoted because the roll-call's own loop would otherwise expand it. \
    # \
    # `gate.c1.classes` IS BANNED FOR THE FIRST TIME and the rung below named it \
    # explicitly as a leaf it could not ban, on the grounds that it had meant the \
    # same thing at every rung since step 09. It still means that thing. What \
    # changed is which leg asks: the class-file census is C-4's inventory half \
    # here and publishes `gate.c4.classes`, so the counter survives verbatim one \
    # prefix over while its old spelling goes dead. That is the third name in \
    # this block to move legs rather than change meaning, and all three moved in \
    # the same direction, out of C-1 and into C-4, because this rung's C-1 asks \
    # about suppression instead of about levels. \
    # \
    # `fact "gate.c1.`, `fact "gate.c2.`, `fact "gate.c3.`, `fact "gate.c4.` and \
    # `fact "gate.c5.` are NOT banned at any depth, because all five are live \
    # prefixes here. `gate.c5.` in particular stays unbannable for a second \
    # consecutive rung -- steps 08, 10 and 11 could ban it whole, step 12 could \
    # not, and this rung's C-5 publishes six leaves under it. The dead gate \
    # spellings under all five prefixes are steps 08 to 12's LEAVES, and only the \
    # leaves this rung does not itself republish. \
    # \
    # C-2'S WHOLE LEAF VOCABULARY TURNED OVER AGAIN and not one name survived, \
    # which is why step 12's c2 spellings are in the list wholesale. Step 12's \
    # C-2 was a set equality and a cleanliness over module imports -- `files`, \
    # `files.want`, `files.was`, `imports`, `imports.want`, `clean`, \
    # `clean.want`, `setfrac`, `cleanfrac`, `spurious`. This rung's C-2 is a \
    # warning count against a fixable population -- `fixable`, `lint`, \
    # `warnings`, `warnfrac` -- and there is no set and no cleanliness in it, so \
    # there is nothing for the old vocabulary to collide with. Nine of the ten \
    # are covered by five prefix-shortened entries below, because `clean` \
    # subsumes `clean.want`, `files` subsumes `files.want` and `files.was`, and \
    # `imports` subsumes `imports.want`; `cleanfrac` is not a child of `clean` \
    # under the dot rule and is listed on its own. \
    # \
    # SIX LEAVES ARE DELIBERATELY ABSENT from the list and every one of the six \
    # is under `gate.c3.`: `departed`, `departed.want`, `depfrac`, `held`, \
    # `held.want` and `holdfrac`, which this rung's C-3 republishes name for name \
    # even though it counts raw type declarations where step 12 counted import \
    # rows. A ban on any of the six would fail on a correct file. That is also \
    # why four of step 12's c3 leaves ARE listed at full depth rather than \
    # shortened -- `departed.excused` cannot be shortened to `departed`, and \
    # `held.ambiguous`, `held.excused`, `held.nested` and `held.static` cannot be \
    # shortened to `held`, because both stems are live. `total` shortens \
    # normally and takes `total.was` with it. A leg keeping its counter names \
    # while changing what it counts is the normal case on this ladder, not the \
    # exception, and it is the whole reason this sweep is over leaves. \
    # \
    # TWO ENTRIES FROM THE RUNG BELOW ARE GONE AND THAT IS NOT A WEAKENING. Step \
    # 12 banned `fact "gate.c4.arrived.want` and `fact "gate.c4.departed.want` at \
    # full depth, because its own C-4 published `gate.c4.arrived` and \
    # `gate.c4.departed` and the shorter forms would have failed. This rung's C-4 \
    # publishes neither, so the two shorter entries go in and the two longer ones \
    # come out: `fact "gate.c4.arrived` matches everything \
    # `fact "gate.c4.arrived.want` matched and more. The list is longer by \
    # thirty-two entries net, not by thirty-four. \
    # \
    # AND ONE FAMILY MOVED THE OTHER WAY AT THE RUNG BELOW AND STAYS PUT HERE. \
    # Step 11's `gate.c4.exec`, `gate.c4.exec.want` and `gate.c4.exec.broken` \
    # went into the ban list at step 12 and came back as `gate.b18.4.exec` and \
    # its two children; this rung's C-4 has no execution term either -- a class \
    # file either carries the major version or it does not, and no test run \
    # changes that -- so the three stay banned and stay live under b18.4, \
    # untouched. `gate.c4.handles` and `gate.c4.setaccessible` travel with them. \
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
                'fact "C4.sealed-hierarchies' 'fact "C5.arrow-switch' \
                'fact "C5.httpclient' \
                'fact "C2.type-patterns' 'fact "C3.pattern-switch' \
                'fact "C4.sequenced-access' \
                'fact "C2.doprivileged-removal' 'fact "C3.unnamed-catch' \
                'fact "C4.varhandle' \
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
                'fact "gate.c4.firstreads' 'fact "gate.c4.sequenced' \
                'fact "gate.c2.departed' 'fact "gate.c2.depfrac' \
                'fact "gate.c2.held' 'fact "gate.c2.holdfrac' \
                'fact "gate.c2.sites' \
                'fact "gate.c3.arrfrac' 'fact "gate.c3.arrived' \
                'fact "gate.c3.read' 'fact "gate.c3.unnamed' \
                'fact "gate.c3.unread' \
                'fact "gate.c4.exec' 'fact "gate.c4.handles' \
                'fact "gate.c4.setaccessible' \
                'fact "gate.c5.arrow' 'fact "gate.c5.colon' \
                'fact "gate.c5.expr' 'fact "gate.c5.files' \
                'fact "gate.c5.size' \
                'fact "gate.c5.cdistub' 'fact "gate.c5.exec' \
                'fact "gate.c5.legacy' 'fact "gate.c5.redirects' \
                'fact "gate.c5.types' \
                'fact "C1.level-' 'fact "C2.module-imports' \
                'fact "C3.import-subsumption' 'fact "C4.scoped-value' \
                'fact "C5.compact-headers' 'fact "gate.c1.at${WANT_MAJOR}' \
                'fact "gate.c1.classes' 'fact "gate.c1.preview' \
                'fact "gate.c2.clean' 'fact "gate.c2.cleanfrac' \
                'fact "gate.c2.files' 'fact "gate.c2.imports' \
                'fact "gate.c2.setfrac' 'fact "gate.c2.spurious' \
                'fact "gate.c3.departed.excused' \
                'fact "gate.c3.held.ambiguous' 'fact "gate.c3.held.excused' \
                'fact "gate.c3.held.nested' 'fact "gate.c3.held.static' \
                'fact "gate.c3.total' 'fact "gate.c4.arrfrac' \
                'fact "gate.c4.arrived' 'fact "gate.c4.convert' \
                'fact "gate.c4.departed' 'fact "gate.c4.depfrac' \
                'fact "gate.c4.held' 'fact "gate.c4.holdfrac' \
                'fact "gate.c4.svcalls' 'fact "gate.c4.tldecls' \
                'fact "gate.c5.flag' 'fact "gate.c5.forks' \
                'fact "gate.c5.probe' 'fact "gate.c5.projects' \
                'fact "gate.c5.tasks'; do \
        if grep -qF "${dead}" /tmp/ts-code.txt; then \
            echo "test.sh still carries an inherited family-C spelling: ${dead}" >&2; \
            grep -nF "${dead}" /tmp/ts-code.txt | head -3 >&2; exit 1; \
        fi; \
    done; \
    # ...and the five leaves this rung's own family C does publish, asserted \
    # present in the same sweep's input, so the bans above cannot be satisfied by \
    # a file that publishes no family-C gate names at all. ALL FIVE ARE MATCHED \
    # WITH THEIR `=`, which is new: every rung since step 09 had to match its \
    # C-1 loosely, as `C1.level-`, because the level arrived from level.txt and \
    # the name carried it. This rung's level leg is C-4 and is called \
    # `C4.level-held`, a literal with no parameter in it, so the loose match \
    # retires with the spelling. B-4's name is still matched as a prefix in the \
    # roll-call below, and it is now the only such match left in this file. \
    for live in 'fact "C1.suppression-budget=' 'fact "C2.warning-free=' \
                'fact "C3.raw-declarations=' 'fact "C4.level-held=' \
                'fact "C5.behaviour-held='; do \
        if ! grep -qF "${live}" /tmp/ts-code.txt; then \
            echo "this rung's family C does not publish ${live}" >&2; exit 1; \
        fi; \
    done; \
    # ...and all SIX inherited families under their new prefixes, in the same \
    # sweep's input, so the bans above cannot be satisfied by a file that dropped \
    # the legs instead of renaming them. B-19's four are the new entries, and \
    # they are the ones the ban list above would otherwise punish twice: \
    # `C2.module-imports` is banned as a family-C leaf three lines up and \
    # required here as `B19.2.module-imports`, which is the whole shape of \
    # a rung promoting the rung below's migration into its regression floor. \
    # B-19 contributes FOUR and not five for the reason the `C1.level-` \
    # paragraph gave: the rung below's first leg was its class-file level, this \
    # rung asks that question itself as C-4, and a fifth entry here would be \
    # asking for a name no correct file writes. \
    for live in 'fact "B14.2.jakarta=' 'fact "B14.3.javax-held=' \
                'fact "B14.4.toarray=' 'fact "B14.5.httpclient=' \
                'fact "B15.2.module-names=' 'fact "B15.3.platform-lookup=' \
                'fact "B16.2.pattern-instanceof=' 'fact "B16.3.text-blocks=' \
                'fact "B16.4.sealed-hierarchies=' 'fact "B16.5.arrow-switch=' \
                'fact "B17.2.type-patterns=' 'fact "B17.3.pattern-switch=' \
                'fact "B17.4.sequenced-access=' \
                'fact "B18.2.doprivileged-removal=' 'fact "B18.3.unnamed-catch=' \
                'fact "B18.4.varhandle=' \
                'fact "B19.2.module-imports=' 'fact "B19.3.import-subsumption=' \
                'fact "B19.4.scoped-value=' 'fact "B19.5.compact-headers='; do \
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
    # FAMILY C'S FIVE LEGS ARE UNINDENTED, and not because they escaped a guard: \
    # each one opens its OWN. C-1 reads ${WORK}/scan.txt behind `[ -s \
    # ${WORK}/scan.txt ]`; C-2, C-3 and C-4 each open `[ -n ${measured} ] && \
    # [ -s ${measured} ] && [ -r baseline/<population>.txt ]` over the source \
    # report and the one population file that leg divides by -- modimport.txt, \
    # jdkimport.txt and scopedvalue.txt, in that order. C-5 opens a fifth guard \
    # of a shape no family-C leg on this ladder has had: `[ -n ${c5flag} ] && \
    # [ ${c5want:-0} -gt 0 ]`, over a flag read out of target.txt and a \
    # population counted out of modnames.txt, reading neither the class-file \
    # scan nor the source report. Five legs, five guards, five banners at column \
    # zero -- where B-14's four legs share ONE guard and are indented three \
    # inside it. A leg whose population file failed to stage reads zero on its \
    # own and does not take the other four with it, which is what five guards \
    # buy and one would not. \
    # \
    # This is the inversion worth stating, because it is the opposite of step \
    # 08: there family C read the published jars and every leg was jar-shaped, \
    # which is why that rung could leave all of them outside a source guard. \
    # Here three of the five count RECORDS the gate compiler wrote, so each had \
    # to acquire a guard of its own; C-1 -- one of the two legs that are not \
    # source-shaped -- reads the same class-file scan B-4 reads; and C-5 reads \
    # no artefact of the scored build at all, forking its own bounded Gradle \
    # invocation afterwards and counting the rows that come back. Counted rather \
    # than listed, for the reason indentation is always counted here: a leg that \
    # drifts into or out of the guard changes its indentation before it changes \
    # anything else, and nothing else about it changes at all. \
    test "$(grep -cE '^   # -+ B-14\.[2-5]$' /tests/test.sh)" = 4; \
    test "$(grep -cE '^# -+ B-15\.[23]$' /tests/test.sh)" = 2; \
    test "$(grep -cE '^# -+ B-16\.[2-5]$' /tests/test.sh)" = 4; \
    test "$(grep -cE '^# -+ C-[1-5]$' /tests/test.sh)" = 5; \
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
    # FAMILY C'S EXPRESSION IS THE ONE SHAPE ON THIS LADDER THAT IS NOT A \
    # WEIGHTED SUM, and it is pinned by its bytes for that reason before any \
    # other. Three legs carry weights -- 0.15, 0.57, 0.28, summing to one -- and \
    # two do not: C-4 and C-5 MULTIPLY that sum. A rung that gave either \
    # multiplier a weight instead would be adding 0.70 x 0.75 x w straight to a \
    # floor with 0.025319 of room, and both read 1.000000 on the INPUT tree, so \
    # the addition would be to a term the candidate is handed. \
    # \
    # THE EXPRESSION CARRIES NO SPACE AROUND EITHER MULTIPLIER, and that is a \
    # requirement rather than a style: the family loop further down anchors on \
    # `0\.28\*c\)\*d\*e$`, and its anchors sit in a space-separated `for` \
    # list where a literal space would split one entry into two. \
    grep -qF '(0.15*a + 0.57*b + 0.28*c)*d*e' /tests/test.sh; \
    grep -qF '(0.20*a + 0.26*b + 0.36*c + 0.08*d) / 0.90' /tests/test.sh; \
    grep -qF '(0.17*a + 0.65*b + 0.14*c) / 0.96' /tests/test.sh; \
    grep -qF '(0.46*a + 0.26*b + 0.24*c) / 0.96' /tests/test.sh; \
    grep -qF '(0.38*b + 0.33*c + 0.25*d + 0.02*e) / 0.98' /tests/test.sh; \
    grep -qF '(0.35*b + 0.45*c) / 0.80' /tests/test.sh; \
    # B-16'S, B-17'S, B-18'S AND B-19'S OWN COMPOSITES, which are step 09's \
    # family C, step 10's, step 11's and step 12's, each with ITS C-1 removed \
    # and the remainder renormalised -- the fourth, fifth, sixth and seventh \
    # times this ladder has performed that subtraction, and the second, third, \
    # fourth and fifth times the dropped leg WAS a class-file major. Step 09's \
    # C-1 asked what fraction of the tree had reached major 61, step 10's asked \
    # the same of 65, step 11's of 66 and step 12's of 69-at-minor-0. This rung \
    # asks it of 69.0 as well, out of the same scan -- but under C-4 and under a \
    # NEW leaf-fact spelling, `C4.level-held`, because a leg that no longer \
    # carries its constant in its name has no reason to keep a parameter there. \
    # `C1.level-${WANT_MAJOR}`, unchanged from step 09 through step 12, is dead \
    # at this rung and banned in the sweep above. A tree at 69 is a tree past \
    # 66, 65 and 61 by arithmetic, so carrying any of those four legs forward \
    # would charge four times over for one reading, and would charge it on a \
    # reading no candidate can fail independently of the one it is already being \
    # charged for. All four go, and each leaves its divisor behind: \
    # 0.20 + 0.26 + 0.36 + 0.08 = 0.90 is B-16's, 0.17 + 0.65 + 0.14 = 0.96 is \
    # B-17's, 0.46 + 0.26 + 0.24 = 0.96 is B-18's, and \
    # 0.38 + 0.33 + 0.25 + 0.02 = 0.98 is B-19's. \
    # \
    # B-19'S DIVISOR IS THE LARGEST OF THE FOUR AND THAT IS THE MEASUREMENT, \
    # not a rounding. Step 12's C-1 carried 0.02 of its family, the smallest \
    # weight any C-1 has ever had on this ladder, because by then the level was \
    # a consequence of the language work rather than a thing to be done -- a \
    # tree that used module imports and compact source files at all had already \
    # reached 69. Dropping a 0.02 leg changes the remaining four by two percent \
    # of themselves. Dropping step 08's C-1, at the other end, left a SQUARE \
    # behind. \
    # \
    # WHAT DOES NOT SURVIVE MATTERS AS MUCH AS WHAT DOES. Not one of the four \
    # dropped C-1s carried a scaling into another leg -- all four were plain \
    # weighted terms, unlike step 08's, which multiplied its own C-2. B-16 is \
    # four legs and a divisor, B-17 is three legs and a divisor, B-18 is three \
    # legs and a divisor, B-19 is four legs and a divisor, and a rung that \
    # squared any of them would be importing step 08's shape into four families \
    # that never had it. \
    # \
    # TWO SCALINGS DO SURVIVE AND NEITHER IS A SQUARE, which is one more than \
    # the rung below had to say. Step 10's C-3 multiplied its source fraction by \
    # the fraction of its five recorded dispatches that still matched, and \
    # B-17.3 keeps that factor; step 11's C-4 multiplied its own by the fraction \
    # of its recorded access failures that still matched, and B-18.4 keeps that \
    # one. Both sets of recordings are still in this rung's era -- \
    # `rec/jdk25-step13`, which moves one transcript and neither of theirs -- so \
    # both factors still have something to read, and a pattern switch or a \
    # VarHandle that stopped answering would otherwise cost nothing at all. \
    # \
    # ...AND FOUR NEGATIVES, one per rung whose family C became a family-B \
    # gate. Each banned spelling is a LIVE numerator with its own C-1 term still \
    # in front of it, which is the precise residue of the edit these guard \
    # against: a file that built B-16, B-17, B-18 or B-19 by copying the old \
    # family-C line and dividing it would satisfy every positive above and still \
    # carry the line it was copied from. Step 12's is the new one, and it is the \
    # only one of the four whose C-1 coefficient is small enough to be mistaken \
    # for a typo: `0.02*a` at the front of a line that is otherwise B-19's \
    # numerator exactly. \
    if grep -qF '0.10*a + 0.20*b + 0.26*c + 0.36*d + 0.08*e' /tests/test.sh; then \
        echo "step 09's five-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
    if grep -qF '0.04*a + 0.17*b + 0.65*c + 0.14*d' /tests/test.sh; then \
        echo "step 10's four-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
    if grep -qF '0.04*a + 0.46*b + 0.26*c + 0.24*d' /tests/test.sh; then \
        echo "step 11's four-leg family C is still spelled as a family C" >&2; \
        exit 1; \
    fi; \
    if grep -qF '0.02*a + 0.38*b + 0.33*c + 0.25*d + 0.02*e' /tests/test.sh; then \
        echo "step 12's five-leg family C is still spelled as a family C" >&2; \
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
    # The weights each family DIVIDES BY, summed. test.sh claims two of these \
    # sums in prose -- family B's NINETEEN to 1.000000000000000000 and B-12's \
    # five to 1.000000 -- and a claim in prose that nothing checks is the same \
    # object as no claim. Every divisor in the file is a claim of the same kind \
    # and is checked here for the same reason: `/ 0.90` on B-13's six, B-14's \
    # four and B-16's four, `/ 0.80` on B-15's two, `/ 0.96` on B-17's three and \
    # B-18's three, and now `/ 0.98` on B-19's four, each one asserting what its \
    # numerator adds up to. \
    # \
    # FAMILY C'S ROW IS THREE NUMBERS AT THIS RUNG AND NOT FIVE, which is the \
    # one entry whose SHAPE changed rather than its contents. This family has \
    # five legs and only three weights; C-4 and C-5 multiply rather than add, so \
    # a sum over five would be summing two numbers that are not weights at all. \
    # Listing them would make the row total 3.000000 and the check would have to \
    # be given a want of 3.000000 to pass -- a literal that asserts nothing, on \
    # a family whose real claim is that its three ADDED terms make one whole. \
    # \
    # FAMILY C'S ROW WAS WRONG THREE RUNGS BELOW AND THIS LOOP DID NOT SAY SO, \
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
    # FAMILY B'S ROW IS EIGHTEEN DECIMAL PLACES WIDE AT THIS RUNG and that is \
    # not cosmetic. Step 07's thirteen weights were exact at six; every rung \
    # since has scaled the whole column by 0.88 to make room for a new gate's \
    # 0.12, and six such scalings take 0.046464 to 0.021578071488331776, which \
    # needs eighteen. A rung that rounded them back to sixteen would print \
    # eighteen weights that no longer sum to 0.88 and a family B that no longer \
    # sums to one -- and this loop is the only thing in the tree that would say \
    # so. The two-step check below pins each printed row to the awk term it was \
    # copied from, so a rounded row and a rounded term would still agree with \
    # each other; only the sum catches the pair. \
    # \
    # THIS IS THE LAST SCALING. steps/ ends at 13, so no rung after this one \
    # takes the column to twenty places, and the eighteen here are exact and \
    # final rather than one more approximation on the way somewhere. \
    # \
    # B-19'S DIVISOR IS THE ONE WORTH STATING TWICE at this rung, for the reason \
    # B-18'S was at the rung below, and it is the SMALLEST such slip the ladder \
    # has had to argue about. Step 12's four remaining legs carried 0.98 of step \
    # 12's family C between them, so the renormalisation has to divide by 0.98; \
    # divide by 1.00 instead and a tree that held every one of step 12's legs \
    # scores B-19 at 0.98 and is charged a fiftieth of the gate for work it did \
    # not fail to do. That is 0.02 x 0.12 x 0.175 = 0.00042 of the reward, half \
    # what the rung below computed, because step 12's dropped C-1 was priced at \
    # 0.02 where step 11's was priced at 0.04. \
    # \
    # THE MARGIN IT WOULD EAT INTO IS 0.025319, A THREE-TEN-THOUSANDTH WIDER \
    # THAN THE RUNG BELOW'S 0.025000, and the difference is worth naming rather \
    # than leaving to be inferred from two nearly identical figures. The rung \
    # below floored at 0.475000 with family A whole; this rung floors at \
    # 0.474681 because its era records a method name an untouched tree has not \
    # written yet, so family A reads 938 of 939 there. A slip of 0.00042 is a \
    # sixtieth of the margin, where the rung below's 0.00084 was a thirtieth. \
    # It is in the direction that makes the floor look better than it is, which \
    # is the direction nothing else here would catch, and halving it changes \
    # nothing about that. \
    for sum in "0.021578071488331776 0.010789035744165888 0.043156142976663552 0.043156142976663552 0.035963452480552960 0.043156142976663552 0.021578071488331776 0.035963452480552960 0.021578071488331776 0.039559797728608256 0.043156142976663552 0.049041071564390400 0.055728490414080000 0.063327830016000000 0.071963443200000000 0.081776640000000000 0.092928000000000000 0.105600000000000000 0.120000000000000000:1.000000:family B" \
               "0.15 0.57 0.28:1.000000:family C" \
               "0.30 0.35 0.10 0.10 0.15:1.000000:B-12" \
               "0.12 0.18 0.03 0.26 0.23 0.08:0.900000:B-13's six, before its divisor" \
               "0.07 0.17 0.12 0.54:0.900000:B-14's four, before its divisor" \
               "0.35 0.45:0.800000:B-15's two, before its divisor" \
               "0.20 0.26 0.36 0.08:0.900000:B-16's four, before its divisor" \
               "0.17 0.65 0.14:0.960000:B-17's three, before its divisor" \
               "0.46 0.26 0.24:0.960000:B-18's three, before its divisor" \
               "0.38 0.33 0.25 0.02:0.980000:B-19's four, before its divisor"; do \
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
    # family C; B-14's four, B-16's four, B-17's three, B-18's three and B-19's \
    # four have no printed rows to compare against, so they are pinned here by \
    # the whole expression instead. B-19's entry is the new one, and it is the \
    # only one of the five whose terms are ALSO this rung's own family-C weights \
    # one rung ago -- 0.38, 0.33, 0.25 and 0.02 are step 12's C-2, C-3, C-4 and \
    # C-5 -- so a rung that had left family C alone would satisfy it with step \
    # 12's arithmetic. That is why the sum above states 0.98 for it and 1.000000 \
    # for family C: the two lists cannot both be right unless family C was \
    # rewritten. B-18's entry says the same thing one rung further back, and it \
    # is left in place because a rung that rebuilt family C from step 11's \
    # weights instead of step 12's is the same mistake wearing an older coat. \
    # \
    # AND THE SHAPE IS NOT THE SAME SHAPE, which is what makes the two lists \
    # unmixable here in a way they were not at any rung below. B-19's four are \
    # four addends over a divisor; family C's three are three addends with TWO \
    # MULTIPLIERS after them and no divisor at all. A transcription of step 12's \
    # family C into this rung's would not merely carry wrong numbers, it would \
    # carry the wrong arithmetic, and the `1.000000` above is what says so: \
    # 0.15 + 0.57 + 0.28 sums to one because C-4 and C-5 take no weight out of \
    # the sum, and any four-term family C sums to something else. \
    grep -qF '0.07*b + 0.17*c + 0.12*d + 0.54*e' /tests/test.sh; \
    grep -qF '0.20*a + 0.26*b + 0.36*c + 0.08*d' /tests/test.sh; \
    grep -qF '0.17*a + 0.65*b + 0.14*c' /tests/test.sh; \
    grep -qF '0.46*a + 0.26*b + 0.24*c' /tests/test.sh; \
    grep -qF '0.38*b + 0.33*c + 0.25*d + 0.02*e' /tests/test.sh; \
    # AND THE PUBLISHED FIGURES THAT ARE NOT THEMSELVES WEIGHTS: what it costs \
    # a correct tree to split `fire` at all. The reference step 12 does not: it \
    # binds the ScopedValue around a lambda and leaves the body where step 11 \
    # cut the three buckets. test.sh's header states that a \
    # candidate who extracts a helper out of `fire` reads B-10 0.985915, \
    # B-16 0.968254 and B-18 0.994690 -- three inherited hold-style keys that \
    # move with the site -- that family B falls by 0.003714 from wherever it \
    # stood, to 0.996286 from the untouched tree's 1.000000 and to 0.994886 \
    # from the golden's 0.998600, and that the reward cost is 0.000650 either \
    # way, so a correct tree that moves the body scores 0.999105 where one that \
    # leaves it alone scores 0.999755. Round 2 of the static review called that \
    # "an assertion" and it was not one: the figures were comments, and a \
    # reprice of family B would have left all of them standing while they \
    # quietly stopped being true. So they are recomputed here from the SAME \
    # nineteen weights the loop above pins to the scorer's own expression. The three per-gate values are INPUTS -- they are what the \
    # nick measures, and nothing in this image can derive them -- and what is \
    # checked is the composition and the reward arithmetic the header does with \
    # them, which is the half a reprice of family B would falsify. \
    # \
    # THE THREE PER-GATE VALUES ARE THE RUNG BELOW'S AND THE WEIGHTS ARE NOT, \
    # which is the whole of what moved. B-10, B-16 and B-18 ask exactly the \
    # questions they asked one rung down, against exactly the same population \
    # files, so what a split of `fire` costs each of them is unchanged; what \
    # changed is that all three weights were scaled by 0.88 to make room for \
    # B-19. B-19 ITSELF DOES NOT MOVE, and that is a property of its populations \
    # rather than an assumption: modimport.txt and jdkimport.txt are keyed by \
    # file, scopedvalue.txt by `<file> field=<name>`, and B-19.5 by project. Not \
    # one of the four keys a method, so there is no leg in the new gate for the \
    # split to land on. \
    splitfire=$(awk 'BEGIN { loss = 0.039559797728608256*(1-0.985915) + 0.081776640000000000*(1-0.968254) + 0.105600000000000000*(1-0.994690); ora = 1 - 0.063327830016000000*(1-0.995607) - 0.105600000000000000*(1-0.989379); printf "%.6f %.6f %.6f %.6f %.6f", 1-loss, ora-loss, 0.70*0.25*loss, 0.30+0.70*(0.25*(ora-loss)+0.75), 0.30+0.70*(0.25*ora+0.75) }'); \
    if [ "${splitfire}" != "0.996286 0.994886 0.000650 0.999105 0.999755" ]; then \
        echo "split-fire worst case recomputes to ${splitfire}, want 0.996286 0.994886 0.000650 0.999105 0.999755 -- family B was repriced and test.sh's header was not" >&2; \
        exit 1; \
    fi; \
    grep -qF "to 0.996286 from the untouched" /tests/test.sh; \
    grep -qF "to 0.994886 from the golden's 0.998600" /tests/test.sh; \
    grep -qF '0.70 x 0.25 x 0.003714 = 0.000650' /tests/test.sh; \
    grep -qF 'SCORES 0.999105 WHERE ONE THAT LEAVES IT ALONE SCORES' /tests/test.sh; \
    # The weights the report PRINTS, pinned to the weights the scorer USES. \
    # These are two separate literals thirty lines apart, and when step 03's C-6 \
    # was added the arithmetic was changed and the report was not -- so every run \
    # printed five gates at the old weights while scoring six at the new ones. A \
    # number that is right, described wrongly, is the one defect a reviewer \
    # cannot see. Every weight in this rung's two families is a fresh literal: \
    # family C was re-weighted from scratch because this step's migration has \
    # THREE weighted gates and two MULTIPLIERS, a shape no rung below it had, \
    # and family B grew a NINETEENTH gate, which moved all eighteen others -- \
    # every step-12 weight was scaled by 0.88 to make room for B-19's 0.12, and \
    # the scaling is exact at EIGHTEEN places, which is why these rows print \
    # eighteen decimals where the rung below printed sixteen. \
    # \
    # ALL EIGHTEEN INHERITED ROWS MOVED AT THIS RUNG, not just the ones nearest \
    # the new gate, so the ban list below names every one of them rather than a \
    # selection. ELEVEN of step 12's eighteen spellings are prefixes of a weight \
    # this rung prints -- one more than the rung below had to contend with -- \
    # and choosing seven of the eleven to trust would be choosing which stale \
    # rows to let through. \
    # \
    # EIGHT OF THOSE ELEVEN ARE THE COLUMN SHIFTING DOWN BY ONE, which is the \
    # trap step 11 found at two positions and step 12 at seven, now at eight. \
    # From B-11 up, every gate's weight is 0.12 x 0.88^k for k positions counted \
    # from the top of the column, so the SEQUENCE of numbers does not move when \
    # a gate is added -- only the labels beside it do. Step 12's B-11 through \
    # B-18 carry exactly this rung's B-12 through B-19 weights, two trailing \
    # zeros short, and a stale row is therefore a correct weight under the \
    # PREVIOUS row's label. The other three are 0.0490410715643904 appearing on \
    # step 12's B-3, B-4 and B-6, which is the same number under labels that \
    # have nothing to do with this rung's B-12 at all. \
    # \
    # WHICH IS WHY EVERY GREP HERE MATCHES THE LABEL AND THE WEIGHT TOGETHER and \
    # none of them matches a weight alone. A ban on `x0.1200000000000000` would \
    # fail on a correct file, because this rung's B-19 prints \
    # `x0.120000000000000000` and the old spelling is a prefix of it. A ban on \
    # `    B18 java 22 held` alone would fail too, because that label is live \
    # here under a new weight. The pair is the only thing that is false on a \
    # correct file and true on a stale one. \
    grep -qF "printf '    B10 java 7 held      %s  x0.039559797728608256" /tests/test.sh; \
    grep -qF "printf '    B11 java 8 lang held %s  x0.043156142976663552" /tests/test.sh; \
    grep -qF "printf '    B12 java 8 lib held  %s  x0.049041071564390400" /tests/test.sh; \
    grep -qF "printf '    B13 java 9-10 held   %s  x0.055728490414080000" /tests/test.sh; \
    grep -qF "printf '    B14 java 11 held     %s  x0.063327830016000000  (penalty %s)" /tests/test.sh; \
    grep -qF "printf '    B15 jpms + services  %s  x0.071963443200000000" /tests/test.sh; \
    grep -qF "printf '    B16 java 17 held     %s  x0.081776640000000000" /tests/test.sh; \
    grep -qF "printf '    B17 java 21 held     %s  x0.092928000000000000" /tests/test.sh; \
    grep -qF "printf '    B18 java 22 held     %s  x0.105600000000000000" /tests/test.sh; \
    grep -qF "printf '    B19 java 25 held     %s  x0.120000000000000000" /tests/test.sh; \
    # AND THE TWO FAMILY-C ROWS THAT PRINT NO WEIGHT AT ALL, which is a shape \
    # no rung below this one had and which the family loop further down cannot \
    # see. That loop reads weights out of rows and compares the sequence with \
    # the scorer's coefficients; a multiplier has no coefficient, so C-4 and \
    # C-5 are invisible to it by construction and would be invisible to it just \
    # as thoroughly if they had been deleted. `(multiplier)` in the literal is \
    # what says the row is there and what says it is not a weighted leg, and \
    # the two facts are asserted in the one string. \
    grep -qF "printf '    C1 suppression budget   %s  x0.15  (allowed %s of %s, extra %s of %s, unused %s of %s)" /tests/test.sh; \
    grep -qF "printf '    C2 warning-free         %s  x0.57  (%s of %s fixable warnings left, bound to C1 %s)" /tests/test.sh; \
    grep -qF "printf '    C3 raw declarations     %s  x0.28  (narrowed %s of %s, held %s of %s)" /tests/test.sh; \
    grep -qF "printf '    C4 level and inventory  %s  (multiplier)  (%s of %s class files, %s of %s at %s.%s)" /tests/test.sh; \
    grep -qF "printf '    C5 behaviour held       %s  (multiplier)  (%s of %s ids passed, %s of %s xml files)" /tests/test.sh; \
    # And the step-12 rows asserted GONE, because the fifteen asserted present \
    # above are fifteen of the twenty-four scored rows this report prints, and a \
    # row this rung forgot to rewrite is a row describing the rung below. Two of \
    # the twenty-four carry `(multiplier)` where a weight would be, which is why \
    # that count says SCORED and not WEIGHTED: C-4 and C-5 are scored without \
    # being weighted, and a rung that let itself write `twenty-two` here would be \
    # a rung that had forgotten they exist. \
    # \
    # B-4 STAYS IN THIS LIST, on the ground that let it back in one rung ago. Its \
    # row PREFIX has been byte-identical from step 08 on -- step 07 printed `>=54` \
    # as a literal and step 08 changed the row to `>=%-3s` from HOLD_MAJOR -- so a \
    # ban written on the prefix would fail on a correct file, and what makes the \
    # entry safe is the weight after it: x0.0490410715643904 where a correct file \
    # prints x0.043156142976663552 under the same label. The weight ALONE would \
    # still be useless, 0.0490410715643904 being a prefix of the \
    # 0.049041071564390400 this rung prints on B-12, and that collision is not an \
    # accident of this one row. \
    # \
    # ELEVEN OF THESE EIGHTEEN WEIGHTS ARE A PREFIX OF A WEIGHT THE REPORT STILL \
    # PRINTS TODAY. From B-11 up, the family-B weights are a geometric chain with \
    # ratio 1/0.88 -- below that the B-10 penalty rung breaks it -- so multiplying \
    # the whole family by 0.88 slides each of those onto the value its neighbour \
    # one row DOWN used to hold: step 12's B-11 0.0490410715643904 is this rung's \
    # B-12 with two zeros appended, and the same holds for eight consecutive \
    # positions, B-11 through B-18. Three more collide off that diagonal, step \
    # 12's B-3, B-4 and B-6 all having carried 0.0490410715643904 as well. Every \
    # one of the eleven is rescued by the label in front of it -- \
    # `B11 java 8 lang held` is not `B12 java 8 lib held ` -- so the ban reads \
    # false where the bare number would read true. THIS IS WHY NO ENTRY IN THIS \
    # LIST IS A BARE NUMBER. \
    # \
    # ALL FIVE FAMILY-C ROWS ARE BANNED BY LABEL ALONE, and this is the first \
    # rung where that is true of the whole family. Every rung before this one \
    # re-labelled some C legs and carried others across, so C-1's entry had to \
    # spell out its tail to be false on a correct file. Step 13 changes family \
    # C's SUBJECT -- it stops asking what the level is and starts asking what the \
    # compiler still complains about -- so not one of `    C1 level %-3s minor`, \
    # `    C2 module imports`, `    C3 imports subsumed`, `    C4 scoped value` \
    # or `    C5 compact headers` survives the rung, and the label is enough. \
    # \
    # CARRYING THEIR WEIGHTS WOULD BE WORSE THAN USELESS, because not one of the \
    # five is a dead number. 0.38, 0.33, 0.25 and 0.02 are step 12's C-2 through \
    # C-5 and all four are live arithmetic one family over, inside the \
    # `(0.38*b + 0.33*c + 0.25*d + 0.02*e) / 0.98` this file asserts PRESENT two \
    # hundred lines above; C-1's own 0.02 is the same literal as C-5's and is \
    # therefore live too, by coincidence rather than by inheritance. A ban on the \
    # label-and-weight pair would be spelling out live coefficients under dead \
    # labels, and would still miss the failure it is for -- a row re-weighted but \
    # never re-labelled. \
    for oldrow in "    B1 no-maven          %s  x0.0245205357821952" \
                  "    B2 wrapper           %s  x0.0122602678910976" \
                  "    B3 published         %s  x0.0490410715643904" \
                  "    B4 one major, >=%-3s  %s  x0.0490410715643904" \
                  "    B5 tests present     %s  x0.0408675596369920" \
                  "    B6 api surface       %s  x0.0490410715643904" \
                  "    B7 tests run         %s  x0.0245205357821952" \
                  "    B8 jupiter held      %s  x0.0408675596369920" \
                  "    B9 junit4 retired    %s  x0.0245205357821952" \
                  "    B10 java 7 held      %s  x0.0449543156006912" \
                  "    B11 java 8 lang held %s  x0.0490410715643904" \
                  "    B12 java 8 lib held  %s  x0.0557284904140800" \
                  "    B13 java 9-10 held   %s  x0.0633278300160000" \
                  "    B14 java 11 held     %s  x0.0719634432000000" \
                  "    B15 jpms + services  %s  x0.0817766400000000" \
                  "    B16 java 17 held     %s  x0.0929280000000000" \
                  "    B17 java 21 held     %s  x0.1056000000000000" \
                  "    B18 java 22 held     %s  x0.1200000000000000" \
                  "    C1 level %-3s minor" "    C2 module imports" \
                  "    C3 imports subsumed" "    C4 scoped value" \
                  "    C5 compact headers"; do \
        if grep -qF "printf '${oldrow}" /tests/test.sh; then \
            echo "the report still prints a step-12 row: ${oldrow}" >&2; \
            exit 1; \
        fi; \
    done; \
    # THE PENALTY IS PRINTED ON B-14'S ROW rather than on a row of its own, and \
    # that is the inheritance made visible. At step 07 it multiplied family C and \
    # earned its own unweighted line; here it multiplies exactly one gate, so it \
    # belongs in that gate's parenthesis and nowhere else. A rung that kept the \
    # standalone line would be telling a reader the penalty multiplies family C, \
    # which at this rung it does not -- it multiplies 0.063327830016 of family B \
    # and nothing else. \
    # \
    # THIS RUNG IS THE ONE WHERE THAT DISTINCTION STOPS BEING BOOKKEEPING, \
    # because the penalty and family C now read the same two properties. The \
    # penalty fires on raw types ADDED over step 07's frozen baseline and on \
    # @SuppressWarnings sites ADDED over it; C-1 and C-3 pay for suppression \
    # sites REMOVED and raw declarations NARROWED against step 13's own. They \
    # are the same two counters read in opposite directions, and on the golden \
    # neither direction touches the other: 40 of 44 sites go and 0 arrive, so \
    # `now_sup > base_sup` is false and the penalty stays 1.000000 while C-1 \
    # reads its budget met. A rung that had left the penalty multiplying family \
    # C would have had it multiply the very legs it double-counts, and the \
    # golden would have paid twice for work it did once. \
    if grep -qF 'anti-gaming penalty     %s  (multiplies family C)' /tests/test.sh; then \
        echo "the report still prints the penalty as a family-C multiplier" >&2; \
        exit 1; \
    fi; \
    # ALL FIVE FAMILY-C ROWS PRINT THEIR RAW READINGS beside their paid ones, \
    # and at this rung that is not a courtesy but the only numbers two of the \
    # rows have. C-4 and C-5 print `(multiplier)` where a weight belongs, so \
    # their rows carry a score and a parenthesis and nothing else; strip the \
    # parenthesis and the row says `0.000000 (multiplier)`, which is true of a \
    # tree that deleted every class file and of a tree that never built one. \
    # \
    # EVERY ONE OF THE FIVE TAILS IS A min()'S INPUTS PRINTED BEFORE THE min \
    # TAKES THEM, and there is not a decorative term among them. This is the \
    # break from the rung below, which had two of its five carrying a census no \
    # arithmetic read -- C-1's preview count and C-5's fork tally, diagnostics \
    # parked in a scored row. Here the mapping is exact: C-1 is \
    # `minf(minf(allow/want, (extraw-extra)/extraw), (unusedw-unused)/unusedw)` \
    # and prints all three pairs; C-3 is `minf(narrowed/open, held/forced)` and \
    # prints both; C-4 is `minf(classfiles/want, at-69.0/classes)` and prints \
    # both; C-5 is `minf(ids/want, xmls/want)` and prints both. A reader can \
    # recompute every one of the five scores from its own row, which no rung \
    # below this one could say of all five. \
    # \
    # C-2'S TAIL NAMES ANOTHER GATE, and that is the one term here that is not \
    # this leg's own reading. `bound to C1 %s` prints C-1's score into C-2's \
    # row because C-2 is `minf(warnfrac, c1)` -- the suppression bind, which is \
    # the whole reason C-2 cannot be satisfied by annotating. Without it a tree \
    # that silenced all 36 warnings with @SuppressWarnings prints C-2 at \
    # 0.000000 beside `0 of 36 fixable warnings left`, and those two numbers \
    # read as a contradiction rather than as the mechanism working. WITH it the \
    # row says which of the two inputs bound the min, and the reader is pointed \
    # one row up to the gate that did it. A rung that dropped this term would \
    # have built the coupling and then hidden it. \
    # \
    # NO TWO OF THIS RUNG'S FIVE TAILS ARE THE SAME AND NO ONE OF THEM IS \
    # ANOTHER WITH A TERM REMOVED, which is what lets the ban list below do \
    # work a count alone cannot. Three of the five open on the same six \
    # characters, `(%s of %s `, and they diverge at the seventh: `fixable`, \
    # `class` and `ids`. Trim C-4's second pair and it lands on \
    # `(%s of %s class files)`, which is step 10's and step 11's C-1 tail and \
    # is dead at this rung, so the truncation is bannable rather than \
    # ambiguous; trim C-1's third pair, or C-3's second, or C-5's second, and \
    # each lands on a string no rung has ever printed. \
    # \
    # THE COUNTS STAY ALL THE SAME, one per row. Every family-C row at this \
    # rung is the same printf shape -- four spaces, a label, a score, a weight \
    # or `(multiplier)`, a parenthesis -- and the five differ only in their \
    # text, which is the shape an edit copies rather than writes. A tail that \
    # appears twice is two rows printing the same parenthesis, and since each \
    # leg reads a different population that is one row reporting another's \
    # numbers, which on the golden is invisible: all five read their own \
    # denominators back. \
    test "$(grep -cF '(allowed %s of %s, extra %s of %s, unused %s of %s)' /tests/test.sh)" = 1; \
    test "$(grep -cF '(%s of %s fixable warnings left, bound to C1 %s)' /tests/test.sh)" = 1; \
    test "$(grep -cF '(narrowed %s of %s, held %s of %s)' /tests/test.sh)" = 1; \
    test "$(grep -cF '(%s of %s class files, %s of %s at %s.%s)' /tests/test.sh)" = 1; \
    test "$(grep -cF '(%s of %s ids passed, %s of %s xml files)' /tests/test.sh)" = 1; \
    # ...and the facts those parentheses are printed from, asserted by name. \
    # C-4's is the one that has to be written carefully: the fact is \
    # `gate.c4.at${WANT_MAJOR}.${WANT_MINOR}`, two variables deep, and it is \
    # single-quoted here so the ${...} reach the file as the literal characters \
    # test.sh spells them with rather than as 69 and 0. It was `gate.c1.at...` \
    # one rung ago and the leg number is the whole of the change, which is the \
    # hazard this whole section exists for. \
    grep -qF 'fact "gate.c1.allow.want=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.extras.want=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.unused.want=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.pairs=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.sites=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.budfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c1.unusedfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.fixable=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.lint=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.warnings=' /tests/test.sh; \
    grep -qF 'fact "gate.c2.warnfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c3.depfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c3.holdfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.classfiles.want=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.classes=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.at${WANT_MAJOR}.${WANT_MINOR}=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.invfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c4.levelfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c5.passfrac=' /tests/test.sh; \
    grep -qF 'fact "gate.c5.xmlfrac=' /tests/test.sh; \
    # ...and the sub-fraction names NO leg publishes, asserted as absences \
    # rather than left to the eye. Two different mistakes land in this list and \
    # they are worth telling apart. \
    # \
    # THE FIRST IS THE RENUMBERING, and it is the one this rung invented. Four \
    # of step 12's five family-C legs did not retire: they moved wholesale into \
    # family B as B-19.2 through B-19.5, keeping their questions, their \
    # populations and their arithmetic, and losing only the letter in front of \
    # their names. So `gate.c2.setfrac` still exists in this file -- as \
    # `gate.b19.2.setfrac`, asserted present in the family-B section above -- \
    # and a leg that published it under `gate.c2` would be a file that had \
    # renumbered the REPORT and forgotten the FACTS. That failure is invisible \
    # on the golden, where both spellings carry the same number, and is exactly \
    # what a harvester reading `gate.c2.setfrac` off this rung would be misled \
    # by: it would be reading the module-import set fraction and believing it \
    # had read the warning fraction. The level names go the other way, C-1's \
    # `classes`, `preview` and `at69.0` having moved DOWN the family to C-4, and \
    # they are banned under c1 for the same reason in the other direction. \
    # \
    # THE SECOND IS THE OLD ONE, a name an edit reaches for out of habit. Every \
    # rung from 07 to 12 named its family-C halves from one vocabulary -- \
    # `depfrac`, `arrfrac`, `holdfrac` -- and at this rung FOUR OF THE FIVE LEGS \
    # have no departure to name. C-1's halves are a budget and an unused count, \
    # C-2's is a warning fraction, C-4's are an inventory and a level, C-5's are \
    # a pass count and an xml count; nothing leaves the tree in any of the four. \
    # Only C-3 keeps the old vocabulary and keeps it honestly, raw declarations \
    # being the one population at this rung that genuinely departs. A leg \
    # publishing `gate.c1.depfrac` while scoring min(budget, unused) would be a \
    # report describing arithmetic the scorer does not do. \
    for deadfrac in 'gate.c1.classes=' 'gate.c1.preview=' \
                    'gate.c1.at${WANT_MAJOR}.${WANT_MINOR}=' \
                    'gate.c1.depfrac=' 'gate.c1.holdfrac=' \
                    'gate.c2.setfrac=' 'gate.c2.cleanfrac=' \
                    'gate.c2.depfrac=' 'gate.c2.holdfrac=' 'gate.c2.arrfrac=' \
                    'gate.c3.arrfrac=' \
                    'gate.c4.depfrac=' 'gate.c4.arrfrac=' 'gate.c4.holdfrac=' \
                    'gate.c4.exec=' \
                    'gate.c5.projects=' 'gate.c5.forks=' \
                    'gate.c5.depfrac=' 'gate.c5.holdfrac='; do \
        if grep -qF "fact \"${deadfrac}" /tests/test.sh; then \
            echo "a family-C leg publishes a sub-fraction its score has no term for: ${deadfrac}" >&2; \
            exit 1; \
        fi; \
    done; \
    # ...and every retired or TRUNCATED tail asserted gone in one sweep. The \
    # entries are older rungs' tails and this rung's own tails with a term \
    # chopped off, which is the exact shape an edit that trimmed a row to fit \
    # the column would leave behind. A tail cut short still prints, still lines \
    # up, and still says nothing about the term that was removed -- and on the \
    # golden, where the terms it kept read their denominators back, it reads \
    # correct. \
    # \
    # ALL FIVE OF THE RUNG BELOW'S TAILS ARE IN THIS LIST, and this rung does \
    # not have to argue for it the way step 12 did. Step 12 had to show that \
    # each of step 11's four had moved by a term or a word; here family C stops \
    # asking about the language level and starts asking what the compiler still \
    # complains about, so there is no tail to compare against and all five are \
    # simply gone. \
    # \
    # FOUR OF THOSE FIVE STILL DESCRIBE ARITHMETIC THIS FILE DOES, which is the \
    # part worth being careful about. Step 12's C-2 through C-5 did not retire; \
    # they became B-19.2 through B-19.5, same questions, same populations, same \
    # min()s, and this file asserts their facts present in the family-B section \
    # above. What retired is their ROWS. B-19 prints ONE row for four legs -- \
    # `B19 java 25 held` and a composite -- so those four parentheses have \
    # nowhere left to print, and a file that kept any of them would be printing \
    # family-B arithmetic in a family-C row. That is the failure this list \
    # catches that the row ban above cannot: the row ban reads labels, and a \
    # tail pasted onto a correctly-labelled row carries no label to read. \
    # \
    # Each truncated form is written with its closing parenthesis so that the \
    # full tail cannot satisfy its own ban: `(allowed %s of %s)` is not a \
    # substring of `(allowed %s of %s, extra %s of %s, unused %s of %s)`, \
    # because the live row has a comma where the ban has a bracket. That is the \
    # whole reason these are fgrep strings ending in `)` rather than prefixes. \
    # C-4's truncation needs no new entry: chop its second pair and it lands on \
    # `(%s of %s class files)`, which has been in this list since step 12 as \
    # step 11's C-1 tail, and one string can be two rungs' mistakes at once. \
    for deadtail in '(converted %s, held plain %s)' '(exec %s)' \
                    '(closed %s of %s, open %s of %s)' '(%s of %s files)' \
                    'readable %s)' \
                    '(converted %s, held %s)' \
                    '(converted %s, held %s, exec %s)' \
                    '(%s of %s class files)' '(departed %s, held %s)' \
                    '(departed %s, arrived %s, held %s)' \
                    '(departed %s, arrived %s, held %s, exec %s)' \
                    '(departed %s)' '(departed %s of %s)' \
                    '(departed %s, arrived %s)' '(sets %s of %s)' \
                    '(%s of %s projects)' \
                    '(%s of %s class files, preview %s)' \
                    '(sets %s of %s, clean %s of %s)' \
                    '(departed %s of %s, held %s of %s)' \
                    '(departed %s, arrived %s, held %s of %s)' \
                    '(%s of %s projects, %s of %s forks)' \
                    '(allowed %s of %s)' '(allowed %s of %s, extra %s of %s)' \
                    '(%s of %s fixable warnings left)' \
                    '(narrowed %s of %s)' '(%s of %s ids passed)'; do \
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
    test "${nrep}" = 5; \
    # The fact-line pattern stays anchored at `^ *fact` and not at `^fact` even \
    # though all five of this rung's family-C facts are unindented, which is what \
    # step 10 settled on once its C-1 stopped being read before its guard. \
    # The tolerant anchor is kept deliberately: it is the anchor that keeps \
    # counting correctly if a later rung puts family C back inside a guard, and a \
    # pattern that has to be edited to stay true is a pattern that will be edited \
    # wrongly. The count is asserted against a literal 5 as well, so the \
    # tolerance cannot hide a leg that grew or vanished on both sides at once. \
    # \
    # THE LITERAL STAYS AT 5 AND THAT IS THE LEAST INFORMATIVE IT HAS EVER BEEN, \
    # which is worth writing down rather than leaving as a number that happened \
    # not to change. Step 12 had five family-C legs and this rung has five, and \
    # not one of the five is the same leg: four of step 12's moved into B-19 and \
    # the fifth, its level census, moved DOWN the family to C-4 to make room for \
    # a suppression budget at C-1. A count that holds while every member is \
    # replaced is a count that proves nothing on its own, and it is here only \
    # because it is the half of the pair the sequence comparison cannot do -- a \
    # family that drops a row AND its coefficient together compares equal. The \
    # work of catching this rung's renumbering is done by the row bans, the tail \
    # bans and the fact-name bans above, every one of which reads TEXT. \
    # \
    # THE ROW PATTERN STILL COUNTS C-4 AND C-5, and it is worth checking why, \
    # because those two rows print `(multiplier)` where every row above them \
    # prints `x0.NN`. This pattern stops at the label -- `^printf '    C[0-9]` \
    # followed by a digit-or-none and a space -- so it never looks as far as the \
    # weight and counts all five. The family loop BELOW does look that far, and \
    # it is the loop that must not count them: three coefficients, three rows. \
    # The two patterns disagree by design and the disagreement is the design. \
    # \
    # And the general form of the same check, for every family at once, because \
    # asserting fifteen literals by hand closes one instance of the hole and \
    # leaves the class of it open -- family B's nineteen printfs and family B's \
    # awk are the same pair of spellings, and they agree today only by habit. For \
    # each family this pulls the weights out of the report rows in order and out \
    # of that family's awk expression in order, and requires the two sequences to \
    # be equal. A family added later is covered by adding its letter to the list. \
    # \
    # The gate index is matched as [0-9][0-9]* and not [0-9], which is not \
    # pedantry: family B passed ten gates eight rungs below, and a one-digit \
    # pattern reads nine rows against nineteen coefficients and calls the \
    # difference a mismatch -- or worse, would have silently skipped B10 through \
    # B19 on both sides if the coefficient pattern had been one-digit too. \
    # And the expected NUMBER of gates in each family, stated here, because the \
    # sequence comparison alone is satisfied by a family that lost a row and its \
    # coefficient together -- which is precisely how a gate gets dropped. \
    # \
    # THE WEIGHT PATTERN IS 0\.[0-9][0-9]* AND IT HAS TO STAY OPEN-ENDED, which \
    # matters more at this rung than at any before it. Family B's weights are \
    # EIGHTEEN decimals wide and family C's are two, on rows a dozen lines apart \
    # in one report; a pattern that pinned a width would read one family and \
    # silently skip the other, and silently skipping a family is the whole of \
    # what this loop exists to catch. Eighteen is two more than the rung below \
    # printed, and nothing in this file was edited to make room for them -- which \
    # is exactly the point. \
    # \
    # FAMILY C'S EXPECTED COUNT IS 3, NOT 5, AND THAT IS THE ONE NUMBER IN THIS \
    # LOOP A READER WILL WANT TO ARGUE WITH. Family C has five legs and prints \
    # five rows; two of them, C-4 and C-5, are MULTIPLIERS and carry no weight at \
    # all. The row pattern requires `  x0.NN` after the score and finds it on \
    # three rows; the coefficient pattern reads `0\.[0-9][0-9]*\*` out of \
    # `(0.15*a + 0.57*b + 0.28*c)*d*e` and finds three numbers, because `*d` and \
    # `*e` have no number in front of them. Three against three. The count of 5 \
    # asserted a few lines above is the OTHER count -- rows printed, not rows \
    # weighted -- and the two disagree because they are about different things. \
    # A rung that made these two numbers agree would have done it by giving C-4 \
    # a weight, which is the change this whole family was restructured to avoid. \
    # \
    # The row pattern needs one space before %s and this rung's family B has rows \
    # whose labels are a character longer than the column, so the columns no \
    # longer line up and the pattern must not depend on them. It matches \
    # ` .*  x0.NN` and not a fixed column for exactly that. The hard cases are \
    # B-4, which carries a `%-3s` conversion in its LABEL; B-14, which carries a \
    # `(penalty %s)` AFTER its weight; and all three weighted family-C rows, each \
    # of which carries a parenthesised tail after its weight -- so on none of \
    # them is the weight the first or the last field on the line. C-1's is the \
    # longest tail any rung has printed, three pairs of numbers, and the pattern \
    # did not have to change to carry it. \
    # \
    # Family C's anchor is `0\.28\*c\)\*d\*e$` and family B's is \
    # `0\.120000000000000000\*b19$`, both anchored at end of line. The C anchor \
    # has to spell the two multipliers out, and that is new. Every rung from 07 \
    # to 12 could anchor on its last WEIGHTED term -- step 12's was `0\.02\*e$` \
    # -- because the last weighted term was the last thing on the line. Here it \
    # is not: the line ends `0.28*c)*d*e`, so `0\.28\*c$` matches nothing and the \
    # anchor must reach through the closing bracket to the end. Spelling them out \
    # turns the anchor into a second assertion for free -- a rung that gave C-4 a \
    # weight, or wrote the multipliers with spaces around them, breaks the anchor \
    # rather than the arithmetic, and the check below reports "0 lines look like \
    # its weight expression" instead of comparing anything. That the expression \
    # carries no space around either `*` is asserted on its own a hundred lines \
    # above, for this reason. \
    # \
    # On the B side `0.120000000000000000*b19` is unique in the file at eighteen \
    # decimals, but the `$` is kept there too. Two other whole-family lines in \
    # this file end in a bare `*<letter>` term -- family A's \
    # `0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e`, and B-19's composite, which \
    # ends `0.02*e) / 0.98` and is the reason step 12's C anchor cannot simply be \
    # carried forward -- and an anchor that can match mid-line is an anchor that \
    # will, one rung from now, on some expression nobody is thinking about yet. \
    # The count below still asserts that each anchor picked exactly ONE line. \
    for fam in B:19:'0\.120000000000000000\*b19$' C:3:'0\.28\*c\)\*d\*e$'; do \
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
    # SIX RUNGS' FAMILY-C NAMES ARE IN THIS LIST UNDER B-14, B-15, B-16, B-17, \
    # B-18 AND B-19 PREFIXES, which is the whole inheritance in twenty-two \
    # lines. The leaf of each name is unchanged -- jakarta, javax-held, toarray, \
    # httpclient, module-names, platform-lookup, pattern-instanceof, \
    # text-blocks, sealed-hierarchies, arrow-switch, type-patterns, \
    # pattern-switch, sequenced-access, doprivileged-removal, unnamed-catch, \
    # varhandle, and now module-imports, import-subsumption, scoped-value and \
    # compact-headers -- because the question each leg asks is unchanged; only \
    # the family that asks it moved. The LEG NUMBER is unchanged too, so \
    # `sed 's/^B1[456789]\.\([0-9]\)\./C\1./'` over this rung's facts.txt turns \
    # the inherited names back into the names steps 07, 08, 09, 10, 11 and 12 \
    # printed, and all six files line up against it. That is what makes the \
    # claims "B-14 is step 07's family C", "B-15 is step 08's", "B-16 is step \
    # 09's", "B-17 is step 10's", "B-18 is step 11's" and "B-19 is step 12's" \
    # checkable rather than decorative. \
    # \
    # THIS IS THE LAST TIME THAT SENTENCE CAN BE WRITTEN, because there is no \
    # step 14 to inherit step 13's family C. That is not a reason to write the \
    # list any more loosely: a rung that knows it is last is the rung most \
    # likely to be edited by someone who has stopped expecting the next one. \
    # \
    # B-15 HAS NO LEG 1 AND ITS GAP IS THE ODD ONE OF THE FIVE. Step 08's C-1 \
    # asked whether the twelve artifacts declared a module name the platform \
    # could read back; B-15 drops it rather than carrying it, because the \
    # manifests are written once and a rung that re-charged for them would be \
    # paying twice for a line in a build file. What survives of it is the \
    # SCALING: B-15.2 is the square of the name fraction, which is what step \
    # 08's C-1-times-C-2 collapses to once C-1 is gone. \
    # \
    # B-16, B-17 AND B-18 EACH HAVE THE SAME GAP, AND IT IS THE NINTH TRAP \
    # RATHER THAN A MATTER OF TASTE. Step 09's C-1 read the fraction of this \
    # tree's class files that stood at major 61, step 10's read the same fraction \
    # at 65, step 11's at 66, and all three are spelled `C1.level-${WANT_MAJOR}` \
    # because the major in that name is a parameter and not a literal. Carrying \
    # any of them would have been wrong whichever way round it was done. Pinned \
    # at its own major it reads 0.000000 on a CORRECT tree one rung later, \
    # because a tree compiled at 69 has no class files left at 66, 65 or 61 -- an \
    # inherited leg that no right answer can pass, which is the ninth trap \
    # exactly. Parameterised at 69 it is this rung's own C-1 under a second name, \
    # charging twice for one class-file scan. So B-16 is step 09's family C with \
    # its C-1 removed and the remaining four renormalised over 0.90, B-17 is step \
    # 10's with the same subtraction and the remaining three renormalised over \
    # 0.96, B-18 is step 11's with the same subtraction and the same divisor, and \
    # the level question is asked once, by C-4. The absent legs are asserted \
    # below beside B-13's and B-14's. \
    # \
    # AND B-19 HAS THE SAME GAP FOR A REASON THAT IS NEITHER OF THE TWO ABOVE. \
    # Step 12's C-1 read the fraction of this tree's class files standing at \
    # major 69 minor 0. B-16's, B-17's and B-18's absent legs read 61, 65 and 66, \
    # majors a correct tree at this rung has no class files at -- inherited legs \
    # no right answer could pass, the ninth trap. Step 12's reads 69.0, and a \
    # correct tree at THIS rung passes it perfectly, because this rung moves no \
    # level. So B-19.1 would not be unpassable; it would be PAID TWICE. This \
    # rung's C-4 asks exactly that question, off exactly that scan, and a \
    # B-19.1 beside it would charge 0.02 of B-19 for a number already worth a \
    # whole multiplier one family over. The four legs that remain are \
    # renormalised over 0.98, the divisor asserted two hundred lines above. \
    for g in B14.2.jakarta B14.3.javax-held B14.4.toarray B14.5.httpclient \
             B14.java11-jakarta \
             B15.2.module-names B15.3.platform-lookup B15.jpms-services \
             B16.2.pattern-instanceof B16.3.text-blocks \
             B16.4.sealed-hierarchies B16.5.arrow-switch B16.java17-constructs \
             B17.2.type-patterns B17.3.pattern-switch B17.4.sequenced-access \
             B17.java21-constructs \
             B18.2.doprivileged-removal B18.3.unnamed-catch B18.4.varhandle \
             B18.security-api-removal \
             B4.one-major-ge- \
             B8.jupiter-held B9.junit4-held B10.java7-held B11.java8lang-held \
             B12a.optional B12b.collectors B12c.objects B12d.jtime \
             B12e.ships-java8-library B12.java8-library \
             B13.2.newinstance B13.3.isaccessible B13.4.boxedctor \
             B13.5.factories B13.6.optional9 B13.7.var B13.java9-10 \
             B19.2.module-imports B19.3.import-subsumption \
             B19.4.scoped-value B19.5.compact-headers B19.java25-constructs \
             C1.suppression-budget C2.warning-free C3.raw-declarations \
             C4.level-held C5.behaviour-held; do \
        grep -qF "fact \"${g}" /tests/test.sh; \
    done; \
    # The five family-C leaves are named for what they ASK, not for what they \
    # read, and the distinction is the one this rung is most likely to blur. \
    # `C1.suppression-budget` is not "there are at most four @SuppressWarnings \
    # left": it is the FOUR NAMED PAIRS of baseline/suppress.txt present AND the \
    # other 46 (file, value) pairs gone AND all 27 values that silence nothing \
    # gone, because a budget written as a count passes a tree that suppressed \
    # four fixable warnings and fixed none of the forced ones. \
    # `C2.warning-free` is not "the build printed no warnings": it is the \
    # SCORER's own javac, at -source 25 under GATE_LINT, reading 0 of the 30 \
    # FIXABLE warnings -- 36 honest minus the 6 the permitted annotations \
    # forgive -- and then min()'d against C-1, so a tree that annotates its way \
    # to silence scores zero here however quiet it is. `C3.raw-declarations` is \
    # not "the word `raw` is gone": it is 7 open raw declarations narrowing AND \
    # the 4 that CANNOT narrow still raw, because both TestNG transformers \
    # implement an interface whose 5.14.6 signature declares its parameters raw \
    # and a tree that parameterises them does not compile. `C4.level-held` is \
    # not "the build file says 25": it is 435 class files still present AND \
    # every one the scan found standing at major 69 MINOR 0, so a release \
    # setting that never reached javac pays nothing and a tree built behind \
    # `--enable-preview`, which writes minor 65535, pays nothing either. \
    # `C5.behaviour-held` is not "the tests were run": it is the 160 ids B-7 \
    # already parsed passing AND the 53 XML files still there, which is that \
    # same evidence read a second time as a HOLD on this rung's work rather \
    # than as a score for the rung that produced it. \
    # \
    # ALL FIVE ARE A MINIMUM OVER THEIR HALVES AND NOT AN AVERAGE, which is new \
    # at this rung: every family C below this one had at least one leg that was \
    # a plain fraction. C-1 takes min(allowed, extras gone, unused gone), C-2 \
    # takes min(warnings, C-1), C-3 takes min(narrowed, held), C-4 takes \
    # min(inventory, level), C-5 takes min(ids, xmls). No half can be bought \
    # with another anywhere in the family. \
    # \
    # C-2'S min() IS THE ONE THAT REACHES OUTSIDE ITS OWN LEG, and that is the \
    # anti-silence mechanism stated as arithmetic. The other four bound a leg by \
    # a second reading of its own population; C-2 is bounded by a DIFFERENT \
    # GATE'S SCORE, because the warning count is the one number in this file the \
    # code under test can drive to zero by writing a comment-shaped annotation, \
    # and C-1 is the number it cannot. \
    # That is why the report prints the sub-fractions beside each leg: the leg's \
    # number IS one of them wherever a min() is taken, and a reader has no way to \
    # tell which without them -- which at this rung is every leg. \
    # \
    # And the measurements underneath them, which are what a reviewer reads when \
    # a leg comes back short. These are asserted by name because a leg that \
    # scores correctly while publishing nothing is a leg no one can debug: at \
    # this rung C-1 reads 0.000000 on a tree that suppressed every warning in \
    # sight and publishes no number at all on a tree whose source report never \
    # arrived, and only `gate.c1=no-report` tells those two apart. \
    # \
    # THE MODULE MEASUREMENTS MOVED PREFIX WITH THEIR FAMILY. Step 08 published \
    # them as `gate.mod.*` when the module graph was its family C; here they are \
    # `gate.b15.mod.*`, under the gate that now asks the question. The bare \
    # `gate.mod.` spelling is asserted gone below for the usual reason -- it \
    # would read the right number under the wrong family -- and the five leaves \
    # are unchanged so the two files still diff. \
    # \
    # AND FOUR RUNGS' FAMILY-C MEASUREMENTS MOVED THE SAME WAY, WITH ONE \
    # DIFFERENCE THAT DECIDES HOW THEY ARE POLICED. Step 09 published its four \
    # under `gate.c2.*` through `gate.c5.*` and they are `gate.b16.2.*` through \
    # `gate.b16.5.*` here; step 10 published its three under `gate.c2.*` through \
    # `gate.c4.*` and they are `gate.b17.2.*` through `gate.b17.4.*`; step 11 \
    # published its three the same way and they are `gate.b18.2.*` through \
    # `gate.b18.4.*`; step 12 published FOUR that moved, `gate.c2.*` through \
    # `gate.c5.*`, and they are `gate.b19.2.*` through `gate.b19.5.*`. All four \
    # sets now sit under the gate that asks the question. But the vacated \
    # prefixes are not vacant: this rung's own C-1 through C-5 publish under \
    # `gate.c1.` through `gate.c5.` again, asking different questions with \
    # different leaves. So the bare prefix CANNOT be asserted gone the way \
    # `gate.mod.` can, and the retired leaves are banned one at a time in the \
    # dead-name loop above instead. \
    # \
    # `gate.c5.` STAYS UNBANNABLE FOR A SECOND RUNG, which is worth saying \
    # because it is the entry a tidying edit would put back. Steps 10 and 11 \
    # could ban the prefix outright, their family C having four legs and no \
    # fifth name that could mean anything. Step 12 withdrew the ban when it grew \
    # a fifth leg, and this rung has five as well -- C-5 publishes six leaves \
    # under it -- so the ban would fail on a correct file. It stays withdrawn. \
    # \
    # SIX LEAVES SURVIVE THE MOVE FROM THE RUNG BELOW IN PLACE, AND ALL SIX ARE \
    # UNDER `gate.c3.`. That is a collapse from the fourteen the rung below \
    # carried, and the arithmetic is worth doing because the reason is the shape \
    # of this rung's family rather than carelessness below. `departed`, \
    # `departed.want`, `depfrac`, `held`, `held.want` and `holdfrac` are live \
    # under `gate.c3.` on both rungs, because raw declarations are the ONE \
    # population at this rung that genuinely departs and holds -- the same \
    # question step 12's C-3 asked of imports, asked of types. Under \
    # `gate.c1.`, `gate.c2.`, `gate.c4.` and `gate.c5.` NOT ONE leaf survives: \
    # a suppression budget, a warning count, a class-file level and a test \
    # tally have no vocabulary in common with a module-import set, an import \
    # subsumption, a ScopedValue conversion and a fork flag. The rung below \
    # published 47 leaves under `gate.c[1-5].`; this one publishes 34; six are \
    # the same string. Those six are still the whole reason the ban list above \
    # is a list of leaves and not a list of prefixes -- a `gate.c3.` prefix ban \
    # would take every one of them with it -- and they are the reason that list \
    # is longer at this rung than at any before it. \
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
             gate.b18.2.departed gate.b18.2.departed.want \
             gate.b18.2.held gate.b18.2.held.want \
             gate.b18.2.held.ctor gate.b18.2.held.ctor.want \
             gate.b18.2.held.setacc gate.b18.2.held.setacc.want \
             gate.b18.2.sites gate.b18.2.sites.was \
             gate.b18.2.depfrac gate.b18.2.holdfrac \
             gate.b18.3.departed gate.b18.3.departed.want \
             gate.b18.3.arrived gate.b18.3.arrived.want \
             gate.b18.3.held gate.b18.3.held.want \
             gate.b18.3.unnamed gate.b18.3.unread gate.b18.3.read \
             gate.b18.3.depfrac gate.b18.3.arrfrac gate.b18.3.holdfrac \
             gate.b18.4.departed gate.b18.4.departed.want \
             gate.b18.4.arrived gate.b18.4.arrived.want \
             gate.b18.4.held gate.b18.4.held.want \
             gate.b18.4.handles gate.b18.4.setaccessible \
             gate.b18.4.setaccessible.was \
             gate.b18.4.depfrac gate.b18.4.arrfrac gate.b18.4.holdfrac \
             gate.b18.4.exec.want gate.b18.4.exec.broken gate.b18.4.exec \
             gate.b19.2.files gate.b19.2.files.want gate.b19.2.files.was \
             gate.b19.2.imports gate.b19.2.imports.want \
             gate.b19.2.clean gate.b19.2.clean.want gate.b19.2.spurious \
             gate.b19.2.setfrac gate.b19.2.cleanfrac \
             gate.b19.3.departed gate.b19.3.departed.want \
             gate.b19.3.departed.excused \
             gate.b19.3.held gate.b19.3.held.want gate.b19.3.held.excused \
             gate.b19.3.total gate.b19.3.total.was \
             gate.b19.3.held.static gate.b19.3.held.nested \
             gate.b19.3.held.ambiguous \
             gate.b19.3.depfrac gate.b19.3.holdfrac \
             gate.b19.4.departed gate.b19.4.arrived gate.b19.4.convert.want \
             gate.b19.4.held gate.b19.4.held.want \
             gate.b19.4.svcalls gate.b19.4.svcalls.want gate.b19.4.tldecls \
             gate.b19.4.depfrac gate.b19.4.arrfrac gate.b19.4.holdfrac \
             gate.b19.5.flag gate.b19.5.projects.floor gate.b19.5.probe \
             gate.b19.5.tasks \
             gate.b19.5.projects gate.b19.5.projects.seen \
             gate.b19.5.projects.want \
             gate.b19.5.forks gate.b19.5.forks.flagged gate.b19.5.projects.was \
             gate.c1 gate.c1.allow.want gate.c1.extras.want gate.c1.unused.want \
             gate.c1.pairs gate.c1.sites \
             gate.c1.allow gate.c1.extras gate.c1.unused \
             gate.c1.budfrac gate.c1.unusedfrac \
             gate.c2 gate.c2.fixable gate.c2.lint gate.c2.warnings \
             gate.c2.warnfrac \
             gate.c3 gate.c3.departed.want gate.c3.held.want \
             gate.c3.open gate.c3.forced \
             gate.c3.departed gate.c3.held \
             gate.c3.depfrac gate.c3.holdfrac \
             gate.c4 gate.c4.classfiles gate.c4.classfiles.want \
             gate.c4.classes \
             gate.c4.invfrac gate.c4.levelfrac \
             gate.c5.passed gate.c5.passed.want gate.c5.xmls gate.c5.xmls.want \
             gate.c5.passfrac gate.c5.xmlfrac \
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
    # AND B-19 IS MISSING ITS LEG 1 FOR A THIRD REASON AGAIN, which is why it \
    # too gets its own check. B-13's, B-14's and B-15's absent legs were dropped \
    # because the thing they measured cannot move at this rung, or is reported \
    # rather than charged. Step 12's C-1 CAN be measured here and its answer is \
    # 1.000000, because step 12 delivered major 69 minor 0 and this rung moves \
    # no level. That is exactly why it must not be carried: a leg that reads \
    # full on the tree the candidate is handed is a leg the candidate is paid \
    # for arriving with, and this rung already charges the same reading as C-4, \
    # where it is worth a whole multiplier rather than 0.02 of one family. \
    if grep -qF 'fact "B19.1.' /tests/test.sh; then \
        echo "B-19 has a leg 1: step 12's C-1 is asked here by C-4, not twice" >&2; \
        exit 1; \
    fi; \
    # THE CLASS-FILE MAJOR IS A FAMILY-C QUESTION AT THIS RUNG, as it has been \
    # at every rung since 09, and at this one it is a family-C question ASKED BY \
    # A DIFFERENT LEG. Step 08's family C was the module graph and read no major \
    # at all; steps 09, 10 and 11 each made the level their C-1 and read \
    # `major=` alone; step 12 made it C-1 too and read `major=` AND `minor=`. \
    # This rung reads both, 69 and 0, out of ${WORK}/scan.txt by a separate \
    # instrument over the class files the build actually produced, and reads \
    # `preview.classes` beside them -- but it reads them as C-4, half of a \
    # MULTIPLIER, with the class-file inventory as the other half. \
    # \
    # THE MOVE FROM C-1 TO C-4 IS THE WHOLE DIFFERENCE BETWEEN A WEIGHT AND A \
    # VETO. At step 12 the level was 0.02 of family C: a tree that missed it \
    # scored 0.98 of what it otherwise would. Here a tree that misses it scores \
    # ZERO family C, because C-4 multiplies the weighted sum rather than \
    # entering it. That is not a harsher rung for its own sake; it is what the \
    # last rung has to do with a level that every rung below it already reached. \
    # The candidate is handed 69.0 and can only lose it, so the only edit the \
    # multiplier can punish is one that drops the release flag -- and a static \
    # closure done by recompiling at a lower release is the exact failure this \
    # step exists to catch. \
    # \
    # SO THE LEVEL IS READ TWICE AT THIS RUNG, by two gates, against two \
    # numbers, and that is deliberate rather than duplicated work: B-4 is a HELD \
    # gate reading `was=` out of level.txt -- major 69, step 12's rung, the floor \
    # the input tree already stands on and must not fall below -- and C-4 is a \
    # HELD gate too, reading `major=` and `minor=`, 69 and 0, as a fraction of \
    # the class files rather than as a single minimum. A rung that pointed both \
    # at one reading would forgive a partial regression: B-4 asks whether the \
    # LOWEST major in the build is 69, C-4 asks what SHARE of the class files \
    # reached 69.0, and a tree with one module left at 66 fails the first while \
    # scoring high on the second. Both reads are asserted, by their distinct \
    # fact names, and level.txt's three keys are asserted distinct and ordered \
    # in the baseline block far above. \
    # \
    # THIS IS THE FIRST RUNG WHERE `was=` AND `major=` HOLD THE SAME NUMBER. The \
    # `was=` key was introduced at step 09, when the level first became a \
    # family-C question, and at every rung since the two have differed by \
    # exactly one level move: 61 against 55, 65 against 61, 66 against 65, 69 \
    # against 66. This rung moves no level, so both are 69. They are still read \
    # by different keys, through different instruments, into differently-shaped \
    # scores, and the moment one of them is rewritten to read the other's key \
    # the distinction above is gone with no number changing anywhere -- which is \
    # precisely the edit a rung with two equal numbers in one file invites. \
    # \
    # THE MINOR IS WHAT MAKES THE SECOND READ WORTH MORE THAN A REPEAT, and it \
    # is the only thing in the image that separates a tree that reached 69 from \
    # a tree that reached a compiler flag. `--enable-preview` emits 69.65535, \
    # which is `major=69` by every test B-4 makes and by every test steps 09 to \
    # 12 would have made. C-4 asks for 69.0 and for `preview.classes` to be \
    # zero, and the ok24 control above cannot tell those two trees apart at all, \
    # because the scorer's own compile never passes the flag. \
    grep -qF 'fact "B4.one-major-ge-${HOLD_MAJOR}=' /tests/test.sh; \
    grep -qF 'fact "C4.level-held=' /tests/test.sh; \
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
    # PRESENT above. \
    # \
    # `0.15*a` WAS THE SIXTH ENTRY IN THIS LIST AT EVERY RUNG FROM 05 TO 12 AND \
    # IT COMES OUT HERE, which is the eleventh trap arriving in the coefficient \
    # space rather than the name space. Step 04's family C opened \
    # `0.15*a + 0.15*b`, and for eight rungs no other family started with that \
    # term, so the bare coefficient-and-position pair was a safe fingerprint for \
    # step 04's line. THIS RUNG'S OWN FAMILY C OPENS `(0.15*a + 0.57*b`. The \
    # weight is a measurement -- C-1 is the suppression budget and 0.15 is what \
    # the rehearsal put on it -- not a number chosen to dodge a ban, and the ban \
    # is what has to move. It widens to step 04's FIRST TWO TERMS TOGETHER, \
    # `0.15*a + 0.15*b`, which is present once in step 04's file and nowhere in \
    # this one. A fingerprint that survives eight rungs is still a fingerprint \
    # that has to be re-checked at the ninth. \
    # \
    # `0.15*e` is still required elsewhere and is family A's fifth term, so the \
    # same coefficient now appears in this file at two positions in two \
    # families, one banned in its step-04 pairing and two required. That is the \
    # reason this ban list works on strings long enough to name a LINE rather \
    # than on strings long enough to name a weight. \
    for dead in 'C1.lambdas' 'C2.functionalinterface' 'C3.streams' \
                'C4.requires-java8' 'requires-java8' '0.15*a + 0.15*b'; do \
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
    # ABOVE family B so that B-10 through B-19 can all read their counters, and \
    # the five held gates above family C -- B-15, B-16, B-17, B-18 and B-19 -- \
    # sit between family B's own gates and family C. Under `set -u` an un-hoist \
    # is not a wrong number, it is a dead verifier -- which is the good case. The \
    # bad case is a hoist that lands after the reads and leaves the variables at \
    # their earlier values, and only the line numbers can tell those apart. \
    # \
    # THE MEASURED GUARD IS IN TWO PLACES AT THIS RUNG AND IT HAS NEVER BEEN \
    # BEFORE, which is the one structural change this rung's family C made and \
    # the one this check exists to pin. At step 07 the last `if [ -z "${measured}"` \
    # in the file opened family C, C-1 was read above it and paid below it, and \
    # the penalty was the last statement inside. From step 08 to step 12 the last \
    # one opened B-14 -- step 07's family C, held -- and family C sat below every \
    # guard in the file, each of its legs opening its own \
    # `[ -n "${measured}" ] && [ -s "${measured}" ]` instead. Here TWO of the \
    # eight open INSIDE family C, C-1's and C-3's, so "the last guard in the file \
    # is B-14's" has stopped being a true sentence about this file. The awk below \
    # therefore counts guards on each SIDE of family C's banner rather than \
    # keeping the last one in the file, and a transcription of the rung below's \
    # `g < c` would have failed on a correct scorer. \
    # \
    # WHY THOSE TWO LEGS AND NOT THE OTHER THREE. C-1 and C-3 are the only legs \
    # here whose population is a census of the SOURCE -- suppression sites and \
    # raw declarations -- and a census taken over a report that does not exist \
    # reads zero sites, which against a `want` read off the frozen baseline is \
    # "everything departed", the ladder's seventh trap. They are spelled `[ -z ]` \
    # with an else-arm rather than `[ -n ]` with a fallthrough because each has a \
    # SENTINEL to publish -- `gate.c1=no-report` and `gate.c3=no-report` -- which \
    # the `[ -n ]` form the held gates use would leave absent, and an absent fact \
    # and a fact reading zero are the distinction this whole file is built on. \
    # C-2 reads javac's own console output, C-4 reads ${WORK}/scan.txt and C-5 \
    # reads the recorded surefire XMLs; not one of the three is a question about \
    # whether the sources parsed, which is why none of them sits in a measured \
    # guard and why K17 carries their measurements with real numbers in. \
    # \
    # `b14 < g` IS STILL ASSERTED AND STILL MEANS WHAT IT MEANT, because `g` is \
    # now the last guard ABOVE family C's banner rather than the last in the \
    # file. The penalty is still the last statement inside it and B-14's own fact \
    # is still published after it closes. \
    # \
    # The guard is anchored by its opening `if [ -z` at column zero, and the \
    # number of them is stated here rather than left implicit. Eight lines in the \
    # file carry that spelling: six above family C, one held block each, with \
    # B-14's the sixth; and family C's two. A B-14 that lost its guard would \
    # silently move the anchor up to B-13's without changing the regex, and the \
    # penalty would then appear to be inside the block it is inside -- which is \
    # why the count below is a separate assertion from the ordering. \
    test "$(grep -c '^if \[ -z ' /tests/test.sh)" = 8; \
    awk '/^# -+ the source AST, compiled once$/{s=NR} \
         /^# -+ family B$/{b=NR} /^# -+ B-10: step 03, held$/{t=NR} \
         /^# -+ B-11: step 04, held$/{u=NR} /^# -+ B-12: step 05, held$/{w=NR} \
         /^# -+ B-12e$/{b12e=NR} /^# =+ B-13$/{b13=NR} /^# =+ B-14$/{b14=NR} \
         /^# =+ B-15$/{b15=NR} /^fact "B15.jpms-services=/{fb15=NR} \
         /^# -+ bucket helpers, shared by B-16, B-17, B-18 and family C$/{bh=NR} \
         /^# =+ B-16$/{b16=NR} /^fact "B16.java17-constructs=/{fb16=NR} \
         /^# =+ B-17$/{b17=NR} /^fact "B17.java21-constructs=/{fb17=NR} \
         /^# =+ B-18$/{b18=NR} /^fact "B18.security-api-removal=/{fb18=NR} \
         /^# =+ B-19$/{b19=NR} /^fact "B19.java25-constructs=/{fb19=NR} \
         /^# =+ family C$/{c=NR} /^fact "familyC=/{fc=NR} \
         /^if \[ -z /{ if (c == 0) g = NR; else cg[++ncg] = NR } \
         /^ *fact "gate.src.penalty=/{pen=NR} \
         /^# -+ C-1$/{c1=NR} /^# -+ C-2$/{c2=NR} /^# -+ C-3$/{c3=NR} \
         /^# -+ C-4$/{c4=NR} /^# -+ C-5$/{c5=NR} \
         /^fact "B7.tests-run=/{v=NR} /^fact "B9.junit4-held=/{b9=NR} \
         /^fact "B10.java7-held=/{fb10=NR} /^fact "B11.java8lang-held=/{fb11=NR} \
         /^fact "B12.java8-library=/{fb12=NR} /^fact "B13.java9-10=/{fb13=NR} \
         /^fact "B14.java11-jakarta=/{fb14=NR} \
         /^fact "familyB=/{fb=NR} /^report /{r=NR} \
         END { if (s == 0 || b == 0 || t == 0 || u == 0 || w == 0 || b12e == 0 \
                   || b13 == 0 || fb13 == 0 || b14 == 0 || fb14 == 0 \
                   || b15 == 0 || fb15 == 0 || bh == 0 \
                   || b16 == 0 || fb16 == 0 || b17 == 0 || fb17 == 0 \
                   || b18 == 0 || fb18 == 0 \
                   || b19 == 0 || fb19 == 0 \
                   || c == 0 || fc == 0 || g == 0 || pen == 0 \
                   || c1 == 0 || c2 == 0 || c3 == 0 || c4 == 0 || c5 == 0 \
                   || v == 0 || b9 == 0 || fb10 == 0 || fb11 == 0 \
                   || fb12 == 0 || fb == 0 || r == 0) { \
                  print "scorer anchor missing: srcAST=" s " Bbanner=" b " B10=" t \
                        " B11=" u " B12=" w " B12e=" b12e " B13=" b13 \
                        " B13fact=" fb13 " B14=" b14 " B14fact=" fb14 \
                        " B15=" b15 " B15fact=" fb15 \
                        " bhelp=" bh " B16=" b16 " B16fact=" fb16 \
                        " B17=" b17 " B17fact=" fb17 \
                        " B18=" b18 " B18fact=" fb18 \
                        " B19=" b19 " B19fact=" fb19 \
                        " Cbanner=" c " C=" fc \
                        " guard=" g " Cguards=" (ncg + 0) " penalty=" pen \
                        " C1=" c1 " C2=" c2 " C3=" c3 " C4=" c4 " C5=" c5 \
                        " B-7=" v " B-9=" b9 " B10fact=" fb10 " B11fact=" fb11 \
                        " B12fact=" fb12 " B=" fb " report=" r; exit 1 } \
               if (!(s < b)) { \
                  print "the source AST is compiled after family B, so the held gates cannot read it"; exit 1 } \
               if (!(v < b9 && b9 < t && t < fb10 && fb10 < u && u < fb11 && fb11 < w \
                     && w < b12e && b12e < fb12 && fb12 < b13 && b13 < fb13 \
                     && fb13 < b14 && b14 < fb14 && fb14 < b15 && b15 < fb15 \
                     && fb15 < bh && bh < b16 && b16 < fb16 \
                     && fb16 < b17 && b17 < fb17 \
                     && fb17 < b18 && b18 < fb18 \
                     && fb18 < b19 && b19 < fb19 \
                     && fb19 < c && c < fc && fc < r)) { \
                  print "the order must be B-7, B-8/B-9, B-10, B-11, B-12 (jars last), B-13, B-14, B-15, the hoisted bucket helpers, B-16, B-17, B-18, B-19, family C, its score, the report"; exit 1 } \
               if (!(b14 < g && g < pen && pen < fb14)) { \
                  print "the penalty must be the last line inside B-14s guard and B-14 must be paid after it closes"; exit 1 } \
               if ((ncg + 0) != 2) { \
                  print "family C opens " (ncg + 0) " measured guard(s) of its own; C-1 and C-3 open one each and no other leg opens any"; exit 1 } \
               if (!(c1 < cg[1] && cg[1] < c2 && c3 < cg[2] && cg[2] < c4)) { \
                  print "family Cs two measured guards must be C-1s and C-3s, one apiece and each inside its own leg"; exit 1 } \
               if (!(c < c1 && c1 < c2 && c2 < c3 && c3 < c4 && c4 < c5 && c5 < fc)) { \
                  print "family Cs five legs must be read in order between its banner and its score"; exit 1 } \
               if (!(fc < fb)) { \
                  print "family B is summed before family C has finished scoring"; exit 1 } }' \
        /tests/test.sh; \
    # A C-5 AT THIS RUNG TOO, AND THE ASSERTION POINTS BACK THE WAY IT USED TO. \
    # The awk above required a `# --- C-5` banner for as long as family C had \
    # five legs, which was true through step 09; steps 10 and 11 had four and \
    # the requirement had to become its own negative, because a requirement that \
    # makes a correct file fail is worse than no requirement. Step 12 had five \
    # and so does this rung -- C-5 is the behaviour multiplier, read off the \
    # surefire XMLs the scored run left behind -- so the banner stays required, \
    # and what is banned is the SIXTH. `C-6` is a spelling step 03 used and no \
    # rung since has; a banner at column zero would be a leg nothing here weighs \
    # and nothing in the rehearsal scores. \
    # \
    # BOTH DIRECTIONS ARE ASSERTED BECAUSE THE PAIR IS WHAT PINS THE COUNT. The \
    # weight check far above reads exactly five numbers out of the family-C \
    # printf and five rows out of the report; this pair says the file's own \
    # section structure agrees with them. A rung that added a leg to the \
    # arithmetic and forgot its banner fails the awk; a rung that added a banner \
    # and forgot the arithmetic fails the count. \
    grep -qE '^# -+ C-5$' /tests/test.sh; \
    if grep -qE '^# -+ C-6$' /tests/test.sh; then \
        echo "test.sh carries a C-6 banner; this rungs family C has five legs" >&2; \
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
    echo "step 12: B-6 reads the same interface surface with the covariant bridge and without it, and still charges a default method the author wrote"; \
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
    echo "step 13 verifier image: assertions passed"

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
                  /tests/baseline/modimport.txt /tests/baseline/jdkimport.txt \
                  /tests/baseline/scopedvalue.txt \
                  /tests/baseline/suppress.txt /tests/baseline/rawdecl.txt \
                  /tests/runner/score.sh \
                  /tests/gates/build/arqgate.jar /tests/gates/build/arqsource.jar \
                  /tests/gates/source-gate.sh /tests/test.sh /tests/helpers.sh \
                  /tests/fixtures/j5-golden.txt /tests/fixtures/c-rehearsal.sh \
                  /tests/fixtures/b89-rehearsal.sh /tests/fixtures/c/golden/report.txt \
                  /tests/fixtures/src-rehearsal.sh /tests/fixtures/pv-rehearsal.sh \
                  /tests/fixtures/mkfix13.py /tests/fixtures/c/pinned/report.txt \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Smoke.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Lib.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Mod.java \
                  /tests/fixtures/smoke/mod/src/main/java/smoke/Raw.java; do \
        if $SP cat "${secret}" > /dev/null 2>&1; then \
            echo "LEAK: 65534 can read ${secret}" >&2; exit 1; \
        fi; \
    done; \
    for secret in /tests/assets/rec /tests/assets/rec/jdk25-step13 /tests/baseline \
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
    echo "step 13: 65534 reads the probe payload, the harness and the test shelf, and nothing else"

WORKDIR /app
