#!/usr/bin/env bash
# Rehearse the ten scoring blocks that read the typed AST, the published jars
# and the class-file scan -- the hoisted source-AST block, B-10, B-11, B-12,
# B-13, B-14, B-15, B-16, B-17 and the whole of family C -- against trees whose
# answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the
# ten blocks are lifted out of the SHIPPED test.sh by their own banners,
# composed in file order behind a srcgate() that hands them a fixture's report
# instead of compiling anything, and run once per fixture. What is asserted is
# the arithmetic over the report, over the jars and over the scan, which is the
# part of these gates that no other check exercises.
#
# TEN blocks and not nine. Step 10's rehearsal composed the source AST, B-10,
# B-11, B-12, B-13, B-14, B-15, B-16 and family C; this step adds B-17, which
# is step 10's whole family C inherited under a new name and renormalised over
# the 0.96 that is left when its C-1 is dropped. The extraction asserts all
# ten are disjoint and in that order in the file -- the counters are read in
# one place and spent in nine others, and a hoist that silently un-hoisted
# would leave B-10 reading variables that do not exist yet, which under `set
# -u` is a dead verifier rather than a wrong number.
#
# STEP 10's C-1 IS THE ONE LEG THAT DID NOT COME ACROSS, and this is the fourth
# rung in a row where that is true. It was the class-file major, and the major
# is THIS rung's own must-flip -- 66 where step 10 wanted 65 -- so carrying it
# forward would grade one artefact twice against two contradictory thresholds.
# B-17 is step 10's three SOURCE legs over the 0.96 they shared, and K17 is the
# row where the consequence is visible: family C reads 0.040000 there, off an
# intact class-file scan, while B-16 and B-17 both read 0.000000, because
# neither of them has a class-file leg left to be full.
#
# FIVE FAMILIES ARE STACKED IN THE HELD HALF NOW -- step 06's as B-13, step
# 07's as B-14, step 08's as B-15, step 09's as B-16 and step 10's as B-17 --
# and the count is the ninth trap's whole subject. Every one of them is scored
# on every row of the table below, including the rows written four rungs ago,
# because a leg that would quietly become unpassable at step 11 is exactly the
# leg nothing at step 11 would otherwise look at.
#
# B-15 REALLY RUNS java, and it is the only block that does. Two of step 08's
# three legs asked java.lang.module what the published artifacts call
# themselves, through arqgate.Modules; one of those two survives as B-15.2 and
# there is no way to rehearse it except by running it. So the fixtures carry
# real jars with real manifests -- wrapped at 72 bytes the way the jar tool
# wraps them, with a real META-INF/services/ entry in the one artifact whose
# descriptor the platform refuses -- and the run script below hands the block
# the same ${WORK}/modulejars.txt the build stage writes.
#
# THE TWO ARTIFACT CHANNELS ARE TWO AGAIN, which reverses step 08. At step 07
# C-1 read ${WORK}/scan.txt and B-12e read the jars, so a fixture could carry
# one and not the other; at step 08 both read ${WORK}/modulejars.txt and `no
# jars` was ONE absence with TWO consequences. C-1 has been back on the
# class-file scan since step 09 -- major 66 over the class files there are --
# so the jars feed B-12e and B-15 and nothing else, and there are four fixtures
# on the split: L1 and L2 move the scan and leave the jars alone, K19 and K20
# move the jars and leave the scan alone.
#
# SO THIS REHEARSAL STAGES A scan.txt AGAIN. run_one copies the fixture's own
# into ${WORK} before the blocks run, the way the verifier copies the one
# arqgate.Scan writes over the built class files, and a fixture with no
# scan.txt is a tree whose build produced no class files at all. That is L2,
# and it is the row that says C-1's absence spelling is the shell literal `0`
# and not `0.000000`.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step11/
# mkfix11.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all. The separation is sharpest on B-15.2, which
# mkfix11.py predicts from the jar PLAN -- twelve Mod records, each with a name
# or without one -- while the rehearsal measures the platform's reading of the
# bytes that plan produced. The two agreeing is a fact about java.lang.module
# and not about either program.
#
# All thirteen figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
#
# THREE SPELLINGS OF ZERO, and K17 carries all three at once:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                C-2 and C-3, and B-16.2, B-16.4, B-16.5, B-17.2 and B-17.4,
#                and gate.b15.departed, gate.b15.arrived and gate.b15.held, all
#                of which are initialised above their guard and published below
#                it. So does C-1 when there is no scan file at all, which is a
#                different tree from a tree that compiled -- L2 and K17 are the
#                two rows, and they are not the same row.
#   `0.000000`   B-12, B-13, B-14, B-15, B-16, B-17 and C-4 are totals computed
#                by an awk that sits OUTSIDE the guard, so they are computed
#                from parts that never ran. Two sub-legs are in that group and
#                their siblings are not: B-16.3 and B-17.3 are each an awk
#                below their own `fi`, while gate.b16.3.movefrac,
#                gate.b16.3.holdfrac and gate.b17.3's three fractions are
#                initialised above it and print the literal `0`.
#
#                WHICH OF C-2, C-3 AND C-4 IS IN WHICH LIST MOVED THIS RUNG AND
#                THE TABLE IS WHERE THAT IS PINNED. At step 10 C-2 and C-4 were
#                the literals and C-3 was the awk; here the legs are different
#                legs, C-2 and C-3 close over a minf() inside their guard and
#                C-4's four-way awk sits outside its own. Transcribing K17's
#                row from the rung below would have put `0` where this file
#                prints `0.000000` twice over -- which is why the row is
#                transcribed from mkfix11.py and not carried.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so a tree nobody could compile has no
#                penalty fact rather than an unfired `1`. So are every
#                gate.b16.* and gate.b17.* measurement and every gate.c2.*,
#                gate.c3 counter and gate.c4.* one.
#
# factck spells the third one out rather than collapsing it into the empty
# string, so `want MISSING` is an assertion and not an accident.
set -uo pipefail

TESTS=${TESTS:-/tests}
FIXDIR=${FIXDIR:-${TESTS}/fixtures/c}
SRC=${SRC:-${TESTS}/test.sh}
GATEJAR=${GATEJAR:-${TESTS}/gates/build/arqmodules.jar}
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

# ...and B-15 shells out to java, inherited from the rung where that was new.
# Two separate things can be missing -- the runtime and the gate jar the
# build.sh above this produced -- and they fail identically at the fact level
# (gate.b15.mod=broken, B-15.2 zero, B15 0.562500) so they are separated here.
command -v java >/dev/null 2>&1 \
   || { echo "c-rehearsal: no java; B-15 cannot be rehearsed" >&2; exit 1; }
[ -r "${GATEJAR}" ] \
   || { echo "c-rehearsal: no ${GATEJAR}; run gates/build.sh first" >&2; exit 1; }

# --- the instrument, before the finding --------------------------------------
# Every B-15.2 figure below is a reading taken by arqgate.Modules, and a
# reading is worth what the instrument is worth. So: run it once, here, over
# the golden fixture's twelve jars, and require the three counts B-15.2
# divides by. If this says 12/12/11 the table's B15 column is measuring the
# platform; if it says anything else, every row falls together and the forty
# failures that follow would all be one.
#
# THE POISONED JAR IS STILL PART OF THE INSTRUMENT CHECK AND NO LONGER PART OF
# THE SCORE. arquillian-impl-base ships a META-INF/services/org.jboss.
# arquillian.spi.client.container.ArquillianDescriptor naming a provider class
# that is not in the module, and ModuleFinder refuses to derive a descriptor
# for it -- which is why derivable is 11 and not 12. Step 08 spent that
# refusal inside its C-1 min(); B-15 reports it and does not score it, because
# the question it was asking -- can the platform read a descriptor out of this
# jar at all -- is one the input tree already answers. So the preflight still
# requires 11, as the proof that the instrument is the instrument, and M1
# below is the fixture that asserts the refusal now costs nothing.
echo "== the module gate, over the golden's own jars"
ls "${FIXDIR}"/golden/jars/*.jar > "${TMP}/preflight-jars.txt" 2>/dev/null || :
pfn=$(grep -c . "${TMP}/preflight-jars.txt" || true)
if [ "${pfn}" != 12 ]; then
   bad "the golden fixture publishes ${pfn} jar(s); this rehearsal was written against 12"
else
   if java -cp "${GATEJAR}" arqgate.Modules \
        "${TMP}/preflight.txt" "${TMP}/preflight-jars.txt" > "${TMP}/preflight.log" 2>&1; then
      pfv() { sed -n "s/^$1=//p" "${TMP}/preflight.txt" | head -1; }
      note "arqgate.Modules: total=$(pfv 'mod\.total') declared=$(pfv 'mod\.declared') derivable=$(pfv 'mod\.derivable') resolved=$(pfv 'mod\.resolved')"
      [ "$(pfv 'mod\.total')" = 12 ] \
         || bad "the module gate reads $(pfv 'mod\.total') jars out of the golden's twelve"
      [ "$(pfv 'mod\.declared')" = 12 ] \
         || bad "the module gate reads $(pfv 'mod\.declared') declared module names out of the golden's twelve"
      [ "$(pfv 'mod\.derivable')" = 11 ] \
         || bad "the module gate reads $(pfv 'mod\.derivable') readable modules; the golden's poisoned artifact should leave 11"
      sed -n 's/^mod\[[0-9]*\]=//p' "${TMP}/preflight.txt" | grep -F 'reason=' \
         | sed 's/^/    refused, as designed: /' | head -2
   else
      bad "arqgate.Modules will not run over the golden's jars:"
      sed -n '1,8p' "${TMP}/preflight.log" | sed 's/^/         /'
   fi
fi

echo
echo "== extracting the ten blocks and the hoisted bucket helpers from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min() gate at this step -- four of B-14's
# legs, B-15's service leg, and C-2, C-3 and C-4 -- would read one half alone.
grep -E '^(fact|frac|clamp01|minf)\(\) \{' "${SRC}" > "${TMP}/helpers.sh"
n=$(grep -c . "${TMP}/helpers.sh" || true)
[ "${n}" = 4 ] || bad "expected 4 helper one-liners in ${SRC}, found ${n}"
note "helpers: ${n} (fact frac clamp01 minf)"

# --- the bucket helpers, which are in NO block -------------------------------
# NINE FUNCTIONS IN THE GAP, and this section exists because the first run of
# step 10's rehearsal did not lift them. At step 09 cbuckets(), fcount(),
# capsum() and capwant() sat INSIDE the family-C block, so lifting family C
# lifted them with it and no separate step was needed. Step 10 hoisted them
# above B-16 and added cnames(); this rung adds four more -- pbuckets(),
# ptot(), psum() and pdep() -- and the gap between B-15's last fact and B-16's
# banner now holds all nine. No block covers that gap.
#
# THREE BLOCKS READ THEM AND NOT TWO, which is what the count is about.
# B-16 IS step 09's family C and reads four (cbuckets fcount capsum capwant);
# B-17 IS step 10's and reads two (cbuckets cnames); this rung's own family C
# reads six (cbuckets fcount pbuckets ptot psum pdep). Only cbuckets() is read
# by all three. One definition beats three that drift.
#
# Composed without them, every call is `command not found`; under `set -uo
# pipefail` and no `-e` the run continues, ${WORK}/cnt-*.txt, nam-*.txt and
# set-*.txt are never written, and the awk that reads them yields 0. That is
# the shape step 10's first run printed: B16 0.400000 and familyC 0.100000 on
# every one of its fifty-five rows, which is one missing `cat` and not
# fifty-five broken gates. (Both figures are as they were printed then -- the
# six legs that read those files at that rung were B-16.2, B-16.3, B-16.5,
# C-2, C-3 and C-4, and 0.100000 was C-1's weight before the
# automated-migrator calibration repriced it to 0.04. This paragraph is a
# record of a run and not a prediction.) The sixth trap says verify the
# instrument before the finding, and the instrument was what was wrong.
#
# So the gap is lifted by name as an ELEVENTH piece and asserted to be exactly
# those nine definitions -- a tenth would be a helper this section is silently
# taking ownership of, and a missing one is a call the composed script cannot
# make.
#
# ptot() IS A ONE-LINER AND THE COUNT HAS TO KNOW IT. Eight of the nine are
# `name() {` on a line of their own; ptot() is `ptot() { awk ...; }` entire, so
# a count anchored on `^[a-z][a-z0-9_]*() {$` reads eight and calls a correct
# file wrong. The regex below accepts either shape, and the name loop under it
# is what actually says which nine.
awk '/^# -* bucket helpers, shared by B-16, B-17 and C$/ { on = 1 }
     /^# =* B-16$/                                       { on = 0 }
     on' "${SRC}" > "${TMP}/bhelp.sh"
head -1 "${TMP}/bhelp.sh" | grep -q 'bucket helpers, shared by B-16, B-17 and C$' \
   || bad "the hoisted bucket helpers do not start at their banner"
grep . "${TMP}/bhelp.sh" | tail -1 | grep -q '^}$' \
   || bad "the hoisted bucket helpers do not end at a function's closing brace"
nbh=$(grep -cE '^[a-z][a-z0-9_]*\(\) \{' "${TMP}/bhelp.sh" || true)
[ "${nbh}" = 9 ] || bad "expected 9 hoisted bucket helpers in ${SRC}, found ${nbh}"
for f in cbuckets fcount capsum capwant cnames pbuckets ptot psum pdep; do
   grep -qE "^${f}\(\) \{" "${TMP}/bhelp.sh" \
      || bad "the hoisted bucket helpers do not define ${f}()"
done
note "bucket helpers: ${nbh} (cbuckets fcount capsum capwant cnames pbuckets ptot psum pdep), lifted from the gap between B-15 and B-16"

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

# --- blocks 2..4: B-10, B-11 and B-12 ----------------------------------------
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

# B-12 swallows B-12e, whose own `# ---- B-12e` banner sits inside this range:
# the constant-pool leg is a fifth term of step 05's gate and not a gate of its
# own, and lifting it separately would compose it without the four source legs
# it multiplies.
sed -n '/^# -* B-12: step 05, held$/,/^fact "B12.java8-library=/p' "${SRC}" \
   > "${TMP}/block-b12.sh"
head -1 "${TMP}/block-b12.sh" | grep -q 'B-12: step 05, held$' \
   || bad "the B-12 block does not start at its banner"
tail -1 "${TMP}/block-b12.sh" | grep -q '^fact "B12.java8-library=' \
   || bad "the B-12 block does not end at its own fact"
grep -q '^# -* B-12e$' "${TMP}/block-b12.sh" \
   || bad "the B-12 block does not contain B-12e's banner; the constant-pool leg was not lifted with it"

# --- blocks 5 and 6: B-13 and B-14 -------------------------------------------
# Their banners are `=` rules and not `-` ones, because each is a whole family
# inherited rather than a gate: at step 06 B-13 was family C in full, and at
# step 07 B-14 was.
sed -n '/^# =* B-13$/,/^fact "B13.java9-10=/p' "${SRC}" > "${TMP}/block-b13.sh"
head -1 "${TMP}/block-b13.sh" | grep -q 'B-13$' \
   || bad "the B-13 block does not start at its banner"
tail -1 "${TMP}/block-b13.sh" | grep -q '^fact "B13.java9-10=' \
   || bad "the B-13 block does not end at its own fact"

sed -n '/^# =* B-14$/,/^fact "B14.java11-jakarta=/p' "${SRC}" > "${TMP}/block-b14.sh"
head -1 "${TMP}/block-b14.sh" | grep -q 'B-14$' \
   || bad "the B-14 block does not start at its banner"
tail -1 "${TMP}/block-b14.sh" | grep -q '^fact "B14.java11-jakarta=' \
   || bad "the B-14 block does not end at its own fact"

# --- block 7: B-15 -----------------------------------------------------------
# Its banner is a `=` rule and a bare name, which is B-13's and B-14's spelling
# and not B-10 through B-12's "step NN, held" one. The rule carries a meaning
# and this block has earned it: B-10, B-11 and B-12 are single gates held from
# single rungs, while B-13 WAS step 06's family C in full, B-14 WAS step 07's,
# and B-15 IS step 08's -- three whole families demoted, renormalised over the
# legs that survive their rung's own C-1. A reader who sees `===` knows the
# block below it is a family and that a drift in it is a regression two rungs
# back. Anchored on exactly one spelling rather than on either, because the
# Dockerfile greps the same string: a rehearsal that accepted both would stop
# being able to say which one shipped.
sed -n '/^# =* B-15$/,/^fact "B15.jpms-services=/p' "${SRC}" \
   > "${TMP}/block-b15.sh"
head -1 "${TMP}/block-b15.sh" | grep -q 'B-15$' \
   || bad "the B-15 block does not start at its banner"
tail -1 "${TMP}/block-b15.sh" | grep -q '^fact "B15.jpms-services=' \
   || bad "the B-15 block does not end at its own fact"

# --- block 8: B-16 -----------------------------------------------------------
# The same `=` rule and bare name as B-13, B-14 and B-15, and for the same
# reason: it is a whole family demoted, not a gate held. B-16 IS step 09's
# family C, minus that rung's class-file leg, renormalised over the 0.90 the
# other four shared.
sed -n '/^# =* B-16$/,/^fact "B16.java17-constructs=/p' "${SRC}" \
   > "${TMP}/block-b16.sh"
head -1 "${TMP}/block-b16.sh" | grep -q 'B-16$' \
   || bad "the B-16 block does not start at its banner"
tail -1 "${TMP}/block-b16.sh" | grep -q '^fact "B16.java17-constructs=' \
   || bad "the B-16 block does not end at its own fact"

# --- block 9: B-17 -----------------------------------------------------------
# The newest of the demoted families and the reason this rehearsal composes ten
# blocks. B-17 IS step 10's family C, minus that rung's class-file leg,
# renormalised over the 0.96 the other three shared -- 0.17 + 0.65 + 0.14.
# FIVE whole families are now stacked in the held half, and the ninth trap is
# about exactly this shape, so the block is lifted and scored on every fixture
# in the table including the fifteen that were written four rungs ago.
#
# ITS BANNER IS ONE CHARACTER FROM B-16's AND THE END ANCHOR IS NOT. A range
# anchored on `/^# =* B-1` would take both; both ends are spelled in full here
# and each is asserted after the lift, so a rehearsal that swallowed B-17 into
# B-16 fails at the anchor rather than by scoring one family twice.
sed -n '/^# =* B-17$/,/^fact "B17.java21-constructs=/p' "${SRC}" \
   > "${TMP}/block-b17.sh"
head -1 "${TMP}/block-b17.sh" | grep -q 'B-17$' \
   || bad "the B-17 block does not start at its banner"
tail -1 "${TMP}/block-b17.sh" | grep -q '^fact "B17.java21-constructs=' \
   || bad "the B-17 block does not end at its own fact"
grep -q '^fact "B16.java17-constructs=' "${TMP}/block-b17.sh" \
   && bad "the B-17 block contains B-16's fact; the two banners were not told apart"

# --- block 10: family C ------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

BLOCKS="block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-b16 block-b17 block-c"
for f in ${BLOCKS}; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The ten are disjoint and in file order. This is the hoist, asserted: B-10
# through B-17 spend counters the first block reads, so the first block has to
# come first, and family C spends them again after all eight.
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
s6=$(grep -n '^# =* B-14$' "${SRC}" | head -1 | cut -d: -f1)
e6=$(grep -n '^fact "B14.java11-jakarta=' "${SRC}" | head -1 | cut -d: -f1)
s7=$(grep -n '^# =* B-15$' "${SRC}" | head -1 | cut -d: -f1)
e7=$(grep -n '^fact "B15.jpms-services=' "${SRC}" | head -1 | cut -d: -f1)
s8=$(grep -n '^# =* B-16$' "${SRC}" | head -1 | cut -d: -f1)
e8=$(grep -n '^fact "B16.java17-constructs=' "${SRC}" | head -1 | cut -d: -f1)
s9=$(grep -n '^# =* B-17$' "${SRC}" | head -1 | cut -d: -f1)
e9=$(grep -n '^fact "B17.java21-constructs=' "${SRC}" | head -1 | cut -d: -f1)
s10=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
# The gap is part of the order now. Its banner must sit strictly between
# B-15's last fact and B-16's, which is the statement that the eleventh piece
# is disjoint from all ten and is where the extraction above assumed it was. A
# rung that moved the helpers back inside family C would lift them twice and
# fail here rather than by defining cbuckets() from two files.
sbh=$(grep -n '^# -* bucket helpers, shared by B-16, B-17 and C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5 e5 s6 e6 s7 e7 sbh s8 e8 s9 e9 s10; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ] && [ "${s5}" -lt "${e5}" ] \
   && [ "${e5}" -lt "${s6}" ] && [ "${s6}" -lt "${e6}" ] && [ "${e6}" -lt "${s7}" ] \
   && [ "${s7}" -lt "${e7}" ] && [ "${e7}" -lt "${sbh}" ] && [ "${sbh}" -lt "${s8}" ] \
   && [ "${s8}" -lt "${e8}" ] && [ "${e8}" -lt "${s9}" ] && [ "${s9}" -lt "${e9}" ] \
   && [ "${e9}" -lt "${s10}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < B-13 ${s5}..${e5} < B-14 ${s6}..${e6} < B-15 ${s7}..${e7} < helpers ${sbh} < B-16 ${s8}..${e8} < B-17 ${s9}..${e9} < C ${s10}"
else
   bad "the ten blocks and the helper gap are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5} ${e5} ${s6} ${e6} ${s7} ${e7} ${sbh} ${s8} ${e8} ${s9} ${e9} ${s10}"
fi

# ...and exactly one copy of each. The hoist's whole argument is that B-16,
# B-17 and family C read the SAME cbuckets(), so a block that carried its own
# would be two gates agreeing about a tree for two different reasons -- and the
# composed script, which cats the gap first, would silently prefer the later
# one. All nine names are checked against all ten blocks and not just the three
# that call them: a copy of psum() inside B-14 would never be reached by B-14
# and would still be the one family C got.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-c; do
   for f in cbuckets fcount capsum capwant cnames pbuckets ptot psum pdep; do
      grep -qE "^${f}\(\) \{" "${TMP}/${blk}.sh" \
         && bad "${blk} defines ${f}() as well; the hoisted copy is meant to be the only one"
   done
done

# --- what came back, and where it is allowed to be ---------------------------
# Step 08's rehearsal asserted that NO block read WANT_MAJOR or ${WORK}/scan.txt,
# because at that rung the class-file level did not move and the scan report was
# not staged. Both are back, and the assertion is inverted rather than deleted:
# exactly ONE block may read each, and it is family C, because C-1 is the leg
# that spends them. A WANT_MAJOR that reappeared in B-10 would be an inherited
# gate silently re-reading this rung's must-flip -- the ninth trap, in the
# direction that raises a score rather than lowering one.
#
# ANCHORED ON THE SLASH for scan.txt. Family C writes ${WORK}/set-*.txt,
# ${WORK}/cnt-*.txt and ${WORK}/sealed-*.txt, and an unanchored `scan\.txt`
# matches the tail of a bucket name -- which reported this rehearsal's own
# bucket file as a leftover the first time the step-08 version of this loop ran. The real reference was
# always ${WORK}/scan.txt, so the separator is part of the pattern.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'WANT_MAJOR' || true)
   [ "${n}" = 0 ] || bad "${blk} reads WANT_MAJOR ${n} time(s); only family C's C-1 may"
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c '/scan\.txt' || true)
   [ "${n}" = 0 ] || bad "${blk} reads scan.txt ${n} time(s); only family C's C-1 may"
done
nwm=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'WANT_MAJOR' || true)
nsc=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c '/scan\.txt' || true)
if [ "${nwm}" -gt 0 ] && [ "${nsc}" -gt 0 ]; then
   note "family C alone reads WANT_MAJOR (${nwm}) and \${WORK}/scan.txt (${nsc}); no other block does"
else
   bad "family C reads WANT_MAJOR ${nwm} time(s) and scan.txt ${nsc} time(s); C-1 needs both"
fi

# ...and the jars are read by exactly the two blocks that have a leg on them.
# This is the other half of the channel split: at step 08 family C and B-12e
# shared modulejars.txt and a fixture could not separate them; here family C
# must not touch it at all, and K19 and K20 are the rows that say the jar
# channel moves without the scan channel moving with it.
for blk in block-b16 block-b17 block-c; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'modulejars\.txt' || true)
   [ "${n}" = 0 ] \
      || bad "${blk} reads modulejars.txt ${n} time(s); the jars are B-12e's and B-15's channel"
done
note "neither B-16, B-17 nor family C reads modulejars.txt: the jar channel and the scan channel are separate again"

# Every gate this rehearsal claims to exercise has to be INSIDE one of the ten
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok22='
inblock block-src 'gate.src.ok21='
inblock block-src 'gate.src.okdef='
inblock block-src 'gate.src=broken'
inblock block-src 'covered() {'
inblock block-src 'coveredboth() {'
inblock block-src 'coveredmethods() {'
inblock block-src 'buckets() {'
inblock block-src 'wantbuckets() {'
inblock block-src 'fbuckets() {'
inblock block-src 'setwant() {'
inblock block-src 'nsbuckets() {'
inblock block-src 'nswant() {'
inblock block-src 'gone() {'
inblock block-src 'hit()'
inblock block-src 'bv() {'
inblock block-src 'tv() {'
inblock block-src 'sv() {'
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
inblock block-b14 'B14.2.jakarta='
inblock block-b14 'B14.3.javax-held='
inblock block-b14 'B14.4.toarray='
inblock block-b14 'B14.5.httpclient='
inblock block-b14 'gate.b14.c2.jakarta.refs='
inblock block-b14 'gate.b14.c3.unscaled='
inblock block-b14 'gate.b14.c4.want='
inblock block-b14 'gate.b14.c5.cdistub='
inblock block-b14 'gate.b14.c5.exec.want='
inblock block-b14 'gate.b14.c5.redirects.honoured='
inblock block-b14 'gate.src.penalty='
inblock block-b14 'baseline/httpids.txt'
# B-15's gate facts all carry a `b15.` prefix, INCLUDING the two absence
# spellings, which is the one edit this rung made to the block it inherited.
# Step 08 published `gate.mod=no-module-jars` unprefixed because `mod` could
# only mean one thing in a family whose every other fact was `gate.c*`; here
# family C owns gate.c1 through gate.c5, and a bare `gate.mod` beside
# `gate.b15.mod.total` is the kind of half-renamed key a later sweep misses.
inblock block-b15 'B15.2.module-names='
inblock block-b15 'B15.3.platform-lookup='
inblock block-b15 'gate.b15.mod.total='
inblock block-b15 'gate.b15.mod.declared='
inblock block-b15 'gate.b15.mod.derivable='
inblock block-b15 'gate.b15.mod.resolved='
inblock block-b15 'gate.b15.mod.resolve='
inblock block-b15 'gate.b15.mod=no-module-jars'
inblock block-b15 'gate.b15.names='
inblock block-b15 'gate.b15.departed='
inblock block-b15 'gate.b15.arrived='
inblock block-b15 'gate.b15.held='
inblock block-b15 'gate.b15.exec.want='
inblock block-b15 'modulejars.txt'
inblock block-b15 'modules.txt'
inblock block-b15 'baseline/modnames.txt'
inblock block-b15 'baseline/svc.txt'
inblock block-b15 'baseline/svcids.txt'
# B-16's four legs, each named by the fact it publishes and by the baseline
# file it divides by -- and every one of those four baselines is a STEP 09
# file that this rung neither re-derived nor re-recorded. That is what makes
# B-16 an inherited family rather than a rewrite: ipattern.txt, strnl.txt,
# tbids.txt, sealed.txt and swarrow.txt reach this image byte for byte as step
# 09 froze them, and the only thing that moved is the weight they are summed
# under.
inblock block-b16 'B16.2.pattern-instanceof='
inblock block-b16 'B16.3.text-blocks='
inblock block-b16 'B16.4.sealed-hierarchies='
inblock block-b16 'B16.5.arrow-switch='
inblock block-b16 'gate.b16.2.converted.want='
inblock block-b16 'gate.b16.2.held.want='
inblock block-b16 'gate.b16.2.sites.want='
inblock block-b16 'gate.b16.2.convfrac='
inblock block-b16 'gate.b16.2.holdfrac='
inblock block-b16 'gate.b16.2.totfrac='
inblock block-b16 'gate.b16.3.moved.want='
inblock block-b16 'gate.b16.3.held.want='
inblock block-b16 'gate.b16.3.textblocks='
inblock block-b16 'gate.b16.3.movefrac='
inblock block-b16 'gate.b16.3.holdfrac='
inblock block-b16 'gate.b16.3.exec.want='
inblock block-b16 'gate.b16.4.roots.want='
inblock block-b16 'gate.b16.4.open.want='
inblock block-b16 'gate.b16.4.closedfrac='
inblock block-b16 'gate.b16.4.openfrac='
inblock block-b16 'gate.b16.5.files.want='
inblock block-b16 'gate.b16.5.arrow='
inblock block-b16 'gate.b16.5.colon='
inblock block-b16 'baseline/ipattern.txt'
inblock block-b16 'baseline/strnl.txt'
inblock block-b16 'baseline/tbids.txt'
inblock block-b16 'baseline/sealed.txt'
inblock block-b16 'baseline/swarrow.txt'
# B-17's three legs, each named by the fact it publishes and by the baseline
# file it divides by -- and every one of those four baselines is a STEP 10
# file that this rung neither re-derived nor re-recorded. THREE LEGS AND NOT
# FOUR, because what crossed the ladder is step 10's family C MINUS its C-1:
# the class-file major is one leg at every rung and it never travels, so
# B-17.2, B-17.3 and B-17.4 are here and step 10's C-1 is not. The weights
# are step 10's own, renormalised over the 0.96 the three of them shared
# there -- which is why B-17 can read 1.000000 on a tree that never once
# reached major 66.
inblock block-b17 'B17.2.type-patterns='
inblock block-b17 'B17.3.pattern-switch='
inblock block-b17 'B17.4.sequenced-access='
inblock block-b17 'gate.b17.2.departed.want='
inblock block-b17 'gate.b17.2.arrived.want='
inblock block-b17 'gate.b17.2.held.want='
inblock block-b17 'gate.b17.2.depfrac='
inblock block-b17 'gate.b17.2.arrfrac='
inblock block-b17 'gate.b17.2.holdfrac='
inblock block-b17 'gate.b17.2.reftests='
inblock block-b17 'gate.b17.2.reftests.was='
inblock block-b17 'gate.b17.2.patterns='
inblock block-b17 'gate.b17.2.swpatterns='
inblock block-b17 'gate.b17.3.departed.want='
inblock block-b17 'gate.b17.3.arrived.want='
inblock block-b17 'gate.b17.3.held.want='
inblock block-b17 'gate.b17.3.depfrac='
inblock block-b17 'gate.b17.3.arrfrac='
inblock block-b17 'gate.b17.3.holdfrac='
inblock block-b17 'gate.b17.3.exec.want='
inblock block-b17 'gate.b17.3.exec.broken='
inblock block-b17 'gate.b17.3.ipleft='
inblock block-b17 'gate.b17.3.nullcase='
inblock block-b17 'gate.b17.3.patterns='
inblock block-b17 'gate.b17.3.switches='
inblock block-b17 'gate.b17.4.departed.want='
inblock block-b17 'gate.b17.4.arrived.want='
inblock block-b17 'gate.b17.4.held.want='
inblock block-b17 'gate.b17.4.depfrac='
inblock block-b17 'gate.b17.4.arrfrac='
inblock block-b17 'gate.b17.4.holdfrac='
inblock block-b17 'gate.b17.4.firstreads='
inblock block-b17 'gate.b17.4.firstreads.was='
inblock block-b17 'gate.b17.4.sequenced='
inblock block-b17 'gate.b17.4.sequenced.read='
inblock block-b17 'gate.b17.4.sequenced.was='
inblock block-b17 'baseline/reftest.txt'
inblock block-b17 'baseline/chain.txt'
inblock block-b17 'baseline/swids.txt'
inblock block-b17 'baseline/seqfirst.txt'
# Family C's four legs, on five baseline files, four of which did not exist a
# rung ago. FOUR LEGS, as at every rung: the class-file major is one of them
# and the other three are whatever this rung's language change is -- here the
# removal of the Security Manager's API surface, the unnamed catch parameter
# it makes reachable, and the VarHandle that replaces the reflection it used
# to wrap. A leg that read another leg's population would score, and would
# score wrongly, and the totals alone could not say which.
#
# C-2 IS THE ONE LEG IN THIS TASK WITH NO ARRIVAL TERM, and the absence is
# asserted below rather than left to the eye. doPrivileged() is removed and
# nothing arrives in its place: the body comes up a level and the wrapper is
# gone, so the leg is a departure half over the frozen call sites and a held
# half over two sub-populations that must survive the unwrapping. A
# `gate.c2.arrived` appearing here would mean somebody had given a removal an
# arrival term it cannot have, and every fixture that DELETED the wrapping
# method would start scoring as though it had converted it.
inblock block-c 'C1.level-'
inblock block-c 'C2.doprivileged-removal='
inblock block-c 'C3.unnamed-catch='
inblock block-c 'C4.varhandle='
inblock block-c 'gate.c1.classes='
inblock block-c 'gate.c1.at'
inblock block-c 'gate.c1=no-scan'
inblock block-c 'gate.c2.departed.want='
inblock block-c 'gate.c2.held.want='
inblock block-c 'gate.c2.held.ctor.want='
inblock block-c 'gate.c2.held.setacc.want='
inblock block-c 'gate.c2.depfrac='
inblock block-c 'gate.c2.holdfrac='
inblock block-c 'gate.c2.sites='
inblock block-c 'gate.c2.sites.was='
inblock block-c 'gate.c3.departed.want='
inblock block-c 'gate.c3.arrived.want='
inblock block-c 'gate.c3.held.want='
inblock block-c 'gate.c3.depfrac='
inblock block-c 'gate.c3.arrfrac='
inblock block-c 'gate.c3.holdfrac='
inblock block-c 'gate.c3.read='
inblock block-c 'gate.c3.unnamed='
inblock block-c 'gate.c3.unread='
inblock block-c 'gate.c4.departed.want='
inblock block-c 'gate.c4.arrived.want='
inblock block-c 'gate.c4.held.want='
inblock block-c 'gate.c4.depfrac='
inblock block-c 'gate.c4.arrfrac='
inblock block-c 'gate.c4.holdfrac='
inblock block-c 'gate.c4.exec.want='
inblock block-c 'gate.c4.exec.broken='
inblock block-c 'gate.c4.handles='
inblock block-c 'gate.c4.setaccessible='
inblock block-c 'gate.c4.setaccessible.was='
inblock block-c 'scan.txt'
inblock block-c 'baseline/priv.txt'
inblock block-c 'baseline/catchparam.txt'
inblock block-c 'baseline/varhandle.txt'
inblock block-c 'baseline/accessible.txt'
inblock block-c 'baseline/vhids.txt'
# THE ABSENCE, STATED. `inblock` can only say a fact is where it belongs; it
# cannot say a fact nobody wrote is missing, and C-2's arrival term is
# precisely a fact nobody wrote.
n=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -cE 'gate\.c2\.(arrived|arrfrac)' || true)
[ "${n}" = 0 ] \
   || bad "family C publishes gate.c2.arrived/arrfrac ${n} time(s); doPrivileged() is removed, not replaced"
note "C-2 publishes a departure half and a held half and no arrival term"

# ...and the five populations B-16 reads, the four B-17 reads and the five
# family C reads are three disjoint lists. FOURTEEN FILES AND THREE BLOCKS
# NOW, where a rung ago it was nine files and two: the ladder put a whole
# extra family in the held half, so the trap has one more direction to come
# from. A family C that still divided by reftest.txt would be scoring this
# rung's work against the rung below's list, and a B-17 that reached for
# varhandle.txt would be scoring the rung below's work against this one's --
# and both would publish a plausible fraction while doing it.
b16base='ipattern strnl tbids sealed swarrow'
b17base='reftest chain swids seqfirst'
cbase='priv catchparam varhandle vhids'
crossbase() {
   for b in $2; do
      n=$(sed 's/#.*//' "${TMP}/$1.sh" | grep -c "baseline/${b}\.txt" || true)
      [ "${n}" = 0 ] || bad "$3 reads baseline/${b}.txt ${n} time(s); that is $4's population"
   done
}
crossbase block-b16 "${b17base}" "B-16"     "B-17"
crossbase block-b16 "${cbase}"   "B-16"     "family C"
crossbase block-b17 "${b16base}" "B-17"     "B-16"
crossbase block-b17 "${cbase}"   "B-17"     "family C"
crossbase block-c   "${b16base}" "family C" "B-16"
crossbase block-c   "${b17base}" "family C" "B-17"
note "B-16's five baselines, B-17's four and family C's four are thirteen files and three blocks, disjoint"

