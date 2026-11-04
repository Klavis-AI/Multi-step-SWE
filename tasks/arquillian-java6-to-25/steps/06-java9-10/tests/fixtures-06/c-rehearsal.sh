#!/usr/bin/env bash
# Rehearse the five scoring blocks that read the typed AST -- the hoisted
# source-AST block, B-10, B-11, B-12, and the whole of family C -- against trees
# whose answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the five
# blocks are lifted out of the SHIPPED test.sh by their own banners, composed in
# file order behind a srcgate() that hands them a fixture's report instead of
# compiling anything, and run once per fixture. What is asserted is the
# arithmetic over the report, which is the part of these gates that no other
# check exercises.
#
# FIVE blocks and not four. Step 05's rehearsal composed the source AST, B-10,
# B-11 and family C; this step adds B-12, which holds step 05's four library
# gates in place and reads the same report plus the same published jars. The
# extraction asserts all five are disjoint and in that order in the file -- the
# counters are read in one place and spent in four others, and a hoist that
# silently un-hoisted would leave B-10 reading variables that do not exist yet,
# which under `set -u` is a dead verifier rather than a wrong number.
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
# not stand-ins for them. The two channels are INDEPENDENT on purpose and the
# fixtures exercise that: K41 has jars with no class files in them AND a scan
# that found none, K42 has neither file at all, and K43 has a full scan over the
# input tree's own jars with not one line of the step done.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step06/
# mkfix06.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all.
#
# All four figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
# At this step the distinction carries three separate findings at once:
# `gate.c1.reach=0` is a scan that found more than one major version or none at
# all, while `gate.c1.reach=0.000000` is a scan that found exactly one and it
# was the wrong one.
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
# B-12e as zero -- which the table below would catch, but as forty-odd identical
# arithmetic failures rather than as the one missing tool. Named here so the
# message says what is actually wrong.
command -v unzip >/dev/null 2>&1 \
   || { echo "c-rehearsal: no unzip; B-12e cannot be rehearsed" >&2; exit 1; }

echo "== extracting the five blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min(arrival, departure) gate at this step --
# which is five of the seven in family C -- would read the arrival half alone.
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
# to 55 with fixtures still at 54 would leave every C-1 reading 0.000000 and the
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

# --- blocks 2, 3 and 4: B-10, B-11 and B-12 ----------------------------------
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

# --- block 5: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

for f in block-src block-b10 block-b11 block-b12 block-c; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The five are disjoint and in file order. This is the hoist, asserted: B-10,
# B-11 and B-12 spend counters the first block reads, so the first block has to
# come first, and family C spends them again after all three.
s1=$(grep -n '^# -* the source AST, compiled once$' "${SRC}" | head -1 | cut -d: -f1)
e1=$(grep -n '^# -* family B$' "${SRC}" | head -1 | cut -d: -f1)
s2=$(grep -n '^# -* B-10: step 03, held$' "${SRC}" | head -1 | cut -d: -f1)
e2=$(grep -n '^fact "B10.java7-held=' "${SRC}" | head -1 | cut -d: -f1)
s3=$(grep -n '^# -* B-11: step 04, held$' "${SRC}" | head -1 | cut -d: -f1)
e3=$(grep -n '^fact "B11.java8lang-held=' "${SRC}" | head -1 | cut -d: -f1)
s4=$(grep -n '^# -* B-12: step 05, held$' "${SRC}" | head -1 | cut -d: -f1)
e4=$(grep -n '^fact "B12.java8-library=' "${SRC}" | head -1 | cut -d: -f1)
s5=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < C ${s5}"
else
   bad "the five blocks are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5}"
fi

# WANT_MAJOR is assigned before all five, which is what makes lifting it
# separately correct rather than convenient.
if [ -n "${s1}" ] && [ "$(grep -n '^WANT_MAJOR=' "${SRC}" | head -1 | cut -d: -f1)" -lt "${s1}" ]; then
   note "WANT_MAJOR is assigned above the first block"
else
   bad "WANT_MAJOR is not assigned above the source-AST block"
fi

# Every gate this rehearsal claims to exercise has to be INSIDE one of the five
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok10='
inblock block-src 'gate.src.ok8='
inblock block-src 'gate.src=broken'
inblock block-src 'covered() {'
inblock block-src 'coveredboth() {'
inblock block-src 'coveredmethods() {'
inblock block-src 'buckets() {'
inblock block-src 'wantbuckets() {'
inblock block-src 'gone() {'
inblock block-src 'hit()'
inblock block-b10 'gate.b10.diamond='
inblock block-b10 'gate.b10.resources='
inblock block-b10 'gate.b10.nio='
inblock block-b10 'gate.b10.nio.behaviour.broken='
inblock block-b11 'gate.b11.lambdafiles.covered='
inblock block-b11 'gate.b11.sam.annotated='
inblock block-b11 'gate.b11.collectmethods.covered='
inblock block-b11 'gate.b11=no-report'
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
inblock block-c 'C1.major'
inblock block-c 'C2.newinstance='
inblock block-c 'C3.isaccessible='
inblock block-c 'C4.boxedctor='
inblock block-c 'C5.factories='
inblock block-c 'C6.optional9='
inblock block-c 'C7.var='
inblock block-c 'gate.c1.reach='
inblock block-c 'gate.c1=no-scan'
inblock block-c 'gate.src.penalty='
inblock block-c 'scan.txt'

# The two artifact gates live OUTSIDE their blocks' own "did anything compile"
# guard, and that is deliberate: a tree that will not compile against the
# scorer's classpath may perfectly well have built and published jars, and what
# those jars contain is a fact about it either way. B-12e sits after the guard
# closes; C-1 sits before it opens, because the scan is the first thing family C
# has to say about a tree. Asserted structurally here and scored on K28 below,
# because this is the kind of property a later edit moves by accident while
# making a guard tidier.
awk '/^if \[ -z "\$\{measured\}" \]/ { g = 1 } g && /^fi$/ { print NR; exit }' \
   "${TMP}/block-b12.sh" > "${TMP}/guard12.txt"
