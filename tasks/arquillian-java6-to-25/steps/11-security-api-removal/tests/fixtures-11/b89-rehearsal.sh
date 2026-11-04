#!/usr/bin/env bash
# B-8 and B-9, rehearsed at image build time against trees whose answer is known.
#
#   bash /tests/fixtures/b89-rehearsal.sh
#
# The two gates are step 02's C-1 and C-2 lifted verbatim, but the block around
# them is new: the baseline reads for its C-3, C-4 and C-5 were removed, the
# variables were renamed, and the block was given its own else branch and its
# own two facts. None of that changes what the gates measure and all of it is
# an opportunity to have changed it by accident, so the gates are re-measured
# here rather than trusted because their bodies were copied.
#
# Same method as fixtures/c-rehearsal.sh: the block is READ OUT OF THE SHIPPED
# test.sh and executed, with a `java` shim that hands it a fixture instead of
# scanning a tree. Seven of step 02's eleven trees, chosen for the four
# properties this block can still get wrong:
#
#   pinned  b8 0.000000  b9 0.000000  the tree step 02 started from
#   golden  b8 1.000000  b9 1.000000  the step-02 golden solution
#   F1      b8 0.000000  b9 0.000000  one dummy Jupiter @Test bolted on each class
#   F2      b8 0.510204  b9 0.510204  half the classes migrated
#   F3      b8 1.000000  b9 0.000000  annotations moved, assertion calls left
#   F7      b8 0.000000  b9 0.000000  the 49 classes deleted outright
#   F8      b8 0.795918  b9 0.795918  10 of the 49 deleted from the golden tree
#
# F1 is the membership-versus-equality control: a class carrying both frameworks
# is not a migrated class, and B-8 reads it as unfinished. F3 is the
# arrival-versus-departure control: B-8 at 1 and B-9 at 0 on the same tree. F7
# and F8 are the deletion control, which is the one that matters most here --
# B-9 is a departure question, and the four figures above are what says a tree
# that deletes the classes answers it with 0 rather than with 1.
#
# The four figures step 02 measured for these two gates are reproduced exactly;
# they are the same gates, so any difference is a transcription error in this
# step's block and this is what would find it.
set -euo pipefail

TESTS="${TESTS:-/tests}"
FIX="${FIX:-${TESTS}/fixtures}"
W="$(mktemp -d)"
trap 'rm -rf "${W}"' EXIT

grep -E '^(fact|frac|clamp01)\(\) \{' "${TESTS}/test.sh" > "${W}/pre.sh"
n=$(grep -c . "${W}/pre.sh" || true)
[ "${n}" = 3 ] || { echo "b89-rehearsal: found ${n} of the 3 shipped helpers" >&2; exit 1; }

# The block, from its banner to the line that publishes B-9. Both ends checked:
# a sed range whose second address never matches returns the rest of the file.
sed -n '/^# -* B-8 and B-9: step 02, held$/,/^fact "B9\.junit4-held=/p' \
   "${TESTS}/test.sh" > "${W}/block.sh"
grep -q '^# -* B-8 and B-9' "${W}/block.sh" || { echo "b89-rehearsal: no B-8/B-9 banner" >&2; exit 1; }
grep -q '^fact "B9\.junit4-held=' "${W}/block.sh" || { echo "b89-rehearsal: no B9 line" >&2; exit 1; }
for g in B8.jupiter-held B9.junit4-held; do
   grep -qF "fact \"${g}=" "${W}/block.sh" \
      || { echo "b89-rehearsal: the extracted block has no ${g}" >&2; exit 1; }
done
# Exactly one reading of the tree, and it is the one the shim replaces. Step 02's
# C block had the same assertion for the same reason.
nj=$(grep -c 'java -cp "${GCP}" arqgate\.Junit5' "${W}/block.sh" || true)
[ "${nj}" = 1 ] || { echo "b89-rehearsal: ${nj} Junit5 invocations in the block" >&2; exit 1; }
# And none of step 02's other four gates came along for the ride. A block that
# still computed them would still score B-8 and B-9 correctly while carrying
# dead reads of baselines this step does not stage.
for stray in C3.assert-throws C4.runwith-retired C5.assertion-conservation \
             C6.still-java6-bytecode; do
   # `grep -q ... && { exit 1; }` reads better and is wrong here: when grep finds
   # nothing -- the case this loop is looking for -- the and-list returns
   # grep's non-zero status, and `set -e` ends the rehearsal with a pass.
   if grep -qF "fact \"${stray}=" "${W}/block.sh"; then
      echo "b89-rehearsal: step 02's ${stray} is still in the block" >&2
      exit 1
   fi
