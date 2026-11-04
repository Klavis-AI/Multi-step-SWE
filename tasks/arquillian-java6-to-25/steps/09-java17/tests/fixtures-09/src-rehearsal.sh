#!/usr/bin/env bash
# Build-time rehearsal for family C's instrument: arqgate.SourceScan, the javac
# plugin, driven through the shipped source-gate.sh by the shipped srcgate()
# with the shipped classpath expression.
#
# Why this exists, stated plainly. fixtures/c/ holds hand-written SourceScan
# reports and c-rehearsal.sh scores each one, which proves that the arithmetic
# over the counters is the arithmetic that was calibrated. It proves nothing
# whatever about the counters, because a hand-written report says exactly what
# it was written to say. The counters are the half that touches the candidate's
# tree, so leaving them unrehearsed would mean the whole of family C rests on an
# instrument no test in the image has ever run.
#
# So: one real tree, compiled here, by this image's own javac, with the plugin
# attached. Every construct in fixtures/smoke exists for one counter and the
# counts below were MEASURED and then frozen -- not predicted from reading
# SourceScan.java, which is how a rehearsal ends up asserting the bug.
#
# Three things are under test and none of them is a copy:
#
#   * the SRCCP expression is lifted out of /tests/test.sh, so a change to the
#     shelf that shortens the scorer's compile classpath fails here;
#   * srcgate() is lifted out of /tests/test.sh, so the rehearsal drives the
#     function the scorer calls rather than a second one that resembles it;
#   * the three probes are the three the scorer makes -- `srcgate 17 17`,
#     `srcgate 16 16`, `srcgate def ""` -- and the two that compile must AGREE,
#     on the bytes.
#
# WHAT STEP 09 ADDS TO STEP 08'S VERSION OF THIS FILE
# ---------------------------------------------------
# Five record blocks that did not exist below this rung -- ipattern, textblock,
# strnl, sealed, switch -- one that is measured and graded by nothing -- record
# -- a fifth cut, and one structural change to the probes.
#
# THE SECOND PROBE IS A REFUSAL AGAIN, and the two sections below this one
# record why it stopped being one. Step 07 and step 08 asked for library work,
# and a library cannot be refused by a language level; this rung's C-4 asks for
# `sealed`, which was a preview feature in 15 and 16 and standard in 17, so the
# tree can once again say no one level down. The block at the probes asserts
# javac's own wording and counts the declarations it names, because three of
# this rung's four constructs compile at 16 on their own and a refusal for any
# other reason would be a broken smoke tree wearing the expected answer.
#
# WHICH MEANS THE BYTE AGREEMENT MOVES. Step 07 asserted it over its level and
# the level below; here those two do not both compile, so it is asserted over
# this rung's level and the image default -- which is the pair srcgate()'s own
# fallback chooses between, and therefore the pair whose disagreement would
# actually change a score.
#
# AND THE STEP-09 CUT CLOSES THE LOOP FROM THE OTHER SIDE. The whole tree
# refuses 16 and the cut tree -- the same tree without this rung's section --
# compiles at 16 clean, at step 08's own class count, with step 08's own four
# `instanceof` sites still recorded and every other new counter at zero. The
# refusal is C-4's and nothing else's, and the five new blocks say nothing about
# a tree that has not done the work.
#
# THE TWO SECTIONS BELOW ARE HISTORY. They are the notes step 07 and step 06
# wrote about their own versions of this file, kept because the blocks they
# describe are still here and still asserted; where they speak in the present
# tense about the probes, the paragraphs above are what holds now.
#
# WHAT STEP 07 ADDED TO STEP 06'S VERSION OF THIS FILE
# ---------------------------------------------------
# Three record blocks that did not exist below this rung -- ns, toarray, http --
# and one structural change to the probes.
#
# THE SECOND PROBE IS NO LONGER A REFUSAL. Every rung from 03 to 06 had a level
# the tree had to say no to, and the refusal was the fixture: step 06's ok8=no
# was a term in its B-4 and the whole of what "this tree now requires Java 9"
# meant at the source level. Java 11 adds exactly one language construct over
# Java 10 (`var` in a lambda parameter) and nothing step 07 asks for is a
# language feature -- a namespace move, `toArray(T[]::new)` and java.net.http
# are all library. So the level assertion this rung makes is C-1's, read from a
# published class-file header, and the second source probe is a CONTROL whose
# expected answer is yes: an ok10 of no says the candidate reached for a
# construct nothing here asked for.
#
# What replaces the disagreement is an AGREEMENT asserted on the bytes: the
# reports from -source 11, -source 10 and the image default must be
# byte-identical, because every counter family C reads is a property of the
# source text rather than of the level javac was asked for. The -source 8
# refusal has not been thrown away -- it moves onto the step-07 CUT, where it
# still means what step 06 measured it to mean.
#
# TWO NAME-SCOPED COUNTERS, AND THE TREE MEASURES WHAT THAT COSTS. toarray
# matches any one-argument call named toArray and http's call half matches six
# bare method names; neither reads an owner, and neither CAN, because a migrated
# call site has deleted the receiver the owner would have come from. Lib.java
# grows an Arrayish with a toArray(String[]) of its own and a Wire with its own
# disconnect/setUseCaches/followRedirects, all of them counted, so the cost is
# visible and frozen rather than discovered later. What scopes the GATE back to
# the real sites is baseline/toarray.txt's five buckets and baseline/http.txt's
# one file, and these assertions are why that scoping is load bearing.
#
# THE HTTP TYPE CHANNEL IS ASSERTED THROUGH FULLY-QUALIFIED USES ONLY. It
# reaches SourceScan through both visitIdentifier and visitMemberSelect and it
# did not always; measured on this tree with the call in visitIdentifier alone,
# a file holding both legacy types and three java.net.http types written out in
# full reported legacyref=0 and clientref=0. Both halves of C-5 rested on that,
# and the departure half would have gone green for a candidate who replaced an
# import with the long spelling and changed nothing else.
#
# WHAT STEP 06 ADDED TO STEP 05'S VERSION, WHICH IS EVERYTHING BELOW THE CUT
# --------------------------------------------------------------------------
# Seven record blocks that did not exist below this rung -- newinstance,
# accessible, boxedctor, legacycoll, factory, getpackage, var -- and one
# counter family, var.candidates, that exists on every tree including the ones
# with no `var` in them at all.
#
# The relationship to them is the one step 05 established and sharpened. Step
# 04's gates could lean on totals; step 05's could not, because a bucket count
# is satisfiable by moving code, so every record had to carry its path and its
# method. At THIS rung each record also carries a DISCRIMINATING FIELD that the
# total cannot express, and the gate reads the field:
#
#   form=legacy | form=ctor            C-2 pays for the second and charges the
#                                      first, and they are the same method name
#   recv=<Owner>                       C-3's receiver is the RESOLVED OWNER of
#                                      the call, not the static type of the
#                                      variable it was made on
#   kind=view | kind=build             C-5 converts builds freely and holds
#     arg=field | local | call | ...   views over fields in place, because one
#                                      of them re-orders a recorded transcript
#   args=<n>                           the arity that tells Map.of from
#                                      Map.ofEntries
#   ctor=yes | ctor=no                 C-7's strict subset, the same type name
#                                      written twice on one line
#
# So every block below is asserted three ways: as a total, as the specific
# records that make the total mean what the gate reads it as, and as the set of
# values the discriminating field takes across the tree. A counter that lost the
# field would still pass the first; one that hard-wired a single value would
# still pass the first two.
#
# TWO FILES, AND THAT IS NOT DECORATION. Every `.files` counter reads 1 on a
# one-file tree whatever distinctFiles() does, and every gate from step 05 up
# intersects <file>#<method> buckets, so a one-file rehearsal would hold the
# file component of every key constant and could not tell a correct key from one
# that drops the path. Lib.java also carries the near misses that need a NAME
# without an ELEMENT -- a class of one's own called Optional, another called
# Objects, a static method called toList, and at this rung a wrapper called
# Integer, a newInstance() of one's own, a Slot with all three accessible calls
# on it, a Bag with of() and copyOf(), and a Collections that is not java.util's
# -- which is the whole of what "SourceScan resolves through javac's elements"
# buys and cannot be said in the file where the real imports are.
#
# THE FLATTEN CHAIN IS FOUR RUNGS DEEP NOW: the step-07 cut lands on the tree
# the paragraph below calls "this rung's", and everything the paragraph says
# about step 05 and step 04 is then reached from there. Smoke.java is step 05's Smoke.java
# with a section appended and Lib.java is step 05's Lib.java with two top-level
# classes appended, both verbatim, so cutting BOTH files at the step-06 marker
# must reproduce step 05's OWN frozen figures -- about forty of them, measured by
# a different rung's rehearsal on a different day. Cutting Smoke.java at the
# step-05 marker and dropping Lib.java must then reproduce step 04's twenty-six.
# A counter that drifted between any two rungs is a disagreement between two
# measurements rather than two separately plausible numbers.
#
# The step-06 cut is also step 06's ANSWER-CAN-BE-YES control: the tree that
# fails at -source 8 must be the SAME tree that passes once the six `var`
# declarations are removed, compiled by the same probe at the same level.
# Without it `ok8=no` is satisfied by a probe that says no to everything. Step
# 07's scorer makes no -source 8 probe, so both halves of that pair now live on
# this side of the cut, as a control over a frozen tree.
set -euo pipefail

TESTS=${1:-/tests}
SMOKE="${TESTS}/fixtures/smoke"
WORK=$(mktemp -d)
trap 'rm -rf "${WORK}"' EXIT

fail() { echo "src-rehearsal: $*" >&2; exit 1; }

# ---------------------------------------------------------------- the payload
[ -f "${TESTS}/gates/source-gate.sh" ] || fail "source-gate.sh is not staged"
[ -f "${TESTS}/gates/build/arqsource.jar" ] || fail "arqsource.jar was not built"
unzip -l "${TESTS}/gates/build/arqsource.jar" > "${WORK}/jar.txt"
grep -q 'arqgate/SourceScan\.class' "${WORK}/jar.txt" \
   || fail "arqsource.jar does not contain arqgate/SourceScan.class"
# A javac plugin is found through a ServiceLoader entry, not by class name. A
# jar with the class and no registration loads, runs nothing, and writes no
# report -- which source-gate.sh reports as a fact about the tree.
grep -q 'META-INF/services/com.sun.source.util.Plugin' "${WORK}/jar.txt" \
   || fail "arqsource.jar registers no javac plugin service"
# arqgate.Plan is the answer key in machine-readable form: it names every site
# the golden solution edits. It must never reach a step's verifier, and this is
# the one place in the image that would notice if staging ever grew a line.
[ ! -f "${TESTS}/gates/build/arqplan.jar" ] \
   || fail "arqplan.jar is staged into the verifier -- that jar is the answer key"
[ ! -f "${TESTS}/gates/src/arqgate/Plan.java" ] \
   || fail "Plan.java is staged into the verifier -- that source is the answer key"
# arqgate.Rewrite is the golden solution's own editor. It needs --release 17 to
# build and this image is a JDK 11, so a staging line that pulled it in would
# break the image build rather than leak quietly -- but it would leak, and the
# assertion costs one line.
[ ! -f "${TESTS}/gates/src/arqgate/Rewrite.java" ] \
   || fail "Rewrite.java is staged into the verifier -- that source is the answer key"
[ -f "${SMOKE}/mod/src/main/java/smoke/Smoke.java" ] \
   || fail "the smoke tree is not staged at ${SMOKE}"
[ -f "${SMOKE}/mod/src/main/java/smoke/Lib.java" ] \
   || fail "the smoke tree's SECOND file is not staged -- every file-scoped key would read 1"

# ------------------------------------------------- the scorer's own two pieces
#
# Extracted by anchor and asserted non-empty before use. `eval ""` is a
# successful no-op, so an extraction that silently matched nothing would leave
# SRCCP unset and srcgate undefined, and the failure would arrive later wearing
# a different name.
srccp_src=$(sed -n '/^SRCCP=\$(/,/^$/p' "${TESTS}/test.sh")
printf '%s' "${srccp_src}" | grep -qF '/tests/assets/testlib/*.jar' \
   || fail "could not extract the SRCCP expression from test.sh"
gate_src=$(sed -n '/^srcgate() {$/,/^}$/p' "${TESTS}/test.sh")
printf '%s' "${gate_src}" | grep -qF 'source-gate.sh' \
   || fail "could not extract srcgate() from test.sh"
printf '%s' "${gate_src}" | grep -qF 'GATE_SOURCE=' \
   || fail "the extracted srcgate() does not set GATE_SOURCE"

TREE="${SMOKE}"
eval "${srccp_src}"
eval "${gate_src}"

[ -n "${SRCCP}" ] || fail "SRCCP came out empty -- the staged shelf has no jars"
# The scorer's classpath is the staged shelf and all of it. Counted with find
# here and with a glob there, so the two disagree if one of them is wrong.
shelf=$(find "${TESTS}/assets/testlib" "${TESTS}/assets/harness" -name '*.jar' | wc -l)

# The three calls the scorer makes, asserted by spelling. The probes below are
# only the scorer's probes if they are handed the same tags and the same levels:
# a rehearsal that compiled at 10 while test.sh had moved to 11 would be green
# and would be measuring a different instrument.
#
# The RETIRED spellings are asserted absent, and there are two of them now. A
# rehearsal that went on driving a probe the scorer had given up would be
# rehearsing a scorer that no longer exists, and the failure would arrive as a
# green run rather than a red one. `srcgate 8 8` is step 06's and has been gone
# since step 07; `srcgate 11 11` is step 08's and is gone as of this rung. Both
# survive further down as the rehearsal's OWN probes over the cuts, which is a
# different claim about a different tree and is labelled as one.
for call in 'ok17=$(srcgate 17 17)' 'ok16=$(srcgate 16 16)' 'okdef=$(srcgate def "")'; do
   grep -qF "${call}" "${TESTS}/test.sh" \
      || fail "test.sh no longer contains the call this rehearsal drives: ${call}"
done
for gone in 'ok8=$(srcgate 8 8)' 'ok11=$(srcgate 11 11)'; do
   grep -qF "${gone}" "${TESTS}/test.sh" \
      && fail "test.sh still makes a retired probe: ${gone} -- this rung's pair is 17 and 16"
done

# ------------------------------------------- the three probes, on the WHOLE tree
# `|| true` on every srcgate call, and not decoration. srcgate ends in
# `sed ... | head -1`, whose status is 2 when source-gate.sh exited before
# writing the .env -- which is exactly the interesting failure. test.sh runs
# under `set -u` alone (rule 8: it must always reach report()), so there it
# lands as an empty ok and reads as "no". Here, under -e and pipefail, the bare
# assignment would end the rehearsal on the assignment line and never print
# which probe failed.
#
# THE SECOND PROBE IS A REFUSAL AGAIN, which is the one structural difference
# from the two rehearsals below this one. Step 03 through step 06 each had a
# level the tree had to REFUSE, and the refusal was the fixture: ok8=no was a
# term in step 06's B-4 and the whole of what "this tree now requires Java 9"
# meant at the source level. Steps 07 and 08 gave that up and had to -- a
# namespace move, `toArray(T[]::new)`, `java.net.http` and a manifest attribute
# are all library, and a library cannot be refused by a language level -- so
# what those two rungs asserted instead was an AGREEMENT on the bytes between
# the rung's level and the one below it.
#
# This rung's C-4 asks for `sealed`, which was a PREVIEW feature in 15 and 16
# and standard in 17. So the refusal comes back, and it comes back as the same
# statement test.sh publishes as gate.src.ok16: a tree that has done C-4's work
# cannot compile at -source 16 without --enable-preview. The smoke tree below
# has done C-4's work, so ok16 is `no` here, and the ASSERTION IS ON JAVAC'S
# OWN MESSAGE and not only on the exit status. That matters because three of
# this rung's four language constructs would leave the probe at `yes` on their
# own -- text blocks are Java 15, pattern `instanceof` is 16 and arrow switches
# are 14 -- so a refusal for any other reason would be a broken smoke tree
# wearing this rung's expected answer. The step-09 cut further down closes the
# loop from the other side: the same tree without its step-09 section compiles
# at 16 clean.
#
# The agreement has not been thrown away either. It moves onto the pair that
# CAN still make it -- the rung's own level and the image default, which are
# the two the fallback in test.sh chooses between -- and it is asserted on the
# bytes there.
ok17=$(srcgate 17 17 || true)
ok16=$(srcgate 16 16 || true)
okdef=$(srcgate def "" || true)
[ "${ok17}" = yes ] || {
   echo "--- javac at -source 17 ---" >&2; tail -40 "${WORK}/src-17.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at -source 17 (source.ok=${ok17})"
}
[ "${ok16}" = no ] \
   || fail "the smoke tree compiled at -source 16 (source.ok=${ok16}) -- its sealed hierarchies are gone"
