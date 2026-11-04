#!/usr/bin/env bash
# Family C, rehearsed at image build time against trees whose answer is known.
#
#   bash /tests/fixtures/c-rehearsal.sh
#
# The whole C block is READ OUT OF THE SHIPPED test.sh and executed, rather than
# restated here: a rehearsal that carries its own copy of the arithmetic passes
# while the scorer says something else, which is the failure mode it exists to
# prevent. The only things supplied from outside are the four names the block
# would otherwise get from the run -- WORK, the mirror, the module-jar list and
# GCP -- and a `java` shim that hands it a fixture instead of scanning a tree.
# Every path it reads for the answers (baseline/tests.txt, baseline/compat.txt,
# baseline/junit5.txt) is the real staged one.
#
# Eleven trees. Two are measurements and nine are edits of a measurement; see
# fixtures/mkfix.sh for what each edit is and which gate it points at.
#
#   pinned   the tree step 02 starts from             C = 0.000000
#   golden   the step-02 golden solution              C = 1.000000
#   F1       one dummy Jupiter @Test per class        C = 0.000000
#   F2       half the classes migrated                C = 0.474725
#   F3       annotations moved, assertions left       C = 0.625000
#   F5       no assertThrows                          C = 0.925000
#   F6       assertions thinned to 60                 C = 0.871035
#   F7       the 49 classes deleted outright          C = 0.000000
#   F8       10 of the 49 deleted from the golden     C = 0.757703
#   F9       the assertThrows calls moved off-site    C = 0.925000
#   F10      the golden tree compiled to major 52     C = 0.850000
#
# Both ends are asserted, not just the ceiling: a gate that always returns 1
# passes "golden = 1", and a gate that always returns 0 passes "pinned = 0".
# The nine in between are what make those two mean something, and each is
# asserted to six decimal places rather than to a band, because a gate that has
# drifted lands somewhere -- just not here.
#
# Three of the middle five are worth reading as findings rather than as numbers:
#
#   F1 scores 0.000000 and not 0.300000. C-1 asks whether each must-migrate
#      class IS a Jupiter test, not whether it CONTAINS one; a class carrying
#      both frameworks reads as unfinished. Written as set membership -- which
#      is the obvious way to write it -- this same fixture scored C-1 = 1.
#   F3 scores 0.625000 with C-1 at 1 and C-2 and C-5 at 0. Moving the
#      annotations and leaving org.junit.Assert in every method body is a real
#      half-migration, and it is the one that shows arrival and departure are
#      two separate measurements.
#   F2 scores C-5 at 0 even though it has 115 Jupiter assertion calls. The
#      guard is `jupiter >= legacy`, and this tree has 168 legacy ones: a tree
#      that has moved less than half its assertions has not moved them.
#   F9 scores 0.925000 and F5 scores the same 0.925000 by the opposite route,
#      which is why this pair is asserted on its gate facts and not only on its
#      total. F5 has no assertThrows calls at all (gate.j5.assertthrows=0); F9
#      has all nineteen and not one of them in a class that owned a frozen
#      @Test(expected=) site (assertthrows=19, assertthrows.onsite=0). Both land
#      on C-3 = 0.5 because each earns the departure half and neither earns the
#      arrival half. Measured against the scorer as it stood before the arrival
#      half was anchored to the sites, the same F9 scored family C = 1.000000 --
#      indistinguishable from the golden tree, on a tree where not one of the
#      nineteen sites was rewritten.
#   F10 scores below the golden tree on C-6 alone: it is the golden solution
#      with every must-migrate class recompiled to major 52 and not one other
#      byte changed, so C-1 through C-5 read exactly what they read on the
#      golden tree. It exists because a review round observed that before C-6
#      the only gate asking whether the rewritten tests are still Java 6
#      bytecode was B-4, worth 0.035 of the step -- the floor arithmetic caps
#      family B, and every weight on a gate the predecessor already passes
#      lands in the predecessor's own score. C-6 asks the same question
#      step-locally, of the classes that BECAME Jupiter tests, which the
#      predecessor has none of; it therefore carries weight without moving the
#      floor. F10 is what says the gate can tell 50 from 52.
#   F7 scores 0.000000 and is the one fixture here that was written against a
#      measured failure rather than against a suspicion. C-2, C-3's departure
#      half and C-4 all ask whether something retired is gone, and all three
#      were written as "baseline minus what is still there" -- so a tree that
#      deletes the classes answers them perfectly. Measured end to end, the
#      step-01 golden tree with every src/test/java removed took 0.654375
#      overall, above this step's own floor. The gates now consult presence
#      first, F7 is the tree that says so, and F8 is the same move at a fifth
#      of the scale so that the answer is a slope and not a cliff.
set -euo pipefail

