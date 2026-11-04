#!/usr/bin/env bash
# The corroboration's verdict, rehearsed at image build time on real runs of the
# real harness.
#
#   bash /tests/fixtures/pv-rehearsal.sh
#
# Step 01's equivalent fed printf'd captures to corrob_verdict. This one cannot:
# the verdict now reads two channels, and the second is XML the JUnit Platform
# writes for itself. A hand-written capture would have to come with a
# hand-written report, and then the fixture would be asserting that two files I
# wrote agree with each other. So the fixtures are nine Java classes, compiled by
# this image's javac against the staged harness jar and RUN -- through the
# shipped run_class, with the shipped flags, under the shipped privilege drop.
# Every function they are driven through is read out of /tests/test.sh rather
# than restated.
#
# The nine single-run faces, and what each one is for. The tenth is a pair
# rather than a row -- one class run twice -- and it is described below, beside
# the retry it is there to hold still:
#
#   PassCase        pass 2      two green methods and one @Disabled. The
#                               skipped case appears in both channels, so this
#                               is also what says the count comparison uses
#                               `found` and not `found - skipped`: subtracting
#                               there scored this face as a disagreement.
#   VintageCase     pass 2      the same shape in JUnit 4, through the vintage
#                               engine. Its report is TEST-junit-vintage.xml and
#                               its case names carry no parentheses, so this is
#                               the face that says the verdict reads both engine
#                               reports and normalises the name.
#   FailCase        fail 3      one assertion failure and one thrown exception
#                               beside a passing method: a red class still
#                               reports how many cases ran.
#   ClassInitFails  fail 1      the static initialiser throws. The platform
#                               blames the METHOD, so this is a fail and not a
#                               classfail -- measured, not assumed.
#   BeforeAllFails  classfail 1 @BeforeAll throws. The platform finds the case,
#                               attributes the error to it, and fails the
#                               container. Distinguishing this from the above is
#                               the whole reason there are two verdicts.
#   NoTests         notloadable a class with no runnable method. With
#                               --fail-if-no-tests that is rc=2.
#   does.not.Exist  notloadable a class this tree does not have. Same rc, and
#                               the caller is what decides which of the two is
#                               the candidate's doing.
#   HaltCase        exited      prints three of the twelve counter lines and
#                               calls Runtime.halt(0). Exits 0. This is step
#                               01's ctlD7 in this harness's costume, and a
#                               tail-of-stdout parse passes it.
#   HaltFull        exited      prints all TWELVE counter lines and then halts.
#                               The block parse cannot refuse this one. The
#                               missing engine report is what does -- which is
#                               the reason --reports-dir is in the invocation at
#                               all, and this is the only face that proves it.
#
# Both halt faces exit 0. If the verdict ever comes to trust an exit status, or
# to read one channel, these two go green and nothing else here changes.
set -euo pipefail

TESTS="${TESTS:-/tests}"
FIX="${FIX:-${TESTS}/fixtures}"
. "${TESTS}/helpers.sh"

# Under /tmp and not mktemp -d: the JVM that runs these fixtures is 65534, and
# these paths have to be ones it can traverse and read.
W=/tmp/pvr
rm -rf "${W}"
mkdir -p "${W}/classes" "${W}/md/src/test/resources" "${W}/md/src/main/resources" "${W}/corrob"

# The scorer's own harness and the run-time shelf, resolved exactly as test.sh
# resolves them. Reading them here also settles the grant: if 65534 could not
# read these two directories, every run below would fail to load a class.
harnessjars=$(find "${TESTS}/assets/harness" -name 'junit-platform-console-standalone-*.jar' \
                2>/dev/null | LC_ALL=C sort | tr '\n' ':' | sed 's/:$//')
[ -n "${harnessjars}" ] || { echo "pv-rehearsal: no harness jar staged" >&2; exit 1; }
cpjars=$(find "${TESTS}/assets/testlib" -name '*.jar' 2>/dev/null \
           | LC_ALL=C sort | tr '\n' ':' | sed 's/:$//')
[ -n "${cpjars}" ] || { echo "pv-rehearsal: no testlib staged" >&2; exit 1; }