# The reason, and not just the refusal. `sealed classes are not supported in
# -source 16` is javac's own wording; each of the two declarations also draws a
# cascading `'{' expected`, because a parser that has rejected `sealed` reads
# the rest of the line as a declaration it cannot finish. Four errors, two of
# them the finding and two of them its shadow, and the count is asserted so
# that a THIRD sealed declaration appearing on the tree shows up here rather
# than silently widening what the refusal is about.
seal_err=$(grep -c 'sealed classes are not supported in -source 16' \
   "${WORK}/src-16.txt.javac.log" 2>/dev/null || true)
[ "${seal_err}" = 2 ] || {
   echo "--- javac at -source 16 ---" >&2; tail -20 "${WORK}/src-16.txt.javac.log" >&2 || :
   fail "the -source 16 refusal names ${seal_err} sealed declaration(s), expected 2 -- the refusal is not C-4's"
}
[ "${okdef}" = yes ] || {
   echo "--- javac at the image default ---" >&2; tail -40 "${WORK}/src-def.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at the image's default level (source.ok=${okdef})"
}

envval() { sed -n "s/^$1=//p" "$2" | head -1; }

eq() { # eq <what> <got> <want> <why>
   if [ "${2}" != "${3}" ]; then
      fail "${1}: got ${2}, expected ${3} -- ${4}"
   fi
   printf '    %-30s %-4s %s\n' "$1" "$2" "$4"
}

echo "--- the -source 17 compile, on the whole tree"
eq 'javac.rc'      "$(envval javac.rc      "${WORK}/src-17.txt.env")" 0  'a clean compile'
eq 'javac.level'   "$(envval javac.level   "${WORK}/src-17.txt.env")" 17 'at this rung, and not at the image default'
eq 'javac.errors'  "$(envval javac.errors  "${WORK}/src-17.txt.env")" 0  'and no errors'
eq 'javac.sources' "$(envval javac.sources "${WORK}/src-17.txt.env")" 2  'two files under */src/main/java'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${WORK}/src-17.txt.env")" "${shelf}" \
   'SRCCP is the whole staged shelf -- 78 testlib jars and the harness'
eq 'javac.classes' "$(envval javac.classes "${WORK}/src-17.txt.env")" 42 \
   'and every nested type was written -- 31 one rung down, plus step 09s eleven'

# The -source 16 refusal wrote no class files and is asserted to have written
# none. A report that came back clean here with javac.classes on it would mean
# the level was not actually applied, and the ok16=no above would be measuring
# something other than the level.
eq 'javac.level (-source 16)' "$(envval javac.level "${WORK}/src-16.txt.env")" 16 \
   'the refused probe did run at the level it was asked for'
eq 'javac.classes (-source 16)' "$(envval javac.classes "${WORK}/src-16.txt.env")" 0 \
   'and wrote nothing: a refusal, not a warning'

# The agreement, on the bytes, against the probe that can still make it.
# Compared as whole files rather than key by key, because naming the keys that
# must match would be naming the keys someone thought of. This is the pair
# test.sh's fallback chooses between -- `measured` is src-17.txt when the tree
# compiles at the rung and src-def.txt when it does not -- so the two reports
# being the same file is the whole of what makes the fallback safe.
eq 'javac.level (default probe)' "$(envval javac.level "${WORK}/src-def.txt.env")" default \
   'GATE_SOURCE empty means no -source flag at all'
cmp -s "${WORK}/src-17.txt" "${WORK}/src-def.txt" \
   || { diff "${WORK}/src-17.txt" "${WORK}/src-def.txt" | head -20 >&2
        fail "the default-level report differs from the -source 17 report -- the fallback reads a different tree"; }
printf '    %-30s %s\n' 'default vs 17' \
   'byte-identical: no counter family C reads depends on the level javac was asked for'

# ============================================= the record blocks, rung by rung
#
# Every block from here to the step-07 cut banner is asserted against the WHOLE
# tree's report, newest rung first: this rung's five, then step 08's one, then
# step 07's three. Everything below the step-07 cut banner is step 06's
# rehearsal driven over the step-07 CUT, which is step 06's own tree -- so the
# figures above that banner are measured on the tree as it stands and the ones
# below it are figures a lower rung froze.
wval() { sed -n "s/^$1=//p" "${WORK}/src-17.txt" | head -1; }
wrecs() { sed -n "s/^$1\[[0-9]*\]=//p" "${WORK}/src-17.txt"; }
whas() { # whas <block> <pattern> <why>
   wrecs "$1" | grep -qF "$2" || fail "$1 has no record matching '$2' -- $3"
   printf '    %-38s %s\n' "$1[..] $2" "$3"
}
wno() { # wno <block> <pattern> <why>
   wrecs "$1" | grep -qF "$2" && fail "$1 records '$2' and must not -- $3"
   printf '    %-38s %s\n' "no $1 $2" "$3"
}
wfieldset() { # wfieldset <block> <field>
   wrecs "$1" \
      | awk -v f="$2=" '{ for (i = 2; i <= NF; i++)
                             if (substr($i, 1, length(f)) == f) print substr($i, length(f) + 1) }' \
      | LC_ALL=C sort -u | paste -sd, -
}
# wnoin <block> <method> <why> -- a refusal about a BUCKET, matched on the whole
# in= field, for the reason noin() gives further down: wno() matches substrings
# and `in=legacy` is a prefix of `in=legacyDocument` and `in=legacyConcat`, so
# the substring form of this refusal would fail on a tree where it holds.
wnoin() {
   local n
   n=$(wrecs "$1" | awk -v m="in=$2" \
      '{ for (i = 2; i <= NF; i++) if ($i == m) { c++; break } } END { print c + 0 }')
   [ "${n}" = 0 ] || fail "$1 records ${n} site(s) in $2() and must not -- $3"
   printf '    %-38s %s\n' "no $1 in=$2" "$3"
}
# winn <block> <method> <n> <why> -- and the positive form, counted per bucket.
winn() {
   local n
   n=$(wrecs "$1" | awk -v m="in=$2" \
      '{ for (i = 2; i <= NF; i++) if ($i == m) { c++; break } } END { print c + 0 }')
   [ "${n}" = "$3" ] || fail "$1 has ${n} site(s) in $2(), expected $3 -- $4"
   printf '    %-38s %-4s %s\n' "$1 in=$2" "${n}" "$4"
}

# =============================================== the step-09 record blocks
#
# Five blocks, and they are the whole of what family C reads at this rung.
# None of the five was rehearsed below this line, and four of the five read
# zero on every tree below it -- which is the shape of instrument that goes
# wrong quietly. ipattern is the exception and the more interesting case: it
# has read 4 on this tree since step 06, because a `instanceof` with a cast
# after it is a shape ordinary Java 6 code is full of, so the step-09 cut
# further down asserts the FOUR it inherits rather than a zero.
#
# THE FIGURES BELOW WERE MEASURED AND THEN FROZEN. Predicting them from reading
# SourceScan.java is how a rehearsal ends up asserting the bug it was written
# to catch, and two of the five have a counter whose value a careful reading
# gets wrong: a text block's `len` is the length of the STRING IT EVALUATES TO
# and not of the source between the delimiters, and an empty `switch (k) {}`
# is counted as colon rather than as neither.

# ---------------------------------------------------------------- ipattern
#
# C-2's channel: every `instanceof` on the tree, with the two readings of the
# rung on one list. `kind=` says whether a pattern variable is bound already and
# `conv=` says whether this site could bind one -- the departure and arrival
# halves of the same edit -- and the list holds both so that neither goes empty
# exactly when the other needs a denominator.
echo "--- ipattern: what a site is, and what it could become"
eq 'ipattern.size'        "$(wval 'ipattern\.size')"        9 'four inherited sites, five written for this rung'
eq 'ipattern.bound'       "$(wval 'ipattern\.bound')"       1 'one site is already a type pattern'
eq 'ipattern.plain'       "$(wval 'ipattern\.plain')"       8 'and size minus bound is the rest, not a second count'
eq 'ipattern.convertible' "$(wval 'ipattern\.convertible')" 5 'four inherited plus the one written to be convertible'
eq 'ipattern.files'       "$(wval 'ipattern\.files')"       2 'in both files'
eq 'ipattern.methods'     "$(wval 'ipattern\.methods')"     8 'eight buckets -- Lib#equals and Smoke#equals are two'
whas ipattern 'kind=plain type=java.lang.String conv=yes in=patternConvertible' \
   'the convertible shape: a plain test and a cast of the same expression below it'
whas ipattern 'kind=plain type=java.lang.Number conv=no in=patternHeld' \
   'and the held shape: a test with nothing to fold into it'
whas ipattern 'kind=bound type=java.lang.CharSequence conv=no in=patternBound' \
   'a site that is already a pattern is kind=bound AND conv=no -- it is the conversion, not a candidate for one'
# The two near misses, and they are the reason conv= is worth having at all. A
# counter that answered "is there a cast to this type further down" rather than
# "a cast of THIS EXPRESSION to this type" would read both of these as
# convertible, and C-2's arrival half would then be asking the candidate to make
# two edits that change what the tree returns.
whas ipattern 'kind=plain type=java.lang.Integer conv=no in=patternOtherVariable' \
   'a cast to the tested type, of a different variable: not convertible'
whas ipattern 'kind=plain type=java.lang.String conv=no in=patternShadowedField' \
   'this.subject tested and the parameter cast: two expressions, and the sharper of the two misses'
eq 'ipattern kind values' "$(wfieldset ipattern kind)" 'bound,plain' \
   'both values on one tree -- a counter that hard-wired kind passes every total above'
eq 'ipattern conv values' "$(wfieldset ipattern conv)" 'no,yes' 'and both values of the other field'

# ------------------------------------------------------- textblock and strnl
#
# C-3's two halves, and they are two BLOCKS because they are two questions. A
# text block arriving is the arrival half; the newline literals leaving the
# method that now holds it is the departure half; and C-3 scores the min of
# them, because a tree that deleted the document satisfies the departure half
# on its own.
#
# THE TWO ARMS OF visitLiteral() ARE `else if` AND THIS IS WHAT SAYS SO. A text
# block holds real newlines. Counted under both blocks it would leave
# strnl.size unmoved by the very edit this rung asks for, and C-3's departure
# half would never move for any candidate.
echo "--- textblock and strnl: one document, two spellings"
eq 'textblock.size'       "$(wval 'textblock\.size')"       3 'two in Smoke.java, one in Lib.java'
eq 'textblock.unreadable' "$(wval 'textblock\.unreadable')" 0 'every unit was readable -- a fact about the run, not the tree'
eq 'textblock.files'      "$(wval 'textblock\.files')"      2 'in both files'
eq 'strnl.size'           "$(wval 'strnl\.size')"           4 'three methods, four literals'
eq 'strnl.concat'         "$(wval 'strnl\.concat')"         2 'both operands of the one concatenation'
eq 'strnl.files'          "$(wval 'strnl\.files')"          2 'in both files'
eq 'strnl.methods'        "$(wval 'strnl\.methods')"        3 'legacy, legacyDocument, legacyConcat'
# THE SAME DOCUMENT, 33 CHARACTERS EITHER WAY. documentTextBlock() and
# legacyDocument() hold the same page: one as a text block and one as an escaped
# literal. The lengths agreeing is the assertion that the two arms are reading
# the same thing and classifying it differently -- which is what C-3 is for --
# rather than one of them measuring the source text and the other the value.
eq 'textblock len values' "$(wfieldset textblock len)" '3,33,4' \
   'the page at 33, the one-liner at 3, and Lib.java Doc#block at 4'
whas strnl 'len=33 cat=no in=legacyDocument' 'the same 33 characters, escaped, counted as a departure'
eq 'strnl cat values' "$(wfieldset strnl cat)" 'no,yes' 'both values -- a concatenated document and a whole one'
winn strnl legacyConcat 2 'two operands, both counted, because either one alone still builds the page by parts'
# The near misses on both sides.
wnoin strnl escapedNotNewline \
   'a backslash and an n is two characters: the counter reads the VALUE, so no record'
wnoin strnl documentTextBlock \
   'and the text block is NOT also a newline literal -- the else-if, which is the whole departure half'
# A TEXT BLOCK CARRIES in= AND THAT IS WHY C-3'S MOVE HALF IS ONE KEY. Both
# sides are keyed `file#method`: the literals have to leave the ONE method that
# held the page, and a block has to arrive in THAT method. Keyed by file, as the
# arrival half read until round 1, an unused block at class scope satisfied it
# beside sixteen deleted literals -- a tree where the document never became a
# text block at all. The plugin emits `in=` LAST on this list for that reason,
# and publishes `textblock.methods` beside `.size` and `.files`.
#
# The names are read off the probe, not guessed: Smoke.java:1232 declares
# documentTextBlock() returning the 33-character page and Smoke.java:1248
# documentTextBlockOneLine() returning the 3-character one, and Lib.java:706
# declares Doc#block() returning the 4-character one. Three blocks, three
# methods, two files -- so the field set is those three names and nothing else.
eq 'textblock in= fields' "$(wfieldset textblock in)" \
   'block,documentTextBlock,documentTextBlockOneLine' \
   'each block named by the method holding it -- the key C-3 joins the arrival on'
eq 'textblock.methods' "$(wval 'textblock\.methods')" 3 \
   'three distinct file#method buckets, which is what C-3 reads the arrival through'
# The other direction: a text block whose value holds no newline at all is still
# a text block. isTextBlock() reads the source through the delimiters, and a
# detector written as "a literal containing a newline" would miss the one-liner
# and pay a candidate nothing for a one-line document -- `textblock len values`
# above reads 3 for it, which is that assertion.
eq 'textblock.files vs strnl.files' \
   "$(wval 'textblock\.files')/$(wval 'strnl\.files')" '2/2' \
   'both blocks reach both files, so neither .files key is the constant 1 a dropped path also gives'

# ------------------------------------------------------------------ sealed
#
# C-4's channel. The record carries a TRIPLE -- file, name and the permits list
# -- because the gate compares triples: a `sealed` modifier whose permits clause
# is a name short does not compile in the real tree, and a gate that asked only
# "is this type sealed" would score that tree with the golden.
echo "--- sealed: three declarations, four shapes, and what is NOT recorded"
eq 'sealed.decl'      "$(wval 'sealed\.decl')"      3 'Signal and Phase in Lib.java, Leg in Smoke.java'
eq 'sealed.nonsealed' "$(wval 'sealed\.nonsealed')" 1 'Stopped, which re-opens what Signal closed'
eq 'sealed.permits'   "$(wval 'sealed\.permits')"   5 'three names on Signal, two on Leg, none on Phase'
eq 'sealed.files'     "$(wval 'sealed\.files')"     2 'in both files'
whas sealed 'name=Signal kind=sealed permits=3 names=Started;Stopped;Paused' \
   'the explicit permits clause, recorded in the order the source writes it'