gend=$(head -1 "${TMP}/guard12.txt")
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

# The compiles happen in the first block and NOWHERE else. This is the other
# half of the hoist: if a compile crept back into a later block, that block
# would be measuring a different report from the ones the others scored, and the
# gates would disagree about the same tree.
n=$(grep -c 'srcgate ' "${TMP}/block-src.sh" || true)
[ "${n}" = 3 ] || bad "expected 3 srcgate calls in the source-AST block, found ${n}"
for blk in block-b10 block-b11 block-b12 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the five blocks may RUN a compiler, a build or a container.
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
# THREE tags at this step, not two: `10`, `8` and `def`. The block asks for 8
# only when 10 succeeded -- a tree that still compiles at -source 8 has no Java
# 10 language feature load-bearing in it, which is a finding and not a failure
# -- and for `def` only when 10 failed. So a fixture's answers.txt carries all
# three and the shim answers whichever the block asks for.
#
# The javac log is written only for the tag that FAILED, because the block only
# reads one when ok10 is no -- it prints the compiler's complaint rather than
# twelve lines of `files=322` from the gate's own .env. A shim that wrote one
# for every tag would hand the block a 1.10 compile that both succeeded and left
# an error log behind, which is a tree no compiler produces.
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
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" \
          "${TMP}/block-b11.sh" "${TMP}/block-b12.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- what each fixture must score --------------------------------------------
# fixture  familyC   B10       B11       B12
# Transcribed by hand from mkfix06.py's printed table. The two ends are the rows
# that carry the step's floor argument: `pinned` is the tree this rung is handed
# and must read familyC 0 with B-10, B-11 and B-12 all in full -- which is the
# 0.474 floor, the tightest on the ladder against migration_prompt item 6's 0.5
# -- and `golden` must read all four in full.
TABLE=$(cat <<'EOF'
pinned   0.000000 1.000000 1.000000 1.000000
golden   1.000000 1.000000 1.000000 1.000000
K1       0.900000 1.000000 1.000000 1.000000
K2       0.900000 1.000000 1.000000 1.000000
K3       0.900000 1.000000 1.000000 1.000000
K4       0.979260 1.000000 1.000000 1.000000
K5       0.866667 1.000000 1.000000 1.000000
K6       0.866667 1.000000 1.000000 1.000000
K7       0.928205 1.000000 1.000000 1.000000
K8       0.933333 1.000000 1.000000 1.000000
K9       0.800000 1.000000 1.000000 1.000000
K10      0.400000 1.000000 1.000000 1.000000
K11      0.900000 1.000000 1.000000 1.000000
K12      0.966667 1.000000 1.000000 1.000000
K13      0.988889 1.000000 1.000000 1.000000
K14      0.711111 1.000000 1.000000 1.000000
K15      0.711111 1.000000 1.000000 1.000000
K16      0.500000 1.000000 1.000000 1.000000
K17      0.958730 1.000000 1.000000 1.000000
K18      0.744444 1.000000 1.000000 1.000000
K19      0.744444 1.000000 1.000000 1.000000
K20      0.914815 1.000000 1.000000 1.000000
K21      0.911111 1.000000 1.000000 1.000000
K22      0.955556 1.000000 1.000000 1.000000
K23      0.911111 1.000000 1.000000 1.000000
K24      0.500000 1.000000 1.000000 1.000000
K25      0.500000 1.000000 1.000000 1.000000
K26      0.500000 1.000000 1.000000 1.000000
K27      0.500000 1.000000 1.000000 1.000000
K28      0.000000 0        0        0.000000
K29      0.061111 1.000000 0.400000 0.000000
K30      1.000000 0.600000 1.000000 1.000000
K31      1.000000 0.700000 1.000000 1.000000
K32      1.000000 0.700000 1.000000 1.000000
K33      1.000000 0.850000 1.000000 1.000000
K34      1.000000 1.000000 0.800000 1.000000
K35      1.000000 1.000000 0.800000 1.000000
K36      1.000000 1.000000 0.400000 1.000000
K37      1.000000 1.000000 1.000000 0.662500
K38      1.000000 1.000000 1.000000 0.612500
K39      1.000000 1.000000 1.000000 0.862500
K40      1.000000 1.000000 1.000000 0.862500
K41      0.900000 1.000000 1.000000 0.850000
K42      0.900000 1.000000 1.000000 0.850000
K45      1.000000 1.000000 1.000000 0.743500
K43      0.000000 1.000000 1.000000 1.000000
K44      1.000000 1.000000 1.000000 0.983152
EOF
)

# Every fixture on disk is in the table and every table row is on disk. The
# ladder's third trap in miniature: a fixture that stopped being scored would
# otherwise disappear from this rehearsal without changing a single line of its
# output.
printf '%s\n' "${TABLE}" | awk '{print $1}' | LC_ALL=C sort > "${TMP}/want.txt"
( cd "${FIXDIR}" && ls -d */ 2>/dev/null ) | tr -d / | LC_ALL=C sort > "${TMP}/have.txt"
if diff "${TMP}/want.txt" "${TMP}/have.txt" > /dev/null; then
   note "$(grep -c . "${TMP}/want.txt") fixtures, and the table names exactly those"
else
   bad "the table and ${FIXDIR} disagree:"
   diff "${TMP}/want.txt" "${TMP}/have.txt" | sed 's/^/         /'
fi