TESTS="${TESTS:-/tests}"
FIX="${FIX:-${TESTS}/fixtures}"
W="$(mktemp -d)"
trap 'rm -rf "${W}"' EXIT

# The shipped helpers, by definition rather than by copy. Three one-liners; if
# any of them stops being a one-liner this count is what says so.
grep -E '^(fact|frac|clamp01)\(\) \{' "${TESTS}/test.sh" > "${W}/pre.sh"
n=$(grep -c . "${W}/pre.sh" || true)
[ "${n}" = 3 ] || { echo "c-rehearsal: found ${n} of the 3 shipped helpers" >&2; exit 1; }

# The shipped family-C block, from its banner to the line that publishes its
# score. Both ends are checked for, because a sed range whose second address
# never matches silently returns the whole rest of the file.
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${TESTS}/test.sh" > "${W}/cblock.sh"
grep -q '^# =* family C$'   "${W}/cblock.sh" || { echo "c-rehearsal: no family C banner" >&2; exit 1; }
grep -q '^fact "familyC='   "${W}/cblock.sh" || { echo "c-rehearsal: no familyC line" >&2; exit 1; }
# Every one of the six gates has to be in there. A range that clipped early
# would still start and end correctly if the banner moved.
for g in C1.jupiter-population C2.junit4-retired C3.assert-throws \
         C4.runwith-retired C5.assertion-conservation C6.still-java6-bytecode; do
   grep -qF "fact \"${g}=" "${W}/cblock.sh" \
      || { echo "c-rehearsal: the extracted block has no ${g}" >&2; exit 1; }
done
# And it must be the block that reads a fixture through arqgate.Junit5 exactly
# once -- the shim below is the only java in it.
nj=$(grep -c 'java -cp "${GCP}" arqgate\.Junit5' "${W}/cblock.sh" || true)
[ "${nj}" = 1 ] || { echo "c-rehearsal: ${nj} Junit5 invocations in the block" >&2; exit 1; }

run_one()
{
   local fixture=$1
   local w="${W}/run"
   rm -rf "${w}"
   mkdir -p "${w}/mirror"
   {
      echo 'set -uo pipefail'
      printf 'WORK=%s\n' "${w}"
      printf 'FACTS=%s/facts.txt\n' "${w}"
      printf ': > "${FACTS}"\n'
      cat "${W}/pre.sh"
      # What the run supplies and the block does not compute. GCP is never
      # dereferenced because the only java call is shimmed; it is set because
      # the block runs under `set -u`.
      echo 'GCP=shimmed'
      echo 'nclass=1'
      printf 'mirror=%s/mirror\n' "${w}"
      printf 'printf %%s\\\\n "%s/mirror" > "${WORK}/modulejars.txt"\n' "${w}"
      printf 'java() { cp "%s" "${WORK}/j5.txt"; }\n' "${fixture}"
      cat "${W}/cblock.sh"
   } > "${w}/run.sh"
   bash "${w}/run.sh" > "${w}/out.txt" 2>&1 || {
      echo "c-rehearsal: the block exited non-zero on ${fixture}" >&2
      sed 's/^/    /' "${w}/out.txt" >&2
      return 1
   }
   # From the facts file rather than from stdout: that is the channel the
   # verifier's own score block reads.
   sed -n 's/^familyC=//p' "${w}/facts.txt" | head -1
}

