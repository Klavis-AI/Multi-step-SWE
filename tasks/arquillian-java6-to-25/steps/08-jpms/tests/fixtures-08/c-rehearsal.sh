#!/usr/bin/env bash
# Rehearse the seven scoring blocks that read the typed AST and the published
# jars -- the hoisted source-AST block, B-10, B-11, B-12, B-13, B-14 and the
# whole of family C -- against trees whose answers were worked out first.
#
# Runs at image build time. Nothing here touches the candidate's tree: the
# seven blocks are lifted out of the SHIPPED test.sh by their own banners,
# composed in file order behind a srcgate() that hands them a fixture's report
# instead of compiling anything, and run once per fixture. What is asserted is
# the arithmetic over the report and over the jars, which is the part of these
# gates that no other check exercises.
#
# SEVEN blocks and not six. Step 07's rehearsal composed the source AST, B-10,
# B-11, B-12, B-13 and family C; this step adds B-14, which is step 07's whole
# family C inherited under a new name and renormalised over the 0.90 that is
# left when its C-1 is dropped. The extraction asserts all seven are disjoint
# and in that order in the file -- the counters are read in one place and spent
# in six others, and a hoist that silently un-hoisted would leave B-10 reading
# variables that do not exist yet, which under `set -u` is a dead verifier
# rather than a wrong number.
#
# FAMILY C REALLY RUNS java. This is the first rung whose own migration is not
# a source shape at all: two of its three legs ask java.lang.module what the
# published artifacts call themselves, through arqgate.Modules, and there is no
# way to rehearse that except by running it. So the fixtures carry real jars
# with real manifests -- wrapped at 72 bytes the way the jar tool wraps them,
# with a real META-INF/services/ entry in the one artifact whose descriptor the
# platform refuses -- and the run script below hands the block the same
# ${WORK}/modulejars.txt the build stage writes. What is exercised is the
# actual ModuleFinder, the actual manifest parse and the actual comm against
# baseline/modnames.txt.
#
# THE TWO ARTIFACT CHANNELS NOW SHARE ONE FILE, which is a change from step 07.
# At that rung C-1 read ${WORK}/scan.txt and B-12e read the jars, so a fixture
# could carry one and not the other and three of them did. Here both read
# ${WORK}/modulejars.txt, so `no jars` is ONE absence with TWO consequences --
# gate.cp=no-jars for B-12e and gate.mod=no-module-jars for family C -- and no
# fixture can separate them, because no tree can. K20 is that fixture and it
# asserts both halves of the one absence.
#
# The numbers in the TABLE below were computed by authoring/fixtures-step08/
# mkfix08.py, by an independent implementation of the same composition, and
# TRANSCRIBED HERE BY HAND. Nothing at build time runs that file -- the verifier
# image has no python3, which makes the separation structural. A rehearsal that
# computed its own expectations would agree with the scorer by construction and
# would assert nothing at all. The separation is sharper at this rung than it
# has ever been: mkfix08.py predicts from the jar PLAN -- twelve Mod records,
# each with a name or without one and poisoned or not -- and the rehearsal
# measures the platform's reading of the bytes that plan produced. The two
# agreeing is a fact about java.lang.module and not about either program.
#
# All nine figures are compared AS STRINGS, because the shell's spelling of a
# gate is part of what is being pinned: `b10=0` and `b11=0` are literals the
# blocks assign and never reach, while a gate they compute publishes "%.6f".
# They are the same number and not the same fact, and normalising them would
# lose the difference between a gate that scored zero and one that never ran.
#
# THREE SPELLINGS OF ZERO, and K17 carries all three at once:
#
#   `0`          B-10 and B-11 initialise their score to it and their measured
#                guard never opens, so the literal survives to the fact. So do
#                gate.c3.departed, gate.c3.arrived and gate.c3.held, which are
#                initialised above family C's guard and published below it.
#   `0.000000`   B-12, B-13, B-14 and C-3 are totals computed by an awk that
#                sits OUTSIDE the guard, so they are computed from parts that
#                never ran.
#   MISSING      every gate.b14.* fact and gate.src.penalty are published
#                INSIDE B-14's guard, so a tree nobody could compile has no
#                penalty fact rather than an unfired `1`.
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

# ...and family C shells out to java, which is new at this rung and is the one
# dependency this file has that no earlier rehearsal had. Two separate things
# can be missing -- the runtime and the gate jar the build.sh above this
# produced -- and they fail identically at the fact level (gate.mod=broken,
# C-1 and C-2 zero, familyC 0.45) so they are separated here.
command -v java >/dev/null 2>&1 \
   || { echo "c-rehearsal: no java; family C cannot be rehearsed" >&2; exit 1; }
[ -r "${GATEJAR}" ] \
   || { echo "c-rehearsal: no ${GATEJAR}; run gates/build.sh first" >&2; exit 1; }

# --- the instrument, before the finding --------------------------------------
# Every C-1 and C-2 figure below is a reading taken by arqgate.Modules, and a
# reading is worth what the instrument is worth. So: run it once, here, over
# the golden fixture's twelve jars, and require the three counts the whole of
# family C divides by. If this says 12/12/11 the table's C-1 and C-2 columns
# are measuring the platform; if it says anything else, every row falls
# together and the twenty-eight failures that follow would all be one.
#
# THE POISONED JAR IS PART OF THE INSTRUMENT CHECK. arquillian-impl-base ships
# a META-INF/services/org.jboss.arquillian.spi.client.container.ArquillianDescriptor
# naming a provider class that is not in the module, and ModuleFinder refuses
# to derive a descriptor for it -- which is why derivable is 11 and not 12, and
# why the golden's mod.resolved is 0 with every name correctly declared. That
# refusal is a documented ModuleFinder behaviour rather than an accident of
# this fixture, and the whole of C-1's min() term is priced on it holding.
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
echo "== extracting the seven blocks from ${SRC}"

# --- the helpers -------------------------------------------------------------
# One-liners at column 0, which is why they can be lifted by name. Exactly four,
# asserted: a rehearsal that quietly picked up three would be composing blocks
# whose min() is missing, and every min(arrival, departure) gate at this step --
# which is four of B-14's legs and two of family C's three -- would read the
# arrival half alone.
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

# --- block 7: family C -------------------------------------------------------
sed -n '/^# =* family C$/,/^fact "familyC=/p' "${SRC}" > "${TMP}/block-c.sh"
head -1 "${TMP}/block-c.sh" | grep -q 'family C$' \
   || bad "the family-C block does not start at its banner"