# ...and no two of them are the same tree. Forty-seven rows of which two were
# accidentally identical would look like forty-seven assertions and be
# forty-six, and the duplicate would agree with its twin whatever either of them
# was measuring. why.txt is excluded: it is prose about the fixture and two
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
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture familyC B10 B11 B12 C1 C2 C3 C4 C5 C6 C7 why
while read -r fix wantc wantb10 wantb11 wantb12; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   gotc=$(fv "${fix}" familyC)
   gotb10=$(fv "${fix}" 'B10\.java7-held')
   gotb11=$(fv "${fix}" 'B11\.java8lang-held')
   gotb12=$(fv "${fix}" 'B12\.java8-library')
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${gotc:-MISSING}" "${gotb10:-MISSING}" "${gotb11:-MISSING}" \
      "${gotb12:-MISSING}" \
      "$(fv "${fix}" "C1\\.major${WANT_MAJOR}")" "$(fv "${fix}" 'C2\.newinstance')" \
      "$(fv "${fix}" 'C3\.isaccessible')" "$(fv "${fix}" 'C4\.boxedctor')" \
      "$(fv "${fix}" 'C5\.factories')" "$(fv "${fix}" 'C6\.optional9')" \
      "$(fv "${fix}" 'C7\.var')" \
      "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   [ "${gotc}" = "${wantc}" ] || bad "${fix}: familyC want ${wantc} got ${gotc:-MISSING}"
   [ "${gotb10}" = "${wantb10}" ] || bad "${fix}: B10 want ${wantb10} got ${gotb10:-MISSING}"
   [ "${gotb11}" = "${wantb11}" ] \
      || bad "${fix}: B11 want ${wantb11} got ${gotb11:-MISSING}"
   [ "${gotb12}" = "${wantb12}" ] \
      || bad "${fix}: B12 want ${wantb12} got ${gotb12:-MISSING}"
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
   # is a load-bearing one: the penalty is computed inside the measured-report
   # guard, so a tree nobody could compile has no penalty fact rather than an
   # unfired `1`. MISSING is spelled out rather than collapsed into the empty
   # string, so `want MISSING` is an assertion and not an accident.
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

# --- pinned: the floor, in full ----------------------------------------------
# This rung's input tree is the step-05 golden. It compiles at 10 AND at 8 --
# nothing in it is a Java 10 language feature -- every family-C source gate
# reads zero, and all three held gates read full. That is the 0.474 floor:
# A = 932/935, B = 1.0, C = 0.0, against migration_prompt item 6's bar of 0.5
# with 0.026 to spare, and it is the tightest floor on this ladder.
#
# Every `want` here is the size of a frozen set or a baseline counter, and
# reading them off the facts is how this rehearsal knows the baseline files
# reached the image at all -- a missing set file makes the want 0 and every
# fraction over it 0, which scores a tree that did nothing the same as one that
# did everything.
factck pinned gate.src.ok10 yes
factck pinned gate.src.ok8 yes
factck pinned gate.c2.want 2
factck pinned gate.c2.departed 0
factck pinned gate.c2.arrived 0
factck pinned gate.src.nilegacy 2
factck pinned gate.src.nictor 11
factck pinned C2.newinstance 0.000000
factck pinned gate.c3.want 12
factck pinned gate.c3.departed 0
# The arrival half alone reads FULL on the input tree: all fourteen
# setAccessible calls are there, and they always were. C-3 is a departure
# question wearing an arrival guard, and this is the row that says so.
factck pinned gate.c3.arrived 12
factck pinned gate.src.isacc 12
factck pinned gate.src.setacc 14
factck pinned C3.isaccessible 0.000000
factck pinned gate.c4.buckets 6
factck pinned gate.c4.departed 0
factck pinned gate.src.boxed 12
factck pinned C4.boxedctor 0.000000
factck pinned gate.c5.want 7
factck pinned gate.c5.departed 0
factck pinned gate.c5.arrived 0
factck pinned gate.c5.views.held 7
factck pinned gate.src.lcbuild 17
factck pinned gate.src.lcviewfield 7
factck pinned gate.src.factory 0
factck pinned C5.factories 0.000000
factck pinned gate.c6.want 3
factck pinned gate.c6.departed 0
factck pinned gate.c6.arrived 0
factck pinned gate.src.ispresent 6
factck pinned C6.optional9 0.000000
factck pinned gate.c7.departed 0.000000
factck pinned gate.c7.arrived 0.000000
factck pinned gate.src.var 0
factck pinned gate.src.varnew 81
factck pinned C7.var 0.000000
# The multiplier is NOT armed on the input tree: no raw type has been added, the
# @SuppressWarnings count is the baseline's, all fourteen setAccessible calls
# survive and all seven views over fields are held. `1` and not `1.000000` --
# the literal the block initialises and never reassigns.
factck pinned gate.src.raw.added 0
factck pinned gate.src.sup 44
factck pinned gate.src.penalty 1
# C-1 on the floor: the jars ARE published, they ARE scanned, every class in
# them has exactly one major version and it is the wrong one. A measured
# 0.000000 -- from the same read the golden's 1.000000 comes from.
factck pinned gate.c1.classes 757
factck pinned gate.c1.major54 0
factck pinned gate.c1.reach 0.000000
factck pinned C1.major54 0.000000
factck pinned familyC 0.000000
# ...and the three held gates, in full, which is the other half of the floor.
factck pinned gate.src.opmethods 31
factck pinned gate.src.loop.now 107
factck pinned gate.src.dia 74
factck pinned gate.b10.nonanon 23
factck pinned gate.b10.explicit.returned 0
factck pinned gate.b10.close 0
factck pinned gate.b10.io 2
factck pinned gate.b10.io.returned 0
factck pinned gate.b10.nioarrive 13
factck pinned gate.b11.lambdafiles.covered 20
factck pinned gate.b11.sam.annotated 24
factck pinned gate.b11.collectmethods.covered 24
factck pinned gate.src.opt.want 16
factck pinned gate.src.opt.departed 16
factck pinned gate.src.opt.arrived 16
factck pinned gate.src.opt.uses 25
factck pinned gate.src.opt.get 0
factck pinned gate.src.sink.want 23
factck pinned gate.src.sink.departed 23
factck pinned gate.src.sink.arrived 23
factck pinned gate.src.sink.faithful 23
factck pinned gate.src.valuetypes.want 3
factck pinned gate.src.valuetypes.migrated 3
factck pinned gate.src.time.want 4
factck pinned gate.src.time.departed 4
factck pinned gate.src.time.arrived 4
factck pinned gate.cp.jars 2
factck pinned gate.cp.classes 24
factck pinned gate.cp.optional 10
factck pinned gate.cp.collectors 13
factck pinned gate.cp.objects 3
factck pinned gate.cp.jtime 4
factck pinned B12e.ships-java8-library 1.000000

