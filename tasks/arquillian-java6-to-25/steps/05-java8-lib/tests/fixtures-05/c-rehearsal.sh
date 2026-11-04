#!/usr/bin/env bash
# Rehearse the four scoring blocks that read the typed AST -- the hoisted
# source-AST block, B-10, B-11, and the whole of family C -- against trees whose
# answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the four
# blocks are lifted out of the SHIPPED test.sh by their own banners, composed in
# file order behind a srcgate() that hands them a fixture's report instead of
# compiling anything, and run once per fixture. What is asserted is the
# arithmetic over the report, which is the part of these gates that no other
# check exercises.
#
# FOUR blocks and not three. Step 04's rehearsal composed the source AST, B-10
# and family C; this step adds B-11, which holds step 04's own three constructs
# in place and reads the same report. The extraction asserts all four are
# disjoint and in that order in the file -- the counters are read in one place
# and spent in three others, and a hoist that silently un-hoisted would leave
# B-10 reading variables that do not exist yet, which under `set -u` is a dead
# verifier rather than a wrong number.
#
# AND C-5 IS REALLY RUN. It is the one gate at this step that is not arithmetic
# over a report: it unzips the jars the build published and binary-greps their
# class files for four package names. So the fixtures carry real jars, the run
# script below writes the modulejars.txt the block reads, and what is exercised
# is the actual unzip, the actual grep and the actual mean -- not a stand-in for
# them. That is the whole reason the must-flip at this rung can be rehearsed at
# all: B-4 does not move between step 04 and step 05, both trees are class-file
# major 52, and a family C built only out of SourceScan records would be one
# instrument's opinion with nothing to check it against.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step05/
# mkfix05.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all.
#
# All three figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `c5=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran --
# which at this step is the difference between a build that published no jar and
# a build that published jars carrying none of the library.
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

# C-5 shells out to unzip, and an image without it would score every fixture's
# C-5 as zero -- which the table below would catch, but as forty
# identical arithmetic failures rather than as the one missing tool. Named here
# so the message says what is actually wrong.
command -v unzip >/dev/null 2>&1 \
   || { echo "c-rehearsal: no unzip; C-5 cannot be rehearsed" >&2; exit 1; }

echo "== extracting the four blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min(arrival, departure) gate at this step --
# which is four of the five in family C -- would read the arrival half alone.
grep -E '^(fact|frac|clamp01|minf)\(\) \{' "${SRC}" > "${TMP}/helpers.sh"
n=$(grep -c . "${TMP}/helpers.sh" || true)
[ "${n}" = 4 ] || bad "expected 4 helper one-liners in ${SRC}, found ${n}"
note "helpers: ${n} (fact frac clamp01 minf)"

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

# --- blocks 2 and 3: B-10 and B-11 -------------------------------------------
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

# --- block 4: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

for f in block-src block-b10 block-b11 block-c; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The four are disjoint and in file order. This is the hoist, asserted: B-10 and
# B-11 spend counters the first block reads, so the first block has to come
# first, and family C spends them again after both.
s1=$(grep -n '^# -* the source AST, compiled once$' "${SRC}" | head -1 | cut -d: -f1)
e1=$(grep -n '^# -* family B$' "${SRC}" | head -1 | cut -d: -f1)
s2=$(grep -n '^# -* B-10: step 03, held$' "${SRC}" | head -1 | cut -d: -f1)
e2=$(grep -n '^fact "B10.java7-held=' "${SRC}" | head -1 | cut -d: -f1)
s3=$(grep -n '^# -* B-11: step 04, held$' "${SRC}" | head -1 | cut -d: -f1)
e3=$(grep -n '^fact "B11.java8lang-held=' "${SRC}" | head -1 | cut -d: -f1)
s4=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < C ${s4}"
else
   bad "the four blocks are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4}"
fi

# Every gate this rehearsal claims to exercise has to be INSIDE one of the four
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok18='
inblock block-src 'gate.src=broken'
inblock block-src 'covered() {'
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
inblock block-c 'C1.optional='
inblock block-c 'C2.collectors='
inblock block-c 'C3.objects='
inblock block-c 'C4.jtime='
inblock block-c 'C5.ships-java8-library='
inblock block-c 'gate.src.opt.consumption='
inblock block-c 'gate.src.sink.faithful='
inblock block-c 'gate.src.penalty='
inblock block-c 'gate.cp.jars='
inblock block-c 'gate.cp=no-jars'
inblock block-c 'modulejars.txt'