done

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
      echo 'GCP=shimmed'
      echo 'nclass=1'
      printf 'mirror=%s/mirror\n' "${w}"
      printf 'printf %%s\\\\n "%s/mirror" > "${WORK}/modulejars.txt"\n' "${w}"
      printf 'java() { cp "%s" "${WORK}/j5.txt"; }\n' "${fixture}"
      cat "${W}/block.sh"
   } > "${w}/run.sh"
   bash "${w}/run.sh" > "${w}/out.txt" 2>&1 || {
      echo "b89-rehearsal: the block exited non-zero on ${fixture}" >&2
      sed 's/^/    /' "${w}/out.txt" >&2
      return 1
   }
   printf '%s %s\n' \
      "$(sed -n 's/^B8\.jupiter-held=//p' "${w}/facts.txt" | head -1)" \
      "$(sed -n 's/^B9\.junit4-held=//p' "${w}/facts.txt" | head -1)"
}

# The fixtures were built against a must-migrate set mkfix.sh derived for
# itself. This is the one the SHIPPED block derives, from the same two baselines.
# They have to be the same 49 names or the fixtures are pointed at a different
# question than the gates answer.
run_one "${FIX}/j5-golden.txt" > /dev/null
if ! diff -q "${FIX}/must.txt" "${W}/run/must.txt" > /dev/null; then
   echo "b89-rehearsal: mkfix.sh and the shipped block disagree about the must-migrate set" >&2
   diff "${FIX}/must.txt" "${W}/run/must.txt" >&2 || :
   exit 1
fi
echo "b89-rehearsal: must-migrate set agrees with the scorer's, $(grep -c . "${FIX}/must.txt") classes"

fail=0
while read -r name w8 w9 tree; do
   [ -n "${name}" ] || continue
   got=$(run_one "${tree}")
   printf 'B8/B9 fixture %-7s want="%s %s" got="%s"\n' "${name}" "${w8}" "${w9}" "${got}"
   [ "${got}" = "${w8} ${w9}" ] || fail=1
done <<TABLE
pinned  0.000000 0.000000 ${FIX}/j5-pinned.txt
golden  1.000000 1.000000 ${FIX}/j5-golden.txt
F1      0.000000 0.000000 ${FIX}/F1.txt
F2      0.510204 0.510204 ${FIX}/F2.txt
F3      1.000000 0.000000 ${FIX}/F3.txt
F7      0.000000 0.000000 ${FIX}/F7.txt
F8      0.795918 0.795918 ${FIX}/F8.txt
TABLE

# pinned, F1 and F7 all read "0.000000 0.000000" by three different routes, so
# the facts that tell them apart are asserted. Without this the trio proves only
# that some route to zero exists.
run_one "${FIX}/j5-pinned.txt" > /dev/null
grep -qxF 'gate.j5.present=49'  "${W}/run/facts.txt" || { echo "b89-rehearsal: the pinned tree is missing classes" >&2; fail=1; }
grep -qxF 'gate.j5.jupiter=0'   "${W}/run/facts.txt" || { echo "b89-rehearsal: the pinned tree has Jupiter tests" >&2; fail=1; }
run_one "${FIX}/F1.txt" > /dev/null
grep -qxF 'gate.j5.present=49'  "${W}/run/facts.txt" || { echo "b89-rehearsal: F1 is missing classes" >&2; fail=1; }
grep -qxF 'gate.j5.jupiter=0'   "${W}/run/facts.txt" || { echo "b89-rehearsal: F1's dummy Jupiter tests were credited" >&2; fail=1; }
grep -qxF 'gate.j5.offenders=49' "${W}/run/facts.txt" || { echo "b89-rehearsal: F1's JUnit 4 references were not seen" >&2; fail=1; }
run_one "${FIX}/F7.txt" > /dev/null
grep -qxF 'gate.j5.present=0'   "${W}/run/facts.txt" || { echo "b89-rehearsal: F7's deleted classes were counted as present" >&2; fail=1; }
grep -qxF 'gate.j5.offenders=0' "${W}/run/facts.txt" || { echo "b89-rehearsal: F7 has offenders it cannot have" >&2; fail=1; }

# The gate-by-gate reading for all seven, so the build log carries what each
# gate said and not only the two scores.
for t in j5-pinned j5-golden F1 F2 F3 F7 F8; do
   run_one "${FIX}/${t}.txt" > /dev/null
   echo "--- ${t}"
   grep -E '^(gate\.j5|B[89]\.)' "${W}/run/facts.txt" | sed 's/^/    /'
done

[ "${fail}" = 0 ] || { echo "b89-rehearsal: a fixture did not score what it must" >&2; exit 1; }
echo "b89-rehearsal: B-8 and B-9 reproduce step 02's four figures on all seven trees"
