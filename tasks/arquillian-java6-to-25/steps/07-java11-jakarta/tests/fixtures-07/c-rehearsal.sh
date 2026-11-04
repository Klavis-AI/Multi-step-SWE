#!/usr/bin/env bash
# Rehearse the six scoring blocks that read the typed AST -- the hoisted
# source-AST block, B-10, B-11, B-12, B-13 and the whole of family C -- against
# trees whose answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the six
# blocks are lifted out of the SHIPPED test.sh by their own banners, composed in
# file order behind a srcgate() that hands them a fixture's report instead of
# compiling anything, and run once per fixture. What is asserted is the
# arithmetic over the report, which is the part of these gates that no other
# check exercises.
#
# SIX blocks and not five. Step 06's rehearsal composed the source AST, B-10,
# B-11, B-12 and family C; this step adds B-13, which is step 06's whole family
# C inherited under a new name and reads the same report. The extraction asserts
# all six are disjoint and in that order in the file -- the counters are read in
# one place and spent in five others, and a hoist that silently un-hoisted would
# leave B-10 reading variables that do not exist yet, which under `set -u` is a
# dead verifier rather than a wrong number.
#
# AND BOTH ARTIFACT CHANNELS ARE REALLY RUN. Two of the gates at this step are
# not arithmetic over a report:
#
#   B-12e unzips the jars the build published and binary-greps their class files
#   for four package names -- step 05's must-flip, held.
#   C-1 reads the class-file major versions out of the scan B-4 ran over those
#   same jars -- this step's must-flip.
#
# So the fixtures carry real jars and a real scan report, the run script below
# writes both of the files those blocks read, and what is exercised is the
# actual unzip, the actual grep, the actual scan reading and the actual means --
# not stand-ins for them. The two channels are INDEPENDENT on purpose and three
# fixtures exercise that: K3 has jars and no scan, K19 has a full scan and jars
# that name none of the four packages, and K20 has neither file at all.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step07/
# mkfix07.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all.
#
# All five figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
# At this step the distinction carries three separate findings at once:
# `gate.c1.reach=0` is a scan that found more than one major version or none at
# all, while `gate.c1.reach=0.000000` is a scan that found exactly one and it
# was the wrong one -- and `gate.src.penalty=1` is a multiplier no term fired
# against, where `1.000000` would mean one had and come back to unity.
#
# A THIRD SPELLING, new at this step and asserted the same way: a fact that is
# not published AT ALL. B-13's six sub-facts and family C's C-2..C-5 sit inside
# their blocks' measured-report guard, so a tree the scorer could not compile
# publishes their totals -- computed by awk outside the guard -- and none of the
# parts. K17 is that tree, and the table spells those cells `-`.
set -uo pipefail
TESTS=${TESTS:-/tests}
FIXDIR=${FIXDIR:-${TESTS}/fixtures/c}
SRC=${SRC:-${TESTS}/test.sh}
TMP=$(mktemp -d)
trap 'rm -rf "${TMP}"' EXIT
fail=0
note() { printf '  %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*"; fail=1; }

[ -r "${SRC}" ] || { echo "c-rehearsal: no ${SRC}" >&2; exit 1; }
[ -d "${FIXDIR}" ] || { echo "c-rehearsal: no ${FIXDIR}" >&2; exit 1; }

# B-12e shells out to unzip, and an image without it would score every fixture's
# B-12e as zero -- which the table below would catch, but as twenty-odd
# identical arithmetic failures rather than as the one missing tool. Named here
# so the message says what is actually wrong.
command -v unzip >/dev/null 2>&1 \
   || { echo "c-rehearsal: no unzip; B-12e cannot be rehearsed" >&2; exit 1; }

echo "== extracting the six blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min(arrival, departure) gate at this step --
# which is four of the five in family C -- would read the arrival half alone.
grep -E '^(fact|frac|clamp01|minf)\(\) \{' "${SRC}" > "${TMP}/helpers.sh"
n=$(grep -c . "${TMP}/helpers.sh" || true)
[ "${n}" = 4 ] || bad "expected 4 helper one-liners in ${SRC}, found ${n}"
note "helpers: ${n} (fact frac clamp01 minf)"

# --- WANT_MAJOR, lifted ------------------------------------------------------
# C-1's target class-file level, and the one thing family C reads that is not in
# a block. It is a single column-0 assignment near the head of the file, which
# reads baseline/level.txt and never a literal; the `case` under it validates
# the answer and calls report 0 on a bad one, so it is deliberately NOT lifted
# -- a rehearsal that exited the way the verifier does would take the image
# build with it and say nothing about why.
#
# Instead the two properties that `case` is there for are asserted here, and one
# more the verifier cannot check: that the level this rehearsal is scoring
# against is the level the golden fixture's own scan reports. A baseline edited
# to 56 with fixtures still at 55 would leave every C-1 reading 0.000000 and the
# TABLE would blame the arithmetic.
grep -E '^WANT_MAJOR=' "${SRC}" > "${TMP}/major.sh"
n=$(grep -c . "${TMP}/major.sh" || true)
[ "${n}" = 1 ] || bad "expected 1 column-0 WANT_MAJOR assignment in ${SRC}, found ${n}"
grep -q 'baseline/level.txt' "${TMP}/major.sh" \
   || bad "WANT_MAJOR is not read from baseline/level.txt"
# shellcheck disable=SC1091
. "${TMP}/major.sh"
case "${WANT_MAJOR:-}" in
   5[0-9]|6[0-9]) note "WANT_MAJOR=${WANT_MAJOR} (from baseline/level.txt)" ;;
   *) bad "baseline/level.txt names no plausible class-file major (${WANT_MAJOR:-missing})" ;;
esac
gmaj=$(sed -n 's/^major\.\([0-9][0-9]*\)=.*/\1/p' "${FIXDIR}/golden/scan.txt" 2>/dev/null | head -1)
[ "${gmaj}" = "${WANT_MAJOR}" ] \
   || bad "the golden fixture's scan reports major ${gmaj:-missing}, baseline/level.txt says ${WANT_MAJOR:-missing}"

# --- block 1: the source AST, compiled once ----------------------------------
# Ends where family B begins. The trailing banner is dropped rather than
# included, and both ends are asserted, because an awk range that never turned
# off would swallow the whole of family B and a rehearsal is not the place to
# find that out by watching it run.
awk '/^# -* the source AST, compiled once$/ { on = 1 }
     /^# -* family B$/                      { on = 0 }
     on' "${SRC}" > "${TMP}/block-src.sh"
head -1 "${TMP}/block-src.sh" | grep -q 'the source AST, compiled once$' \
   || bad "the source-AST block does not start at its banner"
[ -s "${TMP}/block-src.sh" ] || bad "source-AST block empty"
grep . "${TMP}/block-src.sh" | tail -1 | grep -q '^hit()' \
   || bad "the source-AST block does not end at the last of its bucket helpers"

# --- blocks 2..5: B-10, B-11, B-12 and B-13 ----------------------------------
sed -n '/^# -* B-10: step 03, held$/,/^fact "B10.java7-held=/p' "${SRC}" \
   > "${TMP}/block-b10.sh"