# --- golden: the other end ----------------------------------------------------
# Compiles at 10 and NOT at 8, which is the one positive statement this step
# makes about the language level: eighty `var` declarations are load-bearing.
factck golden gate.src.ok10 yes
factck golden gate.src.ok8 no
factck golden gate.c2.departed 2
factck golden gate.c2.arrived 2
factck golden gate.src.nilegacy 0
# The C-2 floor, which is the leg that stops "delete the reflection" from
# passing: thirteen Constructor.newInstance calls, eleven of them already there
# and two arrived from the migration.
factck golden gate.src.nictor 13
factck golden C2.newinstance 1.000000
factck golden gate.c3.departed 12
factck golden gate.c3.arrived 12
factck golden gate.src.isacc 0
factck golden gate.src.setacc 14
factck golden C3.isaccessible 1.000000
factck golden gate.c4.departed 6
factck golden gate.src.boxed 0
factck golden C4.boxedctor 1.000000
factck golden gate.c5.departed 7
factck golden gate.c5.arrived 7
factck golden gate.c5.views.held 7
factck golden gate.src.lcbuild 11
factck golden gate.src.factory 7
factck golden gate.src.factof 6
factck golden gate.src.factcopy 1
factck golden C5.factories 1.000000
factck golden gate.c6.departed 3
factck golden gate.c6.arrived 3
factck golden gate.src.ispresent 0
factck golden C6.optional9 1.000000
factck golden gate.c7.departed 1.000000
factck golden gate.c7.arrived 1.000000
factck golden gate.src.var 80
factck golden gate.src.varnew 1
factck golden C7.var 1.000000
factck golden gate.src.raw.added 0
factck golden gate.src.sup 44
factck golden gate.src.penalty 1
factck golden gate.c1.classes 757
factck golden gate.c1.major54 757
factck golden gate.c1.reach 1.000000
factck golden C1.major54 1.000000
factck golden familyC 1.000000
# The golden's step-03, step-04 and step-05 figures MOVE across this step and
# all three held gates still read 1.0. `newgeneric.diamond` falls 74 -> 73
# because one of the seven hand-built collections C-5 replaced was constructed
# with a diamond; `legacycoll.build` falls 17 -> 11 and `legacycoll.view`
# 8 -> 7 with the same seven. A held-work gate that read any of those raw would
# punish the golden for doing this step correctly.
factck golden gate.src.dia 73
factck golden gate.src.opmethods 31
factck golden gate.src.loop.now 107
factck golden gate.b10.nonanon 23
factck golden gate.b10.explicit.returned 0
factck golden gate.b10.diamond 1.000000
factck golden gate.b11.lambdafiles.covered 20
factck golden gate.b11.sam.annotated 24
factck golden gate.b11.collectmethods.covered 24
factck golden gate.src.opt.departed 16
factck golden gate.src.opt.arrived 16
factck golden gate.src.opt.uses 25
factck golden gate.src.opt.get 0
factck golden gate.src.opt.consumption 1.000000
factck golden gate.src.sink.departed 23
factck golden gate.src.sink.arrived 23
factck golden gate.src.sink.faithful 23
factck golden gate.src.valuetypes.migrated 3
factck golden gate.src.time.departed 4
factck golden gate.src.time.arrived 4
factck golden gate.cp.optional 10
factck golden gate.cp.collectors 13
factck golden gate.cp.objects 3
factck golden gate.cp.jtime 4
factck golden B12e.ships-java8-library 1.000000

# --- C-1's four zeros ---------------------------------------------------------
# K1 / K2 / K3 all read 0.900000 and they are three different findings about a
# build. The SPELLING separates them, which is why these are string compares:
# K1's scan found one major version and it was 52, so the reach is a measured
# 0.000000; K2's found two, and K3's found nothing to report, so both hold the
# block's literal 0. A reader of a failed run wants to know which.
factck K1 gate.c1.classes 757
factck K1 gate.c1.major54 0
factck K1 gate.c1.reach 0.000000
factck K1 C1.major54 0.000000
factck K2 gate.c1.major54 700
factck K2 gate.c1.reach 0
factck K3 gate.c1 no-scan
factck K3 gate.c1.reach 0
# ...and K8 approaches the same region from the other end: C-1 in full and half
# of C-2. It reads 0.933333 -- K1's 0.900000 plus C-1's own 0.10 x 0.84/0.90 =
# 0.093333, less the 0.12 x 0.5 = 0.06 it gives back for the site it missed. A
# row whose total is an exact arithmetic consequence of two named weights is how
# the report's printed weights are shown to be the ones the scorer uses. (It was
# EQUAL to K1 under the first weighting, where C-2 was 0.18; the re-weighting of
# NOTES 11.13 moved C-2 to 0.12 and the coincidence with it. The arithmetic the
# row exists to expose is unchanged, and now has to be read rather than
# recognised.)
factck K8 gate.c1.reach 1.000000
factck K8 gate.c2.departed 1
factck K8 gate.c2.arrived 1
factck K8 gate.src.nilegacy 1
factck K8 gate.src.nictor 12
factck K8 C2.newinstance 0.500000
# K4 is the partial reach, and the only fixture whose C-1 is neither 0 nor 1.
factck K4 gate.c1.major54 600
factck K4 gate.c1.reach 0.792602
factck K4 C1.major54 0.792602