# C-5 lives OUTSIDE the block's own "did anything compile" guard, and that is
# deliberate: a tree that will not compile against the scorer's classpath may
# perfectly well have built and published jars, and whether those jars ship the
# library is a fact about it either way. Asserted structurally here and scored
# on K26 below, because it is the kind of property a later edit moves by
# accident while making the guard tidier.
awk '/^if \[ -z "\$\{measured\}" \]/ { g = 1 } g && /^fi$/ { print NR; exit }' \
   "${TMP}/block-c.sh" > "${TMP}/guard.txt"
gend=$(head -1 "${TMP}/guard.txt")
c5line=$(grep -n 'modulejars.txt' "${TMP}/block-c.sh" | head -1 | cut -d: -f1)
if [ -n "${gend}" ] && [ -n "${c5line}" ] && [ "${c5line}" -gt "${gend}" ]; then
   note "C-5 is outside the measured-report guard (guard ends ${gend}, C-5 at ${c5line})"
else
   bad "C-5 is inside the measured-report guard, or the guard was not found:" \
       "guard ${gend:-?} C-5 ${c5line:-?}"
fi

# The three compiles happen in the first block and NOWHERE else. This is the
# other half of the hoist: if a compile crept back into a later block, that
# block would be measuring a different report from the ones the others scored,
# and the gates would disagree about the same tree.
n=$(grep -c 'srcgate ' "${TMP}/block-src.sh" || true)
[ "${n}" = 3 ] || bad "expected 3 srcgate calls in the source-AST block, found ${n}"
for blk in block-b10 block-b11 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the four blocks may RUN a compiler, a build or a container.
# They are arithmetic over a report and one pass of unzip over jars that already
# exist -- that is what makes them rehearsable at all -- and the one thing that
# touches the tree is srcgate(), which is replaced below. Comments are stripped
# first: the blocks discuss javac at length.
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
# The javac log is written only for the tag that FAILED, because the block only
# reads one when ok18 is no -- it prints the compiler's complaint rather than
# twelve lines of `files=322` from the gate's own .env. A shim that wrote one
# for every tag would hand the block a 1.8 compile that both succeeded and left
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
      # writes and family C only reads. A fixture with no jars/ directory leaves
      # this file empty, which is exactly how a build that published nothing
      # reaches the block: `[ -s ]` and not `[ -f ]`.
      echo 'ls "${FIXDIR}"/jars/*.jar > "${WORK}/modulejars.txt" 2>/dev/null || :'
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" \
          "${TMP}/block-b11.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- what each fixture must score --------------------------------------------