head -1 "${TMP}/block-b10.sh" | grep -q 'B-10: step 03, held$' \
   || bad "the B-10 block does not start at its banner"
tail -1 "${TMP}/block-b10.sh" | grep -q '^fact "B10.java7-held=' \
   || bad "the B-10 block does not end at its own fact"

sed -n '/^# -* B-11: step 04, held$/,/^fact "B11.java8lang-held=/p' "${SRC}" \
   > "${TMP}/block-b11.sh"
head -1 "${TMP}/block-b11.sh" | grep -q 'B-11: step 04, held$' \
   || bad "the B-11 block does not start at its banner"
tail -1 "${TMP}/block-b11.sh" | grep -q '^fact "B11.java8lang-held=' \
   || bad "the B-11 block does not end at its own fact"

sed -n '/^# -* B-12: step 05, held$/,/^fact "B12.java8-library=/p' "${SRC}" \
   > "${TMP}/block-b12.sh"
head -1 "${TMP}/block-b12.sh" | grep -q 'B-12: step 05, held$' \
   || bad "the B-12 block does not start at its banner"
tail -1 "${TMP}/block-b12.sh" | grep -q '^fact "B12.java8-library=' \
   || bad "the B-12 block does not end at its own fact"

# B-13's banner is a `=` rule and not a `-` one, because it is a whole family
# inherited rather than a gate: at step 06 this was family C in full.
sed -n '/^# =* B-13$/,/^fact "B13.java9-10=/p' "${SRC}" > "${TMP}/block-b13.sh"
head -1 "${TMP}/block-b13.sh" | grep -q 'B-13$' \
   || bad "the B-13 block does not start at its banner"
tail -1 "${TMP}/block-b13.sh" | grep -q '^fact "B13.java9-10=' \
   || bad "the B-13 block does not end at its own fact"

# --- block 6: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

