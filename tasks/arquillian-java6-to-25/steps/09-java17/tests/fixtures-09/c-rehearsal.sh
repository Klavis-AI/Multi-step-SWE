#!/usr/bin/env bash
# Rehearse the eight scoring blocks that read the typed AST, the published jars
# and the class-file scan -- the hoisted source-AST block, B-10, B-11, B-12,
# B-13, B-14, B-15 and the whole of family C -- against trees whose answers
# were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the
# eight blocks are lifted out of the SHIPPED test.sh by their own banners,
# composed in file order behind a srcgate() that hands them a fixture's report
# instead of compiling anything, and run once per fixture. What is asserted is
# the arithmetic over the report, over the jars and over the scan, which is the
# part of these gates that no other check exercises.
#
# EIGHT blocks and not seven. Step 08's rehearsal composed the source AST,
# B-10, B-11, B-12, B-13, B-14 and family C; this step adds B-15, which is step
# 08's whole family C inherited under a new name and renormalised over the 0.80
# that is left when its C-1 is dropped. The extraction asserts all eight are
# disjoint and in that order in the file -- the counters are read in one place
# and spent in seven others, and a hoist that silently un-hoisted would leave
# B-10 reading variables that do not exist yet, which under `set -u` is a dead
# verifier rather than a wrong number.
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
# jars` was ONE absence with TWO consequences. This rung's C-1 is back on the
# class-file scan -- major 61 over the class files there are -- so the jars
# feed B-12e and B-15 and nothing else, and there are four fixtures on the
# split: L1 and L2 move the scan and leave the jars alone, K19 and K20 move the
# jars and leave the scan alone.
#
# SO THIS REHEARSAL STAGES A scan.txt AGAIN. run_one copies the fixture's own
# into ${WORK} before the blocks run, the way the verifier copies the one
# arqgate.Scan writes over the built class files, and a fixture with no
# scan.txt is a tree whose build produced no class files at all. That is L2,
# and it is the row that says C-1's absence spelling is the shell literal `0`
# and not `0.000000`.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step09/
# mkfix09.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all. The separation is sharpest on B-15.2, which
# mkfix09.py predicts from the jar PLAN -- twelve Mod records, each with a name
# or without one -- while the rehearsal measures the platform's reading of the
# bytes that plan produced. The two agreeing is a fact about java.lang.module
# and not about either program.
#
# All twelve figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
#
# THREE SPELLINGS OF ZERO, and K17 carries all three at once:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                C-2, C-4 and C-5, and gate.b15.departed, gate.b15.arrived and
#                gate.b15.held, all of which are initialised above their guard
#                and published below it. So does C-1 when there is no scan file
#                at all, which is a different tree from a tree that compiled --
#                L2 and K17 are the two rows, and they are not the same row.
#   `0.000000`   B-12, B-13, B-14, B-15 and C-3 are totals computed by an awk
#                that sits OUTSIDE the guard, so they are computed from parts
#                that never ran.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so a tree nobody could compile has no
#                penalty fact rather than an unfired `1`. So are every gate.c2.*,
#                gate.c4.* and gate.c5.* measurement.
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
echo "== extracting the eight blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min() gate at this step -- four of B-14's
# legs, B-15's service leg, and C-2, C-3 and C-4 -- would read one half alone.
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

# --- block 8: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