whas sealed 'name=Stopped kind=nonsealed permits=0 names=' \
   'non-sealed is its own kind and not an absence -- it is the modifier that re-opens a closed hierarchy'
whas sealed 'name=Phase kind=sealed permits=0 names=' \
   'a sealed class with NO permits clause: javac infers the subtypes from the file, and the record reads 0 and empty'
whas sealed 'name=Leg kind=sealed permits=2 names=Near;Far' \
   'a NESTED sealed interface: name= is the simple name, so the key is file plus simple name'
# What produces no record at all, which is three quarters of the hierarchy.
wno sealed 'name=Started' 'final and permitted: neither sealed nor non-sealed, so nothing is written'
wno sealed 'name=Paused'  'a record is implicitly final, and likewise writes nothing'
wno sealed 'name=Near'    'and the nested permitted subtypes are final too'
eq 'sealed kind values' "$(wfieldset sealed kind)" 'nonsealed,sealed' \
   'both kinds on one tree -- a counter that recorded only the modifier it was named for passes the totals'

# ------------------------------------------------------------------ switch
#
# C-5's channel, and the smallest leg of family C. Two fields, both read: `form`
# says arrow or colon and `expr` says whether the switch is an expression, and
# the four corners below take every combination.
echo "--- switch: two fields, four corners, and the empty one"
eq 'switch.size'  "$(wval 'switch\.size')"  6 'five in Smoke.java, one in Lib.java'
eq 'switch.arrow' "$(wval 'switch\.arrow')" 3 'two statements and one expression'
eq 'switch.colon' "$(wval 'switch\.colon')" 3 'one statement, one expression, and the empty one'
eq 'switch.expr'  "$(wval 'switch\.expr')"  2 'one of each form is an expression'
eq 'switch.files' "$(wval 'switch\.files')" 2 'in both files -- C-5 is scored PER FILE, so this key is load bearing'
whas switch 'form=arrow expr=no cases=3'  'arrow statement: what C-5 asks the real trees two switches to become'
whas switch 'form=colon expr=no cases=3'  'colon statement, with the fall-through that makes the conversion a decision'
whas switch 'form=arrow expr=yes cases=3' 'arrow expression'
whas switch 'form=colon expr=yes cases=2' 'and colon expression, with yield -- the fourth corner'
eq 'switch form values' "$(wfieldset switch form)" 'arrow,colon' 'both forms'
eq 'switch expr values' "$(wfieldset switch expr)" 'no,yes'      'and both readings of the other field, independently'
# THE EMPTY SWITCH IS COUNTED AS COLON, on purpose. `switch (k) {}` has no label
# at all and is vacuously both forms; counted as arrow it would make
# switch.arrow rise on a tree nobody had touched, and C-5's per-file test is
# `enough arrows AND no colon left`, so the wrong choice here would hand a file
# half of its leg for containing an empty switch.
whas switch 'form=colon expr=no cases=0' 'the empty switch: vacuously both forms, counted as the legacy one'
# form=mixed is UNREACHABLE FROM ANY TREE THAT COMPILES and is asserted at zero
# rather than exercised. javac rejects a switch that mixes rule cases with
# statement cases, so the arm exists for a report taken from a tree that did not
# compile -- which is a real case here, because source-gate.sh writes a report
# whether or not javac succeeded. A fixture for it would have to be a file that
# fails to build, and that would take the whole smoke tree down with it.
eq 'switch.mixed' "$(wval 'switch\.mixed')" 0 \
   'no tree that compiles can produce one -- the arm is for reports taken from trees that did not'

# ------------------------------------------------------------------ record
#
# MEASURED AND NOT GRADED. Nothing in this ladder asks for a record, and the two
# counters are frozen here so that a rung which later does ask has the figure it
# started from rather than a number it has to take on trust. Paused is also the
# fixture that says a record produces no `sealed` entry: it is implicitly final,
# and a counter that walked a sealed hierarchy's members would report it.
echo "--- record: measured, and graded by nothing"
eq 'record.decl'       "$(wval 'record\.decl')"       1 'Paused, which implements Signal'
eq 'record.components' "$(wval 'record\.components')" 2 'at and ticks, asked of the ELEMENT and not of the member list'
whas record 'name=Paused components=2' 'and the record carries its own name and arity'


# --------------------------------------------------------------------- ns
#
# Two channels and one bucketing rule. The bucketing is the first two segments
# of the RESOLVED qualified name, which is why javax.lang.model gets a bucket of
# its own: a gate that tested `startsWith("javax.")` and stopped there would put
# it with the six this step has an opinion about, and a gate that tested
# `startsWith("javax")` without the dot would also catch class javaxish in
# Lib.java. Both near misses are on this tree.
#
# THE REFERENCE CHANNEL COUNTS USES AND NOT SITES, and the figures below say so
# rather than hiding it. `javax.ejb.EJBException legacy = new javax.ejb.EJBException(..)`
# publishes TWO records on one line -- the declared type and the created type --
# and `javax.naming.Context ctx = new javax.naming.InitialContext()` publishes
# two records naming two different types. C-2 and C-3 both read reference counts
# against a frozen denominator measured the same way, so a counter that
# de-duplicated here would disagree with baseline/nsmove.txt and baseline/nshold.txt
# rather than with an intuition.
echo "--- ns: the namespace channel"
eq 'ns.ref.javax.annotation' "$(wval 'ns\.ref\.javax\.annotation')" 1 'the @Resource that has not moved'
eq 'ns.ref.jakarta.annotation' "$(wval 'ns\.ref\.jakarta\.annotation')" 1 'and the one that has'
eq 'ns.ref.javax.ejb'      "$(wval 'ns\.ref\.javax\.ejb')"      2 'declared type and created type, one line'
eq 'ns.ref.jakarta.ejb'    "$(wval 'ns\.ref\.jakarta\.ejb')"    2 'the same two on the moved side'
eq 'ns.ref.javax.naming'   "$(wval 'ns\.ref\.javax\.naming')"   2 'Context and InitialContext: a JDK package that stays javax'
eq 'ns.ref.javax.management' "$(wval 'ns\.ref\.javax\.management')" 2 'JMX: still javax in Java 25'
eq 'ns.ref.javax.xml'      "$(wval 'ns\.ref\.javax\.xml')"      2 'JAXP: still javax in Java 25'
eq 'ns.ref.javax.lang'     "$(wval 'ns\.ref\.javax\.lang')"     1 'javax and NOT enterprise -- its own bucket'
eq 'ns.files'              "$(wval 'ns\.files')"                1 'Lib.java holds none, which is what makes javaxish a near miss'
whas ns 'pkg=javax.annotation type=javax.annotation.Resource in=-' \
   'a FIELD annotation has no enclosing method and the bucket is `-`'
whas ns 'pkg=jakarta.annotation type=jakarta.annotation.Resource in=-' \
   'both namespaces on one tree, which is the state a half-done move is in'
whas ns 'pkg=javax.lang type=javax.lang.model.element.Element in=notEnterprise' \
   'the qualified name is recorded in full, so the bucket can be checked against it'
wno ns 'javaxish' 'a class whose name begins with javax is not in the javax namespace'
wno ns 'pkg=javax.annotation.Resource' 'the bucket is two segments, never the whole name'
eq 'ns pkg= values' "$(wfieldset ns pkg)" \
   'jakarta.annotation,jakarta.ejb,javax.annotation,javax.ejb,javax.lang,javax.management,javax.naming,javax.xml' \
   'eight buckets over two namespaces -- a counter that hard-wired the field fails here'

# ---------------------------------------------------------------- toarray
#
# form= is the discriminating field and the arrival half is a MODE rather than
# a name: javac calls a constructor reference `<init>`, so the obvious name test
# reports 0 on a tree holding eight of them, and 0 is also the answer the
# unmigrated tree gives. That is the failure this block exists to catch, and it
# is the one that cost a day at authoring time (NOTES 12.12).
#
# THE COUNTER IS NAME-SCOPED AND THIS TREE MEASURES WHAT THAT COSTS. Arrayish
# in Lib.java declares a static `toArray(String[])` of its own, calls it, and
# the call is counted: three sized records, not two. Scoping the counter by
# receiver type is not available -- a migrated call site has no collection
# receiver to read -- so what scopes the GATE is baseline/toarray.txt's five
# named buckets, and this is the measurement that says the scoping is load
# bearing rather than decorative.
echo "--- toarray: the T[]::new channel"
eq 'toarray.sized'   "$(wval 'toarray\.sized')"   3 'two real call sites plus the name-scoped near miss'
eq 'toarray.ctorref' "$(wval 'toarray\.ctorref')" 2 'String[]::new and the raw one'
eq 'toarray.raw'     "$(wval 'toarray\.raw')"     1 'Optional[]::new -- a raw array no `new` expression appears in'
eq 'toarray.files'   "$(wval 'toarray\.files')"   2 'both files, so the file component of the key is not constant'
whas toarray 'form=sized elem=String in=sizedArrays' 'the pre-11 spelling, element as written'
whas toarray 'form=ctorref elem=String[] in=ctorRefArrays' 'the arrival spelling, qualifier as written'
whas toarray 'form=ctorref elem=Optional[] in=ctorRefArrays' 'and the raw one, recorded the same way'
whas toarray 'Lib.java' 'the near miss IS counted, and saying so is the point'
wno toarray 'in=notToArray' 'a no-argument toArray() is not a one-argument call, and Arrays.copyOf is not a toArray'
eq 'toarray form= values' "$(wfieldset toarray form)" 'ctorref,sized' \
   'both modes on one tree'

# ------------------------------------------------------------------- http
#
# Four forms over two channels. The TYPE channel reaches the scanner through
# both visitIdentifier and visitMemberSelect, and it did not always: measured on
# this tree with the call in visitIdentifier alone, a file holding
# java.net.URLConnection, java.net.HttpURLConnection and three java.net.http
# types written out IN FULL reported http.legacyref=0 and http.clientref=0,
# because a fully-qualified use is a member select and never an identifier. Both
# halves of C-5 rested on that: the departure half would have gone green for a
# candidate who only replaced an import with the long spelling. Every reference
# in Smoke.java's step-07 section is written out in full for exactly that
# reason, so this block is red if the second visitor is ever lost again.
#
# The CALL channel is name-scoped, like toarray's, and Wire in Lib.java is the
# measurement of what that costs: a disconnect() and a setUseCaches() on a
# receiver that is not a connection of any kind are both counted. C-5 is scoped
# to one FILE by baseline/http.txt, which is the answer to that.
echo "--- http: the java.net.http channel"
eq 'http.legacyref'  "$(wval 'http\.legacyref')"  4 'URLConnection once, HttpURLConnection three times, all fully qualified'
eq 'http.legacycall' "$(wval 'http\.legacycall')" 8 'all six names in legacyHttp plus Wires two'
eq 'http.clientref'  "$(wval 'http\.clientref')"  9 'the three types and their two nested ones, counted as uses'
eq 'http.redirects'  "$(wval 'http\.redirects')"  2 'the real one and Wires own'
eq 'http.files'      "$(wval 'http\.files')"      2 'both files'
whas http 'form=legacytype name=java.net.URLConnection in=legacyHttp' \
   'the fully-qualified legacy type, which only the member-select visitor sees'
whas http 'form=legacytype name=java.net.HttpURLConnection in=legacyHttp' 'and the other one'
whas http 'form=clienttype name=java.net.http.HttpClient in=modernHttp' 'the arrival type, same channel'
whas http 'form=clienttype name=java.net.http.HttpResponse.BodyHandlers in=modernHttp' \
   'a NESTED arrival type resolves to its own name -- which is why C-5 truncates at the fourth segment'
whas http 'form=redirects name=followRedirects in=modernHttp' \
   'the policy call, its own form because it has no pre-11 counterpart'
whas http 'form=legacycall name=disconnect in=use' \
   'Wires disconnect IS counted -- the call half reads a bare name and cannot read an owner'
eq 'http form= values' "$(wfieldset http form)" 'clienttype,legacycall,legacytype,redirects' \
   'all four forms on one tree'

# C-5's arrival arithmetic, lifted from test.sh by anchor and driven over this
# tree's records. Five distinct names in, three buckets out: the truncation is
# what stops the denominator from being a spelling.
c5arr=$(wrecs http \
   | awk '$2 == "form=clienttype" {
             for (i = 2; i <= NF; i++)
                if (substr($i, 1, 5) == "name=") {
                   n = substr($i, 6)
                   split(n, q, ".")
                   if (q[1] == "java" && q[2] == "net" && q[3] == "http" && q[4] != "")
                      n = q[1] "." q[2] "." q[3] "." q[4]
                   print n
                } }' | LC_ALL=C sort -u | grep -c . || true)
eq 'C-5 arrival buckets' "${c5arr}" 3 'HttpClient, HttpRequest, HttpResponse -- Redirect and BodyHandlers fold in'
grep -qF 'if (q[1] == "java" && q[2] == "net" && q[3] == "http" && q[4] != "")' "${TESTS}/test.sh" \
   || fail "test.sh no longer truncates C-5's arrival names at the fourth segment"


# --------------------------------------------------------------------- svc
#
# The channel B-15.3 is built from. It was step 08's C-3 and its must-flip; at
# this rung it is inherited, weighted 0.45 of B-15's 0.80, and the block below
# is asserted unchanged -- because an inherited gate that quietly stopped being
# readable is the ninth trap, and the only way to see it is to keep driving the
# rung-below instrument on the rung-above tree.
#
# ITS BUCKET NAMES MOVED AND ITS RECORDS DID NOT. test.sh now buckets these
# three forms as b15legacyscan, b15loadclass and b15arrive rather than the svc*
# names step 08 gave them, so the anchors below are re-keyed and the records are
# step 08's own. A block that had kept the old anchors would have gone on
# asserting a scorer that no longer exists.
#
# TWO CHANNELS, SCOPED DIFFERENTLY, AND THE DIFFERENCE IS THE WHOLE DESIGN.
# countService() reads a method invocation and is scoped by the RESOLVED OWNER
# of the method: java.lang.ClassLoader plus getResources or getResource is a
# legacyscan, java.lang.ClassLoader plus loadClass is a loadclass, and
# java.util.ServiceLoader plus load is a platformload. countServiceType() reads
# an identifier or a member select and is scoped by the RESOLVED QUALIFIED
# NAME, matched whole: exactly java.util.ServiceLoader, or exactly
# java.util.ServiceLoader.Provider.
#
# That is a departure from both channels above it. toarray's call half and
# http's call half are NAME-scoped -- they read a bare method name and cannot
# read an owner, which is why Wire.disconnect() is counted as a legacycall and
# why C-5 is scoped to one file by baseline/http.txt to pay for it. Here that
# trade is not available, and the reason is specific rather than tasteful: THE
# CLASS THIS STEP DELETES IS ITSELF CALLED ServiceLoader. A name-scoped call
# channel would read the project's own ServiceLoader.load(...) -- the
# hand-rolled one, in the tree as it arrives -- as the platform arrival, and
# C-3's arrival half would be green on a tree that had changed nothing at all.
# class ServiceLoader in Lib.java is that measurement, and it is why the
# arrival half can be trusted before any of the rest of C-3 is.
#
# THREE NEAR MISSES, ALL OF THEM IN SvcUser#mine(). Loader is a class with a
# loadClass, a getResources and a getResource on it and no relation to
# java.lang.ClassLoader; smoke.ServiceLoader has a static load(Class). Four
# calls between them, every one of them spelled exactly like a site this gate
# hunts, and the block records none of them. SvcUser#real() is the control
# beside it: one loadClass on a receiver that IS a ClassLoader, counted -- and
# it is the whole of why svc.files reads 2 rather than 1.
echo "--- svc: the service-lookup channel, which is what C-3 reads"
eq 'svc.legacyscan'   "$(wval 'svc\.legacyscan')"   2 'getResources and getResource, both on a real ClassLoader'
eq 'svc.loadclass'    "$(wval 'svc\.loadclass')"    2 'one in Smoke.java, one in Lib.javas SvcUser#real'
eq 'svc.platformload' "$(wval 'svc\.platformload')" 2 'two java.util.ServiceLoader.load calls'
eq 'svc.platformtype' "$(wval 'svc\.platformtype')" 6 'the type channel, which fires more often than the call channel'
eq 'svc.providertype' "$(wval 'svc\.providertype')" 2 'ServiceLoader.Provider, twice'
eq 'svc.files'        "$(wval 'svc\.files')"        2 'both files -- SvcUser#real is what puts Lib.java in'
whas svcload 'form=legacyscan name=getResources in=legacyServiceScan' \
   'the plural scan, the one a hand-rolled provider reader is built on'
