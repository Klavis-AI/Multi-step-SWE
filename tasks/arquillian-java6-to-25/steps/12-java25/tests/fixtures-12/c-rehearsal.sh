#!/usr/bin/env bash
# Rehearse the eleven scoring blocks that read the typed AST, the published
# jars, the class-file scan and the build's own answer about its test forks --
# the hoisted source-AST block, B-10, B-11, B-12, B-13, B-14, B-15, B-16, B-17,
# B-18 and the whole of family C -- against trees whose answers were worked out
# first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the
# eleven blocks are lifted out of the SHIPPED test.sh by their own banners,
# composed in file order behind a srcgate() that hands them a fixture's report
# instead of compiling anything and a run_unprivileged() that hands them a
# fixture's recorded probe output instead of running a build, and run once per
# fixture. What is asserted is the arithmetic over the report, over the jars,
# over the scan and over the argument lists, which is the part of these gates
# that no other check exercises.
#
# ELEVEN blocks and not ten. Step 11's rehearsal composed the source AST, B-10
# through B-17 and family C; this step adds B-18, which is step 11's whole
# family C inherited under a new name and renormalised over the 0.96 that is
# left when its C-1 is dropped -- 0.46, 0.26 and 0.24. The extraction asserts
# all eleven are disjoint and in that order in the file -- the counters are
# read in one place and spent in ten others, and a hoist that silently
# un-hoisted would leave B-10 reading variables that do not exist yet, which
# under `set -u` is a dead verifier rather than a wrong number.
#
# STEP 11's C-1 IS THE ONE LEG THAT DID NOT COME ACROSS, and this is the fifth
# rung in a row where that is true. It was the class-file major, and the major
# is THIS rung's own must-flip -- 69 where step 11 wanted 66 -- so carrying it
# forward would grade one artefact twice against two contradictory thresholds.
# B-18 is step 11's three SOURCE legs over the 0.96 they shared, and K17 is the
# row where the consequence is visible: family C reads 0.040000 there, off an
# intact class-file scan and an intact probe, while B-16, B-17 and B-18 all
# read 0.000000, because not one of the three has a class-file leg left to be
# full.
#
# SIX FAMILIES ARE STACKED IN THE HELD HALF NOW -- step 06's as B-13, step
# 07's as B-14, step 08's as B-15, step 09's as B-16, step 10's as B-17 and
# step 11's as B-18 -- and the count is the ninth trap's whole subject. Every
# one of them is scored on every row of the table below, including the rows
# written five rungs ago, because a leg that would quietly become unpassable at
# step 12 is exactly the leg nothing at step 12 would otherwise look at.
#
# B-15 REALLY RUNS java, and it is still the only block that does. Two of step
# 08's three legs asked java.lang.module what the published artifacts call
# themselves, through arqgate.Modules; one of those two survives as B-15.2 and
# there is no way to rehearse it except by running it. So the fixtures carry
# real jars with real manifests -- wrapped at 72 bytes the way the jar tool
# wraps them, with a real META-INF/services/ entry in the one artifact whose
# descriptor the platform refuses -- and the run script below hands the block
# the same ${WORK}/modulejars.txt the build stage writes.
#
# THREE ARTIFACT CHANNELS NOW, WHERE STEP 11 HAD TWO. The jars feed B-12e and
# B-15; ${WORK}/scan.txt feeds C-1, as it has since step 09; and
# ${WORK}/jvmargs.log feeds C-5, which is new at this rung and is the only leg
# in this task that reads what the BUILD would hand a fork rather than what the
# source says or what the artifacts contain. Each channel is separable in the
# fixtures: L1 and L2 move the scan and leave the jars and the probe alone, K19
# and K20 move the jars and leave the scan and the probe alone, and Q16 through
# Q20 move the probe and leave both artefacts alone.
#
# SO A FIFTH ARTEFACT REACHES ${WORK}. run_one stages the fixture's own
# jvmargs.txt as the probe's stdout, beside report.txt, answers.txt,
# behavior.summary and scan.txt, and a fixture with NO jvmargs.txt is a build
# the probe could not ask at all -- a configuration-time hang, a gradlew that
# will not execute, a reactor that refuses to evaluate. The stub returns
# non-zero for exactly that fixture, which is how `gate.c5.probe` reads
# `failed` in a rehearsal that never started a JVM.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step12/
# mkfix12.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all. The separation is sharpest on B-15.2, which
# mkfix12.py predicts from the jar PLAN -- twelve Mod records, each with a name
# or without one -- while the rehearsal measures the platform's reading of the
# bytes that plan produced. The two agreeing is a fact about java.lang.module
# and not about either program.
#
# All fifteen figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
#
# THREE SPELLINGS OF ZERO, and K17 and L2 between them carry all three:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                B-16.2, B-16.4, B-16.5, B-17.2, B-17.4, B-18.2 and B-18.3, and
#                gate.b15.departed, gate.b15.arrived and gate.b15.held. AND SO
#                DOES EVERY ONE OF FAMILY C's FIVE, which is what changed at
#                this rung: C-2, C-3 and C-4 all close over their own guard, so
#                K17 prints the literal three times; C-1 prints it when there is
#                no scan file at all, which is L2; and C-5 prints it when the
#                build declares no flag to look for.
#   `0.000000`   B-12 through B-18 are totals computed by an awk that sits
#                OUTSIDE the guard, so they are computed from parts that never
#                ran. THREE sub-legs are in that group and their siblings are
#                not: B-16.3, B-17.3 and B-18.4 are each an awk below their own
#                `fi`, because each multiplies by a run-time scaling read from
#                behavior.summary, while gate.b16.3.movefrac,
#                gate.b16.3.holdfrac and the fractions of B-17.3 and B-18.4 are
#                initialised above it and print the literal `0`.
#
#                NOT ONE OF FAMILY C's FIVE LEGS IS IN THIS LIST, AND AT EVERY
#                RUNG SINCE 09 AT LEAST ONE WAS. Step 09's C-3, step 10's C-3
#                and step 11's C-4 were each an awk below their guard; here
#                there is no behaviour scaling anywhere in family C, because
#                none of the five changes this rung asks for has a run-time
#                signature. An import that moved compiles or does not; a
#                ScopedValue either binds or the field is still a ThreadLocal;
#                a fork either got the flag or did not. Transcribing K17's row
#                from the rung below would have put `0.000000` where this file
#                prints `0`, on one cell, and would have been wrong about it
#                while passing everything else.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so a tree nobody could compile has no
#                penalty fact rather than an unfired `1`. So are every
#                gate.b16.*, gate.b17.* and gate.b18.* measurement and every
#                gate.c2.*, gate.c3.* and gate.c4.* one. gate.c1.* and
#                gate.c5.* are NOT: their guards are the scan file and the
#                declared flag, neither of which is about compiling, so K17
#                carries all eight of them with real numbers in.
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
# THE POISONED JAR IS STILL PART OF THE INSTRUMENT CHECK AND NOT PART OF THE
# SCORE. arquillian-impl-base ships a META-INF/services/org.jboss.
# arquillian.spi.client.container.ArquillianDescriptor naming a provider class
# that is not in the module, and ModuleFinder refuses to derive a descriptor
# for it -- which is why derivable is 11 and not 12. Step 08 spent that
# refusal inside its C-1 min(); B-15 reports it and does not score it, because
# the question it was asking -- can the platform read a descriptor out of this
# jar at all -- is one the input tree already answers. So the preflight still
# requires 11, as the proof that the instrument is the instrument, and M1
# below is the fixture that asserts the refusal costs nothing.
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

# --- and the second instrument, which is this rung's own ---------------------
# C-5 DIVIDES BY A FROZEN FLOOR, NOT BY baseline/modnames.txt, AND THE FIRST
# READING OF THIS PREFLIGHT ASSERTED THE WRONG FILE.
#
# The rows were there; the KEYS were not the same keys. modnames.txt holds the
# twelve PUBLISHED ARTIFACT IDS B-15 pairs against the module descriptors --
# arquillian-core-api, arquillian-core-spi and their siblings. The Gradle probe
# C-5 runs answers with PROJECT NAMES off the settings file, and at this rung a
# project name is not its artifact id. Keying C-5's denominator on modnames.txt
# therefore matched nothing on a perfect tree: the golden lineage scored C-5
# 0.000000 while doing everything C-5 asks. A leg that reads 0 of 12 on a
# perfect tree is not a strict leg; it is a broken one.
#
# So the denominator is now target.co.projects -- a floor frozen off the tree,
# in the same file and read through the same tv() as the flag -- and this
# preflight reads THAT. The instrument follows the block: when the block
# changed channel, so did this.
#
# modnames.txt is still asserted, because B-15 still divides by it. It is just
# no longer asserted here on family C's behalf, and the two readings are
# separate lines below so that a future failure says which denominator moved.
echo
echo "== C-5's population, before its fixtures"
c5projw=$(sed -n 's/^target\.co\.projects=//p' "${TESTS}/baseline/target.txt" 2>/dev/null | head -1)
c5flagw=$(sed -n 's/^target\.co\.flag=//p' "${TESTS}/baseline/target.txt" 2>/dev/null | head -1)
if [ -n "${c5projw}" ] && [ "${c5projw}" -gt 0 ] 2>/dev/null && [ -n "${c5flagw}" ]; then
   note "C-5 divides by a frozen floor of ${c5projw} project(s) and greps for ${c5flagw}"
else
   bad "C-5's population did not reach the image:" \
       "target.co.projects '${c5projw:-}' (want a positive integer), flag '${c5flagw:-}' (want a non-empty string)"
fi
c5modn=$(grep -v '^#' "${TESTS}/baseline/modnames.txt" 2>/dev/null | grep -c . || true)
if [ "${c5modn}" = 12 ]; then
   note "baseline/modnames.txt still holds ${c5modn} artifact id(s) -- B-15's denominator, and no longer C-5's"
else
   bad "baseline/modnames.txt holds ${c5modn} row(s), want 12 -- B-15 divides by this"
fi

echo
echo "== extracting the eleven blocks and the hoisted bucket helpers from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min() gate at this step -- four of B-14's
# legs, B-15's service leg, B-18's three, and C-2, C-3 and C-4 -- would read one
# half alone.
grep -E '^(fact|frac|clamp01|minf)\(\) \{' "${SRC}" > "${TMP}/helpers.sh"
n=$(grep -c . "${TMP}/helpers.sh" || true)
[ "${n}" = 4 ] || bad "expected 4 helper one-liners in ${SRC}, found ${n}"
note "helpers: ${n} (fact frac clamp01 minf)"

# --- the bucket helpers, which are in NO block -------------------------------
# NINE FUNCTIONS IN THE GAP, and this section exists because the first run of
# step 10's rehearsal did not lift them. At step 09 cbuckets(), fcount(),
# capsum() and capwant() sat INSIDE the family-C block, so lifting family C
# lifted them with it and no separate step was needed. Step 10 hoisted them
# above B-16 and added cnames(); step 11 added pbuckets(), ptot(), psum() and
# pdep(); and the gap between B-15's last fact and B-16's banner holds all nine
# still. No block covers that gap.
#
# THREE BLOCKS READ THEM AND NONE OF THE THREE IS THIS RUNG'S. B-16 IS step
# 09's family C and reads four (cbuckets fcount capsum capwant); B-17 IS step
# 10's and reads two (cbuckets cnames); B-18 IS step 11's and reads six
# (cbuckets fcount pbuckets ptot psum pdep). THIS RUNG'S OWN FAMILY C READS
# NONE OF THEM, which is new -- at 09, 10 and 11 the live family was among the
# readers -- and the shipped banner above the gap says so in those words.
# Only cbuckets() is read by all three. One definition beats three that drift.
#
# THE COUNT IS ASSERTED AND SO IS THE READERSHIP, twenty lines down, because
# the two can come apart in opposite directions. Nine definitions with a
# family-C caller would mean this rung grew a method-site leg nobody wrote down;
# eight definitions with the same three readers would mean a helper was deleted
# out from under a call. Neither shows up in a total.
#
# Composed without them, every call is `command not found`; under `set -uo
# pipefail` and no `-e` the run continues, ${WORK}/cnt-*.txt, nam-*.txt and
# set-*.txt are never written, and the awk that reads them yields 0. That is
# the shape step 10's first run printed: B16 0.400000 and familyC 0.100000 on
# every one of its fifty-five rows, which is one missing `cat` and not
# fifty-five broken gates. (Both figures are as they were printed then, at a
# rung whose family C did read them and whose C-1 still carried 0.10. This
# paragraph is a record of a run and not a prediction -- at THIS rung the same
# omission would leave family C untouched and drop B16, B17 and B18 together,
# which is a different silhouette for the same fault.) The sixth trap says
# verify the instrument before the finding, and the instrument was what was
# wrong.
#
# So the gap is lifted by name as a TWELFTH piece and asserted to be exactly
# those nine definitions -- a tenth would be a helper this section is silently
# taking ownership of, and a missing one is a call the composed script cannot
# make.
#
# ptot() IS A ONE-LINER AND THE COUNT HAS TO KNOW IT. Eight of the nine are
# `name() {` on a line of their own; ptot() is `ptot() { awk ...; }` entire, so
# a count anchored on `^[a-z][a-z0-9_]*() {$` reads eight and calls a correct
# file wrong. The regex below accepts either shape, and the name loop under it
# is what actually says which nine.
awk '/^# -* bucket helpers, shared by B-16, B-17 and B-18$/ { on = 1 }
     /^# =* B-16$/                                          { on = 0 }
     on' "${SRC}" > "${TMP}/bhelp.sh"
head -1 "${TMP}/bhelp.sh" | grep -q 'bucket helpers, shared by B-16, B-17 and B-18$' \
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

# --- blocks 5..10: the six demoted families ----------------------------------
# Their banners are `=` rules and bare names, which is not B-10 through B-12's
# "step NN, held" spelling. The rule carries a meaning and all six have earned
# it: B-10, B-11 and B-12 are single gates held from single rungs, while B-13
# WAS step 06's family C in full, B-14 step 07's, B-15 step 08's, B-16 step
# 09's, B-17 step 10's and B-18 step 11's -- six whole families demoted, each
# renormalised over the legs that survive its rung's own C-1. A reader who sees
# `===` knows the block below it is a family and that a drift in it is a
# regression somewhere between two and seven rungs back. Anchored on exactly
# one spelling rather than on either, because the Dockerfile greps the same
# string: a rehearsal that accepted both would stop being able to say which one
# shipped.
#
# SIX IS THE NUMBER THE NINTH TRAP IS ABOUT. Every one of them is lifted and
# scored on every fixture in the table, including the fifteen rows written five
# rungs ago, because a leg that would quietly become unpassable at step 12 is
# exactly the leg nothing at step 12 would otherwise look at.
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

sed -n '/^# =* B-15$/,/^fact "B15.jpms-services=/p' "${SRC}" \
   > "${TMP}/block-b15.sh"
head -1 "${TMP}/block-b15.sh" | grep -q 'B-15$' \
   || bad "the B-15 block does not start at its banner"
tail -1 "${TMP}/block-b15.sh" | grep -q '^fact "B15.jpms-services=' \
   || bad "the B-15 block does not end at its own fact"

sed -n '/^# =* B-16$/,/^fact "B16.java17-constructs=/p' "${SRC}" \
   > "${TMP}/block-b16.sh"
head -1 "${TMP}/block-b16.sh" | grep -q 'B-16$' \
   || bad "the B-16 block does not start at its banner"
tail -1 "${TMP}/block-b16.sh" | grep -q '^fact "B16.java17-constructs=' \
   || bad "the B-16 block does not end at its own fact"

sed -n '/^# =* B-17$/,/^fact "B17.java21-constructs=/p' "${SRC}" \
   > "${TMP}/block-b17.sh"
head -1 "${TMP}/block-b17.sh" | grep -q 'B-17$' \
   || bad "the B-17 block does not start at its banner"
tail -1 "${TMP}/block-b17.sh" | grep -q '^fact "B17.java21-constructs=' \
   || bad "the B-17 block does not end at its own fact"
grep -q '^fact "B16.java17-constructs=' "${TMP}/block-b17.sh" \
   && bad "the B-17 block contains B-16's fact; the two banners were not told apart"

# --- block 10: B-18 ----------------------------------------------------------
# The newest of the demoted families and the reason this rehearsal composes
# eleven blocks. B-18 IS step 11's family C, minus that rung's class-file leg,
# renormalised over the 0.96 the other three shared -- 0.46 + 0.26 + 0.24. Its
# three legs are the doPrivileged removal, the unnamed catch parameter and the
# VarHandle conversion, and all three are source legs, which is why the whole
# family fits under one `measured` guard and why K17 reads it 0.000000 rather
# than partly.
#
# ITS BANNER IS ONE CHARACTER FROM B-17's AND THE END ANCHOR IS NOT. A range
# anchored on `/^# =* B-1` would take all six; both ends are spelled in full
# here and each is asserted after the lift, so a rehearsal that swallowed B-18
# into B-17 fails at the anchor rather than by scoring one family twice.
sed -n '/^# =* B-18$/,/^fact "B18.security-api-removal=/p' "${SRC}" \
   > "${TMP}/block-b18.sh"
head -1 "${TMP}/block-b18.sh" | grep -q 'B-18$' \
   || bad "the B-18 block does not start at its banner"
tail -1 "${TMP}/block-b18.sh" | grep -q '^fact "B18.security-api-removal=' \
   || bad "the B-18 block does not end at its own fact"
grep -q '^fact "B17.java21-constructs=' "${TMP}/block-b18.sh" \
   && bad "the B-18 block contains B-17's fact; the two banners were not told apart"

# --- block 11: family C ------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

BLOCKS="block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-b16 block-b17 block-b18 block-c"
for f in ${BLOCKS}; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The eleven are disjoint and in file order. This is the hoist, asserted: B-10
# through B-18 spend counters the first block reads, so the first block has to
# come first, and family C spends them again after all nine.
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
s10=$(grep -n '^# =* B-18$' "${SRC}" | head -1 | cut -d: -f1)
e10=$(grep -n '^fact "B18.security-api-removal=' "${SRC}" | head -1 | cut -d: -f1)
s11=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
# The gap is part of the order. Its banner must sit strictly between B-15's
# last fact and B-16's, which is the statement that the twelfth piece is
# disjoint from all eleven and is where the extraction above assumed it was. A
# rung that moved the helpers back inside family C would lift them twice and
# fail here rather than by defining cbuckets() from two files -- and at THIS
# rung that move would be doubly wrong, because family C calls none of them.
sbh=$(grep -n '^# -* bucket helpers, shared by B-16, B-17 and B-18$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5 e5 s6 e6 s7 e7 sbh s8 e8 s9 e9 s10 e10 s11; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ] && [ "${s5}" -lt "${e5}" ] \
   && [ "${e5}" -lt "${s6}" ] && [ "${s6}" -lt "${e6}" ] && [ "${e6}" -lt "${s7}" ] \
   && [ "${s7}" -lt "${e7}" ] && [ "${e7}" -lt "${sbh}" ] && [ "${sbh}" -lt "${s8}" ] \
   && [ "${s8}" -lt "${e8}" ] && [ "${e8}" -lt "${s9}" ] && [ "${s9}" -lt "${e9}" ] \
   && [ "${e9}" -lt "${s10}" ] && [ "${s10}" -lt "${e10}" ] && [ "${e10}" -lt "${s11}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < B-13 ${s5}..${e5} < B-14 ${s6}..${e6} < B-15 ${s7}..${e7} < helpers ${sbh} < B-16 ${s8}..${e8} < B-17 ${s9}..${e9} < B-18 ${s10}..${e10} < C ${s11}"
else
   bad "the eleven blocks and the helper gap are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5} ${e5} ${s6} ${e6} ${s7} ${e7} ${sbh} ${s8} ${e8} ${s9} ${e9} ${s10} ${e10} ${s11}"
fi

# ...and exactly one copy of each. The hoist's whole argument is that B-16,
# B-17 and B-18 read the SAME cbuckets(), so a block that carried its own
# would be two gates agreeing about a tree for two different reasons -- and the
# composed script, which cats the gap first, would silently prefer the later
# one. All nine names are checked against all eleven blocks and not just the
# three that call them: a copy of psum() inside B-14 would never be reached by
# B-14 and would still be the one B-18 got.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-b18 block-c; do
   for f in cbuckets fcount capsum capwant cnames pbuckets ptot psum pdep; do
      grep -qE "^${f}\(\) \{" "${TMP}/${blk}.sh" \
         && bad "${blk} defines ${f}() as well; the hoisted copy is meant to be the only one"
   done
done

# ...AND THE READERSHIP, WHICH IS THE HALF THE COUNT CANNOT SEE. Nine
# definitions in the gap says the gap is intact; it says nothing about who
# calls them, and at this rung who calls them is the fact that changed. B-16,
# B-17 and B-18 are the three readers and family C is not one, so a call from
# family C is asserted absent rather than assumed absent -- it would mean this
# rung had grown a leg that counts SITES IN A METHOD, and none of C-1 through
# C-5 is that: a census of class files, a set compared per file, held import
# lines counted by their reason, field declarations, and a JVM argument list.
#
# The other eight blocks are checked the same way and for the older reason: a
# call from B-10 would be an inherited gate reaching into a helper written six
# rungs above it.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-c; do
   for f in cbuckets fcount capsum capwant cnames pbuckets ptot psum pdep; do
      n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -cE "(^|[^a-z_])${f} " || true)
      [ "${n}" = 0 ] \
         || bad "${blk} calls ${f}() ${n} time(s); the bucket helpers are B-16's, B-17's and B-18's"
   done
done
for spec in 'block-b16 cbuckets fcount capsum capwant' \
            'block-b17 cbuckets cnames' \
            'block-b18 cbuckets fcount pbuckets ptot psum pdep'; do
   set -- ${spec}; blk=$1; shift
   for f in "$@"; do
      n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -cE "(^|[^a-z_])${f} " || true)
      [ "${n}" -gt 0 ] || bad "${blk} never calls ${f}(); it is one of its bucket helpers"
   done
done
note "the nine hoisted helpers are called by B-16, B-17 and B-18 and by no other block, family C included"

# --- what came back, and where it is allowed to be ---------------------------
# Step 08's rehearsal asserted that NO block read WANT_MAJOR or ${WORK}/scan.txt,
# because at that rung the class-file level did not move and the scan report was
# not staged. Both have been back since 09, and the assertion is inverted rather
# than deleted: exactly ONE block may read each, and it is family C, because
# C-1 is the leg that spends them. A WANT_MAJOR that reappeared in B-10 would be
# an inherited gate silently re-reading this rung's must-flip -- the ninth trap,
# in the direction that raises a score rather than lowering one.
#
# WANT_MINOR IS NEW AND IT IS HELD TO THE SAME RULE. C-1 reads eight bytes this
# rung where every ancestor read four: `majorminor.69.0` and not `major.69`,
# because a `--enable-preview` build stamps minor 65535 (JVMS 4.1) and satisfies
# a major-only reading completely. The minor is therefore part of the same
# channel and not a second one, and a block that reached for it would be doing
# what a block reaching for WANT_MAJOR would.
#
# ANCHORED ON THE SLASH for scan.txt. Family C writes ${WORK}/set-*.txt,
# ${WORK}/cnt-*.txt and ${WORK}/c5-rows.txt, and an unanchored `scan\.txt`
# matches the tail of a bucket name -- which reported this rehearsal's own
# bucket file as a leftover the first time the step-08 version of this loop ran.
# The real reference was always ${WORK}/scan.txt, so the separator is part of
# the pattern.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-b18; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'WANT_MAJOR' || true)
   [ "${n}" = 0 ] || bad "${blk} reads WANT_MAJOR ${n} time(s); only family C's C-1 may"
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'WANT_MINOR' || true)
   [ "${n}" = 0 ] || bad "${blk} reads WANT_MINOR ${n} time(s); only family C's C-1 may"
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c '/scan\.txt' || true)
   [ "${n}" = 0 ] || bad "${blk} reads scan.txt ${n} time(s); only family C's C-1 may"
done
nwm=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'WANT_MAJOR' || true)
nwn=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'WANT_MINOR' || true)
nsc=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c '/scan\.txt' || true)
if [ "${nwm}" -gt 0 ] && [ "${nwn}" -gt 0 ] && [ "${nsc}" -gt 0 ]; then
   note "family C alone reads WANT_MAJOR (${nwm}), WANT_MINOR (${nwn}) and \${WORK}/scan.txt (${nsc}); no other block does"
else
   bad "family C reads WANT_MAJOR ${nwm}, WANT_MINOR ${nwn} and scan.txt ${nsc} time(s); C-1 needs all three"
fi

# THE THIRD ARTIFACT CHANNEL, WHICH IS THIS RUNG'S OWN. C-5 asks Gradle what it
# would hand a test fork and reads the answer back out of ${WORK}/jvmargs.log,
# through the one `run_unprivileged` call in the whole of family C. No other
# block runs a build -- B-15 runs java, and that is a different thing: java over
# jars that are already there, with no project model and no daemon. A second
# `run_unprivileged` appearing in an inherited block would be a held gate that
# had grown a build invocation, which is a timeout risk in every job that runs
# this file and not only a scoring error.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-b18; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'jvmargs' || true)
   [ "${n}" = 0 ] || bad "${blk} reads jvmargs ${n} time(s); the probe is C-5's channel"
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'run_unprivileged' || true)
   [ "${n}" = 0 ] || bad "${blk} calls run_unprivileged ${n} time(s); only C-5 may run a build"
done
njv=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'jvmargs' || true)
nru=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'run_unprivileged' || true)
if [ "${njv}" -gt 0 ] && [ "${nru}" = 1 ]; then
   note "family C alone reads jvmargs (${njv}) and calls run_unprivileged exactly once: C-5's probe"
else
   bad "family C reads jvmargs ${njv} time(s) and calls run_unprivileged ${nru} time(s); C-5 needs the first and exactly one of the second"
fi

# ...and the jars are read by exactly the two blocks that have a leg on them.
# This is the other half of the channel split: at step 08 family C and B-12e
# shared modulejars.txt and a fixture could not separate them; here family C
# must not touch it at all, and K19 and K20 are the rows that say the jar
# channel moves without the scan channel or the probe channel moving with it.
for blk in block-b16 block-b17 block-b18 block-c; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'modulejars\.txt' || true)
   [ "${n}" = 0 ] \
      || bad "${blk} reads modulejars.txt ${n} time(s); the jars are B-12e's and B-15's channel"
done
note "none of B-16, B-17, B-18 or family C reads modulejars.txt: the jar, scan and probe channels are separate"

# ...and the compiler is called in one place, which is the hoist itself. Three
# calls and three tags -- 25, 24 and def -- and every block below reads the ONE
# report they produced. A srcgate() call inside a scoring block would be a
# second compile of the same tree inside a verifier that budgets for one.
for blk in block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-b18 block-c; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'srcgate' || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the tree is compiled once, above family B"
done
nsg=$(sed 's/#.*//' "${TMP}/block-src.sh" | grep -c 'srcgate' || true)
[ "${nsg}" = 3 ] \
   || bad "the source-AST block calls srcgate ${nsg} time(s); this rung compiles at 25, at 24 and at the default"
note "srcgate is called ${nsg} times and all three are in the hoisted block: 25, 24, def"