# baseline/accessible.txt IS THE FOURTEENTH AND IT IS DELIBERATELY NOT IN THAT
# LIST, because it is the first baseline file in this task that two scoring
# blocks read on purpose. Step 06 cut it and every rung since staged it
# unchanged; this rung cannot, because its own C-4 converts four of the 14
# `setAccessible` calls to VarHandle. So the file now has two halves with two
# owners: B-13.3 divides by both halves, and C-2's third term intersects the
# `setacc` half with priv.txt's files to ask whether the constructions under
# an unwrapped `doPrivileged` survived the unwrapping.
#
# EXACTLY TWO BLOCKS, ASSERTED BOTH WAYS. A third reader would be a leg
# scoring itself against another leg's evidence, and a drop to one would mean
# either B-13.3 had quietly stopped dividing by the frozen population or C-2
# had lost the term that distinguishes unwrapping from deletion.
nacc=0
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 \
           block-b15 block-b16 block-b17 block-c; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'baseline/accessible\.txt' || true)
   case "${blk}" in
      block-b13|block-c)
         [ "${n}" -gt 0 ] \
            || bad "${blk} does not read baseline/accessible.txt; it is one of its two owners"
         nacc=$(( nacc + 1 )) ;;
      *)
         [ "${n}" = 0 ] \
            || bad "${blk} reads baseline/accessible.txt ${n} time(s); only B-13 and family C may" ;;
   esac
done
[ "${nacc}" = 2 ] || bad "baseline/accessible.txt has ${nacc} owner(s); it must have exactly two"
note "baseline/accessible.txt is read by B-13 and family C and by no other block"

# --- the guards, and which side of them each leg is on -----------------------
# THE INVERSION STEP 08 INTRODUCED, INHERITED, AND THEN A SECOND ONE. B-15's
# name leg reads the PUBLISHED JARS, and a tree whose sources the scorer could
# not compile against its own classpath may perfectly well have published
# twelve correctly named artifacts -- so B-15.2 sits ABOVE the guard and only
# B-15.3 sits inside it. B-12e is outside its own block's guard for the same
# reason and has been since step 05.
#
# Family C adds the second one: C-1 reads ${WORK}/scan.txt, written by
# arqgate.Scan over the class files the build left behind, and a build can
# leave class files behind that the scorer's own -source 22 probe will not
# accept. So C-1's guard asks about the SCAN and never about `measured`, and
# K17 -- the tree nobody could compile -- scores C-1 at 1.000000 while C-2 and
# C-3 publish the shell literal `0`. That is not a leak: the class files were
# measured, and the level they were compiled at is a fact about them
# regardless of what the scorer could reproduce.
#
# Asserted structurally here and scored on K17 below, because this is the kind
# of property a later edit moves by accident while making a guard tidier, and
# because the cost of getting it wrong is a silent 0.04 handed to a tree that
# never built -- or, in the other direction, a 0 handed to a tree that did.
#
# `guardend` takes the guard's opening line as a pattern rather than assuming
# one spelling: B-10 through B-14 open with `if [ -z "${measured}" ]` and take
# the else branch when there IS a report, while B-15, B-16's four legs,
# B-17's three and family C's three source legs open with
# `if [ -n ... ] && [ -s ... ]` and take the then branch. Two shapes, one
# meaning, and a helper that only knew the first would report every one of
# those guards as missing and pass.
#
# AND A THIRD SPELLING ARRIVED AT THIS RUNG. Two of family C's three source
# guards name TWO baseline files and are written across two lines, because
# C-2 and C-3 each read a population and then intersect it with priv.txt's:
# C-2 asks which constructions under an unwrapped `doPrivileged` survived,
# and C-3 asks which catch parameters in a method that HAD one can go
# unnamed. Their opening line therefore ends in a backslash and not in
# `; then`, so a pattern composed as `... && [ -r /tests/baseline/<one
# file> ]; then` -- which is what every rung from 09 to 10 could assume --
# matches neither of them and reports both guards as missing while passing
# everything else. `cleg` takes an optional second baseline name below and
# composes the continued spelling when it is given one, then reads the line
# under the guard to check that the second file is the one named. `guardend`
# needs no change: it anchors on the opening line as a prefix and the `fi` it
# looks for is still at column 0.
#
# -F, and that is load-bearing rather than tidy. Both guard spellings open
# with `[`, which a basic regular expression reads as a bracket expression, so
# ZGUARD matched its own line by accident -- `if ` plus any one character --
# and the `&&` spellings, which have a second `[` after a literal ` && `,
# matched nothing at all and reported the guard as missing. A pattern that is
# a line of shell is a fixed string.
guardline() { grep -nF -- "$2" "${TMP}/$1.sh" | head -1 | cut -d: -f1; }
guardend() {
   awk -v pat="$2" 'index($0, pat) == 1 { g = 1 } g && /^fi$/ { print NR; exit }' \
      "${TMP}/$1.sh"
}
ZGUARD='if [ -z "${measured}" ]'
MGUARD='if [ -n "${measured}" ] && [ -s "${measured}" ]; then'
SCANGUARD='if [ -s "${WORK}/scan.txt" ]; then'

gend=$(guardend block-b12 "${ZGUARD}")
cpline=$(guardline block-b12 'modulejars.txt')
if [ -n "${gend}" ] && [ -n "${cpline}" ] && [ "${cpline}" -gt "${gend}" ]; then
   note "B-12e is outside the measured-report guard (guard ends ${gend}, B-12e at ${cpline})"
else
   bad "B-12e is inside the measured-report guard, or the guard was not found:" \
       "guard ${gend:-?} B-12e ${cpline:-?}"
fi

# --- B-15: the name leg above the guard, the service leg inside it -----------
bgs=$(guardline block-b15 "${MGUARD}")
bge=$(guardend block-b15 "${MGUARD}")
modline=$(guardline block-b15 'arqgate.Modules')
b152line=$(guardline block-b15 'fact "B15.2.module-names=')
# `fbuckets svcload b15arrive` and not `fbuckets b15arrive`: fbuckets takes the
# RECORD KIND first and the bucket name second, so the two-token form is a
# string this file has never contained and guardline returned empty for it --
# which then reported B-15.3's guard geometry as moved on a block whose
# geometry was correct. Trap 6: the instrument, not the finding.
fbline=$(guardline block-b15 'fbuckets svcload b15arrive')
depline=$(guardline block-b15 'fact "gate.b15.departed=')
for v in bgs bge modline b152line fbline depline; do
   eval "[ -n \"\${${v}}\" ]" || bad "B-15's ${v} anchor was not found"
done
if [ "${modline}" -lt "${bgs}" ] && [ "${b152line}" -lt "${bgs}" ]; then
   note "the module read and B-15.2 are above B-15's measured guard (${modline} ${b152line} < ${bgs})"
else
   bad "B-15.2 has moved inside the measured guard:" \
       "modules ${modline} B-15.2 ${b152line} guard ${bgs}"
fi
if [ "${fbline}" -gt "${bgs}" ] && [ "${fbline}" -lt "${bge}" ] \
   && [ "${depline}" -gt "${bge}" ]; then
   note "B-15.3's buckets are inside the guard (${bgs} < ${fbline} < ${bge}) and its facts are published below it (${depline})"
else
   bad "B-15.3's guard geometry has moved: guard ${bgs}..${bge}, buckets ${fbline}, facts ${depline}"
fi

# --- family C: four legs, four guards, and C-1's is not about compiling ------
# Each of C-2 through C-4 opens its own guard naming its own baseline file, so
# a missing population file costs ONE leg rather than three. They are asserted
# individually and in order: a leg whose guard named the wrong baseline would
# still score -- on a population that is not its own -- and the table's totals
# alone could not say which leg had been handed which list. B-16's four do the
# same thing over step 09's five files and B-17's three over step 10's four,
# below -- ten guards over three blocks, where a rung ago it was eight over
# two.
c1gs=$(guardline block-c "${SCANGUARD}")
c1ge=$(guardend block-c "${SCANGUARD}")
c1line=$(guardline block-c 'fact "C1.level-')
if [ -n "${c1gs}" ] && [ -n "${c1ge}" ] && [ -n "${c1line}" ] \
   && [ "${c1line}" -gt "${c1ge}" ]; then
   note "C-1's guard is the scan file (${c1gs}..${c1ge}) and its fact is published below it (${c1line})"
else
   bad "C-1's guard geometry has moved: scan guard ${c1gs:-?}..${c1ge:-?}, fact ${c1line:-?}"
fi
# ...and it never asks whether anything compiled. This is the whole of K17's
# C-1 cell, structurally.
if sed -n "${c1gs},${c1ge}p" "${TMP}/block-c.sh" | grep -q 'measured'; then
   bad "C-1's guard reads \${measured}; the class-file level is not a fact about the scorer's compile"
else
   note "C-1's guard does not read \${measured}: the scan is its own channel"
fi

# `cleg` takes the BLOCK as its first argument this rung, because the same
# shape now has to be asserted twice over two different extracts. A helper
# hard-wired to block-c would have reported B-16's four guards as missing and
# passed, which is trap 6 with the instrument silently pointed at the wrong
# file.
cleg() {
   local blk=$1 leg=$2 base=$3 factpat=$4 base2=${5:-} g files gs ge ln
   if [ -n "${base2}" ]; then
      g="if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/${base} ] \\"
      files="${base}+${base2}"
   else
      g="if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/${base} ]; then"
      files="${base}"
   fi
   gs=$(guardline "${blk}" "${g}")
   ge=$(guardend "${blk}" "${g}")
   ln=$(guardline "${blk}" "${factpat}")
   # THE CONTINUATION LINE IS READ AND NOT ASSUMED. A two-file guard whose
   # first line still matched but whose second had been repointed at another
   # population would pass every check above and score the leg on the wrong
   # intersection, which is the same failure the one-file form is checked for
   # and one line further down.
   if [ -n "${base2}" ] && [ -n "${gs}" ]; then
      sed -n "$(( gs + 1 ))p" "${TMP}/${blk}.sh" \
         | grep -qF "&& [ -r /tests/baseline/${base2} ]; then" \
         || bad "${leg}: the line under its guard does not name ${base2}"
   fi
   if [ -n "${gs}" ] && [ -n "${ge}" ] && [ -n "${ln}" ] && [ "${ln}" -gt "${ge}" ]; then
      note "$(printf '%-6s guard %s..%s on %-26s total below it at %s' \
              "${leg}" "${gs}" "${ge}" "${files}" "${ln}")"
   else
      bad "${leg}: guard ${gs:-?}..${ge:-?} on ${files} in ${blk}, total ${ln:-?}"
   fi
}
cleg block-c   C-2    priv.txt      'fact "C2.doprivileged-removal=' accessible.txt
cleg block-c   C-3    catchparam.txt 'fact "C3.unnamed-catch='      priv.txt
cleg block-c   C-4    varhandle.txt 'fact "C4.varhandle='
# ...and step 09's four and step 10's three, in the blocks they were demoted
# into. THE BASELINE FILES ARE THE SAME NINE FILES and the guards are the same
# seven guards; only the variable names and the totals moved. That is the
# strongest statement this rehearsal can make about an inherited family -- not
# that it still scores, but that it still reads the same bytes. Both lists are
# spelled out rather than looped over one prefix, because B-16's legs are
# numbered 2..5 and B-17's 2..4 and a loop that got the range wrong would
# quietly check three guards and report four.
cleg block-b16 B-16.2 ipattern.txt 'fact "B16.2.pattern-instanceof='
cleg block-b16 B-16.3 strnl.txt    'fact "B16.3.text-blocks='
cleg block-b16 B-16.4 sealed.txt   'fact "B16.4.sealed-hierarchies='
cleg block-b16 B-16.5 swarrow.txt  'fact "B16.5.arrow-switch='
cleg block-b17 B-17.2 reftest.txt  'fact "B17.2.type-patterns='
cleg block-b17 B-17.3 chain.txt    'fact "B17.3.pattern-switch='
cleg block-b17 B-17.4 seqfirst.txt 'fact "B17.4.sequenced-access='

# THREE final assignments sit OUTSIDE their guards, one in each of the three
# blocks, because each multiplies by a run-time scaling read from
# behavior.summary rather than from the report. That is why B-16.3, B-17.3 and
# C-4 print `0.000000` on K17 while B-16.2, B-16.4, B-16.5, B-17.2, B-17.4,
# C-2 and C-3 print the shell literal `0`, and it is the difference the table
# asserts as strings rather than as numbers.
#
# WHICH LEG IS IN WHICH LIST MOVED THIS RUNG, AND THAT IS WHY BOTH LISTS ARE
# SPELLED OUT HERE. At step 10 the scaled leg of the family being scored was
# C-3 and C-4 was a literal; at this rung C-4 is the scaled one and C-3 is a
# literal, because an unnamed catch parameter has no run-time signature to
# record -- it is the same catch block with one identifier fewer -- while a
# VarHandle read either reaches the field or throws. Nothing about the
# arithmetic changed; the population did. A rehearsal that carried step 10's
# membership forward would assert `0.000000` where this rung prints `0`, on
# two cells, and would be wrong about both while passing everything else.
#
# THEY ARE THREE DIFFERENT SCALINGS OVER THREE DIFFERENT LISTS, which is why
# all three are asserted and not just the shape. B-16.3 is scaled by
# baseline/tbids.txt's five document recordings, B-17.3 by
# baseline/swids.txt's five dispatch recordings and C-4 by
# baseline/vhids.txt's four field-access recordings; the TB*, SW* and VH* runs
# at the foot of this file are what say the three lists are not each other's.
# Two of the three are five long, so the arithmetic cannot say it for them and
# the id lists have to.
outguard() {
   local blk=$1 leg=$2 gpat=$3 apat=$4 ge ln
   ge=$(guardend "${blk}" "${gpat}")
   ln=$(guardline "${blk}" "${apat}")
   if [ -n "${ge}" ] && [ -n "${ln}" ] && [ "${ln}" -gt "${ge}" ]; then
      note "${leg}'s total is assigned below its guard (${ge} < ${ln}): its zero is computed, not initialised"
   else
      bad "${leg}'s total is inside its guard: guard ends ${ge:-?}, assignment ${ln:-?}"
   fi
}
outguard block-b16 B-16.3 \
   "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/strnl.txt ]; then" \
   'b16_3=$(awk -v m='
outguard block-b17 B-17.3 \
   "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/chain.txt ]; then" \
   'b17_3=$(awk -v d='
outguard block-c   C-4 \
   "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/varhandle.txt ]; then" \
   'c4=$(awk -v d='
# ...and the other seven are literals, asserted as literals. `outguard` can
# only say that a computed total is computed below its guard; nothing above
# says that the seven which are NOT scaled are initialised to `0` and left
# there, and the table compares all thirteen figures as strings. A leg that
# gained an `awk` initialiser would print `0.000000` where every K row in the
# table says `0`, and forty-odd rows would fail together on a change that is
# one line long.
while read -r blk var; do
   [ -n "${blk}" ] || continue
   grep -qxF "${var}=0" "${TMP}/${blk}.sh" \
      || bad "${var} is not the shell literal 0 in ${blk}; K17's spelling would move"
done <<'LITS'
block-b16 b16_2
block-b16 b16_4
block-b16 b16_5
block-b17 b17_2
block-b17 b17_4
block-c c2
block-c c3
LITS
note "seven totals are the shell literal 0; the three that are computed are B-16.3, B-17.3 and C-4"

# ...and the parts that are INSIDE a guard are inside it, which is the other
# half of the same property and the one K17's MISSING cells assert
# behaviourally. B-13's six sub-facts and B-14's four legs, its gate.b14.*
# measurements and its penalty are published only when something compiled;
# their totals are not. A part that drifted out of its guard would publish a
# clean 0.000000 for a tree nobody could measure, which reads as a finding
# about the tree rather than about the run -- and gate.src.penalty is the worst
# of them, because `1` is the value it has when no term fired.
inguard() {
   local blk=$1 pat=$2 gs ge ln
   gs=$(guardline "${blk}" "${ZGUARD}")
   ge=$(guardend "${blk}" "${ZGUARD}")
   ln=$(guardline "${blk}" "${pat}")
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
for p in 'fact "B14.2.jakarta=' 'fact "B14.3.javax-held=' \
         'fact "B14.4.toarray=' 'fact "B14.5.httpclient=' \
         'fact "gate.b14.c2.want=' 'fact "gate.b14.c3.want=' \
         'fact "gate.b14.c4.want=' 'fact "gate.b14.c5.exec.want=' \
         'fact "gate.src.penalty='; do
   inguard block-b14 "${p}"
done
# ...and B-14's own total is NOT, which is what makes K17's 0.000000 an
# arithmetic result rather than an initialiser.
b14tot=$(guardline block-b14 'fact "B14.java11-jakarta=')
b14ge=$(guardend block-b14 "${ZGUARD}")
if [ -n "${b14tot}" ] && [ -n "${b14ge}" ] && [ "${b14tot}" -gt "${b14ge}" ]; then
   note "B-13's six parts and B-14's nine are inside their guards; B-14's total is below its own (${b14ge} < ${b14tot})"
else
   bad "B-14's total is inside its measured guard (guard ends ${b14ge:-?}, total ${b14tot:-?})"
fi

# The compiles happen in the first block and NOWHERE else. This is the other
# half of the hoist: if a compile crept back into a later block, that block
# would be measuring a different report from the ones the others scored, and
# the gates would disagree about the same tree.
n=$(grep -c 'srcgate ' "${TMP}/block-src.sh" || true)
[ "${n}" = 3 ] || bad "expected 3 srcgate calls in the source-AST block, found ${n}"
for blk in block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-b16 \
           block-b17 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the ten blocks may run a compiler, a build or a
# container. They are arithmetic over a report, one pass of unzip over jars
# that already exist, one read of a scan file that already exists and ONE run
# of the module gate -- that is what makes them rehearsable at all -- and the
# one thing that touches the tree is srcgate(), which is replaced below.
#
# `java` is split out from the other three and is not forbidden outright,
# because it is load-bearing in exactly one place. It is forbidden in NINE
# blocks and allowed EXACTLY ONCE in B-15, on a line that also names
# arqgate.Modules: a second java invocation there would be a second reading of
# the same jars, and a java in any other block would be a gate measuring
# something this rehearsal has not staged. FAMILY C HAS BEEN ON THE FORBIDDEN
# SIDE SINCE STEP 09, which it was not at step 08 -- C-1 reads a file
# arqgate.Scan wrote during the build stage, and a family C that ran java would
# be re-deriving at score time a reading the build already took. B-16 and B-17
# are on the forbidden side for the same reason and by inheritance: step 09's
# family C ran nothing, step 10's ran nothing, and the leg of step 08's family
# that DID read an artefact is the one leg the ladder kept as B-15. Nine
# blocks that read files and one that runs a program is the whole shape of
# this scorer. Comments are stripped first: the blocks discuss javac at
# length.
cat "${TMP}"/block-*.sh | sed 's/#.*//' \
   | grep -nE '(^|[;&|(]|\$\() *(javac|docker|\./gradlew)([ "]|$)' \
   > "${TMP}/runs.txt" || true
if [ -s "${TMP}/runs.txt" ]; then
   bad "a block runs a compiler, a build or a container:"
   sed 's/^/         /' "${TMP}/runs.txt"
else
   note "no block runs javac, docker or gradlew"
fi
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b16 \
           block-b17 block-c; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" \
       | grep -cE '(^|[;&|(]|\$\() *java([ "]|$)' || true)
   [ "${n}" = 0 ] || bad "${blk} runs java ${n} time(s); only B-15 may"
done
njava=$(sed 's/#.*//' "${TMP}/block-b15.sh" \
        | grep -cE '(^|[;&|(]|\$\() *java([ "]|$)' || true)
nmod=$(sed 's/#.*//' "${TMP}/block-b15.sh" \
       | grep -E '(^|[;&|(]|\$\() *java([ "]|$)' | grep -c 'arqgate\.Modules' || true)
if [ "${njava}" = 1 ] && [ "${nmod}" = 1 ]; then
   note "B-15 runs java exactly once, on arqgate.Modules; no other block runs it"
