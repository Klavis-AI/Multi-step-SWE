#!/usr/bin/env bash
# Step 05 -- 05-java8-lib. Scored per authoring/checklists/step05.md.
# SCORED AS A PRODUCT SINCE 2026-10-13: reward = T1 x T2, where T1 is this
# step's own migrated-ness family and T2 the preservation families; the
# weights named below are now relative weights inside those two factors, and
# any floor arithmetic below is superseded. The formula is at the foot.
#
# Three weights, two families (rule 5):
#
#   A  0.30  behaviour equivalence -- the same 935 recorded fixtures, replayed
#            against the artifacts the candidate's build publishes. A library
#            migration must not change what the code does, and it is the whole
#            reason two of this step's requirements are stated the way they are:
#            DynamicServiceLoader instantiates services in insertion order, and
#            that order is IN the recordings.
#   B  0.70 x 0.25  everything steps 01 to 04 established, re-measured. Seven
#            gates inherited from step 01, two that hold step 02's JUnit 5
#            migration in place, one, B-10, that holds step 03's three Java 7
#            constructs, and one, B-11, that holds step 04's three Java 8
#            language constructs.
#   C  0.70 x 0.75  the tree uses the Java 8 LIBRARY -- Optional where the
#            null-returning lookup helpers were, Collectors where step 04 left
#            accumulate sites standing, java.util.Objects in the value types'
#            equals/hashCode, java.time where the epoch-millis calls were --
#            read off the typed AST by arqgate.SourceScan, with the published
#            jars' own constant pools as an independent second channel.
#
# THIS RUNG DOES NOT MOVE THE CLASS-FILE VERSION, and that is the one structural
# difference between this verifier and the four below it. Step 04 took the tree
# to major 52 and the Java 8 library needs no further level, so baseline/
# level.txt reads 52 here as it did there: B-4 is an INHERITED gate at this step
# rather than the must-flip, and C-5 stands in its place. Every step of this
# ladder has exactly one check that the predecessor tree cannot pass by standing
# still; at this step it is not a number in a class file header, it is whether
# java/util/Optional, java/util/stream/Collectors, java/util/Objects and
# java/time appear in the constant pools of the jars the build publishes. They
# read 0, 0, 0 and 0 on the tree this step is handed -- measured, on the step-04
# golden, with 24 method references already in it, so javac's own
# Objects.requireNonNull desugaring is not quietly supplying one of them.
#
# Why family B is eleven gates and not four inherited families. Step 02 carried
# its predecessor forward as family B and scored its own work as family C; done
# again at every rung, step 13's verifier would carry twelve families. What has
# to hold from step 02 is that the suite is still a Jupiter suite and still free
# of JUnit 4 -- B-8 and B-9. What has to hold from step 03 is B-10, and from
# step 04 B-11, each one gate of three sub-gates read against the rung's own
# frozen sets. Logged in NOTES 9.2 and 10.4.
#
# The ninth trap is what B-10 and B-11 are for. Every gate above them is
# step-local, and a step-local mean cannot notice a tree that earns this rung by
# undoing the one below it: writing Optional and Collectors while unwinding step
# 04's lambdas back into anonymous classes scores the whole of family C and
# regresses step 04 silently.
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
# The class-file major version this rung targets: 52, i.e. Java 8. Staged, so
# that B-4 and the instruction are talking about the same number by
# construction. A missing file must not silently become "0" and pass a tree
# with no class files at all, so it is validated on the spot.
WANT_MAJOR=$(sed -n 's/^major=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
case "${WANT_MAJOR:-}" in
   5[0-9]|6[0-9]) ;;
   *) echo "FATAL: baseline/level.txt names no plausible class-file major (${WANT_MAJOR:-missing})"
      report 0 ;;
esac
GCP="/tests/gates/build/arqgate.jar:/tests/gates/lib/asm-9.10.1.jar:/tests/gates/lib/asm-tree-9.10.1.jar"
WORK=/tmp/verify
rm -rf "${WORK}"; mkdir -p "${WORK}"

# The classpath the SCORER compiles the candidate's sources against, for the
# source gate that B-10 and the whole of family C read. Pinned to the staged
# shelf and never derived from the candidate's build file: a classpath a
# candidate can shorten is a compile a candidate can make fail, and family C
# reads a failed compile as a fact about the tree. 76 dependency jars plus the
# JUnit Platform the migrated tests need.
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

echo "=== step 05 verifier: $(date -u +%FT%TZ) ==="

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
echo "=== family A: behaviour equivalence (935 recorded fixtures, weight 0.30) ==="
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
      RUNNER_ERA=jdk8 \
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
# Five gates read the typed AST -- B-10, which holds step 03's three constructs
# in place, and all four of family C -- so the compiles that produce it happen
# once, here, rather than inside either family. Hoisted above family B and not
# left where step 03 had it, at the head of family C, because B-10 is a family-B
# gate and a block cannot read a report that has not been written yet.
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
# `s -> shout(s)` in code and the same nine characters in a javadoc differ by
# nothing a grep can see, `this::shout` is a method reference and `this.shout`
# is not, and a `for` that collects differs from one that does not by what its
# body does rather than by how it is spelled.
#
# Two instruments, not one. The AST counters say what the tree CONTAINS; javac's
# own acceptance of the tree at a named language level says what the tree
# REQUIRES, and C-4 is that second channel. This step's INPUT -- the step-03
# golden -- passes every "is it absent" half of every gate below trivially and
# compiles at -source 1.7; this tree must not.
echo
echo "--- the source AST: the scorer's own compiles (B-10 and family C read them)"