# Every gate this rehearsal claims to exercise has to be INSIDE one of the
# eleven extracts. A fact that moved out of range would still be published by
# the real verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok25='
inblock block-src 'gate.src.ok24='
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
# B-18's three legs, each named by the fact it publishes and by the baseline
# file it divides by -- and every one of those five baselines is a STEP 11 file
# that this rung neither re-derived nor re-recorded. THREE LEGS AND NOT FOUR,
# because what crossed the ladder is step 11's family C MINUS its C-1: the
# class-file major is one leg at every rung and it never travels, so B-18.2,
# B-18.3 and B-18.4 are here and step 11's C-1 is not. The weights are step
# 11's own, renormalised over the 0.96 the three of them shared there -- 0.46,
# 0.26 and 0.24 -- which is why B-18 can read 1.000000 on a tree that never
# once reached major 66, and why K17 reads it 0.000000 on a tree whose scan is
# perfect.
#
# B-18.2 IS THE ONE LEG IN THIS TASK WITH NO ARRIVAL TERM, and the absence is
# asserted below rather than left to the eye. doPrivileged() is removed and
# nothing arrives in its place: the body comes up a level and the wrapper is
# gone, so the leg is a departure half over the frozen call sites and a held
# half over two sub-populations that must survive the unwrapping. A
# `gate.b18.2.arrived` appearing here would mean somebody had given a removal
# an arrival term it cannot have, and every fixture that DELETED the wrapping
# method would start scoring as though it had converted it.
inblock block-b18 'B18.2.doprivileged-removal='
inblock block-b18 'B18.3.unnamed-catch='
inblock block-b18 'B18.4.varhandle='
inblock block-b18 'gate.b18.2.departed.want='
inblock block-b18 'gate.b18.2.held.want='
inblock block-b18 'gate.b18.2.held.ctor.want='
inblock block-b18 'gate.b18.2.held.setacc.want='
inblock block-b18 'gate.b18.2.depfrac='
inblock block-b18 'gate.b18.2.holdfrac='
inblock block-b18 'gate.b18.2.sites='
inblock block-b18 'gate.b18.2.sites.was='
inblock block-b18 'gate.b18.3.departed.want='
inblock block-b18 'gate.b18.3.arrived.want='
inblock block-b18 'gate.b18.3.held.want='
inblock block-b18 'gate.b18.3.depfrac='
inblock block-b18 'gate.b18.3.arrfrac='
inblock block-b18 'gate.b18.3.holdfrac='
inblock block-b18 'gate.b18.3.read='
inblock block-b18 'gate.b18.3.unnamed='
inblock block-b18 'gate.b18.3.unread='
inblock block-b18 'gate.b18.4.departed.want='
inblock block-b18 'gate.b18.4.arrived.want='
inblock block-b18 'gate.b18.4.held.want='
inblock block-b18 'gate.b18.4.depfrac='
inblock block-b18 'gate.b18.4.arrfrac='
inblock block-b18 'gate.b18.4.holdfrac='
inblock block-b18 'gate.b18.4.exec.want='
inblock block-b18 'gate.b18.4.exec.broken='
inblock block-b18 'gate.b18.4.handles='
inblock block-b18 'gate.b18.4.setaccessible='
inblock block-b18 'gate.b18.4.setaccessible.was='
inblock block-b18 'baseline/priv.txt'
inblock block-b18 'baseline/catchparam.txt'
inblock block-b18 'baseline/varhandle.txt'
inblock block-b18 'baseline/accessible.txt'
inblock block-b18 'baseline/vhids.txt'
n=$(sed 's/#.*//' "${TMP}/block-b18.sh" | grep -cE 'gate\.b18\.2\.(arrived|arrfrac)' || true)
[ "${n}" = 0 ] \
   || bad "B-18 publishes gate.b18.2.arrived/arrfrac ${n} time(s); doPrivileged() is removed, not replaced"
note "B-18.2 publishes a departure half and a held half and no arrival term"
# Family C's FIVE legs, on four baseline files, three of which did not exist a
# rung ago. FIVE AND NOT FOUR, which every rung of this ladder from 05 up until
# now has had: the class-file major is one of them and the other four are this
# rung's own -- the module import, the single-type imports it does and does not
# subsume, the one ThreadLocal whose lifetime is an extent, and the flag the
# test forks are started with.
#
# C-5 IS THE LEG WITH NO POPULATION FILE, and that is not an omission -- but
# the reason is not the one this comment first gave. It first said C-5's twelve
# projects were baseline/modnames.txt's twelve. They are not. modnames.txt
# holds published ARTIFACT IDS and the Gradle probe answers with PROJECT NAMES,
# and keying the leg on the wrong column scored the golden lineage 0 of 12 on a
# tree that had done everything C-5 asks.
#
# So C-5's denominator is `target.co.projects` -- one frozen integer in
# target.txt, next to the flag, read through the same tv(). A co.txt naming
# twelve project names would be a second list to keep in step with the reactor,
# and a floor is all the leg needs: the probe names the projects it finds, and
# `want` is max(seen, floor), so growing the reactor cannot shrink the divisor.
inblock block-c 'C1.level-'
inblock block-c 'C2.module-imports='
inblock block-c 'C3.import-subsumption='
inblock block-c 'C4.scoped-value='
inblock block-c 'C5.compact-headers='
inblock block-c 'gate.c1.classes='
inblock block-c 'gate.c1.at'
inblock block-c 'gate.c1.preview='
inblock block-c 'gate.c1=no-scan'
inblock block-c 'gate.c2.files='
inblock block-c 'gate.c2.files.want='
inblock block-c 'gate.c2.files.was='
inblock block-c 'gate.c2.imports='
inblock block-c 'gate.c2.imports.want='
inblock block-c 'gate.c2.clean='
inblock block-c 'gate.c2.clean.want='
inblock block-c 'gate.c2.spurious='
inblock block-c 'gate.c2.setfrac='
inblock block-c 'gate.c2.cleanfrac='
inblock block-c 'gate.c3.departed='
inblock block-c 'gate.c3.departed.want='
inblock block-c 'gate.c3.held='
inblock block-c 'gate.c3.held.want='
inblock block-c 'gate.c3.held.excused='
inblock block-c 'gate.c3.held.static='
inblock block-c 'gate.c3.held.nested='
inblock block-c 'gate.c3.held.ambiguous='
inblock block-c 'gate.c3.total='
inblock block-c 'gate.c3.total.was='
inblock block-c 'gate.c3.depfrac='
inblock block-c 'gate.c3.holdfrac='
inblock block-c 'gate.c4.departed='
inblock block-c 'gate.c4.arrived='
inblock block-c 'gate.c4.convert.want='
inblock block-c 'gate.c4.held='
inblock block-c 'gate.c4.held.want='
inblock block-c 'gate.c4.svcalls='
inblock block-c 'gate.c4.svcalls.want='
inblock block-c 'gate.c4.tldecls='
inblock block-c 'gate.c4.depfrac='
inblock block-c 'gate.c4.arrfrac='
inblock block-c 'gate.c4.holdfrac='
inblock block-c 'gate.c5.flag='
inblock block-c 'gate.c5.projects.floor='
inblock block-c 'gate.c5.projects.want='
inblock block-c 'gate.c5.probe='
inblock block-c 'gate.c5.tasks='
inblock block-c 'gate.c5.projects='
inblock block-c 'gate.c5.projects.seen='
inblock block-c 'gate.c5.forks='
inblock block-c 'gate.c5.forks.flagged='
inblock block-c 'gate.c5.projects.was='
inblock block-c 'scan.txt'
inblock block-c 'baseline/modimport.txt'
inblock block-c 'baseline/jdkimport.txt'
inblock block-c 'baseline/scopedvalue.txt'
# THE ABSENCE, STATED. `inblock` can only say a fact is where it belongs; it
# cannot say a fact nobody wrote is missing, and C-2's arrival term is
# precisely a fact nobody wrote. A module import is not an arrival in the sense
# C-3 and C-4 use the word: it IS the set C-2 compares, so a `gate.c2.arrived`
# would be counting the leg's own subject a second time under another name, and
# the file that added one module correctly and left another out would start
# scoring for the one it got.
n=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -cE 'gate\.c2\.(arrived|arrfrac)' || true)
[ "${n}" = 0 ] \
   || bad "family C publishes gate.c2.arrived/arrfrac ${n} time(s); C-2 compares a set per file, it does not count arrivals"
note "C-2 publishes a set half and a no-spurious half and no arrival term"

# ...and the five populations B-16 reads, the four B-17 reads, the five B-18
# reads and the four family C reads are four disjoint lists. EIGHTEEN FILES AND
# FOUR BLOCKS NOW, where a rung ago it was fourteen files and three: the ladder
# put a whole extra family in the held half, so the trap has one more direction
# to come from. A family C that still divided by varhandle.txt would be scoring
# this rung's work against the rung below's list, and a B-18 that reached for
# scopedvalue.txt would be scoring the rung below's work against this one's --
# and both would publish a plausible fraction while doing it.
b16base='ipattern strnl tbids sealed swarrow'
b17base='reftest chain swids seqfirst'
b18base='priv catchparam varhandle vhids'
cbase='modimport jdkimport scopedvalue'
crossbase() {
   for b in $2; do
      n=$(sed 's/#.*//' "${TMP}/$1.sh" | grep -c "baseline/${b}\.txt" || true)
      [ "${n}" = 0 ] || bad "$3 reads baseline/${b}.txt ${n} time(s); that is $4's population"
   done
}
crossbase block-b16 "${b17base}" "B-16"     "B-17"
crossbase block-b16 "${b18base}" "B-16"     "B-18"
crossbase block-b16 "${cbase}"   "B-16"     "family C"
crossbase block-b17 "${b16base}" "B-17"     "B-16"
crossbase block-b17 "${b18base}" "B-17"     "B-18"
crossbase block-b17 "${cbase}"   "B-17"     "family C"
crossbase block-b18 "${b16base}" "B-18"     "B-16"
crossbase block-b18 "${b17base}" "B-18"     "B-17"
crossbase block-b18 "${cbase}"   "B-18"     "family C"
crossbase block-c   "${b16base}" "family C" "B-16"
crossbase block-c   "${b17base}" "family C" "B-17"
crossbase block-c   "${b18base}" "family C" "B-18"
note "B-16's five baselines, B-17's four, B-18's four and family C's three are sixteen files and four blocks, disjoint"

# TWO FILES ARE DELIBERATELY NOT IN THOSE FOUR LISTS, because two scoring
# blocks read each on purpose, and a check that simply forbade sharing would
# have to be weakened to let them through. So each is named, and its exact two
# owners are asserted both ways: a third reader would be a leg scoring itself
# against another leg's evidence, and a drop to one would mean a leg had
# quietly stopped dividing by a frozen population.
#
#   baseline/accessible.txt   B-13 and B-18. Step 06 cut it and every rung
#      since staged it unchanged until step 11, whose C-4 converted four of the
#      14 `setAccessible` calls to VarHandle. The file has two halves with two
#      owners: B-13.3 divides by both, and B-18.2's third term intersects the
#      `setacc` half with priv.txt's files to ask whether the constructions
#      under an unwrapped `doPrivileged` survived the unwrapping.
#
#   baseline/modnames.txt     B-15 ALONE, AND THIS IS A DIRECTION CHANGE.
#      It was written here as B-15 and family C: B-15.2 dividing by it to ask
#      what the twelve published artifacts call themselves, C-5 dividing by the
#      same twelve rows to ask how many PROJECTS fork a test JVM with the flag.
#      The second half of that was wrong. The rows are keyed by artifact id --
#      arquillian-core-api and its siblings -- and the Gradle probe answers with
#      project names off the settings file, so C-5 matched nothing at all and
#      scored 0 of 12 on a tree that had set the flag everywhere. C-5 now
#      divides by `target.co.projects`, a frozen floor in target.txt.
#
#      The check below is therefore `oneowner` and not `twoowner`, and it is
#      kept rather than deleted precisely because the drop is the fix: a later
#      edit that reached for modnames.txt from family C again would be re-keying
#      the leg back to artifact ids, and this is the line that would catch it.
twoowner() {
   local base=$1 o1=$2 o2=$3 n c=0
   for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 \
              block-b15 block-b16 block-b17 block-b18 block-c; do
      n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c "baseline/${base}\.txt" || true)
      case "${blk}" in
         "${o1}"|"${o2}")
            [ "${n}" -gt 0 ] \
               || bad "${blk} does not read baseline/${base}.txt; it is one of its two owners"
            c=$(( c + 1 )) ;;
         *)
            [ "${n}" = 0 ] \
               || bad "${blk} reads baseline/${base}.txt ${n} time(s); only ${o1} and ${o2} may" ;;
      esac
   done
   [ "${c}" = 2 ] || bad "baseline/${base}.txt has ${c} owner(s); it must have exactly two"
   note "baseline/${base}.txt is read by ${o1} and ${o2} and by no other block"
}
twoowner accessible block-b13 block-b18

# The single-owner form. Same sweep, same eleven blocks, one permitted reader --
# so it says both halves of the direction change at once: B-15 still reads the
# file, and nobody else does.
oneowner() {
   local base=$1 o1=$2 n c=0
   for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 \
              block-b15 block-b16 block-b17 block-b18 block-c; do
      n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c "baseline/${base}\.txt" || true)
      case "${blk}" in
         "${o1}")
            [ "${n}" -gt 0 ] \
               || bad "${blk} does not read baseline/${base}.txt; it is its only owner"
            c=$(( c + 1 )) ;;
         *)
            [ "${n}" = 0 ] \
               || bad "${blk} reads baseline/${base}.txt ${n} time(s); only ${o1} may" ;;
      esac
   done
   [ "${c}" = 1 ] || bad "baseline/${base}.txt has ${c} owner(s); it must have exactly one"
   note "baseline/${base}.txt is read by ${o1} and by no other block"
}
oneowner modnames block-b15

# --- the guards, and which side of them each leg is on -----------------------
# THE INVERSION STEP 08 INTRODUCED, INHERITED, AND THEN TWO MORE. B-15's name
# leg reads the PUBLISHED JARS, and a tree whose sources the scorer could not
# compile against its own classpath may perfectly well have published twelve
# correctly named artifacts -- so B-15.2 sits ABOVE the guard and only B-15.3
# sits inside it. B-12e is outside its own block's guard for the same reason
# and has been since step 05.
#
# C-1 added the second one at step 09: it reads ${WORK}/scan.txt, written by
# arqgate.Scan over the class files the build left behind, and a build can
# leave class files behind that the scorer's own -source 25 probe will not
# accept. So C-1's guard asks about the SCAN and never about `measured`.
#
# AND C-5 ADDS THE THIRD, WHICH IS THIS RUNG'S. Its guard asks whether there
# is a flag to look for and a project list to divide by -- `[ -n "${c5flag}" ]
# && [ "${c5floor:-0}" -gt 0 ]` -- and neither is a fact about compiling
# either. A tree whose source gate produced nothing still has a build that can
# be asked what it would hand a test fork, and asking it is the whole of the
# leg. So K17 -- the tree nobody could compile -- scores C-1 at 1.000000 AND
# C-5 at 1.000000 while C-2, C-3 and C-4 publish the shell literal `0`, and
# familyC reads 0.040000, which is 0.02 + 0.02 and no more. That is not a
# leak: the class files were measured and the forks were asked, and both are
# facts about the tree regardless of what the scorer could reproduce.
#
# Asserted structurally here and scored on K17 below, because this is the kind
# of property a later edit moves by accident while making a guard tidier, and
# because the cost of getting it wrong is a silent 0.08 handed to a tree that
# never built -- or, in the other direction, a 0 handed to a tree that did.
#
# `guardend` takes the guard's opening line as a pattern rather than assuming
# one spelling: B-10 through B-14 open with `if [ -z "${measured}" ]` and take
# the else branch when there IS a report, while B-15, B-16's four legs,
# B-17's three, B-18's three and family C's three source legs open with
# `if [ -n ... ] && [ -s ... ]` and take the then branch. Two shapes, one
# meaning, and a helper that only knew the first would report every one of
# those guards as missing and pass.
#
# THE THIRD SPELLING IS B-18's AND IT CAME ACROSS WITH THE FAMILY. Two of
# B-18's three guards name TWO baseline files and are written across two
# lines, because B-18.2 and B-18.3 each read a population and then intersect
# it with another: B-18.2 asks which constructions under an unwrapped
# `doPrivileged` survived, and B-18.3 asks which catch parameters in a method
# that HAD one can go unnamed. Their opening line therefore ends in a
# backslash and not in `; then`, so a pattern composed as `... && [ -r
# /tests/baseline/<one file> ]; then` matches neither of them and reports both
# guards as missing while passing everything else. `cleg` takes an optional
# second baseline name below and composes the continued spelling when it is
# given one, then reads the line under the guard to check that the second file
# is the one named. `guardend` needs no change: it anchors on the opening line
# as a prefix and the `fi` it looks for is still at column 0.
#
# THIS RUNG'S OWN THREE SOURCE GUARDS ARE ALL ONE-FILE GUARDS, which is the
# first time since step 09 that family C has had none of the two-file kind.
# C-2 reads modimport.txt, C-3 jdkimport.txt and C-4 scopedvalue.txt, and none
# of the three intersects another population: every one of them is a whole
# frozen list with its own halves inside it. Passing the two-file spelling for
# one of them would compose a guard that is not in the file and report it
# missing, so the argument is simply not given, and the three-argument form is
# what says so.
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
COGUARD='if [ -n "${c5flag}" ] && [ "${c5floor:-0}" -gt 0 ]; then'

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

# --- family C: five legs, five guards, and two of them are not about compiling
# Each of C-2 through C-4 opens its own guard naming its own baseline file, so
# a missing population file costs ONE leg rather than three. They are asserted
# individually and in order: a leg whose guard named the wrong baseline would
# still score -- on a population that is not its own -- and the table's totals
# alone could not say which leg had been handed which list. B-16's four do the
# same thing over step 09's five files, B-17's three over step 10's four and
# B-18's three over step 11's five -- THIRTEEN guards over four blocks, where a
# rung ago it was ten over three.
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
# ...and neither does C-5's, which is the same argument about a different
# artefact and is new at this rung. The probe asks the BUILD what it would
# hand a test fork; the build can answer that whether or not the scorer's own
# javac accepted the sources.
c5gs=$(guardline block-c "${COGUARD}")
c5ge=$(guardend block-c "${COGUARD}")
c5line=$(guardline block-c 'fact "C5.compact-headers=')
if [ -n "${c5gs}" ] && [ -n "${c5ge}" ] && [ -n "${c5line}" ] \
   && [ "${c5line}" -gt "${c5ge}" ]; then
   note "C-5's guard is the flag and the project list (${c5gs}..${c5ge}) and its fact is published below it (${c5line})"
else
   bad "C-5's guard geometry has moved: flag guard ${c5gs:-?}..${c5ge:-?}, fact ${c5line:-?}"
fi
if sed -n "${c5gs},${c5ge}p" "${TMP}/block-c.sh" | grep -q 'measured'; then
   bad "C-5's guard reads \${measured}; what the build would fork is not a fact about the scorer's compile"
else
   note "C-5's guard does not read \${measured}: the probe is its own channel"
fi
# ...and its two population facts are published ABOVE the guard, which is what
# makes `gate.c5.flag` and `gate.c5.projects.floor` readable on a fixture whose
# C-5 never ran. They are the instrument's own reading, and an instrument that
# reported nothing when it found nothing would be indistinguishable from one
# that was not there.
#
# `floor` AND NOT `want`, and the difference is which side of the guard each
# one can be computed on. The floor is one frozen row of target.txt, read by
# tv() before anything runs. `want` is max(seen, floor), and `seen` is what the
# Gradle probe found -- so `want` does not exist until the probe has run, which
# is inside the guard by construction. Asking for it here asked for a fact that
# cannot be where the question put it.
for p in 'fact "gate.c5.flag=' 'fact "gate.c5.projects.floor='; do
   ln=$(guardline block-c "${p}")
   if [ -n "${ln}" ] && [ "${ln}" -lt "${c5gs}" ]; then
      :
   else
      bad "${p} is not above C-5's guard (${ln:-?} < ${c5gs})"
   fi
done
note "gate.c5.flag and gate.c5.projects.floor are published above C-5's guard"

# `cleg` takes the BLOCK as its first argument, because the same shape now has
# to be asserted over four different extracts. A helper hard-wired to block-c
# would have reported B-16's four guards as missing and passed, which is trap 6
# with the instrument silently pointed at the wrong file.
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
cleg block-c   C-2    modimport.txt  'fact "C2.module-imports='
cleg block-c   C-3    jdkimport.txt  'fact "C3.import-subsumption='
cleg block-c   C-4    scopedvalue.txt 'fact "C4.scoped-value='
# ...and step 09's four, step 10's three and step 11's three, in the blocks
# they were demoted into. THE BASELINE FILES ARE THE SAME FOURTEEN FILES and
# the guards are the same ten guards; only the variable names and the totals
# moved. That is the strongest statement this rehearsal can make about an
# inherited family -- not that it still scores, but that it still reads the
# same bytes. All three lists are spelled out rather than looped over one
# prefix, because B-16's legs are numbered 2..5 and B-17's and B-18's 2..4 and
# a loop that got the range wrong would quietly check three guards and report
# four.
cleg block-b16 B-16.2 ipattern.txt  'fact "B16.2.pattern-instanceof='
cleg block-b16 B-16.3 strnl.txt     'fact "B16.3.text-blocks='
cleg block-b16 B-16.4 sealed.txt    'fact "B16.4.sealed-hierarchies='
cleg block-b16 B-16.5 swarrow.txt   'fact "B16.5.arrow-switch='
cleg block-b17 B-17.2 reftest.txt   'fact "B17.2.type-patterns='
cleg block-b17 B-17.3 chain.txt     'fact "B17.3.pattern-switch='
cleg block-b17 B-17.4 seqfirst.txt  'fact "B17.4.sequenced-access='
cleg block-b18 B-18.2 priv.txt      'fact "B18.2.doprivileged-removal=' accessible.txt
cleg block-b18 B-18.3 catchparam.txt 'fact "B18.3.unnamed-catch='      priv.txt
cleg block-b18 B-18.4 varhandle.txt 'fact "B18.4.varhandle='

# THREE final assignments sit OUTSIDE their guards, one in each of three
# blocks, because each multiplies by a run-time scaling read from
# behavior.summary rather than from the report. That is why B-16.3, B-17.3 and
# B-18.4 print `0.000000` on K17 while the other twelve legs print the shell
# literal `0`, and it is the difference the table asserts as strings rather
# than as numbers.
#
# NOT ONE OF THE THREE IS IN FAMILY C, AND AT EVERY RUNG SINCE 09 AT LEAST ONE
# WAS -- step 09's C-3, step 10's C-3, step 11's C-4. This rung has no
# behaviour scaling anywhere in its own family, because none of its five
# changes has a run-time signature to record: an import that moved compiles or
# does not, a ScopedValue either binds or the field is still a ThreadLocal, and
# a fork either got the flag or did not. A rehearsal that carried step 11's
# membership forward would assert `0.000000` where this rung prints `0`, on
# K17's C-4 cell, and would be wrong about it while passing everything else.
#
# THEY ARE THREE DIFFERENT SCALINGS OVER THREE DIFFERENT LISTS, which is why
# all three are asserted and not just the shape. B-16.3 is scaled by
# baseline/tbids.txt's five document recordings, B-17.3 by baseline/swids.txt's
# five dispatch recordings and B-18.4 by baseline/vhids.txt's four field-access
# recordings; the TB*, SW* and VH* runs at the foot of this file are what say
# the three lists are not each other's. Two of the three are five long, so the
# arithmetic cannot say it for them and the id lists have to.
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
outguard block-b18 B-18.4 \
   "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/varhandle.txt ]; then" \
   'b18_4=$(awk -v d='

# ...and the other TWELVE are literals, asserted as literals, and asserted the
# strong way. Step 11's rehearsal checked only that a `<var>=0` line existed,
# which every one of the fifteen legs has -- each initialises above its own
# guard -- so the check could not tell a literal leg from a computed one and
# passed on both. What actually separates them is whether anything assigns to
# the variable BELOW the guard's `fi`, which is exactly what outguard() asserts
# for the three and what this loop asserts the absence of for the twelve. A leg
# that gained an awk initialiser would print `0.000000` where every K row in
# the table says `0`, and forty-odd rows would fail together on a change that
# is one line long.
#
# FIVE OF THE TWELVE ARE FAMILY C's, WHICH IS ALL OF IT, and that is the
# statement this rung's K17 row depends on.
litleg() {
   local blk=$1 var=$2 gpat=$3 ge n
   ge=$(guardend "${blk}" "${gpat}")
   if [ -z "${ge}" ]; then
      bad "${var}: its guard was not found in ${blk}"
      return
   fi
   grep -qxF "${var}=0" "${TMP}/${blk}.sh" \
      || bad "${var} is never initialised to the shell literal 0 in ${blk}"
   n=$(awk -v e="${ge}" -v v="${var}" 'NR > e && index($0, v "=") == 1' \
       "${TMP}/${blk}.sh" | grep -c . || true)
   [ "${n}" = 0 ] \
      || bad "${var} is assigned ${n} time(s) below its guard in ${blk}; K17's spelling would move"
}
MG='if [ -n "${measured}" ] && [ -s "${measured}" ] && [ -r /tests/baseline/'
litleg block-b16 b16_2 "${MG}ipattern.txt ]; then"
litleg block-b16 b16_4 "${MG}sealed.txt ]; then"
litleg block-b16 b16_5 "${MG}swarrow.txt ]; then"
litleg block-b17 b17_2 "${MG}reftest.txt ]; then"
litleg block-b17 b17_4 "${MG}seqfirst.txt ]; then"
litleg block-b18 b18_2 "${MG}priv.txt ] \\"
litleg block-b18 b18_3 "${MG}catchparam.txt ] \\"
litleg block-c   c1    "${SCANGUARD}"
litleg block-c   c2    "${MG}modimport.txt ]; then"
litleg block-c   c3    "${MG}jdkimport.txt ]; then"
litleg block-c   c4    "${MG}scopedvalue.txt ]; then"
litleg block-c   c5    "${COGUARD}"
note "twelve totals are the shell literal 0, family C's five among them; the three that are computed are B-16.3, B-17.3 and B-18.4"

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

# Nothing in any of the eleven blocks may run a compiler, a build or a
# container. They are arithmetic over a report, one pass of unzip over jars
# that already exist, one read of a scan file that already exists, ONE run of
# the module gate and ONE probe of the build -- that is what makes them
# rehearsable at all -- and the two things that touch the tree, srcgate() and
# run_unprivileged(), are both replaced below.
#
# `./gradlew` IS THE ONE THAT MOVED, and it moved into family C on purpose.
# Every rung from 05 to 11 could forbid it outright across every block; C-5
# cannot be written without it, because the whole content of the leg is what
# the build would hand a fork rather than what a build file claims. So the
# sweep forbids it in the ten blocks that have no business running one and
# requires it EXACTLY ONCE in family C, on a line that also names the probe
# task -- a second invocation there would be a second build inside a scorer
# that budgets for one, and one in any other block would be a held gate that
# had grown a build. Comments are stripped first: the blocks discuss javac and
# gradlew at length.
cat "${TMP}"/block-*.sh | sed 's/#.*//' \
   | grep -nE '(^|[;&|(]|\$\() *(javac|docker)([ "]|$)' \
   > "${TMP}/runs.txt" || true
if [ -s "${TMP}/runs.txt" ]; then
   bad "a block runs a compiler or a container:"
   sed 's/^/         /' "${TMP}/runs.txt"
else
   note "no block runs javac or docker"
fi
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 \
           block-b16 block-b17 block-b18; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" \
       | grep -cE '(^|[;&|(]|\$\(|[[:space:]])\./gradlew([ "]|$)' || true)
   [ "${n}" = 0 ] || bad "${blk} runs gradlew ${n} time(s); only C-5's probe may"
done
ngw=$(sed 's/#.*//' "${TMP}/block-c.sh" \
      | grep -cE '(^|[;&|(]|\$\(|[[:space:]])\./gradlew([ "]|$)' || true)
narq=$(sed 's/#.*//' "${TMP}/block-c.sh" \
       | grep -E '(^|[;&|(]|\$\(|[[:space:]])\./gradlew([ "]|$)' \
       | grep -c 'arqTestJvmArgs' || true)
