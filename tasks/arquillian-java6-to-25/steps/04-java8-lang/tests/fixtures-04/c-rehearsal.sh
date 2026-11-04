#!/usr/bin/env bash
# Rehearse the three scoring blocks that read the typed AST -- the hoisted
# source-AST block, B-10, and the whole of family C -- against trees whose
# answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the three
# blocks are lifted out of the SHIPPED test.sh by their own banners, composed in
# file order behind a srcgate() that hands them a fixture's report instead of
# compiling anything, and run once per fixture. What is asserted is the
# arithmetic over the report, which is the part of these gates that no other
# check exercises.
#
# Two blocks and not one, which is where this departs from step 03's rehearsal.
# The three compiles were hoisted above family B so that B-10 could read them,
# so the block under test is no longer contiguous: the counters are read in one
# place, spent in a family-B gate in a second, and spent again in family C in a
# third. All three are extracted, and the extraction asserts they are in that
# order in the file -- a hoist that silently un-hoisted would leave B-10 reading
# variables that do not exist yet, and under `set -u` that is a dead verifier
# rather than a wrong number.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step04/
# mkfix04.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all.
#
# Both figures are compared AS STRINGS, because the shell's spelling of a gate
# is part of what is being pinned: `c4=0` and `cpen=1` are literals the block
# assigns and never reaches, while a gate it computes publishes "%.6f". They are
# the same number and not the same fact, and normalising them would lose the
# difference between a gate that scored zero and one that never ran.
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

echo "== extracting the three blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: minf() moved up here from the head of family C when B-10 started
# needing it, and a rehearsal that quietly picked up three would be composing a
# block whose min() is missing.
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
grep -c . "${TMP}/block-src.sh" | grep -qE '^[0-9]+$' || bad "source-AST block empty"
tail -2 "${TMP}/block-src.sh" | grep -qx '}' \
   || bad "the source-AST block does not end with covered()'s closing brace"

# --- block 2: B-10 -----------------------------------------------------------
sed -n '/^# -* B-10: step 03, held$/,/^fact "B10.java7-held=/p' "${SRC}" \
   > "${TMP}/block-b10.sh"
head -1 "${TMP}/block-b10.sh" | grep -q 'B-10: step 03, held$' \
   || bad "the B-10 block does not start at its banner"
tail -1 "${TMP}/block-b10.sh" | grep -q '^fact "B10.java7-held=' \
   || bad "the B-10 block does not end at its own fact"

# --- block 3: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

for f in block-src block-b10 block-c; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The three are disjoint and in file order. This is the hoist, asserted: B-10
# spends counters the first block reads, so the first block has to come first,
# and family C spends them again after that.
s1=$(grep -n '^# -* the source AST, compiled once$' "${SRC}" | head -1 | cut -d: -f1)
s2=$(grep -n '^# -* B-10: step 03, held$' "${SRC}" | head -1 | cut -d: -f1)
s3=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
e1=$(grep -n '^# -* family B$' "${SRC}" | head -1 | cut -d: -f1)
e2=$(grep -n '^fact "B10.java7-held=' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 s2 s3 e1 e2; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] \
   && [ "${s2}" -lt "${e2}" ] && [ "${e2}" -lt "${s3}" ]; then
   note "order: source-AST ${s1}..${e1} < B-10 ${s2}..${e2} < family C ${s3}"
else
   bad "the three blocks are not disjoint and in order: ${s1} ${e1} ${s2} ${e2} ${s3}"
fi

# Every gate this rehearsal claims to exercise has to be INSIDE one of the three
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.base.anonfn='
inblock block-src 'gate.src.ok18='
inblock block-src 'gate.src=broken'
inblock block-src 'covered() {'
inblock block-src 'coveredboth() {'
inblock block-src 'gate.src.base.loop='
inblock block-b10 'gate.b10.diamond='
inblock block-b10 'gate.b10.resources='
inblock block-b10 'gate.b10.nio='
inblock block-b10 'gate.b10.nio.behaviour.broken='
inblock block-c 'C1.lambdas='
inblock block-c 'C2.functionalinterface='
inblock block-c 'C3.streams='
inblock block-c 'C4.requires-java8='
inblock block-c 'gate.src.penalty='
inblock block-c 'gate.src.loop=absent'
inblock block-c 'gate.src.loop.removed='