whas svcload 'form=legacyscan name=getResource in=legacyServiceScan' \
   'and the singular, which is the same question asked once'
whas svcload 'form=loadclass name=loadClass in=legacyServiceScan' \
   'the reflective load that follows it'
whas svcload 'mod/src/main/java/smoke/Lib.java:' \
   'Lib.java carries a record of its own, so the path component is not a constant'
whas svcload 'form=loadclass name=loadClass in=real' \
   'SvcUser#real: a real ClassLoader receiver in the file whose other four calls are near misses'
whas svcload 'form=platformload name=load in=platformServiceLoad' \
   'the arrival, and the only form C-3 counts as one'
whas svcload 'form=platformtype name=java.util.ServiceLoader in=providerTypes' \
   'the type channel, matched on the whole qualified name'
whas svcload 'form=providertype name=java.util.ServiceLoader.Provider in=providerTypes' \
   'and the nested one, which is a different name and not a prefix match'
wno svcload 'in=mine' \
   'SvcUser#mine holds four calls spelled like the four this gate hunts, on owners that are not'
eq 'svcload form= values' "$(wfieldset svcload form)" \
   'legacyscan,loadclass,platformload,platformtype,providertype' 'all five forms on one tree'
# The in= field, asserted as a whole set rather than as absences. wno() matches
# substrings, so `in=mine` would also be satisfied by a tree on which the method
# had been renamed to something mine is not a prefix of; the set says which four
# methods produce records and therefore that the other twenty-odd do not.
eq 'svcload in= values' "$(wfieldset svcload in)" \
   'legacyServiceScan,platformServiceLoad,providerTypes,real' \
   'four methods produce records and no other method on the tree does'

# TWO platformtype RECORDS LAND ON ONE LINE, twice, and that is the type
# channel doing what it was asked to rather than a double count to be fixed.
# `java.util.ServiceLoader<Runnable> sl = java.util.ServiceLoader.load(...)`
# writes the qualified name twice -- once as the declared type and once as the
# call's qualifier -- and countServiceType is called from visitMemberSelect,
# which sees both. The Provider lines are the same shape one level in:
# java.util.ServiceLoader.Provider CONTAINS java.util.ServiceLoader as its own
# member-select subtree, so each of them scores a providertype and a
# platformtype together.
#
# Counted by SHAPE rather than by line number, because a line number in this
# file is a claim about how far down Smoke.java a section happens to start and
# every block above this one has already been moved once.
ptwice=$(wrecs svcload | awk '{ for (i = 2; i <= NF; i++) if ($i == "form=platformtype") c[$1]++ }
                              END { for (l in c) if (c[l] == 2) n++; print n + 0 }')
eq 'lines with two platformtype' "${ptwice}" 2 \
   'the two ServiceLoader.load declarations: declared type and call qualifier, both member selects'
pwith=$(wrecs svcload | awk '{ for (i = 2; i <= NF; i++) {
                                  if ($i == "form=providertype") p[$1]++
                                  if ($i == "form=platformtype")  t[$1]++ } }
                             END { for (l in p) if (t[l] == p[l]) n++; print n + 0 }')
eq 'Provider lines with a platformtype' "${pwith}" 2 \
   'java.util.ServiceLoader.Provider contains java.util.ServiceLoader, so both visitors fire on it'

# WHAT C-3 ACTUALLY BUCKETS, anchored on test.sh rather than described. Three
# fbuckets calls over this block and no more: legacyscan and loadclass are
# unioned into one departure bucket because they are two spellings of one
# question, and platformload alone is the arrival. platformtype and
# providertype are MEASURED AND NOT SCORED -- they are here so that a future
# rung that wants to score "declared a ServiceLoader but never called load"
# finds the channel already rehearsed, and so that this block is red if the
# type visitor is lost the way http's nearly was.
for b in 'fbuckets svcload b15legacyscan form=legacyscan' \
         'fbuckets svcload b15loadclass  form=loadclass' \
         'fbuckets svcload b15arrive form=platformload'; do
   grep -qF "${b}" "${TESTS}/test.sh" \
      || fail "test.sh no longer buckets svcload this way: ${b}"
done
nbuk=$(grep -c 'fbuckets svcload' "${TESTS}/test.sh" || true)
eq 'fbuckets svcload calls' "${nbuk}" 3 'three buckets, so no form is scored twice and none is scored by accident'
for f in platformtype providertype; do
   grep -qF "fbuckets svcload b15${f}" "${TESTS}/test.sh" \
      && fail "test.sh now buckets form=${f} -- this block measures it and does not score it"
done
printf '    %-38s %s\n' 'platformtype, providertype' 'measured here, bucketed nowhere in test.sh'
# ====================================================== the step-09 cut: step 08s tree
#
# The five cuts below run newest first -- 09, then 07, 06, 05, 04 -- and each
# one drives a LOWER rung's rehearsal over a tree this file's section markers
# cut back to that rung's own. This first one is the youngest and the only one
# whose figures were measured by this rehearsal rather than inherited: the
# step-08 tree is what the step-09 smoke tree was before this rung's section was
# appended to it, and the control it provides is the one the -source 16 refusal
# above cannot provide on its own.
#
# WHAT THIS CUT IS FOR, IN ONE COMPARISON. The whole tree refuses -source 16 and
# the block above asserts that the refusal names two sealed declarations. That
# says the refusal is C-4's. It does NOT say that the rest of the tree is happy
# at 16 -- a second, unrelated 17-ism somewhere in the step-09 section would
# leave the sealed count at 2 and still be a tree that this rung had quietly
# made un-compilable one level down. So the cut tree is compiled AT 16, and it
# has to come back clean. Taken together the two probes say: the step-09 section
# is the only thing on this tree that needs 17, and within it the sealed
# hierarchies are the only thing that needs 17.
# Cut at a section marker and re-compile. Rewriting each construct would be
# twenty fragile substitutions; the marker is one sed address and the result is
# a tree a lower rung already measured.
#
# The marker is matched with leading whitespace optional, because Smoke.java's
# sections are members of a class and Lib.java's are top-level types. That is
# also why only Smoke.java gets a closing brace back.
flatten() { # flatten <marker> <dir> <keep-lib: yes|no>
   local marker=$1 dir=$2 keeplib=$3 cut whole
   mkdir -p "${dir}/mod/src/main/java/smoke"
   sed "/^ *\/\/ -* step ${marker}\$/,\$d" \
      "${SMOKE}/mod/src/main/java/smoke/Smoke.java" \
      > "${dir}/mod/src/main/java/smoke/Smoke.java"
   echo '}' >> "${dir}/mod/src/main/java/smoke/Smoke.java"
   # The cut has to have removed something. A marker that no longer matches
   # would leave the file whole and the failure would read as "the control does
   # not work" rather than "the sed address is stale".
   cut=$(grep -c . "${dir}/mod/src/main/java/smoke/Smoke.java")
   whole=$(grep -c . "${SMOKE}/mod/src/main/java/smoke/Smoke.java")
   [ "${cut}" -lt "${whole}" ] \
      || fail "the step-${marker} section marker did not match Smoke.java -- nothing was cut"
   if [ "${keeplib}" = yes ]; then
      sed "/^ *\/\/ -* step ${marker}\$/,\$d" \
         "${SMOKE}/mod/src/main/java/smoke/Lib.java" \
         > "${dir}/mod/src/main/java/smoke/Lib.java"
      cut=$(grep -c . "${dir}/mod/src/main/java/smoke/Lib.java")
      whole=$(grep -c . "${SMOKE}/mod/src/main/java/smoke/Lib.java")
      [ "${cut}" -lt "${whole}" ] \
         || fail "the step-${marker} section marker did not match Lib.java -- nothing was cut"
   fi
}

echo "--- the step-09 cut: step 08's own smoke tree, compiled one level down"
flatten 09 "${WORK}/f09" yes
TREE="${WORK}/f09"
c09=$(srcgate c09 16 || true)
[ "${c09}" = yes ] || {
   echo "--- javac at -source 16 on the step-09 cut ---" >&2
   tail -40 "${WORK}/src-c09.txt.javac.log" >&2 || :
   fail "step 08's own smoke tree does not compile at -source 16 (source.ok=${c09}) -- something other than C-4 needs 17"
}
# A DISTINCT TAG, so this probe writes src-c09.txt and not src-16.txt. The
# whole-tree -source 16 log is the evidence for the sealed-error count asserted
# forty lines up, and srcgate names its output after the TAG: re-probing with
# `srcgate 16 16` here would overwrite that log with a clean one, and the
# assertion above would still pass because it had already run. A rehearsal that
# destroys its own evidence on the way past is a rehearsal nobody can debug.
env09="${WORK}/src-c09.txt.env"
rep09="${WORK}/src-c09.txt"
eq 'javac.level (step-09 cut)'  "$(envval javac.level  "${env09}")" 16 'one level below this rung'
eq 'javac.errors (step-09 cut)' "$(envval javac.errors "${env09}")" 0 \
   'clean -- the four errors the whole tree draws at 16 are all in the cut section'
eq 'javac.classes (step-09 cut)' "$(envval javac.classes "${env09}")" 31 \
   'the 42 above minus this rungs eleven, which is step 08s own count'

c09val() { sed -n "s/^$1=//p" "${rep09}" | head -1; }
c09recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep09}"; }

# ---- the four this rung INHERITED, and they are not zero -------------------
#
# ipattern is the one family-C counter with a population on the tree below it,
# and that is not an accident of the fixture: `x instanceof T` followed by a
# cast to T is the shape Java 6 code is made of, so the step-08 tree has four
# of them and every one is convertible. The other four blocks read zero here.
#
# ASSERTING THE FOUR RATHER THAN A ZERO IS THE STRONGER CLAIM. A zero would be
# satisfied by a visitor that had stopped running; four sites, in two files, in
# three methods, with conv=yes on all four, is only satisfied by a visitor that
# is running and is reading the same thing it read on the whole tree.
echo "--- the step-09 cut: what family C reads on step 08's tree"
eq 'ipattern.size (cut)'        "$(c09val 'ipattern\.size')"        4 \
   'the four incidental sites step 06 and step 07 left behind'
eq 'ipattern.bound (cut)'       "$(c09val 'ipattern\.bound')"       0 'none of them is a pattern yet'
eq 'ipattern.plain (cut)'       "$(c09val 'ipattern\.plain')"       4 'all four are the plain form'
eq 'ipattern.convertible (cut)' "$(c09val 'ipattern\.convertible')" 4 \
   'and all four have a cast of the same expression after them -- which is why C-2 is worth scoring'
eq 'ipattern.files (cut)'       "$(c09val 'ipattern\.files')"       2 'in both files'
eq 'ipattern.methods (cut)'     "$(c09val 'ipattern\.methods')"     3 'two equals() and legacyHttp()'
for want in 'conv=yes in=equals' 'conv=yes in=legacyHttp'; do
   c09recs ipattern | grep -qF "${want}" \
      || fail "the step-09 cut lost an inherited ipattern site: ${want}"
done
printf '    %-38s %s\n' 'the four inherited sites' 'still recorded, and still convertible'

# ---- and the four that arrive whole at this rung --------------------------
#
# Every scalar of the other four blocks, asserted at zero on a tree that
# compiles. This is the second half of trap 6's instruction: the block above
# says the counters run, and this one says they say NOTHING about a tree that
# has not done the work. A visitor that counted, say, every string literal as a
# newline literal would pass every assertion in the whole-tree section -- the
# figures there are frozen from a measurement, so they would have been frozen
# wrong -- and would fail here, where the answer has to be nothing at all.
for k in textblock.size textblock.unreadable textblock.files textblock.methods \
         strnl.size strnl.concat strnl.files strnl.methods \
         sealed.decl sealed.nonsealed sealed.permits sealed.files \
         switch.size switch.arrow switch.colon switch.expr switch.mixed switch.files \
         record.decl record.components; do
   got=$(c09val "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got}" = 0 ] \
      || fail "the step-09 cut reports ${k}=${got}, expected 0 -- that counter has a population below this rung"
done
printf '    %-38s %s\n' 'the other four blocks, 20 keys' 'all 0 on step 08s tree'
# THE KEYS ARE PRESENT AND READ 0, which is a different statement from being
# absent, and the loop above cannot tell the two apart: c09val returns the empty
# string for a key that is not in the report, and `[ "" = 0 ]` is false, so an
# absent key already fails the loop. Said once here rather than nineteen times
# in the messages.
for k in textblock.size strnl.size sealed.decl switch.size record.decl; do
   grep -q "^${k}=" "${rep09}" \
      || fail "the step-09 cut report has no ${k}= line at all -- the block was not emitted"
done
printf '    %-38s %s\n' 'and the five block headers' 'emitted on a tree with nothing to report'
# No records either, which the scalars above imply and do not assert: a report
# whose scalar said 0 beside a record list that was not empty would be a scorer
# reading two different populations, and three of family C's five legs read the
# RECORDS and not the scalar.
for b in textblock strnl sealed switch record; do
   n=$(c09recs "${b}" | wc -l)
   [ "${n}" = 0 ] || fail "the step-09 cut has ${n} ${b} record(s) and its scalar says 0"
done
printf '    %-38s %s\n' 'no textblock/strnl/sealed/switch/record' 'the record lists are empty, not just the counts'

TREE="${SMOKE}"

# ================================================== the step-07 cut: step 06s tree
#
# Everything below this line is step 06's rehearsal, unchanged, driven over the
# tree this file's step-07 marker cuts away. That tree IS step 06's smoke tree,
# so every figure below was measured by a different rung's rehearsal on a
# different day and is asserted here rather than re-derived -- a counter that
# drifted between the two rungs is a disagreement between two measurements
# rather than two separately plausible numbers.
#
# It also re-arms the -source 8 refusal that step 06's B-4 rested on. This rung
# does not make that probe, and the cut tree is where it still means what it
# meant: six errors, all `class var`, on the six lines the var block records.
# flatten() itself now lives above the step-09 cut, which is the first of the
# five that spends it.