else
   bad "B-15's java invocations: ${njava} (want 1), of which arqgate.Modules: ${nmod} (want 1)"
fi

# --- the shim ----------------------------------------------------------------
# srcgate(), replaced. Same signature, same artefacts, same echoed answer: the
# fixture's report becomes src-<tag>.txt and the fixture's answers become the
# .env the block reads source.ok out of.
#
# THREE tags at this step: `22`, `21` and `def`. The block asks for 22 first;
# if that succeeded it asks for 21, because a tree that still compiles at
# -source 21 has no Java 22 language feature load-bearing in it, which is a
# finding and not a failure; and it asks for `def` only when 22 failed. So a
# fixture's answers.txt carries all three and the shim answers whichever the
# block asks for.
#
# 21 AND NOT 20, AND THE NUMBER IS DERIVED RATHER THAN DECREMENTED. The
# control has to be the level at which exactly one of this rung's four legs
# stops compiling, and three of the four move no level at all: removing
# doPrivileged() is an API deletion and not a language change, so C-2 moves
# none; `privateLookupIn` and `unreflectVarHandle` are Java 9 methods, so C-4
# moves none; and the class-file major is a property of the emitted artifact
# rather than of what javac will parse, so C-1 moves none either. What is left
# is C-3's UNNAMED VARIABLE. `_` has been a reserved keyword and an error as
# an identifier since Java 9, JEP 456 gave it back a meaning as preview in 21
# and standard in 22, so a tree that has done C-3's work cannot compile at
# -source 21 without --enable-preview. Everything step 10 put in -- type
# patterns from 16, the pattern switch from 21, the sequenced accessors which
# are an API and not a language change -- is standard at 21 and leaves the
# control at `yes` on its own. The control therefore reads the unnamed catch
# parameters and nothing else, which is why it cannot be read as a proxy for
# the whole rung and why C-2 and C-4 have no control of their own.
#
# The javac log is written only for the tag that FAILED, because the block only
# reads one when the probe said no -- it prints the compiler's complaint rather
# than twelve lines of `files=322` from the gate's own .env. A shim that wrote
# one for every tag would hand the block a 1.17 compile that both succeeded and
# left an error log behind, which is a tree no compiler produces.
cat > "${TMP}/shim.sh" <<'SHIM'
TREE=/nonexistent

# THE TWO CLASS-FILE MAJORS, BOUND HERE BECAUSE FAMILY C READS THEM AGAIN.
# test.sh assigns both at the top of the file, far above the first block this
# rehearsal lifts, so nothing in the composed script would define them and the
# whole of family C would die on its first reference under `set -u` -- every C
# cell MISSING, which reads like five broken legs and is one missing binding.
#
# Read from the SAME baseline file test.sh reads, and not typed as 66 and 65:
# the fixtures' scan.txt files were generated against that file too, so a
# literal here would be a second source of truth that agrees with the first
# only until someone moves the target major.
#
# This is the binding step 08's shim did not need. At that rung family C asked
# about manifests, the level was held, and the rehearsal asserted that NO block
# read WANT_MAJOR. This rung inverted that assertion -- exactly one block may
# read it, and it is family C -- and the inversion was written without the
# binding that makes it runnable.
WANT_MAJOR=$(sed -n 's/^major=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
HOLD_MAJOR=$(sed -n 's/^was=//p'   /tests/baseline/level.txt 2>/dev/null | head -1)
: "${WANT_MAJOR:?the rehearsal cannot read major= from /tests/baseline/level.txt}"
: "${HOLD_MAJOR:?the rehearsal cannot read was= from /tests/baseline/level.txt}"
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
      # writes and which B-12e and B-15.2 BOTH read. One file, two channels: a
      # fixture with no jars/ directory leaves it empty, which is exactly how a
      # build that published nothing reaches both blocks -- `[ -s ]` and not
      # `[ -f ]` in each of them.
      echo 'ls "${FIXDIR}"/jars/*.jar > "${WORK}/modulejars.txt" 2>/dev/null || :'
      # ...and the class-file scan, which is family C's own channel and nobody
      # else's. COPIED AND NOT LINKED, and absent when the fixture has none: a
      # `cp` that failed would leave whatever the previous fixture wrote, and
      # every run after L2 would read the scan L2 was written to be missing.
      # ${WORK} is wiped per fixture above, which is what makes the absence an
      # absence.
      echo 'if [ -f "${FIXDIR}/scan.txt" ]; then cp "${FIXDIR}/scan.txt" "${WORK}/scan.txt"; fi'
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" "${TMP}/bhelp.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-b11.sh" \
          "${TMP}/block-b12.sh" "${TMP}/block-b13.sh" "${TMP}/block-b14.sh" \
          "${TMP}/block-b15.sh" "${TMP}/block-b16.sh" "${TMP}/block-b17.sh" \
          "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- seventy-two trees, and seventy-two of them different -----------------
# Every fixture is a mutation of one of the two ends, and a mutation that did
# not land produces a directory identical to the end it came from -- which then
# scores exactly what that end scores and passes its row while asserting
# nothing. The seventh trap, in the fixtures rather than in the tree. why.txt
# is excluded because it is prose the generator writes and would make any two
# fixtures differ; everything the blocks actually read is hashed, INCLUDING
# scan.txt, which is what separates L1 and L2 from the golden they are built
# out of.
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

# --- what each fixture must score --------------------------------------------
# fixture  B10 B11 B12 B13 B14 B15 B16 B17 | C1 C2 C3 C4 | familyC
# Transcribed by hand from mkfix11.py's printed table. The two ends are the
# rows that carry the step's floor argument: `pinned` is the tree this rung is
# handed and must read every family-C leg at 0 with all eight held gates in
# full -- migration_prompt item 6's nop run -- and `golden` must read all
# thirteen in full, which is item 6's oracle.
#
# `pinned`'s B15, B16 AND B17 ARE ALL 1.000000 AND THAT IS THE FLOOR ARGUMENT,
# not a mistake. This rung's input tree is step 10's output, so the twelve
# artifacts already declare their module names, the service lookups have
# already moved, every one of step 09's four source constructs is in place and
# so is every one of step 10's. THREE inherited families now start full, and an
# inherited gate that starts full gives the floor no help. THAT IS WHY THIS IS
# THE FIRST RUNG WHOSE FLOOR IS NOT 0.475000, and the reason is not family B's
# three full gates -- it is B-6, which no fixture in this table carries. B-6's
# expectation is cut from the FINISHED tree, so the input tree still publishes
# the 42 `GetTcclAction` signatures C-2 withdraws and family B reads 0.997882
# rather than 1.000000. Family A is not flat either: 903 of 939 packaging
# transcripts, 0.961661, the 36 that C-2's deletion moves. The floor is
# 0.30 x 0.961661 + 0.70 x 0.25 x 0.997882 = 0.463128, a margin of 0.036872
# under item 6's bar, and the NOTES 16.17/16.18 entry measures it rather than
# assuming the 0.475000 every rung below could write down.
#
# THIRTEEN columns and not six, because at this rung neither family's legs are
# interchangeable and the totals hide which one moved. K2 and K20 both read
# B15 0.562500 and they are different trees: K2 published twelve jars and named
# none of them, K20 published nothing at all -- only gate.b15.mod.total tells
# those apart. L12 and L13 both read B16 0.955556 and they are different
# trees: one reverted a file's switch, the other left it colon and added an
# arrow elsewhere -- only gate.b16.5.arrow tells those apart. L6 and L14 both
# read B16 0.711111 and they are different trees: L6 deleted the text block,
# L14 left one in the file but OUTSIDE the method that had to write it -- only
# gate.b16.3.textblocks tells those apart.
#
# B15 0.437500 IS A TRIPLE AND NOT A PAIR, which is the widest collision in
# the table and the one worth reading first. K17, M4 and M5 all read it and
# all three are different trees: K17 has no report at all -- gate.b15.arrived,
# .departed and .held are the shell literal 0 -- M4 has a report whose service
# lookups arrived in no module at all, so gate.b15.arrived is 0.000000 with
# the other two full, and M5's report is the golden's with all 69 recorded
# service behaviours differing, so the three structural halves are full and
# gate.b15.exec is 0.000000 against gate.b15.exec.broken 69. Three shapes, one
# figure, and the composite 0.35 / 0.80 says nothing about which. SVCALL
# reaches the same 0.437500 from the doctored-summary side, which makes four.
#
# TWO PAIRS IN THE N SERIES, and they are the halves of a held leg this rung
# did not write. N1 and N2 both read B17 0.959135: N1 left the three single
# reflective tests standing beside their new patterns, N2 removed the three
# patterns and left the tests gone -- gate.b17.2.depfrac against
# gate.b17.2.arrfrac. N10 and N11 both read B17 0.958333, and they are
# gate.b17.4.depfrac against gate.b17.4.arrfrac. A block that read one channel
# of a pair wrongly would collapse each pair onto one message while keeping the
# number, which is why the facts below are asserted and not only the totals.
#
# N2 MOVES A SECOND HELD COLUMN AND IT DID NOT ONE RUNG AGO. It reads B16
# 0.960784 as well, because THIS rung re-cut ipattern.txt -- 24 sites where
# step 09 had 28, the seven `SecurityActions#getConstructor` ladders having
# left the tree -- and the three patterns step 10 wrote are among the survivors.
# Deleting them is a missing conversion in both families and both say so.
#
# N7 AND N8 WERE A THIRD SUCH PAIR AT THE RUNG BELOW AND ARE NOT ONE HERE.
# Both read 0.675000 there; N7 loses B-17.3's hold half for 0.500000 and lands
# on B17 0.661458, N8 loses two of five recordings for 0.600000 and lands on
# 0.729167. The pair is still worth keeping and both rows still assert
# gate.b17.3.holdfrac AND gate.b17.3.exec, because what makes two rows a pair
# here is the two halves they fail in and not the figure they happened to
# share.
#
# AND ONE PAIR IN THE P SERIES, WHICH IS THIS RUNG'S OWN. P10 and P11 both
# read C4 0.750000 and familyC 0.940000: P10 left one of the four fields
# unconverted so gate.c4.depfrac is 0.750000 and gate.c4.arrfrac 0.750000, and
# P11 built two lookups it never unreflected so gate.c4.depfrac is 1.000000
# and gate.c4.arrfrac 0.750000. The arrival denominator is TWICE the
# departure's -- two records per converted bucket, `privateLookupIn` and
# `unreflectVarHandle` -- and a leg that counted one would collapse these two
# trees onto one message while keeping the score.
#
# N14 IS THE ROW THE FIRST REVIEW ASKED FOR, one family over from where it
# started. Four correct pattern switches added and not one reflective test
# removed: B-17.3 is 0.000000, B-17.2 falls with it to 0.230769 because ten of
# its thirteen conv sites are back, and B17 is 0.186699 -- reached through
# gate.b17.3.depfrac on a tree whose gate.b17.3.arrived, .held and .exec are
# all the golden's. No other row in the table carries that B17 with that
# B-17.2 and that B-17.3.
#
# N15 IS THE SECOND HALF OF THAT STORY AND THE ROW THE SECOND REVIEW ASKED
# FOR, because N14 as it stood only priced the crudest form of the attack.
# The tree N14 describes is one automated rewrite away from a tree that keeps
# every one of its four useless switches and pays nothing for them: run the
# ten `EnterpriseArchive.class.isInstance(x)` arms through the rung below's
# requirement 4 -- OpenRewrite does eight of them unattended -- and every
# `reftest` record is gone while the chains that dispatch are still chains,
# now spelled `x instanceof EnterpriseArchive e` one arm at a time. Against a
# departure half that counted only surviving `reftest` records that tree read
# gate.b17.3.departed 10 of 10 and B-17.3 1.000000. N15 IS THAT TREE, and what
# makes it a row rather than a footnote is that the departure half also counts
# the chain's own type names surviving as `ipattern` in the same bucket --
# gate.b17.3.ipleft 10, residual 0 + 10 against 10 arms, gate.b17.3.departed 0,
# B-17.3 0.000000, B17 0.322917.
#
# N14 AND N15 ARE NOT A PAIR IN THE SENSE THE ROWS ABOVE ARE -- they read
# different B17 figures, so no fact is needed to tell them apart -- but they
# are the two halves of one finding and the column to read them together by is
# B-17.2: 0.230769 against 1.000000 for the same four useless switches. That
# difference is not a leak and it is not generosity. B-17.2 asks that the
# thirteen reflective tests stop being reflective and that patterns arrive
# naming their types, and N15's ten of them did both, so B-17.2 is right to pay
# for them in full, and the whole burden of saying that the DISPATCH did not
# move falls on B-17.3 where it belongs.
#
# N15's B17 0.322917 IS THE ONE COLLISION IN THIS TABLE A DOCTORED SUMMARY CAN
# REACH: SWALL reads it too, from the other side -- five of five recordings
# broken against a perfect source shape. gate.b17.3.exec.broken separates
# them, 0 against 5, and the SW* table below is where that is asserted. SWTWO
# and N8 collide the same way at 0.729167, and TBALL, L6 and L14 at B16
# 0.711111. Three collisions across two channels is what a table with
# thirteen columns and a behaviour scaling per family costs, and naming them
# is cheaper than pretending the totals are unique.
TABLE=$(cat <<'EOF'
pinned   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.000000 0.000000 0.000000 0.000000  0.000000
golden   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K1       1.000000 1.000000 1.000000 1.000000 1.000000 0.930122 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K2       1.000000 1.000000 1.000000 1.000000 1.000000 0.562500 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K3       1.000000 1.000000 1.000000 1.000000 1.000000 0.866319 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K4       1.000000 1.000000 1.000000 1.000000 0.955555 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K5       1.000000 1.000000 1.000000 1.000000 0.987879 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K6       1.000000 1.000000 1.000000 1.000000 0.927273 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K7       1.000000 1.000000 1.000000 1.000000 0.995607 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K8       1.000000 1.000000 1.000000 1.000000 0.932540 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K9       1.000000 1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K10      1.000000 1.000000 1.000000 1.000000 0.916667 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K11      1.000000 1.000000 1.000000 1.000000 0.850000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K12      1.000000 1.000000 1.000000 1.000000 0.700000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K13      1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K14      1.000000 1.000000 1.000000 1.000000 0.800000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K15      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K16      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K17      0        0        0.000000 0.000000 0.000000 0.437500 0.000000 0.000000  1.000000 0        0        0.000000  0.040000
K18      1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K19      1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
K20      1.000000 1.000000 0.850000 1.000000 1.000000 0.562500 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M2       1.000000 1.000000 1.000000 1.000000 1.000000 0.718750 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M3       1.000000 1.000000 1.000000 1.000000 1.000000 0.948864 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M4       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M5       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
M6       1.000000 1.000000 1.000000 1.000000 1.000000 0.926630 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.947226 1.000000 1.000000 1.000000  0.997889
L2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0        1.000000 1.000000 1.000000  0.960000
L3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.843137 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.777778 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.904762 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.711111 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.876191 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.884444 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.818182 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.600000 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.963636 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.955556 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.955556 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
L14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.711111 1.000000  1.000000 1.000000 1.000000 1.000000  1.000000
N1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.959135  1.000000 1.000000 1.000000 1.000000  1.000000
N2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.960784 0.959135  1.000000 1.000000 1.000000 1.000000  1.000000
N3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.949405  1.000000 1.000000 1.000000 1.000000  1.000000
N4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.822917  1.000000 1.000000 1.000000 1.000000  1.000000
N5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.796875  1.000000 1.000000 1.000000 1.000000  1.000000
N6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.850962  1.000000 1.000000 1.000000 1.000000  1.000000
N7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.661458  1.000000 1.000000 1.000000 1.000000  1.000000
N8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.729167  1.000000 1.000000 1.000000 1.000000  1.000000
N9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.756010  1.000000 1.000000 1.000000 1.000000  1.000000
N14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.186699  1.000000 1.000000 1.000000 1.000000  1.000000
N15      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.322917  1.000000 1.000000 1.000000 1.000000  1.000000
N10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.958333  1.000000 1.000000 1.000000 1.000000  1.000000
N11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.958333  1.000000 1.000000 1.000000 1.000000  1.000000
N12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.951389  1.000000 1.000000 1.000000 1.000000  1.000000
N13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.979167  1.000000 1.000000 1.000000 1.000000  1.000000
P1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.850746 1.000000 1.000000  0.931343
P2       1.000000 1.000000 1.000000 0.969231 1.000000 1.000000 1.000000 1.000000  1.000000 0.769231 1.000000 1.000000  0.893846
P3       1.000000 1.000000 1.000000 0.960000 1.000000 1.000000 1.000000 1.000000  1.000000 0.846154 1.000000 0.800000  0.881231
P4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.895522 1.000000 1.000000  0.951940
P5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.923077 1.000000 1.000000  0.964615
P6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.857143 1.000000  0.962857
P7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.785714 1.000000  0.944286
P8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.901961 1.000000  0.974510
P9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.764706 1.000000  0.938824
P10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.750000  0.940000
P11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.750000  0.940000
P12      1.000000 1.000000 1.000000 0.980000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.900000  0.976000
P13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.000000  0.760000
P14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.500000  0.880000
P15      1.000000 1.000000 1.000000 0.980000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.675000  0.922000
EOF
)

echo
echo "== running the composed blocks against each fixture"
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture B10 B11 B12 B13 B14 B15 B16 B17 C1 C2 C3 C4 familyC
while read -r fix w10 w11 w12 w13 w14 w15 w16 w17 wc1 wc2 wc3 wc4 wfc; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   g10=$(fv "${fix}" 'B10\.java7-held')
   g11=$(fv "${fix}" 'B11\.java8lang-held')
   g12=$(fv "${fix}" 'B12\.java8-library')
   g13=$(fv "${fix}" 'B13\.java9-10')
   g14=$(fv "${fix}" 'B14\.java11-jakarta')
   g15=$(fv "${fix}" 'B15\.jpms-services')
   g16=$(fv "${fix}" 'B16\.java17-constructs')
   g17=$(fv "${fix}" 'B17\.java21-constructs')
   # C-1's fact carries the major in its NAME -- `C1.level-66` -- because the
   # number a rung flips to is the one thing about that rung a reader should
   # not have to look up. Spelled out here rather than globbed, so a rung that
   # moved the level without moving this file fails on the key rather than
   # silently reading MISSING into every row. It caught the rung below: the
   # step-09 spelling was `C1.level-61`, and 61 is two majors behind the one
   # the tree now ARRIVES at.
   #
   # THE OTHER THREE MOVED WHOLESALE THIS RUNG and none of them kept its name.
   # Step 10's C-2, C-3 and C-4 were `type-patterns`, `pattern-switch` and
   # `sequenced-access`; they are B-17's now, read three lines up, and this
   # rung's own three are `doprivileged-removal`, `unnamed-catch` and
   # `varhandle`. A rehearsal carried across unchanged would have read the
   # held family's four facts into the C columns and passed every row in the
   # table while scoring nothing this rung wrote.
   gc1=$(fv "${fix}" 'C1\.level-66')
   gc2=$(fv "${fix}" 'C2\.doprivileged-removal')
   gc3=$(fv "${fix}" 'C3\.unnamed-catch')
   gc4=$(fv "${fix}" 'C4\.varhandle')
   gfc=$(fv "${fix}" familyC)
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${g10:-MISSING}" "${g11:-MISSING}" "${g12:-MISSING}" \
      "${g13:-MISSING}" "${g14:-MISSING}" "${g15:-MISSING}" "${g16:-MISSING}" \
      "${g17:-MISSING}" \
      "${gc1:-MISSING}" "${gc2:-MISSING}" "${gc3:-MISSING}" \
      "${gc4:-MISSING}" "${gfc:-MISSING}"
   [ "${g10}" = "${w10}" ] || bad "${fix}: B10 want ${w10} got ${g10:-MISSING}"
   [ "${g11}" = "${w11}" ] || bad "${fix}: B11 want ${w11} got ${g11:-MISSING}"
   [ "${g12}" = "${w12}" ] || bad "${fix}: B12 want ${w12} got ${g12:-MISSING}"
   [ "${g13}" = "${w13}" ] || bad "${fix}: B13 want ${w13} got ${g13:-MISSING}"
   [ "${g14}" = "${w14}" ] || bad "${fix}: B14 want ${w14} got ${g14:-MISSING}"
   [ "${g15}" = "${w15}" ] || bad "${fix}: B15 want ${w15} got ${g15:-MISSING}"
   [ "${g16}" = "${w16}" ] || bad "${fix}: B16 want ${w16} got ${g16:-MISSING}"
   [ "${g17}" = "${w17}" ] || bad "${fix}: B17 want ${w17} got ${g17:-MISSING}"
   [ "${gc1}" = "${wc1}" ] || bad "${fix}: C1 want ${wc1} got ${gc1:-MISSING}"
   [ "${gc2}" = "${wc2}" ] || bad "${fix}: C2 want ${wc2} got ${gc2:-MISSING}"
   [ "${gc3}" = "${wc3}" ] || bad "${fix}: C3 want ${wc3} got ${gc3:-MISSING}"
   [ "${gc4}" = "${wc4}" ] || bad "${fix}: C4 want ${wc4} got ${gc4:-MISSING}"
   [ "${gfc}" = "${wfc}" ] || bad "${fix}: familyC want ${wfc} got ${gfc:-MISSING}"
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
   if grep -qE "^${key}=" "${TMP}/facts-${fix}.txt"; then
      got=$(fv "${fix}" "${key}")
   else
      got=MISSING
   fi
   if [ "${got}" = "${want}" ]; then
      printf '  ok   %-9s %-32s %s\n' "${fix}" "${key}" "${got}"
   else
      bad "${fix}: ${key} want ${want} got ${got:-MISSING}"
   fi
}

# --- pinned: the nop, in full ------------------------------------------------
# This rung's input tree is the step-10 golden. It compiles at 22 AND at 21,
# every family-C leg that carries weight reads zero, and every inherited gate
# reads full. That is migration_prompt item 6's nop run, asserted at image
# build time rather than only in a job.
#
# Every `want` here is the size of a frozen set or a baseline counter, and
# reading them off the facts is how this rehearsal knows the baseline files
# reached the image at all -- a missing set file makes the want 0 and every
# fraction over it 0, which scores a tree that did nothing the same as one
# that did everything.
#
# B-15, B-16 AND B-17 ALL READ 1.000000 ON THE NOP AND THAT IS THE FLOOR
# ARGUMENT, NOT A DEFECT. B-15 is step 08's family C carried forward, B-16 is
# step 09's and B-17 is step 10's, and the tree this rung is handed IS step
# 10's finished answer: twelve artifacts already declare their module names,
# the platform lookup has already moved, seventeen instanceof sites already
# carry a pattern variable, the eleven event roots are already sealed, thirteen
# reflective type tests have already become type patterns, ten if-chain arms
# are already four pattern switches, seven first-element reads already call
# getFirst(). An inherited gate that charged the step-11 candidate for the
# step-10 candidate's work would be the worse verifier. The price of getting it
# right is that family B's inherited gates give the floor no help whatever.
# They are not the whole of family B, though, and this is the rung where that
# stops being a technicality: B-6's expectation is cut from the finished tree,
# the input still publishes the 42 `GetTcclAction` signatures C-2 withdraws,
# and family B therefore reads 0.997882 on the nop rather than 1.000000. With
# family A at 903/939 = 0.961661 the floor is
# 0.30 x 0.961661 + 0.70 x 0.25 x 0.997882 = 0.463128 -- the first rung whose
# floor is not 0.475000 -- and family C still carries the entire separation on
# its own. The NOTES 16.17/16.18 entry for this rung measures that rather than
# assuming it.
#
# THREE INHERITED FAMILIES START FULL AND THAT IS ONE MORE THAN THE RUNG BELOW
# HAD. B-15 and B-16 were both full on step 10's nop too; B-17 is new, and it
# is full here for the same reason they are. The stack only grows, so the
# quantity family B contributes to the separation goes on shrinking as the
# ladder climbs, and the fixtures below are the argument that family C alone is
# still enough to tell a real answer from a plausible one.
#
# THE FOUR FAMILY-C CELLS THAT READ FULL HERE ARE ALL HOLDS, and every one of
# them is a min() term or a multiplier rather than a leg with weight. Thirteen
# privileged constructions and scanners that must NOT lose their wrappers, the
# fifty-one catch parameters whose exception is actually read and must keep
# their names, the ten `setAccessible` calls that must stay reflective, the
# four recordings that say access still fails the way it failed before: the
# unmigrated tree holds all of them for free. Every leg that does carry weight
# -- at66, and every departed and arrived on C-2, C-3 and C-4 -- reads zero on
# the same tree, which is the arithmetic that keeps standing still worth
# nothing.
factck pinned gate.src.ok22 yes
factck pinned gate.src.ok21 yes
factck pinned gate.b15.mod.total 12
factck pinned gate.b15.mod.declared 12
factck pinned gate.b15.mod.derivable 11
factck pinned gate.b15.names 12
factck pinned gate.b15.departed 1.000000
factck pinned gate.b15.arrived 1.000000
factck pinned gate.b15.held 1.000000
factck pinned gate.b15.exec.want 69
factck pinned gate.b15.exec.broken 0
factck pinned gate.b15.exec 1.000000
# ...and B-16 leg by leg, on the tree that HAS done all of it. Step 09's family
# C minus its class-file leg, and the four that came across read on the nop
# exactly what a correct answer to step 09 left behind: seventeen converted
# instanceof sites, one text block, eleven sealed roots with thirty-five
# permitted subtypes, two files whose colon switches became arrow switches.
#
# TWO OF THESE FIGURES ARE THIS RUNG'S OWN AND NOT STEP 09'S, and they are the
# two the ladder could not carry unchanged. `sites.want` is 24 and not step
# 09's 31, because seven of its conv buckets are the seven
# `SecurityActions#getConstructor` ladders THIS rung's C-2 deletes;
# baseline/ipattern.txt was re-cut on the golden for exactly that reason and
# `converted.want` fell 21 -> 17 with it. The measured `sites` still reads 31
# here, because on the SEED those seven methods are still standing -- it is 24
# on the golden, which is the one place the term is tight. NOTES 16.16.
factck pinned gate.b16.2.sites 31
factck pinned gate.b16.2.sites.want 24
factck pinned gate.b16.2.totfrac 1.000000
factck pinned gate.b16.2.converted 17
factck pinned gate.b16.2.converted.want 17
factck pinned gate.b16.2.convfrac 1.000000
factck pinned gate.b16.2.held 7
factck pinned gate.b16.2.held.want 7
factck pinned gate.b16.2.holdfrac 1.000000
factck pinned gate.b16.3.moved 1
factck pinned gate.b16.3.moved.want 1
factck pinned gate.b16.3.movefrac 1.000000
factck pinned gate.b16.3.textblocks 1
factck pinned gate.b16.3.held 7
factck pinned gate.b16.3.held.want 7
factck pinned gate.b16.3.holdfrac 1.000000
factck pinned gate.b16.3.exec.want 5
factck pinned gate.b16.3.exec.broken 0
factck pinned gate.b16.3.exec 1.000000
factck pinned gate.b16.4.decl 11
factck pinned gate.b16.4.nonsealed 0
factck pinned gate.b16.4.permits 35
factck pinned gate.b16.4.roots 11
factck pinned gate.b16.4.roots.want 11
factck pinned gate.b16.4.closedfrac 1.000000
factck pinned gate.b16.4.open 11
factck pinned gate.b16.4.open.want 11
factck pinned gate.b16.4.openfrac 1.000000
# B-16.5 IS THE OTHER FIGURE THAT MOVED, AND IT MOVED FROM THE OTHER SIDE.
# `size` and `arrow` read 6 on this tree and read 2 on step 10's nop, because
# step 10's own C-3 added four pattern switches and every one of them is an
# arrow switch. The leg divides by baseline/swarrow.txt's TWO ROWS and not by
# what the report counts, so the four extra cost nothing; `files.want` is 2 and
# stays 2. A denominator taken from `switch.files` instead would have capped a
# correct tree at 0.333333 -- the ip.* trap arriving from the opposite
# direction, and the reason target.txt states sw.files rather than deriving it.
factck pinned gate.b16.5.size 6
factck pinned gate.b16.5.arrow 6
factck pinned gate.b16.5.colon 0
factck pinned gate.b16.5.expr 3
factck pinned gate.b16.5.files 2
factck pinned gate.b16.5.files.want 2
# ...and B-17 leg by leg, which is the column this rung added. Step 10's family
# C minus ITS class-file leg, by the rule that made step 09's into B-16 one
# rung ago: C-2's thirteen reflective type tests that became type patterns and
# the fourteen that had to stay reflective, C-3's ten if-chain arms that became
# four pattern switches and the two chains that had to stay chains, C-4's seven
# first-element reads that became getFirst() and the nine that could not.
#
# THE HOLD HALVES ARE WHY IT IS WORTH CARRYING, exactly as B-16's are. This
# rung asks a candidate to delete privileged wrappers and to rewrite catch
# parameters, which is work inside the same methods B-17's three legs are
# stated over, and a candidate who deletes a little too much takes the hold
# halves down with it. Nothing else in this file would notice.
factck pinned gate.b17.2.reftests 22
factck pinned gate.b17.2.patterns 31
factck pinned gate.b17.2.swpatterns 10
factck pinned gate.b17.2.departed 13
factck pinned gate.b17.2.departed.want 13
factck pinned gate.b17.2.depfrac 1.000000
factck pinned gate.b17.2.arrived 13
factck pinned gate.b17.2.arrived.want 13
factck pinned gate.b17.2.arrfrac 1.000000
factck pinned gate.b17.2.held 14
factck pinned gate.b17.2.held.want 14
factck pinned gate.b17.2.holdfrac 1.000000
factck pinned gate.b17.3.patterns 10
factck pinned gate.b17.3.switches 4
factck pinned gate.b17.3.nullcase 1
factck pinned gate.b17.3.departed 10
factck pinned gate.b17.3.departed.want 10
factck pinned gate.b17.3.depfrac 1.000000
factck pinned gate.b17.3.arrived 10
factck pinned gate.b17.3.arrived.want 10
factck pinned gate.b17.3.arrfrac 1.000000
factck pinned gate.b17.3.held 2
factck pinned gate.b17.3.held.want 2
factck pinned gate.b17.3.holdfrac 1.000000
factck pinned gate.b17.3.ipleft 0
factck pinned gate.b17.3.exec.want 5
factck pinned gate.b17.3.exec.broken 0
factck pinned gate.b17.3.exec 1.000000
factck pinned gate.b17.4.firstreads 19
factck pinned gate.b17.4.sequenced 7
factck pinned gate.b17.4.sequenced.read 7
factck pinned gate.b17.4.departed 7
factck pinned gate.b17.4.departed.want 7
factck pinned gate.b17.4.depfrac 1.000000
factck pinned gate.b17.4.arrived 7
factck pinned gate.b17.4.arrived.want 7
factck pinned gate.b17.4.arrfrac 1.000000
factck pinned gate.b17.4.held 9
factck pinned gate.b17.4.held.want 9
factck pinned gate.b17.4.holdfrac 1.000000
# ...and family C leg by leg, on the tree that did none of it. The four weights
# these four legs carry -- 0.04, 0.46, 0.26, 0.24 -- are what a candidate who
# does the work is paid, and the row above the table says the sum of them here
# is 0.000000.
#
# FOUR POPULATION COUNTERS READ HIGHER HERE THAN ON THE GOLDEN AND TWO READ
# LOWER, and that is the shape of a departure-and-arrival pair rather than a
# defect. gate.c2.sites is 67 on the input tree and 0 on the answer, because
# every privileged wrapper is still standing; gate.c4.setaccessible is 14
# against 10, the four C-4 converts; gate.c3.unread is 39 against 4, the
# thirty-five catch parameters whose names have not gone yet; gate.c1.classes
# is 746 against 739, the seven nested privileged-action classes the ladders
# take with them. The two that read lower are the arrivals -- gate.c3.unnamed
# 0 against 28 and gate.c4.handles 0 against 8 -- and a tree where a counter
# moved in only one of the two directions did half the work and said it had
# done all of it.
factck pinned gate.c1.classes 746
factck pinned gate.c1.at66 0
factck pinned gate.c2.sites 67
factck pinned gate.c2.departed 0
factck pinned gate.c2.departed.want 67
factck pinned gate.c2.depfrac 0.000000
factck pinned gate.c2.held 13
factck pinned gate.c2.held.want 13
factck pinned gate.c2.held.ctor 7
factck pinned gate.c2.held.ctor.want 7
factck pinned gate.c2.held.setacc 6
factck pinned gate.c2.held.setacc.want 6
factck pinned gate.c2.holdfrac 1.000000
factck pinned gate.c3.read 65
factck pinned gate.c3.unread 39
factck pinned gate.c3.unnamed 0
factck pinned gate.c3.departed 0
factck pinned gate.c3.departed.want 35
factck pinned gate.c3.depfrac 0.000000
factck pinned gate.c3.arrived 0
factck pinned gate.c3.arrived.want 28
factck pinned gate.c3.arrfrac 0.000000
factck pinned gate.c3.held 51
factck pinned gate.c3.held.want 51
factck pinned gate.c3.holdfrac 1.000000
factck pinned gate.c4.setaccessible 14
factck pinned gate.c4.handles 0
factck pinned gate.c4.departed 0
factck pinned gate.c4.departed.want 4
factck pinned gate.c4.depfrac 0.000000
factck pinned gate.c4.arrived 0
factck pinned gate.c4.arrived.want 8
factck pinned gate.c4.arrfrac 0.000000
factck pinned gate.c4.held 10
factck pinned gate.c4.held.want 10
factck pinned gate.c4.holdfrac 1.000000
factck pinned gate.c4.exec.want 4
factck pinned gate.c4.exec.broken 0
factck pinned gate.c4.exec 1.000000
# ...and every one of B-14's channels, because the nop run's whole claim is
# that step 07's work is still there. A pinned row that quietly lost one of
# these would be the step-09 author's problem and not this one's.
factck pinned gate.b14.c2.want 6
factck pinned gate.b14.c2.departed 6
factck pinned gate.b14.c2.arrived 6
factck pinned gate.b14.c2.javax.left 0
factck pinned gate.b14.c2.jakarta.refs 22
factck pinned gate.b14.c3.want 43
factck pinned gate.b14.c3.held 43
factck pinned gate.b14.c3.refs 112
factck pinned gate.b14.c3.unscaled 1.000000
factck pinned gate.b14.c4.want 5
factck pinned gate.b14.c4.departed 5
factck pinned gate.b14.c4.arrived 5
factck pinned gate.b14.c5.legacy 0
factck pinned gate.b14.c5.types 3
factck pinned gate.b14.c5.redirects 1
factck pinned gate.b14.c5.redirects.honoured 1
factck pinned gate.b14.c5.cdistub 3
factck pinned gate.b14.c5.exec.want 18
factck pinned gate.b14.c5.exec.broken 0
factck pinned gate.src.impjx 0
factck pinned gate.src.impjk 3
factck pinned gate.src.penalty 1