tail -1 "${TMP}/block-c.sh" | grep -q '^fact "familyC=' \
   || bad "the family-C block does not end at its own fact"

BLOCKS="block-src block-b10 block-b11 block-b12 block-b13 block-b14 block-c"
for f in ${BLOCKS}; do
   note "$(printf '%-10s %s lines' "${f}" "$(grep -c '' "${TMP}/${f}.sh")")"
done

# The seven are disjoint and in file order. This is the hoist, asserted: B-10
# through B-14 spend counters the first block reads, so the first block has to
# come first, and family C spends them again after all five.
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
s7=$(grep -n '^# =* family C$' "${SRC}" | head -1 | cut -d: -f1)
for v in s1 e1 s2 e2 s3 e3 s4 e4 s5 e5 s6 e6 s7; do
   eval "[ -n \"\${${v}}\" ]" || bad "anchor ${v} not found in ${SRC}"
done
if [ "${s1}" -lt "${e1}" ] && [ "${e1}" -lt "${s2}" ] && [ "${s2}" -lt "${e2}" ] \
   && [ "${e2}" -lt "${s3}" ] && [ "${s3}" -lt "${e3}" ] && [ "${e3}" -lt "${s4}" ] \
   && [ "${s4}" -lt "${e4}" ] && [ "${e4}" -lt "${s5}" ] && [ "${s5}" -lt "${e5}" ] \
   && [ "${e5}" -lt "${s6}" ] && [ "${s6}" -lt "${e6}" ] && [ "${e6}" -lt "${s7}" ]; then
   note "order: src ${s1}..${e1} < B-10 ${s2}..${e2} < B-11 ${s3}..${e3} < B-12 ${s4}..${e4} < B-13 ${s5}..${e5} < B-14 ${s6}..${e6} < C ${s7}"
else
   bad "the seven blocks are not disjoint and in order:" \
       "${s1} ${e1} ${s2} ${e2} ${s3} ${e3} ${s4} ${e4} ${s5} ${e5} ${s6} ${e6} ${s7}"
fi

# --- what this rung stopped reading ------------------------------------------
# Two of step 07's channels are GONE from these blocks and their absence is
# asserted positively, because the failure they would cause is silent. C-1 no
# longer reads a class-file major out of a scan report: that assertion moved up
# into B-4, which is nowhere near any block lifted here, and the scan file this
# rehearsal used to stage is no longer written. A block that still reached for
# WANT_MAJOR would read an unset variable under `set -u` and take the whole
# composed run down; a block that still reached for ${WORK}/scan.txt would find
# nothing and score a clean, wrong zero.
for blk in ${BLOCKS}; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c 'WANT_MAJOR' || true)
   [ "${n}" = 0 ] || bad "${blk} still reads WANT_MAJOR ${n} time(s); C-1's level assertion moved to B-4"
   # ANCHORED ON THE SLASH. Family C writes ${WORK}/buk-svclegacyscan.txt, and
   # an unanchored `scan\.txt` matches the tail of that name -- which reported
   # this rehearsal's own bucket file as a step-07 leftover on its first run.
   # The real reference was always ${WORK}/scan.txt, so the separator is part
   # of the pattern.
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" | grep -c '/scan\.txt' || true)
   [ "${n}" = 0 ] || bad "${blk} still reads scan.txt ${n} time(s); this rehearsal no longer stages one"
done
note "no block reads WANT_MAJOR or scan.txt: step 07's two C-1 channels are gone"

# Every gate this rehearsal claims to exercise has to be INSIDE one of the seven
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
inblock block-src 'setwant() {'
inblock block-src 'nsbuckets() {'
inblock block-src 'nswant() {'
inblock block-src 'gone() {'
inblock block-src 'hit()'
inblock block-src 'bv() {'
inblock block-src 'tv() {'
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
inblock block-c 'C1.modules-named='
inblock block-c 'C2.module-names='
inblock block-c 'C3.platform-lookup='
inblock block-c 'gate.mod.total='
inblock block-c 'gate.mod.declared='
inblock block-c 'gate.mod.derivable='
inblock block-c 'gate.mod.resolved='
inblock block-c 'gate.mod.resolve='
inblock block-c 'gate.mod=no-module-jars'
inblock block-c 'gate.c1.declared='
inblock block-c 'gate.c1.readable='
inblock block-c 'gate.c1.right='
inblock block-c 'gate.c2.names='
inblock block-c 'gate.c3.departed='
inblock block-c 'gate.c3.arrived='
inblock block-c 'gate.c3.held='
inblock block-c 'gate.c3.exec.want='
inblock block-c 'modulejars.txt'
inblock block-c 'modules.txt'
inblock block-c 'baseline/modnames.txt'
inblock block-c 'baseline/svc.txt'
inblock block-c 'baseline/svcids.txt'

# --- the guards, and which side of them each leg is on -----------------------
# THE INVERSION THIS RUNG INTRODUCES. At every step from 02 to 07 family C sat
# wholly inside its block's "did anything compile" guard, because every leg it
# had read the source AST. Here two of the three legs read the PUBLISHED JARS,
# and a tree whose sources the scorer could not compile against its own
# classpath may perfectly well have published twelve correctly named
# artifacts -- so C-1 and C-2 sit ABOVE the guard and only C-3 sits inside it.
# B-12e is outside its own block's guard for the same reason and has been since
# step 05. Asserted structurally here and scored on K17 below, because this is
# the kind of property a later edit moves by accident while making a guard
# tidier, and because the cost of getting it wrong is a silent 0.55 handed to a
# tree that never built.
#
# `guardend` takes the guard's opening line as a pattern rather than assuming
# one spelling: B-10 through B-14 open with `if [ -z "${measured}" ]` and take
# the else branch when there IS a report, while family C opens with
# `if [ -n ... ] && [ -s ... ]` and takes the then branch. Two shapes, one
# meaning, and a helper that only knew the first would report family C's guard
# as missing and pass.
# -F, and that is load-bearing rather than tidy. Both guard spellings open
# with `[`, which a basic regular expression reads as a bracket expression, so
# ZGUARD matched its own line by accident -- `if ` plus any one character --
# and CGUARD, which has a second `[` after a literal ` && `, matched nothing at
# all and reported family C's guard as missing. A pattern that is a line of
# shell is a fixed string.
guardline() { grep -nF -- "$2" "${TMP}/$1.sh" | head -1 | cut -d: -f1; }
guardend() {
   awk -v pat="$2" 'index($0, pat) == 1 { g = 1 } g && /^fi$/ { print NR; exit }' \
      "${TMP}/$1.sh"
}
ZGUARD='if [ -z "${measured}" ]'
CGUARD='if [ -n "${measured}" ] && [ -s "${measured}" ]'

