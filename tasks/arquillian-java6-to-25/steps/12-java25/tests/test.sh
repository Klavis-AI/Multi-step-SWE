#!/usr/bin/env bash
# Step 12 -- 12-java25. Scored per authoring/checklists/step12.md.
# SCORED AS A PRODUCT SINCE 2026-10-13: reward = T1 x T2, where T1 is this
# step's own migrated-ness family and T2 the preservation families; the
# weights named below are now relative weights inside those two factors, and
# any floor arithmetic below is superseded. The formula is at the foot.
#
# Three weights, two families (rule 5):
#
#   A  0.30  behaviour equivalence -- 939 recorded fixtures, replayed against
#            the artifacts the candidate's build publishes, against
#            rec/jdk25-step11: STEP 11'S ERA, REUSED UNCHANGED. No transcript
#            added, none removed, none re-recorded.
#
#            THE FIRST RUNG SINCE 05 THAT STAGES NO ERA OF ITS OWN, and it was
#            measured rather than assumed, because this rung does shorten a
#            shipped archive. C-4 turns `ManagerImpl.handledThrowables` into a
#            ScopedValue and the anonymous `ThreadLocal` subclass that field
#            declared -- `ManagerImpl$1` -- stops being emitted: impl-base goes
#            from 436 class files to 435. Step 11 learned at first hand what
#            deleting one class file does to a recording that enumerates the
#            jar it is in; thirty-seven of its transcripts had to be re-cut for
#            exactly that. It does nothing here, and the reason is which jar:
#            the 36 `servlet/pack*` and `servlet/append*` recordings enumerate
#            arquillian-protocol.jar, and no recording in this era enumerates
#            impl-base's. Step 11's verifier, run unchanged over this rung's
#            golden, reads behavior.expected=939 produced=939 match=939,
#            familyA=1.000000. NOTES 16.61.
#
#            So family A is back to the shape every rung before 11 had: a tree
#            that does the migration correctly changes no recorded outcome, and
#            family A reads 1.000000 both before and after. It is not a free
#            0.30 -- C-4 has a wrong shape that family A and nothing else
#            charges for, and the C-4 block below says which -- it is 0.30 that
#            this rung's work must not spend.
#
#   B  0.70 x 0.25  everything steps 01 to 11 established, re-measured, in
#            EIGHTEEN gates. Seven inherited from step 01, two that hold step
#            02's JUnit 5 migration in place, B-10 for step 03's three Java 7
#            constructs, B-11 for step 04's three Java 8 language constructs,
#            B-12 -- step 05's whole family C -- for the Java 8 library, B-13 --
#            step 06's family C minus its C-1 -- for Java 9/10, B-14 -- step
#            07's family C minus its C-1, penalty term included -- for Java 11
#            and the jakarta namespace, B-15 -- step 08's family C minus its
#            C-1 -- for the module names and the platform service lookup,
#            B-16 -- step 09's family C minus its C-1 -- for pattern
#            `instanceof`, the text block, the sealed hierarchies and the arrow
#            switches, B-17 -- step 10's family C minus its C-1 -- for the type
#            patterns, the pattern switches and the sequenced accessors, and
#            B-18 -- step 11's family C minus its C-1 -- for the security API
#            that left, the unnamed catch parameters and the four VarHandles.
#
#   C  0.70 x 0.75  the tree becomes a Java 25 tree: it reaches the last rung
#            of the ladder, says what it imports in the units the platform is
#            actually shipped in, moves the one piece of per-thread state whose
#            lifetime is an extent, and asks the JVM for the header layout 25
#            made final. Five gates:
#
#              C-1  0.02  every class file the build emits is major 69 AND
#                         minor 0, and there is exactly one major.
#              C-2  0.38  the 495 single-type imports of JDK types in 171 files
#                         become 188 `import module` declarations, scored as
#                         171 whole-set equalities; min()'d against the 151
#                         files in scope that import no JDK type at all and
#                         must not acquire one.
#              C-3  0.33  452 of those 495 lines go and 43 STAY -- 16 static
#                         imports, 2 nested types and 25 `Annotation`s a module
#                         import leaves ambiguous; min()'d so that deleting all
#                         495 is worth nothing.
#              C-4  0.25  one of the tree's 8 ThreadLocal fields becomes a
#                         ScopedValue and the other seven do not; three halves,
#                         so a tree that converts all eight loses the leg.
#              C-5  0.02  the test JVMs this build forks are started with
#                         `-XX:+UseCompactObjectHeaders`, read out of the
#                         argument list Gradle would pass rather than out of a
#                         build file.
#
# AND THE FLOOR UNDER THOSE WEIGHTS IS THE TIGHTEST IN THE LADDER: 0.475, WITH
# 0.025 OF ROOM. It is measured and not estimated. The tree this rung hands the
# candidate is step 11's golden, and step 11's verifier scores that tree
# reward=1.000000 -- so family A reads 1.000000 here and so does every one of
# family B's eighteen gates, B-18 included, because B-18 IS step 11's C-2, C-3
# and C-4 and B-4 holds the major the seed already carries. A tree that is
# handed to the candidate and touched not at all therefore collects
# 0.30 + 0.70*0.25 = 0.475, and every earlier rung's floor was lower only
# because something in its family B still read short on its own input (step
# 11's was 0.463128, its family B 0.997882 on B-6's 42 departing signatures).
#
# What that buys is a hard ceiling on the family C weight a do-nothing tree can
# collect: 0.475 + 0.525*w < 0.5 gives w < 0.047619. AND w IS A SUM OVER EVERY
# LEG SUCH A TREE REACHES, NOT THE LARGEST OF THEM. Two legs here are one line
# of build script each -- C-1 flips `sourceCompatibility` in
# `gradle/java-module.gradle` and the platform's own migrator reaches it
# outright; C-5 adds `jvmArgs` to the shared `Test` configuration -- and a tree
# that writes both and nothing else collects both. At 0.04 apiece that pair was
# 0.08, which is 0.475 + 0.525*0.08 = 0.517 and OVER item 6's bar. The first
# draft of this header priced the legs one at a time and concluded C-1 was "the
# only leg of this family such a tree collects"; it was not, it was the only
# leg the sentence counted, and C-5 sat beside it at the same price for the
# same kind of work. C-1 and C-5 are 0.02 each for that reason. The pair sums
# to 0.04 and lands the two-flag tree at 0.475 + 0.525*0.04 = 0.496, under the
# bar by four thousandths -- the same margin the one-flag tree used to be given,
# now measured over the whole set of free legs instead of over one of them.
# `levelonly`, which flips the level alone, lands at 0.475 + 0.525*0.02 =
# 0.485500.
#
# THE LEVEL IS THE MUST-FLIP AGAIN, which is why C-1 owns it and B-4 is
# forbidden to read it. The split steps 05, 06, 07, 09, 10 and 11 used: C-1
# asserts 69, B-4 asserts `one major, not below 66`, the two numbers are STAGED
# SIDE BY SIDE in baseline/level.txt as `major=69` and `was=66`, and neither is
# a literal in this file. A tree that retargets to 69 collects C-1 once, not
# twice.
#
# AND THIS RUNG ADDS A THIRD NUMBER TO THAT FILE: `minor=0`. From here up the
# target is a release WITH PREVIEW FEATURES IN IT that a candidate has every
# reason to reach for. A tree compiled behind `--enable-preview` stamps major 69
# and MINOR 65535 on every class it emits (JVMS 4.1), a major-only reading
# cannot tell it from a correct one, and it is not a hypothetical: two of the
# three language and runtime features this rung uses were preview in a release
# before 25, every tutorial written before they went final turns the flag on,
# and a tree that leaves it on has not migrated to 25 -- it has migrated to a 25
# that will refuse to load its own class files on the next JVM. C-1 reads both
# halves of the version and says which one failed.
#
# WHAT `Java 25` MEANS HERE AND WHAT IT DOES NOT. The four scored changes are
# the four this tree has the shape for, and each is a POPULATION FROZEN IN
# baseline/ with at least two halves, because each has a blanket answer that
# compiles:
#
#   * MODULE IMPORT DECLARATIONS (JEP 511, final in 25). 495 single-type
#     imports of JDK types across 171 main and test files, out of SIX modules:
#     java.base (167 files), java.naming (8), java.logging (7),
#     java.management (4), java.xml (1) and java.net.http (1) -- 188
#     declarations, because 19 files name more than java.base and 4 name no
#     java.base type at all. `import module M;` is a type-import-on-demand over
#     the EXPORTED PACKAGES of exactly M, so the right answer is per file and
#     cannot be computed from the package prefix: `java.util.List` is java.base
#     and `java.util.logging.Logger` is java.logging. C-2 compares each file's
#     SET, whole; a file with a right module and a missing one scores nothing.
#
#   * AND THE 495 LINES ACTUALLY GO -- 452 OF THEM. A module import subsumes a
#     single-type import of a type in an exported package of that module, and
#     nothing else. 43 must survive it and they are C-3: 16 STATIC imports,
#     which are not type imports at all; 2 NESTED types, imported through an
#     outer class a type-import-on-demand does not walk into; and 25 that are
#     AMBIGUOUS -- every one of them `java.lang.annotation.Annotation`, because
#     JEP 484 put `java.lang.classfile.Annotation` into java.base in 24 and the
#     simple name now resolves to two exported types (JLS 6.5.5). The 25 the
#     compiler teaches, by refusing to build. The other 18 it does not: a
#     static import and a nested-type import compile identically whether the
#     module import is there or not, and both are deleted by any rule that
#     reads "the module import replaces these".
#
#   * SCOPED VALUES (JEP 506, final in 25). 8 ThreadLocal fields in main
#     sources. ONE has the shape: `ManagerImpl.handledThrowables` is bound for
#     the extent of a `fire` and read inside it, which is what
#     `ScopedValue.where(..).run(..)` expresses and what a ThreadLocal
#     expresses only by convention plus a `finally`. The other SEVEN are not,
#     for six different reasons scopedvalue.txt names on each row, and a
#     ScopedValue is immutable inside its extent and invisible outside it, so
#     a field that is assigned after binding or read outside the call that
#     bound it cannot become one. "It is a ThreadLocal, so make it a
#     ScopedValue" is the wrong reading of this construct and this leg prices
#     it at zero.
#     AND THE ONE THAT CONVERTS HAS A WRONG SHAPE THAT ONLY FAMILY A SEES:
#     `fire` re-enters on every `Event.fire` an observer makes and CLEARS the
#     shared set at entry, so binding a fresh set per entry hides an inner
#     handler's record from the outer catch and re-fires an exception this
#     tree's own comment says must be re-thrown. Bind once at the outermost
#     entry. NOTES 16.61.
#
#   * COMPACT OBJECT HEADERS (JEP 519, final in 25). One flag, on the forked
#     test JVM of all twelve projects that have one. It is worth 0.02 -- the
#     same as C-1, because those two are the pair a do-nothing tree reaches
#     with one build-script line each and the header's 0.047619 ceiling is on
#     their SUM -- and it is here for coverage: three of the other four legs
#     read source and the fourth reads class files, and a step called "Java 25"
#     that never asks what the JVM was actually started with would be
#     measuring half the release. What makes it a real reading rather than a
#     grep is WHERE it is taken -- a separate, bounded `arqTestJvmArgs`
#     invocation asks Gradle what it would pass to each `Test` task, after the
#     scored build, because a build file is a claim and the argument list is
#     what the fork gets.
#
# WHY NOT STRUCTURED CONCURRENCY, NOT A PRIMITIVE PATTERN, NOT AN INSTANCE MAIN,
# NOT A FLEXIBLE CONSTRUCTOR BODY, NOT A GATHERER. Structured concurrency is
# still preview at 25 (JEP 505, fifth preview) and so are primitive types in
# patterns (JEP 507); this rung's C-1 exists to REFUSE a tree compiled with
# `--enable-preview`, so no leg of it may need one. Instance main methods (JEP
# 512) have nothing to land on: there is not one `static void main` anywhere in
# this tree's main sources. Flexible constructor bodies (JEP 513) have exactly
# ONE site -- `TestEvent`'s `super(validateAndExtractClass(testInstance,
# testMethod))`, a private static helper that exists only to run two
# `Validate.notNull` calls before the super call -- and a population of one is a
# coin flip, not a leg. Stream gatherers went final in 24, not 25, and no stream
# in this tree has a custom intermediate operation. sun.misc.Unsafe is not used
# anywhere, so java.lang.foreign has nothing to replace, and the four `Thread`
# uses are the JMX test runner's own, where a platform thread is what they mean.
#
# Why family B is eighteen gates and not eleven inherited families. Step 02
# carried its predecessor forward as family B and scored its own work as family
# C; done again at every rung, this verifier would carry eleven families. What
# has to hold from step 02 is that the suite is still a Jupiter suite and still
# free of JUnit 4 -- B-8 and B-9. What has to hold from step 03 is B-10, from
# step 04 B-11, from step 05 B-12, from step 06 B-13, from step 07 B-14, from
# step 08 B-15, from step 09 B-16, from step 10 B-17 and from step 11 B-18,
# each one gate of sub-gates read against that rung's own frozen sets. Logged in
# NOTES 9.2, 10.4, 13.10, 14.5, 15.5, 16.14 and 16.61.
#
# The ninth trap is what B-10 through B-18 are for. Every gate above them is
# step-local, and a step-local mean cannot notice a tree that earns this rung by
# undoing the one below it.
#
# AND AT THIS RUNG THE TRAP WAS ANSWERED IN THE GOLDEN RATHER THAN IN THE
# BASELINE, WHICH IS THE OPPOSITE OF WHAT STEP 11 DID. Step 11 re-cut two
# inherited populations because its own work deleted buckets they named. Nothing
# is re-cut here: every inherited denominator in baseline/target.txt is step
# 11's, copied across unchanged, and step 11's verifier over this rung's golden
# reads 1.000000 on all seventeen of its gates and on its C-2, C-3 and C-4.
# That was not free. The obvious way to write C-4 splits `fire` into a binder
# and a `fireBound` body, and three inherited legs key their populations by
# `<file>#<method>` with all three of their sites inside `fire`: step 03's
# diamond, step 09's pattern `instanceof` and step 11's unnamed catch parameter
# all read the move as a loss (0.985915, 6 of 7, 0.980392). Re-cutting would
# have worked and would have been wrong, because it would ship a fixture
# asserting that a method STEP 12 INVENTED is where step 09's `instanceof`
# lives, and would then fail any candidate who decomposed the conversion
# differently. So the golden binds by re-entering `fire` itself, with a diamond
# rather than an explicit type argument -- `ScopedValue.where(..)` target-types
# it -- and perturbs no inherited population at all. NOTES 16.61.
#
# AND THE COST OF SPLITTING ANYWAY IS PRICED HERE RATHER THAN LEFT AS "A LOSS",
# because a candidate is entitled to decompose the conversion differently and
# this is the file where the size of that is on record. B-10 reads 0.985915 at
# weight 0.04495, B-16 0.968254 at 0.09293 and B-18 0.994690 at 0.12000, which
# carries family B to 0.995780 -- a reward cost of 0.70 x 0.25 x 0.004220 =
# 0.000739. A CORRECT TREE THAT SPLITS `fire` SCORES 0.999261, NOT 1.000000.
# (0.995780 and not 0.995779: the loss is 0.0042204738, and an earlier draft
# rounded it to 0.004221 and THEN subtracted, which is a digit the scorer's own
# `%.6f` over the unrounded composition does not produce. The Dockerfile check
# that recomputes this from the eighteen shipped weights is what said so.)
#
# THAT IS ACCEPTED AND DELIBERATELY NOT REPAIRED, which is a choice and not an
# oversight. The instruction cannot warn about it without naming the scoring
# key -- "do not extract a helper" tells a candidate that methods are keys --
# and re-cutting the three populations onto `fireBound` would trade a 0.0007
# nick on ONE decomposition for a fixture asserting that step 09's `instanceof`
# lives in a method step 12 invented, which would then charge every candidate
# who decomposed it some third way. A bounded, measured cost on one correct
# shape is the cheaper error, and it is bounded because all three legs are
# hold-style: a split moves the site, it does not delete it, so nothing
# cascades into family A or the C legs this rung is actually about.
#
# Every gate is defensive: a gate that cannot run scores 0 and says why, and
# the script always reaches report() (rule 8).
set -u
source /tests/helpers.sh

harden_reward_dir

step_id=$(cat /tests/STEP_ID)
echo "VERIFIER-STEP=${step_id}"
: > "/logs/verifier/${step_id}.ran"

TREE=/app/arquillian
M2=/tmp/.m2/repository
# The class-file version this rung is about, ALL THREE FIGURES STAGED, none of
# them a literal.
#
#   WANT_MAJOR  69, i.e. Java 25 -- the level C-1 asserts. This rung's
#               must-flip in the build file.
#   WANT_MINOR  0 -- the minor C-1 asserts alongside it, which is how a class
#               file says it was compiled WITHOUT `--enable-preview`.
#   HOLD_MAJOR  66, i.e. Java 22 -- the level the tree arrives at, which B-4
#               asserts nothing was taken back below.
#
# TWO NUMBERS BECAUSE THE LEVEL MOVES AGAIN. At steps 05, 06, 07, 09, 10 and 11
# the level was a must-flip, family C's first leg owned it, and B-4 was
# forbidden to look at baseline/level.txt at all -- at step 07 it held a literal
# one rung behind, so that one flip was not scored twice. Step 08 moved no
# level, so its family C had no level leg, there was nothing to collide with,
# and B-4 read the staged number directly.
#
# Here the flip is back and so is the split, and still WITHOUT step 07's
# literal. Both numbers are staged, in the same file, as `major=` and `was=`, so
# the floor and the flip move together when a later rung stages a new pair and
# neither can go quietly stale in a shell script. mk-baseline-step12.sh writes
# all three from the same measurement.
#
# AND THE THIRD FIGURE IS THIS RUNG'S ADDITION, FOR THE REASON THE HEADER GIVES:
# 25 is the first target on this ladder whose headline features were preview in
# a release the candidate may well be reading about, `--enable-preview` is one
# line in the same build file the release flag lives in, and a tree that turns
# it on emits major 69 with MINOR 65535 on every class. A gate that read only
# the major would call that tree migrated. B-4 does not read the minor -- a
# held floor is a statement about the major alone, and charging the preview flag
# on two legs would price one mistake twice.
#
# AND AT THIS RUNG THE LEVEL IS STILL TWO NUMBERS IN THE BUILD FILE, NOT ONE.
# `options.release` is the obvious one. The other is the explicit
# `TargetJvmVersion.TARGET_JVM_VERSION_ATTRIBUTE` step 01 pinned on four
# resolvable classpaths, in a different part of the script. Move one and not the
# other and a 16-project reactor stops resolving -- the producer advertises 25,
# the consumer asks for 22 -- so the build fails outright and family A reads 0
# with it. The gate does not grep for either: C-1 reads the class files the
# build actually emitted, because the step must not dictate whether the
# candidate moves the daemon to 25 or declares a toolchain and leaves it where
# it is. NOTES 16.5, 16.6 carry the two spellings; 16.61 carries the golden's
# 435 class files, all at 69.0.
#
# 69 AND 66 ARE THE PAIR THIS RUNG STAGES AND NEITHER APPEARS BELOW. B-18
# inherits step 11's family C without its C-1 for exactly this reason: the level
# leg that rung owned is now B-4's held number, and scoring it in both places
# would charge one flip twice.
#
# A missing or implausible value must not silently become "0" and pass a tree
# with no class files at all, so all three are validated on the spot.
WANT_MAJOR=$(sed -n 's/^major=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
WANT_MINOR=$(sed -n 's/^minor=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
HOLD_MAJOR=$(sed -n 's/^was=//p'   /tests/baseline/level.txt 2>/dev/null | head -1)
case "${WANT_MAJOR:-}" in
   5[0-9]|6[0-9]) ;;
   *) echo "FATAL: baseline/level.txt names no plausible class-file major (${WANT_MAJOR:-missing})"
      report 0 ;;
esac
case "${HOLD_MAJOR:-}" in
   5[0-9]|6[0-9]) ;;
   *) echo "FATAL: baseline/level.txt names no plausible held major (${HOLD_MAJOR:-missing})"
      report 0 ;;
esac
# 0 and 65535 are the only minors the JVM spec gives a meaning to (JVMS 4.1),
# and a step whose C-1 asserts the preview minor would be asserting the opposite
# of what this rung wants, so the staged value is required to be 0.
case "${WANT_MINOR:-}" in
   0) ;;
   *) echo "FATAL: baseline/level.txt names no usable class-file minor (${WANT_MINOR:-missing})"
      report 0 ;;
esac
[ "${HOLD_MAJOR}" -le "${WANT_MAJOR}" ] || {
   echo "FATAL: baseline/level.txt holds ${HOLD_MAJOR} above the ${WANT_MAJOR} it asks for"
   report 0
}
GCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"
WORK=/tmp/verify
rm -rf "${WORK}"; mkdir -p "${WORK}"

# The classpath the SCORER compiles the candidate's sources against, for the
# source gate that B-10 through B-15 and family C's three API legs read -- C-1
# reads the class files the build emitted and C-4 reads family A's summary
# besides, so family C is not a reading of the source full stop. Pinned to the
# staged shelf and never derived from the candidate's build file: a classpath
# a candidate can shorten is a compile a candidate can make fail, and both
# families read a failed compile as a fact about the tree. 78 dependency jars
# plus the JUnit Platform the migrated tests need -- 79 on javac's command
# line, which is what javac.cpjars prints back.
SRCCP=$( { ls /tests/assets/testlib/*.jar; ls /tests/assets/harness/*.jar; } 2>/dev/null \
         | tr '\n' ':' )

# The library's module directories, the one list srcgate() and srcbind() both
# read (wave H round 4, G2-A1): the baseline's twelve published modules, whose
# <module>/src/main/java and <module>/src/test/java requirement 1 keeps the
# library's sources in. Family C compiles those and nothing else; build logic
# is not part of the library. fixtures/src-rehearsal.sh lifts srcgate() without
# this line, so its smoke tree is compiled whole, as it always was.
SRCMODS="api impl-base junit protocols/jmx protocols/jmx-osgi protocols/servlet spi"
SRCMODS="${SRCMODS} testenrichers/cdi testenrichers/ejb testenrichers/osgi testenrichers/resource testng"

# srcgate <tag> [<source level>] -- one scorer-owned compile of the library
# with arqgate.SourceScan attached, writing ${WORK}/src-<tag>.txt and its .env,
# and echoing that run's source.ok.
#
# Defined here rather than inside the block that calls it because
# fixtures/c-rehearsal.sh replaces exactly this function to feed the scoring
# blocks a tree whose answer is known, and fixtures/src-rehearsal.sh lifts it
# out of this file by `sed` and drives it against the smoke tree. It is the only
# thing in either block that touches the tree.
srcgate() {
   local tag=$1 lvl=${2:-}
   GATE_GATES=/tests/gates GATE_SOURCE="${lvl}" GATE_MODULES="${SRCMODS:-}" \
      bash /tests/gates/source-gate.sh "${TREE}" "${WORK}/src-${tag}.txt" "${SRCCP}" \
      > "${WORK}/src-${tag}.log" 2>&1 || true
   # Bound to what the build published, or refused (wave D, G14; srcbind).
   [ ! -f "${WORK}/bind/state" ] || srcbind "${tag}"
   sed -n 's/^source\.ok=//p' "${WORK}/src-${tag}.txt.env" 2>/dev/null | head -1
}

# srcbind <tag> -- the binding of the scorer's compile to the shipped artifacts
# (wave D, G14), run by srcgate() after each compile against the snapshot taken
# right after family A. The first compile that succeeds decides, and its verdict
# holds for every compile after it. Bound: the reports stand as they are.
# Unbound: that compile and every later one are refused -- no report, no javac
# log, an .env that says source.ok=no -- so every source-derived leg reads the
# tree the way it reads a tree that produced no report. Nothing on stdout,
# which srcgate() echoes; the facts go straight to the facts file.
srcbind() {
   local tag=$1 rep st okv nsrc nmiss nextra nunship nout mods
   rep="${WORK}/src-${tag}.txt"
   st=$(head -1 "${WORK}/bind/state" 2>/dev/null)
   [ "${st}" != yes ] || return 0
   if [ "${st}" = pending ]; then
      okv=$(sed -n 's/^source\.ok=//p' "${rep}.env" 2>/dev/null | head -1)
      [ "${okv}" = yes ] || return 0
      # What is bound is the library: the twelve published modules' main
      # sources, which requirement 1 keeps where they are today, under
      # <module>/src/main/java. A main source anywhere else -- Java build logic
      # in an included build such as build-logic/src/main/java, which Gradle
      # compiles as a build of its own and never publishes -- is bound only when
      # a class compiled from it ships in a module jar. (Wave H, G-A1: binding
      # every compiled main source refused a correct tree that kept a plain Java
      # helper beside its convention plugin, and zeroed family C.) Since wave H
      # round 4 the compile itself covers only these dirs (SRCMODS), so no
      # other main source reaches this list.
      mods="${SRCMODS}"
      grep -F '/src/main/java/' "${rep}.sources" 2>/dev/null | tr '\n' '\0' \
         | xargs -0 -r sha256sum 2>/dev/null \
         | awk -v root="$(cd "${TREE}" 2>/dev/null && pwd)" -v mods="${mods}" '
              BEGIN { n = split(mods, m, " "); for (k = 1; k <= n; k++) lib[root "/" m[k]] = 1 }
              { p = substr($0, 67); i = index(p, "/src/main/java/")
                if (i > 0) print ((substr(p, 1, i - 1) in lib) ? "L " : "O ") $1 " " substr(p, i + 15) }' \
         | LC_ALL=C sort > "${WORK}/bind/treeall-${tag}.txt"
      awk 'FILENAME == ARGV[1] { c = $0; sub(/\.class$/, "", c); n = split(c, a, "/"); sub(/\$.*$/, "", a[n])
                                 s = a[1]; for (k = 2; k <= n; k++) s = s "/" a[k]
                                 ship[s] = 1; next }
           { r = substr($0, 3); p = substr(r, index(r, " ") + 1); sub(/\.java$/, "", p)
             if (substr($0, 1, 1) == "L" || (p in ship)) print r }' \
         "${WORK}/bind/jarclasses.txt" "${WORK}/bind/treeall-${tag}.txt" \
         | LC_ALL=C sort > "${WORK}/bind/tree-${tag}.txt"
      nout=$(( $(grep -c . "${WORK}/bind/treeall-${tag}.txt") - $(grep -c . "${WORK}/bind/tree-${tag}.txt") ))
      grep -v '\(^\|/\)module-info\.class$' "${rep}.classes" 2>/dev/null \
         | LC_ALL=C sort -u > "${WORK}/bind/compiled-${tag}.txt"
      awk 'NR == FNR { p = substr($0, index($0, " ") + 1); sub(/\.java$/, "", p); top[p] = 1; next }
           { c = $0; sub(/\.class$/, "", c); n = split(c, a, "/"); sub(/\$.*$/, "", a[n])
             s = a[1]; for (k = 2; k <= n; k++) s = s "/" a[k]
             if (s in top) print $0 }' \
         "${WORK}/bind/tree-${tag}.txt" "${WORK}/bind/compiled-${tag}.txt" \
         | LC_ALL=C sort -u > "${WORK}/bind/compiledmain-${tag}.txt"
      nsrc=$(grep -c . "${WORK}/bind/tree-${tag}.txt")
      nmiss=$(LC_ALL=C comm -23 "${WORK}/bind/tree-${tag}.txt" "${WORK}/bind/jarsources.txt" | grep -c .)
      nextra=$(LC_ALL=C comm -23 "${WORK}/bind/jarclasses.txt" "${WORK}/bind/compiled-${tag}.txt" | grep -c .)
      nunship=$(LC_ALL=C comm -23 "${WORK}/bind/compiledmain-${tag}.txt" "${WORK}/bind/jarclasses.txt" | grep -c .)
      st=no
      [ "${nsrc:-0}" -gt 0 ] && [ "${nmiss}" = 0 ] && [ "${nextra}" = 0 ] && [ "${nunship}" = 0 ] && st=yes
      echo "${st}" > "${WORK}/bind/state"
      { echo "gate.bind.tag=${tag}"
        echo "gate.bind.mainsources=${nsrc:-0}"
        echo "gate.bind.mainsources.outside=${nout:-0}"
        echo "gate.bind.sources.unshipped=${nmiss}"
        echo "gate.bind.classes.uncompiled=${nextra}"
        echo "gate.bind.classes.unshipped=${nunship}"
        echo "gate.bind=${st}"; } >> "${FACTS}"
      echo "    bind (${tag}): ${nsrc:-0} main source(s) (${nout:-0} outside the modules, unbound), ${nmiss} not shipped as source," \
           "${nextra} shipped class(es) this compile did not produce," \
           "${nunship} produced from main and not shipped -> ${st}" >&2
      [ "${st}" != yes ] || return 0
      { LC_ALL=C comm -23 "${WORK}/bind/tree-${tag}.txt" "${WORK}/bind/jarsources.txt" | head -5 \
           | sed 's/^/    source not in a -sources jar: /'
        LC_ALL=C comm -23 "${WORK}/bind/jarclasses.txt" "${WORK}/bind/compiled-${tag}.txt" | head -5 \
           | sed 's/^/    shipped class the scorer compile did not produce: /'
        LC_ALL=C comm -23 "${WORK}/bind/compiledmain-${tag}.txt" "${WORK}/bind/jarclasses.txt" | head -5 \
           | sed 's/^/    main class not in a module jar: /'; } >&2
   fi
   rm -f "${rep}" "${rep}.javac.log"
   printf 'source.ok=no\nbind.ok=no\n' > "${rep}.env"
}

# Every number the families produce, one per line, so the trajectory analysis
# and the recovery packages read the same record the score was computed from.
FACTS="${WORK}/facts.txt"
: > "${FACTS}"
fact() { echo "$*" >> "${FACTS}"; echo "  $*"; }

# frac <num> <den> -- 0 when the denominator is 0, so a gate that measured
# nothing scores nothing rather than dividing by zero and vanishing.
frac() { awk -v n="${1:-0}" -v d="${2:-0}" 'BEGIN { printf "%.6f", (d + 0 <= 0) ? 0 : (n + 0) / (d + 0) }'; }
clamp01() { awk -v v="${1:-0}" 'BEGIN { v += 0; if (v < 0) v = 0; if (v > 1) v = 1; printf "%.6f", v }'; }

# minf <a> <b> -- the smaller of two scores. Used wherever a gate has an
# arrival half and a departure half: a tree that only departs (deletes the
# code) and a tree that only arrives (adds new code alongside the old) each
# score 0, and only a tree that did both scores.
#
# Defined up here rather than at the head of family C, where step 03 had it,
# because B-10 is the same shape and is scored before family C begins.
minf() { awk -v a="${1:-0}" -v b="${2:-0}" 'BEGIN { printf "%.6f", (a + 0 < b + 0) ? a : b }'; }

# platform_verdict <reports-dir> <stdout capture> <exit status> <class> <headers file>
#
# One corroboration run of one inherited test class under the JUnit Platform,
# classified from two channels that have to agree: the ConsoleLauncher summary
# the run printed, and the XML the platform's own listener wrote into a
# directory the SCORER chose.
#
# Step 01 had one channel -- JUnitCore's last line -- and layered
# arqgate.SelfStop underneath it, because a test that ends the JVM can print
# whatever tail it likes first. Measured here, on this harness: a class whose
# single case prints
#
#     Test run finished after 1 ms
#     [         1 tests found           ]
#     [         0 tests failed          ]
#
# and then calls Runtime.halt(0) exits 0 and satisfies a tail-of-stdout parse
# completely. That is step 01's ctlD7 wearing this harness's costume.
#
# --reports-dir is what makes it cheap to refuse. The engine's XML is flushed
# after the engine finishes, so a JVM that leaves early never writes it: the
# halt above produces TEST-junit-platform-suite.xml and no TEST-junit-jupiter.xml
# at all, while every honest run writes both. So the verdict requires a
# <testcase> for this class in an ENGINE report, and requires its count to
# agree with the summary the run printed. SelfStop still runs underneath, for
# the tree that writes the reports and then halts.
#
# Echoes one word, and a case count for the outcomes that carry one:
#   pass <n>      every case green, on both channels, and the JVM agreed
#   fail <n>      at least one case red; <headers file> names them
#   classfail <n> a container failed -- @BeforeAll or a class-level extension
#   notloadable   discovery found nothing to run here: rc=2. A class with no
#                 runnable methods, or one this tree no longer has.
#   fault         the JVM died before the harness printed its own banner
#   exited        a verdict was expected and is not there, or the two channels
#                 disagree. The only code between the banner and the summary is
#                 the inherited test's own.
platform_verdict() {
   local rd=$1 out=$2 rc=$3 fq=$4 heads=$5
   local banner=no block=no fault=no
   local found=0 failed=0 cfailed=0 skipped=0
   local xcases=0 xfail=0

   : > "${heads}"

   # ConsoleLauncher prints this before it resolves anything named on its
   # command line -- measured against a class that does not exist, where it is
   # still the first line of the capture. The harness comes off the scorer's
   # own jar, first on the system classpath, so nothing of the candidate's has
   # run when it appears. Before it, a crash is the scorer's to own; after it,
   # every byte has passed through code this tree compiled.
   grep -q '^Thanks for using JUnit!' "${out}" && banner=yes
   if [ "${banner}" = no ] \
      && { [ "${rc}" -ge 128 ] \
           || grep -qE 'OutOfMemoryError|A fatal error has been detected|Could not find or load main class|Error occurred during initialization of VM|Could not create the Java Virtual Machine|Could not reserve enough space' "${out}"; }; then
      fault=yes
   fi

   # The whole twelve-line counter block, in the platform's own order, or
   # nothing. Reading single lines with `tail -1` is what the halt above
   # defeats: it printed three of the twelve and satisfied every one of them.
   # Twelve consecutive well-formed lines is not a tail a test writes by
   # accident, and a test that does write all twelve still has to get past the
   # engine report below and SelfStop above.
   local tailblock
   tailblock=$(awk '/^Test run finished after /{ n = 0; delete b }
                    /^\[ *[0-9]+ (containers|tests) (found|skipped|started|aborted|successful|failed) *\]$/ {
                       b[++n] = $0
                       if (n == 12) { for (i = 1; i <= 12; i++) print b[i]; n = 0; delete b }
                    }' "${out}" | tail -12)
   if [ "$(printf '%s\n' "${tailblock}" | grep -c .)" -eq 12 ]; then
      block=yes
      found=$(printf '%s\n'   "${tailblock}" | sed -n 's/^\[ *\([0-9]*\) tests found .*/\1/p'      | tail -1)
      skipped=$(printf '%s\n' "${tailblock}" | sed -n 's/^\[ *\([0-9]*\) tests skipped .*/\1/p'    | tail -1)
      failed=$(printf '%s\n'  "${tailblock}" | sed -n 's/^\[ *\([0-9]*\) tests failed .*/\1/p'     | tail -1)
      cfailed=$(printf '%s\n' "${tailblock}" | sed -n 's/^\[ *\([0-9]*\) containers failed .*/\1/p' | tail -1)
   fi

   # The scorer's own copy of the run, written by the platform rather than by
   # the process's stdout. TEST-junit-platform-suite.xml is excluded on
   # purpose: it is written by an engine that ran nothing here, and the halt
   # above produces it and only it.
   local x
   for x in "${rd}/TEST-junit-jupiter.xml" "${rd}/TEST-junit-vintage.xml"; do
      [ -f "${x}" ] || continue
      tr '\n' ' ' < "${x}" | sed 's|<testcase |\n<testcase |g' | tail -n +2
      echo
   done > "${rd}/.cases" 2>/dev/null
   if [ -s "${rd}/.cases" ]; then
      xcases=$(awk -v c="${fq}" '
         { cls = ""; nm = ""
           if (match($0, /classname="[^"]*"/)) cls = substr($0, RSTART + 11, RLENGTH - 12)
           if (match($0, /[ ]name="[^"]*"/))   nm  = substr($0, RSTART + 7,  RLENGTH - 8)
           if (cls == "" || nm == "") next
           if (cls != c && index(cls, c "$") != 1) next
           n++ }
         END { print n + 0 }' "${rd}/.cases")
      # Jupiter writes the method name with its parentheses and vintage does
      # not, so the id is normalised here exactly as B-7 normalises the
      # candidate's own reports -- one rule, applied to both channels.
      awk -v c="${fq}" '
         { cls = ""; nm = ""
           if (match($0, /classname="[^"]*"/)) cls = substr($0, RSTART + 11, RLENGTH - 12)
           if (match($0, /[ ]name="[^"]*"/))   nm  = substr($0, RSTART + 7,  RLENGTH - 8)
           if (cls == "" || nm == "") next
           if (cls != c && index(cls, c "$") != 1) next
           if (index($0, "<failure") == 0 && index($0, "<error") == 0) next
           sub(/\(\)$/, "", nm)
           print cls "#" nm }' "${rd}/.cases" | LC_ALL=C sort -u > "${heads}"
      xfail=$(grep -c . "${heads}" || true)
   fi

   if [ "${fault}" = yes ]; then echo fault; return; fi
   # rc=2 is ConsoleLauncher's documented "nothing was found to run", and with
   # --fail-if-no-tests it is also what a class that will not load reports. The
   # caller decides whether that is this tree's doing, by asking whether the
   # frozen 2011 run recorded a case for the class.
   if [ "${rc}" -eq 2 ]; then echo notloadable; return; fi
   if [ "${block}" = no ] || [ "${xcases}" -eq 0 ]; then echo exited; return; fi
   # Two channels, one number. A summary that says three cases ran while the
   # platform's own report carries one is a summary somebody wrote.
   #
   # `found` and not `found - skipped`: the platform writes a skipped case into
   # the XML as a <testcase> with a <skipped/> child, so both channels count it.
   # Subtracting here instead scored a class with one @Disabled method as a
   # disagreement and took a perfectly green PassCase out of the comparison --
   # measured, on the fixture table below.
   if [ "${found:-0}" -ne "${xcases}" ]; then echo exited; return; fi
   # What actually ran, which is what the caller adds up: a skipped case is not
   # a corroborated one.
   local ran=$(( found - skipped ))
   if [ "${cfailed:-0}" -gt 0 ]; then echo "classfail ${ran}"; return; fi
   if [ "${failed:-0}" -gt 0 ] || [ "${xfail}" -gt 0 ]; then
      # Either channel claiming a failure is a failure: the point of reading
      # both is that neither gets to be the only witness.
      echo "fail ${ran}"; return
   fi
   if [ "${rc}" -ne 0 ]; then echo exited; return; fi
   echo "pass ${ran}"
}

# The green half of the parse platform_verdict does for the red one, over the
# same `.cases` file that verdict was read from: the ids this run of this class
# EXECUTED and did not fail, in the same `<class>#<method>` form and under the
# same trailing-`()` normalisation B-7 applies to the candidate's own reports.
# One identity rule, three readers.
#
# A skipped case is not a passed one. The platform writes it as a <testcase>
# with a <skipped/> child, so without the third test below an @Disabled method
# would read as green here and excuse a reported failure for a test that did not
# run -- which is the one shape B-5 is separately there to charge.
corrob_passed_ids() {
   local rd=$1 fq=$2
   [ -s "${rd}/.cases" ] || return 0
   awk -v c="${fq}" '
      { cls = ""; nm = ""
        if (match($0, /classname="[^"]*"/)) cls = substr($0, RSTART + 11, RLENGTH - 12)
        if (match($0, /[ ]name="[^"]*"/))   nm  = substr($0, RSTART + 7,  RLENGTH - 8)
        if (cls == "" || nm == "") next
        if (cls != c && index(cls, c "$") != 1) next
        if (index($0, "<failure") > 0 || index($0, "<error") > 0) next
        if (index($0, "<skipped") > 0) next
        sub(/\(\)$/, "", nm)
        print cls "#" nm }' "${rd}/.cases"
}

# The ids this run of this class SKIPPED that the frozen 2011 run PASSED. The
# scorer picks the runner and the classpath, so only the test's own code -- an
# assumption that cannot hold, an OS or JRE condition, a condition method --
# can make one of those skip here. The caller adds them to corrob-failed.txt:
# a report that says such a case passed is then contradicted, exactly as one
# that hides a failure is. The golden tree has none (its seven skipped cases
# are ids the baseline skipped or never named). Trajectory review 2026-10-06,
# closing round 3 G13 together with the report parse.
corrob_skipped_ids() {
   local rd=$1 fq=$2
   [ -s "${rd}/.cases" ] || return 0
   awk -v c="${fq}" -v basef=/tests/baseline/testresults.txt '
      BEGIN {
        while ((getline bl < basef) > 0) {
           split(bl, bfld, " ")
           if (bfld[2] == "pass") basepass[bfld[1]] = 1
        }
        close(basef)
      }
      { cls = ""; nm = ""
        if (match($0, /classname="[^"]*"/)) cls = substr($0, RSTART + 11, RLENGTH - 12)
        if (match($0, /[ ]name="[^"]*"/))   nm  = substr($0, RSTART + 7,  RLENGTH - 8)
        if (cls == "" || nm == "") next
        if (cls != c && index(cls, c "$") != 1) next
        if (index($0, "<skipped") == 0) next
        if (index($0, "<failure") > 0 || index($0, "<error") > 0) next
        sub(/\(\)$/, "", nm)
        if ((cls "#" nm) in basepass) print cls "#" nm }' "${rd}/.cases"
}

echo "=== step 10 verifier: $(date -u +%FT%TZ) ==="

if [ ! -d "${TREE}" ]; then
   echo "FATAL: no tree at ${TREE}; /app holds: $(ls -A /app 2>/dev/null | tr '\n' ' ')"
   fact "scorer=broken"
   fact "scorer.reason=no-tree"
   report 0
fi

give_app_to_unprivileged

# ---------------------------------------------------------------- family A
#
# runner/score.sh owns the build, the probe and the comparison. RUNNER_BUILD is
# publishToMavenLocal and not build: that task does not run tests, so family A
# cannot be moved by the test suite and the two families stay independent.
echo
echo "=== family A: behaviour equivalence (939 recorded fixtures, weight 0.30) ==="
a_score=0
# B-1 reads this: "the project builds with Gradle" is half of that gate, and an
# unset value must read as "it did not", not as an unbound-variable crash.
b_ok=
# Root, deliberately. The runner demotes the build and the probe to 65534 on its
# own; running the whole of it as 65534 meant it could not read its own script
# out of the 0700 payload, and family A scored 0 on a permission error.
if env \
      RUNNER_TREE="${TREE}" \
      RUNNER_BUILD='./gradlew --no-daemon publishToMavenLocal' \
      RUNNER_PUBLISHED="${M2}" \
      RUNNER_ASSETS=/tests/assets \
      RUNNER_ERA=jdk25-step11 \
      RUNNER_IDS=ids.txt \
      RUNNER_OUT="${WORK}/runner" \
      bash /tests/runner/score.sh > "${WORK}/runner.log" 2>&1; then
   :
fi
sed -n '1,120p' "${WORK}/runner.log" | sed 's/^/    /'
summary="${WORK}/runner/behavior.summary"
if [ -r "${summary}" ]; then
   cat "${summary}" >> "${FACTS}"
   grep -E '^behavior\.(build|jars|probe|expected|produced|match|scorer)' "${summary}" \
      | sed 's/^/  /'
   b_ok=$(sed -n 's/^behavior\.build=//p' "${summary}" | head -1)
   b_match=$(sed -n 's/^behavior\.match=//p' "${summary}" | head -1)
   b_exp=$(sed -n 's/^behavior\.expected=//p' "${summary}" | head -1)
   if [ "${b_ok:-}" = ok ]; then
      a_score=$(frac "${b_match:-0}" "${b_exp:-0}")
   else
      echo "  family A: build did not succeed (behavior.build=${b_ok:-missing}) -> 0"
   fi
else
   echo "  family A: runner wrote no summary -> 0"
   fact "behavior.scorer=broken"
fi
fact "familyA=${a_score}"

# Whatever the build published, once, for the gates that read it.
PUB="${M2}/org/jboss/arquillian"
find "${PUB}" -type f \( -name '*.jar' -o -name '*.pom' \) 2>/dev/null \
   | sed "s|^${PUB}/||" | LC_ALL=C sort > "${WORK}/published.txt" || : > "${WORK}/published.txt"
# The module jars: no classifier, so neither -sources nor -tests.
find "${PUB}" -type f -name 'arquillian-*-1.0.0-SNAPSHOT.jar' 2>/dev/null \
   | LC_ALL=C sort > "${WORK}/modulejars.txt" || : > "${WORK}/modulejars.txt"
fact "published.files=$(grep -c . "${WORK}/published.txt")"
fact "published.modulejars=$(grep -c . "${WORK}/modulejars.txt")"

# What the build just published, read once, before any other candidate code
# runs (wave D, G14). The source gates compile the tree's own
# <module>/src/main/java with the scorer's javac, and until this rung nothing
# tied that compile to the jars the build shipped: a migrated copy of the
# sources kept beside an unmigrated build source set was scored in place of the
# shipped code. srcbind() ties the two, on the first scorer compile that
# succeeds -- every main source it compiled ships byte for byte in a -sources
# jar, every class in a module jar is a class it produced, and every class it
# produced from a main source is in a module jar. The jars are read as 65534,
# the uid that wrote the repository they sit in, so root follows nothing that
# uid left there; the lists they become are root's.
BIND="${WORK}/bind"
mkdir -p "${BIND}"
find "${PUB}" -type f -name 'arquillian-*-1.0.0-SNAPSHOT-sources.jar' 2>/dev/null \
   | LC_ALL=C sort > "${BIND}/sourcejars.txt" || : > "${BIND}/sourcejars.txt"
run_unprivileged timeout -s KILL 300 bash -c \
   'for j in "$@"; do unzip -Z1 "$j" || exit 1; done' bindls $(cat "${WORK}/modulejars.txt") \
   > "${BIND}/jarentries.txt" 2> "${BIND}/snap.log"
bindls_rc=$?
# Every .class entry counts, META-INF/ included (wave H round 4): a
# multi-release jar's META-INF/versions/<n>/... class is a shipped class too,
# loaded in place of the base one on a new enough JVM, and no scorer compile
# produces one, so srcbind reads it as a shipped class it did not compile.
# module-info is the one exception, wherever it sits.
grep '\.class$' "${BIND}/jarentries.txt" \
   | grep -v '\(^\|/\)module-info\.class$' | LC_ALL=C sort -u > "${BIND}/jarclasses.txt"
run_unprivileged timeout -s KILL 300 bash -c \
   'for j in "$@"; do unzip -Z1 "$j" | grep "\.java\$" | while IFS= read -r e; do h=$(unzip -p "$j" "$e" | sha256sum); printf "%s %s\n" "${h%% *}" "$e"; done; done' \
   bindsrc $(cat "${BIND}/sourcejars.txt") > "${BIND}/jarsources.raw" 2>> "${BIND}/snap.log"
bindsrc_rc=$?
LC_ALL=C sort "${BIND}/jarsources.raw" > "${BIND}/jarsources.txt"
bindstate=pending
[ "${bindls_rc}" = 0 ] && [ "${bindsrc_rc}" = 0 ] && [ -s "${BIND}/jarclasses.txt" ] \
   && [ -s "${BIND}/jarsources.txt" ] || bindstate=no
echo "${bindstate}" > "${BIND}/state"
fact "gate.bind.snapshot=${bindstate}"
fact "gate.bind.jarclasses=$(grep -c . "${BIND}/jarclasses.txt")"
fact "gate.bind.jarsources=$(grep -c . "${BIND}/jarsources.txt")"

# ----------------------------------------------- the source AST, compiled once
#
# Both families read the typed AST -- B-10 through B-16, which hold steps 03 to
# 09's constructs in place, and three of family C's five legs, which read this
# rung's own constructs -- so the compiles that produce it happen once, here,
# rather than inside either family. Hoisted above family B and not left where
# step 03 had it, at the head of family C, because B-10 is a family-B gate and a
# block cannot read a report that has not been written yet.
#
# Nothing below this point edits the tree, so the hoist changes no measurement.
# It does put the compiles before `gradlew test` rather than after it, which is
# the one visible difference and is deliberate: these compiles are the SCORER's,
# against the SCORER's pinned classpath, driven over every .java under every
# module's src/. They do not consult the candidate's build file -- that is the
# one file a candidate can edit to change what a build reports about the
# candidate -- so there is nothing for the suite's run to contribute to them.
#
# Everything here is read off the AST (rule 11) and never out of source text.
# This rung needs it more literally than any before it, because its largest leg
# asks a question the text of an import line does not answer. `import module M;`
# is a type-import-on-demand over the EXPORTED PACKAGES of M, so writing one
# correctly means knowing, for every JDK type a file names, WHICH MODULE that
# type's package is exported by -- and the package prefix does not say:
# `java.util.List` is java.base and `java.util.logging.Logger` is java.logging,
# five characters apart in the same source line. The gate records `mod=` off the
# RESOLVED symbol's module for all 495, which is why C-2 can compare a file's
# module set rather than count lines. C-3's three hold classes are the same
# problem one layer down: a static import, a nested-type import and a plain type
# import are four tokens apart in text and are three different things to the
# resolver, and only one of them is subsumed by a module import.
#
# Three instruments, not one. The AST counters say what the tree CONTAINS. The
# jars the build published say what the tree EMITS, and at this rung that second
# channel is read twice: by arqgate.Scan for the class-file version C-1 asks
# about -- BOTH HALVES OF IT, major and minor -- and B-4 holds the major of, and
# by arqgate.Modules for the Automatic-Module-Name manifest attribute B-15
# holds. Neither is visible in any source file, which is the point -- a
# class-file version is a property of the ARTIFACT, and a tree that writes `25`
# into a build file it does not apply has changed nothing a consumer can see.
# javac's acceptance at a named -source level is the third reading, printed as
# gate.src.ok25 / gate.src.ok24 and scored by nothing. A FOURTH instrument
# arrives with C-5 and is neither source nor artifact: the argument list Gradle
# would hand a forked test JVM, asked of the build in a separate invocation
# after the scored one.
#
# This step's INPUT -- step 11's blind snapshot -- reads `modimport.size=0` and
# `scopedvalue.size=0`, carries all 495 single-type JDK imports across 171
# files, declares 8 ThreadLocal fields and no ScopedValue, emits major 66, and
# forks its test JVMs with no header flag: a measured zero on all five family-C
# legs below. This tree must do all five. The golden reads 188 / 171 module
# imports, 43 surviving single-type imports, 7 ThreadLocal declarations and 1
# ScopedValue, 435 class files at 69.0, and the flag on all twelve.
echo
echo "--- the source AST: the scorer's own compiles (B-10 and family C read them)"

# The frozen expectations. baseline/source.txt is where the tree starts and
# baseline/target.txt is where it has to get to; both were measured with this
# same gate and both are staged, so no threshold is derived at grading time from
# a tree the candidate controls.
#
# source.txt is STEP 11'S BLIND SNAPSHOT and not step 11's golden, because the
# blind snapshot is the tree this rung's candidate is actually handed: every
# step on this ladder is seeded from the previous step's blind output, so the
# golden describes a tree that no arm of this step will ever see. That the two
# CAN differ is not hypothetical -- step 06's blind agent missed one Optional
# arrival, which is why target.b13.opt.arrived is 2 and not 3 -- and a
# source.txt taken from a golden would describe a starting point some number of
# edits ahead of the real one.
#
# It is also measured at -source 25 with the gate that SHIPS HERE and not with
# the one that shipped at step 11: step 11's SourceScan cannot see modimport,
# jdkimport, scopedvalue or threadlocal at all -- it predates every one of those
# record kinds -- so its report would be missing exactly the populations family
# C scores, and its javac cannot parse a module import in the first place. Step
# 11's own source.txt and target.txt are therefore not staged beside these. Two
# files that both want to land as baseline/source.txt is how the wrong one comes
# to be read; what step 11 needs carried forward is carried under its own
# target.priv.*, target.cp.* and target.vh.* key spellings in the file below,
# and its population files -- priv.txt, catchparam.txt, vhids.txt -- are staged
# under their own names.
bv() { sed -n "s/^$1=//p" /tests/baseline/source.txt | head -1; }
tv() { sed -n "s/^$1=//p" /tests/baseline/target.txt | head -1; }

base_anonfn=$(bv 'anonymousfunctional\.size')
base_conv=$(bv 'anonymousfunctional\.convertible')
base_collect=$(bv 'foreach\.collect')
base_loop=$(bv 'loop\.size')
base_samann=$(bv 'iface\.samannotated')
base_explicit=$(bv 'newgeneric\.explicit')
base_anon=$(bv 'newgeneric\.explicitanon')
base_raw=$(bv 'newgeneric\.raw')
base_sup=$(bv 'suppress\.sites')
tgt_anonfn=$(tv 'target\.anonfn')
# Step 04's own arrival figures -- how many files held a lambda, how many SAMs
# were annotated, how many buckets held a pipeline -- are NOT read under those
# names here. B-11 reads them under target.b11.*, below, and reading them twice
# under two spellings is how the two copies come to disagree. The bare
# target.lambdafiles / target.sam / target.collectmethods / target.stream.files
# / target.level17.floor keys are step 04's, they describe step 04's rung, and
# this file does not carry them.
tgt_dia=$(tv 'target\.diamond')
tgt_nonanon=$(tv 'target\.explicit\.nonanon')
tgt_res=$(tv 'target\.try\.resources')
tgt_io=$(tv 'target\.iofile')
b10_dia=$(tv 'target\.b10\.diamond')
b10_close=$(tv 'target\.b10\.close')
b10_nio=$(tv 'target\.b10\.nio')
# B-11's three denominators: how much work STEP 04 did, taken from step 04's own
# target.txt when this file was frozen and republished here under a b11 prefix.
# Not re-measured from this tree, and not read out of the step-04 set files by
# counting their lines either -- a set file with a line dropped would then move
# its own denominator with it and the gate would read full on a smaller
# question. See B-11.
b11_lambdafiles=$(tv 'target\.b11\.lambdafiles')
b11_sam=$(tv 'target\.b11\.sam')
b11_collectmethods=$(tv 'target\.b11\.collectmethods')
# B-12's four denominators and four constant-pool floors: how much work STEP 05
# did, taken from step 05's own target.txt when this file was frozen and
# republished here under a b12 prefix. Not re-measured, and not counted out of
# the staged set files either -- a set file with a line dropped would then move
# its own denominator with it and the gate would read full on a smaller
# question. The bare target.optbuckets / target.sinks / target.valuetypes /
# target.timesites / target.c5.*.floor spellings are step 05's, they live in
# step 05's target.txt, and this file does not carry them: baseline/target.txt
# here is step 10's, and two files that both want to land as baseline/target.txt
# is how the wrong one comes to be read.
b12_optbuckets=$(tv 'target\.b12\.optbuckets')
b12_sinks=$(tv 'target\.b12\.sinks')
b12_valuetypes=$(tv 'target\.b12\.valuetypes')
b12_timesites=$(tv 'target\.b12\.timesites')
b12_opt_floor=$(tv 'target\.b12\.optional\.floor')
b12_col_floor=$(tv 'target\.b12\.collectors\.floor')
b12_obj_floor=$(tv 'target\.b12\.objects\.floor')
b12_jt_floor=$(tv 'target\.b12\.jtime\.floor')

# B-13's six denominators and its three floors: how much work STEP 06 did,
# taken from step 06's own target.txt when this file was frozen and republished
# here under a b13 prefix. Not re-measured from this tree, and not counted out
# of the staged set files either -- a set file with a line dropped would then
# move its own denominator with it and the gate would read full on a smaller
# question. The bare target.c2..c7 spellings are STEP 06'S, they live in step
# 06's target.txt, and this file does not carry them.
#
# ONE OF THE ELEVEN IS NOT STEP 06'S FIGURE, and it is the one that matters.
# target.b13.opt.arrived is 2 where step 06's target.c6.methods is 3, because
# it is counted on the tree this step is HANDED rather than on the tree step 06
# asked for. The step-06 blind agent removed all three `isPresent()` branches
# and reached a Java 9 method at two of them, writing an early-return
# Optional.of/empty() loop at the third; that was adjudicated as an agent miss,
# no edit was made, and the snapshot is what it is. A hold gate whose arrival
# denominator came from the previous rung's REQUIREMENT rather than from the
# previous rung's RESULT could never read full here, and this rung's candidate
# would be paying for the last one's miss. See baseline/optarr.txt, which is
# the list it is counted from, and the ninth trap, which is what it is.
b13_legacy=$(tv 'target\.b13\.legacy')
b13_ctorfloor=$(tv 'target\.b13\.ctor\.floor')
b13_guards=$(tv 'target\.b13\.guards')
b13_setacc=$(tv 'target\.b13\.setacc')
b13_saccb=$(tv 'target\.b13\.setacc\.buckets')
b13_boxed=$(tv 'target\.b13\.boxed')
b13_builds=$(tv 'target\.b13\.builds')
b13_views=$(tv 'target\.b13\.views')
b13_optdep=$(tv 'target\.b13\.opt\.departed')
b13_optarr=$(tv 'target\.b13\.opt\.arrived')
b13_cand=$(tv 'target\.b13\.candidatenew')
b13_want=$(tv 'target\.b13\.want')

# B-14's own populations and floors: STEP 07'S OWN FIGURES, carried forward a
# fifth time and not re-measured. Step 07 measured them on the step-06 blind
# snapshot; step 08 republished them bare; steps 09, 10 and 11 republished them
# under the b14 prefix; this rung republishes what step 11 published, which is
# the same ten numbers -- checked by diffing them, not asserted.
#
# NOT RE-MEASURED IS A DECISION WITH A CHECK BEHIND IT rather than a
# convenience. Step 06's gate had to re-measure one of its inherited targets
# because the step-05 agent missed an arrival the target claimed. Step 07's
# blind run met every one of these four legs in full -- familyC=1.000000,
# gate.c5.legacy=0 against a baseline of 14 -- and no rung's blind run since has
# moved one of them.
#
# AND AT THIS RUNG THE CHECK IS STRONGER THAN THE WORDING IT INHERITS, because
# it is no longer taken off a neighbouring rung's blind run. Step 11's verifier
# ships these same ten numbers, and step 11's verifier scores the tree this rung
# hands its candidate at reward=1.000000 -- so B-14 is known to read full on
# THIS rung's input, rather than known to have read full on a tree four rungs
# back that nothing since has disturbed. The re-measurement is recorded as NOT
# NEEDED rather than skipped: see the B-14 banner below.
#
# THE KEYS CARRY THE B14 PREFIX IN baseline/target.txt HERE, WHERE AT STEP 08
# THEY KEPT STEP 07'S BARE SPELLING. Step 08 could leave them bare because its
# own family C was named for modules and services and nothing in its
# target.txt began `target.c`. This rung's family C is five legs and a reader
# meeting `target.c2.refs` in this file would reasonably take it for C-2's
# denominator, which it is not -- C-2's is target.rt.*. So every carried
# denominator in this file now says which family reads it: b13 for step 06's,
# b14 for step 07's, b15 for step 08's, b16 for step 09's. The renaming is done
# in ONE place -- the rung's own mk-baseline script, which reads the previous
# rung's file by its old key and writes this one by its new; step 09 did it for
# b13 to b15 and mk-baseline-step10.sh did it for b16, and no rung since has had
# a family C to rename -- so the value is provably the same number and only the
# label moved. The SHELL names carried
# the b14 prefix already, for the collision the sixth trap would otherwise find
# after it had scored.
tgt_b14_2_buckets=$(tv 'target\.b14\.c2\.buckets')
tgt_b14_2_refs=$(tv 'target\.b14\.c2\.refs')
tgt_b14_3_buckets=$(tv 'target\.b14\.c3\.buckets')
tgt_b14_3_refs=$(tv 'target\.b14\.c3\.refs')
tgt_b14_4_buckets=$(tv 'target\.b14\.c4\.buckets')
tgt_b14_4_sized=$(tv 'target\.b14\.c4\.sized')
# A path, not a count: B-14.5 is scoped by FILE. See baseline/http.txt.
tgt_b14_5_file=$(tv 'target\.b14\.c5\.file')
tgt_b14_5_legacy=$(tv 'target\.b14\.c5\.legacy')
tgt_b14_5_types=$(tv 'target\.b14\.c5\.types')
tgt_b14_5_stub=$(tv 'target\.b14\.c5\.cdistub')

base_nonanon=$(( ${base_explicit:-0} - ${base_anon:-0} ))
# How many anonymous functional bodies have to go: 48 - 12. Taken as a
# difference of two frozen figures rather than written down as 36, so that a
# drift in either end is an arithmetic error here instead of a third number
# somebody has to keep in step with the other two.
anonfn_need=$(( ${base_anonfn:-0} - ${tgt_anonfn:-0} ))
fact "gate.src.base.anonfn=${base_anonfn:-0}"
fact "gate.src.base.convertible=${base_conv:-0}"
fact "gate.src.base.collect=${base_collect:-0}"
# Every loop statement in the tree the candidate was handed -- for, enhanced-for,
# while, do. B-12's collectors leg asks whether the 23 loops step 05 turned into
# pipelines are still pipelines, and this is the only figure that can tell that
# from a tree where they merely changed spelling: see the no-reshape term in
# B-12 below.
fact "gate.src.base.loop=${base_loop:-0}"
fact "gate.src.base.nonanon=${base_nonanon}"
fact "gate.src.need.anonfn=${anonfn_need}"
# 24 at this rung, and printed because B-11 reads it as a hold rather than as
# an arrival: every @FunctionalInterface step 04 added has to still be there,
# and a figure that had fallen would say which direction the tree moved.
fact "gate.src.base.samannotated=${base_samann:-0}"

# The three compiles. -source 25 is the measurement: it is the rung, and
# source-gate.sh passes -source and -target together so javac never refuses the
# pair. -source 24 runs only if the first succeeded, so that a difference
# between them is a difference of language level and not of environment. The
# default-level compile is the fallback: a tree that overshot the level, or that
# does not build at 25 for some reason of its own, is still measured so that it
# is graded on the work it did rather than on nothing.
#
# AND AT THIS RUNG THE MEASUREMENT LEVEL IS LOAD-BEARING FOR THE BASELINE TOO.
# baseline/source.txt and every arrival figure in baseline/target.txt were taken
# at -source 25 with this gate, because `import module` parses at no lower
# release: a report taken at 22 would carry `modimport.size=0` for the golden
# and this rung's two largest legs would be scored against a denominator the
# compiler refused to produce. mk-baseline-step12.sh takes both readings at 25.
#
# gate.src.ok24 is PUBLISHED AND SCORED BY NOTHING, and it is expected to read
# `no` -- the fourth rung running, after steps 09, 10 and 11, where the control
# flips. It flips for exactly ONE of this rung's five legs. `ScopedValue` is a
# class in java.lang and not a language change, so C-4 moves no -source level;
# `-XX:+UseCompactObjectHeaders` is an argument to a JVM and javac never sees
# it, so C-5 does not either; the class-file major is a property of the emitted
# artifact and not of what javac will parse; and C-3's 43 holds are ordinary
# import declarations that have been legal since Java 1. What is left is the
# MODULE IMPORT DECLARATION: JEP 511 was preview in 22, 23 and 24 and standard
# in 25, so a tree that has done C-2's work cannot compile at -source 24 without
# --enable-preview and this control reads `no`. Everything step 11 put in --
# the unnamed catch parameters (22), `privateLookupIn` and `unreflectVarHandle`
# (9, both APIs) -- is standard at 24 and leaves it at `yes` on its own, so `no`
# here means specifically that the module imports are there.
#
# IT IS NOT THE PREVIEW CHECK AND MUST NOT BE MISTAKEN FOR ONE. The scorer runs
# this compile without --enable-preview, so a candidate who turned the flag on
# in their own build reads `no` here exactly like a candidate who did the work
# properly. What separates those two trees is C-1's minor, read off the class
# files the candidate's own build emitted, and nothing else in this file.
#
# A control and not a gate. It is not scored, in either direction: `yes` on a
# tree that did everything else would be a true statement about a tree missing
# one leg C-2 already charges, and scoring it would charge the same absence
# twice. What it is for is the trajectory record -- the one line that says
# which of two shapes a partial answer has.
measured=
ok25=$(srcgate 25 25)
fact "gate.src.ok25=${ok25:-no}"
grep -E '^javac\.' "${WORK}/src-25.txt.env" 2>/dev/null | sed 's/^/    25: /'
ok24=no
okdef=no
if [ "${ok25:-no}" = yes ]; then
   measured="${WORK}/src-25.txt"
   ok24=$(srcgate 24 24)
   fact "gate.src.ok24=${ok24:-no}"
   grep -E '^javac\.' "${WORK}/src-24.txt.env" 2>/dev/null | sed 's/^/    24: /'
   if [ "${ok24:-no}" = yes ]; then
      echo "    the tree still compiles at -source 24: no Java 25 language construct is load-bearing yet"
   else
      echo "    the tree no longer compiles at -source 24: a Java 25 language construct arrived, which is what C-2 asks for"
   fi
else
   # The tree does not compile at Java 25 with the scorer's classpath.
   echo "    the tree does not compile at -source 25; measuring at the default level"
   # The COMPILER's complaint, not the gate's .env. src-25.log holds both --
   # source-gate.sh tees the .env to stdout and tails the javac log to stderr,
   # and srcgate() catches both in one file -- so `head` of it prints twelve
   # lines of `files=322` and never the reason.
   if [ -s "${WORK}/src-25.txt.javac.log" ]; then
      grep -E ': error: |^[0-9]+ error' "${WORK}/src-25.txt.javac.log" \
         | head -12 | sed 's/^/    /'
   else
      sed -n '1,12p' "${WORK}/src-25.log" | sed 's/^/    /'
   fi
   okdef=$(srcgate def "")
   fact "gate.src.okdef=${okdef:-no}"
   [ "${okdef:-no}" = yes ] && measured="${WORK}/src-def.txt"
fi

# Every counter both families read, taken once. Initialised first, because
# `set -u` is on and two separate blocks read them: a tree that produced no
# report at all must reach both verdicts and be told it produced none, rather
# than killing the script between them.
now_anonfn=0; now_conv=0; now_forward=0
now_lambda=0; now_lambdafiles=0; now_mref=0
now_ifacedecl=0; now_sam=0; now_samann=0
now_foreach=0; now_collect=0; now_loop=
now_streams=0; now_streamops=0; now_streamfiles=0; now_streamopfiles=0
now_opmethods=
now_explicit=0; now_anon=0; now_dia=0; now_dfiles=0; now_raw=0
now_res=0; now_resvars=0; now_close=0
now_ionew=0; now_iodecl=0; now_nioref=0; now_niostatic=0
now_sup=0; now_files=0
if [ -z "${measured}" ]; then
   # Nothing was measured. Say so rather than reporting clean zeros that read
   # like a finding about the tree.
   fact "gate.src=broken"
else
   # 0 when the key is absent, which here is a real case and not a defence
   # against one. SourceScan emits an ns.ref.<prefix> aggregate only for
   # prefixes it actually saw, so a tree that has given the jakarta namespace
   # back has no ns.ref.jakarta.annotation line at all -- and the arrival half
   # of step 07's C-2, held here as B-14, has to read that absence as zero
   # rather than as the empty string. Step 06's sv() returned the empty string
   # because every key it read was always present; here that would leave
   # `[ "${x}" -gt 0 ]` comparing nothing.
   sv() { sed -n "s/^$1=//p" "${measured}" | head -1 | grep -E '^-?[0-9]+$' || echo 0; }
   now_anonfn=$(sv 'anonymousfunctional\.size')
   now_conv=$(sv 'anonymousfunctional\.convertible')
   now_forward=$(sv 'anonymousfunctional\.forward')
   now_lambda=$(sv 'lambda\.size')
   now_lambdafiles=$(sv 'lambda\.files')
   now_mref=$(sv 'methodref\.size')
   now_ifacedecl=$(sv 'iface\.decl')
   now_sam=$(sv 'iface\.sam')
   now_samann=$(sv 'iface\.samannotated')
   now_foreach=$(sv 'foreach\.size')
   now_collect=$(sv 'foreach\.collect')
   now_loop=$(sv 'loop\.size')
   now_streams=$(sv 'stream\.producers')
   now_streamops=$(sv 'stream\.ops')
   now_streamfiles=$(sv 'stream\.files')
   now_streamopfiles=$(sv 'stream\.opfiles')
   # Left EMPTY when the key is absent, like loop.size and for the same reason:
   # a gate jar too old to write the `in=` field on its records writes no
   # bucket count either, and C-3's arrival half would then be scored against
   # records it cannot key. Absent is published as `absent`, not as 0.
   now_opmethods=$(sv 'stream\.opmethods')
   now_explicit=$(sv 'newgeneric\.explicit')
   now_anon=$(sv 'newgeneric\.explicitanon')
   now_dia=$(sv 'newgeneric\.diamond')
   now_dfiles=$(sv 'newgeneric\.diamondfiles')
   now_raw=$(sv 'newgeneric\.raw')
   now_res=$(sv 'try\.resources')
   now_resvars=$(sv 'try\.resourcevars')
   now_close=$(sv 'close\.manual')
   now_ionew=$(sv 'iofile\.new')
   now_iodecl=$(sv 'iofile\.decl')
   now_nioref=$(sv 'niofile\.ref')
   now_niostatic=$(sv 'niofile\.staticref')
   now_sup=$(sv 'suppress\.sites')
   now_files=$(sv 'files')
   for k in anonfn conv forward lambda lambdafiles mref ifacedecl sam samann \
            foreach collect streams streamops streamfiles streamopfiles \
            explicit anon dia \
            dfiles raw res resvars close ionew iodecl nioref niostatic sup files; do
      eval "now_${k}=\${now_${k}:-0}"
      eval "fact \"gate.src.${k}=\${now_${k}}\""
   done
   # Outside the loop because both are published as `absent` rather than as 0,
   # and because neither is read by a gate at this rung: C-3's arrival half was
   # step 04's, and B-11 asks the same question of step 04's frozen buckets
   # instead of of these totals. They are published anyway. The trajectory
   # analysis reads facts.txt, `gate.src.base.loop` is printed above with no
   # counterpart otherwise, and loop.size is the only figure in the file that
   # would show a candidate unwinding step 04's pipelines back into loops while
   # this step's family C still read full.
   fact "gate.src.opmethods=${now_opmethods:-absent}"
   fact "gate.src.loop.now=${now_loop:-absent}"
fi

# covered <set file> <record prefix>... -- how many of the paths named in a
# frozen set file carry at least one record of any of the named kinds in the
# report that was just measured.
#
# This is family C's whole defence against a tree that moved the counters
# without doing the work, and B-10 does not need it: step 03's gates already
# established WHERE its constructs are, and what is asked here is only that the
# totals did not fall. The set files name sites by path, which is why they are
# staged into the VERIFIER and the verifier alone -- environment_mode is
# "separate", so /tests is not a directory the candidate's container can read.
#
# The path is field 1 of a record, up to the colon that precedes the line
# number; the set file's own first field is compared against it. A set file line
# may carry more fields (samifaces.txt carries the interface's simple name), and
# the extra fields are matched by the caller, not here.
covered() {
   local setfile=$1; shift
   local pat
   for pat in "$@"; do
      sed -n "s/^${pat}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
         | awk '{ split($1, a, ":"); print a[1] }'
   done | LC_ALL=C sort -u > "${WORK}/cov-hit.txt"
   grep -v '^#' "${setfile}" | grep . | awk '{print $1}' | LC_ALL=C sort -u \
      > "${WORK}/cov-want.txt"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/cov-hit.txt" | grep -c . || true
}

# coveredboth <setfile> <pat-a> <pat-b> -- how many of the set's files carry a
# site of BOTH kinds.
#
# covered() above takes the UNION of its patterns, which is the right shape when
# two spellings answer one question: a lambda and a method reference are both
# "this file has a lambda form in it". This one is the intersection, for when two
# different facts have to hold of the same file, and it exists because one of
# them did not. `xs.stream();` as a statement of its own is a stream PRODUCER
# with no stage attached: it enters the stream world and leaves it again having
# done nothing, and a file-level "does this file hold a pipeline?" question
# answered from producers alone says yes. Asking for a producer and a stage in
# the same file is asking whether anything was actually piped.
#
# It leaves cov-want.txt and cov-hit.txt where covered() leaves them, so the
# caller's `comm -23` listing of what is still missing reads the same either way.
coveredboth() {
   local setfile=$1 a=$2 b=$3
   sed -n "s/^${a}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); print f[1] }' | LC_ALL=C sort -u \
      > "${WORK}/cov-a.txt"
   sed -n "s/^${b}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); print f[1] }' | LC_ALL=C sort -u \
      > "${WORK}/cov-b.txt"
   LC_ALL=C comm -12 "${WORK}/cov-a.txt" "${WORK}/cov-b.txt" > "${WORK}/cov-hit.txt"
   grep -v '^#' "${setfile}" | grep . | awk '{print $1}' | LC_ALL=C sort -u \
      > "${WORK}/cov-want.txt"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/cov-hit.txt" | grep -c . || true
}

# coveredmethods <setfile> <pat-a> <pat-b> -- coveredboth(), one scope down.
#
# The set file's lines are `<path>#<method>` and a record's bucket is its path
# joined to its own `in=` field, which SourceScan writes as the OUTERMOST
# enclosing method. Round 2 of this step's static review is why it exists:
# asking whether a FILE holds a pipeline is answered yes by one
# `xs.stream().count();` per file, and `count()` is a stage, so the
# producer-and-stage conjunction of coveredboth() does not see it coming. Pair
# that with removing the 24 collecting loops by some means that is neither a
# loop nor a stream -- `dst.addAll(src)`, `src.forEach(dst::add)` -- and the
# census half is passed honestly while 0.60 of family C is paid for no pipeline
# at all. A method is the scope the instruction names: the pipeline goes where
# the loop was.
#
# A record with no `in=` field contributes no bucket, so a stale gate jar reads
# as "nothing arrived" rather than as "everything did". That is the safe
# direction, and the one the seventh trap asks for.
#
# Leaves cov-want.txt and cov-hit.txt where covered() leaves them, so the
# caller's `comm -23` listing of what is still missing reads the same either way.
coveredmethods() {
   local setfile=$1 a=$2 b=$3 pat
   for pat in "${a}" "${b}"; do
      sed -n "s/^${pat}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
         | awk '{ split($1, f, ":"); m = "";
                  for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4);
                  if (m != "") print f[1] "#" m }' \
         | LC_ALL=C sort -u > "${WORK}/cov-${pat}.txt"
   done
   LC_ALL=C comm -12 "${WORK}/cov-${a}.txt" "${WORK}/cov-${b}.txt" > "${WORK}/cov-hit.txt"
   grep -v '^#' "${setfile}" | grep . | awk '{print $1}' | LC_ALL=C sort -u \
      > "${WORK}/cov-want.txt"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/cov-hit.txt" | grep -c . || true
}

# buckets <record kind> -- every distinct <file>#<method> holding at least one
# record of that kind, into ${WORK}/buk-<kind>.txt, sorted and unique.
#
# coveredmethods() above answers "which of a named set of buckets hold BOTH of
# two kinds", which is the only bucket question step 04 had. Every one of this
# step's four construct gates asks a different pair: which of a named set hold
# NONE of a kind (the departure half) and which hold SOME of another (the
# arrival half). Splitting the bucket list out is what lets both be asked of the
# same set file without measuring it twice, and it is what lets the departure
# half be a `comm -23` rather than a subtraction of two counts -- a subtraction
# would read full for a tree that removed one site and added another somewhere
# else in the same set.
#
# The key is <path>#<outermost enclosing method>, exactly as coveredmethods()
# builds it and exactly as baseline/optbuckets.txt, sinks.txt and timesites.txt
# were frozen. `in=` is LAST on a SourceScan record on purpose: this loop takes
# the whole rest of the field, so a field added after it would split one bucket
# into two silently. See the plugin's own header.
buckets() {
   sed -n "s/^$1\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); m = "";
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4);
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort -u > "${WORK}/buk-$1.txt"
}

# wantbuckets <set file> -- field 1 of every data line, into ${WORK}/cov-want.txt.
# Split out of covered() for the same reason buckets() is: this step asks two
# questions of one set file and neither is covered()'s.
wantbuckets() {
   grep -v '^#' "$1" | grep . | awk '{print $1}' | LC_ALL=C sort -u \
      > "${WORK}/cov-want.txt"
}

# fbuckets <record kind> <tag> <field> [<field>] -- buckets(), with a filter
# on the record's own fields, into ${WORK}/buk-<tag>.txt.
#
# Every departure/arrival pair below needs one and buckets() cannot give it: a
# `legacycoll` record is a VIEW or a BUILD according to its `kind=` field and
# the two answer opposite questions of the same record kind, an `accessible`
# record is a guard or a setAccessible according to its `call=` field, a
# `newinstance` record is the migration's start or its end according to its
# `form=` field, and a `toarray` record is the pre-11 spelling or the Java 11
# one according to the same field. A tag rather than the kind names the output
# file, so that one record kind can be bucketed twice under two filters without
# the second overwriting the first.
#
# Fields are matched whole, against every field from 2 on, so a positional
# record works as well as a key=value one: `optop` writes
# `<where> use isPresent in=<m>` and `fbuckets optop ispresentb isPresent use`
# asks for both of those words. A record with no `in=` field contributes no
# bucket, so a stale gate jar reads as "nothing is there" rather than as
# "everything departed" -- the safe direction, and the one the seventh trap
# asks for.
fbuckets() {
   local kind=$1 tag=$2 w1=$3 w2=${4:-}
   sed -n "s/^${kind}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk -v w1="${w1}" -v w2="${w2}" '
         { m = ""; h1 = 0; h2 = (w2 == "" ? 1 : 0)
           for (i = 2; i <= NF; i++) {
              if (substr($i, 1, 3) == "in=") m = substr($i, 4)
              if ($i == w1) h1 = 1
              if (w2 != "" && $i == w2) h2 = 1
           }
           if (h1 && h2 && m != "") { split($1, a, ":"); print a[1] "#" m } }' \
      | LC_ALL=C sort -u > "${WORK}/buk-${tag}.txt"
}

# setwant <set file> <first-field value> -- field 2 of every data line whose
# field 1 is the named tag, into ${WORK}/cov-want.txt.
#
# wantbuckets() takes field 1, which is right for the set files that are a
# plain list of buckets. newinstance.txt and accessible.txt are not: each
# carries TWO populations that must be told apart -- the buckets that have to
# change and the buckets that have to survive -- and keeping them in one file
# is what makes it impossible to stage half of the pair.
setwant() {
   grep -v '^#' "$1" | grep . | awk -v t="$2" '$1 == t { print $2 }' \
      | LC_ALL=C sort -u > "${WORK}/cov-want.txt"
}

# nsbuckets <tag> <awk condition on p> -- the namespace records, keyed by
# PACKAGE AND BUCKET rather than by bucket, into ${WORK}/buk-<tag>.txt.
# nswant <set file> -- the same key out of a two-column set file whose field 1
# is the package and field 2 the bucket, into ${WORK}/cov-want.txt.
#
# A key of its own, and this is the one place in the file where the bucket is
# not the whole key. Step 07 asks two opposite questions about javax
# references -- two packages must leave and six must stay -- and one method can
# hold both kinds. CDIInjectionEnricher#injectClass references javax.enterprise
# AND javax.inject; EJBInjectionEnricher#injectClass references javax.ejb and
# javax.naming. With the bucket alone as the key, moving javax.ejb out of that
# method would leave the bucket still occupied by javax.naming and the
# departure half would read it as not done -- and moving javax.naming out, which
# is the one thing this step must NOT do, would leave the hold reading full. The
# package has to be in the key for either question to mean anything.
#
# `|` as the separator because it cannot occur in a package name or a path, and
# because both sides are then a plain `comm` of sorted lines like every other
# gate in this file.
nsbuckets() {
   local tag=$1 cond=$2
   sed -n 's/^ns\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk "{ p = substr(\$2, 5) } ${cond} {
                split(\$1, f, \":\"); m = \"\"
                for (i = 2; i <= NF; i++) if (substr(\$i, 1, 3) == \"in=\") m = substr(\$i, 4)
                if (m != \"\") print p \"|\" f[1] \"#\" m }" \
      | LC_ALL=C sort -u > "${WORK}/buk-${tag}.txt"
}
nswant() {
   grep -v '^#' "$1" | grep . | awk '{ print $1 "|" $2 }' | LC_ALL=C sort -u \
      > "${WORK}/cov-want.txt"
}

# gone <set file> / hit <set file> -- how many members of the set named in
# ${WORK}/cov-want.txt have NO bucket, and how many have one, in
# ${WORK}/buk-<kind>.txt. Both callers have already run wantbuckets and buckets.
gone() { LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-$1.txt" | grep -c . || true; }
hit()  { LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-$1.txt" | grep -c . || true; }

# ---------------------------------------------------------------- family B
echo
echo "=== family B: migrated-ness gates ==="

# B-1  the Maven build is gone, not disabled.                     (inherited)
#
# Step 01's must-flip, held here. The label is worth getting right. This rung
# has TWO documented semantic changes rather than the one every rung from 05 to
# 09 had: C-1's class-file level, which lives in a build constant, and JEP
# 441's null rule, which lives in the source and is carried by the five
# recordings baseline/swids.txt names rather than by any gate. Neither of them
# is B-1, and neither is B-4: at this rung B-4 is a regression floor one rung
# behind the level flip, and a gate that also called itself the must-flip would
# say the scorer had a third thing it was really asking for.
#
# Both halves of "the project builds with Gradle, and Maven is gone", not just
# the second. An empty directory has no pom.xml either, and a tree that
# published nothing has not moved its build to Gradle -- it has no build at
# all. Without the first half, `rm -rf` on the tree scored a tenth of family B
# and the step's stated nop of 0.0 was really 0.05.
#
# "Built" is the publish succeeding AND a module jar coming out of it, not the
# exit status on its own: a `gradlew` that is a shell script exiting 0 reports
# behavior.build=ok, published nothing, and scored this gate 1 on the strength
# of a file it never wrote. The jars are the same ones B-3 grades, so this
# costs a real migration nothing.
poms=$(find "${TREE}" -name pom.xml -not -path '*/.git/*' 2>/dev/null | wc -l)
fact "gate.poms=${poms}"
b1=0
if [ "${poms}" -eq 0 ] && [ "${b_ok:-}" = ok ] && [ -s "${WORK}/modulejars.txt" ]; then
   b1=1
elif [ "${poms}" -eq 0 ]; then
   echo "    no pom.xml, but the build published no module jar (behavior.build=${b_ok:-missing}, jars=$(grep -c . "${WORK}/modulejars.txt")) -> 0"
fi
fact "B1.no-maven=${b1}"

# B-2  the build is self-describing through a committed wrapper.
b2=0
wrapper_props="${TREE}/gradle/wrapper/gradle-wrapper.properties"
if [ -x "${TREE}/gradlew" ] \
   && [ -f "${TREE}/gradle/wrapper/gradle-wrapper.jar" ] \
   && [ -f "${wrapper_props}" ] \
   && grep -q '^distributionUrl=' "${wrapper_props}"; then
   b2=1
   fact "gate.wrapper.dist=$(sed -n 's/^distributionUrl=//p' "${wrapper_props}" | head -1)"
else
   fact "gate.wrapper.dist=absent"
fi
fact "B2.wrapper=${b2}"

# B-3  the published surface, coordinate for coordinate.
#
# Only *.jar and *.pom: maven-metadata-local.xml is Maven's own local
# bookkeeping and is not a property of the artifacts. Extras are subtracted so
# that publishing a superset is not a way to score full marks.
grep -v 'maven-metadata-local.xml' /tests/baseline/artifacts.txt \
   | LC_ALL=C sort > "${WORK}/want-artifacts.txt"
want_n=$(grep -c . "${WORK}/want-artifacts.txt")
hit=$(LC_ALL=C comm -12 "${WORK}/want-artifacts.txt" "${WORK}/published.txt" | grep -c . || true)
extra=$(LC_ALL=C comm -13 "${WORK}/want-artifacts.txt" "${WORK}/published.txt" | grep -c . || true)
miss=$(LC_ALL=C comm -23 "${WORK}/want-artifacts.txt" "${WORK}/published.txt" | grep -c . || true)
fact "gate.artifacts.want=${want_n}"
fact "gate.artifacts.hit=${hit}"
fact "gate.artifacts.missing=${miss}"
fact "gate.artifacts.extra=${extra}"
LC_ALL=C comm -23 "${WORK}/want-artifacts.txt" "${WORK}/published.txt" | head -20 \
   | sed 's/^/    missing: /'
b3=$(clamp01 "$(awk -v h="${hit}" -v e="${extra}" -v w="${want_n}" \
      'BEGIN { printf "%.6f", (w <= 0) ? 0 : (h - e) / w }')")
fact "B3.published-surface=${b3}"

# ------------------------------------------------------- the inherited suite
#
# Run once, here, because three gates read it and the running is the expensive
# part. B-5 counts the test population out of the class files this leaves
# behind: family A's publish compiles test classes only for the three modules
# that publish a -tests jar, so a B-5 placed before this point sees 43 of the
# 55 classes and scores a complete tree 0.768. B-4 reads the same class files,
# for the same reason: the step's requirement is ONE major version across
# everything the build compiles and nothing below step 11's rung, major 66 --
# a version, not the class count two lines up. And until the suite has run,
# the test classes of nine of the twelve modules have not been compiled at
# all -- a majors.distinct read before this point is a count over a third of
# the tree. B-7 reads the
# reports.
#
# The marker is what makes a committed TEST-*.xml worthless: only reports this
# run wrote are read. Stale reports are removed before the marker is set, not
# after -- a report this run did not write must not be able to answer for it,
# and -newer alone would still trust one whose mtime happened to land on the
# right side.
echo
echo "--- running the inherited suite (B-4, B-5 and B-7 all read it)"
# EVERY report, wherever the build writes it -- the same population the stale
# count below and the read-back after the run use -- and not only the ones
# under the default build/test-results. The instruction leaves the output
# directory to the candidate, and a build that writes elsewhere kept its old
# reports when only build/test-results was cleared: Gradle saw the Test task's
# outputs intact, called it UP-TO-DATE, and this run wrote no report at all
# (wave R 2026-10-20, g02 A-1). Removing them is what makes Gradle run it.
find "${TREE}" -name 'TEST-*.xml' -not -path '*/.gradle/*' -delete 2>/dev/null || :
stale=$(find "${TREE}" -name 'TEST-*.xml' -not -path '*/.gradle/*' 2>/dev/null | grep -c . || true)
fact "gate.testxml.stale=${stale}"
marker="${WORK}/before-test"
: > "${marker}"
# --continue, because without it the first failing module takes the rest of the
# reactor down with it: a plain `gradlew test` on this tree stops at :impl-base
# and 8 modules never report, which reads as a suite that was deleted rather
# than as one module that failed.
# Capped, because this is the one wait inside the verifier whose length the
# candidate writes. Every other long phase already has a limit of its own --
# runner/score.sh caps the family-A build at 1500s and the probe at 1200s, and
# the corroboration caps each class and the whole loop -- but a Test task that
# sleeps would run until Harbor killed the container at
# [steps.verifier] timeout_sec, and a killed container writes no reward.json at
# all. A tree that hangs has to score, not vanish. Every tree measured here
# that builds at all spends 17-42s (gate.testrun.gradle prints the figure on
# every run; the frozen solution is 19s unloaded and 34s under contention, and
# the dearest reading of the lot is 42s, from a tree whose tests fail), so
# 900s is more than twenty times the dearest measured cost and is not a
# threshold any correct tree can feel; it is the
# difference between a bounded 0 and a missing trial. It is `timeout` inside
# `run_unprivileged` for the same reason runner/score.sh gives at its own build
# arm: the kill has to come from a process that is still allowed to signal the
# child.
t_limit="${TESTRUN_LIMIT:-900}"
t_rc=0
t_t0=${SECONDS}
run_unprivileged env -C "${TREE}" timeout -s KILL "${t_limit}" \
   ./gradlew --no-daemon --continue test \
   > "${WORK}/test.log" 2>&1 || t_rc=$?
t_elapsed=$(( SECONDS - t_t0 ))
# `timeout -s KILL` reports 137 on the kill and 124 on the TERM it never gets
# to send here; anything else is the build's own status, which does not decide
# anything on its own -- the reports do.
t_state=ok
if [ "${t_rc}" -eq 137 ] || [ "${t_rc}" -eq 124 ]; then
   t_state=timeout
elif [ "${t_rc}" -ne 0 ]; then
   t_state=failed
fi
[ "${t_rc}" -eq 0 ] || echo "    (gradlew test exited ${t_rc} -- the reports decide, not the exit code)"
fact "gate.testrun.gradle=${t_state},seconds=${t_elapsed},limit=${t_limit}"
tail -25 "${WORK}/test.log" | sed 's/^/    /'
find "${TREE}" -name 'TEST-*.xml' -newer "${marker}" -not -path '*/.gradle/*' \
   2>/dev/null | LC_ALL=C sort > "${WORK}/xml.txt" || : > "${WORK}/xml.txt"
fact "gate.testxml.files=$(grep -c . "${WORK}/xml.txt")"

# The project's own compiled classes, wherever in the tree they ended up,
# mirrored once: B-4 reads them for class-file versions and B-5 for the test
# population.
#
# Selected by CLASS IDENTITY, not by path. The inherited sources are fixed --
# this step migrates a build system, it does not write Java -- so the set of
# top-level classes they compile to is known, recorded once against the pinned
# tree, and shipped as baseline/classnames.txt. A tree .class file is mirrored
# if and only if its fully-qualified name, with any $-suffix stripped, is in
# that list: matched on the path SUFFIX, so the name is read from below
# whatever compiled-output root the candidate's build chose
# (impl-base/build/classes/java/main/org/jboss/.../Foo.class is
# org.jboss.....Foo) and a renamed module directory changes nothing.
#
# The path carve-outs this replaces were load-bearing in both directions and
# wrong in both. Excluding nothing swept in classes that are not the library --
# a buildSrc convention plugin compiles at Gradle's own JVM default, and B-4
# reads a second major version and scores 0 on a tree whose library jars are
# all major 50. Excluding ./.gradle/* was the necessary inverse, because the
# wrapper cache holds third-party classes -- and it was also a hiding place:
# an inherited class compiled to the wrong major and dropped there fell out of
# the mirror entirely. Identity decides both: the buildSrc class is not in the
# list and is ignored wherever it sits, the inherited class is in the list and
# is found wherever it sits, so there is no directory left to hide in.
#
# Mirrored into one scratch root rather than passed as a file list: xargs
# splits a long list across several commands, and each would rewrite the output
# file, so a gate would silently report only the last batch. One root is one
# invocation whatever the tree's size. The matching is one gawk process over
# the whole find, not one test per file.
mirror="${WORK}/treeclasses"
mkdir -p "${mirror}"
: > "${WORK}/classmap.txt"
( cd "${TREE}" 2>/dev/null \
  && find . -type f -name '*.class' -print0 \
     | awk -v names=/tests/baseline/classnames.txt \
           -v classmap="${WORK}/classmap.txt" '
        # RS is switched to NUL only after classnames.txt has been read:
        # getline-from-a-file honours RS too, and setting it first swallowed
        # the whole list as one record -- 322 names became one, every class
        # file was foreign, and the mirror came out empty.
        BEGIN { maxc = 0
                while ((getline n < names) > 0) {
                   if (n == "") continue
                   gsub(/\./, "/", n)
                   allow[n] = 1
                   k = split(n, part, "/")
                   if (k > maxc) maxc = k
                }
                close(names)
                if (maxc == 0) { print "no allowed class names" > "/dev/stderr"; exit 1 }
                RS = "\0"; ORS = "\0"
              }
        {
           path = $0
           if (path == "") next
           # The name below the compiled-output root: drop .class, then drop
           # any $-suffix from the FINAL component only, which is how
           # classnames.txt was recorded. An inner, anonymous or local class is
           # therefore covered by its outer class.
           p = path
           sub(/\.class$/, "", p)
           sub(/\$[^\/]*$/, "", p)
           n = split(p, a, "/")
           s = a[n]
           hit = ""
           for (k = 1; k <= maxc && k <= n - 1; k++) {
              if (k > 1) s = a[n - k + 1] "/" s
              if (s in allow) { hit = s; break }
           }
           if (hit == "") next
           root = substr(p, 1, length(p) - length(hit) - 1)
           gsub(/\//, ".", hit)
           # printf, not print: ORS is NUL for the xargs stream, and the class
           # map is read back a line at a time.
           printf "%s\t%s\n", hit, root >> classmap
           print path
        }' \
     | xargs -0 -r cp --parents -t "${mirror}" ) > "${WORK}/mirror.log" 2>&1
nclass=$(find "${mirror}" -name '*.class' | wc -l)
fact "gate.tree.classfiles=${nclass}"
# What the identity rule turned away, so that a tree which compiled the library
# somewhere this rule cannot see is visible as a number rather than as a
# mysteriously small population.
allclass=$(cd "${TREE}" 2>/dev/null && find . -name '*.class' | wc -l)
fact "gate.tree.classfiles.seen=${allclass:-0}"
fact "gate.tree.classfiles.foreign=$(( ${allclass:-0} - nclass ))"

# B-4  the compiled bytecode is one level, and not below Java 22.  (inherited)
#
# SPLIT, because the level is this rung's must-flip again -- the shape steps
# 05, 06, 07 and 09 all gave this gate. Step 08 put it back together, one
# reader over baseline/level.txt, because its family C had no level leg to
# collide with. Here C-1 asserts 69, so B-4 is a REGRESSION FLOOR one rung
# behind the flip, asking nothing C-1 asks.
#
#   majors.distinct == 1   -- the build is internally consistent. A tree that
#                             compiles its main sources at 69 and its tests at
#                             66 because one project's toolchain block was
#                             missed shows two, and that is a defect whichever
#                             level is "right". C-1 cannot see it: C-1 divides
#                             the class files AT 69 by the total and would read
#                             a mixed tree as partial credit, which for a
#                             fraction is the correct answer to a different
#                             question.
#   major >= HOLD_MAJOR    -- nothing was taken BACK below step 11's rung of
#                             66. Not 69: that is C-1's number and asking it
#                             here would be paying twice for one flip.
#
# HOLD_MAJOR AND NOT A LITERAL, which is where this differs from steps 05, 06
# and 07. Their reason for the literal was that reading baseline/level.txt here
# would be reading family C's number with family C's file. Staging BOTH numbers
# in that file -- `major=69` for C-1 and `was=66` here -- keeps the two
# questions apart while keeping both out of the shell script, so a later rung
# stages one new pair and both gates follow. The fact's own NAME carries the
# figure, so the report says which floor was applied and the Dockerfile can
# assert it.
#
# Everything the build compiled, not only what it published: the twelve module
# jars AND every class file the build left in the tree, which is where the
# compiled test classes are. Scanning the jars alone let a tree emit one major
# for its main sources and another for its tests and still score this gate 1.
b4=0
if [ -s "${WORK}/modulejars.txt" ] || [ "${nclass}" -gt 0 ]; then
   if java -cp "${GCP}" arqgate.Scan "${WORK}/scan.txt" "${mirror}" \
        $(cat "${WORK}/modulejars.txt") > "${WORK}/scan.log" 2>&1; then
      cls=$(sed -n 's/^classes=//p' "${WORK}/scan.txt" | head -1)
      distinct=$(sed -n 's/^majors\.distinct=//p' "${WORK}/scan.txt" | head -1)
      # The level the tree actually compiled to, taken from the scan's own
      # major.<N> lines. There is more than one only when distinct > 1, and the
      # conditional below refuses that case before this is read as an answer.
      onemajor=$(sed -n 's/^major\.\([0-9][0-9]*\)=.*/\1/p' "${WORK}/scan.txt" | head -1)
      mhave=$(sed -n "s/^major\.${onemajor:-0}=//p" "${WORK}/scan.txt" | head -1)
      fact "gate.classes=${cls:-0}"
      fact "gate.majors.distinct=${distinct:-0}"
      fact "gate.major.one=${onemajor:-0}"
      grep '^major\.' "${WORK}/scan.txt" | sed 's/^/    /'
      if [ "${distinct:-0}" != 1 ]; then
         echo "    more than one class-file major version present -> 0"
      elif [ "${onemajor:-0}" -lt "${HOLD_MAJOR}" ]; then
         echo "    class-file major ${onemajor:-0} is below step 11's rung of ${HOLD_MAJOR} -> 0"
      else
         b4=$(frac "${mhave:-0}" "${cls:-0}")
      fi
   else
      fact "gate.scan=broken"
      sed -n '1,10p' "${WORK}/scan.log" | sed 's/^/    /'
   fi
else
   fact "gate.scan=no-classes"
fi
fact "B4.one-major-ge-${HOLD_MAJOR}=${b4}"

# B-5  every inherited test is still there.
#
# Read out of class files, never out of a build's test report: on the pinned
# baseline the Maven reports give three different totals for one run.
b5=0
if [ "${nclass}" -gt 0 ] || [ -s "${WORK}/modulejars.txt" ]; then
   if java -cp "${GCP}" arqgate.Tests "${WORK}/tests.txt" \
        "${mirror}" $(cat "${WORK}/modulejars.txt") > "${WORK}/tests.log" 2>&1; then
      grep '^test\.method=' /tests/baseline/tests.txt | LC_ALL=C sort > "${WORK}/want-tests.txt"
      grep '^test\.method=' "${WORK}/tests.txt"       | LC_ALL=C sort > "${WORK}/got-tests.txt"
      tw=$(grep -c . "${WORK}/want-tests.txt")
      th=$(LC_ALL=C comm -12 "${WORK}/want-tests.txt" "${WORK}/got-tests.txt" | grep -c . || true)
      tdis=$(sed -n 's/^tests\.disabled=//p' "${WORK}/tests.txt" | head -1)
      want_dis=$(sed -n 's/^tests\.disabled=//p' /tests/baseline/tests.txt | head -1)
      fact "gate.tests.want=${tw}"
      fact "gate.tests.present=${th}"
      fact "gate.tests.classes=$(sed -n 's/^tests\.classes=//p' "${WORK}/tests.txt" | head -1)"
      fact "gate.tests.disabled=${tdis:-0}"
      fact "gate.tests.disabled.baseline=${want_dis:-0}"
      LC_ALL=C comm -23 "${WORK}/want-tests.txt" "${WORK}/got-tests.txt" | head -15 \
         | sed 's/^/    absent: /'
      b5=$(frac "${th}" "${tw}")
      # A disabled test is still a test: switching one off must not be a way to
      # keep the population whole, so it is penalised separately.
      if [ "${tdis:-0}" -gt "${want_dis:-0}" ]; then
         echo "    ${tdis} disabled vs ${want_dis} at the baseline -> halved"
         b5=$(awk -v v="${b5}" 'BEGIN { printf "%.6f", v / 2 }')
      fi
   else
      fact "gate.tests=broken"
      sed -n '1,10p' "${WORK}/tests.log" | sed 's/^/    /'
   fi
else
   fact "gate.tests=no-classes"
fi
fact "B5.test-population=${b5}"

# B-6  the public API surface is unchanged.
#
# THE HEADING GOES BACK TO `unchanged`, AFTER ONE RUNG WHERE IT DID NOT. Steps
# 04 through 10 each said "unchanged", each staged an api.txt byte-identical to
# the rung below, and each read 1.000000 on the tree it was handed. Step 11 was
# the exception: it staged 1105 signatures where step 10 staged 1147, because 42
# of step 10's lines described the seven `SecurityActions$GetTcclAction` enums
# its own family C removed, and its B-6 therefore read 0.961991 on the tree its
# candidate was handed. This rung stages step 11's 1105 byte-identical and reads
# 1.000000 on its input again.
#
# AND THAT WAS NOT OBVIOUS, BECAUSE THIS RUNG ALSO DELETES A CLASS FILE. C-4
# turns `ManagerImpl.handledThrowables` into a ScopedValue and the anonymous
# `ThreadLocal` subclass it used to initialise -- `ManagerImpl$1` -- stops being
# emitted: the tree goes from 436 class files to 435, and the jar it leaves is
# impl-base's. Step 11's 42 came out of exactly that kind of deletion, so the
# question had to be asked rather than assumed. The answer is in gates/apisig.sh's own scope rule: ANONYMOUS AND
# LOCAL CLASSES ARE DROPPED, by the `$1`, `$2` naming, because a surface dump
# that counted them would charge every rung for every lambda-shaped rewrite it
# made. `GetTcclAction` was a NAMED nested enum and was in the dump; a class
# with no name in the language is not. Measured, not argued: step 11's verifier
# over this rung's golden reads B-6 at 1.000000 with the same 1105-line file.
# NOTES 16.61.
#
# NOTHING ARRIVES EITHER, AND THAT HALF IS UNCHANGED. No requirement here asks
# for a new public type: an import declaration has no signature at all, a
# ScopedValue over a private field is invisible from outside the class, a JVM
# flag is invisible from outside the process, and the level flip is invisible
# from outside the jar. Every one of the 1105 must be there and nothing else may
# be.
#
# AN ALLOWLIST IS STILL THE WRONG SHAPE, and the note step 11 wrote is kept
# because the reasoning outlives the rung that needed it: B-6 considered one at
# step 05 -- the note is in B-11's block below -- and rejected it there for the
# reason it would be rejected here. A signature that a rung requires to go is a
# DEPARTURE, and a gate that exempts it is a gate that cannot tell a tree which
# removed it from a tree which did not. When the surface has to move, the file
# moves with it and the floor absorbs the difference; it does not move here.
#
# ONE MODIFIER IS NORMALISED, AND THAT IS WHAT baseline/apinorm.txt IS FOR.
# `sealed`, `non-sealed` and the permits clause are attributes rather than
# access flags, and javap does not print them, so the sealed work B-16.4 holds
# is invisible here -- as it should be. `final` is NOT an attribute. It is
# ACC_FINAL, javap prints it, and javac REQUIRES every permitted subtype of a
# sealed type to be one of final, sealed or non-sealed. So a tree that keeps
# B-16.4 correct has twenty-nine published classes `final` and, unnormalised, a
# gate comparing it against a pre-step-09 surface would charge it for each of
# them twice -- once as a line that went missing and once as a line that
# arrived. The normalisation moves 63 lines on the staged side and 63 on the
# candidate's, so the comparison is unaffected -- checked, not assumed, by
# mk-baseline-step11.sh, which staged this file and this file's 40 names. This
# rung stages both unchanged and re-checks nothing, because this rung's family C
# publishes no type, removes no named one and touches no modifier: the surface
# it is comparing is the surface step 11 measured.
#
# THE NORMALISATION IS THE SAME ONE THAT MADE THE BASELINE, AND IT IS SCOPED,
# SYMMETRIC AND LOCAL. Scoped: it strips `final ` from the type header of the
# 40 names in baseline/apinorm.txt and from no other line, so `final` remains a
# graded modifier everywhere else in the surface. Symmetric: the identical awk
# ran over the staged side in mk-baseline-step11.sh, so the two sides are
# compared after the same transformation rather than one being loosened.
# Local: it touches the type HEADER only -- the part javap repeats on every
# member line -- so a method that stops being final is still a member that
# changed.
#
# NONE OF THE 40 IS A `GetTcclAction`, checked rather than assumed: apinorm.txt
# is step 09's list of sealed-hierarchy names and the seven enums are not in
# it, so the 42 leave through the comparison and not through the exemption.
#
# It is here and not in gates/apisig.sh for the reason a gate is not a
# baseline: apisig.sh is staged at every rung from 01 on and dumps what javap
# says. A rung-specific exemption belongs to the rung, in a file the rung
# stages, where the next author can see the list and decide whether it still
# applies.
#
# The charge is otherwise step 08's, unchanged and symmetric -- a member that
# goes missing and a member that arrives cost the same, in either direction.
b6=0
if [ -s "${WORK}/modulejars.txt" ]; then
   if bash /tests/gates/apisig.sh "${WORK}/api.txt" $(cat "${WORK}/modulejars.txt") \
        > "${WORK}/api.log" 2>&1; then
      # normapi -- strip `final ` from the type header of the staged names, and
      # of nothing else. A missing or empty apinorm.txt normalises nothing,
      # which is the strict reading and cannot pay a candidate anything.
      normapi() {
         awk -v names="$(grep -v '^#' /tests/baseline/apinorm.txt 2>/dev/null | grep . | tr '\n' ' ')" '
            BEGIN { nn = split(names, a, " "); for (i = 1; i <= nn; i++) if (a[i] != "") keep[a[i]] = 1 }
            {
               p = index($0, " :: ")
               h = (p ? substr($0, 1, p - 1) : $0)
               t = ""
               nw = split(h, w, " ")
               for (i = 1; i <= nw; i++) if (w[i] == "class" || w[i] == "interface") { t = w[i + 1]; break }
               sub(/<.*/, "", t); sub(/^.*\./, "", t)
               if (t in keep) {
                  gsub(/(^| )final class /, " class ", h)
                  gsub(/(^| )final interface /, " interface ", h)
                  sub(/^ /, "", h)
                  $0 = (p ? h substr($0, p) : h)
               }
               print
            }'
      }
      fact "gate.api.normalised=$(grep -v '^#' /tests/baseline/apinorm.txt 2>/dev/null | grep -c . || true)"
      LC_ALL=C sort /tests/baseline/api.txt > "${WORK}/want-api.txt"
      normapi < "${WORK}/api.txt" | LC_ALL=C sort > "${WORK}/got-api.txt"
      aw=$(grep -c . "${WORK}/want-api.txt")
      ah=$(LC_ALL=C comm -12 "${WORK}/want-api.txt" "${WORK}/got-api.txt" | grep -c . || true)
      ae=$(LC_ALL=C comm -13 "${WORK}/want-api.txt" "${WORK}/got-api.txt" | grep -c . || true)
      fact "gate.api.want=${aw}"
      fact "gate.api.hit=${ah}"
      fact "gate.api.extra=${ae}"
      LC_ALL=C comm -23 "${WORK}/want-api.txt" "${WORK}/got-api.txt" | head -10 \
         | sed 's/^/    absent: /'
      b6=$(clamp01 "$(awk -v h="${ah}" -v e="${ae}" -v w="${aw}" \
            'BEGIN { printf "%.6f", (w <= 0) ? 0 : (h - e) / w }')")
   else
      fact "gate.api=broken"
      sed -n '1,10p' "${WORK}/api.log" | sed 's/^/    /'
   fi
else
   fact "gate.api=no-module-jars"
fi
fact "B6.api-surface=${b6}"

# B-7  the tests actually run, and nothing that passed now fails.
echo
echo "--- B-7: reading the suite's reports"
b7=0
# Declared out here, not inside: the verdict below reads it, and a tree that
# wrote no report at all must not make it an unset variable.
contradicted=0
# The corroboration's skips that are the candidate's own doing, counted apart
# from the ones that are the scorer's or the inherited suite's. A skip the tree
# caused cannot be free: see the block that computes it.
declined=0
# Compiled classes in this tree that can stop the JVM before the corroboration's
# runner reports. Declared out here for the same reason `contradicted` is: the
# verdict reads it whether or not the block that counts it ran.
selfstop=0
if [ -s "${WORK}/xml.txt" ]; then
   # One <testcase ...> per line, each carrying its own <failure>/<error>
   # children, because those are nested inside it.
   #
   # The trailing echo is load-bearing. tr collapses each file to a single line,
   # so sed emits no final newline, and appending 53 files then welds each
   # file's last <testcase> onto the next file's preamble. That is not
   # hypothetical: it turned 160 cases into 108 records, which made 52 inherited
   # tests look as though they had not run and scored a fully migrated tree 0 on
   # a suite where every test passed. Counting one file at a time hid it,
   # because each substitution stood alone. The parser-health check below is
   # what turns a repeat of it into a named scorer fault instead.
   : > "${WORK}/cases.txt"
   : > "${WORK}/raw-cases.txt"
   while IFS= read -r f; do
      { tr '\n' ' ' < "${f}" | sed 's|<testcase |\n<testcase |g' | tail -n +2; echo; } \
         >> "${WORK}/raw-cases.txt"
   done < "${WORK}/xml.txt"
   # A case reported <skipped/> is not a pass when the frozen baseline PASSED
   # it. Disabling a test at run time -- an assumption that cannot hold, an
   # OS or JRE condition, a condition method -- writes <skipped/> exactly as a
   # static @Disabled does, and arqgate.Tests counts only the static switches;
   # the corroboration below skips such a case too, so nothing else would
   # contradict it. Read as fail here, it is a new failure and B-7 charges it.
   # Ids the baseline itself skipped (the one skipped method, and the three
   # class-level ids of the @Ignore-d classes, whose methods Gradle reports
   # one by one under ids the baseline never names) are untouched: the golden
   # tree reports 7 skipped cases and none of them is a baseline `pass` id.
   # (Trajectory review 2026-10-06, closing round 3 G13.)
   awk -v basef=/tests/baseline/testresults.txt '
      BEGIN {
         while ((getline bl < basef) > 0) {
            split(bl, bfld, " ")
            if (bfld[2] == "pass") basepass[bfld[1]] = 1
         }
         close(basef)
      }
      {
         cls = ""; nm = ""
         if (match($0, /classname="[^"]*"/))  cls = substr($0, RSTART + 11, RLENGTH - 12)
         if (match($0, /[ ]name="[^"]*"/))    nm  = substr($0, RSTART + 7,  RLENGTH - 8)
         if (cls == "" || nm == "") next
         # Jupiter writes the method name WITH its parentheses --
         # name="shouldEnrichTestInstance()" -- and the vintage and testng
         # engines write it without. The frozen baseline was recorded from a
         # 2011 surefire run and so carries neither. Measured on the golden
         # tree: 160 <testcase> elements, of which 155 carry a trailing () and
         # the 5 that do not are exactly the vintage compat-surface cases.
         # Without this one substitution every Jupiter case reads as an id the
         # baseline never mentions, uncovered comes out at 155, and a fully
         # migrated tree scores B-7 = 0.
         sub(/\(\)$/, "", nm)
         bad = (index($0, "<failure") > 0 || index($0, "<error") > 0) ? "fail" : "pass"
         if (bad == "pass" && index($0, "<skipped") > 0 && ((cls "#" nm) in basepass)) bad = "fail"
         print cls "#" nm, bad
      }' "${WORK}/raw-cases.txt" > "${WORK}/parsed.txt"
   LC_ALL=C sort -u "${WORK}/parsed.txt" > "${WORK}/cases.txt"
   # Two independent measurements of one number, kept apart on purpose. nparsed
   # is how many <testcase> elements the parser recovered and must equal what is
   # in the files; ncases is how many distinct tests those describe, and may
   # legitimately be smaller if a suite reports an id twice. Comparing the
   # deduplicated count against the raw one would confuse a parser fault with a
   # suite that repeats a name.
   nparsed=$(grep -c . "${WORK}/parsed.txt" || true)
   nraw=$(xargs -a "${WORK}/xml.txt" -r grep -oh '<testcase ' 2>/dev/null | grep -c . || true)
   awk '{print $1}' "${WORK}/cases.txt" | LC_ALL=C sort -u > "${WORK}/ran-ids.txt"
   ncases=$(grep -c . "${WORK}/ran-ids.txt" || true)
   fact "gate.testrun.elements=${nraw}"
   fact "gate.testrun.parsed=${nparsed}"
   awk '$2 == "fail" {print $1}' "${WORK}/cases.txt" | LC_ALL=C sort -u > "${WORK}/failed.txt"
   # Two sources. What the frozen baseline itself did not pass, and the tests
   # measured to depend on JVM class-loading order -- those cannot be scored as
   # pass-or-fail, and B-5 is what stops a candidate from simply deleting them.
   { awk '$2 == "failure" || $2 == "error" {print $1}' /tests/baseline/testresults.txt
     grep -v '^#' /tests/baseline/orderdep.txt | grep .
   } | LC_ALL=C sort -u > "${WORK}/allowed-failures.txt"
   fact "gate.testrun.allowed.orderdep=$(grep -v '^#' /tests/baseline/orderdep.txt | grep -c . || true)"
   LC_ALL=C comm -23 "${WORK}/failed.txt" "${WORK}/allowed-failures.txt" \
      > "${WORK}/newfail.txt"
   newfail=$(grep -c . "${WORK}/newfail.txt" || true)
   fact "gate.testrun.cases=${ncases}"
   fact "gate.testrun.failed=$(grep -c . "${WORK}/failed.txt")"
   fact "gate.testrun.newfail=${newfail}"
   fact "gate.testrun.allowed=$(grep -c . "${WORK}/allowed-failures.txt")"
   head -15 "${WORK}/newfail.txt" | sed 's/^/    new failure: /'
   # The raw reading, and the one B-7 charges, kept apart. What is charged is
   # settled below, after the corroboration has run these same classes itself: a
   # reported failure the scorer's own run of that class passes is two runs of
   # one tree disagreeing, not a test this step broke. Until then the charge is
   # the raw reading, so a corroboration that does not run leaves this gate
   # exactly as strict as it was before the excusal existed.
   newfail_charged=${newfail}
   # Coverage of the inherited suite, not a count of reports. A count is
   # satisfied by any 150 ids at all, so a `test` task that wrote fabricated
   # reports of its own invention scored this gate 1 without running a single
   # inherited test. What the step asks is that every test that runs today
   # still runs, so what is measured is that the ids the frozen baseline
   # reported are all present in what this run reported.
   #
   # Identity is by <class>#<method>, with one measured exception. Surefire
   # collapses a class whose every method is @Ignore'd into a single pseudo-case
   # named after the class itself -- `C#C` -- and Gradle instead writes each
   # method as skipped. Measured on the golden tree: of the baseline's 157 ids,
   # 154 are reported by name and the other 3 are exactly those pseudo-cases,
   # which appear as the 6 methods of their three classes. So an id whose method
   # part is its own class name is covered by any case reported for that class,
   # and every other id must be reported by name. Nothing is dropped: those
   # three classes must still report, and B-5 separately requires all 164
   # methods to exist and no more of them to be switched off than before.
   #
   # FILENAME and not the usual NR == FNR: when the first file is empty awk
   # never reads a record from it, so NR == FNR stays true for every line of
   # the second and the whole baseline is swallowed as though it had been
   # reported. That is exactly the case this gate exists to catch -- a tree
   # whose reports name no test at all came out as "nothing uncovered" and
   # scored 1.
   awk -v idfile="${WORK}/ran-ids.txt" '
      FILENAME == idfile {
         gotid[$0] = 1
         c = $0; sub(/#.*$/, "", c); gotcls[c] = 1
         next
      }
      {
         id = $1
         c = id; sub(/#.*$/, "", c)
         m = id; sub(/^[^#]*#/, "", m)
         if (m == c) { if (!(c in gotcls)) print id }
         else if (!(id in gotid)) print id
      }' "${WORK}/ran-ids.txt" /tests/baseline/testresults.txt \
      | LC_ALL=C sort -u > "${WORK}/uncovered.txt"
   want_ids=$(awk '{print $1}' /tests/baseline/testresults.txt | LC_ALL=C sort -u | grep -c . || true)
   uncovered=$(grep -c . "${WORK}/uncovered.txt" || true)
   fact "gate.testrun.baseline.ids=${want_ids}"
   fact "gate.testrun.uncovered=${uncovered}"
   head -15 "${WORK}/uncovered.txt" | sed 's/^/    did not run: /'

   # --- corroboration: the scorer runs the inherited tests itself -------------
   #
   # Everything above this point reads TEST-*.xml that the candidate's own build
   # wrote, inside a tree the candidate controls. That is a claim, not a
   # measurement: a `test` task can be written to emit a green report for a case
   # that failed or never ran, and the reader above counts it as a pass. So the
   # scorer runs a set of the inherited test classes itself -- its own JVM, its
   # own classpath, its own parse of the output -- and compares the two.
   #
   # Only one direction of disagreement is charged. A case the scorer's own run
   # FAILED while the report calls it passed is a CONTRADICTION: one of the two
   # is fabricated, and the report is the one the candidate wrote. The other
   # direction is not evidence of anything -- a report that admits a failure is
   # already counted by newfail, and a case that fails there and passes here is
   # the flakiness the order-dependent test documents.
   #
   # Everything here is defensive on purpose. The compiled-output root and the
   # module directory are resolved from the TREE and never from directory names,
   # because the candidate chooses both; and when either cannot be resolved, or
   # the JVM will not load the class, or the run produces output this parser does
   # not recognise, the class is SKIPPED and counted -- never failed. A
   # corroboration harness that can take a gate down by not working is worse than
   # no harness at all, so a broken one says it is broken and contributes zero
   # contradictions.
   c_classes=0; c_ran=0; c_ok=0; c_fail=0; c_classfail=0
   cs_noroot=0; cs_dup=0; cs_nosrc=0; cs_init=0; cs_timeout=0; cs_other=0; cs_budget=0
   cs_nested=0; cs_exited=0; cs_unloadable=0; cs_testngonly=0
   corrob_state=scorer-broken
   : > "${WORK}/contradicted.txt"
   : > "${WORK}/corrob-failed.txt"
   : > "${WORK}/corrob-passed.txt"
   mkdir -p "${WORK}/corrob"
   # The class list is the scorer's own, read from the frozen baseline: a
   # corroboration whose scope the candidate picks is not a corroboration. A
   # class the tree no longer has simply fails to load and is skipped here --
   # B-5 is the gate that charges for its absence.
   sed -n 's/^test\.class=//p' /tests/baseline/tests.txt | LC_ALL=C sort -u \
      > "${WORK}/corrob-classes.txt"
   # The classes the frozen 2011 run recorded at least one case for. Two of the
   # 55 are not among them -- the nested fixture class JUnitIntegrationTestCase
   # $ArquillianClass1, and TestNGDeploymentAppenderTestCase -- and those two are
   # the whole of the inherited suite's shape that this runner cannot speak for.
   # Every other class in the list demonstrably ran under a JUnit runner once,
   # so a class in this file that will not load now is this tree's doing.
   sed -n 's/^\([^#]*\)#.*/\1/p' /tests/baseline/testresults.txt | LC_ALL=C sort -u \
      > "${WORK}/baseline-cased.txt"
   # ...with one exception, and it is in a file of its own with its derivation
   # written out: a class every one of whose recorded cases came out of the
   # TestNG provider is a class this harness cannot re-run on ANY tree, the
   # golden one included, so "will not load now" says nothing about it. See
   # baseline/testng-cased.txt and the `notloadable` arm below.
   grep -v '^#' /tests/baseline/testng-cased.txt | grep . | LC_ALL=C sort -u \
      > "${WORK}/testng-cased.txt"
   # The exemption only ever shrinks the charged set, so it has to be a subset
   # of the cased set: a name in it that the frozen run recorded nothing for
   # would exempt something this reasoning does not cover. Asserted at image
   # build time; re-checked here and, on drift, thrown away rather than
   # honoured, so the failure mode is the charge as it stood before this file
   # existed and not a free pass under a stale name.
   if [ -n "$(LC_ALL=C comm -23 "${WORK}/testng-cased.txt" "${WORK}/baseline-cased.txt")" ]; then
      echo "    scorer fault: baseline/testng-cased.txt names a class the frozen run" \
           "recorded no case for -- exemption discarded"
      : > "${WORK}/testng-cased.txt"
   fi
   # The module directory, by structure rather than by name: D is the module
   # directory of class C when D/src/test/java/<C as a path>.java exists. One
   # walk of the tree builds the whole map, because 55 walks cost 55 times as
   # much and answer the same question. CWD matters -- one inherited case reads
   # src/test/resources/tooling/arquillian.xml through a relative path and fails
   # anywhere else.
   find "${TREE}" -path '*/src/test/java/*' -name '*.java' -type f 2>/dev/null \
      | awk -F'/src/test/java/' 'NF == 2 { p = $2; sub(/\.java$/, "", p); gsub(/\//, ".", p)
                                          print p "\t" $1 }' \
      | LC_ALL=C sort -u > "${WORK}/testsrc.txt"
   # The candidate's twelve module jars carry the main classes; assets/testlib
   # carries what the migrated TEST classes need at run time. Neither is read
   # out of the tree's build scripts.
   #
   # testlib and not probe-lib, which is where step 01 got this. probe-lib is
   # the 2011 closure of the MAIN code -- junit 4.8.1, mockito 1.8.x, testng
   # 5.14 -- and a test class compiled against Jupiter and Mockito 4 does not
   # load against it. testlib is that same closure with the two test libraries
   # replaced at the versions the instruction names, and Jupiter itself left out
   # because the harness jar below already carries it and must be the copy that
   # wins. 76 jars, asserted at image build time.
   cpjars=$( { LC_ALL=C sort "${WORK}/modulejars.txt"
               find /tests/assets/testlib -name '*.jar' | LC_ALL=C sort
             } | tr '\n' ':' | sed 's/:$//' )
   # The runner the scorer invokes comes off the scorer's own shelf, ahead of
   # everything else, because everything else on the classpath below is the
   # candidate's: the compiled output root, the two resource directories the
   # inherited tests need in order to find their fixtures, and the module jars.
   # A class file dropped into any of them shadows whatever comes after it, and
   # org/junit/platform/console/ConsoleLauncher.class under
   # <module>/src/test/resources is enough to replace the platform with a
   # program of the candidate's own that prints a green summary for every class
   # it is handed. Step 01 measured the JUnitCore version of exactly that: a
   # tree that did it and laundered one genuinely failing case out of its XML
   # scored 1.000000 against the oracle's 1.000000 (ctlD1).
   #
   # junit-platform-console-standalone is one shaded jar carrying the launcher,
   # the Jupiter API and engine, the vintage engine, JUnit 4, opentest4j,
   # apiguardian and hamcrest -- so putting it first puts the whole harness,
   # both engines and both assertion APIs out of the tree's reach in one move.
   # Nothing a correct tree does needs to override any of them, so this costs a
   # correct tree nothing.
   #
   # --class-path is NOT used to pass the candidate's roots, and that is a
   # measured decision rather than a style one. ConsoleLauncher's --class-path
   # builds a child URLClassLoader for what it is given; java.protocol.handler.pkgs
   # resolves a URL stream handler through ClassLoader.getSystemClassLoader(),
   # so jboss-vfs's `vfs:` handler becomes invisible from there and
   # OSGiDeploymentPackagerTestCase#testValidBundle fails against a tree where
   # nothing is wrong. Everything goes on the system -cp instead, harness first.
   harnessjars=$(find /tests/assets/harness -name 'junit-platform-console-standalone-*.jar' \
                   2>/dev/null | LC_ALL=C sort | tr '\n' ':' | sed 's/:$//')
   # A corroboration reads what a JVM prints while the inherited test runs inside
   # that same JVM. Anything in the tree that can stop the JVM before the runner
   # speaks can put whatever it likes at the end of the capture and have the exit
   # status agree with it: ctlD7 printed a well-formed green JUnitCore tail and
   # halted with 0, and scored 1.000000 against the oracle's 1.000000. No nonce
   # fixes that from inside -- the test can read anything the scorer hands its
   # own runner -- so it is settled outside the JVM, on the artifact.
   #
   # arqgate.SelfStop is ASM over the same artifacts this classpath loads, and it
   # matches an actual invoke of an actual method on an actual owner. The first
   # version of this check was two `grep -qaF` over raw class bytes and was wrong
   # at both edges: `java/lang/RuntimeException` contains `java/lang/Runtime`, so
   # a class that threw and logged was charged; and it walked loose `.class`
   # files only, so a helper packed into a module jar with the loose copy deleted
   # was invisible. Four routes are counted -- Runtime.halt/exit, System.exit,
   # Unsafe, and a class holding both halves of a reflective call -- plus
   # System.load/loadLibrary, since a native library can call _exit where no
   # Java-side scan would see it. The reflective route is a conjunction: the
   # machinery (Class.forName / getMethod / invoke / setAccessible /
   # Lookup.find*), a stop member name as a constant, AND the owner. The owner
   # half is read three ways -- as a Utf8 (`"java.lang.Runtime"`), as a Type or
   # MethodHandle constant (`Runtime.class`), and as the owner of an invoke
   # (`Runtime.getRuntime()`). None of the three is ever a hit on its own.
   # arqgate/SelfStop.java's class javadoc is the specification; the eleven
   # rehearsal faces in this step's Dockerfile are the enforcement.
   #
   # The roots are the whole tree and the twelve module jars, which is exactly
   # the candidate's contribution to the classpath above. Not probe-lib: that is
   # the scorer's own 2011 closure. Not jars found by recursion either -- a tree
   # can hold a cache of third-party jars, and half of Maven Central calls
   # System.exit somewhere, so charging those would be charging the ecosystem.
   # The whole tree is walked and not the mirror: the mirror holds only names the
   # frozen list knows, and ctlE3 moves the halt one compilation unit away into a
   # class that list has never heard of, scoring 1.000000 before this existed.
   #
   # Measured over the golden tree: 468 classes, 0 hits, 0 unreadable. A correct
   # migration pays nothing for any of the five routes.
   selfstop_state=ok
   # Twice, then give up. The scanner catches Throwable per class and per root,
   # so a corrupt class file or an unopenable jar costs that artifact and nothing
   # else; a failure of the process itself is the scorer's, and a broken scorer
   # does not get to charge whoever happens to be under it.
   if ! java -cp "${GCP}" arqgate.SelfStop "${WORK}/selfstop.txt" "${TREE}" \
           $(cat "${WORK}/modulejars.txt") > "${WORK}/selfstop.log" 2>&1; then
      if ! java -cp "${GCP}" arqgate.SelfStop "${WORK}/selfstop.txt" "${TREE}" \
              $(cat "${WORK}/modulejars.txt") > "${WORK}/selfstop.log" 2>&1; then
         selfstop_state=broken
      fi
   fi
   if [ "${selfstop_state}" = ok ]; then
      selfstop_hits=$(grep -c . "${WORK}/selfstop.txt" || true)
      selfstop_seen=$(sed -n 's/^arqgate\.selfstop: .*classes=\([0-9][0-9]*\) .*/\1/p' \
                         "${WORK}/selfstop.log" | head -1)
      selfstop_unread=$(grep -c . "${WORK}/selfstop.txt.unreadable" 2>/dev/null || true)
      # Three ways this tree fails to answer the question, and all three count.
      # A hit is the answer being yes. An artifact the reader could not parse is
      # the answer being unavailable, and the frozen tree has none. And a walk
      # that reached fewer class files than are in the tree is a walk that did
      # not finish -- a deep enough directory overflows the stack, a large enough
      # class file exhausts the heap -- because a scanner the thing it is
      # scanning can switch off is not a gate.
      #
      # `allclass` is the check on that last one, and it has to be `allclass` and
      # not `nclass`: `nclass` counts the mirror, which by construction holds only
      # names on the frozen list, so a tree could carry a hundred thousand class
      # files and satisfy it after reading 468. `allclass` is a plain `find` over
      # the whole tree, taken independently further up, which is what makes it
      # usable here. Read-and-failed is added to read-and-parsed on the left, so
      # a corrupt class file is charged once as unreadable, not again as a hole.
      selfstop=$(( selfstop_hits + selfstop_unread ))
      selfstop_reached=$(( ${selfstop_seen:-0} + ${selfstop_unread:-0} ))
      if [ "${selfstop_reached}" -lt "${allclass:-0}" ]; then
         selfstop=$(( selfstop + allclass - selfstop_reached ))
      fi
   else
      # Twice. Every per-class and per-root failure is caught inside the scanner,
      # and the image build asserts this class is in the jar, so what is left is
      # this tree. The precedent is arqgate.Scan's: a gate that cannot read the
      # tree scores the gate 0 rather than guessing in the tree's favour.
      selfstop=1
   fi

   c_start=${SECONDS}
   # Named, because the verdict below has to compare the clock against it.
   # 300 against a golden corroboration that finishes all 54 runnable classes
   # in 66 seconds under the platform -- measured on the pinned golden tree,
   # 65 s between the first and the last capture plus the first class's own run,
   # against step 01's 21 s for the same 54 classes under JUnitCore. The
   # difference is one JVM start per class discovering two engines rather than
   # none. The slowest single inherited class is still two orders of magnitude
   # inside the per-class limit, and a timeout is charged, so the margin has to
   # be one a loaded machine cannot eat.
   c_limit=300
   if [ "$(grep -c . "${WORK}/modulejars.txt" || true)" -lt 12 ]; then
      # Without the module jars the inherited tests cannot resolve the classes
      # they test, so every class would skip; say so once instead of 55 times.
      corrob_state=no-module-jars
   elif [ ! -s "${WORK}/corrob-classes.txt" ] || [ -z "${harnessjars}" ]; then
      # The scorer's own frozen class list and the scorer's own junit jar, both
      # asserted present when the image is built. Missing here means the scorer
      # is broken, and a broken scorer does not get to charge whoever happens to
      # be under it.
      corrob_state=scorer-broken
   elif [ ! -s "${WORK}/testsrc.txt" ] || [ ! -s "${WORK}/classmap.txt" ]; then
      # Both of these are properties of the TREE, not of the scorer: no test
      # source under src/test/java anywhere in it, or not one compiled class the
      # frozen list recognises. Either one switches this whole block off in a
      # single move, so either one has to be charged rather than waved through.
      corrob_state=declined
   else
      corrob_state=ok
      while IFS= read -r fq; do
         [ -n "${fq}" ] || continue
         # A whole-run budget as well as a per-class one: 55 classes that each
         # sit just inside the per-class timeout would hold the scorer for four
         # hours. 900 against the golden tree's 66 seconds under this harness,
         # and charged for the same reason a timeout is: the only thing that
         # spends this budget is the tree's own inherited tests taking it.
         if [ "$(( SECONDS - c_start ))" -ge 900 ]; then
            cs_budget=$(( cs_budget + 1 )); continue
         fi
         roots=$(awk -F'\t' -v c="${fq}" '$1 == c { print $2 }' "${WORK}/classmap.txt" \
                   | LC_ALL=C sort -u)
         nroot=$(printf '%s\n' "${roots}" | grep -c . || true)
         if [ "${nroot}" -eq 0 ]; then
            # classnames.txt records top-level names, so a nested test class --
            # arqgate.Tests reports one, JUnitIntegrationTestCase$ArquillianClass1
            # -- has no root of its own. It is a fixture class the outer test
            # drives, the frozen baseline never reported it as a case of its own,
            # and so it can never be one of the passes this block checks. Counted
            # separately from a genuinely unlocatable class.
            case "${fq}" in
               *'$'*) cs_nested=$(( cs_nested + 1 )) ;;
               *)     cs_noroot=$(( cs_noroot + 1 )) ;;
            esac
            continue
         fi
         # Two compiled copies of one test class and the scorer cannot tell which
         # one the build produced. Running either would let the other answer for
         # it, so neither runs and the class is counted: B-4 and B-5 still see
         # both copies, and this number says why corroboration passed the class
         # over.
         if [ "${nroot}" -gt 1 ]; then cs_dup=$(( cs_dup + 1 )); continue; fi
         rootrel=${roots#./}
         root="${TREE}/${rootrel}"
         md=$(awk -F'\t' -v c="${fq}" '$1 == c { print $2 }' "${WORK}/testsrc.txt" | head -1)
         if [ -z "${md}" ] || [ ! -d "${md}" ] || [ ! -d "${root}" ]; then
            cs_nosrc=$(( cs_nosrc + 1 )); continue
         fi
         c_classes=$(( c_classes + 1 ))
         cout="${WORK}/corrob/${fq}.txt"
         # One run of one class. Sets crc and celapsed, and writes the capture
         # to $1. setpriv outside timeout, so that a class that hangs is killed
         # rather than orphaned while its supervisor dies. The tree is writable
         # by SCORE_UID on purpose: one inherited case writes target/ under the
         # CWD.
         crd="${WORK}/corrob/${fq}.reports"
         run_class() {
            local t0=${SECONDS}
            crc=0
            # Emptied and handed over before every run, including the retry: the
            # verdict treats a missing engine report as evidence, so a report
            # left behind by the previous attempt would answer for this one.
            # Owned by SCORE_UID because the JVM that writes it is not root.
            rm -rf "${crd}"; mkdir -p "${crd}"
            chown "${SCORE_UID}:${SCORE_GID}" "${crd}" 2>/dev/null || :
            # --details=summary and not --details=none: none suppresses the
            # twelve-line counter block the verdict parses. --fail-if-no-tests
            # is what turns "this class has nothing to run" into rc=2 rather
            # than into a green empty run. --reports-dir is the second channel.
            run_unprivileged env -C "${md}" timeout -s KILL "${c_limit}" \
               java -cp "${harnessjars}:${root}:${md}/src/test/resources:${md}/src/main/resources:${cpjars}" \
                    org.junit.platform.console.ConsoleLauncher execute \
                    --select-class="${fq}" --details=summary --disable-ansi-colors \
                    --fail-if-no-tests --reports-dir="${crd}" > "$1" 2>&1 || crc=$?
            celapsed=$(( SECONDS - t0 ))
         }
         # 124 and 137 are what `timeout` reports when it gives up on a class.
         # They are also two of the 256 values Runtime.halt() will hand the
         # shell on request, and the clock is what tells the two apart: the
         # supervisor fires at c_limit seconds and never before, so a timeout
         # status that arrives early was chosen by the tree. Measured: a tree
         # whose failing case halted with 137 on sight of the scorer had that
         # class skipped for free and scored 1.000000 (review round 3).
         timed_out() {
            { [ "${crc}" -eq 124 ] || [ "${crc}" -eq 137 ]; } \
               && [ "${celapsed}" -ge "${c_limit}" ]
         }
         # One class, corroborated: the run, the verdict, and the second run a
         # charge has to survive. Sets cverdict -- to `timeout` when the
         # supervisor took the class away from us, which is a word
         # platform_verdict cannot produce, so the caller can tell the two apart.
         #
         # Three verdicts are re-run, and they are exactly the three this block
         # charges for. `exited` is the one a loaded machine could hand an
         # innocent tree: an OOM kill from outside leaves the same silent capture
         # a halt() does. `fail` and `classfail` are the two that become
         # contradictions, and the inherited suite has at least one case that can
         # fail from a race rather than from a defect -- measured, not supposed:
         # ProtocolTestCase#shouldReturnThrownException failed once in 9 whole-
         # suite runs of the golden tree and passed in this block's own run of
         # that class in the same container (NOTES 10.14). A trap repeats and a
         # hiccup does not, so the charge has to survive a second run -- and if
         # the second run produces a verdict, that is the one that counts.
         #
         # A second run costs a correct tree nothing: it happens only when a
         # verdict is about to be charged, and on a tree where nothing is wrong
         # none is.
         corroborate_class() {
            run_class "${cout}"
            if timed_out; then cverdict=timeout; return; fi
            cverdict=$(platform_verdict "${crd}" "${cout}" "${crc}" "${fq}" \
                                        "${WORK}/corrob/${fq}.heads")
            case "${cverdict%% *}" in
               exited|fail|classfail) ;;
               *) return ;;
            esac
            run_class "${cout}.retry"
            if timed_out; then cverdict=timeout; return; fi
            cverdict=$(platform_verdict "${crd}" "${cout}.retry" "${crc}" "${fq}" \
                                        "${WORK}/corrob/${fq}.heads")
         }
         corroborate_class
         if [ "${cverdict}" = timeout ]; then cs_timeout=$(( cs_timeout + 1 )); continue; fi
         cvkind=${cverdict%% *}
         cvn=${cverdict##* }
         [ "${cvn}" = "${cvkind}" ] && cvn=0
         case "${cvkind}" in
            pass)
               c_ran=$(( c_ran + cvn )); c_ok=$(( c_ok + 1 ))
               corrob_skipped_ids "${crd}" "${fq}" >> "${WORK}/corrob-failed.txt"
               corrob_passed_ids "${crd}" "${fq}" >> "${WORK}/corrob-passed.txt" ;;
            fail)
               # The heads file is written from the platform's own engine XML,
               # in the same `<class>#<method>` form and under the same trailing
               # `()` normalisation the report reader above uses -- one identity
               # rule, applied to both channels.
               c_ran=$(( c_ran + cvn )); c_fail=$(( c_fail + 1 ))
               cat "${WORK}/corrob/${fq}.heads" >> "${WORK}/corrob-failed.txt"
               corrob_skipped_ids "${crd}" "${fq}" >> "${WORK}/corrob-failed.txt"
               # A red class still ran its green methods, and this run is what
               # can speak for them: the ids it passed go in beside the ones a
               # class that came out wholly green contributes.
               corrob_passed_ids "${crd}" "${fq}" >> "${WORK}/corrob-passed.txt" ;;
            classfail)
               # The platform blamed the container and no test in it -- a
               # @BeforeAll, a class-level extension, or a static initialiser
               # threw. The class did not pass, so every
               # case the report claims passed for it is contradicted by this
               # run, exactly as a named method failure is. Left uncharged, it
               # is one more way to buy the class out of the comparison.
               c_ran=$(( c_ran + cvn )); c_fail=$(( c_fail + 1 ))
               c_classfail=$(( c_classfail + 1 ))
               awk -F'#' -v c="${fq}" '$1 == c' "${WORK}/ran-ids.txt" \
                  >> "${WORK}/corrob-failed.txt" ;;
            notloadable)
               # rc=2: no engine on this classpath found anything to run here.
               # Not loadable, no runnable methods, or written for a framework
               # neither engine speaks -- the harness jar carries the Jupiter and
               # vintage engines and no TestNG one, so a class that is still pure
               # TestNG lands exactly here. That is the scorer's choice of
               # engines and not the candidate's doing -- but only for a class
               # the frozen run never recorded a case for. A class that produced
               # cases in 2011 and is undiscoverable now lost its runnable
               # methods, or threw on the way in, in THIS tree; that is charged
               # below as `unloadable`. Measured on the golden tree: 54 classes,
               # 54 discovered, 0 in this bucket -- including the two
               # arquillian-testng test classes, which the golden solution
               # deliberately does NOT rewrite: they are compatibility surface.
               # They are discovered here because their OWN test methods are
               # JUnit 4 (org.junit.Test) and the vintage engine speaks that.
               # What stays TestNG in that module is the two
               # @org.testng.annotations.Test FIXTURE methods TestNGTestRunner is
               # pointed at -- and those two are exactly what the frozen 2011 run
               # recorded cases for, which is why requirement 8 is about them and
               # not about the class's own tests.
               #
               # A candidate can build that pure-TestNG tree on purpose, and one
               # did: blind run m04-s02-blind moved TestNGTestRunnerTestCase's
               # own two tests onto TestNG as well, which is how it got the two
               # fixture ids back into the XML after step 01's tree had dropped
               # them. Requirements 7 and 8 both allow it. The class then lands
               # here -- and it is in baseline-cased.txt, so it used to be
               # charged `unloadable` for a tree that had done the step BETTER.
               # The third bucket below is that correction: for a class every
               # recorded case of which came out of the TestNG provider, "no
               # JUnit engine discovers it" is true on the golden tree's version
               # of those ids too, so the charge was measuring loadability as a
               # proxy and corroborating nothing. See baseline/testng-cased.txt.
               # Measured while the whole bucket was free: a laundered
               # failing case that printed JUnit's own initializationError
               # transcript and halted scored 1.000000 against the oracle's
               # 1.000000 (ctlD8, review round 3).
               if ! LC_ALL=C grep -qxF "${fq}" "${WORK}/baseline-cased.txt"; then
                  cs_init=$(( cs_init + 1 ))
               elif LC_ALL=C grep -qxF "${fq}" "${WORK}/testng-cased.txt"; then
                  cs_testngonly=$(( cs_testngonly + 1 ))
               else
                  cs_unloadable=$(( cs_unloadable + 1 ))
               fi ;;
            fault)
               cs_other=$(( cs_other + 1 )) ;;
            *)
               cs_exited=$(( cs_exited + 1 )) ;;
         esac
      done < "${WORK}/corrob-classes.txt"
   fi
   c_elapsed=$(( SECONDS - c_start ))
   fact "gate.testrun.corroborate=${corrob_state}"
   fact "gate.testrun.corroborate.classes=${c_classes}"
   fact "gate.testrun.corroborate.seconds=${c_elapsed}"
   if [ "${corrob_state}" = ok ]; then
      awk '$2 == "pass" { print $1 }' "${WORK}/cases.txt" | LC_ALL=C sort -u \
         > "${WORK}/xml-passed.txt"
      grep -v '^#' /tests/baseline/orderdep.txt | grep . | LC_ALL=C sort -u \
         > "${WORK}/orderdep-ids.txt"
      LC_ALL=C sort -u "${WORK}/corrob-failed.txt" > "${WORK}/corrob-failed-u.txt"
      # Excused: the one case measured to depend on JVM class-loading order, and
      # nothing else. Not the wider allowed-failures set -- those are excused as
      # failures because the frozen baseline did not pass them either, and a
      # report claiming one of them PASSED while the scorer's own run fails it is
      # precisely the fabrication this block exists to catch.
      LC_ALL=C comm -12 "${WORK}/corrob-failed-u.txt" "${WORK}/xml-passed.txt" \
         > "${WORK}/corrob-disagree.txt"
      LC_ALL=C comm -23 "${WORK}/corrob-disagree.txt" "${WORK}/orderdep-ids.txt" \
         > "${WORK}/contradicted.txt"
      contradicted=$(grep -c . "${WORK}/contradicted.txt" || true)
      fact "gate.testrun.corroborate.failed=$(grep -c . "${WORK}/corrob-failed-u.txt" || true)"
      # Printed for the same reason the excused order-dependent case is: a
      # class-level failure charges every case the report claims passed for that
      # class, and a charge that does not say where it came from cannot be
      # argued with. The golden tree's count is 0.
      fact "gate.testrun.corroborate.classfail=${c_classfail}"
      fact "gate.testrun.corroborate.disagree=$(grep -c . "${WORK}/corrob-disagree.txt" || true)"
      # The same comparison, the other way round. A case the report calls FAILED
      # that the scorer's own run of that class PASSED is not a test this step
      # broke: two runs of one tree disagreed, and the one the scorer watched --
      # its own JVM, its own classpath, its own parse -- is the one it can speak
      # for. Measured: the step-02 harbor oracle reported
      # ProtocolTestCase#shouldReturnThrownException failed while this block's
      # own run of that class passed all five of its cases in the same
      # container, and the step scored 0.982500 for it (NOTES 10.14).
      #
      # This does not soften the gate in the direction that can be exploited. A
      # candidate gains nothing by having its own suite report a failure: the
      # excusal can only ever hand back a gate that reading had already lost,
      # and it is the candidate's own report that has to admit the failure
      # first. A test the migration genuinely breaks fails in this block's run
      # too, where the candidate chooses neither the runner nor the classpath --
      # and the ids here are only the ones this run executed and did not fail,
      # so a class it skipped, refused or never reached excuses nothing.
      LC_ALL=C sort -u "${WORK}/corrob-passed.txt" > "${WORK}/corrob-passed-u.txt"
      LC_ALL=C comm -12 "${WORK}/newfail.txt" "${WORK}/corrob-passed-u.txt" \
         > "${WORK}/newfail-excused.txt"
      LC_ALL=C comm -23 "${WORK}/newfail.txt" "${WORK}/corrob-passed-u.txt" \
         > "${WORK}/newfail-charged.txt"
      newfail_charged=$(grep -c . "${WORK}/newfail-charged.txt" || true)
      fact "gate.testrun.corroborate.passed=$(grep -c . "${WORK}/corrob-passed-u.txt" || true)"
      fact "gate.testrun.newfail.excused=$(grep -c . "${WORK}/newfail-excused.txt" || true)"
      fact "gate.testrun.newfail.charged=${newfail_charged}"
      # Printed for the reason the order-dependent excusal is printed: an
      # exclusion that fires silently cannot be argued with.
      head -15 "${WORK}/newfail-excused.txt" \
         | sed "s/^/    excused -- the report fails it, the scorer's own run passes it: /"
      # Printed, because an exclusion that fires silently cannot be checked: this
      # is the only line that distinguishes "the order-dependent case disagreed
      # and was excused" from "nothing disagreed at all".
      LC_ALL=C comm -12 "${WORK}/corrob-disagree.txt" "${WORK}/orderdep-ids.txt" \
         | sed 's/^/    excused as order-dependent: /'
   fi
   # All eleven buckets, so this is the sum of the numbers `skipped.detail`
   # prints below it. It was nine: cs_unloadable was missing, and on the golden
   # tree cs_unloadable is 0, so the shortfall never showed. Display only --
   # nothing reads c_skipped but its own `fact` line, and `declined` is computed
   # separately, from the charged seven. Asserted against the detail line at
   # image build time, because that is the check the nine-bucket version of this
   # sum would have failed.
   c_skipped=$(( cs_noroot + cs_nested + cs_dup + cs_nosrc + cs_init + cs_timeout \
                 + cs_other + cs_budget + cs_exited + cs_unloadable + cs_testngonly ))
   # Of the eleven buckets `skipped.detail` prints, seven are the candidate's
   # doing and four are not.
   #
   #   noroot   the frozen list names a test class and the tree holds no
   #            compiled copy of it -- the tree decided that
   #   dupclass two compiled copies, so neither can answer for the other -- the
   #            tree decided that too
   #   nosrc    the class's module cannot be found by its source layout -- ditto
   #   exited   the JVM went away between the harness's own banner and the
   #            summary-plus-engine-report the platform always writes, with no
   #            fault of its own to show for it -- or the two channels disagree
   #            about how many cases ran. The only code between the banner and
   #            the report is the inherited test's -- ditto
   #   unloadable   a class the frozen run DID record cases for, which now has
   #            no runnable methods or throws on the way in: it lost them here
   #   timeout / budget   the scorer's own limits, but reachable from the tree:
   #            a class that blocks is a class that is not compared, so leaving
   #            these free left a way to simply not be corroborated (Patch G)
   #
   #   nested   a `$` name the frozen list carries and the reports never did;
   #            a property of the scorer's own list
   #   notloadable  a class no engine on the scorer's classpath discovers --
   #            but only where the frozen run recorded no case for it either:
   #            the inherited suite's shape, which this step asks nobody to
   #            change
   #   unparsed the runner's own output did not parse: the scorer's own fault
   #   testngonly  a class whose every recorded case came out of the TestNG
   #            provider, and which no JUnit engine on this classpath discovers
   #            now: the harness has no TestNG engine, so those ids were never
   #            corroborated on any tree and their absence here says nothing
   #            about this one -- baseline/testng-cased.txt
   #
   # The four stay free, because a corroboration that can be
   # taken down by not working is worse than none. The seven are charged,
   # because otherwise B-7 is bought by making the harness decline: launder one
   # failing case out of the XML and delete that one compiled test class, and
   # the reported pass has nothing left to contradict it. Measured before this
   # block existed: that tree scored 0.998628 against the oracle's 1.0, paying
   # 0.0014 at B-5 to recover 0.05 at B-7. `exited` is the same trade with no
   # price at all: a laundered failing case whose body calls Runtime.halt(0)
   # when it finds the scorer's harness in sun.java.command scored 1.000000
   # while that outcome was still free (ctlD4). The golden tree skips one class,
   # the nested one, and so is charged nothing.
   if [ "${corrob_state}" = declined ]; then
      declined=$(grep -c . "${WORK}/corrob-classes.txt" || true)
   else
      declined=$(( cs_noroot + cs_dup + cs_nosrc + cs_exited + cs_unloadable + cs_timeout + cs_budget ))
   fi
   fact "gate.testrun.corroborate.ran=${c_ran}"
   fact "gate.testrun.corroborated=$(( c_ran - contradicted ))"
   fact "gate.testrun.corroborate.skipped=${c_skipped}"
   fact "gate.testrun.corroborate.skipped.detail=noroot=${cs_noroot},nested=${cs_nested},dupclass=${cs_dup},nosrc=${cs_nosrc},notloadable=${cs_init},unloadable=${cs_unloadable},testngonly=${cs_testngonly},timeout=${cs_timeout},unparsed=${cs_other},budget=${cs_budget},exited=${cs_exited}"
   fact "gate.testrun.corroborate.declined=${declined}"
   fact "gate.testrun.selfstop=${selfstop}"
   fact "gate.testrun.selfstop.scan=${selfstop_state:-notrun},classes=${selfstop_seen:-0},unreadable=${selfstop_unread:-0},treeclassfiles=${allclass:-0}"
   head -5 "${WORK}/selfstop.txt" \
      | sed "s#^${TREE}/##; s/^/    ends the JVM from inside a test run: /"
   fact "gate.testrun.contradicted=${contradicted}"
   head -15 "${WORK}/contradicted.txt" \
      | sed 's/^/    contradiction -- the report says pass, the scorer run fails: /'
   if [ "${nparsed}" -ne "${nraw}" ]; then
      # The parser, not the candidate. Say which it is rather than charging a
      # scorer fault to whoever happens to be under it.
      echo "    parser recovered ${nparsed} of ${nraw} <testcase> element(s) -- scorer fault"
   elif [ "${uncovered}" -eq 0 ] && [ "${newfail_charged}" -eq 0 ] && [ "${contradicted}" -eq 0 ] \
        && [ "${declined}" -eq 0 ] && [ "${selfstop}" -eq 0 ]; then
      b7=1
   elif [ "${uncovered}" -gt 0 ]; then
      echo "    ${uncovered} of ${want_ids} inherited test(s) did not run -> 0"
   elif [ "${newfail_charged}" -gt 0 ]; then
      echo "    ${newfail_charged} test(s) the frozen baseline passed fail in this tree," \
           "in the report and in the scorer's own run of them -> 0"
   elif [ "${contradicted}" -gt 0 ]; then
      echo "    ${contradicted} reported pass(es) contradicted by the scorer's own run -> 0"
   elif [ "${declined}" -gt 0 ]; then
      echo "    ${declined} inherited test class(es) could not be re-run from this tree" \
           "-- missing, duplicated, with no module to run them in, unloadable" \
           "where the frozen run recorded cases, over the clock, or ending the" \
           "JVM before the run reported them -> 0"
   elif [ "${selfstop}" -gt 0 ]; then
      if [ "${selfstop_state:-notrun}" != ok ]; then
         echo "    the compiled artifacts in this tree could not be read for code that" \
              "ends the JVM out from under a test run -> 0"
      else
         echo "    ${selfstop} compiled class(es) in this tree can end the JVM out from" \
              "under a test run, or could not be read to say -> 0"
      fi
   fi
else
   echo "    no test report was written by this run -> 0"
fi
fact "B7.tests-run=${b7}"

# ------------------------------------------------- B-8 and B-9: step 02, held
#
# Step 02 moved this suite from JUnit 4 to JUnit 5. A step that migrates the
# language level has no business touching that, and a step that lets it be
# undone has not held its predecessor's ground -- so the two gates that say the
# migration is still there are inherited whole, as B-8 and B-9. They are step
# 02's C-1 and C-2, lifted verbatim including their comments, because a
# re-derivation of a reviewed gate is a new gate.
#
# Lifted through step 03 unchanged, comments included, for the same reason step
# 03 lifted them from step 02: a re-derivation of a reviewed gate is a new gate.
#
# Its C-3, C-4 and C-5 are NOT carried forward. They grade how step 02 was done
# -- which of nineteen named @Test(expected=) sites became assertThrows, which
# sixteen @RunWith classes moved, how many assertion calls survived the move --
# and none of them can regress without B-8 or B-9 regressing first: a class that
# stops being a pure Jupiter test fails B-8, and one that names org.junit again
# fails B-9. Its C-6 asked whether the migrated classes were still major 50;
# here that question belongs to B-4, which is not a must-flip but a regression
# floor: ONE major across everything the build compiled, not below 66. The
# must-flip at this rung is C-1 at 69. NOTES 8.5.
#
# Scope. Both gates are restricted to the MUST-MIGRATE set: the 55 test classes
# the frozen 2011 run recorded, minus the six in baseline/compat.txt. Those six
# are about JUnit 4 rather than merely written in it -- they construct
# org.junit.runner.Runner instances, hand them fixture classes and assert on the
# Result that comes back -- so a gate that charged them for naming a retired type
# would be asking the candidate to delete the thing under test. 55 - 6 = 49, and
# both numbers come from the scorer's own baseline rather than from anything the
# tree can move.
#
# The mirror and the module jars, exactly as B-4 and B-5 read them: the mirror is
# the tree's own compiled output filtered to the frozen class names, so a
# third-party jar unpacked into the tree cannot answer for it.
j5_state=notrun
b8=0; b9=0
grep '^test\.class=' /tests/baseline/tests.txt \
   | sed 's/^test\.class=//' | tr '.' '/' | LC_ALL=C sort -u > "${WORK}/all-testclasses.txt"
grep -v '^#' /tests/baseline/compat.txt | grep . \
   | tr '.' '/' | LC_ALL=C sort -u > "${WORK}/compat.txt"
LC_ALL=C comm -23 "${WORK}/all-testclasses.txt" "${WORK}/compat.txt" > "${WORK}/must.txt"
must_n=$(grep -c . "${WORK}/must.txt" || true)
fact "gate.j5.must=${must_n}"
# The denominator is frozen: 49, from the scorer's own baseline and never from
# the candidate's tree. A denominator a candidate can shrink is a gate a
# candidate can pass by deleting. Freezing it was necessary and, on its own, was
# not sufficient -- with the numerators written as "baseline minus what is
# left", deletion moved the numerator instead and paid just as well. The
# presence block below is the other half of that fix.
if [ "${must_n}" -eq 0 ]; then
   # The scorer's own baseline, not the tree's: say so rather than reporting a
   # confident zero from an instrument that was pointed at nothing.
   fact "gate.j5=no-baseline"
elif [ "${nclass}" -eq 0 ] && [ ! -s "${WORK}/modulejars.txt" ]; then
   fact "gate.j5=no-classes"
elif java -cp "${GCP}" arqgate.Junit5 "${WORK}/j5.txt" \
        "${mirror}" $(cat "${WORK}/modulejars.txt") > "${WORK}/j5.log" 2>&1; then
   j5_state=ok
   fact "gate.j5.classes=$(sed -n 's/^j5\.classes=//p' "${WORK}/j5.txt" | head -1)"

   # Presence, measured before anything is asked about it.
   #
   # B-9 asks a departure question -- is the retired type gone -- and a tree
   # can answer it by deleting the class. Written as "baseline count minus what
   # is still there", which is the obvious way to write it, it scores 1.0 on a
   # tree with no test sources at all: measured at step 02, such a tree took
   # 0.654375 against a verifier written that way, above that step's own floor.
   # So the classes the step is about are counted first, and a class that is not
   # here is not credited for anything it no longer contains.
   #
   # j5.seen is arqgate.Junit5's record of every class it read, which is the
   # only honest source for this: inferring presence from the absence of
   # findings is the bug, not the fix.
   sed -n 's/^j5\.seen=//p' "${WORK}/j5.txt" | LC_ALL=C sort -u > "${WORK}/j5-seen.txt"
   LC_ALL=C comm -12 "${WORK}/must.txt" "${WORK}/j5-seen.txt" > "${WORK}/j5-present.txt"
   present_n=$(grep -c . "${WORK}/j5-present.txt" || true)
   fact "gate.j5.present=${present_n}"
   fact "gate.j5.absent=$(( must_n - present_n ))"
   LC_ALL=C comm -23 "${WORK}/must.txt" "${WORK}/j5-present.txt" | head -10 \
      | sed 's/^/    no longer in the tree: /'

   # B-8  the must-migrate classes are still Jupiter tests.
   #
   # The framework field of a j5.test line is a comma-joined SET, not a single
   # name: the pinned tree carries `junit4,testng` on one class and the golden
   # tree carries `junit4,junit5` on another, so a test for equality against
   # "junit5" and a test for membership of "junit5" are different gates.
   #
   # This is the equality one, and deliberately. Membership scores a tree that
   # bolts one dummy @org.junit.jupiter.api.Test method onto all 49 classes and
   # changes nothing else: measured on a synthetic tree built from the pinned
   # one that way (fixture F1), this gate reads 1 under membership and 0 under
   # equality -- at step 02, family C 0.300000 against 0.000000, since C-1 was
   # the only gate such a tree moved. Equality asks the question -- is this class a
   # Jupiter test now -- and a half-migrated class, one that carries both, reads
   # as not finished rather than as finished.
   #
   # Measured: pinned tree 0 of 49, golden tree 49 of 49, every one of them
   # exactly `junit5`. The seven classes on the golden tree that are not pure
   # Jupiter are the six compat ones and two production classes in
   # arquillian-testng, none of which is in the must-migrate set.
   awk -F'[=|]' 'NR == FNR { m[$0] = 1; next }
                 $1 == "j5.test" && $3 == "junit5" && ($2 in m) { print $2 }' \
      "${WORK}/must.txt" "${WORK}/j5.txt" | LC_ALL=C sort -u > "${WORK}/j5-jupiter.txt"
   b8hit=$(grep -c . "${WORK}/j5-jupiter.txt" || true)
   fact "gate.j5.jupiter=${b8hit}"
   LC_ALL=C comm -23 "${WORK}/must.txt" "${WORK}/j5-jupiter.txt" | head -10 \
      | sed 's/^/    not a Jupiter test: /'
   b8=$(clamp01 "$(frac "${b8hit}" "${must_n}")")

   # B-9  the retired JUnit 4 types are still gone from those classes.
   #
   # A reference of any kind -- annotation, imported type, invoke owner, constant
   # -- to org.junit.*, junit.framework.* or the JUnit 4 runner API. Nested
   # classes are charged to the top-level class that owns them, because that is
   # the unit the candidate edits and the unit compat.txt exempts; without the
   # normalisation `JUnitIntegrationTestCase$ArquillianClass1` would be charged
   # while its owner was exempt.
   #
   # Separate from B-8 on purpose: a class can move every annotation to Jupiter
   # and still call org.junit.Assert.assertEquals in every method. Measured on a
   # synthetic tree that did exactly that (fixture F3), B-8 stays 1.0 and this
   # gate goes to 0. Arrival and departure are two measurements.
   awk -F'[=|]' 'NR == FNR { m[$0] = 1; next }
                 $1 == "j5.legacyref" { c = $2; sub(/\$.*$/, "", c); if (c in m) print c }' \
      "${WORK}/must.txt" "${WORK}/j5.txt" | LC_ALL=C sort -u > "${WORK}/j5-offenders.txt"
   b9off=$(grep -c . "${WORK}/j5-offenders.txt" || true)
   fact "gate.j5.offenders=${b9off}"
   head -10 "${WORK}/j5-offenders.txt" | sed 's/^/    still names a retired JUnit type: /'
   # Present AND clean over the frozen 49. An offender is by construction a
   # class that was read, so this never goes negative; a class that is gone
   # counts as neither present nor clean, which is the whole point.
   b9=$(clamp01 "$(frac "$(( present_n - b9off ))" "${must_n}")")
else
   # Same precedent as arqgate.Scan's: a gate that cannot read the artifacts says
   # so, rather than reporting a clean zero that looks like a measurement.
   fact "gate.j5=broken"
   sed -n '1,10p' "${WORK}/j5.log" | sed 's/^/    /'
fi
fact "B8.jupiter-held=${b8}"
fact "B9.junit4-held=${b9}"

# ----------------------------------------------------- B-10: step 03, held
#
# The ladder's ninth trap, made into a gate. Every other gate in this verifier
# is step-local, and a step-local score cannot see a tree that earns this rung
# by undoing the one below it -- converting the anonymous bodies to lambdas
# while putting the explicit type arguments back, or writing streams while
# hand-closing the resources step 03 moved into try clauses. Each of those
# scores family C in full. Under the mean, one rung's regression is a third of
# a step's worth of score spread over thirteen steps, which is to say invisible.
#
# Three sub-gates, one per construct step 03 asked for, each min(arrival,
# departure) for the same reason step 03's own gates were: a tree that deleted
# the code scores the departure half and not the arrival half, and a tree that
# wrote new code alongside the old scores the arrival half and not the
# departure half. Weighted 0.4 / 0.3 / 0.3 -- the diamond carries more because
# it is 93 sites in 47 files against 9 try statements and 11 file sites, which
# is the same proportion step 03's own family C used.
#
# The denominators are step 03's populations, from baseline/target.txt, and the
# derivation of each is written out there. They are NOT this step's figures:
# what is being asked is how much of step 03's work is still standing, so what
# it divides by is how much work step 03 did.
#
# Several of step 03's figures have moved down the ladder for honest reasons,
# and getting that wrong is the way this gate fails a correct tree rather than a
# regressing one. `newgeneric.explicit` fell 50 -> 36 at step 04, because 14
# of the explicit-generic sites sit on anonymous classes and an anonymous class
# cannot become a lambda and keep its type arguments; `explicitanon` fell
# 27 -> 13 by the same 14. So the quantity read here is the NON-anonymous
# remainder, explicit - explicitanon, which is 23 at both ends of THIS step too.
# A gate reading raw `explicit` would score a correct tree 0.72 on this third.
echo
echo "--- B-10: the three Java 7 constructs step 03 asked for are still there"
b10=0
if [ -z "${measured}" ]; then
   echo "    no compile of this tree produced a report -> 0"
   fact "gate.b10=no-report"
else
   # The diamond third. Arrival is the diamonds themselves; departure is that
   # the explicit type arguments did not come back. A site that came back is
   # charged against the 93 that went, so undoing the whole construct reads 0
   # and undoing one site of it costs one site's worth.
   now_nonanon=$(( ${now_explicit:-0} - ${now_anon:-0} ))
   [ "${now_nonanon}" -lt 0 ] && now_nonanon=0
   dia_back=$(( now_nonanon - ${tgt_nonanon:-0} ))
   [ "${dia_back}" -lt 0 ] && dia_back=0
   fact "gate.b10.nonanon=${now_nonanon}"
   fact "gate.b10.explicit.returned=${dia_back}"
   #
   # The arrival half divides by target.diamond -- how many diamonds the tree
   # must still HOLD -- and the departure half by target.b10.diamond -- how many
   # sites step 03 MOVED. At steps 03 and 04 those were the same 93 counted from
   # opposite ends. They diverged at step 05, and they diverge a little further
   # here: 73 against 93.
   #
   #   19 of the 93 went at step 05. They were the SINK CONSTRUCTIONS of the 23
   #   accumulate buckets -- `List<X> xs = new ArrayList<>();` followed by a
   #   terminal forEach(xs::add) -- and `collect(Collectors.toList())`
   #   constructs nothing for a diamond to sit on. The tree handed to this step
   #   holds the remaining 74.
   #
   #   THE TWENTIETH WENT AT STEP 07, once, and it was named:
   #   MethodParameterInjectionPoint#getAnnotations returned
   #   `new HashSet<>(Arrays.asList(...))`, step 07's C-5 required that
   #   construction to become `Set.of(...)`, and no factory form has a
   #   type-argument position for a diamond to sit in: 74 -> 73.
   #
   #   THE TWENTY-FIRST AND TWENTY-SECOND WENT AT STEP 08, and they were
   #   named: DynamicServiceLoader:196 and spi ServiceLoader:142 were both
   #   `new LinkedHashSet<>()`, the accumulators the two hand-written loaders
   #   filled a line at a time. Step 08's C-3 replaced both loops with a stream
   #   that collects to `LinkedHashSet::new`, and a method reference has no
   #   type-argument position either. 73 -> 71, and newgeneric.diamondfiles
   #   fell 40 -> 39 with them, because DynamicServiceLoader held no other
   #   diamond.
   #
   # So target.diamond is 71 and target.b10.diamond stays 93, and that is the
   # ninth trap answered rather than walked into: a gate reading 71/93 would
   # score a correct tree 0.763 on this third for doing THIS step's work, and a
   # departure half re-based on 71 would stop noticing the 22 explicit type
   # arguments step 03 removed from sites that are still there. The departure
   # half is not affected by any of the three moves -- it reads
   # explicit - explicitanon, which neither a factory, a collector nor a stream
   # touches, and both the tree handed to this step and a correctly migrated one
   # measure 36 - 13 = 23, the same 23 step 04 ended on, so dia_back is 0 on a
   # correct tree.
   #
   # STEP 06'S `var` EXEMPTION IS ALREADY SPENT WHEN THIS RUNG STARTS, and
   # B-13's departure denominator loses its +1 because of it. Exactly one of
   # step 06's 81 `var` candidates had a diamond initialiser
   # -- DynamicServiceLoader:196,
   # `LinkedHashSet<Class<? extends T>> providers = new LinkedHashSet<>()` --
   # and converting it would have meant writing the type argument out on the
   # right, turning a diamond back into an explicit-type-argument site. Step 06
   # asked for 80 of its 81 and left that one alone, and every rung since has
   # stated the departure denominator as want+1 so that the exempt site would
   # not be charged.
   #
   # STEP 08'S C-3 deleted that line outright -- not this rung's, whose C-3 is
   # the pattern switch -- together with the two BufferedReader resource
   # variables of
   # the two try clauses below, so of step 06's 81 sites 78 survive into the
   # tree THIS rung is handed and all 78 are `var`. The ARRIVAL half was re-based
   # at step 09 and target.b13.want has stayed at 78 through every rung since;
   # nothing this step asks for takes one of the 78 away.
   #
   # THE DEPARTURE HALF IS WHERE THE ALLOWANCE HAD TO BE WITHDRAWN. The site the
   # +1 was protecting went with step 08's deletion, so the tree this rung is
   # handed holds `var.candidatenew=0`, and a copied 79 over a want of 78 would
   # clamp to 1.0 with one regression still to spare -- an inherited gate quietly
   # loosened by a lower rung's work, which is the ninth trap in its exact shape.
   # target.b13.candidatenew is therefore 78. Step 09 was the rung that first
   # re-measured it, and the figure is derived rather than asserted -- want plus
   # whatever redundant declarations the input tree actually still holds, 78 + 0
   # -- so the allowance comes back by itself if a later rung re-introduces an
   # exemption. mk-baseline-step12.sh carries the 78 out of step 11's staged
   # target.txt with every other inherited denominator; this rung re-derives
   # nothing, because it moves no `var` declaration and no diamond.
   #
   # The four buckets whose step-05 sink SURVIVES as a diamond are the
   # early-return guards that rewrite left behind -- `if (clazz == null) return
   # new ArrayList<>();` in Reflections x3 and ContainerDefImpl#getDependencies.
   # A candidate who spells those `Collections.emptyList()` instead reads 69/73
   # here, which costs 0.0004 of the step's reward; that is the accepted price
   # of a denominator that is one tree's reach, and it is stated in
   # baseline/target.txt beside the number.
   #
   # authoring/verify-baseline-step06.sh asserts 73 + 20 = 93 against the
   # measured reports rather than leaving the pair to drift apart unnoticed --
   # and, because that sum is invariant under losing a diamond ANYWHERE, also
   # asserts that none arrived and that the twentieth is the one named above.
   b10d=$(minf "$(clamp01 "$(frac "${now_dia:-0}" "${tgt_dia:-0}")")" \
               "$(clamp01 "$(frac "$(( ${b10_dia:-0} - dia_back ))" "${b10_dia:-0}")")")

   # The try-with-resources third. 11 hand-written close() calls became 9
   # resource clauses at step 03 -- two of the 11 acquisitions share a try --
   # and step 08's C-3 deleted two of those 9 there, one per hand-written
   # loader. Both were `try (InputStream is = url.openStream(); BufferedReader
   # r = ...)` reading a provider file a line at a time, and
   # java.util.ServiceLoader does that reading itself. So the arrival half
   # divides by the 7 clauses that survived step 08 and must still be here --
   # target.try.resources 9 -> 7, with try.resourcevars falling 14 -> 10
   # alongside -- and the departure half still by the 11 closes, none of which
   # this rung brings back. A close that came back is a resource this tree
   # leaks again.
   fact "gate.b10.close=${now_close:-0}"
   b10r=$(minf "$(clamp01 "$(frac "${now_res:-0}" "${tgt_res:-0}")")" \
               "$(clamp01 "$(frac "$(( ${b10_close:-0} - ${now_close:-0} ))" "${b10_close:-0}")")")

   # The java.nio.file third. 11 of the 13 java.io.File sites moved and 2 did
   # not, for the behavioural reason step 03's instruction states and its
   # baseline/nioids.txt names; both halves divide by the 11 that moved, and
   # never by the 13 NIO.2 references the golden solution happened to write for
   # them. A denominator taken from one correct tree's spelling scores spelling.
   #
   # ref + staticref on the arrival side, exactly as step 03 read it:
   # `import static java.nio.file.Files.exists;` leaves `exists(p)` with no
   # qualifier for niofile.ref to see, and a tree that held the migration that
   # way would otherwise read an arrival of 0.
   now_io=$(( ${now_ionew:-0} + ${now_iodecl:-0} ))
   now_nioarrive=$(( ${now_nioref:-0} + ${now_niostatic:-0} ))
   io_back=$(( now_io - ${tgt_io:-0} ))
   [ "${io_back}" -lt 0 ] && io_back=0
   fact "gate.b10.io=${now_io}"
   fact "gate.b10.nioarrive=${now_nioarrive}"
   fact "gate.b10.io.returned=${io_back}"
   b10n=$(minf "$(clamp01 "$(frac "${now_nioarrive}" "${b10_nio:-0}")")" \
               "$(clamp01 "$(frac "$(( ${b10_nio:-0} - io_back ))" "${b10_nio:-0}")")")

   # The proviso, read the same way step 03 read it. Four of the 939 recorded
   # transcripts are the behaviours the file-API migration must not change --
   # Files.newOutputStream on a directory throws where new FileOutputStream
   # throws something else, File.createTempFile rejects prefixes Files
   # .createTempFile accepts. Family A weighs all four at 4/939, which is 0.0013
   # of this step and is a rounding error rather than an incentive, so they are
   # read here as well and halve this third.
   #
   # Deliberately the same four ids and the same halving as step 03's C-3: two
   # rungs must not disagree about what the behaviour was.
   nio_broken=0
   if [ -r "${summary}" ] && [ -r /tests/baseline/nioids.txt ]; then
      nio_broken=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                            /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                            END { print n + 0 }' /tests/baseline/nioids.txt "${summary}")
   else
      # No summary means family A did not run, and a proviso that cannot be
      # checked must not be assumed satisfied.
      # `grep -c` exits 1 on a count of zero, so `|| echo 1` on the end of the
      # substitution would leave the two-line string "0\n1" in the variable and
      # the -gt below would abort on it. The case sweeps every way this can fail
      # to be a positive integer -- missing file, empty file, non-numeric -- to
      # the same answer: a proviso that cannot be checked is not satisfied.
      nio_broken=$(grep -c '^[a-z]' /tests/baseline/nioids.txt 2>/dev/null || true)
      case "${nio_broken}" in ''|0|*[!0-9]*) nio_broken=1 ;; esac
   fi
   fact "gate.b10.nio.behaviour.broken=${nio_broken}"
   if [ "${nio_broken}" -gt 0 ]; then
      echo "    ${nio_broken} recorded behaviour(s) that the file migration must preserve no longer match -> halved"
      b10n=$(awk -v v="${b10n}" 'BEGIN { printf "%.6f", v / 2 }')
   fi

   fact "gate.b10.diamond=${b10d}"
   fact "gate.b10.resources=${b10r}"
   fact "gate.b10.nio=${b10n}"
   b10=$(awk -v d="${b10d}" -v r="${b10r}" -v n="${b10n}" \
         'BEGIN { printf "%.6f", 0.4*d + 0.3*r + 0.3*n }')
   [ "${b10}" = "1.000000" ] || \
      echo "    step 03's constructs: diamond ${b10d}, try-with-resources ${b10r}, java.nio.file ${b10n}"
fi
fact "B10.java7-held=${b10}"


# ----------------------------------------------------- B-11: step 04, held
#
# The question B-10 asks of step 03, asked of step 04. The ninth trap does not
# stop applying because there are four rungs below instead of three: a tree that
# writes Optional and Collectors while unwinding step 04's lambdas back into
# anonymous classes scores the whole of family C, and under the mean one rung's
# regression spread over thirteen steps is invisible.
#
# Three sub-gates, one per construct step 04 asked for, each read against step
# 04's OWN frozen set rather than against a re-measurement of this tree:
#
#   baseline/lambdafiles.txt      the 20 files that held a convertible
#                                 anonymous functional body before step 04
#   baseline/samifaces.txt        the 24 (path, Name) SAM pairs step 04 marked
#   baseline/collectmethods.txt   the 24 <file>#<method> buckets that held a
#                                 collecting enhanced-for before step 04
#
# Those three files are step 04's, staged here unchanged, for the reason B-10's
# denominators are step 03's: what is being asked is how much of step 04's work
# is still standing, so what it divides by is how much work step 04 did. The
# denominators come from baseline/target.txt's b11 keys and not from counting
# the set files' own lines -- a set file with a line dropped would otherwise
# move its own denominator with it and read full on a smaller question.
#
# NO DEPARTURE HALF, and that is where this gate's shape differs from B-10's.
# Each of step 03's three constructs REPLACED something that can come back -- an
# explicit type argument, a manual close(), a java.io.File -- so "did it return"
# is a question with an answer and B-10 asks it. Step 04's three did not work
# that way: the only way to unwrite a lambda is to put an anonymous class back
# where it was, and that is a file in lambdafiles.txt losing its lambda, which
# the arrival half already refuses. A second term reading
# `anonymousfunctional.size` would charge the same regression twice, and would
# also charge STEP 05's C-1, held here inside B-12, for the one thing it
# legitimately does to that counter -- an Optional-returning helper whose
# callers stop needing a null
# check does not add an anonymous class, but a candidate who writes
# `Optional.ofNullable(x).orElseGet(() -> ...)` writes a lambda, and a gate
# reading the total both ways would have to decide which direction was honest.
#
# THE THIRD SUB-GATE AND STEP 05'S C-2, HELD HERE AS B-12b, READ THE SAME
# BUCKETS FROM OPPOSITE ENDS, deliberately, and this is the ninth trap
# wearing its other mask -- a regression gate that goes RED on a tree for
# doing a LATER step correctly.
# Step 04 left 24 `src.forEach(sink::add)` accumulate sites standing inside the
# pipelines it built (NOTES 10.2), and step 05's C-2 converts 23 of them to
# `collect(Collectors...)`. Measured against SourceScan's own rules: the
# producer is unaffected (`xs.stream()` and `Arrays.stream(xs)` both still
# ENTER), and `collect` is itself a stream OP, because countStream files any
# call with a stream-typed receiver as an op. So a converted bucket still holds
# a producer and still holds an op and coveredmethods() reads it exactly as full
# after the conversion as before. The Dockerfile rehearses this on a fixture
# rather than leaving it to be discovered on the golden.
echo
echo "--- B-11: the three Java 8 language constructs step 04 asked for are still there"
b11=0
if [ -z "${measured}" ]; then
   echo "    no compile of this tree produced a report -> 0"
   fact "gate.b11=no-report"
else
   # The lambda third. Coverage of step 04's 20 files, counting a lambda OR a
   # method reference in each, exactly as step 04's own C-1 counted it: 24 of
   # the sites step 04 arrived at are method references, and a gate reading
   # lambda records alone would charge a tree for holding `this::handle`.
   cov11a=$(covered /tests/baseline/lambdafiles.txt lambda methodref)
   fact "gate.b11.lambdafiles.covered=${cov11a}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/cov-hit.txt" | head -8 \
      | sed 's/^/    lambda or method reference gone from: /'
   b11l=$(clamp01 "$(frac "${cov11a}" "${b11_lambdafiles:-0}")")

   # The @FunctionalInterface third, read as the same SET of (path, Name) pairs
   # step 04 froze -- not as a count, which 24 trivial annotated interfaces in a
   # file of one's own would satisfy.
   sed -n 's/^interfacedecl\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '$0 ~ /annotated=yes/ { split($1, a, ":"); print a[1] " " $2 }' \
      | LC_ALL=C sort -u > "${WORK}/sam11-now.txt"
   grep -v '^#' /tests/baseline/samifaces.txt | grep . | LC_ALL=C sort -u \
      > "${WORK}/sam11-want.txt"
   sam11=$(LC_ALL=C comm -12 "${WORK}/sam11-want.txt" "${WORK}/sam11-now.txt" | grep -c . || true)
   fact "gate.b11.sam.annotated=${sam11}"
   LC_ALL=C comm -23 "${WORK}/sam11-want.txt" "${WORK}/sam11-now.txt" | head -8 \
      | sed 's/^/    annotation gone from: /'
   b11s=$(clamp01 "$(frac "${sam11}" "${b11_sam:-0}")")

   # The stream third. The 24 buckets that held a collecting loop before step 04
   # must each still hold a stream producer AND a stream op -- the conjunction
   # step 04's own C-3 settled on in round 2 of its static review, at the same
   # scope, for the same reason: a file-level reading is satisfied by one
   # `xs.stream().count();` per file.
   cov11c=$(coveredmethods /tests/baseline/collectmethods.txt stream streamop)
   fact "gate.b11.collectmethods.covered=${cov11c}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/cov-hit.txt" | head -8 \
      | sed 's/^/    stream pipeline gone from: /'
   b11c=$(clamp01 "$(frac "${cov11c}" "${b11_collectmethods:-0}")")

   fact "gate.b11.lambdas=${b11l}"
   fact "gate.b11.functionalinterface=${b11s}"
   fact "gate.b11.streams=${b11c}"
   # 0.20 / 0.20 / 0.60 -- step 04's own C-1 / C-2 / C-3 weights (0.15 / 0.15 /
   # 0.60, over a fourth gate this one does not inherit) renormalised over the
   # three, so this gate holds step 04's work in the proportions step 04 valued
   # it. Its C-4 is not carried: it asked whether the tree requires Java 8, and
   # B-4 above asks the compiled form of exactly that question at full weight.
   b11=$(awk -v l="${b11l}" -v s="${b11s}" -v c="${b11c}" \
         'BEGIN { printf "%.6f", 0.20*l + 0.20*s + 0.60*c }')
   [ "${b11}" = "1.000000" ] || \
      echo "    step 04's constructs: lambdas ${b11l}, @FunctionalInterface ${b11s}, streams ${b11c}"
fi
fact "B11.java8lang-held=${b11}"

# ----------------------------------------------------- B-12: step 05, held
#
# STEP 05'S WHOLE FAMILY C, MOVED INTO FAMILY B INTACT. Five sub-gates -- four
# read off the typed AST compiled above, one off the constant pools of the jars
# the build published -- asking exactly what they asked one rung down: is
# `Optional` where the null-returning lookup helpers were, is a `Collectors`
# factory where the hand-written accumulate loops were, does `java.util.Objects`
# carry the value types' equals and hashCode, is `java.time` where the
# epoch-millis calls were, and do the published jars' constant pools name all
# four.
#
# Three things changed and nothing else did.
#
#   1. The five scores are b12_opt / b12_col / b12_obj / b12_jt / b12_lib
#      rather than c1..c5, and their facts are B12a..B12e. A file with two
#      families cannot have two c1s.
#   2. The four denominators and four constant-pool floors are read as
#      target.b12.* instead of target.optbuckets / target.sinks /
#      target.valuetypes / target.timesites / target.c5.*.floor. Those bare
#      spellings belong to step 05's target.txt; the file staged here is step
#      10's, and carrying step 05's forward under its own names would put two
#      files in line to land as baseline/target.txt with the order of a staging
#      loop deciding which one a gate read.
#   3. The anti-gaming multiplier came OUT of this block. It penalises raw
#      types and suppressions ADDED against the frozen baseline, which is a
#      statement about the edits this step's candidate made and not about the
#      ones step 05's did -- so it belongs wherever that question is asked
#      once, and at this rung that is B-14's `b14pen`, which multiplies `b14`
#      and nothing else. Family C carries NO anti-gaming multiplier here at
#      all; the family C banner gives the floor arithmetic that makes that a
#      requirement and not a preference.
#
# The four frozen set files it reads -- optbuckets.txt, sinks.txt,
# valuetypes.txt, timesites.txt -- are step 05's own, staged here unchanged.
# They name sites by path and environment_mode is "separate", so /tests is not a
# directory the candidate's container can read.
#
# baseline/source.txt is NOT step 05's input any more, and the three `bv` reads
# below are the only place that shows. It is this step's input, which is step
# 05's OUTPUT, so retnull.size, accum.size and millis.size read the migrated
# figures rather than the unmigrated ones. They are published as facts and read
# by no gate -- every gate here divides by a frozen target.b12.* denominator --
# so the change of meaning costs nothing. It would have cost everything if any
# of the three had been a denominator.
#
# Weighted 0.12 of family B, the same share B-10 and B-11 took when each was the
# newest inherited gate: a regression gate is a floor and not a requirement, a
# tree that does nothing at all to step 05's work collects it in full, and the
# floor arithmetic at the foot of this file depends on exactly that.
b12_opt=0; b12_col=0; b12_obj=0; b12_jt=0; b12_lib=0; b12=0
echo
echo "--- B-12: step 05's Java 8 library migration, held"

# The baseline's own library figures, for the facts and for nothing else: every
# gate below divides by a frozen SET, so these are published so that a reader of
# facts.txt can see the two ends of each question without opening two files.
base_retnull=$(bv 'retnull\.size')
base_accum=$(bv 'accum\.size')
base_millis=$(bv 'millis\.size')
fact "gate.src.base.retnull=${base_retnull:-0}"
fact "gate.src.base.accum=${base_accum:-0}"
fact "gate.src.base.millis=${base_millis:-0}"

if [ -z "${measured}" ]; then
   # Nothing was measured. Say so rather than reporting five clean zeros that
   # read like a finding about the tree.
   echo "  B-12: no compile of this tree produced a report -> 0"
else
   now_retnull=$(sv 'retnull\.size')
   now_optret=$(sv 'optret\.size')
   now_optop=$(sv 'optop\.size')
   now_optget=$(sv 'optop\.get')
   now_accum=$(sv 'accum\.size')
   now_collect2=$(sv 'collect\.size')
   now_objects=$(sv 'objects\.size')
   now_jtime=$(sv 'jtime\.size')
   now_millis=$(sv 'millis\.size')
   for k in retnull optret optop optget accum collect2 objects jtime millis; do
      eval "now_${k}=\${now_${k}:-0}"
      eval "fact \"gate.src.${k}=\${now_${k}}\""
   done

   # ---------------------------------------------------------------- B-12a
   # Optional where the null-returning lookup helpers were.
   #
   # SIXTEEN BUCKETS AND NOT FORTY-TWO, and the sixteen are the PRIVATE ones.
   # The figures in this paragraph are STEP 05'S baseline, which is where the
   # sixteen were chosen; on the tree handed to THIS step they have already
   # moved -- retnull.size reads 25 and optret.size 16 -- and that is the
   # regression this gate holds rather than the work it asks for.
   # retnull.size is 42 at step 05's baseline; 22 of those sites sit in methods
   # apisig.sh prints, and B-6 is an EXACT set -- want=1105 hit=1105 extra=0 on
   # this rung's golden, where a widened surface is charged exactly as a
   # narrowed one. Changing a published `T foo()` to `Optional<T> foo()` removes
   # a line from `hit` and adds one to `extra`, so an instruction that asked for
   # it would be asking the candidate to fail an inherited gate worth more than
   # this one. Three ways out were available -- confine the leg to non-published
   # methods, re-baseline B-6 here, or give B-6 an allowlist of permitted
   # signature moves -- and the first is taken, because the private population
   # is large enough to carry the gate on its own and the other two both spend
   # the property that B-6's expectation is a set the CANDIDATE is never invited
   # to move.
   #
   # THAT PROPERTY IS NOT THE SAME AS THE EXPECTATION NEVER MOVING, and step 11
   # is where the difference showed. B-6's file was re-cut THERE, 1147 lines to
   # 1105, because that step's own family C deleted the seven
   # `SecurityActions$GetTcclAction` enums and their 42 signatures went with
   # them. That was an authoring-side re-cut of what the FINISHED tree
   # publishes, made once and checked against the golden; it left the candidate
   # facing the same exact set it always faced. This rung inherits the 1105 and
   # re-cuts nothing. An allowlist would have been the other thing -- a standing
   # licence to move signatures, checked against nothing -- and B-6's own block
   # says why that was refused both times. NOTES 10.5, 16.18 and 16.61.
   #
   # The 42 break down as 22 api=yes (20 public + 2 protected), 17 private
   # api=no, and 3 in anonymous class bodies. The 17 private sites sit in 16
   # buckets -- ContainerRegistryCreator#getActivatedConfiguration and
   # #readActivatedValue are one bucket each and EngineDefImpl contributes two
   # -- and one of the 42 is in src/test. baseline/optbuckets.txt carries the
   # derivation and reproduces it from source.txt in one awk.
   #
   # THREE HALVES.
   #
   # Departure: of the 16 buckets, how many hold NO retnull record at all. A
   # `comm -23` against the set and not a subtraction of two counts: a
   # subtraction reads full for a tree that removed one site and grew another
   # somewhere else in the same set.
   #
   # Arrival: of the 16, how many now declare a method returning
   # java.util.Optional. SourceScan resolves that through javac's ELEMENTS, so
   # `Optional<T>`, `java.util.Optional<T>` and an Optional behind a type
   # variable are one finding and a class somebody wrote called Optional is not.
   #
   # Consumption: a SHARE, not a count, and it is the term that distinguishes a
   # migration from a rename. `findMatch(..).get()` is `findMatch(..)` with more
   # syntax: the null is still there, it is now a NoSuchElementException, and
   # every counter above reads full. So the third term is the fraction of
   # instance calls ON an Optional that are NOT get(). A tree that consumes its
   # sixteen Optionals with orElse / orElseGet / orElseThrow / ifPresent / map /
   # filter reads 1.000000; one that consumes them all with get() reads 0; one
   # that writes `if (o.isPresent()) return o.get();` reads 0.5, which is the
   # right answer for a tree that moved the null check rather than removing it.
   #
   # `make` records -- static calls, Optional.of / ofNullable / empty -- are NOT
   # in the denominator. They are how an Optional is produced, and producing one
   # is what the arrival half already scores; counting them here would let a
   # tree raise the share by producing more.
   buckets retnull
   buckets optret
   wantbuckets /tests/baseline/optbuckets.txt
   opt_want=$(grep -c . "${WORK}/cov-want.txt" || true)
   opt_dep=$(gone retnull)
   opt_arr=$(hit optret)
   fact "gate.src.opt.want=${opt_want}"
   fact "gate.src.opt.departed=${opt_dep}"
   fact "gate.src.opt.arrived=${opt_arr}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-retnull.txt" | head -8 \
      | sed 's/^/    still returns null: /'
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-optret.txt" | head -8 \
      | sed 's/^/    no Optional return yet: /'
   sed -n 's/^optop\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '$2 == "use"' > "${WORK}/optuse.txt" || true
   optuse=$(grep -c . "${WORK}/optuse.txt" || true)
   optget=$(awk '$3 == "get"' "${WORK}/optuse.txt" | grep -c . || true)
   fact "gate.src.opt.uses=${optuse}"
   fact "gate.src.opt.get=${optget}"
   awk '$3 != "get" { print $3 }' "${WORK}/optuse.txt" | LC_ALL=C sort | uniq -c \
      | LC_ALL=C sort -rn | head -6 | sed 's/^/    consumed with: /'
   optcons=$(clamp01 "$(frac "$(( optuse - optget ))" "${optuse}")")
   fact "gate.src.opt.consumption=${optcons}"
   [ "${optget}" -eq 0 ] || \
      echo "    ${optget} of ${optuse} Optional use(s) are get() -- the null moved rather than went"
   b12_opt=$(minf "$(minf "$(clamp01 "$(frac "${opt_dep}" "${b12_optbuckets:-0}")")" \
                     "$(clamp01 "$(frac "${opt_arr}" "${b12_optbuckets:-0}")")")" \
             "${optcons}")

   # ---------------------------------------------------------------- B-12b
   # Collectors where step 04 left the accumulate sites standing.
   #
   # Step 04 converted 24 collecting enhanced-for loops into stream pipelines
   # and, by design, left every one of them ending in `src.forEach(sink::add)`
   # over a sink the method had already constructed (NOTES 10.2). That shape is
   # a pipeline with its terminal operation written by hand, and converting it
   # is what this leg is. 23 of the 24 are in src/main and those 23 are the
   # population; the 24th is CDIInjectionEnricherTestCase#createDeployment, in
   # src/test, and B-12 grades the library this task publishes.
   #
   # Departure: of the 23 buckets, how many hold no accum record.
   # Arrival: how many hold a collect record. Published as a fact and not
   # multiplied in, because fidelity below is a strict subset of it -- a bucket
   # cannot have an ACCEPTED collector without having a collector -- and a term
   # that cannot change the answer is noise in a score expression.
   # Fidelity: how many hold a collect record whose factory is accepted for the
   # factory baseline/sinks.txt names. That is the half that makes this a
   # measurement rather than a count of the word `collect`.
   #
   #   want toList        accept toList, or toCollection
   #   want toSet         accept toSet, or toCollection
   #   want toCollection  accept toCollection ONLY
   #
   # toCollection is accepted wherever toList or toSet is wanted because four of
   # the 23 buckets mutate the collected result afterwards -- Reflections' three
   # addAll calls and MethodParameterInjectionPoint#getQualifiers' add -- and
   # toCollection(ArrayList::new) is the pedantically faithful answer there.
   # toSet is NOT accepted where toCollection is wanted, and that is the one
   # site this whole leg turns on: DynamicServiceLoader#createInstances collects
   # into a LinkedHashSet, Collectors.toSet() makes no ordering guarantee, and
   # DynamicServiceLoader's instantiation order is IN the 939 recordings family
   # A replays. Which collection a toCollection actually builds is then family
   # A's question and not this file's -- SourceScan records the factory's simple
   # name and not its supplier, because the supplier is a method reference whose
   # target type is not resolved at the call site the plugin sees.
   buckets accum
   buckets collect
   wantbuckets /tests/baseline/sinks.txt
   sink_want=$(grep -c . "${WORK}/cov-want.txt" || true)
   sink_dep=$(gone accum)
   sink_arr=$(hit collect)
   fact "gate.src.sink.want=${sink_want}"
   fact "gate.src.sink.departed=${sink_dep}"
   fact "gate.src.sink.arrived=${sink_arr}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-accum.txt" | head -8 \
      | sed 's/^/    still accumulates by hand: /'
   sed -n 's/^collect\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); m = ""; to = ""
               for (i = 2; i <= NF; i++) {
                  if (substr($i, 1, 3) == "to=") to = substr($i, 4)
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4) }
               if (m != "" && to != "") print f[1] "#" m, to }' \
      | LC_ALL=C sort -u > "${WORK}/collect-to.txt"
   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           want[$1] = $2
           next
        }
        { got[$1 SUBSEP $2] = 1 }
        END {
           n = 0
           for (b in want) {
              w = want[b]
              ok = ((b SUBSEP w) in got)
              if (!ok && (w == "toList" || w == "toSet")) ok = ((b SUBSEP "toCollection") in got)
              if (ok) n++; else print "    wrong collector (want " w "): " b > "/dev/stderr"
           }
           print n + 0
        }' /tests/baseline/sinks.txt "${WORK}/collect-to.txt" \
        > "${WORK}/sinkfid.txt" 2> "${WORK}/sinkbad.txt"
   sink_fid=$(head -1 "${WORK}/sinkfid.txt")
   fact "gate.src.sink.faithful=${sink_fid:-0}"
   head -8 "${WORK}/sinkbad.txt"
   b12_col=$(minf "$(clamp01 "$(frac "${sink_dep}" "${b12_sinks:-0}")")" \
             "$(clamp01 "$(frac "${sink_fid:-0}" "${b12_sinks:-0}")")")

   # ---------------------------------------------------------------- B-12c
   # java.util.Objects in the value types' equals and hashCode.
   #
   # Three classes, and measured rather than chosen: ProtocolDescription,
   # DeploymentTargetDescription and TargetDescription are the ONLY src/main
   # classes declaring both equals(Object) and hashCode(), and no class in the
   # tree declares one without the other. All three carry character-identical
   # bodies apart from the cast type -- the Eclipse-generated null-ternary
   # hashCode and the nested-if equals -- which is exactly the shape
   # Objects.hash and Objects.equals exist to replace.
   #
   # ARRIVAL ONLY, and the weight is 0.10 because of it. There is no departure
   # counter available for this one: what has to go is a null-ternary inside an
   # expression and a nested if/else chain, and SourceScan records neither. It
   # is not invented here. What replaces a departure half is SCOPE: the gate
   # does not ask whether the file mentions Objects, it asks whether the
   # <file>#equals bucket holds an Objects.equals call AND the <file>#hashCode
   # bucket holds an Objects.hash call. A candidate who decorates some other
   # method with Objects.requireNonNull collects nothing, and one who calls
   # Objects.equals inside equals() has done the work whatever else is still in
   # the body.
   #
   # Objects.hash and not Objects.hashCode: hashCode(Object) is the null-safe
   # single-value form and does not combine fields, so a hashCode built from it
   # still hand-rolls the 31 * result loop. `hash` is accepted, `hashCode` is
   # not, and this is the one place in B-12 where a method NAME is matched
   # rather than an element resolved -- both names come off the same resolved
   # java.util.Objects element, so what is being told apart is two members of
   # one class and not two classes that share a spelling.
   sed -n 's/^objects\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); m = ""
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
               if (m != "") print f[1] "#" m, $2 }' \
      | LC_ALL=C sort -u > "${WORK}/objects-in.txt"
   vt_want=$(grep -v '^#' /tests/baseline/valuetypes.txt | grep -c . || true)
   # Every other leg of B-12 names what it did not find -- B-12a prints
   # "still returns null", B-12b "still accumulates by hand" and "wrong
   # collector", B-12d "still reads the wall clock". This one printed nothing,
   # which made a B-12c of 0.000000 the one number in the step with no address
   # under it: the three files are not guessable from the score and the
   # baseline list is not on the candidate's side of the wall. Two halves are
   # required per file, so the line says WHICH half is missing -- someone who
   # wrote Objects.equals and left the 31 * result loop standing has a
   # different problem from someone who did neither, and one line of transcript
   # is the difference between those two readings.
   #
   # Sorted because `for (f in want)` has no defined order in awk and a
   # transcript that shuffles between two runs of the same tree is a diff with
   # nothing behind it.
   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           want[$1] = 1
           next
        }
        { got[$1 SUBSEP $2] = 1 }
        END {
           n = 0
           for (f in want) {
              e = ((f "#equals" SUBSEP "equals") in got)
              h = ((f "#hashCode" SUBSEP "hash") in got)
              if (e && h) { n++; continue }
              if (!e && !h)      why = "no Objects.equals in equals(), no Objects.hash in hashCode()"
              else if (!e)       why = "no Objects.equals in equals()"
              else               why = "no Objects.hash in hashCode()"
              print "    value type not migrated: " f " -- " why > "/dev/stderr"
           }
           print n + 0
        }' /tests/baseline/valuetypes.txt "${WORK}/objects-in.txt" \
        > "${WORK}/vthit.txt" 2> "${WORK}/vtbad.txt"
   vt_hit=$(head -1 "${WORK}/vthit.txt")
   fact "gate.src.valuetypes.want=${vt_want}"
   fact "gate.src.valuetypes.migrated=${vt_hit:-0}"
   LC_ALL=C sort "${WORK}/vtbad.txt" | head -8
   b12_obj=$(clamp01 "$(frac "${vt_hit:-0}" "${b12_valuetypes:-0}")")

   # ---------------------------------------------------------------- B-12d
   # java.time where the epoch-millis calls were.
   #
   # Five System.currentTimeMillis calls in four buckets -- at STEP 05'S
   # baseline, which is where the four were chosen. On the tree handed to this
   # step millis.size reads 0 and jtime.size 11: all five have gone and the
   # four buckets carry java.time instead, which is the state this gate holds.
   # Those five were the whole legacy-time population of the tree: millis.size=5
   # with every one of them
   # spelled System.currentTimeMillis says there is no System.nanoTime and no
   # Date, Calendar, GregorianCalendar, DateFormat or SimpleDateFormat call
   # anywhere -- SourceScan's millis counter takes all seven shapes. A `new
   # Date()` would not be a MethodInvocationTree and so would be invisible to
   # it, which is why an independent grep is recorded beside the measurement in
   # baseline/timesites.txt rather than instead of it: zero files in the whole
   # repository name any of those five types.
   #
   # BUCKETS AND NOT SITES, and baseline/timesites.txt's second column is why
   # the distinction is written down: ServletMethodExecutor#execute holds two of
   # the five calls, so a site-level departure would pay half a bucket for
   # converting the start of a timing interval and leaving the end. A bucket
   # with no millis record left is a bucket where BOTH went.
   #
   # min(departure, arrival), the same shape as B-12a and B-12b and for the same
   # reason: a tree that deletes the timing and a tree that adds an Instant
   # beside the millis each score one half and neither scores.
   #
   # WHAT IS NOT ASKED. TestResult.getStart() and getEnd() return long, they are
   # in B-6's 1105-line expectation, and this step does not move them. The
   # migration wanted here is the SOURCE of the value -- Instant.now() and
   # Duration.between over a Clock the caller could substitute, rather than a
   # static read of the wall clock -- not the type it is stored in. A candidate
   # who changes the field type fails B-6 and learns nothing this step is about.
   buckets millis
   buckets jtime
   wantbuckets /tests/baseline/timesites.txt
   time_want=$(grep -c . "${WORK}/cov-want.txt" || true)
   time_dep=$(gone millis)
   time_arr=$(hit jtime)
   fact "gate.src.time.want=${time_want}"
   fact "gate.src.time.departed=${time_dep}"
   fact "gate.src.time.arrived=${time_arr}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-millis.txt" | head -8 \
      | sed 's/^/    still reads the wall clock: /'
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-jtime.txt" | head -8 \
      | sed 's/^/    no java.time yet: /'
   b12_jt=$(minf "$(clamp01 "$(frac "${time_dep}" "${b12_timesites:-0}")")" \
             "$(clamp01 "$(frac "${time_arr}" "${b12_timesites:-0}")")")
fi

# ---------------------------------------------------------------- B-12e
# The tree SHIPS the Java 8 library.                            (inherited)
#
# Outside the block above on purpose. Every gate in it is one instrument's
# opinion -- arqgate.SourceScan reading javac's typed AST of a compile the
# SCORER ran, over whatever sources it found. This one asks a different
# instrument a different question of a different artifact: are the four package
# names in the constant pools of the jars the candidate's own BUILD published.
#
# It is NOT one of this step's must-flips -- the class-file level C-1 asks for
# and JEP 441's null rule are. Its four counts reading 0 on the input tree is a fact
# about that tree, for the reason set out below, and a different thing from
# owning it. On the tree this step is handed the four counts are 0, 0, 0 and
# 0 -- measured with /work/scratch/cpscan.sh on the step-04 golden, against
# java/util/stream/=13 and java/util/function/=13 in the same scan, so the
# instrument is reading real constant pools and the four zeroes are a fact about
# the tree rather than a broken grep. java/util/Objects reading 0 there is worth
# stating separately: javac emits Objects.requireNonNull into the desugaring of
# some method references, that tree has 24 of them, and none of them produced
# one. The zero is therefore this tree's zero and not this compiler's.
#
# WHY A FLOOR RATHER THAN A FLAG. One Optional in one class makes the package
# name appear, and a binary read would hand the whole of this gate to a tree
# edited in one place. So each package is scaled by how many published classes
# reference it, against a floor in baseline/target.txt set WELL below what the
# golden reaches -- the same construction as step 04's B-12d level-error floor,
# and for the same reason: a denominator taken from one correct tree's exact
# reach is scoring that tree's spelling.
#
# THE FOUR ARE AVERAGED AND NOT MINIMISED. min() would make the whole gate the
# smallest of four legs, and the smallest is java.time, whose entire population
# is 4 buckets -- a tree that did the Optional and Collectors work in full and
# skipped the 4 timing sites would read 0 on the must-flip. Family C already
# scores each leg's own work; this gate's question is whether the library
# reaches the published artifacts at all, and a mean answers it.
#
# EACH LEG IS SCALED BY THE SOURCE LEG IT CORROBORATES, which is the whole
# reason this gate is safe to price at 0.15. Static review round 1 (R5) priced
# the attack: the floors are deliberately low, so a tree that writes ~15 dummy
# references -- `Optional.empty()` assigned to an unused local, in enough
# classes to clear 5/6/2/2 -- clears B-12e in full while B-12a..B-12d all read 0, and
# 0.30*1 + 0.70*(0.25*1 + 0.75*0.15) = 0.553750 is over the item-6 bar of 0.5
# without one line of the migration being done. Raising the floors to the
# golden's exact reach only raises the attack's price, and buys it by scoring
# one correct tree's spelling -- the thing the floors exist to avoid.
#
# So the fix is at the other end: a constant-pool hit PAYS ONLY WHAT ITS OWN
# SOURCE LEG EARNED. `optional_term * C1`, `collectors_term * C2`, and so on.
# A reference the source work explains pays in full, a reference to a package
# the tree never actually migrated pays nothing, and the dummy tree above reads
# 0 on all four. It also makes the gate say something sharper than it did:
# "the library the source moved to is the library the jars ship", which is the
# question a constant pool is the right instrument for. Read the other way, the
# two halves are each other's control -- source without pool means the work did
# not reach a published artifact, pool without source means the reference is
# not the migration.
#
# The oracle is unmoved (all four source legs are 1.0, so each term is its own
# raw fraction), and so is the floor arm: its B-12a..B-12d are already 0, so its
# B-12 was 0 before this change and is 0 after it.
#
# Binary grep over class files, which is what a constant-pool reference to
# java/util/Optional looks like on disk: the CONSTANT_Utf8 entry holding the
# class name is the literal byte string. Read out of the PUBLISHED jars, the
# same population apisig.sh reads, and not out of the tree's compiled output --
# a class that never reaches a jar is a class this project does not ship.
echo
echo "--- B-12e: the published jars reference the Java 8 library"
cpdir="${WORK}/cpscan"
rm -rf "${cpdir}"; mkdir -p "${cpdir}"
if [ -s "${WORK}/modulejars.txt" ]; then
   njar=0
   while IFS= read -r j; do
      [ -n "${j}" ] && [ -f "${j}" ] || continue
      njar=$(( njar + 1 ))
      unzip -qo "${j}" '*.class' -d "${cpdir}/m${njar}" >/dev/null 2>&1 || true
   done < "${WORK}/modulejars.txt"
   cpclasses=$(find "${cpdir}" -name '*.class' | wc -l)
   fact "gate.cp.jars=${njar}"
   fact "gate.cp.classes=${cpclasses}"
   if [ "${cpclasses}" -eq 0 ]; then
      echo "    the published jars carry no class files -> 0"
   else
      # How many published classes name this package. `grep -lZ` and a NUL count
      # rather than `grep -lc | wc -l`: a jar entry may hold any byte in its
      # name, and -Z is the only form that survives one.
      cpcount() {
         find "${cpdir}" -name '*.class' -print0 \
            | xargs -0 -r grep -lZ --binary-files=binary -F "$1" 2>/dev/null \
            | tr -dc '\0' | wc -c
      }
      b12cp_opt=$(cpcount 'java/util/Optional')
      b12cp_col=$(cpcount 'java/util/stream/Collectors')
      b12cp_obj=$(cpcount 'java/util/Objects')
      b12cp_jt=$(cpcount 'java/time/')
      fact "gate.cp.optional=${b12cp_opt}"
      fact "gate.cp.collectors=${b12cp_col}"
      fact "gate.cp.objects=${b12cp_obj}"
      fact "gate.cp.jtime=${b12cp_jt}"
      f1=$(clamp01 "$(frac "${b12cp_opt}" "${b12_opt_floor:-0}")")
      f2=$(clamp01 "$(frac "${b12cp_col}" "${b12_col_floor:-0}")")
      f3=$(clamp01 "$(frac "${b12cp_obj}" "${b12_obj_floor:-0}")")
      f4=$(clamp01 "$(frac "${b12cp_jt}" "${b12_jt_floor:-0}")")
      printf '    Optional %s/%s  Collectors %s/%s  Objects %s/%s  java.time %s/%s\n' \
         "${b12cp_opt}" "${b12_opt_floor:-0}" "${b12cp_col}" "${b12_col_floor:-0}" \
         "${b12cp_obj}" "${b12_obj_floor:-0}" "${b12cp_jt}" "${b12_jt_floor:-0}"
      # Each leg times the source leg it corroborates -- see the block above.
      # b12_opt..b12_jt are already computed and printed; this gate is last on purpose.
      b12_lib=$(awk -v a="${f1}" -v b="${f2}" -v c="${f3}" -v d="${f4}" \
               -v p="${b12_opt:-0}" -v q="${b12_col:-0}" -v r="${b12_obj:-0}" -v s="${b12_jt:-0}" \
           'BEGIN { printf "%.6f", (a * p + b * q + c * r + d * s) / 4 }')
      printf '    corroborated: %s*%s %s*%s %s*%s %s*%s -> %s\n' \
         "${f1}" "${b12_opt:-0}" "${f2}" "${b12_col:-0}" "${f3}" "${b12_obj:-0}" \
         "${f4}" "${b12_jt:-0}" "${b12_lib}"
   fi
else
   echo "    the build published no module jar -> 0"
   fact "gate.cp=no-jars"
fi

fact "B12a.optional=${b12_opt}"
fact "B12b.collectors=${b12_col}"
fact "B12c.objects=${b12_obj}"
fact "B12d.jtime=${b12_jt}"
fact "B12e.ships-java8-library=${b12_lib}"
# 0.30 / 0.35 / 0.10 / 0.10 / 0.15 -- step 05's own internal weights, unchanged.
#
# NOT renormalised, and the four AST legs are not re-weighted over four now that
# the gate is inherited: the five already sum to 1.000000, and changing how step
# 05's migration divides its own score is the one thing an inherited gate must
# not do. B-12 is worth 0.12 of family B; how it divides that 0.12 internally is
# step 05's arithmetic and is left alone.
#
# The constant-pool leg is the one a reader will ask about, because it is the
# only leg here that is not an AST counter and B-12 is a regression gate rather
# than a must-flip. It stays for the reason it was written: a tree can carry
# `Optional` in a source file the build never compiles, or in a module it never
# publishes, and all four AST legs would read full. That failure mode does not
# become less real when the gate becomes inherited -- a step-06 candidate who
# drops a module from the build reaches exactly it.
#
# The anti-gaming multiplier that stood here at step 05 has moved down into this
# step's own family, where the raw types and suppressions it counts are the ones
# THIS step's candidate added rather than step 05's. See the head of this block.
#
# On one line, and staying on one line. The Dockerfile pulls each family's
# weights out of its own `printf "%.6f"` expression and compares them, in order,
# with the weights the report prints; a continuation splits the expression
# across two lines, the grep finds the half without the printf, and the check
# reports "0 lines look like its weight expression" instead of comparing
# anything.
b12=$(awk -v a="${b12_opt}" -v b="${b12_col}" -v c="${b12_obj}" -v d="${b12_jt}" -v e="${b12_lib}" 'BEGIN {
   printf "%.6f", 0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e
}')
fact "B12.java8-library=${b12}"


# ============================================================== B-13
#
# Step 06's migration, held. The twelve gates above hold steps 01 to 05; this
# one holds the rung immediately below, which is the rung whose work is easiest
# to undo by accident at this one.
#
# B-13 IS STEP 06'S FAMILY C, MINUS ITS C-1, and the subtraction is the whole
# of the adaptation. Step 06's C-1 asserted that every class file was major 54.
# THIS RUNG MOVES IT TO 69. Carrying that leg forward verbatim would ship an
# inherited gate that this step's own requirements make unpassable -- a tree
# that does exactly what the instruction asks scores 0 on a family-B leg -- and
# that is the ninth trap in the shape that is easiest to introduce, because the
# way you introduce it is by copying a block that was correct one rung ago. The
# class-file level is not unheld: B-4 above asks that there is exactly one
# class-file major and that it did not fall below 66 -- five rungs above step
# 06's 54, and therefore a strictly stronger floor than the one step 06 stated
# -- and C-1 below asks that it reached 69. Between them the question is asked
# twice in the right directions and not once in the wrong one.
#
# The six remaining legs keep step 06's own weights, renormalised over 0.90 --
# the share those six carried of step 06's family C -- so that their proportions
# to each other are exactly what step 06 measured them to be and the composite
# is a fraction of one gate rather than of a family.
#
# TWO OF STEP 06'S FOUR PENALTY TERMS MOVED IN HERE, as min() terms on the legs
# they guard, and that is a deliberate change of shape rather than a copy. At
# step 06 the setAccessible floor and the unmodifiable-views hold were
# multipliers on the whole of family C, because what they guarded was that
# step's own C-3 and C-5 being passed the cheap way. Here the thing they guard
# is SIX rungs back, and a tree that deletes step 06's setAccessible calls
# should lose step 06's gate and not this rung's Java 25 score. Left as
# family-C multipliers they would have halved this step's module-import and
# scoped-value work for a regression in step 06's reflection work, which is an
# arithmetic connection between two unrelated things.
#
# baseline/newinstance.txt, boxedctor.txt, builds.txt, views.txt, optops.txt
# and varbuckets.txt are STEP 06'S FROZEN SETS, staged here unchanged. They
# name the buckets that had to change at that rung, and a hold gate has to ask
# about those buckets and not about whatever this tree happens to hold now:
# re-deriving them from this tree would ask "did the migrated sites stay
# migrated" of a population from which every migrated site had already been
# removed, and the denominator would be zero.
#
# baseline/accessible.txt IS THE ONE THAT IS NOT VERBATIM, and B-13.3 below is
# where that shows. Its `guard` half is step 06's twelve buckets unchanged --
# every one of those methods is still here, three of them now reaching their
# field through a VarHandle, and a guard coming back is a failure however the
# field is read. Its `setacc` half is re-stated over the TEN calls that outlived
# STEP 11's C-4, which converted four of step 06's fourteen with three of those
# four sitting in guard buckets. Asking the arrival half over the guard list,
# which is what step 10 did and what copying step 10's block forward would have
# done, reads 9 of 12 on a correctly migrated tree. The ten are still ten here:
# this rung touches no `setAccessible` call, and step 11's verifier over this
# rung's golden reads B-13 full. NOTES 16.16.
#
# baseline/optarr.txt is the exception and is new: see the b13_optarr comment
# where the denominators are read.
#
# baseline/varbuckets.txt IS STAGED AND IS NOT READ HERE, which is why it is
# named in a comment and nowhere else. B-13.7 scores on two counters -- how
# many declarations this tree writes `var` and how many convertible candidates
# it has left -- and not on the 81 buckets, because a bucket list would ask the
# question per method and `var` is decided per declaration. The file is still
# staged because the image's baseline-consistency stage checks those 81 against
# this tree's single surviving candidate, which is the only place the one
# deliberate exemption is evidenced rather than asserted.
b13_2=0; b13_3=0; b13_4=0; b13_5=0; b13_6=0; b13_7=0; b13=0
echo
echo "--- B-13: step 06's Java 9/10 migration, still there"

if [ -z "${measured}" ]; then
   # Nothing was measured. Say so rather than reporting six clean zeros that
   # read like a finding about the tree.
   echo "  B-13: no compile of this tree produced a report -> B-13 = 0"
else
   now_ni=$(sv 'newinstance\.size')
   now_nilegacy=$(sv 'newinstance\.legacy')
   now_nictor=$(sv 'newinstance\.ctor')
   now_isacc=$(sv 'accessible\.isaccessible')
   now_setacc=$(sv 'accessible\.setaccessible')
   now_canacc=$(sv 'accessible\.canaccess')
   now_boxed=$(sv 'boxedctor\.size')
   now_lcview=$(sv 'legacycoll\.view')
   now_lcviewfield=$(sv 'legacycoll\.viewfield')
   now_lcbuild=$(sv 'legacycoll\.build')
   now_factory=$(sv 'factory\.size')
   now_factof=$(sv 'factory\.of')
   now_factcopy=$(sv 'factory\.copyof')
   now_ispresent=$(sv 'optop\.ispresent')
   now_var=$(sv 'var\.size')
   now_varcand=$(sv 'var\.candidates')
   now_varnew=$(sv 'var\.candidatenew')
   now_varmulti=$(sv 'var\.multideclarator')
   now_getpkg=$(sv 'getpackage\.size')
   for k in ni nilegacy nictor isacc setacc canacc boxed lcview lcviewfield \
            lcbuild factory factof factcopy ispresent var varcand varnew \
            varmulti getpkg; do
      eval "now_${k}=\${now_${k}:-0}"
      eval "fact \"gate.src.${k}=\${now_${k}}\""
   done

   # B-13.2  `Class.newInstance` stayed migrated, and the reflection is still
   # there. Step 06's C-2 verbatim: the two buckets have no legacy call, both
   # have a Constructor.newInstance, and the tree holds at least 13 of those in
   # total so that "delete the reflection" cannot pass a gate about migrating it.
   fbuckets newinstance nilegacy form=legacy
   fbuckets newinstance nictorb  form=ctor
   setwant /tests/baseline/newinstance.txt legacy
   fact "gate.b13.c2.want=$(grep -c . "${WORK}/cov-want.txt" || true)"
   b13c2dep=$(gone nilegacy)
   b13c2arr=$(hit nictorb)
   fact "gate.b13.c2.departed=${b13c2dep}"
   fact "gate.b13.c2.arrived=${b13c2arr}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-nilegacy.txt" \
      | sed 's/^/    Class.newInstance is back in: /'
   [ "${now_nictor}" -ge "${b13_ctorfloor:-0}" ] || \
      echo "    ${now_nictor} Constructor.newInstance call(s) against step 06's floor of ${b13_ctorfloor:-0}"
   b13_2=$(minf "$(minf "$(clamp01 "$(frac "${b13c2dep}" "${b13_legacy:-0}")")" \
                        "$(clamp01 "$(frac "${b13c2arr}" "${b13_legacy:-0}")")")" \
                "$(clamp01 "$(frac "${now_nictor}" "${b13_ctorfloor:-0}")")")
   fact "B13.2.newinstance=${b13_2}"

   # B-13.3  the twelve `isAccessible()` guards stayed gone, the ten
   # `setAccessible` calls that outlive C-4 stayed, and there are still ten.
   #
   # THE TWO HALVES READ TWO DIFFERENT WANT-LISTS, WHICH WAS STEP 11'S CHANGE
   # AND IS KEPT HERE. Departure is asked over accessible.txt's `guard` half,
   # twelve buckets, unchanged from step 06. Arrival is asked over its `setacc`
   # half, ten buckets, because step 11's C-4 converted four of step 06's
   # fourteen calls to VarHandle and three of them are in guard buckets: asked
   # over the guard list the arrival half would cap at 9/12 on a tree that did
   # exactly what step 11 asked, and this rung is handed such a tree. setwant()
   # is therefore called twice, once per half, and each comm runs against the
   # want-list its own question needs.
   #
   # THE THIRD TERM IS STEP 06'S PENALTY, moved in. At that rung it halved the
   # whole of family C; here it caps this leg, because what it guards is step
   # 06's reflection work and not step 07's namespace work. A tree that deleted
   # the ten surviving setAccessible calls instead of keeping them loses this
   # leg in proportion and nothing else.
   fbuckets accessible guardb  call=isAccessible
   fbuckets accessible setaccb call=setAccessible
   setwant /tests/baseline/accessible.txt guard
   fact "gate.b13.c3.want=$(grep -c . "${WORK}/cov-want.txt" || true)"
   b13c3dep=$(gone guardb)
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-guardb.txt" | head -8 \
      | sed 's/^/    isAccessible() guard is back in: /'
   setwant /tests/baseline/accessible.txt setacc
   fact "gate.b13.c3.want.setacc=$(grep -c . "${WORK}/cov-want.txt" || true)"
   b13c3arr=$(hit setaccb)
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-setaccb.txt" | head -8 \
      | sed 's/^/    no setAccessible left in: /'
   fact "gate.b13.c3.departed=${b13c3dep}"
   fact "gate.b13.c3.arrived=${b13c3arr}"
   fact "gate.b13.c3.setacc=${now_setacc}"
   [ "${now_setacc}" -ge "${b13_setacc:-0}" ] || \
      echo "    ${now_setacc} setAccessible call(s) against the ${b13_setacc:-0} that outlive C-4 -- the reflection was removed, not converted"
   b13_3=$(minf "$(minf "$(clamp01 "$(frac "${b13c3dep}" "${b13_guards:-0}")")" \
                        "$(clamp01 "$(frac "${b13c3arr}" "${b13_saccb:-0}")")")" \
                "$(clamp01 "$(frac "${now_setacc}" "${b13_setacc:-0}")")")
   fact "B13.3.isaccessible=${b13_3}"

   # B-13.4  no wrapper constructor came back, in step 06's six buckets or
   # anywhere else.
   buckets boxedctor
   grep -v '^#' /tests/baseline/boxedctor.txt | grep . \
      | awk '{ split($1, f, ":"); m = ""
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
               if (m != "") print f[1] "#" m }' \
      | LC_ALL=C sort -u > "${WORK}/cov-want.txt"
   b13c4n=$(grep -c . "${WORK}/cov-want.txt" || true)
   b13c4dep=$(gone boxedctor)
   fact "gate.b13.c4.buckets=${b13c4n}"
   fact "gate.b13.c4.departed=${b13c4dep}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-boxedctor.txt" | head -8 \
      | sed 's/^/    a wrapper constructor is back in: /'
   b13_4=$(minf "$(clamp01 "$(frac "${b13c4dep}" "${b13c4n}")")" \
                "$(clamp01 "$(frac "$(( ${b13_boxed:-0} - now_boxed ))" "${b13_boxed:-0}")")")
   fact "B13.4.boxedctor=${b13_4}"

   # B-13.5  the seven factory conversions stayed factories and the seven views
   # stayed views.
   #
   # THE THIRD TERM IS STEP 06'S OTHER PENALTY, moved in for the same reason as
   # B-13.3's. EXP-06D measured what converting a view over a field costs:
   # `svc.sl-two` differs, non-deterministically, because `Set.of` iterates in a
   # per-JVM salted order. That is a family-A failure waiting on a coin flip,
   # and the cheapest way to provoke it is to "finish" a collections migration
   # somebody already stopped halfway on purpose.
   fbuckets legacycoll lcbuild0 kind=build
   fbuckets legacycoll lcviewl  kind=view arg=local
   LC_ALL=C sort -u "${WORK}/buk-lcbuild0.txt" "${WORK}/buk-lcviewl.txt" \
      > "${WORK}/buk-lcbuildb.txt"
   fbuckets legacycoll lcviewf  kind=view arg=field
   buckets factory
   wantbuckets /tests/baseline/builds.txt
   fact "gate.b13.c5.want=$(grep -c . "${WORK}/cov-want.txt" || true)"
   b13c5dep=$(gone lcbuildb)
   b13c5arr=$(hit factory)
   fact "gate.b13.c5.departed=${b13c5dep}"
   fact "gate.b13.c5.arrived=${b13c5arr}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-factory.txt" | head -8 \
      | sed 's/^/    no List\/Set\/Map factory left in: /'
   wantbuckets /tests/baseline/views.txt
   b13c5held=$(hit lcviewf)
   fact "gate.b13.c5.views.held=${b13c5held}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-lcviewf.txt" \
      | sed 's/^/    no longer an unmodifiable view over a field: /'
   b13_5=$(minf "$(minf "$(clamp01 "$(frac "${b13c5dep}" "${b13_builds:-0}")")" \
                        "$(clamp01 "$(frac "${b13c5arr}" "${b13_builds:-0}")")")" \
                "$(clamp01 "$(frac "${b13c5held}" "${b13_views:-0}")")")
   fact "B13.5.factories=${b13_5}"

   # B-13.6  the `isPresent()` branches stayed gone and the Java 9 calls stayed.
   #
   # THE TWO HALVES DIVIDE BY DIFFERENT NUMBERS, 3 and 2, and that asymmetry is
   # the whole reason optarr.txt exists. Three buckets had to lose an
   # `isPresent()` branch at step 06 and all three did. Two of the three reached
   # a Java 9 method; ProtocolRegistry#findDefaultProtocol reached an
   # early-return Optional.of/empty() loop instead, which is a correct departure
   # and an arrival at nothing this gate can see. Dividing the arrival half by
   # 3 would cap a perfect tree at 0.666667 on this leg forever, for a miss the
   # rung below made.
   fbuckets optop ispresentb isPresent use
   sed -n 's/^optop\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '$2 == "use" && ($3 == "or" || $3 == "ifPresentOrElse" || $3 == "stream") {
                m = ""
                for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
                if (m != "") { split($1, f, ":"); print f[1] "#" m } }' \
      | LC_ALL=C sort -u > "${WORK}/buk-opt9.txt"
   wantbuckets /tests/baseline/optops.txt
   b13c6dep=$(gone ispresentb)
   fact "gate.b13.c6.want=$(grep -c . "${WORK}/cov-want.txt" || true)"
   fact "gate.b13.c6.departed=${b13c6dep}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-ispresentb.txt" \
      | sed 's/^/    branches on isPresent() again: /'
   wantbuckets /tests/baseline/optarr.txt
   b13c6arr=$(hit opt9)
   fact "gate.b13.c6.arrived=${b13c6arr}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-opt9.txt" \
      | sed 's/^/    no or\/ifPresentOrElse\/stream left in: /'
   b13_6=$(minf "$(clamp01 "$(frac "${b13c6dep}" "${b13_optdep:-0}")")" \
                "$(clamp01 "$(frac "${b13c6arr}" "${b13_optarr:-0}")")")
   fact "B13.6.optional9=${b13_6}"

   # B-13.7  `var` is still on the eighty declarations.
   b13c7dep=$(clamp01 "$(frac "$(( ${b13_cand:-0} - now_varnew ))" "${b13_want:-0}")")
   b13c7arr=$(clamp01 "$(frac "${now_var}" "${b13_want:-0}")")
   fact "gate.b13.c7.departed=${b13c7dep}"
   fact "gate.b13.c7.arrived=${b13c7arr}"
   printf '    redundant declarations %s -> %s of %s asked; var on %s\n' \
      "${b13_cand:-0}" "${now_varnew}" "${b13_want:-0}" "${now_var}"
   b13_7=$(minf "${b13c7dep}" "${b13c7arr}")
   fact "B13.7.var=${b13_7}"
fi

# Step 06's own weights for its six surviving legs, renormalised over the 0.90
# they carried between them. On one line, and staying on one line: the
# Dockerfile reads each weight expression out of its own `printf "%.6f"` line.
b13=$(awk -v b="${b13_2}" -v c="${b13_3}" -v d="${b13_4}" -v e="${b13_5}" -v f="${b13_6}" -v g="${b13_7}" 'BEGIN {
   printf "%.6f", (0.12*b + 0.18*c + 0.03*d + 0.26*e + 0.23*f + 0.08*g) / 0.90
}')
fact "B13.java9-10=${b13}"


# ============================================================== B-14
#
# Step 07's migration, held. The thirteen gates above hold steps 01 to 06; this
# one holds the rung immediately below, which is the rung whose work is easiest
# to undo by accident at this one.
#
# B-14 IS STEP 07'S FAMILY C, MINUS ITS C-1, and the subtraction is the whole
# of the adaptation. Step 07's C-1 asserted that every class file was major 55
# and paid that reading only in proportion to the four source legs beside it.
# THIS RUNG MOVES THE CLASS-FILE LEVEL TO 69. Carrying that leg forward
# verbatim would ship an inherited gate that this step's own requirements make
# unpassable -- a tree that compiles at 25 exactly as the instruction asks
# publishes major 69, reads 0 against step 07's 55 and loses a tenth of a
# family-B gate for being right -- and that is the ninth trap in the shape that
# is easiest to introduce, met here a second time and for the same reason B-13
# gives above, because the way you introduce it is by copying a block that was
# correct one rung ago. Step 07's corroboration arithmetic does not soften it:
# C-1 paid its reach TIMES the weighted mean of the four source legs, which is
# what caught a tree that flipped the build file and nothing else, and a reach
# of 0 multiplies to 0 however well the four legs beside it read. The level
# step 07 reached is not unheld, and neither is any level reached since: B-4
# above asks that there is exactly one class-file major and that it is not
# below 66 -- step 11's number, which is four rungs above step 07's 55 and
# therefore subsumes it -- asked as a floor rather than as an equality, which
# is the direction a hold gate asks it in; and C-1 below asks that this rung
# reached 69. Between them the question is asked twice in the right directions
# and not once in the wrong one. See B-4, where the floor stays one rung behind
# the flip, and the gate splits from C-1 again for this reason.
#
# The four remaining legs keep step 07's own weights, renormalised over 0.90 --
# the share those four carried of step 07's family C -- so that their
# proportions to each other are exactly what step 07 measured them to be and
# the composite is a fraction of one gate rather than of a family.
#
# STEP 07'S THREE-TERM ANTI-GAMING PENALTY CAME IN HERE WHOLE, as a multiplier
# on this gate and not on this step's family C, and that placement is the same
# argument step 07 made when it moved two of step 06's four penalty terms into
# B-13. Two of the three terms -- raw types added, @SuppressWarnings added --
# are hygiene measured against baseline/source.txt, which at this rung is step
# 11's blind snapshot and therefore carries step 07's result forward through
# four more rungs of held gates; the third, `Class[]::new` where
# `Class<?>[]::new` was meant, is
# about step 07's own toArray work and about nothing else. What all three guard
# is the previous rung's tree. A candidate who adds raw types at this rung
# should lose the gate that holds the rung where raw types were removed, not
# this rung's Java 25 score, and leaving them on family C would have halved the
# module-import work for a regression in the generics work.
#
# baseline/nsmove.txt, nshold.txt, toarray.txt, http.txt and httpids.txt are
# STEP 07'S FROZEN SETS, staged here unchanged. They name the buckets that had
# to change at that rung, and a hold gate has to ask about those buckets and
# not about whatever this tree happens to hold now: re-deriving them from this
# tree would ask "did the migrated sites stay migrated" of a population from
# which every migrated site had already been removed, and the denominator would
# be zero.
#
# ALL ELEVEN DENOMINATORS ARE STEP 07'S OWN FIGURES, unlike B-13's, and the
# difference is worth naming because it is the ninth trap read in the other
# direction. B-13 had to re-measure one of its eleven on the tree step 07 was
# handed, because the step-06 blind agent missed one arrival and a hold gate
# cannot charge this rung for the last one's miss. The step-07 blind agent
# missed nothing: its run reads familyC=1.000000, C2 through C5 all full, and
# `gate.c5.legacy=0` on a baseline of 14. So every figure step 07 froze is also
# a figure its result achieved, and republishing them under a b14 prefix asks
# exactly the question a hold gate should ask. The re-measurement B-13 needed
# is recorded here as NOT NEEDED rather than skipped, because "the denominators
# are the previous rung's targets" is only safe when that rung's result met
# them, and the way that goes wrong silently is by nobody checking.
b14_2=0; b14_3=0; b14_4=0; b14_5=0; b14pen=1; b14=0
echo
echo "--- B-14: step 07's Java 11 / jakarta migration, still there"

if [ -z "${measured}" ]; then
   # Nothing was measured. Say so rather than reporting four clean zeros that
   # read like a finding about the tree.
   echo "  B-14: no compile of this tree produced a report -> B-14.2..B-14.5 = 0"
else
   nw_jxann=$(sv 'ns\.ref\.javax\.annotation')
   nw_jxejb=$(sv 'ns\.ref\.javax\.ejb')
   nw_jkann=$(sv 'ns\.ref\.jakarta\.annotation')
   nw_jkejb=$(sv 'ns\.ref\.jakarta\.ejb')
   nw_impjx=$(( $(sv 'ns\.import\.javax\.annotation' | grep -E '^[0-9]+$' || echo 0) \
              + $(sv 'ns\.import\.javax\.ejb'        | grep -E '^[0-9]+$' || echo 0) ))
   nw_impjk=$(( $(sv 'ns\.import\.jakarta\.annotation' | grep -E '^[0-9]+$' || echo 0) \
              + $(sv 'ns\.import\.jakarta\.ejb'        | grep -E '^[0-9]+$' || echo 0) ))
   nw_nsfiles=$(sv 'ns\.files')
   nw_tasized=$(sv 'toarray\.sized')
   nw_tactor=$(sv 'toarray\.ctorref')
   nw_taraw=$(sv 'toarray\.raw')
   nw_hlegref=$(sv 'http\.legacyref')
   nw_hlegcall=$(sv 'http\.legacycall')
   nw_hclient=$(sv 'http\.clientref')
   nw_hredir=$(sv 'http\.redirects')
   for k in jxann jxejb jkann jkejb nsfiles tasized tactor taraw \
            hlegref hlegcall hclient hredir; do
      eval "nw_${k}=\${nw_${k}:-0}"
      eval "fact \"gate.src.${k}=\${nw_${k}}\""
   done
   fact "gate.src.impjx=${nw_impjx}"
   fact "gate.src.impjk=${nw_impjk}"

   # ------------------------------------------------------------- B-14.2
   # javax.annotation and javax.ejb become jakarta.annotation and jakarta.ejb.
   #
   # This is the migration line's jakarta bullet, scoped to the part of it that
   # has an answer at this rung. `@Resource` and `@EJB` are the two annotations
   # the enrichers read reflectively; jakarta.annotation-api 2.0.0 and
   # jakarta.ejb-api 4.0.0 are staged on the scorer's source classpath so the
   # migrated tree compiles, and they are on the candidate's too.
   #
   # FOUR TERMS, and each one closes a different cheap pass.
   #
   #   departed buckets   the javax references left the six buckets that hold
   #                      them. Without it, adding a jakarta import next to a
   #                      javax one scores the arrival half
   #   arrived buckets    a jakarta reference is in each of those six. Without
   #                      it, deleting the enrichers scores the departure half
   #   departed total     no javax.annotation or javax.ejb reference survives
   #                      ANYWHERE. Without it, a bucket can be half-moved --
   #                      the import rewritten, one of eleven annotation uses
   #                      left behind -- and both bucket halves read full,
   #                      because a bucket is occupied or not and does not
   #                      count
   #   arrived total      at least 22 jakarta references exist. Without it, one
   #                      reference per bucket scores the arrival half
   #
   # The import counts are published and not scored. They corroborate: a tree
   # whose references moved and whose imports did not has fully qualified them
   # inline, which is a correct migration written oddly, and a gate that failed
   # it would be scoring style.
   nsbuckets jxmove 'p == "javax.annotation" || p == "javax.ejb"'
   nsbuckets jkmove 'p == "jakarta.annotation" || p == "jakarta.ejb"'
   nswant /tests/baseline/nsmove.txt
   # nswant keys on pkg|bucket and the two sides of this gate use DIFFERENT
   # packages, so the want set has to be re-keyed to each side's prefix rather
   # than compared as it stands. The departure side is the file as frozen; the
   # arrival side is the same buckets under the jakarta spelling.
   cp "${WORK}/cov-want.txt" "${WORK}/cov-want-jx.txt"
   sed 's/^javax\./jakarta./' "${WORK}/cov-want-jx.txt" | LC_ALL=C sort -u \
      > "${WORK}/cov-want-jk.txt"
   b14_2n=$(grep -c . "${WORK}/cov-want-jx.txt" || true)
   b14_2dep=$(LC_ALL=C comm -23 "${WORK}/cov-want-jx.txt" "${WORK}/buk-jxmove.txt" | grep -c . || true)
   b14_2arr=$(LC_ALL=C comm -12 "${WORK}/cov-want-jk.txt" "${WORK}/buk-jkmove.txt" | grep -c . || true)
   b14_2jx=$(( nw_jxann + nw_jxejb ))
   b14_2jk=$(( nw_jkann + nw_jkejb ))
   fact "gate.b14.c2.want=${b14_2n}"
   fact "gate.b14.c2.departed=${b14_2dep}"
   fact "gate.b14.c2.arrived=${b14_2arr}"
   fact "gate.b14.c2.javax.left=${b14_2jx}"
   fact "gate.b14.c2.jakarta.refs=${b14_2jk}"
   LC_ALL=C comm -12 "${WORK}/cov-want-jx.txt" "${WORK}/buk-jxmove.txt" \
      | sed 's/^/    still references javax: /'
   LC_ALL=C comm -23 "${WORK}/cov-want-jk.txt" "${WORK}/buk-jkmove.txt" \
      | sed 's/^/    no jakarta reference yet: /'
   [ "${b14_2jx}" -eq 0 ] || \
      echo "    ${b14_2jx} javax.annotation/javax.ejb reference(s) survive somewhere in the tree"
   printf '    imports: javax %s -> jakarta %s\n' "${nw_impjx}" "${nw_impjk}"
   b14_2=$(minf "$(minf "$(clamp01 "$(frac "${b14_2dep}" "${tgt_b14_2_buckets:-0}")")" \
                     "$(clamp01 "$(frac "${b14_2arr}" "${tgt_b14_2_buckets:-0}")")")" \
             "$(minf "$(clamp01 "$(frac "$(( ${tgt_b14_2_refs:-0} - b14_2jx ))" "${tgt_b14_2_refs:-0}")")" \
                     "$(clamp01 "$(frac "${b14_2jk}" "${tgt_b14_2_refs:-0}")")")")
   fact "B14.2.jakarta=${b14_2}"

   # ------------------------------------------------------------- B-14.3
   # The other six javax packages are exactly where they were.
   #
   # 43 (package, bucket) pairs over 112 references. The package is part of the
   # key: one method can hold two javax packages, and with the bucket alone as
   # the key moving one of them would leave it occupied by the other.
   #
   # C-3 IS SCALED BY C-2, AND STEP 07'S FLOOR ARITHMETIC IS WHY. The scaling is
   # step 07's design, recorded here because it is why the shape B-14 inherits
   # is the shape it is. ON STEP 07'S INPUT TREE all 43 pairs held, that tree
   # having migrated nothing; an unscaled C-3 would have paid it 0.17 of step
   # 07's family C -- 0.089 of the reward on top of the 0.474 the step-06
   # solution already scored there, which is 0.563 for standing still and fails
   # migration_prompt item 6 outright. Scaled, a tree that has done none of
   # C-2's work collects none of C-3's, and a tree that has done all of it
   # collects C-3 for having done it WITHOUT breaking the other six packages.
   # That is the question this leg is actually asking, and the scaling is what
   # makes the arithmetic say so.
   #
   # It is not a penalty multiplier, which is the other way this could have been
   # written. A penalty would fire only on damage, and this leg has to pay for
   # PRECISION -- for the candidate who read far enough to find out that
   # javax.management is a JDK package. Precision that is only ever rewarded by
   # the absence of a punishment is indistinguishable from inaction.
   nsbuckets nshold 'p != "javax.annotation" && p != "javax.ejb" && p !~ /^jakarta\./'
   nswant /tests/baseline/nshold.txt
   b14_3n=$(grep -c . "${WORK}/cov-want.txt" || true)
   b14_3held=$(hit nshold)
   grep -v '^#' /tests/baseline/nshold.txt | grep . | awk '{ print $1 }' \
      | LC_ALL=C sort -u > "${WORK}/nshold-pkgs.txt"
   b14_3refs=$(sed -n 's/^ns\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk 'NR == FNR { k[$0] = 1; next } { p = substr($2, 5) } p in k { n++ } END { print n + 0 }' \
             "${WORK}/nshold-pkgs.txt" -)
   fact "gate.b14.c3.want=${b14_3n}"
   fact "gate.b14.c3.held=${b14_3held}"
   fact "gate.b14.c3.refs=${b14_3refs}"
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-nshold.txt" | head -12 \
      | sed 's/^/    a javax reference that had to stay is gone: /'
   [ "${b14_3refs}" -ge "${tgt_b14_3_refs:-0}" ] || \
      echo "    ${b14_3refs} held javax reference(s) against the baseline's ${tgt_b14_3_refs:-0}"
   b14_3raw=$(minf "$(clamp01 "$(frac "${b14_3held}" "${tgt_b14_3_buckets:-0}")")" \
                "$(clamp01 "$(frac "${b14_3refs}" "${tgt_b14_3_refs:-0}")")")
   fact "gate.b14.c3.unscaled=${b14_3raw}"
   b14_3=$(awk -v a="${b14_3raw}" -v b="${b14_2}" 'BEGIN { printf "%.6f", a * b }')
   fact "B14.3.javax-held=${b14_3}"

   # ------------------------------------------------------------- B-14.4
   # `c.toArray(new T[c.size()])` becomes `c.toArray(T[]::new)`.
   #
   # Java 11 added `Collection.toArray(IntFunction)`. The pre-11 spelling
   # allocates the array at the call site and the JDK either fills it or throws
   # it away; the constructor reference hands the collection a sizer and lets it
   # allocate once. 8 calls in 5 buckets, all `new T[...]` with an explicit
   # size or a zero.
   #
   # Four terms, for the same reasons C-2 has four: the bucket halves say the
   # work happened where it was asked, and the totals stop a bucket being
   # half-converted or a site being deleted rather than migrated.
   #
   # The RAW TYPE this leg invites is handled in the penalty below rather than
   # here. `c.toArray(Class[]::new)` compiles, is raw, and is what a hurried
   # conversion of `new Class[list.size()]` produces; `newgeneric.raw` cannot
   # see it, because a member reference is not a `new` expression, and
   # `toarray.raw` is the channel that can.
   fbuckets toarray tasized  form=sized
   fbuckets toarray tactorb  form=ctorref
   wantbuckets /tests/baseline/toarray.txt
   b14_4n=$(grep -c . "${WORK}/cov-want.txt" || true)
   b14_4dep=$(gone tasized)
   b14_4arr=$(hit tactorb)
   fact "gate.b14.c4.want=${b14_4n}"
   fact "gate.b14.c4.departed=${b14_4dep}"
   fact "gate.b14.c4.arrived=${b14_4arr}"
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-tasized.txt" \
      | sed 's/^/    still sizes the array at the call site: /'
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-tactorb.txt" \
      | sed 's/^/    no T[]::new yet: /'
   b14_4=$(minf "$(minf "$(clamp01 "$(frac "${b14_4dep}" "${tgt_b14_4_buckets:-0}")")" \
                     "$(clamp01 "$(frac "${b14_4arr}" "${tgt_b14_4_buckets:-0}")")")" \
             "$(minf "$(clamp01 "$(frac "$(( ${tgt_b14_4_sized:-0} - nw_tasized ))" "${tgt_b14_4_sized:-0}")")" \
                     "$(clamp01 "$(frac "${nw_tactor}" "${tgt_b14_4_sized:-0}")")")")
   fact "B14.4.toarray=${b14_4}"

   # ------------------------------------------------------------- B-14.5
   # HttpURLConnection becomes java.net.http.
   #
   # One file: ServletMethodExecutor.java, whose `execute(String)` opens a
   # URLConnection, casts it to HttpURLConnection, disables caching, polls for a
   # 200 and disconnects. 14 records. Java 11 made java.net.http standard and
   # HttpClient/HttpRequest/HttpResponse replace all of it.
   #
   # SCOPED BY FILE AND NOT BY <file>#<method>, which is the one place this
   # step departs from every other gate in the file. `execute` is a private
   # method whose entire body this migration replaces; a candidate who splits it
   # into a send and a poll, or who renames it, has done the work, and a
   # bucket-scoped arrival half would score that tree 0 for a legitimate
   # refactor. The file is the unit that is stable across the change.
   #
   # THE THREE CDI RECORDS ARE NOT SCORED AND ARE NOT A GATE. An earlier draft
   # of this leg asked for `http.legacyref == 0` tree-wide; the golden reads 3,
   # because CDIInjectionEnricherTestCase#createDeployment holds an anonymous
   # `new URLConnection(u){...}` used as an injection fixture -- not an HTTP
   # call, no HttpClient shape, and `java.net.URLConnection` is not deprecated.
   # The draft gate would have scored the finished migration zero. They are
   # published as gate.b14.c5.cdistub so a trajectory analysis can see them, and
   # what stops a candidate deleting the stub is family A and family B's test
   # gates, which is the right instrument for "a test disappeared".
   #
   # THE LAST QUARTER IS followRedirects, and it is not a detail. This is the
   # one behaviour in the pair that does not carry over by spelling:
   # HttpURLConnection follows redirects by default, HttpClient does not, and a
   # migration that does not name a policy has quietly changed what the code
   # does. 0.75 for moving it and 0.25 for having noticed.
   sed -n 's/^http\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk -v f="${tgt_b14_5_file}" '{ split($1, p, ":") } p[1] == f' > "${WORK}/http-file.txt"
   b14_5leg=$(awk '$2 == "form=legacytype" || $2 == "form=legacycall"' "${WORK}/http-file.txt" | grep -c . || true)
   # Distinct TOP-LEVEL java.net.http types, and the truncation is the whole of
   # what makes three a denominator rather than a spelling.
   #
   # SourceScan records the resolved qualified name, and a nested type resolves
   # to its own: the golden's `execute` publishes five distinct names --
   # HttpClient, HttpClient.Redirect, HttpRequest, HttpResponse and
   # HttpResponse.BodyHandlers -- because it names a redirect policy and a body
   # handler. Counting names as recorded would make the denominator 5 and would
   # charge a correct migration for spelling: `BodyHandlers.ofString()` through
   # a static import, `BodyHandler<String>` written instead of the factory, or a
   # policy read from a constant all do the same work and publish a different
   # set. Truncating at the fourth segment asks the question the prose above
   # asks -- did all three of the replacement types arrive -- and answers it the
   # same way for every spelling of them. Measured on the golden before it was
   # written this way: five names in, three buckets out.
   b14_5arr=$(awk '$2 == "form=clienttype" {
                   for (i = 2; i <= NF; i++)
                      if (substr($i, 1, 5) == "name=") {
                         n = substr($i, 6)
                         split(n, q, ".")
                         if (q[1] == "java" && q[2] == "net" && q[3] == "http" && q[4] != "")
                            n = q[1] "." q[2] "." q[3] "." q[4]
                         print n
                      } }' \
              "${WORK}/http-file.txt" | LC_ALL=C sort -u | grep -c . || true)
   b14_5red=$(awk '$2 == "form=redirects"' "${WORK}/http-file.txt" | grep -c . || true)
   [ "${b14_5red}" -gt 1 ] && b14_5red=1
   b14_5stub=$(( ${nw_hlegref:-0} + ${nw_hlegcall:-0} - b14_5leg ))
   [ "${b14_5stub}" -lt 0 ] && b14_5stub=0
   fact "gate.b14.c5.legacy=${b14_5leg}"
   fact "gate.b14.c5.types=${b14_5arr}"
   fact "gate.b14.c5.redirects=${b14_5red}"
   fact "gate.b14.c5.cdistub=${b14_5stub}"
   [ "${b14_5leg}" -eq 0 ] || \
      printf '    %s of %s legacy HTTP record(s) survive in %s\n' \
         "${b14_5leg}" "${tgt_b14_5_legacy:-0}" "${tgt_b14_5_file}"
   [ "${b14_5red}" -eq 1 ] || \
      echo "    no followRedirects policy is named -- HttpClient does not follow them and HttpURLConnection did"
   [ "${b14_5stub}" -eq "${tgt_b14_5_stub:-0}" ] || \
      printf '    the CDI URLConnection stub reads %s record(s), not %s -- reported, not scored\n' \
         "${b14_5stub}" "${tgt_b14_5_stub:-0}"
   # AND THE SHAPE IS WITHDRAWN WHEN THE EXCHANGE STOPS WORKING.
   #
   # Everything above this point is shape: fourteen records gone, three types
   # arrived, a policy named. Shape is cheap to fake. A file that deletes the
   # polling loop outright, declares HttpClient, HttpRequest and HttpResponse,
   # names a policy and returns null collects every term above and reads C-5 =
   # 1.000000 having done none of the work. What that tree loses is the
   # behaviour of the exchange -- eighteen of family A's 939 ids. THE FIGURES
   # BELOW ARE STEP 07'S, WHERE THE FINDING AROSE, and they are left in the
   # past tense on purpose: at that rung this leg paid 0.54 x 0.525 = 0.2835 of
   # the reward against 18/935 x 0.30 = 0.005775 for the behaviour, a 49-to-1
   # return on gutting a method, and that is not a gate but an invitation.
   # Round 1 of step 07's static review said so and the scaling below is the
   # answer to it (NOTES 12.33).
   #
   # FOUR RUNGS OF DILUTION HAVE SINCE CLOSED THE GAP, WHICH IS NOT A REASON
   # TO DROP THE SCALING. B-14 is worth 0.081776640000 of family B here and
   # family B is 0.175 of the reward, so this leg pays
   # (0.54/0.90) x 0.081776640000 x 0.175 = 0.008587 and the eighteen ids are
   # worth 18/939 x 0.30 = 0.005751: about 1.5 to 1. The incentive is nearly
   # gone, but "nearly" is what the ninth trap lives in, and a gate that
   # stopped reading behaviour the rung it came
   # from tied it to would be a gate that got weaker as it got older.
   #
   # So the leg is scaled by those eighteen ids, exactly the way B-10's third
   # is scaled by the four in baseline/nioids.txt: the list is DATA, one id per
   # line, read at run time rather than a prefix written into this file. Two
   # rungs agreeing on how a source gate is tied to recorded behaviour is worth
   # more than either of them being clever about it.
   #
   # A proviso that cannot be checked is NOT satisfied -- B-10's rule, in
   # B-10's words. No summary means family A did not run, and shape on its own
   # is not evidence of an exchange. The three spellings of zero are kept:
   # want=0 says the list itself could not be read, want=18 broken=18 says the
   # exchange is gone, and a bare 0 is never printed here.
   #
   # A fraction and not a halving, because eighteen ids make a gradient where
   # four made a coin: a rewrite with one broken path has done nearly the work
   # and a rewrite with eighteen has done none of it.
   b14_5exec=0.000000
   b14_5execn=0
   b14_5execbad=0
   if [ -r /tests/baseline/httpids.txt ]; then
      b14_5execn=$(grep -c '^[a-z]' /tests/baseline/httpids.txt 2>/dev/null || true)
      case "${b14_5execn}" in ''|*[!0-9]*) b14_5execn=0 ;; esac
   fi
   if [ "${b14_5execn}" -eq 0 ]; then
      echo "    B-14.5: baseline/httpids.txt names no recorded behaviour of the exchange -- the scaling cannot be read"
   elif [ -r "${summary}" ]; then
      b14_5execbad=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                           /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                           END { print n + 0 }' /tests/baseline/httpids.txt "${summary}")
      b14_5exec=$(clamp01 "$(frac "$(( b14_5execn - b14_5execbad ))" "${b14_5execn}")")
   else
      b14_5execbad="${b14_5execn}"
   fi
   fact "gate.b14.c5.exec.want=${b14_5execn}"
   fact "gate.b14.c5.exec.broken=${b14_5execbad}"
   fact "gate.b14.c5.exec=${b14_5exec}"
   [ "${b14_5execbad}" -eq 0 ] || \
      printf '    %s of %s recorded behaviour(s) of the exchange no longer match -- C-5 scaled by %s\n' \
         "${b14_5execbad}" "${b14_5execn}" "${b14_5exec}"
   # AND THE REDIRECT QUARTER IS PAID ONLY WHEN THE REDIRECT STILL WORKS.
   #
   # gate.b14.c5.redirects is a source fact, and a shallow one: it reads 1 when the
   # rewritten exchange names a followRedirects policy at all. It cannot read
   # WHICH policy. The scan records the argument as a name, the three names are
   # not equivalent, and the one HttpClient uses when nobody names anything is
   # NEVER -- which is precisely what the HttpURLConnection loop did not do.
   #
   # Left alone that is perverse arithmetic, and it is worth writing the numbers
   # out. An agent who writes .followRedirects(Redirect.NEVER) has named a
   # policy, collects the quarter, and still breaks servlet/exec-redirect:
   # 17/18 * 1.00 = 0.944444. An agent who names no policy at all does the same
   # thing at run time, for the same reason, and reads 17/18 * 0.75 = 0.708333.
   # The gate would be paying a quarter of C-5 for a keyword that moved no
   # behaviour, and paying the careless agent more than the silent one.
   #
   # So the quarter carries a proviso, in B-10's shape: the policy has to be
   # named AND the one recording that exercises it has to still match. A proviso
   # that cannot be checked must not be assumed satisfied, so an unreadable
   # summary withdraws the quarter rather than granting it -- which costs
   # nothing extra, because the same unreadable summary has already set b14_5exec
   # to 0 and taken all of C-5 with it.
   b14_5redid=servlet/exec-redirect
   b14_5redok=0
   if [ "${b14_5red}" -eq 0 ]; then
      :
   elif [ ! -r "${summary}" ]; then
      echo "    B-14.5: a followRedirects policy is named, but no behaviour summary can be read --"
      echo "         the quarter that rides on it is withdrawn rather than assumed"
   elif grep -qE "^behavior\.(differ|missing)\[[0-9]+\]=${b14_5redid}\$" "${summary}"; then
      printf '    B-14.5: a followRedirects policy is named, but %s no longer matches --\n' "${b14_5redid}"
      echo "         the policy named is not the one the old loop had; the quarter is withdrawn"
   else
      b14_5redok=1
   fi
   fact "gate.b14.c5.redirects.honoured=${b14_5redok}"
   b14_5=$(awk -v d="$(clamp01 "$(frac "$(( ${tgt_b14_5_legacy:-0} - b14_5leg ))" "${tgt_b14_5_legacy:-0}")")" \
            -v a="$(clamp01 "$(frac "${b14_5arr}" "${tgt_b14_5_types:-0}")")" \
            -v r="${b14_5redok}" -v x="${b14_5exec}" \
            'BEGIN { m = (d < a) ? d : a; printf "%.6f", x * m * (0.75 + 0.25 * r) }')
   fact "B14.5.httpclient=${b14_5}"

   # The anti-gaming multiplier.
   #
   # Not a gate: a gate pays the unmigrated tree for a property it has for free,
   # and every one of the three terms below is a property this step's input has
   # for free -- it has no raw types, no added suppressions, and no toArray
   # constructor references at all, raw or otherwise. It is a penalty on B-14
   # instead -- `b14pen` multiplies `b14` at the composite below and is read
   # nowhere else, so what it costs is 0.081776640000 of family B and not one
   # point of family C -- and the floor arithmetic in the header is why that
   # distinction cannot be waved through: at this rung family A and family B
   # BOTH read 1.000000 on the tree the candidate is handed, which is the
   # tightest the floor has ever been, 0.475, with exactly 0.025 of room under
   # item 6's bar. A family C term paying that tree 0.525*w spends all of it at
   # w = 0.047619, and that budget is shared: C-1 and C-5 are 0.02 apiece and
   # a do-nothing tree that writes both build-script lines spends the whole of
   # it (see the header -- pricing those two one at a time is how 0.08 got
   # past a cap written for 0.047619).
   #
   # Three terms, not step 06's four. The first two are step 04's, carried
   # forward unchanged against this step's own frozen baseline. The third is
   # this step's. Step 06's other two -- the setAccessible floor and the views
   # hold -- did not disappear: they moved into B-13.3 and B-13.5 as min()
   # terms, because what they guard is step 06's work and a regression in it
   # should cost step 06's gate rather than this step's migration score.
   raw_added=$(( ${now_raw:-0} - ${base_raw:-0} ))
   [ "${raw_added}" -lt 0 ] && raw_added=0
   fact "gate.src.raw.added=${raw_added}"
   if [ "${raw_added}" -gt 0 ]; then
      echo "    ${raw_added} raw type(s) added over the baseline's ${base_raw:-0} -> B-14 penalised"
      b14pen=$(awk -v r="${raw_added}" 'BEGIN { v = 1 - r / 10; if (v < 0) v = 0; printf "%.6f", v }')
   fi
   if [ "${now_sup:-0}" -gt "${base_sup:-0}" ]; then
      echo "    ${now_sup} @SuppressWarnings site(s) against the baseline's ${base_sup} -> B-14 halved"
      b14pen=$(awk -v p="${b14pen}" 'BEGIN { printf "%.6f", p / 2 }')
   fi
   # `Class[]::new` where `Class<?>[]::new` was meant. The specific instrument,
   # because the general one cannot see it: a member reference is not a `new`
   # expression and newgeneric.raw counts new expressions. The golden reads 0 --
   # all eight of its constructor references write their type argument out --
   # so this is a property the correct answer has and the hurried one does not.
   if [ "${nw_taraw:-0}" -gt 0 ]; then
      echo "    ${nw_taraw} raw-element toArray constructor reference(s) -- Class[]::new, not Class<?>[]::new -> B-14 halved"
      b14pen=$(awk -v p="${b14pen}" 'BEGIN { printf "%.6f", p / 2 }')
   fi
   fact "gate.src.penalty=${b14pen}"
fi

# The B-14 composite.
#
# Step 07's four surviving legs at step 07's own weights, renormalised over the
# 0.90 that is left when its C-1 is dropped, times the penalty that came in
# whole. 0.07 + 0.17 + 0.12 + 0.54 = 0.90, so dividing by 0.90 restores the
# proportions the four had to each other without inventing a fifth.
#
# On one line, for the reason every weight expression in this file is on one
# line: the Dockerfile greps the printf.
b14=$(awk -v b="${b14_2}" -v c="${b14_3}" -v d="${b14_4}" -v e="${b14_5}" -v p="${b14pen}" 'BEGIN {
   printf "%.6f", p * ((0.07*b + 0.17*c + 0.12*d + 0.54*e) / 0.90)
}')
fact "B14.java11-jakarta=${b14}"

# ============================================================== B-15
#
# B-15 IS STEP 08'S FAMILY C, MINUS ITS C-1, and the subtraction is the whole
# of the adaptation -- the shape B-13 and B-14 have, for the reason they have
# it. Step 08 scored three legs: C-1, the twelve jars each DECLARE a module
# name and the eleven the platform could read still read; C-2, and the twelve
# names are the RIGHT names; C-3, the two hand-rolled service scanners became
# java.util.ServiceLoader while the eleven by-name loads did not move.
#
# C-1 GOES, AND NOTHING GOES WITH IT. It was a min() of three fractions and one
# of them -- the right-name count -- is C-2's own measurement, so the question
# "are the twelve names right" is asked here in full. What C-1 added on top was
# the DECLARED count and the READABLE count, and both are held from inside:
# C-2's fraction is computed against the twelve names staged in
# baseline/modnames.txt, and a jar that stops declaring a name publishes the
# one the platform derives from its FILE name instead, which on this tree is
# not the staged name for any of the twelve. A tree that drops all twelve
# attributes reads this gate at 0.000000 whether or not a declared-count term
# is present. The two facts are still emitted, unscored, so the trajectory
# record carries the platform's verdict.
#
# The derivable-descriptor term does NOT come forward, and deliberately. Step
# 08 read it against ELEVEN -- the number its own input tree already reached --
# precisely so that it raised no floor there. Carried here it would be a term
# the input tree passes for free inside a gate the input tree already reads at
# 1.000000, which is the definition of a gate that measures nothing. What it
# guarded -- "do not invent a second malformed provider file" -- is guarded by
# family A, which replays 88 transcripts through Descriptors.importAs.
#
# The two surviving legs keep step 08's own weights, renormalised over the 0.80
# they carried between them, so their proportions to each other are exactly
# what step 08 measured them to be and the composite is a fraction of one gate
# rather than of a family. 0.35 + 0.45 = 0.80.
#
# THE DENOMINATORS ARE STEP 08'S TARGETS AND THAT IS ONLY SAFE BECAUSE THE RUNG
# MET THEM. B-14's header says the way that goes wrong silently is by nobody
# checking; checked here twice. Step 08's blind run read reward=1.000000, and
# mk-baseline-step10.sh re-measured the seed IT was written for and found
# svc.platformload at 2, svc.loadclass at 11 and thirteen svcload buckets -- the
# figures target.b15.* still carry. Nothing between that rung and this one moves
# a service lookup, and step 11's verifier over this rung's golden reads B-15
# full.
echo
echo "--- B-15: step 08's module names and platform service lookup, held"

# modv <key> -- one fact out of the module gate's report, the way bv() and tv()
# read the two staged baselines. Separate from sv() because this report is not
# the source scan: a tree whose sources do not compile can still publish jars,
# and a tree that publishes nothing can still compile.
modv() { sed -n "s/^$1=//p" "${WORK}/modules.txt" 2>/dev/null | head -1; }

# The platform's reading of the published jars.
#
# The SAME list of jars apisig.sh graded -- ${WORK}/modulejars.txt, filtered to
# artifacts with no classifier -- and not a fresh glob. A -sources.jar holds
# .java files and no packages of classes, so it derives no descriptor and
# belongs on no module path; a gate that globbed *.jar would report seven
# failures that are not the candidate's and are not even wrong.
: > "${WORK}/modules.txt"
if [ -s "${WORK}/modulejars.txt" ]; then
   java -cp /tests/gates/build/arqmodules.jar arqgate.Modules \
        "${WORK}/modules.txt" "${WORK}/modulejars.txt" \
        > "${WORK}/modules.log" 2>&1 \
      || { fact "gate.b15.mod=broken"; sed -n '1,10p' "${WORK}/modules.log" | sed 's/^/    /'; }
else
   fact "gate.b15.mod=no-module-jars"
fi

tgt_b15_modtotal=$(tv 'target\.b15\.mod\.total')
mod_total=$(modv 'mod\.total')
mod_declared=$(modv 'mod\.declared')
mod_derivable=$(modv 'mod\.derivable')
mod_resolved=$(modv 'mod\.resolved')
mod_resolve=$(modv 'mod\.resolve')
for v in mod_total mod_declared mod_derivable mod_resolved; do
   eval "case \"\${${v}:-}\" in ''|*[!0-9]*) ${v}=0 ;; esac"
done
fact "gate.b15.mod.total=${mod_total}"
fact "gate.b15.mod.declared=${mod_declared}"
fact "gate.b15.mod.derivable=${mod_derivable}"
fact "gate.b15.mod.resolved=${mod_resolved}"
fact "gate.b15.mod.resolve=${mod_resolve:-unread}"

# The denominator is the BASELINE's twelve and never mod.total, and that is the
# difference between a gate and a ratio. A tree that publishes one jar and
# names it would read 1/1 on a self-referential denominator and take this leg
# whole having withdrawn eleven artifacts from the build. B-3 would see that,
# but B-15 should not be paying for it either.
[ "${mod_total}" -eq "${tgt_b15_modtotal:-0}" ] || \
   echo "    ${mod_total} published module jar(s) against the ${tgt_b15_modtotal:-0} step 08 published -- the leg below divides by ${tgt_b15_modtotal:-0}"

# ---------------------------------------------------------------- B-15.2
# The twelve names, pairwise against baseline/modnames.txt.
#
# Pairwise and not as two sets: a tree that gives arquillian-api the spi's name
# and the spi the api's has twelve correct names in the wrong twelve jars, and
# a set comparison reads that as perfect.
b15_2=0
b15names=0
if [ -s "${WORK}/modules.txt" ] && [ -r /tests/baseline/modnames.txt ]; then
   sed -n 's/^mod\[[0-9]*\]=//p' "${WORK}/modules.txt" \
      | awk '{ n = "-"; for (i = 2; i <= NF; i++) if (substr($i, 1, 5) == "name=") n = substr($i, 6)
               print $1 " " n }' | LC_ALL=C sort > "${WORK}/mod-got.txt"
   grep -v '^#' /tests/baseline/modnames.txt | grep . | awk '{print $1 " " $2}' \
      | LC_ALL=C sort > "${WORK}/mod-want.txt"
   b15names=$(LC_ALL=C comm -12 "${WORK}/mod-want.txt" "${WORK}/mod-got.txt" | grep -c . || true)
   LC_ALL=C comm -23 "${WORK}/mod-want.txt" "${WORK}/mod-got.txt" | head -6 \
      | sed 's/^/    module name not as published: /'
fi
fact "gate.b15.names=${b15names}"
# Step 08 scaled this leg by its C-1, which is the shape step 05's R5 finding
# put into every family C since: a name is worth what the naming behind it
# earned. With C-1 gone as a leg the scaling survives as what it always was --
# the right-name fraction squared -- so eleven of twelve reads 0.840278 here
# rather than 0.916667, and the last name still costs more than the first ten.
b15right=$(clamp01 "$(frac "${b15names}" "${tgt_b15_modtotal:-0}")")
b15_2=$(awk -v n="${b15right}" 'BEGIN { printf "%.6f", n * n }')
fact "B15.2.module-names=${b15_2}"

# ---------------------------------------------------------------- B-15.3
# The service lookup is still the platform's -- and the eleven by-name loads
# are still there.
#
# Step 08's C-3 whole, including the scaling by the 69 recorded service
# behaviours. The hold half is what makes this a regression gate rather than a
# census: a tree that rewrites all thirteen svcload buckets into
# java.util.ServiceLoader has broken eleven of them, and family A would say so
# in 69 transcripts, but the gate that names step 08's work is where it should
# be charged.
tgt_b15_legacy=$(tv 'target\.b15\.svc\.legacy')
tgt_b15_arrive=$(tv 'target\.b15\.svc\.arrive')
tgt_b15_keep=$(tv 'target\.b15\.svc\.keep')
b15dep=0; b15arr=0; b15keep=0

# NO REPORT IS NOT A DEPARTURE, and this guard is the seventh trap in one line.
# gone() counts the members of a frozen set that have NO bucket, so an empty
# bucket file reads as "both departed" -- and a tree whose sources do not
# compile produces exactly that, as does a stale gate jar that writes no
# svcload records at all.
if [ -n "${measured}" ] && [ -s "${measured}" ]; then
   # legacyscan and loadclass are two spellings of one question -- "this method
   # resolves something by name at run time" -- and fbuckets ANDs its filters,
   # so the union is taken here rather than asked for once.
   fbuckets svcload b15legacyscan form=legacyscan
   fbuckets svcload b15loadclass  form=loadclass
   LC_ALL=C sort -u "${WORK}/buk-b15legacyscan.txt" "${WORK}/buk-b15loadclass.txt" \
      > "${WORK}/buk-b15legacy.txt"

   # The arrival is the CALL and not the type. `java.util.ServiceLoader x;`
   # records a platformtype and loads nothing; only form=platformload is a
   # lookup actually performed.
   fbuckets svcload b15arrive form=platformload

   setwant /tests/baseline/svc.txt legacy
   b15dep=$(clamp01 "$(frac "$(gone b15legacy)" "${tgt_b15_legacy:-0}")")
   LC_ALL=C comm -12 "${WORK}/cov-want.txt" "${WORK}/buk-b15legacy.txt" | head -4 \
      | sed 's/^/    reads META-INF\/services by hand again: /'

   setwant /tests/baseline/svc.txt arrive
   b15arr=$(clamp01 "$(frac "$(hit b15arrive)" "${tgt_b15_arrive:-0}")")
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-b15arrive.txt" | head -4 \
      | sed 's/^/    no java.util.ServiceLoader.load here: /'

   setwant /tests/baseline/svc.txt keep
   b15keep=$(clamp01 "$(frac "$(hit b15legacy)" "${tgt_b15_keep:-0}")")
   LC_ALL=C comm -23 "${WORK}/cov-want.txt" "${WORK}/buk-b15legacy.txt" | head -4 \
      | sed 's/^/    a by-name lookup that is not a service lookup has gone: /'
else
   echo "    B-15.3: no compile of this tree produced a report -> B-15.3 = 0"
fi

fact "gate.b15.departed=${b15dep}"
fact "gate.b15.arrived=${b15arr}"
fact "gate.b15.held=${b15keep}"

# AND SCALED BY THE 69 RECORDINGS THAT GO THROUGH A SERVICE LOOKUP, because
# every one of the three halves above is a fact about source shape, and source
# shape is cheap to produce and cheap to destroy. The list is DATA under
# baseline/, one id per line, staged from step 08 unchanged.
#
# A proviso that cannot be checked is NOT satisfied -- B-10's rule, kept here:
# no summary means family A did not run, and a source shape on its own is not
# evidence that anything still resolves a provider.
b15exec=0.000000
b15execn=0
b15execbad=0
if [ -r /tests/baseline/svcids.txt ]; then
   b15execn=$(grep -c '^[a-z]' /tests/baseline/svcids.txt 2>/dev/null || true)
   case "${b15execn}" in ''|*[!0-9]*) b15execn=0 ;; esac
fi
if [ "${b15execn}" -eq 0 ]; then
   echo "    B-15.3: baseline/svcids.txt names no recorded service-lookup behaviour -- the scaling cannot be read"
elif [ -r "${summary}" ]; then
   b15execbad=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                         /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                         END { print n + 0 }' /tests/baseline/svcids.txt "${summary}")
   b15exec=$(clamp01 "$(frac "$(( b15execn - b15execbad ))" "${b15execn}")")
else
   b15execbad="${b15execn}"
fi
fact "gate.b15.exec.want=${b15execn}"
fact "gate.b15.exec.broken=${b15execbad}"
fact "gate.b15.exec=${b15exec}"
[ "${b15execbad}" -eq 0 ] || \
   printf '    %s of %s recorded service-lookup behaviour(s) no longer match -- B-15.3 scaled by %s\n' \
      "${b15execbad}" "${b15execn}" "${b15exec}"
b15_3=$(awk -v d="${b15dep}" -v a="${b15arr}" -v k="${b15keep}" -v e="${b15exec}" 'BEGIN {
   m = (d < a ? d : a); if (k < m) m = k
   printf "%.6f", m * e
}')
fact "B15.3.platform-lookup=${b15_3}"

# The B-15 composite.
#
# Step 08's two surviving legs at step 08's own weights, renormalised over the
# 0.80 that is left when its C-1 is dropped. 0.35 + 0.45 = 0.80, so dividing by
# 0.80 restores the proportions the two had to each other without inventing a
# third.
#
# On one line, for the reason every weight expression in this file is on one
# line: the Dockerfile greps the printf.
b15=$(awk -v b="${b15_2}" -v c="${b15_3}" 'BEGIN {
   printf "%.6f", (0.35*b + 0.45*c) / 0.80
}')
fact "B15.jpms-services=${b15}"

# ----------------------------- bucket helpers, shared by B-16, B-17 and B-18
#
# HOISTED ABOVE B-16 RATHER THAN LEFT IN FAMILY C, which is where they sat at
# step 09 and where the legs that use them now no longer are. THREE BLOCKS READ
# THEM AND NONE OF THE THREE IS THIS RUNG'S: B-16 is step 09's family C, B-17
# is step 10's and B-18 is step 11's. One copy, defined once, above the first
# block that reads it -- two copies that drifted would be two gates agreeing
# about a tree for different reasons.
#
# THIS IS THE FIRST RUNG WHOSE OWN FAMILY C READS NONE OF THEM, and the banner
# says so rather than carrying the rung below's `and C` forward, because a
# reader who is told family C reads pbuckets() will go and look. Every one of
# these nine counts SITES IN A METHOD, and not one of this rung's five legs is
# counted that way: C-1 is a census of class files, C-2 compares a SET per
# FILE, C-3 counts held import lines by the `why=` on each, C-4 counts field
# declarations, and C-5 reads a JVM argument list. A leg that wanted a bucket
# here would be a leg about methods, and this rung has none.
#
# Nine of them, and which block reads which is not an accident of history:
#
#   cbuckets  B-16, B-17, B-18 counts per method, out of the candidate's report
#   fcount    B-16, B-18       the same, keyed by file
#   capsum    B-16             min() per bucket against a pre-bucketed file
#   capwant   B-16             that file's own denominator
#   cnames    B-17             `<bucket> <simple type>` pairs, one per name
#   pbuckets  B-18             counts per method, out of a RAW frozen file
#   ptot      B-18             that file's own denominator
#   psum      B-18             capsum()'s rule, for the files pbuckets() writes
#   pdep      B-18             the departure half, max(0, frozen - counted)
#
# ONLY cbuckets() IS READ BY ALL THREE, and the two halves of the list are two
# spellings of one rule rather than two rules. Steps 06 through 10 froze their
# populations already bucketed, as `<tag> <bucket> <n>`, so capsum() and
# capwant() read the bucket straight off the line. Step 11's populations are
# frozen AS THE PLUGIN WROTE THEM, one raw record per line, so pbuckets() has
# to derive the bucket first and ptot(), psum() and pdep() work off what it
# derived. min() per bucket is min() per bucket in both halves; a leg of B-18
# calls psum(), a leg of B-16 calls capsum(), and neither can call the other's
# because neither can read the other's file.
# cbuckets <record kind> <tag> [<field>] -- like fbuckets(), but keeping the
# COUNT of matching records in each bucket rather than only the bucket's name,
# into ${WORK}/cnt-<tag>.txt as `<bucket> <n>`.
#
# A count and not a set, because legs in BOTH families are about HOW MANY
# sites in a method moved -- B-16.2 and B-16.3 here, and C-2 and C-4 below.
# ObserverImpl#invoke holds two convertible
# `instanceof` tests and Reflections#getType holds two; a set-valued bucket
# reads "one of the two converted" as done. Capped at what the baseline froze,
# by capsum() below, so that adding a nineteenth site to a bucket cannot pay
# for the one that was not converted.
#
# A record with no `in=` field contributes no bucket, so a stale gate jar reads
# as "nothing is there" rather than as "everything departed" -- the seventh
# trap, the safe direction, the same one fbuckets() takes.
cbuckets() {
   local kind=$1 tag=$2 w1=${3:-}
   sed -n "s/^${kind}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk -v w1="${w1}" '
         { m = ""; h1 = (w1 == "" ? 1 : 0)
           for (i = 2; i <= NF; i++) {
              if (substr($i, 1, 3) == "in=") m = substr($i, 4)
              if (w1 != "" && $i == w1) h1 = 1
           }
           if (h1 && m != "") { split($1, a, ":"); n[a[1] "#" m]++ } }
         END { for (k in n) print k, n[k] }' \
      | LC_ALL=C sort > "${WORK}/cnt-${tag}.txt"
}

# fcount <record kind> <tag> [<field>] -- the same, keyed by FILE rather than
# by bucket, into ${WORK}/fil-<tag>.txt as `<file> <n>`.
#
# `sealed` and `switch` records have no `in=` field and cannot have one: a
# declaration and a statement are not inside a method in the sense the bucket
# key means. The plugin's own header says so. So the key is the file, and every
# gate below that uses it is stated over files in baseline/ as well -- B-16.5
# reads `switch` through this helper for exactly that reason.
#
# `textblock` is NOT in that group any more. It carries an `in=` field as of
# step 09's review round 1 because that leg's arrival half is a method-scoped
# claim, so B-16.3 reads text blocks through cbuckets() above rather than
# through here.
fcount() {
   local kind=$1 tag=$2 w1=${3:-}
   sed -n "s/^${kind}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk -v w1="${w1}" '
         { h1 = (w1 == "" ? 1 : 0)
           for (i = 2; i <= NF; i++) if (w1 != "" && $i == w1) h1 = 1
           if (h1) { split($1, a, ":"); n[a[1]]++ } }
         END { for (k in n) print k, n[k] }' \
      | LC_ALL=C sort > "${WORK}/fil-${tag}.txt"
}

# capsum <set file> <tag> <count file> -- the sum, over every line of the set
# file whose first field is <tag>, of min(what was counted, what was frozen).
#
# min() per bucket and not a total, for the reason cbuckets() keeps counts: a
# plain sum would let a bucket that gained a site pay for a bucket that lost
# one. Missing counts read 0, which is the strict direction.
capsum() {
   awk -v t="$2" '
      FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == t) want[$2] = $3; next }
      { have[$1] = $2 }
      END { s = 0
            for (b in want) { h = (b in have) ? have[b] : 0
                              s += (h < want[b]) ? h : want[b] }
            print s + 0 }' "$1" "$3"
}

# capwant <set file> <tag> -- the denominator capsum() is measured against,
# read out of the same file rather than from a target.txt key, so that the two
# cannot disagree. target.txt carries the same figures and the Dockerfile
# asserts they match.
capwant() {
   awk -v t="$2" '$0 !~ /^#/ && $1 == t { s += $3 } END { print s + 0 }' "$1"
}

# cnames <record kind> <tag> <field> -- `<bucket> <simple type name>` pairs, one
# line per NAME, into ${WORK}/nam-<tag>.txt.
#
# The third helper shape this file needs and the one step 09 had no use for. Its
# legs asked HOW MANY sites in a method moved; two of step 10's -- B-17.2 and
# B-17.4 here -- ask WHICH TYPE the method now names, because a reflective test
# that has gone is only half of a type pattern and the other half is the type
# itself appearing in a language test at the same place. B-17 IS ITS ONLY
# CALLER AT THIS RUNG: no leg of this rung's family C asks about a type name.
#
# <field> is `type` for an `ipattern` record, which carries one resolved type,
# and `types` for a `swpat` record, which carries the semicolon-joined list of
# the types its arms bind. One helper for both because the claim is the same
# claim: this method names this type in a pattern.
#
# PACKAGE QUALIFIERS ARE STRIPPED, and that is a requirement rather than a
# convenience. A `swpat` record's `types=` is the pattern's SOURCE TEXT, so it
# reads `WebArchive` where the file imported the type and
# `org.jboss.shrinkwrap.api.spec.WebArchive` where it did not -- both legal,
# both the same promise -- while an `ipattern` record's `type=` is always the
# resolved name. Leading segments that begin with a lower-case letter are
# dropped from each, which is C-4's rule at step 09 and leaves a nested type's
# outer name alone. A trailing type argument list goes with them: `List<String>`
# and `List` are one type here.
#
# A record with no `in=` field contributes nothing, and neither does an empty or
# `-` type. Both are the safe direction -- the seventh trap -- and both are
# reachable: `swpat` is emitted only where a switch binds at least one pattern,
# so a stale gate jar that emits none reads as "no type is named anywhere".
cnames() {
   local kind=$1 tag=$2 fld=$3
   sed -n "s/^${kind}\[[0-9]*\]=//p" "${measured}" 2>/dev/null \
      | awk -v fld="${fld}=" '
         { m = ""; v = ""; fl = length(fld)
           for (i = 2; i <= NF; i++) {
              if (substr($i, 1, 3) == "in=") m = substr($i, 4)
              else if (substr($i, 1, fl) == fld) v = substr($i, fl + 1)
           }
           if (m == "" || v == "" || v == "-") next
           split($1, a, ":")
           k = split(v, P, ";")
           for (j = 1; j <= k; j++) {
              s = P[j]
              sub(/<.*$/, "", s)
              while (match(s, /^[a-z][A-Za-z0-9_]*\./)) s = substr(s, RLENGTH + 1)
              if (s != "") print a[1] "#" m, s
           } }' \
      | LC_ALL=C sort -u > "${WORK}/nam-${tag}.txt"
}

# pbuckets <population file> <tag> [<field>] [<bucket file to subtract>] -- the
# same shape as cbuckets(), but over a FROZEN POPULATION rather than over the
# candidate's report, into ${WORK}/set-<tag>.txt as `<bucket> <n>`.
#
# THE FOURTH HELPER SHAPE, AND THE RUNG THAT NEEDED IT IS THIS ONE. Steps 06
# through 10 froze their populations as `<tag> <bucket> <n>` -- already
# bucketed, read by capsum() and capwant() above. Step 11's three own
# populations are frozen AS THE PLUGIN WROTE THEM, one raw record per line,
# because for a leg whose subject is a removed API the record IS the evidence
# and a reader of a failed run has to be able to find the site. So the bucket
# is derived here, and it is derived by the same three lines cbuckets() uses,
# out of the same `<file>:<line> ... in=<method>` shape. The line number is
# read by nobody, which is what lets the three come forward byte-for-byte at
# this rung: they name 45 files between them, C-2 rewrites the import block of
# 42 of those, and every line number below an import block moves when it does.
# A gate that compared them would fail a correct tree 42 times.
#
# <field> selects a subset the way cbuckets()'s third argument does -- a whole
# word, `kind=unread` or `verdict=convert`, matched against the record's own
# fields. <bucket file to subtract> drops every bucket named in the first
# column of another set file, which is how C-3 states its denominators over
# what survives C-2 without typing the survivors anywhere. It defaults to
# /dev/null, which drops nothing.
#
# A record with no `in=` field contributes no bucket, as in cbuckets(): the
# safe direction, the seventh trap.
pbuckets() {
   local pop=$1 tag=$2 w1=${3:-} ex=${4:-/dev/null}
   awk -v w1="${w1}" '
      FILENAME == ARGV[1] { drop[$1] = 1; next }
      $0 ~ /^#/ || $0 == "" { next }
      { m = ""; h1 = (w1 == "" ? 1 : 0)
        for (i = 2; i <= NF; i++) {
           if (substr($i, 1, 3) == "in=") m = substr($i, 4)
           if (w1 != "" && $i == w1) h1 = 1
        }
        if (!h1 || m == "") next
        split($1, a, ":")
        b = a[1] "#" m
        if (!(b in drop)) n[b]++ }
      END { for (k in n) print k, n[k] }' "${ex}" "${pop}" \
   | LC_ALL=C sort > "${WORK}/set-${tag}.txt"
}

# ptot <set file> -- the denominator, summed out of the frozen file itself
# rather than from a target.txt key, so that the two cannot disagree.
# target.txt carries the same figures and the Dockerfile asserts they match.
# capwant()'s rule, for the set files pbuckets() writes.
ptot() { awk '{ s += $2 } END { print s + 0 }' "$1"; }

# psum <set file> <count file> -- the sum, over every frozen bucket, of
# min(what was counted, what was frozen). capsum()'s rule, and kept for
# capsum()'s reason: a plain sum would let a bucket that gained a site pay for
# a bucket that lost one. A missing count reads 0, which is the strict
# direction.
psum() {
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { s = 0
              for (b in w) { g = (b in h) ? h[b] : 0
                             s += (g < w[b]) ? g : w[b] }
              print s + 0 }' "$1" "$2"
}

# pdep <set file> <count file> -- the departure half: the sum, over every
# frozen bucket, of max(0, what was frozen - what is still counted). Clamped at
# zero per bucket so a method that lost a site it never had cannot pay for one
# that kept the site it did.
pdep() {
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { s = 0
              for (b in w) { g = (b in h) ? h[b] : 0
                             d = w[b] - g
                             s += (d > 0) ? d : 0 }
              print s + 0 }' "$1" "$2"
}

# ============================================================== B-16
#
# B-16 IS STEP 09'S FAMILY C, MINUS ITS C-1, and the subtraction is the whole
# of the adaptation -- the shape B-13, B-14 and B-15 have, for the reason they
# have it. Step 09 scored five legs: C-1, every class file the build emits is
# major 61; C-2, an `instanceof` followed by a cast of the same expression is a
# binding pattern and the seven without a cast are not; C-3, the one HTML
# document becomes a text block and the two XML fragment builders do not; C-4,
# the eleven event hierarchies close and the eleven extension seams stay open;
# C-5, the two colon-form switches become arrow switches.
#
# C-1 WENT BECAUSE B-4 ASKED IT, one rung ago. This is the ordinary
# subtraction -- the one steps 05, 06 and 07 handed forward and step 08 could
# not, because its must-flip was a manifest attribute and no class-file level
# moved. Step 09's level DID move, 55 -> 61, so when step 10 made this gate out
# of step 09's family C its HOLD_MAJOR read 61 and its B-4 asked "one major,
# and not below 61".
#
# AND ON THIS RUNG HOLD_MAJOR IS 66, WHICH IS WHY THE SUBTRACTION STAYS MADE
# RATHER THAN BEING RE-ARGUED. B-4 here asks "one major, and not below 66" --
# step 11's rung, staged as `was=` in baseline/level.txt -- and 66 is five
# majors above 61, so the floor B-16's C-1 would have asserted is subsumed by
# the one B-4 already asserts. Restoring a level leg here would pay a tree a
# third time for a constant B-4 and C-1 between them already price once each.
#
# THE OTHER FOUR COME FORWARD WHOLE, WITH THEIR OWN DENOMINATORS AND THEIR OWN
# HOLD HALVES. Each of the four is a min() of a departure and a hold over a set
# frozen in baseline/, and the hold halves are the reason they are worth
# carrying. Step 10 asked a candidate to rewrite reflective type tests as type
# patterns and if-chains as pattern switches, which is work in exactly the files
# step 09's C-2 and C-4 are stated over: a candidate who reached for the nearest
# construct everywhere -- a pattern variable at the seven sites that have no
# cast, a `sealed` modifier on the eleven roots this library is extended through
# -- undid step 09 while appearing to do step 10, and nothing else in that file
# would have seen it. THIS rung's own work is nowhere near those files' pattern
# tests, which is exactly when an inherited gate stops being re-read and starts
# being assumed. THE NINTH TRAP IS ABOUT PRECISELY THAT, and the fixtures
# rehearse it: every inherited gate is scored on every fixture, including the
# fixtures written three rungs below.
#
# THE FOUR KEEP STEP 09'S OWN WEIGHTS, renormalised over the 0.90 they carried
# between them once C-1's 0.10 is gone: 0.20 + 0.26 + 0.36 + 0.08 = 0.90. Their
# proportions to each other are exactly what step 09's automated-migrator
# calibration measured them to be -- NOTES 14.15 -- and the composite is a
# fraction of one gate rather than of a family. The same construction B-15 used
# on step 08's two legs over 0.80.
#
# THE DENOMINATORS ARE STEP 09'S TARGETS AND THAT IS ONLY SAFE BECAUSE THE RUNG
# MET THEM. Checked twice, as at every rung since B-14's header first said how
# this goes wrong silently. Step 09's blind run read reward=1.000000, and
# mk-baseline-step10.sh re-measured eleven of the fifteen target.b16.* figures
# against the seed IT was written for rather than copying them forward; the four
# it could not re-measure are named in baseline/target.txt and in NOTES 15.7, and
# every one of them is a quantity this tree cannot answer for -- three because
# the work being inherited has already consumed them, one because it counts
# recordings rather than sites.
echo
echo "--- B-16: step 09's Java 17 language constructs, held"

# The sub-terms the report prints, initialised here rather than inside the
# guards that compute them -- a leg whose compile never happened still has to
# print a row.
b16_2conv=0; b16_2hold=0; b16_2tot=0; b16_5hit=0
tgt_b16_sw_files=0; tgt_b16_sealed_roots=0; tgt_b16_sealed_open=0
# ---------------------------------------------------------------- B-16.2
# `instanceof` followed by a cast becomes a pattern -- and only there.
#
# THE SET IS FROZEN AND IT HAS TWO HALVES IN ONE FILE. baseline/ipattern.txt
# carries `conv <bucket> <n>` for the 14 methods holding the 17 tests that are
# followed by a cast of the same expression to the tested type, and
# `skip <bucket> <n>` for the 7 that are not. One file, the shape step 06 put
# into accessible.txt and newinstance.txt, for the reason it gave: keeping both
# populations in one file is what makes it impossible to stage half of the pair.
#
# THE PLUGIN DECIDES WHICH HALF A SITE IS IN, NOT THIS SCRIPT AND NOT A HUMAN.
# SourceScan writes `conv=yes` when the enclosing unit contains a cast of the
# tested expression's own source text to the tested type, and `conv=no`
# otherwise, and mk-baseline-step09.sh froze the two lists straight out of that
# field. THE FILE IS STEP 11'S RE-CUT, STAGED HERE BYTE-FOR-BYTE, AND IT WAS
# THE FIRST POPULATION FILE IN THE LADDER THAT COULD NOT BE STAGED FORWARD
# UNCHANGED. Step 09 cut 18 conv buckets over 21 bound sites plus the 7 skip, 28
# in all, and seven of those conv buckets were the seven
# `SecurityActions#getConstructor` ladders STEP 11's C-2 deleted -- a method that
# is gone cannot hold a pattern. mk-baseline-step11.sh therefore re-cut the
# partition ON THAT RUNG'S GOLDEN, out of the same `conv=` field and by the same
# rule: 14 conv buckets over 17 sites, the same 7 skip, 24 in all. This rung
# renames no method in any of them and re-cuts nothing -- the one shape of C-4
# that would have, a `fire` split into a binder and a body, was rejected for
# that reason (NOTES 16.61). Staging step 09's file forward would have charged
# a correct tree 14 of its 21 conv SITES -- over 11 of its 18 buckets, because
# capsum() takes a per-bucket min and a departed bucket contributes zero -- and
# capped B-16.2 at 0.666667. That figure is the whole leg and not just a term:
# convfrac 14/21 sits below holdfrac 7/7, and below totfrac however that term's
# denominator is taken (24/24 if target.b16.ip.total were still re-measured on
# the golden, 24/28 if it had been staged with the file), so the min() is
# convfrac either way. The ninth trap arriving through the baseline rather than
# through the gate list, which is the one way an inherited gate can punish the
# work a rung asks for. NOTES 16.16, and baseline/target.txt says it again over
# the figures.
#
# AND THE POPULATION MOVED IN BOTH DIRECTIONS, WHICH IS WHY 18 - 7 IS NOT 14.
# THREE BUCKETS ARRIVED between step 09's cut and step 11's seed:
# DeploymentGenerator#buildTestableDeployments, OSGiDeploymentPackager
# #generateDeployment and ResourceInjectionEnricher#isPrimitiveNull, one bound
# pattern each. STEP 10's OWN REQUIREMENT 4 PUT THEM THERE -- they are three of
# its `conv litinstance` rows, `X.class.isInstance(v)` rewritten to
# `v instanceof X x`, and step 10's baseline/reftest.txt names all three. So
# 18 - 7 + 3 = 14 conv buckets and 21 - 7 + 3 = 17 sites. The eleven buckets
# the two files share hold 14 sites in each and not one of them changed its
# count, and the 7 skip rows are byte for byte step 09's -- so the only two
# movements in the file are the seven departures and the three arrivals, and
# both are somebody's requirement rather than drift.
#
# THAT IS ALSO WHAT target.b16.ip.was.* MEANS, AND IT IS NOT STEP 09's FILE.
# Those three keys read 21 buckets / 24 sites / 31 total because they were cut
# on STEP 11'S SEED -- step 10's golden, where the seven ladders still stood AND
# the three arrivals were already in place. 18 / 21 / 28 is what step 09 froze,
# and no key carries it, which is correct: the `was` in those names is about the
# tree at the start of the rung that re-cut the file, not about the last rung
# that cut it. It also explains why that re-cut had to run on the golden rather
# than on the frozen list -- a list naming seven methods that were about to
# leave would have reproduced the damage instead of finding it.
#
# THE THREE ARE STAGED AND READ BY NOTHING, at this rung and at step 11 before
# it: they are provenance for the 14 / 17 / 24 above them and no gate consults
# them, which is why they can be carried out of step 11's target.txt verbatim
# even though the tree THIS rung is handed is step 11's golden, where the seven
# ladders are already gone. A key a gate read would have had to be re-cut; a key
# that records how a denominator came to be does not.
# What WAS re-measured besides are the denominators the file is read against --
# target.b16.ip.* -- because every tree from step 10's golden on is one where
# step 09's work is already done. This rung carries them unchanged. What is graded
# here is `kind=bound` -- whether a binding pattern is actually present --
# because `conv` is a question about the UNCONVERTED spelling and empties
# itself as the work is done. Grading `conv` would be grading a denominator
# that the candidate's own edits erase.
#
# THREE TERMS, ALL min()ed.
#
#   the 17 convert    sum over the 14 buckets of min(bound sites found, sites
#                     frozen), over 17. Capped per bucket because ObserverImpl
#                     #invoke and Reflections#getType and #isType hold two
#                     each, and a bucket that gained a site must not pay for a
#                     bucket that lost one.
#   the 7 stay        sum over the 7 buckets of min(plain sites found, sites
#                     frozen), over 7. Without this a candidate who converts
#                     all 24 scores the leg whole, and converting all 24 is
#                     the specific wrong answer this construct invites: six of
#                     the seven are `catch (Throwable t) { if (t instanceof X)
#                     throw t; }` and an assertion about a type, where a
#                     pattern variable would be declared and never read.
#   24 remain         ipattern.size against the frozen total. It shuts the
#                     door a candidate could otherwise walk through by DELETING
#                     the sites it did not want to convert, since a deleted site
#                     is neither bound nor plain.
#
#                     AND THIS IS THE TERM THE RE-CUT MOVED. The seed scans 31
#                     instanceof sites against a frozen total of 24, because the
#                     seven ladders are still standing; the golden scans exactly
#                     24, because they are not. So the term is slack at the
#                     start of the rung, exactly tight at the end of it, and
#                     falls only on a tree that deleted sites BEYOND the seven
#                     C-2 takes with it. Both ends were measured rather than
#                     argued: `gate.b16.2.sites` reads 31 on the nop fixture and
#                     24 on the golden in fixtures/c-rehearsal.sh.
#
#                     THIS TERM CAN NEVER BE THE min() ON A REACHABLE TREE, and
#                     it is kept anyway. A deleted site comes off the 17 or off
#                     the 7 AND off the 24, and 1/17 and 1/7 are both bigger
#                     than 1/24, so whichever of the other two terms owned that
#                     site has already fallen further. Delete k sites beyond the
#                     seven, j of them from the 7 and k-j from the 17. totfrac
#                     is 1 - k/24. If j >= 7k/24 then j/7 >= k/24, so holdfrac
#                     is at or below totfrac. Otherwise j < 7k/24, so
#                     k-j > 17k/24, so (k-j)/17 > k/24 and convfrac is strictly
#                     below it. Either way the min() is one of the other two,
#                     equality only at k = 0.
#
#                     It stays for two reasons. It is PUBLISHED -- `fact
#                     gate.b16.2.totfrac` below -- so a report says what the tree
#                     still HELD and not only what it converted. And it is what
#                     closes the deletion hole if a later rung re-weights this
#                     leg: the dominance above is an arithmetic consequence of 7
#                     and 17 being smaller than 24, not a property of the
#                     construct, so a rung that dropped one of the two halves or
#                     widened a denominator would reopen the hole with nothing
#                     else holding it shut.
#
#                     Clamped, so a tree that ADDS an `instanceof` somewhere is
#                     not paid for it either way.
b16_2=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/ipattern.txt ]; then
   cbuckets ipattern ipbound kind=bound
   cbuckets ipattern ipplain kind=plain
   ip_conv_n=$(capsum /tests/baseline/ipattern.txt conv "${WORK}/cnt-ipbound.txt")
   ip_hold_n=$(capsum /tests/baseline/ipattern.txt skip "${WORK}/cnt-ipplain.txt")
   ip_conv_w=$(capwant /tests/baseline/ipattern.txt conv)
   ip_hold_w=$(capwant /tests/baseline/ipattern.txt skip)
   ip_size=$(sv 'ipattern\.size'); case "${ip_size:-}" in ''|*[!0-9]*) ip_size=0 ;; esac
   tgt_b16_ip_total=$(tv 'target\.b16\.ip\.total')
   fact "gate.b16.2.converted=${ip_conv_n}"
   fact "gate.b16.2.converted.want=${ip_conv_w}"
   fact "gate.b16.2.held=${ip_hold_n}"
   fact "gate.b16.2.held.want=${ip_hold_w}"
   fact "gate.b16.2.sites=${ip_size}"
   fact "gate.b16.2.sites.want=${tgt_b16_ip_total:-0}"
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == "conv") w[$2] = $3; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    still a plain instanceof (" g " of " w[b] " bound): " b } }' \
      /tests/baseline/ipattern.txt "${WORK}/cnt-ipbound.txt" | LC_ALL=C sort | head -6
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == "skip") w[$2] = $3; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    an instanceof with no cast after it was changed anyway: " b } }' \
      /tests/baseline/ipattern.txt "${WORK}/cnt-ipplain.txt" | LC_ALL=C sort | head -6
   b16_2conv=$(clamp01 "$(frac "${ip_conv_n}" "${ip_conv_w}")")
   b16_2hold=$(clamp01 "$(frac "${ip_hold_n}" "${ip_hold_w}")")
   b16_2tot=$(clamp01 "$(frac "${ip_size}" "${tgt_b16_ip_total:-0}")")
   fact "gate.b16.2.convfrac=${b16_2conv}"
   fact "gate.b16.2.holdfrac=${b16_2hold}"
   fact "gate.b16.2.totfrac=${b16_2tot}"
   b16_2=$(minf "$(minf "${b16_2conv}" "${b16_2hold}")" "${b16_2tot}")
else
   echo "    B-16.2: no compile of this tree produced a report -> B-16.2 = 0"
fi
fact "B16.2.pattern-instanceof=${b16_2}"

# ---------------------------------------------------------------- B-16.3
# The one document becomes a text block; the two fragment builders do not.
#
# baseline/strnl.txt carries `move <bucket> <n>` for the single method that
# writes a document -- ServletTestRunner#doGet, 16 newline literals -- and
# `hold <bucket> <n>` for the two that build XML by recursion, 3 and 4.
#
# THE MOVE HALF NEEDS BOTH DIRECTIONS, BOTH KEYED TO THE BUCKET. A bucket whose
# newline literals have all gone could have had its body deleted, so the
# departure alone is not evidence; a text block is not evidence on its own
# either, since a candidate could add one anywhere. Both together, per move
# bucket: NO newline literal left in the bucket AND at least one text block IN
# THAT SAME BUCKET.
#
# Keyed by FILE, as this gate read until round 1, the arrival was a formality:
# an unused `"""..."""` constant at class scope, sitting beside sixteen deleted
# literals, satisfied it in a tree where the document never became a text block
# at all. That is the gaming path this closes. `textblock` records now carry an
# `in=` field -- the plugin was changed to emit one, and publishes
# `textblock.methods` beside `.size` and `.files` -- because the arrival half is
# a claim about ONE METHOD, the method that used to write a document a line at a
# time, and the bucket key is the only way to state it. Departure and arrival
# are now read on the same key, which is the pair the claim actually needs.
#
# THE HOLD HALF IS THE LEG. "Multi-line string, therefore text block" is the
# wrong reading of this construct and it is the reading an automated migrator
# and a hurried agent both reach for. format() and formatNode() interpolate the
# node's own name into each fragment and recurse; a text block would have to be
# broken back apart to do it, or formatted() called on it, and either is longer
# and slower than what is there. Their 7 newline literals have to survive.
#
# AND SCALED BY THE FIVE RECORDINGS THAT CAPTURE THE DOCUMENT'S BYTES, because
# a text block is the one construct on step 09's rung that can change behaviour
# while compiling perfectly. `webrunner.html-body` records body.length=245 and
# seventeen body[] lines; a text block that loses the final newline reads 244,
# one that is written with the closing delimiter mis-indented re-indents every
# line, and one whose lines were reordered while being merged serves a
# different page. baseline/tbids.txt is the five ids, staged as DATA.
b16_3=0
b16_3move=0
b16_3hold=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/strnl.txt ]; then
   cbuckets strnl tbleft
   cbuckets textblock tbin
   tb_move_w=$(awk '$0 !~ /^#/ && $1 == "move" { n++ } END { print n + 0 }' /tests/baseline/strnl.txt)
   tb_move_n=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == "move") mv[$2] = 1; next }
                    FILENAME == ARGV[2] { left[$1] = $2; next }
                    { tbf[$1] = $2 }
                    END { n = 0
                          for (b in mv) if (!(b in left) && (b in tbf)) n++
                          print n + 0 }' \
                 /tests/baseline/strnl.txt "${WORK}/cnt-tbleft.txt" "${WORK}/cnt-tbin.txt")
   tb_hold_n=$(capsum /tests/baseline/strnl.txt hold "${WORK}/cnt-tbleft.txt")
   tb_hold_w=$(capwant /tests/baseline/strnl.txt hold)
   fact "gate.b16.3.moved=${tb_move_n}"
   fact "gate.b16.3.moved.want=${tb_move_w}"
   fact "gate.b16.3.held=${tb_hold_n}"
   fact "gate.b16.3.held.want=${tb_hold_w}"
   fact "gate.b16.3.textblocks=$(sv 'textblock\.size')"
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == "move") mv[$2] = 1; next }
        FILENAME == ARGV[2] { left[$1] = $2; next }
        { tbf[$1] = $2 }
        END { for (b in mv) {
                 if (b in left) print "    " left[b] " newline literal(s) still written a line at a time: " b
                 else if (!(b in tbf)) print "    the literals have gone but no text block was written in this method: " b } }' \
      /tests/baseline/strnl.txt "${WORK}/cnt-tbleft.txt" "${WORK}/cnt-tbin.txt" | LC_ALL=C sort | head -4
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 == "hold") w[$2] = $3; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    an XML fragment builder lost " (w[b] - g) " of its " w[b] " newline literal(s): " b } }' \
      /tests/baseline/strnl.txt "${WORK}/cnt-tbleft.txt" | LC_ALL=C sort | head -4
   b16_3move=$(clamp01 "$(frac "${tb_move_n}" "${tb_move_w}")")
   b16_3hold=$(clamp01 "$(frac "${tb_hold_n}" "${tb_hold_w}")")
else
   echo "    B-16.3: no compile of this tree produced a report -> B-16.3 = 0"
fi
fact "gate.b16.3.movefrac=${b16_3move}"
fact "gate.b16.3.holdfrac=${b16_3hold}"

# A proviso that cannot be checked is NOT satisfied -- B-10's rule, kept here:
# no summary means family A did not run, and a source shape on its own is not
# evidence that the document still reads the way it read.
b16_3exec=0.000000
b16_3execn=0
b16_3execbad=0
if [ -r /tests/baseline/tbids.txt ]; then
   b16_3execn=$(grep -c '^[a-z]' /tests/baseline/tbids.txt 2>/dev/null || true)
   case "${b16_3execn}" in ''|*[!0-9]*) b16_3execn=0 ;; esac
fi
if [ "${b16_3execn}" -eq 0 ]; then
   echo "    B-16.3: baseline/tbids.txt names no recorded document behaviour -- the scaling cannot be read"
elif [ -r "${summary}" ]; then
   b16_3execbad=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                        /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                        END { print n + 0 }' /tests/baseline/tbids.txt "${summary}")
   b16_3exec=$(clamp01 "$(frac "$(( b16_3execn - b16_3execbad ))" "${b16_3execn}")")
else
   b16_3execbad="${b16_3execn}"
fi
fact "gate.b16.3.exec.want=${b16_3execn}"
fact "gate.b16.3.exec.broken=${b16_3execbad}"
fact "gate.b16.3.exec=${b16_3exec}"
[ "${b16_3execbad}" -eq 0 ] || \
   printf '    %s of %s recorded document behaviour(s) no longer match -- B-16.3 scaled by %s\n' \
      "${b16_3execbad}" "${b16_3execn}" "${b16_3exec}"
b16_3=$(awk -v m="${b16_3move}" -v h="${b16_3hold}" -v e="${b16_3exec}" \
        'BEGIN { v = (m < h) ? m : h; printf "%.6f", v * e }')
fact "B16.3.text-blocks=${b16_3}"

# ---------------------------------------------------------------- B-16.4
# The event hierarchies close; the extension seams stay open.
#
# baseline/sealed.txt carries three kinds of line. `root <file> <simple-name>
# <class|interface> <permits;...>` for the 11 hierarchies that have to become
# sealed and the exact set of subtypes each one has to permit. `open <file>
# <simple-name>` for the 11 sealable roots that have to stay open. `leaf
# <simple-name>` for the 29 permitted subtypes, recorded and scored by nothing
# -- see below.
#
# THE TWO LISTS ARE A CHECKED PARTITION AND NOT A PAIR OF OPINIONS.
# mk-baseline-step09.sh derived, from a javap dump of that tree's own class
# files, every type with two or more direct subtypes all of which are in the
# root's own package -- the 22 types the unnamed module permits sealing at all
# -- and refuses to write anything unless `root` and `open` together are
# exactly those 22 and every root's permits list equals its actual direct-
# subtype set in both directions. A hierarchy that appears later has to be
# classified rather than silently ignored. NOTES 14.4 records the two awk
# defects that check caught while the lists were being written. The file is
# staged here byte-for-byte out of step 09, held and not extended; its two
# re-measurable denominators, roots and permits, are re-read off THIS tree into
# target.b16.sealed.roots and .permits, and the partition's own 40/11 pair
# cannot be, because eleven of the forty are required to carry no record.
#
# WHY THE HOLD HALF IS HALF THE LEG. `sealed` is the one construct on step 09's rung
# with a wrong answer that nothing else in this verifier can see. Family A
# cannot: nothing in this tree subclasses ResourceProvider, Typed,
# AbstractContext, the descriptor implementation bases or the tests' own
# service doubles from outside, so sealing those nine breaks no fixture and no
# test. B-6 cannot: `sealed`, `non-sealed` and `permits` are attributes rather
# than access flags and javap does not print them. The suite cannot. A
# candidate told "seal the hierarchies" who seals everything sealable has
# closed this library's extension points -- the @ArquillianResource provider
# interface among them -- and every instrument in this file except this one
# would report a clean tree. min(11 closed, 11 open), so that tree scores 0.
#
# TWO OF THE ELEVEN ARE HELD OPEN BY THE COMPILER RATHER THAN BY THIS GATE, and
# they are on the list for the same reason the other nine are: a candidate who
# seals them has made the tree worse. ExecutionEvent and LifecycleEvent are
# event roots by shape, and both carry the @FunctionalInterface step 04 added,
# whose 24-name population B-11 still holds. A sealed interface is not
# functional, so javac rejects the combination outright and the whole tree
# stops compiling; deleting the annotation to make it compile costs two of
# B-11's twenty-four. This gate scores them anyway, because a leg whose hold
# half is enforced somewhere else is a leg that stops being checked the day
# that somewhere else changes.
#
# NO LEAF GATE, AND THAT IS THE COMPILER'S DOING RATHER THAN AN OMISSION. javac
# requires every type named in a permits clause to be final, sealed or
# non-sealed, and requires the permits clause to name exactly the direct
# subtypes it can see. A tree that sealed a root and left one subtype
# unmarked, or that named a subtype it does not have, does not compile -- so
# `measured` would be the default-level report or nothing at all, and this leg
# reads 0 for a reason it would print. The 29 leaves are staged anyway because
# B-6's normalisation needs their names, and because the next rung's author
# should not have to re-derive them.
#
# THE PERMITS LISTS ARE COMPARED AS SETS AFTER STRIPPING PACKAGE QUALIFIERS.
# The plugin records what was written -- `AfterClass` or
# `org.jboss.arquillian.spi.event.suite.AfterClass`, both legal, both the same
# promise -- so leading segments that begin with a lower-case letter are
# dropped from each name and the list is sorted before the comparison. Order
# in a permits clause carries no meaning and this gate does not invent one.
b16_4=0
b16_4roots=0
b16_4open=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/sealed.txt ]; then
   sed -n 's/^sealed\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ split($1, a, ":"); f = a[1]; nm = ""; kd = ""; pn = ""
               for (i = 2; i <= NF; i++) {
                  if (substr($i, 1, 5) == "name=")       nm = substr($i, 6)
                  else if (substr($i, 1, 5) == "kind=")  kd = substr($i, 6)
                  else if (substr($i, 1, 6) == "names=") pn = substr($i, 7)
               }
               if (kd != "sealed" || nm == "") next
               k = split(pn, P, ";")
               for (j = 1; j <= k; j++) {
                  s = P[j]
                  while (match(s, /^[a-z][A-Za-z0-9_]*\./)) s = substr(s, RLENGTH + 1)
                  P[j] = s
               }
               for (x = 1; x < k; x++) for (y = x + 1; y <= k; y++)
                  if (P[y] < P[x]) { t = P[x]; P[x] = P[y]; P[y] = t }
               out = ""
               for (j = 1; j <= k; j++) out = (out == "") ? P[j] : out ";" P[j]
               print f "|" nm "|" out }' \
      | LC_ALL=C sort -u > "${WORK}/sealed-got.txt"
   cut -d'|' -f1,2 "${WORK}/sealed-got.txt" | LC_ALL=C sort -u > "${WORK}/sealed-gotkeys.txt"

   awk '$0 !~ /^#/ && $1 == "root" {
           k = split($5, P, ";")
           for (x = 1; x < k; x++) for (y = x + 1; y <= k; y++)
              if (P[y] < P[x]) { t = P[x]; P[x] = P[y]; P[y] = t }
           out = ""
           for (j = 1; j <= k; j++) out = (out == "") ? P[j] : out ";" P[j]
           print $2 "|" $3 "|" out }' /tests/baseline/sealed.txt \
      | LC_ALL=C sort -u > "${WORK}/sealed-want.txt"
   awk '$0 !~ /^#/ && $1 == "open" { print $2 "|" $3 }' /tests/baseline/sealed.txt \
      | LC_ALL=C sort -u > "${WORK}/sealed-open.txt"

   tgt_b16_sealed_roots=$(tv 'target\.b16\.sealed\.roots')
   tgt_b16_sealed_open=$(tv 'target\.b16\.sealed\.open')
   b16_4roots=$(LC_ALL=C comm -12 "${WORK}/sealed-want.txt" "${WORK}/sealed-got.txt" | grep -c . || true)
   b16_4open=$(LC_ALL=C comm -23 "${WORK}/sealed-open.txt" "${WORK}/sealed-gotkeys.txt" | grep -c . || true)
   fact "gate.b16.4.decl=$(sv 'sealed\.decl')"
   fact "gate.b16.4.nonsealed=$(sv 'sealed\.nonsealed')"
   fact "gate.b16.4.permits=$(sv 'sealed\.permits')"
   fact "gate.b16.4.roots=${b16_4roots}"
   fact "gate.b16.4.roots.want=${tgt_b16_sealed_roots:-0}"
   fact "gate.b16.4.open=${b16_4open}"
   fact "gate.b16.4.open.want=${tgt_b16_sealed_open:-0}"
   LC_ALL=C comm -23 "${WORK}/sealed-want.txt" "${WORK}/sealed-got.txt" | head -6 \
      | sed 's/^/    not sealed, or not permitting exactly these: /'
   LC_ALL=C comm -12 "${WORK}/sealed-open.txt" "${WORK}/sealed-gotkeys.txt" | head -6 \
      | sed 's/^/    a type this tree is extended through has been closed: /'
   b16_4a=$(clamp01 "$(frac "${b16_4roots}" "${tgt_b16_sealed_roots:-0}")")
   b16_4b=$(clamp01 "$(frac "${b16_4open}"  "${tgt_b16_sealed_open:-0}")")
   fact "gate.b16.4.closedfrac=${b16_4a}"
   fact "gate.b16.4.openfrac=${b16_4b}"
   b16_4=$(minf "${b16_4a}" "${b16_4b}")
else
   echo "    B-16.4: no compile of this tree produced a report -> B-16.4 = 0"
fi
fact "B16.4.sealed-hierarchies=${b16_4}"

# ---------------------------------------------------------------- B-16.5
# The two colon switches become arrow switches.
#
# baseline/swarrow.txt names the two files and how many switches each holds.
# A `switch` record carries no `in=` field -- a switch is a statement and the
# plugin's header says why the bucket key does not apply -- so the population
# is stated over files, which is also how a reader would state it: one switch
# in DeployableTestBuilder, one in DynamicServiceLoaderTestCase.
#
# BOTH DIRECTIONS, per file: at least as many arrow-form switches as the
# baseline counted, AND no colon-form switch left in that file. The second half
# is what makes it a conversion rather than an addition -- a candidate who
# leaves the old switch and writes a new one beside it scores nothing -- and it
# is cheap to satisfy honestly because a file holding one switch has one
# answer.
#
# `switch.expr` IS EMITTED AND SCORED BY NOTHING. An arrow switch and a switch
# EXPRESSION are two different changes and only the first is asked for here:
# both of these switches assign to a local and break, and whether they read
# better as an expression is a judgement about these two methods rather than a
# property of Java 17. A gate that demanded it would be a gate about taste.
b16_5=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/swarrow.txt ]; then
   fcount switch swarrow form=arrow
   fcount switch swcolon form=colon
   tgt_b16_sw_files=$(tv 'target\.b16\.sw\.files')
   b16_5hit=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 != "") want[$1] = $2; next }
                FILENAME == ARGV[2] { arrow[$1] = $2; next }
                { colon[$1] = $2 }
                END { n = 0
                      for (f in want) { a = (f in arrow) ? arrow[f] : 0
                                        c = (f in colon) ? colon[f] : 0
                                        if (a >= want[f] && c == 0) n++ }
                      print n + 0 }' \
             /tests/baseline/swarrow.txt "${WORK}/fil-swarrow.txt" "${WORK}/fil-swcolon.txt")
   fact "gate.b16.5.files=${b16_5hit}"
   fact "gate.b16.5.files.want=${tgt_b16_sw_files:-0}"
   fact "gate.b16.5.arrow=$(sv 'switch\.arrow')"
   fact "gate.b16.5.colon=$(sv 'switch\.colon')"
   fact "gate.b16.5.expr=$(sv 'switch\.expr')"
   fact "gate.b16.5.size=$(sv 'switch\.size')"
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $1 != "") want[$1] = $2; next }
        FILENAME == ARGV[2] { arrow[$1] = $2; next }
        { colon[$1] = $2 }
        END { for (f in want) { a = (f in arrow) ? arrow[f] : 0
                                c = (f in colon) ? colon[f] : 0
                                if (c > 0) print "    " c " colon-form switch(es) remain: " f
                                else if (a < want[f]) print "    " a " of " want[f] " switch(es) are arrow form: " f } }' \
      /tests/baseline/swarrow.txt "${WORK}/fil-swarrow.txt" "${WORK}/fil-swcolon.txt" \
      | LC_ALL=C sort | head -4
   b16_5=$(clamp01 "$(frac "${b16_5hit}" "${tgt_b16_sw_files:-0}")")
else
   echo "    B-16.5: no compile of this tree produced a report -> B-16.5 = 0"
fi
fact "B16.5.arrow-switch=${b16_5}"

# Step 09's four surviving legs at step 09's own weights, renormalised over the
# 0.90 that is left when its C-1 is dropped. 0.20 + 0.26 + 0.36 + 0.08 = 0.90,
# so dividing by 0.90 restores the proportions the four had to each other
# without inventing a fifth.
#
# On one line, for the reason every weight expression in this file is on one
# line: the Dockerfile greps the printf.
b16=$(awk -v a="${b16_2}" -v b="${b16_3}" -v c="${b16_4}" -v d="${b16_5}" 'BEGIN {
   printf "%.6f", (0.20*a + 0.26*b + 0.36*c + 0.08*d) / 0.90
}')
fact "B16.java17-constructs=${b16}"

# ============================================================== B-17
#
# B-17 IS STEP 10'S FAMILY C, MINUS ITS C-1 -- the subtraction B-16 made of
# step 09 one rung ago, and B-13, B-14 and B-15 before it. Step 10 scored four
# legs: C-1, every class file the build emits is major 65; C-2, a reflective
# type test with an `instanceof` form becomes a type pattern and the fourteen
# without one stay reflective; C-3, four if-chains of type tests over one
# selector become pattern switches and two dispatches on a `Class` OBJECT do
# not; C-4, seven guarded first-element reads become `getFirst()` and nine
# reads that are not first-element reads stay as they are.
#
# C-1 GOES BECAUSE B-4 NOW ASKS IT. Step 10's level moved 61 -> 65, and two
# rungs on HOLD_MAJOR reads 66 out of baseline/level.txt -- step 11's rung --
# so B-4 asks "one major, and not below 66" over the same scan step 10's C-1
# read, which is the same floor one major stronger. A second copy
# here would pay a tree twice for one constant. The ordinary subtraction, the
# one step 08 alone could not make.
#
# THE OTHER THREE COME FORWARD WHOLE, WITH STEP 10'S OWN POPULATION FILES
# STAGED BYTE-FOR-BYTE -- baseline/reftest.txt, chain.txt, seqfirst.txt and
# swids.txt -- AND THAT IS A MEASUREMENT AND NOT A CONVENIENCE. Twice an
# inherited population has had to be re-cut because the rung inheriting it moved
# one of its own sites: step 06's accessible.txt, when step 11's C-4 took four
# `setAccessible` calls away from it, and step 09's ipattern.txt, when step 11's
# C-2 deleted seven of its bound-pattern buckets. So the question was asked a
# third time at step 11, where mk-baseline-step11.sh read all three of step 10's
# files against that rung's golden and found every one of B-17's nine halves
# reading full: 14 hold sites and 0 reflective tests left in the 7 conv buckets,
# 10 pattern-switch arms and 0 chain types still spelled one at a time, 9 hold
# sites and 7 sequenced reads. THIS rung asks it a fourth time and gets the
# whole-verifier answer, which is stronger than any one file's: step 11's
# verifier run unchanged over this rung's golden reads 1.000000 on every one of
# its seventeen gates, B-17 among them. NOTES 16.61.
#
# THE THREE KEEP STEP 10'S OWN WEIGHTS, renormalised over the 0.96 they carried
# between them once C-1's 0.04 is gone: 0.17 + 0.65 + 0.14 = 0.96. Their
# proportions to each other are what step 10's calibration measured -- NOTES
# 15.14 -- and the composite is a fraction of one gate rather than of a family.
#
# THE DENOMINATORS ARE COUNTED OUT OF THE STAGED FILES AND NOT OUT OF
# target.txt, which is capwant()'s rule and for capwant()'s reason: two copies
# of one figure are two chances to disagree. baseline/target.txt carries the
# same thirteen under `target.b17.*` and the Dockerfile asserts the two match.
# The four keys this file DOES read from target.txt are the four the population
# files cannot state -- `rt.scanned`, `sq.scanned` and `sq.new`, the three
# figures THIS RUNG'S SEED reads for the three records the populations are cut
# from; and `ch.ids`, which counts recordings rather than sites -- and
# every one of the four is published and scored by nothing.
echo
echo "--- B-17: step 10's Java 21 constructs, held"

# The sub-terms the report prints, initialised before the guards that compute
# them, because `set -u` is on and a leg whose compile never happened still has
# to print a row.
b17_2dep=0; b17_2arr=0; b17_2hold=0
b17_3dep=0; b17_3arr=0; b17_3hold=0
b17_4dep=0; b17_4arr=0; b17_4hold=0
# ---------------------------------------------------------------- B-17.2
# A reflective type test becomes a type pattern -- where it has that form.
#
# baseline/reftest.txt carries one row per site, `<bucket> conv|hold
# call=<name> type=<T>`: 13 `isInstance`/`isAssignableFrom` calls in 7 methods
# whose argument is a value and whose type is a class literal, and 14 that are
# neither. The 14 are the reason this leg is not a count of what is left:
# ServiceLoader and DynamicServiceLoader ask assignability between two `Class`
# OBJECTS, and a pattern binds a value.
#
# THREE TERMS, ALL min()ed.
#
#   the 13 depart   per conv bucket, max(0, sites frozen - `reftest` records
#                   still there), summed and over 13. Clamped per bucket, so a
#                   method that GAINED a reflective test cannot pay for one
#                   that kept its own.
#   the 13 arrive   per conv row, its type named by an `ipattern` or a `swpat`
#                   record IN THAT SAME BUCKET, compared with package
#                   qualifiers and type arguments cut from both sides. The two
#                   record kinds are unioned because step 10's C-3 turns four
#                   of these buckets into pattern switches, and a type bound by
#                   a switch arm is bound.
#   the 14 stay     per hold bucket, min(records still there, records frozen),
#                   over 14. Without it a tree that converted all 27 -- or
#                   deleted the 14 -- scores the leg whole.
b17_2=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/reftest.txt ]; then
   cbuckets reftest rtleft
   cnames ipattern iptype type
   cnames swpat swtype types
   LC_ALL=C sort -u "${WORK}/nam-iptype.txt" "${WORK}/nam-swtype.txt" > "${WORK}/nam-rtarr.txt"

   rt_dep_w=$(awk '$0 !~ /^#/ && $2 == "conv" { n++ } END { print n + 0 }' /tests/baseline/reftest.txt)
   rt_hold_w=$(awk '$0 !~ /^#/ && $2 == "hold" { n++ } END { print n + 0 }' /tests/baseline/reftest.txt)
   rt_dep_n=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "conv") w[$1]++; next }
                   { h[$1] = $2 }
                   END { s = 0
                         for (b in w) { g = (b in h) ? h[b] : 0
                                        d = w[b] - g
                                        s += (d > 0) ? d : 0 }
                         print s + 0 }' \
                /tests/baseline/reftest.txt "${WORK}/cnt-rtleft.txt")
   rt_hold_n=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "hold") w[$1]++; next }
                    { h[$1] = $2 }
                    END { s = 0
                          for (b in w) { g = (b in h) ? h[b] : 0
                                         s += (g < w[b]) ? g : w[b] }
                          print s + 0 }' \
                 /tests/baseline/reftest.txt "${WORK}/cnt-rtleft.txt")
   rt_arr_w=$(awk '$0 !~ /^#/ && $2 == "conv" {
                      t = ""
                      for (i = 3; i <= NF; i++) if (substr($i, 1, 5) == "type=") t = substr($i, 6)
                      while (match(t, /^[a-z][A-Za-z0-9_]*\./)) t = substr(t, RLENGTH + 1)
                      if (t != "" && t != "-") k[$1 "|" t] = 1 }
                   END { n = 0; for (x in k) n++; print n + 0 }' /tests/baseline/reftest.txt)
   rt_arr_n=$(awk 'FILENAME == ARGV[1] { have[$1 "|" $2] = 1; next }
                   $0 !~ /^#/ && $2 == "conv" {
                      t = ""
                      for (i = 3; i <= NF; i++) if (substr($i, 1, 5) == "type=") t = substr($i, 6)
                      while (match(t, /^[a-z][A-Za-z0-9_]*\./)) t = substr(t, RLENGTH + 1)
                      if (t != "" && t != "-") k[$1 "|" t] = 1 }
                   END { n = 0; for (x in k) if (x in have) n++; print n + 0 }' \
                "${WORK}/nam-rtarr.txt" /tests/baseline/reftest.txt)
   fact "gate.b17.2.departed=${rt_dep_n}"
   fact "gate.b17.2.departed.want=${rt_dep_w}"
   fact "gate.b17.2.arrived=${rt_arr_n}"
   fact "gate.b17.2.arrived.want=${rt_arr_w}"
   fact "gate.b17.2.held=${rt_hold_n}"
   fact "gate.b17.2.held.want=${rt_hold_w}"
   fact "gate.b17.2.reftests=$(sv 'reftest\.size')"
   fact "gate.b17.2.reftests.was=$(tv 'target\.b17\.rt\.scanned')"
   fact "gate.b17.2.patterns=$(sv 'ipattern\.size')"
   fact "gate.b17.2.swpatterns=$(sv 'swpat\.patterns')"
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "conv") w[$1]++; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g > 0) print "    " g " of " w[b] " reflective type test(s) came back: " b } }' \
      /tests/baseline/reftest.txt "${WORK}/cnt-rtleft.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] { have[$1 "|" $2] = 1; next }
        $0 !~ /^#/ && $2 == "conv" {
           t = ""
           for (i = 3; i <= NF; i++) if (substr($i, 1, 5) == "type=") t = substr($i, 6)
           while (match(t, /^[a-z][A-Za-z0-9_]*\./)) t = substr(t, RLENGTH + 1)
           if (t != "" && t != "-" && !(($1 "|" t) in have))
              print "    nothing in this method tests for " t " as a type any more: " $1 }' \
      "${WORK}/nam-rtarr.txt" /tests/baseline/reftest.txt | LC_ALL=C sort -u | head -5
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "hold") w[$1]++; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    a reflective test with no instanceof form was changed anyway (" g " of " w[b] " left): " b } }' \
      /tests/baseline/reftest.txt "${WORK}/cnt-rtleft.txt" | LC_ALL=C sort | head -5
   b17_2dep=$(clamp01 "$(frac "${rt_dep_n}" "${rt_dep_w}")")
   b17_2arr=$(clamp01 "$(frac "${rt_arr_n}" "${rt_arr_w}")")
   b17_2hold=$(clamp01 "$(frac "${rt_hold_n}" "${rt_hold_w}")")
   fact "gate.b17.2.depfrac=${b17_2dep}"
   fact "gate.b17.2.arrfrac=${b17_2arr}"
   fact "gate.b17.2.holdfrac=${b17_2hold}"
   b17_2=$(minf "$(minf "${b17_2dep}" "${b17_2arr}")" "${b17_2hold}")
else
   echo "    B-17.2: no compile of this tree produced a report -> B-17.2 = 0"
fi
fact "B17.2.type-patterns=${b17_2}"

# ---------------------------------------------------------------- B-17.3
# Four chains of type tests are pattern switches, and two are not.
#
# baseline/chain.txt carries one row per chain, `<bucket> conv|hold arms=<n>
# sel=<expr> types=<T;T>`: four chains covering 10 arms that became switches,
# and two of two arms each that must not, because their selector is a `Class`
# object and their arms ask about assignability between two types.
#
# THREE TERMS, ALL min()ed, AND WEIGHTED BY ARMS RATHER THAN BY CHAINS, because
# a three-arm chain is more work than a two-arm one.
#
#   the 10 depart   per conv chain, max(0, arms - the OLD SPELLING still in
#                   that bucket), over 10, where the old spelling is counted
#                   two ways and added: a `reftest` record still in the bucket,
#                   and a chain type still named by an `ipattern` record in the
#                   same bucket. Both, because step 10's review found that
#                   counting only `reftest` scores a tree that turned each arm
#                   into `if (v instanceof X x)` and left the if-chain standing
#                   beside the switch -- a rewrite OpenRewrite performs
#                   unattended on eight of these ten arms.
#   the 10 arrive   per conv chain, both of: a `swpat` record in that method
#                   whose `patterns=` is at least the chain's arm count, AND
#                   every type the chain tested named by an arm of that switch.
#                   Over the same 10.
#   the 2 stay      per hold chain, an ABSENCE: no pattern switch in that
#                   method names the types that chain tested.
#
# AND SCALED BY THE FIVE RECORDINGS IN baseline/swids.txt, because a pattern
# switch is the one construct on that rung that changes behaviour while
# compiling perfectly: `if (X.class.isInstance(v))` is false for null and falls
# through, `switch (v)` over type patterns throws NullPointerException before
# any arm is considered unless `case null` is written. Three nulls and two
# unmatched types, reaching all four converted chains.
#
# THE FIVE WERE RE-RECORDED FOR THIS ERA AT STEP 11 AND STILL READ, which is
# worth a line because 37 of the 939 transcripts moved between `rec/jdk25` and
# `rec/jdk25-step11`, and mk-baseline-step11.sh checked all five ids against that
# era's id list when it staged them. THIS rung stages the same era, byte for byte
# and with no transcript added or removed (see the header), so the five ids named
# here are the five that were checked and nothing re-checks them. A `differ` is a
# candidate's regression and not a bookkeeping one.
b17_3=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/chain.txt ]; then
   # cbuckets and cnames AGAIN, and the repetition is the point: this guard is
   # on baseline/chain.txt and B-17.2's is on baseline/reftest.txt, so a missing
   # set file costs ONE leg and not two. Both helpers are idempotent and cheap.
   cbuckets reftest rtleft
   cnames ipattern iptype type
   cnames swpat swtype types
   sed -n 's/^swpat\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ m = ""; p = 0
               for (i = 2; i <= NF; i++) {
                  if (substr($i, 1, 3) == "in=") m = substr($i, 4)
                  else if (substr($i, 1, 9) == "patterns=") p = substr($i, 10) + 0
               }
               if (m == "") next
               split($1, a, ":"); b = a[1] "#" m
               if (p > n[b]) n[b] = p }
             END { for (k in n) print k, n[k] }' \
      | LC_ALL=C sort > "${WORK}/swpat-pat.txt"

   # One denominator for two halves -- the 10 arms of the four conv chains --
   # so the departure and the arrival cannot come to disagree about how much of
   # this leg there is.
   ch_conv_w=$(awk '$0 !~ /^#/ && $2 == "conv" {
                       for (i = 3; i <= NF; i++)
                          if (substr($i, 1, 5) == "arms=") tot += substr($i, 6) + 0 }
                    END { print tot + 0 }' /tests/baseline/chain.txt)
   ch_hold_w=$(awk '$0 !~ /^#/ && $2 == "hold" { n++ } END { print n + 0 }' /tests/baseline/chain.txt)
   ch_dep_n=$(awk 'FILENAME == ARGV[1] { h[$1] = $2; next }
                   FILENAME == ARGV[2] { nm[$1 "|" $2] = 1; next }
                   $0 !~ /^#/ && $2 == "conv" {
                      arms = 0; ty = ""
                      for (i = 3; i <= NF; i++) {
                         if (substr($i, 1, 5) == "arms=")       arms = substr($i, 6) + 0
                         else if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
                      }
                      g = ($1 in h) ? h[$1] : 0
                      k = split(ty, T, ";")
                      for (j = 1; j <= k; j++) {
                         nm1 = T[j]
                         sub(/<.*$/, "", nm1)
                         while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
                         if (($1 "|" nm1) in nm) g++
                      }
                      d = arms - g
                      s += (d > 0) ? d : 0 }
                   END { print s + 0 }' \
                "${WORK}/cnt-rtleft.txt" "${WORK}/nam-iptype.txt" /tests/baseline/chain.txt)
   # The `instanceof` half of that residual on its own, published and not
   # scored, so a reader of a failed run can tell "the reflective call came
   # back" from "an instanceof chain was written beside the switch".
   ch_ipl_n=$(awk 'FILENAME == ARGV[1] { nm[$1 "|" $2] = 1; next }
                   $0 !~ /^#/ && $2 == "conv" {
                      ty = ""
                      for (i = 3; i <= NF; i++)
                         if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
                      k = split(ty, T, ";")
                      for (j = 1; j <= k; j++) {
                         nm1 = T[j]
                         sub(/<.*$/, "", nm1)
                         while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
                         if (($1 "|" nm1) in nm) n++
                      } }
                   END { print n + 0 }' \
                "${WORK}/nam-iptype.txt" /tests/baseline/chain.txt)
   ch_arr_n=$(awk 'FILENAME == ARGV[1] { pat[$1] = $2; next }
                   FILENAME == ARGV[2] { nm[$1 "|" $2] = 1; next }
                   $0 !~ /^#/ && $2 == "conv" {
                      arms = 0; ty = ""
                      for (i = 3; i <= NF; i++) {
                         if (substr($i, 1, 5) == "arms=")       arms = substr($i, 6) + 0
                         else if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
                      }
                      ok = ((($1 in pat) ? pat[$1] : 0) >= arms) ? 1 : 0
                      k = split(ty, T, ";")
                      for (j = 1; j <= k; j++) {
                         nm1 = T[j]
                         sub(/<.*$/, "", nm1)
                         while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
                         if (!(($1 "|" nm1) in nm)) ok = 0
                      }
                      if (ok) tot += arms }
                   END { print tot + 0 }' \
                "${WORK}/swpat-pat.txt" "${WORK}/nam-swtype.txt" /tests/baseline/chain.txt)
   ch_hold_n=$(awk 'FILENAME == ARGV[1] { nm[$1 "|" $2] = 1; next }
                    $0 !~ /^#/ && $2 == "hold" {
                       ty = ""
                       for (i = 3; i <= NF; i++) if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
                       k = split(ty, T, ";"); bad = 0
                       for (j = 1; j <= k; j++) {
                          nm1 = T[j]
                          sub(/<.*$/, "", nm1)
                          while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
                          if (($1 "|" nm1) in nm) bad = 1
                       }
                       if (!bad) n++ }
                    END { print n + 0 }' \
                 "${WORK}/nam-swtype.txt" /tests/baseline/chain.txt)
   fact "gate.b17.3.departed=${ch_dep_n}"
   fact "gate.b17.3.departed.want=${ch_conv_w}"
   fact "gate.b17.3.ipleft=${ch_ipl_n}"
   fact "gate.b17.3.arrived=${ch_arr_n}"
   fact "gate.b17.3.arrived.want=${ch_conv_w}"
   fact "gate.b17.3.held=${ch_hold_n}"
   fact "gate.b17.3.held.want=${ch_hold_w}"
   fact "gate.b17.3.switches=$(sv 'swpat\.size')"
   fact "gate.b17.3.patterns=$(sv 'swpat\.patterns')"
   fact "gate.b17.3.nullcase=$(sv 'swpat\.nullcase')"
   awk 'FILENAME == ARGV[1] { h[$1] = $2; next }
        $0 !~ /^#/ && $2 == "conv" {
           arms = 0
           for (i = 3; i <= NF; i++)
              if (substr($i, 1, 5) == "arms=") arms = substr($i, 6) + 0
           g = ($1 in h) ? h[$1] : 0
           if (g > 0)
              print "    " g " of " arms " reflective type test(s) came back beside the switch: " $1 }' \
      "${WORK}/cnt-rtleft.txt" /tests/baseline/chain.txt | LC_ALL=C sort -u | head -5
   awk 'FILENAME == ARGV[1] { nm[$1 "|" $2] = 1; next }
        $0 !~ /^#/ && $2 == "conv" {
           ty = ""
           for (i = 3; i <= NF; i++) if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
           k = split(ty, T, ";")
           for (j = 1; j <= k; j++) {
              nm1 = T[j]
              sub(/<.*$/, "", nm1)
              while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
              if (($1 "|" nm1) in nm)
                 print "    this chain tests " nm1 " one arm at a time again, as an instanceof beside the switch: " $1
           } }' \
      "${WORK}/nam-iptype.txt" /tests/baseline/chain.txt | LC_ALL=C sort -u | head -5
   awk 'FILENAME == ARGV[1] { pat[$1] = $2; next }
        FILENAME == ARGV[2] { nm[$1 "|" $2] = 1; next }
        $0 !~ /^#/ && $2 == "conv" {
           arms = 0; ty = ""
           for (i = 3; i <= NF; i++) {
              if (substr($i, 1, 5) == "arms=")       arms = substr($i, 6) + 0
              else if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
           }
           have = ($1 in pat) ? pat[$1] : 0
           if (have < arms) { print "    " have " pattern arm(s) where the chain has " arms ": " $1; next }
           k = split(ty, T, ";")
           for (j = 1; j <= k; j++) {
              nm1 = T[j]
              sub(/<.*$/, "", nm1)
              while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
              if (!(($1 "|" nm1) in nm))
                 print "    the switch in this method has no arm for " nm1 " any more: " $1
           } }' \
      "${WORK}/swpat-pat.txt" "${WORK}/nam-swtype.txt" /tests/baseline/chain.txt \
      | LC_ALL=C sort -u | head -5
   awk 'FILENAME == ARGV[1] { nm[$1 "|" $2] = 1; next }
        $0 !~ /^#/ && $2 == "hold" {
           ty = ""
           for (i = 3; i <= NF; i++) if (substr($i, 1, 6) == "types=") ty = substr($i, 7)
           k = split(ty, T, ";")
           for (j = 1; j <= k; j++) {
              nm1 = T[j]
              sub(/<.*$/, "", nm1)
              while (match(nm1, /^[a-z][A-Za-z0-9_]*\./)) nm1 = substr(nm1, RLENGTH + 1)
              if (($1 "|" nm1) in nm)
                 print "    a dispatch on a Class object has been made a pattern switch: " $1
           } }' \
      "${WORK}/nam-swtype.txt" /tests/baseline/chain.txt | LC_ALL=C sort -u | head -4
   b17_3dep=$(clamp01 "$(frac "${ch_dep_n}" "${ch_conv_w}")")
   b17_3arr=$(clamp01 "$(frac "${ch_arr_n}" "${ch_conv_w}")")
   b17_3hold=$(clamp01 "$(frac "${ch_hold_n}" "${ch_hold_w}")")
else
   echo "    B-17.3: no compile of this tree produced a report -> B-17.3 = 0"
fi
fact "gate.b17.3.depfrac=${b17_3dep}"
fact "gate.b17.3.arrfrac=${b17_3arr}"
fact "gate.b17.3.holdfrac=${b17_3hold}"

# The behaviour scaling. Read from baseline/swids.txt, cross-checked against
# target.b17.ch.ids so that a mis-staged set file is caught here rather than
# read as five recordings that happen to pass.
b17_3exec=0.000000
b17_3execn=0
b17_3execbad=0
if [ -r /tests/baseline/swids.txt ]; then
   b17_3execn=$(grep -c '^[a-z]' /tests/baseline/swids.txt 2>/dev/null || true)
   case "${b17_3execn}" in ''|*[!0-9]*) b17_3execn=0 ;; esac
fi
tgt_b17_ch_ids=$(tv 'target\.b17\.ch\.ids')
case "${tgt_b17_ch_ids:-}" in ''|*[!0-9]*) tgt_b17_ch_ids=0 ;; esac
[ "${b17_3execn}" -eq "${tgt_b17_ch_ids}" ] || \
   printf '    baseline/swids.txt names %s recorded dispatch(es), target.txt says %s\n' \
      "${b17_3execn}" "${tgt_b17_ch_ids}"
if [ "${b17_3execn}" -eq 0 ]; then
   echo "    B-17.3: baseline/swids.txt names no recorded dispatch behaviour -- the scaling cannot be read"
elif [ -r "${summary}" ]; then
   b17_3execbad=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                           /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                           END { print n + 0 }' /tests/baseline/swids.txt "${summary}")
   b17_3exec=$(clamp01 "$(frac "$(( b17_3execn - b17_3execbad ))" "${b17_3execn}")")
else
   b17_3execbad="${b17_3execn}"
fi
fact "gate.b17.3.exec.want=${b17_3execn}"
fact "gate.b17.3.exec.broken=${b17_3execbad}"
fact "gate.b17.3.exec=${b17_3exec}"
[ "${b17_3execbad}" -eq 0 ] || \
   printf '    %s of %s recorded dispatch behaviour(s) no longer match -- B-17.3 scaled by %s\n' \
      "${b17_3execbad}" "${b17_3execn}" "${b17_3exec}"
b17_3=$(awk -v d="${b17_3dep}" -v m="${b17_3arr}" -v h="${b17_3hold}" -v e="${b17_3exec}" \
           'BEGIN { v = (d < m) ? d : m; if (h < v) v = h; printf "%.6f", v * e }')
fact "B17.3.pattern-switch=${b17_3}"

# ---------------------------------------------------------------- B-17.4
# A guarded first-element read is `getFirst()`, and nine reads are not.
#
# baseline/seqfirst.txt carries one row per site, `<bucket> conv|hold
# recv=<expr> form=<f>`: 7 reads in 7 methods where an emptiness test guards a
# `.get(0)` or a `.iterator().next()` on a `List`, and 9 that read some other
# index, or read a type with no sequenced view, or have no guard to be the
# first element OF.
#
# THREE TERMS, ALL min()ed.
#
#   the 7 depart    per conv bucket, max(0, sites frozen - `seqfirst` records
#                   still there), over 7.
#   the 7 arrive    per conv bucket, at least one `seqnew` record with
#                   `op=getFirst` in that same method. Bucket-wise and not
#                   site-wise, because the seven conv buckets hold one site
#                   each.
#   the 9 stay      per hold bucket, min(records still there, records frozen),
#                   over 9.
#
# `seqnew.size` and `seqnew.read` are published and scored by nothing.
# target.b17.sq.new is 7, which is what this tree reads on the way IN: step 10
# started from a measured zero and left seven behind, and this rung holds them.
# A `.was` naming step 10's zero would tell a reader of this run that seven
# sequenced reads arrived during step 11. They did not.  NOTES 16.20.
b17_4=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/seqfirst.txt ]; then
   cbuckets seqfirst sqleft
   cbuckets seqnew sqfirst op=getFirst

   sq_dep_w=$(awk '$0 !~ /^#/ && $2 == "conv" { n++ } END { print n + 0 }' /tests/baseline/seqfirst.txt)
   sq_hold_w=$(awk '$0 !~ /^#/ && $2 == "hold" { n++ } END { print n + 0 }' /tests/baseline/seqfirst.txt)
   sq_arr_w=$(awk '$0 !~ /^#/ && $2 == "conv" { b[$1] = 1 }
                   END { n = 0; for (x in b) n++; print n + 0 }' /tests/baseline/seqfirst.txt)
   sq_dep_n=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "conv") w[$1]++; next }
                   { h[$1] = $2 }
                   END { s = 0
                         for (b in w) { g = (b in h) ? h[b] : 0
                                        d = w[b] - g
                                        s += (d > 0) ? d : 0 }
                         print s + 0 }' \
                /tests/baseline/seqfirst.txt "${WORK}/cnt-sqleft.txt")
   sq_hold_n=$(awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "hold") w[$1]++; next }
                    { h[$1] = $2 }
                    END { s = 0
                          for (b in w) { g = (b in h) ? h[b] : 0
                                         s += (g < w[b]) ? g : w[b] }
                          print s + 0 }' \
                 /tests/baseline/seqfirst.txt "${WORK}/cnt-sqleft.txt")
   sq_arr_n=$(awk 'FILENAME == ARGV[1] { have[$1] = $2; next }
                   $0 !~ /^#/ && $2 == "conv" { w[$1] = 1 }
                   END { n = 0
                         for (b in w) if ((b in have) && have[b] + 0 > 0) n++
                         print n + 0 }' \
                "${WORK}/cnt-sqfirst.txt" /tests/baseline/seqfirst.txt)
   fact "gate.b17.4.departed=${sq_dep_n}"
   fact "gate.b17.4.departed.want=${sq_dep_w}"
   fact "gate.b17.4.arrived=${sq_arr_n}"
   fact "gate.b17.4.arrived.want=${sq_arr_w}"
   fact "gate.b17.4.held=${sq_hold_n}"
   fact "gate.b17.4.held.want=${sq_hold_w}"
   fact "gate.b17.4.firstreads=$(sv 'seqfirst\.size')"
   fact "gate.b17.4.firstreads.was=$(tv 'target\.b17\.sq\.scanned')"
   fact "gate.b17.4.sequenced=$(sv 'seqnew\.size')"
   fact "gate.b17.4.sequenced.read=$(sv 'seqnew\.read')"
   fact "gate.b17.4.sequenced.was=$(tv 'target\.b17\.sq\.new')"
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "conv") w[$1]++; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g > 0) print "    " g " of " w[b] " first-element read(s) written the old way again: " b } }' \
      /tests/baseline/seqfirst.txt "${WORK}/cnt-sqleft.txt" | LC_ALL=C sort | head -4
   awk 'FILENAME == ARGV[1] { have[$1] = $2; next }
        $0 !~ /^#/ && $2 == "conv" { w[$1] = 1 }
        END { for (b in w) if (!(b in have) || have[b] + 0 == 0)
                 print "    no sequenced first-element read in this method any more: " b }' \
      "${WORK}/cnt-sqfirst.txt" /tests/baseline/seqfirst.txt | LC_ALL=C sort | head -4
   awk 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $2 == "hold") w[$1]++; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    a read that cannot become getFirst() was changed anyway (" g " of " w[b] " left): " b } }' \
      /tests/baseline/seqfirst.txt "${WORK}/cnt-sqleft.txt" | LC_ALL=C sort | head -4
   b17_4dep=$(clamp01 "$(frac "${sq_dep_n}" "${sq_dep_w}")")
   b17_4arr=$(clamp01 "$(frac "${sq_arr_n}" "${sq_arr_w}")")
   b17_4hold=$(clamp01 "$(frac "${sq_hold_n}" "${sq_hold_w}")")
   fact "gate.b17.4.depfrac=${b17_4dep}"
   fact "gate.b17.4.arrfrac=${b17_4arr}"
   fact "gate.b17.4.holdfrac=${b17_4hold}"
   b17_4=$(minf "$(minf "${b17_4dep}" "${b17_4arr}")" "${b17_4hold}")
else
   echo "    B-17.4: no compile of this tree produced a report -> B-17.4 = 0"
fi
fact "B17.4.sequenced-access=${b17_4}"

# Step 10's three surviving legs at step 10's own weights, renormalised over the
# 0.96 that is left when its C-1 is dropped. 0.17 + 0.65 + 0.14 = 0.96, so
# dividing by 0.96 restores the proportions the three had to each other without
# inventing a fourth.
#
# On one line, for the reason every weight expression in this file is on one
# line: the Dockerfile greps the printf.
b17=$(awk -v a="${b17_2}" -v b="${b17_3}" -v c="${b17_4}" 'BEGIN {
   printf "%.6f", (0.17*a + 0.65*b + 0.14*c) / 0.96
}')
fact "B17.java21-constructs=${b17}"

# ============================================================== B-18
#
# B-18 IS STEP 11'S FAMILY C, MINUS ITS C-1 -- the subtraction B-17 made of step
# 10 one rung ago, and B-16, B-15, B-14 and B-13 before it. Step 11 scored four
# legs: C-1, every class file the build emits is major 66; C-2, every
# `doPrivileged` call and the 27 `PrivilegedAction` bodies behind them come out
# and the reflective construction they were wrapping stays; C-3, the 28 catch
# parameters nothing reads become `_` and the 51 that are read keep their names;
# C-4, four `setAccessible` calls become `privateLookupIn` + `unreflectVarHandle`
# and the ten that must not move stay put.
#
# C-1 GOES BECAUSE B-4 NOW ASKS IT. Step 11's level moved 65 -> 66, so
# HOLD_MAJOR reads 66 out of baseline/level.txt on this rung and B-4 asks "one
# major, and not below 66" over the same scan step 11's C-1 read. A second copy
# here would pay a tree twice for one constant. The ordinary subtraction.
#
# THE OTHER THREE COME FORWARD WHOLE, WITH STEP 11'S OWN POPULATION FILES STAGED
# BYTE-FOR-BYTE -- baseline/priv.txt, catchparam.txt, varhandle.txt, vhids.txt
# and accessible.txt -- AND THAT IS A MEASUREMENT AND NOT A CONVENIENCE. Three
# rungs running, an inherited population has had to be re-cut because the rung
# inheriting it moved one of its own sites: step 06's accessible.txt and step
# 09's ipattern.txt at step 11, step 04's lambdafiles.txt found by calibration
# at the same rung. So the question was asked a fourth time, and this rung is
# the first since 09 where the answer is no.
#
# IT IS ASKED TWICE OVER, BECAUSE THIS RUNG'S EDIT IS UNUSUALLY WIDE AND
# UNUSUALLY SHALLOW. Step 12 deletes 452 import lines and inserts 188, which
# moves nearly every line number in 171 of the tree's 322 sources -- more churn
# by line count than any rung below it -- and touches no population at all,
# because every population here is keyed by `<file>` or `<file>#<method>` and
# neither key is a line number. The one edit that COULD have moved a bucket is
# C-4's ScopedValue conversion, which lives in ManagerImpl#fire, and three
# inherited legs key sites inside that method; it is written not to split it,
# and NOTES 16.61 is where that is recorded. Both halves of the answer were then
# measured rather than argued: NOTES 16.61 ran step 11's ENTIRE verifier against
# this rung's golden and all seventeen inherited legs read 1.000000, and
# mk-baseline-step12.sh re-measures B-18's own four on THIS RUNG'S SEED at main
# scope -- priv 0, catchparam 28 unnamed / 51 read / 0 unread in 61 methods over
# 43 files, varhandle 8 calls in 4 methods over 4 files -- every one of them
# reproducing step 11's `want` column exactly, which is what step 11's blind
# round-2 score of 1.0 predicted.
#
# MAIN SCOPE, AND THE POPULATION FILE IS WHAT ENFORCES IT. The gate runs at ALL
# scope on this rung, because C-2 and C-3 are all-scope legs. Step 11 stated
# these three at main scope, and the restriction travels in the staged file
# rather than in a path filter: pbuckets() intersects an all-scope report
# against baseline/catchparam.txt, whose rows are main-scope only. Not cosmetic
# -- all-scope catchparam reads 89 with 4 unread against main scope's 79 with 0,
# and those four test-scope parameters would turn a leg this rung holds in full
# into one that is four short.
#
# THE THREE KEEP STEP 11'S OWN WEIGHTS, renormalised over the 0.96 they carried
# between them once C-1's 0.04 is gone: 0.46 + 0.26 + 0.24 = 0.96. Their
# proportions to each other are what step 11's calibration measured -- NOTES
# 16.36 -- and the composite is a fraction of one gate rather than of a family.
# The renormalised C-3 term, 0.26/0.96 = 0.270833, is the same figure step 11's
# calibrator printed as `familyC without C-1`, which is the one leg the
# automigrator shelf reached.
#
# THE DENOMINATORS ARE COUNTED OUT OF THE STAGED FILES AND NOT OUT OF
# target.txt, which is capwant()'s rule and for capwant()'s reason: two copies
# of one figure are two chances to disagree. baseline/target.txt carries the
# same figures under `target.b18.*` and the Dockerfile asserts the two match.
# The two keys this block DOES read from target.txt -- `b18.priv.sites` and
# `acc.total`, the seed-side figures the populations are cut from -- are
# published and scored by nothing.
echo
echo "--- B-18: step 11's security-API removal, held"

# The sub-terms the report prints, initialised before the guards that compute
# them, because `set -u` is on and a leg whose compile never happened still has
# to print a row. B-18.2 has no arrival term: `doPrivileged` removal is a
# departure and a hold, and there is no construct it arrives AT.
b18_2=0; b18_3=0; b18_4=0
b18_2dep=0; b18_2hold=0
b18_3dep=0; b18_3arr=0; b18_3hold=0
b18_4dep=0; b18_4arr=0; b18_4hold=0
# ---------------------------------------------------------------- B-18.2
# The removed API goes, and what it wrapped stays.
#
# THE SET IS FROZEN AND IT IS THE PLUGIN'S OWN RECORD. baseline/priv.txt holds
# one row per site for all 67 uses of the removed API in this tree's main
# sources, exactly as arqgate.SourceScan wrote it -- `<file>:<line>
# form=call|typeref name=<n> in=<method>` -- because for this leg the record IS
# the evidence. 20 of the rows are `form=call`, the `doPrivileged` invocations
# themselves; 47 are `form=typeref`, the `AccessController`, `PrivilegedAction`
# and `PrivilegedExceptionAction` names those invocations need. A tree that
# deletes the call and leaves the `implements PrivilegedAction<ClassLoader>` on
# the inner class has moved 20 of 67, and that is the number this leg prints.
#
# THE BUCKET IS DERIVED HERE, ON BOTH SIDES, BY THE SAME THREE LINES. The rows
# carry a line number and the gate never reads it: pbuckets() below turns
# `<file>:<line> ... in=<method>` into `<file>#<method>` out of the frozen file
# and cbuckets() does the same out of the candidate's report. Line numbers move
# when a file is edited, which is what this rung does to every one of these
# files; a method name does not.
#
# TWO TERMS, min()ed, AND THE SECOND ONE IS THE WHOLE REASON THE FIRST IS SAFE.
#
#   the 67 depart   per frozen bucket, how many of its sites are no longer
#                   there, summed and taken over 67. Clamped per bucket at the
#                   frozen count, so a method that lost a site it never had
#                   cannot pay for one that kept the site it did.
#
#   the 13 stay     the payload. 7 `newinstance form=ctor` records -- one in
#                   each of the 7 files priv.txt's own rows name -- and the 6
#                   `setAccessible` calls that accessible.txt's `setacc` half
#                   places in those same files. Over 13.
#
#                   NEITHER DENOMINATOR IS TYPED ANYWHERE. The 7 is the file
#                   list priv.txt implies and the 6 is what falls out of
#                   intersecting accessible.txt with it, so narrowing the
#                   departure population narrows the hold population with it
#                   and the two cannot drift apart. target.txt carries 13, 7
#                   and 6 as well and the Dockerfile asserts they agree.
#
#                   THE CONSTRUCTION HALF IS COUNTED PER FILE AND NOT PER
#                   SITE, which is a deliberate weakening. The 7 constructions
#                   are one per file and the question this half asks is whether
#                   the file still constructs reflectively at all; a file that
#                   grew a second `newInstance` scores the same 1 as before, so
#                   a candidate cannot pay for a deleted construction with an
#                   added one. The `setAccessible` half is stronger -- it is
#                   keyed by method, because accessible.txt froze the methods.
#
#                   6 OF THE 13 ARE INSIDE THE VERY METHODS THIS LEG REWRITES
#                   and 0 of the 7 are. That is not decoration: the scanners'
#                   `setAccessible` calls sit in the same buckets as the
#                   `doPrivileged` wrappers around them, so the hold is the
#                   sharpest available evidence that the wrapper was unwrapped
#                   rather than deleted. The constructions are one level down,
#                   in `newInstance`, and they survive the unwrapping of the
#                   ladder above them -- but not the deletion of the file.
b18_2=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/priv.txt ] \
   && [ -r /tests/baseline/accessible.txt ]; then
   pbuckets /tests/baseline/priv.txt privall
   cbuckets priv privleft
   fcount newinstance ctorleft form=ctor
   cbuckets accessible accleft call=setAccessible
   awk '$0 !~ /^#/ && $0 != "" { split($1, a, ":"); f[a[1]] = 1 }
        END { for (k in f) print k, 1 }' /tests/baseline/priv.txt \
      | LC_ALL=C sort > "${WORK}/set-privfile.txt"
   awk 'FILENAME == ARGV[1] { p[$1] = 1; next }
        $0 !~ /^#/ && $1 == "setacc" { split($2, b, "#"); if (b[1] in p) print $2, 1 }' \
      "${WORK}/set-privfile.txt" /tests/baseline/accessible.txt \
      | LC_ALL=C sort -u > "${WORK}/set-privacc.txt"

   pv_dep_w=$(ptot "${WORK}/set-privall.txt")
   pv_dep_n=$(pdep "${WORK}/set-privall.txt" "${WORK}/cnt-privleft.txt")
   pv_ctor_w=$(ptot "${WORK}/set-privfile.txt")
   pv_ctor_n=$(psum "${WORK}/set-privfile.txt" "${WORK}/fil-ctorleft.txt")
   pv_acc_w=$(ptot "${WORK}/set-privacc.txt")
   pv_acc_n=$(psum "${WORK}/set-privacc.txt" "${WORK}/cnt-accleft.txt")
   fact "gate.b18.2.departed=${pv_dep_n}"
   fact "gate.b18.2.departed.want=${pv_dep_w}"
   fact "gate.b18.2.held=$(( pv_ctor_n + pv_acc_n ))"
   fact "gate.b18.2.held.want=$(( pv_ctor_w + pv_acc_w ))"
   fact "gate.b18.2.held.ctor=${pv_ctor_n}"
   fact "gate.b18.2.held.ctor.want=${pv_ctor_w}"
   fact "gate.b18.2.held.setacc=${pv_acc_n}"
   fact "gate.b18.2.held.setacc.want=${pv_acc_w}"
   fact "gate.b18.2.sites=$(sv 'priv\.size')"
   fact "gate.b18.2.sites.was=$(tv 'target\.b18\.priv\.sites')"
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g > 0) print "    " g " of " w[b] " use(s) of the removed API still there: " b } }' \
      "${WORK}/set-privall.txt" "${WORK}/cnt-privleft.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] { w[$1] = 1; next }
        { if ($1 in w) h[$1] = $2 }
        END { for (f in w) if (!(f in h) || h[f] + 0 == 0)
                 print "    this file no longer constructs anything reflectively -- the payload went with the wrapper: " f }' \
      "${WORK}/set-privfile.txt" "${WORK}/fil-ctorleft.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] { w[$1] = 1; next }
        { if ($1 in w) h[$1] = $2 }
        END { for (b in w) if (!(b in h) || h[b] + 0 == 0)
                 print "    the scanner in this method no longer reads the member it was unwrapping: " b }' \
      "${WORK}/set-privacc.txt" "${WORK}/cnt-accleft.txt" | LC_ALL=C sort | head -5
   b18_2dep=$(clamp01 "$(frac "${pv_dep_n}" "${pv_dep_w}")")
   b18_2hold=$(clamp01 "$(frac "$(( pv_ctor_n + pv_acc_n ))" "$(( pv_ctor_w + pv_acc_w ))")")
   fact "gate.b18.2.depfrac=${b18_2dep}"
   fact "gate.b18.2.holdfrac=${b18_2hold}"
   b18_2=$(minf "${b18_2dep}" "${b18_2hold}")
else
   echo "    B-18.2: no compile of this tree produced a report -> C-2 = 0"
fi
fact "B18.2.doprivileged-removal=${b18_2}"

# ---------------------------------------------------------------- B-18.3
# A catch parameter nothing reads is written `_`.
#
# THE SET IS FROZEN AND IT HAS THREE HALVES IN ONE FILE. baseline/catchparam.txt
# carries one row per catch parameter in this tree's main sources, as the
# plugin wrote it -- `<file>:<line> name=<n>|_ kind=unnamed|read|unread
# type=<T> in=<method>` -- all 93 of them: 58 whose name the block reads, 35
# whose name it does not, and 0 already unnamed, because this is a Java 6 tree
# and `_` was not a name it could be given. One file, the shape steps 06, 09
# and 10 used, for the reason they gave: keeping the populations in one file is
# what makes it impossible to stage half of the pair.
#
# THE PLUGIN DECIDES WHICH HALF A ROW IS IN, NOT THIS SCRIPT AND NOT A HUMAN.
# SourceScan walks the catch block's own subtree and asks whether the parameter
# symbol is used in it; `kind=read` means it is, `kind=unread` means it is not,
# `kind=unnamed` means the source already wrote `_`. NOTES 16.15 records the
# census and the two blocks that sit closest to the line -- a `printStackTrace`
# on the caught object is a read, and a `log.warn("...")` that mentions the
# exception only in its message text is not.
#
# EVERY DENOMINATOR HERE HAS priv.txt's BUCKETS SUBTRACTED, and that is the
# whole of this leg's coupling to C-2. 7 of the 35 unread parameters and 7 of
# the 58 read ones are inside the `doPrivileged` ladders C-2 deletes; they do
# not survive this rung in any form, so asking for them to arrive as `_` or to
# stay as they are would ask for two incompatible things at once. The
# subtraction is computed from the two staged files, not typed: `pbuckets ...
# "${WORK}/set-privall.txt"` drops exactly the buckets priv.txt names. 35 - 7
# = 28 arrivals and 58 - 7 = 51 holds, and target.txt's `want.unnamed=28` and
# `want.read=51` are the same two numbers arrived at from the golden side.
#
# THREE TERMS, ALL min()ed.
#
#   the 35 depart    per frozen `unread` bucket, how many of its unread
#                    parameters are no longer unread, summed and taken over 35
#                    -- ALL 35, including the 7 inside the ladders. A tree that
#                    does C-3 and skips C-2 still has those 7 and can still
#                    score them, by renaming them where they stand; a tree that
#                    does neither reads 0.8 here and is capped there.
#
#   the 28 arrive    per frozen `unread` bucket OUTSIDE priv.txt's buckets,
#                    min(unnamed parameters now in that method, unread
#                    parameters frozen in it), over 28. Bucket-keyed and capped
#                    per bucket, so a method that writes `_` for a parameter
#                    that was always read cannot pay for a method that renamed
#                    nothing.
#
#                    THE DEPARTURE HALF ALONE WOULD BE SATISFIED BY DELETION
#                    AND THIS ONE IS NOT, which is the same hole C-2's hold
#                    closes and it is closed the same way. Delete the catch
#                    block and its `unread` record goes; no `unnamed` record
#                    appears in that bucket, and the min() reads 0 for it.
#
#   the 51 stay      per frozen `read` bucket outside priv.txt's buckets,
#                    min(read parameters still there, frozen), over 51. Without
#                    it the answer "write `_` everywhere" scores the leg whole,
#                    and it is the specific wrong answer this construct
#                    invites: 58 of the 93 parameters here are read, several of
#                    them one line below a catch that is not, and a tree that
#                    renames those does not compile.
b18_3=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/catchparam.txt ] \
   && [ -r /tests/baseline/priv.txt ]; then
   pbuckets /tests/baseline/priv.txt privall
   pbuckets /tests/baseline/catchparam.txt cpdep  kind=unread
   pbuckets /tests/baseline/catchparam.txt cparr  kind=unread "${WORK}/set-privall.txt"
   pbuckets /tests/baseline/catchparam.txt cphold kind=read   "${WORK}/set-privall.txt"
   cbuckets catchparam cpunnamed kind=unnamed
   cbuckets catchparam cpunread  kind=unread
   cbuckets catchparam cpread    kind=read

   cp_dep_w=$(ptot "${WORK}/set-cpdep.txt")
   cp_dep_n=$(pdep "${WORK}/set-cpdep.txt" "${WORK}/cnt-cpunread.txt")
   cp_arr_w=$(ptot "${WORK}/set-cparr.txt")
   cp_arr_n=$(psum "${WORK}/set-cparr.txt" "${WORK}/cnt-cpunnamed.txt")
   cp_hold_w=$(ptot "${WORK}/set-cphold.txt")
   cp_hold_n=$(psum "${WORK}/set-cphold.txt" "${WORK}/cnt-cpread.txt")
   fact "gate.b18.3.departed=${cp_dep_n}"
   fact "gate.b18.3.departed.want=${cp_dep_w}"
   fact "gate.b18.3.arrived=${cp_arr_n}"
   fact "gate.b18.3.arrived.want=${cp_arr_w}"
   fact "gate.b18.3.held=${cp_hold_n}"
   fact "gate.b18.3.held.want=${cp_hold_w}"
   fact "gate.b18.3.unnamed=$(sv 'catchparam\.unnamed')"
   fact "gate.b18.3.unread=$(sv 'catchparam\.unread')"
   fact "gate.b18.3.read=$(sv 'catchparam\.read')"
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g > 0) print "    " g " of " w[b] " catch parameter(s) still declare a name nothing reads: " b } }' \
      "${WORK}/set-cpdep.txt" "${WORK}/cnt-cpunread.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    " g " of " w[b] " unread catch parameter(s) in this method are written _: " b } }' \
      "${WORK}/set-cparr.txt" "${WORK}/cnt-cpunnamed.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    a catch parameter this block reads is no longer read here (" g " of " w[b] " left): " b } }' \
      "${WORK}/set-cphold.txt" "${WORK}/cnt-cpread.txt" | LC_ALL=C sort | head -5
   b18_3dep=$(clamp01 "$(frac "${cp_dep_n}" "${cp_dep_w}")")
   b18_3arr=$(clamp01 "$(frac "${cp_arr_n}" "${cp_arr_w}")")
   b18_3hold=$(clamp01 "$(frac "${cp_hold_n}" "${cp_hold_w}")")
   fact "gate.b18.3.depfrac=${b18_3dep}"
   fact "gate.b18.3.arrfrac=${b18_3arr}"
   fact "gate.b18.3.holdfrac=${b18_3hold}"
   b18_3=$(minf "$(minf "${b18_3dep}" "${b18_3arr}")" "${b18_3hold}")
else
   echo "    B-18.3: no compile of this tree produced a report -> C-3 = 0"
fi
fact "B18.3.unnamed-catch=${b18_3}"

# ---------------------------------------------------------------- B-18.4
# Four forced-open fields become VarHandles, and ten do not.
#
# THE SET IS FROZEN AND ITS ARRIVAL HALF IS MEASURED AGAINST ZERO.
# baseline/varhandle.txt carries one row for each of the 14 `setAccessible`
# calls in this tree's main sources -- `<file>:<line> call=setAccessible
# recv=<T> in=<method> verdict=<v>` -- and the tree it is handed has 0
# `privateLookupIn` calls and 0 `unreflectVarHandle` calls. So the arrival is
# scored against a measured zero rather than against a threshold, exactly as
# step 10's C-4 scored `seqnew`.
#
# THE VERDICTS ARE DERIVED FROM THE RECORD AND NOT FROM A TYPED LIST.
# `verdict=scanner` is a call whose method hands the `Field` or `Method` back
# to its caller -- there is no access here to route through a handle.
# `verdict=behaviour` is a `Constructor.newInstance` or `Method.invoke` chain,
# where reflection reports inaccessibility as a checked `IllegalAccessException`
# at the call site and a `MethodHandles.Lookup` refuses earlier and with a
# different type. `verdict=convert` is what is left: 4 plain private-field
# reads and writes whose only failure modes are argument and access errors that
# no transcript records. The file's header carries the argument and
# mk-baseline-step11.sh derives every verdict from the record's own file and
# receiver type.
#
# THREE STRUCTURAL TERMS AND ONE SCALING.
#
#   the 8 arrive     per `convert` bucket, 1 for a `privateLookupIn` and 1 for
#                    an `unreflectVarHandle`, over 8. COUNTED SEPARATELY AND
#                    CAPPED SEPARATELY, because the two calls are two halves of
#                    one promise: a `privateLookupIn` with no
#                    `unreflectVarHandle` is a lookup nobody used, and an
#                    `unreflectVarHandle` without one cannot see a private
#                    field at all. Two lookups in one method may not pay for a
#                    missing handle.
#
#   the 4 depart     per `convert` bucket, whether its `setAccessible` call is
#                    gone, over 4. A VarHandle added beside the reflection it
#                    was supposed to replace pays nothing.
#
#   the 10 stay      per bucket varhandle.txt does NOT mark `convert`,
#                    min(`setAccessible` calls still there, frozen), over 10.
#                    This is the term that makes the leg a judgement rather
#                    than a sweep: `unreflectVarHandle` is one expression and
#                    it is available for all 14 sites, so the wrong answer here
#                    is the thorough one.
#
#   x the 4          the structural terms multiplied by the fraction of
#                    baseline/vhids.txt still matching, which is B-10's rule
#                    and B-17.3's shape. The four recordings named there are
#                    the ones whose outcome is an `IllegalAccessException`
#                    THIS TREE raised -- exactly the wrapping the four
#                    `behaviour` verdicts refuse to convert -- and the list is
#                    closed in the image against the era rather than trusted as
#                    prose. A tree that keeps all ten calls and rewrites what
#                    surrounds them scores the hold half in full and this one
#                    at zero.
#
#                    FOUR AND NOT THE FIVE THE STRING FINDS. A fifth transcript
#                    carries `IllegalAccessException` out of
#                    `java.util.ServiceLoader$ProviderImpl` inside `java.base`,
#                    wrapped in the `ServiceConfigurationError` that says so;
#                    that is the JDK's reflection refusing, not this tree's, no
#                    site in varhandle.txt is on its path, and svcids.txt
#                    already scales B-15.3 by it. The image derives the split
#                    (IllegalAccessException AND NOT ServiceConfigurationError)
#                    and checks all six behaviour-id lists pairwise disjoint,
#                    fifteen pairs, so no recorded fixture prices two legs.
b18_4=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/varhandle.txt ]; then
   pbuckets /tests/baseline/varhandle.txt vhconv verdict=convert
   pbuckets /tests/baseline/varhandle.txt vhhold "" "${WORK}/set-vhconv.txt"
   cbuckets varhandle vhlookup    call=privateLookupIn
   cbuckets varhandle vhunreflect call=unreflectVarHandle
   cbuckets accessible accleft    call=setAccessible

   vh_dep_w=$(ptot "${WORK}/set-vhconv.txt")
   vh_dep_n=$(pdep "${WORK}/set-vhconv.txt" "${WORK}/cnt-accleft.txt")
   vh_arr_w=$(( vh_dep_w * 2 ))
   vh_arr_n=$(( $(psum "${WORK}/set-vhconv.txt" "${WORK}/cnt-vhlookup.txt") \
              + $(psum "${WORK}/set-vhconv.txt" "${WORK}/cnt-vhunreflect.txt") ))
   vh_hold_w=$(ptot "${WORK}/set-vhhold.txt")
   vh_hold_n=$(psum "${WORK}/set-vhhold.txt" "${WORK}/cnt-accleft.txt")
   fact "gate.b18.4.departed=${vh_dep_n}"
   fact "gate.b18.4.departed.want=${vh_dep_w}"
   fact "gate.b18.4.arrived=${vh_arr_n}"
   fact "gate.b18.4.arrived.want=${vh_arr_w}"
   fact "gate.b18.4.held=${vh_hold_n}"
   fact "gate.b18.4.held.want=${vh_hold_w}"
   fact "gate.b18.4.handles=$(sv 'varhandle\.size')"
   fact "gate.b18.4.setaccessible=$(sv 'accessible\.size')"
   fact "gate.b18.4.setaccessible.was=$(tv 'target\.acc\.total')"
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g > 0) print "    the field this method forces open is still forced open: " b } }' \
      "${WORK}/set-vhconv.txt" "${WORK}/cnt-accleft.txt" | LC_ALL=C sort | head -4
   awk 'FILENAME == ARGV[1] { w[$1] = 1; next }
        FILENAME == ARGV[2] { l[$1] = $2; next }
        { u[$1] = $2 }
        END { for (b in w) {
                 g = (b in l) ? l[b] : 0; u1 = (b in u) ? u[b] : 0
                 if (g == 0 && u1 == 0) print "    no VarHandle is built in this method at all: " b
                 else if (g == 0)       print "    a Field is unreflected here with no privateLookupIn to see it: " b
                 else if (u1 == 0)      print "    a private Lookup is taken here and no VarHandle is made from it: " b } }' \
      "${WORK}/set-vhconv.txt" "${WORK}/cnt-vhlookup.txt" "${WORK}/cnt-vhunreflect.txt" \
      | LC_ALL=C sort | head -4
   awk 'FILENAME == ARGV[1] { w[$1] = $2; next }
        { h[$1] = $2 }
        END { for (b in w) { g = (b in h) ? h[b] : 0
                             if (g < w[b]) print "    a setAccessible call that had to stay is gone (" g " of " w[b] " left): " b } }' \
      "${WORK}/set-vhhold.txt" "${WORK}/cnt-accleft.txt" | LC_ALL=C sort | head -5
   b18_4dep=$(clamp01 "$(frac "${vh_dep_n}" "${vh_dep_w}")")
   b18_4arr=$(clamp01 "$(frac "${vh_arr_n}" "${vh_arr_w}")")
   b18_4hold=$(clamp01 "$(frac "${vh_hold_n}" "${vh_hold_w}")")
else
   echo "    B-18.4: no compile of this tree produced a report -> C-4 = 0"
fi
fact "gate.b18.4.depfrac=${b18_4dep}"
fact "gate.b18.4.arrfrac=${b18_4arr}"
fact "gate.b18.4.holdfrac=${b18_4hold}"

# A proviso that cannot be checked is NOT satisfied -- B-10's rule, kept here
# and kept for the reason step 10's C-3 kept it: no summary means family A did
# not run, and a source shape on its own is not evidence that the four sites
# this leg refuses to convert still fail the way they failed.
b18_4exec=0.000000
b18_4execn=0
b18_4execbad=0
if [ -r /tests/baseline/vhids.txt ]; then
   b18_4execn=$(grep -c '^[a-z]' /tests/baseline/vhids.txt 2>/dev/null || true)
   case "${b18_4execn}" in ''|*[!0-9]*) b18_4execn=0 ;; esac
fi
if [ "${b18_4execn}" -eq 0 ]; then
   echo "    B-18.4: baseline/vhids.txt names no recorded access failure -- the scaling cannot be read"
elif [ -r "${summary}" ]; then
   b18_4execbad=$(awk -F= 'FILENAME == ARGV[1] { if ($0 !~ /^#/ && $0 != "") want[$0] = 1; next }
                        /^behavior\.(differ|missing)\[/ { if ($2 in want) n++ }
                        END { print n + 0 }' /tests/baseline/vhids.txt "${summary}")
   b18_4exec=$(clamp01 "$(frac "$(( b18_4execn - b18_4execbad ))" "${b18_4execn}")")
else
   b18_4execbad="${b18_4execn}"
fi
fact "gate.b18.4.exec.want=${b18_4execn}"
fact "gate.b18.4.exec.broken=${b18_4execbad}"
fact "gate.b18.4.exec=${b18_4exec}"
[ "${b18_4execbad}" -eq 0 ] || \
   printf '    %s of %s recorded access failure(s) no longer match -- C-4 scaled by %s\n' \
      "${b18_4execbad}" "${b18_4execn}" "${b18_4exec}"
b18_4=$(awk -v d="${b18_4dep}" -v m="${b18_4arr}" -v h="${b18_4hold}" -v e="${b18_4exec}" \
        'BEGIN { v = (d < m) ? d : m; if (h < v) v = h; printf "%.6f", v * e }')
fact "B18.4.varhandle=${b18_4}"

b18=$(awk -v a="${b18_2}" -v b="${b18_3}" -v c="${b18_4}" 'BEGIN {
   printf "%.6f", (0.46*a + 0.26*b + 0.24*c) / 0.96
}')
fact "B18.security-api-removal=${b18}"
# =========================================================== family C
#
# The migration this step actually asks for: the tree reaches the last rung of
# the ladder, says what it imports in the units the platform is actually shipped
# in, moves the one piece of per-thread state whose lifetime is an extent, and
# asks the JVM for the header layout 25 made final.
#
# THE LEVEL IS THE MUST-FLIP AND C-1 OWNS IT, as at every rung since 05, with
# one addition this rung is the first to need. `major=69` is C-1's number and
# `was=66` is B-4's, staged side by side in baseline/level.txt so neither gate
# reads the other's; and beside them now `minor=0`, because from here up the
# target level is a release WITH PREVIEW FEATURES IN IT. A tree that compiles
# behind `--enable-preview` stamps major 69 and minor 65535 on every class it
# emits, and a major-only reading cannot tell that tree from a correct one. It
# is not a hypothetical: all three of this rung's language and runtime changes
# were preview in some release before 25, the shape of the flag is in every
# tutorial written before they went final, and a tree that turns it on has not
# migrated to 25 -- it has migrated to a 25 that will not run on the next one.
#
# FOUR CHANGES AND A CENSUS AMONG NONE OF THEM. Each is a population frozen in
# baseline/ with at least two halves, because each has a wrong way to make it
# and every one of the wrong ways is reachable from this tree:
#
#   C-2  0.38  MODULE IMPORTS. 495 single-type imports of JDK types across 171
#              files collapse to 188 `import module` declarations. JEP 511 made
#              that final in 25 and it is the one change in this rung that
#              touches nearly every file in the tree.
#
#              IT IS SCORED AS 171 SET EQUALITIES AND NOT AS 188 LINES, which is
#              the whole design of the leg. `import module java.base;` in all
#              171 files would produce 171 correct-looking lines and would be
#              wrong in 19 of them, because 19 files name a type java.base does
#              not export -- java.naming, java.logging, java.management,
#              java.xml, java.net.http -- and a module import is a type-import-
#              on-demand over the EXPORTED packages of exactly the module named.
#              Conversely `import module` for all six everywhere would compile
#              and would be a worse statement of the tree's dependencies than
#              the 495 lines it replaced. So each file's set is compared whole,
#              and a file with a right module and a missing one scores nothing.
#
#              AND A SECOND HALF OVER THE 151 FILES WITH NOTHING TO DO.
#              modimport.txt carries a row for EVERY file in scope and not only
#              for the 171 with work: the other 151 -- 118 main and 33 test --
#              carry `mods=- n=0`, because they import no JDK type at all. A
#              module import in one of those is an import of something the file
#              does not use, which is precisely what a blanket edit leaves
#              behind, and it is invisible to a leg that only looks where it
#              expects to find work. min(set equality, no spurious arrivals) is
#              what makes the blanket edit unprofitable, and the second half has
#              a population of its own rather than an absence to argue from.
#
#   C-3  0.33  AND THE 495 LINES ACTUALLY GO -- BUT NOT ALL OF THEM. 452 depart
#              and 43 HOLD, and the 43 are the leg. A module import subsumes a
#              single-type import of a type in an exported package of that
#              module, and nothing else:
#
#                * 16 are STATIC imports. `import static java.util.Arrays.asList`
#                  is not a type import and no module import reaches it.
#                * 2 are NESTED types. `java.util.ServiceLoader.Provider` is
#                  imported through its outer class, which a type-import-on-
#                  demand does not walk into.
#                * 25 are AMBIGUOUS, every one of them
#                  `java.lang.annotation.Annotation`. JEP 484 put
#                  `java.lang.classfile.Annotation` in java.base in 24, so as of
#                  this release `import module java.base` leaves the simple name
#                  `Annotation` ambiguous, and a file that deletes the explicit
#                  import DOES NOT COMPILE. This is the one leg of this rung
#                  that a compiler failure teaches, and the population records
#                  the reason on every row so the report can say which of the
#                  three it was.
#
#              min(departure, HOLD), because deleting all 495 is one sed away
#              and is not this migration.
#
#   C-4  0.25  ONE ScopedValue, AND SEVEN ThreadLocals THAT MUST NOT MOVE. JEP
#              506 made ScopedValue final in 25. This tree has 8 ThreadLocal
#              fields and exactly one of them has the shape ScopedValue is for:
#              `ManagerImpl.handledThrowables` is bound for the extent of a
#              `fire` and read inside it, which is what `ScopedValue.where(..)
#              .run(..)` expresses and what a ThreadLocal expresses only by
#              convention plus a `finally`. The other seven are not that shape
#              and scopedvalue.txt records why on each, one token per row: one
#              is debug-only state created on demand inside debug(), one is an
#              SPI boundary between two calls a third party makes, one is a
#              save-register for a context classloader whose reader is
#              third-party code, one is a PUBLIC STATIC whose readers this tree
#              does not own, one is written by a constructor and read by
#              something that does not enclose it, and two span a framework
#              callback -- separate invocations by JUnit and by TestNG, not a
#              nested region of one. A ScopedValue is immutable inside its
#              extent and visible only within it; a field that is ASSIGNED after
#              binding, or READ outside the call that bound it, cannot become
#              one.
#
#              THREE HALVES: the one field left ThreadLocal's declaration, the
#              one field arrived as a ScopedValue declaration, and the seven are
#              still ThreadLocal declarations. A tree that converts all eight
#              scores the arrival and loses the hold.
#
#   C-5  0.02  COMPACT OBJECT HEADERS, ASKED OF THE BUILD AND READ BACK OUT OF
#              IT. JEP 519 made `-XX:+UseCompactObjectHeaders` a product flag in
#              25 -- no `-XX:+UnlockExperimentalVMOptions` in front of it, which
#              is the whole content of the JEP -- and a project whose tests fork
#              a JVM is where it has to be set to mean anything.
#
#              SMALL, AND DELIBERATELY SO. It is one flag, it is worth 0.02 --
#              priced with C-1 rather than on its own, because those two are
#              the pair a do-nothing tree reaches with one build-script line
#              each and the header's 0.047619 ceiling is on their SUM -- and
#              it is here because it is the only item in this rung that is a
#              RUNTIME change rather than a source one; a step called "Java 25"
#              whose every leg reads source would be measuring half the release.
#              It is read by asking Gradle what it would pass to the test JVM --
#              a separate, bounded `arqTestJvmArgs` invocation after the scored
#              `gradlew test`, never out of a build file -- because a build file
#              is a claim and the argument list is what the fork gets.
#
# NO ANTI-GAMING MULTIPLIER ON THIS FAMILY, for the reason every rung since 09
# has given: every leg here is a min() over sets frozen in baseline/, two of
# them a min() of three halves, and a term that paid the input tree for what it
# already has would spend the room under item 6's bar rather than defend it.
c1=0; c2=0; c3=0; c4=0; c5=0
# The sub-terms the report prints, initialised here rather than inside the
# guards that compute them: a leg whose compile never happened still has to
# print a row, and a row with an empty field is a row a reader cannot read.
c1cls=0; c1have=0; c1prev=0
c2set=0; c2clean=0; c2ok=0; c2w=0; c2cl=0; c2nn=0; c2spur=0
c3dep=0; c3hold=0; c3depn=0; c3depw=0; c3holdn=0; c3holdw=0
c4dep=0; c4arr=0; c4hold=0; c4depn=0; c4arrn=0; c4convw=0; c4holdn=0; c4holdw=0
c5n=0; c5w=0; c5t=0; c5h=0
echo
echo "=== family C: Java 25 (weight 0.70 x 0.75) ==="

# ---------------------------------------------------------------- C-1
# Every class file the build emits is major 69, minor 0.
#
# Read out of the constant pools of the jars the build published AND of every
# class file the build left in the tree -- the same scan B-4 read, written by
# arqgate.Scan above -- and never out of a build file. A `release` line is a
# claim; a class-file major is what the artifact IS.
#
# A FRACTION AND NOT A FLAG, the same division of labour with B-4 that steps
# 05, 06, 07, 09, 10 and 11 used. This leg asks HOW MUCH of the tree reached 69;
# B-4 asks whether what the tree reached is internally consistent and not below
# 66. A tree that moved eleven of its twelve projects scores 0.916667 here and
# 0.000000 on B-4, because two majors are present.
#
# THE MINOR IS THE PART THAT IS NEW AT THIS RUNG, and it is read from the same
# eight bytes. `majorminor.69.0` and not `major.69`: a `--enable-preview` build
# emits major 69 with minor 65535 (JVMS 4.1), so it satisfies `major.69`
# completely and `majorminor.69.0` not at all. Nothing is subtracted and no
# multiplier is applied -- the preview classes simply are not in the numerator,
# which is the correct arithmetic for a fraction of classes at a level. The
# count is published as its own fact besides, so a tree that scores badly here
# is told which of the two reasons it was.
#
# 69, 0 AND 66 ARE READ OUT OF baseline/level.txt AND ARE NOT LITERALS ANYWHERE
# IN THIS FILE. `major=69` is WANT_MAJOR, `minor=0` is WANT_MINOR and `was=66`
# is HOLD_MAJOR, all three parsed in the header.
c1=0
if [ -s "${WORK}/scan.txt" ]; then
   c1cls=$(sed -n 's/^classes=//p' "${WORK}/scan.txt" | head -1)
   c1have=$(sed -n "s/^majorminor\.${WANT_MAJOR}\.${WANT_MINOR}=//p" "${WORK}/scan.txt" | head -1)
   c1prev=$(sed -n 's/^preview\.classes=//p' "${WORK}/scan.txt" | head -1)
   case "${c1cls:-}"  in ''|*[!0-9]*) c1cls=0 ;;  esac
   case "${c1have:-}" in ''|*[!0-9]*) c1have=0 ;; esac
   case "${c1prev:-}" in ''|*[!0-9]*) c1prev=0 ;; esac
   fact "gate.c1.classes=${c1cls}"
   fact "gate.c1.at${WANT_MAJOR}.${WANT_MINOR}=${c1have}"
   fact "gate.c1.preview=${c1prev}"
   c1=$(frac "${c1have}" "${c1cls}")
   [ "${c1have}" -eq "${c1cls}" ] || \
      printf '    %s of %s class files are major %s minor %s\n' \
         "${c1have}" "${c1cls}" "${WANT_MAJOR}" "${WANT_MINOR}"
   [ "${c1prev}" -eq 0 ] || \
      printf '    %s class file(s) carry the preview marker (minor 65535): this tree compiled behind --enable-preview, and every feature this step asks for is final in %s\n' \
         "${c1prev}" "${WANT_MAJOR}"
else
   fact "gate.c1=no-scan"
   echo "    C-1: no class-file scan was produced -> C-1 = 0"
fi
fact "C1.level-${WANT_MAJOR}=${c1}"

# ---------------------------------------------------------------- C-2
# 188 `import module` declarations across 171 files, and none in the other 151.
#
# TWO HALVES BECAUSE ONE HALF IS A BLANKET EDIT. `import module java.base;` in
# every file in the tree is one line of sed, produces 322 statements where 188
# belong, and would score the first half at 152/171 on its own. The second half
# is the 151 files that import no JDK type and so need no module import; a
# blanket edit scores 0 there and min() makes the whole leg 0. The two halves
# come out of ONE frozen population file -- baseline/modimport.txt carries the
# 151 as explicit `n=0` rows -- so the scope of the second half is a set this
# rung froze and not a path pattern this script invents.
#
# SET EQUALITY PER FILE AND NOT A COUNT. A row names the exact set of modules
# the file needs: `mods=java.base,java.naming n=2`. A file that writes one of
# the two scores nothing for that file, and so does a file that writes all six.
# 19 of the 171 need more than java.base (13 need two modules, 2 need three,
# and 4 need a single module that is not java.base) which is exactly the
# population a one-module-fits-all edit gets wrong.
#
# A FILE THE CANDIDATE ADDED IS NOT SCORED EITHER WAY. It is in neither half of
# a frozen population, and this leg does not have an opinion about new files.
c2=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/modimport.txt ]; then
   sed -n 's/^modimport\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ f = $1; sub(/:[0-9]+$/, "", f); nm = ""
               for (i = 2; i <= NF; i++) if (index($i, "name=") == 1) nm = substr($i, 6)
               if (nm != "") print f, nm }' \
      | LC_ALL=C sort -u > "${WORK}/mi-got.txt"
   awk '$0 !~ /^#/ && $0 != "" {
           mods = ""
           for (i = 2; i <= NF; i++) if (index($i, "mods=") == 1) mods = substr($i, 6)
           if (mods == "" || mods == "-") next
           n = split(mods, a, ",")
           for (i = 1; i <= n; i++) print $1, a[i] }' \
      /tests/baseline/modimport.txt | LC_ALL=C sort -u > "${WORK}/mi-want.txt"

   # One pass, four numbers, left on disk under their own name rather than
   # captured positionally: the tally is in the packet a reviewer reads, and
   # nothing in this file has to reach for $1.
   awk '
      FILENAME == ARGV[1] {
         if ($0 ~ /^#/ || $0 == "") next
         k = 0
         for (i = 2; i <= NF; i++) if (index($i, "n=") == 1) k = substr($i, 3) + 0
         if (k > 0) work[$1] = 1; else none[$1] = 1
         next }
      FILENAME == ARGV[2] { w[$1 SUBSEP $2] = 1; next }
                          { g[$1 SUBSEP $2] = 1; gf[$1] = 1 }
      END {
         for (k in w) { split(k, a, SUBSEP); if (!(k in g)) bad[a[1]] = 1 }
         for (k in g) { split(k, a, SUBSEP); if ((a[1] in work) && !(k in w)) bad[a[1]] = 1 }
         ok = 0; nw = 0; for (f in work) { nw++; if (!(f in bad)) ok++ }
         cl = 0; nn = 0; for (f in none) { nn++; if (!(f in gf)) cl++ }
         print ok, nw, cl, nn }' \
      /tests/baseline/modimport.txt "${WORK}/mi-want.txt" "${WORK}/mi-got.txt" \
      > "${WORK}/c2-tally.txt"
   c2ok=$(awk '{ print $1 + 0 }' "${WORK}/c2-tally.txt")
   c2w=$( awk '{ print $2 + 0 }' "${WORK}/c2-tally.txt")
   c2cl=$(awk '{ print $3 + 0 }' "${WORK}/c2-tally.txt")
   c2nn=$(awk '{ print $4 + 0 }' "${WORK}/c2-tally.txt")
   c2spur=$(( c2nn - c2cl ))

   fact "gate.c2.files=${c2ok}"
   fact "gate.c2.files.want=${c2w}"
   fact "gate.c2.files.was=$(tv 'target\.mi\.was')"
   fact "gate.c2.imports=$(sv 'modimport\.size')"
   fact "gate.c2.imports.want=$(tv 'target\.mi\.imports')"
   fact "gate.c2.clean=${c2cl}"
   fact "gate.c2.clean.want=${c2nn}"
   fact "gate.c2.spurious=${c2spur}"

   awk 'FILENAME == ARGV[1] { w[$1 SUBSEP $2] = 1; wf[$1] = 1; next }
                            { g[$1 SUBSEP $2] = 1 }
        END { for (k in w) { split(k, a, SUBSEP); if (!(k in g)) miss[a[1]] = miss[a[1]] " " a[2] }
              for (k in g) { split(k, a, SUBSEP); if ((a[1] in wf) && !(k in w)) extra[a[1]] = extra[a[1]] " " a[2] }
              for (f in miss)  print "    this file still needs a module import it does not have --" miss[f] ": " f
              for (f in extra) print "    this file imports a module it does not need --" extra[f] ": " f }' \
      "${WORK}/mi-want.txt" "${WORK}/mi-got.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           k = 0; for (i = 2; i <= NF; i++) if (index($i, "n=") == 1) k = substr($i, 3) + 0
           if (k == 0) none[$1] = 1
           next }
        { if ($1 in none) print "    this file imports no JDK type and needs no module import: " $1 }' \
      /tests/baseline/modimport.txt "${WORK}/mi-got.txt" | LC_ALL=C sort -u | head -5

   c2set=$(clamp01 "$(frac "${c2ok}" "${c2w}")")
   c2clean=$(clamp01 "$(frac "${c2cl}" "${c2nn}")")
   fact "gate.c2.setfrac=${c2set}"
   fact "gate.c2.cleanfrac=${c2clean}"
   c2=$(minf "${c2set}" "${c2clean}")
else
   echo "    C-2: no compile of this tree produced a report -> C-2 = 0"
fi
fact "C2.module-imports=${c2}"

# ---------------------------------------------------------------- C-3
# 452 of the 495 single-type JDK imports go, and 43 STAY.
#
# min(departure, HOLD), and the hold is the leg. Deleting all 495 imports is one
# `sed -i '/^import java\./d'` away and produces a tree that does not compile --
# but a scorer that only counted departures would have called it finished before
# the compiler was consulted, and a candidate that deletes 495 and puts 30 back
# under a compiler's direction has still not found the other 13.
#
# THE 43 ARE THREE DIFFERENT REASONS AND THE POPULATION RECORDS WHICH:
#
#   16 static  -- `import static java.util.Arrays.asList` imports a MEMBER. A
#                 module import is a type-import-on-demand and does not reach
#                 members at all, so no module import can subsume it.
#   2  nested  -- `java.util.ServiceLoader.Provider` is a member type of
#                 ServiceLoader, not a member of a package; a type-import-on-
#                 demand over java.util does not walk into it.
#   25 ambiguous -- every one of them `java.lang.annotation.Annotation`. JEP 484
#                 added `java.lang.classfile.Annotation` to java.base in 24, and
#                 both packages are exported, so `import module java.base` alone
#                 leaves the simple name `Annotation` ambiguous and the file
#                 fails to compile. The explicit single-type import is what
#                 resolves it -- JLS 6.5.5 gives a single-type import precedence
#                 over an on-demand one -- and it is the one of the three the
#                 compiler will teach a candidate, loudly, 25 times.
#
# A HOLD THE TREE NEVER HAD TO MAKE LEAVES THE DENOMINATOR. A file that names
# the type in full -- `java.util.ServiceLoader.Provider::get` -- imports nothing
# and needs nothing held, and the excuse below grants that only when the simple
# name appears nowhere unqualified in the file. Deleting the import and keeping
# the simple name is still the whole loss it was.
#
# KEYED ON <file> AND THE FULLY QUALIFIED NAME, never on a line number: this
# rung deletes 452 import lines and inserts 188, so every record below an import
# block moves, and a population keyed on position would read the correct tree as
# a total loss.
c3=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/jdkimport.txt ]; then
   sed -n 's/^jdkimport\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ f = $1; sub(/:[0-9]+$/, "", f); q = ""
               for (i = 2; i <= NF; i++) if (index($i, "q=") == 1) q = substr($i, 3)
               if (q != "") print f, q }' \
      | LC_ALL=C sort -u > "${WORK}/ji-got.txt"

   # A HELD IMPORT CAN BE ABSENT FOR A REASON THAT IS NOT A DELETION: the file
   # may spell the reference out in full. `java.util.ServiceLoader.Provider::get`
   # needs no import and names exactly what the imported simple name named, and
   # a tree that arrived at this step written that way never had a line to hold.
   # Such a record leaves the denominator rather than counting against the tree.
   #
   # The excuse is granted only when the simple name appears NOWHERE unqualified
   # in that file, which is exactly what a tree that deleted the import and kept
   # the simple name fails: that tree does not compile and it is not excused.
   # Comment text is stripped per line first -- `// Provider::type names the
   # implementation` and a javadoc sentence about the Service Provider mechanism
   # are both prose, and reading either as a use would refuse the excuse to the
   # one tree that deserves it. A block comment opened mid-line survives that
   # stripping; the cost is a refused excuse, never a granted one.
   awk '{ if ($0 ~ /^#/ || $0 == "") next
          q = ""; v = ""
          for (i = 2; i <= NF; i++) {
             if (index($i, "q=") == 1) q = substr($i, 3)
             else if (index($i, "verdict=") == 1) v = substr($i, 9) }
          if (q != "" && v == "hold") print $1, q }' \
      /tests/baseline/jdkimport.txt | LC_ALL=C sort -u > "${WORK}/ji-hold.txt"
   : > "${WORK}/ji-excuse.txt"
   while read -r jf jq; do
      [ -n "${jq:-}" ] || continue
      if grep -qxF "${jf} ${jq}" "${WORK}/ji-got.txt"; then continue; fi
      # a file that is gone is a loss and not an excuse
      [ -r "${TREE}/${jf}" ] || continue
      js=${jq##*.}
      sed -e 's://.*::' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\*/d' \
         "${TREE}/${jf}" > "${WORK}/ji-src.txt" 2>/dev/null || :
      if ! grep -qE '(^|[^A-Za-z0-9_$.])'"${js}"'([^A-Za-z0-9_$]|$)' \
              "${WORK}/ji-src.txt"; then
         echo "${jf} ${jq}" >> "${WORK}/ji-excuse.txt"
      fi
   done < "${WORK}/ji-hold.txt"
   fact "gate.c3.held.excused=$(grep -c . "${WORK}/ji-excuse.txt" || true)"

   # THE SAME EXCUSE, ON THE OTHER LEG. A depart row is scored by its ABSENCE,
   # so a tree that never wrote the import reads as a tree that removed it and
   # collects credit for a deletion it never performed. That is not hypothetical
   # at this rung: the population was frozen off the BLIND chain, and the golden
   # step-11 solution reaches six of these rows by a chained or fully-qualified
   # form -- `MethodHandles.privateLookupIn(..).unreflectVarHandle(f).set(..)`
   # where the blind chain names a `VarHandle` local, `new ObjectInputStream(new
   # ByteArrayInputStream(..))` where it names an `InputStream` -- so it has no
   # line to delete. Both styles are correct Java and both scored step 11 at
   # 1.000000, because step 11 scored calls and constructs. C-3 is the first leg
   # that scores IMPORTS, which is exactly where the two styles part.
   #
   # The predicate is the hold leg's, unchanged: the row leaves the denominator
   # only when the simple name appears NOWHERE unqualified in that file. A real
   # departure KEEPS the simple name -- supplying it is what the module import
   # is for -- so a tree that deleted `import java.util.List;` and still writes
   # `List.of` is not excused, and never was. And because an absent row always
   # counted toward the numerator, moving it out of numerator and denominator
   # together can only lower the ratio or leave it at 1.0: this excuse cannot
   # grant credit, it can only stop granting it.
   awk '{ if ($0 ~ /^#/ || $0 == "") next
          q = ""; v = ""
          for (i = 2; i <= NF; i++) {
             if (index($i, "q=") == 1) q = substr($i, 3)
             else if (index($i, "verdict=") == 1) v = substr($i, 9) }
          if (q != "" && v == "depart") print $1, q }' \
      /tests/baseline/jdkimport.txt | LC_ALL=C sort -u > "${WORK}/ji-dep.txt"
   : > "${WORK}/ji-dep-excuse.txt"
   while read -r jf jq; do
      [ -n "${jq:-}" ] || continue
      if grep -qxF "${jf} ${jq}" "${WORK}/ji-got.txt"; then continue; fi
      # a file that is gone is a loss and not an excuse
      [ -r "${TREE}/${jf}" ] || continue
      js=${jq##*.}
      sed -e 's://.*::' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\*/d' \
         "${TREE}/${jf}" > "${WORK}/ji-src.txt" 2>/dev/null || :
      if ! grep -qE '(^|[^A-Za-z0-9_$.])'"${js}"'([^A-Za-z0-9_$]|$)' \
              "${WORK}/ji-src.txt"; then
         echo "${jf} ${jq}" >> "${WORK}/ji-dep-excuse.txt"
      fi
   done < "${WORK}/ji-dep.txt"
   fact "gate.c3.departed.excused=$(grep -c . "${WORK}/ji-dep-excuse.txt" || true)"

   awk '
      FILENAME == ARGV[1] {
         if ($0 ~ /^#/ || $0 == "") next
         q = ""; v = ""
         for (i = 2; i <= NF; i++) {
            if (index($i, "q=") == 1) q = substr($i, 3)
            else if (index($i, "verdict=") == 1) v = substr($i, 9) }
         if (q == "") next
         if (v == "depart") { dep[$1 SUBSEP q] = 1; nd++ }
         else if (v == "hold") { hold[$1 SUBSEP q] = 1 }
         next }
      FILENAME == ARGV[2] { ex[$1 SUBSEP $2] = 1; next }
      FILENAME == ARGV[3] { dx[$1 SUBSEP $2] = 1; next }
      { g[$1 SUBSEP $2] = 1 }
      END { d = 0; nde = 0
            for (k in dep) {
               if (k in dx) continue
               nde++; if (!(k in g)) d++ }
            h = 0; nh = 0
            for (k in hold) {
               if (k in g)          { h++; nh++ }
               else if (!(k in ex))        nh++ }
            print d, nde + 0, h, nh + 0 }' \
      /tests/baseline/jdkimport.txt "${WORK}/ji-excuse.txt" \
      "${WORK}/ji-dep-excuse.txt" "${WORK}/ji-got.txt" > "${WORK}/c3-tally.txt"
   c3depn=$( awk '{ print $1 + 0 }' "${WORK}/c3-tally.txt")
   c3depw=$( awk '{ print $2 + 0 }' "${WORK}/c3-tally.txt")
   c3holdn=$(awk '{ print $3 + 0 }' "${WORK}/c3-tally.txt")
   c3holdw=$(awk '{ print $4 + 0 }' "${WORK}/c3-tally.txt")

   fact "gate.c3.departed=${c3depn}"
   fact "gate.c3.departed.want=${c3depw}"
   fact "gate.c3.held=${c3holdn}"
   fact "gate.c3.held.want=${c3holdw}"
   fact "gate.c3.total=$(sv 'jdkimport\.size')"
   fact "gate.c3.total.was=$(tv 'target\.ji\.total')"
   fact "gate.c3.held.static=$(tv 'target\.ji\.hold\.static')"
   fact "gate.c3.held.nested=$(tv 'target\.ji\.hold\.nested')"
   fact "gate.c3.held.ambiguous=$(tv 'target\.ji\.hold\.ambiguous')"

   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           q = ""; v = ""; y = ""
           for (i = 2; i <= NF; i++) {
              if (index($i, "q=") == 1) q = substr($i, 3)
              else if (index($i, "verdict=") == 1) v = substr($i, 9)
              else if (index($i, "why=") == 1) y = substr($i, 5) }
           if (q != "" && v == "hold") { need[$1 SUBSEP q] = y }
           next }
        FILENAME == ARGV[2] { delete need[$1 SUBSEP $2]; next }
        { seen[$1 SUBSEP $2] = 1 }
        END { for (k in need) if (!(k in seen)) {
                 split(k, a, SUBSEP)
                 print "    this import is not subsumed by any module import (" need[k] ") and its removal does not compile: " a[2] " in " a[1] } }' \
      /tests/baseline/jdkimport.txt "${WORK}/ji-excuse.txt" \
      "${WORK}/ji-got.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           q = ""; v = ""
           for (i = 2; i <= NF; i++) {
              if (index($i, "q=") == 1) q = substr($i, 3)
              else if (index($i, "verdict=") == 1) v = substr($i, 9) }
           if (q != "" && v == "depart") go[$1 SUBSEP q] = 1
           next }
        { if ($1 SUBSEP $2 in go) {
             split($1 SUBSEP $2, a, SUBSEP)
             print "    a module import subsumes this and it is still written out: " a[2] " in " a[1] } }' \
      /tests/baseline/jdkimport.txt "${WORK}/ji-got.txt" | LC_ALL=C sort | head -5

   c3dep=$(clamp01 "$(frac "${c3depn}" "${c3depw}")")
   c3hold=$(clamp01 "$(frac "${c3holdn}" "${c3holdw}")")
   fact "gate.c3.depfrac=${c3dep}"
   fact "gate.c3.holdfrac=${c3hold}"
   c3=$(minf "${c3dep}" "${c3hold}")
else
   echo "    C-3: no compile of this tree produced a report -> C-3 = 0"
fi
fact "C3.import-subsumption=${c3}"

# ---------------------------------------------------------------- C-4
# One ThreadLocal becomes a ScopedValue. Seven do not.
#
# THREE HALVES, because a conversion has three separable failure modes and each
# one is somebody's plausible reading of the task:
#
#   DEPARTED  `ManagerImpl.handledThrowables` is no longer declared a
#             ThreadLocal. A tree that adds a ScopedValue beside the ThreadLocal
#             and leaves both has not migrated anything; it has two sources of
#             truth for one piece of state.
#   ARRIVED   and it IS declared a ScopedValue. A tree that deletes the field
#             scores the departure and nothing else -- which is the same shape
#             as C-3's deletion, and the same answer.
#   HELD      the other seven are still ThreadLocal declarations. `ScopedValue`
#             everywhere is one search-and-replace and breaks four of them
#             outright.
#
# WHY EXACTLY ONE, AND WHY THIS ONE. A ScopedValue is bound for the dynamic
# extent of a call and is IMMUTABLE inside it. `handledThrowables` is set at the
# top of `fire`, read by the handlers `fire` invokes, and cleared in a `finally`
# -- which is `ScopedValue.where(HANDLED, set).run(() -> ...)` written out by
# hand, minus the leak if the `finally` is ever missed. Of the other seven:
# `eventStack` is MUTATED in place after binding and is debug-only, which a
# ScopedValue cannot express; `previousClassloader` exists to be restored after
# the extent it is set in has ended, which is the opposite of extent-scoped;
# `activeStore` is read across an SPI boundary by callers this tree does not
# own; the four in the two `Arquillian` runners are set by a constructor or an
# early callback and read by a later one, and there is no single call whose
# extent contains both. scopedvalue.txt carries the reason on each row.
#
# KEYED ON <file> AND FIELD NAME. Not the line: `ManagerImpl` loses an import
# block and gains a static initialiser at this rung, and the field moves.
c4=0
if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/scopedvalue.txt ]; then
   sed -n 's/^threadlocal\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '$0 ~ /(^| )form=decl( |$)/ {
               f = $1; sub(/:[0-9]+$/, "", f); n = ""
               for (i = 2; i <= NF; i++) if (index($i, "name=") == 1) n = substr($i, 6)
               if (n != "") print f, n }' \
      | LC_ALL=C sort -u > "${WORK}/c4-tl.txt"
   sed -n 's/^scopedvalue\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '$0 ~ /(^| )form=decl( |$)/ {
               f = $1; sub(/:[0-9]+$/, "", f); n = ""
               for (i = 2; i <= NF; i++) if (index($i, "name=") == 1) n = substr($i, 6)
               if (n != "") print f, n }' \
      | LC_ALL=C sort -u > "${WORK}/c4-sv.txt"
   awk '
      FILENAME == ARGV[1] {
         if ($0 ~ /^#/ || $0 == "") next
         fl = ""; v = ""
         for (i = 2; i <= NF; i++) {
            if (index($i, "field=") == 1) fl = substr($i, 7)
            else if (index($i, "verdict=") == 1) v = substr($i, 9) }
         if (fl == "") next
         if (v == "convert") { conv[$1 SUBSEP fl] = 1; nc++ }
         else if (v == "hold") { hold[$1 SUBSEP fl] = 1; nh++ }
         next }
      FILENAME == ARGV[2] { tl[$1 SUBSEP $2] = 1; next }
                          { sv[$1 SUBSEP $2] = 1 }
      END { d = 0; for (k in conv) if (!(k in tl)) d++
            a = 0; for (k in conv) if (k in sv)    a++
            h = 0; for (k in hold) if (k in tl)    h++
            print d, a, nc + 0, h, nh + 0 }' \
      /tests/baseline/scopedvalue.txt "${WORK}/c4-tl.txt" "${WORK}/c4-sv.txt" \
      > "${WORK}/c4-tally.txt"
   c4depn=$( awk '{ print $1 + 0 }' "${WORK}/c4-tally.txt")
   c4arrn=$( awk '{ print $2 + 0 }' "${WORK}/c4-tally.txt")
   c4convw=$(awk '{ print $3 + 0 }' "${WORK}/c4-tally.txt")
   c4holdn=$(awk '{ print $4 + 0 }' "${WORK}/c4-tally.txt")
   c4holdw=$(awk '{ print $5 + 0 }' "${WORK}/c4-tally.txt")

   fact "gate.c4.departed=${c4depn}"
   fact "gate.c4.arrived=${c4arrn}"
   fact "gate.c4.convert.want=${c4convw}"
   fact "gate.c4.held=${c4holdn}"
   fact "gate.c4.held.want=${c4holdw}"
   # size minus decls, which is the call sites: the gate counts a declaration
   # as a scopedvalue record too, and the published figure it is compared
   # against is the golden's counted the same way.
   fact "gate.c4.svcalls=$(( $(sv 'scopedvalue\.size') - $(sv 'scopedvalue\.decls') ))"
   fact "gate.c4.svcalls.want=$(tv 'target\.sv\.calls')"
   fact "gate.c4.tldecls=$(sv 'threadlocal\.decls')"

   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           fl = ""; v = ""; y = ""
           for (i = 2; i <= NF; i++) {
              if (index($i, "field=") == 1) fl = substr($i, 7)
              else if (index($i, "verdict=") == 1) v = substr($i, 9)
              else if (index($i, "why=") == 1) y = substr($i, 5) }
           if (fl != "" && v == "hold") need[$1 SUBSEP fl] = y
           next }
        { seen[$1 SUBSEP $2] = 1 }
        END { for (k in need) if (!(k in seen)) {
                 split(k, a, SUBSEP)
                 print "    this field is not extent-scoped (" need[k] ") and is no longer a ThreadLocal: " a[2] " in " a[1] } }' \
      /tests/baseline/scopedvalue.txt "${WORK}/c4-tl.txt" | LC_ALL=C sort | head -5
   awk 'FILENAME == ARGV[1] {
           if ($0 ~ /^#/ || $0 == "") next
           fl = ""; v = ""
           for (i = 2; i <= NF; i++) {
              if (index($i, "field=") == 1) fl = substr($i, 7)
              else if (index($i, "verdict=") == 1) v = substr($i, 9) }
           if (fl != "" && v == "convert") { want[$1 SUBSEP fl] = 1 }
           next }
        FILENAME == ARGV[2] { tl[$1 SUBSEP $2] = 1; next }
                            { sv[$1 SUBSEP $2] = 1 }
        END { for (k in want) { split(k, a, SUBSEP)
                 if (k in tl) print "    this field is bound for the extent of one call and is still a ThreadLocal: " a[2] " in " a[1]
                 else if (!(k in sv)) print "    this field left as a ThreadLocal and did not arrive as a ScopedValue -- it was deleted, not migrated: " a[2] " in " a[1] } }' \
      /tests/baseline/scopedvalue.txt "${WORK}/c4-tl.txt" "${WORK}/c4-sv.txt" | LC_ALL=C sort | head -5

   c4dep=$(clamp01  "$(frac "${c4depn}"  "${c4convw}")")
   c4arr=$(clamp01  "$(frac "${c4arrn}"  "${c4convw}")")
   c4hold=$(clamp01 "$(frac "${c4holdn}" "${c4holdw}")")
   fact "gate.c4.depfrac=${c4dep}"
   fact "gate.c4.arrfrac=${c4arr}"
   fact "gate.c4.holdfrac=${c4hold}"
   c4=$(minf "$(minf "${c4dep}" "${c4arr}")" "${c4hold}")
else
   echo "    C-4: no compile of this tree produced a report -> C-4 = 0"
fi
fact "C4.scoped-value=${c4}"

# ---------------------------------------------------------------- C-5
# Every test JVM the build forks is started with compact object headers.
#
# JEP 519 made -XX:+UseCompactObjectHeaders a product flag in 25; through 24 it
# was experimental and needed -XX:+UnlockExperimentalVMOptions in front of it.
# That is the whole of the change and it is one line in the build -- which is
# why this leg is 0.02 and not more, and why it is priced together with C-1
# rather than on its own: those two are the pair a do-nothing tree reaches with
# one build-script line each, and the header's 0.047619 ceiling is on their
# SUM. It is here because it is the one thing at this rung that is a RUNTIME
# decision rather than a source or a class-file one, and a rung that asked only
# about source would let a tree claim Java 25 while every JVM it starts is
# configured like a Java 22 one.
#
# READ BY ASKING GRADLE WHAT IT WOULD PASS, NOT BY READING THE BUILD FILE.
# `jvmArgs` can be set in the subproject, in an `allprojects` block, in a
# convention plugin under buildSrc, in a `tasks.withType(Test)` in the root, or
# through a `JAVA_TOOL_OPTIONS` the build exports; all five are correct answers
# and only one of them is greppable. So an init script registers one task that
# asks each Test task for its own argument list, and the tally is of what that
# task printed. A build file is a claim. The argument list is what the fork
# gets.
#
# BOTH `jvmArgs` AND `allJvmArgs`, joined into the same row. The first is what
# the build set explicitly; the second is that plus everything Gradle adds of
# its own, which is where a flag that arrived through a toolchain or a
# `JAVA_TOOL_OPTIONS` shows up. The row is searched as a whole and the flag
# counts from either, because the question is whether the fork gets it.
#
# A SEPARATE, BOUNDED INVOCATION AFTER THE SCORED `gradlew test`, never folded
# into it: `arqTestJvmArgs` must not be able to change what the scored run did,
# and a probe that shares a task graph with the thing it measures is a probe
# that can. It runs second, so the daemon-less build is warm and the whole call
# is a configuration pass plus twelve `println`s -- measured at 10-15s on both
# the seed and the frozen solution. 600s is forty times the dearest reading and
# is not a threshold any tree can feel; it is there so that a build file which
# hangs at configuration time costs this leg and nothing else. `timeout` inside
# `run_unprivileged`, for the reason the scored run gives at its own arm.
#
# EVERY Test TASK OF A PROJECT, NOT ANY. A project whose second Test task was
# left behind still forks a JVM without the flag, and `any` would score it
# full. The projects are whichever ones the probe found forking -- keyed on the
# project path the task belongs to, counted, and floored at the twelve this
# rung's seed forks from -- and they are deliberately NOT matched against
# baseline/modnames.txt. That file holds the published artifact ids B-15 reads,
# and an artifact id is not a project name: `:api` publishing `arquillian-api`
# is the ordinary Gradle arrangement and a lineage that keeps the two spellings
# apart is not a lineage that lost the flag.
#
# THE FLAG IS READ OUT OF baseline/target.txt AND IS NOT A LITERAL HERE, the
# same discipline C-1 applies to the class-file level.
#
# WHETHER THE JVM ACCEPTS IT IS NOT ASKED HERE, and is not unasked. A flag the
# JVM refuses kills the fork, the fork writes no report, and family A charges
# for every test that did not run. Re-checking it here would charge one mistake
# twice.
c5=0
c5flag=$(tv 'target\.co\.flag')
c5floor=$(tv 'target\.co\.projects')
fact "gate.c5.flag=${c5flag:-absent}"
fact "gate.c5.projects.floor=${c5floor:-0}"
if [ -n "${c5flag}" ] && [ "${c5floor:-0}" -gt 0 ]; then
cat > "${WORK}/jvmargs.gradle" <<'ARQ_JVMARGS_GRADLE'
// Ask every Test task what it would hand its fork. Registered in
// projectsEvaluated so that a build which configures its Test tasks late is
// still answered, and guarded at every read because a task type this build has
// customised may refuse one of the two accessors.
gradle.projectsEvaluated {
   rootProject.allprojects { p ->
      p.tasks.register('arqTestJvmArgs') {
         doLast {
            p.tasks.withType(Test).each { t ->
               def explicit = ''
               def all = ''
               try { explicit = t.jvmArgs.join(' ') } catch (Throwable ignored) { }
               try { all = t.allJvmArgs.join(' ') } catch (Throwable ignored) { }
               println "ARQ_TESTJVM ${p.name} ${p.path}:${t.name} ${explicit} ${all}"
            }
         }
      }
   }
}
ARQ_JVMARGS_GRADLE
   co_limit="${JVMARGS_LIMIT:-600}"
   co_rc=0
   co_t0=${SECONDS}
   run_unprivileged env -C "${TREE}" timeout -s KILL "${co_limit}" \
      ./gradlew --no-daemon -q --init-script "${WORK}/jvmargs.gradle" arqTestJvmArgs \
      > "${WORK}/jvmargs.log" 2>&1 || co_rc=$?
   co_elapsed=$(( SECONDS - co_t0 ))
   co_state=ok
   if [ "${co_rc}" -eq 137 ] || [ "${co_rc}" -eq 124 ]; then
      co_state=timeout
   elif [ "${co_rc}" -ne 0 ]; then
      co_state=failed
   fi
   fact "gate.c5.probe=${co_state},seconds=${co_elapsed},limit=${co_limit}"
   # The rows decide, not the exit status: a reactor with one broken project
   # still prints the other eleven, and a probe that read `failed` as zero would
   # throw them away.
   grep '^ARQ_TESTJVM ' "${WORK}/jvmargs.log" 2>/dev/null \
      > "${WORK}/c5-rows.txt" || : > "${WORK}/c5-rows.txt"
   fact "gate.c5.tasks=$(grep -c . "${WORK}/c5-rows.txt" || true)"
   if [ "${co_state}" != ok ]; then
      echo "    (arqTestJvmArgs exited ${co_rc} -- the rows it printed decide, not the exit code)"
      tail -12 "${WORK}/jvmargs.log" | sed 's/^/    /'
   fi
   # THE POPULATION IS THE PROJECTS THAT FORK, NOT A LIST OF NAMES. The rows
   # are keyed on the project PATH the task belongs to and nothing is compared
   # against a published artifact id: a Gradle project's name is a directory
   # name, an artifact id is what the publication calls it, and the two are
   # free to differ -- `:api` publishing `arquillian-api` is the ordinary case,
   # not a defect. A tally that matched artifact ids against project names read
   # a tree with the flag on all twelve forks as a tree with none.
   #
   # The denominator is the larger of the projects seen and the twelve that
   # fork a test JVM in this rung's seed, so that a tree which deleted its Test
   # tasks scores 0 out of twelve rather than 1 out of the empty set it left.
   awk -v flag="${c5flag}" '
      { pp = $3; sub(/:[^:]*$/, "", pp); if (pp == "") pp = ":"
        seen[pp]++
        if (index($0, flag) > 0) hit[pp]++ }
      END { ok = 0; np = 0
            for (p in seen) { np++; if (hit[p] + 0 == seen[p]) ok++ }
            tt = 0; for (p in seen) tt += seen[p]
            hh = 0; for (p in hit)  hh += hit[p]
            print ok, np, tt, hh }' \
      "${WORK}/c5-rows.txt" > "${WORK}/c5-tally.txt"
   c5n=$(awk '{ print $1 + 0 }' "${WORK}/c5-tally.txt")
   c5p=$(awk '{ print $2 + 0 }' "${WORK}/c5-tally.txt")
   c5t=$(awk '{ print $3 + 0 }' "${WORK}/c5-tally.txt")
   c5h=$(awk '{ print $4 + 0 }' "${WORK}/c5-tally.txt")
   c5w=$(awk -v a="${c5p}" -v b="${c5floor:-0}" 'BEGIN { print (a + 0 > b + 0) ? a + 0 : b + 0 }')
   fact "gate.c5.projects=${c5n}"
   fact "gate.c5.projects.seen=${c5p}"
   fact "gate.c5.projects.want=${c5w}"
   fact "gate.c5.forks=${c5t}"
   fact "gate.c5.forks.flagged=${c5h}"
   fact "gate.c5.projects.was=$(tv 'target\.co\.was')"
   awk -v flag="${c5flag}" -v floor="${c5floor:-0}" '
      { pp = $3; sub(/:[^:]*$/, "", pp); if (pp == "") pp = ":"
        seen[pp]++
        if (index($0, flag) > 0) hit[pp]++ }
      END { np = 0
            for (p in seen) {
               np++
               if (hit[p] + 0 == 0)
                  print "    every test JVM this project forks is started without " flag ": " p
               else if (hit[p] + 0 < seen[p])
                  print "    " (seen[p] - hit[p]) " of " seen[p] " test JVMs this project forks are started without " flag ": " p }
            if (np < floor + 0)
               print "    " (floor - np) " of the " floor " projects that fork a test JVM in this seed fork none here" }' \
      "${WORK}/c5-rows.txt" | LC_ALL=C sort | head -6
   c5=$(clamp01 "$(frac "${c5n}" "${c5w}")")
else
   echo "    C-5: no flag or no project population to measure -> C-5 = 0"
fi
fact "C5.compact-headers=${c5}"

# ------------------------------------------------------- the family C score
#
# 0.02 / 0.38 / 0.33 / 0.25 / 0.02, and the split is by how much of each leg a
# reader of this library has to supply that a rewriting tool cannot.
#
# C-1 IS 0.02 AND IT IS THE ONE LINE OF BUILD SCRIPT. Step 09 priced its level
# flip at a tenth after measuring that the platform's own migrator reached it
# outright; steps 10 and 11 cut it to 0.04 on two further rungs of evidence,
# and this is the fifth. Nothing about 66 -> 69 makes that constant harder to
# move than 61 -> 65 was, and the minor-version reading this rung adds to the
# leg does not make it harder either: it is a reason a tree can LOSE the 0.02,
# not extra work to earn it. What took it from 0.04 to 0.02 is not new evidence
# about the flip; it is that this rung has a SECOND one-line build-script leg
# and the do-nothing budget in the header is shared between them.
#
# C-2 IS THE LARGEST AND IT IS THE RUNG'S NAME. 171 files, 188 declarations,
# 495 lines subsumed -- the widest single edit in the ladder, and the one whose
# right answer is per-file. A tool can find every `import java.util.List;` in
# this tree in one pass; what it cannot do is decide, for each file, WHICH of
# the six modules that file's types come out of, and it cannot decide it by
# package prefix either, because `java.util.logging` is java.logging and
# `java.util.List` is java.base. The leg is scored as 171 set equalities for
# that reason, and the 151 files with nothing to do are scored too, because the
# blanket answer -- `import module java.base;` at the top of everything -- is
# exactly what a tool that has not made the decision produces.
#
# C-3 IS 0.33 FOR THE 43 THAT STAY, AND FOR NOTHING ELSE. Deleting the 452 is a
# consequence of C-2, not a second piece of work, and the departure half is here
# so that a tree which adds module imports and keeps all 495 single-type imports
# collects C-2 and NOT this leg. It does not stop that tree collecting C-2 --
# C-2 and C-3 are separate weighted terms and such a tree banks C-2's 0.38 in
# full; what the departure half does is withhold C-3's 0.33 from it, which is
# the larger of the two numbers and the whole point. An earlier draft of this
# comment said "cannot collect C-2 for free" and was wrong about which leg the
# half protects. The 43 are the leg. 25 of them the
# compiler teaches -- `Annotation` is ambiguous the moment `java.base` is
# imported as a module, and a tree that removes the explicit import does not
# build -- but the other 18 it does not: a static import and a nested-type
# import are both invisible to a module import, both compile perfectly well
# whether they are there or not, and both are DELETED by any rule that reads
# "the module import replaces these". Those 18 are why this leg is priced above
# C-4 and below C-2.
#
# C-4 IS 0.25 FOR ONE EDIT AND SEVEN REFUSALS, which is the same shape step 11
# priced its VarHandle leg at and for the same reason: the site count is tiny
# and the judgement is the whole of it. `ScopedValue` compiles for all eight
# fields. Seven of them are wrong -- one is mutated in place after binding, one
# is read across an SPI boundary, one exists to be restored AFTER its extent
# ends, one is a public static the tree does not own the readers of, and three
# are written in one framework-driven call and read in another with no call
# enclosing both -- and nothing in the language says so. A tree that converts all eight compiles,
# passes its own tests where the difference is not exercised, and is wrong.
#
# C-5 IS 0.02 AND IS ONE FLAG. It is here for coverage rather than for
# difficulty: three of this rung's four other legs read source and the fourth
# reads class files, and a step called "Java 25" that never asks what the JVM
# was actually started with would be measuring half the release. 0.02 is what
# that coverage is worth -- and 0.02 rather than 0.04 because this leg and C-1
# are the two a do-nothing tree reaches with one build-script line each, and
# the header's 0.047619 ceiling is on their SUM. Priced at 0.04 apiece they
# summed to 0.517 and cleared item 6's bar between them while the tree had done
# none of the rung's work.
#
# WHAT A REGEX CANNOT DO AND WHAT A CATALOGUE CANNOT DO ARE TWO DIFFERENT
# LINES, AND THIS RUNG SITS ON THE SECOND ONE HARDER THAN ANY BEFORE IT. No
# regex passes any of the five. The catalogue is the live question: an
# `import module` rule and a `ScopedValue` rule are both plausible shelf items,
# and if either exists and works, this family is cheap. The weights above are
# written BEFORE that measurement and the pre-commitment is one-directional --
# a measured automigrator share above the ~30% bar moves these five WEIGHTS and
# never the bar. Item 4.3's `auto` arm is where the reading is taken and
# calibrate-step12.sh is what takes it.
#
# NO ANTI-GAMING MULTIPLIER, for the reason the family banner gives: every leg
# here is a min() over a frozen population, and C-2's and C-4's halves already
# charge the two blanket edits a multiplier would be aimed at.
#
# On one line, and staying on one line, for the reason every other family's
# weight expression is: the Dockerfile greps the printf.
c_score=$(awk -v a="${c1}" -v b="${c2}" -v c="${c3}" -v d="${c4}" -v e="${c5}" 'BEGIN {
   printf "%.6f", 0.02*a + 0.38*b + 0.33*c + 0.25*d + 0.02*e
}')
fact "familyC=${c_score}"
# ------------------------------------------------------------------ score
#
# Family B's EIGHTEEN weights are step 11's seventeen, scaled by 0.88 so that
# they keep their proportions to each other, plus 0.12 for the step-11 gate --
# the same construction step 11 used on step 10's sixteen, step 10 on step 09's
# fifteen, step 09 on step 08's fourteen, step 08 on step 07's thirteen, step 07
# on step 06's twelve, step 06 on step 05's eleven, step 05 on step 04's ten,
# step 04 on step 03's nine, and step 03 on step 02's:
#
#   B1 0.0245205357821952  B2 0.0122602678910976
#   B3 0.0490410715643904  B4 0.0490410715643904  (0.1348629468020736)
#   B5 0.0408675596369920  B6 0.0490410715643904
#   B7 0.0245205357821952                         (0.1144291669835776)
#   B8 0.0408675596369920  B9 0.0245205357821952  (0.0653880954191872)
#   B10 0.0449543156006912                        (0.0449543156006912)
#   B11 0.0490410715643904                        (0.0490410715643904)
#   B12 0.0557284904140800                        (0.0557284904140800)
#   B13 0.0633278300160000                        (0.0633278300160000)
#   B14 0.0719634432000000                        (0.0719634432000000)
#   B15 0.0817766400000000                        (0.0817766400000000)
#   B16 0.0929280000000000                        (0.0929280000000000)
#   B17 0.1056000000000000                        (0.1056000000000000)
#   B18 0.1200000000000000                        (0.1200000000000000)
#
# SIXTEEN DECIMAL PLACES, which is the figure step 11's own weight block
# predicted this rung would need. Step 11's seventeen were exact at fourteen
# because step 10's sixteen were exact at twelve; multiplying by 0.88 again
# moves them two more places:
# 0.02786424520704 -> 0.0245205357821952, 0.01393212260352 -> 0.0122602678910976,
# 0.05572849041408 -> 0.0490410715643904, 0.04644040867840 -> 0.0408675596369920,
# 0.05108444954624 -> 0.0449543156006912, 0.06332783001600 -> 0.0557284904140800,
# 0.07196344320000 -> 0.0633278300160000, 0.08177664000000 -> 0.0719634432000000,
# 0.09292800000000 -> 0.0817766400000000, 0.10560000000000 -> 0.0929280000000000,
# 0.12000000000000 -> 0.1056000000000000.
# Every one is exact at sixteen places, so the eighteen sum to
# 1.0000000000000000 with nothing left over and no weight has to absorb a
# remainder. Step 13 would need eighteen places, which is still inside what awk
# prints.
#
# THE REPORT ROWS PRINT SIXTEEN DECIMALS TOO. Not cosmetic: the Dockerfile
# pulls the weights out of the printed rows and out of this expression and
# compares the two sequences positionally, so a row that printed `x0.02452054`
# against a coefficient of `0.0245205357821952` would be a mismatch -- and
# rounding the row to make the check pass would be a report that describes a
# weighting the scorer does not use.
#
# 0.12 for B-18 rather than a share proportional to what it holds, for the
# reason B-10 through B-17 each took 0.12 in their turn: a regression gate is a
# floor and not a requirement, a tree that does nothing at all to step 11's
# work collects it in full, and the floor arithmetic below depends on exactly
# that.
#
# On one line, and staying on one line. The Dockerfile pulls each family's
# weights out of its own `printf "%.6f"` expression and compares them, in
# order, with the weights the report prints; a continuation splits the
# expression across two lines, the grep finds the half without the printf, and
# the check reports "0 lines look like its weight expression" instead of
# comparing anything.
b_score=$(awk -v b1="${b1}" -v b2="${b2}" -v b3="${b3}" -v b4="${b4}" \
              -v b5="${b5}" -v b6="${b6}" -v b7="${b7}" -v b8="${b8}" \
              -v b9="${b9}" -v b10="${b10}" -v b11="${b11}" -v b12="${b12}" \
              -v b13="${b13}" -v b14="${b14}" -v b15="${b15}" -v b16="${b16}" \
              -v b17="${b17}" -v b18="${b18}" 'BEGIN {
   printf "%.6f", 0.0245205357821952*b1 + 0.0122602678910976*b2 + 0.0490410715643904*b3 + 0.0490410715643904*b4 + 0.0408675596369920*b5 + 0.0490410715643904*b6 + 0.0245205357821952*b7 + 0.0408675596369920*b8 + 0.0245205357821952*b9 + 0.0449543156006912*b10 + 0.0490410715643904*b11 + 0.0557284904140800*b12 + 0.0633278300160000*b13 + 0.0719634432000000*b14 + 0.0817766400000000*b15 + 0.0929280000000000*b16 + 0.1056000000000000*b17 + 0.1200000000000000*b18
}')
fact "familyB=${b_score}"

# A and the migrated-ness half, 0.30 / 0.70; inside that half, a quarter for
# holding what steps 01 to 11 won and three quarters for doing step 12. The
# same shape as the ten rungs below, and at this rung for the same reason the
# nine below step 11 had, which is worth saying because step 11 was the one
# exception and this file inherits its machinery.
#
# STEP 11 WAS REQUIRED TO CHANGE A SHIPPED MODULE'S ARTIFACT INVENTORY AND THIS
# RUNG IS NOT -- BUT ONLY JUST, AND THE DIFFERENCE WAS MEASURED. Step 11's seven
# `SecurityActions` classes lost their anonymous `PrivilegedAction`
# implementations, 36 of family A's 939 transcripts read a jar's entry list or a
# class's nest, and those 36 had to be re-recorded FROM THE MIGRATED TREE --
# the one departure from rule 5 on this ladder, NOTES 16.17. THIS rung also
# deletes a class file: C-4 converts `ManagerImpl.handledThrowables` to a
# ScopedValue and the anonymous `ThreadLocal` subclass that field declared,
# `ManagerImpl$1`, stops being emitted, so arquillian-impl-base goes from 436
# class files to 435. It changes no transcript, because the 36 packaging
# transcripts enumerate arquillian-protocol.jar and no recording in this era
# enumerates impl-base's -- and that is a measurement and not an argument:
# step 11's verifier, run unchanged over this rung's golden, reads
# behavior.expected=939 produced=939 match=939. NOTES 16.61.
#
# THE FLOOR IS EXACT RATHER THAN ESTIMATED, AND IT IS BACK TO 0.475 -- THE
# TIGHTEST VALUE IT CAN TAKE. Two different trees sit at that number and the
# distinction is load-bearing here. The `floor` ARM scores the GOLDEN chain's
# step-11 output. This rung's INPUT -- the tree the candidate is actually handed
# -- is step 11's BLIND snapshot, and the two are NOT the same tree: they part
# on six import lines, which is the whole reason C-3 carries an `excused`
# mechanism on its depart leg. Without that excuse the arm read 0.477230 rather
# than 0.475000, because C-3 was paying the golden chain for six deletions it
# never made (NOTES 16.74). The argument below is about the INPUT, because the
# INPUT is what a do-nothing candidate scores; the arm is the check on it.
# Step 11's verifier scores the tree this rung hands its candidate at
# reward=1.000000: family A 1.000000, all seventeen of its gates 1.000000, its
# own family C 1.000000. Every one of those seventeen is a gate of this file's
# eighteen, and the eighteenth -- B-18 -- IS step 11's C-2, C-3 and C-4, which
# read 1.000000 there too. B-4 holds `was=66` and the seed's class files are at
# major 66. So on the tree the candidate is handed
#
#   family A = 1.000000   family B = 1.000000   family C = 0.000000
#
# and the reward is
#
#   0.30*1.000000 + 0.70*(0.25*1.000000 + 0.75*0.0) = 0.30 + 0.175 = 0.475
#
# exactly -- against migration_prompt item 6's requirement of 0.5 or under. The
# margin is 0.025, which is the NARROWEST on this ladder and is narrow for a
# good reason: every rung whose floor was lower had something in its own
# family B still reading short on its own input, which is a defect of the
# inheritance and not a safety margin. Step 11's 0.463128 was B-6 charging its
# input tree for 42 signatures its own family C was about to remove. Nothing
# here is charged for work it has not done, so the floor is the arithmetic
# minimum a rung of this shape can have and there is no slack anywhere else to
# find.
#
# THAT MARGIN IS WHY EVERY LEG OF FAMILY C HAS TO READ A HARD ZERO ON THE INPUT
# TREE, and each of the five was measured there rather than argued from the
# requirements. The input report is baseline/source.txt's own run -- the same
# gates/scan.sh and gates/ast.sh this file runs, over the seed:
#
#   C-1  the class files are at major 66 and minor 0. `majorminor.69.0` is
#        absent from the Scan report, so c1have reads 0 of 436 -- and the level
#        is the must-flip, so this is the leg that cannot read anything but 0 on
#        an untouched tree by construction. `preview.classes` reads 0 as well,
#        which is the right answer for the wrong reason: a Java 22 tree built
#        without --enable-preview has no preview class files either.
#   C-2  `modimport.size=0`. Not one `import module` declaration exists in the
#        tree, so the set-equality half reads 0 of 171. The no-spurious half
#        reads 151 of 151 -- every file that should import no JDK type still
#        imports none -- and min() takes the 0. THIS IS THE LEG WHERE THE
#        SECOND HALF READING 1.0 ON THE INPUT TREE IS THE POINT: it is a hold,
#        and a hold is satisfied by not breaking something.
#   C-3  `jdkimport.size=495` and every one of the 495 is still exactly where
#        jdkimport.txt froze it, so the departure half reads 0 of 452. The hold
#        half reads 43 of 43 -- 16 static, 2 nested, 25 `Annotation` -- and
#        min() takes the 0.
#   C-4  `scopedvalue.size=0`. Not one `ScopedValue` appears anywhere in the
#        tree, so the arrival half reads 0 of 1 and the departure half reads 0
#        of 1. The hold half reads 7 of 7: `threadlocal.decls=8` and all eight
#        are still ThreadLocal declarations, which includes the seven that must
#        stay. min() takes the 0, twice over.
#   C-5  the twelve test tasks fork a JVM with `-Xmx512m -Dfile.encoding=UTF-8
#        -Duser.country=US -Duser.language=en -Duser.variant -ea` and a display
#        name generator, and `-XX:+UseCompactObjectHeaders` is in none of the
#        twelve argument lists, so the leg reads 0 of 12.
#
# THERE IS NO ANTI-GAMING MULTIPLIER ON THIS FAMILY. Step 08's three terms read
# 1.0 on the input tree -- no raw types added, no suppressions added, no raw
# toArray -- so any weight w they took INSIDE family C would add w straight to
# the floor, and at a margin of 0.025 that is the one place on this ladder where
# it could not be afforded even if it were otherwise a good idea. It is not:
# they are inside B-14, multiplying the gate that holds step 07's tree, and a
# term that pays a tree for a property it arrived with is a term that measures
# nothing. This family does its anti-gaming in the legs themselves instead. C-2
# is a min() of a set equality and a no-spurious-arrivals half, C-3 is a min()
# of a departure and a hold, C-4 is a min() of a departure, an arrival and a
# hold, so deleting the code scores nothing and adding a construct beside it
# scores nothing. C-5 counts twelve forks and not one build file. Addition earns
# nothing on any of the five.
# PRODUCT SCORING (2026-10-13): reward = T1 x T2, replacing the weighted sum
# 0.30*A + 0.70*(0.25*B + 0.75*C). T1, migrated-ness, is family C -- this
# rung's own work, already in [0,1]. T2, preservation, is family A (the
# recorded behaviour) with family B (every earlier rung, held), at their old
# relative weights 0.30 : 0.175, renormalised by 0.475. A tree that does none
# of this rung's work reads familyC = 0 and scores 0 however much it
# preserves, which supersedes the floor arithmetic above. Every hard zero stays
# one: a build that does not build zeroes family A, and any multiplier inside
# family C still multiplies.
pres=$(awk -v a="${a_score}" -v b="${b_score}" 'BEGIN { printf "%.6f", (0.30*a + 0.175*b) / 0.475 }')
mig="${c_score}"
final=$(awk -v t1="${mig}" -v t2="${pres}" 'BEGIN { printf "%.6f", t1 * t2 }')
fact "migrated=${mig}"
fact "preserved=${pres}"
fact "reward=${final}"

echo
echo "=== score ==="
printf '  family A (behaviour)            %s  T2 0.30/0.475\n' "${a_score}"
printf '  family B (steps 01-11 held)     %s  T2 0.175/0.475\n' "${b_score}"
printf '    B1 no-maven          %s  x0.0245205357821952\n' "${b1}"
printf '    B2 wrapper           %s  x0.0122602678910976\n' "${b2}"
printf '    B3 published         %s  x0.0490410715643904\n' "${b3}"
printf '    B4 one major, >=%-3s  %s  x0.0490410715643904\n' "${HOLD_MAJOR}" "${b4}"
printf '    B5 tests present     %s  x0.0408675596369920\n' "${b5}"
printf '    B6 api surface       %s  x0.0490410715643904\n' "${b6}"
printf '    B7 tests run         %s  x0.0245205357821952\n' "${b7}"
printf '    B8 jupiter held      %s  x0.0408675596369920\n' "${b8}"
printf '    B9 junit4 retired    %s  x0.0245205357821952\n' "${b9}"
printf '    B10 java 7 held      %s  x0.0449543156006912\n' "${b10}"
printf '    B11 java 8 lang held %s  x0.0490410715643904\n' "${b11}"
printf '    B12 java 8 lib held  %s  x0.0557284904140800\n' "${b12}"
printf '    B13 java 9-10 held   %s  x0.0633278300160000\n' "${b13}"
printf '    B14 java 11 held     %s  x0.0719634432000000  (penalty %s)\n' "${b14}" "${b14pen}"
printf '    B15 jpms + services  %s  x0.0817766400000000\n' "${b15}"
printf '    B16 java 17 held     %s  x0.0929280000000000\n' "${b16}"
printf '    B17 java 21 held     %s  x0.1056000000000000\n' "${b17}"
printf '    B18 java 22 held     %s  x0.1200000000000000\n' "${b18}"
printf '  family C (java 25)              %s  T1\n' "${c_score}"
printf '    C1 level %-3s minor %-5s %s  x0.02  (%s of %s class files, preview %s)\n' "${WANT_MAJOR}" "${WANT_MINOR}" "${c1}" "${c1have}" "${c1cls}" "${c1prev}"
printf '    C2 module imports       %s  x0.38  (sets %s of %s, clean %s of %s)\n' "${c2}" "${c2ok}" "${c2w}" "${c2cl}" "${c2nn}"
printf '    C3 imports subsumed     %s  x0.33  (departed %s of %s, held %s of %s)\n' "${c3}" "${c3depn}" "${c3depw}" "${c3holdn}" "${c3holdw}"
printf '    C4 scoped value         %s  x0.25  (departed %s, arrived %s, held %s of %s)\n' "${c4}" "${c4depn}" "${c4arrn}" "${c4holdn}" "${c4holdw}"
printf '    C5 compact headers      %s  x0.02  (%s of %s projects, %s of %s forks)\n' "${c5}" "${c5n}" "${c5w}" "${c5h}" "${c5t}"
printf '  reward                          %s\n' "${final}"

cp "${FACTS}" /logs/verifier/facts.txt 2>/dev/null || true
report "${final}"
