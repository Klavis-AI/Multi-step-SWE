#!/usr/bin/env bash
# Family C, rehearsed at image build time against reports whose answer is known.
#
#   bash /tests/fixtures/c-rehearsal.sh
#
# The whole C block is READ OUT OF THE SHIPPED test.sh and executed, rather than
# restated here: a rehearsal that carries its own copy of the arithmetic passes
# while the scorer says something else, which is the failure mode it exists to
# prevent. The only things supplied from outside are the three names the block
# would otherwise get from the run -- WORK, FACTS and the behaviour summary --
# and a replacement for srcgate(), the one function in the block that touches
# the tree. Every path it reads for its expectations (baseline/source.txt,
# baseline/target.txt, baseline/nioids.txt) is the real staged one.
#
# Nineteen reports. Two are measurements of real trees and seventeen are edits of
# a measurement; fixtures/mkfix.py says what each edit is, which gate it points
# at, and computes the figures asserted below from an independent
# implementation of the same composition.
#
#   pinned  0.000000  the tree step 03 starts from
#   golden  1.000000  the step-03 golden solution
#   G1      0.551939  about half of each of the three constructs
#   G2      0.650000  the type arguments deleted, no diamond written
#   G3      0.650000  93 new diamond sites beside the 116 untouched ones
#   G13     0.000000  the type arguments dropped to raw types, not to diamonds
#   G4      0.750000  the 11 close() calls deleted, no resource clause written
#   G5      0.750000  the java.io.File uses deleted, no NIO reference written
#   G6      0.875000  every File site migrated, including the two that must not be
#   G7      0.500000  five raw types added over the baseline's zero
#   G8      0.500000  six @SuppressWarnings sites added
#   G9      0.850000  all three constructs present, and it still compiles at 1.6
#   G10     0.850000  the 1.6 compile fails, but not over the language level
#   G11     0.850000  it does not compile at 1.7; graded at the default level
#   G12     0.000000  no compile of the tree produced a report at all
#   G14     0.007500  the 1.6 flip bought with one construct and nothing else
#   G15     1.000000  eight qualified NIO references and five static-imported
#   G16     0.931818  the same eight, with nothing making up the missing three
#   G17     1.000000  all 11 sites moved, reached in 11 references not 13
#
# Both ends are asserted, not just the ceiling: a gate that always returns 1
# passes "golden = 1", and a gate that always returns 0 passes "pinned = 0".
# The thirteen in between are what make those two mean something, and each is
# asserted to six decimal places rather than to a band, because a gate that has
# drifted lands somewhere -- just not here.
#
# Six are worth reading as findings rather than as numbers:
#
#   G3 scores C-1 = 0. Written as `0.75*converted + 0.25*breadth`, which is how
#      it was written first, this same report scored C-1 = 0.250000: a tree that
#      wrote 93 brand-new diamond sites in 47 files and converted none of the
#      143 it already had collected a quarter of the gate for the breadth of
#      code it had merely added. Breadth now multiplies instead of adding, so it
#      can only scale work that happened.
#   G2 and G13 are the same move with and without the compiler noticing.
#      Dropping `<String,String>` from `new HashMap<String,String>()` and
#      writing nothing in its place gives a raw type, and a raw type is what
#      G13 is: the AST tells `new HashMap()` from `new HashMap<>()`, and the
#      multiplier takes 93 added raw types to zero. G2 is the same report with
#      the sites gone from the tree entirely rather than made raw, which is a
#      deletion, and C-1 reads it as one.
#   G4 and G5 are deletion at the other two gates. Every gate here is the
#      smaller of an arrival term and a departure term, and these two reports
#      are what says so: the 11 manual close() calls are gone in G4 and no try
#      statement acquires a resource, so the departure half reads 1 and the gate
#      reads 0. A deleted close() is a leaked handle, not a migration.
#   G6 is the proviso being scored. Two of the 13 java.io.File sites change
#      what the code does when they move -- Files.newOutputStream throws a
#      different exception type on a directory than new FileOutputStream does,
#      and Files.createTempFile accepts prefixes that File.createTempFile
#      rejects -- and four of family A's 935 transcripts record those
#      behaviours. Four of 935 is worth 0.0013 of the step, which is not an
#      incentive; halving C-3 is worth 0.0656, which is.
#   G9 is not a tree that can exist, and that is the point. Ninety-three diamond
#      operators do not compile at -source 1.6, so no real tree carries this
#      report. It is here because C-4 is meant to be an INDEPENDENT channel --
#      javac's own answer about the tree, not a count read off it -- and the
#      only way to show two channels are independent is to feed them
#      contradictory inputs and check that the second one still answers.
#   G14 is the cheapest tree that flips C-4, and it is here because round 1 of
#      the static review found that tree was worth 0.554 of the step. One
#      diamond plus 1.7 in the build files makes ok17=yes, ok16=no and every
#      1.6 error a language-level error, and a binary C-4 paid the full 0.15
#      for it. C-4 is now scaled by how many constructs javac objects to at
#      1.6 -- 1 of the 20 that read full credit here, against the golden's 55 --
#      so the same tree scores 0.007500 of family C and 0.476 of the step.
#      golden is its clean control: the scaling must not cost a real migration
#      anything, and golden still reads 1.000000.
#   G15 and G16 are C-3's arrival half in two spellings. `Files.exists(p)` on a
#      static import is a reference to java.nio.file.Files that SourceScan's
#      niofile.ref never saw, because ref counts member selects and a static
#      import is a bare identifier; round 1 found it, the counter now reports
#      it as niofile.staticref, and the gate adds the two. G15 splits thirteen
#      references 8/5 across the two spellings and must read 1.000000; G16 is
#      the same eight with nothing making up the difference and must read
#      0.931818. Without the second, the pair would prove only that some route
#      to a passing arrival exists.
#   G17 is the other half of the same round-1 finding, and the one that says
#      what the denominator is. Arrival used to divide by the golden's own 13
#      references; the requirement is that 11 sites move, and a migration that
#      moved all 11 and reached them in 11 references was reading 0.846 for
#      complete work. It now divides by those 11, so G17 reads 1.000000.
#      G16 is its control: 8 of 11 is still short, and still scores short.
#   G12 scores 0 and reports gate.src=broken. A block that read four clean
#      zeros off a compile that never happened would be reporting a finding
#      about the scorer as a finding about the tree, and the fact is what says
#      which of the two it is.
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
grep -q '^# =* family C$' "${W}/cblock.sh" || { echo "c-rehearsal: no family C banner" >&2; exit 1; }
grep -q '^fact "familyC=' "${W}/cblock.sh" || { echo "c-rehearsal: no familyC line" >&2; exit 1; }
# Every one of the four gates and the multiplier has to be in there. A range
# that clipped early would still start and end correctly if the banner moved.
for g in C1.diamond C2.try-with-resources C3.nio-file C4.requires-java7 \
         gate.src.penalty; do
   grep -qF "fact \"${g}=" "${W}/cblock.sh" \
      || { echo "c-rehearsal: the extracted block has no ${g}" >&2; exit 1; }