# The frozen expectations. baseline/source.txt is where the tree starts and
# baseline/target.txt is where it has to get to; both were measured with this
# same gate and both are staged, so no threshold is derived at grading time from
# a tree the candidate controls.
#
# source.txt is the step-04 GOLDEN, not the pristine tree: this rung's input is
# the last rung's output, and step 04's own source.txt would describe a tree
# that no longer exists by the time this step runs. It is also measured with the
# gate that SHIPS HERE and not with the one that shipped there -- step 04's
# SourceScan cannot see retnull, optret, optop, accum, collect, objects, jtime
# or millis at all, so baseline-step04-source.txt would be the wrong file in two
# independent ways.
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
# Family C's own populations and floors. Every one of them is a count of what
# THIS step asks for, measured on the golden with this same gate; the .floor
# figures are the fraction of the golden's own reach that reads full credit, and
# they exist for the reason step 04's target.level17.floor existed: a gate whose
# denominator is one correct tree's exact reach is scoring that tree's spelling.
tgt_optbuckets=$(tv 'target\.optbuckets')
tgt_sinks=$(tv 'target\.sinks')
tgt_valuetypes=$(tv 'target\.valuetypes')
tgt_timesites=$(tv 'target\.timesites')
c5_opt_floor=$(tv 'target\.c5\.optional\.floor')
c5_col_floor=$(tv 'target\.c5\.collectors\.floor')
c5_obj_floor=$(tv 'target\.c5\.objects\.floor')
c5_jt_floor=$(tv 'target\.c5\.jtime\.floor')

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
# while, do. C-3 asks whether 24 loops BECAME pipelines, and this is the only
# figure that can tell that from a tree where they changed spelling: see the
# no-reshape term in C-3 below.
fact "gate.src.base.loop=${base_loop:-0}"
fact "gate.src.base.nonanon=${base_nonanon}"
fact "gate.src.need.anonfn=${anonfn_need}"
# 0, and printed because C-2 has no departure half: every annotation this
# scorer finds is one this step's candidate added.
fact "gate.src.base.samannotated=${base_samann:-0}"

# The three compiles. -source 1.8 is the measurement AND half of C-4; -source
# 1.7 runs only if the first succeeded, so that a difference between them is a
# difference of language level and not of environment. The default-level compile
# is the fallback: a tree that overshot the level, or that does not build at
# 1.8 for some reason of its own, is still measured so that it is graded on the
# work it did rather than on nothing.
measured=
ok18=$(srcgate 18 1.8)
fact "gate.src.ok18=${ok18:-no}"
grep -E '^javac\.' "${WORK}/src-18.txt.env" 2>/dev/null | sed 's/^/    1.8: /'
ok17=no
okdef=no
if [ "${ok18:-no}" = yes ]; then
   measured="${WORK}/src-18.txt"
   ok17=$(srcgate 17 1.7)
   fact "gate.src.ok17=${ok17:-no}"
   grep -E '^javac\.' "${WORK}/src-17.txt.env" 2>/dev/null | sed 's/^/    1.7: /'
else
   # The tree does not compile at Java 8 with the scorer's classpath. That is
   # C-4 = 0 whatever else is true.
   echo "    the tree does not compile at -source 1.8; measuring at the default level"
   # The COMPILER's complaint, not the gate's .env. src-18.log holds both --
   # source-gate.sh tees the .env to stdout and tails the javac log to stderr,
   # and srcgate() catches both in one file -- so `head` of it prints twelve
   # lines of `files=322` and never the reason.
   if [ -s "${WORK}/src-18.txt.javac.log" ]; then
      grep -E ': error: |^[0-9]+ error' "${WORK}/src-18.txt.javac.log" \
         | head -12 | sed 's/^/    /'
   else
      sed -n '1,12p' "${WORK}/src-18.log" | sed 's/^/    /'
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
   sv() { sed -n "s/^$1=//p" "${measured}" | head -1; }
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
# Step 01's must-flip, held here. The label is worth getting right: B-4 is this
# step's must-flip and there is exactly one per rung, so a second gate carrying
# the word would say the scorer had two things it was really asking for.
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
# for the same reason: the step's requirement is that every class the build
# compiles is Java 8 bytecode and that only one major version is present, and
# until the suite has run, the test classes of nine of the twelve modules have
# not been compiled at all -- a majors.distinct read before this point is a
# count over a third of the tree. B-7 reads the
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