gend=$(guardend block-b12 "${ZGUARD}")
cpline=$(guardline block-b12 'modulejars.txt')
if [ -n "${gend}" ] && [ -n "${cpline}" ] && [ "${cpline}" -gt "${gend}" ]; then
   note "B-12e is outside the measured-report guard (guard ends ${gend}, B-12e at ${cpline})"
else
   bad "B-12e is inside the measured-report guard, or the guard was not found:" \
       "guard ${gend:-?} B-12e ${cpline:-?}"
fi

cgs=$(guardline block-c "${CGUARD}")
cge=$(guardend block-c "${CGUARD}")
modline=$(guardline block-c 'arqgate.Modules')
c1line=$(guardline block-c 'fact "C1.modules-named=')
c2line=$(guardline block-c 'fact "C2.module-names=')
fbline=$(guardline block-c 'fbuckets svcload svcarrive')
depline=$(guardline block-c 'fact "gate.c3.departed=')
for v in cgs cge modline c1line c2line fbline depline; do
   eval "[ -n \"\${${v}}\" ]" || bad "family C's ${v} anchor was not found"
done
if [ "${modline}" -lt "${cgs}" ] && [ "${c1line}" -lt "${cgs}" ] \
   && [ "${c2line}" -lt "${cgs}" ]; then
   note "the module read, C-1 and C-2 are all above family C's measured guard (${modline} ${c1line} ${c2line} < ${cgs})"
else
   bad "C-1 or C-2 has moved inside family C's measured guard:" \
       "modules ${modline} C-1 ${c1line} C-2 ${c2line} guard ${cgs}"
fi
if [ "${fbline}" -gt "${cgs}" ] && [ "${fbline}" -lt "${cge}" ] \
   && [ "${depline}" -gt "${cge}" ]; then
   note "C-3's buckets are inside the guard (${cgs} < ${fbline} < ${cge}) and its facts are published below it (${depline})"
else
   bad "C-3's guard geometry has moved: guard ${cgs}..${cge}, buckets ${fbline}, facts ${depline}"
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
for blk in block-b10 block-b11 block-b12 block-b13 block-b14 block-c; do
   n=$(grep -c 'srcgate ' "${TMP}/${blk}.sh" || true)
   [ "${n}" = 0 ] || bad "${blk} calls srcgate ${n} time(s); the compiles are hoisted"
done
note "srcgate: 3 calls, all in the source-AST block"

# Nothing in any of the seven blocks may run a compiler, a build or a
# container. They are arithmetic over a report, one pass of unzip over jars
# that already exist and ONE run of the module gate -- that is what makes them
# rehearsable at all -- and the one thing that touches the tree is srcgate(),
# which is replaced below.
#
# `java` is split out from the other three and is not forbidden outright,
# because at this rung it is load-bearing. It is forbidden in six blocks and
# allowed EXACTLY ONCE in the seventh, on a line that also names
# arqgate.Modules: a second java invocation in family C would be a second
# reading of the same jars, and a java in any other block would be a gate
# measuring something this rehearsal has not staged. Comments are stripped
# first: the blocks discuss javac at length.
cat "${TMP}"/block-*.sh | sed 's/#.*//' \
   | grep -nE '(^|[;&|(]|\$\() *(javac|docker|\./gradlew)([ "]|$)' \
   > "${TMP}/runs.txt" || true
if [ -s "${TMP}/runs.txt" ]; then
   bad "a block runs a compiler, a build or a container:"
   sed 's/^/         /' "${TMP}/runs.txt"
else
   note "no block runs javac, docker or gradlew"
fi
for blk in block-src block-b10 block-b11 block-b12 block-b13 block-b14; do
   n=$(sed 's/#.*//' "${TMP}/${blk}.sh" \
       | grep -cE '(^|[;&|(]|\$\() *java([ "]|$)' || true)
   [ "${n}" = 0 ] || bad "${blk} runs java ${n} time(s); only family C may"
done
njava=$(sed 's/#.*//' "${TMP}/block-c.sh" \
        | grep -cE '(^|[;&|(]|\$\() *java([ "]|$)' || true)
nmod=$(sed 's/#.*//' "${TMP}/block-c.sh" \
       | grep -E '(^|[;&|(]|\$\() *java([ "]|$)' | grep -c 'arqgate\.Modules' || true)
if [ "${njava}" = 1 ] && [ "${nmod}" = 1 ]; then
   note "family C runs java exactly once, on arqgate.Modules; no other block runs it"
else
   bad "family C's java invocations: ${njava} (want 1), of which arqgate.Modules: ${nmod} (want 1)"
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
      # writes and which B-12e and family C's first two legs BOTH read. One
      # file, two channels: a fixture with no jars/ directory leaves it empty,
      # which is exactly how a build that published nothing reaches both
      # blocks -- `[ -s ]` and not `[ -f ]` in each of them.
      echo 'ls "${FIXDIR}"/jars/*.jar > "${WORK}/modulejars.txt" 2>/dev/null || :'
      cat "${TMP}/helpers.sh" "${TMP}/shim.sh" \
          "${TMP}/block-src.sh" "${TMP}/block-b10.sh" "${TMP}/block-b11.sh" \
          "${TMP}/block-b12.sh" "${TMP}/block-b13.sh" "${TMP}/block-b14.sh" \
          "${TMP}/block-c.sh"
   } > "${run}"
   bash "${run}" > "${TMP}/out-${fix}.txt" 2>&1
   cp "${TMP}/w-${fix}/facts.txt" "${TMP}/facts-${fix}.txt" 2>/dev/null \
      || : > "${TMP}/facts-${fix}.txt"
}
fv() { sed -n "s/^$2=//p" "${TMP}/facts-$1.txt" | head -1; }