# --- C-2's two halves and its floor -------------------------------------------
# K5 / K6 share 0.866667 -- (10/9) x (0.90 - 0.12), C-2's whole weight lost two
# ways. K5 never migrated: both Class.newInstance calls are
# still there. K6 deleted them and wrote nothing in their place -- the departure
# half alone would score it full.
factck K5 gate.src.nilegacy 2
factck K5 gate.c2.departed 0
factck K5 gate.c2.arrived 0
factck K6 gate.src.nilegacy 0
factck K6 gate.c2.departed 2
factck K6 gate.c2.arrived 0
factck K6 gate.src.nictor 11
# K7 is the floor doing its job: both calls migrated, both target buckets hit,
# and seven OTHER Constructor.newInstance calls deleted on the way past.
# 6 of 13, and the two halves above it read full.
factck K7 gate.c2.departed 2
factck K7 gate.c2.arrived 2
factck K7 gate.src.nictor 6
factck K7 C2.newinstance 0.461538

# --- C-3's two halves, and the penalty that is not C-3 ------------------------
# K9 reads 0.800000 and K18 / K19 read 0.744444 -- one whole gate lost three
# different ways, and after the NOTES 11.13 re-weighting the two gates no longer
# weigh the same: (10/9) x (0.90 - 0.18) for C-3 and (10/9) x (0.90 - 0.23) for
# C-6. That the three no longer share a total is itself worth having; a shared
# total is a weaker assertion than two totals that differ by exactly the two
# weights' difference.
factck K9 gate.c3.departed 0
factck K9 gate.c3.arrived 12
factck K9 gate.src.isacc 12
factck K9 C3.isaccessible 0.000000
# K10 is the wrong way to remove a guard: delete the setAccessible call with it.
# C-3 reads zero from the ARRIVAL half, and the multiplier fires as well,
# because the tree lost a capability rather than migrating one.
factck K10 gate.src.setacc 0
factck K10 gate.c3.departed 12
factck K10 gate.c3.arrived 0
factck K10 C3.isaccessible 0.000000
factck K10 gate.src.penalty 0.500000
factck K11 gate.c3.departed 6
factck K11 gate.c3.arrived 12
factck K11 C3.isaccessible 0.500000

# --- C-4: buckets, and a count that has to fall -------------------------------
factck K12 gate.c4.departed 0
factck K12 gate.src.boxed 12
factck K12 C4.boxedctor 0.000000
# K13: two wrapper constructors in two methods left behind. C-4 counts BUCKETS
# on one leg and SITES on the other, and the bucket leg is the binding one --
# 4 of 6 rather than 10 of 12.
factck K13 gate.c4.departed 4
factck K13 gate.src.boxed 2
factck K13 C4.boxedctor 0.666667

# --- C-5: the factories, the views, and the difference ------------------------
# K14 / K15 share 0.711111 -- (10/9) x (0.90 - 0.26), the heaviest single loss
# any fixture takes -- and are C-5's two halves. K14 wrote List.of() and
# left the hand-built collection standing next to it; K15 deleted the
# construction and wrote no factory call at all.
factck K14 gate.c5.departed 0
factck K14 gate.c5.arrived 7
factck K14 gate.src.lcbuild 17
factck K15 gate.c5.departed 7
factck K15 gate.c5.arrived 0
factck K15 gate.src.factory 0
# K17 is the near miss: six of the seven buckets converted, both halves 6/7.
factck K17 gate.c5.departed 6
factck K17 gate.c5.arrived 6
factck K17 C5.factories 0.857143
# K16 and K27 are the thing C-5 must NOT reward, and they are the reason the
# view hold is a MULTIPLIER rather than a leg of C-5: both score C-5 in full and
# both halve family C. A codemod that rewrites `Collections.unmodifiableList(x)`
# to `List.copyOf(x)` exists, fires on the seven views over fields, and turns a
# live view into a snapshot -- which EXP-06D measured as a behaviour change.
factck K16 gate.c5.views.held 0
factck K16 gate.src.lcviewfield 0
factck K16 gate.src.factory 14
factck K16 C5.factories 1.000000
factck K16 gate.src.penalty 0.500000
factck K27 gate.c5.views.held 6
factck K27 gate.src.lcviewfield 6
factck K27 gate.src.factory 8
factck K27 C5.factories 1.000000
factck K27 gate.src.penalty 0.500000

# --- C-6: the Java 9 Optional idioms ------------------------------------------
# K18 / K19 are the two halves. K18 wrote or()/ifPresentOrElse() and left every
# isPresent() branch beside it; K19 rewrote the branches to map/orElse, which is
# step 05's migration and not this one.
factck K18 gate.c6.departed 0
factck K18 gate.c6.arrived 3
factck K18 gate.src.ispresent 6
factck K18 C6.optional9 0.000000
factck K19 gate.c6.departed 3
factck K19 gate.c6.arrived 0
factck K19 gate.src.ispresent 0
factck K19 C6.optional9 0.000000
# K20: one of ExceptionProxy's three isPresent() branches left behind. C-6
# counts BUCKETS, so a bucket with one branch still in it has not departed --
# 2 of 3, not 5 of 6.
factck K20 gate.c6.departed 2
factck K20 gate.c6.arrived 3
factck K20 gate.src.ispresent 1
factck K20 C6.optional9 0.666667