# B-4  the compiled bytecode is Java 8.                           (inherited)
#
# Inherited from step 01 in shape and, for the first time on this ladder, NOT
# retargeted: 50 was the answer while the tree was a Java 6 tree, 51 while it
# was a Java 7 tree, 52 once step 04 moved it, and 52 is still the answer. The
# Java 8 library runs on a Java 8 class file; there is no level above it to ask
# for, and inventing one would be asking for a migration this step is not.
#
# So this gate no longer separates this step's tree from its input, and nothing
# in family B does. That is not a hole, it is a relocation: C-5 is this step's
# must-flip, it reads the published jars rather than the source, and the floor
# arithmetic at the foot of this file is computed with family B reading 1.000000
# on the unmigrated tree rather than 0.86.
#
# It stays a gate, at full weight, for the reason every inherited gate stays
# one: a tree that raises the level back down, or leaves half its classes at 52
# and compiles the rest at 61 to reach a library method that is not on 8, has
# regressed the rung below and this is where that is seen.
#
# Everything the build compiled, not only what it published: the twelve module
# jars AND every class file the build left in the tree, which is where the
# compiled test classes are. Scanning the jars alone let a tree emit major 52
# for its main sources and 61 for its tests and still score this gate 1, while
# the step asks that every class the build compiles be major 52.
#
# The number is read from baseline/level.txt rather than written here, so that
# the rung this step targets is a staged expectation and not a literal buried in
# a conditional.
b4=0
if [ -s "${WORK}/modulejars.txt" ] || [ "${nclass}" -gt 0 ]; then
   if java -cp "${GCP}" arqgate.Scan "${WORK}/scan.txt" "${mirror}" \
        $(cat "${WORK}/modulejars.txt") > "${WORK}/scan.log" 2>&1; then
      cls=$(sed -n 's/^classes=//p' "${WORK}/scan.txt" | head -1)
      mwant=$(sed -n "s/^major\.${WANT_MAJOR}=//p" "${WORK}/scan.txt" | head -1)
      distinct=$(sed -n 's/^majors\.distinct=//p' "${WORK}/scan.txt" | head -1)
      fact "gate.classes=${cls:-0}"
      fact "gate.major.${WANT_MAJOR}=${mwant:-0}"
      fact "gate.majors.distinct=${distinct:-0}"
      grep '^major\.' "${WORK}/scan.txt" | sed 's/^/    /'
      # One major version, and it is 52. A tree that is half-migrated shows two.
      if [ "${distinct:-0}" = 1 ]; then
         b4=$(frac "${mwant:-0}" "${cls:-0}")
      else
         echo "    more than one class-file major version present -> 0"
      fi
   else
      fact "gate.scan=broken"
      sed -n '1,10p' "${WORK}/scan.log" | sed 's/^/    /'
   fi
else
   fact "gate.scan=no-classes"