# The fixtures were built against a must-migrate set mkfix.sh derived for
# itself. This is the one the SHIPPED block derives, from the same two baselines,
# with its own three lines of shell. They have to be the same 49 names or the
# fixtures are pointed at a different question than the gate answers.
run_one "${FIX}/j5-golden.txt" > /dev/null
if ! diff -q "${FIX}/must.txt" "${W}/run/must.txt" > /dev/null; then
   echo "c-rehearsal: mkfix.sh and the shipped C block disagree about the must-migrate set" >&2
   diff "${FIX}/must.txt" "${W}/run/must.txt" >&2 || :
   exit 1
fi
echo "c-rehearsal: must-migrate set agrees with the scorer's, $(grep -c . "${FIX}/must.txt") classes"

fail=0
while read -r name want tree; do
   [ -n "${name}" ] || continue
   got=$(run_one "${tree}")
   printf 'family C fixture %-8s want=%s got=%s\n' "${name}" "${want}" "${got}"
   [ "${got}" = "${want}" ] || fail=1
done <<TABLE
pinned  0.000000 ${FIX}/j5-pinned.txt
golden  1.000000 ${FIX}/j5-golden.txt
F1      0.000000 ${FIX}/F1.txt
F2      0.474725 ${FIX}/F2.txt
F3      0.625000 ${FIX}/F3.txt
F5      0.925000 ${FIX}/F5.txt
F6      0.871035 ${FIX}/F6.txt
F7      0.000000 ${FIX}/F7.txt
F8      0.757703 ${FIX}/F8.txt
F9      0.925000 ${FIX}/F9.txt
F10     0.850000 ${FIX}/F10.txt
TABLE

# F5 and F9 share a total, so the two facts that tell them apart are asserted
# directly. Without this the pair proves only that some route to 0.925000
# exists; with it, each proves the route it was built for.
run_one "${FIX}/F5.txt" > /dev/null
grep -qxF 'gate.j5.assertthrows=0'        "${W}/run/facts.txt" || { echo "c-rehearsal: F5 reports assertThrows calls" >&2; fail=1; }
grep -qxF 'gate.j5.assertthrows.onsite=0' "${W}/run/facts.txt" || { echo "c-rehearsal: F5 credits an on-site call" >&2; fail=1; }
run_one "${FIX}/F9.txt" > /dev/null
grep -qxF 'gate.j5.assertthrows=19'       "${W}/run/facts.txt" || { echo "c-rehearsal: F9 lost its assertThrows calls" >&2; fail=1; }
grep -qxF 'gate.j5.assertthrows.onsite=0' "${W}/run/facts.txt" || { echo "c-rehearsal: F9's moved calls were credited to a site" >&2; fail=1; }

# F10 differs from the golden tree in one gate, so that gate is asserted at both
# ends: 49 Jupiter tests at major 50 on the golden tree, 0 on F10. A C-6 that
# had drifted into counting something else could still separate the two totals.
run_one "${FIX}/j5-golden.txt" > /dev/null
grep -qxF 'gate.j5.jupiter.major50=49' "${W}/run/facts.txt" || { echo "c-rehearsal: the golden tree is not 49 Jupiter tests at major 50" >&2; fail=1; }
run_one "${FIX}/F10.txt" > /dev/null
grep -qxF 'gate.j5.jupiter.major50=0' "${W}/run/facts.txt" || { echo "c-rehearsal: F10's major-52 Jupiter tests were credited as major 50" >&2; fail=1; }

# The gate-by-gate reading for all eleven, so the build log carries what each gate
# said and not only the weighted total. A total can be right for the wrong
# reasons -- two gates off by compensating amounts -- and this is where that
# would be visible.
for t in j5-pinned j5-golden F1 F2 F3 F5 F6 F7 F8 F9 F10; do
   run_one "${FIX}/${t}.txt" > /dev/null
   echo "--- ${t}"
   grep -E '^(gate\.j5|C[1-6]\.)' "${W}/run/facts.txt" | sed 's/^/    /'
done

[ "${fail}" = 0 ] || { echo "c-rehearsal: a fixture did not score what it must" >&2; exit 1; }
echo "c-rehearsal: family C scores all eleven trees as measured"