javac -nowarn -cp "${harnessjars}" -d "${W}/classes" "${FIX}"/cl/*.java
n=$(find "${W}/classes" -name '*.class' | wc -l)
[ "${n}" -ge 9 ] || { echo "pv-rehearsal: only ${n} fixture classes compiled" >&2; exit 1; }
chmod -R a+rX "${W}"

# The two shipped functions. run_class is defined inside the corroboration loop
# and is indented; the indent comes off so it can be sourced, and nothing else
# is touched.
sed -n '/^platform_verdict() {/,/^}/p' "${TESTS}/test.sh" > "${W}/pv.sh"
sed -n '/^         run_class() {/,/^         }/p' "${TESTS}/test.sh" \
   | sed 's/^         //' > "${W}/rc.sh"
for f in pv rc; do
   [ -s "${W}/${f}.sh" ] || { echo "pv-rehearsal: ${f} came out empty" >&2; exit 1; }
done
# It is the shipped invocation and not something that merely parses: every flag
# the verdict depends on is named here, so a change to any of them fails this
# build rather than a grading run.
for flag in -- '--details=summary' '--disable-ansi-colors' '--fail-if-no-tests' \
            '--reports-dir="${crd}"' '--select-class="${fq}"' \
            'org.junit.platform.console.ConsoleLauncher execute'; do
   [ "${flag}" = -- ] && continue
   grep -qF -- "${flag}" "${W}/rc.sh" \
      || { echo "pv-rehearsal: run_class no longer passes ${flag}" >&2; exit 1; }
done
grep -qF 'run_unprivileged env -C "${md}" timeout -s KILL "${c_limit}"' "${W}/rc.sh" \
   || { echo "pv-rehearsal: run_class no longer demotes, or no longer has a limit" >&2; exit 1; }
# The three the second run is made of, read out of test.sh for the reason the
# other two are: a rehearsal that restates a function is asserting about a copy.
sed -n '/^         timed_out() {/,/^         }/p' "${TESTS}/test.sh" \
   | sed 's/^         //' > "${W}/to.sh"
sed -n '/^         corroborate_class() {/,/^         }/p' "${TESTS}/test.sh" \
   | sed 's/^         //' > "${W}/cc.sh"
sed -n '/^corrob_passed_ids() {/,/^}/p' "${TESTS}/test.sh" > "${W}/cp.sh"
for f in to cc cp; do
   [ -s "${W}/${f}.sh" ] || { echo "pv-rehearsal: ${f} came out empty" >&2; exit 1; }
done
# The retry is what this file is here to hold still, so it is named rather than
# left to the behaviour below: all three charged verdicts re-run, and none of
# the others.
grep -qF 'exited|fail|classfail) ;;' "${W}/cc.sh" \
   || { echo "pv-rehearsal: corroborate_class no longer re-runs exactly the charged verdicts" >&2
        exit 1; }
# shellcheck disable=SC1090
. "${W}/pv.sh"
# shellcheck disable=SC1090
. "${W}/rc.sh"
# shellcheck disable=SC1090
. "${W}/to.sh"
# shellcheck disable=SC1090
. "${W}/cc.sh"
# shellcheck disable=SC1090
. "${W}/cp.sh"

# What the loop supplies to run_class. root is the fixture class tree in place of
# the candidate's build output; md is the module directory it runs from.
root="${W}/classes"
md="${W}/md"
c_limit=60

fail=0
while read -r fq want; do
   [ -n "${fq}" ] || continue
   crd="${W}/corrob/${fq}.reports"
   cout="${W}/corrob/${fq}.txt"
   crc=0
   celapsed=0
   run_class "${cout}"
   got=$(platform_verdict "${crd}" "${cout}" "${crc}" "${fq}" "${W}/corrob/${fq}.heads")
   reports=$(find "${crd}" -name 'TEST-*.xml' -printf '%f ' 2>/dev/null || true)
   printf 'verdict %-16s rc=%-3s want=%-13s got=%-13s reports: %s\n' \
      "${fq}" "${crc}" "${want}" "${got}" "${reports:-none}"
   if [ "${got}" != "${want}" ]; then
      fail=1
      sed 's/^/      /' "${cout}" | tail -20
   fi
done <<TABLE
PassCase        pass 2
VintageCase     pass 2
FailCase        fail 3
ClassInitFails  fail 1
BeforeAllFails  classfail 1
NoTests         notloadable
does.not.Exist  notloadable
HaltCase        exited
HaltFull        exited
TABLE

# --- the second run a charge has to survive ---------------------------------
#
# Everything above ran each face once, which is what the verdict is. What B-7
# charges is the verdict corroborate_class arrives at after the retry, and these
# two are its control pair: FailCase is red on every run and must stay charged,
# FlakyCase is red on its first and green on its second and must not be. Without
# the first of the two a retry that simply answered `pass` would satisfy this
# file; without the second, one that never re-ran would.
WORK="${W}"
marker="${TMPDIR:-/tmp}/pvr-flaky.marker"
for face in 'FailCase fail 3' 'FlakyCase pass 2'; do
   # shellcheck disable=SC2086
   set -- ${face}
   fq=$1; want="$2 $3"
   rm -f "${marker}"
   crd="${W}/corrob/${fq}.cc.reports"
   cout="${W}/corrob/${fq}.cc.txt"
   crc=0; celapsed=0; cverdict=
   corroborate_class
   printf 'retried %-16s want=%-13s got=%-13s\n' "${fq}" "${want}" "${cverdict}"
   if [ "${cverdict}" != "${want}" ]; then
      fail=1
      sed 's/^/      /' "${cout}" | tail -20
   fi
done
# ...and the single run the retry overrules, so the flaky face is known to be
# flaky rather than merely green. Same class, same flags, one run: red.
rm -f "${marker}"
crd="${W}/corrob/FlakyCase.one.reports"
cout="${W}/corrob/FlakyCase.one.txt"
crc=0; celapsed=0
run_class "${cout}"
got=$(platform_verdict "${crd}" "${cout}" "${crc}" FlakyCase "${W}/corrob/FlakyCase.one.heads")
printf 'single  %-16s want=%-13s got=%-13s\n' FlakyCase 'fail 2' "${got}"
[ "${got}" = "fail 2" ] \
   || { echo "pv-rehearsal: the flaky face is not red on its first run -- the pair above" \
             "is not measuring a retry" >&2; fail=1; }
rm -f "${marker}"

# --- the green half of the same parse ---------------------------------------
#
# corrob_passed_ids reads the .cases the verdict was read from, and B-7 excuses
# a reported failure only for an id it names. Three properties, on faces whose
# contents are known: a red class still names its green method and neither of
# its red ones; a skipped case is not a pass, on both engines; and the id is
# written in the one form the report reader also uses.
pids() { corrob_passed_ids "${W}/corrob/$1.reports" "$1" | LC_ALL=C sort | tr '\n' ' '; }
got=$(pids FailCase)
[ "${got}" = "FailCase#passes " ] \
   || { echo "pv-rehearsal: corrob_passed_ids on the red face gave '${got}'" >&2; fail=1; }
got=$(pids PassCase)
[ "${got}" = "PassCase#a PassCase#b " ] \
   || { echo "pv-rehearsal: corrob_passed_ids on the green face gave '${got}'" >&2; fail=1; }
got=$(pids VintageCase)
[ "${got}" = "VintageCase#v1 VintageCase#v2 " ] \
   || { echo "pv-rehearsal: corrob_passed_ids on the vintage face gave '${got}'" >&2; fail=1; }
got=$(pids HaltFull)
[ -z "${got}" ] \
   || { echo "pv-rehearsal: corrob_passed_ids named '${got}' for a run that wrote no engine" \
             "report -- an excusal can be bought by leaving early" >&2; fail=1; }

# The failure headers, which is what B-7 prints for a red class. Asserted
# separately: a verdict of `fail` with an empty heads file would still be the
# right word and would name nothing.
grep -qxF 'FailCase#failsHere' "${W}/corrob/FailCase.heads" \
   || { echo "pv-rehearsal: the failure headers do not name failsHere" >&2; fail=1; }
grep -qxF 'FailCase#alsoFails' "${W}/corrob/FailCase.heads" \
   || { echo "pv-rehearsal: the failure headers do not name alsoFails" >&2; fail=1; }
# Vintage writes case names without parentheses and Jupiter writes them with; the
# verdict normalises, so a red vintage class would name a bare method too. This
# is the positive control for that normalisation, on a channel that is green:
# the Jupiter face's headers came back parenthesis-free above.
if grep -q '()' "${W}/corrob/FailCase.heads"; then
   echo "pv-rehearsal: a failure header kept its parentheses" >&2; fail=1
fi
# Both halt faces exited 0 and were still refused. Asserted on the evidence the
# refusal rests on rather than only on the word: HaltFull printed the whole
# twelve-line block, and no engine report exists for it.
grep -q '^\[ *1 tests successful *\]$' "${W}/corrob/HaltFull.txt" \
   || { echo "pv-rehearsal: the HaltFull face did not print the full counter block" >&2; fail=1; }
if [ -f "${W}/corrob/HaltFull.reports/TEST-junit-jupiter.xml" ]; then
   echo "pv-rehearsal: the HaltFull face left an engine report; it is not testing the second channel" >&2
   fail=1
fi
[ -f "${W}/corrob/PassCase.reports/TEST-junit-jupiter.xml" ] \
   || { echo "pv-rehearsal: an honest run wrote no engine report; the control cannot fail" >&2; fail=1; }
[ -f "${W}/corrob/VintageCase.reports/TEST-junit-vintage.xml" ] \
   || { echo "pv-rehearsal: the vintage face wrote no vintage report" >&2; fail=1; }

rm -rf "${W}"
[ "${fail}" = 0 ] || { echo "pv-rehearsal: a face did not get the verdict it must" >&2; exit 1; }
echo "pv-rehearsal: the shipped verdict classifies all nine single-run faces as measured," \
     "the retry charges only what repeats, and the excusal names only what passed"