# --- golden: the oracle ------------------------------------------------------
# Every inherited gate reads exactly what it read on `pinned` -- that is what
# "does not regress the previous rungs" means, spelled as an assertion -- and
# the four family-C legs all move. 739 class files at major 66 instead of 0,
# sixty-seven privileged wrappers gone instead of 0, thirty-five catch
# parameters surrendered and twenty-eight unnamed variables arrived instead of
# 0, four `setAccessible` calls replaced by eight VarHandle operations instead
# of 0. The four holds do not move at all, which is the other half of the
# claim: the correct answer removes what was asked for and nothing else.
#
# THIS IS THE FIRST RUNG WHOSE ORACLE IS SMALLER THAN ITS SEED, and three
# counters say so. gate.c1.classes is 739 against the nop's 746, because the
# seven `SecurityActions` ladders take seven nested privileged-action classes
# with them. gate.b16.2.sites and gate.b17.2.patterns -- which are one scan
# counter read by two different gates -- are both 24 against the nop's 31, for
# the same seven methods. Every rung below this one added code and the
# inherited counters only ever read higher on the oracle; here they read lower,
# and the two gates that divide by them were re-cut on the golden rather than
# on the seed so that the fall costs a correct tree nothing. B-16.2's
# `sites.want` is 24 and its term is exactly tight here, which is the one place
# in the table where that term is not slack. NOTES 16.16.
#
# THE CONTROL FLIPS AND THIS IS THE ROW THAT SAYS SO. gate.src.ok22 is `yes`
# and gate.src.ok21 is `no` on the oracle: `_` as an unnamed variable is
# standard at 22 and preview at 21, so a tree that has done C-3's work cannot
# be parsed at -source 21 without --enable-preview. Nothing scores it. It is
# the single line in the report that distinguishes a partial answer that did
# C-3 from one that did everything else instead, and the nop's `yes` two
# sections above is the other end of it.
factck golden gate.src.ok22 yes
factck golden gate.src.ok21 no
factck golden gate.b15.mod.total 12
factck golden gate.b15.mod.declared 12
factck golden gate.b15.mod.derivable 11
factck golden gate.b15.names 12
factck golden gate.b15.departed 1.000000
factck golden gate.b15.arrived 1.000000
factck golden gate.b15.held 1.000000
factck golden gate.b15.exec.want 69
factck golden gate.b15.exec.broken 0
factck golden gate.b15.exec 1.000000
factck golden gate.b16.2.sites 24
factck golden gate.b16.2.sites.want 24
factck golden gate.b16.2.totfrac 1.000000
factck golden gate.b16.2.converted 17
factck golden gate.b16.2.converted.want 17
factck golden gate.b16.2.convfrac 1.000000
factck golden gate.b16.2.held 7
factck golden gate.b16.2.held.want 7
factck golden gate.b16.2.holdfrac 1.000000
factck golden gate.b16.3.moved 1
factck golden gate.b16.3.movefrac 1.000000
factck golden gate.b16.3.textblocks 1
factck golden gate.b16.3.held 7
factck golden gate.b16.3.holdfrac 1.000000
factck golden gate.b16.3.exec.want 5
factck golden gate.b16.3.exec.broken 0
factck golden gate.b16.3.exec 1.000000
factck golden gate.b16.4.decl 11
factck golden gate.b16.4.nonsealed 0
factck golden gate.b16.4.permits 35
factck golden gate.b16.4.roots 11
factck golden gate.b16.4.roots.want 11
factck golden gate.b16.4.closedfrac 1.000000
factck golden gate.b16.4.open 11
factck golden gate.b16.4.openfrac 1.000000
factck golden gate.b16.5.size 6
factck golden gate.b16.5.arrow 6
factck golden gate.b16.5.colon 0
factck golden gate.b16.5.expr 3
factck golden gate.b16.5.files 2
# ...and B-17 reads on the oracle exactly what it read on the nop, every leg,
# including the three counters the deletion moved underneath it. The seven
# ladders held no reflective type test step 10 had converted, no chain arm and
# no first-element read, so all three of B-17's departure-and-arrival pairs are
# untouched by this rung's work. gate.b17.2.patterns fell 31 -> 24 all the
# same, and the leg does not read it as a denominator -- which is the whole of
# why it is safe.
factck golden gate.b17.2.reftests 22
factck golden gate.b17.2.patterns 24
factck golden gate.b17.2.swpatterns 10
factck golden gate.b17.2.departed 13
factck golden gate.b17.2.departed.want 13
factck golden gate.b17.2.depfrac 1.000000
factck golden gate.b17.2.arrived 13
factck golden gate.b17.2.arrived.want 13
factck golden gate.b17.2.arrfrac 1.000000
factck golden gate.b17.2.held 14
factck golden gate.b17.2.held.want 14
factck golden gate.b17.2.holdfrac 1.000000
factck golden gate.b17.3.patterns 10
factck golden gate.b17.3.switches 4
factck golden gate.b17.3.nullcase 1
factck golden gate.b17.3.departed 10
factck golden gate.b17.3.departed.want 10
factck golden gate.b17.3.depfrac 1.000000
factck golden gate.b17.3.arrived 10
factck golden gate.b17.3.arrived.want 10
factck golden gate.b17.3.arrfrac 1.000000
factck golden gate.b17.3.held 2
factck golden gate.b17.3.holdfrac 1.000000
factck golden gate.b17.3.ipleft 0
factck golden gate.b17.3.exec.want 5
factck golden gate.b17.3.exec.broken 0
factck golden gate.b17.3.exec 1.000000
factck golden gate.b17.4.firstreads 19
factck golden gate.b17.4.sequenced 7
factck golden gate.b17.4.sequenced.read 7
factck golden gate.b17.4.departed 7
factck golden gate.b17.4.departed.want 7
factck golden gate.b17.4.depfrac 1.000000
factck golden gate.b17.4.arrived 7
factck golden gate.b17.4.arrived.want 7
factck golden gate.b17.4.arrfrac 1.000000
factck golden gate.b17.4.held 9
factck golden gate.b17.4.holdfrac 1.000000
# ...and this rung's own four, every one of them at the top of its range. The
# two denominators that are NOT the same number on both ends of the rung are
# C-1's -- 739 class files and not the seed's 746 -- and C-2's departure, which
# is 67 sites to remove against a tree that has 0 left.
factck golden gate.c1.classes 739
factck golden gate.c1.at66 739
factck golden gate.c2.sites 0
factck golden gate.c2.departed 67
factck golden gate.c2.departed.want 67
factck golden gate.c2.depfrac 1.000000
factck golden gate.c2.held 13
factck golden gate.c2.held.want 13
factck golden gate.c2.held.ctor 7
factck golden gate.c2.held.setacc 6
factck golden gate.c2.holdfrac 1.000000
factck golden gate.c3.read 58
factck golden gate.c3.unread 4
factck golden gate.c3.unnamed 28
factck golden gate.c3.departed 35
factck golden gate.c3.departed.want 35
factck golden gate.c3.depfrac 1.000000
factck golden gate.c3.arrived 28
factck golden gate.c3.arrived.want 28
factck golden gate.c3.arrfrac 1.000000
factck golden gate.c3.held 51
factck golden gate.c3.holdfrac 1.000000
factck golden gate.c4.setaccessible 10
factck golden gate.c4.handles 8
factck golden gate.c4.departed 4
factck golden gate.c4.departed.want 4
factck golden gate.c4.depfrac 1.000000
factck golden gate.c4.arrived 8
factck golden gate.c4.arrived.want 8
factck golden gate.c4.arrfrac 1.000000
factck golden gate.c4.held 10
factck golden gate.c4.holdfrac 1.000000
factck golden gate.c4.exec.want 4
factck golden gate.c4.exec.broken 0
factck golden gate.c4.exec 1.000000
factck golden gate.src.penalty 1

# The one fact in B-15 that cannot be asserted by equality, and the reason is
# worth writing down: arqgate.Modules reports the resolution failure with the
# jar's path as the platform saw it, which is an ABSOLUTE path under a mktemp
# directory and is different on every run. A factck on it would fail always and
# a factck on the empty string would pass always.
#
# What is asserted instead is the shape and the subject. The tree declares
# twelve names correctly and STILL does not resolve as a module configuration,
# because arquillian-impl-base ships a META-INF/services/ entry naming a
# provider that is not in the module and ModuleFinder refuses to derive a
# descriptor for that jar. One unreadable artifact takes the whole
# configuration down, so mod.resolved is 0 on the correct answer. That is not a
# defect and it is not scored -- nothing in B-15 reads mod.resolved, and as of
# this rung nothing reads mod.derivable either. It is published so the
# trajectory analysis can see WHY the number it is looking at is what it is.
factck golden gate.b15.mod.resolved 0
gres=$(fv golden 'gate\.b15\.mod\.resolve')
case "${gres}" in
   Unable_to_derive_module_descriptor_for_*arquillian-impl-base*)
      note "golden gate.b15.mod.resolve names the poisoned artifact: ${gres##*/}" ;;
   *) bad "golden gate.b15.mod.resolve does not name the poisoned artifact: ${gres:-MISSING}" ;;
esac
# ...and the tree that published nothing has nothing to say about resolution.
factck K20 gate.b15.mod.resolve unread
factck K20 gate.b15.mod 'no-module-jars'

# --- C-1: the class-file major, on its own channel ---------------------------
# L1 and L2 are the two rows that exist because C-1 sits on a channel of its
# own. C-1 reads ${WORK}/scan.txt, written by arqgate.Scan over what the build
# left in build/classes; every other leg of family C reads the source AST that
# srcgate compiled. A tree can move either without the other, and at step 08 no
# fixture could say so because both of that rung's channels came off one list
# of jars.
#
# L1 is the partial migration that actually happens: the modules whose build
# file moved are at 66 and the thirty-nine classes whose did not are still at
# 65. The leg is linear in the fraction, so 700/739 is 0.947226 and familyC is
# 0.997889 -- C-1 is 0.04 of the family, so the thirty-nine class files still
# at 65 cost 0.04 x 39/739 = 0.002111 and nothing else.
#
# AND THE DENOMINATOR IS 739 AND NOT THE SEED'S 746. This row is a tree that
# did the rest of the rung: the seven `SecurityActions` ladders are gone and
# their seven nested privileged-action classes with them, so there are 739
# class files to have an opinion about. A fixture that had been carried across
# from step 10 unchanged would have divided by 746, published 0.938338, and
# been wrong by one part in four hundred in a direction no total would show.
factck L1 gate.c1.classes 739
factck L1 gate.c1.at66 700
factck L1 C1.level-66 0.947226
# ...and L2 is the absence. No scan.txt at all, which is what a build that
# produced no class files leaves behind, and the leg publishes the sentinel and
# the shell literal `0` rather than a computed 0.000000 over an empty file.
# THE OTHER THREE LEGS ARE UNTOUCHED, which is the whole point of the row: the
# source AST arrived, so C-2, C-3 and C-4 all read full and familyC is exactly
# 1 - 0.04 = 0.960000. B-16 and B-17 are untouched too, on a third channel
# again. A family C that read its major out of the report, or that let one
# missing file zero the whole family, fails here and nowhere else.
factck L2 gate.c1 'no-scan'
factck L2 gate.c1.classes MISSING
factck L2 gate.c1.at66 MISSING
factck L2 C1.level-66 0
factck L2 gate.c2.depfrac 1.000000
factck L2 gate.c3.depfrac 1.000000
factck L2 gate.c3.arrfrac 1.000000
factck L2 gate.c4.arrfrac 1.000000
factck L2 gate.b16.4.decl 11
factck L2 gate.b16.5.arrow 6

# --- C-2: the ladder that goes and the two things inside it that must not ----
# P1 through P5 are family C's own second leg, and the leg is two halves under
# a min() for a reason the rung below never had to state: JEP 486 removed the
# Security Manager, so `AccessController.doPrivileged` has nothing left to do
# and every one of its 67 sites must go -- but the ladder is not empty. Each
# `SecurityActions` wrapper exists to reach a `Constructor.newInstance` or a
# `setAccessible`, and those 13 calls are the work the ladder was wrapping.
# Deleting the ladder with its contents satisfies the first half perfectly and
# is not the migration.
#
# P1 AND P4 TAKE THE DEPARTURE DOWN, P2, P3 AND P5 THE HOLD, AND NO ROW TAKES
# BOTH. That is the only way to show a min() is over two things: a tree that
# lost both would score the same as one that lost the lower, and the table
# would show one cell where two facts moved.
#
# P1 left one of the seven wrapper files alone -- ten sites in four buckets --
# so the departure reads 57 of 67 and the hold is whole. It is the row that
# says this leg prices what a tree did NOT do: P1 did more of the rung's work
# than any other P row and is the third-cheapest of the five.
factck P1 gate.c2.sites            10
factck P1 gate.c2.departed         57
factck P1 gate.c2.departed.want    67
factck P1 gate.c2.depfrac          0.850746
factck P1 gate.c2.held             13
factck P1 gate.c2.held.want        13
factck P1 gate.c2.held.ctor        7
factck P1 gate.c2.held.setacc      6
factck P1 gate.c2.holdfrac         1.000000
factck P1 C2.doprivileged-removal  0.850746
factck P1 familyC                  0.931343
# P4 is the same half from the other side and it is the row the `in=-` bucket
# rule exists for: every CALL was unwrapped and all seven `implements
# PrivilegedAction` declarations were left standing, inside no method at all.
# pbuckets() keys those `<file>#-`, so they are 7 of the 67; a census that
# dropped them would hand this tree 1.000000 for a class that still implements
# an interface JEP 486 removed.
factck P4 gate.c2.sites            7
factck P4 gate.c2.departed         60
factck P4 gate.c2.depfrac          0.895522
factck P4 gate.c2.holdfrac         1.000000
factck P4 C2.doprivileged-removal  0.895522
factck P4 familyC                  0.951940
# P2 DELETED THREE LADDERS WHOLE and the departure half reads 1.000000 -- a
# deletion satisfies it perfectly, which is exactly why it is not the whole
# leg. The hold reads 10 of 13, its constructor term 4 of 7, and B-13 falls
# with it: `newinstance.ctor` is step 06's floor and reads the same 10 against
# its own want of 13. One tree, two rungs, and the second one notices because
# the first one asked for the call in the first place.
factck P2 gate.c2.depfrac          1.000000
factck P2 gate.c2.held             10
factck P2 gate.c2.held.ctor        4
factck P2 gate.c2.held.ctor.want   7
factck P2 gate.c2.held.setacc      6
factck P2 gate.c2.holdfrac         0.769231
factck P2 C2.doprivileged-removal  0.769231
factck P2 B13.java9-10             0.969231
factck P2 familyC                  0.893846
# P3 IS THE ROW WHERE ONE RECORD KIND MOVES THREE COLUMNS. Two of the six
# annotation scanners went with their wrappers, so C-2's hold reads 11 of 13 on
# its `setAccessible` term, B-13.3 reads 8 of its ten surviving calls, and
# C-4's hold reads the SAME 8 of 10 -- a deleted `setAccessible` is three
# different failures and the tree it describes is one tree. The three are not
# redundant: C-2 asks for the 6 inside the wrapper files, B-13.3 and C-4 ask
# over all 10 that survive this rung.
factck P3 gate.c2.depfrac          1.000000
factck P3 gate.c2.held             11
factck P3 gate.c2.held.ctor        7
factck P3 gate.c2.held.setacc      4
factck P3 gate.c2.held.setacc.want 6
factck P3 gate.c2.holdfrac         0.846154
factck P3 C2.doprivileged-removal  0.846154
factck P3 gate.c4.setaccessible    8
factck P3 gate.c4.held             8
factck P3 gate.c4.held.want        10
factck P3 gate.c4.holdfrac         0.800000
factck P3 C4.varhandle             0.800000
factck P3 B13.java9-10             0.960000
factck P3 familyC                  0.881231
# P5 IS THE ROW THAT SAYS THE HOLD IS CAPPED PER FILE. One wrapper file lost
# its construction and another grew two extra -- a refactor that MOVED the
# reflective creation. C-2's hold reads 12 of 13, because the file that gained
# two scores the 1 it always scored and the file that lost its own scores 0.
# B-13 reads 1.000000 on the same tree: its floor term is a bare
# `newinstance.ctor` over the whole tree, which is 14 against a want of 13 and
# clamps. Two arithmetics over one record kind, disagreeing on purpose, and
# this is the row that says which one family C uses.
factck P5 gate.c2.depfrac          1.000000
factck P5 gate.c2.held             12
factck P5 gate.c2.held.ctor        6
factck P5 gate.c2.holdfrac         0.923077
factck P5 C2.doprivileged-removal  0.923077
factck P5 B13.java9-10             1.000000
factck P5 familyC                  0.964615
# ...and the other three legs of family C are untouched in all five, which is
# what makes each row a one-leg finding except where it says otherwise.
for f in P1 P2 P4 P5; do
   factck "${f}" C1.level-66  1.000000
   factck "${f}" C3.unnamed-catch 1.000000
   factck "${f}" C4.varhandle 1.000000
   factck "${f}" gate.c3.depfrac 1.000000
   factck "${f}" gate.c3.arrfrac 1.000000
   factck "${f}" gate.c3.holdfrac 1.000000
   factck "${f}" gate.c4.holdfrac 1.000000
   factck "${f}" gate.c4.exec 1.000000
   factck "${f}" B17.java21-constructs 1.000000
   factck "${f}" B16.java17-constructs 1.000000
done
factck P3 C1.level-66      1.000000
factck P3 C3.unnamed-catch 1.000000

# --- C-3: three terms, and the two denominators that differ by seven ---------
# P6 through P9 are the unnamed catch parameter, JEP 456, and the leg is THREE
# terms under a min() rather than two. A parameter that is read keeps its name;
# a parameter that is read by nothing becomes `_`. So there is a departure
# (unread parameters that still have names), an arrival (unread parameters that
# now say `_`) and a hold (read parameters that must keep theirs), and a tree
# can fail any one of the three while satisfying the other two.
#
# THE DEPARTURE AND THE ARRIVAL ARE STATED OVER DIFFERENT POPULATIONS AND THE
# DIFFERENCE IS EXACTLY SEVEN. 35 unread parameters exist in the input tree; 28
# of them survive C-2, because seven are inside the `SecurityActions` ladders
# C-2 deletes. They cannot be asked to arrive as `_` AND to still be there.
# P6 charges both halves for the same four parameters and they charge different
# prices -- 31 of 35 departing, 24 of 28 arriving -- and the min() takes the
# arrival. A leg that stated both over 35, or both over 28, would read this
# tree at 0.885714 or at 0.857143 in BOTH cells and the pair would not show.
factck P6 gate.c3.unread           8
factck P6 gate.c3.unnamed          24
factck P6 gate.c3.read             58
factck P6 gate.c3.departed         31
factck P6 gate.c3.departed.want    35
factck P6 gate.c3.depfrac          0.885714
factck P6 gate.c3.arrived          24
factck P6 gate.c3.arrived.want     28
factck P6 gate.c3.arrfrac          0.857143
factck P6 gate.c3.held             51
factck P6 gate.c3.holdfrac         1.000000
factck P6 C3.unnamed-catch         0.857143
factck P6 familyC                  0.962857
# P7 IS THE ROW THE ARRIVAL HALF EXISTS FOR, and it is C-2's hold turned on a
# different leg. Six catch blocks were DELETED rather than their parameters
# renamed: the exception is swallowed by not being caught at all. The departure
# reads 1.000000, because a parameter that is not there is not unread. `Stop
# doing it` and `delete the code that did it` produce the same departure count
# and only one of them is the migration.
factck P7 gate.c3.unread           4
factck P7 gate.c3.unnamed          22
factck P7 gate.c3.departed         35
factck P7 gate.c3.depfrac          1.000000
factck P7 gate.c3.arrived          22
factck P7 gate.c3.arrfrac          0.785714
factck P7 gate.c3.holdfrac         1.000000
factck P7 C3.unnamed-catch         0.785714
factck P7 familyC                  0.944286
# P8 takes the hold and NOTHING ELSE, which is the assertion. Five parameters
# that WERE read are `_` now -- the candidate deleted the logging along with the
# name -- so the hold reads 46 of 51 and the arrival does not rise: those five
# buckets are outside the 24 it is stated over, and psum() caps each of those at
# what it asked for anyway. A tree cannot pay for a diagnostic it deleted with a
# rename it was owed. THE HOLD IS 51 AND NOT 58, for C-2's reason again: the
# seven read parameters inside the deleted ladders are subtracted here too.
factck P8 gate.c3.read             53
factck P8 gate.c3.unnamed          33
factck P8 gate.c3.held             46
factck P8 gate.c3.held.want        51
factck P8 gate.c3.holdfrac         0.901961
factck P8 gate.c3.arrived          28
factck P8 gate.c3.arrfrac          1.000000
factck P8 gate.c3.depfrac          1.000000
factck P8 C3.unnamed-catch         0.901961
factck P8 familyC                  0.974510
# P9 IS THE ROW THAT SAYS THERE ARE THREE TERMS AND NOT TWO. All three fall at
# once and the min() takes the worst of them: twelve read parameters renamed
# `_` for 0.764706, two never renamed for 0.942857 departing and 0.928571
# arriving. A leg that min()ed the departure against the arrival and ADDED the
# hold, or averaged any two of the three, would read this tree above 0.764706
# and the table would show it in one cell.
factck P9 gate.c3.read             46
factck P9 gate.c3.unnamed          38
factck P9 gate.c3.unread           6
factck P9 gate.c3.departed         33
factck P9 gate.c3.depfrac          0.942857
factck P9 gate.c3.arrived          26
factck P9 gate.c3.arrfrac          0.928571
factck P9 gate.c3.held             39
factck P9 gate.c3.holdfrac         0.764706
factck P9 C3.unnamed-catch         0.764706
factck P9 familyC                  0.938824
# ...and C-2 and C-4 are whole in all four. A catch-parameter edit that moved
# either would mean the three legs are reading one another's populations rather
# than three cuts of one AST.
for f in P6 P7 P8 P9; do
   factck "${f}" C1.level-66            1.000000
   factck "${f}" C2.doprivileged-removal 1.000000
   factck "${f}" C4.varhandle           1.000000
   factck "${f}" gate.c2.depfrac        1.000000
   factck "${f}" gate.c2.holdfrac       1.000000
   factck "${f}" gate.c4.depfrac        1.000000
   factck "${f}" gate.c4.arrfrac        1.000000
   factck "${f}" gate.c4.holdfrac       1.000000
   factck "${f}" B13.java9-10           1.000000
   factck "${f}" B17.java21-constructs  1.000000