# --- what each fixture must score --------------------------------------------
# fixture  B10 B11 B12 B13 B14 | C1 C2 C3 | familyC
# Transcribed by hand from mkfix08.py's printed table. The two ends are the
# rows that carry the step's floor argument: `pinned` is the tree this rung is
# handed and must read every family-C leg at 0 with all five held gates in
# full -- migration_prompt item 6's nop run -- and `golden` must read all nine
# in full, which is item 6's oracle.
#
# NINE columns and not five, because at this rung family C's three legs are
# not interchangeable and the total hides which one moved. K2 and K20 both
# read familyC 0.450000 and they are different trees: K2 published twelve jars
# and named none of them, K20 published nothing at all. Only the C-1 and C-2
# columns, and the gate.mod.* facts below, tell them apart.
TABLE=$(cat <<'EOF'
pinned   1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0.000000 0.000000 0.000000
golden   1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K1       1.000000 1.000000 1.000000 1.000000 1.000000 0.916667 0.840278 1.000000 0.927431
K2       1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0.000000 1.000000 0.450000
K3       1.000000 1.000000 1.000000 1.000000 1.000000 0.833333 0.694444 1.000000 0.859722
K4       1.000000 1.000000 1.000000 1.000000 0.955555 1.000000 1.000000 1.000000 1.000000
K5       1.000000 1.000000 1.000000 1.000000 0.987879 1.000000 1.000000 1.000000 1.000000
K6       1.000000 1.000000 1.000000 1.000000 0.927273 1.000000 1.000000 1.000000 1.000000
K7       1.000000 1.000000 1.000000 1.000000 0.995607 1.000000 1.000000 1.000000 1.000000
K8       1.000000 1.000000 1.000000 1.000000 0.932540 1.000000 1.000000 1.000000 1.000000
K9       1.000000 1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000
K10      1.000000 1.000000 1.000000 1.000000 0.916667 1.000000 1.000000 1.000000 1.000000
K11      1.000000 1.000000 1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000
K12      1.000000 1.000000 1.000000 1.000000 0.700000 1.000000 1.000000 1.000000 1.000000
K13      1.000000 1.000000 1.000000 1.000000 0.600000 1.000000 1.000000 1.000000 1.000000
K14      1.000000 1.000000 1.000000 1.000000 0.800000 1.000000 1.000000 1.000000 1.000000
K15      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000
K16      1.000000 1.000000 1.000000 1.000000 0.500000 1.000000 1.000000 1.000000 1.000000
K17      0        0        0.000000 0.000000 0.000000 1.000000 1.000000 0.000000 0.550000
K18      1.000000 1.000000 1.000000 0.866667 1.000000 1.000000 1.000000 1.000000 1.000000
K19      1.000000 1.000000 0.850000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000
K20      1.000000 1.000000 0.850000 1.000000 1.000000 0.000000 0.000000 1.000000 0.450000
M1       1.000000 1.000000 1.000000 1.000000 1.000000 0.909091 0.909091 1.000000 0.950000
M2       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.500000 0.775000
M3       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.909091 0.959091
M4       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0.550000
M5       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.000000 0.550000
M6       1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.869565 0.941304
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

# ...and no two of them are the same tree. Twenty-eight rows of which two were
# accidentally identical would look like twenty-eight assertions and be
# twenty-seven, and the duplicate would agree with its twin whatever either of
# them was measuring. This matters more at this rung than at the last: the jars
# are generated, and a generator bug that ignored a fixture's `mods=` argument
# would produce byte-identical jar sets under different names. why.txt is
# excluded: it is prose about the fixture and two fixtures could legitimately
# share a sentence.
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
   fixture B10 B11 B12 B13 B14 C1 C2 C3 familyC
while read -r fix w10 w11 w12 w13 w14 wc1 wc2 wc3 wfc; do
   [ -n "${fix}" ] || continue
   [ -d "${FIXDIR}/${fix}" ] || { bad "no fixture ${fix}"; continue; }
   run_one "${fix}"
   g10=$(fv "${fix}" 'B10\.java7-held')
   g11=$(fv "${fix}" 'B11\.java8lang-held')
   g12=$(fv "${fix}" 'B12\.java8-library')
   g13=$(fv "${fix}" 'B13\.java9-10')
   g14=$(fv "${fix}" 'B14\.java11-jakarta')
   gc1=$(fv "${fix}" 'C1\.modules-named')
   gc2=$(fv "${fix}" 'C2\.module-names')
   gc3=$(fv "${fix}" 'C3\.platform-lookup')
   gfc=$(fv "${fix}" familyC)
   printf '  %-8s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %-9s %s\n' \
      "${fix}" "${g10:-MISSING}" "${g11:-MISSING}" "${g12:-MISSING}" \
      "${g13:-MISSING}" "${g14:-MISSING}" "${gc1:-MISSING}" "${gc2:-MISSING}" \
      "${gc3:-MISSING}" "${gfc:-MISSING}"
   [ "${g10}" = "${w10}" ] || bad "${fix}: B10 want ${w10} got ${g10:-MISSING}"
   [ "${g11}" = "${w11}" ] || bad "${fix}: B11 want ${w11} got ${g11:-MISSING}"
   [ "${g12}" = "${w12}" ] || bad "${fix}: B12 want ${w12} got ${g12:-MISSING}"
   [ "${g13}" = "${w13}" ] || bad "${fix}: B13 want ${w13} got ${g13:-MISSING}"
   [ "${g14}" = "${w14}" ] || bad "${fix}: B14 want ${w14} got ${g14:-MISSING}"
   [ "${gc1}" = "${wc1}" ] || bad "${fix}: C1 want ${wc1} got ${gc1:-MISSING}"
   [ "${gc2}" = "${wc2}" ] || bad "${fix}: C2 want ${wc2} got ${gc2:-MISSING}"
   [ "${gc3}" = "${wc3}" ] || bad "${fix}: C3 want ${wc3} got ${gc3:-MISSING}"
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
# This rung's input tree is the step-07 golden. It compiles at 11 AND at 10,
# every family-C leg reads zero, and all five held gates read full. That is
# migration_prompt item 6's nop run, asserted at image build time rather than
# only in a job.
#
# Every `want` here is the size of a frozen set or a baseline counter, and
# reading them off the facts is how this rehearsal knows the baseline files
# reached the image at all -- a missing set file makes the want 0 and every
# fraction over it 0, which scores a tree that did nothing the same as one
# that did everything.
#
# THE TWO INTERESTING CELLS ARE gate.mod.derivable AND gate.c3.held. The input
# tree already publishes eleven jars the platform can read as modules and it
# already holds all eleven by-name loads that are not service lookups: both
# are properties it has for free. Neither is a leg with its own weight --
# c1read is a min() term and c3keep is one of three inside a min() -- which is
# exactly the arithmetic that keeps the floor at 0.30*A + 0.175 instead of
# paying the unmigrated tree for standing still.
factck pinned gate.src.ok11 yes
factck pinned gate.src.ok10 yes
factck pinned gate.mod.total 12
factck pinned gate.mod.declared 0
factck pinned gate.mod.derivable 11
factck pinned gate.c1.declared 0.000000
factck pinned gate.c1.readable 1.000000
factck pinned gate.c1.right 0.000000
factck pinned gate.c2.names 0
factck pinned gate.c3.departed 0.000000
factck pinned gate.c3.arrived 0.000000
factck pinned gate.c3.held 1.000000
factck pinned gate.c3.exec.want 69
factck pinned gate.c3.exec.broken 0
factck pinned gate.c3.exec 1.000000
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
# The one difference from `pinned` on the source side is C-3's two buckets; on
# the artifact side it is twelve manifests. gate.mod.declared going 0 -> 12 and
# gate.c2.names going 0 -> 12 is the whole of this step's first two legs, and
# gate.mod.derivable staying at 11 is the assertion that the correct answer
# does NOT delete the descriptor service file to make the number tidier.
factck golden gate.mod.total 12
factck golden gate.mod.declared 12
factck golden gate.mod.derivable 11
factck golden gate.c1.declared 1.000000
factck golden gate.c1.readable 1.000000
factck golden gate.c1.right 1.000000
factck golden gate.c2.names 12
factck golden gate.c3.departed 1.000000
factck golden gate.c3.arrived 1.000000
factck golden gate.c3.held 1.000000
factck golden gate.c3.exec.want 69
factck golden gate.c3.exec.broken 0
factck golden gate.c3.exec 1.000000
factck golden gate.src.penalty 1

# The one fact in family C that cannot be asserted by equality, and the reason
# is worth writing down: arqgate.Modules reports the resolution failure with
# the jar's path as the platform saw it, which is an ABSOLUTE path under a
# mktemp directory and is different on every run. A factck on it would fail
# always and a factck on the empty string would pass always.
#
# What is asserted instead is the shape and the subject. The golden declares
# twelve names correctly and STILL does not resolve as a module configuration,
# because arquillian-impl-base ships a META-INF/services/ entry naming a
# provider that is not in the module and ModuleFinder refuses to derive a
# descriptor for that jar. One unreadable artifact takes the whole
# configuration down, so mod.resolved is 0 on the correct answer. That is not
# a defect in the golden and it is not scored: nothing in family C reads
# mod.resolved. It is published so the trajectory analysis can see WHY the
# number it is looking at is what it is, and it is checked here so that a
# future edit which "fixes" it by deleting the service file gets caught by the
# gate.mod.derivable assertion above rather than by a candidate.
factck golden gate.mod.resolved 0
gres=$(fv golden 'gate\.mod\.resolve')
case "${gres}" in
   Unable_to_derive_module_descriptor_for_*arquillian-impl-base*)
      note "golden gate.mod.resolve names the poisoned artifact: ${gres##*/}" ;;
   *) bad "golden gate.mod.resolve does not name the poisoned artifact: ${gres:-MISSING}" ;;