echo "--- the step-07 cut: step 06's own smoke tree, and step 06's own figures"
flatten 07 "${WORK}/f07" yes
TREE="${WORK}/f07"
ok10=$(srcgate 10 10 || true)
ok8=$(srcgate 8 8 || true)
[ "${ok10}" = yes ] || {
   echo "--- javac at -source 10 on the step-07 cut ---" >&2
   tail -40 "${WORK}/src-10.txt.javac.log" >&2 || :
   fail "step 06's own smoke tree does not compile at -source 10 (source.ok=${ok10})"
}
[ "${ok8}" = no ] \
   || fail "step 06's own smoke tree compiled at -source 8 (source.ok=${ok8}) -- its six var declarations are gone"

env10="${WORK}/src-10.txt.env"
rep10="${WORK}/src-10.txt"
env8="${WORK}/src-8.txt.env"

envval() { sed -n "s/^$1=//p" "$2" | head -1; }
repval() { sed -n "s/^$1=//p" "${rep10}" | head -1; }

eq() { # eq <what> <got> <want> <why>
   if [ "${2}" != "${3}" ]; then
      fail "${1}: got ${2}, expected ${3} -- ${4}"
   fi
   printf '    %-30s %-4s %s\n' "$1" "$2" "$4"
}

# ...and every counter step 07 and step 08 introduced goes to zero on this
# tree. The step-06 rehearsal below was carried forward unchanged and says
# nothing about these sixteen keys; this is the one place that states what the
# cut is supposed to have removed, in the form the f06 and f05 sweeps further
# down already use. It is not decoration: the step-08 section of Lib.java
# declares a class named ServiceLoader, and a marker that stopped matching
# Lib.java would leave it standing in what is meant to be step 06's tree.
for k in ns.files toarray.sized toarray.ctorref toarray.raw toarray.files \
         http.legacyref http.legacycall http.clientref http.redirects http.files \
         svc.legacyscan svc.loadclass svc.platformload svc.platformtype \
         svc.providertype svc.files; do
   got=$(repval "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got:-0}" = 0 ] || fail "flattened ${k} is ${got}, not 0 -- the step-07 cut did not remove it"
done
printf '    %-30s %s\n' 'f07 step-07/08 counters' 'all 16 keys read 0'
# ns's per-bucket keys are written only for buckets that were seen at all, so
# they are asserted as an absence rather than as a zero -- ns.ref.javax.ejb is
# MISSING here and was 2 on the whole tree.
if grep -q '^ns\.ref\.' "${rep10}"; then
   fail "the step-07 cut still writes an ns.ref.* bucket -- the namespace section survived the marker"
fi
printf '    %-30s %s\n' 'f07 ns.ref.* buckets' 'none written at all, which is not the same as zero'

recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep10}"; }
hasrec() { # hasrec <block> <pattern> <why>
   recs "$1" | grep -qF "$2" || fail "$1 has no record matching '$2' -- $3"
   printf '    %-30s %s\n' "$1[..] $2" "$3"
}
norec() { # norec <block> <pattern> <why>
   recs "$1" | grep -qF "$2" && fail "$1 records '$2' and must not -- $3"
   printf '    %-30s %s\n' "no $1 $2" "$3"
}
# noin <block> <method> <why> -- the same refusal, but on the WHOLE in= field.
#
# norec() matches substrings, and `in=collect` is a prefix of `in=collected`:
# written that way, the assertion that a method merely NAMED collect produces no
# record would fail on a tree where it produces none, because five other methods
# whose names start with the same eight letters produce seven. Every refusal
# about a bucket goes through here instead, matched field-for-field.
noin() {
   local n
   n=$(recs "$1" | awk -v m="in=$2" \
      '{ for (i = 2; i <= NF; i++) if ($i == m) { c++; break } } END { print c + 0 }')
   [ "${n}" = 0 ] || fail "$1 records ${n} site(s) in $2() and must not -- $3"
   printf '    %-30s %s\n' "no $1 in=$2" "$3"
}
# inn <block> <method> <n> <why> -- and the positive form, counted per bucket.
inn() {
   local n
   n=$(recs "$1" | awk -v m="in=$2" \
      '{ for (i = 2; i <= NF; i++) if ($i == m) { c++; break } } END { print c + 0 }')
   [ "${n}" = "$3" ] || fail "$1 has ${n} site(s) in $2(), expected $3 -- $4"
   printf '    %-30s %-4s %s\n' "$1 in=$2" "${n}" "$4"
}
# The set of values a discriminating field takes across the whole tree, as one
# comma-separated string. A counter that hard-wired the field passes every total
# and every per-record assertion above and fails here.
fieldset() { # fieldset <block> <field>
   recs "$1" \
      | awk -v f="$2=" '{ for (i = 2; i <= NF; i++)
                             if (substr($i, 1, length(f)) == f) print substr($i, length(f) + 1) }' \
      | LC_ALL=C sort -u | paste -sd, -
}

echo "--- the -source 10 compile"
eq 'javac.rc'      "$(envval javac.rc      "${env10}")" 0   'a clean compile'
eq 'javac.level'   "$(envval javac.level   "${env10}")" 10  'at this rung, and not at the image default'
eq 'javac.errors'  "$(envval javac.errors  "${env10}")" 0   'and no errors'
eq 'javac.sources' "$(envval javac.sources "${env10}")" 2   'two files under */src/main/java'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${env10}")" "${shelf}" \
   'SRCCP is the whole staged shelf'
eq 'javac.classes' "$(envval javac.classes "${env10}")" 25  'and every nested type was written'
eq 'files'         "$(repval files)"         2 'the plugin analysed every file it was handed'

# The default-probe comparison that stood here belongs to the whole tree and has
# moved up to the step-07 section, where src-def.txt is a report over the same
# sources it is being compared against. Running it here would compare the cut
# tree's report with the whole tree's and fail for a reason that has nothing to
# do with the level.

# =========================================================== C-2: newInstance
#
# Departure: `Class.newInstance()`, which Java 9 deprecated because it
# propagates the constructor's checked exceptions without declaring them.
# Arrival: `getDeclaredConstructor().newInstance()`, which wraps them in an
# InvocationTargetException. The two are told apart by the RESOLVED OWNER --
# java.lang.Class against java.lang.reflect.Constructor -- and the method name
# is the same word in both, which is the whole reason form= exists.
echo "--- C-2: the two newInstance forms, which share a name and not an owner"
eq 'newinstance.size'    "$(repval 'newinstance\.size')"    5 'two legacy, three ctor'
eq 'newinstance.legacy'  "$(repval 'newinstance\.legacy')"  2 'Class.newInstance, in both files'
eq 'newinstance.ctor'    "$(repval 'newinstance\.ctor')"    3 'Constructor.newInstance'
eq 'newinstance.methods' "$(repval 'newinstance\.methods')" 3 'in three buckets'
eq 'newinstance.files'   "$(repval 'newinstance\.files')"   2 'across both files, not one'
eq 'newinstance form values' "$(fieldset newinstance form)" 'ctor,legacy' \
   'the field takes both its values on this tree'
hasrec newinstance 'form=legacy in=legacyInstance' 'the deprecated call'
hasrec newinstance 'form=ctor in=ctorInstance'     'and the replacement'
# Two ctor records in ONE bucket, which is the arity that cannot be produced by
# deleting text: `getDeclaredConstructor(String.class).newInstance("x")` is the
# shape a candidate has to write out, and a gate counting buckets rather than
# records would not notice that the second one was never written.
inn newinstance ctorInstance 2 'both arities, in one bucket'
hasrec newinstance 'form=legacy in=made' 'the second file has both forms too'
hasrec newinstance 'form=ctor in=made'   'so newinstance.files is measured, not 1'
# Impostor06.Handle declares newInstance() in both arities. Same word, an owner
# that is neither Class nor Constructor, and no record at all.
noin newinstance impostors06 'a newInstance() of ones own is not java.lang.Class'

# =========================================================== C-3: setAccessible
#
# recv= is the qualified owner of the RESOLVED method, and this block is the
# only place in the image where the difference from "the static type of the
# receiver" is visible. isAccessible() and canAccess() are declared on
# AccessibleObject alone, so a Field receiver still reports AccessibleObject;
# setAccessible IS overridden by Field, Method and Constructor, so the same
# Field variable reports Field one line later. Six records, four values of
# recv=, three receivers -- and a gate that read the declared type of the
# variable would produce a different six.
echo "--- C-3: all three accessible calls, and what recv= actually names"
eq 'accessible.size'          "$(repval 'accessible\.size')"          7 'six here plus one in Lib.java'
eq 'accessible.isaccessible'  "$(repval 'accessible\.isaccessible')"  1 'the deprecated query'
eq 'accessible.canaccess'     "$(repval 'accessible\.canaccess')"     1 'its Java 9 replacement'
eq 'accessible.setaccessible' "$(repval 'accessible\.setaccessible')" 5 'and the call that opens the door'
eq 'accessible.methods'       "$(repval 'accessible\.methods')"       2 'in two buckets'
eq 'accessible.files'         "$(repval 'accessible\.files')"         2 'in both files'
eq 'accessible call values'   "$(fieldset accessible call)" 'canAccess,isAccessible,setAccessible' \
   'all three names the counter takes'
eq 'accessible recv values'   "$(fieldset accessible recv)" 'AccessibleObject,Constructor,Field,Method' \
   'four owners off three receivers -- the field is the resolved owner'
hasrec accessible 'call=isAccessible recv=AccessibleObject in=reflectiveAccess' \
   'a Field receiver, and the owner is still the supertype'
hasrec accessible 'call=canAccess recv=AccessibleObject in=reflectiveAccess' \
   'the same, for the call that replaces it'
hasrec accessible 'call=setAccessible recv=Field in=reflectiveAccess' \
   'the SAME variable, one line later, reporting Field -- because setAccessible is overridden'
hasrec accessible 'call=setAccessible recv=Method in=reflectiveAccess'      'a Method receiver'
hasrec accessible 'call=setAccessible recv=Constructor in=reflectiveAccess' 'a Constructor receiver'
hasrec accessible 'call=setAccessible recv=AccessibleObject in=reflectiveAccess' \
   'and the same Field again through an AccessibleObject-typed variable'
hasrec accessible 'call=setAccessible recv=Field in=made' 'the second file, so .files is not 1'
inn accessible reflectiveAccess 6 'six records in one bucket'
# Impostor06.Slot declares all three, on a class that is not an AccessibleObject
# and could not be made one. Nothing.
noin accessible impostors06 'setAccessible on a class of ones own is not reflection'

# ============================================================ C-4: boxed ctors
echo "--- C-4: the wrapper constructors, and the valueOf calls beside them"
eq 'boxedctor.size'    "$(repval 'boxedctor\.size')"    5 'four here plus one in Lib.java'
eq 'boxedctor.methods' "$(repval 'boxedctor\.methods')" 3 'in three buckets'
eq 'boxedctor.files'   "$(repval 'boxedctor\.files')"   2 'in both files'
for w in Integer Long Boolean Double Short; do
   eq "boxedctor.type.${w}" "$(repval "boxedctor\\.type\\.java\\.lang\\.${w}")" 1 \
      "one ${w}, and the per-type key names the resolved class"
done
hasrec boxedctor 'java.lang.Integer in=boxed' 'three wrappers in one bucket'
hasrec boxedctor 'java.lang.Long in=boxed'    'the second'
hasrec boxedctor 'java.lang.Boolean in=boxed' 'the third'
hasrec boxedctor 'java.lang.Double in=notBoxed' 'a fourth in a bucket of its own'
hasrec boxedctor 'java.lang.Short in=boxedHere' 'and a fifth in the other file'
# notBoxed() holds one construction and two valueOf calls. `Integer.valueOf(4)`
# is the ARRIVAL shape -- it is what the rewrite produces -- so a counter that
# matched the wrapper name rather than the `new` would read three here and would
# charge the candidate for doing the work.
inn boxedctor notBoxed 1 'one of three: valueOf is the arrival shape, not a site'
# Impostor06 declares its own Integer with an int constructor. `new Integer(3)`
# there is a NewClassTree whose created type is smoke.Impostor06.Integer.
noin boxedctor impostors06 'a wrapper of ones own is not java.lang.Integer'

# ======================================================= C-5: the collections
#
# Departure and arrival in one gate, and the departure half is the one that has
# to be careful. `Collections.unmodifiableList(field)` is a live window onto
# state the owner goes on mutating; `Collections.emptyList()` builds a value.
# Converting the second is free and converting the first changes what callers
# see -- on the real tree one of them re-orders a recorded transcript -- so the
# record carries kind= and arg= and the gate holds the views in place.
echo "--- C-5 departure: both kinds, and all four argument shapes"
eq 'legacycoll.size'      "$(repval 'legacycoll\.size')"      8 'seven here plus one in Lib.java'
eq 'legacycoll.view'      "$(repval 'legacycoll\.view')"      4 'four wrapping calls'
eq 'legacycoll.viewfield' "$(repval 'legacycoll\.viewfield')" 2 'two of them over a field'
eq 'legacycoll.build'     "$(repval 'legacycoll\.build')"     4 'and four that construct'
eq 'legacycoll.methods'   "$(repval 'legacycoll\.methods')"   5 'in five buckets'
eq 'legacycoll.files'     "$(repval 'legacycoll\.files')"     2 'in both files'
eq 'legacycoll kind values' "$(fieldset legacycoll kind)" 'build,view' 'both kinds'
eq 'legacycoll arg values'  "$(fieldset legacycoll arg)"  'call,field,literal,local,none' \
   'every shape argShape distinguishes, on one tree'
hasrec legacycoll 'call=unmodifiableList kind=view arg=field in=viewOfField' \
   'the load-bearing one: a window onto a field'
hasrec legacycoll 'call=unmodifiableSet kind=view arg=local in=viewOfLocal' \
   'the same call over a local, which is a value'
hasrec legacycoll 'call=unmodifiableMap kind=view arg=call in=viewOfCall' \
   'and over something computed, which resolves to no element'
hasrec legacycoll 'call=emptyList kind=build arg=none in=builds' 'a no-argument factory'
hasrec legacycoll 'call=singletonList kind=build arg=call in=builds' \
   'a string literal is not an element either, so it reads computed'
# `Foo.class` is a MemberSelectTree that javac resolves to a synthetic element
# of kind FIELD. Left alone, every `Arrays.asList(A.class, B.class, ...)` in the
# real tree came back arg=field -- the one value that is supposed to mean "a
# live window onto somebody's mutable state" -- so class literals are called out
# before the element test runs, and this is the record that says so.
hasrec legacycoll 'call=asList kind=build arg=literal in=builds' \
   'a class literal, which javac resolves to a FIELD and which is not one'
hasrec legacycoll 'call=asList kind=build arg=local in=builds' 'and a parameter'
hasrec legacycoll 'call=unmodifiableList kind=view arg=field in=view' \
   'the second file has a view over a field too'
noin legacycoll impostors06 'a Collections of ones own wraps nothing'

echo "--- C-5 arrival: three owners, both calls, three arities"
eq 'factory.size'    "$(repval 'factory\.size')"    7 'six here plus one in Lib.java'
eq 'factory.of'      "$(repval 'factory\.of')"      6 'of, entry and ofEntries all count as of'
eq 'factory.copyof'  "$(repval 'factory\.copyof')"  1 'copyOf is counted apart'
eq 'factory.methods' "$(repval 'factory\.methods')" 2 'in two buckets'
eq 'factory.files'   "$(repval 'factory\.files')"   2 'in both files'
eq 'factory of+copyof' "$(( $(repval 'factory\.of') + $(repval 'factory\.copyof') ))" \
   "$(repval 'factory\.size')" 'the two partition the total'