fi
fact "B4.major${WANT_MAJOR}=${b4}"

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
b6=0
if [ -s "${WORK}/modulejars.txt" ]; then
   if bash /tests/gates/apisig.sh "${WORK}/api.txt" $(cat "${WORK}/modulejars.txt") \
        > "${WORK}/api.log" 2>&1; then
      LC_ALL=C sort /tests/baseline/api.txt > "${WORK}/want-api.txt"
      LC_ALL=C sort "${WORK}/api.txt"       > "${WORK}/got-api.txt"
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
# here that question belongs to B-4 at 52, where it is a must-flip. NOTES 8.5.
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
# Two of step 03's figures move across this step for an honest reason, and
# getting that wrong is the way this gate fails a correct tree rather than a
# regressing one. `newgeneric.explicit` falls 50 -> 36 on the golden, because 14
# of the explicit-generic sites sit on anonymous classes and an anonymous class
# cannot become a lambda and keep its type arguments; `explicitanon` falls
# 27 -> 13 by the same 14. So the quantity read here is the NON-anonymous
# remainder, explicit - explicitanon, which is 23 at both ends. A gate reading
# raw `explicit` would score the golden 0.72 on this third for doing the step
# correctly.
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
   # opposite ends. AT THIS STEP THEY DIVERGE, 74 against 93, and the divergence
   # is caused by this step's own C-2:
   #
   #   19 of the 93 diamonds were the SINK CONSTRUCTIONS of the 23 accumulate
   #   buckets -- `List<X> xs = new ArrayList<>();` followed by a terminal
   #   forEach(xs::add) -- and `collect(Collectors.toList())` constructs nothing
   #   for a diamond to sit on. Measured, per (file, type), on the golden:
   #   93 - 19 = 74, and the 19 are exactly sink declarations.
   #
   # So target.diamond is 74 and target.b10.diamond stays 93, and that is the
   # ninth trap answered rather than walked into: a gate reading 74/93 would
   # score the golden 0.796 on this third for doing THIS step correctly, and a
   # departure half re-based on 74 would stop noticing the 19 explicit type
   # arguments step 03 removed from sites that are still there. The departure
   # half is not affected by C-2 at all -- it reads explicit - explicitanon,
   # which C-2 does not touch, and the golden measures 36 - 13 = 23, the same 23
   # step 04 ended on, so dia_back is 0 on a correct tree.
   #
   # The four buckets whose sink SURVIVES as a diamond are the early-return
   # guards C-2's rewrite left behind -- `if (clazz == null) return new
   # ArrayList<>();` in Reflections x3 and ContainerDefImpl#getDependencies. A
   # candidate who spells those `Collections.emptyList()` instead reads 70/74
   # here, which costs 0.0004 of the step's reward; that is the accepted price
   # of a denominator that is one tree's reach, and it is stated in
   # baseline/target.txt beside the number.
   #
   # authoring/verify-baseline-step05.sh asserts 74 + 19 = 93 against the two
   # measured reports rather than leaving the pair to drift apart unnoticed --
   # and, because that sum is invariant under losing a diamond ANYWHERE, also
   # asserts that none arrived and that every one of the 19 that left belonged
   # to a file holding a C-2 sink.
   b10d=$(minf "$(clamp01 "$(frac "${now_dia:-0}" "${tgt_dia:-0}")")" \
               "$(clamp01 "$(frac "$(( ${b10_dia:-0} - dia_back ))" "${b10_dia:-0}")")")

   # The try-with-resources third. 11 hand-written close() calls became 9
   # resource clauses -- two of the 11 acquisitions share a try -- so the
   # arrival half divides by the 9 clauses and the departure half by the 11
   # closes. A close that came back is a resource this tree leaks again.
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

   # The proviso, read the same way step 03 read it. Four of the 935 recorded
   # transcripts are the behaviours the file-API migration must not change --
   # Files.newOutputStream on a directory throws where new FileOutputStream
   # throws something else, File.createTempFile rejects prefixes Files
   # .createTempFile accepts. Family A weighs all four at 4/935, which is 0.0013
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
# also charge THIS step's C-1 for the one thing it legitimately does to that
# counter -- an Optional-returning helper whose callers stop needing a null
# check does not add an anonymous class, but a candidate who writes
# `Optional.ofNullable(x).orElseGet(() -> ...)` writes a lambda, and a gate
# reading the total both ways would have to decide which direction was honest.
#
# THE THIRD SUB-GATE AND THIS STEP'S C-2 READ THE SAME BUCKETS FROM OPPOSITE
# ENDS, deliberately, and this is the ninth trap wearing its other mask -- a
# regression gate that goes RED on a tree for doing the current step correctly.
# Step 04 left 24 `src.forEach(sink::add)` accumulate sites standing inside the
# pipelines it built (NOTES 10.2), and this step's C-2 converts 23 of them to
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