BLOCKS="block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-c"
for f in ${BLOCKS}; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The eight are disjoint and in file order. This is the hoist, asserted: B-10
# through B-15 spend counters the first block reads, so the first block has to
# come first, and family C spends them again after all six.
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
s8=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5 e5 s6 e6 s7 e7 s8; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ] && [ "${s5}" -lt "${e5}" ] \
   && [ "${e5}" -lt "${s6}" ] && [ "${s6}" -lt "${e6}" ] && [ "${e6}" -lt "${s7}" ] \
   && [ "${s7}" -lt "${e7}" ] && [ "${e7}" -lt "${s8}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < B-13 ${s5}..${e5} < B-14 ${s6}..${e6} < B-15 ${s7}..${e7} < C ${s8}"
else
   bad "the eight blocks are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5} ${e5} ${s6} ${e6} ${s7} ${e7} ${s8}"
fi

# --- what came back, and where it is allowed to be ---------------------------
# Step 08's rehearsal asserted that NO block read WANT_MAJOR or ${WORK}/scan.txt,
# because at that rung the class-file level did not move and the scan report was
# not staged. Both are back, and the assertion is inverted rather than deleted:
# exactly ONE block may read each, and it is family C, because C-1 is the leg
# that spends them. A WANT_MAJOR that reappeared in B-10 would be an inherited
# gate silently re-reading this rung's must-flip -- the ninth trap, in the
# direction that raises a score rather than lowering one.
#
# ANCHORED ON THE SLASH for scan.txt. Family C writes ${WORK}/cnt-*.txt and
# ${WORK}/sealed-*.txt, and an unanchored `scan\.txt` matches the tail of a
# bucket name -- which reported this rehearsal's own bucket file as a leftover
# the first time the step-08 version of this loop ran. The real reference was
# always ${WORK}/scan.txt, so the separator is part of the pattern.
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-b15; do
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
n=$(sed 's/#.*//' "${TMP}/block-c.sh" | grep -c 'modulejars\.txt' || true)
[ "${n}" = 0 ] \
   || bad "family C reads modulejars.txt ${n} time(s); the jars are B-12e's and B-15's channel"
note "family C does not read modulejars.txt: the jar channel and the scan channel are separate again"

# Every gate this rehearsal claims to exercise has to be INSIDE one of the seven
# extracts. A fact that moved out of range would still be published by the real
# verifier and would silently stop being rehearsed here.
inblock() {
   local blk=$1 pat=$2
   grep -q -- "${pat}" "${TMP}/${blk}.sh" || bad "${pat} is not inside ${blk}"
}
inblock block-src 'gate.src.ok17='
inblock block-src 'gate.src.ok16='
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
# Family C's five legs, each named by the fact it publishes and by the
# baseline file it divides by. Five baselines and not one: a leg that read
# another leg's population would score, and would score wrongly, and the
# totals alone could not say which.
inblock block-c 'C1.level-'
inblock block-c 'C2.pattern-instanceof='
inblock block-c 'C3.text-blocks='
inblock block-c 'C4.sealed-hierarchies='
inblock block-c 'C5.arrow-switch='
inblock block-c 'gate.c1.classes='
inblock block-c 'gate.c1=no-scan'
inblock block-c 'gate.c2.converted.want='
inblock block-c 'gate.c2.held.want='
inblock block-c 'gate.c2.sites.want='
inblock block-c 'gate.c2.convfrac='
inblock block-c 'gate.c2.holdfrac='
inblock block-c 'gate.c2.totfrac='
inblock block-c 'gate.c3.moved.want='
inblock block-c 'gate.c3.held.want='
inblock block-c 'gate.c3.textblocks='
inblock block-c 'gate.c3.movefrac='
inblock block-c 'gate.c3.holdfrac='
inblock block-c 'gate.c3.exec.want='
inblock block-c 'gate.c4.roots.want='
inblock block-c 'gate.c4.open.want='
inblock block-c 'gate.c4.closedfrac='
inblock block-c 'gate.c4.openfrac='
inblock block-c 'gate.c5.files.want='
inblock block-c 'gate.c5.arrow='
inblock block-c 'gate.c5.colon='
inblock block-c 'scan.txt'
inblock block-c 'baseline/ipattern.txt'
inblock block-c 'baseline/strnl.txt'
inblock block-c 'baseline/tbids.txt'
inblock block-c 'baseline/sealed.txt'
inblock block-c 'baseline/swarrow.txt'

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
# leave class files behind that the scorer's own -source 17 probe will not
# accept. So C-1's guard asks about the SCAN and never about `measured`, and
# K17 -- the tree nobody could compile -- scores C-1 at 1.000000 while C-2,
# C-4 and C-5 publish the shell literal `0`. That is not a leak: the class
# files were measured, and the level they were compiled at is a fact about
# them regardless of what the scorer could reproduce.
#
# Asserted structurally here and scored on K17 below, because this is the kind
# of property a later edit moves by accident while making a guard tidier, and
# because the cost of getting it wrong is a silent 0.10 handed to a tree that
# never built -- or, in the other direction, a 0 handed to a tree that did.
#
# `guardend` takes the guard's opening line as a pattern rather than assuming
# one spelling: B-10 through B-14 open with `if [ -z "${measured}" ]` and take
# the else branch when there IS a report, while B-15 and family C's four
# source legs open with `if [ -n ... ] && [ -s ... ]` and take the then
# branch. Two shapes, one meaning, and a helper that only knew the first would
# report every one of those guards as missing and pass.
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

# --- family C: five legs, five guards, and C-1's is not about compiling ------
# Each of C-2 through C-5 opens its own guard naming its own baseline file, so
# a missing population file costs ONE leg rather than four. They are asserted
# individually and in order: a leg whose guard named the wrong baseline would
# still score -- on a population that is not its own -- and the table's totals
# alone could not say which leg had been handed which list.
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

cleg() {
   local leg=$1 base=$2 factpat=$3 gs ge ln
   gs=$(guardline block-c "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/${base} ]; then")
   ge=$(guardend block-c "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/${base} ]; then")
   ln=$(guardline block-c "${factpat}")
   if [ -n "${gs}" ] && [ -n "${ge}" ] && [ -n "${ln}" ] && [ "${ln}" -gt "${ge}" ]; then
      note "$(printf '%-4s guard %s..%s on %-14s total below it at %s' \
              "${leg}" "${gs}" "${ge}" "${base}" "${ln}")"
   else
      bad "${leg}: guard ${gs:-?}..${ge:-?} on ${base}, total ${ln:-?}"
   fi
}
cleg C-2 ipattern.txt 'fact "C2.pattern-instanceof='
cleg C-3 strnl.txt    'fact "C3.text-blocks='
cleg C-4 sealed.txt   'fact "C4.sealed-hierarchies='
cleg C-5 swarrow.txt  'fact "C5.arrow-switch='

# C-3's final assignment is the one that sits OUTSIDE its guard, because it
# multiplies by a run-time scaling read from behavior.summary rather than from
# the report. That is why C-3 prints `0.000000` on K17 while C-2, C-4 and C-5
# print the shell literal `0`, and it is the difference the table asserts as
# strings rather than as numbers.
c3gend=$(guardend block-c "if [ -n \"\${measured}\" ] && [ -s \"\${measured}\" ] && [ -r /tests/baseline/strnl.txt ]; then")
c3asg=$(guardline block-c 'c3=$(awk -v m=')
if [ -n "${c3gend}" ] && [ -n "${c3asg}" ] && [ "${c3asg}" -gt "${c3gend}" ]; then
   note "C-3's total is assigned below its guard (${c3gend} < ${c3asg}): its zero is computed, not initialised"
else
   bad "C-3's total is inside its guard: guard ends ${c3gend:-?}, assignment ${c3asg:-?}"
fi

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
for blk in block-b10 block-b11 block-b12 block-b13 block-b14 block-b15 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the eight blocks may run a compiler, a build or a
# container. They are arithmetic over a report, one pass of unzip over jars
# that already exist, one read of a scan file that already exists and ONE run
# of the module gate -- that is what makes them rehearsable at all -- and the
# one thing that touches the tree is srcgate(), which is replaced below.
#
# `java` is split out from the other three and is not forbidden outright,
# because it is load-bearing in exactly one place. It is forbidden in seven
# blocks and allowed EXACTLY ONCE in B-15, on a line that also names
# arqgate.Modules: a second java invocation there would be a second reading of
# the same jars, and a java in any other block would be a gate measuring
# something this rehearsal has not staged. FAMILY C IS NOW ON THE FORBIDDEN
# SIDE, which it was not at step 08 -- C-1 reads a file arqgate.Scan wrote
# during the build stage, and a family C that ran java would be re-deriving at
# score time a reading the build already took. Comments are stripped first:
# the blocks discuss javac at length.
cat "${TMP}"/block-*.sh | sed 's/#.*//' \
   | grep -nE '(^|[;&|(]|\$\() *(javac|docker|\./gradlew)([ "]|$)' \
   > "${TMP}/runs.txt" || true
if [ -s "${TMP}/runs.txt" ]; then
   bad "a block runs a compiler, a build or a container:"
   sed 's/^/         /' "${TMP}/runs.txt"
else
   note "no block runs javac, docker or gradlew"
fi
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-c; do
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
# THREE tags at this step: `17`, `16` and `def`. The block asks for 17 first;
# if that succeeded it asks for 16, because a tree that still compiles at
# -source 16 has no Java 17 language feature load-bearing in it, which is a
# finding and not a failure; and it asks for `def` only when 17 failed. So a
# fixture's answers.txt carries all three and the shim answers whichever the
# block asks for.
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
# cell MISSING, which reads like eight broken legs and is one missing binding.
#
# Read from the SAME baseline file test.sh reads, and not typed as 61 and 55:
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
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-b11.sh" \
          "${TMP}/block-b12.sh" "${TMP}/block-b13.sh" "${TMP}/block-b14.sh" \
          "${TMP}/block-b15.sh" "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- forty-two trees, and forty-two of them different -----------------------
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
# fixture  B10 B11 B12 B13 B14 B15 | C1 C2 C3 C4 C5 | familyC
# Transcribed by hand from mkfix09.py's printed table. The two ends are the
# rows that carry the step's floor argument: `pinned` is the tree this rung is
# handed and must read every family-C leg at 0 with all six held gates in
# full -- migration_prompt item 6's nop run -- and `golden` must read all
# twelve in full, which is item 6's oracle.
#
# `pinned`'s B15 IS 1.000000 AND THAT IS THE FLOOR ARGUMENT, not a mistake.
# This rung's input tree is step 08's output, so the twelve artifacts already
# declare their module names and the service lookups have already moved. An
# inherited gate that starts full gives the floor no help, which is why step
# 09's floor is exactly 0.475000 and why NOTES 15.3 argues it rather than
# assuming it.
#
# TWELVE columns and not six, because at this rung neither family's legs are
# interchangeable and the totals hide which one moved. K2 and K20 both read
# B15 0.562500 and they are different trees: K2 published twelve jars and named
# none of them, K20 published nothing at all -- only gate.b15.mod.total tells
# those apart. L12 and L13 both read C-5 0.500000 and they are different
# trees: one reverted a file's switch, the other left it colon and added an
# arrow elsewhere -- only gate.c5.arrow tells those apart. L6 and L14 both read
# familyC 0.740000 and they are different trees: L6 deleted the text block,
# L14 left one in the file but OUTSIDE the method that had to write it --
# only gate.c3.textblocks tells those apart.
TABLE=$(cat <<'EOF'
pinned   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000
golden   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K1       1.000000 1.000000 1.000000 1.000000 1.000000 0.930122 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K2       1.000000 1.000000 1.000000 1.000000 1.000000 0.562500 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K3       1.000000 1.000000 1.000000 1.000000 1.000000 0.866319 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K4       1.000000 1.000000 1.000000 1.000000 0.955555 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K5       1.000000 1.000000 1.000000 1.000000 0.987879 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K6       1.000000 1.000000 1.000000 1.000000 0.927273 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K7       1.000000 1.000000 1.000000 1.000000 0.995607 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K8       1.000000 1.000000 1.000000 1.000000 0.932540 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K9       1.000000 1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K10      1.000000 1.000000 1.000000 1.000000 0.916667 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K11      1.000000 1.000000 1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K12      1.000000 1.000000 1.000000 1.000000 0.700000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K13      1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K14      1.000000 1.000000 1.000000 1.000000 0.800000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K15      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K16      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K17      0        0        0.000000 0.000000 0.000000 0.437500 1.000000 0        0.000000 0        0        0.100000
K18      1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K19      1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K20      1.000000 1.000000 0.850000 1.000000 1.000000 0.562500 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M2       1.000000 1.000000 1.000000 1.000000 1.000000 0.718750 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M3       1.000000 1.000000 1.000000 1.000000 1.000000 0.948864 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M4       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M5       1.000000 1.000000 1.000000 1.000000 1.000000 0.437500 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
M6       1.000000 1.000000 1.000000 1.000000 1.000000 0.926630 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
L1       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.938338 1.000000 1.000000 1.000000 1.000000 0.993834
L2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0        1.000000 1.000000 1.000000 1.000000 0.900000
L3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.428571 1.000000 1.000000 1.000000 0.885714
L4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 1.000000 1.000000 1.000000 0.800000
L5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.571429 1.000000 1.000000 1.000000 0.914286
L6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 1.000000 1.000000 0.740000
L7       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.571429 1.000000 1.000000 0.888572
L8       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000 0.896000
L9       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.545455 1.000000 0.836364
L10      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 1.000000 0.640000
L11      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.909091 1.000000 0.967273
L12      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.500000 0.960000
L13      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.500000 0.960000
L14      1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 1.000000 1.000000 0.740000
EOF
)

echo
echo "== running the composed blocks against each fixture"
printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
   fixture B10 B11 B12 B13 B14 B15 C1 C2 C3 C4 C5 familyC
while read -r fix w10 w11 w12 w13 w14 w15 wc1 wc2 wc3 wc4 wc5 wfc; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   g10=$(fv "${fix}" 'B10\.java7-held')
   g11=$(fv "${fix}" 'B11\.java8lang-held')
   g12=$(fv "${fix}" 'B12\.java8-library')
   g13=$(fv "${fix}" 'B13\.java9-10')
   g14=$(fv "${fix}" 'B14\.java11-jakarta')
   g15=$(fv "${fix}" 'B15\.jpms-services')
   # C-1's fact carries the major in its NAME -- `C1.level-61` -- because the
   # number a rung flips to is the one thing about that rung a reader should
   # not have to look up. Spelled out here rather than globbed, so a rung that
   # moved the level without moving this file fails on the key rather than
   # silently reading MISSING into every row.
   gc1=$(fv "${fix}" 'C1\.level-61')
   gc2=$(fv "${fix}" 'C2\.pattern-instanceof')
   gc3=$(fv "${fix}" 'C3\.text-blocks')
   gc4=$(fv "${fix}" 'C4\.sealed-hierarchies')
   gc5=$(fv "${fix}" 'C5\.arrow-switch')
   gfc=$(fv "${fix}" familyC)
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${g10:-MISSING}" "${g11:-MISSING}" "${g12:-MISSING}" \
      "${g13:-MISSING}" "${g14:-MISSING}" "${g15:-MISSING}" \
      "${gc1:-MISSING}" "${gc2:-MISSING}" "${gc3:-MISSING}" \
      "${gc4:-MISSING}" "${gc5:-MISSING}" "${gfc:-MISSING}"
   [ "${g10}" = "${w10}" ] || bad "${fix}: B10 want ${w10} got ${g10:-MISSING}"
   [ "${g11}" = "${w11}" ] || bad "${fix}: B11 want ${w11} got ${g11:-MISSING}"
   [ "${g12}" = "${w12}" ] || bad "${fix}: B12 want ${w12} got ${g12:-MISSING}"
   [ "${g13}" = "${w13}" ] || bad "${fix}: B13 want ${w13} got ${g13:-MISSING}"
   [ "${g14}" = "${w14}" ] || bad "${fix}: B14 want ${w14} got ${g14:-MISSING}"
   [ "${g15}" = "${w15}" ] || bad "${fix}: B15 want ${w15} got ${g15:-MISSING}"
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
# This rung's input tree is the step-08 golden. It compiles at 17 AND at 16,
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
# B-15 READS 1.000000 ON THE NOP AND THAT IS THE FLOOR ARGUMENT, NOT A DEFECT.
# B-15 is step 08's family C carried forward, and the tree this rung is handed
# IS step 08's finished answer: twelve artifacts already declare their module
# names, the platform lookup has already moved, the sixty-nine recorded
# behaviours are intact. An inherited gate that charged the step-09 candidate
# for the step-08 candidate's work would be the worse verifier. The price of
# getting it right is that family B gives the floor no help whatever -- the
# floor is 0.30*A + 0.70*0.25*1.0 = 0.475000 and family C carries the entire
# separation on its own. NOTES 15.3 argues that rather than assuming it.
#
# THE FOUR FAMILY-C CELLS THAT READ FULL HERE ARE ALL HOLDS, and every one of
# them is a min() term or a multiplier rather than a leg with weight. Seven
# instanceof sites that must not become patterns, seven newline literals that
# must not become a text block, eleven extension points that must not be
# sealed, five document behaviours that must not change: the unmigrated tree
# holds all of them for free. Every leg that does carry weight -- at61,
# converted, moved, roots, arrow -- reads zero on the same tree, which is the
# arithmetic that keeps standing still worth nothing.
factck pinned gate.src.ok17 yes
factck pinned gate.src.ok16 yes
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
# ...and family C leg by leg, on the tree that did none of it. The five weights
# these five legs carry -- 0.10, 0.20, 0.26, 0.36, 0.08 -- are what a candidate
# who does the work is paid, and the row above the table says the sum of them
# here is 0.000000.
factck pinned gate.c1.classes 746
factck pinned gate.c1.at61 0
factck pinned gate.c2.sites 28
factck pinned gate.c2.sites.want 28
factck pinned gate.c2.totfrac 1.000000
factck pinned gate.c2.converted 0
factck pinned gate.c2.converted.want 21
factck pinned gate.c2.convfrac 0.000000
factck pinned gate.c2.held 7
factck pinned gate.c2.held.want 7
factck pinned gate.c2.holdfrac 1.000000
factck pinned gate.c3.moved 0
factck pinned gate.c3.moved.want 1
factck pinned gate.c3.movefrac 0.000000
factck pinned gate.c3.textblocks 0
factck pinned gate.c3.held 7
factck pinned gate.c3.held.want 7
factck pinned gate.c3.holdfrac 1.000000
factck pinned gate.c3.exec.want 5
factck pinned gate.c3.exec.broken 0
factck pinned gate.c3.exec 1.000000
factck pinned gate.c4.decl 0
factck pinned gate.c4.nonsealed 0
factck pinned gate.c4.permits 0
factck pinned gate.c4.roots 0
factck pinned gate.c4.roots.want 11
factck pinned gate.c4.closedfrac 0.000000
factck pinned gate.c4.open 11
factck pinned gate.c4.open.want 11
factck pinned gate.c4.openfrac 1.000000
factck pinned gate.c5.size 2
factck pinned gate.c5.arrow 0
factck pinned gate.c5.colon 2
factck pinned gate.c5.expr 0
factck pinned gate.c5.files 0
factck pinned gate.c5.files.want 2
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
# the five family-C legs all move. 746 class files at major 61 instead of 0,
# twenty-one instanceof sites converted instead of 0, one text block instead of
# 0, eleven sealed roots with thirty-five permitted subtypes instead of 0, two
# arrow switches instead of 0. The four holds do not move at all, which is the
# other half of the claim: the correct answer converts what was asked for and
# nothing else.
factck golden gate.src.ok17 yes
factck golden gate.src.ok16 no
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
factck golden gate.c1.classes 746
factck golden gate.c1.at61 746
factck golden gate.c2.converted 21
factck golden gate.c2.convfrac 1.000000
factck golden gate.c2.held 7
factck golden gate.c2.holdfrac 1.000000
factck golden gate.c2.sites 28
factck golden gate.c2.totfrac 1.000000
factck golden gate.c3.moved 1
factck golden gate.c3.movefrac 1.000000
factck golden gate.c3.textblocks 1
factck golden gate.c3.held 7
factck golden gate.c3.holdfrac 1.000000
factck golden gate.c3.exec.want 5
factck golden gate.c3.exec.broken 0
factck golden gate.c3.exec 1.000000
factck golden gate.c4.decl 11
factck golden gate.c4.nonsealed 0
factck golden gate.c4.permits 35
factck golden gate.c4.roots 11
factck golden gate.c4.roots.want 11
factck golden gate.c4.closedfrac 1.000000
factck golden gate.c4.open 11
factck golden gate.c4.openfrac 1.000000
factck golden gate.c5.size 2
factck golden gate.c5.arrow 2
factck golden gate.c5.colon 0
factck golden gate.c5.expr 0
factck golden gate.c5.files 2
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
# L1 and L2 are the two rows that exist because this rung put the class-file
# scan back on a channel of its own. C-1 reads ${WORK}/scan.txt, written by
# arqgate.Scan over what the build left in build/classes; every other leg of
# family C reads the source AST that srcgate compiled. A tree can move either
# without the other, and at step 08 no fixture could say so because both of
# that rung's channels came off one list of jars.
#
# L1 is the partial migration that actually happens: the modules whose build
# file moved are at 61 and the forty-six classes whose did not are still at 55.
# The leg is linear in the fraction, so 700/746 is 0.938338 and familyC is
# 0.993834 -- C-1 is 0.10 of the family, so the forty-six class files still at
# 55 cost 0.10 x 46/746 = 0.006166 and nothing else.
factck L1 gate.c1.classes 746
factck L1 gate.c1.at61 700
factck L1 C1.level-61 0.938338
# ...and L2 is the absence. No scan.txt at all, which is what a build that
# produced no class files leaves behind, and the leg publishes the sentinel and
# the shell literal `0` rather than a computed 0.000000 over an empty file.
# THE OTHER FOUR LEGS ARE UNTOUCHED, which is the whole point of the row: the
# source AST arrived, so C-2 through C-5 all read full and familyC is exactly
# 1 - 0.10 = 0.900000. A family C that read its major out of the report, or
# that let one missing file zero the whole family, fails here and nowhere else.
factck L2 gate.c1 'no-scan'
factck L2 gate.c1.classes MISSING
factck L2 gate.c1.at61 MISSING
factck L2 C1.level-61 0
factck L2 gate.c4.decl 11
factck L2 gate.c5.arrow 2

# --- C-2: the pattern, the hold and the sites --------------------------------
# Three terms under a min(), and L3, L4 and L5 each take one of them down while
# leaving the other two at 1.000000. That is the only way to show a min() is
# over three things: a fixture that dropped two of them would score the same as
# one that dropped the lower.
#
# L3 is the migration that stopped halfway -- nine of twenty-one convertible
# sites carry a pattern variable and twelve are still a plain instanceof with a
# cast after them. L4 is the opposite error and the more interesting one: every
# convertible site converted AND the seven sites that have no cast after them
# bound as well, which does not compile in the real tree and which a gate that
# only counted `kind=bound` would read as a perfect score. L5 deleted three of
# those seven rather than leaving them alone, and it is the only row where the
# total-site term moves: 25 sites where the baseline recorded 28.
factck L3 gate.c2.converted 9
factck L3 gate.c2.converted.want 21
factck L3 gate.c2.convfrac 0.428571
factck L3 gate.c2.held 7
factck L3 gate.c2.holdfrac 1.000000
factck L3 gate.c2.sites 28
factck L3 gate.c2.totfrac 1.000000
factck L4 gate.c2.converted 21
factck L4 gate.c2.convfrac 1.000000
factck L4 gate.c2.held 0
factck L4 gate.c2.held.want 7
factck L4 gate.c2.holdfrac 0.000000
factck L4 gate.c2.sites 28
factck L4 gate.c2.totfrac 1.000000
factck L5 gate.c2.converted 21
factck L5 gate.c2.convfrac 1.000000
factck L5 gate.c2.held 4
factck L5 gate.c2.holdfrac 0.571429
factck L5 gate.c2.sites 25
factck L5 gate.c2.sites.want 28
factck L5 gate.c2.totfrac 0.892857

# --- C-3: one document moved, seven literals held, five behaviours kept ------
# L6, L7, L8 and L14 are the four ways to lose the text-block leg. The third of
# them loses it without touching a line of the source shape, and the fourth
# loses it while satisfying every FILE-level reading of the gate.
#
# L6 removed the text block and did NOT put the literals back, which is the
# state a revert leaves when it reverts half of itself: gate.c3.textblocks is 0
# and the move fraction with it. L7 kept the text block and flattened three of
# the seven newline literals in the XML fragment builder -- the hold half, and
# the leg that says one HTML document becomes a text block and the builders do
# not. L8 has the golden's source shape exactly and two of the five recorded
# document behaviours differ, which is the multiplier: a text block whose
# content is not byte-identical to the string it replaced is a regression
# wearing the right syntax.
factck L6 gate.c3.moved 0
factck L6 gate.c3.moved.want 1
factck L6 gate.c3.movefrac 0.000000
factck L6 gate.c3.textblocks 0
factck L6 gate.c3.held 7
factck L6 gate.c3.holdfrac 1.000000
factck L6 gate.c3.exec 1.000000
# L14 IS L6'S TWIN ON ALL TWELVE COLUMNS and a different tree: the sixteen
# literals are gone from ServletTestRunner#doGet exactly as in L6, and a text
# block IS still in that file -- an unused constant at class scope, which
# SourceScan records as `in=-`. gate.c3.textblocks reads 1 here and 0 on L6,
# and that one fact is the whole separation.
#
# It is also the fixture that fails the OLD scorer. Keyed by file, the arrival
# half read this tree as satisfied -- move 1/1, C-3 1.000000, familyC 1.000000
# -- because a text block was somewhere in the file. Keyed by bucket it reads
# 0/1, because `...ServletTestRunner.java#-` is not `...#doGet`. A row that
# scores 0.740000 against the shipped scorer and 1.000000 against the one this
# rung replaced is the proof the replacement did something.
factck L14 gate.c3.moved 0
factck L14 gate.c3.moved.want 1
factck L14 gate.c3.movefrac 0.000000
factck L14 gate.c3.textblocks 1
factck L14 gate.c3.held 7
factck L14 gate.c3.holdfrac 1.000000
factck L14 gate.c3.exec 1.000000
factck L7 gate.c3.moved 1
factck L7 gate.c3.movefrac 1.000000
factck L7 gate.c3.textblocks 1
factck L7 gate.c3.held 4
factck L7 gate.c3.held.want 7
factck L7 gate.c3.holdfrac 0.571429
factck L7 gate.c3.exec 1.000000
factck L8 gate.c3.moved 1
factck L8 gate.c3.movefrac 1.000000
factck L8 gate.c3.textblocks 1
factck L8 gate.c3.held 7
factck L8 gate.c3.holdfrac 1.000000
factck L8 gate.c3.exec.want 5
factck L8 gate.c3.exec.broken 2
factck L8 gate.c3.exec 0.600000

# --- C-4: eleven hierarchies closed and everything else left open ------------
# The leg with two halves that pull in opposite directions, and L9, L10 and L11
# are one fixture for each half plus the one that shows the closed half
# compares TRIPLES and not names.
#
# L9 sealed six of the eleven event roots. L10 sealed all eleven AND the eleven
# extension points the tree is extended through -- the reading of "make the
# hierarchies closed" that a tool applying the modifier everywhere it compiles
# would produce, and the reason the open half exists: openfrac 0.000000 takes
# the leg to zero even though closedfrac is full. L11 sealed all eleven and
# left one permitted subtype out of one clause. gate.c4.decl is still 11 on
# L11 and roots is 10, which is the pair of facts that separates it from L9 --
# a gate that asked only "is this type sealed" would read L11 at full.
factck L9 gate.c4.decl 6
factck L9 gate.c4.roots 6
factck L9 gate.c4.roots.want 11
factck L9 gate.c4.permits 13
factck L9 gate.c4.closedfrac 0.545455
factck L9 gate.c4.open 11
factck L9 gate.c4.openfrac 1.000000
factck L10 gate.c4.decl 22
factck L10 gate.c4.roots 11
factck L10 gate.c4.permits 57
factck L10 gate.c4.closedfrac 1.000000
factck L10 gate.c4.open 0
factck L10 gate.c4.open.want 11
factck L10 gate.c4.openfrac 0.000000
factck L11 gate.c4.decl 11
factck L11 gate.c4.roots 10
factck L11 gate.c4.permits 34
factck L11 gate.c4.closedfrac 0.909091
factck L11 gate.c4.open 11
factck L11 gate.c4.openfrac 1.000000

# --- C-5: two switches, and the pair that scores the same for two reasons ----
# L12 and L13 are the collision the twelve-column table was widened for. Both
# read C-5 0.500000 and both read familyC 0.960000, and they are not the same
# tree: L12 reverted DeployableTestBuilder's switch to colon form, so there is
# one arrow left in the tree and one colon; L13 left DynamicServiceLoaderTest's
# colon switch exactly where it was and added a NEW arrow switch beside it, so
# there are two arrows and one colon and three switches in all.
#
# gate.c5.arrow and gate.c5.size are what tell them apart -- 1 and 2 against 2
# and 3 -- and the leg reads both because it asks two questions per file:
# enough arrows AND no colon left. A per-file arrow count on its own would read
# L13 as a converted file, which is how a tree gets credit for a switch it
# added rather than one it migrated.
factck L12 gate.c5.size 2
factck L12 gate.c5.arrow 1
factck L12 gate.c5.colon 1
factck L12 gate.c5.files 1
factck L12 gate.c5.files.want 2
factck L13 gate.c5.size 3
factck L13 gate.c5.arrow 2
factck L13 gate.c5.colon 1
factck L13 gate.c5.files 1
factck L13 gate.c5.files.want 2

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
# class files are there. gate.b15.departed, .arrived and .held are the shell
# literal `0`, assigned above the guard and published below it. C-3 and the
# four inherited families are `0.000000`, computed by awks that sit outside
# their guards from parts that never ran. C-2, C-4 and C-5 print the literal
# `0` and every gate.c2.*, gate.c4.* and gate.c5.* fact is MISSING, published
# only inside the per-leg baseline guards -- as is the whole of B-14.
#
# familyC IS 0.100000 AND THAT IS EXACTLY C-1's WEIGHT. A family C whose five
# legs shared one guard would read 0 here; one that treated a missing AST as a
# missing family would read MISSING. The number is the assertion that the five
# legs are five legs on two channels.
factck K17 gate.src.ok17 no
factck K17 gate.src.okdef no
factck K17 gate.b15.mod.total 12
factck K17 gate.b15.mod.declared 12
factck K17 gate.b15.mod.derivable 11
factck K17 gate.b15.names 12
factck K17 gate.b15.departed 0
factck K17 gate.b15.arrived 0
factck K17 gate.b15.held 0
factck K17 gate.c1.classes 746
factck K17 gate.c1.at61 746
factck K17 C1.level-61 1.000000
# The shell literal `0` and not `0.000000`, which is the same distinction K17
# is carrying three other times on this row. c3move and c3hold are initialised
# above C-3's guard and published below it, so an uncompilable tree publishes
# the initialiser; C-3's total on the line above is `0.000000` because it is
# assigned below the guard and computed from the execution scaling. One leg,
# two spellings, and which one a fact gets is decided by which side of the
# guard its assignment sits on -- exactly as for gate.b15.departed/arrived/held.
factck K17 gate.c3.movefrac 0
factck K17 gate.c3.holdfrac 0
# ...and the scaling is not the thing that fell: K17's behavior.summary is the
# golden's and reads clean on both lists. B-15.3's proviso and C-3's are each
# 1.000000 on a tree whose source shape neither could measure, which is the
# guard doing its job rather than the proviso doing it.
factck K17 gate.b15.exec.want 69
factck K17 gate.b15.exec.broken 0
factck K17 gate.b15.exec 1.000000
factck K17 gate.c3.exec.want 5
factck K17 gate.c3.exec.broken 0
factck K17 gate.c3.exec 1.000000
# The MISSING list, which is the spelling a factck on a number cannot express.
# Three of family C's five legs and the whole of B-14.
for k in gate.c2.sites gate.c2.sites.want gate.c2.converted gate.c2.held \
         gate.c3.moved gate.c3.held gate.c3.textblocks \
         gate.c4.decl gate.c4.roots gate.c4.open gate.c4.permits \
         gate.c5.size gate.c5.arrow gate.c5.colon gate.c5.files \
         gate.b14.c2.want gate.b14.c3.want gate.b14.c4.want \
         gate.b14.c5.exec.want gate.b14.c5.redirects gate.src.penalty \
         gate.src.impjx B14.2.jakarta B14.5.httpclient; do
   factck K17 "${k}" MISSING
done

# --- K18 and K19: the two held gates that are not B-14 -----------------------
# Both are here because they move a column no other fixture moves, which is how
# this rehearsal knows the twelve columns are twelve channels. K18 puts two
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
# full -- move ONE thing, and require the composed blocks to notice. Ten
# perturbations on six channels, and every expected figure below is derived in
# the comment above it rather than transcribed, because these trees do not
# exist in mkfix09.py's table.
#
# The six channels are the point of the count. The source report, the
# class-file scan, the published jars, and the three id lists the one
# behavior.summary is read against -- httpids, svcids, tbids. A rung with two
# artifact channels and three behaviour lists can get any one of them crossed
# with any other, and the only way to see a crossing is to move one and assert
# that the other five did not.
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
# ...and the rest of the tree, unmoved. The five family-C legs are the
# load-bearing ones: a source-report edit that moved a family-C leg would mean
# this rung's five gates are reading the rung below's records.
factck PERT B14.2.jakarta        1.000000
factck PERT B14.3.javax-held     1.000000
factck PERT B14.5.httpclient     1.000000
factck PERT gate.src.penalty     1
factck PERT B15.2.module-names   1.000000
factck PERT B15.3.platform-lookup 1.000000
factck PERT B15.jpms-services    1.000000
factck PERT C1.level-61          1.000000
factck PERT C2.pattern-instanceof 1.000000
factck PERT C3.text-blocks       1.000000
factck PERT C4.sealed-hierarchies 1.000000
factck PERT C5.arrow-switch      1.000000
factck PERT familyC              1.000000
factck PERT B10.java7-held       1.000000
factck PERT B11.java8lang-held   1.000000
factck PERT B12.java8-library    1.000000
factck PERT B13.java9-10         1.000000

# --- can it fail, on the channel this rung brought back ----------------------
# C-1 reads ${WORK}/scan.txt and nothing else in the verifier does. L1 is the
# fixture for the partial level move and mkfix09.py wrote its scan.txt; this
# run writes the SAME shape by hand, out of this script, and requires the same
# two numbers. If write_scan() and the scorer ever agreed on a spelling neither
# arqgate.Scan nor a reader would recognise, L1 would pass and this would not.
#
#   classes=746, major.55=46, major.61=700
#   C-1 = 700/746 = 0.938338, and the other four legs untouched
#   familyC = 0.10*0.938338 + 0.90 = 0.993834
echo
echo "== can it fail: the golden's class-file scan, written by hand"
scandir="${TMP}/scanp"
rm -rf "${scandir}"; mkdir -p "${scandir}"
cp -R "${FIXDIR}/golden" "${scandir}/SCANPERT"
cat > "${scandir}/SCANPERT/scan.txt" <<'SCAN'
classes=746
major.55=46
major.61=700
majors.distinct=2
SCAN
nrep=$(diff "${FIXDIR}/golden/report.txt" "${scandir}/SCANPERT/report.txt" | grep -c '^[<>]' || true)
njar=$(ls "${scandir}/SCANPERT"/jars/*.jar 2>/dev/null | grep -c . || true)
nsum=$(grep -c '^major\.61=700$' "${scandir}/SCANPERT/scan.txt" || true)
if [ "${nrep}" = 0 ] && [ "${njar}" = 12 ] && [ "${nsum}" = 1 ]; then
   note "the scan was rewritten and nothing else: report.txt untouched, twelve jars still published"
else
   bad "the scan perturbation did not land as described:" \
       "report.txt changed lines ${nrep} (want 0), jars ${njar} (want 12)," \
       "major.61=700 lines ${nsum} (want 1)"
fi
_savedfix=${FIXDIR}
FIXDIR=${scandir}
run_one SCANPERT
FIXDIR=${_savedfix}
factck SCANPERT gate.c1.classes       746
factck SCANPERT gate.c1.at61          700
factck SCANPERT C1.level-61           0.938338
factck SCANPERT familyC               0.993834
# ...and the four legs that read the AST, and the two inherited gates that read
# the jars. A C-1 that fell back to the report's own level record, or a B-15.2
# that read the scan, shows up here and in no fixture.
factck SCANPERT C2.pattern-instanceof 1.000000
factck SCANPERT C3.text-blocks        1.000000
factck SCANPERT C4.sealed-hierarchies 1.000000
factck SCANPERT C5.arrow-switch       1.000000
factck SCANPERT B15.2.module-names    1.000000
factck SCANPERT B15.jpms-services     1.000000
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
#   familyC = 1.000000, on all five legs
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
factck MODPERT gate.c1.classes         746
factck MODPERT gate.c1.at61            746
factck MODPERT C1.level-61             1.000000
factck MODPERT familyC                 1.000000
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

# --- can it fail, four more times: the recorded behaviour, broken ------------
# THREE of the twelve columns are scaled by what family A still reproduces, and
# they are scaled by THREE DIFFERENT LISTS read out of ONE file: B-14.5 by
# baseline/httpids.txt's eighteen exchange ids, B-15.3 by
# baseline/svcids.txt's sixty-nine service lookups, C-3 by
# baseline/tbids.txt's five document recordings. B-10's nio quarter reads a
# fourth. No fixture in the table exercises the http list, because every
# fixture's summary carries only the ids its own catalogue row asked for.
#
# Those scalings exist because the source legs above them are all SHAPE --
# records gone, types declared, a call site present -- and a file that deletes
# the method body has every one of them. Family A notices, but eighteen of 935
# transcripts is 0.005775 of the step against B-14's weight, five is 0.001604
# against C-3's 0.26, and both are trades in favour of faking it. The scalings
# are what close them. So what this rehearsal has to prove is not that they
# exist but that each is a FRACTION of its OWN list read at run time -- not a
# boolean, not a halving, and not one of the other three.
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
# on any breakage would read 0.500000 in BOTH runs. B-15, family C and B-10
# have to stay at 1.000000 in both, and those three are the assertion that the
# four provisos read their own lists and not each other's.
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
# The four lists are disjoint, which is the precondition every assertion in
# this section rests on. An id that appeared on two of them would make one
# perturbation move two gates and the "unmoved" rows below would be wrong
# rather than reassuring. Four lists is six pairs, and the sixth pair --
# svcids against tbids -- is the one this rung added: sixty-nine service
# lookups and five document recordings, both of them behaviours of the same
# web runner, and nothing but this loop says they are different behaviours.
for pair in "httpids svcids" "httpids nioids" "httpids tbids" \
            "svcids nioids" "svcids tbids" "nioids tbids"; do
   set -- ${pair}
   n=$(LC_ALL=C comm -12 \
         <(grep '^[a-z]' "/tests/baseline/$1.txt" | LC_ALL=C sort) \
         <(grep '^[a-z]' "/tests/baseline/$2.txt" | LC_ALL=C sort) | grep -c . || true)
   [ "${n}" = 0 ] || bad "baseline/$1.txt and baseline/$2.txt share ${n} id(s)"
done
note "httpids (18), svcids (69), tbids (5) and nioids are four disjoint lists"

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
# ...and the sixty-nine, the five, and B-10's four. Four lists, four provisos,
# and only one of them fired.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.b15.exec.broken 0
   factck "${f}" gate.b15.exec        1.000000
   factck "${f}" B15.3.platform-lookup 1.000000
   factck "${f}" B15.jpms-services    1.000000
   factck "${f}" gate.c3.exec.broken  0
   factck "${f}" gate.c3.exec         1.000000
   factck "${f}" C3.text-blocks       1.000000
   factck "${f}" familyC              1.000000
   factck "${f}" gate.src.penalty     1
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held      1.000000
   factck "${f}" B11.java8lang-held  1.000000
   factck "${f}" B12.java8-library   1.000000
   factck "${f}" B13.java9-10        1.000000
done

# --- the same instrument, pointed at the other two lists ---------------------
# One of the sixty-nine, broken, and nothing else; then one of the five. The
# two runs are built the same way BEHAVONE is, out of the same golden, one line
# appended -- which is the reason both are here rather than just the
# arithmetic. If two of the lists were ever crossed, each run would move the
# other's gate and both would still pass their own assertion. The `unmoved`
# rows are what catches that.
#
#   SVCONE   68/69 = 0.985507 on B-15.3's proviso, over an unchanged min() of
#            1.000000, so B-15 = (0.35 + 0.45*0.985507)/0.80 = 0.991848
#   TBONE    4/5 = 0.800000 on C-3's, so C-3 = 0.800000 and
#            familyC = 0.74 + 0.26*0.800000 = 0.948000
#
# NEITHER FIGURE IS REACHABLE FROM THE OTHER LIST. Sixty-nine cannot produce
# 0.800000 and five cannot produce 0.985507, so a crossed pair does not merely
# score wrongly here, it scores impossibly. familyC 0.948000 is this run's
# alone: it read 0.960000, and shared that with L12 and L13, until NOTES 14.15
# moved C-3's weight from 0.20 to 0.26 on the automated-migrator calibration.
# The C-3 column is still what says WHY the three differ -- 1.000000 on the two
# switch rows and 0.800000 here -- and now the total says it too.
echo
echo "== can it fail: one service lookup, then one document recording"
svcone=$(printf '%s\n' "${svcids}" | head -1)
tbone=$(printf '%s\n' "${tbids}" | head -1)
for v in SVCONE TBONE; do
   rm -rf "${behavdir:?}/${v}"
   cp -R "${FIXDIR}/golden" "${behavdir}/${v}"
done
echo "behavior.differ[0]=${svcone}" >> "${behavdir}/SVCONE/behavior.summary"
echo "behavior.differ[0]=${tbone}"  >> "${behavdir}/TBONE/behavior.summary"
nsv=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/SVCONE/behavior.summary" || true)
ntbp=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/TBONE/behavior.summary" || true)
if [ "${nsv}" = 1 ] && [ "${ntbp}" = 1 ] && [ -n "${svcone}" ] && [ -n "${tbone}" ] \
   && [ "${svcone}" != "${tbone}" ]; then
   note "one broken service lookup (${svcone}) and one broken document recording (${tbone})"
else
   bad "the one-id perturbations did not land: svc ${nsv} (want 1) id '${svcone}'," \
       "tb ${ntbp} (want 1) id '${tbone}'"
fi
_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one SVCONE
run_one TBONE
FIXDIR=${_savedfix}

factck SVCONE gate.b15.exec.want    69
factck SVCONE gate.b15.exec.broken  1
factck SVCONE gate.b15.exec         0.985507
factck SVCONE gate.b15.departed     1.000000
factck SVCONE gate.b15.arrived      1.000000
factck SVCONE gate.b15.held         1.000000
factck SVCONE B15.2.module-names    1.000000
factck SVCONE B15.3.platform-lookup 0.985507
factck SVCONE B15.jpms-services     0.991848
# ...and the other three lists did not notice.
factck SVCONE gate.c3.exec.broken   0
factck SVCONE C3.text-blocks        1.000000
factck SVCONE familyC               1.000000
factck SVCONE gate.b14.c5.exec.broken 0
factck SVCONE B14.java11-jakarta    1.000000
factck SVCONE gate.b10.nio.behaviour.broken 0
factck SVCONE B10.java7-held        1.000000

factck TBONE gate.c3.exec.want      5
factck TBONE gate.c3.exec.broken    1
factck TBONE gate.c3.exec           0.800000
factck TBONE gate.c3.moved          1
factck TBONE gate.c3.movefrac       1.000000
factck TBONE gate.c3.held           7
factck TBONE gate.c3.holdfrac       1.000000
factck TBONE C3.text-blocks         0.800000
factck TBONE familyC                0.948000
# ...and the other three lists did not notice, and neither did the four legs of
# family C that are not C-3. This is the row that says the five-id list belongs
# to one leg and not to the family.
factck TBONE C1.level-61            1.000000
factck TBONE C2.pattern-instanceof  1.000000
factck TBONE C4.sealed-hierarchies  1.000000
factck TBONE C5.arrow-switch        1.000000
factck TBONE gate.b15.exec.broken   0
factck TBONE B15.jpms-services      1.000000
factck TBONE gate.b14.c5.exec.broken 0
factck TBONE B14.java11-jakarta     1.000000
factck TBONE gate.b10.nio.behaviour.broken 0
factck TBONE B10.java7-held         1.000000

# --- one absence, four consequences ------------------------------------------
# A summary that cannot be read at all is not the same as a summary that reads
# clean, and B-10 settled which way that falls at step 03: a proviso that
# cannot be checked must not be assumed satisfied. The tempting implementation
# is the other one -- leave the shape standing when there is no evidence -- and
# it is tempting because it never fires in a healthy run. It fires when the
# build fails, which is exactly when a tree that never ran would otherwise
# collect B-14.5, B-15.3 and C-3 for a file nobody executed.
#
# ONE FILE, FOUR PROVISOS. behavior.summary is a single artefact and B-10,
# B-14.5, B-15.3 and C-3 all read it against four different lists. There is no
# run in which it is absent for one of them and present for another, so there
# is one perturbation here and not four, and it has to assert all four outcomes
# at once. Step 08's draft of this section had two runs -- one that deleted the
# file and checked B-14.5 while reporting family C at 1.000000, and one that
# deleted the same file and checked C-3 while reporting B-14 at 1.000000 --
# and each was a tree the other proved impossible.
#
#   B-10      four nio ids unreadable -> nio_broken = 4 -> its third is halved
#             -> B-10 = 0.4 + 0.3 + 0.3*0.5 = 0.850000
#   B-14.5    0.000000 and the redirect quarter withdrawn with it
#             -> B-14 = (0.07+0.17+0.12)/0.90 = 0.400000
#   B-15.3    0.000000 over an unchanged min() of 1.000000
#             -> B-15 = 0.35/0.80 = 0.437500
#   C-3       0.000000 over an unchanged min() of 1.000000
#             -> familyC = 1 - 0.26 = 0.740000
#
# 0.850000 is also K19's B-12, 0.437500 is also K17's, M4's and M5's B-15, and
# 0.740000 is also L6's and L14's familyC -- L2's was on that list too until C-1's
# weight moved and took it to 0.900000. None of the three coincidences matters:
# no other row in this file reads all four of those numbers at once, and the
# facts below are what make it a reading rather than an arithmetic accident.
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
factck GONE gate.c3.exec.want      5
factck GONE gate.c3.exec.broken    5
factck GONE gate.c3.exec           0.000000
factck GONE C3.text-blocks         0.000000
factck GONE familyC                0.740000
# The shape is untouched in all four. Every source leg the four provisos scale
# is still full, which is what makes this the file-deleted case rather than the
# work-undone one.
factck GONE gate.b14.c5.legacy     0
factck GONE gate.b14.c5.types      3
factck GONE gate.b14.c5.redirects  1
factck GONE gate.b15.departed      1.000000
factck GONE gate.b15.arrived       1.000000
factck GONE gate.b15.held          1.000000
factck GONE B15.2.module-names     1.000000
factck GONE gate.c3.moved          1
factck GONE gate.c3.held           7
factck GONE C1.level-61            1.000000
factck GONE C2.pattern-instanceof  1.000000
factck GONE C4.sealed-hierarchies  1.000000
factck GONE C5.arrow-switch        1.000000
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
   factck "${f}" gate.c3.exec.broken 0
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