done
# And it must be the block that reaches the tree only through srcgate, which is
# the function this rehearsal replaces. Three calls: 1.7, then either 1.6 or the
# default level. A fourth would be a path the shim does not cover, and a
# path the shim does not cover is a path this rehearsal says nothing about.
ns=$(grep -c 'srcgate ' "${W}/cblock.sh" || true)
[ "${ns}" = 3 ] || { echo "c-rehearsal: ${ns} srcgate calls in the block, expected 3" >&2; exit 1; }
# Nothing else in the block may run a compiler or read the tree directly.
if grep -nE '(^|[^a-z.])(javac|java|docker|gradlew?)[ "]' "${W}/cblock.sh" \
     | grep -vE '^[0-9]+:[[:space:]]*#' ; then
   echo "c-rehearsal: the block reaches outside srcgate" >&2
   exit 1
fi

run_one()
{
   local fixture=$1
   local w="${W}/run"
   rm -rf "${w}"
   mkdir -p "${w}"
   {
      echo 'set -uo pipefail'
      printf 'WORK=%s\n' "${w}"
      printf 'FACTS=%s/facts.txt\n' "${w}"
      printf ': > "${FACTS}"\n'
      cat "${W}/pre.sh"
      # What the run supplies and the block does not compute: the behaviour
      # summary family A wrote, which C-3's proviso reads.
      printf 'summary=%s/behavior.summary\n' "${fixture}"
      # The shim. Every file the real source-gate.sh leaves behind is left
      # behind here too, with the same names, because the block reads three of
      # them by name -- the report, its .env and its javac log -- and a shim
      # that supplied only the report would leave C-4 reading a missing file
      # and scoring 0 for the wrong reason.
      printf 'FIXDIR=%s\n' "${fixture}"
      cat <<'SHIM'
srcgate() {
   local tag=$1
   cp "${FIXDIR}/report.txt" "${WORK}/src-${tag}.txt"
   printf 'javac.level=%s\njavac.errors=%s\nsource.ok=%s\n' \
      "${tag}" \
      "$(sed -n 's/^errors=//p' "${FIXDIR}/answers.txt" | head -1)" \
      "$(sed -n "s/^ok${tag}=//p" "${FIXDIR}/answers.txt" | head -1)" \
      > "${WORK}/src-${tag}.txt.env"
   cp "${FIXDIR}/javac16.log" "${WORK}/src-${tag}.txt.javac.log"
   : > "${WORK}/src-${tag}.log"
   sed -n "s/^ok${tag}=//p" "${FIXDIR}/answers.txt" | head -1
}
SHIM
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

# The fixtures were calibrated against the frozen baselines. These are the
# numbers the SHIPPED block reads out of the same two files with its own bv()
# and tv(). They have to agree, or the fixtures are pointed at a different
# question than the gate answers.
run_one "${FIX}/c/golden" > /dev/null
for want in gate.src.base.nonanon=116 gate.src.base.io=13 gate.src.target.diamond=93; do
   grep -qxF "${want}" "${W}/run/facts.txt" \
      || { echo "c-rehearsal: the block does not read ${want} from the staged baselines" >&2
           grep -E '^gate\.src\.(base|target)' "${W}/run/facts.txt" >&2 || :
           exit 1; }
done
echo "c-rehearsal: the block and the fixtures agree on the frozen baselines"

fail=0
while read -r name want; do
   [ -n "${name}" ] || continue
   got=$(run_one "${FIX}/c/${name}")
   printf 'family C fixture %-7s want=%s got=%s  %s\n' \
      "${name}" "${want}" "${got}" "$(cat "${FIX}/c/${name}/why.txt")"
   [ "${got}" = "${want}" ] || fail=1
done <<TABLE
pinned  0.000000
golden  1.000000
G1      0.551939
G2      0.650000
G3      0.650000
G13     0.000000
G4      0.750000
G5      0.750000
G6      0.875000
G7      0.500000
G8      0.500000
G9      0.850000
G10     0.850000
G11     0.850000
G12     0.000000
G14     0.007500
G15     1.000000
G16     0.931818
G17     1.000000
TABLE

# G9, G10 and G11 share a total, so the facts that tell them apart are asserted
# directly. Without this the trio proves only that some route to 0.850000
# exists; with it, each proves the route it was built for.
run_one "${FIX}/c/G9" > /dev/null
grep -qxF 'gate.src.ok17=yes' "${W}/run/facts.txt" || { echo "c-rehearsal: G9 did not compile at 1.7" >&2; fail=1; }
grep -qxF 'gate.src.ok16=yes' "${W}/run/facts.txt" || { echo "c-rehearsal: G9's 1.6 compile was not accepted" >&2; fail=1; }
run_one "${FIX}/c/G10" > /dev/null
grep -qxF 'gate.src.16.errors=7'      "${W}/run/facts.txt" || { echo "c-rehearsal: G10 lost its 1.6 error count" >&2; fail=1; }
grep -qxF 'gate.src.16.levelerrors=3' "${W}/run/facts.txt" || { echo "c-rehearsal: G10's non-level errors were counted as level errors" >&2; fail=1; }
run_one "${FIX}/c/G11" > /dev/null
grep -qxF 'gate.src.ok17=no'   "${W}/run/facts.txt" || { echo "c-rehearsal: G11 compiled at 1.7" >&2; fail=1; }
grep -qxF 'gate.src.okdef=yes' "${W}/run/facts.txt" || { echo "c-rehearsal: G11 was not graded at the default level" >&2; fail=1; }

# C-4's breadth, at both ends. golden and G14 differ in nothing but how many
# constructs javac rejects at 1.6, so the scaling is the only thing that can
# separate 1.000000 from 0.050000, and the fact is asserted on both.
run_one "${FIX}/c/golden" > /dev/null
grep -qxF 'gate.src.16.breadth=1.000000' "${W}/run/facts.txt" \
   || { echo "c-rehearsal: the golden tree did not read full C-4 breadth" >&2; fail=1; }
run_one "${FIX}/c/G14" > /dev/null
grep -qxF 'gate.src.16.levelerrors=1'    "${W}/run/facts.txt" || { echo "c-rehearsal: G14 lost its single level error" >&2; fail=1; }
grep -qxF 'gate.src.16.breadth=0.050000' "${W}/run/facts.txt" || { echo "c-rehearsal: G14's one-construct flip was not scaled" >&2; fail=1; }

# C-3's arrival half counts both spellings of a java.nio.file reference. G15
# and G16 carry the same eight member selects; only G15 has the five static
# imports, and only the sum can tell them apart.
run_one "${FIX}/c/G15" > /dev/null
grep -qxF 'gate.src.nioref=8'      "${W}/run/facts.txt" || { echo "c-rehearsal: G15 lost its member-select references" >&2; fail=1; }
grep -qxF 'gate.src.niostatic=5'   "${W}/run/facts.txt" || { echo "c-rehearsal: G15's static imports were not read" >&2; fail=1; }
grep -qxF 'gate.src.nioarrive=13'  "${W}/run/facts.txt" || { echo "c-rehearsal: G15's two spellings were not added" >&2; fail=1; }
run_one "${FIX}/c/G16" > /dev/null
grep -qxF 'gate.src.niostatic=0'   "${W}/run/facts.txt" || { echo "c-rehearsal: G16 has static imports it was not given" >&2; fail=1; }
grep -qxF 'gate.src.nioarrive=8'   "${W}/run/facts.txt" || { echo "c-rehearsal: G16's arrival half did not fall to eight" >&2; fail=1; }

# The denominator arrival divides by is the 11 sites that move, not the 13
# references the golden happened to write. Both numbers are recorded as facts so
# that a build which quietly restored the old one is visible in the log rather
# than only in a total.
run_one "${FIX}/c/G17" > /dev/null
grep -qxF 'gate.src.nioarrive=11'      "${W}/run/facts.txt" || { echo "c-rehearsal: G17 lost its eleven references" >&2; fail=1; }
grep -qxF 'gate.src.nioneed=11'        "${W}/run/facts.txt" || { echo "c-rehearsal: arrival is not dividing by the 11 moved sites" >&2; fail=1; }
grep -qxF 'gate.src.golden.nioref=13'  "${W}/run/facts.txt" || { echo "c-rehearsal: the golden's own reference count was lost" >&2; fail=1; }

# G12 must say the gate is broken, not that the tree is empty.
run_one "${FIX}/c/G12" > /dev/null
grep -qxF 'gate.src=broken' "${W}/run/facts.txt" \
   || { echo "c-rehearsal: G12 reported four clean zeros instead of a broken gate" >&2; fail=1; }

# G2 and G13 both score C-1 = 0 and only one of them is penalised, so the
# multiplier is asserted at both ends of that pair.
run_one "${FIX}/c/G2" > /dev/null
grep -qxF 'gate.src.raw.added=0' "${W}/run/facts.txt" || { echo "c-rehearsal: G2 was penalised for raw types it does not have" >&2; fail=1; }
run_one "${FIX}/c/G13" > /dev/null
grep -qxF 'gate.src.raw.added=93' "${W}/run/facts.txt" || { echo "c-rehearsal: G13's raw types were not counted" >&2; fail=1; }
grep -qxF 'gate.src.penalty=0.000000' "${W}/run/facts.txt" || { echo "c-rehearsal: G13 was not taken to zero" >&2; fail=1; }

# G6 must be halved by the proviso and not by anything else: C-3's own two
# terms both read 1 on it, which is what makes the halving visible.
run_one "${FIX}/c/G6" > /dev/null
grep -qxF 'gate.src.nio.behaviour.broken=4' "${W}/run/facts.txt" || { echo "c-rehearsal: G6's broken transcripts were not seen" >&2; fail=1; }
grep -qxF 'C3.nio-file=0.500000'            "${W}/run/facts.txt" || { echo "c-rehearsal: G6's C-3 was not halved" >&2; fail=1; }
run_one "${FIX}/c/golden" > /dev/null
grep -qxF 'gate.src.nio.behaviour.broken=0' "${W}/run/facts.txt" || { echo "c-rehearsal: the golden tree was charged with broken transcripts" >&2; fail=1; }

# The gate-by-gate reading for all nineteen, so the build log carries what each
# gate said and not only the weighted total. A total can be right for the wrong
# reasons -- two gates off by compensating amounts -- and this is where that
# would be visible.
for t in pinned golden G1 G2 G3 G13 G4 G5 G6 G7 G8 G9 G10 G11 G12 G14 G15 G16 G17; do
   run_one "${FIX}/c/${t}" > /dev/null
   echo "--- ${t}"
   grep -E '^(gate\.src|C[1-4]\.)' "${W}/run/facts.txt" | sed 's/^/    /'
done

[ "${fail}" = 0 ] || { echo "c-rehearsal: a fixture did not score what it must" >&2; exit 1; }
echo "c-rehearsal: family C scores all nineteen reports as calibrated"