# =========================================================== family C
#
# The migration this step actually asks for: the tree starts using the library
# Java 8 shipped. Four constructs read off the typed AST that was compiled above
# (rule 11) and never out of source text -- `Optional` in a javadoc and
# `Optional` in a return type differ by nothing a grep can see -- and one read
# off the constant pools of the jars the build published, which is a different
# instrument asking a different question.
#
# EVERY CONSTRUCT GATE IS SCORED OVER A FROZEN SET OF BUCKETS, never over a
# count, and at this step that is not a refinement, it is the only thing that
# makes the gates mean anything. Step 04 could lean on counts for part of its
# work because `foreach.collect` falling from 24 to 0 is a fact about 24
# specific loops. Nothing here is like that. `retnull.size` falling from 42 to
# 26 is satisfied by deleting sixteen `return null;` statements and letting the
# methods fall off the end of a void body; `optret.size` rising from 0 to 16 is
# satisfied by sixteen new methods in a file of one's own. So each gate names
# the <file>#<method> buckets that must change, and asks each bucket the
# departure question and the arrival question separately:
#
#   baseline/optbuckets.txt    16 private null-returning lookup helpers
#   baseline/sinks.txt         23 src/main accumulate buckets, with the
#                              Collectors factory each one requires
#   baseline/valuetypes.txt     3 classes that hand-roll equals + hashCode
#   baseline/timesites.txt      4 buckets holding the 5 epoch-millis calls
#
# environment_mode is "separate", so /tests is not a directory the candidate's
# container can read, and those four files name sites by path.
#
# THE MUST-FLIP IS C-5 AND IT IS NOT AN AST COUNTER. B-4 does not move at this
# rung -- major 52 in, major 52 out -- so nothing in family B separates this
# step's tree from its input, and a family C built only out of SourceScan
# records would be one instrument's opinion, unchecked. C-5 asks the published
# jars whether java/util/Optional, java/util/stream/Collectors,
# java/util/Objects and java/time are in their constant pools. That is a
# question about code that SHIPS: a tree can carry Optional in a source file the
# build never compiles, or in a module it never publishes, and every counter
# above would read full.
c1=0; c2=0; c3=0; c4=0; c5=0; cpen=1
echo
echo "=== family C: the Java 8 library migration (weight 0.70 x 0.75) ==="

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
   echo "  family C: no compile of this tree produced a report -> 0"
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

   # ---------------------------------------------------------------- C-1
   # Optional where the null-returning lookup helpers were.
   #
   # SIXTEEN BUCKETS AND NOT FORTY-TWO, and the sixteen are the PRIVATE ones.
   # retnull.size is 42 at the baseline; 22 of those sites sit in methods
   # apisig.sh prints, and B-6 is an EXACT set -- want=1147 hit=1147 extra=0,
   # where a widened surface is charged exactly as a narrowed one. Changing a
   # published `T foo()` to `Optional<T> foo()` removes a line from `hit` and
   # adds one to `extra`, so an instruction that asked for it would be asking
   # the candidate to fail an inherited gate worth more than this one. Three
   # ways out were available -- confine the leg to non-published methods,
   # re-baseline B-6 here, or give B-6 an allowlist of permitted signature
   # moves -- and the first is taken, because the private population is large
   # enough to carry the gate on its own and the other two both spend the
   # property that B-6's expectation has been frozen once, at step 01, and read
   # unchanged at every class-file version since. NOTES 10.5.
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
   c1=$(minf "$(minf "$(clamp01 "$(frac "${opt_dep}" "${tgt_optbuckets:-0}")")" \
                     "$(clamp01 "$(frac "${opt_arr}" "${tgt_optbuckets:-0}")")")" \
             "${optcons}")

   # ---------------------------------------------------------------- C-2
   # Collectors where step 04 left the accumulate sites standing.
   #
   # Step 04 converted 24 collecting enhanced-for loops into stream pipelines
   # and, by design, left every one of them ending in `src.forEach(sink::add)`
   # over a sink the method had already constructed (NOTES 10.2). That shape is
   # a pipeline with its terminal operation written by hand, and converting it
   # is what this leg is. 23 of the 24 are in src/main and those 23 are the
   # population; the 24th is CDIInjectionEnricherTestCase#createDeployment, in
   # src/test, and family C grades the library this task publishes.
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
   # DynamicServiceLoader's instantiation order is IN the 935 recordings family
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
   c2=$(minf "$(clamp01 "$(frac "${sink_dep}" "${tgt_sinks:-0}")")" \
             "$(clamp01 "$(frac "${sink_fid:-0}" "${tgt_sinks:-0}")")")

   # ---------------------------------------------------------------- C-3
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
   # not, and this is the one place in family C where a method NAME is matched
   # rather than an element resolved -- both names come off the same resolved
   # java.util.Objects element, so what is being told apart is two members of
   # one class and not two classes that share a spelling.
   sed -n 's/^objects\[[0-9]*\]=//p' "${measured}" 2>/dev/null \
      | awk '{ split($1, f, ":"); m = ""
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
               if (m != "") print f[1] "#" m, $2 }' \
      | LC_ALL=C sort -u > "${WORK}/objects-in.txt"
   vt_want=$(grep -v '^#' /tests/baseline/valuetypes.txt | grep -c . || true)
   # Every other leg of family C names what it did not find -- C-1 prints
   # "still returns null", C-2 "still accumulates by hand" and "wrong
   # collector", C-4 "still reads the wall clock". This one printed nothing,
   # which made a C-3 of 0.000000 the one number in the step with no address
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
   c3=$(clamp01 "$(frac "${vt_hit:-0}" "${tgt_valuetypes:-0}")")

   # ---------------------------------------------------------------- C-4
   # java.time where the epoch-millis calls were.
   #
   # Five System.currentTimeMillis calls in four buckets, and they are the whole
   # legacy-time population of this tree: millis.size=5 with every one of them
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
   # min(departure, arrival), the same shape as C-1 and C-2 and for the same
   # reason: a tree that deletes the timing and a tree that adds an Instant
   # beside the millis each score one half and neither scores.
   #
   # WHAT IS NOT ASKED. TestResult.getStart() and getEnd() return long, they are
   # in B-6's frozen 1147-line set, and this step does not move them. The
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
   c4=$(minf "$(clamp01 "$(frac "${time_dep}" "${tgt_timesites:-0}")")" \
             "$(clamp01 "$(frac "${time_arr}" "${tgt_timesites:-0}")")")

   # The anti-gaming multiplier.
   #
   # Not a gate: a gate pays the unmigrated tree for a property it has for free,
   # and raw types at zero is a property this step's input already has. It is a
   # penalty on the migration score instead, and the floor arithmetic at the
   # foot of this file is why that distinction cannot be waved through -- family
   # B reads 1.000000 on the unmigrated tree at this rung, so the floor has less
   # room in it than at any step below and a term paying that tree 0.525*w would
   # spend what room is left.
   #
   # Carried forward from step 04 unchanged, against this step's own frozen
   # baseline. Both halves are live here for a new reason: `Optional` and
   # `Collectors.toList()` are exactly where an unchecked cast gets papered
   # over, and `(List<Foo>) stream.collect(toList())` with a @SuppressWarnings
   # over it is the shape this catches.
   raw_added=$(( ${now_raw:-0} - ${base_raw:-0} ))
   [ "${raw_added}" -lt 0 ] && raw_added=0
   fact "gate.src.raw.added=${raw_added}"
   if [ "${raw_added}" -gt 0 ]; then
      echo "    ${raw_added} raw type(s) added over the baseline's ${base_raw:-0} -> family C penalised"
      cpen=$(awk -v r="${raw_added}" 'BEGIN { v = 1 - r / 10; if (v < 0) v = 0; printf "%.6f", v }')
   fi
   if [ "${now_sup:-0}" -gt "${base_sup:-0}" ]; then
      echo "    ${now_sup} @SuppressWarnings site(s) against the baseline's ${base_sup} -> family C halved"
      cpen=$(awk -v p="${cpen}" 'BEGIN { printf "%.6f", p / 2 }')
   fi
   fact "gate.src.penalty=${cpen}"