# fixture  familyC   B10       B11
# Transcribed by hand from mkfix05.py's printed table. The two ends are the rows
# that carry the step's floor argument: `pinned` is the tree this rung is handed
# and must read familyC 0 with B-10 and B-11 both in full -- which is the 0.475
# floor, the tightest on the ladder against migration_prompt item 6's 0.5 -- and
# `golden` must read all three in full.
TABLE=$(cat <<'EOF'
pinned   0.000000 1.000000 1.000000
golden   1.000000 1.000000 1.000000
K1       0.662500 1.000000 1.000000
K2       0.662500 1.000000 1.000000
K3       0.662500 1.000000 1.000000
K4       0.831250 1.000000 1.000000
K5       0.831250 1.000000 1.000000
K6       0.662500 1.000000 1.000000
K7       0.612500 1.000000 1.000000
K8       0.983152 1.000000 1.000000
K9       0.679891 1.000000 1.000000
K10      0.612500 1.000000 1.000000
K11      0.814674 1.000000 1.000000
K12      0.862500 1.000000 1.000000
K13      0.862500 1.000000 1.000000
K14      0.862500 1.000000 1.000000
K15      0.954167 1.000000 1.000000
K16      0.862500 1.000000 1.000000
K17      0.862500 1.000000 1.000000
K18      0.965625 1.000000 1.000000
K19      0.000000 1.000000 1.000000
K20      0.137500 1.000000 1.000000
K21      0.850000 1.000000 1.000000
K22      0.850000 1.000000 1.000000
K23      0.850000 1.000000 1.000000
K24      0.500000 1.000000 1.000000
K25      0.500000 1.000000 1.000000
K26      0.000000 0        0
K36      0.000000 1.000000 0.400000
K27      1.000000 0.600000 1.000000
K28      1.000000 0.700000 1.000000
K29      1.000000 0.700000 1.000000
K30      1.000000 0.850000 1.000000
K31      1.000000 1.000000 0.800000
K32      1.000000 1.000000 0.800000
K33      1.000000 1.000000 0.400000
K34      0.612500 1.000000 0.425000
K35      0.387500 1.000000 1.000000
K37      0.000000 1.000000 1.000000
K38      0.977500 1.000000 1.000000
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

# ...and no two of them are the same tree. Thirty-eight rows of which two were
# accidentally identical would look like forty assertions and be
# thirty-nine, and the duplicate would agree with its twin whatever either of
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
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture familyC B10 B11 C1 C2 C3 C4 C5 why
while read -r fix wantc wantb wantb11; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   gotc=$(fv "${fix}" familyC)
   gotb=$(fv "${fix}" 'B10\.java7-held')
   gotb11=$(fv "${fix}" 'B11\.java8lang-held')
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${gotc:-MISSING}" "${gotb:-MISSING}" "${gotb11:-MISSING}" \
      "$(fv "${fix}" 'C1\.optional')" "$(fv "${fix}" 'C2\.collectors')" \
      "$(fv "${fix}" 'C3\.objects')" "$(fv "${fix}" 'C4\.jtime')" \
      "$(fv "${fix}" 'C5\.ships-java8-library')" \
      "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   [ "${gotc}" = "${wantc}" ] || bad "${fix}: familyC want ${wantc} got ${gotc:-MISSING}"
   [ "${gotb}" = "${wantb}" ] || bad "${fix}: B10 want ${wantb} got ${gotb:-MISSING}"
   [ "${gotb11}" = "${wantb11}" ] \
      || bad "${fix}: B11 want ${wantb11} got ${gotb11:-MISSING}"
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
   got=$(fv "${fix}" "${key}")
   if [ "${got}" = "${want}" ]; then
      printf '  ok   %-8s %-32s %s\n' "${fix}" "${key}" "${got}"
   else
      bad "${fix}: ${key} want ${want} got ${got:-MISSING}"
   fi
}

# pinned and golden: the two ends, read in full. Every `want` here is the size
# of a frozen set, and reading them off the facts is how this rehearsal knows
# the baseline files reached the image at all -- a missing set file makes the
# want 0 and every fraction over it 0, which scores a tree that did nothing the
# same as one that did everything.
factck pinned gate.src.opt.want 16
factck pinned gate.src.opt.departed 0
factck pinned gate.src.opt.arrived 0
factck pinned gate.src.opt.uses 0
factck pinned gate.src.opt.consumption 0.000000
factck pinned gate.src.sink.want 23
factck pinned gate.src.sink.departed 0
factck pinned gate.src.sink.arrived 0
factck pinned gate.src.sink.faithful 0
factck pinned gate.src.valuetypes.want 3
factck pinned gate.src.valuetypes.migrated 0
factck pinned gate.src.time.want 4
factck pinned gate.src.time.departed 0
factck pinned gate.src.time.arrived 0
factck pinned gate.src.opmethods 24
factck pinned gate.src.loop.now 115
factck pinned gate.b10.nonanon 23
factck pinned gate.b10.explicit.returned 0
factck pinned gate.b11.lambdafiles.covered 20
factck pinned gate.b11.sam.annotated 24
factck pinned gate.b11.collectmethods.covered 24
# The floor's own shape: the input tree PUBLISHES jars, they ARE scanned, and
# they carry none of the library. C-5 reads 0.000000 and not 0 -- a measured
# zero, from the same unzip and the same grep the golden's 1.000000 comes from.
factck pinned gate.cp.jars 2
factck pinned gate.cp.classes 24
factck pinned gate.cp.optional 0
factck pinned gate.cp.collectors 0
factck pinned gate.cp.objects 0
factck pinned gate.cp.jtime 0
factck pinned C5.ships-java8-library 0.000000