# --- C-7: var ------------------------------------------------------------------
# K21 / K23 share 0.911111 -- (10/9) x (0.90 - 0.08) -- and are var's two
# halves, which is the whole reason
# C-7 has two. K21 converted nothing. K23 wrote eighty `var` declarations
# somewhere OTHER than the eighty asked for: the arrival half alone reads full,
# and the candidate population never fell.
factck K21 gate.src.var 0
factck K21 gate.src.varnew 81
factck K21 gate.c7.departed 0.000000
factck K21 gate.c7.arrived 0.000000
factck K23 gate.src.var 80
factck K23 gate.src.varnew 81
factck K23 gate.c7.departed 0.000000
factck K23 gate.c7.arrived 1.000000
factck K22 gate.src.var 40
factck K22 gate.src.varnew 41
factck K22 gate.c7.departed 0.500000
factck K22 gate.c7.arrived 0.500000
factck K22 C7.var 0.500000

# --- the multiplier's four arms -----------------------------------------------
# K24 / K25 / K26 / K16 / K27 all read 0.500000 with every gate in family C full.
# Five trees, five different capabilities lost, one arithmetic.
factck K24 gate.src.raw.added 5
factck K24 gate.src.penalty 0.500000
factck K25 gate.src.sup 50
factck K25 gate.src.raw.added 0
factck K25 gate.src.penalty 0.500000
# K26 is the arm that C-3 cannot see. One setAccessible call deleted -- and NOT
# one of the twelve C-3 guards, so C-3's arrival half still reads 12 of 12 and
# the gate is satisfied. The count is what notices.
factck K26 gate.src.setacc 13
factck K26 gate.c3.arrived 12
factck K26 C3.isaccessible 1.000000
factck K26 gate.src.penalty 0.500000

# --- the instrument, failing two ways ------------------------------------------
# K28: nothing compiled. All four report-reading blocks must say so, and none
# may report a clean zero that reads like a finding about the tree.
#
# BOTH artifact channels still RUN -- they are outside the measured-report
# guard, the jars were published and the scan found 757 classes at major 54 --
# and neither can PAY. B-12e's four legs are each scaled by the source leg they
# corroborate, and C-1's reach is scaled by the whole of C-2..C-7; on a tree the
# scorer could not compile those legs hold their blocks' literal 0. So the
# spellings are measured zeros and not unmeasured literals, and familyC is
# 0.000000 rather than the 0.10 a C-1 that paid on its own reach would have
# handed a tree that would not build.
factck K28 gate.src.ok10 no
factck K28 gate.src.okdef no
factck K28 gate.src broken
factck K28 gate.b10 no-report
factck K28 gate.b11 no-report
factck K28 B10.java7-held 0
factck K28 B11.java8lang-held 0
factck K28 B12a.optional 0
factck K28 B12e.ships-java8-library 0.000000
factck K28 B12.java8-library 0.000000
factck K28 gate.cp.jars 2
factck K28 gate.cp.optional 10
factck K28 gate.c1.classes 757
factck K28 gate.c1.major54 757
factck K28 gate.c1.reach 1.000000
factck K28 C1.major54 0.000000
factck K28 familyC 0.000000
# C-2..C-7 publish NO FACT AT ALL on a tree nobody compiled, and that is the
# block's stated choice rather than an omission: six clean zeros in the report
# read like six findings about the tree. `gate.src=broken` above carries the
# whole statement, and C-1 and familyC are published because they were actually
# computed -- C-1 from a scan that really did run, familyC from the sum of six
# legs that held their literals. The absence is asserted rather than tolerated,
# because a future edit that "tidied" these back in would change what a reader
# of a failed run is told without changing one number.
#
# B-12 does the opposite with the same information -- B12a..B12d publish their
# literal 0 above, because those four facts are written outside its guard -- and
# the two are not reconciled on purpose: B-12's legs are named by facts nothing
# else publishes, while family C's six are each restated by a `gate.c<n>.*`
# triple that is equally absent. Recorded in NOTES-authoring.md.
for g in C2.newinstance C3.isaccessible C4.boxedctor C5.factories \
         C6.optional9 C7.var; do
   factck K28 "${g}" MISSING
done
for g in gate.c2.want gate.c3.want gate.c4.buckets gate.c5.want gate.c6.want \
         gate.c5.views.held gate.src.setacc gate.src.var; do
   factck K28 "${g}" MISSING
done
# ...and the penalty is published INSIDE the guard, so a tree that produced no
# report has no penalty fact at all. Reporting `1` there would read as "no
# anti-gaming arm fired", which is a statement about a tree nobody measured.
factck K28 gate.src.penalty MISSING
#
# K29 is the seventh trap made scorable: a verifier built from a gate jar too
# old to write the `in=` field. Every record still exists and no record can be
# attributed to a method, so every DEPARTURE half -- which counts buckets that
# are absent -- reads FULL, and every ARRIVAL half reads zero. A family C with
# only departure terms would score this tree 1.0 and a reviewer reading the
# facts would see a clean migration.
factck K29 gate.c2.departed 2
factck K29 gate.c2.arrived 0
factck K29 gate.c3.departed 12
factck K29 gate.c3.arrived 0
factck K29 gate.c5.departed 7
factck K29 gate.c5.arrived 0
factck K29 gate.c6.departed 3
factck K29 gate.c6.arrived 0
factck K29 gate.src.opt.departed 16
factck K29 gate.src.opt.arrived 0
factck K29 gate.src.sink.departed 23
factck K29 gate.src.sink.arrived 0
factck K29 gate.src.time.departed 4
factck K29 gate.src.time.arrived 0
factck K29 gate.src.opmethods absent
factck K29 gate.b11.collectmethods.covered 0
# The view hold reads zero from the same cause, so the multiplier fires too --
# which is correct: a tree whose records cannot be attributed has not been shown
# to hold anything.
factck K29 gate.c5.views.held 0
factck K29 gate.src.penalty 0.500000
# ...and the gates that count rather than attribute still read FULL, which is
# what makes this a diagnosis rather than a blank verifier. C-4's second leg and
# both of C-7's are counters, not sets: they need no `in=` and they say so.
factck K29 C4.boxedctor 1.000000
factck K29 C7.var 1.000000
factck K29 gate.b11.lambdafiles.covered 20
factck K29 gate.b11.sam.annotated 24
factck K29 C1.major54 0.122222
factck K29 familyC 0.061111