fi

# ---------------------------------------------------------------- C-5
# The tree SHIPS the Java 8 library.                            (must-flip)
#
# Outside the block above on purpose. Every gate in it is one instrument's
# opinion -- arqgate.SourceScan reading javac's typed AST of a compile the
# SCORER ran, over whatever sources it found. This one asks a different
# instrument a different question of a different artifact: are the four package
# names in the constant pools of the jars the candidate's own BUILD published.
#
# It is this step's must-flip, and it is the only one, because B-4 does not move
# at this rung. On the tree this step is handed the four counts are 0, 0, 0 and
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
# golden reaches -- the same construction as step 04's C-4 level-error floor,
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
# classes to clear 5/6/2/2 -- clears C-5 in full while C-1..C-4 all read 0, and
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
# raw fraction), and so is the floor arm: its C-1..C-4 are already 0, so its
# family C was 0 before this change and is 0 after it.
#
# Binary grep over class files, which is what a constant-pool reference to
# java/util/Optional looks like on disk: the CONSTANT_Utf8 entry holding the
# class name is the literal byte string. Read out of the PUBLISHED jars, the
# same population apisig.sh reads, and not out of the tree's compiled output --
# a class that never reaches a jar is a class this project does not ship.
echo
echo "--- C-5: the published jars reference the Java 8 library"
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
      c5opt=$(cpcount 'java/util/Optional')
      c5col=$(cpcount 'java/util/stream/Collectors')
      c5obj=$(cpcount 'java/util/Objects')
      c5jt=$(cpcount 'java/time/')
      fact "gate.cp.optional=${c5opt}"
      fact "gate.cp.collectors=${c5col}"
      fact "gate.cp.objects=${c5obj}"
      fact "gate.cp.jtime=${c5jt}"
      f1=$(clamp01 "$(frac "${c5opt}" "${c5_opt_floor:-0}")")
      f2=$(clamp01 "$(frac "${c5col}" "${c5_col_floor:-0}")")
      f3=$(clamp01 "$(frac "${c5obj}" "${c5_obj_floor:-0}")")
      f4=$(clamp01 "$(frac "${c5jt}" "${c5_jt_floor:-0}")")
      printf '    Optional %s/%s  Collectors %s/%s  Objects %s/%s  java.time %s/%s\n' \
         "${c5opt}" "${c5_opt_floor:-0}" "${c5col}" "${c5_col_floor:-0}" \
         "${c5obj}" "${c5_obj_floor:-0}" "${c5jt}" "${c5_jt_floor:-0}"
      # Each leg times the source leg it corroborates -- see the block above.
      # c1..c4 are already computed and printed; this gate is last on purpose.
      c5=$(awk -v a="${f1}" -v b="${f2}" -v c="${f3}" -v d="${f4}" \
               -v p="${c1:-0}" -v q="${c2:-0}" -v r="${c3:-0}" -v s="${c4:-0}" \
           'BEGIN { printf "%.6f", (a * p + b * q + c * r + d * s) / 4 }')
      printf '    corroborated: %s*%s %s*%s %s*%s %s*%s -> %s\n' \
         "${f1}" "${c1:-0}" "${f2}" "${c2:-0}" "${f3}" "${c3:-0}" \
         "${f4}" "${c4:-0}" "${c5}"
   fi
else
   echo "    the build published no module jar -> 0"
   fact "gate.cp=no-jars"
fi