done

# --- C-4: four terms, and the fourth one multiplies --------------------------
# P10 through P15 are the VarHandle leg, and it is the one leg of family C with
# a behaviour list of its own: three structural terms under a min(), times a
# fourth read off baseline/vhids.txt's four recorded access failures. Four
# `setAccessible` calls must go, eight records must arrive in their place --
# `privateLookupIn` and `unreflectVarHandle`, two per converted bucket -- and
# the ten calls that survive must stay.
#
# P10 AND P11 ARE THE SAME 0.750000 FROM TWO SIDES and the pair is why the
# arrival denominator is twice the departure's. P10 did not convert one field
# at all: departure 3 of 4, arrival 6 of 8. P11 converted two and never
# unreflected them -- `privateLookupIn` is there, `unreflectVarHandle` is not:
# departure 4 of 4, arrival the same 6 of 8. `gate.c4.depfrac` is what tells
# them apart, and a leg that counted one record per bucket instead of two would
# collapse the two trees onto one message while keeping the score.
factck P10 gate.c4.setaccessible   11
factck P10 gate.c4.handles         6
factck P10 gate.c4.departed        3
factck P10 gate.c4.departed.want   4
factck P10 gate.c4.depfrac         0.750000
factck P10 gate.c4.arrived         6
factck P10 gate.c4.arrived.want    8
factck P10 gate.c4.arrfrac         0.750000
factck P10 gate.c4.held            10
factck P10 gate.c4.holdfrac        1.000000
factck P10 C4.varhandle            0.750000
factck P10 B13.java9-10            1.000000
factck P10 familyC                 0.940000
factck P11 gate.c4.setaccessible   10
factck P11 gate.c4.handles         6
factck P11 gate.c4.departed        4
factck P11 gate.c4.depfrac         1.000000
factck P11 gate.c4.arrived         6
factck P11 gate.c4.arrfrac         0.750000
factck P11 gate.c4.holdfrac        1.000000
factck P11 C4.varhandle            0.750000
factck P11 familyC                 0.940000
# P12 IS THE HOLD, AND THE HOLD IS THE JUDGEMENT THIS LEG IS ABOUT. A field
# that must NOT move was converted anyway, so nine of the ten survive; the
# arrival does not rise, because psum() states it over the four convert buckets
# and this is not one of them. `unreflectVarHandle` compiles for all fourteen
# sites and nothing in the language says which ten must stay -- the four
# recorded access failures do. B-13.3 reads the same 9 of 10 and carries B-13
# to 0.980000, on the same tree.
factck P12 gate.c4.setaccessible   9
factck P12 gate.c4.handles         10
factck P12 gate.c4.held            9
factck P12 gate.c4.held.want       10
factck P12 gate.c4.holdfrac        0.900000
factck P12 gate.c4.arrived         8
factck P12 gate.c4.arrfrac         1.000000
factck P12 gate.c4.depfrac         1.000000
factck P12 C4.varhandle            0.900000
factck P12 B13.java9-10            0.980000
factck P12 familyC                 0.976000
# P13 IS THE ONLY ROW THAT SAYS THE DEPARTURE IS NOT READ OFF `varhandle`. All
# four handles were built AND all four `setAccessible` calls left beside them --
# the tree that added the new API without removing the old one. The arrival
# reads 8 of 8 and the leg is 0.000000 anyway, because the departure is read off
# the candidate's `accessible` records and reads 0 of 4. A leg that scored the
# arrival alone would pay this tree in full for a migration that removed
# nothing, which is the same reading error B-13.3 was rebaselined over one rung
# ago.
factck P13 gate.c4.setaccessible   14
factck P13 gate.c4.handles         8
factck P13 gate.c4.departed        0
factck P13 gate.c4.departed.want   4
factck P13 gate.c4.depfrac         0.000000
factck P13 gate.c4.arrived         8
factck P13 gate.c4.arrfrac         1.000000
factck P13 gate.c4.holdfrac        1.000000
factck P13 gate.c4.exec            1.000000
factck P13 C4.varhandle            0.000000
factck P13 familyC                 0.760000
# P14 AND P15 ARE THE FOURTH TERM, AND IT MULTIPLIES. P14's source shape is
# perfect and two of the four recorded access failures answer differently under
# the candidate's tree: every structural term reads 1.000000 and the leg reads
# 0.500000. THE FOUR IDS ARE C-4'S OWN AND NOBODY ELSE'S -- the fifth transcript
# carrying an `IllegalAccessException` raises it inside `java.util.ServiceLoader`
# and is B-15.3's, which is why B-15 reads 1.000000 in this row rather than
# 0.985507.
factck P14 gate.c4.depfrac         1.000000
factck P14 gate.c4.arrfrac         1.000000
factck P14 gate.c4.holdfrac        1.000000
factck P14 gate.c4.exec.want       4
factck P14 gate.c4.exec.broken     2
factck P14 gate.c4.exec            0.500000
factck P14 C4.varhandle            0.500000
factck P14 B15.jpms-services       1.000000
factck P14 familyC                 0.880000
# P15 is the product, and it is the row that says the scaling IS a product. One
# held field converted -- P12's edit -- for a structural 0.900000, and one of
# the four access failures answering differently for a scaling of 0.750000. A
# leg that min()ed the scaling in would read 0.750000 here and be wrong by
# 0.018000 of family C; a leg that added it would be wrong the other way.
# Neither is visible from a row that moves only one of the two, which is why
# this row moves both. The scaling is C-4's alone and no B leg sees it: B-13.3
# reads 0.900000 here for P12's reason and B-13 0.980000.
factck P15 gate.c4.setaccessible   9
factck P15 gate.c4.handles         10
factck P15 gate.c4.held            9
factck P15 gate.c4.holdfrac        0.900000
factck P15 gate.c4.depfrac         1.000000
factck P15 gate.c4.arrfrac         1.000000
factck P15 gate.c4.exec.broken     1
factck P15 gate.c4.exec            0.750000
factck P15 C4.varhandle            0.675000
factck P15 B13.java9-10            0.980000
factck P15 familyC                 0.922000
# ...and the three legs above C-4 are whole in all six, including on the two
# rows that move a `setAccessible` count. C-2's hold asks for the six inside the
# wrapper files and P12's and P15's converted field is not one of them, which is
# the distinction P3 made from the other direction.
for f in P10 P11 P12 P13 P14 P15; do
   factck "${f}" C1.level-66             1.000000
   factck "${f}" C2.doprivileged-removal 1.000000
   factck "${f}" C3.unnamed-catch        1.000000
   factck "${f}" gate.c2.depfrac         1.000000
   factck "${f}" gate.c2.holdfrac        1.000000
   factck "${f}" gate.c2.held.setacc     6
   factck "${f}" gate.c3.depfrac         1.000000
   factck "${f}" gate.c3.arrfrac         1.000000
   factck "${f}" gate.c3.holdfrac        1.000000
   factck "${f}" B16.java17-constructs   1.000000
   factck "${f}" B17.java21-constructs   1.000000
done

# --- B-16.2: the pattern, the hold and the sites -----------------------------
# Three terms under a min(), and L3, L4 and L5 each take one of them down while
# leaving the other two at 1.000000. That is the only way to show a min() is
# over three things: a fixture that dropped two of them would score the same as
# one that dropped the lower.
#
# L3 is the migration that stopped halfway -- five of seventeen convertible
# sites carry a pattern variable and twelve are still a plain instanceof with a
# cast after them. L4 is the opposite error and the more interesting one: every
# convertible site converted AND the seven sites that have no cast after them
# bound as well, which does not compile in the real tree and which a gate that
# only counted `kind=bound` would read as a perfect score. L5 deleted three of
# those seven rather than leaving them alone.
#
# THE TOTAL-SITE TERM IS LIVE AT THIS RUNG AND IT WAS NOT AT THE RUNG BELOW.
# Step 10 could not make it move: its population was 28 against a tree that
# scanned 31, so three sites could be deleted before the fraction left 1. This
# rung re-cut baseline/ipattern.txt on the GOLDEN -- 24 sites, exactly what the
# golden scans -- so L5's three deletions read 21/24 = 0.875000 and the term
# finally says something. It is still not the min(): L5's hold half is
# 0.571429, which is the dominance argument in B-16.2's own header arriving as
# a measurement rather than as algebra. The sites the fraction is short of are
# not the sites the hold half is short of, and gate.b16.2.held is what says so.
factck L3 gate.b16.2.converted 5
factck L3 gate.b16.2.converted.want 17
factck L3 gate.b16.2.convfrac 0.294118
factck L3 gate.b16.2.held 7
factck L3 gate.b16.2.holdfrac 1.000000
factck L3 gate.b16.2.sites 24
factck L3 gate.b16.2.totfrac 1.000000
factck L4 gate.b16.2.converted 17
factck L4 gate.b16.2.convfrac 1.000000
factck L4 gate.b16.2.held 0
factck L4 gate.b16.2.held.want 7
factck L4 gate.b16.2.holdfrac 0.000000
factck L4 gate.b16.2.sites 24
factck L4 gate.b16.2.totfrac 1.000000
factck L5 gate.b16.2.converted 17
factck L5 gate.b16.2.convfrac 1.000000
factck L5 gate.b16.2.held 4
factck L5 gate.b16.2.holdfrac 0.571429
factck L5 gate.b16.2.sites 21
factck L5 gate.b16.2.sites.want 24
factck L5 gate.b16.2.totfrac 0.875000
# ...and the three deleted sites are gone from B-17.2's counter too, because
# one scan number feeds both gates. gate.b17.2.patterns reads 21 on L5 against
# the golden's 24, and B-17.2's own score does not move: the counter is
# reported and the arrival half keys on the `reftest` buckets, not on it. One
# number, two readers, one of them scoring it and one of them not -- which is
# the arrangement that lets an inherited gate publish a population without
# charging for it.
factck L5 gate.b17.2.patterns 21
factck L5 gate.b17.2.arrfrac 1.000000

# --- B-16.3: one document moved, seven literals held, five behaviours kept ---
# L6, L7, L8 and L14 are the four ways to lose the text-block leg. The third of
# them loses it without touching a line of the source shape, and the fourth
# loses it while satisfying every FILE-level reading of the gate.
#
# L6 removed the text block and did NOT put the literals back, which is the
# state a revert leaves when it reverts half of itself: gate.b16.3.textblocks is 0
# and the move fraction with it. L7 kept the text block and flattened three of
# the seven newline literals in the XML fragment builder -- the hold half, and
# the leg that says one HTML document becomes a text block and the builders do
# not. L8 has the golden's source shape exactly and two of the five recorded
# document behaviours differ, which is the multiplier: a text block whose
# content is not byte-identical to the string it replaced is a regression
# wearing the right syntax.
factck L6 gate.b16.3.moved 0
factck L6 gate.b16.3.moved.want 1
factck L6 gate.b16.3.movefrac 0.000000
factck L6 gate.b16.3.textblocks 0
factck L6 gate.b16.3.held 7
factck L6 gate.b16.3.holdfrac 1.000000
factck L6 gate.b16.3.exec 1.000000
# L14 IS L6'S TWIN ON ALL THIRTEEN COLUMNS and a different tree: the sixteen
# literals are gone from ServletTestRunner#doGet exactly as in L6, and a text
# block IS still in that file -- an unused constant at class scope, which
# SourceScan records as `in=-`. gate.b16.3.textblocks reads 1 here and 0 on
# L6, and that one fact is the whole separation.
#
# It is also the fixture that fails the scorer step 09 replaced. Keyed by file,
# the arrival half read this tree as satisfied -- move 1/1, the leg 1.000000,
# the family 1.000000 -- because a text block was somewhere in the file. Keyed
# by bucket it reads 0/1, because `...ServletTestRunner.java#-` is not
# `...#doGet`. A row that scores B16 0.711111 against the shipped scorer and
# 1.000000 against the one step 09 replaced is the proof the replacement did
# something, and it is carried forward here because the lesson is not step 09's
# alone: N9 is the same defect one rung up, in B-17.3 -- a pattern switch that
# left generateDeployment and turned up in handleArchive, one file and two
# methods, and it costs B-17 0.756010 while every file-level reading of that
# leg is satisfied.
factck L14 gate.b16.3.moved 0
factck L14 gate.b16.3.moved.want 1
factck L14 gate.b16.3.movefrac 0.000000
factck L14 gate.b16.3.textblocks 1
factck L14 gate.b16.3.held 7
factck L14 gate.b16.3.holdfrac 1.000000
factck L14 gate.b16.3.exec 1.000000
factck L7 gate.b16.3.moved 1
factck L7 gate.b16.3.movefrac 1.000000
factck L7 gate.b16.3.textblocks 1
factck L7 gate.b16.3.held 4
factck L7 gate.b16.3.held.want 7
factck L7 gate.b16.3.holdfrac 0.571429
factck L7 gate.b16.3.exec 1.000000
factck L8 gate.b16.3.moved 1
factck L8 gate.b16.3.movefrac 1.000000
factck L8 gate.b16.3.textblocks 1
factck L8 gate.b16.3.held 7
factck L8 gate.b16.3.holdfrac 1.000000
factck L8 gate.b16.3.exec.want 5
factck L8 gate.b16.3.exec.broken 2
factck L8 gate.b16.3.exec 0.600000
# ...and the five text-block ids are not the five switch ids, which is a claim
# worth more at every rung that keeps both: the two lists are the same LENGTH,
# so nothing in the arithmetic would notice if one were read for the other.
# BOTH ARE INHERITED NOW AND THE PAIRING MOVED WITH THEM. At step 10 the
# switch ids were family C's; here they are B-17.3's, and the two id lists
# this fixture separates are two HELD gates' rather than one held and one own.
# L8 breaks two of B-16.3's five and B-17.3's five recorded dispatch inputs
# read clean, which is the first of the disjointness claims this file makes
# and the only one a fixture rather than a perturbation gets to make.
factck L8 gate.b17.3.exec.want 5
factck L8 gate.b17.3.exec.broken 0
factck L8 gate.b17.3.exec 1.000000

# --- B-16.4: eleven hierarchies closed and everything else left open ---------
# The leg with two halves that pull in opposite directions, and L9, L10 and L11
# are one fixture for each half plus the one that shows the closed half
# compares TRIPLES and not names.
#
# L9 sealed six of the eleven event roots. L10 sealed all eleven AND the eleven
# extension points the tree is extended through -- the reading of "make the
# hierarchies closed" that a tool applying the modifier everywhere it compiles
# would produce, and the reason the open half exists: openfrac 0.000000 takes
# the leg to zero even though closedfrac is full. L11 sealed all eleven and
# left one permitted subtype out of one clause. gate.b16.4.decl is still 11 on
# L11 and roots is 10, which is the pair of facts that separates it from L9 --
# a gate that asked only "is this type sealed" would read L11 at full.
factck L9 gate.b16.4.decl 6
factck L9 gate.b16.4.roots 6
factck L9 gate.b16.4.roots.want 11
factck L9 gate.b16.4.permits 13
factck L9 gate.b16.4.closedfrac 0.545455
factck L9 gate.b16.4.open 11
factck L9 gate.b16.4.openfrac 1.000000
factck L10 gate.b16.4.decl 22
factck L10 gate.b16.4.roots 11
factck L10 gate.b16.4.permits 57
factck L10 gate.b16.4.closedfrac 1.000000
factck L10 gate.b16.4.open 0
factck L10 gate.b16.4.open.want 11
factck L10 gate.b16.4.openfrac 0.000000
factck L11 gate.b16.4.decl 11
factck L11 gate.b16.4.roots 10
factck L11 gate.b16.4.permits 34
factck L11 gate.b16.4.closedfrac 0.909091
factck L11 gate.b16.4.open 11
factck L11 gate.b16.4.openfrac 1.000000

# --- B-16.5: two switches, and the pair that scores the same for two reasons -
# L12 and L13 are the collision the thirteen-column table was widened for. Both
# read B-16.5 0.500000 and both read B16 0.955556, and they are not the same
# tree: L12 reverted DeployableTestBuilder's switch to colon form, so there is
# one of step 09's two arrows left and one colon; L13 left
# DynamicServiceLoaderTest's colon switch exactly where it was and added a NEW
# arrow switch beside it, so both of step 09's arrows are there and a colon as
# well. Two rungs on, and the pair still scores identically and still is not
# the same tree.
#
# gate.b16.5.arrow and gate.b16.5.size are what tell them apart -- 5 and 6
# against 6 and 7 -- and the leg reads both because it asks two questions per
# file: enough arrows AND no colon left. A per-file arrow count on its own
# would read L13 as a converted file, which is how a tree gets credit for a
# switch it added rather than one it migrated.
#
# THE RAW COUNTERS MOVED ONE RUNG AGO AND HAVE STAYED MOVED. At step 09 these
# two rows read arrow 1 and 2 against size 2 and 3; since step 10 they read 5
# and 6 against 6 and 7, because that rung's four pattern switches are
# arrow-form and join the same population -- they are B-17.3's now and they
# are still in the tree. The FILE half is what the leg scores -- one file of
# two on both rows -- and it has not moved across either rung, which is the
# assertion that B-16.5 counts per file and not per switch. A leg that had
# scored the raw arrow count would have paid step 10's candidate for step 09's
# leg four times over, and would go on paying step 11's for it.
factck L12 gate.b16.5.size 6
factck L12 gate.b16.5.arrow 5
factck L12 gate.b16.5.colon 1
factck L12 gate.b16.5.files 1
factck L12 gate.b16.5.files.want 2
factck L13 gate.b16.5.size 7
factck L13 gate.b16.5.arrow 6
factck L13 gate.b16.5.colon 1
factck L13 gate.b16.5.files 1
factck L13 gate.b16.5.files.want 2

# --- B-17.2: the test that goes, the pattern that arrives, the fourteen stay -
# Step 10's family C, republished whole as this rung's B-17, and its first leg
# still takes three halves under a min(). The four rows come in two shapes. N1
# and N2 are THE SAME TWO NUMBERS FROM OPPOSITE SIDES -- B-17.2 0.769231 and
# B17 0.959135 on both -- and they are the pair that says departure and arrival
# are measured separately. N3 and N4 take the hold half down by four and then
# by all fourteen.
#
# N1 left the three single reflective tests standing BESIDE the patterns that
# replace them: DeploymentGenerator#buildTestableDeployments,
# OSGiDeploymentPackager#generateDeployment and
# ResourceInjectionEnricher#isPrimitiveNull each test the type twice, once with
# isAssignableFrom and once with a pattern. The arrival half is full and the
# departure half is 10/13. N2 removed the three patterns and left the tests
# gone: the departure half is full -- departure is measured by what is no
# longer in the report -- and the arrival half is 10/13. gate.b17.2.depfrac and
# gate.b17.2.arrfrac are swapped between the two rows and that is the whole
# difference AT THIS GATE. A leg that scored departures alone would pay N2 in
# full for a tree that reads the type some third way; a leg that scored
# arrivals alone would pay N1 in full for a tree that still runs
# isAssignableFrom on every call.
#
# gate.b17.2.reftests IS THE THIRD CHANNEL AND IT SEPARATES THEM AGAIN. N1
# reads 25 -- the golden's 22 plus the three that stayed -- and N2 reads the
# golden's own 22. It is reported and not scored, and this pair is why: the
# counter moves on one of two trees that score the same at B-17.
factck N1 gate.b17.2.departed 10
factck N1 gate.b17.2.departed.want 13
factck N1 gate.b17.2.depfrac 0.769231
factck N1 gate.b17.2.arrived 13
factck N1 gate.b17.2.arrfrac 1.000000
factck N1 gate.b17.2.held 14
factck N1 gate.b17.2.holdfrac 1.000000
factck N1 gate.b17.2.reftests 25
factck N1 gate.b17.2.patterns 24
factck N2 gate.b17.2.departed 13
factck N2 gate.b17.2.depfrac 1.000000
factck N2 gate.b17.2.arrived 10
factck N2 gate.b17.2.arrived.want 13
factck N2 gate.b17.2.arrfrac 0.769231
factck N2 gate.b17.2.held 14
factck N2 gate.b17.2.holdfrac 1.000000
factck N2 gate.b17.2.reftests 22
factck N2 gate.b17.2.patterns 21
# ...AND AT THIS RUNG N2 MOVES A SECOND GATE, WHICH IT DID NOT MOVE AT THE ONE
# BELOW. The three patterns it deleted are three of B-16.2's fourteen convert
# buckets, because ipattern.txt was re-cut on THIS rung's golden and those
# three patterns arrived with step 10's family C: a population cut before step
# 10 ran could not name them and a population cut after it does. Convert reads
# 14 of 17, the total-site term reads 21 of 24, and B-16.2 takes the min --
# 0.823529, which carries B-16 to 0.960784. One fixture, two gates, and they
# disagree about how much it cost.
#
# THE MIN IS THE CONVERT HALF AND NOT THE TOTAL, and the reason generalises: a
# deleted convert site costs 1/17 on the convert channel and 1/24 on the total
# channel, and 1/17 is the larger loss, so inside the population convert
# always dominates. The total-site term is there for the other shape, a tree
# that deletes instanceof sites no conv bucket names -- and one rung ago that
# was the ONLY channel that could have seen N2 at all, since the three buckets
# were then outside step 09's population. It saw nothing: step 10's golden
# scanned 31 sites against a want of 28, and N2 spent exactly the three sites
# of slack. This rung has none, which is the whole difference between
# gate.b16.2.sites reading 28 there and 21 here.
factck N1 gate.b16.2.sites 24
factck N1 gate.b16.2.converted 17
factck N1 gate.b16.2.convfrac 1.000000
factck N2 gate.b16.2.sites 21
factck N2 gate.b16.2.sites.want 24
factck N2 gate.b16.2.totfrac 0.875000
factck N2 gate.b16.2.converted 14
factck N2 gate.b16.2.converted.want 17
factck N2 gate.b16.2.convfrac 0.823529
factck N2 gate.b16.2.held 7
factck N2 gate.b16.2.holdfrac 1.000000
#
# N3 converted ManagerImpl's four reflective tests as well -- getContext,
# getExtension, resolveObservers and resolveInterceptorObservers, which test a
# Class VALUE against a type the caller supplies and cannot be an `instanceof`
# pattern at all. The hold half is 10/14 and the other two are full, which is
# the fixture that makes B-17.2 a min() of three rather than of two: a sweep
# that converted every isAssignableFrom it could find has written a tree that
# does not compile, and both halves that measure the WORK are full on it.
#
# N4 deleted the whole population, the thirteen that had to leave and the
# fourteen that had to stay. Departure full, arrival full, hold 0/14, B-17.2
# 0.000000 and B17 0.822917. DELETION SCORES AS BADLY AS LEAVING IT, which is
# the rule every rung since 07 has kept and which this rung now enforces from
# the held half of the report rather than from its own.
# gate.b17.2.reftests reads 8 against the golden's 22, and the eight that are
# left are the ones no baseline row names.
factck N3 gate.b17.2.departed 13
factck N3 gate.b17.2.depfrac 1.000000
factck N3 gate.b17.2.arrived 13
factck N3 gate.b17.2.arrfrac 1.000000
factck N3 gate.b17.2.held 10
factck N3 gate.b17.2.held.want 14
factck N3 gate.b17.2.holdfrac 0.714286
factck N3 gate.b17.2.reftests 18
factck N4 gate.b17.2.departed 13
factck N4 gate.b17.2.depfrac 1.000000
factck N4 gate.b17.2.arrived 13
factck N4 gate.b17.2.arrfrac 1.000000
factck N4 gate.b17.2.held 0
factck N4 gate.b17.2.held.want 14
factck N4 gate.b17.2.holdfrac 0.000000
factck N4 gate.b17.2.reftests 8