hasrec factory 'java.util.List.of args=3 in=factories'      'List.of, and the arity beside it'
hasrec factory 'java.util.Set.of args=1 in=factories'       'Set.of'
hasrec factory 'java.util.Map.of args=2 in=factories'       'Map.of takes pairs, so 2'
hasrec factory 'java.util.List.copyOf args=1 in=factories'  'copyOf'
hasrec factory 'java.util.Map.entry args=2 in=factories'    'entry'
hasrec factory 'java.util.Map.ofEntries args=1 in=factories' \
   'and ofEntries, which only the arity tells from Map.of'
hasrec factory 'java.util.List.of args=1 in=boxedHere' 'the second file, so .files is not 1'
# java.util.Optional is not one of the three owners. `Optional.of(..)` in
# findNested() is an optop make and must not be a factory record -- if it were,
# C-5's arrival half would be paid by step 05's work.
noin factory findNested 'Optional.of is not a collection factory'
norec factory 'java.util.Optional' 'and no record anywhere names that owner'
noin factory impostors06 'nor is a static of() on a Bag'

# =============================================================== getPackage
# Published as a fact at this rung rather than gated: the real tree has two
# sites and B-12e reads the jars, not the source. Rehearsed anyway, because the
# counter asks for the owner AND the arity and only a tree with both shapes on
# it can say whether it asks for both.
echo "--- getPackage: the one-argument static, and the instance call beside it"
eq 'getpackage.size'  "$(repval 'getpackage\.size')"  2 'one per file'
eq 'getpackage.files' "$(repval 'getpackage\.files')" 2 'in both files'
hasrec getpackage 'call=getPackage in=packages'  'Package.getPackage("smoke")'
hasrec getpackage 'call=getPackage in=boxedHere' 'and the same in the other file'
noin getpackage noArgPackage \
   'getClass().getPackage() is a different owner at a different arity, and Java 9 left it alone'

# ============================================================== C-6: Optional
#
# Java 9 gave Optional the three operations that make an isPresent() branch
# unnecessary -- or, ifPresentOrElse, stream -- and C-6 asks for one of those
# three in the bucket where an isPresent() used to be. `map` and `orElseGet`
# would also remove one and they are Java 8 spellings step 05 could already have
# used, so they are deliberately not in the arrival set.
echo "--- C-6: the three Java 9 operations, and the branch they replace"
# optop.size, .methods and .get are asserted in the B-12 section below, where
# step 05 left them -- 20, 8 and 1, the first two moved by exactly this block's
# six records and the third not moved at all. Only isPresent is read here.
eq 'optop.ispresent' "$(repval 'optop\.ispresent')" 3  'three buckets still branch'
hasrec optop 'use ifPresentOrElse in=java9Optional' 'the two-consumer form'
hasrec optop 'use or in=java9Optional'              'the supplier form'
hasrec optop 'use stream in=stillBranching'         'and the third, which is a use like any other'
hasrec optop 'use isPresent in=stillBranching'      'the branch left standing beside it'
# The lambda inside ifPresentOrElse() contains no Optional call; the one inside
# or() does. enclosingMethod() resets at a method and not at a lambda, so that
# `Optional.of(..)` is attributed to java9Optional -- which is the bucket C-6
# reads, and the reason a candidate cannot escape the arrival half by writing
# the replacement inside a lambda.
hasrec optop 'make of in=java9Optional' 'a make inside a lambda belongs to the enclosing METHOD'
# C-6's own arithmetic, on records a compiler produced: the buckets that hold a
# Java 9 operation, and the buckets that still branch on isPresent().
recs optop | awk '$2 == "use" && ($3 == "or" || $3 == "ifPresentOrElse" || $3 == "stream") {
                     m = ""
                     for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
                     if (m != "") { split($1, f, ":"); print f[1] "#" m } }' \
   | LC_ALL=C sort -u > "${WORK}/opt9.txt"
recs optop | awk '$2 == "use" && $3 == "isPresent" {
                     m = ""
                     for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
                     if (m != "") { split($1, f, ":"); print f[1] "#" m } }' \
   | LC_ALL=C sort -u > "${WORK}/ispres.txt"
eq 'C-6 buckets with a Java 9 op' "$(grep -c . "${WORK}/opt9.txt" || true)" 2 \
   'java9Optional and stillBranching'
eq 'C-6 buckets still branching'  "$(grep -c . "${WORK}/ispres.txt" || true)" 3 \
   'uses, unwrapped and stillBranching'
# stillBranching() is in BOTH sets, and that is the fixture rather than an
# accident: C-6 takes the min of a departure and an arrival over the same
# want-set, so a bucket that acquired `stream()` while keeping its isPresent()
# satisfies one half and not the other. A gate that read only the arrival half
# would credit it.
grep -qF 'smoke/Smoke.java#stillBranching' "${WORK}/opt9.txt" \
   && grep -qF 'smoke/Smoke.java#stillBranching' "${WORK}/ispres.txt" \
   || fail "the half-converted bucket is not in both sets -- the min has nothing to bite on"
printf '    %-30s %s\n' 'C-6 half-converted bucket' \
   'stillBranching has a Java 9 op AND an isPresent() -- arrival yes, departure no'

# ==================================================================== C-7: var
#
# Two counters and they are not two halves of one thing. var.size counts the
# declarations that ALREADY use it; var.candidates counts the declarations that
# COULD, and it is non-zero on a tree with no `var` in it at all -- step 03's
# half of this file has four. That is why C-7 scores a departure against a
# frozen count rather than a ratio of candidates: the denominator moves when the
# candidate writes ordinary code.
echo "--- C-7: var, in both directions"
eq 'var.size'            "$(repval 'var\.size')"            6 'five here plus one in Lib.java'
eq 'var.methods'         "$(repval 'var\.methods')"         2 'in two buckets'
eq 'var.files'           "$(repval 'var\.files')"           2 'in both files'
eq 'var.candidates'      "$(repval 'var\.candidates')"      45 'the convertible declarations'
eq 'var.candidatenew'    "$(repval 'var\.candidatenew')"    14 'of which the same name twice'
eq 'var.candidatefiles'  "$(repval 'var\.candidatefiles')"  2  'in both files'
eq 'var.multideclarator' "$(repval 'var\.multideclarator')" 2  'int a = 1, b = 2 -- two, subtracted'
inn var inferred 5 'four positions: local, enhanced-for, basic-for and resource'
hasrec var 'inferred=int in=inferred'                    'a primitive'
hasrec var 'inferred=java.lang.StringBuilder in=inferred' 'a constructed type'
hasrec var 'inferred=java.lang.String in=inferred'        'the enhanced-for variable'
hasrec var 'inferred=java.io.FileInputStream in=inferred' 'and a resource variable'
hasrec var 'inferred=java.util.List in=boxedHere'         'the second file, so var.files is not 1'
# The candidate side, in the method written to hold one of each. Three
# candidates and six exclusions, and the exclusions are the point: a declared
# type that is not the initialiser's, a null literal, a lambda, an array
# initialiser, an anonymous body, and two declarators over one written type.
inn varcand candidates 3 'three of the nine declarations there are convertible'
hasrec varcand 'type=java.util.ArrayList init=java.util.ArrayList ctor=yes in=candidates' \
   'the same name twice, which is candidatenew'
hasrec varcand 'type=java.lang.StringBuilder init=java.lang.StringBuilder ctor=yes in=candidates' \
   'and again'
hasrec varcand 'type=java.lang.String init=java.lang.String ctor=no in=candidates' \
   'a call whose return type is written out: convertible, and not a construction'
# A `var` declaration is not a candidate for itself. countVar returns at the
# inferred test, so inferred() contributes five var records and no varcand ones
# -- without which C-7's departure and arrival halves would both move when only
# one of them should.
noin varcand inferred 'a declaration that already says var is not a candidate to say it'

# ==================================== B-12: step 05's library, held in place
#
# Eight record families this rung does not gate and the rung below does. B-12
# holds them, so a candidate who deletes an Optional to make a var conversion
# tidier pays for it there. Every assertion in this section is step 05's own
# rehearsal's, carried across word for word, and that is the point: they were
# written against a tree step 05 measured, and re-running them here on a tree
# with two hundred more lines in it is a statement that the appended section
# disturbed nothing. Three figures move and all three are optop's, because C-6
# below reads that block -- they are re-measured in place rather than relaxed.

# ------------------------------------------------------ B-12a: Optional
#
# Three halves: departure (a bucket holds no retnull record), arrival (a bucket
# declares a method returning java.util.Optional) and consumption (the share of
# instance calls ON an Optional that are not get()).
echo "--- B-12a: the null-returning lookup helpers, and what replaced them"
eq 'retnull.size'    "$(repval 'retnull\.size')"    9 'eight here plus one in Lib.java'
eq 'retnull.methods' "$(repval 'retnull\.methods')" 9 'each in a bucket of its own'
# 2 and not 1, which is the whole reason Lib.java exists: this key is what
# separates "the tree still returns null somewhere" from "this file does".
eq 'retnull.files'   "$(repval 'retnull\.files')"   2 'across both files, not one'
eq 'retnull.api'     "$(repval 'retnull\.api')"     3 'byDefault, lookupPublic, lookupProtected'

# All four modifier states, both api answers, and the disagreement between them.
# api is what apisig.sh would print, and modsOf is the declared modifier: an
# interface member with no modifier at all is implicitly public, so `mods=package
# api=yes` is the correct reading of `default String byDefault(..)` and a gate
# that used mods as a proxy for the published surface would get it backwards.
# Step 05's C-1 was confined to the PRIVATE population exactly because B-6 is an
# exact set (want=1147 hit=1147 extra=0) and widening a published signature is
# charged there like deleting it -- so this pair of fields is the one that rung's
# scope rested on, and B-12 now holds the result of it.
hasrec retnull 'mods=package ret=String api=yes in=byDefault' \
   'an interface default: no modifier, published anyway'
hasrec retnull 'mods=private ret=String api=no in=lookupPrivate'   'private, and not published'
hasrec retnull 'mods=public ret=String api=yes in=lookupPublic'    'public, and published'
hasrec retnull 'mods=protected ret=String api=yes in=lookupProtected' 'protected likewise'
hasrec retnull 'mods=package ret=String api=no in=lookupPackage'   'package-private, and not'
# All three scopes. api is `"decl".equals(scope) && isApiMember(m)`, so the two
# anonymous-body overrides read api=no although both are declared public: an
# override inside `new Mapper() { public String map(..) }` is not a line
# apisig.sh prints. Asserted because the conjunction is load-bearing and a
# refactor that dropped the scope test would leave every counter above intact.
hasrec retnull 'scope=anon mods=public ret=String api=no in=-' \
   'a field initialiser body: public override, outside any method, api=no'
hasrec retnull 'scope=anon mods=public ret=String api=no in=anonInMethod' \
   'and the same body inside a method, attributed to the OUTERMOST one'
hasrec retnull 'scope=lambda mods=- ret=- api=no in=lambdaReturnsNull' \
   'a lambda has no modifiers and no declared return type'
# The near miss. `return k.isEmpty() ? null : k;` is a null return that is not a
# retnull site: visitReturn counts a NULL_LITERAL expression and a ternary is a
# CONDITIONAL_EXPRESSION. This is a real limit of the departure half and it is
# frozen here rather than discovered on a candidate's tree -- a bucket can be
# emptied of retnull records by rewriting the statement as a ternary without
# writing an Optional, and what stops that paying is that the arrival half is a
# separate conjunct and the gate takes the min.
noin retnull ternaryNull \
   'a ternary null is invisible to visitReturn -- departure alone is not enough'

eq 'optret.size'  "$(repval 'optret\.size')"  3 'three Optional-returning declarations'
eq 'optret.files' "$(repval 'optret\.files')" 2 'in both files'
# Three spellings, one element. This is what "resolved through javac's elements"
# buys and it is the arrival half's entire correctness argument.
hasrec optret 'ret=Optional<String> name=findPrivate'             'the simple name'
hasrec optret 'ret=java.util.Optional<String> name=findQualified' 'the qualified name'
hasrec optret 'ret=Optional<Map<String, String>> name=findNested' 'and a nested type argument'
# Lib.Optional is a class of one's own called Optional, with get/isPresent/orElse
# on it, and `impostors()` returns one. If the arrival half matched the spelling
# it would read four here and a candidate could have paid step 05's C-1 by
# declaring a class.
norec optret 'name=impostors' 'a class called Optional is not java.util.Optional'

eq 'optop.size'    "$(repval 'optop\.size')"    20 'six make, fourteen use'
eq 'optop.methods' "$(repval 'optop\.methods')" 8  'across eight buckets'
eq 'optop.get'     "$(repval 'optop\.get')"     1  'exactly one of them is get()'
hasrec optop 'make ofNullable in=findQualified' 'a static factory is a make'
hasrec optop 'make of in=findNested'            'and so is of'
hasrec optop 'make empty in=uses'               'and empty'
hasrec optop 'use orElseGet in=uses'            'an instance call is a use'
hasrec optop 'use ifPresent in=uses'            'including the ones that take a lambda'
hasrec optop 'use orElseThrow in=orThrow'       'and the throwing consumer'
hasrec optop 'use get in=unwrapped'             'the one get(), in the method that unwraps'
# Same three names, on a type that is not java.util.Optional. `make` records are
# deliberately NOT in the consumption denominator -- producing an Optional is
# what the arrival half scores -- so if the impostor's calls were counted they
# would land in `use` and RAISE the share, which is the wrong direction.
noin optop impostors 'the impostor class contributes nothing to either side'

# The consumption share, computed here exactly as the gate computes it, on
# records a compiler produced. This is the term that separates a migration from
# a rename: `findMatch(..).get()` is `findMatch(..)` with more syntax. It MOVES at
# this rung -- 8 of 9 becomes 13 of 14 -- because C-6's arrival shapes are six more
# records in the same block, five of them uses. Re-measured rather than carried.
recs optop | awk '$2 == "use"' > "${WORK}/optuse.txt"
optuse=$(grep -c . "${WORK}/optuse.txt" || true)
optget=$(awk '$3 == "get"' "${WORK}/optuse.txt" | grep -c . || true)
eq 'optop uses'  "${optuse}" 14 'fourteen instance calls on an Optional'
eq 'optop gets'  "${optget}" 1 'one of which is get()'
cons=$(awk -v n="$(( optuse - optget ))" -v d="${optuse}" \
   'BEGIN { printf "%.6f", (d <= 0) ? 0 : n / d }')
eq 'B-12 consumption share' "${cons}" 0.928571 '13 of 14 -- a tree of all-get() reads 0.000000'
# `unwrapped()` is `if (o.isPresent()) return o.get();` written as a ternary: two
# uses, one of them get(), which is the 0.5 the gate's comment names as the right
# answer for a tree that moved the null check rather than removing it. Both its
# records are in one bucket, so the share is computable per bucket as well as
# over the tree, and this is the fixture that says so.
ubuck=$(recs optop | awk '$0 ~ /in=unwrapped/' | grep -c . || true)
ubget=$(recs optop | awk '$0 ~ /in=unwrapped/ && $3 == "get"' | grep -c . || true)
eq 'unwrapped uses' "${ubuck}" 2 'isPresent and get, in one bucket'
eq 'unwrapped gets' "${ubget}" 1 'so that bucket alone reads 0.5'