fact "C1.optional=${c1}"
fact "C2.collectors=${c2}"
fact "C3.objects=${c3}"
fact "C4.jtime=${c4}"
fact "C5.ships-java8-library=${c5}"
# 0.30 / 0.35 / 0.10 / 0.10 / 0.15, times the anti-gaming penalty.
#
# Weighted by how much of the tree each leg moves and by how much of it an
# automated migrator reaches, which is the axis step 04 settled on after its own
# first calibration measured what it had been guessed at. The search by recipe
# id and catalogue text is in authoring/automigrate-step05.sh:
#
#   C-1  nothing. Turning `return null` into `Optional.empty()` changes a
#        method's contract and every caller with it; no recipe decides which
#        null-returning methods are lookups whose absence is ordinary
#   C-2  nothing in this direction. rewrite-migrate-java rewrites collectors
#        that already exist -- ReplaceStreamCollectorsToList takes
#        collect(toList()) to toList() on Java 16+, which is the opposite
#        migration -- and nothing turns a hand-written terminal forEach(add)
#        into one, because which collector is correct depends on what the sink
#        was constructed as
#   C-3  nothing, and this one was guessed wrong before it was run. The design
#        note here used to read "YES -- this is the reachable leg", on the
#        reasoning that the static-analysis catalogue carries equals/hashCode
#        rewrites and every mainstream IDE offers the same inspection. It does
#        carry them, and they do not fire on these three classes: Refaster's
#        ObjectsEquals matches `a == b || (a != null && a.equals(b))`, and what
#        the three value types hand-roll is Eclipse-generated nested-if equals
#        over a `getClass() != obj.getClass()` guard. Nothing in any of the
#        three catalogues builds Objects.hash out of a `31 * result`
#        accumulation at all. Measured: EqualityRulesRecipes 0 edits,
#        ReferentialEqualityToObjectEquals 0, NullRulesRecipes 0,
#        ReplaceValidateNotNullHavingVarargsWithObjectsRequireNonNull 0
#   C-4  nothing. System.currentTimeMillis to Instant.now is not a mechanical
#        substitution -- one returns a long and the other an object, and which
#        of the two readings in a timing interval becomes the Instant and which
#        becomes the Duration is a decision
#   C-5  reached to the extent C-3 is, since Objects in three published classes
#        puts java/util/Objects in three constant pools -- one of four legs,
#        whose floor is two, so the automated tree collects that leg whole
#
# So automation reaches NONE of the five legs. That is the measurement, not the
# prediction: thirteen recipes drawn from rewrite-migrate-java 3.23.0,
# rewrite-static-analysis 2.19.0 and error-prone-contrib 0.27.0 -- every one in
# those catalogues that could plausibly touch Optional, Collectors, Objects or
# java.time -- were run in sequence over the step-04 tree, and between them they
# kept 10 file-edits and 13 hunks, none of which lands in any of the four
# populations. Five are `size() == 0` to `isEmpty()` and five are lambda to
# method reference, which is step 04's migration, not this one. The automated
# tree scores 0.475000: exactly the floor, 0% of this step's headroom, against
# rule 5's ceiling of ~30%.
#
# Two of those thirteen rounds are worth keeping in view, because each is a way
# an automated-migrator calibration reads high when nobody is checking.
# CollectionRulesRecipes emitted 23 hunks and 16 of them had to be reverted: it
# rewrites `list.get(0)` to `list.getFirst()`, which is java.util.Sequenced-
# Collection and Java 21, so the "migration" stopped the Java 8 tree compiling.
# NullRulesRecipes emitted 218 -- two thirds of the tree, one hunk per file --
# and every one of them was an OpenRewrite search marker, spliced in because
# jspecify-1 is not on the pinned shelf. Markers are comments: they compile, so
# a javac acceptance gate passes them, and they would have been counted as 218
# successful edits. Both are handled in authoring/automigrate-step05.sh, which
# compiles after every round and discards marker-only edits by name.
#
# C-2 is the largest at 0.35: 23 buckets, three halves, and the only leg whose
# fidelity term can be wrong in a way that compiles and passes every other gate.
# C-1 takes 0.30 for 16 buckets and a consumption term that reaches every caller
# of them. C-3 and C-4 take 0.10 each because their populations are the two
# small ones, 3 classes and 4 buckets. (C-3 was given the smaller share partly
# on the belief that it was the automatable leg; the calibration then found it
# is not. The weight stayed at 0.10 anyway, on the population argument alone,
# which is the reason that survived the measurement.) C-5 takes 0.15 -- more
# than step 04 gave its own must-flip, because at that step B-4 flipped too and
# here nothing else does.
#
# On one line, and staying on one line. The Dockerfile pulls each family's
# weights out of its own `printf "%.6f"` expression and compares them, in order,
# with the weights the report prints; a continuation splits the expression
# across two lines, the grep finds the half without the printf, and the check
# reports "0 lines look like its weight expression" instead of comparing
# anything.
c_score=$(awk -v a="${c1}" -v b="${c2}" -v c="${c3}" -v d="${c4}" -v e="${c5}" -v p="${cpen}" 'BEGIN {
   printf "%.6f", p * (0.30*a + 0.35*b + 0.10*c + 0.10*d + 0.15*e)
}')
fact "familyC=${c_score}"