# --- B-17.3: ten arms into four switches, two chains that stay chains --------
# Six rows, and four of them move a second leg on purpose. B-17.3 is a min() of
# a DEPARTURE half, an arrival half and a hold half, scaled by the five
# recorded dispatch inputs, and the three are counted in two different units:
# ARMS for the departure and the arrival, CHAINS for the hold.
#
# THE DEPARTURE HALF IS STEP 10'S FIRST REVIEW'S AND N14 IS ITS ROW. The
# arrival half asks only that a covering pattern switch be present in the
# method, so until the departure half existed a switch planted beside a chain
# that never left collected the whole of that rung's 0.65. The departure half
# counts the same `reftest` records B-17.2's departure counts, in the four
# buckets baseline/chain.txt calls conv, over the same ten arms.
#
# N5 gave the v_2_5 packager's switch two arms where the chain had three --
# EnterpriseArchive and WebArchive matched, JavaArchive falling to the default.
# The arrival half counts arms and the whole three-arm chain is lost rather
# than one arm of it: 7/10 = 0.700000. The departure half is untouched at
# 10/10, because the chain did leave -- N5 arrived somewhere wrong, it did not
# fail to go. gate.b17.3.patterns reads 9 against the golden's 10, and the TYPE
# SET is the condition that failed rather than the count.
#
# N6 is the mirror and the most plausible mistake at the rung this leg came
# from: one arm of the beans.xml processor's switch names `Archive` where the
# chain tested for WebArchive. The arm count does not move --
# gate.b17.3.patterns is still 10 -- and the two-arm chain is lost to the
# coverage condition, so the arrival half is 8/10 with the departure half again
# full at 10/10. IT MOVES B-17.2 AS WELL, and that is structural rather than
# sloppy: B-17.2's arrival half reads the same `swpat` type set, so the
# WebArchive arrival is gone from it too and B-17.2 is 12/13 = 0.923077. A tree
# that sends WebArchive to the wrong branch has both failed to arrive at a
# pattern for it and failed to convert the chain, and the gate charges each
# once. TWO LEGS OF ONE HELD GATE, which is why N6's B17 is 0.850962 and not
# either leg's own figure.
factck N5 gate.b17.3.arrived 7
factck N5 gate.b17.3.arrived.want 10
factck N5 gate.b17.3.arrfrac 0.700000
factck N5 gate.b17.3.departed 10
factck N5 gate.b17.3.departed.want 10
factck N5 gate.b17.3.depfrac 1.000000
factck N5 gate.b17.3.held 2
factck N5 gate.b17.3.holdfrac 1.000000
factck N5 gate.b17.3.patterns 9
factck N5 gate.b17.3.switches 4
factck N5 gate.b17.3.exec 1.000000
factck N5 gate.b17.2.swpatterns 9
factck N5 gate.b17.2.arrfrac 1.000000
factck N6 gate.b17.3.arrived 8
factck N6 gate.b17.3.arrfrac 0.800000
factck N6 gate.b17.3.departed 10
factck N6 gate.b17.3.depfrac 1.000000
factck N6 gate.b17.3.held 2
factck N6 gate.b17.3.holdfrac 1.000000
factck N6 gate.b17.3.patterns 10
factck N6 gate.b17.3.switches 4
factck N6 gate.b17.2.arrived 12
factck N6 gate.b17.2.arrived.want 13
factck N6 gate.b17.2.arrfrac 0.923077
factck N6 gate.b17.2.departed 13
factck N6 gate.b17.2.depfrac 1.000000
#
# N7 and N8 FAIL IN TWO DIFFERENT HALVES OF THE SAME LEG, which is what the
# pair is for; they do not meet on a figure, and the five-id swids.txt is what
# parted them. N7 planted a pattern switch in the annotation scenario
# generator's `validate` dispatch, where the baseline says the chain must stay
# a chain because its arms are not disjoint and its order is the semantics:
# the hold half is 1/2, B-17.3 is 0.500000 and B17 0.661458. N8 has the
# golden's source shape exactly -- all three static halves 1.000000 -- and two
# of the five recorded dispatch inputs behave differently: the scaling is 3/5,
# B-17.3 is 0.600000 and B17 0.729167. A BROKEN EXECUTION AND A MISSING
# CONVERSION ARE DIFFERENT FAILURES, gate.b17.3.holdfrac against gate.b17.3.exec
# is where the difference is written, and both rows assert both facts now that
# the totals happen to tell them apart as well.
#
# THE HOLD HALF COUNTS CHAINS AND NOT ARMS, which N7 is the reason for: a
# per-arm denominator would have paid the candidate for converting half of a
# dispatch that should not have been touched at all. N7's planted switch also
# lands in B-17.2's `swpat` population, so gate.b17.2.swpatterns reads 12 --
# reported and not scored, because the two types it names were already
# arriving.
#
# JEP 441 THROWS ON NULL WHERE AN IF-CHAIN FALLS THROUGH, which is what N8's
# five ids are recorded against -- FIVE RECORDINGS OVER FOUR CHAINS, three at
# the two enricher dispatches and two at the two packagers, which is why the
# id count and the arm count are different numbers and neither is four. This
# tree answers that input three different ways across the four chains, so no
# static rule is right at all four sites and the transcripts are the only
# reading of those inputs that is not an author's opinion.
factck N7 gate.b17.3.departed 10
factck N7 gate.b17.3.depfrac 1.000000
factck N7 gate.b17.3.arrived 10
factck N7 gate.b17.3.arrfrac 1.000000
factck N7 gate.b17.3.held 1
factck N7 gate.b17.3.held.want 2
factck N7 gate.b17.3.holdfrac 0.500000
factck N7 gate.b17.3.patterns 12
factck N7 gate.b17.3.switches 5
factck N7 gate.b17.3.exec 1.000000
factck N7 gate.b17.2.swpatterns 12
factck N8 gate.b17.3.departed 10
factck N8 gate.b17.3.depfrac 1.000000
factck N8 gate.b17.3.arrived 10
factck N8 gate.b17.3.arrfrac 1.000000
factck N8 gate.b17.3.held 2
factck N8 gate.b17.3.holdfrac 1.000000
factck N8 gate.b17.3.patterns 10
factck N8 gate.b17.3.switches 4
factck N8 gate.b17.3.exec.want 5
factck N8 gate.b17.3.exec.broken 2
factck N8 gate.b17.3.exec 0.600000
# ...and N8's two broken ids are not on any of the other four lists. The five
# populations share one behavior.summary and this is the fixture-side half of
# that claim; the perturbation section below is the other half. B-16.3's own
# five ids are the pairing that moved with the family: BOTH ARE INHERITED NOW,
# and a run that broke one list and not the other still says which.
factck N8 gate.b16.3.exec.broken 0
factck N8 gate.b16.3.exec 1.000000
factck N8 gate.b15.exec.broken 0
factck N8 gate.b14.c5.exec.broken 0
factck N8 gate.c4.exec.broken 0
factck N8 gate.c4.exec 1.000000
#
# N9 IS L14'S LESSON ONE GATE OVER. The v_3 packager's chain is gone and a
# pattern switch on the same three types sits in the same FILE -- in
# `handleArchive`, not in `generateDeployment` where the chain was. The conv
# half keys on file#method, so the three-arm chain scores nothing: 7/10, the
# same conv half as N5. gate.b17.3.switches is still 4 and gate.b17.3.patterns
# still 10, which is the pair that separates the two rows, and B-17.2's arrival
# half falls with it because the three arrivals it counted in that method left
# with the switch. Every arrival half in this gate is method-scoped, and L14
# and N9 are the two rows that say why -- N9 costs B17 0.756010 across the two
# legs, which is more than either leg alone would have charged.
factck N9 gate.b17.3.arrived 7
factck N9 gate.b17.3.arrived.want 10
factck N9 gate.b17.3.arrfrac 0.700000
factck N9 gate.b17.3.departed 10
factck N9 gate.b17.3.departed.want 10
factck N9 gate.b17.3.depfrac 1.000000
factck N9 gate.b17.3.held 2
factck N9 gate.b17.3.holdfrac 1.000000
factck N9 gate.b17.3.patterns 10
factck N9 gate.b17.3.switches 4
factck N9 gate.b17.3.exec 1.000000
factck N9 gate.b17.2.arrived 10
factck N9 gate.b17.2.arrfrac 0.769231
factck N9 gate.b17.2.departed 13
factck N9 gate.b17.2.depfrac 1.000000
#
# N14 WAS STEP 10'S FIRST REVIEW'S BLOCKER AND IT IS THE ROW THAT LEG DID NOT
# HAVE. The four pattern switches are the golden's, arm for arm:
# gate.b17.3.switches 4, gate.b17.3.patterns 10, gate.b17.3.nullcase 1, and
# gate.b17.3.arrived 10 out of 10. Nothing was CONVERTED. The reflective chains
# are back in all four conv buckets at the lines the input tree had them on,
# standing beside the switches that replaced none of them, so
# gate.b17.3.departed reads 0 and the min() takes that half: B-17.3 is 0.000000
# on a tree whose every other B-17.3 fact is the golden's.
#
# THE SCALING CANNOT REACH THIS TREE, WHICH IS WHY THE HALF HAD TO EXIST. The
# chain is still what runs, so all five recorded dispatch inputs still answer
# as the baseline says and gate.b17.3.exec reads 1.000000. A leg shaped
# min(arrival, hold) x exec paid this tree the whole of step 10's 0.65; that
# rung's review priced the same tree at reward 0.837 with a class-file level
# flip and 0.970 on top of OpenRewrite, and N14 is the fixture that said
# neither price was available any more. It is republished here so that a step
# 11 candidate cannot buy those prices back by undoing the rung below while it
# works on this one.
#
# B-17.2 FALLS WITH IT AND IT FALLS THE SAME WAY, which is the second thing the
# row is for: the ten restored records are ten of B-17.2's thirteen departures,
# so gate.b17.2.depfrac reads 0.230769 off the three standalone sites N14 did
# not touch, while gate.b17.2.arrfrac stays 1.000000. That pair is N1's lesson
# on a different population, and the two rows are each other's check -- N1
# restores the three standalone tests and not the chains, N14 the chains and
# not the three. A gate that read one population for both would not be able to
# give both rows their B-17.2. Together the two legs put N14 at B17 0.186699,
# the lowest figure any fixture in this rehearsal puts on a held gate.
factck N14 gate.b17.3.departed 0
factck N14 gate.b17.3.departed.want 10
factck N14 gate.b17.3.depfrac 0.000000
factck N14 gate.b17.3.arrived 10
factck N14 gate.b17.3.arrived.want 10
factck N14 gate.b17.3.arrfrac 1.000000
factck N14 gate.b17.3.held 2
factck N14 gate.b17.3.held.want 2
factck N14 gate.b17.3.holdfrac 1.000000
factck N14 gate.b17.3.ipleft 0
factck N14 gate.b17.3.patterns 10
factck N14 gate.b17.3.switches 4
factck N14 gate.b17.3.nullcase 1
factck N14 gate.b17.3.exec.want 5
factck N14 gate.b17.3.exec.broken 0
factck N14 gate.b17.3.exec 1.000000
factck N14 gate.b17.2.departed 3
factck N14 gate.b17.2.departed.want 13
factck N14 gate.b17.2.depfrac 0.230769
factck N14 gate.b17.2.arrived 13
factck N14 gate.b17.2.arrived.want 13
factck N14 gate.b17.2.arrfrac 1.000000
factck N14 gate.b17.2.held 14
factck N14 gate.b17.2.holdfrac 1.000000
factck N14 gate.b17.2.reftests 32

# --- B-17.3 again: the chain that stopped being reflective and stayed a chain
# N15 is N14 after step 10's requirement 4 has been run over the same tree, and
# it is the row that says the departure half counts a SPELLING and not a record
# kind. Not one of the ten `Foo.class.isInstance(x)` arms is left --
# gate.b17.2.reftests is back at the golden's 22 -- and every one of them is
# now `x instanceof Foo f` on the line the reflective call was on, inside the
# same four conv methods, with the golden's four switches, ten arms and one
# `case null` still standing beside them untouched. The chain is still what
# dispatches.
#
# gate.b17.3.ipleft IS THE FACT THIS ROW EXISTS FOR and it is why it was
# published rather than only summed: it reads 10 here and 0 on the golden, on
# N14 and on the input tree, so a reader of a failed run can tell `the
# reflective call is still there` (N14: ipleft 0, reftests 32) from `it became
# an instanceof and stayed` (N15: ipleft 10, reftests 22) without looking at
# the tree. The residual is 0 surviving reftests plus 10 surviving names
# against 10 arms, so gate.b17.3.departed is 0 and the min() takes that half
# again: B-17.3 0.000000 with gate.b17.3.arrived, gate.b17.3.held,
# gate.b17.3.nullcase and gate.b17.3.exec all the golden's, exactly as on N14.
#
# B-17.2 IS FULL HERE AND THAT IS THE ASSERTION, not an oversight. The ten arms
# departed as B-17.2 asks and arrived as B-17.2 asks -- an `ipattern` naming
# the type is what its arrival accepts -- so gate.b17.2.depfrac and
# gate.b17.2.arrfrac are both 1.000000 and B-17.2 is 1.000000 against N14's
# 0.230769 on the same four switches. gate.b17.2.patterns reads 34 against the
# golden's 24: the ten extra patterns ARE the ten chain arms, and that fact
# plus gate.b17.3.ipleft is the whole difference between the two rows. B17
# 0.322917 is 1 - 0.65/0.96 -- every leg of the gate but the one that matters,
# which is the point -- and familyC is a clean 1.000000, because this rung's
# own four legs never looked at any of it.
#
# AND B-16.2 MUST NOT MOVE, which the B16 column asserts. Ten `ipattern`
# records were added and B-16.2's conv and hold halves are capsum()s over THIS
# rung's own ipattern buckets; none of these four chain methods is one of them,
# so neither half can see them. Its third term is the total-site fraction
# against 24, exactly met at the golden's 24 and still capped at 1.000000 on
# 34. A scorer that had summed ipattern sites anywhere would move B16 here.
#
# AND THE CAP IS DOING REAL WORK AT THIS RUNG, which it was not doing at the
# one below. There the golden scanned 31 against a want of 28 and the term had
# three sites of headroom before the cap was even reached; here the golden sits
# exactly on 24, so N15's 34 is the first fixture in the ladder whose
# total-site term is held down by min() alone rather than by slack. The two
# facts below are the same counter read by two gates -- 34 at gate.b16.2.sites
# and 34 at gate.b17.2.patterns -- and neither scores it above its cap.
factck N15 gate.b17.3.departed 0
factck N15 gate.b17.3.departed.want 10
factck N15 gate.b17.3.depfrac 0.000000
factck N15 gate.b17.3.ipleft 10
factck N15 gate.b17.3.arrived 10
factck N15 gate.b17.3.arrived.want 10
factck N15 gate.b17.3.arrfrac 1.000000
factck N15 gate.b17.3.held 2
factck N15 gate.b17.3.held.want 2
factck N15 gate.b17.3.holdfrac 1.000000
factck N15 gate.b17.3.patterns 10
factck N15 gate.b17.3.switches 4
factck N15 gate.b17.3.nullcase 1
factck N15 gate.b17.3.exec.want 5
factck N15 gate.b17.3.exec.broken 0
factck N15 gate.b17.3.exec 1.000000
factck N15 gate.b17.2.departed 13
factck N15 gate.b17.2.departed.want 13
factck N15 gate.b17.2.depfrac 1.000000
factck N15 gate.b17.2.arrived 13
factck N15 gate.b17.2.arrived.want 13
factck N15 gate.b17.2.arrfrac 1.000000
factck N15 gate.b17.2.held 14
factck N15 gate.b17.2.holdfrac 1.000000
factck N15 gate.b17.2.reftests 22
factck N15 gate.b17.2.patterns 34
factck N15 gate.b16.2.sites 34
factck N15 gate.b16.2.sites.want 24
factck N15 gate.b16.2.totfrac 1.000000
factck N15 gate.b16.2.converted 17
factck N15 gate.b16.2.convfrac 1.000000
factck N15 gate.b16.2.held 7
factck N15 gate.b16.2.holdfrac 1.000000

# --- B-17.4: seven heads that move, nine that must not -----------------------
# B-17.2's shape again on a different population, and N10 and N11 are this
# gate's version of N1 and N2: the same two figures -- B-17.4 0.714286 and B17
# 0.958333 -- with gate.b17.4.depfrac and gate.b17.4.arrfrac swapped.
#
# N10 left two of the seven guarded first-element reads as index arithmetic
# AND put the getFirst() that replaces them in beside it, so
# ContainerRegistry#findDefaultContainer and ProtocolRegistry#findDefaultProtocol
# each read the head twice. N11 has the same two reads departed and no
# sequenced access arrived: the head is fetched some third way, or the method
# no longer fetches it.
#
# THE TWO DENOMINATORS ARE COMPUTED SEPARATELY and are both 7 only because
# every convertible bucket holds exactly one site. Departures count SITES and
# arrivals count BUCKETS, and the shell derives each from its own population
# rather than reusing the other -- which is invisible at this rung and would
# not be at a rung where one method read the head twice on purpose.
#
# THE REPORTED COUNTERS ARE WHAT TELL THE PAIR APART, and they move on
# opposite rows: N10 carries gate.b17.4.firstreads up to 21 with
# gate.b17.4.sequenced still at the golden's 7, N11 leaves firstreads at 19 and
# takes sequenced and sequenced.read down to 5. Two trees, one score, four
# counters, and no two of them agree.
factck N10 gate.b17.4.departed 5
factck N10 gate.b17.4.departed.want 7
factck N10 gate.b17.4.depfrac 0.714286
factck N10 gate.b17.4.arrived 7
factck N10 gate.b17.4.arrfrac 1.000000
factck N10 gate.b17.4.held 9
factck N10 gate.b17.4.holdfrac 1.000000
factck N10 gate.b17.4.firstreads 21
factck N10 gate.b17.4.sequenced 7
factck N10 gate.b17.4.sequenced.read 7
factck N11 gate.b17.4.departed 7
factck N11 gate.b17.4.depfrac 1.000000
factck N11 gate.b17.4.arrived 5
factck N11 gate.b17.4.arrived.want 7
factck N11 gate.b17.4.arrfrac 0.714286
factck N11 gate.b17.4.held 9
factck N11 gate.b17.4.holdfrac 1.000000
factck N11 gate.b17.4.firstreads 19
factck N11 gate.b17.4.sequenced 5
factck N11 gate.b17.4.sequenced.read 5
#
# N12 is the hold half: three of the nine reads that must not move are gone --
# DynamicServiceLoader#onlyOne, which reads the head of a list it has just
# asserted has exactly one element, and EventTestRunnerAdaptor#test, which
# reads a queue it is draining. Those nine read index 0 of something that is
# not a List, or read it under a guard that makes the sequenced form a
# different program, and the hold half is what stops a blanket sweep scoring.
#
# N13 IS THE ROW THE COUNTER EXISTS TO BE WRONG ABOUT. The sequenced access
# arrived and reads the WRONG END: findDefaultContainer calls getLast() where
# the code it replaced read index 0. The arrival half keys on `op=getFirst` and
# is 6/7; gate.b17.4.sequenced and gate.b17.4.sequenced.read BOTH STILL READ 7,
# because getLast is a read like any other and those counters count reads. They
# are reported and not scored, and a leg that had scored either would have paid
# this tree in full for a method that now returns a different container.
#
# NONE OF THE FOUR TOUCHES THIS RUNG'S C-4, which the familyC column asserts on
# every one of them. Step 11's own C-4 converts `setAccessible` to VarHandle
# and counts `varhandle` records; B-17.4 converts index-0 reads to getFirst()
# and counts `seqfirst` records. The two legs share a JEP-era heading and
# nothing else, and four rows that move one while leaving the other at
# 1.000000 are how that is stated rather than asserted.
factck N12 gate.b17.4.departed 7
factck N12 gate.b17.4.depfrac 1.000000
factck N12 gate.b17.4.arrived 7
factck N12 gate.b17.4.arrfrac 1.000000
factck N12 gate.b17.4.held 6
factck N12 gate.b17.4.held.want 9
factck N12 gate.b17.4.holdfrac 0.666667
factck N12 gate.b17.4.firstreads 16
factck N13 gate.b17.4.departed 7
factck N13 gate.b17.4.depfrac 1.000000
factck N13 gate.b17.4.arrived 6
factck N13 gate.b17.4.arrived.want 7
factck N13 gate.b17.4.arrfrac 0.857143
factck N13 gate.b17.4.held 9
factck N13 gate.b17.4.holdfrac 1.000000
factck N13 gate.b17.4.firstreads 19
factck N13 gate.b17.4.sequenced 7
factck N13 gate.b17.4.sequenced.read 7
factck N10 gate.c4.setaccessible 10
factck N10 gate.c4.handles 8
factck N11 gate.c4.setaccessible 10
factck N11 gate.c4.handles 8
factck N12 gate.c4.setaccessible 10
factck N12 gate.c4.handles 8
factck N13 gate.c4.setaccessible 10
factck N13 gate.c4.handles 8

# --- B-15.2: the three ways to lose the module names -------------------------
# K1, K2 and K20 all lose module names and they lose them differently. K1
# publishes twelve jars and forgets one manifest attribute: eleven names, and
# B-15.2 is the SQUARE of 11/12 -- 0.840278 and not 0.916667. The square is
# where step 08's C-1 went. That rung scaled its name leg by a separate
# readability leg and this rung does not read readability at all, so the
# scaling survives as the square and the twelfth name still costs more than the
# first eleven. K2 publishes the same twelve jars with no attribute at all,
# which is the state step 08's INPUT tree was in. K20 published nothing.
#
# THE THREE ARE NOT INTERCHANGEABLE and the facts say so: mod.total is 12, 12
# and 0. A gate that divided by mod.total instead of by the baseline's twelve
# would read K20's name leg as 0/0 and, depending on which way frac() fell, as
# either 0 or 1 -- and 1 would hand 0.35/0.80 of B-15 to a build that published
# nothing.
factck K1 gate.b15.mod.total 12
factck K1 gate.b15.mod.declared 11
factck K1 gate.b15.mod.derivable 11
factck K1 gate.b15.names 11
factck K2 gate.b15.mod.total 12
factck K2 gate.b15.mod.declared 0
factck K2 gate.b15.mod.derivable 11
factck K2 gate.b15.names 0
factck K20 gate.b15.mod.total 0
factck K20 gate.b15.mod.declared 0
factck K20 gate.b15.mod.derivable 0
factck K20 gate.b15.names 0

# K3 is the fixture the name leg exists for. Twelve jars, twelve manifest
# attributes, every one of them legal -- and two of the names were derived from
# the file name rather than from the packages the sources are in:
# arquillian.impl.base for a project whose sources are all under
# org.jboss.arquillian.impl, arquillian.protocol.jmx.osgi for one whose sources
# are all under org.jboss.arquillian.packager.osgi. That is the reading an
# agent who never opened the sources produces, it is the reading that would be
# wrong FOREVER once published, and the only thing in this verifier that can
# see it is the pairwise comm against baseline/modnames.txt.
#
# mod.declared is 12 AND names is 10, and the gap between those two numbers is
# the whole assertion. B-15.2 is the square of 10/12 = 0.694444.
factck K3 gate.b15.mod.declared 12
factck K3 gate.b15.mod.derivable 11
factck K3 gate.b15.names 10
factck K3 B15.2.module-names 0.694444

# ...and M1 is the row that says what this rung STOPPED reading. Every name
# declared and every name correct, and one more artifact the platform will not
# read, because the candidate left a second malformed file under META-INF/
# services/. mod.derivable falls from 11 to 10 and B-15 does not move at all:
# 1.000000, the same as the golden.
#
# AT STEP 08 THIS FIXTURE SCORED 0.909091 ON THAT LEG. The readability term was
# one of three under a min() and it was the term that made an unrelated
# packaging accident cost a migration score. Step 08's requirement was about
# the module path and could defend it; step 09's B-15 is an inherited
# no-regression gate and cannot, so mod.derivable is published and unscored.
# M1 is how this rehearsal proves the term is actually gone rather than merely
# renamed -- the fixture is unchanged from step 08 and the answer moved.
factck M1 gate.b15.mod.declared 12
factck M1 gate.b15.mod.derivable 10
factck M1 gate.b15.names 12
factck M1 B15.2.module-names 1.000000

# --- B-15.3: three halves, one at a time -------------------------------------
# M2, M3 and M4 each break exactly one of them, and the min() means each one
# alone is enough. M2 puts the spi's ServiceLoader#reload back to a hand-rolled
# scan -- the departure half AND the arrival half fall together, because they
# are two readings of the same method. M3 stops FilteredURLClassLoader#loadClass
# loading by name, which is the hold half and is the leg that says "eleven
# by-name loads are NOT service lookups and must not move". M4 turns both
# platformload records into platformtype: `java.util.ServiceLoader x;` declared
# and never called, which is the shape of a migration that compiles and
# resolves nothing.
factck M2 gate.b15.departed 0.500000
factck M2 gate.b15.arrived 0.500000
factck M2 gate.b15.held 1.000000
factck M3 gate.b15.departed 1.000000
factck M3 gate.b15.arrived 1.000000
factck M3 gate.b15.held 0.909091
factck M4 gate.b15.departed 1.000000
factck M4 gate.b15.arrived 0.000000
factck M4 gate.b15.held 1.000000
# ...and M5 and M6 break none of the three and lose the leg anyway, because the
# scaling is the recorded behaviour and not the source shape. M5 is the file
# that has every bucket in the right place and resolves nothing at run time.
factck M5 gate.b15.departed 1.000000
factck M5 gate.b15.arrived 1.000000
factck M5 gate.b15.held 1.000000
factck M5 gate.b15.exec.want 69
factck M5 gate.b15.exec.broken 69
factck M5 gate.b15.exec 0.000000
factck M6 gate.b15.exec.want 69
factck M6 gate.b15.exec.broken 9
factck M6 gate.b15.exec 0.869565

# --- B-14: step 07's family C, still there -----------------------------------
# K4 through K16 each take one thing back out of step 07's work and nothing
# else, and B-14 is the only figure in the table that moves. That is the point
# of an inherited family: a regression against the previous rung's result
# costs the previous rung's gate and leaves this rung's own three legs alone.
# Every one of these rows reads familyC 1.000000, which is only meaningful
# alongside the B-14 column that does not.
#
# K4 and K5 are the namespace pair. K4 reverts a whole method and takes three
# javax.annotation references back with it; K5 leaves ONE, in
# CDIInjectionEnricher#getInstanceByType, which is the shape of a find-and-
# replace that missed a fully-qualified name in a method body. The bucket
# halves see the first and not the second; the javax.left counter sees both.
factck K4 gate.b14.c2.departed 5
factck K4 gate.b14.c2.arrived 5
factck K4 gate.b14.c2.javax.left 3
factck K4 gate.b14.c2.jakarta.refs 19
factck K4 gate.src.impjx 1
factck K4 gate.src.impjk 2
factck K5 gate.b14.c2.departed 6
factck K5 gate.b14.c2.arrived 6
factck K5 gate.b14.c2.javax.left 1
factck K5 gate.b14.c2.jakarta.refs 22
# K6 keeps every bucket occupied and drops six of the jakarta references, which
# is the tree that renamed the imports and left the bodies half-converted.
factck K6 gate.b14.c2.departed 6
factck K6 gate.b14.c2.arrived 6
factck K6 gate.b14.c2.javax.left 0
factck K6 gate.b14.c2.jakarta.refs 16
# K7 and K8 are the HOLD. javax.enterprise and javax.management are packages
# this rung explicitly does NOT move, and a candidate who renames them has
# broken a tree that compiled. Two sizes: two references and forty.
factck K7 gate.b14.c3.held 42
factck K7 gate.b14.c3.refs 110
factck K7 gate.b14.c3.unscaled 0.976744
factck K8 gate.b14.c3.held 33
factck K8 gate.b14.c3.refs 72
factck K8 gate.b14.c3.unscaled 0.642857
# K9 and K10 are B-14.4's two halves disagreeing. K9 puts every toArray site
# back to `new T[c.size()]`; K10 converts three of eight.
factck K9 gate.b14.c4.want 5
factck K9 gate.b14.c4.departed 0
factck K9 gate.b14.c4.arrived 0
factck K10 gate.b14.c4.departed 2
factck K10 gate.b14.c4.arrived 2
# K11 through K13 are B-14.5. K11 drops the followRedirects record and loses
# the quarter; K12 leaves seven legacy records standing in the one file this
# gate reads; K13 declares one arrival type where three were asked for.
factck K11 gate.b14.c5.redirects 0
factck K11 gate.b14.c5.redirects.honoured 0
factck K11 gate.b14.c5.legacy 0
factck K12 gate.b14.c5.legacy 7
factck K12 gate.b14.c5.redirects 1
factck K13 gate.b14.c5.types 1
factck K13 gate.b14.c5.legacy 0
# K14 through K16 are the penalty, which is a multiplier on B-14 and not a leg
# of it. Three spellings of the same idea and three different arithmetic: two
# raw types added is 1 - 2/10, a suppression added is a halving, and a raw-
# element toArray constructor reference is the other halving. K14 is the only
# one of the three that publishes a non-zero gate.src.raw.added.
factck K14 gate.src.raw.added 2
factck K14 gate.src.penalty 0.800000
factck K15 gate.src.raw.added 0
factck K15 gate.src.penalty 0.500000
factck K16 gate.src.raw.added 0
factck K16 gate.src.penalty 0.500000