for f in block-src block-b10 block-b11 block-b12 block-b13 block-c; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The six are disjoint and in file order. This is the hoist, asserted: B-10,
# B-11, B-12 and B-13 spend counters the first block reads, so the first block
# has to come first, and family C spends them again after all four.
s1=$(grep -n '^# -* the source AST, compiled once$' "${SRC}" | head -1 | cut -d: -f1)
e1=$(grep -n '^# -* family B$' "${SRC}" | head -1 | cut -d: -f1)
s2=$(grep -n '^# -* B-10: step 03, held$' "${SRC}" | head -1 | cut -d: -f1)
e2=$(grep -n '^fact "B10.java7-held=' "${SRC}" | head -1 | cut -d: -f1)
s3=$(grep -n '^# -* B-11: step 04, held$' "${SRC}" | head -1 | cut -d: -f1)
e3=$(grep -n '^fact "B11.java8lang-held=' "${SRC}" | head -1 | cut -d: -f1)
s4=$(grep -n '^# -* B-12: step 05, held$' "${SRC}" | head -1 | cut -d: -f1)
e4=$(grep -n '^fact "B12.java8-library=' "${SRC}" | head -1 | cut -d: -f1)
s5=$(grep -n '^# =* B-13$' "${SRC}" | head -1 | cut -d: -f1)
e5=$(grep -n '^fact "B13.java9-10=' "${SRC}" | head -1 | cut -d: -f1)
s6=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5 e5 s6; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ] && [ "${s5}" -lt "${e5}" ] \
   && [ "${e5}" -lt "${s6}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < B-13 ${s5}..${e5} < C ${s6}"
else
   bad "the six blocks are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5} ${e5} ${s6}"
fi

# WANT_MAJOR is assigned before all six, which is what makes lifting it
# separately correct rather than convenient.
if [ -n "${s1}" ] && [ "$(grep -n '^WANT_MAJOR=' "${SRC}" | head -1 | cut -d: -f1)" -lt "${s1}" ]; then
   note "WANT_MAJOR is assigned above the first block"
else
   bad "WANT_MAJOR is not assigned above the source-AST block"
fi

# Every gate this rehearsal claims to exercise has to be INSIDE one of the six
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok11='
inblock block-src 'gate.src.ok10='
inblock block-src 'gate.src.okdef='
inblock block-src 'gate.src=broken'
inblock block-src 'covered() {'
inblock block-src 'coveredboth() {'
inblock block-src 'coveredmethods() {'
inblock block-src 'buckets() {'
inblock block-src 'wantbuckets() {'
inblock block-src 'fbuckets() {'
inblock block-src 'nsbuckets() {'
inblock block-src 'nswant() {'
inblock block-src 'gone() {'
inblock block-src 'hit()'
inblock block-b10 'gate.b10.diamond='
inblock block-b10 'gate.b10.resources='
inblock block-b10 'gate.b10.nio='
inblock block-b10 'gate.b10.nio.behaviour.broken='
inblock block-b11 'gate.b11.lambdafiles.covered='
inblock block-b11 'gate.b11.sam.annotated='
inblock block-b11 'gate.b11.collectmethods.covered='
inblock block-b12 'B12a.optional='
inblock block-b12 'B12b.collectors='
inblock block-b12 'B12c.objects='
inblock block-b12 'B12d.jtime='
inblock block-b12 'B12e.ships-java8-library='
inblock block-b12 'gate.src.opt.consumption='
inblock block-b12 'gate.src.sink.faithful='
inblock block-b12 'gate.cp.jars='
inblock block-b12 'gate.cp=no-jars'
inblock block-b12 'modulejars.txt'
inblock block-b13 'B13.2.newinstance='
inblock block-b13 'B13.3.isaccessible='
inblock block-b13 'B13.4.boxedctor='
inblock block-b13 'B13.5.factories='
inblock block-b13 'B13.6.optional9='
inblock block-b13 'B13.7.var='
inblock block-c 'C1.major'
inblock block-c 'C2.jakarta='
inblock block-c 'C3.javax-held='
inblock block-c 'C4.toarray='
inblock block-c 'C5.httpclient='
inblock block-c 'gate.c1.reach='
inblock block-c 'gate.c1=no-scan'
inblock block-c 'gate.c2.want='
inblock block-c 'gate.c3.unscaled='
inblock block-c 'gate.c4.want='
inblock block-c 'gate.c5.cdistub='
inblock block-c 'gate.src.penalty='
inblock block-c 'scan.txt'

# The two artifact gates live OUTSIDE their blocks' own "did anything compile"
# guard, and that is deliberate: a tree that will not compile against the
# scorer's classpath may perfectly well have built and published jars, and what
# those jars contain is a fact about it either way. B-12e sits after the guard
# closes; C-1 sits before it opens, because the scan is the first thing family C
# has to say about a tree. Asserted structurally here and scored on K17 below,
# because this is the kind of property a later edit moves by accident while
# making a guard tidier.
guardend() {
   awk '/^if \[ -z "\$\{measured\}" \]/ { g = 1 } g && /^fi$/ { print NR; exit }' \
      "${TMP}/$1.sh"
}
gend=$(guardend block-b12)
cpline=$(grep -n 'modulejars.txt' "${TMP}/block-b12.sh" | head -1 | cut -d: -f1)
if [ -n "${gend}" ] && [ -n "${cpline}" ] && [ "${cpline}" -gt "${gend}" ]; then
   note "B-12e is outside the measured-report guard (guard ends ${gend}, B-12e at ${cpline})"
else
   bad "B-12e is inside the measured-report guard, or the guard was not found:" \
       "guard ${gend:-?} B-12e ${cpline:-?}"
fi
gstart=$(grep -n '^if \[ -z "${measured}" \]' "${TMP}/block-c.sh" | head -1 | cut -d: -f1)
c1line=$(grep -n 'scan.txt' "${TMP}/block-c.sh" | head -1 | cut -d: -f1)
if [ -n "${gstart}" ] && [ -n "${c1line}" ] && [ "${c1line}" -lt "${gstart}" ]; then
   note "C-1 is outside the measured-report guard (C-1 at ${c1line}, guard opens ${gstart})"
else
   bad "C-1 is inside the measured-report guard, or the guard was not found:" \
       "C-1 ${c1line:-?} guard ${gstart:-?}"
fi

# ...and the parts that are INSIDE a guard are inside it, which is the other
# half of the same property and the one K17's `-` cells assert behaviourally.
# B-13's six sub-facts and family C's C-2..C-5 are published only when something
# compiled; their totals are not. A part that drifted out of its guard would
# publish a clean 0.000000 for a tree nobody could measure, which reads as a
# finding about the tree rather than about the run.
inguard() {
   local blk=$1 pat=$2 gs ge ln
   gs=$(grep -n '^if \[ -z "${measured}" \]' "${TMP}/${blk}.sh" | head -1 | cut -d: -f1)
   ge=$(guardend "${blk}")
   ln=$(grep -n -- "${pat}" "${TMP}/${blk}.sh" | head -1 | cut -d: -f1)
   if [ -n "${gs}" ] && [ -n "${ge}" ] && [ -n "${ln}" ] \
      && [ "${ln}" -gt "${gs}" ] && [ "${ln}" -lt "${ge}" ]; then
      :
   else
      bad "${pat} is not inside ${blk}'s measured guard (${gs:-?} < ${ln:-?} < ${ge:-?})"
   fi
}
for p in 'fact "B13.2.newinstance=' 'fact "B13.3.isaccessible=' \
         'fact "B13.4.boxedctor=' 'fact "B13.5.factories=' \
         'fact "B13.6.optional9=' 'fact "B13.7.var='; do
   inguard block-b13 "${p}"
done
for p in 'fact "C2.jakarta=' 'fact "C3.javax-held=' 'fact "C4.toarray=' \
         'fact "C5.httpclient='; do
   inguard block-c "${p}"
done
note "B-13's six parts and C-2..C-5 are all inside their blocks' measured guard"

# The compiles happen in the first block and NOWHERE else. This is the other
# half of the hoist: if a compile crept back into a later block, that block
# would be measuring a different report from the ones the others scored, and the
# gates would disagree about the same tree.
n=$(grep -c 'srcgate ' "${TMP}/block-src.sh" || true)
[ "${n}" = 3 ] || bad "expected 3 srcgate calls in the source-AST block, found ${n}"
for blk in block-b10 block-b11 block-b12 block-b13 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the six blocks may RUN a compiler, a build or a container.
# They are arithmetic over a report, one pass of unzip over jars that already
# exist and one read of a scan report -- that is what makes them rehearsable at
# all -- and the one thing that touches the tree is srcgate(), which is replaced
# below. Comments are stripped first: the blocks discuss javac at length.
cat "${TMP}"/block-*.sh | sed 's/#.*//' \
   | grep -nE '(^|[;&|(]|\$\() *(javac|java|docker|\./gradlew)([ "]|$)' \
   > "${TMP}/runs.txt" || true
if [ -s "${TMP}/runs.txt" ]; then
   bad "a block runs a compiler, a build or a container:"
   sed 's/^/         /' "${TMP}/runs.txt"
else
   note "no block runs javac, java, docker or gradlew"
fi

# --- the shim ----------------------------------------------------------------
# srcgate(), replaced. Same signature, same artefacts, same echoed answer: the
# fixture's report becomes src-<tag>.txt and the fixture's answers become the
# .env the block reads source.ok out of.
#
# THREE tags at this step: `11`, `10` and `def`. The block asks for 11 first;
# if that succeeded it asks for 10, because a tree that still compiles at
# -source 10 has no Java 11 language feature load-bearing in it, which is a
# finding and not a failure; and it asks for `def` only when 11 failed. So a
# fixture's answers.txt carries all three and the shim answers whichever the
# block asks for.
#
# The javac log is written only for the tag that FAILED, because the block only
# reads one when the probe said no -- it prints the compiler's complaint rather
# than twelve lines of `files=322` from the gate's own .env. A shim that wrote
# one for every tag would hand the block a 1.11 compile that both succeeded and
# left an error log behind, which is a tree no compiler produces.
cat > "${TMP}/shim.sh" <<'SHIM'
TREE=/nonexistent
srcgate() {
   local tag=$1 lvl=${2:-} ok
   cp "${FIXDIR}/report.txt" "${WORK}/src-${tag}.txt"
   echo "the fixture's compile, not a real one" > "${WORK}/src-${tag}.log"
   ok=$(sed -n "s/^ok${tag}=//p" "${FIXDIR}/answers.txt" | head -1)
   if [ "${ok:-no}" != yes ]; then
      printf 'Tree.java:1: error: the fixture did not compile at %s\n1 error\n' \
         "${lvl:-default}" > "${WORK}/src-${tag}.txt.javac.log"
   fi
   { echo "javac.level=${lvl:-default}"
     echo "source.ok=${ok:-no}"
   } > "${WORK}/src-${tag}.txt.env"
   echo "${ok:-no}"
}
SHIM

run_one() {
   local fix=$1 d="${FIXDIR}/$1" run="${TMP}/run-$1.sh"
   {
      echo 'set -uo pipefail'
      echo "FIXDIR='${d}'"
      echo "WORK='${TMP}/w-${fix}'"
      echo 'rm -rf "${WORK}"; mkdir -p "${WORK}"'
      echo 'FACTS="${WORK}/facts.txt"; : > "${FACTS}"'
      echo 'summary="${FIXDIR}/behavior.summary"'
      # The list of published jars, which in the real verifier the build stage
      # writes and B-12e only reads. A fixture with no jars/ directory leaves
      # this file empty, which is exactly how a build that published nothing
      # reaches the block: `[ -s ]` and not `[ -f ]`.
      echo 'ls "${FIXDIR}"/jars/*.jar > "${WORK}/modulejars.txt" 2>/dev/null || :'
      # ...and the class-file scan B-4 ran over those same jars, which family C
      # only reads. A fixture with no scan.txt leaves no file at all, which is
      # how a build that published nothing scannable reaches C-1.
      echo 'cp "${FIXDIR}/scan.txt" "${WORK}/scan.txt" 2>/dev/null || :'
      cat "${TMP}/major.sh" "${TMP}/helpers.sh" "${TMP}/shim.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-b11.sh" \
          "${TMP}/block-b12.sh" "${TMP}/block-b13.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- what each fixture must score --------------------------------------------
# fixture  familyC   B10       B11       B12       B13
# Transcribed by hand from mkfix07.py's printed table. The two ends are the rows
# that carry the step's floor argument: `pinned` is the tree this rung is handed
# and must read familyC 0 with B-10 through B-13 all in full -- migration_prompt
# item 6's nop run -- and `golden` must read all five in full, which is item 6's
# oracle.
TABLE=$(cat <<'EOF'
pinned   0.000000 1.000000 1.000000 1.000000 1.000000
golden   1.000000 1.000000 1.000000 1.000000 1.000000
K1       0.900000 1.000000 1.000000 1.000000 1.000000
K2       0.900000 1.000000 1.000000 1.000000 1.000000
K3       0.900000 1.000000 1.000000 1.000000 1.000000
K4       0.955555 1.000000 1.000000 1.000000 1.000000
K5       0.987879 1.000000 1.000000 1.000000 1.000000
K6       0.927273 1.000000 1.000000 1.000000 1.000000
K7       0.995607 1.000000 1.000000 1.000000 1.000000
K8       0.932540 1.000000 1.000000 1.000000 1.000000
K9       0.866667 1.000000 1.000000 1.000000 1.000000
K10      0.916667 1.000000 1.000000 1.000000 1.000000
K11      0.850000 1.000000 1.000000 1.000000 1.000000
K12      0.700000 1.000000 1.000000 1.000000 1.000000
K13      0.600000 1.000000 1.000000 1.000000 1.000000
K14      0.800000 1.000000 1.000000 1.000000 1.000000
K15      0.500000 1.000000 1.000000 1.000000 1.000000
K16      0.500000 1.000000 1.000000 1.000000 1.000000
K17      0.000000 0        0        0.000000 0.000000
K18      1.000000 1.000000 1.000000 1.000000 0.866667
K19      1.000000 1.000000 1.000000 0.850000 1.000000
K20      0.900000 1.000000 1.000000 0.850000 1.000000
EOF
)

# The table names EVERY fixture in the directory, and the directory holds every
# fixture the table names. A fixture added to the tree and not to the table
# would otherwise disappear from this rehearsal without changing a single line
# of its output.
printf '%s\n' "${TABLE}" | awk '{print $1}' | LC_ALL=C sort > "${TMP}/want.txt"
( cd "${FIXDIR}" && ls -d */ 2>/dev/null ) | tr -d / | LC_ALL=C sort > "${TMP}/have.txt"
if diff "${TMP}/want.txt" "${TMP}/have.txt" > /dev/null; then
   note "$(grep -c . "${TMP}/want.txt") fixtures, and the table names exactly those"
else
   bad "the table and ${FIXDIR} disagree:"
   diff "${TMP}/want.txt" "${TMP}/have.txt" | sed 's/^/         /'
fi

# ...and no two of them are the same tree. Twenty-two rows of which two were
# accidentally identical would look like twenty-two assertions and be
# twenty-one, and the duplicate would agree with its twin whatever either of
# them was measuring. why.txt is excluded: it is prose about the fixture and two
# fixtures could legitimately share a sentence.
( cd "${FIXDIR}" && for d in */; do
     printf '%s %s\n' \
        "$(find "${d}" -type f ! -name why.txt | LC_ALL=C sort | xargs md5sum \
           | awk '{print $1}' | md5sum | awk '{print $1}')" "${d%/}"
  done ) | LC_ALL=C sort > "${TMP}/digests.txt"
ndup=$(awk '{print $1}' "${TMP}/digests.txt" | uniq -d | grep -c . || true)
if [ "${ndup}" = 0 ]; then
   note "all $(grep -c . "${TMP}/digests.txt") fixtures are distinct trees"
else
   bad "${ndup} fixture(s) are byte-identical to another:"
   awk '{print $1}' "${TMP}/digests.txt" | uniq -d \
      | while read -r h; do grep "^${h}" "${TMP}/digests.txt" | sed 's/^/         /'; done
fi

echo
echo "== running the composed blocks against each fixture"
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture familyC B10 B11 B12 B13 C1 C2 C3 C4 C5
while read -r fix wantc wantb10 wantb11 wantb12 wantb13; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   gotc=$(fv "${fix}" familyC)
   gotb10=$(fv "${fix}" 'B10\.java7-held')
   gotb11=$(fv "${fix}" 'B11\.java8lang-held')
   gotb12=$(fv "${fix}" 'B12\.java8-library')
   gotb13=$(fv "${fix}" 'B13\.java9-10')
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${gotc:-MISSING}" "${gotb10:-MISSING}" "${gotb11:-MISSING}" \
      "${gotb12:-MISSING}" "${gotb13:-MISSING}" \
      "$(fv "${fix}" "C1\\.major${WANT_MAJOR}")" "$(fv "${fix}" 'C2\.jakarta')" \
      "$(fv "${fix}" 'C3\.javax-held')" "$(fv "${fix}" 'C4\.toarray')" \
      "$(fv "${fix}" 'C5\.httpclient')"
   [ "${gotc}" = "${wantc}" ] || bad "${fix}: familyC want ${wantc} got ${gotc:-MISSING}"
   [ "${gotb10}" = "${wantb10}" ] \
      || bad "${fix}: B10 want ${wantb10} got ${gotb10:-MISSING}"
   [ "${gotb11}" = "${wantb11}" ] \
      || bad "${fix}: B11 want ${wantb11} got ${gotb11:-MISSING}"
   [ "${gotb12}" = "${wantb12}" ] \
      || bad "${fix}: B12 want ${wantb12} got ${gotb12:-MISSING}"
   [ "${gotb13}" = "${wantb13}" ] \
      || bad "${fix}: B13 want ${wantb13} got ${gotb13:-MISSING}"
done <<EOF
${TABLE}
EOF

# --- the facts that tell the equal-scoring fixtures apart --------------------
# Several fixtures share a total on purpose, because they are the pairs the
# gates exist to separate and a total alone cannot show that the SEPARATION
# happened -- only that the arithmetic came out the same. Each assertion below
# names the channel that distinguishes one from its twin.
echo
echo "== the facts behind the totals"
factck() {
   local fix=$1 key=$2 want=$3 got
   # A fact that is not published at all is its own answer, and at this step it
   # is a load-bearing one: the penalty and C-2..C-5 are computed inside the
   # measured-report guard, so a tree nobody could compile has no penalty fact
   # rather than an unfired `1`. MISSING is spelled out rather than collapsed
   # into the empty string, so `want MISSING` is an assertion and not an
   # accident.
   if grep -qE "^${key}=" "${TMP}/facts-${fix}.txt"; then
      got=$(fv "${fix}" "${key}")
   else
      got=MISSING
   fi
   if [ "${got}" = "${want}" ]; then
      printf '  ok   %-8s %-32s %s\n' "${fix}" "${key}" "${got}"
   else
      bad "${fix}: ${key} want ${want} got ${got:-MISSING}"
   fi
}

# --- pinned: the nop, in full ------------------------------------------------
# This rung's input tree is the step-06 golden. It compiles at 11 AND at 10 --
# nothing in it is a Java 11 language feature -- every family-C source gate
# reads zero, and all four held gates read full. That is migration_prompt item
# 6's nop run, asserted at image build time rather than only in a job.
#
# Every `want` here is the size of a frozen set or a baseline counter, and
# reading them off the facts is how this rehearsal knows the baseline files
# reached the image at all -- a missing set file makes the want 0 and every
# fraction over it 0, which scores a tree that did nothing the same as one that
# did everything.
factck pinned gate.src.ok11 yes
factck pinned gate.src.ok10 yes
factck pinned gate.c1.classes 757
factck pinned "gate.c1.major${WANT_MAJOR}" 757
factck pinned gate.c1.reach 1.000000
factck pinned gate.c2.want 6
factck pinned gate.c2.departed 0
factck pinned gate.c2.arrived 0
factck pinned gate.c2.javax.left 22
factck pinned gate.c2.jakarta.refs 0
factck pinned gate.c3.want 43
factck pinned gate.c3.held 43
factck pinned gate.c3.refs 112
factck pinned gate.c4.want 5
factck pinned gate.c4.departed 0
factck pinned gate.c4.arrived 0
factck pinned gate.c5.legacy 14
factck pinned gate.c5.types 0
factck pinned gate.c5.redirects 0
factck pinned gate.c5.redirects.honoured 0
factck pinned gate.src.jxann 17
factck pinned gate.src.jxejb 5
factck pinned gate.src.jkann 0
factck pinned gate.src.jkejb 0
factck pinned gate.src.tasized 8
factck pinned gate.src.tactor 0
factck pinned gate.src.taraw 0
factck pinned gate.src.raw.added 0
factck pinned gate.src.penalty 1
factck pinned C2.jakarta 0.000000
factck pinned C4.toarray 0.000000
factck pinned C5.httpclient 0.000000
factck pinned "C1.major${WANT_MAJOR}" 0.000000
# C-3 IS THE ONE TO READ TWICE. Its own arithmetic is full on this tree -- all
# 43 held (package, bucket) pairs are there, because the tree has not been
# touched -- and it still scores zero, because C-3 is scaled by C-2: holding
# what you were told to hold is worth nothing until you have moved what you
# were told to move. The unscaled fact and the scored fact disagreeing by the
# whole of C-3 is the check, and it is the one line here that a reader who
# thought C-3 was independent would get wrong.
factck pinned gate.c3.unscaled 1.000000
factck pinned C3.javax-held 0.000000

# --- golden: the other end ---------------------------------------------------
factck golden gate.src.ok11 yes
factck golden gate.c1.reach 1.000000
factck golden gate.c2.departed 6
factck golden gate.c2.arrived 6
factck golden gate.c2.javax.left 0
factck golden gate.c2.jakarta.refs 22
factck golden gate.c3.held 43
factck golden gate.c3.refs 112
factck golden gate.c3.unscaled 1.000000
factck golden gate.c4.departed 5
factck golden gate.c4.arrived 5
factck golden gate.c5.legacy 0
factck golden gate.c5.types 3
factck golden gate.c5.redirects 1
# ...and the proviso that rides on it. The golden names a policy AND keeps
# servlet/exec-redirect matching, which is the only combination that pays.
factck golden gate.c5.redirects.honoured 1
# The golden's summary says `behavior.build=ok` and names nothing broken,
# so all eighteen recorded exchange behaviours still match and C-5's
# behaviour scaling is the identity. This is the healthy reading of the
# channel the two BEHAV* runs below break on purpose.
factck golden gate.c5.exec.want   18
factck golden gate.c5.exec.broken 0
factck golden gate.c5.exec        1.000000
factck golden gate.src.jxann 0
factck golden gate.src.jxejb 0
factck golden gate.src.jkann 17
factck golden gate.src.jkejb 5
factck golden gate.src.tasized 0
factck golden gate.src.tactor 8
factck golden gate.src.penalty 1
factck golden C2.jakarta 1.000000
factck golden C3.javax-held 1.000000
factck golden C4.toarray 1.000000
factck golden C5.httpclient 1.000000
factck golden "C1.major${WANT_MAJOR}" 1.000000
# The golden's five recorded java.net.http names collapse to THREE arrival
# buckets, because C-5 truncates a written type at its fourth dotted segment:
# java.net.http.HttpClient.Redirect and java.net.http.HttpClient.Builder are
# both java.net.http.HttpClient. Three is therefore the ceiling and not a
# shortfall, and a reader who counted the records would make it five and score
# the finished migration 0.6.
factck golden gate.c5.cdistub 3

# --- C-1's three zeros, which are three findings -----------------------------
# The whole tree at 54 (K1), a tree split across two majors (K2), and no scan at
# all (K3) all score C-1 zero and all mean something different. The reach fact
# is what separates them, and its SPELLING is half the separation: K1 divided
# and got nothing, K2 and K3 never divided.
factck K1 gate.c1.classes 757
factck K1 "gate.c1.major${WANT_MAJOR}" 0
factck K1 gate.c1.reach 0.000000
factck K1 "C1.major${WANT_MAJOR}" 0.000000
factck K2 gate.c1.classes 757
factck K2 "gate.c1.major${WANT_MAJOR}" 1
factck K2 gate.c1.reach 0
factck K3 gate.c1 no-scan
factck K3 gate.c1.reach 0
factck K3 gate.c1.classes MISSING

# --- C-2's two halves and the total that is neither --------------------------
# K4 is a bucket that did not move; K5 is every bucket moved and a reference
# added back somewhere no bucket names; K6 is every bucket moved and six
# references deleted rather than migrated. The bucket halves cannot tell K5 and
# K6 from the golden -- all three depart 6 and arrive 6 -- and the reference
# totals can.
factck K4 gate.c2.departed 5
factck K4 gate.c2.arrived 5
factck K4 gate.c2.javax.left 3
factck K4 gate.src.jxann 3
factck K4 gate.src.jkann 14
factck K5 gate.c2.departed 6
factck K5 gate.c2.arrived 6
factck K5 gate.c2.javax.left 1
factck K5 gate.c2.jakarta.refs 22
factck K6 gate.c2.departed 6
factck K6 gate.c2.arrived 6
factck K6 gate.c2.javax.left 0
factck K6 gate.c2.jakarta.refs 16
factck K6 gate.src.jkejb 1

# --- C-3: one pair gone, and a whole package renamed -------------------------
factck K7 gate.c3.held 42
factck K7 gate.c3.refs 110
factck K7 gate.c3.unscaled 0.976744
factck K8 gate.c3.held 33
factck K8 gate.c3.refs 72
factck K8 gate.c3.unscaled 0.642857

# --- C-4: buckets and sites, and the gate taking the smaller -----------------
factck K9 gate.c4.departed 0
factck K9 gate.c4.arrived 0
factck K9 gate.src.tasized 8
factck K9 gate.src.tactor 0
factck K10 gate.c4.departed 2
factck K10 gate.c4.arrived 2
factck K10 gate.src.tasized 5
factck K10 gate.src.tactor 3
factck K10 C4.toarray 0.375000

# --- C-5: the redirect policy, the survivors, and the collapsed names --------
factck K11 gate.c5.redirects 0
factck K11 gate.c5.redirects.honoured 0
factck K11 gate.src.hredir 0
factck K11 C5.httpclient 0.750000
factck K12 gate.c5.legacy 7
factck K12 gate.src.hlegref 5
factck K12 gate.src.hlegcall 5
factck K12 gate.c5.cdistub 3
factck K13 gate.c5.types 1
factck K13 C5.httpclient 0.333333

# --- the three penalty terms, each on its own --------------------------------
# K14 adds raw types, K15 adds a suppression, K16 spells one constructor
# reference Class[]::new. All three leave every family-C leg full and take
# family C down anyway, which is the point: the multiplier is not a gate.
# K15 and K16 both read 0.500000 and are not the same finding -- the facts
# below are what says which term fired.
factck K14 gate.src.raw.added 2
factck K14 gate.src.penalty 0.800000
factck K15 gate.src.raw.added 0
factck K15 gate.src.taraw 0
factck K15 gate.src.penalty 0.500000
factck K16 gate.src.raw.added 0
factck K16 gate.src.taraw 1
factck K16 gate.src.penalty 0.500000

# --- the instrument, refusing ------------------------------------------------
# Nothing compiled. Four things have to be true at once and each is a different
# kind of zero:
#   the blocks say so, rather than reporting clean zeros about the tree;
#   B-10 and B-11 publish the LITERAL 0 their blocks assign and never reach;
#   B-12, B-12e, B-13 and familyC publish 0.000000, because their totals are
#      computed by an awk that sits outside the guard;
#   B-13.2..7, C-2..C-5 and the penalty publish NOTHING, because their fact
#      lines are inside it.
# And C-1 still reads the scan, still reports a full reach, and still scores
# zero -- because the mean it scales is zero, not because it could not see.
factck K17 gate.src broken
factck K17 gate.src.ok11 no
factck K17 gate.src.okdef no
factck K17 gate.src.ok10 MISSING
factck K17 gate.c1.reach 1.000000
factck K17 "C1.major${WANT_MAJOR}" 0.000000
factck K17 B12e.ships-java8-library 0.000000
factck K17 B13.2.newinstance MISSING
factck K17 B13.7.var MISSING
factck K17 C2.jakarta MISSING
factck K17 C5.httpclient MISSING
factck K17 gate.src.penalty MISSING

# --- the ninth trap: an inherited gate falling on its own ---------------------
# K18 undoes step 06's Class.newInstance work and nothing else. B-13 falls to
# 0.866667 and family C is EXACTLY 1.000000 -- a step-local mean would have
# reported this tree finished. This is the one fixture here whose whole purpose
# is that two families disagree about the same report.
factck K18 gate.b13.c2.want 2
factck K18 gate.b13.c2.departed 0
factck K18 gate.b13.c2.arrived 0
factck K18 B13.2.newinstance 0.000000
factck K18 B13.3.isaccessible 1.000000
factck K18 familyC 1.000000

# --- the two artifact channels are two channels ------------------------------
# K19 and K20 are the same source report as the golden and differ only in what
# the build left behind. K19's jars exist, unzip, and contain twenty-four class
# files that name none of the four packages -- B-12e divides and gets nothing.
# K20 has no jars file at all -- B-12e never divides, and its literal 0 is the
# same number and a different fact. The same distinction, in the same run, in a
# second block: this is why the table compares strings.
factck K19 gate.cp.jars 2
factck K19 gate.cp.classes 24
factck K19 gate.cp.optional 0
factck K19 gate.cp.jtime 0
factck K19 B12a.optional 1.000000
factck K19 B12e.ships-java8-library 0.000000
factck K19 gate.c1.reach 1.000000
factck K19 familyC 1.000000
factck K20 gate.cp no-jars
factck K20 gate.cp.jars MISSING
factck K20 B12a.optional 1.000000
factck K20 B12e.ships-java8-library 0
factck K20 gate.c1 no-scan
factck K20 gate.c1.reach 0
factck K20 "C1.major${WANT_MAJOR}" 0.000000

# --- family C is untouched by the five held-work fixtures --------------------
# K17 excepted -- nothing was measured there at all -- a fixture that moves an
# inherited gate must not move this step's own family, and vice versa. Asserted
# as a pair of loops rather than one line each, so that a gate added to either
# family later is covered without editing this block.
for f in K18; do
   factck "${f}" familyC 1.000000
done
for f in K4 K5 K6 K7 K8 K9 K10 K11 K12 K13 K14 K15 K16; do
   factck "${f}" B13.java9-10 1.000000
   factck "${f}" B12.java8-library 1.000000
done

# --- and the rehearsal proves it can fail ------------------------------------
# Everything above this line is a clean run against a fixture whose answer was
# written down first, and a rehearsal whose assertions cannot fire agrees with
# anything. So: take the golden -- the one tree that scores every gate in full
# -- move ONE field of its report, and require the composed blocks to notice.
#
# The field is chosen so that the noticing has to happen in one specific place.
# `toarray[2]` is the sole record in C-4's ContainerRegistry#create bucket. Its
# `in=` is re-pointed at a method name that is not one of toarray.txt's five,
# and nothing else in the report is touched -- in particular the summary line
# `toarray.ctorref=8` still says eight, because an `in=` is an attribution and
# not a count.
#
# So C-4's two legs have to disagree. The BUCKET leg loses a bucket and reads
# 4 of 5; the SITE leg reads the summary and still reads 8 of 8. C-4 is a min
# over both pairs, so it falls to 0.800000 and no further -- a value no fixture
# in the table produces, which is what makes this a reading and not a match
# against something already asserted. C-1 is scaled by the four source legs and
# falls with it to (0.07 + 0.17 + 0.12*0.8 + 0.54) / 0.90 = 0.973333, and
# familyC to 0.10*0.973333 + 0.876 = 0.973333.
#
# Everything else has to stay exactly where the golden left it. A perturbation
# that moved C-2, C-3, C-5 or any of the four held gates would mean the blocks
# are not reading the channels this file claims they read.
echo
echo "== can it fail: one field of the golden's report, moved"
pertdir="${TMP}/pert"
rm -rf "${pertdir}"; mkdir -p "${pertdir}"
cp -R "${FIXDIR}/golden" "${pertdir}/PERT"
pertrep="${pertdir}/PERT/report.txt"
sed -i 's|^\(toarray\[2\]=.*ContainerRegistry\.java:59 form=ctorref elem=String\[\] in=\)create$|\1createFrom|' \
   "${pertrep}"

# Assert on the BYTES that the edit landed, and that the original is gone.
# A sed that matched nothing leaves a byte-identical copy of the golden, which
# would then score 1.000000 and be reported as a passing perturbation.
nold=$(grep -cE 'ContainerRegistry\.java:59 .* in=create$'     "${pertrep}" || true)
nnew=$(grep -cE 'ContainerRegistry\.java:59 .* in=createFrom$' "${pertrep}" || true)
nctor=$(grep -cE '^toarray\.ctorref=8$'                        "${pertrep}" || true)
ndiff=$(diff "${FIXDIR}/golden/report.txt" "${pertrep}" | grep -c '^[<>]' || true)
if [ "${nold}" = 0 ] && [ "${nnew}" = 1 ] && [ "${nctor}" = 1 ] && [ "${ndiff}" = 2 ]; then
   note "the edit landed: one line changed, in= re-pointed, toarray.ctorref=8 untouched"
else
   bad "the perturbation did not land as described:" \
       "in=create ${nold} (want 0), in=createFrom ${nnew} (want 1)," \
       "ctorref=8 ${nctor} (want 1), changed lines ${ndiff} (want 2)"
fi

_savedfix=${FIXDIR}
FIXDIR=${pertdir}
run_one PERT
FIXDIR=${_savedfix}

# The leg that moved, and the leg beside it that must not have.
factck PERT gate.c4.want     5
factck PERT gate.c4.departed 5
factck PERT gate.c4.arrived  4
factck PERT gate.src.tactor  8
factck PERT C4.toarray       0.800000
factck PERT "C1.major${WANT_MAJOR}" 0.973333
factck PERT familyC          0.973333
# ...and the rest of the tree, unmoved.
factck PERT gate.c1.reach    1.000000
factck PERT C2.jakarta       1.000000
factck PERT C3.javax-held    1.000000
factck PERT C5.httpclient    1.000000
factck PERT gate.src.penalty 1
factck PERT B10.java7-held   1.000000
factck PERT B11.java8lang-held 1.000000
factck PERT B12.java8-library  1.000000
factck PERT B13.java9-10       1.000000

# --- can it fail, twice more: the recorded exchange, broken -------------------
# The perturbation above moves the SOURCE report and watches a source leg fall.
# C-5 carries a second channel that no fixture in the table exercises, because
# every fixture's behavior.summary says `behavior.build=ok` and nothing else:
# the eighteen ids in baseline/httpids.txt, which are what the exchange this
# step rewrites actually does, and which scale C-5 whole.
#
# That channel exists because of round 1 of this step's static review. C-5's
# four source legs are all SHAPE -- records gone, types declared, a redirect
# policy named -- and a file that deletes the method body has every one of
# them. Family A notices, but eighteen of 935 transcripts is 0.005775 of the
# step and C-5 is 0.2835 of it, a 49-to-1 trade in favour of faking it. The
# scaling is what closes that, so it has to be the thing this rehearsal proves
# hardest: not that it exists, but that it is a FRACTION of a list read at run
# time, and not a boolean or a halving.
#
# So two runs, both off the golden, both touching nothing but the summary:
#
#   BEHAVALL   all eighteen broken -- nine as `differ` and nine as `missing`,
#              because the awk reads both channels and a rehearsal that only
#              ever sends `differ` would agree with a gate that reads one.
#              c5exec 0 -> C-5 0 -> S = 0.07 + 0.17 + 0.12 = 0.36
#              -> C-1 = 0.36/0.90 = 0.400000, familyC = 0.04 + 0.36 = 0.400000
#
#   BEHAVHALF  nine of the eighteen broken. c5exec 0.500000 -> C-5 0.500000
#              -> S = 0.07 + 0.17 + 0.12 + 0.27 = 0.63
#              -> C-1 = 0.700000, familyC = 0.07 + 0.63 = 0.700000
#
# 0.400000 and 0.700000 are values no fixture in the table produces, and the
# pair of them is what a halving could not produce: a gate that halved C-5 on
# any breakage would read 0.500000 in BOTH runs. Everything else -- C-2, C-3,
# C-4, the penalty, B-10 through B-13 -- has to stay where the golden left it.
# B-10 reads ${summary} too, against baseline/nioids.txt, and none of the four
# ids on that list is an exchange id; B-10 staying at 1.000000 is the assertion
# that the two provisos read their own lists and not each other's.
echo
echo "== can it fail: the golden's recorded exchange, broken"

httpids=$(grep '^[a-z]' /tests/baseline/httpids.txt)
nhttp=$(printf '%s\n' "${httpids}" | grep -c .)
[ "${nhttp}" = 18 ] || bad "baseline/httpids.txt names ${nhttp} ids, this block was written against 18"

# Build the two doctored summaries. The golden's own summary is copied first
# and appended to, so a run that read the wrong file would lose the
# `behavior.build=ok` line the other blocks depend on.
behavdir="${TMP}/behav"
rm -rf "${behavdir}"; mkdir -p "${behavdir}"
for v in BEHAVALL BEHAVHALF; do
   cp -R "${FIXDIR}/golden" "${behavdir}/${v}"
done
i=0
for id in ${httpids}; do
   if [ "${i}" -lt 9 ]; then
      echo "behavior.differ[${i}]=${id}"  >> "${behavdir}/BEHAVALL/behavior.summary"
      echo "behavior.differ[${i}]=${id}"  >> "${behavdir}/BEHAVHALF/behavior.summary"
   else
      echo "behavior.missing[${i}]=${id}" >> "${behavdir}/BEHAVALL/behavior.summary"
   fi
   i=$(( i + 1 ))
done

# Assert on the bytes, the same way the PERT block does: a loop that appended
# nothing leaves a byte-identical copy of the golden, which would then score
# 1.000000 and be reported as a passing perturbation.
nall=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/BEHAVALL/behavior.summary"  || true)
nhalf=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/BEHAVHALF/behavior.summary" || true)
nmiss=$(grep -cE '^behavior\.missing\['          "${behavdir}/BEHAVALL/behavior.summary"  || true)
nbuild=$(grep -cE '^behavior\.build=ok$'         "${behavdir}/BEHAVALL/behavior.summary"  || true)
nrep=$(diff "${FIXDIR}/golden/report.txt" "${behavdir}/BEHAVALL/report.txt" | grep -c '^[<>]' || true)
if [ "${nall}" = 18 ] && [ "${nhalf}" = 9 ] && [ "${nmiss}" = 9 ] \
   && [ "${nbuild}" = 1 ] && [ "${nrep}" = 0 ]; then
   note "the summaries landed: 18 broken (9 differ, 9 missing) and 9 broken, report.txt untouched"
else
   bad "the behaviour perturbation did not land as described:" \
       "all ${nall} (want 18), half ${nhalf} (want 9), missing ${nmiss} (want 9)," \
       "build lines ${nbuild} (want 1), report.txt changed lines ${nrep} (want 0)"
fi

_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one BEHAVALL
run_one BEHAVHALF
FIXDIR=${_savedfix}

# The scaling itself, spelled out: the denominator is the list, the numerator is
# what family A still matches, and the quotient is what multiplies C-5.
factck BEHAVALL  gate.c5.exec.want   18
factck BEHAVALL  gate.c5.exec.broken 18
factck BEHAVALL  gate.c5.exec        0.000000
factck BEHAVHALF gate.c5.exec.want   18
factck BEHAVHALF gate.c5.exec.broken 9
factck BEHAVHALF gate.c5.exec        0.500000

# C-5's four SOURCE legs are untouched in both runs -- the shape is still
# perfect. That is the whole point: the leg falls on behaviour alone.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.c5.legacy    0
   factck "${f}" gate.c5.types     3
   factck "${f}" gate.c5.redirects 1
done
# The proviso splits them, and it splits them on WHICH id broke rather than on
# how many. BEHAVALL breaks servlet/exec-redirect among its eighteen, so the
# quarter goes; BEHAVHALF breaks the first nine, which do not include it, so the
# quarter stays. Neither of those two numbers is visible in familyC -- C-5 is
# already 0 in one and the quarter is still paid in the other -- which is why
# they are asserted here as facts rather than left to the composite.
factck BEHAVALL  gate.c5.redirects.honoured 0
factck BEHAVHALF gate.c5.redirects.honoured 1
factck BEHAVALL  C5.httpclient 0.000000
factck BEHAVHALF C5.httpclient 0.500000

factck BEHAVALL  "C1.major${WANT_MAJOR}" 0.400000
factck BEHAVALL  familyC                 0.400000
factck BEHAVHALF "C1.major${WANT_MAJOR}" 0.700000
factck BEHAVHALF familyC                 0.700000

# ...and the rest of the tree, unmoved. B-10 is the load-bearing one.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.c1.reach      1.000000
   factck "${f}" C2.jakarta         1.000000
   factck "${f}" C3.javax-held      1.000000
   factck "${f}" C4.toarray         1.000000
   factck "${f}" gate.src.penalty   1
   factck "${f}" B10.java7-held     1.000000
   factck "${f}" B11.java8lang-held 1.000000
   factck "${f}" B12.java8-library  1.000000
   factck "${f}" B13.java9-10       1.000000
done

# --- and the third spelling of zero ------------------------------------------
# A summary that cannot be read at all is not the same as a summary that reads
# clean, and B-10 already settled which way that falls: a proviso that cannot be
# checked must not be assumed satisfied. The tempting implementation is the
# other one -- leave the shape standing when there is no evidence -- and it is
# tempting because it never fires in a healthy run. It fires when the build
# fails, which is exactly when a tree that never compiled would otherwise
# collect 0.2835 for a file nobody ran.
echo
echo "== can it fail: no summary at all"
cp -R "${FIXDIR}/golden" "${behavdir}/BEHAVGONE"
rm -f "${behavdir}/BEHAVGONE/behavior.summary"
[ -r "${behavdir}/BEHAVGONE/behavior.summary" ] \
   && bad "BEHAVGONE still has a behavior.summary" \
   || note "the summary is gone, and nothing else with it"
_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one BEHAVGONE
FIXDIR=${_savedfix}
factck BEHAVGONE gate.c5.exec.want   18
factck BEHAVGONE gate.c5.exec.broken 18
factck BEHAVGONE gate.c5.exec        0.000000
factck BEHAVGONE C5.httpclient       0.000000
factck BEHAVGONE familyC             0.400000
# The unreadable summary takes the redirect quarter with it, for the reason
# above: an unchecked proviso is not a satisfied one. It costs nothing here --
# C-5 is 0 either way -- and it is asserted so that the two withdrawals cannot
# quietly become one.
factck BEHAVGONE gate.c5.redirects.honoured 0

# --- and the one recording the quarter rides on ------------------------------
#
# gate.c5.redirects is a source fact and it is shallow: the scan sees that a
# followRedirects policy is NAMED, not which one. Round 2 of the static review
# priced what that costs. HttpClient's default is NEVER, so an agent who names
# NEVER has written a keyword that changes nothing, and before the proviso
# existed that keyword paid better than silence:
#
#   .followRedirects(NEVER)   red=1, servlet/exec-redirect broken  17/18 * 1.00
#   nothing at all            red=0, servlet/exec-redirect broken  17/18 * 0.75
#
# Same run-time behaviour, quarter of C-5 apart, and the careless spelling won.
# The quarter now carries B-10's proviso -- named AND the recording that
# exercises it still matching -- so the two collapse onto the same number and
# only a policy that actually follows redirects is paid for.
#
# Two runs, one broken id each, off the golden:
#
#   BEHAVRED   servlet/exec-redirect. exec 17/18 = 0.944444, quarter withdrawn
#              -> C-5 = 0.944444 * 0.75 = 0.708333, familyC 0.825000
#   BEHAVONE   servlet/exec-404-forever. exec 17/18, quarter kept
#              -> C-5 = 0.944444, familyC 0.966666
#
# The pair is the assertion. A gate that read the COUNT would put these two at
# the same number; a gate that read the LIST puts them 0.141666 of family C
# apart. And BEHAVONE is the control that says the proviso has not quietly
# become "any breakage at all costs the quarter".
echo
echo "== can it fail: one broken recording, and whether it is the redirect"
for v in BEHAVRED BEHAVONE; do
   rm -rf "${behavdir:?}/${v}"
   cp -R "${FIXDIR}/golden" "${behavdir}/${v}"
done
redid=servlet/exec-redirect
oneid=$(printf '%s\n' "${httpids}" | grep -v "^${redid}\$" | head -1)
echo "behavior.differ[0]=${redid}" >> "${behavdir}/BEHAVRED/behavior.summary"
echo "behavior.differ[0]=${oneid}" >> "${behavdir}/BEHAVONE/behavior.summary"

# The bytes, again: a loop that appended nothing leaves two copies of the
# golden, which score 1.000000 and read as a passing perturbation.
nred=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/BEHAVRED/behavior.summary" || true)
none=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/BEHAVONE/behavior.summary" || true)
if [ "${nred}" = 1 ] && [ "${none}" = 1 ] && [ "${oneid}" != "${redid}" ] \
   && [ -n "${oneid}" ]; then
   note "one broken id each: ${redid} and ${oneid}"
else
   bad "the one-id perturbation did not land: red ${nred} (want 1)," \
       "other ${none} (want 1), other id '${oneid}' (want a non-empty id that is not ${redid})"
fi

_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one BEHAVRED
run_one BEHAVONE
FIXDIR=${_savedfix}

# Same exec fraction in both -- one of eighteen, either way.
for f in BEHAVRED BEHAVONE; do
   factck "${f}" gate.c5.exec.want   18
   factck "${f}" gate.c5.exec.broken 1
   factck "${f}" gate.c5.exec        0.944444
   factck "${f}" gate.c5.redirects   1
done
# ...and the proviso is what tells them apart.
factck BEHAVRED gate.c5.redirects.honoured 0
factck BEHAVONE gate.c5.redirects.honoured 1
factck BEHAVRED C5.httpclient 0.708333
factck BEHAVONE C5.httpclient 0.944444
factck BEHAVRED "C1.major${WANT_MAJOR}" 0.825000
factck BEHAVRED familyC                 0.825000
factck BEHAVONE "C1.major${WANT_MAJOR}" 0.966666
factck BEHAVONE familyC                 0.966666

# Nothing else moved, B-10 included: its list has no exchange id on it.
for f in BEHAVRED BEHAVONE; do
   factck "${f}" gate.c1.reach      1.000000
   factck "${f}" C2.jakarta         1.000000
   factck "${f}" C3.javax-held      1.000000
   factck "${f}" C4.toarray         1.000000
   factck "${f}" gate.src.penalty   1
   factck "${f}" B10.java7-held     1.000000
   factck "${f}" B11.java8lang-held 1.000000
   factck "${f}" B12.java8-library  1.000000
   factck "${f}" B13.java9-10       1.000000
done

echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: OK"
else
   echo "c-rehearsal: FAILED"
   exit 1
fi