# ------------------------------------------------------------------ score
#
# Family B's eleven weights are step 04's ten, scaled by 0.88 so that they keep
# their proportions to each other, plus 0.12 for the step-04 gate -- the same
# construction step 04 used on step 03's nine, and step 03 on step 02's:
#
#   B1 0.06  B2 0.03  B3 0.12  B4 0.12  B5 0.10  B6 0.12  B7 0.06   (0.61)
#   B8 0.10  B9 0.06                                                (0.16)
#   B10 0.11                                                        (0.11)
#   B11 0.12                                                        (0.12)
#
# Every one of the ten rounds to two places on its own and the rounding happens
# to cancel: the five that round up and the five that round down sum to 0.88
# exactly, so no weight has to absorb a remainder the way step 04's B-2 did. The
# eleven sum to 1.000000, which the Dockerfile asserts rather than trusts.
#
# 0.12 for B-11 rather than a share proportional to what it holds, for the
# reason B-10 took 0.12: a regression gate is a floor and not a requirement, a
# tree that does nothing at all to step 04's work collects it in full, and the
# floor arithmetic below depends on exactly that.
#
# On one line, and staying on one line. The Dockerfile pulls each family's
# weights out of its own `printf "%.6f"` expression and compares them, in order,
# with the weights the report prints; a continuation splits the expression
# across two lines, the grep finds the half without the printf, and the check
# reports "0 lines look like its weight expression" instead of comparing
# anything.
b_score=$(awk -v b1="${b1}" -v b2="${b2}" -v b3="${b3}" -v b4="${b4}" \
              -v b5="${b5}" -v b6="${b6}" -v b7="${b7}" -v b8="${b8}" \
              -v b9="${b9}" -v b10="${b10}" -v b11="${b11}" 'BEGIN {
   printf "%.6f", 0.06*b1 + 0.03*b2 + 0.12*b3 + 0.12*b4 + 0.10*b5 + 0.12*b6 + 0.06*b7 + 0.10*b8 + 0.06*b9 + 0.11*b10 + 0.12*b11
}')
fact "familyB=${b_score}"

# A and the migrated-ness half, 0.30 / 0.70; inside that half, a quarter for
# holding what steps 01 to 04 won and three quarters for doing step 05. The same
# shape as the three rungs below, and for the same reason: family A cannot tell
# this step's tree from its predecessor's, because the migration this step asks
# for is required to change nothing that a caller can observe.
#
# THE FLOOR IS TIGHTER HERE THAN AT ANY RUNG BELOW, and it is exact rather than
# estimated. The step-04 golden tree under this scorer scores family A in full
# and all ELEVEN B gates in full: B-4 does not move at this rung, so for the
# first time on this ladder there is no inherited gate the predecessor fails.
#
#   0.30*1.0 + 0.70*(0.25*1.0 + 0.75*0.0) = 0.475
#
# against migration_prompt item 6's requirement of 0.5 or under. The margin is
# 0.025, and the reason that is acceptable rather than alarming is that neither
# term can drift: family B is capped at 1.000000 and is already there, and
# family C is not an estimate that might come in a little high -- every one of
# its five gates reads a measured zero on that tree. C-1's 16 buckets all still
# return null and none declares an Optional; C-2's 23 all still accumulate by
# hand; C-3's 3 value types hand-roll equals and hashCode; C-4's 4 buckets all
# still call System.currentTimeMillis; and C-5's four constant-pool counts are
# 0, 0, 0 and 0 in jars that already carry 13 classes referencing
# java/util/stream. A floor that cannot move is worth more than a wider one
# that can, and the alternative -- widening the margin by shifting the B/C split
# away from 0.25/0.75 -- would buy 0.035 by making this rung's arithmetic
# disagree with the four below it for no reason a reader could check.
#
# The same argument is why family C's anti-gaming term is a MULTIPLIER on C
# rather than a gate of its own, and it is sharper at this rung than it was at
# step 04. That term reads 1.0 on the unmigrated tree, which has no raw types
# and no added suppressions, having added nothing; any weight w it took inside
# family C would add 0.525*w straight to a floor that has 0.025 of room, so it
# would fail item 6 outright at w = 0.05. A term that pays a tree for not having
# done the damage is not a gate; it is a penalty, and it is written as one.
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
printf '  family B (steps 01-04 held)     %s  T2 0.175/0.475\n' "${b_score}"
printf '    B1 no-maven         %s  x0.06\n' "${b1}"
printf '    B2 wrapper          %s  x0.03\n' "${b2}"
printf '    B3 published        %s  x0.12\n' "${b3}"
printf '    B4 major%-3s         %s  x0.12\n' "${WANT_MAJOR}" "${b4}"
printf '    B5 tests present    %s  x0.10\n' "${b5}"
printf '    B6 api surface      %s  x0.12\n' "${b6}"
printf '    B7 tests run        %s  x0.06\n' "${b7}"
printf '    B8 jupiter held     %s  x0.10\n' "${b8}"
printf '    B9 junit4 retired   %s  x0.06\n' "${b9}"
printf '    B10 java 7 held     %s  x0.11\n' "${b10}"
printf '    B11 java 8 lang held %s  x0.12\n' "${b11}"
printf '  family C (Java 8 library)       %s  T1\n' "${c_score}"
printf '    C1 optional             %s  x0.30\n' "${c1}"
printf '    C2 collectors           %s  x0.35\n' "${c2}"
printf '    C3 objects              %s  x0.10\n' "${c3}"
printf '    C4 java.time            %s  x0.10\n' "${c4}"
printf '    C5 ships the library    %s  x0.15\n' "${c5}"
printf '    anti-gaming penalty     %s  (multiplies family C)\n' "${cpen}"
printf '  reward                          %s\n' "${final}"

cp "${FACTS}" /logs/verifier/facts.txt 2>/dev/null || true
report "${final}"