# ---------------------------------------------------- B-12b: Collectors
#
# Departure: of the wanted buckets, how many hold no accum record. Fidelity: how
# many hold a collect record whose factory is ACCEPTED for the factory
# baseline/sinks.txt names.
echo "--- B-12b: the accumulate sites step 04 left standing, and the collectors"
eq 'accum.size'    "$(repval 'accum\.size')"    6 'six forEach(sink::add) sites'
eq 'accum.methods' "$(repval 'accum\.methods')" 3 'in three buckets'
# All three operations the counter accepts, each named. A counter matching `add`
# alone reads 4 here and step 04's own 24-bucket population was built with put
# and addAll in it.
hasrec accum 'op=add sinktype=java.util.List in=accumulate'  'add into a List'
hasrec accum 'op=put sinktype=java.util.Map in=accumulate'   'put into a Map'
hasrec accum 'op=addAll sinktype=java.util.List in=accumAll' 'addAll into a List'
# sinktype is the DECLARED type of the sink and not the constructed one, and
# these two records are the fixture that says so: `Set<String> seen = new
# LinkedHashSet<>()` reads java.util.Set and `LinkedHashSet<String> ordered =
# new LinkedHashSet<>()` reads java.util.LinkedHashSet, for two variables built
# by the same expression. That is why baseline/sinks.txt carries the wanted
# factory per bucket, MEASURED at authoring time from the golden, rather than
# derived from sinktype at scoring time: the one order-bearing sink in the real
# tree (DynamicServiceLoader#createInstances) is declared Set, and deriving from
# sinktype would have asked it for toSet() and lost the iteration order that is
# in the 935 recordings family A replays.
hasrec accum 'sink=seen op=add sinktype=java.util.Set in=accumulate' \
   'declared Set, constructed LinkedHashSet -- sinktype says Set'
hasrec accum 'sink=ordered op=add sinktype=java.util.LinkedHashSet in=accumulate' \
   'the same construct spelled concretely -- so the reading is the declaration'
# The two shapes the counter refuses, and both are in accumulate() beside the
# four it accepts. `s -> copy.put(s, s)` is a lambda body and not a method
# reference; `this::shout` is a method reference whose target is not a sink
# operation. A counter that matched forEach's ARGUMENT loosely would read 6 in
# this one method instead of 4.
eq 'accum in accumulate' "$(recs accum | grep -cF 'in=accumulate' || true)" 4 \
   'four of the six, and the other two shapes in the same method are refused'

eq 'collect.size'         "$(repval 'collect\.size')"         7 'seven collect(..) calls'
eq 'collect.methods'      "$(repval 'collect\.methods')"      7 'one per bucket'
eq 'collect.tolist'       "$(repval 'collect\.tolist')"       2 'one imported, one static-imported'
eq 'collect.toset'        "$(repval 'collect\.toset')"        1 'one toSet'
eq 'collect.tocollection' "$(repval 'collect\.tocollection')" 1 'one toCollection'
# `other` is everything that is not one of the three the acceptance rule names,
# and joining() is in it: 2 + 1 + 1 + 3 = 7. Asserted with the arithmetic spelled
# out because "3" looks like the two impostors and is not.
eq 'collect.other'        "$(repval 'collect\.other')"        3 'two impostors AND joining'
eq 'collect factors sum'  "$(( $(repval 'collect\.tolist') + $(repval 'collect\.toset') \
                             + $(repval 'collect\.tocollection') + $(repval 'collect\.other') ))" \
   "$(repval 'collect\.size')" 'the four buckets partition the total'
hasrec collect 'to=toList in=collected'               'the plain toList'
hasrec collect 'to=toList in=collectedStatic'         'and one reached through a static import'
hasrec collect 'to=toSet in=collectedSet'             'toSet'
hasrec collect 'to=toCollection in=collectedOrdered'  'toCollection'
hasrec collect 'to=joining in=joined'                 'and a factory the rule does not name'
# The two ways to write something that is not a Collectors call and would be
# taken for one by a looser reading: an OWNER that is not java.util.stream.
# Collectors, and an ARITY that is not the one-argument collect(Collector).
hasrec collect 'to=other in=viaImpostor' 'a static toList() on a class of ones own'
hasrec collect 'to=other in=viaThreeArg' 'and the three-argument collect(..)'
# ...and the two that produce no record at all. `String collect(List)` is a
# method merely NAMED collect, and notACollect() calls it. A name-matching
# counter finds a collector in a method that has no stream in it.
noin collect notACollect 'calling a method named collect is not collecting'
noin collect collect     'and declaring one is not either -- in=collect prefixes in=collected'
# The factory, per bucket, in the shape the gate's acceptance rule consumes. The
# rule itself (want toList -> accept toList or toCollection; want toSet -> accept
# toSet or toCollection; want toCollection -> accept toCollection ONLY) is
# arithmetic over a set file and belongs to c-rehearsal.sh, which lifts it out of
# test.sh and runs it on reports written to exercise all three legs. What is
# rehearsed HERE is the input that arithmetic reads: that a real compile produces
# one <file>#<method> key per collect with the factory beside it.
recs collect \
   | awk '{ split($1, f, ":"); m = ""; to = ""
            for (i = 2; i <= NF; i++) {
               if (substr($i, 1, 3) == "to=") to = substr($i, 4)
               if (substr($i, 1, 3) == "in=") m = substr($i, 4) }
            if (m != "" && to != "") print f[1] "#" m, to }' \
   | LC_ALL=C sort -u > "${WORK}/collect-to.txt"
eq 'collect bucket/factory pairs' "$(grep -c . "${WORK}/collect-to.txt" || true)" 7 \
   'every collect record yields a key and a factory'
grep -qF 'smoke/Smoke.java#collectedOrdered toCollection' "${WORK}/collect-to.txt" \
   || fail "the bucket/factory pairing lost the toCollection site"

# ------------------------------------------------------- B-12c: Objects
echo "--- B-12c: java.util.Objects in the value types"
eq 'objects.size'    "$(repval 'objects\.size')"    6 'six java.util.Objects calls'
eq 'objects.methods' "$(repval 'objects\.methods')" 5 'in five buckets'
hasrec objects 'equals in=equals'         'Objects.equals inside equals()'
hasrec objects 'hash in=hashCode'         'Objects.hash inside hashCode()'
hasrec objects 'requireNonNull in=required' 'and a requireNonNull the gate buckets away from'
# hash and hashCode are two MEMBERS of one resolved class, and only hash is
# accepted: hashCode(Object) is the null-safe single-value form and does not
# combine fields, so a hashCode built from it still hand-rolls the 31*result
# loop. Lib.Halfway is exactly that tree and it must not count.
hasrec objects 'hashCode in=hashCode' 'Objects.hashCode exists and is NOT Objects.hash'
# Lib.Objects is a class of one's own with equals and hash on it. Same two
# spellings, no element -- and it contributes nothing, which is what makes the
# name match above a match on a member of a resolved class rather than on a word.
noin objects impostorObjects 'a class called Objects is not java.util.Objects'

# The predicate the gate computes, on real records: a file counts when its
# <file>#equals bucket holds an `equals` record AND its <file>#hashCode bucket
# holds a `hash` record. 1 of 2, and the 2 are the fixture -- Smoke.java does the
# work, Lib.java calls java.util.Objects in both members and still fails, because
# Halfway#hashCode reaches for hashCode instead of hash.
#
# Lib.Legacy and Lib.Halfway share the Lib.java#equals and Lib.java#hashCode
# buckets: file-level bucketing cannot tell two value types in one file apart.
# That is a real property and it is written down rather than worked around,
# because it does not reach the gate -- each of the three files in
# baseline/valuetypes.txt declares exactly one class with equals and hashCode.
recs objects \
   | awk '{ split($1, f, ":"); m = ""
            for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4)
            if (m != "") print f[1] "#" m, $2 }' \
   | LC_ALL=C sort -u > "${WORK}/objects-in.txt"
vt_hit=$(awk '{ got[$1 SUBSEP $2] = 1 }
              END { n = 0
                    for (k in got) {
                       split(k, p, SUBSEP)
                       split(p[1], b, "#")
                       if (b[2] != "equals" || p[2] != "equals") continue
                       if ((b[1] "#hashCode" SUBSEP "hash") in got) n++
                    }
                    print n + 0 }' "${WORK}/objects-in.txt")
eq 'B-12c files migrated' "${vt_hit}" 1 'Smoke.java yes; Lib.java uses hashCode, not hash'

# ---------------------------------------------------------- B-12d: time
echo "--- B-12d: java.time where the epoch-millis calls were"
eq 'jtime.size'    "$(repval 'jtime\.size')"    6 'six java.time calls'
eq 'jtime.files'   "$(repval 'jtime\.files')"   2 'in both files'
eq 'jtime.methods' "$(repval 'jtime\.methods')" 2 'in two buckets'
hasrec jtime 'java.time.Instant.now as=call in=elapsed'       'Instant.now'
hasrec jtime 'java.time.Instant.ofEpochMilli as=call in=elapsed' 'and the bridge from millis'
hasrec jtime 'java.time.Duration.between as=call in=elapsed'  'Duration.between'
hasrec jtime 'java.time.LocalDate.now as=call in=dayOfYear'   'and a fourth owner in the other file'
eq 'millis.size'    "$(repval 'millis\.size')"    4 'four legacy clock reads'
eq 'millis.methods' "$(repval 'millis\.methods')" 2 'in two buckets'
# Four of the seven shapes the counter takes, and that is the point: the real
# tree's millis population is five System.currentTimeMillis calls and NOTHING
# else, so a counter that only knew that spelling would read identically on it.
# The other three shapes are rehearsed here because the departure half has to
# stay honest about a candidate who swaps one legacy clock for another.
hasrec millis 'System.currentTimeMillis in=legacy'   'the shape the real tree has'
hasrec millis 'System.nanoTime in=legacy'            'the one it would be swapped for'
hasrec millis 'java.util.Date.getTime in=legacy'     'a legacy type member'
hasrec millis 'java.text.DateFormat.format in=stamp' 'and a formatter, resolved to its declaring class'
# `new java.util.Date()` is on the line above getTime() and is NOT a
# MethodInvocationTree, so the millis counter cannot see it. This is why
# baseline/timesites.txt records an independent grep for the five legacy types
# beside the measurement rather than instead of it -- and the grep found zero
# files in the whole repository, which is the only reason the blind spot is
# harmless there.
eq 'millis in legacy()' "$(recs millis | grep -cF 'in=legacy' || true)" 3 \
   'three of the four; the constructor on the same line is invisible'

# ==================================================== B-11: step 04, held
echo "--- B-11: step 04s constructs, and their co-existence with this steps C-7"
eq 'lambda.size'        "$(repval 'lambda\.size')"        19 'fifteen from step 05, four more here'
eq 'lambda.files'       "$(repval 'lambda\.files')"       1  'all of them in Smoke.java'
eq 'methodref.size'     "$(repval 'methodref\.size')"     13 'unmoved by this rungs half'
eq 'iface.samannotated' "$(repval 'iface\.samannotated')" 1  'one @FunctionalInterface'
eq 'iface.sam'          "$(repval 'iface\.sam')"          3  'Handler, Mapper, Lookup'
eq 'iface.decl'         "$(repval 'iface\.decl')"         4  'Both has two abstract methods'
eq 'iface.default'      "$(repval 'iface\.default')"      1  'Lookup#byDefault'
[ "$(repval 'iface\.samannotated')" -le "$(repval 'iface\.sam')" ] \
   || fail "samannotated exceeds sam in a tree that compiles -- one of the two counters is wrong"
eq 'foreach.collectmethods' "$(repval 'foreach\.collectmethods')" 2 'loops() and unwound()'
eq 'stream.opmethods'       "$(repval 'stream\.opmethods')"       11 'ten from step 05 plus stillBranching'
eq 'stream.producers'       "$(repval 'stream\.producers')"       18 \
   'seventeen from step 05 plus Optional.stream() -- known, frozen, and reaches no gate'
hasrec stream 'ENTER toList in=toList' \
   'Collectors.toList() files as a producer -- known, frozen, and reaches no gate'
norec  stream 'ENTER collect' 'collect is an op on a stream, not an entry'
hasrec streamop 'ON collect in=collected' 'which is what makes a converted bucket still hold one'

buckets() { # buckets <block> [substring filter]
   recs "$1" \
      | { if [ -n "${2:-}" ]; then grep -F "$2"; else cat; fi; } \
      | awk '{ split($1, a, ":"); m = "";
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4);
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u
}
buckets stream   '' > "${WORK}/bk-prod.txt"
buckets streamop '' > "${WORK}/bk-stage.txt"
LC_ALL=C comm -12 "${WORK}/bk-prod.txt" "${WORK}/bk-stage.txt" > "${WORK}/bk-both.txt"
covered_buckets() { grep -qF "$1" "${WORK}/bk-both.txt"; }

# The co-existence fixture, which is the ninth trap wearing its other mask: a
# regression gate that goes RED for doing the CURRENT step correctly. Step 05
# converted 23 accumulate sites into pipelines; this rung rewrites declarations
# inside them. Both shapes are in this tree and both must stay covered.
covered_buckets 'Smoke.java#accumulate' \
   || fail "an UNCONVERTED accumulate bucket is not covered -- B-11's denominator is wrong"
covered_buckets 'Smoke.java#collected' \
   || fail "a CONVERTED bucket lost its B-11 coverage -- C-2 and B-11 would fight"
covered_buckets 'Smoke.java#collectedOrdered' \
   || fail "toCollection breaks B-11 coverage"
# And this rung's own pair: stillBranching() holds `o.stream().count()`, a
# producer and an op in one bucket, so C-6's arrival shape is a B-11-covered
# bucket rather than a new one that has to be argued about.
covered_buckets 'Smoke.java#stillBranching' \
   || fail "C-6's stream() arrival is not a covered bucket -- C-6 and B-11 would fight"
printf '    %-30s %s\n' 'B-11 vs C-6' \
   'stillBranching holds Optional.stream() and an op, and stays covered'
# ...and the control, without which the assertions above are satisfied by a gate
# that says yes to everything.
if covered_buckets 'Smoke.java#unwound'; then
   fail "a bucket with no pipeline in it is covered -- the conjunction is not conjoining"
fi
hasrec foreach 'collect=yes in=unwound' 'a collecting loop with no pipeline beside it'
printf '    %-30s %s\n' 'B-11 control' 'unwound() holds a collecting loop and is NOT covered'

# ====================================================== B-10 and below, held
echo "--- B-10: what step 03 won, still visible here"
eq 'newgeneric.explicit'     "$(repval 'newgeneric\.explicit')"     3 'two HashMaps plus loops destination'
eq 'newgeneric.explicitanon' "$(repval 'newgeneric\.explicitanon')" 1 'one of them has a class body'
eq 'explicit - explicitanon' \
   "$(( $(repval 'newgeneric\.explicit') - $(repval 'newgeneric\.explicitanon') ))" 2 \
   'the non-anonymous remainder, which is what B-10 holds'
# 17 and not step 05's 9: every sink and every collection this rung constructs is
# a diamond. B-10's non-regression term reads explicit, not diamond, so this
# rises freely -- asserted anyway, because a term that read diamond would make
# every step above this one unreachable and the number is where that shows.
eq 'newgeneric.diamond'      "$(repval 'newgeneric\.diamond')"      17 'this rungs collections are all diamonds'
eq 'newgeneric.diamondfiles' "$(repval 'newgeneric\.diamondfiles')" 2  'in both files'
eq 'newgeneric.raw'          "$(repval 'newgeneric\.raw')"          1  'new HashMap(), raw -- the penalty reads this'
eq 'suppress.sites'          "$(repval 'suppress\.sites')"          1  'one @SuppressWarnings'
eq 'suppress.pairs'          "$(repval 'suppress\.pairs')"          2  'carrying two values'
# 2 and not step 05's 1: C-7's arrival half writes `try (var in = ...)`, which is
# a try-with-resources in its own right. B-10 holds try.resources at or above
# step 03's reading, so a rung that ADDS one is safe and a rung that converts one
# back is not -- and this is the number that would show it.
eq 'try.resources'           "$(repval 'try\.resources')"           2  'step 03s, plus C-7s resource var'
eq 'try.resourcevars'        "$(repval 'try\.resourcevars')"        2  'one resource apiece'
eq 'close.manual'            "$(repval 'close\.manual')"            1  'the hand-written close() in the finally'
eq 'iofile.new'              "$(repval 'iofile\.new')"              4  'three from step 03 plus C-7s'
eq 'niofile.ref'             "$(repval 'niofile\.ref')"             2  'Paths.get and Files.exists'
eq 'niofile.staticref'       "$(repval 'niofile\.staticref')"       1  'isDirectory(p), through the static import'