if [ "${ngw}" = 1 ] && [ "${narq}" = 1 ]; then
   note "family C runs gradlew exactly once, on arqTestJvmArgs; no other block runs it"
else
   bad "family C's gradlew invocations: ${ngw} (want 1), of which arqTestJvmArgs: ${narq} (want 1)"
fi
# ...and java is split out the same way and for the same reason, one rung
# older. It is forbidden in TEN blocks and allowed EXACTLY ONCE in B-15, on a
# line that also names arqgate.Modules: a second java invocation there would be
# a second reading of the same jars, and a java in any other block would be a
# gate measuring something this rehearsal has not staged. FAMILY C HAS BEEN ON
# THE FORBIDDEN SIDE SINCE STEP 09 and stays there at this rung -- C-1 reads a
# file arqgate.Scan wrote during the build stage, and C-5 shells out to the
# BUILD and not to a JVM. Ten blocks that read files, one that runs a program
# and one that asks a build a question is the whole shape of this scorer.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b16 \
           block-b17 block-b18 block-c; do
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
# THREE tags at this step: `25`, `24` and `def`. The block asks for 25 first;
# if that succeeded it asks for 24, because a tree that still compiles at
# -source 24 has no Java 25 language feature load-bearing in it, which is a
# finding and not a failure; and it asks for `def` only when 25 failed. So a
# fixture's answers.txt carries all three and the shim answers whichever the
# block asks for.
#
# 24 AND NOT 23, AND THE NUMBER IS DERIVED RATHER THAN DECREMENTED. The
# control has to be the level at which exactly one of this rung's five legs
# stops compiling, and four of the five move no level at all: `ScopedValue` is
# a class in java.lang and binding one is an ordinary method call, so C-4 moves
# none; `-XX:+UseCompactObjectHeaders` is an argument javac never sees, so C-5
# moves none; the class-file major is a property of the emitted artifact rather
# than of what javac will parse, so C-1 moves none; and C-3's 43 holds are
# ordinary import declarations that have been legal since Java 1. What is left
# is C-2's MODULE IMPORT DECLARATION. JEP 511 made it standard in 25 after
# three releases of preview, so a tree that has done C-2's work cannot compile
# at -source 24 without --enable-preview. The control therefore reads the
# module imports and nothing else, which is why it cannot be read as a proxy
# for the whole rung and why C-3, C-4 and C-5 have no control of their own.
#
# The javac log is written only for the tag that FAILED, because the block only
# reads one when the probe said no -- it prints the compiler's complaint rather
# than twelve lines of `files=322` from the gate's own .env. A shim that wrote
# one for every tag would hand the block a 1.17 compile that both succeeded and
# left an error log behind, which is a tree no compiler produces.
#
# AND run_unprivileged(), REPLACED, WHICH IS THIS RUNG'S ADDITION. The real one
# is in /tests/helpers.sh and calls setpriv; what family C hands it is `env -C
# "${TREE}" timeout -s KILL 600 ./gradlew ... arqTestJvmArgs`, and ${TREE} is
# /nonexistent here. The stub swallows every argument and prints the fixture's
# jvmargs.txt, which is the probe's stdout and the fifth artefact. THE ABSENT
# FILE IS A STATE AND NOT AN OVERSIGHT: the stub returns 1 for it, which the
# block reads as `co_rc` non-zero and neither 137 nor 124, publishes as
# `gate.c5.probe=failed,...`, and scores at 0 because no row reached
# c5-rows.txt. That is a build nobody could ask -- a configuration-time hang, a
# gradlew that is not executable, a reactor that will not evaluate -- and Q20
# is the fixture that spends it.
#
# IT RETURNS 1 AND NOT 124 OR 137 ON PURPOSE. Those two are the timeout arm,
# which prints `gate.c5.probe=timeout` and a different line of report, and a
# stub that returned one of them would rehearse the timeout arm while the
# catalogue's prose called it a failure. The `timeout` arm is reachable only
# from a real clock and this file does not have one; it is asserted by reading
# the shipped block's `case` above rather than by running it.
cat > "${TMP}/shim.sh" <<'SHIM'
TREE=/nonexistent