# --- B-10's three thirds, each undone on its own, and the proviso -------------
# K30 is built on the GOLDEN, so 73 diamonds come back and not step 03's 93:
# twenty were the sink constructions step 05's collectors removed and one was
# the hand-built collection this step's C-5 replaced, and a tree cannot unwind a
# diamond that is not there. 36 + 73 = 109 explicit, less the 13 anonymous, is
# 96 against the 23 the step began with.
factck K30 gate.b10.nonanon 96
factck K30 gate.b10.explicit.returned 73
factck K30 gate.b10.diamond 0.000000
factck K31 gate.b10.close 11
factck K31 gate.b10.resources 0.000000
factck K32 gate.b10.io 13
factck K32 gate.b10.io.returned 11
factck K32 gate.b10.nioarrive 0
factck K32 gate.b10.nio 0.000000
factck K33 gate.b10.nio.behaviour.broken 4
factck K33 gate.b10.nio 0.500000

# --- B-11's three thirds, each undone on its own ------------------------------
factck K34 gate.b11.lambdafiles.covered 0
factck K34 gate.b11.lambdas 0.000000
factck K35 gate.b11.sam.annotated 0
factck K35 gate.b11.functionalinterface 0.000000
factck K36 gate.b11.collectmethods.covered 0
factck K36 gate.b11.streams 0.000000

# --- B-12's five legs, each undone on its own ---------------------------------
# K37..K40 each break one source leg and leave the jars untouched. The
# constant-pool leg that corroborates the broken one falls with it -- that is
# step 05's R5 finding, held at this rung -- so B-12e reads 0.750000 on all four
# rather than 1.0. The references ARE in the published jars and they pay
# nothing.
factck K37 gate.src.opt.departed 0
factck K37 gate.src.opt.arrived 0
factck K37 B12a.optional 0.000000
factck K37 gate.cp.optional 10
factck K37 B12e.ships-java8-library 0.750000
factck K38 gate.src.sink.departed 0
factck K38 gate.src.sink.arrived 0
factck K38 gate.src.sink.faithful 0
factck K38 B12b.collectors 0.000000
factck K38 B12e.ships-java8-library 0.750000
factck K39 gate.src.valuetypes.migrated 0
factck K39 B12c.objects 0.000000
factck K39 B12e.ships-java8-library 0.750000
factck K40 gate.src.time.departed 0
factck K40 gate.src.time.arrived 0
factck K40 B12d.jtime 0.000000
factck K40 B12e.ships-java8-library 0.750000
# K45: the sixteen Optionals are declared and nineteen of the twenty-five uses
# are get(). Both set halves read full and the consumption term is 6 of 25 --
# the null moved rather than went, and B-12a is the leg that notices.
factck K45 gate.src.opt.uses 25
factck K45 gate.src.opt.get 19
factck K45 gate.src.opt.consumption 0.240000
factck K45 B12a.optional 0.240000
factck K45 B12e.ships-java8-library 0.810000
factck K45 B12.java8-library 0.743500

# --- the two artifact channels are two channels --------------------------------
# K41 and K42 share 0.900000 and 0.850000, and they are the two ways a build can
# publish nothing worth reading. They are the assertion that B-12e and C-1 are
# INDEPENDENT readings and not one reading reported twice: each fixture fails
# both, by its own route, and each publishes its own distinct evidence.
factck K41 gate.cp.jars 2
factck K41 gate.cp.classes 0
# ...and NOT a `gate.cp.optional=0`. There were no class files to look in, so
# there is no population to report a count over, and a zero there would claim a
# measurement that was never taken. The jars-and-no-classes branch names the two
# facts it actually has -- two jars, no classes -- and stops.
factck K41 gate.cp.optional MISSING
factck K41 B12e.ships-java8-library 0
factck K41 gate.c1.classes 0
factck K41 gate.c1.reach 0
factck K41 C1.major54 0.000000
factck K42 gate.cp no-jars
factck K42 B12e.ships-java8-library 0
factck K42 gate.c1 no-scan
factck K42 gate.c1.reach 0
# ...and every source gate in both of them still reads full, which is what makes
# these findings about the BUILD and not about the tree.
factck K41 C5.factories 1.000000
factck K41 C7.var 1.000000
factck K42 C5.factories 1.000000
factck K42 C7.var 1.000000

# --- K43: THE CHEAP TREE -------------------------------------------------------
# This rung's input tree -- the step-05 golden, nothing migrated, every family-C
# source gate 0 -- recompiled at --release 10 so that all 757 published classes
# are major 54. That is one line of one build file, and under a C-1 that paid on
# its own reach it would collect 0.10 of family C for free.
#
# The two facts below are the whole finding: the class files ARE at 54 (the scan
# read them, the reach is a measured 1.000000) and they pay NOTHING, because
# C-2..C-7 are zero and C-1 is scaled by them. This row and `pinned` differ in
# exactly one file -- scan.txt -- and score the same.
factck K43 gate.c1.classes 757
factck K43 gate.c1.major54 757
factck K43 gate.c1.reach 1.000000
factck K43 C1.major54 0.000000
factck K43 C2.newinstance 0.000000
factck K43 C3.isaccessible 0.000000
factck K43 C4.boxedctor 0.000000
factck K43 C5.factories 0.000000
factck K43 C6.optional9 0.000000
factck K43 C7.var 0.000000
factck K43 familyC 0.000000
# ...and all three held gates read full on it, so the reward it would collect is
# the floor and nothing over it.
factck K43 B10.java7-held 1.000000
factck K43 B11.java8lang-held 1.000000
factck K43 B12.java8-library 1.000000