# The list blocks, not just the totals. A counter that increments without
# recording where is one the trajectory analysis cannot follow back to a line.
for blk in diamond close iofile niofile suppress anonfn lambda methodref stream \
           streamop loop foreach interfacedecl retnull optret optop accum \
           collect objects jtime millis newinstance boxedctor factory \
           legacycoll accessible getpackage var varcand; do
   grep -qE "^${blk}\[0\]=.*\.java:[0-9]+" "${rep10}" \
      || fail "${blk}[0] does not name a line of a source file"
done

# ==================================== the -source 8 probe, and what refuses it
#
# ok8 is a term in this step's B-4 and the whole of what "the tree now requires
# Java 9 or later" means at the source level, so it is not enough that the probe
# said no: it has to have said no for the reason the step is about. Six errors,
# every one of them `cannot find symbol / class var` -- at -source 8 the word is
# an ordinary type name and javac looks for a class called var -- and the six
# lines they name are EXACTLY the six lines the var block records. A tree that
# failed at 8 for an unrelated reason would satisfy ok8=no and would mean
# nothing.
echo "--- the -source 8 probe, and the six declarations that refuse it"
e8=$(envval javac.errors "${env8}")
eq 'javac.errors@8'  "${e8}" 6 'one per var declaration'
# 12 and not 0: javac attributes and writes the classes it CAN before it gives
# up, so a rejected tree still leaves a partial output directory. Frozen at the
# measured figure rather than assumed empty -- and it is 12 of the 25 the clean
# compile writes, which is itself a statement that the probe compiled the whole
# tree and did not stop at the first error.
eq 'javac.classes@8' "$(envval javac.classes "${env8}")" 12 'a partial output: 12 of 25'
lvl8=$(grep -cE 'symbol: +class var' "${WORK}/src-8.txt.javac.log" || true)
eq 'var errors@8' "${lvl8}" 6 'every one of them is javac looking for a class called var'
grep -F ': error:' "${WORK}/src-8.txt.javac.log" \
   | sed 's/: error:.*//' | sed 's#^.*/mod/#mod/#' \
   | LC_ALL=C sort -u > "${WORK}/err8.txt"
recs var | awk '{ print $1 }' | LC_ALL=C sort -u > "${WORK}/var8.txt"
if ! diff -u "${WORK}/var8.txt" "${WORK}/err8.txt" > "${WORK}/var8.diff"; then
   sed -n '1,20p' "${WORK}/var8.diff" >&2
   fail "the lines that fail at -source 8 are not the lines the var block records"
fi
printf '    %-30s %s\n' 'ok8 vs var records' \
   'the six failing lines ARE the six var records -- nothing else stops this tree at 8'

# ================================================= the three flatten controls
#
# Three more cuts, by the same mechanism as the step-07 one above and for the
# same reason: each lands on a tree a lower rung already measured, and each
# asserts that rung's frozen figures rather than re-deriving them.
#
# flatten() itself is defined further up, because the step-07 cut needs it
# before any of these three do.

echo "--- the step-06 cut: the same tree without var must be step 05s, and must compile at 8"
flatten 06 "${WORK}/f06" yes
TREE="${WORK}/f06"
f06=$(srcgate f06 8 || true)
[ "${f06}" = yes ] || {
   echo "--- javac at -source 8 on the flattened tree ---" >&2
   tail -40 "${WORK}/src-f06.txt.javac.log" >&2 || :
   fail "step 05's own smoke tree was rejected at -source 8 (source.ok=${f06})"
}
fv() { sed -n "s/^$1=//p" "${WORK}/src-f06.txt" | head -1; }
# Forty figures step 05's own rehearsal froze, on a different day, against a
# tree it measured. Asserted rather than re-derived: a counter that drifted
# between the two rungs is a disagreement between two measurements.
eq 'f06 javac.classes' "$(envval javac.classes "${WORK}/src-f06.txt.env")" 17 "step 05's figure"
eq 'f06 files'         "$(fv 'files')"         2 "step 05's figure"
eq 'f06 retnull.size'    "$(fv 'retnull\.size')"    9 "step 05's figure"
eq 'f06 retnull.methods' "$(fv 'retnull\.methods')" 9 "step 05's figure"
eq 'f06 retnull.files'   "$(fv 'retnull\.files')"   2 "step 05's figure"
eq 'f06 retnull.api'     "$(fv 'retnull\.api')"     3 "step 05's figure"
eq 'f06 optret.size'     "$(fv 'optret\.size')"     3 "step 05's figure"
eq 'f06 optret.files'    "$(fv 'optret\.files')"    2 "step 05's figure"
eq 'f06 optop.size'      "$(fv 'optop\.size')"      14 "step 05's figure"
eq 'f06 optop.methods'   "$(fv 'optop\.methods')"   6  "step 05's figure"
eq 'f06 optop.get'       "$(fv 'optop\.get')"       1  "step 05's figure"
eq 'f06 accum.size'      "$(fv 'accum\.size')"      6  "step 05's figure"
eq 'f06 accum.methods'   "$(fv 'accum\.methods')"   3  "step 05's figure"
eq 'f06 collect.size'         "$(fv 'collect\.size')"         7 "step 05's figure"
eq 'f06 collect.methods'      "$(fv 'collect\.methods')"      7 "step 05's figure"
eq 'f06 collect.tolist'       "$(fv 'collect\.tolist')"       2 "step 05's figure"
eq 'f06 collect.toset'        "$(fv 'collect\.toset')"        1 "step 05's figure"
eq 'f06 collect.tocollection' "$(fv 'collect\.tocollection')" 1 "step 05's figure"
eq 'f06 collect.other'        "$(fv 'collect\.other')"        3 "step 05's figure"
eq 'f06 objects.size'    "$(fv 'objects\.size')"    6 "step 05's figure"
eq 'f06 objects.methods' "$(fv 'objects\.methods')" 5 "step 05's figure"
eq 'f06 jtime.size'      "$(fv 'jtime\.size')"      6 "step 05's figure"
eq 'f06 jtime.files'     "$(fv 'jtime\.files')"     2 "step 05's figure"
eq 'f06 jtime.methods'   "$(fv 'jtime\.methods')"   2 "step 05's figure"
eq 'f06 millis.size'     "$(fv 'millis\.size')"     4 "step 05's figure"
eq 'f06 millis.methods'  "$(fv 'millis\.methods')"  2 "step 05's figure"
eq 'f06 lambda.size'     "$(fv 'lambda\.size')"     15 "step 05's figure"
eq 'f06 methodref.size'  "$(fv 'methodref\.size')"  13 "step 05's figure"
eq 'f06 iface.decl'      "$(fv 'iface\.decl')"      4  "step 05's figure"
eq 'f06 iface.sam'       "$(fv 'iface\.sam')"       3  "step 05's figure"
eq 'f06 iface.samannotated' "$(fv 'iface\.samannotated')" 1 "step 05's figure"
eq 'f06 foreach.collectmethods' "$(fv 'foreach\.collectmethods')" 2 "step 05's figure"
eq 'f06 stream.producers' "$(fv 'stream\.producers')" 17 "step 05's figure"
eq 'f06 stream.opmethods' "$(fv 'stream\.opmethods')" 10 "step 05's figure"
eq 'f06 newgeneric.explicit' "$(fv 'newgeneric\.explicit')" 3 "step 05's figure"
eq 'f06 newgeneric.diamond'  "$(fv 'newgeneric\.diamond')"  9 "step 05's figure"
eq 'f06 newgeneric.raw'      "$(fv 'newgeneric\.raw')"      1 "step 05's figure"
eq 'f06 try.resources'   "$(fv 'try\.resources')"   1 "step 05's figure"
eq 'f06 close.manual'    "$(fv 'close\.manual')"    1 "step 05's figure"
eq 'f06 iofile.new'      "$(fv 'iofile\.new')"      3 "step 05's figure"
eq 'f06 niofile.ref'     "$(fv 'niofile\.ref')"     2 "step 05's figure"
eq 'f06 niofile.staticref' "$(fv 'niofile\.staticref')" 1 "step 05's figure"
# ...and every counter this step introduced goes to zero. Not "goes down": to
# zero, because every construct that moves one of them is below a marker in one
# of the two files. A key wired to a constant reads the full-tree figure here.
for k in newinstance.size newinstance.legacy newinstance.ctor newinstance.methods \
         newinstance.files boxedctor.size boxedctor.methods boxedctor.files \
         factory.size factory.of factory.copyof factory.methods factory.files \
         legacycoll.size legacycoll.view legacycoll.viewfield legacycoll.build \
         legacycoll.methods legacycoll.files accessible.size accessible.isaccessible \
         accessible.canaccess accessible.setaccessible accessible.methods \
         accessible.files getpackage.size getpackage.files var.size var.methods \
         var.files var.multideclarator; do
   got=$(fv "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got:-0}" = 0 ] || fail "flattened ${k} is ${got}, not 0 -- the counter is not following the edit"
done
printf '    %-30s %s\n' 'f06 step-06 counters' 'all 31 keys read 0'
# var.candidates is NOT in that list and must not be. It counts the declarations
# that COULD be written with var, which is a property of ordinary code: the
# step-05 tree has fifteen and the step-03 half alone has four. C-7 therefore
# scores against a count frozen at authoring time rather than against this
# denominator, and the two numbers below are what that decision looks like.
eq 'f06 var.candidates'   "$(fv 'var\.candidates')"   15 'a tree with no var in it still has candidates'
eq 'f06 var.candidatenew' "$(fv 'var\.candidatenew')" 4  'four of them the same name twice'

echo "--- the step-05 cut: and again, one rung further down"
# Lib.java is not copied at all here. Smoke.java's step-03 and step-04 halves are
# step 04's own file verbatim, so what is left differs from the tree step 04
# measured only in the import block and the header javadoc, neither of which is
# counted.
flatten 05 "${WORK}/f05" no
TREE="${WORK}/f05"
f05=$(srcgate f05 8 || true)
[ "${f05}" = yes ] || {
   echo "--- javac at -source 8 on the twice-flattened tree ---" >&2
   tail -40 "${WORK}/src-f05.txt.javac.log" >&2 || :
   fail "step 04's own smoke tree was rejected at -source 8 (source.ok=${f05})"
}
fv() { sed -n "s/^$1=//p" "${WORK}/src-f05.txt" | head -1; }
eq 'f05 files'                "$(fv 'files')"                        1 'one file again'
eq 'f05 anonymous.size'       "$(fv 'anonymous\.size')"              4 "step 04's figure"
eq 'f05 anonymousfunctional'  "$(fv 'anonymousfunctional\.size')"    3 "step 04's figure"
eq 'f05 .convertible'         "$(fv 'anonymousfunctional\.convertible')" 1 "step 04's figure"
eq 'f05 .forward'             "$(fv 'anonymousfunctional\.forward')" 1 "step 04's figure"
eq 'f05 lambda.size'          "$(fv 'lambda\.size')"                 5 "step 04's figure"
eq 'f05 lambda.expr'          "$(fv 'lambda\.expr')"                 4 "step 04's figure"
eq 'f05 methodref.size'       "$(fv 'methodref\.size')"              2 "step 04's figure"
eq 'f05 iface.decl'           "$(fv 'iface\.decl')"                  3 "step 04's figure"
eq 'f05 iface.sam'            "$(fv 'iface\.sam')"                   2 "step 04's figure"
eq 'f05 iface.samannotated'   "$(fv 'iface\.samannotated')"          1 "step 04's figure"
eq 'f05 iface.abstract'       "$(fv 'iface\.abstract')"              4 "step 04's figure"
eq 'f05 foreach.size'         "$(fv 'foreach\.size')"                3 "step 04's figure"
eq 'f05 foreach.collect'      "$(fv 'foreach\.collect')"             1 "step 04's figure"
eq 'f05 foreach.collectmethods' "$(fv 'foreach\.collectmethods')"    1 "step 04's figure"
eq 'f05 loop.size'            "$(fv 'loop\.size')"                   6 "step 04's figure"
eq 'f05 stream.producers'     "$(fv 'stream\.producers')"            2 "step 04's figure"
eq 'f05 stream.ops'           "$(fv 'stream\.ops')"                  4 "step 04's figure"
eq 'f05 stream.opmethods'     "$(fv 'stream\.opmethods')"            2 "step 04's figure"
eq 'f05 newgeneric.explicit'  "$(fv 'newgeneric\.explicit')"         3 "step 04's figure"
eq 'f05 newgeneric.diamond'   "$(fv 'newgeneric\.diamond')"          1 "step 04's figure"
eq 'f05 newgeneric.raw'       "$(fv 'newgeneric\.raw')"              1 "step 04's figure"
eq 'f05 try.resources'        "$(fv 'try\.resources')"               1 "step 04's figure"
eq 'f05 close.manual'         "$(fv 'close\.manual')"                1 "step 04's figure"
eq 'f05 iofile.new'           "$(fv 'iofile\.new')"                  3 "step 04's figure"
eq 'f05 niofile.ref'          "$(fv 'niofile\.ref')"                 2 "step 04's figure"
eq 'f05 niofile.staticref'    "$(fv 'niofile\.staticref')"           1 "step 04's figure"
for k in retnull.size retnull.methods retnull.files retnull.api \
         optret.size optret.files optop.size optop.methods optop.get \
         accum.size accum.methods collect.size collect.methods \
         collect.tolist collect.toset collect.tocollection collect.other \
         objects.size objects.methods jtime.size jtime.files jtime.methods \
         millis.size millis.methods; do
   got=$(fv "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got:-0}" = 0 ] || fail "twice-flattened ${k} is ${got}, not 0 -- the counter is not following the edit"
done
printf '    %-30s %s\n' 'f05 step-05 counters' 'all 24 keys read 0'

echo "--- the step-04 cut: the Java 7 half is still the Java 7 half"
# Not a term in any gate at this rung -- test.sh no longer probes 1.7 at all --
# and kept because it is the cheapest statement in the image that the step-03
# half of this file has not quietly acquired a Java 8 construct. If it ever
# does, three rungs' worth of frozen figures above become claims about a
# different tree.
flatten 04 "${WORK}/f04" no
TREE="${WORK}/f04"
f04=$(srcgate f04 1.7 || true)
[ "${f04}" = yes ] || {
   echo "--- javac at -source 1.7 on the step-03 tree ---" >&2
   tail -40 "${WORK}/src-f04.txt.javac.log" >&2 || :
   fail "the Java 7 half of the same file was rejected at 1.7 (source.ok=${f04})"
}
printf '    %-30s %s\n' 'the step-03 half at 1.7' 'yes -- three rungs of source levels, three answers'

echo "src-rehearsal: SourceScan counts a real two-file tree as measured; this rung's five blocks read nine instanceof sites in two kinds, one document written twice, four sealed shapes and four switch corners, and the near misses beside each of them produce nothing; the step-09 cut is step 08's tree at -source 16, clean, at step 08's 31 classes with its four inherited instanceof sites and every other new counter at zero; the inherited svc channel B-15 reads is still scoped by resolved owner and resolved qualified name; the step-07 cut takes all sixteen step-07 and step-08 counters to zero; and the same tree without its six var declarations is step 05's, at step 05's forty figures"