# --- K17: the tree nobody could compile, and all three spellings of zero -----
# K17 has no report, twelve intact published jars AND an intact class-file
# scan, which is the combination this rung's guard inversion exists for. At
# step 08 the two artifact-backed legs were both family C's; here one of them
# is B-15.2's and the other is C-1's, on two channels that no longer share a
# file.
#
# READ THE FOUR SPELLINGS TOGETHER. C-1 is 1.000000 -- a real computed score on
# a tree that does not compile, because arqgate.Scan reads class files and the
# class files are there, all 739 of them and every one at major 66.
# gate.b15.departed, .arrived and .held are the shell literal `0`, assigned
# above the guard and published below it. B-15.3, B-16.3, B-17.3 and C-4 read
# `0.000000`, computed by awks that sit outside their guards out of parts that
# never ran. B-16.2, B-16.4, B-16.5, B-17.2, B-17.4, C-2 and C-3 print the
# literal `0`, and every counter fact behind those seven is MISSING, published
# only inside the per-leg baseline guards -- as is the whole of B-14 and the
# whole of the source scan's own report.
#
# WHICH SPELLING A LEG GETS IS DECIDED BY ONE THING, AND IT IS NOT THE FAMILY.
# A leg that scales by a behaviour list computes its total BELOW its guard, out
# of fractions that therefore have to be initialised above it and published
# outside it; a leg with no such scaling keeps its total and its fractions both
# inside. Five legs scale by a list at this rung -- B-14.5, B-15.3, B-16.3,
# B-17.3 and C-4 -- and B-14.5 is behind the compile guard that publishes
# nothing here, so exactly four legs print `0.000000` and the other seven
# print `0`. B-15.3 is the one of the four that is easy to miscount, because
# its three fractions are not named `gate.b15.3.*` -- they are
# gate.b15.departed, .arrived and .held, asserted six lines above, and the leg
# they feed is spelled the same way as the other three all the same.
#
# AND THE TWO SPELLINGS CHANGED HANDS BETWEEN FAMILY C's LAST TWO LEGS SINCE
# THE RUNG BELOW. There C-3 was the pattern switch, scaled by swids.txt, and
# C-4 was the sequenced accessors with no behaviour to scale by; here C-3 is
# the unnamed catch parameter, which records nothing because a binding nobody
# reads cannot be observed to behave, and C-4 is the VarHandle conversion,
# scaled by vhids.txt. The letters did not move and the weights did not move.
# The guard geometry underneath them did, so a fixture that asserted
# `0.000000` on C-3 out of habit would be asserting the rung below's scorer
# against this one's -- which is exactly the sort of drift the ladder is here
# to catch, and the reason the spellings are written out one leg at a time
# rather than described.
#
# familyC IS 0.040000 AND THAT IS EXACTLY C-1's WEIGHT. A family C whose four
# legs shared one guard would read 0 here; one that treated a missing AST as a
# missing family would read MISSING. The number is the assertion that the four
# legs are four legs on two channels. B16 AND B17 ARE BOTH 0.000000 FOR THE
# MIRROR REASON: every leg either of them has is on the source channel, so
# neither has anything left to keep.
factck K17 gate.src.ok22 no
factck K17 gate.src.okdef no
factck K17 gate.b15.mod.total 12
factck K17 gate.b15.mod.declared 12
factck K17 gate.b15.mod.derivable 11
factck K17 gate.b15.names 12
factck K17 gate.b15.departed 0
factck K17 gate.b15.arrived 0
factck K17 gate.b15.held 0
factck K17 gate.c1.classes 739
factck K17 gate.c1.at66 739
factck K17 C1.level-66 1.000000
# The shell literal `0` and not `0.000000`, which is the same distinction K17
# is carrying several other times on this row -- and it lands on all three
# scaled source legs at once, two of them inherited and one of them this
# rung's own. b16_3move, b16_3hold, b17_3dep/arr/hold and c4dep/arr/hold are
# initialised above their guards and published below them, so an uncompilable
# tree publishes the initialiser; each of the three totals is `0.000000`
# because it is assigned below the guard by the awk that multiplies the min by
# the execution scaling. Three legs, two spellings each, and which one a fact
# gets is decided by which side of the guard its assignment sits on -- exactly
# as for gate.b15.departed/arrived/held, which is the fourth leg of the same
# shape wearing different counter names.
factck K17 gate.b16.3.movefrac 0
factck K17 gate.b16.3.holdfrac 0
factck K17 gate.b17.3.depfrac 0
factck K17 gate.b17.3.arrfrac 0
factck K17 gate.b17.3.holdfrac 0
factck K17 gate.c4.depfrac 0
factck K17 gate.c4.arrfrac 0
factck K17 gate.c4.holdfrac 0
# ...and the scaling is not the thing that fell: K17's behavior.summary is the
# golden's and reads clean on all four lists a tree at this rung can be scaled
# by. B-15.3's proviso, B-16.3's, B-17.3's and C-4's are each 1.000000 on a
# tree whose source shape none of them could measure, which is the guard doing
# its job rather than the proviso doing it. Note the four populations are four
# different sizes -- 69, 5, 5 and 4 -- so a fixture that published one exec
# figure for all of them would be caught here rather than in the arithmetic.
factck K17 gate.b15.exec.want 69
factck K17 gate.b15.exec.broken 0
factck K17 gate.b15.exec 1.000000
factck K17 gate.b16.3.exec.want 5
factck K17 gate.b16.3.exec.broken 0
factck K17 gate.b16.3.exec 1.000000
factck K17 gate.b17.3.exec.want 5
factck K17 gate.b17.3.exec.broken 0
factck K17 gate.b17.3.exec 1.000000
factck K17 gate.c4.exec.want 4
factck K17 gate.c4.exec.broken 0
factck K17 gate.c4.exec 1.000000
# ...and the eleven leg totals, spelled out one at a time, because this is the
# claim above turned into assertions. `0.000000` is what a leg assigned OUTSIDE
# its guard prints; `0` is what a leg assigned INSIDE one prints, because the
# initialiser is all that survives. Nothing but the position of an assignment
# decides which, and a scorer that moved one of these four composites inside
# its guard -- a tidying edit that changes no arithmetic on any tree that
# compiles -- would change nothing anywhere else in this file.
factck K17 B15.3.platform-lookup   0.000000
factck K17 B16.3.text-blocks       0.000000
factck K17 B17.3.pattern-switch    0.000000
factck K17 C4.varhandle            0.000000
factck K17 B16.2.pattern-instanceof 0
factck K17 B16.4.sealed-hierarchies 0
factck K17 B16.5.arrow-switch       0
factck K17 B17.2.type-patterns      0
factck K17 B17.4.sequenced-access   0
factck K17 C2.doprivileged-removal  0
factck K17 C3.unnamed-catch         0
# The MISSING list, which is the spelling a factck on a number cannot express.
# Four of B-16's legs, all three of B-17's, three of family C's four, and the
# whole of B-14 and of the source scan's own report. gate.src.ok21 is on it for
# a reason of its own and not for the guard's: the control compile only runs
# when the measurement compile succeeded, so a tree that does not compile at 22
# is never asked about 21 at all -- the absence IS the answer, and it is not
# the same answer as `no`.
for k in gate.b16.2.sites gate.b16.2.sites.want gate.b16.2.converted \
         gate.b16.2.held gate.b16.3.moved gate.b16.3.held \
         gate.b16.3.textblocks gate.b16.4.decl gate.b16.4.roots \
         gate.b16.4.open gate.b16.4.permits gate.b16.5.size \
         gate.b16.5.arrow gate.b16.5.colon gate.b16.5.files \
         gate.b17.2.departed gate.b17.2.arrived gate.b17.2.held \
         gate.b17.2.depfrac gate.b17.2.arrfrac gate.b17.2.holdfrac \
         gate.b17.2.reftests gate.b17.2.patterns gate.b17.2.swpatterns \
         gate.b17.3.departed gate.b17.3.arrived gate.b17.3.held \
         gate.b17.3.ipleft gate.b17.3.patterns gate.b17.3.switches \
         gate.b17.3.nullcase gate.b17.4.departed gate.b17.4.arrived \
         gate.b17.4.held gate.b17.4.depfrac gate.b17.4.arrfrac \
         gate.b17.4.holdfrac gate.b17.4.firstreads gate.b17.4.sequenced \
         gate.b17.4.sequenced.read \
         gate.c2.departed gate.c2.held gate.c2.held.ctor \
         gate.c2.held.setacc gate.c2.depfrac gate.c2.holdfrac gate.c2.sites \
         gate.c3.departed gate.c3.arrived gate.c3.held gate.c3.depfrac \
         gate.c3.arrfrac gate.c3.holdfrac gate.c3.unnamed gate.c3.unread \
         gate.c3.read gate.c4.departed gate.c4.arrived gate.c4.held \
         gate.c4.handles gate.c4.setaccessible \
         gate.b14.c2.want gate.b14.c3.want gate.b14.c4.want \
         gate.b14.c5.exec.want gate.b14.c5.redirects gate.src.ok21 \
         gate.src.penalty gate.src.impjx B14.2.jakarta B14.5.httpclient; do
   factck K17 "${k}" MISSING
done

# --- K18 and K19: the two held gates that are not B-14 -----------------------
# Both are here because they move a column no other fixture moves, which is how
# this rehearsal knows the thirteen columns are thirteen channels. K18 puts two
# reflection buckets back to Class.newInstance and costs B-13 alone; K19 is the
# build that dropped a module, and it is the one fixture where B-12's four AST
# legs are full and its constant-pool leg is empty.
#
# K19 IS THE FIXTURE THAT SEPARATES THE TWO CHANNELS THAT SHARE A FILE. Its
# manifests are untouched, so B-15.2 reads twelve declared names off exactly
# the jars B-12e finds nothing in. One jar set, two readings, and only one of
# them falls.
factck K18 gate.b15.mod.declared 12
factck K18 gate.b15.names 12
factck K19 gate.cp.jars 12
factck K19 gate.cp.classes 24
factck K19 gate.cp.optional 0
factck K19 gate.cp.collectors 0
factck K19 gate.cp.objects 0
factck K19 gate.cp.jtime 0
factck K19 gate.b15.mod.declared 12
factck K19 gate.b15.names 12
# ...and the golden's own constant pools, so that K19's zeros are a difference
# and not a tool that never worked. The four floors come out of
# baseline/target.txt and the jars were written to meet them exactly.
factck golden gate.cp.jars 12
factck golden gate.cp.classes 24
factck golden gate.cp.optional 5
factck golden gate.cp.collectors 6
factck golden gate.cp.objects 2
factck golden gate.cp.jtime 2
# K20 is the same absence read by the third channel: no jars at all, so B-12e
# publishes the sentinel rather than four counts.
#
# THREE ABSENCES AT THIS RUNG AND THREE SENTINELS, each naming its own channel:
# gate.cp=no-jars for the constant pools, gate.b15.mod=no-module-jars for the
# manifests, gate.c1=no-scan for the class-file major on L2. Not one of the
# three is spelled 0.000000, which is what lets a reader of a failed job tell
# a build that published nothing from a candidate that converted nothing.
factck K20 gate.cp 'no-jars'

# --- and the rehearsal proves it can fail ------------------------------------
# Everything above this line is a clean run against a fixture whose answer was
# written down first, and a rehearsal whose assertions cannot fire agrees with
# anything. So: take the golden -- the one tree that scores every gate in
# full -- move ONE thing, and require the composed blocks to notice. Twenty
# perturbations on eight channels, and every expected figure below is derived
# in the comment above it rather than transcribed, because these trees do not
# exist in mkfix11.py's table.
#
# The eight channels are the point of the count. The source report, the
# class-file scan, the published jars, and the FIVE id lists the one
# behavior.summary is read against -- httpids, svcids, tbids, swids, vhids. A
# rung with two artifact channels and five behaviour lists can get any one of
# them crossed with any other, and the only way to see a crossing is to move
# one and assert that the other seven did not. The fifth list is this rung's:
# four recorded access failures, on the one artefact that already carried
# ninety-seven ids for four other legs.
#
# The first moves one field of the source report. `toarray[2]` is the sole
# record in B-14.4's ContainerRegistry#create bucket. Its `in=` is re-pointed
# at a method name that is not one of toarray.txt's five, and nothing else is
# touched -- in particular the summary line `toarray.ctorref=8` still says
# eight, because an `in=` is an attribution and not a count.
#
# So B-14.4's two pairs have to disagree. The BUCKET pair loses a bucket and
# reads 4 of 5; the SITE pair reads the summary and still reads 8 of 8. B-14.4
# is a min over both pairs, so it falls to 0.800000 and no further -- a value
# no fixture in the table produces. B-14 falls with it to
# (0.07 + 0.17 + 0.12*0.8 + 0.54) / 0.90 = 0.876/0.90 = 0.973333, and neither
# B-15 nor family C moves at all, because nothing in either reads a toarray
# record.
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

factck PERT gate.b14.c4.want     5
factck PERT gate.b14.c4.departed 5
factck PERT gate.b14.c4.arrived  4
factck PERT B14.4.toarray        0.800000
factck PERT B14.java11-jakarta   0.973333
# ...and the rest of the tree, unmoved. The four family-C legs are the
# load-bearing ones: a source-report edit that moved a family-C leg would mean
# this rung's four gates are reading a rung below's records. B-16 and B-17 are
# here for the same reason one and two rungs nearer -- both read this same
# report, B-16 over four legs and B-17 over three, and a toarray record is in
# none of those seven populations any more than it is in family C's four.
#
# THE LEG NAMES ARE THE ASSERTION AS MUCH AS THE NUMBERS ARE. At step 10 the
# three names below read C2.type-patterns, C3.pattern-switch and
# C4.sequenced-access; at this rung those three facts still exist and are
# still 1.000000, but they are spelled B17.2/.3/.4 and family C's own three
# are different gates over different populations. A row carried forward
# unedited would have asserted six true statements about the rung below and
# none about this one.
factck PERT B14.2.jakarta        1.000000
factck PERT B14.3.javax-held     1.000000
factck PERT B14.5.httpclient     1.000000
factck PERT gate.src.penalty     1
factck PERT B15.2.module-names   1.000000
factck PERT B15.3.platform-lookup 1.000000
factck PERT B15.jpms-services    1.000000
factck PERT B16.java17-constructs 1.000000
factck PERT B17.java21-constructs 1.000000
factck PERT C1.level-66             1.000000
factck PERT C2.doprivileged-removal 1.000000
factck PERT C3.unnamed-catch        1.000000
factck PERT C4.varhandle            1.000000
factck PERT familyC                 1.000000
factck PERT B10.java7-held       1.000000
factck PERT B11.java8lang-held   1.000000
factck PERT B12.java8-library    1.000000
factck PERT B13.java9-10         1.000000