esac
# ...and the tree that published nothing has nothing to say about resolution.
factck K20 gate.mod.resolve unread
factck K20 gate.mod 'no-module-jars'

# --- the three ways to lose C-1 and C-2 --------------------------------------
# K1, K2 and K20 all lose module names and they lose them differently. K1
# publishes twelve jars and forgets one manifest attribute: eleven declared,
# eleven names, C-1 11/12 and C-2 scaled by it. K2 publishes the same twelve
# jars with no attribute at all: the state this rung's input tree is in, which
# is why its C-1 and C-2 match `pinned` while its C-3 does not. K20 published
# nothing, so both legs read zero for a reason no manifest edit could fix.
#
# THE THREE ARE NOT INTERCHANGEABLE and the facts say so: mod.total is 12, 12
# and 0. A gate that divided by mod.total instead of by the baseline's twelve
# would read K20's C-1 as 0/0 and, depending on which way frac() fell, as
# either 0 or 1 -- and 1 would hand 0.55 of family C to a build that published
# nothing.
factck K1 gate.mod.total 12
factck K1 gate.mod.declared 11
factck K1 gate.mod.derivable 11
factck K1 gate.c1.declared 0.916667
factck K1 gate.c1.readable 1.000000
factck K1 gate.c1.right 0.916667
factck K1 gate.c2.names 11
factck K2 gate.mod.total 12
factck K2 gate.mod.declared 0
factck K2 gate.c1.declared 0.000000
factck K2 gate.c2.names 0
factck K20 gate.mod.total 0
factck K20 gate.mod.declared 0
factck K20 gate.mod.derivable 0
factck K20 gate.c1.declared 0.000000
factck K20 gate.c1.readable 0.000000
factck K20 gate.c1.right 0.000000
factck K20 gate.c2.names 0

# K3 is the fixture the whole of C-2 exists for -- and, since round 1 of this
# step's review, the fixture that shows C-1's third term working. Twelve jars,
# twelve manifest attributes, every one of them legal -- and two of the names
# were derived from the file name rather than from the packages the sources are
# in. That is the reading an agent who never opened the sources produces, it is
# the reading that would be wrong FOREVER once published, and the only thing in
# this verifier that can see it is the pairwise comm against
# baseline/modnames.txt.
#
# SO C-1 FALLS WITH IT NOW, and that is the row's point. declared is still 12/12
# and readable still 11/11 -- both legs of the old two-term C-1 read 1.000000 --
# but right is 10/12 = 0.833333 and C-1 is the min of the three. Before the
# third term this fixture read C-1 1.000000 and C-2 0.833333 for familyC
# 0.941667; it now reads 0.833333 and 0.694444 for 0.859722. The gap between
# those two totals, 0.081945, is what naming twelve artifacts without reading
# their sources used to be worth.
factck K3 gate.mod.declared 12
factck K3 gate.mod.derivable 11
factck K3 gate.c1.declared 1.000000
factck K3 gate.c1.readable 1.000000
factck K3 gate.c1.right 0.833333
factck K3 gate.c2.names 10
factck K3 C1.modules-named 0.833333
factck K3 C2.module-names 0.694444