# THE THREE CLASS-FILE CONSTANTS, BOUND HERE BECAUSE FAMILY C READS TWO OF
# THEM. test.sh assigns all three at the top of the file, far above the first
# block this rehearsal lifts, so nothing in the composed script would define
# them and the whole of family C would die on its first reference under
# `set -u` -- every C cell MISSING, which reads like five broken legs and is
# one missing binding.
#
# Read from the SAME baseline file test.sh reads, and not typed as 69, 0 and
# 66: the fixtures' scan.txt files were generated against that file too, so a
# literal here would be a second source of truth that agrees with the first
# only until someone moves the target major.
#
# WANT_MINOR IS THE ONE THIS RUNG ADDS, and it is the reason the binding is
# three lines rather than the two step 11 shipped. C-1 reads
# `majorminor.${WANT_MAJOR}.${WANT_MINOR}` out of scan.txt, so a shim that
# bound only the major would compose a key ending in a bare dot, match no line
# of any fixture's scan, and read c1have as empty on all ninety-two rows --
# including `golden`. Every C-1 cell would be 0 and the failure would look like
# a broken leg rather than an unbound variable.
#
# HOLD_MAJOR IS BOUND AND READ BY NOTHING HERE, deliberately. B-4 is the gate
# that reads `was=`, and B-4 is below this rehearsal's floor -- it is inside
# B-10's held family, scored from the report rather than from the scan. The
# `:?` on it is therefore an assertion about baseline/level.txt rather than a
# binding any lifted block needs: the two numbers are staged side by side in
# one file so neither gate reads the other's, and a file that lost one of them
# would break a gate this rehearsal cannot see.
WANT_MAJOR=$(sed -n 's/^major=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
WANT_MINOR=$(sed -n 's/^minor=//p' /tests/baseline/level.txt 2>/dev/null | head -1)
HOLD_MAJOR=$(sed -n 's/^was=//p'   /tests/baseline/level.txt 2>/dev/null | head -1)
: "${WANT_MAJOR:?the rehearsal cannot read major= from /tests/baseline/level.txt}"
: "${WANT_MINOR:?the rehearsal cannot read minor= from /tests/baseline/level.txt}"
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
run_unprivileged() {
   [ -f "${FIXDIR}/jvmargs.txt" ] || return 1
   cat "${FIXDIR}/jvmargs.txt"
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
      # THE FIFTH ARTEFACT IS NOT STAGED HERE and that is the difference
      # between it and the other four. jvmargs.txt is the probe's STDOUT, and
      # the block redirects the probe's stdout into ${WORK}/jvmargs.log itself;
      # staging a copy of the file into ${WORK} would put a second, unread
      # artefact beside the one the block wrote. The stub above reads it
      # straight out of ${FIXDIR}, which is also what makes its ABSENCE reach
      # the block as a non-zero exit rather than as an empty log -- two states
      # the block distinguishes and this rehearsal scores separately.
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" "${TMP}/bhelp.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-b11.sh" \
          "${TMP}/block-b12.sh" "${TMP}/block-b13.sh" "${TMP}/block-b14.sh" \
          "${TMP}/block-b15.sh" "${TMP}/block-b16.sh" "${TMP}/block-b17.sh" \
          "${TMP}/block-b18.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- ninety-two trees, and ninety-two of them different ----------------------
# Every fixture is a mutation of one of the two ends, and a mutation that did
# not land produces a directory identical to the end it came from -- which then
# scores exactly what that end scores and passes its row while asserting
# nothing. The seventh trap, in the fixtures rather than in the tree. why.txt
# is excluded because it is prose the generator writes and would make any two
# fixtures differ; everything the blocks actually read is hashed, INCLUDING
# scan.txt and jvmargs.txt, which is what separates L1, L2 and Q20 from the
# golden they are built out of.
#
# THE FIFTH ARTEFACT MADE THIS CHECK STRICTLY STRONGER AND ALSO STRICTLY
# NECESSARY. Q20 differs from the golden in one file and one file only -- the
# one it does not have -- and `find -type f` under a directory with no
# jvmargs.txt hashes a shorter list, which is exactly the difference. A digest
# taken over a fixed list of four names would have called Q20 a duplicate of
# the golden and passed, on the one row that rehearses the probe's failure arm.
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
# fixture  B10 B11 B12 B13 B14 B15 B16 B17 B18 | C1 C2 C3 C4 C5 | familyC
# Transcribed by hand from mkfix12.py's printed table. The two ends are the
# rows that carry the step's floor argument: `pinned` is the tree this rung is
# handed and must read every family-C leg at 0 with all nine held gates in
# full -- migration_prompt item 6's nop run -- and `golden` must read all
# fifteen in full, which is item 6's oracle.
#
# THE TRANSCRIPTION IS BY HAND ON PURPOSE AND IT IS THE POINT OF THE FILE. A
# rehearsal that computed these figures would agree with the scorer by
# construction and could not disagree with it about anything; and the verifier
# image has no python3 in it, so the generator could not be re-run here even if
# that were wanted. Fifteen figures per row, ninety-two rows, compared as
# STRINGS.
#
# `pinned`'s NINE FAMILY-B GATES ARE ALL 1.000000 AND THAT IS THE FLOOR
# ARGUMENT, not a mistake. This rung's input tree is step 11's output, so the
# twelve artifacts already declare their module names, the service lookups have
# already moved, step 09's four source constructs are in place, step 10's three
# are in place, and step 11's three are in place too -- every `doPrivileged`
# withdrawn, every catch parameter that can go unnamed unnamed, every
# reflective field access a VarHandle.
#
# THIS RUNG IS THEREFORE BACK ON THE LADDER'S NOMINAL FLOOR, 0.475000, and it
# is the first since step 10 that is. Step 11's was 0.463128 for one specific
# reason -- B-6's expectation is cut from the FINISHED tree, and step 11's own
# C-2 withdrew 42 `GetTcclAction` signatures the input tree still published, so
# family B read 0.997882 and family A 0.961661 on its nop. NOTHING THIS RUNG
# DOES MOVES A HELD EXPECTATION. C-2 rewrites import declarations, C-3 deletes
# them, C-4 moves one field's type, C-5 adds a JVM argument, and C-1 reads a
# class-file header; not one of those changes a signature, a package, a
# transcript or a service file, so family A is flat at 1.000000 and family B is
# flat at 1.000000 and the floor is
# 0.30 x 1 + 0.70 x 0.25 x 1 = 0.475000 exactly.
#
# WHICH MAKES THE MARGIN 0.025000, THE NARROWEST ON THE LADDER, and it is what
# sets C-1's and C-5's weights rather than the other way round. Item 6's bar is
# that the nop must not reach 0.5, so the legs such a tree collects are
# admissible only while 0.475 + 0.525w < 0.5, that is w < 0.047619 -- AND w IS
# THEIR SUM, NOT THE LARGEST OF THEM. An earlier draft read that bound one leg
# at a time and said "C-1 at 0.04 lands the level-only arm at 0.496000"; true
# of C-1 alone, wrong about the rung, because C-5 is a SECOND leg a tree that
# compiles nothing still collects -- the probe only asks what the build would
# hand a test fork -- and 0.04 + 0.04 put a two-flag tree at 0.517000, over the
# bar. C-1 and C-5 are 0.02 each for that reason: their sum is 0.04, a two-flag
# tree lands at 0.496000 and clears by 0.004, and the level-only arm reads
# 0.485500. NOTES 16.64 measures this rather than assuming it, and the
# `levelonly` calibration is the run that spends it. B-14's penalty block says
# the same thing in the shipped file, which is where a reader will meet it.
#
# FIFTEEN columns and not six, because at this rung neither family's legs are
# interchangeable and the totals hide which one moved. ELEVEN GROUPS OF ROWS
# ARE IDENTICAL IN ALL FIFTEEN CELLS, which is what a ladder with nine held
# legs and a five-leg family C costs, and every one of the eleven has a
# `factck` group under the table naming the channel that separates it:
#
#   all fifteen full  GOLDEN, M1 AND Q8 -- the group worth reading first,
#                     because two of its three rows are assertions that
#                     something did NOT score. M1 wrote a thirteenth malformed
#                     provider file and B-15 reports `gate.b15.mod.derivable=10`
#                     beside B15 1.000000, which is the evidence that step 08's
#                     derivable term really is gone rather than merely
#                     unmentioned. Q8 added a class the population has never
#                     heard of and gave it a module import: `gate.c2.imports`
#                     reads 189 against a want of 188 and is published rather
#                     than scored, because a leg that charged for the extra
#                     would make writing a class cost score and a leg that
#                     credited it would let a candidate raise C-2 by writing
#                     files. NEITHER ROW IS DISTINGUISHABLE FROM THE ORACLE BY
#                     ANY CELL IN THIS TABLE, and that is precisely their
#                     content.
#   B14 0.500000      K15 and K16 -- and the two are closer than any other pair
#                     here: every one of B-14's twenty gate facts agrees
#                     between them, `gate.src.penalty` reads 0.500000 on both,
#                     and ONE fact in the whole run differs. K15 added a
#                     @SuppressWarnings site and the suppression term publishes
#                     no count -- it prints its line and halves -- while K16's
#                     `Class[]::new` fires the toArray term, so
#                     `gate.src.taraw` reads 0 and 1. Two different mistakes,
#                     one multiplier, one distinguishing byte.
#   B15 0.437500      M4 and M5 -- and K17 reaches the same figure from a third
#                     direction, which makes it the widest collision in the
#                     table even though K17's other columns separate it. M4 has
#                     a report whose service lookups arrived in no module at
#                     all, so gate.b15.arrived is 0.000000 with the other two
#                     halves full; M5's structural halves are full with all 69
#                     recorded service behaviours differing; K17 has no report
#                     at all, so gate.b15.arrived, .departed and .held are the
#                     shell literal 0.
#   B16 0.711111      L6 and L14 -- the text block deleted against one left in
#                     the file but OUTSIDE the method that had to write it.
#   B16 0.955556      L12 and L13 -- a reverted switch against a colon arm left
#                     standing beside a new arrow one.
#   B17 0.958333      N10 and N11 -- gate.b17.4.depfrac against .arrfrac.
#   B18 0.937500      P10 and P11 -- gate.b18.4.depfrac against .arrfrac, over
#                     an arrival denominator that is twice the departure's:
#                     two records per converted bucket, `privateLookupIn` and
#                     `unreflectVarHandle`.
#
# AND FOUR IN THIS RUNG'S OWN FAMILY, which is more than any rung below and is
# what a family of five legs at 0.02, 0.38, 0.33, 0.25 and 0.02 does to a table
# of totals:
#
#   familyC 0.997778  Q3 and Q6 -- 187 declarations against 188. Same score,
#                     same denominator, and `gate.c2.imports` is the channel:
#                     a leg that counted declarations instead of comparing sets
#                     would call one of the two correct.
#   familyC 0.620000  Q4 and Q7 -- the blanket `import module java.base;` in
#                     all 322 files against not one module import in the tree.
#                     `gate.c2.setfrac` reads 0.888889 and 0.000000 and
#                     `gate.c2.cleanfrac` reads 0.000000 and 1.000000. One tree
#                     did none of this leg and the other did all of it wrongly,
#                     and the min() is the only thing that says which.
#   familyC 0.750000  Q14, Q15 and Q16 -- a min() of three halves over
#                     denominators 1, 1 and 7 has exactly three ways to fall,
#                     and gate.c4.departed/.arrived/.held read 0/1/7, 1/0/7 and
#                     1/1/0.
#   familyC 0.998333  Q18 and Q19 -- eleven projects flagged by hand against one
#                     project with two Test tasks and only one of them flagged.
#                     `gate.c5.tasks` reads 12 and 13 and `gate.c5.forks.flagged`
#                     11 and 12. An `any` over the rows would have scored Q19 at
#                     1.000000, which is why the tally is per project and per
#                     task rather than a grep over the log.
#
# AND ONE MORE THAT IS NOT A CELL COLLISION AT ALL BUT A TOTAL ONE, which is
# this rung's own addition to the ladder: L2, Q1 AND Q20 ALL READ familyC
# 0.980000 THROUGH THREE DIFFERENT LEGS. L2 has no scan, so C-1 is the shell
# literal 0 under `gate.c1=no-scan`; Q1 compiled the whole tree behind
# `--enable-preview`, so C-1 is 0.000000 with `gate.c1.preview=730` and
# `major.69` reading full; and Q20's probe never answered, so C-5 is 0.000000
# with `gate.c5.probe=failed` and `gate.c5.tasks=0`. THREE WAYS TO LOSE ONE
# 0.02 LEG, and no two of them are the same leg. Their C columns differ, so the
# table separates them -- but the total does not, and the total is what a
# reader of a scored run sees first.
#
# TWO COLLISIONS ARE IN A SINGLE COLUMN AND NOT IN THE ROW, and they are named
# here because a reader comparing columns will meet them: K19 and K20 both read
# B12 0.850000 -- K19 published its twelve named jars with none of the four
# markers in them, K20 published nothing at all, so B-12e is 0.000000 there and
# the shell literal 0 here under `gate.cp=no-jars` -- and they part company at
# B15, 1.000000 against 0.562500, because K19's manifests are untouched. P12
# and P15 both read B13 0.980000, B-13.3 counting nine of its ten surviving
# `setAccessible` calls on a field B-18.4 says must not have moved, and they
# part company at B18, 0.975000 against 0.918750, because P15 also breaks one
# of the four recorded access failures. Both pairs are separated by the table
# itself and neither needs a fact; naming them is what stops the next reader
# looking for the fact that is missing.
#
# AND FOUR ZERO COLLISIONS AGAINST `pinned`, one per source leg, which are not
# in the lists above because they are not two ways of doing something wrong:
# Q1 with pinned at C1 0.000000, Q4 and Q7 with it at C2, Q9 at C3,
# Q14/Q15/Q16 at C4 and Q20 at C5. `pinned` reaches every one of them by having
# done nothing at all and the Q rows by having done one thing wrongly, which is
# exactly the distinction item 6's nop run exists to make and exactly why the
# groups below assert the gate facts and not the cells.
TABLE=$(cat <<'EOF'
pinned   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.000000 0.000000 0.000000 0.000000 0.000000  0.000000
golden   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K1       1.000000 1.000000 1.000000 1.000000 1.000000 0.930122 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K2       1.000000 1.000000 1.000000 1.000000 1.000000 0.562500 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K3       1.000000 1.000000 1.000000 1.000000 1.000000 0.866319 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K4       1.000000 1.000000 1.000000 1.000000 0.955555 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K5       1.000000 1.000000 1.000000 1.000000 0.987879 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K6       1.000000 1.000000 1.000000 1.000000 0.927273 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K7       1.000000 1.000000 1.000000 1.000000 0.995607 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K8       1.000000 1.000000 1.000000 1.000000 0.932540 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K9       1.000000 1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K10      1.000000 1.000000 1.000000 1.000000 0.916667 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K11      1.000000 1.000000 1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K12      1.000000 1.000000 1.000000 1.000000 0.700000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K13      1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K14      1.000000 1.000000 1.000000 1.000000 0.800000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K15      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K16      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K17      0 0 0.000000 0.000000 0.000000 0.437500 0.000000 0.000000 0.000000  1.000000 0 0 0 1.000000  0.040000
K18      1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K19      1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
K20      1.000000 1.000000 0.850000 1.000000 1.000000 0.562500 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M2       1.000000 1.000000 1.000000 1.000000 1.000000 0.718750 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M3       1.000000 1.000000 1.000000 1.000000 1.000000 0.948864 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M4       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M5       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
M6       1.000000 1.000000 1.000000 1.000000 1.000000 0.926630 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.958904 1.000000 1.000000 1.000000 1.000000  0.999178
L2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0 1.000000 1.000000 1.000000 1.000000  0.980000
L3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.843137 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.777778 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.904762 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.711111 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.876191 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.884444 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.818182 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.963636 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.955556 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.955556 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
L14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.711111 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.959135 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.960784 0.959135 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.949405 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.822917 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.796875 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.850962 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.661458 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.729167 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.756010 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.186699 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N15      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.322917 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.958333 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.958333 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.951389 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
N13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.979167 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.928482  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P2       1.000000 1.000000 1.000000 0.969231 1.000000 1.000000 1.000000 1.000000 0.889423  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P3       1.000000 1.000000 1.000000 0.960000 1.000000 1.000000 1.000000 1.000000 0.876282  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.949938  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.963141  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.961310  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.941964  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.973448  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.936275  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.937500  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.937500  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P12      1.000000 1.000000 1.000000 0.980000 1.000000 1.000000 1.000000 1.000000 0.975000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.750000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.875000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
P15      1.000000 1.000000 1.000000 0.980000 1.000000 1.000000 1.000000 1.000000 0.918750  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
Q1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.000000 1.000000 1.000000 1.000000 1.000000  0.980000
Q2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  0.945205 1.000000 1.000000 1.000000 1.000000  0.998904
Q3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.994152 1.000000 1.000000 1.000000  0.997778
Q4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.000000 1.000000 1.000000 1.000000  0.620000
Q5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.801325 1.000000 1.000000 1.000000  0.924504
Q6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.994152 1.000000 1.000000 1.000000  0.997778
Q7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 0.000000 1.000000 1.000000 1.000000  0.620000
Q8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 1.000000  1.000000
Q9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.000000 1.000000 1.000000  0.670000
Q10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.418605 1.000000 1.000000  0.808140
Q11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.627907 1.000000 1.000000  0.877209
Q12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.953488 1.000000 1.000000  0.984651
Q13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 0.911504 1.000000 1.000000  0.970796
Q14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.000000 1.000000  0.750000
Q15      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.000000 1.000000  0.750000
Q16      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.000000 1.000000  0.750000
Q17      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 0.714286 1.000000  0.928571
Q18      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 0.916667  0.998333
Q19      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 0.916667  0.998333
Q20      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000  1.000000 1.000000 1.000000 1.000000 0.000000  0.980000
EOF
)

echo
echo "== running the composed blocks against each fixture"
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture B10 B11 B12 B13 B14 B15 B16 B17 B18 C1 C2 C3 C4 C5 familyC
while read -r fix w10 w11 w12 w13 w14 w15 w16 w17 w18 wc1 wc2 wc3 wc4 wc5 wfc; do
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
   g18=$(fv "${fix}" 'B18\.security-api-removal')
   # C-1's fact carries the major in its NAME -- `C1.level-69` -- because the
   # number a rung flips to is the one thing about that rung a reader should
   # not have to look up. Spelled out here rather than globbed, so a rung that
   # moved the level without moving this file fails on the key rather than
   # silently reading MISSING into every row. It caught step 11: the step-09
   # spelling was `C1.level-61`, and 61 was two majors behind the one the tree
   # arrived at.
   #
   # THE OTHER FOUR MOVED WHOLESALE THIS RUNG and none of them kept its name.
   # Step 11's C-2, C-3 and C-4 were `doprivileged-removal`, `unnamed-catch`
   # and `varhandle`; they are B-18's now, read one line up, and this rung's
   # own four are `module-imports`, `import-subsumption`, `scoped-value` and
   # `compact-headers`. A rehearsal carried across unchanged would have read
   # the held family's three facts into the C columns and passed every row in
   # the table while scoring nothing this rung wrote.
   #
   # AND THERE IS A FIFTH TO READ NOW, which no rung below had: five legs is
   # the widest family C on the ladder, and `C5.compact-headers` is the only
   # one of the five that is not a fact about source text.
   gc1=$(fv "${fix}" 'C1\.level-69')
   gc2=$(fv "${fix}" 'C2\.module-imports')
   gc3=$(fv "${fix}" 'C3\.import-subsumption')
   gc4=$(fv "${fix}" 'C4\.scoped-value')
   gc5=$(fv "${fix}" 'C5\.compact-headers')
   gfc=$(fv "${fix}" familyC)
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${g10:-MISSING}" "${g11:-MISSING}" "${g12:-MISSING}" \
      "${g13:-MISSING}" "${g14:-MISSING}" "${g15:-MISSING}" "${g16:-MISSING}" \
      "${g17:-MISSING}" "${g18:-MISSING}" \
      "${gc1:-MISSING}" "${gc2:-MISSING}" "${gc3:-MISSING}" \
      "${gc4:-MISSING}" "${gc5:-MISSING}" "${gfc:-MISSING}"
   [ "${g10}" = "${w10}" ] || bad "${fix}: B10 want ${w10} got ${g10:-MISSING}"
   [ "${g11}" = "${w11}" ] || bad "${fix}: B11 want ${w11} got ${g11:-MISSING}"
   [ "${g12}" = "${w12}" ] || bad "${fix}: B12 want ${w12} got ${g12:-MISSING}"
   [ "${g13}" = "${w13}" ] || bad "${fix}: B13 want ${w13} got ${g13:-MISSING}"
   [ "${g14}" = "${w14}" ] || bad "${fix}: B14 want ${w14} got ${g14:-MISSING}"
   [ "${g15}" = "${w15}" ] || bad "${fix}: B15 want ${w15} got ${g15:-MISSING}"
   [ "${g16}" = "${w16}" ] || bad "${fix}: B16 want ${w16} got ${g16:-MISSING}"
   [ "${g17}" = "${w17}" ] || bad "${fix}: B17 want ${w17} got ${g17:-MISSING}"
   [ "${g18}" = "${w18}" ] || bad "${fix}: B18 want ${w18} got ${g18:-MISSING}"
   [ "${gc1}" = "${wc1}" ] || bad "${fix}: C1 want ${wc1} got ${gc1:-MISSING}"
   [ "${gc2}" = "${wc2}" ] || bad "${fix}: C2 want ${wc2} got ${gc2:-MISSING}"
   [ "${gc3}" = "${wc3}" ] || bad "${fix}: C3 want ${wc3} got ${gc3:-MISSING}"
   [ "${gc4}" = "${wc4}" ] || bad "${fix}: C4 want ${wc4} got ${gc4:-MISSING}"
   [ "${gc5}" = "${wc5}" ] || bad "${fix}: C5 want ${wc5} got ${gc5:-MISSING}"
   [ "${gfc}" = "${wfc}" ] || bad "${fix}: familyC want ${wfc} got ${gfc:-MISSING}"
done <<EOF
${TABLE}
EOF

# --- the facts that tell the equal-scoring fixtures apart --------------------
# Several fixtures share a total on purpose, because they are the pairs the
# gates exist to separate and a total alone cannot show that the SEPARATION
# happened -- only that the arithmetic came out the same. Each assertion below
# names the channel that distinguishes one from its twin.
#
# THIS RUNG NEEDS THE CHANNEL MORE THAN ANY BELOW IT, and mkfix12.py's own
# catalogue note says why: a family of five legs at 0.02, 0.38, 0.33, 0.25 and
# 0.02 produces collisions a family of four never did. Q3 and Q6 both land on
# 0.997778, Q4 and Q7 on 0.620000, Q14, Q15 and Q16 on 0.750000, Q18 and Q19 on
# 0.998333, and L2, Q1 and Q20 all on 0.980000 by three different routes
# through three different legs. Eleven more pairs collide in a single column.
# The TABLE above proves the arithmetic; only the assertions below prove the
# gates.
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

# THE ONE FACT factck CANNOT READ, and the reason it gets a helper of its own.
# C-5's probe publishes `gate.c5.probe=<state>,seconds=<n>,limit=<n>` -- three
# fields on one line, because the elapsed seconds and the limit are the two
# numbers a reader needs when the state is `timeout` and there is nowhere else
# in the transcript to put them. mkfix12.py models the STATE alone and calls it
# `c5.probe.state`, which is the right thing for a model that has no clock: a
# generator that had to predict an elapsed-seconds figure would be asserting
# the speed of the machine the image happens to build on.
#
# So the rehearsal asserts the first comma-separated field and nothing else.
# `fv` hands back the whole value and `cut -d, -f1` takes the state; the two
# numbers beside it are printed for a human and scored by no one.
probeck() {
   local fix=$1 want=$2 got
   if grep -qE '^gate\.c5\.probe=' "${TMP}/facts-${fix}.txt"; then
      got=$(fv "${fix}" 'gate\.c5\.probe' | cut -d, -f1)
   else
      got=MISSING
   fi
   if [ "${got}" = "${want}" ]; then
      printf '  ok   %-9s %-32s %s\n' "${fix}" "gate.c5.probe(state)" "${got}"
   else
      bad "${fix}: gate.c5.probe state want ${want} got ${got:-MISSING}"
   fi
}

# THE THIRD HELPER IS A CROSS-FIXTURE ONE AND IT IS NEW AT THIS RUNG. factck
# and probeck each read one fixture; sep() reads two and asserts that a named
# key does NOT read the same on both. It is what turns "these two rows are
# identical in the table on purpose" from a remark into a check: the row loop
# in the section above proves the arithmetic collides, and this proves the
# gates underneath it did not. A key that stopped separating a pair -- because
# a population shrank, or because a fact line moved inside a guard -- reads the
# same on both and fails here while every total still agrees.
sep() {
   local a=$1 b=$2 key=$3 va vb
   if grep -qE "^${key}=" "${TMP}/facts-${a}.txt"; then va=$(fv "${a}" "${key}"); else va=MISSING; fi
   if grep -qE "^${key}=" "${TMP}/facts-${b}.txt"; then vb=$(fv "${b}" "${key}"); else vb=MISSING; fi
   if [ "${va}" != "${vb}" ]; then
      printf '  ok   %-6s|%-6s %-30s %s | %s\n' "${a}" "${b}" "${key}" "${va}" "${vb}"
   else
      bad "${a}/${b}: ${key} reads ${va} on both -- it does not part them"
   fi
}

# --- pinned: the nop, in full ------------------------------------------------
# This rung's input tree is the step-11 golden -- specifically the round-2
# blind tree, which NOTES 16.53 tabulated against step 11's own oracle and
# found equal to the digit. Every family-C leg that carries weight reads zero
# on it and every inherited gate reads full. That is migration_prompt item 6's
# nop run, asserted at image build time rather than only in a job.
#
# Every `want` here is the size of a frozen set or a baseline counter, and
# reading them off the facts is how this rehearsal knows the baseline files
# reached the image at all -- a missing set file makes the want 0 and every
# fraction over it 0, which scores a tree that did nothing the same as one
# that did everything.
#
# FOUR INHERITED FAMILIES READ 1.000000 ON THE NOP AND THAT IS ONE MORE THAN
# THE RUNG BELOW HAD. B-15 is step 08's family C, B-16 is step 09's, B-17 is
# step 10's, B-18 is step 11's, and the tree this rung is handed IS step 11's
# finished answer: twelve artifacts declare their module names, seventeen
# instanceof sites carry a pattern variable, eleven event roots are sealed,
# thirteen reflective type tests are type patterns, ten if-chain arms are four
# pattern switches, sixty-seven privileged wrappers are gone, thirty-five catch
# parameters are `_`, four `setAccessible` calls are VarHandles. An inherited
# gate that charged the step-12 candidate for the step-11 candidate's work
# would be the worse verifier. The price is that family B gives the floor no
# help whatever.
#
# AND THIS TIME IT GIVES IT NONE AT ALL, which is the arithmetic that makes
# this rung's floor the tightest on the ladder. Step 11's B-6 expectation was
# cut from the FINISHED tree, so its nop read 0.997882 on family B and
# 0.961661 on family A and its floor came out at 0.463128. Nothing this rung
# does moves a held expectation: C-2 rewrites import declarations, C-3 deletes
# them, C-4 moves one field's type, C-5 adds a JVM argument, C-1 reads a
# class-file header. Not one of those changes a signature, a package, a
# transcript or a service file, so family A and family B BOTH read 1.000000
# here and the floor is 0.30 x 1 + 0.70 x 0.25 x 1 = 0.475000 exactly, with
# 0.025000 of margin under item 6's bar. Family C carries the entire
# separation on its own and has 0.025 to do it in. NOTES 16.6x.
#
# THE CONTROL READS `yes` HERE AND ON EXACTLY ONE OTHER FIXTURE. gate.src.ok24
# is published and scored by nothing; it says whether the tree still compiles
# at -source 24, and JEP 511 made the module import declaration standard in 25
# after three releases of preview, so it reads `no` on every tree that declares
# one. The nop declares none. Q7 -- the golden with all 188 module imports
# taken back out -- declares none either, and those two are the whole of the
# `yes` column. K17 is the third answer: it compiles at no level at all and the
# control is never reached, which is why ok24 is MISSING there and okdef is
# what gets published instead.
factck pinned gate.src.ok25 yes
factck pinned gate.src.ok24 yes
# ...and B-15 leg by leg, on the tree that has already done step 08's work:
# twelve Automatic-Module-Name headers, the service lookup moved off the
# hand-rolled scan, sixty-nine recorded service behaviours intact.
factck pinned gate.b15.arrived             1.000000
factck pinned gate.b15.departed            1.000000
factck pinned gate.b15.exec                1.000000
factck pinned gate.b15.exec.broken         0
factck pinned gate.b15.exec.want           69
factck pinned gate.b15.held                1.000000
factck pinned gate.b15.mod.declared        12
factck pinned gate.b15.mod.derivable       11
factck pinned gate.b15.mod.total           12
factck pinned gate.b15.names               12
# ...and B-16, step 09's, with the two figures this rung inherited rather than
# re-cut. `sites.want` is 24 and `converted.want` is 17 because step 11's C-2
# deleted the seven `SecurityActions#getConstructor` ladders the older
# denominator counted; baseline/ipattern.txt was re-cut on step 11's golden and
# has not moved since. The measured `sites` reads 24 here too, unlike one rung
# down, because the seed IS that golden.
factck pinned gate.b16.2.converted         17
factck pinned gate.b16.2.converted.want    17
factck pinned gate.b16.2.convfrac          1.000000
factck pinned gate.b16.2.held              7
factck pinned gate.b16.2.held.want         7
factck pinned gate.b16.2.holdfrac          1.000000
factck pinned gate.b16.2.sites             24
factck pinned gate.b16.2.sites.want        24
factck pinned gate.b16.2.totfrac           1.000000
factck pinned gate.b16.3.exec              1.000000
factck pinned gate.b16.3.exec.broken       0
factck pinned gate.b16.3.exec.want         5
factck pinned gate.b16.3.held              7
factck pinned gate.b16.3.held.want         7
factck pinned gate.b16.3.holdfrac          1.000000
factck pinned gate.b16.3.moved             1
factck pinned gate.b16.3.moved.want        1
factck pinned gate.b16.3.movefrac          1.000000
factck pinned gate.b16.3.textblocks        1
factck pinned gate.b16.4.closedfrac        1.000000
factck pinned gate.b16.4.decl              11
factck pinned gate.b16.4.nonsealed         0
factck pinned gate.b16.4.open              11
factck pinned gate.b16.4.open.want         11
factck pinned gate.b16.4.openfrac          1.000000
factck pinned gate.b16.4.permits           35
factck pinned gate.b16.4.roots             11
factck pinned gate.b16.4.roots.want        11
factck pinned gate.b16.5.arrow             6
factck pinned gate.b16.5.colon             0
factck pinned gate.b16.5.expr              2
factck pinned gate.b16.5.files             2
factck pinned gate.b16.5.files.want        2
factck pinned gate.b16.5.size              6
# ...and B-17, step 10's: thirteen reflective type tests that became type
# patterns and fourteen that had to stay reflective, ten if-chain arms that
# became four pattern switches and two chains that had to stay chains, seven
# first-element reads that became getFirst() and nine that could not.
factck pinned gate.b17.2.arrfrac           1.000000
factck pinned gate.b17.2.arrived           13
factck pinned gate.b17.2.arrived.want      13
factck pinned gate.b17.2.departed          13
factck pinned gate.b17.2.departed.want     13
factck pinned gate.b17.2.depfrac           1.000000
factck pinned gate.b17.2.held              14
factck pinned gate.b17.2.held.want         14
factck pinned gate.b17.2.holdfrac          1.000000
factck pinned gate.b17.2.patterns          24
factck pinned gate.b17.2.reftests          22
factck pinned gate.b17.2.swpatterns        10
factck pinned gate.b17.3.arrfrac           1.000000
factck pinned gate.b17.3.arrived           10
factck pinned gate.b17.3.arrived.want      10
factck pinned gate.b17.3.departed          10
factck pinned gate.b17.3.departed.want     10
factck pinned gate.b17.3.depfrac           1.000000
factck pinned gate.b17.3.exec              1.000000
factck pinned gate.b17.3.exec.broken       0
factck pinned gate.b17.3.exec.want         5
factck pinned gate.b17.3.held              2
factck pinned gate.b17.3.held.want         2
factck pinned gate.b17.3.holdfrac          1.000000
factck pinned gate.b17.3.ipleft            0
factck pinned gate.b17.3.nullcase          1
factck pinned gate.b17.3.patterns          10
factck pinned gate.b17.3.switches          4
factck pinned gate.b17.4.arrfrac           1.000000
factck pinned gate.b17.4.arrived           7
factck pinned gate.b17.4.arrived.want      7
factck pinned gate.b17.4.departed          7
factck pinned gate.b17.4.departed.want     7
factck pinned gate.b17.4.depfrac           1.000000
factck pinned gate.b17.4.firstreads        19
factck pinned gate.b17.4.held              9
factck pinned gate.b17.4.held.want         9
factck pinned gate.b17.4.holdfrac          1.000000
factck pinned gate.b17.4.sequenced         7
factck pinned gate.b17.4.sequenced.read    7
# ...and B-18, which is the column this rung added: step 11's family C minus
# its class-file leg, by the rule that made step 09's into B-16 and step 10's
# into B-17. Sixty-seven privileged wrappers departed and thirteen privileged
# constructions held, thirty-five catch parameters unnamed and fifty-one names
# that are actually read held, four `setAccessible` calls unreflected into
# VarHandles and ten that had to stay reflective.
#
# ITS HOLD HALVES ARE WHY IT IS WORTH CARRYING, exactly as B-16's and B-17's
# are. This rung asks a candidate to delete 452 import declarations and to
# rewrite 188 more, which is work in 171 of the tree's files, and a candidate
# whose sed took a line too many takes these halves down with it. Nothing else
# in this file would notice.
factck pinned gate.b18.2.departed          67
factck pinned gate.b18.2.departed.want     67
factck pinned gate.b18.2.depfrac           1.000000
factck pinned gate.b18.2.held              13
factck pinned gate.b18.2.held.ctor         7
factck pinned gate.b18.2.held.ctor.want    7
factck pinned gate.b18.2.held.setacc       6
factck pinned gate.b18.2.held.setacc.want  6
factck pinned gate.b18.2.held.want         13
factck pinned gate.b18.2.holdfrac          1.000000
factck pinned gate.b18.2.sites             0
factck pinned gate.b18.2.sites.was         67
factck pinned gate.b18.3.arrfrac           1.000000
factck pinned gate.b18.3.arrived           28
factck pinned gate.b18.3.arrived.want      28
factck pinned gate.b18.3.departed          35
factck pinned gate.b18.3.departed.want     35
factck pinned gate.b18.3.depfrac           1.000000
factck pinned gate.b18.3.held              51
factck pinned gate.b18.3.held.want         51
factck pinned gate.b18.3.holdfrac          1.000000
factck pinned gate.b18.3.read              57
factck pinned gate.b18.3.unnamed           28
factck pinned gate.b18.3.unread            4
factck pinned gate.b18.4.arrfrac           1.000000
factck pinned gate.b18.4.arrived           8
factck pinned gate.b18.4.arrived.want      8
factck pinned gate.b18.4.departed          4
factck pinned gate.b18.4.departed.want     4
factck pinned gate.b18.4.depfrac           1.000000
factck pinned gate.b18.4.exec              1.000000
factck pinned gate.b18.4.exec.broken       0
factck pinned gate.b18.4.exec.want         4
factck pinned gate.b18.4.handles           8
factck pinned gate.b18.4.held              10
factck pinned gate.b18.4.held.want         10
factck pinned gate.b18.4.holdfrac          1.000000
factck pinned gate.b18.4.setaccessible     10
factck pinned gate.b18.4.setaccessible.was 14
# ...and family C leg by leg, on the tree that did none of it. The five weights
# these five legs carry -- 0.02, 0.38, 0.33, 0.25, 0.02 -- are what a candidate
# who does the work is paid, and the row above the table says the sum of them
# here is 0.000000.
#
# EVERY HOLD HALF READS FULL AND EVERY WEIGHTED HALF READS ZERO, which is the
# arithmetic that keeps standing still worth nothing, and at this rung it is
# the whole of family C rather than most of it. The 151 files with no JDK
# import to collapse are clean because nobody has touched them; the 43 imports
# a module import cannot subsume are held because nobody deleted them; the
# seven ThreadLocals that must not move have not moved. All three cost the nop
# nothing and pay it nothing, because each is the min() partner of a term that
# reads zero.
#
# THREE COUNTERS READ HIGHER HERE THAN ON THE GOLDEN AND THAT IS A DEPARTURE
# SEEN FROM BEHIND. gate.c3.total is 495 against 43 -- the 452 single-type
# imports that have not gone yet. gate.c4.tldecls is 8 against 7 -- the one
# ThreadLocal field that becomes a ScopedValue. And gate.c1.classes is 732
# against 730, which is the subtlest of the three: `handledThrowables` is
# `new ThreadLocal<..>() { initialValue() }` in the seed and
# `ScopedValue.newInstance()` in the golden, so ManagerImpl$1 stops existing --
# and it is counted twice, once in impl-base's build output and once inside the
# published arquillian-impl-base jar. C-1 is a FRACTION and not a difference,
# so 0 of 732 and 730 of 730 are both exact and the leg charges this rung
# nothing for doing its own work.
#
# THE OTHER ANONYMOUS ThreadLocal IN THAT FILE STAYS. `eventStack` is the
# debug-only one scopedvalue.txt holds, and a tree that converted it too would
# lose a second class file here and C-4's hold with it. Q16 is that tree.
factck pinned gate.c1.at69.0               0
factck pinned gate.c1.classes              732
factck pinned gate.c1.preview              0
factck pinned gate.c2.clean                151
factck pinned gate.c2.clean.want           151
factck pinned gate.c2.cleanfrac            1.000000
factck pinned gate.c2.files                0
factck pinned gate.c2.files.want           171
factck pinned gate.c2.files.was            0
factck pinned gate.c2.imports              0
factck pinned gate.c2.imports.want         188
factck pinned gate.c2.setfrac              0.000000
factck pinned gate.c2.spurious             0
factck pinned gate.c3.departed             0
factck pinned gate.c3.departed.want        452
factck pinned gate.c3.depfrac              0.000000
factck pinned gate.c3.held                 43
factck pinned gate.c3.held.ambiguous       25
factck pinned gate.c3.held.nested          2
factck pinned gate.c3.held.static          16
factck pinned gate.c3.held.want            43
factck pinned gate.c3.holdfrac             1.000000
factck pinned gate.c3.total                495
factck pinned gate.c3.total.was            495
factck pinned gate.c4.arrfrac              0.000000
factck pinned gate.c4.arrived              0
factck pinned gate.c4.convert.want         1
factck pinned gate.c4.departed             0
factck pinned gate.c4.depfrac              0.000000
factck pinned gate.c4.held                 7
factck pinned gate.c4.held.want            7
factck pinned gate.c4.holdfrac             1.000000
factck pinned gate.c4.svcalls              0
factck pinned gate.c4.svcalls.want         8
factck pinned gate.c4.tldecls              8
factck pinned gate.c5.flag                 -XX:+UseCompactObjectHeaders
factck pinned gate.c5.forks                12
factck pinned gate.c5.forks.flagged        0
factck pinned gate.c5.projects             0
factck pinned gate.c5.projects.want        12
factck pinned gate.c5.projects.was         0
factck pinned gate.c5.tasks                12
probeck pinned ok

# --- golden: the oracle ------------------------------------------------------
# The other end of the same tree. Every leg full, every hold still held, and
# the four inherited families untouched after 452 import declarations were
# deleted out of 171 files -- which is the assertion worth having here, because
# "delete the imports" is one `sed -i` away from also deleting B-18's fifty-one
# catch parameter names or B-16's seven held literals, and the table's B
# columns would go on reading 1.000000 for a tree that never ran the sed at
# all. Only a run that does the work AND holds everything proves the two are
# compatible.
#
# THE CONTROL READS `no` HERE, WHICH IS THE ANSWER IT IS FOR. The golden
# declares 188 module imports; JEP 511 made those standard in 25 and they were
# preview in 22, 23 and 24; so the golden does not compile at -source 24 and
# gate.src.ok24 says so. A model keyed on ok24 rather than ok25 would have
# called the oracle unmeasured, which is the inverted reading of a control that
# nothing scores. mkfix12.py's score() carries that note beside `measured`.
factck golden gate.src.ok25 yes
factck golden gate.src.ok24 no
# The published artifacts, on the channel B-12e and B-15 share: twelve jars,
# two class entries in each, and the four constant-pool floors step 05 froze.
# K19 moves these and leaves the scan alone; K20 removes the jars entirely and
# is the `gate.cp=no-jars` row.
factck golden gate.cp.jars 12
factck golden gate.cp.classes 24
factck golden gate.cp.optional 5
factck golden gate.cp.collectors 6
factck golden gate.cp.objects 2
factck golden gate.cp.jtime 2
# The four inherited families, on the tree that did this rung's work. Identical
# to the nop's, which is the whole claim: none of C-1..C-5 touches a signature,
# a package, a transcript or a service file.
factck golden gate.b15.arrived             1.000000
factck golden gate.b15.departed            1.000000
factck golden gate.b15.exec                1.000000
factck golden gate.b15.exec.broken         0
factck golden gate.b15.exec.want           69
factck golden gate.b15.held                1.000000
factck golden gate.b15.mod.declared        12
factck golden gate.b15.mod.derivable       11
factck golden gate.b15.mod.total           12
factck golden gate.b15.names               12
factck golden gate.b16.2.converted         17
factck golden gate.b16.2.converted.want    17
factck golden gate.b16.2.convfrac          1.000000
factck golden gate.b16.2.held              7
factck golden gate.b16.2.held.want         7
factck golden gate.b16.2.holdfrac          1.000000
factck golden gate.b16.2.sites             24
factck golden gate.b16.2.sites.want        24
factck golden gate.b16.2.totfrac           1.000000
factck golden gate.b16.3.exec              1.000000
factck golden gate.b16.3.exec.broken       0
factck golden gate.b16.3.exec.want         5
factck golden gate.b16.3.held              7
factck golden gate.b16.3.held.want         7
factck golden gate.b16.3.holdfrac          1.000000
factck golden gate.b16.3.moved             1
factck golden gate.b16.3.moved.want        1
factck golden gate.b16.3.movefrac          1.000000
factck golden gate.b16.3.textblocks        1
factck golden gate.b16.4.closedfrac        1.000000
factck golden gate.b16.4.decl              11
factck golden gate.b16.4.nonsealed         0
factck golden gate.b16.4.open              11
factck golden gate.b16.4.open.want         11
factck golden gate.b16.4.openfrac          1.000000
factck golden gate.b16.4.permits           35
factck golden gate.b16.4.roots             11
factck golden gate.b16.4.roots.want        11
factck golden gate.b16.5.arrow             6
factck golden gate.b16.5.colon             0
factck golden gate.b16.5.expr              2
factck golden gate.b16.5.files             2
factck golden gate.b16.5.files.want        2
factck golden gate.b16.5.size              6
factck golden gate.b17.2.arrfrac           1.000000
factck golden gate.b17.2.arrived           13
factck golden gate.b17.2.arrived.want      13
factck golden gate.b17.2.departed          13
factck golden gate.b17.2.departed.want     13
factck golden gate.b17.2.depfrac           1.000000
factck golden gate.b17.2.held              14
factck golden gate.b17.2.held.want         14
factck golden gate.b17.2.holdfrac          1.000000
factck golden gate.b17.2.patterns          24
factck golden gate.b17.2.reftests          22
factck golden gate.b17.2.swpatterns        10
factck golden gate.b17.3.arrfrac           1.000000
factck golden gate.b17.3.arrived           10
factck golden gate.b17.3.arrived.want      10
factck golden gate.b17.3.departed          10
factck golden gate.b17.3.departed.want     10
factck golden gate.b17.3.depfrac           1.000000
factck golden gate.b17.3.exec              1.000000
factck golden gate.b17.3.exec.broken       0
factck golden gate.b17.3.exec.want         5
factck golden gate.b17.3.held              2
factck golden gate.b17.3.held.want         2
factck golden gate.b17.3.holdfrac          1.000000
factck golden gate.b17.3.ipleft            0
factck golden gate.b17.3.nullcase          1
factck golden gate.b17.3.patterns          10
factck golden gate.b17.3.switches          4
factck golden gate.b17.4.arrfrac           1.000000
factck golden gate.b17.4.arrived           7
factck golden gate.b17.4.arrived.want      7
factck golden gate.b17.4.departed          7
factck golden gate.b17.4.departed.want     7
factck golden gate.b17.4.depfrac           1.000000
factck golden gate.b17.4.firstreads        19
factck golden gate.b17.4.held              9
factck golden gate.b17.4.held.want         9
factck golden gate.b17.4.holdfrac          1.000000
factck golden gate.b17.4.sequenced         7
factck golden gate.b17.4.sequenced.read    7
factck golden gate.b18.2.departed          67
factck golden gate.b18.2.departed.want     67
factck golden gate.b18.2.depfrac           1.000000
factck golden gate.b18.2.held              13
factck golden gate.b18.2.held.ctor         7
factck golden gate.b18.2.held.ctor.want    7
factck golden gate.b18.2.held.setacc       6
factck golden gate.b18.2.held.setacc.want  6
factck golden gate.b18.2.held.want         13
factck golden gate.b18.2.holdfrac          1.000000
factck golden gate.b18.2.sites             0
factck golden gate.b18.2.sites.was         67
factck golden gate.b18.3.arrfrac           1.000000
factck golden gate.b18.3.arrived           28
factck golden gate.b18.3.arrived.want      28
factck golden gate.b18.3.departed          35
factck golden gate.b18.3.departed.want     35
factck golden gate.b18.3.depfrac           1.000000
factck golden gate.b18.3.held              51
factck golden gate.b18.3.held.want         51
factck golden gate.b18.3.holdfrac          1.000000
factck golden gate.b18.3.read              57
factck golden gate.b18.3.unnamed           28
factck golden gate.b18.3.unread            4
factck golden gate.b18.4.arrfrac           1.000000
factck golden gate.b18.4.arrived           8
factck golden gate.b18.4.arrived.want      8
factck golden gate.b18.4.departed          4
factck golden gate.b18.4.departed.want     4
factck golden gate.b18.4.depfrac           1.000000
factck golden gate.b18.4.exec              1.000000
factck golden gate.b18.4.exec.broken       0
factck golden gate.b18.4.exec.want         4
factck golden gate.b18.4.handles           8
factck golden gate.b18.4.held              10
factck golden gate.b18.4.held.want         10
factck golden gate.b18.4.holdfrac          1.000000
factck golden gate.b18.4.setaccessible     10
factck golden gate.b18.4.setaccessible.was 14
# ...and family C, full. Five legs, eleven halves between them, and the two
# denominators that move with the tree rather than standing still.
#
# C-1 IS TWO READINGS AND NOT ONE, and this is the first rung where that is
# true. `at69.0` is 730 of 730 class files at major 69 AND minor 0; `preview`
# is the count at minor 65535, and it is 0. A tree compiled behind
# --enable-preview stamps major 69 on everything it emits and a major-only
# reading cannot tell it from this one. Q1 is that tree and it scores C-1 at
# zero with 730 class files at major 69.
#
# gate.c3.total READS 43 HERE AND 495 ON THE NOP, and `total.was` reads 495 on
# both. The denominator of C-3's departure half is the frozen 495 from
# baseline/jdkimport.txt and not the measured total; the measured total is what
# says the departure happened, and a leg that divided by it would have paid a
# tree for deleting the evidence. Q9 -- 43 held imports deleted -- is the row
# where the two readings come apart.
#
# gate.c4.tldecls READS 7 AND gate.c2.files.was READS 0. The first is the one
# ThreadLocal that left; the second is the seed's module-import count, which is
# zero because the seed declares none, and it is published so a reader can see
# that 171 is an arrival rather than a tree that always looked like this.
factck golden gate.c1.at69.0               730
factck golden gate.c1.classes              730
factck golden gate.c1.preview              0
factck golden gate.c2.clean                151
factck golden gate.c2.clean.want           151
factck golden gate.c2.cleanfrac            1.000000
factck golden gate.c2.files                171
factck golden gate.c2.files.want           171
factck golden gate.c2.files.was            0
factck golden gate.c2.imports              188
factck golden gate.c2.imports.want         188
factck golden gate.c2.setfrac              1.000000
factck golden gate.c2.spurious             0
factck golden gate.c3.departed             452
factck golden gate.c3.departed.want        452
factck golden gate.c3.depfrac              1.000000
factck golden gate.c3.held                 43
factck golden gate.c3.held.ambiguous       25
factck golden gate.c3.held.nested          2
factck golden gate.c3.held.static          16
factck golden gate.c3.held.want            43
# ZERO, ON THE GOLDEN, AND THAT IS NOT A CONTRADICTION OF THE SECTION BELOW.
# The golden's TREE here is /nonexistent like every other fixture's, so the
# excuse clause stats nothing and grants nothing, and `held.want` is the full
# 43. On the real golden tree two of the 43 are excused and the denominator is
# 41 -- which is measured on a tree that exists, in its own section further
# down, because it cannot be measured here.
factck golden gate.c3.held.excused         0
factck golden gate.c3.holdfrac             1.000000
factck golden gate.c3.total                43
factck golden gate.c3.total.was            495
factck golden gate.c4.arrfrac              1.000000
factck golden gate.c4.arrived              1
factck golden gate.c4.convert.want         1
factck golden gate.c4.departed             1
factck golden gate.c4.depfrac              1.000000
factck golden gate.c4.held                 7
factck golden gate.c4.held.want            7
factck golden gate.c4.holdfrac             1.000000
factck golden gate.c4.svcalls              8
factck golden gate.c4.svcalls.want         8
factck golden gate.c4.tldecls              7
factck golden gate.c5.flag                 -XX:+UseCompactObjectHeaders
factck golden gate.c5.forks                12
factck golden gate.c5.forks.flagged        12
factck golden gate.c5.projects             12
factck golden gate.c5.projects.want        12
factck golden gate.c5.projects.was         0
factck golden gate.c5.tasks                12
probeck golden ok

# --- C-1: the class-file level, and the minor this rung had to start reading -
# Four fixtures on one channel, and they are four because the leg is two
# readings now rather than one. L1 stopped 30 class files short of major 69.
# Q2 compiled 40 of them behind --enable-preview. Q1 compiled ALL of them
# behind it -- 730 at major 69 and 730 at minor 65535 -- which is the tree a
# major-only reading could not tell from the golden, and it is why level.txt
# carries `minor=0` beside `major=69` and `was=66`. L2 produced no scan at all.
#
# THE MINOR IS NOT A REFINEMENT, IT IS THE LEG. All three of this rung's
# language and runtime changes were preview in some release before 25 and the
# shape of the flag is in every tutorial written before they went final, so
# `--enable-preview` is the single most reachable wrong answer here. A tree
# that turns it on has not migrated to 25; it has migrated to a 25 that will
# not run on the next one. Q1 scores C-1 at 0.000000 with every class file at
# the right major.
#
# gate.c1.classes READS 730 ON ALL FOUR, which is the denominator moving with
# the tree the way the nop's 732 says it should: these four are the golden with
# one thing changed, and the thing changed is never the class count.
factck L1 gate.c1.classes 730
factck L1 gate.c1.at69.0 700
factck L1 gate.c1.preview 0
factck Q2 gate.c1.classes 730
factck Q2 gate.c1.at69.0 690
factck Q2 gate.c1.preview 40
factck Q1 gate.c1.classes 730
factck Q1 gate.c1.at69.0 0
factck Q1 gate.c1.preview 730
# L2 IS THE ABSENCE AND NOT A ZERO, and the fact says which. `gate.c1=no-scan`
# is a different line from `gate.c1.at69.0=0`: the first says the build emitted
# no class files the scanner could read, the second says it emitted 730 and
# none of them is at the level. Both score C-1 at zero and only one of them is
# a tree that compiled. L2's other four legs are the golden's, which is what
# makes it one of this rung's three 0.980000 rows.
factck L2 gate.c1 no-scan
factck L2 gate.c2.setfrac 1.000000
factck L2 gate.c3.depfrac 1.000000
factck L2 gate.c4.arrfrac 1.000000
factck L2 gate.c5.forks.flagged 12

# --- C-2: 171 sets, 151 files with nothing to do, and the blanket edit -------
# Six fixtures, and between them they are every way to get a module import
# wrong. The leg is min(set equality over 171 files, no spurious arrival in the
# other 151), and each of the six moves exactly one of those two halves.
#
# Q3 AND Q6 SCORE THE SAME AND ARE NOT THE SAME TREE. Both read setfrac
# 0.994152 -- 170 of 171 files whose module set matches -- and both are
# 0.997778 in the totals column. Q3 DELETED a module import: ArquillianResource
# .java declared java.naming and java.base and now declares only java.base, so
# `imports` falls to 187. Q6 declared the WRONG ONE: DynamicServiceLoader.java
# declares java.management where it needs java.logging, so `imports` stays at
# 188 and one file's set is still wrong. gate.c2.imports is the only fact that
# separates them, and it is why the count is published beside the fraction.
factck Q3 gate.c2.files 170
factck Q3 gate.c2.imports 187
factck Q3 gate.c2.setfrac 0.994152
factck Q3 gate.c2.cleanfrac 1.000000
factck Q6 gate.c2.files 170
factck Q6 gate.c2.imports 188
factck Q6 gate.c2.setfrac 0.994152
factck Q6 gate.c2.cleanfrac 1.000000
# Q4 IS THE BLANKET EDIT AND Q7 IS THE TREE THAT NEVER STARTED, and they also
# score the same: 0.620000, because C-2's 0.38 is the whole of what either
# loses. The two are opposite in every fact under it. Q4 put
# `import module java.base;` in all 322 files: 152 of the 171 sets come out
# right, which is the 19 files that name a type java.base does not export
# coming out wrong, and all 151 of the clean files now import something they do
# not use -- so setfrac is 0.888889, cleanfrac is 0.000000, and min() takes the
# second. Q7 has no module imports at all: setfrac 0.000000, cleanfrac
# 1.000000, min() takes the first.
#
# THIS IS THE PAIR THE SECOND HALF EXISTS FOR. A leg stated only over the 171
# files with work would have paid Q4 0.888889 for an edit that is wrong in 19
# files and spurious in 151 more, and would have ranked it above a tree that
# did four fifths of the job properly. The 151 rows carrying `mods=- n=0` in
# modimport.txt are a population and not an absence to argue from.
factck Q4 gate.c2.files 152
factck Q4 gate.c2.imports 322
factck Q4 gate.c2.setfrac 0.888889
factck Q4 gate.c2.clean 0
factck Q4 gate.c2.cleanfrac 0.000000
factck Q4 gate.c2.spurious 151
factck Q7 gate.c2.files 0
factck Q7 gate.c2.imports 0
factck Q7 gate.c2.setfrac 0.000000
factck Q7 gate.c2.clean 151
factck Q7 gate.c2.cleanfrac 1.000000
factck Q7 gate.c2.spurious 0
# Q5 IS THE HALF-BLANKET: all 171 sets right and 30 of the 151 clean files
# given a module import anyway. setfrac is 1.000000 and cleanfrac is 0.801325,
# and min() takes the one that noticed. A tree can be perfect where the leg
# expects work and still be wrong where it does not.
factck Q5 gate.c2.files 171
factck Q5 gate.c2.setfrac 1.000000
factck Q5 gate.c2.clean 121
factck Q5 gate.c2.cleanfrac 0.801325
factck Q5 gate.c2.spurious 30
# Q8 SCORES 1.000000 ON EVERY CELL AND IS NOT THE GOLDEN. One module import in
# a file that is in neither half -- not among the 171 with work, not among the
# 151 with none -- so `imports` reads 189 against a want of 188 and nothing
# else moves. It is here because a row that asserts a NON-EVENT is worth having
# on a leg whose scope is stated as two populations rather than as a tree: the
# count says the file exists and the fractions say the leg does not reach it.
factck Q8 gate.c2.files 171
factck Q8 gate.c2.imports 189
factck Q8 gate.c2.setfrac 1.000000
factck Q8 gate.c2.cleanfrac 1.000000
factck Q8 gate.c2.spurious 0

# --- C-3: the 452 that go and the 43 that must not ---------------------------
# Five fixtures. One takes the whole hold, three take it a reason at a time,
# and the fifth leaves the departure short. The leg is
# min(452 departed, 43 held), and the three reasons are published separately
# because they are not equally hard to get right.
#
# Q9 IS THE sed ANSWER and it is the row this leg exists for. Every
# `import java.` line in the tree is gone -- the 43 a module import does not
# reach along with the 452 it does. The departure half reads 1.000000,
# perfectly, which is precisely why it is not the whole leg; the hold reads 0
# of 43; C-3 is 0.000000. One command and 0.33 of the family.
factck Q9 gate.c3.departed 452
factck Q9 gate.c3.depfrac 1.000000
factck Q9 gate.c3.held 0
factck Q9 gate.c3.held.excused 0
factck Q9 gate.c3.holdfrac 0.000000
factck Q9 gate.c3.total 0
factck Q9 gate.c3.total.was 495
# THE THREE REASONS, ONE ROW EACH, AND THEY ARE NOT EQUALLY HARD. Q10 deletes
# the 25 ambiguous `java.lang.annotation.Annotation` imports -- JEP 484 put
# `java.lang.classfile.Annotation` in java.base at 24, so `import module
# java.base` leaves the simple name ambiguous and none of those 25 files
# build. That is the half a candidate's own compiler teaches, and it is why the
# other two are worth more than it: Q11's 16 static imports and Q12's 2
# nested-type imports compile perfectly well after deletion and change what the
# file means. A gate that published one figure for the 43 could not say which
# of the three a partial answer got.
factck Q10 gate.c3.held 18
factck Q10 gate.c3.holdfrac 0.418605
factck Q10 gate.c3.depfrac 1.000000
factck Q11 gate.c3.held 27
factck Q11 gate.c3.holdfrac 0.627907
factck Q12 gate.c3.held 41
factck Q12 gate.c3.holdfrac 0.953488
# Q12's 41 of 43 is the reading with the excuse CLOSED, and the 0 below is what
# closes it: TREE=/nonexistent. Q12 is the row the excuse is supposed to refuse,
# so the number is right either way -- which is exactly why it cannot stand as
# evidence that the clause works, and why the tree-backed section below exists.
factck Q12 gate.c3.held.excused 0
# THE BREAKDOWN IS THE BASELINE'S AND NOT THE TREE'S, which is why it reads the
# same on all three. `held.ambiguous`, `held.static` and `held.nested` are the
# 25 / 16 / 2 split of baseline/jdkimport.txt's hold set, published so the
# report can name the reason on a row the candidate lost; the measured figure
# is `held`. A gate that had recomputed the split from the candidate's tree
# would have made all three read 0 on Q9 and said nothing about why.
factck Q10 gate.c3.held.ambiguous 25
factck Q10 gate.c3.held.static 16
factck Q10 gate.c3.held.nested 2
factck Q11 gate.c3.held.ambiguous 25
factck Q11 gate.c3.held.static 16
factck Q11 gate.c3.held.nested 2
factck Q12 gate.c3.held.ambiguous 25
factck Q12 gate.c3.held.static 16
factck Q12 gate.c3.held.nested 2
# Q13 IS THE HALF THAT KEEPS C-2 HONEST. Forty of the 452 are still written
# out beside the module imports that subsume them: the `import module` line was
# added and the lines it replaces were not removed. The hold is full, the
# departure is 412 of 452, and `gate.c3.total` reads 83 -- the 43 that belong
# there plus the 40 that do not. Without this half a tree collects all 0.38 of
# C-2 by adding 188 lines and deleting nothing, which is the cheapest edit
# available and is not this migration.
factck Q13 gate.c3.departed 412
factck Q13 gate.c3.depfrac 0.911504
factck Q13 gate.c3.held 43
factck Q13 gate.c3.holdfrac 1.000000
factck Q13 gate.c3.total 83

# --- C-4: one arrives, one departs, seven hold -- and three ways to lose it --
# Q14, Q15 and Q16 all score C-4 at 0.000000 and familyC at 0.750000, and they
# are three trees rather than one row printed three times. A min() of three
# halves over denominators 1, 1 and 7 has exactly three ways to fall, and this
# is each of them once. `departed`, `arrived` and `held` read 0/1/7 at Q14,
# 1/0/7 at Q15 and 1/1/0 at Q16. Nothing else in the table separates them.
#
#   Q14  the belt-and-braces conversion: the ScopedValue was added and the
#        ThreadLocal it replaces is still declared. Two sources of truth for
#        one piece of state and a `finally` that still has to run.
#   Q15  the ThreadLocal went and nothing arrived: the field was deleted and
#        its readers rewritten to take the value as a parameter. A defensible
#        refactor and not this leg. The call sites are left in place on
#        purpose -- `svcalls` still reads 8, because a row that deleted the
#        reads as well would be arguing its case with a fact nobody scores.
#   Q16  all eight converted. `ScopedValue` compiles for all eight; seven of
#        them are wrong for reasons nothing in the language states, and the
#        tests that would notice are the ones JUnit and TestNG drive.
factck Q14 gate.c4.departed 0
factck Q14 gate.c4.arrived 1
factck Q14 gate.c4.held 7
factck Q14 gate.c4.tldecls 8
factck Q14 gate.c4.depfrac 0.000000
factck Q14 gate.c4.arrfrac 1.000000
factck Q14 gate.c4.holdfrac 1.000000
factck Q15 gate.c4.departed 1
factck Q15 gate.c4.arrived 0
factck Q15 gate.c4.held 7
factck Q15 gate.c4.tldecls 7
factck Q15 gate.c4.svcalls 8
factck Q15 gate.c4.depfrac 1.000000
factck Q15 gate.c4.arrfrac 0.000000
factck Q15 gate.c4.holdfrac 1.000000
factck Q16 gate.c4.departed 1
factck Q16 gate.c4.arrived 1
factck Q16 gate.c4.held 0
factck Q16 gate.c4.tldecls 0
factck Q16 gate.c4.depfrac 1.000000
factck Q16 gate.c4.arrfrac 1.000000
factck Q16 gate.c4.holdfrac 0.000000
# Q17 IS WHY THE HOLD IS SEVEN ROWS AND NOT A BOOLEAN. Two of the seven were
# converted -- a candidate that read the rule as `per-thread state becomes a
# ScopedValue` and applied it where the shape looked close enough. 5 of 7 is
# 0.714286, and from every other channel in this report -- the level, the
# module imports, the single-type imports, the flag -- this tree is the golden.
factck Q17 gate.c4.held 5
factck Q17 gate.c4.tldecls 5
factck Q17 gate.c4.holdfrac 0.714286
factck Q17 gate.c4.depfrac 1.000000
factck Q17 gate.c4.arrfrac 1.000000

# --- C-5: twelve projects, thirteen tasks, and the probe that never answered -
# Q18 and Q19 both read 11 of 12 for 0.916667 and 0.998333 in the totals
# column, and they are the pair that says the tally is per project AND per
# task. Q18 is eleven build files edited by hand and a twelfth that was not.
# Q19 is one project with TWO `Test` tasks where only one got the flag:
# `gate.c5.tasks` reads 13 against Q18's 12 and `forks.flagged` reads 12
# against 11, and C-5 counts a project only when EVERY row it printed carries
# the flag. An `any` over the rows would have scored Q19 at 1.000000, which is
# the whole reason the tally is not a grep over the log.
factck Q18 gate.c5.tasks 12
factck Q18 gate.c5.forks 12
factck Q18 gate.c5.forks.flagged 11
factck Q18 gate.c5.projects 11
probeck Q18 ok
factck Q19 gate.c5.tasks 13
factck Q19 gate.c5.forks 13
factck Q19 gate.c5.forks.flagged 12
factck Q19 gate.c5.projects 11
probeck Q19 ok
# Q20 IS THE ONE PLACE IN FAMILY C WHERE AN ABSENT ARTEFACT SCORES ZERO RATHER
# THAN PUBLISHING AN EXCUSE. There is no jvmargs.txt, so `run_unprivileged`
# returns non-zero, the probe state is `failed`, and no row reaches the tally.
# A build nobody can ask cannot be said to pass the flag. C-1 publishes
# `no-scan` on L2 and still scores zero; C-5 publishes a state and does the
# same; the difference is that C-1's absence is a build that emitted no class
# files and C-5's is a build that would not answer a question.
#
# THE PROBE STATE IS THE FACT, NOT THE EXIT CODE. `gate.c5.probe` ships as
# `<state>,seconds=<n>,limit=<n>` and probeck reads the first field: `failed`
# is any non-zero exit that is not 124 or 137, and those two are `timeout`,
# because a probe killed at the limit and a build that refused to configure are
# different things to tell a candidate.
probeck Q20 failed
factck Q20 gate.c5.tasks 0
factck Q20 gate.c5.forks 0
factck Q20 gate.c5.forks.flagged 0
factck Q20 gate.c5.projects 0
factck Q20 gate.c5.projects.want 12

# --- B-18.2: sixty-seven wrappers gone and thirteen constructions held -------
# This rung's new inherited column, leg by leg, and the four fixtures that move
# its first leg. B-18 is step 11's family C minus its class-file leg, and it is
# carried at a weight of (0.46a + 0.26b + 0.24c)/0.96 -- the ladder rule, the
# survivors renormalised over the weight they still carry.
#
#   P1  ten wrapper sites still standing in the resource enricher
#   P4  seven class declarations that still implement PrivilegedAction
#   P2  three held constructions deleted with the ladder that wrapped them
#   P5  one construction moved out and two added somewhere else
#
# P1 AND P4 ARE THE SAME HALF FROM TWO DIRECTIONS. `sites` reads 10 and 7
# against a golden's 0, and `departed` falls to 57 and 60 against a want of 67:
# a wrapper the candidate left is a site the scanner still finds. P2 and P5
# move the OTHER half, which is the one a candidate loses by deleting too much
# rather than too little -- `held.ctor` falls and `sites` does not move at all.
factck P1     gate.b18.2.departed          57
factck P1     gate.b18.2.depfrac           0.850746
factck P1     gate.b18.2.sites             10
factck P4     gate.b18.2.departed          60
factck P4     gate.b18.2.depfrac           0.895522
factck P4     gate.b18.2.sites             7
factck P2     gate.b18.2.held              10
factck P2     gate.b18.2.held.ctor         4
factck P2     gate.b18.2.holdfrac          0.769231
factck P5     gate.b18.2.held              12
factck P5     gate.b18.2.held.ctor         6
factck P5     gate.b18.2.holdfrac          0.923077
# --- B-18.3: thirty-five parameters unnamed, fifty-one names that are read ---
#   P6  four catch parameters still named; four fewer arrivals AND departures
#   P7  six catch blocks deleted outright -- the arrival falls, the departure
#       does not, because a block that is gone is not a block that held a name
#   P8  five READ parameters renamed `_`, which is the hold half falling while
#       both other halves stay full
#   P9  twelve read parameters renamed and two never renamed -- all three
#       halves move at once, which is the row that says they are three
factck P6     gate.b18.3.arrfrac           0.857143
factck P6     gate.b18.3.arrived           24
factck P6     gate.b18.3.departed          31
factck P6     gate.b18.3.depfrac           0.885714
factck P6     gate.b18.3.unnamed           24
factck P6     gate.b18.3.unread            8
factck P7     gate.b18.3.arrfrac           0.785714
factck P7     gate.b18.3.arrived           22
factck P7     gate.b18.3.unnamed           22
factck P8     gate.b18.3.held              46
factck P8     gate.b18.3.holdfrac          0.901961
factck P8     gate.b18.3.read              52
factck P8     gate.b18.3.unnamed           33
factck P9     gate.b18.3.arrfrac           0.928571
factck P9     gate.b18.3.arrived           26
factck P9     gate.b18.3.departed          33
factck P9     gate.b18.3.depfrac           0.942857
factck P9     gate.b18.3.held              39
factck P9     gate.b18.3.holdfrac          0.764706
factck P9     gate.b18.3.read              45
factck P9     gate.b18.3.unnamed           38
factck P9     gate.b18.3.unread            6
# --- B-18.4: four setAccessible calls unreflected and ten that stay ----------
#   P10  one field still forced open and two handle records gone
#   P11  two handles never unreflected
#   P12  one held call converted
#   P13  four fields still forced open beside their handles
#   P14  two of four access-failure recordings differ -- behaviour and not
#        source, which is why its only moving fact is `exec`
#   P15  one held call converted AND one recording differs: source and
#        behaviour at once
#
# P10 AND P11 BOTH READ B-18 AT 0.937500 AND THE TABLE CANNOT SEPARATE THEM.
# B-18.4 is min(departure, arrival, hold) scaled by the recorded behaviour, and
# both trees land that min() on 0.750000 by a different half: P10 lost the
# DEPARTURE as well as the arrival -- one field still forced open, so
# `setaccessible` reads 11 against the golden's 10 and `depfrac` falls to
# 0.750000 -- while P11 kept every departure and only two handles never
# arrived, so its `depfrac` is still 1.000000. Three facts say so and the total
# says nothing.
#
# P12 AND P15 DO NOT COLLIDE HERE -- 0.975000 against 0.918750 -- and they
# collide one column to the left, at B-13 0.980000. `gate.b18.4.exec` is what
# parts them: identical source, and P15 additionally has one of the four
# access-failure recordings differing.
factck P10    gate.b18.4.arrfrac           0.750000
factck P10    gate.b18.4.arrived           6
factck P10    gate.b18.4.departed          3
factck P10    gate.b18.4.depfrac           0.750000
factck P10    gate.b18.4.handles           6
factck P10    gate.b18.4.setaccessible     11
factck P11    gate.b18.4.arrfrac           0.750000
factck P11    gate.b18.4.arrived           6
factck P11    gate.b18.4.handles           6
factck P12    gate.b18.4.handles           10
factck P12    gate.b18.4.held              9
factck P12    gate.b18.4.holdfrac          0.900000
factck P12    gate.b18.4.setaccessible     9
factck P13    gate.b18.4.departed          0
factck P13    gate.b18.4.depfrac           0.000000
factck P13    gate.b18.4.setaccessible     14
factck P14    gate.b18.4.exec              0.500000
factck P14    gate.b18.4.exec.broken       2
factck P15    gate.b18.4.exec              0.750000
factck P15    gate.b18.4.exec.broken       1
factck P15    gate.b18.4.handles           10
factck P15    gate.b18.4.held              9
factck P15    gate.b18.4.holdfrac          0.900000
factck P15    gate.b18.4.setaccessible     9

# --- B-17: step 10's family C, three legs and fifteen ways to lose one -------
#   N1   three reflective tests still beside the pattern that replaced them
#   N2   three instanceof patterns removed -- and the one row in this series
#        that moves TWO columns, because ipattern.txt is B-16.2's population
#        as well and B-16 falls to 0.960784 with it
#   N3   four held reflective tests gone from ManagerImpl
#   N4   fourteen reflective tests deleted outright
#   N5   the v_2_5 packager switch declares two arms where three are wanted
#   N6   the beans.xml processor switch matches Archive instead of WebArchive
#   N7   a pattern switch planted where the population expects none
#   N8   two of five recorded dispatch inputs differ -- behaviour, not source
#   N9   one switch moved out of generateDeployment and one into handleArchive
#   N14  ten reflective tests back beside four switches that replaced none
#   N15  ten chain arms rewritten as instanceof patterns instead
#   N10  two index reads still beside the getFirst() that replaced them
#   N11  two sequenced reads removed
#   N12  three held first-element reads gone
#   N13  findDefaultContainer reads getLast()
factck N1     gate.b17.2.departed          10
factck N1     gate.b17.2.depfrac           0.769231
factck N1     gate.b17.2.reftests          25
factck N2     gate.b16.2.converted         14
factck N2     gate.b16.2.convfrac          0.823529
factck N2     gate.b16.2.sites             21
factck N2     gate.b16.2.totfrac           0.875000
factck N2     gate.b17.2.arrfrac           0.769231
factck N2     gate.b17.2.arrived           10
factck N2     gate.b17.2.patterns          21
factck N3     gate.b17.2.held              10
factck N3     gate.b17.2.holdfrac          0.714286
factck N3     gate.b17.2.reftests          18
factck N4     gate.b17.2.held              0
factck N4     gate.b17.2.holdfrac          0.000000
factck N4     gate.b17.2.reftests          8
factck N5     gate.b17.2.swpatterns        9
factck N5     gate.b17.3.arrfrac           0.700000
factck N5     gate.b17.3.arrived           7
factck N5     gate.b17.3.patterns          9
factck N6     gate.b17.2.arrfrac           0.923077
factck N6     gate.b17.2.arrived           12
factck N6     gate.b17.3.arrfrac           0.800000
factck N6     gate.b17.3.arrived           8
factck N7     gate.b17.2.swpatterns        12
factck N7     gate.b17.3.held              1
factck N7     gate.b17.3.holdfrac          0.500000
factck N7     gate.b17.3.patterns          12
factck N7     gate.b17.3.switches          5
factck N8     gate.b17.3.exec              0.600000
factck N8     gate.b17.3.exec.broken       2
factck N9     gate.b17.2.arrfrac           0.769231
factck N9     gate.b17.2.arrived           10
factck N9     gate.b17.3.arrfrac           0.700000
factck N9     gate.b17.3.arrived           7
factck N14    gate.b17.2.departed          3
factck N14    gate.b17.2.depfrac           0.230769
factck N14    gate.b17.2.reftests          32
factck N14    gate.b17.3.departed          0
factck N14    gate.b17.3.depfrac           0.000000
factck N15    gate.b16.2.sites             34
factck N15    gate.b17.2.patterns          34
factck N15    gate.b17.3.departed          0
factck N15    gate.b17.3.depfrac           0.000000
factck N15    gate.b17.3.ipleft            10
factck N10    gate.b17.4.departed          5
factck N10    gate.b17.4.depfrac           0.714286
factck N10    gate.b17.4.firstreads        21
factck N11    gate.b17.4.arrfrac           0.714286
factck N11    gate.b17.4.arrived           5
factck N11    gate.b17.4.sequenced         5
factck N11    gate.b17.4.sequenced.read    5
factck N12    gate.b17.4.firstreads        16
factck N12    gate.b17.4.held              6
factck N12    gate.b17.4.holdfrac          0.666667
factck N13    gate.b17.4.arrfrac           0.857143
factck N13    gate.b17.4.arrived           6
# --- B-16: step 09's family C, and the two figures this rung inherited -------
#   L3   twelve sites in nine buckets back to a plain instanceof
#   L4   seven held sites bound as well
#   L5   three held sites deleted
#   L6   one text block removed; the literals are still gone
#   L7   three newline literals gone from ToolingDeploymentFormatter#format
#   L8   two of five recorded document behaviours differ
#   L9   five roots no longer sealed
#   L10  eleven extension points closed as well
#   L11  ContainerControlEvent no longer permits StopContainer
#   L12  one switch in DeployableTestBuilder back to colon form
#   L13  one colon switch held and one arrow switch added elsewhere
#   L14  one text block moved to class scope
factck L3     gate.b16.2.converted         5
factck L3     gate.b16.2.convfrac          0.294118
factck L4     gate.b16.2.held              0
factck L4     gate.b16.2.holdfrac          0.000000
factck L5     gate.b16.2.held              4
factck L5     gate.b16.2.holdfrac          0.571429
factck L5     gate.b16.2.sites             21
factck L5     gate.b16.2.totfrac           0.875000
factck L5     gate.b17.2.patterns          21
factck L6     gate.b16.3.moved             0
factck L6     gate.b16.3.movefrac          0.000000
factck L6     gate.b16.3.textblocks        0
factck L7     gate.b16.3.held              4
factck L7     gate.b16.3.holdfrac          0.571429
factck L8     gate.b16.3.exec              0.600000
factck L8     gate.b16.3.exec.broken       2
factck L9     gate.b16.4.closedfrac        0.545455
factck L9     gate.b16.4.decl              6
factck L9     gate.b16.4.permits           13
factck L9     gate.b16.4.roots             6
factck L10    gate.b16.4.decl              22
factck L10    gate.b16.4.open              0
factck L10    gate.b16.4.openfrac          0.000000
factck L10    gate.b16.4.permits           57
factck L11    gate.b16.4.closedfrac        0.909091
factck L11    gate.b16.4.permits           34
factck L11    gate.b16.4.roots             10
factck L12    gate.b16.5.arrow             5
factck L12    gate.b16.5.colon             1
factck L12    gate.b16.5.files             1
factck L13    gate.b16.5.colon             1
factck L13    gate.b16.5.files             1
factck L13    gate.b16.5.size              7
factck L14    gate.b16.3.moved             0
factck L14    gate.b16.3.movefrac          0.000000
# --- B-15: step 08's family C, on the channel the jars and the report share --
#   K1   arquillian-testng publishes no Automatic-Module-Name
#   K2   twelve jars and not one Automatic-Module-Name among them
#   K3   two module names derived from the file name rather than the packages
#   M1   arquillian-spi carries an illegal META-INF/services entry
#   M2   the spi ServiceLoader#reload back to a hand-rolled scan
#   M3   FilteredURLClassLoader#loadClass no longer loads anything by name
#   M4   both platformload records become platformtype
#   M5   all 69 recorded service behaviours differ
#   M6   9 of 69 differ
factck K1     gate.b15.mod.declared        11
factck K1     gate.b15.names               11
factck K2     gate.b15.mod.declared        0
factck K2     gate.b15.names               0
factck K3     gate.b15.names               10
factck M1     gate.b15.mod.derivable       10
factck M2     gate.b15.arrived             0.500000
factck M2     gate.b15.departed            0.500000
factck M3     gate.b15.held                0.909091
factck M4     gate.b15.arrived             0.000000
factck M5     gate.b15.exec                0.000000
factck M5     gate.b15.exec.broken         69
factck M6     gate.b15.exec                0.869565
factck M6     gate.b15.exec.broken         9
# --- B-14 and below: step 07's family C and the rungs under it ---------------
# Nine of these move B-14's five legs and four move a column further down. They
# are asserted here for the reason every rung has asserted them: an inherited
# gate that stopped publishing would read 1.000000 on every fixture in this
# file and no total would change.
#
#   K4   getResourceName reverted, three javax references back
#   K5   one javax.annotation reference in CDIInjectionEnricher
#   K6   six jakarta references dropped, every bucket still occupied
#   K7   javax.enterprise dropped, two references
#   K8   forty javax.management references renamed
#   K9   eight sized, no ctorref
#   K10  three ctorref, five sized
#   K11  no followRedirects record
#   K12  seven legacy records survive in ServletMethodExecutor.java
#   K13  one distinct arrival type
#   K14  newgeneric.raw=2
#   K15  suppress.sites=45
#   K16  toarray.raw=1
factck K4     gate.b14.c2.arrived          5
factck K4     gate.b14.c2.departed         5
factck K4     gate.b14.c2.jakarta.refs     19
factck K4     gate.b14.c2.javax.left       3
factck K4     gate.src.jkann               14
factck K4     gate.src.jxann               3
factck K5     gate.b14.c2.javax.left       1
factck K5     gate.src.jxann               1
factck K6     gate.b14.c2.jakarta.refs     16
factck K6     gate.src.jkann               15
factck K6     gate.src.jkejb               1
factck K7     gate.b14.c3.held             42
factck K7     gate.b14.c3.refs             110
factck K7     gate.b14.c3.unscaled         0.976744
factck K8     gate.b14.c3.held             33
factck K8     gate.b14.c3.refs             72
factck K8     gate.b14.c3.unscaled         0.642857
factck K9     gate.b14.c4.arrived          0
factck K9     gate.b14.c4.departed         0
factck K9     gate.src.tactor              0
factck K9     gate.src.tasized             8
factck K10    gate.b14.c4.arrived          2
factck K10    gate.b14.c4.departed         2
factck K10    gate.src.tactor              3
factck K10    gate.src.tasized             5
factck K11    gate.b14.c5.redirects        0
factck K11    gate.b14.c5.redirects.honoured 0
factck K11    gate.src.hredir              0
factck K12    gate.b14.c5.legacy           7
factck K12    gate.src.hlegref             10
factck K13    gate.b14.c5.types            1
factck K14    gate.src.penalty             0.800000
factck K14    gate.src.raw.added           2
factck K15    gate.src.penalty             0.500000
factck K16    gate.src.penalty             0.500000
factck K16    gate.src.taraw               1

# --- K17: the tree nobody could compile, in three spellings of zero ---------
# All three source probes refused, so there is no measured report, and what a
# fixture with no report asserts is not a number -- it is WHERE EACH NUMBER
# COMES FROM. The scorer's blocks fall into three groups the moment the report
# is missing, and this fixture is the only row in the table that shows all
# three at once:
#
#   * a leg initialised to the shell LITERAL 0 above its guard and assigned
#     nowhere below it prints `0`;
#   * a leg whose final assignment is an awk expression OUTSIDE the guard
#     prints `0.000000`, because awk ran on an empty population;
#   * a fact line INSIDE the guard prints nothing at all, and the key is
#     absent from the facts file.
#
# The section above already proved the geometry -- twelve legs literal, three
# computed, and every gate fact inside its own guard. This asserts that the
# geometry produces the three spellings on a real fixture, which is the only
# way to know the two statements are about the same code.
#
# FAMILY C HAS A FOURTH PROPERTY AND IT IS THIS RUNG'S OWN: all five of its
# legs are literals, AND TWO OF THEM READ FULL ANYWAY. C-1 reads ${WORK}/
# scan.txt, which arqgate.Scan wrote over the class files the build left in the
# tree, and those class files are at major 69 whatever the scorer's own three
# probes later made of the sources. C-5 reads the argument list Gradle would
# hand a test fork, and a tree that does not compile still CONFIGURES. So
# family C is 0.02 + 0.02 = 0.040000 on a tree that compiles nothing, and every
# other family is 0.000000 or lower. A rung that had put its C-5 behind the
# compiler -- read the flag out of a build file after a successful build, say
# -- would read 0.040000 here and would have been measuring the compiler twice.
factck K17    gate.src.ok25                no
factck K17    gate.src.okdef               no
factck K17    gate.src.ok24                MISSING
# the literal 0: assigned above the guard, never below it
factck K17    gate.b15.arrived             0
factck K17    gate.b15.departed            0
factck K17    gate.b15.held                0
factck K17    gate.b16.3.holdfrac          0
factck K17    gate.b16.3.movefrac          0
factck K17    gate.b17.3.arrfrac           0
factck K17    gate.b17.3.depfrac           0
factck K17    gate.b17.3.holdfrac          0
factck K17    gate.b18.4.arrfrac           0
factck K17    gate.b18.4.depfrac           0
factck K17    gate.b18.4.holdfrac          0
# nothing at all: the fact lines are inside the guard the report never opened
factck K17    gate.b16.2.converted         MISSING
factck K17    gate.b16.4.roots             MISSING
factck K17    gate.b16.5.arrow             MISSING
factck K17    gate.b17.2.arrived           MISSING
factck K17    gate.b17.4.arrived           MISSING
factck K17    gate.b18.2.departed          MISSING
factck K17    gate.b18.3.arrived           MISSING
factck K17    gate.c2.files                MISSING
factck K17    gate.c2.imports              MISSING
factck K17    gate.c3.held                 MISSING
factck K17    gate.c3.departed             MISSING
factck K17    gate.c4.arrived              MISSING
factck K17    gate.c4.held                 MISSING
# the execution channels are read from the build's own transcript and not from
# the report, so all four survive a tree that compiled nothing -- which is why
# B-16, B-17 and B-18 land on 0.000000 rather than on a scaled fraction
factck K17    gate.b15.exec                1.000000
factck K17    gate.b15.exec.broken         0
factck K17    gate.b15.exec.want           69
factck K17    gate.b16.3.exec              1.000000
factck K17    gate.b16.3.exec.broken       0
factck K17    gate.b16.3.exec.want         5
factck K17    gate.b17.3.exec              1.000000
factck K17    gate.b17.3.exec.broken       0
factck K17    gate.b17.3.exec.want         5
factck K17    gate.b18.4.exec              1.000000
factck K17    gate.b18.4.exec.broken       0
factck K17    gate.b18.4.exec.want         4
# the module channel reads twelve published manifests and never asked javac
factck K17    gate.b15.mod.declared        12
factck K17    gate.b15.mod.derivable       11
factck K17    gate.b15.mod.total           12
factck K17    gate.b15.names               12
# C-1 is full, and it is full at THIS rung's major and not the rung below's
factck K17    gate.c1.classes              730
factck K17    gate.c1.at69.0               730
factck K17    gate.c1.preview              0
# C-5 is full, because Gradle configures a project it cannot compile
factck K17    gate.c5.flag                 -XX:+UseCompactObjectHeaders
factck K17    gate.c5.tasks                12
factck K17    gate.c5.forks                12
factck K17    gate.c5.forks.flagged        12
factck K17    gate.c5.projects             12
factck K17    gate.c5.projects.want        12
factck K17    gate.c5.projects.was         0
probeck K17   ok

# --- K18, K19, K20: the three the generator prints no facts for -------------
# These three move a gate this rung's fixture generator does not model. K18
# puts two frozen buckets back on Class.newInstance and B-13's first leg falls;
# K19 publishes twelve jars with no Java 8 library name in any of them and
# B-12e's four legs fall; K20 publishes no jars at all and B-12e and B-15.2
# fall together. mkfix12.py carries no gate.b12 or gate.b13 model -- it inherits
# those columns from the rung that introduced them -- so there is no literal to
# transcribe for the leg that moved, and the row total in the table above is
# what asserts it: K18 reads B-13 0.866667, K19 and K20 read B-12 0.850000, and
# K20 reads B-15 0.562500 as well.
#
# WHAT THIS GROUP ASSERTS IS THE OTHER HALF -- the channels that must NOT have
# moved, which is the whole reason the three fixtures exist. Step 07 needed
# three fixtures to keep the jar readings apart and this rung still needs them,
# because there are now THREE readings of one jar set and a scan besides:
# B-12e unzips the jars and looks for library names, B-15.2 hands the same
# jars to java.lang.module and asks for a module name, and C-1 reads
# ${WORK}/scan.txt, which is the build's output tree and not the jars at all. A
# fixture that fell in more than one of those at once would say nothing about
# which reading had failed.
factck K18    gate.src.ok25                yes
factck K18    gate.src.ok24                no
factck K18    gate.cp.jars                 12
factck K18    gate.cp.classes              24
factck K18    gate.cp.optional             5
factck K18    gate.cp.collectors           6
factck K18    gate.cp.objects              2
factck K18    gate.cp.jtime                2
factck K18    gate.b15.mod.declared        12
factck K18    gate.b15.mod.derivable       11
factck K18    gate.b15.names               12
factck K18    gate.c1.classes              730
factck K18    gate.c1.at69.0               730
factck K18    gate.c1.preview              0
factck K18    gate.c2.setfrac              1.000000
factck K18    gate.c3.holdfrac             1.000000
factck K18    gate.c4.holdfrac             1.000000
probeck K18   ok
# K19: the jar set is the same size and carries none of the four names. The
# manifests are untouched, so B-15.2 is unmoved; the class files are untouched,
# so C-1 is unmoved. One jar set, three readings, one of them falls.
factck K19    gate.cp.jars                 12
factck K19    gate.cp.classes              24
factck K19    gate.cp.optional             0
factck K19    gate.cp.collectors           0
factck K19    gate.cp.objects              0
factck K19    gate.cp.jtime                0
factck K19    gate.b15.mod.declared        12
factck K19    gate.b15.mod.derivable       11
factck K19    gate.b15.mod.total           12
factck K19    gate.b15.names               12
factck K19    gate.c1.classes              730
factck K19    gate.c1.at69.0               730
probeck K19   ok
# K20: nothing was published. ONE ABSENCE WITH TWO CONSEQUENCES AND NOT THREE
# -- B-12e takes its no-jars branch and B-15.2 takes its no-module-jars branch,
# and C-1 does not move, because the class files are not the jars and a build
# that compiled and published nothing still left them in the tree.
factck K20    gate.cp                      no-jars
factck K20    gate.cp.jars                 MISSING
factck K20    gate.b15.mod                 no-module-jars
factck K20    gate.b15.mod.declared        0
factck K20    gate.b15.mod.derivable       0
factck K20    gate.b15.mod.total           0
factck K20    gate.b15.names               0
factck K20    gate.c1.classes              730
factck K20    gate.c1.at69.0               730
factck K20    gate.c1.preview              0
probeck K20   ok

# --- the eleven collisions, and the fact that parts each one ----------------
# The row loop above asserts every fixture's fifteen cells, so a pair that
# stopped colliding would fail there. What it cannot show is that the pair is
# DISTINGUISHED -- two rows reading the same fifteen numbers are, to the table,
# one row printed twice. sep() reads the two facts files and asserts the named
# key differs, which is the statement the pair was built to make.
#
# Eleven groups collide on all fifteen cells, and a family of five legs at
# 0.02, 0.38, 0.33, 0.25 and 0.02 is why there are so many: a leg that falls to
# zero and a leg that falls to zero look alike whatever made them fall.
#
# THE FIRST GROUP IS THE ONE THAT MATTERS MOST, because all three of its rows
# read 1.000000 everywhere. golden, M1 and Q8 are a full row each, and two of
# them are ADVERSARIAL: M1 writes a thirteenth malformed provider file and Q8
# writes a 189th module import. Both are non-events by construction -- the
# first is a service entry B-15.3 does not read, the second is a module import
# in a file whose set is already right -- and a rehearsal that asserted totals
# alone could not tell a non-event from an unbuilt fixture. These two rows are
# the reason this section asserts facts.
sep golden M1   gate.b15.mod.derivable
sep golden Q8   gate.c2.imports
sep M1     Q8   gate.b15.mod.derivable
sep M1     Q8   gate.c2.imports
# C-4's three halves, one zeroed each. The leg is min(departed, arrived, held)
# and 0.24 x 0 is 0.24 x 0 whichever of the three it was.
sep Q14 Q15     gate.c4.arrived
sep Q14 Q15     gate.c4.departed
sep Q14 Q16     gate.c4.held
sep Q15 Q16     gate.c4.held
# C-2's two halves. Q4 rewrote all 322 imports in 152 files and left 151
# spurious arrivals; Q7 rewrote nothing at all. Same 0.620000, opposite trees.
sep Q4  Q7      gate.c2.files
sep Q4  Q7      gate.c2.spurious
# C-2's set half, one file short each, in two different ways: Q3 is a file with
# a module missing and Q6 a file with the wrong module named. 170 of 171.
sep Q3  Q6      gate.c2.imports
# C-5 counted over twelve tasks and over thirteen. Q19's thirteenth fork is a
# task the golden does not have, so eleven of twelve projects is eleven of
# twelve either way.
sep Q18 Q19     gate.c5.tasks
sep Q18 Q19     gate.c5.forks
# B-18.4's halves. P10 left one setAccessible call standing and P11 converted
# one fewer site; 0.937500 both ways, and the two are opposite mistakes.
sep P10 P11     gate.b18.4.depfrac
sep P10 P11     gate.b18.4.setaccessible
# B-17.4's arrival and departure halves, swapped.
sep N10 N11     gate.b17.4.departed
sep N10 N11     gate.b17.4.arrived
# B-15's structure half against its execution scaling: M4 built the tree wrong
# and M5 broke the run. 0.437500 either way and only one of them is a build
# that works.
sep M4  M5      gate.b15.arrived
sep M4  M5      gate.b15.exec
# B-16.3's population against B-16.3's own text-block census: L6 moved nothing
# and had nothing to move, L14 moved nothing and did.
sep L6  L14     gate.b16.3.textblocks
# B-16.5's arrow census against its size: one arm short, and one arm too many.
sep L12 L13     gate.b16.5.arrow
sep L12 L13     gate.b16.5.size
# and the narrowest of the eleven: K15 and K16 differ in ONE published fact in
# the whole run. Both cost the source penalty its half; only K16's is a raw
# type the toArray reader can see.
sep K15 K16     gate.src.taraw
#
# THREE MORE SHARE FAMILY C's TOTAL WITHOUT SHARING THE ROW -- L2, Q1 and Q20
# all reach 0.980000 -- and they are worth separating for the same reason even
# though the row loop already parts them: the three are the rung's three ways
# of losing a 0.02 leg, and each loses it through a different channel. L2 has
# no scan to read, Q1 has a scan of 730 preview classes, and Q20's compact-
# headers probe never returned.
sep L2  Q1      gate.c1.classes
sep Q1  Q20     gate.c1.at69.0
sep L2  Q20     gate.c5.tasks
note "ninety-two fixtures asserted at the fact level; eleven collisions parted"

# --- and the rehearsal proves it can fail ------------------------------------
# Everything above this line is a clean run against a fixture whose answer was
# written down first, and a rehearsal whose assertions cannot fire agrees with
# anything. So: take the golden -- the one tree that scores every gate in
# full -- move ONE thing, and require the composed blocks to notice. Twenty-one
# perturbations on NINE channels, and every expected figure below is derived in
# the comment above it rather than transcribed, because these trees do not
# exist in mkfix12.py's table.
#
# The nine channels are the point of the count. FOUR ARTEFACT CHANNELS -- the
# source report, the class-file scan, the published jars, and the argument list
# Gradle would hand a test fork -- and the FIVE id lists the one
# behavior.summary is read against: httpids, svcids, tbids, swids, vhids. A
# rung with four artifact channels and five behaviour lists can get any one of
# them crossed with any other, and the only way to see a crossing is to move
# one and assert that the other eight did not.
#
# THE FOURTH ARTEFACT CHANNEL IS THIS RUNG'S, and it is why the count went from
# eight to nine. jvmargs.txt is read by C-5 and by nothing else in the
# verifier; it is also the only channel here that is not a file the build
# leaves behind but the STDOUT OF A SECOND INVOCATION, which is why the fixture
# that does not have one scores a probe failure rather than an empty leg. Its
# perturbation is below, and it is the one that could not have been written at
# any rung before this.
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
# B-15, nor B-16, B-17, B-18, nor family C moves at all, because nothing in any
# of them reads a toarray record.
#
# THE LINE NUMBER IS NOT STEP 11's AND THAT IS NOT A TYPO. The same record was
# perturbed one rung down, where it read `ContainerRegistry.java:59`; here it
# reads :55, because the tree this rung is handed is step 11's ANSWER and that
# answer took four lines out of the top of that file -- every record the report
# carries for it shifts by exactly four, catchparam 77->73, diamond 46->42,
# legacycoll 93->89, methodref and toarray 59->55. A row carried forward
# unedited would have matched nothing, left a byte-identical copy of the
# golden, and scored 1.000000 on every line below -- which is precisely what
# the landing assertion under the sed is for, and the reason it counts bytes
# rather than trusting sed's exit status, which is 0 either way.
# -----------------------------------------------------------------------------
# C-3's EXCUSE, ON A REAL TREE -- THE ONE CLAUSE NO FIXTURE ABOVE CAN REACH
#
# Every one of the 92 fixtures runs with TREE=/nonexistent. That is deliberate
# and it is what makes them cheap: they are report-only trees, and what they
# exercise is the half of every block that reads ${measured} and the frozen
# baselines. But C-3's hold half has one clause that STATS A FILE UNDER ${TREE}
# and reads its bytes, and a clause whose only input is a path that cannot
# exist is a clause the table above can say nothing whatever about.
#
# WHAT THE CLAUSE IS FOR. C-3 holds 43 single-type imports that `import module`
# does not subsume, two of them nested types. A tree that DELETES a nested-type
# import and leaves the bare simple name behind has lost the meaning of the
# file and does not compile, and the hold half is right to refuse it -- that is
# Q12, 41 of 43. But a tree that deletes the import and writes the type's
# ENCLOSING NAME through at every use site has kept the meaning exactly, and
# refusing THAT would be refusing the better answer for looking like the worse
# one. The excuse is the whole of the difference: the import may go if the
# simple name appears nowhere unqualified in that file.
#
# The golden lineage writes `java.util.ServiceLoader.Provider::get` in full and
# is excused on both of the two. Q12 sheds the import and keeps `Provider`, and
# is not. THE TABLE PROVES ONLY THE SECOND HALF. With TREE=/nonexistent the
# `[ -r "${TREE}/${jf}" ]` fails for every row, nobody is ever excused, and Q12
# reads 41 of 43 whether this clause is in the file or deleted from it. An
# unexercised code path in the scorer is a code path that is not scored, and at
# this rung it is the one that separates the right answer from the wrong one.
#
# So it is lifted out by its own two anchor lines and run over four purpose-
# built files under a real TREE. VERBATIM, and by extraction rather than by
# re-typing: an excuse re-implemented here would be a second implementation,
# and the two would agree right up until the day one of them was edited.
echo
echo "== C-3's excuse, on a real tree, which no report-only fixture can reach"

# --- the clause itself, lifted ------------------------------------------------
# Anchored at column 4 on both ends, which is the indentation it has inside
# C-3's guard. A copy of either string at column 1 would be a different
# statement in a different block, and `index(...) == 4` is what says so.
awk 'index($0, ": > \"${WORK}/ji-excuse.txt\"") == 4 { f = 1 }
     f { print }
     index($0, "done < \"${WORK}/ji-hold.txt\"") == 4 { if (f) exit }' \
   "${SRC}" > "${TMP}/excuse.sh"
n=$(grep -c . "${TMP}/excuse.sh" || true)
[ "${n}" = 14 ] \
   || bad "the lifted excuse clause is ${n} line(s), want 14 -- its anchors have moved in ${SRC}"
for tok in 'ji-excuse.txt' 'ji-hold.txt' 'ji-got.txt' 'grep -qxF' '${TREE}/${jf}' \
           'js=${jq##*.}' 'ji-src.txt'; do
   grep -qF -- "${tok}" "${TMP}/excuse.sh" \
      || bad "the lifted excuse clause does not contain ${tok}; the lift took the wrong range"
done
[ "$(grep -cF 'while read -r jf jq' "${TMP}/excuse.sh" || true)" = 1 ] \
   || bad "the lifted excuse clause does not hold exactly one read loop"
bash -n "${TMP}/excuse.sh" \
   || bad "the lifted excuse clause is not valid shell on its own"
note "C-3's excuse clause lifted verbatim from ${SRC}: ${n} lines, one read loop"

# --- and the tally that reads its output, lifted the same way -----------------
# The excused set is not a score by itself; it is ARGV[2] of the awk that
# computes the hold pair, and what has to be asserted is the PAIR. So that awk
# comes across too, by line range between its own two unique lines, with the
# invocation's trailing quote-backslash stripped so it can be run with -f over
# files this section owns rather than over /tests/baseline/jdkimport.txt.
# `FILENAME == ARGV[1] {` IS NOT UNIQUE IN ${SRC} AND THE FIRST READING OF THIS
# ANCHORED ON IT. Two-file awks are the house idiom at this step -- the first
# match is in B-10's block, some four thousand lines up -- so the lift took
# lines 2694..6462, sed printed a program of four thousand lines, awk parsed it
# and produced nothing, and the pair read empty. The instrument, not the
# finding, again. So the anchor is the END line, which IS unique because no
# other awk in the file prints that tuple, and the start is the nearest `awk '`
# at column 4 above it.
tend=$(grep -nF 'print d, nde + 0, h, nh + 0 }' "${SRC}" | head -1 | cut -d: -f1)
tstart=$(awk -v e="${tend:-0}" 'NR < e && $0 == "   awk \x27" { n = NR } END { print n + 1 }' "${SRC}")
[ "$(grep -cF 'print d, nde + 0, h, nh + 0 }' "${SRC}" || true)" = 1 ] \
   || bad "the hold tally's closing line is not unique in ${SRC}; the lift below cannot anchor on it"
if [ -n "${tstart}" ] && [ "${tstart}" -gt 1 ] && [ -n "${tend}" ] \
   && [ "${tend}" -gt "${tstart}" ] && [ "$(( tend - tstart ))" -lt 40 ]; then
   sed -n "${tstart},${tend}p" "${SRC}" | sed '$s/'"'"' \\$//' > "${TMP}/c3tally.awk"
   note "C-3's hold tally lifted verbatim: lines ${tstart}..${tend} of ${SRC}"
else
   bad "C-3's hold tally could not be lifted: start '${tstart:-}' end '${tend:-}'"
   : > "${TMP}/c3tally.awk"
fi

# --- four files, and each one is a different way for the excuse to answer -----
#
#   Full.java   the import is gone AND every use is written out in full. The
#               only row that may be excused. It also carries a line comment
#               and a javadoc sentence that both say "Provider" in prose,
#               because the clause strips comments before it looks and a clause
#               that did not would refuse the one tree that deserves the
#               excuse.
#   Bare.java   the import is gone and `Provider::get` is left bare. This is
#               Q12's shape, it does not compile, and it is refused.
#   Gone.java   named in the hold list and absent from the tree. A file that is
#               gone is a loss, not an excuse -- the clause says so in its own
#               comment and this is the row that holds it to it.
#   Kept.java   the import is still there, so the row is in ji-got.txt and the
#               loop never reaches the stat. It is the pair's numerator.
xt="${TMP}/c3x"
rm -rf "${xt}"; mkdir -p "${xt}/tree/a" "${xt}/work"
cat > "${xt}/tree/a/Full.java" <<'C3X_FULL'
package a;

/**
 * The Service Provider mechanism is described in java.util.ServiceLoader.
 */
public final class Full {
   // Provider::type names the implementation class
   public static java.util.List<Object> all() {
      return java.util.ServiceLoader.load(Object.class).stream()
         .map(java.util.ServiceLoader.Provider::get)
         .toList();
   }
}
C3X_FULL
cat > "${xt}/tree/a/Bare.java" <<'C3X_BARE'
package a;

public final class Bare {
   public static java.util.List<Object> all() {
      return java.util.ServiceLoader.load(Object.class).stream()
         .map(Provider::get)
         .toList();
   }
}
C3X_BARE
cat > "${xt}/tree/a/Kept.java" <<'C3X_KEPT'
package a;

import java.util.ServiceLoader.Provider;

public final class Kept {
   public static Provider<Object> first(java.util.ServiceLoader<Object> l) {
      return l.stream().findFirst().orElseThrow();
   }
}
C3X_KEPT
[ ! -e "${xt}/tree/a/Gone.java" ] || bad "Gone.java must not exist; that is the whole of its row"

cat > "${xt}/jdkimport.txt" <<'C3X_BASE'
# a four-row stand-in for baseline/jdkimport.txt, hold verdicts only
a/Bare.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=hold why=nested
a/Full.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=hold why=nested
a/Gone.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=hold why=nested
a/Kept.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=hold why=nested
C3X_BASE
cat > "${xt}/work/ji-hold.txt" <<'C3X_HOLD'
a/Bare.java java.util.ServiceLoader.Provider
a/Full.java java.util.ServiceLoader.Provider
a/Gone.java java.util.ServiceLoader.Provider
a/Kept.java java.util.ServiceLoader.Provider
C3X_HOLD
cat > "${xt}/work/ji-got.txt" <<'C3X_GOT'
a/Kept.java java.util.ServiceLoader.Provider
C3X_GOT

# The depart excuse's output, which this stand-in leaves empty on purpose: every
# row in the four above is a HOLD verdict, so the depart leg has nothing to
# excuse and both halves of the pair must read 0. It is passed anyway because
# the lifted tally now takes it as ARGV[3] -- omitting it would slide ji-got
# into the excuse slot and the pair would read a denominator of nothing.
: > "${xt}/work/ji-dep-excuse.txt"

# --- run it, over a TREE that is there ---------------------------------------
( TREE="${xt}/tree"; WORK="${xt}/work"; . "${TMP}/excuse.sh" ) \
   || bad "the lifted excuse clause did not run to completion over the real tree"
xgot=$(LC_ALL=C sort "${xt}/work/ji-excuse.txt" 2>/dev/null | tr '\n' ';')
xwant='a/Full.java java.util.ServiceLoader.Provider;'
if [ "${xgot}" = "${xwant}" ]; then
   note "the excuse is granted to Full.java alone: not to the bare simple name, not to the missing file, not to the file that kept its import"
else
   bad "the excuse went to the wrong rows:" "got '${xgot}'" "want '${xwant}'"
fi

awk -f "${TMP}/c3tally.awk" "${xt}/jdkimport.txt" "${xt}/work/ji-excuse.txt" \
   "${xt}/work/ji-dep-excuse.txt" "${xt}/work/ji-got.txt" > "${xt}/tally.txt" 2>/dev/null || :
xt_real=$(cat "${xt}/tally.txt" 2>/dev/null)
if [ "${xt_real}" = "0 0 1 3" ]; then
   note "C-3's hold pair reads 1 of 3 with the excuse live: Full.java left the denominator, Bare.java and Gone.java did not"
else
   bad "C-3's hold pair over the real tree reads '${xt_real:-}', want '0 0 1 3' (depart 0 of 0, hold 1 of 3)"
fi

# --- and the same four rows the way the 92 fixtures see them ------------------
# THIS IS THE GAP, MEASURED. Identical hold list, identical got list, identical
# lifted clause -- only TREE moves, and it moves to what every fixture above
# already uses. Nobody is excused, the denominator goes back to four, and the
# pair reads 1 of 4. The two lines are 3 and 4, and no row in the table above
# can tell them apart.
rm -f "${xt}/work/ji-excuse.txt"
( TREE=/nonexistent; WORK="${xt}/work"; . "${TMP}/excuse.sh" ) \
   || bad "the lifted excuse clause did not run to completion over TREE=/nonexistent"
xn=$(grep -c . "${xt}/work/ji-excuse.txt" 2>/dev/null || true)
awk -f "${TMP}/c3tally.awk" "${xt}/jdkimport.txt" "${xt}/work/ji-excuse.txt" \
   "${xt}/work/ji-dep-excuse.txt" "${xt}/work/ji-got.txt" > "${xt}/tally0.txt" 2>/dev/null || :
xt_non=$(cat "${xt}/tally0.txt" 2>/dev/null)
if [ "${xn}" = 0 ] && [ "${xt_non}" = "0 0 1 4" ]; then
   note "with TREE=/nonexistent the same four rows excuse nobody and read 1 of 4 -- which is why the 92 fixtures above cannot score this clause, and why it is scored here"
else
   bad "TREE=/nonexistent did not close the excuse:" \
       "excused ${xn} (want 0), pair '${xt_non:-}' (want '0 0 1 4')"
fi

echo
echo "== C-3's OTHER excuse, on the leg that pays for absence"
# The hold leg's excuse was rehearsed above. The depart leg's was added later,
# after the floor arm read `departed 6/452` for six imports the golden chain had
# never written, and leaving it unrehearsed would reproduce exactly the
# asymmetry it was added to close: one leg held to a real tree, the other taken
# on the say-so of its own comment. So it is lifted and run the same way.
#
# It lifts on the same column-4 idiom. The clause that BUILDS ji-dep.txt out of
# the baseline is deliberately left behind, exactly as ji-hold.txt is above:
# this section supplies the list, so that what is under test is the predicate
# and not the parse.
awk 'index($0, ": > \"${WORK}/ji-dep-excuse.txt\"") == 4 { f = 1 }
     f { print }
     index($0, "done < \"${WORK}/ji-dep.txt\"") == 4 { if (f) exit }' \
   "${SRC}" > "${TMP}/dexcuse.sh"
nd=$(grep -c . "${TMP}/dexcuse.sh" || true)
[ "${nd}" = 14 ] \
   || bad "the lifted depart-excuse clause is ${nd} line(s), want 14 -- its anchors have moved in ${SRC}"
for tok in 'ji-dep-excuse.txt' 'ji-dep.txt' 'ji-got.txt' 'grep -qxF' '${TREE}/${jf}' \
           'js=${jq##*.}' 'ji-src.txt'; do
   grep -qF -- "${tok}" "${TMP}/dexcuse.sh" \
      || bad "the lifted depart-excuse clause does not contain ${tok}; the lift took the wrong range"
done
[ "$(grep -cF 'while read -r jf jq' "${TMP}/dexcuse.sh" || true)" = 1 ] \
   || bad "the lifted depart-excuse clause does not hold exactly one read loop"
bash -n "${TMP}/dexcuse.sh" \
   || bad "the lifted depart-excuse clause is not valid shell on its own"
note "C-3's depart excuse lifted verbatim from ${SRC}: ${nd} lines, one read loop"

# --- four depart rows, and the second one is the whole point -----------------
#
#   Moved.java    the import is gone and `Provider` is still written bare. This
#                 is a REAL departure -- supplying the simple name is what the
#                 module import is for -- and it must be REFUSED the excuse and
#                 counted. If this row is ever excused the leg has stopped
#                 paying for the work it exists to pay for.
#   Never.java    the import is gone and every use is fully qualified, so the
#                 file never named the type and never had a line to delete.
#                 This is the golden chain's six rows in miniature, and the
#                 only row that may be excused.
#   Missing.java  named in the depart list and absent from the tree. Refused,
#                 by the same `[ -r ]` the hold leg uses, and therefore counted
#                 as a departure. That is deliberate and it is the direction
#                 that costs nothing: a deleted file is punished on the hold
#                 leg and by family B, and C-3 is a min() of the two legs.
#   Kept.java     the import is still there, so the row is in ji-got.txt, the
#                 loop `continue`s before the stat, and the row sits in the
#                 denominator without paying. It is what a tree that did not do
#                 the work looks like.
xd="${TMP}/c3d"
rm -rf "${xd}"; mkdir -p "${xd}/tree/a" "${xd}/work"
cat > "${xd}/tree/a/Moved.java" <<'C3D_MOVED'
package a;

public final class Moved {
   public static Provider<Object> first(java.util.ServiceLoader<Object> l) {
      return l.stream().findFirst().orElseThrow();
   }
}
C3D_MOVED
cat > "${xd}/tree/a/Never.java" <<'C3D_NEVER'
package a;

/** Reaches the nested type by its full name, so it never had a Provider import. */
public final class Never {
   // no Provider import here, and this comment says Provider to prove it is stripped
   public static java.util.ServiceLoader.Provider<Object> first(
         java.util.ServiceLoader<Object> l) {
      return l.stream().findFirst().orElseThrow();
   }
}
C3D_NEVER
cat > "${xd}/tree/a/Kept.java" <<'C3D_KEPT'
package a;

import java.util.ServiceLoader.Provider;

public final class Kept {
   public static Provider<Object> first(java.util.ServiceLoader<Object> l) {
      return l.stream().findFirst().orElseThrow();
   }
}
C3D_KEPT
[ ! -e "${xd}/tree/a/Missing.java" ] || bad "Missing.java must not exist; that is the whole of its row"

cat > "${xd}/jdkimport.txt" <<'C3D_BASE'
# a four-row stand-in for baseline/jdkimport.txt, depart verdicts only
a/Moved.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=depart why=modimport
a/Never.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=depart why=modimport
a/Missing.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=depart why=modimport
a/Kept.java q=java.util.ServiceLoader.Provider kind=nested mod=java.base verdict=depart why=modimport
C3D_BASE
cat > "${xd}/work/ji-dep.txt" <<'C3D_DEP'
a/Kept.java java.util.ServiceLoader.Provider
a/Missing.java java.util.ServiceLoader.Provider
a/Moved.java java.util.ServiceLoader.Provider
a/Never.java java.util.ServiceLoader.Provider
C3D_DEP
cat > "${xd}/work/ji-got.txt" <<'C3D_GOT'
a/Kept.java java.util.ServiceLoader.Provider
C3D_GOT
# no hold rows in this stand-in, so the hold half of the pair is 0 0 throughout
: > "${xd}/work/ji-excuse.txt"

( TREE="${xd}/tree"; WORK="${xd}/work"; . "${TMP}/dexcuse.sh" ) \
   || bad "the lifted depart-excuse clause did not run to completion over the real tree"
dgot=$(LC_ALL=C sort "${xd}/work/ji-dep-excuse.txt" 2>/dev/null | tr '\n' ';')
dwant='a/Never.java java.util.ServiceLoader.Provider;'
if [ "${dgot}" = "${dwant}" ]; then
   note "the depart excuse is granted to Never.java alone: not to the bare simple name, not to the missing file, not to the file that kept its import"
else
   bad "the depart excuse went to the wrong rows:" "got '${dgot}'" "want '${dwant}'"
fi

awk -f "${TMP}/c3tally.awk" "${xd}/jdkimport.txt" "${xd}/work/ji-excuse.txt" \
   "${xd}/work/ji-dep-excuse.txt" "${xd}/work/ji-got.txt" > "${xd}/tally.txt" 2>/dev/null || :
xd_real=$(cat "${xd}/tally.txt" 2>/dev/null)
if [ "${xd_real}" = "2 3 0 0" ]; then
   note "the depart pair reads 2 of 3: Moved and Missing pay, Never leaves both halves, Kept sits in the denominator"
else
   bad "C-3's depart pair over the real tree reads '${xd_real:-}', want '2 3 0 0' (depart 2 of 3)"
fi

# AND THE DIRECTION, WHICH IS THE CLAIM THIS EXCUSE RESTS ON. Close the excuse
# and the same four rows read 3 of 4. 2/3 is 0.666667 and 3/4 is 0.750000, so
# turning the excuse ON LOWERED the leg. That is not a property of these four
# rows: an absent row always counted toward the numerator, so moving it out of
# numerator and denominator together can only lower the ratio or leave it at
# 1.0. An excuse that could raise a score would need an argument before it
# could be added to a closed rung; this one is measured, here, as unable to.
rm -f "${xd}/work/ji-dep-excuse.txt"
( TREE=/nonexistent; WORK="${xd}/work"; . "${TMP}/dexcuse.sh" ) \
   || bad "the lifted depart-excuse clause did not run to completion over TREE=/nonexistent"
dn=$(grep -c . "${xd}/work/ji-dep-excuse.txt" 2>/dev/null || true)
awk -f "${TMP}/c3tally.awk" "${xd}/jdkimport.txt" "${xd}/work/ji-excuse.txt" \
   "${xd}/work/ji-dep-excuse.txt" "${xd}/work/ji-got.txt" > "${xd}/tally0.txt" 2>/dev/null || :
xd_non=$(cat "${xd}/tally0.txt" 2>/dev/null)
if [ "${dn}" = 0 ] && [ "${xd_non}" = "3 4 0 0" ]; then
   note "with the depart excuse closed the same four rows read 3 of 4 -- the excuse lowers the leg and cannot raise it"
else
   bad "TREE=/nonexistent did not close the depart excuse:" \
       "excused ${dn} (want 0), pair '${xd_non:-}' (want '3 4 0 0')"
fi

echo
echo "== can it fail: one field of the golden's report, moved"
pertdir="${TMP}/pert"
rm -rf "${pertdir}"; mkdir -p "${pertdir}"
cp -R "${FIXDIR}/golden" "${pertdir}/PERT"
pertrep="${pertdir}/PERT/report.txt"
sed -i 's|^\(toarray\[2\]=.*ContainerRegistry\.java:55 form=ctorref elem=String\[\] in=\)create$|\1createFrom|' \
   "${pertrep}"

# Assert on the BYTES that the edit landed, and that the original is gone.
# A sed that matched nothing leaves a byte-identical copy of the golden, which
# would then score 1.000000 and be reported as a passing perturbation.
nold=$(grep -cE 'ContainerRegistry\.java:55 .* in=create$'     "${pertrep}" || true)
nnew=$(grep -cE 'ContainerRegistry\.java:55 .* in=createFrom$' "${pertrep}" || true)
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
# ...and the rest of the tree, unmoved. The five family-C legs are the
# load-bearing ones: a source-report edit that moved a family-C leg would mean
# this rung's five gates are reading a rung below's records. B-16, B-17 and
# B-18 are here for the same reason one, two and three rungs nearer -- all
# three read this same report, B-16 over four legs, B-17 over three and B-18
# over three, and a toarray record is in none of those ten populations any more
# than it is in family C's five.
#
# THE LEG NAMES ARE THE ASSERTION AS MUCH AS THE NUMBERS ARE. At step 11 the
# three names below read C2.doprivileged-removal, C3.unnamed-catch and
# C4.varhandle; at this rung those three facts still exist and are still
# 1.000000, but they are spelled B18.2/.3/.4 and family C's own five are
# different gates over different populations. A row carried forward unedited
# would have asserted three true statements about the rung below and none about
# this one -- and would have said nothing at all about C-5, which is the leg
# this rung added and the one with no predecessor to inherit a name from.
factck PERT B14.2.jakarta        1.000000
factck PERT B14.3.javax-held     1.000000
factck PERT B14.5.httpclient     1.000000
factck PERT gate.src.penalty     1
factck PERT B15.2.module-names   1.000000
factck PERT B15.3.platform-lookup 1.000000
factck PERT B15.jpms-services    1.000000
factck PERT B16.java17-constructs 1.000000
factck PERT B17.java21-constructs 1.000000
factck PERT B18.2.doprivileged-removal 1.000000
factck PERT B18.3.unnamed-catch        1.000000
factck PERT B18.4.varhandle            1.000000
factck PERT B18.security-api-removal   1.000000
factck PERT C1.level-69              1.000000
factck PERT C2.module-imports        1.000000
factck PERT C3.import-subsumption    1.000000
factck PERT C4.scoped-value          1.000000
factck PERT C5.compact-headers       1.000000
factck PERT familyC                  1.000000
factck PERT B10.java7-held       1.000000
factck PERT B11.java8lang-held   1.000000
factck PERT B12.java8-library    1.000000
factck PERT B13.java9-10         1.000000
probeck PERT ok

# --- can it fail, on the channel this rung reads two numbers out of ----------
# C-1 reads ${WORK}/scan.txt and nothing else in the verifier does. L1 and Q2
# are the fixtures for the two ways that file can be wrong and mkfix12.py wrote
# both scans; this run writes a THIRD shape by hand, out of this script, and
# requires a number neither of them produces. If write_scan() and the scorer
# ever agreed on a spelling neither arqgate.Scan nor a reader would recognise,
# L1 and Q2 would pass and this would not.
#
# THE SHAPE IS THREE-WAY AND THAT IS THE WHOLE POINT OF IT. Every fixture in
# the table splits the scan two ways at most -- L1 has two majors and one
# minor, Q2 has one major and two minors, Q1 has one of each and both wrong.
# None of them can separate a leg that reads `majorminor.69.0` from one that
# reads `major.69`, because in a two-way scan those two numbers are equal or
# the difference is the whole residue. A tree with a residual major AND a
# preview slice makes them differ by an amount that is neither:
#
#   classes=730, majorminor.66.0=22, majorminor.69.0=662, majorminor.69.65535=46
#   major.66=22, major.69=708  (708 = 662 + 46, which is the trap)
#
#   read as majorminor.69.0:  662/730 = 0.906849, familyC = 0.998137
#   read as major.69:         708/730 = 0.969863, familyC = 0.998795
#
# The first pair is asserted below. A C-1 that had been written against
# `major.` -- which is what every rung from 05 to 11 read, because none of them
# had a minor to care about -- reads the second pair and fails this row. That
# is a statement no rung before this one could make and it is why this
# perturbation exists at all rather than being carried forward from step 11's.
#
# 22 AT 66 AND NOT AT SOME OTHER MAJOR. 66 is HOLD_MAJOR, the level this rung
# is handed, so a residual written at any other number would be describing an
# intermediate build no rung of this ladder ever produced. And 46 preview
# classes at 69.65535 is the OTHER half of the same tree: a build that moved
# most of the reactor to 25 and turned --enable-preview on for part of it,
# which is exactly the half-done shape this leg exists to price.
#
# Nothing else in the file is touched, so the other four family-C legs, all
# nine held B-legs and the source penalty are unmoved. THE PREVIEW COUNT IS
# PUBLISHED AND NOT SUBTRACTED: gate.c1.preview reads 46, the report prints its
# line, and 46 is already absent from the numerator because those classes are
# not at minor 0. A leg that subtracted it as well would charge the tree twice.
echo
echo "== can it fail: the golden's class-file scan, split three ways"
scanpdir="${TMP}/scanp"
rm -rf "${scanpdir}"; mkdir -p "${scanpdir}"
cp -R "${FIXDIR}/golden" "${scanpdir}/SCANPERT"
cat > "${scanpdir}/SCANPERT/scan.txt" <<'SCANPERT_SCAN'
classes=730
major.66=22
major.69=708
majors.distinct=2
majorminor.66.0=22
majorminor.69.0=662
majorminor.69.65535=46
majorminors.distinct=3
preview.classes=46
SCANPERT_SCAN

# The hand-written scan has to add up and has to differ from the golden's, and
# both are asserted rather than assumed. A heredoc cannot fail to land the way
# a sed can, but it CAN be written by someone who did not check the arithmetic,
# and a scan whose parts do not sum to `classes=` describes no tree at all.
nsum=$(awk -F= '/^majorminor\./ { n += $2 } END { print n + 0 }' \
   "${scanpdir}/SCANPERT/scan.txt")
ncls=$(sed -n 's/^classes=//p' "${scanpdir}/SCANPERT/scan.txt" | head -1)
nsrep=$(diff "${FIXDIR}/golden/report.txt" "${scanpdir}/SCANPERT/report.txt" | grep -c '^[<>]' || true)
nsjvm=$(diff "${FIXDIR}/golden/jvmargs.txt" "${scanpdir}/SCANPERT/jvmargs.txt" | grep -c '^[<>]' || true)
if [ "${nsum}" = "${ncls}" ] && [ "${ncls}" = 730 ] && [ "${nsrep}" = 0 ] && [ "${nsjvm}" = 0 ]; then
   note "the hand-written scan sums to its own classes=730; report.txt and jvmargs.txt untouched"
else
   bad "the scan perturbation does not describe a tree:" \
       "majorminor rows sum to ${nsum}, classes=${ncls} (want both 730)," \
       "report.txt changed lines ${nsrep} (want 0), jvmargs.txt ${nsjvm} (want 0)"
fi

_savedfix=${FIXDIR}
FIXDIR=${scanpdir}
run_one SCANPERT
FIXDIR=${_savedfix}

factck SCANPERT gate.c1.classes       730
factck SCANPERT gate.c1.at69.0        662
factck SCANPERT gate.c1.preview       46
factck SCANPERT C1.level-69           0.906849
factck SCANPERT C2.module-imports     1.000000
factck SCANPERT C3.import-subsumption 1.000000
factck SCANPERT C4.scoped-value       1.000000
factck SCANPERT C5.compact-headers    1.000000
factck SCANPERT familyC               0.998137
# ...and every held family, because a class-file scan is family C's channel
# alone. gate.cp.jars is here for the same reason it is under MODPERT below,
# from the other side: the jars are on disk and the scan is a file about them,
# and a B-12 that had learned to read the scan instead of the jars would move
# on this row.
factck SCANPERT B14.java11-jakarta    1.000000
factck SCANPERT B15.2.module-names    1.000000
factck SCANPERT B15.jpms-services     1.000000
factck SCANPERT B16.java17-constructs 1.000000
factck SCANPERT B17.java21-constructs 1.000000
factck SCANPERT B18.security-api-removal 1.000000
factck SCANPERT gate.cp.jars          12
factck SCANPERT gate.src.penalty      1
probeck SCANPERT ok

# --- can it fail, on the channel two rungs below was about -------------------
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
#   familyC = 1.000000, on all FIVE legs, and B-16, B-17 and B-18 all full
#
# 0.930122 IS ALSO K1's B-15 AT THIS RUNG TOO AND THAT COLLISION IS THE
# ASSERTION. K1 publishes twelve jars and names eleven of them; MODPERT
# publishes eleven and names all eleven. The two trees are different in the
# only way that matters to a build engineer and identical to the score, because
# the leg divides by the BASELINE's twelve in both cases. gate.b15.mod.total --
# 12 against 11 -- is the only fact that separates them, and a leg that divided
# by mod.total would read MODPERT at 1.000000 and hand a build that withdrew an
# artifact from publication a perfect name score.
#
# THE OTHER HALF OF THIS ROW IS WHAT DID NOT MOVE. mod.derivable falls to 10
# and B-15 does not notice, which is the same statement M1 makes from the other
# direction: this rung publishes readability and does not score it.
#
# AND C-5 DOES NOT MOVE EITHER, which is new here. The probe asks Gradle about
# twelve PROJECTS and this deletes one project's published JAR; a C-5 that had
# taken its denominator from the jars rather than from modnames.txt would read
# 12 flagged projects out of 11 and clamp to 1.000000 by luck, or 11 of 11 and
# pass, and either way would have stopped measuring the thing it names.
echo
echo "== can it fail: one of the golden's twelve jars, gone"
modpdir="${TMP}/modp"
rm -rf "${modpdir}"; mkdir -p "${modpdir}"
cp -R "${FIXDIR}/golden" "${modpdir}/MODPERT"
gonejar="${modpdir}/MODPERT/jars/arquillian-testng-1.0.0-SNAPSHOT.jar"
[ -f "${gonejar}" ] || bad "the golden does not publish arquillian-testng; this block was written against a jar that exists"
rm -f "${gonejar}"
nleft=$(ls "${modpdir}/MODPERT"/jars/*.jar 2>/dev/null | grep -c . || true)
nrep=$(diff "${FIXDIR}/golden/report.txt"  "${modpdir}/MODPERT/report.txt"  | grep -c '^[<>]' || true)
nscan=$(diff "${FIXDIR}/golden/scan.txt"   "${modpdir}/MODPERT/scan.txt"   | grep -c '^[<>]' || true)
njvm=$(diff "${FIXDIR}/golden/jvmargs.txt" "${modpdir}/MODPERT/jvmargs.txt" | grep -c '^[<>]' || true)
if [ "${nleft}" = 11 ] && [ ! -f "${gonejar}" ] && [ "${nrep}" = 0 ] && [ "${nscan}" = 0 ] \
   && [ "${njvm}" = 0 ]; then
   note "eleven jars left, arquillian-testng gone, report.txt scan.txt jvmargs.txt untouched"
else
   bad "the jar perturbation did not land as described:" \
       "jars left ${nleft} (want 11), report.txt changed lines ${nrep} (want 0)," \
       "scan.txt ${nscan} (want 0), jvmargs.txt ${njvm} (want 0)"
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
factck MODPERT gate.c1.classes         730
factck MODPERT gate.c1.at69.0          730
factck MODPERT C1.level-69             1.000000
factck MODPERT C5.compact-headers      1.000000
factck MODPERT gate.c5.projects        12
factck MODPERT gate.c5.forks           12
factck MODPERT familyC                 1.000000
factck MODPERT B16.java17-constructs   1.000000
factck MODPERT B17.java21-constructs   1.000000
factck MODPERT B18.security-api-removal 1.000000
factck MODPERT B14.java11-jakarta      1.000000
probeck MODPERT ok
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

# --- can it fail, on the channel that did not exist one rung ago -------------
# The fourth artefact channel, and the fourth perturbation. jvmargs.txt is the
# stdout of the `arqTestJvmArgs` probe: twelve rows, one per project, each
# carrying `-XX:+UseCompactObjectHeaders` twice -- once in the task's own
# `jvmArgs` and once again in `allJvmArgs`, which is what the init script
# prints and what a real Gradle reactor hands back.
#
# Q18 and Q19 are the table's two fixtures on this channel and both move the
# ROW COUNT: Q18 strips the flag from arquillian-junit's single row, Q19
# appends a thirteenth row for a task the baseline has never heard of. Both
# land on C-5 = 11/12 = 0.916667 from opposite directions. THIS ONE MOVES
# NEITHER COUNT. Twelve rows before and twelve after; two of them lose the
# flag, both occurrences, and the projects they belong to fall out of the
# `hit[p] == seen[p]` test:
#
#   gate.c5.tasks 12, gate.c5.forks 12, gate.c5.forks.flagged 10
#   gate.c5.projects 10 of a want of 12
#   C-5 = 10/12 = 0.833333
#   familyC = 0.02 + 0.96 + 0.02*0.833333 = 0.996667
#
# NEITHER FIGURE APPEARS ANYWHERE IN THE TABLE ABOVE, which is the point: the
# ninety-two rows produce exactly three values of C-5 -- 0.000000 once,
# 0.916667 twice and 1.000000 eighty-six times -- so a C-5 that had been
# written to read the FORK count rather than the PROJECT count would score
# 10/12 here as well and pass, but would score Q19 at 12/13 and fail that row
# instead. Running both is what separates them.
#
# TWO PROJECTS AND NOT ONE, for the same reason. One project losing the flag is
# Q18 again with a different name on it; two is a number the leg can only reach
# by counting projects, and it is also the shape a real half-done migration has
# -- a `subprojects { test { jvmArgs ... } }` block added to the root build
# file with two projects that configure their own Test task and override it.
#
# arquillian-protocol-jmx-osgi and arquillian-testenricher-ejb are chosen
# because neither is arquillian-junit, which Q18 already spends, and neither is
# arquillian-testng, whose jar MODPERT above deletes. Three perturbations that
# named the same project would have been three statements about that project.
echo
echo "== can it fail: two of the golden's twelve projects fork without the flag"
copdir="${TMP}/cop"
rm -rf "${copdir}"; mkdir -p "${copdir}"
cp -R "${FIXDIR}/golden" "${copdir}/COPERT"
copjvm="${copdir}/COPERT/jvmargs.txt"
sed -i -e '/^ARQ_TESTJVM arquillian-protocol-jmx-osgi /s/ -XX:+UseCompactObjectHeaders//g' \
       -e '/^ARQ_TESTJVM arquillian-testenricher-ejb /s/ -XX:+UseCompactObjectHeaders//g' \
   "${copjvm}"

# Count the bytes, not the sed's exit status. Twelve rows before and after, the
# flag on ten of them, gone from both occurrences on the two named, and 24
# occurrences down to 20 -- a sed that had dropped the `g` would leave 22, both
# named rows would still carry one occurrence each, the leg's `index($0, flag)`
# would still find it, both projects would still read `hit == seen` and C-5
# would still be 1.000000. The row would pass while measuring a file the
# perturbation had barely touched.
crow=$(grep -c '^ARQ_TESTJVM ' "${copjvm}" || true)
cflag=$(grep -c 'UseCompactObjectHeaders' "${copjvm}" || true)
cocc=$(grep -o 'UseCompactObjectHeaders' "${copjvm}" | grep -c . || true)
cnamed=$(grep -E '^ARQ_TESTJVM (arquillian-protocol-jmx-osgi|arquillian-testenricher-ejb) ' \
   "${copjvm}" | grep -c 'UseCompactObjectHeaders' || true)
cdiff=$(diff "${FIXDIR}/golden/jvmargs.txt" "${copjvm}" | grep -c '^[<>]' || true)
if [ "${crow}" = 12 ] && [ "${cflag}" = 10 ] && [ "${cocc}" = 20 ] \
   && [ "${cnamed}" = 0 ] && [ "${cdiff}" = 4 ]; then
   note "twelve rows still, the flag on ten of them, both occurrences gone from two"
else
   bad "the jvmargs perturbation did not land as described:" \
       "rows ${crow} (want 12), flagged rows ${cflag} (want 10)," \
       "flag occurrences ${cocc} (want 20), on the two named ${cnamed} (want 0)," \
       "changed lines ${cdiff} (want 4)"
fi

_savedfix=${FIXDIR}
FIXDIR=${copdir}
run_one COPERT
FIXDIR=${_savedfix}

probeck COPERT ok
factck COPERT gate.c5.tasks          12
factck COPERT gate.c5.forks          12
factck COPERT gate.c5.forks.flagged  10
factck COPERT gate.c5.projects       10
factck COPERT gate.c5.projects.want  12
factck COPERT C5.compact-headers     0.833333
factck COPERT familyC                0.996667
# ...and the four legs that share the family with it, unmoved -- which is the
# assertion that the fifth artefact reaches C-5 and reaches nothing else. C-1
# is the one to read: it and C-5 are the only two legs at this rung that do not
# come off the source report, they are worth the same 0.04, and a run that
# crossed their two channels would show up here and nowhere else in the file.
factck COPERT C1.level-69            1.000000
factck COPERT gate.c1.classes        730
factck COPERT C2.module-imports      1.000000
factck COPERT C3.import-subsumption  1.000000
factck COPERT C4.scoped-value        1.000000
factck COPERT gate.src.penalty       1
factck COPERT B14.java11-jakarta     1.000000
factck COPERT B15.jpms-services      1.000000
factck COPERT gate.b15.mod.total     12
factck COPERT B16.java17-constructs  1.000000
factck COPERT B17.java21-constructs  1.000000
factck COPERT B18.security-api-removal 1.000000
factck COPERT gate.cp.jars           12

# --- can it fail, sixteen more times: the recorded behaviour, broken ---------
# FIVE of the fourteen leg columns are scaled by what family A still
# reproduces, and they are scaled by FIVE DIFFERENT LISTS read out of ONE file:
# B-14.5 by baseline/httpids.txt's eighteen exchange ids, B-15.3 by
# baseline/svcids.txt's sixty-nine service lookups, B-16.3 by
# baseline/tbids.txt's five document recordings, B-17.3 by
# baseline/swids.txt's five dispatch recordings, and B-18.4 by
# baseline/vhids.txt's four field-access recordings. B-10's nio quarter reads a
# sixth. No fixture in the table exercises the http list, because every
# fixture's summary carries only the ids its own catalogue row asked for.
#
# AND NOT ONE OF THE FIVE IS IN FAMILY C. That is the sentence this section is
# written around and it is the first time it has been true since the scalings
# began. At step 09 tbids.txt scaled `C3.text-blocks`; at step 10 the same five
# ids scaled `B16.3.text-blocks` while `C3` named the pattern switch and
# swids.txt; at step 11 swids.txt scaled `B17.3.pattern-switch` while `C4`
# named the VarHandle and vhids.txt. At THIS rung vhids.txt scales
# `B18.4.varhandle`, every list has moved into the held families, and family
# C's five legs are scaled by NOTHING -- because C-1 reads a class-file scan,
# C-2 and C-3 read import declarations, C-4 reads a field declaration, and C-5
# reads an argument list, and not one of those five is a behaviour a test
# transcript could record.
#
# WHICH MAKES THE UNMOVED ROWS THE LOAD-BEARING HALF OF THIS SECTION RATHER
# THAN ITS REASSURING ONE. Every rung from 09 to 11 could check its family-C
# scaling by moving a list and reading familyC; this rung has to check that
# familyC does NOT move, sixteen times, and the only thing that makes that a
# measurement rather than an absence is that five of the six lists DO move
# something else in the same run. A section carried forward from step 11 with
# its names renamed would have asserted a scaling on C-4 that this rung's C-4
# does not have, come out to the right arithmetic on the four inherited lists,
# and quietly certified a leg nobody had wired up.
#
# Those scalings exist because the source legs above them are all SHAPE --
# records gone, types declared, a call site present -- and a file that deletes
# the method body has every one of them. Family A notices, but eighteen of 939
# transcripts is 0.005751 of the step against B-14's weight, five is 0.001597
# against B-16.3's and against B-17.3's, four is 0.001278 against B-18.4's, and
# every one of them is a trade in favour of faking it. The scalings are what
# close them. So what this rehearsal has to prove is not that they exist but
# that each is a FRACTION of its OWN list read at run time -- not a boolean,
# not a halving, and not one of the other four.
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
# on any breakage would read 0.500000 in BOTH runs. B-15, B-16, B-17, B-18 and
# family C have to stay at 1.000000 in both, and those five are the assertion
# that the five provisos read their own lists and not each other's.
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
# AND THE SIXTH, WHICH NO RUN IN THIS SECTION DOCTORS. nioids.txt is read here
# only as a filename, by the disjointness loop below and by the unmoved rows at
# the foot of the file; nothing between them ever loads its contents into a
# variable. It still gets a length preflight, because GONE asserts
# `gate.b10.nio.behaviour.broken=4` and that 4 is this file's line count and
# nothing else -- a fifth id would make GONE fail on a number that was right.
nioids=$(grep '^[a-z]' /tests/baseline/nioids.txt)
nnio=$(printf '%s\n' "${nioids}" | grep -c .)
[ "${nnio}" = 4 ] || bad "baseline/nioids.txt names ${nnio} ids, this block was written against 4"
# The six lists are disjoint, which is the precondition every assertion in
# this section rests on. An id that appeared on two of them would make one
# perturbation move two gates and the "unmoved" rows below would be wrong
# rather than reassuring. Six lists is fifteen pairs.
#
# THE SIX ARE THE SAME SIX AS ONE RUNG AGO AND THIS RUNG ADDED NONE, which is
# worth saying because the rung DID add a leg with a run-time reading -- C-5
# asks Gradle what it would hand a test fork. That reading is not a recorded
# behaviour and it is not in behavior.summary: it is the fourth ARTEFACT
# channel, it has its own perturbation above, and the reason it is not a
# seventh entry in this loop is that no test transcript can record a JVM
# argument. A rehearsal that had put it here would have been asserting that
# `gate.c5.*` reads a list it has never opened.
#
# The one to read twice is still tbids against swids: five document recordings
# and five dispatch recordings, the two lists whose legs were both called C-3
# at one rung or another, and THE SAME LENGTH since step 10 -- which makes this
# loop matter MORE at this rung than it did three ago, not less. At five and
# five each produces the other's fractions exactly, so nothing but this loop
# and the unmoved rows at the foot of the file says that what the web runner
# writes into an HTML page and what the deployment packager answers to an
# archive no arm accepts are different behaviours of different code.
#
# VHIDS AND NIOIDS ARE BOTH FOUR LONG AND THAT PAIR IS NOW THE OTHER ONE THE
# ARITHMETIC CANNOT SEPARATE. 0.750000 and 0.500000 are quarters, no fifth is
# either of them, so the vh triple is still separable from the two fives by
# number alone -- but two four-long lists produce each other's fractions
# exactly, and B-10's quarter and B-18.4's scaling would be indistinguishable
# by any number either of them prints. Two unseparable pairs out of fifteen,
# and this loop is what carries both.
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
# ...and the two artefacts this rung added to the golden are untouched too. A
# `cp -R` that had dropped one of them would leave every family-C assertion in
# the sixteen runs below reading a leg that could not have scored, and the
# whole "family C does not move" claim would be an artefact of the copy.
nscn=$(diff "${FIXDIR}/golden/scan.txt"    "${behavdir}/BEHAVALL/scan.txt"    | grep -c '^[<>]' || true)
njvm=$(diff "${FIXDIR}/golden/jvmargs.txt" "${behavdir}/BEHAVALL/jvmargs.txt" | grep -c '^[<>]' || true)
if [ "${nall}" = 18 ] && [ "${nhalf}" = 9 ] && [ "${nmiss}" = 9 ] \
   && [ "${nbuild}" = 1 ] && [ "${nrep}" = 0 ] \
   && [ "${nscn}" = 0 ] && [ "${njvm}" = 0 ]; then
   note "the summaries landed: 18 broken (9 differ, 9 missing) and 9 broken, report.txt scan.txt jvmargs.txt untouched"
else
   bad "the behaviour perturbation did not land as described:" \
       "all ${nall} (want 18), half ${nhalf} (want 9), missing ${nmiss} (want 9)," \
       "build lines ${nbuild} (want 1), report.txt changed lines ${nrep} (want 0)," \
       "scan.txt ${nscn} (want 0), jvmargs.txt ${njvm} (want 0)"
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
# it -- it is thirteenth in the list -- so the quarter stays.
factck BEHAVALL  gate.b14.c5.redirects.honoured 0
factck BEHAVHALF gate.b14.c5.redirects.honoured 1
factck BEHAVALL  B14.5.httpclient      0.000000
factck BEHAVHALF B14.5.httpclient      0.500000
factck BEHAVALL  B14.java11-jakarta    0.400000
factck BEHAVHALF B14.java11-jakarta    0.700000
# ...and the sixty-nine, the two fives, the four, and B-10's four. Six lists,
# six provisos, and only one of them fired. Family C is asserted leg by leg
# rather than through familyC alone, because at this rung familyC is a sum of
# five terms none of which reads a summary: a total that held while one of its
# five had quietly acquired a scaling and another had quietly lost 0.02 would
# be a total that agreed with the wrong tree.
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
   factck "${f}" gate.b18.4.exec.broken 0
   factck "${f}" gate.b18.4.exec      1.000000
   factck "${f}" B18.4.varhandle      1.000000
   factck "${f}" B18.security-api-removal 1.000000
   factck "${f}" C1.level-69          1.000000
   factck "${f}" C2.module-imports    1.000000
   factck "${f}" C3.import-subsumption 1.000000
   factck "${f}" C4.scoped-value      1.000000
   factck "${f}" C5.compact-headers   1.000000
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
#   VHALL    all four, two `differ` and two `missing`. B-18.4 = 0.000000, so
#            B-18 = (0.46 + 0.26)/0.96 = 0.750000
#   VHTWO    two of the four. 2/4 = 0.500000, so
#            B-18 = (0.72 + 0.24*0.500000)/0.96 = 0.875000
#   VHONE    one. 3/4 = 0.750000, so B-18 = 0.937500
#
# SIXTY-NINE CANNOT PRODUCE 0.600000, BUT FIVE AND FIVE PRODUCE EACH OTHER'S
# FIGURES EXACTLY. Against svcids a crossed pair does not merely score wrongly,
# it scores impossibly -- five cannot produce 0.985507. Against tbids there is
# no impossibility left at all: tbids.txt and swids.txt are the same length, so
# TBTWO and SWTWO both read 0.600000 and TBONE and SWONE both read 0.800000.
# The arithmetic stopped separating those two lists at step 10 and it has not
# started again.
#
# AND THE VH TRIPLE IS NO LONGER THE ONE THE ARITHMETIC SAVES EITHER, which is
# what changed at this rung. vhids.txt is four long and quarters are still not
# fifths -- 0.750000 and 0.500000 are unreachable from a five-long list -- but
# nioids.txt is ALSO four long, so B-10's quarter and B-18.4's scaling produce
# each other's fractions exactly, and the only reason no run below crosses them
# is the disjointness loop and the unmoved rows. Both of this rung's
# unseparable pairs are same-length pairs, and neither of them can be closed by
# choosing better numbers.
#
# WHAT SEPARATES THE TWO FIVES IS WHICH COLUMN MOVED, and that is the whole of
# it. TBTWO moves B16.3.text-blocks to 0.600000 and leaves
# B17.3.pattern-switch at 1.000000; SWTWO moves B17.3.pattern-switch to
# 0.600000 and leaves B16.3.text-blocks at 1.000000. A scorer handed one list
# for both legs would move both columns in both runs, and the UNMOVED rows
# below are the only place that is visible -- as are the disjointness loop
# above and L8 and N8 up in the table. Read together they are a stronger claim
# than the arithmetic ever was, because they name the fact rather than infer
# it from a fraction; but they now have to carry it alone, so none of them is
# optional.
#
# NONE OF THE TWELVE TOUCHES familyC, AND THAT IS THE ROW TO READ. One rung ago
# the VH triple was family C's own and moved familyC through C-4; here it moves
# B-18 and family C is flat across all twelve. The four-way split of the twelve
# runs across B-15, B-16, B-17 and B-18 is itself the claim that the four
# provisos live on four gates, and the sixty family-C rows underneath are the
# claim that none of them lives on a fifth.
#
# THE COINCIDENCES, AND WHY THEY ARE NOT A PROBLEM. SVCALL's 0.437500 is K17's,
# M4's and M5's B-15; TBALL's 0.711111 is L6's and L14's B-16 and TBTWO's
# 0.884444 is L8's; SWALL's 0.322917 is N15's B-17 and SWTWO's 0.729167 is
# N8's; VHALL's 0.750000 is P13's B-18, VHTWO's 0.875000 is P14's and VHONE's
# 0.937500 is P10's and P11's. The VH triple collides on ALL THREE rows, which
# is new at this rung and follows from the leg having moved into a family whose
# fixtures spend it. Every one of those table rows reaches the number through a
# SOURCE fact -- a converted site missing, a held site moved -- on a summary
# that is clean, and every run here reaches it through a broken summary over
# source that is whole. The `gate.*.exec.broken` rows below are what separate
# them, and they are asserted for that reason and not for the arithmetic.
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
   gotj=$(diff "${FIXDIR}/golden/jvmargs.txt" "${behavdir}/${v}/jvmargs.txt" | grep -c '^[<>]' || true)
   if [ "${got}" = "${nbrk}" ] && [ "${gotd}" = "${ndiff}" ] \
      && [ "${gotb}" = 1 ] && [ "${gotr}" = 0 ] && [ "${gotj}" = 0 ]; then
      note "${v}: ${nbrk} of ${lst} broken, ${ndiff} as differ, report.txt and jvmargs.txt untouched"
   else
      bad "${v} did not land as described: broken ${got} (want ${nbrk})," \
          "differ ${gotd} (want ${ndiff}), build lines ${gotb} (want 1)," \
          "report.txt changed lines ${gotr} (want 0), jvmargs.txt ${gotj} (want 0)"
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
# which did not move at this rung, did not move one rung ago, and DID move two
# rungs ago when swids.txt was family C's own list.
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
   factck "${f}" C1.level-69             1.000000
   factck "${f}" C2.module-imports       1.000000
   factck "${f}" C3.import-subsumption   1.000000
   factck "${f}" C4.scoped-value         1.000000
   factck "${f}" C5.compact-headers      1.000000
   factck "${f}" familyC                 1.000000
done

# --- the four
# Last rung's family-C list, inherited whole, and the one list in the image
# whose leg changed families between two consecutive rungs without changing
# either its length or its ids. Four recorded field accesses: two `differ` and
# two `missing` in VHALL, so the alternation is exercised on it too and not
# only on the three it inherited from further down.
factck VHALL  gate.b18.4.exec.want   4
factck VHALL  gate.b18.4.exec.broken 4
factck VHALL  gate.b18.4.exec        0.000000
factck VHALL  B18.4.varhandle        0.000000
factck VHALL  B18.security-api-removal 0.750000
factck VHTWO  gate.b18.4.exec.want   4
factck VHTWO  gate.b18.4.exec.broken 2
factck VHTWO  gate.b18.4.exec        0.500000
factck VHTWO  B18.4.varhandle        0.500000
factck VHTWO  B18.security-api-removal 0.875000
factck VHONE  gate.b18.4.exec.want   4
factck VHONE  gate.b18.4.exec.broken 1
factck VHONE  gate.b18.4.exec        0.750000
factck VHONE  B18.4.varhandle        0.750000
factck VHONE  B18.security-api-removal 0.937500
# The four setAccessible calls that left, the eight lookup-and-unreflect pairs
# that arrived, the ten calls that had to stay and the ten sites the scan
# still finds are all in the report in all three runs. THE SCALING MULTIPLIES
# A MIN THAT IS 1.000000, which is the only shape in which the three B-18
# figures above are readable as the scaling and not as a source loss: a tree
# that had converted three of the four heads would reach 0.750000 on the
# min() instead, and B-18.4's three fractions are what tell the two apart.
for f in VHALL VHTWO VHONE; do
   factck "${f}" gate.b18.4.departed       4
   factck "${f}" gate.b18.4.depfrac        1.000000
   factck "${f}" gate.b18.4.arrived        8
   factck "${f}" gate.b18.4.arrfrac        1.000000
   factck "${f}" gate.b18.4.held           10
   factck "${f}" gate.b18.4.holdfrac       1.000000
   factck "${f}" gate.b18.4.handles        8
   factck "${f}" gate.b18.4.setaccessible  10
   factck "${f}" B18.2.doprivileged-removal 1.000000
   factck "${f}" B18.3.unnamed-catch        1.000000
   factck "${f}" C1.level-69                1.000000
   factck "${f}" C5.compact-headers         1.000000
   factck "${f}" familyC                    1.000000
   factck "${f}" B17.java21-constructs      1.000000
done

# --- and the five lists each run did not touch
# Twelve runs, six provisos, one moved each time. This is the loop that says
# the lists are READ separately at run time and not merely STORED separately
# on disk: every one of these rows would move if two of the six files were
# ever given to the same awk.
#
# THE ROWS THAT DID NOT EXIST A RUNG AGO ARE THE ONES TO READ. At step 09 this
# was three triples against four provisos; here it is four triples against six,
# and the likeliest single mistake at THIS rung is not a crossed list at all --
# it is a family-C leg that acquired a scaling, because family C has had one at
# every rung since 09 and has none at this one. That mistake passes every
# arithmetic check in the section above and shows up only in the familyC and
# C1..C5 rows, which is why they are asserted in every one of the twelve runs
# rather than once at the end.
for f in SVCALL SVCHALF SVCONE; do
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.3.text-blocks      1.000000
   factck "${f}" B16.java17-constructs  1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" B17.3.pattern-switch   1.000000
   factck "${f}" B17.java21-constructs  1.000000
   factck "${f}" gate.b18.4.exec.broken 0
   factck "${f}" B18.4.varhandle        1.000000
   factck "${f}" B18.security-api-removal 1.000000
   factck "${f}" familyC                1.000000
   factck "${f}" C5.compact-headers     1.000000
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
   factck "${f}" gate.b18.4.exec.broken 0
   factck "${f}" B18.4.varhandle        1.000000
   factck "${f}" B18.security-api-removal 1.000000
   factck "${f}" familyC                1.000000
   factck "${f}" C5.compact-headers     1.000000
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done
# SW and VH each assert fewer rows here, because each of them already asserted
# the other family's total up in its own block: SWALL..SWONE carry familyC and
# all five C legs there, VHALL..VHONE carry B17.java21-constructs, familyC,
# C-1 and C-5. What is left here is the exec.broken row, which is the one that
# says the untouched list was READ and found clean rather than never opened.
for f in SWALL SWTWO SWONE; do
   factck "${f}" gate.b15.exec.broken   0
   factck "${f}" B15.3.platform-lookup  1.000000
   factck "${f}" B15.jpms-services      1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.3.text-blocks      1.000000
   factck "${f}" B16.java17-constructs  1.000000
   factck "${f}" gate.b18.4.exec.broken 0
   factck "${f}" B18.4.varhandle        1.000000
   factck "${f}" B18.security-api-removal 1.000000
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
   factck "${f}" B17.3.pattern-switch   1.000000
   factck "${f}" gate.b14.c5.exec.broken 0
   factck "${f}" B14.java11-jakarta     1.000000
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held         1.000000
done

# --- and the file that is not there at all -----------------------------------
# Every run above doctored a summary. This one deletes it, which is the state a
# tree reaches when family A never ran -- a build that failed, a harness that
# died, a container that went away -- and it is the state in which every
# proviso in the file has to be READ AS UNSATISFIED rather than skipped. The
# rule is B-10's and it has been carried unchanged since step 03: a proviso
# that cannot be checked is not satisfied.
#
#   B-10's nio quarter   halved, not zeroed -- the four ids are a proviso on a
#                        third, and the block halves that third
#                        -> B-10 = 0.4 + 0.3 + 0.3*0.5 = 0.850000
#   B-14.5    0.000000 and the redirect quarter withdrawn with it
#             -> B-14 = (0.07+0.17+0.12)/0.90 = 0.400000
#   B-15.3    0.000000 over an unchanged min() of 1.000000
#             -> B-15 = 0.35/0.80 = 0.437500
#   B-16.3    0.000000 over an unchanged min() of 1.000000
#             -> B-16 = (0.20+0.36+0.08)/0.90 = 0.711111
#   B-17.3    0.000000 over an unchanged min() of 1.000000
#             -> B-17 = (0.17+0.14)/0.96 = 0.322917
#   B-18.4    0.000000 over an unchanged min() of 1.000000
#             -> B-18 = (0.46+0.26)/0.96 = 0.750000
#   family C  UNMOVED. 1.000000, all five legs.
#
# THE LAST LINE IS THE ONE THIS RUNG ADDS AND IT IS THE SHARPEST THING IN THE
# FILE. Step 09's GONE asserted four numbers, step 10's five, step 11's six --
# and step 11's sixth was family C's own, because vhids.txt was family C's list
# and the absent summary took 0.24 out of the rung's own score. HERE THE SIXTH
# IS B-18.4 AND FAMILY C DOES NOT MOVE. Six provisos are withdrawn across five
# HELD legs and this rung's five legs all stand, because not one of them reads
# a summary: a class-file scan, three source populations and an argument list
# are all artefacts family A's absence does not touch.
#
# WHICH IS WHY familyC IS THE WEAKEST ASSERTION HERE AND THE CHANNELS UNDER IT
# ARE THE STRONG ONES. 1.000000 is what sixty-eight of the ninety rows in the
# TABLE read in that column; it is the single least discriminating number in
# this file. What makes this run a reading rather than a coincidence is the
# fifteen gate.c* rows below it -- 730 classes at major 69 minor 0, 0 preview,
# 171 sets and 151 clean files, 452 departed and 43 held, one converted field
# and seven that stayed, twelve forks and twelve flagged. A family C that had
# quietly acquired a summary-scaled leg would still print 1.000000 on one of
# the fixtures somewhere; it could not print all fifteen of those with no
# summary on disk.
#
# The coincidences, as ever, and as ever they do not matter: 0.850000 is not in
# the B-10 column at all (K11's is B-14, K19's and K20's are B-12), 0.400000
# is in no B-14 cell, 0.437500 is K17's, M4's and M5's B-15, 0.711111 is L6's
# and L14's B-16, 0.322917 is N15's B-17, and 0.750000 is P13's B-18. No row in
# the TABLE reads all seven of those at once, and no row reads any of them with
# every source population intact.
echo
echo "== can it fail: no summary at all"
rm -rf "${behavdir:?}/GONE"
cp -R "${FIXDIR}/golden" "${behavdir}/GONE"
rm -f "${behavdir}/GONE/behavior.summary"
gonescan=$(diff "${FIXDIR}/golden/scan.txt"    "${behavdir}/GONE/scan.txt"    | grep -c '^[<>]' || true)
gonejvm=$(diff  "${FIXDIR}/golden/jvmargs.txt" "${behavdir}/GONE/jvmargs.txt" | grep -c '^[<>]' || true)
gonerep=$(diff  "${FIXDIR}/golden/report.txt"  "${behavdir}/GONE/report.txt"  | grep -c '^[<>]' || true)
if [ ! -r "${behavdir}/GONE/behavior.summary" ] \
   && [ "${gonescan}" = 0 ] && [ "${gonejvm}" = 0 ] && [ "${gonerep}" = 0 ]; then
   note "the summary is gone, and nothing else with it -- report, scan and jvmargs all intact"
else
   bad "GONE did not land as described: summary readable" \
       "$([ -r "${behavdir}/GONE/behavior.summary" ] && echo yes || echo no)," \
       "scan.txt changed lines ${gonescan}, jvmargs.txt ${gonejvm}, report.txt ${gonerep} (want 0 0 0)"
fi
_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one GONE
FIXDIR=${_savedfix}
factck GONE gate.b10.nio.behaviour.broken 4
factck GONE B10.java7-held          0.850000
factck GONE gate.b14.c5.exec.want   18
factck GONE gate.b14.c5.exec.broken 18
factck GONE gate.b14.c5.exec        0.000000
factck GONE gate.b14.c5.redirects.honoured 0
factck GONE B14.5.httpclient        0.000000
factck GONE B14.java11-jakarta      0.400000
factck GONE gate.b15.exec.want      69
factck GONE gate.b15.exec.broken    69
factck GONE gate.b15.exec           0.000000
factck GONE B15.3.platform-lookup   0.000000
factck GONE B15.jpms-services       0.437500
factck GONE gate.b16.3.exec.want    5
factck GONE gate.b16.3.exec.broken  5
factck GONE gate.b16.3.exec         0.000000
factck GONE B16.3.text-blocks       0.000000
factck GONE B16.java17-constructs   0.711111
factck GONE gate.b17.3.exec.want    5
factck GONE gate.b17.3.exec.broken  5
factck GONE gate.b17.3.exec         0.000000
factck GONE B17.3.pattern-switch    0.000000
factck GONE B17.java21-constructs   0.322917
factck GONE gate.b18.4.exec.want    4
factck GONE gate.b18.4.exec.broken  4
factck GONE gate.b18.4.exec         0.000000
factck GONE B18.4.varhandle         0.000000
factck GONE B18.security-api-removal 0.750000

# The shape is untouched in all six, which is what makes this the file-deleted
# case rather than the work-undone one.
factck GONE gate.b14.c5.legacy      0
factck GONE gate.b14.c5.types       3
factck GONE gate.b14.c5.redirects   1
factck GONE gate.src.penalty        1
factck GONE gate.b15.departed       1.000000
factck GONE gate.b15.arrived        1.000000
factck GONE gate.b15.held           1.000000
factck GONE B15.2.module-names      1.000000
factck GONE gate.b16.3.moved        1
factck GONE gate.b16.3.movefrac     1.000000
factck GONE gate.b16.3.held         7
factck GONE gate.b16.3.holdfrac     1.000000
factck GONE B16.2.pattern-instanceof 1.000000
factck GONE B16.4.sealed-hierarchies 1.000000
factck GONE B16.5.arrow-switch      1.000000
factck GONE gate.b17.3.departed     10
factck GONE gate.b17.3.depfrac      1.000000
factck GONE gate.b17.3.arrived      10
factck GONE gate.b17.3.arrfrac      1.000000
factck GONE gate.b17.3.held         2
factck GONE gate.b17.3.holdfrac     1.000000
factck GONE gate.b17.3.switches     4
factck GONE B17.2.type-patterns     1.000000
factck GONE B17.4.sequenced-access  1.000000
factck GONE gate.b18.4.departed     4
factck GONE gate.b18.4.depfrac      1.000000
factck GONE gate.b18.4.arrived      8
factck GONE gate.b18.4.arrfrac      1.000000
factck GONE gate.b18.4.held         10
factck GONE gate.b18.4.holdfrac     1.000000
factck GONE gate.b18.4.handles      8
factck GONE gate.b18.4.setaccessible 10
factck GONE B18.2.doprivileged-removal 1.000000
factck GONE B18.3.unnamed-catch     1.000000

# --- and this rung, standing, with fifteen channels to say so ----------------
factck GONE gate.c1.classes         730
factck GONE gate.c1.at69.0          730
factck GONE gate.c1.preview         0
factck GONE C1.level-69             1.000000
factck GONE gate.c2.files           171
factck GONE gate.c2.setfrac         1.000000
factck GONE gate.c2.clean           151
factck GONE gate.c2.cleanfrac       1.000000
factck GONE gate.c2.spurious        0
factck GONE C2.module-imports       1.000000
factck GONE gate.c3.departed        452
factck GONE gate.c3.depfrac         1.000000
factck GONE gate.c3.held            43
factck GONE gate.c3.holdfrac        1.000000
factck GONE C3.import-subsumption   1.000000
factck GONE gate.c4.departed        1
factck GONE gate.c4.arrived         1
factck GONE gate.c4.held            7
factck GONE gate.c4.holdfrac        1.000000
factck GONE gate.c4.tldecls         7
factck GONE C4.scoped-value         1.000000
factck GONE gate.c5.forks           12
factck GONE gate.c5.forks.flagged   12
factck GONE gate.c5.projects        12
factck GONE C5.compact-headers      1.000000
factck GONE familyC                 1.000000

# --- and the redirect quarter, on its own ------------------------------------
# gate.b14.c5.redirects is a source fact and it is shallow: the scan sees that
# a followRedirects policy is NAMED, not which one. HttpClient's default is
# NEVER, so an agent who names NEVER has written a keyword that changes
# nothing, and before the proviso existed that keyword paid better than
# silence. The quarter carries B-10's rule -- named AND the recording that
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
#
# NEITHER NUMBER IS IN THE B-14 COLUMN, which is worth one line because it has
# not been true of this pair at every rung: 0.825000 and 0.966666 are both
# absent from all ninety rows, so unlike GONE's six these two stand on the
# arithmetic alone and need no distinguishing fact behind them. 0.966666 is
# also the one place in this file where the six-decimal rounding is VISIBLE --
# 17/18 is 0.9444444 and the leg publishes 0.944444, so the composite lands on
# 0.96666640 and prints 0.966666 where an unrounded chain would print 0.966667.
# A rehearsal that expected 0.966667 would be asserting a pipeline that rounds
# once at the end, and this one rounds at every leg.
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
# And the same five-leg reading of family C the twelve runs above carried, on
# the pair that moves the smallest thing in the file. If one httpid can reach
# a family-C leg, the leg is reading a channel it has no business in.
for f in BEHAVRED BEHAVONE; do
   factck "${f}" C1.level-69          1.000000
   factck "${f}" C2.module-imports    1.000000
   factck "${f}" C3.import-subsumption 1.000000
   factck "${f}" C4.scoped-value      1.000000
   factck "${f}" C5.compact-headers   1.000000
   factck "${f}" familyC              1.000000
   factck "${f}" gate.b18.4.exec.broken 0
   factck "${f}" B18.4.varhandle      1.000000
   factck "${f}" B18.security-api-removal 1.000000
   factck "${f}" gate.b17.3.exec.broken 0
   factck "${f}" B17.3.pattern-switch 1.000000
   factck "${f}" B17.java21-constructs 1.000000
   factck "${f}" gate.b16.3.exec.broken 0
   factck "${f}" B16.java17-constructs 1.000000
   factck "${f}" gate.b15.exec.broken 0
   factck "${f}" B15.jpms-services    1.000000
   factck "${f}" B10.java7-held       1.000000
   factck "${f}" B11.java8lang-held   1.000000
   factck "${f}" B12.java8-library    1.000000
   factck "${f}" B13.java9-10         1.000000
done

# -----------------------------------------------------------------------------
echo
if [ "${fail}" = 0 ]; then
   echo "c-rehearsal: OK"
else
   echo "c-rehearsal: FAILED"
   exit 1
fi