# --- can it fail, on the channel this rung brought back ----------------------
# C-1 reads ${WORK}/scan.txt and nothing else in the verifier does. L1 is the
# fixture for the partial level move and mkfix11.py wrote its scan.txt; this
# run writes the SAME shape by hand, out of this script, and requires the same
# two numbers. If write_scan() and the scorer ever agreed on a spelling neither
# arqgate.Scan nor a reader would recognise, L1 would pass and this would not.
#
#   classes=739, major.65=46, major.66=693
#   C-1 = 693/739 = 0.937754, and the other three legs untouched
#   familyC = 0.04*0.937754 + 0.96 = 0.997510
#
# IT IS DELIBERATELY NOT L1's NUMBERS. Both rows now divide by 739, because
# both describe a tree that did this rung's deletion and left only the level
# behind, so the denominators no longer tell them apart and the numerators
# have to: L1 leaves 39 classes at the old major and reads 0.947226, this
# leaves 46 and reads 0.937754. Two rows that agreed would prove only that one
# of them ran.
#
# THE RESIDUAL MAJOR MOVED WITH THE RUNG and that is worth writing rather than
# discovering. At step 10 the classes that had not moved were at 61; here they
# are at 65, because the tree this rung is handed is step 10's answer and 65 is
# what it compiled to. The leg reads `major.${WANT_MAJOR}=` and the total, so
# the residual's spelling changes nothing it scores -- which is exactly why a
# hand-written scan has to carry the right one anyway: a fixture whose residual
# major were impossible would be describing a tree that cannot exist. 65 is
# also HOLD_MAJOR, the number this rung's must-flip is measured against, so a
# residual written at any other major would be claiming an intermediate build
# that no rung of this ladder ever produced.
echo
echo "== can it fail: the golden's class-file scan, written by hand"
scandir="${TMP}/scanp"
rm -rf "${scandir}"; mkdir -p "${scandir}"
cp -R "${FIXDIR}/golden" "${scandir}/SCANPERT"
cat > "${scandir}/SCANPERT/scan.txt" <<'SCAN'
classes=739
major.65=46
major.66=693
majors.distinct=2
SCAN
nrep=$(diff "${FIXDIR}/golden/report.txt" "${scandir}/SCANPERT/report.txt" | grep -c '^[<>]' || true)
njar=$(ls "${scandir}/SCANPERT"/jars/*.jar 2>/dev/null | grep -c . || true)
nsum=$(grep -c '^major\.66=693$' "${scandir}/SCANPERT/scan.txt" || true)
if [ "${nrep}" = 0 ] && [ "${njar}" = 12 ] && [ "${nsum}" = 1 ]; then
   note "the scan was rewritten and nothing else: report.txt untouched, twelve jars still published"
else
   bad "the scan perturbation did not land as described:" \
       "report.txt changed lines ${nrep} (want 0), jars ${njar} (want 12)," \
       "major.66=693 lines ${nsum} (want 1)"
fi
_savedfix=${FIXDIR}
FIXDIR=${scandir}
run_one SCANPERT
FIXDIR=${_savedfix}
factck SCANPERT gate.c1.classes       739
factck SCANPERT gate.c1.at66          693
factck SCANPERT C1.level-66           0.937754
factck SCANPERT familyC               0.997510
# ...and the three legs that read the AST, and the inherited gates that read
# the jars or the report. A C-1 that fell back to the report's own level
# record, or a B-15.2 that read the scan, shows up here and in no fixture.
factck SCANPERT C2.doprivileged-removal 1.000000
factck SCANPERT C3.unnamed-catch      1.000000
factck SCANPERT C4.varhandle          1.000000
factck SCANPERT B15.2.module-names    1.000000
factck SCANPERT B15.jpms-services     1.000000
factck SCANPERT B16.java17-constructs 1.000000
factck SCANPERT B17.java21-constructs 1.000000
factck SCANPERT gate.cp.jars          12

# --- can it fail, on the channel the rung below was about --------------------
# The two perturbations above move a source report and a class-file scan.
# B-15.2 reads neither: it reads twelve files on disk through
# java.lang.module. So the third perturbation deletes one of them.
#
# arquillian-testng is chosen and the choice is not arbitrary. It carries none
# of B-12e's four marker packages -- the markers sit in the first six class
# files, which are api, spi and impl-base -- so B-12's five legs are unmoved
# and the fall is B-15's alone. It is also not the poisoned artifact, so
# derivable falls by one for the same reason total does and not for two.
#
#   mod.total 11, mod.declared 11, mod.derivable 10, names 11
#   B-15.2 = (11/12)^2 = 0.840278
#   B-15.3 = 1.000000, nothing in the source moved
#   B-15 = (0.35*0.840278 + 0.45) / 0.80 = 0.930122
#   familyC = 1.000000, on all four legs, and B-16 = 1.000000 on all four of
#   its own
#
# 0.930122 IS ALSO K1's B-15 AND THAT COLLISION IS THE ASSERTION. K1 publishes
# twelve jars and names eleven of them; MODPERT publishes eleven and names all
# eleven. The two trees are different in the only way that matters to a build
# engineer and identical to the score, because the leg divides by the
# BASELINE's twelve in both cases. gate.b15.mod.total -- 12 against 11 -- is
# the only fact that separates them, and a leg that divided by mod.total would
# read MODPERT at 1.000000 and hand a build that withdrew an artifact from
# publication a perfect name score.
#
# THE OTHER HALF OF THIS ROW IS WHAT DID NOT MOVE. mod.derivable falls to 10
# and B-15 does not notice, which is the same statement M1 makes from the other
# direction: this rung publishes readability and does not score it.
echo
echo "== can it fail: one of the golden's twelve jars, gone"
modpdir="${TMP}/modp"
rm -rf "${modpdir}"; mkdir -p "${modpdir}"
cp -R "${FIXDIR}/golden" "${modpdir}/MODPERT"
gonejar="${modpdir}/MODPERT/jars/arquillian-testng-1.0.0-SNAPSHOT.jar"
[ -f "${gonejar}" ] || bad "the golden does not publish arquillian-testng; this block was written against a jar that exists"
rm -f "${gonejar}"
nleft=$(ls "${modpdir}/MODPERT"/jars/*.jar 2>/dev/null | grep -c . || true)
nrep=$(diff "${FIXDIR}/golden/report.txt" "${modpdir}/MODPERT/report.txt" | grep -c '^[<>]' || true)
nscan=$(diff "${FIXDIR}/golden/scan.txt" "${modpdir}/MODPERT/scan.txt" | grep -c '^[<>]' || true)
if [ "${nleft}" = 11 ] && [ ! -f "${gonejar}" ] && [ "${nrep}" = 0 ] && [ "${nscan}" = 0 ]; then
   note "eleven jars left, arquillian-testng gone, report.txt and scan.txt untouched"
else
   bad "the jar perturbation did not land as described:" \
       "jars left ${nleft} (want 11), report.txt changed lines ${nrep} (want 0)," \
       "scan.txt changed lines ${nscan} (want 0)"
fi

_savedfix=${FIXDIR}
FIXDIR=${modpdir}
run_one MODPERT
FIXDIR=${_savedfix}

factck MODPERT gate.b15.mod.total      11
factck MODPERT gate.b15.mod.declared   11
factck MODPERT gate.b15.mod.derivable  10
factck MODPERT gate.b15.names          11
factck MODPERT B15.2.module-names      0.840278
factck MODPERT B15.3.platform-lookup   1.000000
factck MODPERT B15.jpms-services       0.930122
# The denominator did NOT follow the tree down, and neither did family C. A
# rung whose class-file scan came off the published jars -- which is the shape
# step 08 had -- would lose C-1 here too, and this row would stop being a
# one-channel finding.
factck MODPERT gate.b15.departed       1.000000
factck MODPERT gate.c1.classes         739
factck MODPERT gate.c1.at66            739
factck MODPERT C1.level-66             1.000000
factck MODPERT familyC                 1.000000
factck MODPERT B16.java17-constructs   1.000000
factck MODPERT B17.java21-constructs   1.000000
factck MODPERT B14.java11-jakarta      1.000000
# ...and B-12e read eleven jars and found all four package counts intact,
# which is why B-12 is unmoved and why this row is a B-15 finding and not a
# two-family one.
factck MODPERT gate.cp.jars        11
factck MODPERT gate.cp.classes     22
factck MODPERT gate.cp.optional    5
factck MODPERT gate.cp.collectors  6
factck MODPERT gate.cp.objects     2
factck MODPERT gate.cp.jtime       2
factck MODPERT B12.java8-library   1.000000

# --- can it fail, five more times: the recorded behaviour, broken -----------
# FIVE of the thirteen columns are scaled by what family A still reproduces,
# and they are scaled by FIVE DIFFERENT LISTS read out of ONE file: B-14.5 by
# baseline/httpids.txt's eighteen exchange ids, B-15.3 by
# baseline/svcids.txt's sixty-nine service lookups, B-16.3 by
# baseline/tbids.txt's five document recordings, B-17.3 by
# baseline/swids.txt's five dispatch recordings, and C-4 by
# baseline/vhids.txt's four field-access recordings. B-10's nio quarter reads a
# sixth. No fixture in the table exercises the http list, because every
# fixture's summary carries only the ids its own catalogue row asked for.
#
# THE LIST THAT USED TO BE FAMILY C'S IS NOW INHERITED FOR THE SECOND RUNG
# RUNNING, AND THIS TIME THE NAME IT VACATED WENT TO A LEG WITH NO LIST AT ALL.
# At step 09 tbids.txt scaled `C3.text-blocks`; at step 10 the same five ids
# scaled `B16.3.text-blocks` while `C3` named the pattern switch and swids.txt;
# here swids.txt scales `B17.3.pattern-switch` and `C3` names the unnamed catch
# parameter, WHICH IS SCALED BY NOTHING, because a binding nobody reads has no
# run-time behaviour to record. Family C's scaled leg at this rung is C-4 and
# its list is vhids.txt. A section carried forward with its names unchanged
# would have moved a fact called C-3 with the swids run, come out to the right
# arithmetic, and asserted nothing whatever about either of the two lists this
# rung actually cares about -- and it would also have asserted a scaling on a
# leg that has none, which is the sharper of the two errors because no run in
# this file could have contradicted it.
#
# Those scalings exist because the source legs above them are all SHAPE --
# records gone, types declared, a call site present -- and a file that deletes
# the method body has every one of them. Family A notices, but eighteen of 939
# transcripts is 0.005751 of the step against B-14's weight, five is 0.001597
# against B-16.3's and against B-17.3's, four is 0.001278 against C-4's 0.24,
# and every one of them is a trade in favour of faking it. The scalings are
# what close them. So what this rehearsal has to prove is not that they exist
# but that each is a FRACTION of its OWN list read at run time -- not a
# boolean, not a halving, and not one of the other four.
#
#   BEHAVALL   all eighteen http ids broken -- nine as `differ` and nine as
#              `missing`, because the awk reads both channels and a rehearsal
#              that only ever sent `differ` would agree with a gate that read
#              one. B-14.5 = 0, B-14 = (0.07+0.17+0.12)/0.90 = 0.400000
#   BEHAVHALF  nine of the eighteen. B-14.5 = 0.500000,
#              B-14 = (0.36 + 0.54*0.5)/0.90 = 0.700000
#
# 0.400000 and 0.700000 are values no fixture in the table produces, and the
# pair of them is what a halving could not produce: a gate that halved B-14.5
# on any breakage would read 0.500000 in BOTH runs. B-15, B-16, family C and
# B-10 have to stay at 1.000000 in both, and those four are the assertion that
# the five provisos read their own lists and not each other's.
echo
echo "== can it fail: the golden's recorded exchange, broken"

httpids=$(grep '^[a-z]' /tests/baseline/httpids.txt)
nhttp=$(printf '%s\n' "${httpids}" | grep -c .)
[ "${nhttp}" = 18 ] || bad "baseline/httpids.txt names ${nhttp} ids, this block was written against 18"
svcids=$(grep '^[a-z]' /tests/baseline/svcids.txt)
nsvc=$(printf '%s\n' "${svcids}" | grep -c .)
[ "${nsvc}" = 69 ] || bad "baseline/svcids.txt names ${nsvc} ids, this block was written against 69"
tbids=$(grep '^[a-z]' /tests/baseline/tbids.txt)
ntb=$(printf '%s\n' "${tbids}" | grep -c .)
[ "${ntb}" = 5 ] || bad "baseline/tbids.txt names ${ntb} ids, this block was written against 5"
swids=$(grep '^[a-z]' /tests/baseline/swids.txt)
nsw=$(printf '%s\n' "${swids}" | grep -c .)
[ "${nsw}" = 5 ] || bad "baseline/swids.txt names ${nsw} ids, this block was written against 5"
vhids=$(grep '^[a-z]' /tests/baseline/vhids.txt)
nvh=$(printf '%s\n' "${vhids}" | grep -c .)
[ "${nvh}" = 4 ] || bad "baseline/vhids.txt names ${nvh} ids, this block was written against 4"
# The six lists are disjoint, which is the precondition every assertion in
# this section rests on. An id that appeared on two of them would make one
# perturbation move two gates and the "unmoved" rows below would be wrong
# rather than reassuring. Six lists is fifteen pairs, and the five this rung
# added are the five that involve vhids.
#
# The one to read twice is still tbids against swids: five document recordings
# and five dispatch recordings, the two lists whose legs were both called C-3
# at one rung or another, and THE SAME LENGTH since the rung below -- which
# makes this loop matter MORE at this rung than it did two ago, not less.
# While swids held four ids the two legs could be told apart by arithmetic
# alone: no four-long list produces 0.600000. At five and five each produces
# the other's fractions exactly, so nothing but this loop and the unmoved rows
# at the foot of the file says that what the web runner writes into an HTML
# page and what the deployment packager answers to an archive no arm accepts
# are different behaviours of different code.
#
# VHIDS IS FOUR LONG AND THAT PUTS IT BACK ON THE OTHER SIDE OF THAT LINE.
# 0.750000 and 0.500000 are not reachable from a five-long list -- fifths are
# 0.200000 apart and quarters are 0.250000 -- so the newest of the six is the
# one list this section can still separate by arithmetic alone, and the two
# that cannot be separated that way are the two the loop below is carrying.
# nioids is also four long, which is why the vh-vs-nio pair is in the list
# rather than assumed: two four-long lists produce each other's fractions
# exactly, and B-10's quarter and C-4's scaling would be indistinguishable by
# any number either of them prints.
for pair in "httpids svcids" "httpids nioids" "httpids tbids" "httpids swids" \
            "httpids vhids" \
            "svcids nioids" "svcids tbids" "svcids swids" "svcids vhids" \
            "nioids tbids" "nioids swids" "nioids vhids" \
            "tbids swids" "tbids vhids" "swids vhids"; do
   set -- ${pair}
   n=$(LC_ALL=C comm -12 \
         <(grep '^[a-z]' "/tests/baseline/$1.txt" | LC_ALL=C sort) \
         <(grep '^[a-z]' "/tests/baseline/$2.txt" | LC_ALL=C sort) | grep -c . || true)
   [ "${n}" = 0 ] || bad "baseline/$1.txt and baseline/$2.txt share ${n} id(s)"
done
note "httpids (18), svcids (69), tbids (5), swids (5), vhids (4) and nioids (4) are six disjoint lists"

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

factck BEHAVALL  gate.b14.c5.exec.want   18
factck BEHAVALL  gate.b14.c5.exec.broken 18
factck BEHAVALL  gate.b14.c5.exec        0.000000
factck BEHAVHALF gate.b14.c5.exec.want   18
factck BEHAVHALF gate.b14.c5.exec.broken 9
factck BEHAVHALF gate.b14.c5.exec        0.500000
# B-14.5's four SOURCE legs are untouched in both runs -- the shape is still
# perfect. That is the whole point: the leg falls on behaviour alone.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.b14.c5.legacy    0
   factck "${f}" gate.b14.c5.types     3
   factck "${f}" gate.b14.c5.redirects 1
done
# The redirect proviso splits them, and it splits them on WHICH id broke rather
# than on how many. BEHAVALL breaks servlet/exec-redirect among its eighteen,
# so the quarter goes; BEHAVHALF breaks the first nine, which do not include
# it, so the quarter stays.
factck BEHAVALL  gate.b14.c5.redirects.honoured 0
factck BEHAVHALF gate.b14.c5.redirects.honoured 1
factck BEHAVALL  B14.5.httpclient      0.000000
factck BEHAVHALF B14.5.httpclient      0.500000
factck BEHAVALL  B14.java11-jakarta    0.400000
factck BEHAVHALF B14.java11-jakarta    0.700000
# ...and the sixty-nine, the two fives, the four, and B-10's four. Six lists,
# six provisos, and only one of them fired.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.b15.exec.broken 0
   factck "${f}" gate.b15.exec        1.000000
   factck "${f}" B15.3.platform-lookup 1.000000
   factck "${f}" B15.jpms-services    1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" gate.b16.3.exec      1.000000
   factck "${f}" B16.3.text-blocks    1.000000
   factck "${f}" B16.java17-constructs 1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" gate.b17.3.exec      1.000000
   factck "${f}" B17.3.pattern-switch 1.000000
   factck "${f}" B17.java21-constructs 1.000000
   factck "${f}" gate.c4.exec.broken  0
   factck "${f}" gate.c4.exec         1.000000
   factck "${f}" C4.varhandle         1.000000
   factck "${f}" familyC              1.000000
   factck "${f}" gate.src.penalty     1
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held      1.000000
   factck "${f}" B11.java8lang-held  1.000000
   factck "${f}" B12.java8-library   1.000000
   factck "${f}" B13.java9-10        1.000000
done

# --- the same instrument, pointed at the other four lists --------------------
# The http pair above proved the instrument on one list. Twelve runs here point
# it at the other four, in the same three shapes each time: the whole list,
# part of it, and a single id. The arithmetic is the assertion, and it comes
# out differently for each list because each list is a different length and
# each proviso sits under a different weight.
#
#   SVCALL   all sixty-nine, thirty-five as `differ` and thirty-four as
#            `missing`. B-15.3 = 0.000000 over an unchanged min() of 1.000000,
#            so B-15 = 0.35/0.80 = 0.437500
#   SVCHALF  thirty-five of the sixty-nine. 34/69 = 0.492754, so
#            B-15 = (0.35 + 0.45*0.492754)/0.80 = 0.714674
#   SVCONE   one. 68/69 = 0.985507, so B-15 = 0.991848
#
#   TBALL    all five, three `differ` and two `missing`. B-16.3 = 0.000000, so
#            B-16 = (0.20 + 0.36 + 0.08)/0.90 = 0.711111
#   TBTWO    two of the five. 3/5 = 0.600000, so
#            B-16 = (0.64 + 0.26*0.600000)/0.90 = 0.884444
#   TBONE    one. 4/5 = 0.800000, so B-16 = 0.942222
#
#   SWALL    all five, three `differ` and two `missing`. B-17.3 = 0.000000, so
#            B-17 = (0.17 + 0.14)/0.96 = 0.322917
#   SWTWO    two of the five. 3/5 = 0.600000, so
#            B-17 = (0.31 + 0.65*0.600000)/0.96 = 0.729167
#   SWONE    one. 4/5 = 0.800000, so B-17 = 0.864583
#
#   VHALL    all four, two `differ` and two `missing`. C-4 = 0.000000, so
#            familyC = 0.04 + 0.46 + 0.26 = 0.760000
#   VHTWO    two of the four. 2/4 = 0.500000, so
#            familyC = 0.76 + 0.24*0.500000 = 0.880000
#   VHONE    one. 3/4 = 0.750000, so familyC = 0.940000
#
# SIXTY-NINE CANNOT PRODUCE 0.600000, BUT FIVE AND FIVE PRODUCE EACH OTHER'S
# FIGURES EXACTLY, and that is the one thing about this section that the
# five-id swids.txt changed a rung ago and this rung has not changed back.
# Against svcids a crossed pair does not merely score wrongly, it scores
# impossibly -- five cannot produce 0.985507. Against tbids there is no
# impossibility left at all: tbids.txt and swids.txt are the same length, so
# TBTWO and SWTWO both read 0.600000 and TBONE and SWONE both read 0.800000.
# THE ARITHMETIC USED TO SEPARATE THOSE TWO LISTS AND IT DOES NOT ANY MORE.
#
# AND THE FOURTH TRIPLE IS THE ONE THE ARITHMETIC STILL SEPARATES, which is
# worth saying in the same breath rather than leaving to be noticed. vhids.txt
# is four long: 0.750000 and 0.500000 are quarters, no fifth is either of them,
# and no run in this file reads either number except the two VH runs and the
# table rows that reach them through C-4's source halves. The newest list is
# the easiest of the six to prove, and that is exactly why it is not the one
# the unmoved rows are written for.
#
# WHAT SEPARATES THE TWO FIVES IS WHICH COLUMN MOVED, and that is now the
# whole of it. TBTWO moves B16.3.text-blocks to 0.600000 and leaves
# B17.3.pattern-switch at 1.000000; SWTWO moves B17.3.pattern-switch to
# 0.600000 and leaves B16.3.text-blocks at 1.000000. A scorer handed one list
# for both legs would move both columns in both runs, and the UNMOVED rows
# below are the only place that is visible -- as are the disjointness loop
# above and L8 and N8 up in the table. Read together they are a stronger claim
# than the arithmetic ever was, because they name the fact rather than infer
# it from a fraction; but they now have to carry it alone, so none of them is
# optional.
#
# THE TWO LISTS THAT ARE ONE RENAME APART ARE THE POINT OF KEEPING BOTH
# TRIPLES. Neither run would say anything if the other were missing, because
# each of them is the other's control. SWALL and TBALL are still the pair to
# read first: both reach 0.000000, TBALL leaves B-17 at 1.000000 and SWALL
# leaves B-16 at 1.000000, and a scorer that read one list for both legs could
# not do both. Note that neither of them touches familyC at all any more --
# that column is VHALL's now, and the three-way split of the twelve runs
# across B-15, B-16, B-17 and family C is itself the claim that the four
# provisos live on four gates.
#
# THE COINCIDENCES, AND WHY THEY ARE NOT A PROBLEM HERE EITHER. SVCALL's
# 0.437500 is K17's, M4's and M5's B-15; TBTWO's 0.884444 is L8's B-16;
# TBALL's 0.711111 is L6's and L14's B-16; SWTWO's 0.729167 is N8's B-17 and
# SWALL's 0.322917 is N15's; VHONE's 0.940000 is P10's and P11's familyC and
# VHALL's 0.760000 is P13's. Every one of those table rows reaches the number
# through a SOURCE fact -- a converted site missing, a held site moved -- on a
# summary that is clean, and every run here reaches it through a broken
# summary over source that is whole. The `gate.*.exec.broken` rows below are
# what separate them, and they are asserted for that reason and not for the
# arithmetic.
echo
echo "== can it fail: the other four lists, whole, part, and one"

# One table, four lists, twelve runs: <name> <list> <broken> <as-differ>. What
# `broken` leaves over goes on the `missing` channel, so each of the four
# lists has at least one run that exercises both. The awk behind every one of
# these gates reads the two channels with a single alternation, and a
# rehearsal that only ever sent `differ` would agree with a gate that had
# quietly stopped reading the other one.
for v in SVCALL SVCHALF SVCONE TBALL TBTWO TBONE SWALL SWTWO SWONE \
         VHALL VHTWO VHONE; do
   rm -rf "${behavdir:?}/${v}"
   cp -R "${FIXDIR}/golden" "${behavdir}/${v}"
done
while read -r v lst nbrk ndiff; do
   [ -n "${v}" ] || continue
   case "${lst}" in
      svcids) _ids=${svcids} ;;
      tbids)  _ids=${tbids}  ;;
      swids)  _ids=${swids}  ;;
      vhids)  _ids=${vhids}  ;;
      *)      bad "${v} names list '${lst}', which this block does not load"; continue ;;
   esac
   i=0
   for id in ${_ids}; do
      [ "${i}" -lt "${nbrk}" ] || break
      if [ "${i}" -lt "${ndiff}" ]; then
         echo "behavior.differ[${i}]=${id}"  >> "${behavdir}/${v}/behavior.summary"
      else
         echo "behavior.missing[${i}]=${id}" >> "${behavdir}/${v}/behavior.summary"
      fi
      i=$(( i + 1 ))
   done
   got=$(grep -cE  '^behavior\.(differ|missing)\[' "${behavdir}/${v}/behavior.summary" || true)
   gotd=$(grep -cE '^behavior\.differ\['           "${behavdir}/${v}/behavior.summary" || true)
   gotb=$(grep -cE '^behavior\.build=ok$'          "${behavdir}/${v}/behavior.summary" || true)
   gotr=$(diff "${FIXDIR}/golden/report.txt" "${behavdir}/${v}/report.txt" | grep -c '^[<>]' || true)
   if [ "${got}" = "${nbrk}" ] && [ "${gotd}" = "${ndiff}" ] \
      && [ "${gotb}" = 1 ] && [ "${gotr}" = 0 ]; then
      note "${v}: ${nbrk} of ${lst} broken, ${ndiff} as differ, report.txt untouched"
   else
      bad "${v} did not land as described: broken ${got} (want ${nbrk})," \
          "differ ${gotd} (want ${ndiff}), build lines ${gotb} (want 1)," \
          "report.txt changed lines ${gotr} (want 0)"
   fi
done <<EOF
SVCALL  svcids 69 35
SVCHALF svcids 35 35
SVCONE  svcids  1  1
TBALL   tbids   5  3
TBTWO   tbids   2  2
TBONE   tbids   1  1
SWALL   swids   5  3
SWTWO   swids   2  2
SWONE   swids   1  1
VHALL   vhids   4  2
VHTWO   vhids   2  2
VHONE   vhids   1  1
EOF

_savedfix=${FIXDIR}
FIXDIR=${behavdir}
for v in SVCALL SVCHALF SVCONE TBALL TBTWO TBONE SWALL SWTWO SWONE \
         VHALL VHTWO VHONE; do
   run_one "${v}"
done
FIXDIR=${_savedfix}

# --- the sixty-nine
factck SVCALL  gate.b15.exec.want    69
factck SVCALL  gate.b15.exec.broken  69
factck SVCALL  gate.b15.exec         0.000000
factck SVCALL  B15.3.platform-lookup 0.000000
factck SVCALL  B15.jpms-services     0.437500
factck SVCHALF gate.b15.exec.want    69
factck SVCHALF gate.b15.exec.broken  35
factck SVCHALF gate.b15.exec         0.492754
factck SVCHALF B15.3.platform-lookup 0.492754
factck SVCHALF B15.jpms-services     0.714674
factck SVCONE  gate.b15.exec.want    69
factck SVCONE  gate.b15.exec.broken  1
factck SVCONE  gate.b15.exec         0.985507
factck SVCONE  B15.3.platform-lookup 0.985507
factck SVCONE  B15.jpms-services     0.991848
# B-15.3's three source halves are whole in all three, and so is B-15.2, which
# is what makes these the behaviour case rather than the service-declaration
# one. 0.492754 is a number only a sixty-nine-long list produces: it is not
# reachable from five, from four, from eighteen, or from any halving.
for f in SVCALL SVCHALF SVCONE; do
   factck "${f}" gate.b15.departed   1.000000
   factck "${f}" gate.b15.arrived    1.000000
   factck "${f}" gate.b15.held       1.000000
   factck "${f}" B15.2.module-names  1.000000
done

# --- the five
factck TBALL  gate.b16.3.exec.want   5
factck TBALL  gate.b16.3.exec.broken 5
factck TBALL  gate.b16.3.exec        0.000000
factck TBALL  B16.3.text-blocks      0.000000
factck TBALL  B16.java17-constructs  0.711111
factck TBTWO  gate.b16.3.exec.want   5
factck TBTWO  gate.b16.3.exec.broken 2
factck TBTWO  gate.b16.3.exec        0.600000
factck TBTWO  B16.3.text-blocks      0.600000
factck TBTWO  B16.java17-constructs  0.884444
factck TBONE  gate.b16.3.exec.want   5
factck TBONE  gate.b16.3.exec.broken 1
factck TBONE  gate.b16.3.exec        0.800000
factck TBONE  B16.3.text-blocks      0.800000
factck TBONE  B16.java17-constructs  0.942222
# The text block is still there and the literals it replaced are still gone in
# all three, and the other three legs of B-16 never look at a summary. This is
# the row that says the five-id list belongs to ONE leg of the inherited gate
# and not to the gate.
for f in TBALL TBTWO TBONE; do
   factck "${f}" gate.b16.3.moved         1
   factck "${f}" gate.b16.3.movefrac      1.000000
   factck "${f}" gate.b16.3.held          7
   factck "${f}" gate.b16.3.holdfrac      1.000000
   factck "${f}" gate.b16.3.textblocks    1
   factck "${f}" B16.2.pattern-instanceof 1.000000
   factck "${f}" B16.4.sealed-hierarchies 1.000000
   factck "${f}" B16.5.arrow-switch       1.000000
done

# --- the other five
factck SWALL  gate.b17.3.exec.want   5
factck SWALL  gate.b17.3.exec.broken 5
factck SWALL  gate.b17.3.exec        0.000000
factck SWALL  B17.3.pattern-switch   0.000000
factck SWALL  B17.java21-constructs  0.322917
factck SWTWO  gate.b17.3.exec.want   5
factck SWTWO  gate.b17.3.exec.broken 2
factck SWTWO  gate.b17.3.exec        0.600000
factck SWTWO  B17.3.pattern-switch   0.600000
factck SWTWO  B17.java21-constructs  0.729167
factck SWONE  gate.b17.3.exec.want   5
factck SWONE  gate.b17.3.exec.broken 1
factck SWONE  gate.b17.3.exec        0.800000
factck SWONE  B17.3.pattern-switch   0.800000
factck SWONE  B17.java21-constructs  0.864583
# The ten chains that left, the ten arms that arrived, the two held chains,
# the four switches and the one `case null` are all still in the report in all
# three runs: THE TREE IS THE GOLDEN AND ONLY THE SUMMARY WAS DOCTORED, so
# both halves of the conversion must read full and not just one of them.
# B-17.3 is the only leg of the inherited JEP-441 gate with a behaviour
# scaling, and its two siblings are the rows that say so -- as is familyC,
# which does not move in any of the three and did move in all three one rung
# ago.
for f in SWALL SWTWO SWONE; do
   factck "${f}" gate.b17.3.departed   10
   factck "${f}" gate.b17.3.depfrac    1.000000
   factck "${f}" gate.b17.3.arrived    10
   factck "${f}" gate.b17.3.arrfrac    1.000000
   factck "${f}" gate.b17.3.held       2
   factck "${f}" gate.b17.3.holdfrac   1.000000
   factck "${f}" gate.b17.3.switches   4
   factck "${f}" gate.b17.3.patterns   10
   factck "${f}" gate.b17.3.nullcase   1
   factck "${f}" B17.2.type-patterns     1.000000
   factck "${f}" B17.4.sequenced-access  1.000000
   factck "${f}" C1.level-66             1.000000
   factck "${f}" C2.doprivileged-removal 1.000000
   factck "${f}" C3.unnamed-catch        1.000000
   factck "${f}" C4.varhandle            1.000000
   factck "${f}" familyC                 1.000000
done

# --- the four
# This rung's own, and the only one of the five whose fractions no other list
# in the image can produce. Four recorded field accesses: two `differ` and two
# `missing` in VHALL, so the alternation is exercised on the newest list too
# and not only on the four it inherited.
factck VHALL  gate.c4.exec.want   4
factck VHALL  gate.c4.exec.broken 4
factck VHALL  gate.c4.exec        0.000000
factck VHALL  C4.varhandle        0.000000
factck VHALL  familyC             0.760000
factck VHTWO  gate.c4.exec.want   4
factck VHTWO  gate.c4.exec.broken 2
factck VHTWO  gate.c4.exec        0.500000
factck VHTWO  C4.varhandle        0.500000
factck VHTWO  familyC             0.880000
factck VHONE  gate.c4.exec.want   4
factck VHONE  gate.c4.exec.broken 1
factck VHONE  gate.c4.exec        0.750000
factck VHONE  C4.varhandle        0.750000
factck VHONE  familyC             0.940000
# The four setAccessible calls that left, the eight lookup-and-unreflect pairs
# that arrived, the ten calls that had to stay and the ten sites the scan
# still finds are all in the report in all three runs. THE SCALING MULTIPLIES
# A MIN THAT IS 1.000000, which is the only shape in which the three familyC
# figures above are readable as the scaling and not as a source loss: a tree
# that had converted three of the four heads would reach 0.750000 on the
# min() instead, and C-4's three fractions are what tell the two apart.
for f in VHALL VHTWO VHONE; do
   factck "${f}" gate.c4.departed       4
   factck "${f}" gate.c4.depfrac        1.000000
   factck "${f}" gate.c4.arrived        8
   factck "${f}" gate.c4.arrfrac        1.000000
   factck "${f}" gate.c4.held           10
   factck "${f}" gate.c4.holdfrac       1.000000
   factck "${f}" gate.c4.handles        8
   factck "${f}" gate.c4.setaccessible  10
   factck "${f}" C1.level-66             1.000000
   factck "${f}" C2.doprivileged-removal 1.000000
   factck "${f}" C3.unnamed-catch        1.000000
   factck "${f}" B17.java21-constructs   1.000000
done

# --- and the five lists each run did not touch
# Twelve runs, six provisos, one moved each time. This is the loop that says
# the lists are READ separately at run time and not merely STORED separately
# on disk: every one of these 144 rows would move if two of the six files were
# ever given to the same awk.
#
# THE TWO RUNGS' WORTH OF GROWTH ARE WHY THIS LOOP IS THE EXPENSIVE ONE. At
# step 09 it was three triples against four provisos; here it is four triples
# against six, and the rows that matter most are the ones that did not exist
# a rung ago: every SVC, TB and VH run asserting B-17.3 clean, and every SVC,
# TB and SW run asserting C-4 clean. A scorer that handed swids.txt to C-4's
# awk -- the single likeliest mistake at this rung, because C-4 is where C-3's
# scaling used to live -- would pass every arithmetic check in the section
# above and fail nine rows here.
for f in SVCALL SVCHALF SVCONE; do
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.3.text-blocks      1.000000
   factck "${f}" B16.java17-constructs  1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" B17.3.pattern-switch   1.000000
   factck "${f}" B17.java21-constructs  1.000000
   factck "${f}" gate.c4.exec.broken    0
   factck "${f}" C4.varhandle           1.000000
   factck "${f}" familyC                1.000000
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done
for f in TBALL TBTWO TBONE; do
   factck "${f}" gate.b15.exec.broken   0
   factck "${f}" B15.3.platform-lookup  1.000000
   factck "${f}" B15.jpms-services      1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" B17.3.pattern-switch   1.000000
   factck "${f}" B17.java21-constructs  1.000000
   factck "${f}" gate.c4.exec.broken    0
   factck "${f}" C4.varhandle           1.000000
   factck "${f}" familyC                1.000000
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done
# SW and VH each assert eleven and not thirteen, because each of them already
# asserted the other family's total up in its own block: SWALL..SWONE carry
# familyC and C4.varhandle there, VHALL..VHONE carry B17.java21-constructs.
# What is left here is the exec.broken row, which is the one that says the
# untouched list was READ and found clean rather than never opened.
for f in SWALL SWTWO SWONE; do
   factck "${f}" gate.b15.exec.broken   0
   factck "${f}" B15.3.platform-lookup  1.000000
   factck "${f}" B15.jpms-services      1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.3.text-blocks      1.000000
   factck "${f}" B16.java17-constructs  1.000000
   factck "${f}" gate.c4.exec.broken    0
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done
for f in VHALL VHTWO VHONE; do
   factck "${f}" gate.b15.exec.broken   0
   factck "${f}" B15.3.platform-lookup  1.000000
   factck "${f}" B15.jpms-services      1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.3.text-blocks      1.000000
   factck "${f}" B16.java17-constructs  1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done

# --- one absence, six consequences -------------------------------------------
# A summary that cannot be read at all is not the same as a summary that reads
# clean, and B-10 settled which way that falls at step 03: a proviso that
# cannot be checked must not be assumed satisfied. The tempting implementation
# is the other one -- leave the shape standing when there is no evidence -- and
# it is tempting because it never fires in a healthy run. It fires when the
# build fails, which is exactly when a tree that never ran would otherwise
# collect B-14.5, B-15.3, B-16.3, B-17.3 and C-4 for a file nobody executed.
#
# ONE FILE, SIX PROVISOS. behavior.summary is a single artefact and B-10,
# B-14.5, B-15.3, B-16.3, B-17.3 and C-4 all read it against six different
# lists. There is no run in which it is absent for one of them and present for
# another, so there is one perturbation here and not six, and it has to
# assert all six outcomes at once. Step 08's draft of this section had two
# runs -- one that deleted the file and checked B-14.5 while reporting family
# C at 1.000000, and one that deleted the same file and checked the text-block
# leg while reporting B-14 at 1.000000 -- and each was a tree the other proved
# impossible.
#
#   B-10      four nio ids unreadable -> nio_broken = 4 -> its third is halved
#             -> B-10 = 0.4 + 0.3 + 0.3*0.5 = 0.850000
#   B-14.5    0.000000 and the redirect quarter withdrawn with it
#             -> B-14 = (0.07+0.17+0.12)/0.90 = 0.400000
#   B-15.3    0.000000 over an unchanged min() of 1.000000
#             -> B-15 = 0.35/0.80 = 0.437500
#   B-16.3    0.000000 over an unchanged min() of 1.000000
#             -> B-16 = (0.20+0.36+0.08)/0.90 = 0.711111
#   B-17.3    0.000000 over an unchanged min() of 1.000000
#             -> B-17 = (0.17+0.14)/0.96 = 0.322917
#   C-4       0.000000 over an unchanged min() of 1.000000
#             -> familyC = 1 - 0.24 = 0.760000
#
# THIS IS THE ONE RUN IN THE FILE THAT READS THE SPLIT AS A SPLIT. Step 09's
# GONE asserted four numbers, step 10's five; the sixth did not appear because
# a list changed hands -- swids.txt changing hands cost nothing, since the row
# that used to say `C3.pattern-switch` now says `B17.3.pattern-switch` and
# there are still five -- it appeared because family C GREW a leg with a list
# of its own. A GONE carried forward with its five rows renamed and nothing
# else would report 0.850000, 0.400000, 0.437500, 0.711111 and 0.322917, pass
# on all five, and say nothing whatever about vhids.txt: the absent file would
# withdraw every inherited proviso while leaving this rung's own standing, and
# a tree that never built would still collect C-4's 0.24.
#
# 0.850000 is also K11's B-14 and K19's and K20's B-12, 0.437500 is also
# K17's, M4's and M5's B-15, 0.711111 is also L6's and L14's B-16, 0.322917 is
# also N15's B-17 and SWALL's, and 0.760000 is also P13's familyC and VHALL's.
# None of the coincidences matters: no other row in this file reads all six of
# those numbers at once, and the facts below are what make it a reading rather
# than an arithmetic accident.
echo
echo "== can it fail: no summary at all"
rm -rf "${behavdir:?}/GONE"
cp -R "${FIXDIR}/golden" "${behavdir}/GONE"
rm -f "${behavdir}/GONE/behavior.summary"
if [ -r "${behavdir}/GONE/behavior.summary" ]; then
   bad "GONE still has a behavior.summary"
else
   note "the summary is gone, and nothing else with it"
fi
_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one GONE
FIXDIR=${_savedfix}
factck GONE gate.b10.nio.behaviour.broken 4
factck GONE B10.java7-held         0.850000
factck GONE gate.b14.c5.exec.want  18
factck GONE gate.b14.c5.exec.broken 18
factck GONE gate.b14.c5.exec       0.000000
factck GONE gate.b14.c5.redirects.honoured 0
factck GONE B14.5.httpclient       0.000000
factck GONE B14.java11-jakarta     0.400000
factck GONE gate.b15.exec.want     69
factck GONE gate.b15.exec.broken   69
factck GONE gate.b15.exec          0.000000
factck GONE B15.3.platform-lookup  0.000000
factck GONE B15.jpms-services      0.437500
factck GONE gate.b16.3.exec.want   5
factck GONE gate.b16.3.exec.broken 5
factck GONE gate.b16.3.exec        0.000000
factck GONE B16.3.text-blocks      0.000000
factck GONE B16.java17-constructs  0.711111
factck GONE gate.b17.3.exec.want   5
factck GONE gate.b17.3.exec.broken 5
factck GONE gate.b17.3.exec        0.000000
factck GONE B17.3.pattern-switch   0.000000
factck GONE B17.java21-constructs  0.322917
factck GONE gate.c4.exec.want      4
factck GONE gate.c4.exec.broken    4
factck GONE gate.c4.exec           0.000000
factck GONE C4.varhandle           0.000000
factck GONE familyC                0.760000
# The shape is untouched in all six. Every source leg the six provisos scale
# is still full, which is what makes this the file-deleted case rather than the
# work-undone one.
factck GONE gate.b14.c5.legacy     0
factck GONE gate.b14.c5.types      3
factck GONE gate.b14.c5.redirects  1
factck GONE gate.b15.departed      1.000000
factck GONE gate.b15.arrived       1.000000
factck GONE gate.b15.held          1.000000
factck GONE B15.2.module-names     1.000000
factck GONE gate.b16.3.moved       1
factck GONE gate.b16.3.movefrac    1.000000
factck GONE gate.b16.3.held        7
factck GONE gate.b16.3.holdfrac    1.000000
factck GONE B16.2.pattern-instanceof 1.000000
factck GONE B16.4.sealed-hierarchies 1.000000
factck GONE B16.5.arrow-switch     1.000000
factck GONE gate.b17.3.departed    10
factck GONE gate.b17.3.depfrac     1.000000
factck GONE gate.b17.3.arrived     10
factck GONE gate.b17.3.arrfrac     1.000000
factck GONE gate.b17.3.held        2
factck GONE gate.b17.3.holdfrac    1.000000
factck GONE gate.b17.3.switches    4
factck GONE gate.c4.departed       4
factck GONE gate.c4.depfrac        1.000000
factck GONE gate.c4.arrived        8
factck GONE gate.c4.arrfrac        1.000000
factck GONE gate.c4.held           10
factck GONE gate.c4.holdfrac       1.000000
factck GONE gate.c4.handles        8
factck GONE gate.c4.setaccessible  10
factck GONE B17.2.type-patterns      1.000000
factck GONE B17.4.sequenced-access   1.000000
factck GONE C1.level-66              1.000000
factck GONE C2.doprivileged-removal  1.000000
factck GONE C3.unnamed-catch         1.000000
factck GONE gate.src.penalty       1

# --- and the redirect quarter, on its own ------------------------------------
# gate.b14.c5.redirects is a source fact and it is shallow: the scan sees that
# a followRedirects policy is NAMED, not which one. HttpClient's default is
# NEVER, so an agent who names NEVER has written a keyword that changes
# nothing, and before the proviso existed that keyword paid better than
# silence. The quarter now carries B-10's rule -- named AND the recording that
# exercises it still matching -- so the careless spelling and silence collapse
# onto the same number.
#
#   BEHAVRED   servlet/exec-redirect. exec 17/18 = 0.944444, quarter withdrawn
#              -> B-14.5 = 0.944444 * 0.75 = 0.708333, B-14 = 0.825000
#   BEHAVONE   servlet/exec-404-forever. exec 17/18, quarter kept
#              -> B-14.5 = 0.944444, B-14 = 0.966666
#
# The pair is the assertion. A gate that read the COUNT would put these two at
# the same number; a gate that read the LIST puts them 0.141666 of B-14 apart.
# And BEHAVONE is the control that says the proviso has not quietly become
# "any breakage at all costs the quarter".
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

for f in BEHAVRED BEHAVONE; do
   factck "${f}" gate.b14.c5.exec.want   18
   factck "${f}" gate.b14.c5.exec.broken 1
   factck "${f}" gate.b14.c5.exec        0.944444
   factck "${f}" gate.b14.c5.redirects   1
done
factck BEHAVRED gate.b14.c5.redirects.honoured 0
factck BEHAVONE gate.b14.c5.redirects.honoured 1
factck BEHAVRED B14.5.httpclient    0.708333
factck BEHAVONE B14.5.httpclient    0.944444
factck BEHAVRED B14.java11-jakarta  0.825000
factck BEHAVONE B14.java11-jakarta  0.966666
for f in BEHAVRED BEHAVONE; do
   factck "${f}" familyC             1.000000
   factck "${f}" C4.varhandle        1.000000
   factck "${f}" gate.c4.exec.broken 0
   factck "${f}" B17.3.pattern-switch 1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" B17.java21-constructs 1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.java17-constructs 1.000000
   factck "${f}" gate.b15.exec.broken 0
   factck "${f}" B15.jpms-services   1.000000
   factck "${f}" B10.java7-held      1.000000
   factck "${f}" B11.java8lang-held  1.000000
   factck "${f}" B12.java8-library   1.000000
   factck "${f}" B13.java9-10        1.000000
done

echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: OK"
else
   echo "c-rehearsal: FAILED"
   exit 1
fi