# ...and M1 is the third of C-1's three min() terms: every name declared and
# correct, and one more artifact the platform will not read, because the
# candidate invented a second malformed file under META-INF/services/. C-1
# falls to 10/11 even though c1decl AND c1right are both still 1.000000, which
# is what a min() term is for and what a weighted leg would not have done.
#
# K3 AND M1 ARE THE PAIR THAT PROVES THE min() IS OVER THREE THINGS AND NOT TWO.
# Each holds two of the terms at 1.000000 and drops the third, and they drop
# different ones: K3 keeps declared and readable and loses right, M1 keeps
# declared and right and loses readable. A C-1 that had quietly gone back to
# two terms would read 1.000000 on one of the two.
factck M1 gate.mod.declared 12
factck M1 gate.mod.derivable 10
factck M1 gate.c1.declared 1.000000
factck M1 gate.c1.readable 0.909091
factck M1 gate.c1.right 1.000000
factck M1 gate.c2.names 12

# --- C-3's three halves, one at a time ---------------------------------------
# M2, M3 and M4 each break exactly one of them, and the min() means each one
# alone is enough. M2 puts the spi's ServiceLoader#reload back to a hand-rolled
# scan -- the departure half AND the arrival half fall together, because they
# are two readings of the same method. M3 stops FilteredURLClassLoader#loadClass
# loading by name, which is the hold half and is the leg that says "eleven
# by-name loads are NOT service lookups and must not move". M4 turns both
# platformload records into platformtype: `java.util.ServiceLoader x;` declared
# and never called, which is the shape of a migration that compiles and
# resolves nothing.
factck M2 gate.c3.departed 0.500000
factck M2 gate.c3.arrived 0.500000
factck M2 gate.c3.held 1.000000
factck M3 gate.c3.departed 1.000000
factck M3 gate.c3.arrived 1.000000
factck M3 gate.c3.held 0.909091
factck M4 gate.c3.departed 1.000000
factck M4 gate.c3.arrived 0.000000
factck M4 gate.c3.held 1.000000
# ...and M5 and M6 break none of the three and lose C-3 anyway, because the
# scaling is the recorded behaviour and not the source shape. M5 is the file
# that has every bucket in the right place and resolves nothing at run time.
factck M5 gate.c3.departed 1.000000
factck M5 gate.c3.arrived 1.000000
factck M5 gate.c3.held 1.000000
factck M5 gate.c3.exec.want 69
factck M5 gate.c3.exec.broken 69
factck M5 gate.c3.exec 0.000000
factck M6 gate.c3.exec.want 69
factck M6 gate.c3.exec.broken 9
factck M6 gate.c3.exec 0.869565

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
# K17 has no report and twelve intact published jars, which is the combination
# this rung's guard inversion exists for. Family C's first two legs read the
# jars and are UNMOVED at 1.000000; C-3 reads the source AST and is not, and
# says so rather than reading an empty bucket file as "both departed".
#
# Read the three columns together. gate.c3.departed is the shell literal `0`,
# assigned above the guard and published below it. B-12, B-13, B-14 and C-3
# are `0.000000`, computed by awks that sit outside their guards from parts
# that never ran. Every gate.b14.* fact and gate.src.penalty are MISSING,
# published only inside B-14's guard. Three spellings, one tree, and the only
# way to tell a gate that scored zero from one that never ran.
factck K17 gate.src.ok11 no
factck K17 gate.src.okdef no
factck K17 gate.mod.total 12
factck K17 gate.mod.declared 12
factck K17 gate.mod.derivable 11
factck K17 gate.c1.declared 1.000000
factck K17 gate.c2.names 12
factck K17 gate.c3.departed 0
factck K17 gate.c3.arrived 0
factck K17 gate.c3.held 0
# ...and the scaling is not the thing that fell: K17's behavior.summary is the
# golden's and reads clean. C-3 is zero because its three halves are, which is
# the guard doing its job and not the proviso doing it.
factck K17 gate.c3.exec.want 69
factck K17 gate.c3.exec.broken 0
factck K17 gate.c3.exec 1.000000
for k in gate.b14.c2.want gate.b14.c3.want gate.b14.c4.want \
         gate.b14.c5.exec.want gate.b14.c5.redirects gate.src.penalty \
         gate.src.impjx B14.2.jakarta B14.5.httpclient; do
   factck K17 "${k}" MISSING
done

# --- K18 and K19: the two held gates that are not B-14 -----------------------
# Both are here because they move a column no family-C fixture moves, which is
# how this rehearsal knows the nine columns are nine channels. K18 puts two
# reflection buckets back to Class.newInstance and costs B-13 alone; K19 is the
# build that dropped a module, and it is the one fixture where B-12's four AST
# legs are full and its constant-pool leg is empty.
#
# K19 IS THE FIXTURE THAT SEPARATES THE TWO CHANNELS THAT SHARE A FILE. Its
# manifests are untouched, so family C reads twelve declared names and twelve
# correct ones off exactly the jars B-12e finds nothing in. One jar set, two
# readings, and only one of them falls.
factck K18 gate.mod.declared 12
factck K18 gate.c2.names 12
factck K19 gate.cp.jars 12
factck K19 gate.cp.classes 24
factck K19 gate.cp.optional 0
factck K19 gate.cp.collectors 0
factck K19 gate.cp.objects 0
factck K19 gate.cp.jtime 0
factck K19 gate.mod.declared 12
factck K19 gate.c2.names 12
# ...and the golden's own constant pools, so that K19's zeros are a difference
# and not a tool that never worked. The four floors come out of
# baseline/target.txt and the jars were written to meet them exactly.
factck golden gate.cp.jars 12
factck golden gate.cp.classes 24
factck golden gate.cp.optional 5
factck golden gate.cp.collectors 6
factck golden gate.cp.objects 2
factck golden gate.cp.jtime 2
# K20 is the same absence read by the other channel: no jars at all, so B-12e
# publishes the sentinel rather than four counts.
factck K20 gate.cp 'no-jars'