factck golden gate.src.opt.departed 16
factck golden gate.src.opt.arrived 16
factck golden gate.src.opt.uses 25
factck golden gate.src.opt.get 0
factck golden gate.src.opt.consumption 1.000000
factck golden gate.src.sink.departed 23
factck golden gate.src.sink.arrived 23
# THE ACCEPTANCE RULE, ON THE CORRECT TREE. Seven of the golden's 23 buckets are
# collected with toCollection where sinks.txt names toList or toSet -- six
# collect into a list the method then addAll()s into, and the seventh adds to
# the set afterwards. A gate without the rule would score the golden 16/23 here
# and hand the step a solution that cannot reach 1.0. This is the assertion that
# the rule is load-bearing on the tree the step ships, not only on the fixtures
# below it.
factck golden gate.src.sink.faithful 23
factck golden gate.src.valuetypes.migrated 3
factck golden gate.src.time.departed 4
factck golden gate.src.time.arrived 4
factck golden gate.src.opmethods 31
factck golden gate.src.loop.now 107
factck golden gate.cp.jars 2
factck golden gate.cp.classes 24
factck golden gate.cp.optional 10
factck golden gate.cp.collectors 13
factck golden gate.cp.objects 3
factck golden gate.cp.jtime 4
factck golden C5.ships-java8-library 1.000000
# The golden's step-03 and step-04 figures MOVE across this step and B-10 and
# B-11 both still read 1.0. `newgeneric.diamond` falls 93 -> 74 with the 19 sink
# constructions the collectors replaced, `loop.size` falls 115 -> 107, and
# `methodref.size` falls 24 -> 9 because fifteen of step 04's method references
# were the `sink::add` arguments collect() removed. A held-work gate that read
# any of those raw would punish the golden for doing this step correctly.
factck golden gate.b10.nonanon 23
factck golden gate.b10.explicit.returned 0
factck golden gate.b11.lambdafiles.covered 20
factck golden gate.b11.sam.annotated 24
factck golden gate.b11.collectmethods.covered 24

# K1 / K2 / K3 / K6 all read 0.700000, and they are four different trees. C-1 is
# worth 0.30 and each of them loses exactly it, by a different route.
factck K1 gate.src.opt.departed 16
factck K1 gate.src.opt.arrived 0
factck K1 gate.src.opt.uses 0
factck K2 gate.src.opt.departed 0
factck K2 gate.src.opt.arrived 16
factck K3 gate.src.opt.uses 25
factck K3 gate.src.opt.get 25
factck K3 gate.src.opt.consumption 0.000000
# K6 and K1 agree on both halves of the set question and disagree on the third
# term: K6's tree really does have 25 Optional uses, all of them in methods that
# are not the sixteen. The departure half alone would score it full.
factck K6 gate.src.opt.departed 16
factck K6 gate.src.opt.arrived 0
factck K6 gate.src.opt.uses 25
factck K6 gate.src.opt.consumption 1.000000

# K4 / K5 share 0.850000 and are the two ways to half of C-1.
factck K4 gate.src.opt.uses 32
factck K4 gate.src.opt.get 16
factck K4 gate.src.opt.consumption 0.500000
factck K5 gate.src.opt.departed 8
factck K5 gate.src.opt.arrived 8
factck K5 gate.src.opt.consumption 1.000000

# C-2's fidelity half, which is the only leg at this step that can be wrong in a
# way that compiles and passes every other gate. K7 and K10 share 0.650000 --
# the loops deleted, versus deleted and 23 collectors written somewhere else.
factck K7 gate.src.sink.departed 23
factck K7 gate.src.sink.arrived 0
factck K10 gate.src.sink.departed 23
factck K10 gate.src.sink.arrived 0
factck K10 gate.src.sink.faithful 0
# K8 is the one that matters: 22 buckets right, and the LinkedHashSet collected
# with toSet. Set iteration order is in the 935 recordings family A replays, so
# this tree passes every AST gate and changes behaviour. 22/23 and not 23/23.
factck K8 gate.src.sink.arrived 23
factck K8 gate.src.sink.faithful 22
factck K8 C2.collectors 0.956522
# K9: every sink collected with toSet. Only the four that wanted a set are
# faithful; toCollection is accepted for toList and toSet and NOT the reverse,
# so a blanket toSet does not buy the acceptance rule.
factck K9 gate.src.sink.faithful 4
# K11: twelve buckets, all through toCollection. The acceptance rule again, on a
# partial tree -- faithful equals arrived, so none of the twelve is charged for
# choosing the general form.
factck K11 gate.src.sink.departed 12
factck K11 gate.src.sink.arrived 12
factck K11 gate.src.sink.faithful 12