# --- family C is untouched by the seven held-work fixtures ---------------------
# Which is what makes B-10, B-11 and B-12 separate gates rather than second
# readings of the same thing.
for fix in K30 K31 K32 K33 K34 K35 K36 K37 K38 K39 K40 K45; do
   factck "${fix}" familyC 1.000000
done

# --- the co-existence pairs ----------------------------------------------------
# Checklist item 16. A step whose gates contradict each other cannot be solved,
# and the only way to show they do not is to find the sites where two gates read
# the SAME code and require one tree to satisfy both.
echo
echo "== the co-existence pairs"
#
# PAIR 1: C-5 and B-12b, over DeploymentScenario#getStartupDeploymentsFor. That
# one method is both a hand-built collection C-5 asks to become a factory call
# AND one of the twenty-three collector sinks B-12 holds from step 05. The
# golden satisfies both at once -- `List.copyOf(...)` over a stream that still
# ends in a collector -- and reads full in each.
factck golden gate.c5.departed 7
factck golden gate.c5.arrived 7
factck golden C5.factories 1.000000
factck golden gate.src.sink.departed 23
factck golden gate.src.sink.arrived 23
factck golden gate.src.sink.faithful 23
factck golden B12b.collectors 1.000000
#
# K44 is its control, and the reason the pair is needed rather than the
# demonstration alone: the same method converted to `List.copyOf` with the
# collector DROPPED and the accumulation written back by hand. C-5 still reads
# full -- the factory call is there, the construction is gone -- and B-12b
# falls to 22 of 23. If the golden and K44 read the same, this rehearsal is not
# measuring the co-existence at all.
factck K44 C5.factories 1.000000
factck K44 gate.src.sink.departed 22
factck K44 gate.src.sink.arrived 22
factck K44 gate.src.sink.faithful 22
factck K44 B12b.collectors 0.956522
factck K44 B12e.ships-java8-library 0.989131
factck K44 B12.java8-library 0.983152
factck K44 familyC 1.000000
#
# PAIR 2: C-7 and B-10's diamond third. `var` and the diamond operator read the
# same declarations from opposite ends -- B-10 asks that a generic construction
# still carry `<>` rather than an explicit type argument, C-7 asks that the
# declaration's type be inferred -- and `var x = new ArrayList<>()` satisfies
# both. The golden has 80 of 81 candidates converted AND 73 diamonds held.
factck golden gate.src.varcand 416
factck golden gate.src.varmulti 0
factck golden gate.b10.diamond 1.000000

# --- the control on the control ------------------------------------------------
# Everything above is a clean run, and a harness that cannot fail proves nothing
# about the runs that passed. So the golden's report is perturbed in one field
# -- the `in=` on the Set.of() call in MethodParameterInjectionPoint, re-pointed
# at a method that is not one of C-5's seven buckets -- and the composed blocks
# are required to NOTICE: the factory record still exists and still says
# `java.util.Set.of`, so the ARRIVAL half has to fall to 6 while the departure
# half stays at 7, and familyC has to fall to K17's 0.958730 by a route K17 does
# not take. A nine-character edit in one record of a 517-line report, and it has
# to move the total. If this reads 1.000000 the rehearsal above was measuring
# nothing.
echo
echo "== the negative control: does this harness notice a wrong tree?"
cp -r "${FIXDIR}/golden" "${TMP}/broken"
sed -i 's/ in=getAnnotations$/ in=getAnnotationsCopy/' "${TMP}/broken/report.txt"
grep -q ' in=getAnnotationsCopy$' "${TMP}/broken/report.txt" \
   || bad "the negative control's edit did not land"
grep -q ' in=getAnnotations$' "${TMP}/broken/report.txt" \
   && bad "the negative control's edit left the original in= behind"
FIXDIR_SAVE=${FIXDIR}; FIXDIR=${TMP}
run_one broken
FIXDIR=${FIXDIR_SAVE}
gotc=$(fv broken familyC)
gotdep=$(fv broken 'gate\.c5\.departed')
gotarr=$(fv broken 'gate\.c5\.arrived')
gotc5=$(fv broken 'C5\.factories')
if [ "${gotc}" = "0.958730" ] && [ "${gotdep}" = "7" ] && [ "${gotarr}" = "6" ] \
   && [ "${gotc5}" = "0.857143" ]; then
   note "the perturbed golden reads familyC ${gotc}, C-5 ${gotdep} departed / ${gotarr} arrived -- the harness notices"
else
   bad "the perturbed golden read familyC ${gotc:-MISSING} C-5 ${gotdep:-MISSING}/${gotarr:-MISSING}" \
       "C5.factories ${gotc5:-MISSING}; want 0.958730, 7/6 and 0.857143." \
       "This rehearsal is not measuring the blocks"
fi

# --- the full reading, for the record ------------------------------------------
echo
echo "== every gate, every fixture"
while read -r fix _ _ _ _; do
   [ -n "${fix}" ] || continue
   printf '  --- %s: %s\n' "${fix}" "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   grep -E '^(gate\.(src|b10|b11|c[1-7]|cp)|C[1-7]\.|B1[012][a-e]?\.|familyC)' \
      "${TMP}/facts-${fix}.txt" | sed 's/^/        /'
done <<EOF
${TABLE}
EOF

echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: the source-AST, B-10, B-11, B-12 and family-C blocks score every fixture as measured"
else
   echo "c-rehearsal: FAILURES above" >&2
fi
exit "${fail}"