# --- and the rehearsal proves it can fail ------------------------------------
# Everything above this line is a clean run against a fixture whose answer was
# written down first, and a rehearsal whose assertions cannot fire agrees with
# anything. So: take the golden -- the one tree that scores every gate in
# full -- move ONE thing, and require the composed blocks to notice. Six
# perturbations, on five different channels, and every expected figure below
# is derived in the comment above it rather than transcribed, because these
# trees do not exist in mkfix08.py's table.
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
# (0.07 + 0.17 + 0.12*0.8 + 0.54) / 0.90 = 0.876/0.90 = 0.973333, and family C
# does not move at all, because nothing in family C reads a toarray record.
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
# ...and the rest of the tree, unmoved. Family C is the load-bearing one: a
# source-report edit that moved a module leg would mean the two families are
# reading each other's channels.
factck PERT B14.2.jakarta        1.000000
factck PERT B14.3.javax-held     1.000000
factck PERT B14.5.httpclient     1.000000
factck PERT gate.src.penalty     1
factck PERT C1.modules-named     1.000000
factck PERT C2.module-names      1.000000
factck PERT C3.platform-lookup   1.000000
factck PERT familyC              1.000000
factck PERT B10.java7-held       1.000000
factck PERT B11.java8lang-held   1.000000
factck PERT B12.java8-library    1.000000
factck PERT B13.java9-10         1.000000

# --- can it fail, on the channel this rung is actually about -----------------
# The perturbation above moves a source report and watches a source leg fall.
# Family C's first two legs read no source report at all: they read twelve
# files on disk through java.lang.module. So the second perturbation deletes
# one of them.
#
# arquillian-testng is chosen and the choice is not arbitrary. It carries none
# of B-12e's four marker packages -- the markers sit in the first six class
# files, which are api, spi and impl-base -- so B-12's five legs are unmoved
# and the fall is family C's alone. It is also not the poisoned artifact, so
# derivable falls by one for the same reason total does and not for two.
#
#   mod.total 11, mod.declared 11, mod.derivable 10
#   c1decl = 11/12 = 0.916667  c1read = 10/11 = 0.909091  c1right = 11/12 = 0.916667
#   C-1 = min of the three = 0.909091, the readable term; the third term is not
#         the minimum here, because the eleven jars that remain are all named
#         correctly and it is the twelfth's absence that costs
#   c2names = 11 -> 11/12 = 0.916667, scaled by C-1 -> 0.833334
#   C-3 = 1.000000, nothing in the source moved
#   familyC = 0.20*0.909091 + 0.35*0.833334 + 0.45 = 0.923485
#
# THE C-2 FIGURE IS THE ONE TO READ, and what it is worth changed in round 1 of
# this step's review. 0.916667 * 0.909091 = 0.833333719697, which rounds UP to
# 0.833334; K3's C-2 was 10/12 * 1 = 0.833333, one unit in the last place away,
# and the scaling by C-1 was the only thing separating two findings the table
# would otherwise confuse -- K3 named two artifacts wrongly, MODPERT published
# eleven. C-1's new third term has since pulled K3's C-2 down to 0.694444, so
# the two now sit 0.138890 apart and telling them apart no longer rests on the
# sixth decimal. The figure is still asserted to six places anyway: the
# coincidence is gone, the arithmetic that produced it is not, and a C-2 that
# stopped scaling by C-1 would read 0.916667 on this row.
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
if [ "${nleft}" = 11 ] && [ ! -f "${gonejar}" ] && [ "${nrep}" = 0 ]; then
   note "eleven jars left, arquillian-testng gone, report.txt untouched"
else
   bad "the jar perturbation did not land as described:" \
       "jars left ${nleft} (want 11), report.txt changed lines ${nrep} (want 0)"
fi

_savedfix=${FIXDIR}
FIXDIR=${modpdir}
run_one MODPERT
FIXDIR=${_savedfix}

factck MODPERT gate.mod.total      11
factck MODPERT gate.mod.declared   11
factck MODPERT gate.mod.derivable  10
factck MODPERT gate.c1.declared    0.916667
factck MODPERT gate.c1.readable    0.909091
factck MODPERT gate.c1.right       0.916667
factck MODPERT C1.modules-named    0.909091
factck MODPERT gate.c2.names       11
factck MODPERT C2.module-names     0.833334
factck MODPERT C3.platform-lookup  1.000000
factck MODPERT familyC             0.923485
# The denominators did NOT follow the tree down. This is the assertion the
# whole perturbation is for: a gate that divided by mod.total would read
# 11/11 and 10/10 and hand a build that withdrew an artifact from publication
# a perfect C-1.
factck MODPERT gate.c3.departed    1.000000
factck MODPERT B14.java11-jakarta  1.000000
# ...and B-12e read eleven jars and found all four package counts intact,
# which is why B-12 is unmoved and why this row is a family-C finding and not
# a two-family one.
factck MODPERT gate.cp.jars        11
factck MODPERT gate.cp.classes     22
factck MODPERT gate.cp.optional    5
factck MODPERT gate.cp.collectors  6
factck MODPERT gate.cp.objects     2
factck MODPERT gate.cp.jtime       2
factck MODPERT B12.java8-library   1.000000

# --- can it fail, three more times: the recorded behaviour, broken -----------
# Two of the nine columns are scaled by what family A still reproduces, and
# they are scaled by DIFFERENT lists: B-14.5 by baseline/httpids.txt's
# eighteen exchange ids, C-3 by baseline/svcids.txt's sixty-nine service
# lookups. Both read the same behavior.summary. No fixture in the table
# exercises either, because every fixture's summary says `behavior.build=ok`
# and nothing else.
#
# Those scalings exist because the source legs above them are all SHAPE --
# records gone, types declared, a call site present -- and a file that deletes
# the method body has every one of them. Family A notices, but eighteen of 935
# transcripts is 0.005775 of the step against B-14's 0.12, and sixty-nine is
# 0.073797 against C-3's 0.3375. Both are trades in favour of faking it, and
# the scalings are what close them. So what this rehearsal has to prove is not
# that they exist but that each is a FRACTION of its own list read at run
# time -- not a boolean, not a halving, and not the other list.
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
# on any breakage would read 0.500000 in BOTH runs. Family C has to stay at
# 1.000000 in both, and B-10 too -- its four nio ids are on a third list -- and
# those two are the assertion that the three provisos read their own lists and
# not each other's.
echo
echo "== can it fail: the golden's recorded exchange, broken"