# K12 / K13 / K14 share 0.900000 and are three different wrong routes to C-3.
# The gate is deliberately blind to WHICH wrong route -- its question is whether
# the value type delegates, and `Objects.hashCode(x)` is not `Objects.hash(..)`
# however much it looks like it. What is asserted instead is that each of them
# fails C-3 and NOTHING ELSE moves: a near miss that also disturbed C-1 or C-2
# would be a gate reading the wrong records.
for fix in K12 K13 K14; do
   factck "${fix}" gate.src.valuetypes.migrated 0
   factck "${fix}" C3.objects 0.000000
   factck "${fix}" C1.optional 1.000000
   factck "${fix}" C2.collectors 1.000000
   factck "${fix}" C4.jtime 1.000000
done
factck K15 gate.src.valuetypes.migrated 2
factck K15 C3.objects 0.666667

# K16 / K17 share 0.900000 and are the two halves of C-4.
factck K16 gate.src.time.departed 4
factck K16 gate.src.time.arrived 0
factck K17 gate.src.time.departed 0
factck K17 gate.src.time.arrived 4
# K18: one of ServletMethodExecutor#execute's two currentTimeMillis calls left
# behind. C-4 counts BUCKETS, so a bucket with one call still in it has not
# departed -- 3 of 4, not 9 of 10.
factck K18 gate.src.time.departed 3
factck K18 gate.src.time.arrived 4
factck K18 C4.jtime 0.750000

# C-5's three zeros, which are three different findings about a build and which
# a total alone cannot tell apart: all three read familyC 0.850000.
factck K21 gate.cp.jars 2
factck K21 gate.cp.classes 0
factck K21 C5.ships-java8-library 0
factck K22 gate.cp no-jars
factck K22 C5.ships-java8-library 0
factck K23 gate.cp.jars 2
factck K23 gate.cp.classes 24
factck K23 gate.cp.optional 0
factck K23 C5.ships-java8-library 0.000000
# ...and K23 is the must-flip doing its job: every source gate full, the
# published jars carrying none of it. Source alone would score this 1.0.
factck K23 C1.optional 1.000000
factck K23 C2.collectors 1.000000
factck K19 gate.cp.optional 1
factck K19 C5.ships-java8-library 0.000000

# K37 and K38 are C-5's two halves, one fixture each, and they are here because
# static review round 1 (R5) priced the tree K37 is.
#
# K37 IS THE ATTACK. This rung's input tree -- the step-04 golden, nothing
# migrated, all four source legs 0 -- with jars whose constant pools reach the
# library exactly as fully as the golden's do. Fifteen or so dummy references
# buy that reach. Under a C-5 that paid on its own reach it scored familyC
# 0.150000 and a reward of 0.553750, over item 6's bar of 0.5 with not one line
# of the step done. The two facts below are the whole finding: the references
# ARE there (the fixture landed, the unzip and the grep found them) and they
# pay NOTHING, because C-1..C-4 are zero and each C-5 leg is scaled by its own.
factck K37 gate.cp.optional 10
factck K37 gate.cp.collectors 13
factck K37 gate.cp.objects 3
factck K37 gate.cp.jtime 4
factck K37 C1.optional 0.000000
factck K37 C5.ships-java8-library 0.000000
factck K37 familyC 0.000000
# K38 is its mirror, and it is what stops the scaling from quietly replacing
# the floors: full source work, and jars that reach only 2 of the 5 published
# classes the Optional floor asks for. 2/5 scaled by a C-1 of 1.0 is 0.4, and
# the mean of (0.4, 1, 1, 1) is 0.850000. Drop the scaling and K37 fails; drop
# the floors and K38 does.
factck K38 gate.cp.optional 2
factck K38 C1.optional 1.000000
factck K38 C5.ships-java8-library 0.850000
factck K38 familyC 0.977500