# The three compiles happen in the first block and NOWHERE else. This is the
# other half of the hoist: if a compile crept back into family C, family C would
# be measuring a different report from the one B-10 scored, and the two gates
# would disagree about the same tree.
n=$(grep -c 'srcgate ' "${TMP}/block-src.sh" || true)
[ "${n}" = 3 ] || bad "expected 3 srcgate calls in the source-AST block, found ${n}"
for blk in block-b10 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the three blocks may RUN a compiler, a build or a container.
# They are arithmetic over a report -- that is what makes them rehearsable at
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
# srcgate(), replaced. Same signature, same four artefacts, same echoed answer:
# the fixture's report becomes src-<tag>.txt, the fixture's answers become the
# .env the block reads javac.errors out of, and the fixture's javac log becomes
# what C-4 greps for level errors.
#
# The 1.7 error count goes into the 1.7 .env ALONE. C-4 reads that one file, and
# a shim that wrote the same count into all three would hand the block a 1.8
# compile that both succeeded and raised 47 errors -- a tree no compiler
# produces, and a fixture that could not tell a gate reading the wrong .env from
# one reading the right one.
cat > "${TMP}/shim.sh" <<'SHIM'
TREE=/nonexistent
srcgate() {
   local tag=$1 lvl=${2:-} ok errs=0
   cp "${FIXDIR}/report.txt" "${WORK}/src-${tag}.txt"
   echo "the fixture's compile, not a real one" > "${WORK}/src-${tag}.log"
   ok=$(sed -n "s/^ok${tag}=//p" "${FIXDIR}/answers.txt" | head -1)
   if [ "${tag}" = 17 ]; then
      errs=$(sed -n 's/^errors=//p' "${FIXDIR}/answers.txt" | head -1)
      cp "${FIXDIR}/javac17.log" "${WORK}/src-${tag}.txt.javac.log"
   fi
   { echo "javac.level=${lvl:-default}"
     echo "javac.errors=${errs:-0}"
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
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- what each fixture must score --------------------------------------------
# fixture  familyC   B10
# Transcribed by hand from mkfix04.py's printed table. The two ends are the
# rows that carry the step's floor argument: `pinned` is the tree this rung is
# handed and must read familyC 0 with B-10 in full, and `golden` must read both
# in full -- B-10 included, which is the assertion that the gate does not
# punish the golden for the two step-03 figures that legitimately move.
TABLE=$(cat <<'EOF'
pinned   0.000000 1.000000
golden   1.000000 1.000000
H1       0.550000 1.000000
H2       0.850000 1.000000
H3       0.850000 1.000000
H4       0.850000 1.000000
H5       0.850000 1.000000
H6       1.000000 1.000000
H7       0.850000 1.000000
H8       0.925000 1.000000
H9       0.400000 1.000000
H10      0.400000 1.000000
H11      0.700000 1.000000
H25      0.400000 1.000000
H26      0.400000 1.000000
H27      0.400000 1.000000
H28      0.400000 1.000000
H29      0.400000 1.000000
H12      0.900000 1.000000
H13      0.900000 1.000000
H14      0.900000 1.000000
H15      0.000000 0
H16      0.005000 1.000000
H17      1.000000 1.000000
H18      0.500000 1.000000
H19      0.500000 1.000000
H20      1.000000 0.600000
H21      1.000000 0.700000
H22      1.000000 0.700000
H23      1.000000 0.499120
H24      1.000000 0.850000
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

echo
echo "== running the composed blocks against each fixture"
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture familyC B10 C1 C2 C3 C4 why
while read -r fix wantc wantb; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   gotc=$(fv "${fix}" familyC)
   gotb=$(fv "${fix}" 'B10\.java7-held')
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${gotc:-MISSING}" "${gotb:-MISSING}" \
      "$(fv "${fix}" 'C1\.lambdas')" "$(fv "${fix}" 'C2\.functionalinterface')" \
      "$(fv "${fix}" 'C3\.streams')" "$(fv "${fix}" 'C4\.requires-java8')" \
      "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   [ "${gotc}" = "${wantc}" ] || bad "${fix}: familyC want ${wantc} got ${gotc:-MISSING}"
   [ "${gotb}" = "${wantb}" ] || bad "${fix}: B10 want ${wantb} got ${gotb:-MISSING}"
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
      printf '  ok   %-8s %-30s %s\n' "${fix}" "${key}" "${got}"
   else
      bad "${fix}: ${key} want ${want} got ${got:-MISSING}"
   fi
}

# pinned and golden: the two ends, read in full.
factck pinned gate.src.base.anonfn 48
factck pinned gate.src.need.anonfn 36
factck pinned gate.src.anonfn.converted 0
factck pinned gate.src.lambdafiles.covered 0
factck pinned gate.src.sam.want 24
factck pinned gate.src.sam.annotated 0
factck pinned gate.src.collect.migrated 0
factck pinned gate.src.streamfiles.covered 0
factck pinned gate.src.collectmethods.covered 0
factck pinned gate.src.opmethods 0
factck pinned gate.src.ok17 yes
factck pinned gate.b10.nonanon 23
factck pinned gate.b10.explicit.returned 0
factck golden gate.src.anonfn.converted 36
factck golden gate.src.lambdafiles.covered 20
factck golden gate.src.sam.annotated 24
factck golden gate.src.collect.migrated 24
factck golden gate.src.streamfiles.covered 14
factck golden gate.src.collectmethods.covered 24
factck golden gate.src.opmethods 24
factck golden gate.src.loop.now 115
factck golden gate.src.loop.removed 24
factck golden gate.src.streamopfiles 14
factck pinned gate.src.loop.now 139
factck pinned gate.src.loop.removed 0
factck pinned gate.src.streamopfiles 0
factck golden gate.src.17.errors 47
factck golden gate.src.17.levelerrors 47
# The golden's generics figures MOVE and B-10 still reads 1.0. This pair is the
# assertion that the gate reads the non-anonymous remainder and not the raw
# count: 36 - 13 is the same 23 that 50 - 27 was.
factck golden gate.src.explicit 36
factck golden gate.src.anon 13
factck golden gate.b10.nonanon 23
factck golden gate.b10.explicit.returned 0

# H2 / H3 / H4 / H5 all read familyC 0.850000, and they are four different
# trees. Deleting, adding alongside, adding in the wrong files, and making the
# bodies inconvertible are separated here and nowhere else.
factck H2 gate.src.anonfn.converted 36
factck H2 gate.src.lambdafiles.covered 0
factck H3 gate.src.anonfn.converted 0
factck H3 gate.src.lambdafiles.covered 20
factck H4 gate.src.anonfn.converted 36
factck H4 gate.src.lambdafiles.covered 0
# The trick the departure half is chosen to survive: `convertible` collapses to
# 0 and `size` does not move, so the gate that reads `size` reports no work.
factck H5 gate.src.conv 0
factck H5 gate.src.anonfn 48
factck H5 gate.src.anonfn.converted 0

# H6: every conversion spelled as a method reference. Reads 1.000000, which is
# the whole point -- the arrival half counts a lambda OR a method reference, and
# a gate that read lambda[] records alone would score this tree 0 for doing the
# work in the other correct spelling.
factck H6 gate.src.lambda 0
factck H6 gate.src.mref 36
factck H6 gate.src.lambdafiles.covered 20
factck H6 C1.lambdas 1.000000

# H7: 24 annotated interfaces, none of them the tree's. The count says 24 and
# the set says 0.
factck H7 gate.src.samann 24
factck H7 gate.src.sam.annotated 0

# H9 / H10 / H25 / H26 / H27 / H28 / H29 all share 0.400000, and they are seven
# different trees that reach it through five different terms of C-3. The total
# is the same by construction -- C-3 is the only gate any of them fails and it
# is worth 0.60 of family C -- so the total says nothing at all about which of
# them was caught, and each is separated below by the fact that caught it.
#
# H9 / H10: the loops deleted, versus the loops deleted and 24 pipelines written
# somewhere else. Both fail arrival.
factck H9 gate.src.collect.migrated 24
factck H9 gate.src.streams 0
factck H9 gate.src.streamfiles.covered 0
factck H10 gate.src.streams 24
factck H10 gate.src.streamfiles.covered 0
factck H10 gate.src.collectmethods.covered 0

# H25 / H26 / H27 are round 1 of this step's static review, made scorable. The
# review found C-3 satisfiable without a pipeline by two independent routes, and
# each of these takes one of them; all three scored 1.000000 against the gate as
# it stood before the round.
#
# H25: the 24 collecting loops respelled as `while`, with pipelines written
# alongside them. Departure and arrival both read in full -- `foreach.collect`
# is 0 and all 14 frozen files hold a producer and a stage -- and the whole-tree
# loop census is the only thing that knows the loops are still there.
factck H25 gate.src.collect.migrated 24
factck H25 gate.src.streamfiles.covered 14
factck H25 gate.src.base.loop 139
factck H25 gate.src.loop.now 139
factck H25 gate.src.loop.removed 0
factck H25 C3.streams 0.000000

# H26: `xs.stream();` as a statement, in each of the 14 right files. A producer
# that enters the stream world and comes straight back. The producer census
# reads 24 in the right 14 files and the stage census reads none, so the
# conjunction coveredboth() asks for is what refuses it.
factck H26 gate.src.streams 24
factck H26 gate.src.streamfiles 14
factck H26 gate.src.streamopfiles 0
factck H26 gate.src.streamfiles.covered 0
factck H26 gate.src.collectmethods.covered 0
factck H26 gate.src.opmethods 0
factck H26 C3.streams 0.000000

# H28 is round 2 of the static review, and it is the only fixture here whose
# two coverage facts DISAGREE. One `xs.stream().filter(..)` per frozen file, in
# a method that never held a loop: the file-scope conjunction reads 14 of 14 and
# the method-scope conjunction reads 0 of 24. Fourteen statements bought 0.60 of
# family C under the round-1 gate, and the pair of facts below is the whole
# difference between the two rounds, measured rather than described.
factck H28 gate.src.streams 24
factck H28 gate.src.streamfiles 14
factck H28 gate.src.streamopfiles 14
factck H28 gate.src.streamfiles.covered 14
factck H28 gate.src.collectmethods.covered 0
factck H28 gate.src.opmethods 14
factck H28 C3.streams 0.000000

# H29 is H27 one term over: a verifier built from a gate jar too old to write
# the in= field on a record. Nothing can be bucketed, so nothing can be paired,
# and the arrival half must read 0 rather than inheriting the file-scope answer.
# The published `absent` is what says which of the two it is -- a golden tree
# and a stale jar would otherwise be told apart by nothing at all.
factck H29 gate.src.streams 24
factck H29 gate.src.streamfiles.covered 14
factck H29 gate.src.collectmethods.covered 0
factck H29 gate.src.opmethods absent
factck H29 C3.streams 0.000000
# ...and a tree scored by a gate that CAN bucket must not publish it, or
# `absent` separates nothing.
for fix in golden H26 H28; do
   got=$(fv "${fix}" 'gate\.src\.opmethods')
   [ "${got}" = absent ] && bad "${fix}: published gate.src.opmethods=absent with in= fields present"
done

# H27 is not a candidate at all: it is this verifier built from a gate jar too
# old to emit the loop census. `${now_loop:-0}` would have read the absence as
# 139 loops removed and handed the no-reshape term full credit, which is the
# ladder's seventh trap exactly -- a guard that disappears without a symptom.
# The gate scores zero and says which of the two it is.
factck H27 gate.src.loop absent
factck H27 gate.src.loop.now absent
factck H27 gate.src.loop.removed 0
factck H27 C3.streams 0.000000
# ...and the three clean trees above it must NOT publish that fact, or `absent`
# would be something the scorer says about every tree and would separate
# nothing.
for fix in golden H25 H26; do
   got=$(fv "${fix}" 'gate\.src\.loop')
   [ -z "${got}" ] || bad "${fix}: published gate.src.loop=${got} with a census present"
done

# H12 / H13 / H14 share 0.900000 and are three different refusals of C-4.
factck H12 gate.src.ok17 yes
factck H13 gate.src.17.errors 7
factck H13 gate.src.17.levelerrors 3
factck H14 gate.src.ok18 no
factck H14 gate.src.okdef yes
# ...and none of the three may leave a breadth reading behind: the fact is
# published only where the taxonomy check passed.
for fix in H12 H13 H14; do
   got=$(fv "${fix}" 'gate\.src\.17\.breadth')
   [ -z "${got}" ] || bad "${fix}: published a breadth of ${got} with C-4 refused"
done

# H15: nothing compiled. Both blocks must say so and neither may report a clean
# zero that reads like a finding about the tree.
factck H15 gate.src broken
factck H15 gate.b10 no-report

# H16 / H17: the two ends of the breadth scale. One lambda buys 1/20 and not the
# flip's whole 0.15; exactly 20 file-features buys all of it.
factck H16 gate.src.17.levelerrors 1
factck H16 gate.src.17.breadth 0.050000
factck H17 gate.src.17.levelerrors 20
factck H17 gate.src.17.breadth 1.000000

# H18 / H19 share 0.500000 and are the two halves of the multiplier.
factck H18 gate.src.raw.added 5
factck H18 gate.src.penalty 0.500000
factck H19 gate.src.raw.added 0
factck H19 gate.src.sup 50
factck H19 gate.src.penalty 0.500000

# B-10's three thirds, each undone on its own, and the proviso.
factck H20 gate.b10.explicit.returned 93
factck H20 gate.b10.diamond 0.000000
factck H21 gate.b10.close 11
factck H21 gate.b10.resources 0.000000
factck H22 gate.b10.io 13
factck H22 gate.b10.io.returned 11
factck H22 gate.b10.nioarrive 0
factck H22 gate.b10.nio 0.000000
# H23 gives about half of each third back, and the three sub-scores have to come
# out different from each other: one number for all three would mean the block
# was reading one construct and spending it thrice.
factck H23 gate.b10.diamond 0.505376
factck H23 gate.b10.resources 0.444444
factck H23 gate.b10.nio 0.545455
factck H24 gate.b10.nio.behaviour.broken 4
factck H24 gate.b10.nio 0.500000
# ...and family C is untouched by all four, which is what makes B-10 a separate
# gate rather than a second reading of the same thing.
for fix in H20 H21 H22 H23 H24; do
   factck "${fix}" familyC 1.000000
done

# --- the control on the control ----------------------------------------------
# Everything above is a clean run, and a harness that cannot fail proves nothing
# about the runs that passed. So the golden's report is perturbed in one counter
# -- the anonymous bodies put back -- and the composed blocks are required to
# NOTICE: familyC has to fall to H3's 0.850000 and C-1 to zero. If this reads
# 1.000000 the rehearsal above was measuring nothing.
echo
echo "== the negative control: does this harness notice a wrong tree?"
cp -r "${FIXDIR}/golden" "${TMP}/broken"
sed -i 's/^anonymousfunctional\.size=12$/anonymousfunctional.size=48/' \
   "${TMP}/broken/report.txt"
grep -qx 'anonymousfunctional.size=48' "${TMP}/broken/report.txt" \
   || bad "the negative control's edit did not land"
FIXDIR_SAVE=${FIXDIR}; FIXDIR=${TMP}
run_one broken
FIXDIR=${FIXDIR_SAVE}
gotc=$(fv broken familyC)
gotc1=$(fv broken 'C1\.lambdas')
if [ "${gotc}" = "0.850000" ] && [ "${gotc1}" = "0.000000" ]; then
   note "the perturbed golden reads familyC ${gotc} C1 ${gotc1} -- the harness notices"
else
   bad "the perturbed golden read familyC ${gotc:-MISSING} C1 ${gotc1:-MISSING}," \
       "want 0.850000 and 0.000000; this rehearsal is not measuring the blocks"
fi

# --- the full reading, for the record ----------------------------------------
echo
echo "== every gate, every fixture"
while read -r fix _ _; do
   [ -n "${fix}" ] || continue
   printf '  --- %s: %s\n' "${fix}" "$(head -1 "${FIXDIR}/${fix}/why.txt")"
   grep -E '^(gate\.(src|b10)|C[0-9]\.|B10\.|familyC)' "${TMP}/facts-${fix}.txt" \
      | sed 's/^/        /'
done <<EOF
${TABLE}
EOF

echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: the source-AST, B-10 and family-C blocks score every fixture as measured"
else
   echo "c-rehearsal: FAILURES above" >&2
fi
exit "${fail}"