httpids=$(grep '^[a-z]' /tests/baseline/httpids.txt)
nhttp=$(printf '%s\n' "${httpids}" | grep -c .)
[ "${nhttp}" = 18 ] || bad "baseline/httpids.txt names ${nhttp} ids, this block was written against 18"
svcids=$(grep '^[a-z]' /tests/baseline/svcids.txt)
nsvc=$(printf '%s\n' "${svcids}" | grep -c .)
[ "${nsvc}" = 69 ] || bad "baseline/svcids.txt names ${nsvc} ids, this block was written against 69"
# The three lists are disjoint, which is the precondition every assertion in
# this section rests on. An id that appeared on two of them would make one
# perturbation move two gates and the "unmoved" rows below would be wrong
# rather than reassuring.
for pair in "httpids svcids" "httpids nioids" "svcids nioids"; do
   set -- ${pair}
   n=$(LC_ALL=C comm -12 \
         <(grep '^[a-z]' "/tests/baseline/$1.txt" | LC_ALL=C sort) \
         <(grep '^[a-z]' "/tests/baseline/$2.txt" | LC_ALL=C sort) | grep -c . || true)
   [ "${n}" = 0 ] || bad "baseline/$1.txt and baseline/$2.txt share ${n} id(s)"
done
note "httpids (18), svcids (69) and nioids are three disjoint lists"

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
# ...and family C, and the sixty-nine, and B-10's four. Three lists, three
# provisos, and only one of them fired.
for f in BEHAVALL BEHAVHALF; do
   factck "${f}" gate.c3.exec.broken 0
   factck "${f}" gate.c3.exec        1.000000
   factck "${f}" C3.platform-lookup  1.000000
   factck "${f}" familyC             1.000000
   factck "${f}" gate.src.penalty    1
   factck "${f}" gate.b10.nio.behaviour.broken 0
   factck "${f}" B10.java7-held      1.000000
   factck "${f}" B11.java8lang-held  1.000000
   factck "${f}" B12.java8-library   1.000000
   factck "${f}" B13.java9-10        1.000000
done

# --- the same instrument, pointed at the other list --------------------------
# One of the sixty-nine, broken, and nothing else. C-3 falls to 68/69 =
# 0.985507 and family C to 0.55 + 0.45*0.985507 = 0.993478, which is a value
# no fixture and no other perturbation produces. B-14 stays at 1.000000.
#
# Only ONE of the three SVC runs is here, and the two that are not are not
# missing: M5 breaks all sixty-nine and M6 breaks nine, both statically, both
# in the table above. What the table cannot show is a single id, because a
# fixture that broke one would be indistinguishable from M6 at six decimal
# places of familyC only if 68/69 and 60/69 were the same number -- they are
# not, and the real reason this run exists is that it is built the same way
# BEHAVONE is, out of the same golden, one line appended. If the two lists
# were ever crossed, this run and BEHAVONE would move each other's gate and
# both would still pass their own assertion. The `unmoved` rows are what
# catches that.
echo
echo "== can it fail: one of the golden's sixty-nine service lookups, broken"
svcone=$(printf '%s\n' "${svcids}" | head -1)
cp -R "${FIXDIR}/golden" "${behavdir}/SVCONE"
echo "behavior.differ[0]=${svcone}" >> "${behavdir}/SVCONE/behavior.summary"
nsv=$(grep -cE '^behavior\.(differ|missing)\[' "${behavdir}/SVCONE/behavior.summary" || true)
if [ "${nsv}" = 1 ] && [ -n "${svcone}" ]; then
   note "one broken service-lookup recording: ${svcone}"
else
   bad "the svc perturbation did not land: appended ${nsv} (want 1), id '${svcone}'"
fi
_savedfix=${FIXDIR}
FIXDIR=${behavdir}
run_one SVCONE
FIXDIR=${_savedfix}
factck SVCONE gate.c3.exec.want   69
factck SVCONE gate.c3.exec.broken 1
factck SVCONE gate.c3.exec        0.985507
factck SVCONE gate.c3.departed    1.000000
factck SVCONE gate.c3.arrived     1.000000
factck SVCONE gate.c3.held        1.000000
factck SVCONE C3.platform-lookup  0.985507
factck SVCONE familyC             0.993478
factck SVCONE C1.modules-named    1.000000
factck SVCONE C2.module-names     1.000000
# ...and the other two lists did not notice.
factck SVCONE gate.b14.c5.exec.broken 0
factck SVCONE B14.java11-jakarta      1.000000
factck SVCONE gate.b10.nio.behaviour.broken 0
factck SVCONE B10.java7-held          1.000000

# --- one absence, three consequences -----------------------------------------
# A summary that cannot be read at all is not the same as a summary that reads
# clean, and B-10 settled which way that falls at step 03: a proviso that
# cannot be checked must not be assumed satisfied. The tempting implementation
# is the other one -- leave the shape standing when there is no evidence -- and
# it is tempting because it never fires in a healthy run. It fires when the
# build fails, which is exactly when a tree that never ran would otherwise
# collect B-14.5 and C-3 for a file nobody executed.
#
# ONE FILE, THREE PROVISOS. behavior.summary is a single artefact and B-10,
# B-14.5 and C-3 all read it against three different lists. There is no run in
# which it is absent for one of them and present for another, so there is one
# perturbation here and not three, and it has to assert all three outcomes at
# once. The first draft of this section had two runs -- one that deleted the
# file and checked B-14.5 while reporting family C at 1.000000, and one that
# deleted the same file and checked C-3 while reporting B-14 at 1.000000 --
# and each was a tree the other proved impossible.
#
#   B-10      four nio ids unreadable -> nio_broken = 4 -> its third is halved
#             -> B-10 = 0.4 + 0.3 + 0.3*0.5 = 0.850000
#   B-14.5    0.000000 and the redirect quarter withdrawn with it
#             -> B-14 = (0.07+0.17+0.12)/0.90 = 0.400000
#   C-3       c3exec 0.000000 over an unchanged min() of 1.000000
#             -> familyC = 0.20 + 0.35 = 0.550000
#
# 0.850000 is also K19's B-12 and 0.550000 is also K17's and M4's familyC, and
# neither coincidence matters: no other row in this file reads all three of
# those numbers at once, and the three facts below are what make it a reading.
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
factck GONE gate.c3.exec.want      69
factck GONE gate.c3.exec.broken    69
factck GONE gate.c3.exec           0.000000
factck GONE C3.platform-lookup     0.000000
factck GONE familyC                0.550000
# The shape is untouched in all three. Every source leg the three provisos
# scale is still full, which is what makes this the file-deleted case rather
# than the work-undone one.
factck GONE gate.b14.c5.legacy     0
factck GONE gate.b14.c5.types      3
factck GONE gate.b14.c5.redirects  1
factck GONE gate.c3.departed       1.000000
factck GONE gate.c3.arrived        1.000000
factck GONE gate.c3.held           1.000000
factck GONE C1.modules-named       1.000000
factck GONE C2.module-names        1.000000
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