# K20 was the automated migrator's predicted ceiling -- rewrite-migrate-java
# reaching C-3 whole and the quarter of C-5 that follows from it, 0.1375 of
# family C -- and the calibration has since measured the real figure at
# 0.000000: not one of the catalogue's equals/hashCode recipes matches an
# Eclipse-generated nested-if equals. So this row no longer pins a prediction
# about any tool. It pins the arithmetic for a tree in that shape, which the
# scorer still owes whether or not anything can build one.
factck K20 gate.src.valuetypes.migrated 3
factck K20 C3.objects 1.000000
factck K20 gate.cp.objects 3
factck K20 C5.ships-java8-library 0.250000
factck K20 C1.optional 0.000000
factck K20 C2.collectors 0.000000
factck K20 C4.jtime 0.000000

# K24 / K25 share 0.500000 and are the two halves of the multiplier.
factck K24 gate.src.raw.added 5
factck K24 gate.src.penalty 0.500000
factck K25 gate.src.raw.added 0
factck K25 gate.src.penalty 0.500000

# K26 and K36 share 0.000000 and are the two ways the INSTRUMENT fails.
#
# K26: nothing compiled. All three blocks must say so, and none may report a
# clean zero that reads like a finding about the tree. C-5 still RUNS -- it is
# outside the measured-report guard, the jars were published, and the four
# packages are found in them, which the gate.cp facts below record. What it
# cannot do any more is PAY: each of its legs is scaled by the source leg it
# corroborates, and on a tree the scorer could not compile those four legs hold
# the block's literal 0. So the spelling is a measured 0.000000 and not the
# unmeasured literal 0, and familyC is 0.000000 rather than the 0.15 a C-5 that
# paid on its own reach would have handed a tree that would not build.
factck K26 gate.src broken
factck K26 gate.b10 no-report
factck K26 gate.b11 no-report
factck K26 C1.optional 0
factck K26 C4.jtime 0
factck K26 C5.ships-java8-library 0.000000
factck K26 familyC 0.000000
#
# K36 is the seventh trap made scorable: a verifier built from a gate jar too
# old to write the `in=` field. Every record still exists and no record can be
# attributed to a method, so every DEPARTURE half -- which counts buckets that
# are absent -- reads FULL, and every arrival half reads zero. A gate with only
# a departure term would score this tree 1.0 and a reviewer reading the facts
# would see a clean migration. The pair below is the shape of that failure:
# departed 16 of 16 and arrived 0 of 16, from the same records.
factck K36 gate.src.opt.departed 16
factck K36 gate.src.opt.arrived 0
factck K36 gate.src.sink.departed 23
factck K36 gate.src.sink.arrived 0
factck K36 gate.src.time.departed 4
factck K36 gate.src.time.arrived 0
factck K36 gate.src.opmethods absent
factck K36 gate.b11.collectmethods.covered 0
# ...and the two halves of B-11 that need no attribution still read full, which
# is what makes this a diagnosis rather than a blank verifier.
factck K36 gate.b11.lambdafiles.covered 20
factck K36 gate.b11.sam.annotated 24

# B-10's three thirds, each undone on its own, and the proviso.
# K27 is built on the GOLDEN, so 74 diamonds come back and not step 03's 93:
# the other 19 were the sink constructions C-2 already removed, and a tree
# cannot unwind a diamond that is not there. 36 + 74 = 110 explicit, less the 13
# anonymous, is 97 against the 23 the step began with.
factck K27 gate.b10.nonanon 97
factck K27 gate.b10.explicit.returned 74
factck K27 gate.b10.diamond 0.000000
factck K28 gate.b10.close 11
factck K28 gate.b10.resources 0.000000
factck K29 gate.b10.io 13
factck K29 gate.b10.io.returned 11
factck K29 gate.b10.nioarrive 0
factck K29 gate.b10.nio 0.000000
factck K30 gate.b10.nio.behaviour.broken 4
factck K30 gate.b10.nio 0.500000

# B-11's three thirds, each undone on its own.
factck K31 gate.b11.lambdafiles.covered 0
factck K31 gate.b11.lambdas 0.000000
factck K32 gate.b11.sam.annotated 0
factck K32 gate.b11.functionalinterface 0.000000
factck K33 gate.b11.collectmethods.covered 0
factck K33 gate.b11.streams 0.000000

# ...and family C is untouched by all seven, which is what makes B-10 and B-11
# separate gates rather than second readings of the same thing.
for fix in K27 K28 K29 K30 K31 K32 K33; do
   factck "${fix}" familyC 1.000000
done

# --- the co-existence pair ---------------------------------------------------
# Checklist item 16, and item 61 of the step's own list. B-11's third sub-gate
# and this step's C-2 read THE SAME 23 BUCKETS FROM OPPOSITE ENDS: B-11 asks
# whether they still hold a pipeline, C-2 asks whether the pipeline now ends in
# a collector. The two must be satisfiable together, or the step contradicts
# itself and no tree can score both.
#
# K35 is the demonstration: C-2 alone, done correctly. Every one of the 23
# buckets is collected, and every one of them STILL reads as a stream bucket in
# B-11 -- collect() is a terminal operation on a pipeline, not a replacement for
# one. Full in C-2 and full in B-11's stream third, from the same records.
echo
echo "== the B-11 / C-2 co-existence pair"
factck K35 gate.src.sink.arrived 23
factck K35 gate.src.sink.faithful 23
factck K35 C2.collectors 1.000000
factck K35 gate.b11.collectmethods.covered 24
factck K35 gate.b11.streams 1.000000
factck K35 'B11\.java8lang-held' 1.000000
#
# K34 is its control, and the reason the pair is needed rather than the
# demonstration alone: the same 23 buckets converted to plain `for` loops
# instead. That tree loses C-2 -- nothing is collected -- AND loses B-11's
# stream third, because a for loop is not a pipeline. One bucket of 24 survives,
# the one in src/test that was never a sink. If K35 and K34 read the same, this
# rehearsal is not measuring the co-existence at all.
factck K34 gate.src.sink.arrived 0
factck K34 C2.collectors 0.000000
factck K34 gate.b11.collectmethods.covered 1
factck K34 gate.b11.streams 0.041667
factck K34 'B11\.java8lang-held' 0.425000

# --- the control on the control ----------------------------------------------
# Everything above is a clean run, and a harness that cannot fail proves nothing
# about the runs that passed. So the golden's report is perturbed in one field
# -- the LinkedHashSet bucket's collector changed from toCollection to toSet --
# and the composed blocks are required to NOTICE: sink.faithful has to fall to
# 22, C-2 to K8's 0.956522 and familyC to K8's 0.983152. A one-word edit in one
# record of a 53 KB report, and it has to move the total. If this reads 1.000000
# the rehearsal above was measuring nothing.
echo
echo "== the negative control: does this harness notice a wrong tree?"
cp -r "${FIXDIR}/golden" "${TMP}/broken"
sed -i 's/^\(collect\[[0-9]*\]=.*DynamicServiceLoader\.java:[0-9]* \)to=toCollection/\1to=toSet/' \
   "${TMP}/broken/report.txt"
grep -q 'DynamicServiceLoader\.java:[0-9]* to=toSet' "${TMP}/broken/report.txt" \
   || bad "the negative control's edit did not land"
FIXDIR_SAVE=${FIXDIR}; FIXDIR=${TMP}
run_one broken
FIXDIR=${FIXDIR_SAVE}
gotc=$(fv broken familyC)
gotf=$(fv broken 'gate\.src\.sink\.faithful')
if [ "${gotc}" = "0.983152" ] && [ "${gotf}" = "22" ]; then
   note "the perturbed golden reads familyC ${gotc} faithful ${gotf} -- the harness notices"
else
   bad "the perturbed golden read familyC ${gotc:-MISSING} faithful ${gotf:-MISSING}," \
       "want 0.983152 and 22; this rehearsal is not measuring the blocks"
fi

# --- the full reading, for the record ----------------------------------------
echo
echo "== every gate, every fixture"
while read -r fix _ _ _; do
   [ -n "${fix}" ] || continue
   printf '  --- %s: %s\n' "${fix}" "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   grep -E '^(gate\.(src|b10|b11|cp)|C[0-9]\.|B1[01]\.|familyC)' \
      "${TMP}/facts-${fix}.txt" | sed 's/^/        /'
done <<EOF
${TABLE}
EOF

echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: the source-AST, B-10, B-11 and family-C blocks score every fixture as measured"
else
   echo "c-rehearsal: FAILURES above" >&2
fi
exit "${fail}"
