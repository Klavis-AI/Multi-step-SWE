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
#   * the three probes are the three the scorer makes -- `srcgate 25 25`,
#     `srcgate 24 24`, `srcgate def ""` -- and the two that compile must AGREE,
#     on the bytes.
#
# WHAT STEP 13 ADDS TO STEP 12'S VERSION OF THIS FILE
# ---------------------------------------------------
# One record block that did not exist below this rung -- rawdecl -- a FOURTH
# FILE in the smoke tree, a second and a third compile of that tree under
# different flags, and NO NEW CUT.
#
# NO NEW CUT IS THE STRUCTURAL POINT, and it falls out of the third file rather
# than having to be arranged. flatten() writes Smoke.java and Lib.java and
# nothing else, so a file it has never heard of is off all eight cut trees for
# free: Raw.java is absent from every one of them the way Mod.java is, and
# every figure below the cut banner is the figure step 12 measured, unmoved.
# The two absences are asserted in flatten() itself rather than trusted.
#
# THE FOURTH FILE IS C-3's, AND IT IS SEPARATE FOR A DIFFERENT REASON THAN
# C-2's WAS. Mod.java is its own file because an import declaration sits above
# every section marker and cannot be cut. Raw.java is its own file because what
# it holds is a class IMPLEMENTING A LIBRARY INTERFACE: the forced half of
# rawdecl is decided by reading org.testng.IAnnotationTransformer out of
# testng-5.14.6.jar on the compile classpath, which is a claim about the shelf
# and the gate's SRCCP together. Nine raw declarations inside the file that
# eight sed addresses run over would have been the one place on this tree where
# an accident is expensive.
#
# THREE WHOLE-TREE FIGURES MOVE AND ALL THREE ARE IN THE ENV:
#   javac.sources    3 ->  4   the file itself
#   javac.classes   53 -> 56   Raw, Source and RawSub: three top-level types
#   javac.warnings  10 -> 20   ten more [rawtypes], which is the cross-check
# Three iface.* counters rise with them -- abstract, decl and sam -- because
# Source and Comparator are interfaces and one of them is functional. No line
# in this file asserts iface.* on the WHOLE tree; step 09's block reads those
# three on the step-10 CUT, where Raw.java is not, so they are untouched where
# they are read. That was measured over the full report, key by key, and
# rawdecl.* and iface.* were the only families that moved.
#
# AND THE COMPILES ARE THE FLAG. GATE_LINT is set once, near the top, out of
# baseline/target.txt, so the three probes this file has always made now run
# under the scorer's own flags -- which is what test.sh does, in that order.
# The block above the cuts adds two more compiles of the same tree, one with
# the flag empty and one with the -deprecation carve-out off, and asserts that
# the plugin's report is the SAME BYTES in all three and that javac.warnings is
# the only env key that moves. C-2's channel and C-3's channel come out of one
# compile and are independent, and that is where it is measured.
#
# THE SECTIONS BELOW ARE HISTORY. They are the notes steps 12, 11, 10, 09, 07
# and 06 wrote about their own versions of this file, kept because the blocks
# they describe are still here and still asserted; where they speak in the
# present tense about the probes, the paragraphs above are what holds now.
#
# WHAT STEP 12 ADDED TO STEP 11'S VERSION OF THIS FILE
# ---------------------------------------------------
# Four record blocks that did not exist below this rung -- modimport, jdkimport,
# scopedvalue and threadlocal -- a THIRD FILE in the smoke tree, an eighth cut,
# and a shift in the probes that is three levels wide rather than one.
#
# THE THIRD FILE IS THE STRUCTURAL CHANGE, AND C-2 FORCES IT. Every cut below
# is one sed address over Smoke.java -- from a section marker to the end of the
# file -- and C-2's construct is an IMPORT DECLARATION, which sits above every
# marker there is. A step-12 section written into Smoke.java would survive its
# own cut, and the step-12 cut would read modimport.size=1 where it has to read
# 0. Mod.java solves that without a line of new cut logic: flatten() writes only
# the files it knows about, so a file it has never heard of is absent from all
# eight cut trees for free. It is also why the step-12 cut asserts
# javac.sources=2 -- the file count is what proves the file was excluded, rather
# than merely unmentioned.
#
# AND IT IS WHY flatten() GREW A THIRD MODE. `yes` cuts Lib.java at the same
# marker and `no` drops it; this rung needs Lib.java WHOLE, because step 12
# appends nothing to it and a cut at a marker Lib.java does not carry would trip
# flatten()'s own "nothing was cut" guard. `whole` is that mode, and the guard
# stays on for the two modes that cut.
#
# THE SECOND PROBE IS A REFUSAL AT 24, AND IT IS THE NARROWEST ANY RUNG HAS
# DRAWN -- one error, from one declaration, in the file that exists to carry it.
# The level is DERIVED and not decremented: four of this rung's five legs move
# no source level at all. ScopedValue is a class in java.lang and binding one is
# an ordinary method call, so C-4 moves none; -XX:+UseCompactObjectHeaders is an
# argument javac never sees, so C-5 moves none; the class-file major is a
# property of the emitted artifact rather than of what javac will parse, so C-1
# moves none; and C-3's holds are ordinary import declarations that have been
# legal since Java 1. What is left is C-2's module import declaration, which JEP
# 511 made standard in 25 after three releases of preview. So 24 is the one
# level at which exactly one of the five legs stops compiling, and the assertion
# is on javac's own wording -- `module imports are not supported in -source 24`
# -- counted, so that a second error of any kind is a broken smoke tree rather
# than a stricter compiler.
#
# THE RETIRED LIST GREW BY TWO AND THE LEVELS JUMPED BY THREE. Step 11 retired
# `ok20` alone, because `srcgate 21 21` was still a call test.sh made with the
# opposite expected answer. Here BOTH of step 11's spellings go: this is the
# last rung and its level is the target itself, so 22 and 21 are gone and 23 was
# never a level any scorer in this task asked about. The list is still a list of
# SPELLINGS rather than of levels, for the reason step 11 wrote down, and this
# rung is simply the one where every spelling on it happens to have retired.
#
# AND THE STEP-12 CUT CLOSES THE LOOP FROM THE OTHER SIDE, with one difference
# from every cut below it that has to be said out loud. On every rung so far the
# cut's claim was "the new counters all read zero". Here three of the four do
# and THE FOURTH DOES NOT: jdkimport reads 37 on the cut tree, because step 11's
# tree already had 37 ordinary JDK imports in it and this rung's scanner counts
# them wherever it finds them. So the step-12 cut asserts modimport 0,
# scopedvalue 0, threadlocal 0 and jdkimport 37 -- three read zero and the
# fourth reads 37 -- and a rehearsal that asserted four zeroes would fail on a
# correct tree. That is the whole reason the cut is written out rather than
# looped over a list of keys.
#
# AND THE THREE FIGURES THIS RUNG MOVED BELOW ITSELF ARE ASSERTED ON BOTH
# TREES. Mod.java writes `java.util.ServiceLoader` and `Provider` in its
# providerTypes(), so svc.platformtype, svc.providertype and svc.files all rise
# on the whole tree -- 7, 3 and 3 where step 11 measured 6, 2 and 2. The svc
# block below asserts the new figures and the step-12 cut asserts the old ones,
# which is step 11's own treatment of try.size and var.candidates applied to the
# three counters this rung moves. None of the three is scored by any gate in
# test.sh -- B-15 buckets legacyscan, loadclass and platformload and nothing
# else, and the block below still asserts that -- so what moved is a
# measurement and not a leg.
#
# (Measured in authoring and recorded in NOTES rather than asserted here,
# because this file has no copy of step 11's tree to compare against: the
# step-12 cut's report is byte-identical to the report this same scanner
# produces over step 11's smoke tree. The tables below are that comparison
# written out key by key, which is the part a rehearsal can make.)
#
# WHAT STEP 11 ADDED TO STEP 10'S VERSION OF THIS FILE
# ---------------------------------------------------
# Three record blocks that did not exist below this rung -- priv, catchparam
# and varhandle -- a seventh cut, and the same one-level shift in the probes
# that each of the last two rungs made.
#
# THE THREE ARE NEW TO THE PLUGIN AND NOT ONLY TO THIS FILE, as step 10's four
# were: they are methods that did not exist in the jar the rung below built, so
# until this section runs there is nothing in the image that has ever driven
# them over a compiler. The reports under fixtures/c/ carry all three and say
# whatever they were written to say.
#
# THE SECOND PROBE IS A REFUSAL AT 21, and it is the narrowest refusal any rung
# has had. Step 09's was `sealed` at 16 and step 10's was the pattern `switch`
# at 20; this rung's is the unnamed variable of JEP 456, preview in 21 and
# standard in 22, and it is drawn by ONE construct in the whole smoke tree --
# the `catch (IllegalStateException _)` in catchUnnamed(). Everything else the
# rung reads is older than the tree being migrated: AccessController has been
# there since 1.2, multi-catch since 7, MethodHandles.privateLookupIn since 9.
# So the refusal is one error, javac's own wording is asserted, and a second
# error of any kind is a broken smoke tree rather than a stricter one.
#
# AND THE CALL THAT DID NOT RETIRE. The retired list grew by a pair a rung
# through step 10 because each rung's own level moved off the list of levels
# the scorer asks about. This rung retires `ok20` ALONE. `srcgate 21 21` is
# still a call test.sh makes -- with the opposite expected answer, `no` where
# step 10 wanted `yes` -- so retiring it by level would delete an assertion the
# scorer still depends on. The list is a list of SPELLINGS for that reason, and
# this is the rung that makes the distinction load bearing rather than tidy.
#
# AND THE STEP-11 CUT CLOSES THE LOOP FROM THE OTHER SIDE. The whole tree
# refuses 21; the same tree without this rung's section compiles at 21 clean,
# at step 10's own class count, with step 10's four record blocks reading step
# 10's own whole-tree figures and all three of this rung's reading nothing at
# all. Three counters DID move that step 10 never mentions -- try.size,
# var.candidates and var.candidatemethods, because this section writes eight
# try statements and five locals -- and the cut asserts those at step 10's
# values too, so the movement is recorded rather than discovered later by
# somebody reconciling it with an edit.
#
# WHAT STEP 10 ADDED TO STEP 09'S VERSION OF THIS FILE
# ---------------------------------------------------
# Four record blocks that did not exist below this rung -- reftest, seqfirst,
# seqnew and swpat -- a sixth cut, and the same structural change to the probes
# that step 09 made one level up.
#
# THE FOUR ARE NEW TO THE PLUGIN AND NOT ONLY TO THIS FILE. Every block step 09
# added counted something SourceScan could already see; these four are methods
# that did not exist in the jar the rung below built, so until this section runs
# there is nothing in the image that has ever driven them over a compiler. The
# reports under fixtures/c/ carry all four and say whatever they were written to
# say, which is the whole of why this file exists.
#
# THE SECOND PROBE IS A REFUSAL AT 20. Step 09's was a refusal at 16, because
# `sealed` was preview in 15 and 16 and standard in 17. This rung's is the same
# shape one level up: the pattern `switch` of JEP 441 was preview from 17
# through 20 and standard in 21, so a tree that has done C-3's work cannot
# compile at -source 20. The other three constructs cannot make that statement
# and are not asked to -- `getFirst`, `getLast` and `reversed` are METHODS, and
# a method is not a language level, while `Class.isInstance` has been there
# since 1.1. So the refusal names the pattern switch and nothing else, and the
# block at the probes asserts javac's own wording in both of its spellings:
# `patterns in switch statements`, once per file, and `deconstruction patterns`
# for the record pattern, which javac reports as a feature of its own.
#
# AND THE STEP-10 CUT CLOSES THE LOOP FROM THE OTHER SIDE, as the step-09 cut
# does one rung down. The whole tree refuses 20; the same tree without this
# rung's section compiles at 20 clean, at step 09's own class count, with step
# 09's own five record blocks reading step 09's own whole-tree figures and all
# four of this rung's reading nothing at all. That last clause is where C-5's
# numbers now live: a pattern switch IS a switch, so switch.size, switch.arrow
# and switch.expr all rise on the whole tree, and the figures step 09 froze are
# asserted on the cut where they are still the truth.
#
# WHAT STEP 09 ADDED TO STEP 08'S VERSION OF THIS FILE
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
# arqgate.Rewrite is the golden solution's own editor, and this guard is the one
# that would fail SILENTLY without it. build.sh compiles Rewrite.java only when
# the OpenRewrite runtime is staged beside it and prints `skipping arqrewrite.jar`
# otherwise, so a staging line that pulled the source in would build green and
# ship the editor's source into a verifier the candidate reads. The assertion
# costs one line. Plan.java above is the same argument for step 04's emitter.
[ ! -f "${TESTS}/gates/src/arqgate/Rewrite.java" ] \
   || fail "Rewrite.java is staged into the verifier -- that source is the answer key"
[ -f "${SMOKE}/mod/src/main/java/smoke/Smoke.java" ] \
   || fail "the smoke tree is not staged at ${SMOKE}"
[ -f "${SMOKE}/mod/src/main/java/smoke/Lib.java" ] \
   || fail "the smoke tree's SECOND file is not staged -- every file-scoped key would read 1"
# AND THE THIRD, WHICH IS THIS RUNG'S. Mod.java carries the whole of C-2 and
# every jdkimport reading the other two files cannot produce -- the nested
# import, the static import, the on-demand import and the ambiguous one. Its
# absence would leave modimport.size at 0 on a tree the block above has just
# compiled at 25, which reads as a scanner that cannot see module imports and is
# in fact a staging line that was not written.
[ -f "${SMOKE}/mod/src/main/java/smoke/Mod.java" ] \
   || fail "the smoke tree's THIRD file is not staged -- C-2 has no construct anywhere on this tree"
[ -f "${SMOKE}/mod/src/main/java/smoke/Raw.java" ] \
   || fail "the smoke tree's FOURTH file is not staged -- C-3 has no forced declaration anywhere on this tree"

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
# a rehearsal that compiled at 24 while test.sh had moved to 25 would be green
# and would be measuring a different instrument.
#
# The RETIRED spellings are asserted absent. A rehearsal that went on driving a
# probe the scorer had given up would be rehearsing a scorer that no longer
# exists, and the failure would arrive as a green run rather than a red one.
# `srcgate 8 8` is step 06's, `srcgate 11 11` is step 08's, `srcgate 17 17` and
# `srcgate 16 16` are step 09's, `srcgate 20 20` is step 10's, and `srcgate
# 22 22` and `srcgate 21 21` are step 11's and are both gone as of this rung.
# All seven survive further down as the rehearsal's OWN probes over the cuts,
# which is a different claim about a different tree and is labelled as one.
#
# THE LIST GREW BY TWO, WHICH STEP 11 SAID WOULD HAPPEN AND SAID WHY. Step 11
# retired `ok20` alone and kept `ok21` on the live list, because test.sh still
# made that call with the opposite expected answer -- so the list had to be a
# list of spellings rather than of levels. Here the rung's own level is 25, the
# target itself, and BOTH of step 11's spellings retire together. 23 never
# appears on either list: no scorer in this task ever asked for it, and a
# retired list that tried to be a range rather than a set would name it.
#
# THE CONTROL IS ASSERTED BY ITS OWN SPELLING AND NOT BY ITS LEVEL, for the same
# reason it was one rung down. test.sh publishes ok24 as a control that nothing
# scores, and a control the scorer had quietly stopped making would leave this
# rehearsal driving the only probe that still existed and calling the pair
# agreed.
for call in 'ok25=$(srcgate 25 25)' 'ok24=$(srcgate 24 24)' 'okdef=$(srcgate def "")'; do
   grep -qF "${call}" "${TESTS}/test.sh" \
      || fail "test.sh no longer contains the call this rehearsal drives: ${call}"
done
for gone in 'ok8=$(srcgate 8 8)' 'ok11=$(srcgate 11 11)' \
            'ok17=$(srcgate 17 17)' 'ok16=$(srcgate 16 16)' \
            'ok20=$(srcgate 20 20)' 'ok22=$(srcgate 22 22)' \
            'ok21=$(srcgate 21 21)'; do
   grep -qF "${gone}" "${TESTS}/test.sh" \
      && fail "test.sh still makes a retired probe: ${gone} -- this rung's pair is 25 and 24"
done

# ------------------------------------------------ this rung's flags, set ONCE
#
# GATE_LINT is step 13's one addition to the gate's command line, and test.sh
# sets it at the top, before the first probe it makes. So does this file, from
# the same string in the same baseline: a rehearsal that compiled the whole
# tree bare and then bolted a lint probe on the side would be driving a compile
# the scorer never makes and calling it the scorer's.
#
# LIFTED AND THEN ASSERTED, which is the pair every other extraction in this
# file uses. target.w.lint is what the golden was measured under, and a
# baseline that had quietly lost the -deprecation carve-out would move every
# warning count in this file and in the score at once. What the carve-out is
# worth on this tree is measured further down, in the block above the cuts.
GATE_LINT=$(sed -n 's/^target\.w\.lint=//p' "${TESTS}/baseline/target.txt" | head -1)
[ -n "${GATE_LINT}" ] \
   || fail "baseline/target.txt has no target.w.lint -- this rung's compile has no flags"
[ "${GATE_LINT}" = '-Xlint:all -Xlint:-deprecation' ] \
   || fail "target.w.lint is '${GATE_LINT}', expected '-Xlint:all -Xlint:-deprecation'"
lint_call="GATE_LINT=\$(tv 'target\\.w\\.lint')"
grep -qF "${lint_call}" "${TESTS}/test.sh" \
   || fail "test.sh no longer reads GATE_LINT out of target.w.lint"
grep -qF 'GATE_LINT="${GATE_LINT:-}"' "${TESTS}/test.sh" \
   || fail "srcgate() no longer passes GATE_LINT through -- the probes below would compile bare"

# AND IT IS ADDITIVE, asserted on the gate's own text. Every rung below this
# one runs source-gate.sh with GATE_LINT unset, and the fallback is what keeps
# their javac command the bytes it was. A gate that had made this rung's flags
# unconditional would change twelve closed rungs, and this is the line that
# notices.
grep -qF '${GATE_LINT:--nowarn}' "${TESTS}/gates/source-gate.sh" \
   || fail "source-gate.sh no longer falls back to -nowarn -- GATE_LINT is not additive any more"
printf '    %-38s %s\n' 'GATE_LINT' "${GATE_LINT}"

# ------------------------------------------- the three probes, on the WHOLE tree
# `|| true` on every srcgate call, and not decoration. srcgate ends in
# `sed ... | head -1`, whose status is 2 when source-gate.sh exited before
# writing the .env -- which is exactly the interesting failure. test.sh runs
# under `set -u` alone (rule 8: it must always reach report()), so there it
# lands as an empty ok and reads as "no". Here, under -e and pipefail, the bare
# assignment would end the rehearsal on the assignment line and never print
# which probe failed.
#
# THE SECOND PROBE IS A REFUSAL, three levels up from where step 11 put it and
# for a reason that had to be derived rather than decremented. Steps 03 through
# 06 each had a level the tree must REFUSE and the refusal was the fixture;
# steps 07 and 08 gave it up, because a namespace move, `toArray(T[]::new)`,
# `java.net.http` and a manifest attribute are all library and a library cannot
# be refused by a language level; steps 09, 10 and 11 got it back with sealed,
# the pattern switch and the unnamed variable.
#
# This rung has FIVE legs and only one of them can refuse anything. C-1 is the
# class-file major, a property of the artifact javac emits rather than of what
# javac will parse. C-4 binds a ScopedValue, which is a class in java.lang and
# an ordinary method call at every site. C-5 passes -XX:+UseCompactObjectHeaders
# to a JVM, an argument javac never sees. C-3's held imports have been legal
# since Java 1. What is left is C-2's `import module java.base`, standard in 25
# under JEP 511 after three releases of preview -- so 24 is the only level at
# which this tree can say no, and the refusal is C-2's alone by construction
# rather than by luck.
#
# WHICH IS ALSO WHY THE REFUSAL CANNOT BE READ AS A PROXY FOR THE RUNG. ok24
# says the module imports arrived. It says nothing at all about C-3, C-4 or
# C-5, and test.sh publishes it as a control that nothing scores for exactly
# that reason. The step-12 cut further down closes the loop from the other
# side: the same tree without Mod.java and without this rung's section compiles
# at 24 clean.
#
# The agreement on the bytes stays on the pair that can still make it -- this
# rung's level and the image default, which are the two srcgate()'s own
# fallback chooses between.
ok25=$(srcgate 25 25 || true)
ok24=$(srcgate 24 24 || true)
okdef=$(srcgate def "" || true)
[ "${ok25}" = yes ] || {
   echo "--- javac at -source 25 ---" >&2; tail -40 "${WORK}/src-25.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at -source 25 (source.ok=${ok25})"
}
[ "${ok24}" = no ] \
   || fail "the smoke tree compiled at -source 24 (source.ok=${ok24}) -- its module import is gone"
# The reason, and not just the refusal. ONE message from ONE declaration:
# `module imports are not supported in -source 24`, drawn by the single
# `import module java.base` at the top of Mod.java. Narrower than step 11's one
# error from one catch parameter only in that the construct is an import, which
# is the whole reason Mod.java exists -- and the narrowness is the point. There
# is exactly one place on this tree where the level can be felt, so a count of
# anything other than 1 means either that the declaration is gone or that
# something else on the tree has quietly stopped compiling one level down.
#
# THE MESSAGE AND THE TOTAL ARE BOTH ASSERTED, for that second reason. A count
# of the wording alone would stay at 1 while a later edit added an unrelated
# error beside it, and javac.errors further down is what closes that.
mi_err=$(grep -c 'module imports are not supported in -source 24' \
   "${WORK}/src-24.txt.javac.log" 2>/dev/null || true)
[ "${mi_err}" = 1 ] || {
   echo "--- javac at -source 24 ---" >&2; tail -20 "${WORK}/src-24.txt.javac.log" >&2 || :
   fail "the -source 24 refusal names ${mi_err} module import(s), expected 1 -- the refusal is not C-2's"
}
# AND THE FILE IT NAMES. Step 11's refusal could not say this and did not try:
# its construct was one line in a file full of other things. This rung's is the
# first line of code in a file that exists for it, so the refusal can be pinned
# to Mod.java by name -- and a refusal that named Smoke.java or Lib.java would
# be a tree where something else had acquired a 25-ism.
grep -q 'Mod\.java:[0-9]*: error: module imports are not supported in -source 24' \
   "${WORK}/src-24.txt.javac.log" \
   || fail "the -source 24 refusal does not name Mod.java -- something other than C-2 needs 25"
printf '    %-30s %s\n' 'the -source 24 refusal' \
   'one error, in Mod.java, naming the module import'
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

echo "--- the -source 25 compile, on the whole tree"
eq 'javac.rc'      "$(envval javac.rc      "${WORK}/src-25.txt.env")" 0  'a clean compile'
eq 'javac.level'   "$(envval javac.level   "${WORK}/src-25.txt.env")" 25 'at this rung, and not at the image default'
eq 'javac.errors'  "$(envval javac.errors  "${WORK}/src-25.txt.env")" 0  'and no errors'
eq 'javac.sources' "$(envval javac.sources "${WORK}/src-25.txt.env")" 4  'FOUR files under */src/main/java -- Raw.java is the one this rung adds'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${WORK}/src-25.txt.env")" "${shelf}" \
   'SRCCP is the whole staged shelf -- 78 testlib jars and the harness'
eq 'javac.classes' "$(envval javac.classes "${WORK}/src-25.txt.env")" 56 \
   'and every nested type was written -- 53 one rung down, plus Raw, Source and RawSub'

# The -source 24 refusal wrote no class files and is asserted to have written
# none. A report that came back clean here with javac.classes on it would mean
# the level was not actually applied, and the ok24=no above would be measuring
# something other than the level.
#
# ZERO AND NOT A PARTIAL COUNT, which is a difference worth naming because the
# -source 8 probe further down writes 12 of 25. javac attributes and emits what
# it can before giving up on an ERROR IN A BODY; a module import that will not
# parse at the level asked for is refused before any class is written at all.
eq 'javac.errors (-source 24)' "$(envval javac.errors "${WORK}/src-24.txt.env")" 1 \
   'the one module-import message, and nothing else'
eq 'javac.level (-source 24)' "$(envval javac.level "${WORK}/src-24.txt.env")" 24 \
   'the refused probe did run at the level it was asked for'
eq 'javac.classes (-source 24)' "$(envval javac.classes "${WORK}/src-24.txt.env")" 0 \
   'and wrote nothing: a refusal, not a warning'
eq 'javac.sources (-source 24)' "$(envval javac.sources "${WORK}/src-24.txt.env")" 4 \
   'over the same four files: the refusal is the level, not a shorter tree'

# The agreement, on the bytes, against the probe that can still make it.
# Compared as whole files rather than key by key, because naming the keys that
# must match would be naming the keys someone thought of. This is the pair
# test.sh's fallback chooses between -- `measured` is src-25.txt when the tree
# compiles at the rung and src-def.txt when it does not -- so the two reports
# being the same file is the whole of what makes the fallback safe.
eq 'javac.level (default probe)' "$(envval javac.level "${WORK}/src-def.txt.env")" default \
   'GATE_SOURCE empty means no -source flag at all'
cmp -s "${WORK}/src-25.txt" "${WORK}/src-def.txt" \
   || { diff "${WORK}/src-25.txt" "${WORK}/src-def.txt" | head -20 >&2
        fail "the default-level report differs from the -source 25 report -- the fallback reads a different tree"; }
printf '    %-30s %s\n' 'default vs 25' \
   'byte-identical: no counter family C reads depends on the level javac was asked for'

# ============================================= the record blocks, rung by rung
#
# Every block from here to the step-07 cut banner is asserted against the WHOLE
# tree's report, newest rung first: this rung's four, then step 11's three,
# then step 10's four, then step 09's five, then step 08's one, then step 07's
# three. Everything below the step-07 cut banner is step 06's
# rehearsal driven over the step-07 CUT, which is step 06's own tree -- so the
# figures above that banner are measured on the tree as it stands and the ones
# below it are figures a lower rung froze.
wval() { sed -n "s/^$1=//p" "${WORK}/src-25.txt" | head -1; }
wrecs() { sed -n "s/^$1\[[0-9]*\]=//p" "${WORK}/src-25.txt"; }
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


# =============================================== the step-12 record blocks
#
# Four blocks, and between them they were the whole of what family C read ONE
# RUNG DOWN out of the SOURCE report. At this rung they are B-19 -- step 12's
# family C, scored again under family B as the clause that says the last rung
# still holds -- and they are read through the same gate, off the same report,
# by the same counters. That is why they are still asserted here in full.
#
# STEP 13's OWN TWO ARE A SEPARATE BLOCK, further down, just above the cuts:
# `rawdecl`, which is all of what C-3 reads, and the warning count C-2 divides
# into. Step 12's C-2, C-3 and C-4 are the four blocks below.
#
# THREE OF THE FOUR READ ZERO ONE RUNG DOWN AND THE FOURTH DOES NOT. `jdkimport`
# has been counting ordinary JDK imports since step 09 and reads 37 on the
# step-12 cut, which is the figure the cut asserts further down. A section that
# assumed "new block, therefore zero below" would be asserting a bug into the
# only block on this rung whose population predates it.
#
# Each block asserts the totals, the records that make the totals mean what the
# gate reads them as, and the set of values the discriminating field takes. The
# discriminating fields are `name` for modimport, `kind` and `q` for jdkimport,
# and `form` for both C-4 blocks -- and every one of them is read by a gate.

# -------------------------------------------------------------- modimport
#
# C-2's channel, and the smallest block in this file: one record, because one
# module import is all a file can usefully have and one file is all this tree
# has. The block is short and the two things it says are both about SHAPE.
echo "--- modimport: one row, and the two fields C-2 takes out of it"
eq 'modimport.size'  "$(wval 'modimport\.size')"  1 'one module import, in Mod.java'
eq 'modimport.files' "$(wval 'modimport\.files')" 1 'and it is the only file with one'
whas modimport 'name=java.base' \
   'the module, which is the name C-2 matches against baseline/modimport.txt'
eq 'modimport name values' "$(wfieldset modimport name)" 'java.base' \
   'and it is the only module imported anywhere on this tree'

# THE FIRST FIELD IS <file>:<line> AND C-2 STRIPS THE LINE, which is the one
# piece of parsing the leg does and the one thing that would silently halve it.
# `sub(/:[0-9]+$/, "", f)` anchors at the END of the field, so a record whose
# path happened to contain a colon still keys on the file. Rehearsed by doing
# what the gate does and checking the pair that comes out, because a gate that
# stopped stripping would produce `Mod.java:18` as the file name, match nothing
# in the baseline, and score C-2 at zero on a correct tree.
mi_key=$(wrecs modimport \
   | awk '{ f = $1; sub(/:[0-9]+$/, "", f); nm = ""
            for (i = 2; i <= NF; i++) if (index($i, "name=") == 1) nm = substr($i, 6)
            if (nm != "") print f, nm }' | LC_ALL=C sort -u)
eq 'the (file, module) pair C-2 keys on' "${mi_key}" \
   'mod/src/main/java/smoke/Mod.java java.base' \
   'the line number stripped and the module kept'

# ------------------------------------------------------------- jdkimport
#
# C-3's channel, and the one block on this rung with a population older than
# the rung. Forty-two rows in three files, and the four `kind` values are the
# four answers the leg can give about a single import: `type` is the one a
# module import subsumes, and `static`, `nested` and `ondemand` are three of
# the four reasons one does not. The fourth reason is AMBIGUITY, which is not a
# kind but a number in the `ambig` field, and it is the seed's commonest hold.
echo "--- jdkimport: forty-two rows, four kinds, and one ambiguous name"
eq 'jdkimport.size'      "$(wval 'jdkimport\.size')"      42 'seven in Lib.java, thirty in Smoke.java, five in Mod.java'
eq 'jdkimport.files'     "$(wval 'jdkimport\.files')"      3 'all three: this is the block Mod.java did not have to itself'
eq 'jdkimport.type'      "$(wval 'jdkimport\.type')"      37 'thirty-seven plain single-type imports'
eq 'jdkimport.static'    "$(wval 'jdkimport\.static')"     3 'three static imports'
eq 'jdkimport.nested'    "$(wval 'jdkimport\.nested')"     1 'one nested type'
eq 'jdkimport.ondemand'  "$(wval 'jdkimport\.ondemand')"   1 'and one on-demand'
eq 'jdkimport.ambiguous' "$(wval 'jdkimport\.ambiguous')"  1 'one name the module import cannot supply'
# 37 + 3 + 1 + 1 = 42, ASSERTED AND NOT LEFT TO THE READER. The four kinds are
# counted in four separate branches of one if/else chain, so a row that fell
# through all four would be in `size` and in no kind, and every eq above would
# still pass. This is the sum that catches it.
kindsum=$(( $(wval 'jdkimport\.type') + $(wval 'jdkimport\.static') \
          + $(wval 'jdkimport\.nested') + $(wval 'jdkimport\.ondemand') ))
eq 'the four kinds, summed' "${kindsum}" 42 \
   'every row has exactly one kind: none fell through the chain'
eq 'jdkimport kind values' "$(wfieldset jdkimport kind)" 'nested,ondemand,static,type' \
   'all four, on one tree'
eq 'jdkimport mod values' "$(wfieldset jdkimport mod)" 'java.base' \
   'and every one of them resolves to java.base, which is the module Mod.java imports'

# THE FIVE ROWS OF MOD.JAVA ARE FIVE DIFFERENT ANSWERS, one per hold reason
# plus the departure that was not made. Every other row on this tree is the
# plain `type` shape; these five are the block's whole discriminating power.
whas jdkimport 'q=java.lang.annotation.Annotation pkg=java.lang.annotation mod=java.base kind=type simple=Annotation ambig=3' \
   'HELD, ambiguous: three packages java.base exports declare an Annotation'
whas jdkimport 'q=java.util.ServiceLoader.Provider pkg=java.util mod=java.base kind=nested simple=Provider ambig=0' \
   'HELD, nested: a module import supplies package members, not type members'
whas jdkimport 'q=java.lang.annotation.RetentionPolicy.RUNTIME pkg=java.lang.annotation mod=java.base kind=static simple=RUNTIME ambig=0' \
   'HELD, static: a static import names a member of a type'
whas jdkimport 'q=java.util.zip.* pkg=java.util.zip mod=java.base kind=ondemand simple=* ambig=0' \
   'ON DEMAND: subsumed in substance, a different declaration in form'
whas jdkimport 'mod/src/main/java/smoke/Mod.java:50 q=java.util.List pkg=java.util mod=java.base kind=type simple=List ambig=1' \
   'and one plain departure the migration did not make, which is what C-3 pays for'
# AMBIG IS A COUNT AND NOT A FLAG, and the three values it takes here say so.
# 0 is "the question was not asked" -- ambiguity() is only called for kind=type,
# so the static, nested and on-demand rows carry 0 and are held for their kind
# instead. 1 is "exactly one type with this simple name is visible", which is
# the subsumable case. 3 is Annotation. A reading that treated ambig as a
# boolean would hold all five of Mod.java's rows and pay C-3's departure half
# for none of the thirty-seven below them.
eq 'jdkimport ambig values' "$(wfieldset jdkimport ambig)" '0,1,3' \
   'not asked, asked and answered once, asked and answered three times'
amb0=$(wrecs jdkimport | grep -c ' ambig=0 ' || true)
eq 'rows carrying ambig=0' "${amb0}" 5 \
   'the three static, the one nested and the one on-demand -- every row ambiguity() was not asked about'

# THE NEAR MISS IS AN IMPORT THAT IS NOT IN THE REPORT AT ALL, which makes it
# the only near miss in this file asserted by ABSENCE of a row rather than by a
# row with the wrong field. Mod.java imports org.testng.annotations.Test and
# annotates a method with it; org.testng is on the classpath -- SRCCP stages
# testng-5.14.6.jar -- rather than in a module the JDK ships, so Jdk.packageOf()
# hands back null and imports() skips the declaration before deciding a kind.
#
# THIS IS THE SEED'S OWN SHAPE AND NOT A CONTRIVANCE. C-3's population is 495
# rows, and 171 files carry several thousand import declarations between them;
# the difference is org.jboss and org.testng and everything else off the module
# graph. A scanner that counted them would score C-3's departure half against a
# denominator no migration can move, and every figure above would still pass.
wno jdkimport 'org.testng' \
   'a classpath import is skipped, not classified: Jdk.packageOf() is null for it'
wno jdkimport 'kind=type simple=Test' \
   'and it is skipped before a kind is chosen, so it is not a held type either'
eq 'jdkimport pkg values' "$(wfieldset jdkimport pkg)" \
   'java.io,java.lang.annotation,java.lang.reflect,java.nio.file,java.security,java.time,java.util,java.util.stream,java.util.zip' \
   'nine packages, every one of them exported by java.base'

# C-3 KEYS ON (FILE, Q) AND NOT ON Q, and this tree is built so that the
# difference is visible. java.util.List is imported in all three files;
# ArrayList, HashMap, Map and Collectors in two each. Keyed on the pair there
# are forty-two; keyed on q alone there are thirty-six, and six of the rows a
# migration has to move would be invisible to the leg. Computed here the way
# the gate computes it, so that the `sort -u` in the gate is rehearsed and not
# merely read.
ji_pairs=$(wrecs jdkimport \
   | awk '{ f = $1; sub(/:[0-9]+$/, "", f); q = ""
            for (i = 2; i <= NF; i++) if (index($i, "q=") == 1) q = substr($i, 3)
            if (q != "") print f, q }' | LC_ALL=C sort -u | grep -c .)
ji_qonly=$(wrecs jdkimport \
   | awk '{ for (i = 2; i <= NF; i++) if (index($i, "q=") == 1) print substr($i, 3) }' \
   | LC_ALL=C sort -u | grep -c .)
eq 'the (file, q) pairs C-3 keys on' "${ji_pairs}" 42 \
   'every row survives the sort -u: no two rows are the same import in the same file'
eq 'the q values alone' "${ji_qonly}" 36 \
   'six fewer -- which is what the leg would lose if the file were dropped from the key'

# ------------------------------------------------------ scopedvalue / threadlocal
#
# C-4's two channels, and they are one leg because the migration is a
# REPLACEMENT: a ThreadLocal that survives is the thing the scoped value was
# supposed to displace. Both blocks carry the same two forms -- `decl` for the
# field declaration and `call` for every use -- and the gate reads the decls out
# of both and the calls out of one.
#
# THE GATE MATCHES form=decl WITH `(^| )form=decl( |$)` AND THE FIELD IS LAST ON
# THE LINE, which is why the `$` alternative is load-bearing rather than
# defensive. Every other record kind in this file puts `in=` last; these two put
# `form=` there, because the declaration records have no enclosing method and
# `in=-` reads better before the form than after it. A regex written
# `form=decl ` would match neither row and C-4 would read zero declarations on a
# tree that has two.
echo "--- scopedvalue and threadlocal: two blocks, one leg, and the form field"
eq 'scopedvalue.size'   "$(wval 'scopedvalue\.size')"   11 'one declaration and ten calls'
eq 'scopedvalue.decls'  "$(wval 'scopedvalue\.decls')"   1 'the one ScopedValue field'
eq 'scopedvalue.files'  "$(wval 'scopedvalue\.files')"   1 'all of it in Smoke.java'
eq 'threadlocal.size'   "$(wval 'threadlocal\.size')"    4 'one declaration and three calls'
eq 'threadlocal.decls'  "$(wval 'threadlocal\.decls')"   1 'the one ThreadLocal field that stayed'
eq 'threadlocal.files'  "$(wval 'threadlocal\.files')"   1 'in the same file'
# svcalls IS PUBLISHED AS size - decls AND NOT COUNTED, so the two numbers above
# are the whole of it. Ten here; a block that recorded the declaration as a call
# as well would publish eleven and the leg would pay for a use nobody wrote.
eq 'the calls C-4 derives' "$(( $(wval 'scopedvalue\.size') - $(wval 'scopedvalue\.decls') ))" 10 \
   'size minus decls, which is how gate.c4.svcalls is published'
whas scopedvalue 'name=REQUEST decl=FIELD mods=private,static,final in=- form=decl' \
   'the declaration, with form= last on the line'
whas threadlocal 'name=SAVED decl=FIELD mods=private,static,final in=- form=decl' \
   'and the one that did not migrate, the same shape'
# Rehearsed against the gate's own regex, on the whole block and not on the two
# rows above: a THIRD record that ended `form=declared` would be matched by a
# looser pattern and counted as a declaration.
ndecl=$(wrecs scopedvalue; wrecs threadlocal)
ndecl=$(printf '%s\n' "${ndecl}" | awk '$0 ~ /(^| )form=decl( |$)/ { n++ } END { print n + 0 }')
eq 'rows matching the gate own form=decl regex' "${ndecl}" 2 \
   'exactly the two declarations, matched at end of line'
eq 'scopedvalue form values' "$(wfieldset scopedvalue form)" 'call,decl' 'both forms'
eq 'threadlocal form values' "$(wfieldset threadlocal form)" 'call,decl' 'and both again'
# THE RECEIVER IS RECORDED AND THE CARRIER IS A RECEIVER OF ITS OWN. where()
# returns a ScopedValue.Carrier and run()/call() are called on IT, not on
# ScopedValue, so a channel scoped to one owner would record the where() and
# lose the run() that makes it an extent. Two owners, and both are asserted.
eq 'scopedvalue recv values' "$(wfieldset scopedvalue recv)" 'Carrier,ScopedValue' \
   'the value and the carrier where() hands back'
eq 'scopedvalue call values' "$(wfieldset scopedvalue call)" \
   'call,get,isBound,newInstance,orElse,run,where' \
   'seven methods: the factory, the two binders, the two runners and the three reads'
eq 'threadlocal call values' "$(wfieldset threadlocal call)" 'get,remove,set' \
   'and the three a ThreadLocal is actually used through'
whas scopedvalue 'form=call call=where recv=ScopedValue in=inExtent' 'the binder'
whas scopedvalue 'form=call call=run recv=Carrier in=inExtent' 'and the runner, on the carrier'
whas scopedvalue 'form=call call=call recv=Carrier in=computeInExtent' \
   'the value-returning runner, which is the one a Callable reaches'
whas scopedvalue 'form=call call=isBound recv=ScopedValue in=readExtent' \
   'the guarded read, which is the shape that has no ThreadLocal equivalent'
whas threadlocal 'form=call call=remove in=restore' \
   'and the remove() in a finally, which is what a ThreadLocal needs and a scoped value does not'
# THE BUCKETS, counted. Three methods use the scoped value and two the thread
# local, and the declarations are in neither because a field has no enclosing
# method -- which is the `in=-` above, and the reason both blocks' `in` sets
# carry the dash.
winn scopedvalue inExtent        3 'where, run and the get inside the extent'
winn scopedvalue readExtent      3 'isBound, orElse and get: the three ways out'
winn scopedvalue computeInExtent 3 'where, call and get again, for the value-returning form'
winn threadlocal register        1 'the set'
winn threadlocal restore         2 'the get and the remove'
eq 'scopedvalue in values' "$(wfieldset scopedvalue in)" '-,computeInExtent,inExtent,readExtent' \
   'three methods and the field'
eq 'threadlocal in values' "$(wfieldset threadlocal in)" '-,register,restore' \
   'two methods and the field'

# =============================================== the step-11 record blocks
#
# Three blocks, and they are the whole of what family C reads at this rung that
# step 10 did not. All three read zero on every tree below this one -- the
# step-11 cut further down is where that is asserted -- and all three are new
# METHODS in the plugin rather than new readings of an old one, so nothing in
# the image has ever driven them over a compiler before this section runs.
#
# Each block asserts the same three things the blocks below it do: the totals,
# the records that make the totals mean what the gate reads them as, and the
# set of values the discriminating field takes. The discriminating fields at
# this rung are `form` and `name` for priv, `kind` for catchparam, and `call`
# for varhandle, and every one of them is read by a gate.

# ------------------------------------------------------------------- priv
#
# C-2's channel, and the one that reaches two visitors. A privileged TYPE is
# counted from visitIdentifier and from visitMemberSelect both, because a
# fully-qualified use is a member select and never an identifier; a privileged
# CALL is counted in countLibrary off the method symbol's owner, which does not
# care how the receiver was spelled. The two spellings therefore have to produce
# the same three records, and propertySimple() and propertyQualified() are the
# pair that says so. Before this rung the qualified one produced ONE -- the call
# -- and C-2's departure half would have paid 47 of 67 for an edit that replaced
# imports with long names and moved nothing. NOTES 16.27.
echo "--- priv: one call form, four types, and the spelling that used to vanish"
eq 'priv.size'    "$(wval 'priv\.size')"    13 'twelve in Smoke.java, one in Lib.java'
eq 'priv.files'   "$(wval 'priv\.files')"    2 'in both files'
eq 'priv.methods' "$(wval 'priv\.methods')"  5 'in five buckets, two of which are the dash'
whas priv 'form=call name=doPrivileged in=propertySimple' \
   'the call, through the imported receiver'
whas priv 'form=typeref name=AccessController in=propertySimple' \
   'the receiver type, which is a typeref of its own and not part of the call record'
whas priv 'form=typeref name=PrivilegedAction in=propertySimple' \
   'and the action type'
whas priv 'form=call name=doPrivileged in=propertyQualified' \
   'the same call written java.security.AccessController.doPrivileged'
whas priv 'form=typeref name=AccessController in=propertyQualified' \
   'the same receiver type, recovered from a member select'
whas priv 'form=typeref name=PrivilegedAction in=propertyQualified' \
   'and the same action type: three records either way'
# THE TWO SPELLINGS ARE COUNTED AS A PAIR AND NOT AS TWO ROWS THAT HAPPEN TO
# EXIST. Both buckets hold exactly three, so a reading that recovered the call
# and dropped the types -- which is what visitIdentifier alone does -- would
# leave the qualified bucket at 1 while every whas above it still passed.
winn priv propertySimple    3 'one call and two types'
winn priv propertyQualified 3 'and the same three, spelled out in full'
# The enclosing selects `java.security` and `java` resolve to PACKAGES, not to
# TypeElements, and are passed over. A channel that counted every member select
# whose text started with the package would report five here.
eq 'priv name values' "$(wfieldset priv name)" \
   'AccessController,PrivilegedAction,PrivilegedActionException,PrivilegedExceptionAction,doPrivileged' \
   'all four privileged types and the one call, on one tree'
eq 'priv form values' "$(wfieldset priv form)" 'call,typeref' 'and both forms'
# THE CHECKED HALF, which is the only place the other two types appear and the
# only site that is a priv record and a catchparam record at the same line.
winn priv propertyChecked 4 'the call, the receiver, the action and the exception'
whas priv 'form=typeref name=PrivilegedExceptionAction in=propertyChecked' \
   'the checked action type'
whas priv 'form=typeref name=PrivilegedActionException in=propertyChecked' \
   'and the catch parameter, counted because a catch type is an identifier too'
# IN=- IS AN IMPLEMENTS CLAUSE and seven of the real tree's 67 rows are this
# shape: a class declaration has no enclosing method, so enclosingMethod() walks
# to the root without finding one.
whas priv 'form=typeref name=PrivilegedAction in=-' \
   'a named action class, in Smoke.java and again in Lib.java'
whas priv 'form=typeref name=PrivilegedExceptionAction in=-' \
   'and the checked one beside it'
# THE NEAR MISS IS AN OWNER MISS. Lib.java has a doPrivileged of its own and
# Slots#impostors calls it; the channel resolves java.security.AccessController
# before it counts, so the bucket is empty. This is the opposite trade from
# step 10's reftest and seqnew, whose look-alikes DO count, and the two
# sections read together are the record of which counters can read an owner.
wnoin priv impostors 'a doPrivileged on a type of this project own is not a privileged call'

# ------------------------------------------------------------- catchparam
#
# C-3's channel, and `kind` is the field the leg turns on. `unnamed` is what the
# rung is for; `read` and `unread` split the named ones, and only the unread are
# convertible -- a parameter the block mentions cannot become `_` without
# breaking the block that mentions it.
#
# READ IS DECIDED BY JAVAC'S ELEMENT AND NOT BY THE IDENTIFIER'S TEXT, which is
# what catchShadowed() is for: a local class inside the catch declares a field
# with the parameter's name and reads it, so a name-matching scanner calls the
# site read and this one calls it unread. Unread is the side C-3 pays for, so
# the name-matching reading would quietly stop paying for sites like it.
echo "--- catchparam: three kinds, and what read is decided by"
eq 'catchparam.size'    "$(wval 'catchparam\.size')"    8 'seven in Smoke.java, one in Lib.java'
eq 'catchparam.unnamed' "$(wval 'catchparam\.unnamed')" 1 'one `_`, and it is the whole of the -source 21 refusal'
eq 'catchparam.read'    "$(wval 'catchparam\.read')"    5 'five parameters the block mentions'
eq 'catchparam.unread'  "$(wval 'catchparam\.unread')"  2 'and two it does not, which are the convertible ones'
eq 'catchparam.files'   "$(wval 'catchparam\.files')"   2 'in both files'
eq 'catchparam.methods' "$(wval 'catchparam\.methods')" 8 'and in eight buckets: one catch each'
whas catchparam 'name=e kind=read type=IllegalStateException in=catchRead' \
   'named and mentioned'
whas catchparam 'name=e kind=unread type=IllegalStateException in=catchUnread' \
   'named and never mentioned: the shape C-3 converts'
whas catchparam 'name=_ kind=unnamed type=IllegalStateException in=catchUnnamed' \
   'and already converted'
whas catchparam 'name=e kind=unread type=IllegalStateException in=catchShadowed' \
   'the shadow: an identifier spelled e that resolves to a field of a local class'
whas catchparam 'name=e kind=read type=PrivilegedActionException in=propertyChecked' \
   'the catch that is also a priv typeref'
whas catchparam 'name=e kind=read type=NullPointerException in=label' \
   'and the second file'
eq 'catchparam kind values' "$(wfieldset catchparam kind)" 'read,unnamed,unread' \
   'all three readings, on one tree'
eq 'catchparam name values' "$(wfieldset catchparam name)" '_,e' \
   'and both spellings of the name field, the underscore standing for an empty one'
# THE UNION TYPE, and the claim it exists to make. A multi-catch writes SPACES
# inside type=, so the record for catchUnion() has more whitespace-separated
# fields than every other row. pbuckets() and cbuckets() find `in=` by scanning
# for the prefix rather than by counting fields -- substr($i, 1, 3) == "in=" --
# and winn below does the same, so a union row buckets like any other. A
# positional reading would put this site in a bucket named
# `IllegalArgumentException` and C-3 would pay for a method nobody has.
whas catchparam 'kind=read type=IllegalStateException | IllegalArgumentException in=catchUnion' \
   'the union, spelled with the pipe and the spaces javac prints'
winn catchparam catchUnion 1 \
   'and it still buckets: in= is found by prefix, not by position'
# IN=- IS A CATCH WITH NO ENCLOSING METHOD -- a static initializer is a block
# member of the class and not a MethodTree. The tree being migrated has no catch
# in this position; all 93 of its main-scope rows name a method. The fixture is
# here so the dash is a measured reading rather than a shape nobody ever put
# through the scanner.
whas catchparam 'name=e kind=read type=IllegalStateException in=-' \
   'a catch in a static initializer, bucketed under the dash'

# -------------------------------------------------------------- varhandle
#
# C-4's arrival half, and it counts TWO records per converted site:
# MethodHandles.privateLookupIn and Lookup.unreflectVarHandle are separate
# calls and are counted separately, which is why the golden tree reads
# varhandle.size=8 over four converted methods rather than 4. A leg that
# assumed one record per site would read the golden as half migrated.
echo "--- varhandle: two calls per arrival, and both owners resolved"
eq 'varhandle.size'    "$(wval 'varhandle\.size')"    4 'two in Smoke.java, two in Lib.java'
eq 'varhandle.arrival' "$(wval 'varhandle\.arrival')" 4 'and the counter agrees with the list it was bumped beside'
eq 'varhandle.files'   "$(wval 'varhandle\.files')"   2 'in both files'
eq 'varhandle.methods' "$(wval 'varhandle\.methods')" 2 'in two buckets: one arrival apiece, two records each'
whas varhandle 'form=arrival call=privateLookupIn in=readTicks'      'the lookup'
whas varhandle 'form=arrival call=unreflectVarHandle in=readTicks'   'and the handle it is used to make'
whas varhandle 'form=arrival call=privateLookupIn in=read'           'the second file'
whas varhandle 'form=arrival call=unreflectVarHandle in=read'        'and its pair'
winn varhandle readTicks 2 'two records, one site'
eq 'varhandle call values' "$(wfieldset varhandle call)" \
   'privateLookupIn,unreflectVarHandle' 'both halves of the arrival'
eq 'varhandle form values' "$(wfieldset varhandle form)" 'arrival' \
   'one form: this channel reads arrivals, and the departures it is paired with are step 06s setAccessible calls'
# BOTH OWNERS ARE RESOLVED, and Lib.java has look-alikes for both names. The
# second owner is a NESTED type -- java.lang.invoke.MethodHandles.Lookup -- so
# the check is on a qualified name with three dots in it and would be the first
# thing to break if the channel ever compared simple names.
wnoin varhandle impostors 'privateLookupIn and unreflectVarHandle on a type of this project own are not arrivals'

# =============================================== the step-10 record blocks
#
# Four blocks, and they are the whole of what family C read at STEP 10 that
# step 09 did not. All four read zero on every tree below step 10's section --
# the step-10 cut further down is where that is asserted -- and this rung holds
# them as B-17, so every figure below is a held one: a number that moved here
# would be a leg the candidate is scored on having changed under it.
#
# Each block asserts the same three things the blocks below it do: the totals,
# the records that make the totals mean what the gate reads them as, and the
# set of values the discriminating field takes. The discriminating fields at
# step 10 were `form` and `cast` for reftest, `guard` and `op` for seqfirst,
# `op` for seqnew, and `patterns`, `nullcase` and `deflt` for swpat, and every
# one of them is read by a gate.

# ---------------------------------------------------------------- reftest
#
# C-2's channel. Five spellings of a runtime type test written through
# java.lang.Class, and they are not interchangeable: only the two that have an
# INSTANCE to bind and a TYPE to name can become a type pattern, because there
# is no `instanceof` that takes a Class object on its right. `form` is what
# tells them apart and `cast` is what says whether the pattern would absorb
# anything, and a gate that counted sites would pay the same for all five.
echo "--- reftest: five spellings, and the two that can become a pattern"
eq 'reftest.size'    "$(wval 'reftest\.size')"    12 'ten in Smoke.java, two in Lib.java'
eq 'reftest.literal' "$(wval 'reftest\.literal')"  9 'nine written with a class LITERAL'
eq 'reftest.var'     "$(wval 'reftest\.var')"      3 'and three with a Class value, which name no type at all'
eq 'reftest.files'   "$(wval 'reftest\.files')"    2 'in both files'
eq 'reftest.methods' "$(wval 'reftest\.methods')" 10 'in ten buckets -- refChain() holds three of the twelve'
whas reftest 'form=litinstance type=java.lang.String sel=greeting recv=String.class cast=yes in=refLitInstance' \
   'the convertible shape: a class-literal test with a cast of the same expression after it'
whas reftest 'form=litinstance type=java.lang.Number sel=amount recv=Number.class cast=no in=refLitInstanceHeld' \
   'and the held shape, which has nothing to fold in'
whas reftest 'form=litassignget type=java.lang.Number sel=payload recv=Number.class cast=yes in=refLitAssignGetClassCast' \
   'isAssignableFrom(v.getClass()) recovers v as the subject, and Number.class.cast(v) counts as the cast'
whas reftest 'form=litassign type=java.lang.CharSequence sel=other recv=CharSequence.class cast=no in=refLitAssign' \
   'a class literal on the left and a Class VALUE on the right: a type, no instance, no pattern'
whas reftest 'form=varinstance type=- sel=value recv=kind cast=no in=refVarInstance' \
   'a Class value on the left: no type is named in the source at all'
whas reftest 'form=varassign type=- sel=sub recv=kind cast=no in=refVarAssign' \
   'and the same story on the other method name'
eq 'reftest form values' "$(wfieldset reftest form)" \
   'litassign,litassignget,litinstance,varassign,varinstance' 'all five spellings, on one tree'
eq 'reftest cast values' "$(wfieldset reftest cast)" 'no,yes' \
   'and both readings of the field C-2 pays on'
# THE CAST IS MATCHED ON THE RESOLVED TYPE AND NOT ON THE SPELLING. `String.class`
# and `(String)` are two different trees and both go through typeTextAt, so they
# meet at java.lang.String. A matcher that compared the written text would read
# cast=no on every site in a file that imports the type, which is every site in
# the real tree.
whas reftest 'type=java.lang.String' 'the class literal is reported by its RESOLVED name'
# THE SECOND CAST SPELLING IS COLLECTED BY A SCANNER OF ITS OWN, and
# refLitAssignGetClassCast() is the only fixture that reaches it: it writes
# `Number.class.cast(payload)` and no `(Number) payload` anywhere. Dropping that
# scanner would leave this one site at cast=no while every other cast=yes on the
# tree stayed green, which is the shape of regression a total cannot see.
winn reftest refLitAssignGetClassCast 1 'one site, and its cast is the reflective spelling'
# THE CHAIN, which is what C-3 groups: three tests on ONE selector in one
# if/else-if. The three rows do not share a cast= -- the Boolean arm has no cast
# after it -- so a grouping that read the first row and assumed the rest would
# describe this chain wrongly.
winn reftest refChain 3 'three sites in one bucket, which is what a chain looks like from here'
whas reftest 'form=litinstance type=java.lang.String sel=event recv=String.class cast=yes in=refChain'  'the first arm'
whas reftest 'form=litinstance type=java.lang.Integer sel=event recv=Integer.class cast=yes in=refChain' 'the second'
whas reftest 'form=litinstance type=java.lang.Boolean sel=event recv=Boolean.class cast=no in=refChain'  'and the third, which casts nothing'
# SEL= IS THE EXPRESSION TEXT AND TWO SPELLINGS ARE TWO SELECTORS.
# refShadowedSelector() tests `this.probe` and casts the PARAMETER of the same
# name: cast=no, because the two expressions print differently and resolve
# differently. A matcher that compared the trailing identifier would call this
# convertible and be wrong about a shadowed field, and a chain grouped on that
# reading would gain an arm that is not in the source.
whas reftest 'sel=this.probe' 'the field, written as the source writes it'
wno reftest 'sel=this.probe recv=String.class cast=yes' \
   'and it absorbs nothing: the cast is of `probe`, which is a different expression'
# THE VAR FORMS ARE NAME-SCOPED AND THE COST IS FROZEN HERE. `isInstance` on
# anything at all is a varinstance, and Lib.java has a Matcher of its own with a
# method of that name that has nothing to do with java.lang.Class. It cannot be
# otherwise -- a migrated call site keeps its receiver and changes its name, so
# the owner is the one thing that did not move -- and what scopes the GATE back
# to the real sites is baseline/reftest.txt, whose buckets name files and
# methods. Step 07 made the same trade for toarray and http.
whas reftest 'form=varinstance type=- sel=candidate recv=m cast=no in=matched' \
   'a look-alike in an unrelated class, counted, because the counter reads a name'

# --------------------------------------------------------------- seqfirst
#
# C-4's departure half: the two spellings this codebase uses to read a
# collection's first element. `guard` is the field the leg turns on, and it is
# syntactic on purpose -- get(0) on an empty list throws
# IndexOutOfBoundsException and getFirst() throws NoSuchElementException, so the
# rewrite preserves behaviour only where the list is provably non-empty, and an
# enclosing test on the SAME receiver is the evidence for that.
echo "--- seqfirst: two spellings, three guards, and what a guard is not"
eq 'seqfirst.size'    "$(wval 'seqfirst\.size')"    10 'eight in Smoke.java, two in Lib.java'
eq 'seqfirst.guarded' "$(wval 'seqfirst\.guarded')"  6 'six of the ten sit under an enclosing test on the receiver'
eq 'seqfirst.files'   "$(wval 'seqfirst\.files')"    2 'in both files'
eq 'seqfirst.methods' "$(wval 'seqfirst\.methods')" 10 'and in ten buckets: one site each'
whas seqfirst 'op=get0 recv=java.util.List expr=names guard=size in=firstGuardedBySize' \
   'the shape C-4 pays for: size() > 0 on the same receiver'
whas seqfirst 'op=get0 recv=java.util.List expr=labels guard=size in=firstGuardedByIsEmpty' \
   'and the other spelling of the same guarantee'
whas seqfirst 'op=get0 recv=java.util.List expr=tags guard=pred in=firstGuardedByPredicate' \
   'a null check is not an emptiness check, and is reported apart from one'
whas seqfirst 'op=get0 recv=java.util.List expr=parts guard=none in=firstUnguarded' \
   'and no enclosing if at all'
whas seqfirst 'op=iternext recv=java.util.Set expr=bag guard=size in=firstIteratorGuarded' \
   'iterator().next(), whose receiver is the collection and not the iterator'
whas seqfirst 'op=iternext recv=java.util.Set expr=pool guard=none in=firstIteratorUnguarded' \
   'and unguarded'
eq 'seqfirst op values'    "$(wfieldset seqfirst op)"    'get0,iternext'   'both spellings'
eq 'seqfirst guard values' "$(wfieldset seqfirst guard)" 'none,pred,size'  'and all three readings of the field the leg turns on'
eq 'seqfirst recv values'  "$(wfieldset seqfirst recv)"  'java.util.List,java.util.Set' \
   'recv is the RESOLVED erased type, which is what decides whether getFirst() exists'
# THE CORRELATED GUARD, and the sharpest row here: firstGuardedByACount() sits
# under `if (count > 0)`, which is a true statement about the list and is
# written about something else. guard is none, and it must be -- the correlation
# is not in the syntax, and a gate that accepted it would be asking an agent to
# have read another project's javadoc to know that a count and a collection move
# together.
whas seqfirst 'op=get0 recv=java.util.List expr=items guard=none in=firstGuardedByACount' \
   'a guard on a different expression is not a guard on this one'
# AND THE TWO COSTS OF READING THE GUARD AS TEXT, both frozen rather than
# discovered later. firstGuardedByALength() mentions the receiver and contains
# `.length`, so it reads as a size guard and guarantees nothing; Cursor.getFirst()
# in Lib.java is guarded by a CONDITIONAL EXPRESSION rather than an if, so it
# reads as none and is in fact safe. One direction each, which is what a three
# substring test costs. What keeps either from reaching a score is
# baseline/seqfirst.txt, which names the seven real sites and the reason for
# each.
whas seqfirst 'op=get0 recv=java.util.List expr=rows guard=size in=firstGuardedByALength' \
   'a guard that guarantees nothing, read as a size guard'
whas seqfirst 'op=get0 recv=java.util.List expr=backing guard=none in=getFirst' \
   'and a guarantee that is really there, read as none, because guardOn walks ifs'
# THE TWO NEAR MISSES, and neither leaves a record. get(1) is not a
# first-element read -- the argument is matched as the literal zero and not as
# any integer -- and an Iterator held in a VARIABLE is not
# `c.iterator().next()`, because the iterator() call is what names the
# collection a rewrite would have to reach.
wnoin seqfirst firstNearMisses 'get(1) and a variable iterator are both silent'

# ----------------------------------------------------------------- seqnew
#
# C-4's arrival half, and it exists because an absence is not an achievement.
# seqfirst records the OLD spelling, so a method that lost its get(0) reports
# the same thing whether the call was rewritten or the code was deleted; the
# baseline names the method that must ACQUIRE one of these, and deleting the
# call satisfies neither half.
echo "--- seqnew: the sequenced spellings, and the one that is not a read"
eq 'seqnew.size'    "$(wval 'seqnew\.size')"    5 'four in Smoke.java, one in Lib.java'
eq 'seqnew.read'    "$(wval 'seqnew\.read')"    4 'of which four are reads -- reversed() is a view'
eq 'seqnew.files'   "$(wval 'seqnew\.files')"   2 'in both files'
eq 'seqnew.methods' "$(wval 'seqnew\.methods')" 5 'and in five buckets'
whas seqnew 'op=getFirst recv=java.util.List expr=names in=sequencedFirst' 'the JEP 431 read C-4 asks for'
whas seqnew 'op=getLast recv=java.util.List expr=names in=sequencedLast'   'and its other end'
eq 'seqnew op values' "$(wfieldset seqnew op)" 'getFirst,getLast,reversed' 'all three'
# REVERSED IS COUNTED AND IS NOT A READ. It shares the interface and is not a
# first element, and a gate that summed the three would accept a reversed() in
# place of the getFirst() it asked for. The two scalars are what separate them
# and the difference is asserted rather than the pair.
whas seqnew 'op=reversed recv=java.util.List expr=names in=sequencedReversed' \
   'the view, recorded, and excluded from seqnew.read'
# WHAT THIS BLOCK DOES NOT CLAIM. Deque and LinkedList have carried getFirst()
# since Java 1.2, so a record here is a statement about the SPELLING and not
# about the platform level. The level is C-1's question, asked of the class
# files, and a rung that asked it twice in two instruments that can disagree
# would have a gate nobody can satisfy.
whas seqnew 'op=getFirst recv=java.util.LinkedList expr=queue in=sequencedOnDeque' \
   'a getFirst() that has been legal since 1.2, recorded exactly like the new ones'
# AND THE NAME SCOPE, with its cost and its near miss in one class. Cursor has a
# no-argument getFirst(), which is a record, and a one-argument overload of the
# same name, which is not: seqnew requires an empty argument list.
whas seqnew 'op=getFirst recv=smoke.Cursor expr=c in=cursorFirst' \
   'a look-alike on a type of this projects own, counted, because the counter reads a name'
# AND THE DISCRIMINATION IS BY ARITY AND NOT BY NAME. cursorFirst() calls
# `c.getFirst()` and `c.getFirst(1)` on the same receiver in one expression and
# exactly one of them is a record. A counter that matched the name alone would
# read two here, and on the real tree it would read every surviving
# `list.get(n)` that had been renamed as a sequenced access.
winn seqnew cursorFirst 1 'two getFirst calls in the bucket, one record'

# ------------------------------------------------------------------ swpat
#
# C-3's arrival half: the pattern switch of JEP 441, which is the only
# LANGUAGE construct this rung asks for and therefore the only one that can be
# refused by a -source level. `patterns` counts the bindings in the label
# region, which is not the number of cases -- a record pattern contributes one
# per component -- and `nullcase` is the field that says whether the rewrite
# kept the selector's null behaviour or changed it.
echo "--- swpat: the pattern switch, its bindings, and its null label"
eq 'swpat.size'     "$(wval 'swpat\.size')"     5  'four in Smoke.java, one in Lib.java'
eq 'swpat.patterns' "$(wval 'swpat\.patterns')" 11 'eleven bindings across the five, which is not five and not the case count'
eq 'swpat.nullcase' "$(wval 'swpat\.nullcase')"  1 'one of the five writes `case null`'
eq 'swpat.files'    "$(wval 'swpat\.files')"     2 'in both files'
eq 'swpat.methods'  "$(wval 'swpat\.methods')"   5 'and in five buckets'
whas swpat 'sel=event cases=3 patterns=2 nullcase=no deflt=yes types=String;Integer in=switchOverTypes' \
   'a pattern switch STATEMENT, naming refChains first two types in refChains order'
whas swpat 'sel=token cases=4 patterns=2 nullcase=yes deflt=yes types=String;Integer in=switchWithNullCase' \
   'and as an EXPRESSION, with the null label'
eq 'swpat nullcase values' "$(wfieldset swpat nullcase)" 'no,yes' 'both readings of the field that changes what the code means'
eq 'swpat deflt values'    "$(wfieldset swpat deflt)"    'no,yes' 'and both of the other one'
# CASE NULL IS THE HALF THAT CHANGES BEHAVIOUR. A switch without one throws
# NullPointerException on a null selector, exactly as every switch did before
# 21, so a rewrite that adds a null case where the if/else-if chain returned a
# default has changed the answer -- and one that omits it where the chain fell
# through to its last arm has too. The field is reported and the decision is in
# baseline/swids.txt, per site.
winn swpat switchWithNullCase 1 'the only switch on the tree with a null label'
# THE GUARD IS INSIDE THE LABEL REGION AND IS NOT A BINDING. switchGuarded() has
# `case Integer i when i.intValue() > 0` and a second `case Integer i` after it:
# two bindings of the same type, and the guard expression contributes none. A
# reader that ended the label region at the first `:` or `,` inside the guard
# would lose the binding that follows it.
whas swpat 'sel=measure cases=3 patterns=2 nullcase=no deflt=yes types=Integer;Integer in=switchGuarded' \
   'a guarded pattern and an unguarded one of the same type, counted twice'
# THE RECORD PATTERN IS THE ONLY ARM THAT CONTRIBUTES MORE THAN ONE BINDING, and
# switchOverSignal() is also the only switch on the tree that is exhaustive
# WITHOUT a default: Signal is sealed and all three of its subtypes are named.
# deflt=no is therefore a reading no other row takes, and patterns=4 over
# cases=3 is what says a deconstruction was counted per component.
whas swpat 'sel=signal cases=3 patterns=4 nullcase=no deflt=no types=Started;Stopped;String;int in=switchOverSignal' \
   'three cases, four bindings, no default -- a sealed selector needs none'
# AND THE SECOND FILE, so swpat.files is not the 1 a dropped path component also
# produces.
whas swpat 'sel=value cases=2 patterns=1 nullcase=no deflt=yes types=Number in=describe' \
   'one pattern and a default, in Lib.java'

# =============================================== the step-09 record blocks
#
# Five blocks, and at this rung they are B-16: step 09's family C, inherited
# whole except for the class-file leg that C-1 now asks at a higher major. The
# figures are step 09's own and they are RE-MEASURED here rather than copied,
# because the tree underneath them has grown a step-10 section since step 09
# froze them -- and three of the fifteen keys below moved when it did. The
# three are switch.size, switch.arrow and switch.expr, they moved because a
# pattern switch is a switch, and the step-10 cut further down asserts step
# 09's own six on step 09's own tree. Both readings are correct and the file
# holds them apart on purpose.
#
# None of the five was rehearsed below this line, and four of the five read
# zero on every tree below step 09 -- which is the shape of instrument that
# goes wrong quietly. ipattern is the exception and the more interesting case:
# it has read 4 on this tree since step 06, because a `instanceof` with a cast
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
#
# THE TOTALS MOVED AT THIS RUNG AND THE CORNERS DID NOT. A pattern switch is a
# switch, so the five swpat sites in the block above are also five `switch`
# records -- all five in arrow form, four of them expressions -- and step 09's
# 6/3/2 is 11/8/6 on the tree this rung ships. The colon count did not move,
# which is the reading that says the five are all arrows. Nothing here is a
# correction of step 09: the step-10 cut asserts 6/3/2 on step 09's own tree,
# and a file that stated only one of the two would leave nobody able to say
# whether a changed total was this rung's doing or a counter coming loose.
echo "--- switch: two fields, four corners, and this rung's five more"
eq 'switch.size'  "$(wval 'switch\.size')"  11 'the six step 09 measured, plus the five pattern switches this rung adds'
eq 'switch.arrow' "$(wval 'switch\.arrow')"  8 'three, plus all five of them'
eq 'switch.colon' "$(wval 'switch\.colon')"  3 'unchanged: one statement, one expression, and the empty one'
eq 'switch.expr'  "$(wval 'switch\.expr')"   6 'two, plus the four of this rungs five that are expressions'
eq 'switch.files' "$(wval 'switch\.files')"  2 'in both files -- C-5 is scored PER FILE, so this key is load bearing'
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
eq 'svc.platformtype' "$(wval 'svc\.platformtype')" 7 'six below this rung, and Mod.java names java.util.ServiceLoader once more'
eq 'svc.providertype' "$(wval 'svc\.providertype')" 3 'ServiceLoader.Provider, three times: twice below this rung and once in Mod.javas import'
eq 'svc.files'        "$(wval 'svc\.files')"        3 'all three -- SvcUser#real is what puts Lib.java in, and Mod.java arrives at this rung'
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
# ============================================== the step-13 record blocks
#
# Two, and they are family C's own: the warning count C-2 divides into, and
# `rawdecl`, which is the whole of what C-3 reads. Neither is a section in
# Smoke.java -- one is a flag on the command line and the other is a FOURTH
# FILE -- so neither reaches a cut tree and every figure below the cuts is the
# one step 12 froze.
#
# C-1 reads the class-file scan, C-4 reads suppress.txt and C-5 reads a probe's
# stdout, so those three legs have no block here and say so in their own words
# further up. Family C at this rung is five legs over two instruments, and this
# is the instrument.

# --------------------------------------------- GATE_LINT: the warning channel
#
# C-2 scores 1 - warnings/fixable and `warnings` is a count of javac's own
# console lines. It is the one number in this task the code under test can send
# to zero WITHOUT FIXING ANYTHING -- one annotation does it -- which is why
# test.sh binds C-2 to C-1 with min(). None of that is this file's business.
# This file's business is that the number exists, moves with the flag, is
# counted off the right stream, and does not touch the other channel.
echo "--- GATE_LINT: one tree, one plugin, three flag sets, three numbers"
[ "${TREE}" = "${SMOKE}" ] \
   || fail "the whole tree is no longer the tree in hand -- this block must run above the cuts"

# THE CONTRAST PROBE, and the $( ) around it is not decoration: a bare
# VAR=value prefix on a FUNCTION call is one of the two things bash does
# depending on POSIX mode -- it can outlive the call -- and a command
# substitution makes the answer the same in both.
oknolint=$(GATE_LINT="" srcgate nolint 25 || true)
[ "${oknolint}" = yes ] \
   || fail "the smoke tree does not compile at 25 without the lint flags (source.ok=${oknolint})"

# THREE AND NOT ZERO, WHICH IS A CORRECTION TO PROSE THIS LADDER CARRIED FOR A
# RUNG. source-gate.sh says beside the flag that the count is "reported as 0
# under the default -nowarn". It is not. -nowarn silences the OPTIONAL lints
# and leaves the MANDATORY ones, and [removal] is mandatory: Smoke.java's three
# java.security.AccessController uses have been reported at every rung in this
# ladder, under every flag set, since step 07 put them there. Nothing below
# step 13 READS javac.warnings, so nothing was ever scored on the wrong figure
# -- but a rung that inherited the sentence and asserted 0 would go red on a
# correct tree, and that is the failure this line exists to have already had.
eq 'javac.warnings (GATE_LINT unset)' \
   "$(envval javac.warnings "${WORK}/src-nolint.txt.env")" 3 \
   'three [removal] -- mandatory warnings are not lints and -nowarn does not reach them'
eq 'javac.warnings (this rung)' \
   "$(envval javac.warnings "${WORK}/src-25.txt.env")" 20 \
   'and this is what the rung asks for on this tree'

# THE PLUGIN'S REPORT IS THE SAME BYTES UNDER BOTH, which is the strongest
# thing this block can say and the reason C-3 cannot be silenced. The warning
# half is javac's console; the source half is the plugin's report; they come
# out of ONE compile and they are independent. A tree that annotated every
# warning away would take javac.warnings to 0 and would not move one row of
# rawdecl -- so a candidate cannot buy C-2 and C-3 with the same edit, and the
# one it can buy is the one min() has already bound to C-1.
cmp -s "${WORK}/src-25.txt" "${WORK}/src-nolint.txt" \
   || { diff "${WORK}/src-25.txt" "${WORK}/src-nolint.txt" | head -20 >&2
        fail "the plugin's report changed with the lint flags -- the source half must not depend on them"; }
envmoved=$(diff "${WORK}/src-25.txt.env" "${WORK}/src-nolint.txt.env" | grep -c '^[<>]' || true)
eq 'env lines that moved' "${envmoved}" 2 'javac.warnings, on both sides of the diff, and nothing else'
printf '    %-38s %s\n' 'report bytes under both flag sets' 'identical -- an annotation cannot reach C-3'

# THE FAMILIES, because the count alone would survive a flag string that had
# collapsed to one lint. Five on this tree, and the eleven [rawtypes] are the
# ones the block below cross-checks rawdecl against.
fams=$(grep -o ': warning: \[[a-z-]*\]' "${WORK}/src-25.txt.javac.log" \
         | sed -e 's/.*\[//' -e 's/\]//' | LC_ALL=C sort | uniq -c \
         | awk '{ print $2 "=" $1 }' | paste -sd, -)
eq 'the warning families' "${fams}" \
   'auxiliaryclass=4,fallthrough=1,rawtypes=11,removal=3,unchecked=1' \
   'five families summing to the 20 above'

# AND WHAT THE CARVE-OUT IS WORTH, measured rather than argued. -Xlint:all on
# its own reports 31 here: eleven [deprecation] rows that this rung's own
# string drops. The reason they are dropped is in test.sh beside the same flag
# -- the two dependency chains this ladder resolves put junit at 4.8.1 and at
# 4.13.2, the two versions deprecate different members, and a count that is
# legal at two values cannot be a baseline. Eleven is how much of C-2's
# numerator that decision is worth on a four-file tree; on the seed it is the
# difference between a scorable figure and a coin toss.
okdep=$(GATE_LINT="-Xlint:all" srcgate dep 25 || true)
[ "${okdep}" = yes ] \
   || fail "the smoke tree does not compile at 25 under -Xlint:all (source.ok=${okdep})"
eq 'javac.warnings (carve-out off)' \
   "$(envval javac.warnings "${WORK}/src-dep.txt.env")" 31 \
   'eleven more than the rung reads'
eq 'the rows the carve-out drops' \
   "$(grep -c ': warning: \[deprecation\]' "${WORK}/src-dep.txt.javac.log" || true)" 11 \
   'every one of them [deprecation], and the whole of the difference'

# ------------------------------------------------------- rawdecl: C-3's channel
#
# Eleven rows: nine in Raw.java, which exists for this block, and two that were
# already on the tree and are left where they are. The two matter more than
# their number. A block whose whole population lived in the file it arrived
# with would hold the FILE half of every bucket key constant, and rawdecl.files
# could not tell a correct key from one that had dropped the path -- which is
# the half capsum() needs, because the seed's heaviest open bucket holds 2 and
# its six open buckets are spread over five files.
echo "--- rawdecl: eleven declarations, three forced, seven buckets, two files"
eq 'rawdecl.size'    "$(wval 'rawdecl\.size')"    11 'nine in Raw.java, two in Smoke.java'
eq 'rawdecl.forced'  "$(wval 'rawdecl\.forced')"   3 'two parameters and one return type'
eq 'rawdecl.open'    "$(wval 'rawdecl\.open')"     8 'size minus forced -- the only one of the five C-3 scores'
eq 'rawdecl.files'   "$(wval 'rawdecl\.files')"    2 'Raw.java and Smoke.java'
eq 'rawdecl.methods' "$(wval 'rawdecl\.methods')"  7 'five buckets in Raw.java and two in Smoke.java'

# THE FORCED HALF IS DECIDED AGAINST A CLASS FILE ON THE SHELF, which is the
# one thing about this block no hand-written report could rehearse and the
# reason it is here rather than only in c-rehearsal.sh. Raw implements
# org.testng.IAnnotationTransformer out of testng-5.14.6.jar -- SRCCP is what
# puts it on the compile classpath -- and that interface declares
# transform(ITestAnnotation, Class, Constructor, Method). Class and Constructor
# are generic and written raw there, so an implementation cannot narrow them
# and go on overriding anything. The seed's own forced population is exactly
# this shape: four rows, two raw parameters on each of two transform methods.
whas rawdecl 'java.lang.Class as=param depth=0 forced=yes in=transform' \
   'imposed by the interface, at parameter 1'
whas rawdecl 'java.lang.reflect.Constructor as=param depth=0 forced=yes in=transform' \
   'and at parameter 2'
# AND THE NEAR MISS IS THE THIRD PARAMETER OF THE SAME METHOD. Method declares
# no type parameters at all, so it is not raw, so it is not a row. A rule keyed
# on "parameter of an overriding method" rather than on the type would have
# three here and would inflate the denominator C-3 divides by -- in the
# direction that makes a candidate's work look smaller than it is.
wno rawdecl 'java.lang.reflect.Method' \
   'not generic, so not raw, so not a declaration this counts'

# THE SAME DECISION MADE AGAINST A SOURCE SUPERTYPE, in the same round, because
# a forced-ness that only ever came out of a jar would not have been shown to
# read the tree it is compiling. Source#all() returns a raw java.util.List and
# overrides nothing, so it is open; Raw#all() overrides it and is forced.
#
# BOTH ROWS ARE in=all AND BOTH ARE IN Raw.java, SO THEY SHARE A BUCKET -- and
# that bucket is in the open set and in the forced set at once. It is the shape
# a bucketer that assumed a bucket belongs to one side or the other would get
# wrong, the seed has no bucket like it, and this tree has one on purpose.
whas rawdecl 'java.util.List as=return depth=0 forced=yes in=all' 'the override'
whas rawdecl 'java.util.List as=return depth=0 forced=no in=all'  'and the declaration it overrides'

# THE FIELDS, AND WHAT THE SIX WORDS declKind() HAS ARE DOING. Four of them
# appear here. `catch` is unreachable BY CONSTRUCTION -- a generic class may not
# extend Throwable, so an exception parameter's type can never declare type
# parameters and can never be raw -- and `resource` is reachable and is not on
# this tree: every construct that gets to it (a generic AutoCloseable, and a
# try-with-resources to open it in) would move a counter step 07 froze. Leaving
# it uncovered is a choice that costs nothing, and the reason it costs nothing
# is that NO GATE READS `as=` AT ALL: it is in the record so a human reading a
# recovery package can see what kind of declaration a row is, and C-3 buckets
# on the file and the method.
eq 'rawdecl as values'     "$(wfieldset rawdecl as)"     'field,local,param,return' \
   'four of declKind()'"'"'s six words'
eq 'rawdecl forced values' "$(wfieldset rawdecl forced)" 'no,yes' 'both halves populated'
eq 'rawdecl depth values'  "$(wfieldset rawdecl depth)"  '0,1' \
   'the top of a written type, and inside another type'"'"'s arguments'
eq 'rawdecl rows at depth 1' \
   "$(wrecs rawdecl | grep -c 'depth=1' || true)" 1 \
   'the raw List inside Map<String, List> -- a row a top-level-only walk would miss'
whas rawdecl 'java.util.List as=field depth=1 forced=no' 'and it is that field'
# AND ONE MORE NEAR MISS, of the kind every other block in this file has: a
# CAST to a raw type is not a declaration and is not a row. javac warns about
# it under [rawtypes] all the same, which is the first of the two directions
# the cross-check below goes in.
wnoin rawdecl nearMisses 'a cast to a raw type declares nothing'

# ------------------------------------------- the buckets, through C-3's own code
#
# cbuckets() is lifted out of test.sh the way SRCCP and srcgate() were, pointed
# at this tree's report, and driven with the two calls test.sh makes. Asserting
# the bucket keys against a copy of the function would be asserting the copy.
cb_src=$(sed -n '/^cbuckets() {$/,/^}$/p' "${TESTS}/test.sh")
printf '%s' "${cb_src}" | grep -qF 'cnt-' \
   || fail "could not extract cbuckets() from test.sh"
printf '%s' "${cb_src}" | grep -qF 'split($1, a, ":")' \
   || fail "the extracted cbuckets() does not split the site on its first colon"
eval "${cb_src}"
measured="${WORK}/src-25.txt"
for call in "cbuckets rawdecl rdopen   'forced=no'" "cbuckets rawdecl rdforced 'forced=yes'"; do
   grep -qF "${call}" "${TESTS}/test.sh" \
      || fail "test.sh no longer buckets rawdecl this way: ${call}"
done
eq 'cbuckets rawdecl calls in test.sh' \
   "$(grep -c 'cbuckets rawdecl' "${TESTS}/test.sh" || true)" 2 \
   'the open half and the forced half, and no third'
cbuckets rawdecl rdopen   'forced=no'
cbuckets rawdecl rdforced 'forced=yes'

# THE KEYS, WRITTEN OUT. Long on purpose: a bucket key is a path and a method
# name joined by '#', and the two ways to get it wrong -- dropping the path, or
# splitting the site on the wrong colon and keeping the line number -- both
# produce keys that still LOOK like keys and still sum to the right total.
eq 'the open buckets' "$(paste -sd';' "${WORK}/cnt-rdopen.txt")" \
   'mod/src/main/java/smoke/Raw.java#- 2;mod/src/main/java/smoke/Raw.java#all 1;mod/src/main/java/smoke/Raw.java#names 2;mod/src/main/java/smoke/Raw.java#take 1;mod/src/main/java/smoke/Smoke.java#- 1;mod/src/main/java/smoke/Smoke.java#ctorRefArrays 1' \
   'six keys, no line numbers in any of them'
eq 'the forced buckets' "$(paste -sd';' "${WORK}/cnt-rdforced.txt")" \
   'mod/src/main/java/smoke/Raw.java#all 1;mod/src/main/java/smoke/Raw.java#transform 2' \
   'two keys, and one of them is also in the list above'
eq 'the open buckets sum to rawdecl.open' \
   "$(awk '{ s += $2 } END { print s + 0 }' "${WORK}/cnt-rdopen.txt")" 8 \
   'nothing was dropped on the way through the bucketer'
eq 'the forced buckets sum to rawdecl.forced' \
   "$(awk '{ s += $2 } END { print s + 0 }' "${WORK}/cnt-rdforced.txt")" 3 \
   'and nothing was dropped on the other side either'
eq 'the bucket in both halves' \
   "$(comm -12 <(cut -d' ' -f1 "${WORK}/cnt-rdopen.txt") \
               <(cut -d' ' -f1 "${WORK}/cnt-rdforced.txt"))" \
   'mod/src/main/java/smoke/Raw.java#all' \
   'all() overrides and is overridden -- one bucket, both sides'

# FOUR OF THE EIGHT BUCKET KEYS ON THIS TREE END IN '#-', and the seed's own
# rawdecl.txt says four of six do. That is not a coincidence and it is not a
# bug: enclosingMethod() has nothing to return for a FIELD, cbuckets() drops
# records whose in= is empty -- `if (h1 && m != "")` -- and the gate writes '-'
# so the row survives as a bucket of its own per file. Two raw fields of one
# class collapse into one bucket, which SHRINKS the denominator rather than
# inflating it, and this tree has exactly that collapse in Raw.java: two fields,
# one key, count 2.
eq "Raw.java's field bucket" \
   "$(awk '$1 ~ /Raw.java#-$/ { print $2 }' "${WORK}/cnt-rdopen.txt")" 2 \
   'two raw fields at two depths, collapsed into one key by design'

# ------------------------------ and the cross-check that is the whole argument
#
# javac's own [rawtypes] diagnostics over the same compile: eleven of them, the
# same count rawdecl publishes -- and NOT THE SAME ELEVEN. Both directions are
# populated on this tree, which is what makes the two instruments a check on
# each other rather than one instrument read twice.
grep ': warning: \[rawtypes\]' "${WORK}/src-25.txt.javac.log" \
   | sed -n 's#^.*/\([A-Za-z]*\.java\):\([0-9]*\): warning:.*#\1:\2#p' \
   | LC_ALL=C sort > "${WORK}/rt-javac.txt"
wrecs rawdecl | awk '{ print $1 }' | sed 's#.*/##' \
   | LC_ALL=C sort > "${WORK}/rt-rawdecl.txt"
eq 'javac [rawtypes] sites' "$(grep -c . "${WORK}/rt-javac.txt")" 11 'one per diagnostic'
eq 'rawdecl sites'          "$(grep -c . "${WORK}/rt-rawdecl.txt")" 11 \
   'rawdecl.size, read a second way'

# AND NEITHER LIST IS A SET, which is the reason baseline/rawdecl.txt carries
# no line numbers at all. Raw.java declares a raw return type and a raw local
# ON ONE LINE; both instruments report that line twice; and a cross-check that
# had deduplicated would have come out 10 against 10 and called it agreement
# while quietly dropping a row from each side. The golden moves lines in 32
# files -- <file> and in=<method> survive that and <file>:<line> does not -- so
# a line number is a thing to compare WITH here and never a thing to store.
eq 'the line two declarations share' \
   "$(uniq -d "${WORK}/rt-rawdecl.txt")" 'Raw.java:68' \
   'one raw return type and one raw local, written on one line on purpose'
eq 'and javac reports that line twice too' \
   "$(grep -c '^Raw.java:68$' "${WORK}/rt-javac.txt")" 2 'Set and Iterator, separately'
eq 'sites both instruments have' \
   "$(comm -12 "${WORK}/rt-javac.txt" "${WORK}/rt-rawdecl.txt" | grep -c .)" 10 \
   'ten of eleven, which is the agreement; the two that differ are below'

# DIRECTION ONE: javac sees a raw SUPERTYPE and rawdecl does not. RawSub
# implements java.util.Comparator, javac reports it, and there is no row --
# because a supertype is what MAKES other sites forced, not a site anyone can
# narrow on its own. Counting it would put a row in C-3's denominator that no
# migration can move.
jonly=$(comm -23 "${WORK}/rt-javac.txt" "${WORK}/rt-rawdecl.txt")
eq 'sites javac has and rawdecl has not' "$(printf '%s\n' "${jonly}" | grep -c .)" 1 \
   'one, and the source line says which'
jsrc=$(sed -n "${jonly##*:}p" "${SMOKE}/mod/src/main/java/smoke/${jonly%%:*}")
printf '%s' "${jsrc}" | grep -qF 'implements java.util.Comparator' \
   || fail "the row javac has and rawdecl has not is not the raw supertype: ${jsrc}"
printf '    %-38s %s\n' "${jonly}" 'a raw supertype: javac warns, rawdecl does not count'

# DIRECTION TWO, AND IT IS THE ONE C-3 EXISTS FOR. rawdecl has a row javac does
# not: a raw field in Smoke.java whose class carries
# @SuppressWarnings({"rawtypes", "unchecked"}). javac is silent there BY
# INSTRUCTION -- the annotation is how a candidate makes a warning go away
# without changing a type -- and the plugin walks the tree and records the
# declaration anyway. That is the anti-silence property C-2's min() binding
# protects from the other side, demonstrated here on a tree rather than argued
# in a comment.
ronly=$(comm -13 "${WORK}/rt-javac.txt" "${WORK}/rt-rawdecl.txt")
eq 'sites rawdecl has and javac has not' "$(printf '%s\n' "${ronly}" | grep -c .)" 1 \
   'one, and the line it names says why'
# AND THE LINE IT NAMES IS THE ANNOTATION ITSELF, not the declaration beneath
# it. A declaration's position in javac's own tree begins at its first
# MODIFIER, and here the first modifier is the suppression -- so the row the
# plugin keeps and the annotation that hides it from javac come out as the same
# line number. Asserted rather than remarked on, because it is the kind of
# off-by-one that would make this check pass on the wrong line.
rsup=$(sed -n "${ronly##*:}p" "${SMOKE}/mod/src/main/java/smoke/${ronly%%:*}")
printf '%s' "${rsup}" | grep -qF '@SuppressWarnings' \
   || fail "the row rawdecl has and javac has not is not under a @SuppressWarnings: ${rsup}"
printf '%s' "${rsup}" | grep -qF 'rawtypes' \
   || fail "the suppression above ${ronly} does not name rawtypes: ${rsup}"
printf '    %-38s %s\n' "${ronly}" 'suppressed: javac is silent, rawdecl is not'

# ============================================ the cuts: each rung's own tree
#
# The eight cuts below run newest first -- 12, then 11, 10, 09, 07, 06, 05, 04
# -- and each one drives a LOWER rung's rehearsal over a tree this file's
# section markers cut back to that rung's own. The first two are the ones whose
# figures were measured by THIS rehearsal rather than inherited: the step-12
# cut is what the smoke tree was before this rung's file and section were added
# to it, and the step-11 cut is what it was one section before that.
#
# THE FIRST OF THE EIGHT IS THE ONLY ONE THAT CUTS A FILE RATHER THAN A
# SECTION, and flatten's third argument grew a third value for it. C-2's
# construct is an import declaration, which sits above every section marker
# there is and cannot be reached by a sed address that runs from a marker to
# the end of the file. So the step-12 cut is Mod.java's absence, and Lib.java
# -- which this rung did not touch -- is copied whole rather than cut at a
# marker it does not carry.
#
# WHAT A CUT IS FOR, IN ONE COMPARISON. The whole tree refuses -source 21 and
# the probe block above asserts that the refusal is one error naming an unnamed
# variable and nothing else. That says the refusal is C-3's. It does NOT say
# that the rest of the tree is happy at 21 -- a second, unrelated 22-ism
# somewhere in the step-11 section would leave that count where it is and still
# be a tree that this rung had quietly made un-compilable one level down. So the
# cut tree is compiled AT 21 and has to come back clean. Taken together the two
# probes say: the step-11 section is the only thing on this tree that needs 22,
# and within it the unnamed catch parameter is the only thing that needs 22.
# Each cut below repeats that argument one rung lower, against the level that
# rung refused -- 20 for step 10's pattern switches, 16 for step 09's sealed
# hierarchies, and so on down.
# Cut at a section marker and re-compile. Rewriting each construct would be
# twenty fragile substitutions; the marker is one sed address and the result is
# a tree a lower rung already measured.
#
# The marker is matched with leading whitespace optional, because Smoke.java's
# sections are members of a class and Lib.java's are top-level types. That is
# also why only Smoke.java gets a closing brace back.
#
# THE THIRD VALUE OF THE THIRD ARGUMENT IS NEW AT THIS RUNG, and the reason is
# that this rung's section is the first one Lib.java does not carry. `yes` cuts
# Lib.java at the same marker and `no` leaves it out of the tree entirely; both
# were enough while every rung wrote into both files. Step 12 wrote into neither
# -- its work is in Mod.java -- so its cut needs Lib.java WHOLE: cutting it at
# marker 12 would find no marker, leave the file at full length, and trip
# flatten's own "nothing was cut" guard on a tree that is perfectly correct.
#
# AND MOD.JAVA IS ABSENT FROM EVERY CUT, WITHOUT A FLAG FOR IT. flatten writes
# Smoke.java always and Lib.java on request, and it writes nothing else, so a
# file it does not know about cannot reach a cut tree. That is the mechanism the
# step-12 cut relies on and the reason C-2's construct could be put in a third
# file at all: an import declaration sits above every section marker there is,
# so no sed address can cut one, and the only way to take a module import off a
# tree is to take the file off the tree.
flatten() { # flatten <marker> <dir> <lib: yes|no|whole>
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
   case "${keeplib}" in
      yes)
         sed "/^ *\/\/ -* step ${marker}\$/,\$d" \
            "${SMOKE}/mod/src/main/java/smoke/Lib.java" \
            > "${dir}/mod/src/main/java/smoke/Lib.java"
         cut=$(grep -c . "${dir}/mod/src/main/java/smoke/Lib.java")
         whole=$(grep -c . "${SMOKE}/mod/src/main/java/smoke/Lib.java")
         [ "${cut}" -lt "${whole}" ] \
            || fail "the step-${marker} section marker did not match Lib.java -- nothing was cut"
         ;;
      whole)
         # Copied and asserted equal, rather than copied and trusted. The claim
         # this mode makes is "Lib.java is not part of this cut", and a cp that
         # silently truncated would make the claim false in the direction that
         # loses records rather than the one that gains them.
         cp "${SMOKE}/mod/src/main/java/smoke/Lib.java" \
            "${dir}/mod/src/main/java/smoke/Lib.java"
         cmp -s "${SMOKE}/mod/src/main/java/smoke/Lib.java" \
                "${dir}/mod/src/main/java/smoke/Lib.java" \
            || fail "the step-${marker} cut did not copy Lib.java whole"
         ;;
      no) ;;
      *) fail "flatten: unknown lib mode '${keeplib}'" ;;
   esac
   # AND NOTHING ELSE, ASSERTED. The cut trees are the only place in this file
   # where a tree is built rather than staged, and "Mod.java is absent" is the
   # whole of what makes the step-12 cut a step-11 tree. A flatten that grew a
   # third copy -- or a stale directory from a previous run of this script --
   # would put C-2's construct back on a tree the next hundred lines assert has
   # none, and every one of those assertions would fail with a confusing
   # message about counters instead of this one.
   [ ! -e "${dir}/mod/src/main/java/smoke/Mod.java" ] \
      || fail "the step-${marker} cut has a Mod.java in it -- flatten writes two files and this tree has four"
   # AND THE FOURTH FILE THE SAME WAY, WHICH COST NOTHING TO ARRANGE. Raw.java
   # holds step 13's raw declarations and is off every cut for the same reason
   # Mod.java is: flatten() writes two files by name, so a tree built here can
   # only ever have the two. That is why this rung added no cut of its own --
   # every figure below this banner is step 12's, unmoved, because the file
   # that would have moved them is not on any of these trees.
   [ ! -e "${dir}/mod/src/main/java/smoke/Raw.java" ] \
      || fail "the step-${marker} cut has a Raw.java in it -- flatten writes two files and this tree has four"
}

echo "--- the step-12 cut: step 11's own smoke tree, compiled one level down"
flatten 12 "${WORK}/f12" whole
TREE="${WORK}/f12"
c12=$(srcgate c12 24 || true)
[ "${c12}" = yes ] || {
   echo "--- javac at -source 24 on the step-12 cut ---" >&2
   tail -40 "${WORK}/src-c12.txt.javac.log" >&2 || :
   fail "step 11's own smoke tree does not compile at -source 24 (source.ok=${c12}) -- something other than C-2 needs 25"
}
env12="${WORK}/src-c12.txt.env"
rep12="${WORK}/src-c12.txt"
eq 'javac.level (step-12 cut)'  "$(envval javac.level  "${env12}")" 24 'one level below this rung'
eq 'javac.errors (step-12 cut)' "$(envval javac.errors "${env12}")" 0 \
   'clean -- the one error the whole tree draws at 24 is in the file this cut leaves out'
eq 'javac.sources (step-12 cut)' "$(envval javac.sources "${env12}")" 2 \
   'TWO files: this is the cut that takes a file away rather than a section'
eq 'javac.classes (step-12 cut)' "$(envval javac.classes "${env12}")" 52 \
   'the 53 above minus Mod itself, which is step 11s own count'
c12def=$(srcgate c12def "" || true)
[ "${c12def}" = yes ] \
   || fail "the step-12 cut does not compile at the default level (source.ok=${c12def}) -- the cut tree is broken, not merely one level down"
eq 'javac.classes (step-12 cut, default)' \
   "$(envval javac.classes "${WORK}/src-c12def.txt.env")" 52 'the same 52, unprompted'

c12val() { sed -n "s/^$1=//p" "${rep12}" | head -1; }
c12recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep12}"; }

# ---- the three this rung INHERITED, at the figures step 11 froze ------------
#
# B-18 is step 11's family C carried whole, less its class-file leg, and the
# tree it was measured on is exactly this cut. Thirteen scalars, every one of
# them the figure this file asserts on the whole tree a few hundred lines up,
# re-read here on the cut -- and every one of them the SAME on both trees,
# because nothing this rung writes is a privileged call, a catch parameter or a
# VarHandle.
#
# THAT SAMENESS IS THE ASSERTION AND NOT A REASON TO SKIP IT. This rung has to
# show that its section is inert in the other direction, which is the claim a
# table of identical figures makes and a missing table does not. A priv channel
# rewired to count every member select, or a catchparam that counted every
# VariableTree, would move one of these thirteen and nothing else in this file
# would notice.
echo "--- the step-12 cut: the inherited blocks, at step 11's own figures"
while read -r key want why; do
   got=$(c12val "$(printf '%s' "${key}" | sed 's/\./\\./g')")
   [ "${got}" = "${want}" ] \
      || fail "the step-12 cut reports ${key}=${got}, expected ${want} -- that is step 11s frozen figure, and B-18 scores it"
   printf '    %-30s %-4s %s\n' "${key}" "${got}" "${why}"
done <<'INHERITED12'
priv.size 13 every privileged call and type on step 11s tree
priv.files 2 both files
priv.methods 5 five buckets, two of which are the dash
catchparam.size 8 eight catch parameters
catchparam.unnamed 1 one underscore
catchparam.read 5 five the block mentions
catchparam.unread 2 and two it does not
catchparam.files 2 both files
catchparam.methods 8 one catch each
varhandle.size 4 the VarHandle arrivals
varhandle.arrival 4 all four of them
varhandle.files 2 both files
varhandle.methods 2 in two buckets
INHERITED12

# ---- the twenty-two lower-rung counters this rung moved ---------------------
#
# Step 11's cut wrote out three of these and called them "the ones a later
# reader would otherwise find by surprise". This rung moved twenty-two, because
# its section declares a ScopedValue and a ThreadLocal and Mod.java is a whole
# file of ordinary Java on top of that, so every counter that has ever counted a
# lambda, a stream op, a for-each or a `new` with explicit type arguments went
# up. None is read by any gate in test.sh -- they are step 05's, step 06's and
# step 08's counters and this rung scores none of them -- and that is exactly
# why they are tabled rather than trusted: a figure no gate reads is a figure
# nothing else in the image would notice going wrong.
#
# THE TABLE IS THE OTHER HALF OF THE WHOLE-TREE TABLE. Each row is the value one
# rung down; the whole-tree section above asserts the value here. Read together
# they are the movement, measured at both ends, and a counter that started
# reading a construct it used to skip moves one end and not the other.
echo "--- the step-12 cut: the lower-rung counters this rung moved"
while read -r key want why; do
   got=$(c12val "$(printf '%s' "${key}" | sed 's/\./\\./g')")
   [ "${got}" = "${want}" ] \
      || fail "the step-12 cut reports ${key}=${got}, expected ${want} -- this rungs movement table is stale"
   printf '    %-30s %-4s %s\n' "${key}" "${got}" "${why}"
done <<'MOVED12'
files 2 two files, and the whole tree has three
foreach.collect 2 back from 3
foreach.collectmethods 2 back from 3
foreach.size 5 back from 6: Mod loops over loader.stream().toList()
lambda.expr 17 back from 19: the run() and the call() bodies
lambda.size 19 back from 21
loop.size 9 back from 10
newgeneric.explicit 7 back from 9: new ThreadLocal<ClassLoader>() and new ArrayList<String>()
newgeneric.explicitfiles 1 back from 2
stream.files 2 back from 3
stream.opfiles 2 back from 3
stream.opmethods 12 back from 13
stream.ops 17 back from 18
stream.producers 19 back from 20
svc.files 2 back from 3
svc.platformtype 6 back from 7: Mod names java.util.ServiceLoader
svc.providertype 2 back from 3: and ServiceLoader.Provider
try.size 11 back from 12: the finally around SAVED.remove()
var.candidatefiles 2 back from 3
var.candidatemethods 46 back from 49
var.candidatenew 20 back from 22
var.candidates 90 back from 93
MOVED12
# AND THE COUNTERS THAT DID NOT MOVE, named rather than left to the absence of a
# row above. These are the seven families whose gates watch a construct this
# rung's section does not write, and a section that had quietly acquired one --
# a switch in the extent body, a record for the carrier -- would move one of
# them and every figure in this file would still pass, because nothing above
# asserts what a counter is NOT.
for k in switch.size ipattern.size catchparam.size varhandle.size \
         record.size sealed.size priv.size; do
   a=$(c12val "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   b=$(wval   "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${a}" = "${b}" ] \
      || fail "${k} reads ${a} on the step-12 cut and ${b} on the whole tree -- this rungs section touched a gate it should not"
done
printf '    %-38s %s\n' 'seven families, cut vs whole' 'identical: the step-12 section writes none of their constructs'

# ---- and the four blocks at THIS rung, three at zero and one at 37 ----------
#
# Trap 6's instruction applied to the counters this rung is the first to ship --
# with the exception this rung's notes single out. modimport, scopedvalue and
# threadlocal read nothing on a tree that has not done the work, and jdkimport
# reads THIRTY-SEVEN, because the scanner has been counting ordinary JDK imports
# since step 09 and step 11's tree is full of them.
#
# A SECTION THAT ASSERTED FOUR ZEROES WOULD FAIL ON A CORRECT TREE, which is
# worth saying out loud because every cut below this one asserts nothing but
# zeroes and copying the pattern forward is the obvious thing to do. C-3 does
# not score a population it invented at this rung; it scores 495 rows that were
# in the seed before the migration started, and the 37 here are this tree's
# share of exactly that.
for k in modimport.size modimport.files \
         scopedvalue.size scopedvalue.files scopedvalue.decls \
         threadlocal.size threadlocal.files threadlocal.decls \
         jdkimport.nested jdkimport.ondemand jdkimport.ambiguous; do
   got=$(c12val "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got}" = 0 ] \
      || fail "the step-12 cut reports ${k}=${got}, expected 0 -- that counter has a population below this rung"
done
printf '    %-38s %s\n' 'three new blocks and three hold reasons' 'all 0 on step 11s tree'
eq 'jdkimport.size (cut12)'  "$(c12val 'jdkimport\.size')"  37 \
   'NOT zero: step 09 shipped this counter and step 11s tree has 37 ordinary JDK imports'
eq 'jdkimport.files (cut12)' "$(c12val 'jdkimport\.files')"  2 'in both of the two files this cut leaves'
eq 'jdkimport.type (cut12)'    "$(c12val 'jdkimport\.type')"    35 'thirty-five plain ones, back from 37'
eq 'jdkimport.static (cut12)'  "$(c12val 'jdkimport\.static')"   2 'and two static ones, back from 3'
# 35 + 2 = 37 HERE TOO, which is the same fall-through check the whole-tree
# block makes and is worth making twice: the branch that would swallow a row is
# reached on both trees and the two populations have nothing in common but the
# code path.
eq 'the two kinds, summed (cut12)' \
   "$(( $(c12val 'jdkimport\.type') + $(c12val 'jdkimport\.static') ))" 37 \
   'every row has exactly one kind on this tree as well'
# THE KEYS ARE PRESENT AND READ 0, which is a different statement from being
# absent, and the loop above cannot tell the two apart: c12val returns the empty
# string for a key that is not in the report and `[ "" = 0 ]` is false, so an
# absent key already fails the loop. Said once here rather than eleven times in
# the messages.
for k in modimport.size scopedvalue.size threadlocal.size jdkimport.size; do
   grep -q "^${k}=" "${rep12}" \
      || fail "the step-12 cut report has no ${k}= line at all -- the block was not emitted"
done
printf '    %-38s %s\n' 'and the four block headers' 'emitted on a tree with three of them empty'
for b in modimport scopedvalue threadlocal; do
   n=$(c12recs "${b}" | wc -l)
   [ "${n}" = 0 ] \
      || fail "the step-12 cut lists ${n} ${b} record(s) on a tree whose ${b}.size is 0"
done
printf '    %-38s %s\n' 'and the three record lists' 'empty, not merely uncounted'
# AND THE FOURTH LIST IS NOT EMPTY AND CARRIES NO ROW FROM THE ABSENT FILE.
# Thirty-seven records, none of them naming Mod.java -- which is the cut itself,
# stated in the one block that can state it. Every other new block on this rung
# proves the file is gone by reading zero; this one has to prove it by reading
# the right 37.
njr=$(c12recs jdkimport | wc -l)
eq 'jdkimport records (cut12)' "${njr}" 37 'the list is as long as the counter says'
c12recs jdkimport | grep -q 'Mod\.java' \
   && fail "the step-12 cut reports a jdkimport from Mod.java -- the file flatten does not write is on the tree"
printf '    %-38s %s\n' 'no jdkimport from Mod.java' 'the cut is the absent file, and the report says so'

echo "--- the step-11 cut: step 10's own smoke tree, compiled one level down"
flatten 11 "${WORK}/f11" yes
TREE="${WORK}/f11"
c11=$(srcgate c11 21 || true)
[ "${c11}" = yes ] || {
   echo "--- javac at -source 21 on the step-11 cut ---" >&2
   tail -40 "${WORK}/src-c11.txt.javac.log" >&2 || :
   fail "step 10's own smoke tree does not compile at -source 21 (source.ok=${c11}) -- something other than C-3 needs 22"
}
env11="${WORK}/src-c11.txt.env"
rep11="${WORK}/src-c11.txt"
eq 'javac.level (step-11 cut)'  "$(envval javac.level  "${env11}")" 21 'one level below this rung'
eq 'javac.errors (step-11 cut)' "$(envval javac.errors "${env11}")" 0 \
   'clean -- the one error the whole tree draws at 21 is in the cut section'
eq 'javac.classes (step-11 cut)' "$(envval javac.classes "${env11}")" 45 \
   'the 52 above minus the seven types this rungs section declares, which is step 10s own count'
# AND THE SAME CUT AT THE DEFAULT LEVEL, which is this cut's half of the
# byte-agreement check the whole tree makes at the top of the file. 45 classes
# at -source 21 and 45 with no -source flag at all says the count is a property
# of the tree rather than of the flag, so the subtraction 52 - 45 is a
# statement about seven type declarations and not about two compilers.
c11def=$(srcgate c11def "" || true)
[ "${c11def}" = yes ] \
   || fail "the step-11 cut does not compile at the default level (source.ok=${c11def}) -- the cut tree is broken, not merely one level down"
eq 'javac.classes (step-11 cut, default)' \
   "$(envval javac.classes "${WORK}/src-c11def.txt.env")" 45 'the same 45, unprompted'

c11val() { sed -n "s/^$1=//p" "${rep11}" | head -1; }
c11recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep11}"; }

# ---- the four this rung INHERITED, at the figures step 10 froze -------------
#
# B-17 is step 10's family C carried whole, less its class-file leg, and the
# tree it was measured on is exactly this cut. Every number in the table below
# is the one this file asserts on the whole tree a few hundred lines up, re-read
# here on the cut -- and unlike the step-10 cut's table, every one of them is
# the SAME on both trees, because nothing this rung's section writes is a
# runtime type test, a first-element read or a switch.
#
# THAT SAMENESS IS THE ASSERTION AND NOT A REASON TO SKIP IT. Step 10 had to
# hold two readings apart because a pattern switch is a switch; this rung has to
# show that its section is inert in the other direction, which is the claim a
# table of identical figures makes and a missing table does not. A priv channel
# that had been wired to count every member select, or a catchparam that counted
# every VariableTree, would move one of these eighteen and nothing else in the
# file would notice.
echo "--- the step-11 cut: the inherited blocks, at step 10's own figures"
while read -r key want why; do
   got=$(c11val "$(printf '%s' "${key}" | sed 's/\./\\./g')")
   [ "${got}" = "${want}" ] \
      || fail "the step-11 cut reports ${key}=${got}, expected ${want} -- that is step 10s frozen figure, and B-17 scores it"
   printf '    %-30s %-4s %s\n' "${key}" "${got}" "${why}"
done <<'INHERITED11'
reftest.size 12 every Class-based type test on step 10s tree
reftest.literal 9 nine written with a class literal
reftest.var 3 and three with a Class value
reftest.files 2 both files
reftest.methods 10 ten buckets
seqfirst.size 10 the two spellings of a first-element read
seqfirst.guarded 6 six of them under a test on the receiver
seqfirst.files 2 both files
seqfirst.methods 10 ten buckets
seqnew.size 5 the SequencedCollection arrivals
seqnew.read 4 of which four are reads
seqnew.files 2 both files
seqnew.methods 5 five buckets
swpat.size 5 five pattern switches
swpat.patterns 11 eleven bindings across them
swpat.nullcase 1 one case null
swpat.files 2 both files
swpat.methods 5 five buckets
INHERITED11
# THE THREE KEYS THAT DID MOVE, written out rather than tabled, because they are
# the ones a later reader would otherwise find by surprise. None is read by any
# gate in test.sh -- they are step 05's and step 03's counters and this rung
# scores neither -- but this section writes eight try statements and five
# convertible locals, so all three rise on the whole tree. Recorded here so the
# movement is a measurement rather than something reconciled later with an edit.
eq 'try.size (cut11)' "$(c11val 'try\.size')" 3 \
   'step 10s three, with step 11s eight cut away -- the whole tree reads 12'
eq 'var.candidates (cut11)' "$(c11val 'var\.candidates')" 85 \
   'and step 10s 85: the whole tree reads 93, and the step-12 cut 90'
eq 'var.candidatemethods (cut11)' "$(c11val 'var\.candidatemethods')" 43 \
   'in 43 buckets: the whole tree reads 49, and the step-12 cut 46'
for want in 'form=litinstance type=java.lang.String sel=greeting' 'op=get0 recv=java.util.List expr=names'; do
   c11recs reftest | grep -qF "${want}" || c11recs seqfirst | grep -qF "${want}" \
      || fail "the step-11 cut lost an inherited step-10 site: ${want}"
done
printf '    %-38s %s\n' 'the inherited step-10 sites' 'still recorded, and still discriminated'

# ---- and the three that arrive whole at THIS rung --------------------------
#
# Thirteen scalars at zero on a tree that compiles, and three empty record
# lists. This is trap 6's instruction applied to the counters this rung is the
# first to ship: the whole-tree blocks above say they run, and this says they
# say nothing about a tree that has not done the work. A priv channel that
# counted every type reference, a catchparam that counted every catch in the
# JDK's sense of one, or a varhandle that counted every MethodHandles call would
# pass every assertion in the whole-tree section -- those figures were frozen
# from a measurement, so they would have been frozen wrong -- and fails here,
# where the answer has to be nothing at all.
#
# IT IS ALSO THE LEVEL CONTROL FOR C-1 AND THE OTHER THREE LEGS AT ONCE. The
# step-11 cut is the tree the floor calibration scores with this step's
# verifier, and the figure it is expected to land on is arithmetic over exactly
# these zeros; a counter with a population here would move that figure without
# moving anything a human reads.
for k in priv.size priv.files priv.methods \
         catchparam.size catchparam.unnamed catchparam.read catchparam.unread \
         catchparam.files catchparam.methods \
         varhandle.size varhandle.arrival varhandle.files varhandle.methods; do
   got=$(c11val "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got}" = 0 ] \
      || fail "the step-11 cut reports ${k}=${got}, expected 0 -- that counter has a population below this rung"
done
printf '    %-38s %s\n' 'the three new blocks, 13 keys' 'all 0 on step 10s tree'
# THE KEYS ARE PRESENT AND READ 0, which is a different statement from being
# absent, and the loop above cannot tell the two apart: c11val returns the empty
# string for a key that is not in the report and `[ "" = 0 ]` is false, so an
# absent key already fails the loop. Said once here rather than thirteen times
# in the messages.
for k in priv.size catchparam.size varhandle.size; do
   grep -q "^${k}=" "${rep11}" \
      || fail "the step-11 cut report has no ${k}= line at all -- the block was not emitted"
done
printf '    %-38s %s\n' 'and the three block headers' 'emitted on a tree with nothing to report'
for b in priv catchparam varhandle; do
   n=$(c11recs "${b}" | wc -l)
   [ "${n}" = 0 ] \
      || fail "the step-11 cut lists ${n} ${b} record(s) on a tree whose ${b}.size is 0"
done
printf '    %-38s %s\n' 'and the three record lists' 'empty, not merely uncounted'

echo "--- the step-10 cut: step 09's own smoke tree, compiled one level down"
flatten 10 "${WORK}/f10" yes
TREE="${WORK}/f10"
c10=$(srcgate c10 20 || true)
[ "${c10}" = yes ] || {
   echo "--- javac at -source 20 on the step-10 cut ---" >&2
   tail -40 "${WORK}/src-c10.txt.javac.log" >&2 || :
   fail "step 09's own smoke tree does not compile at -source 20 (source.ok=${c10}) -- something other than C-3 needs 21"
}
env10="${WORK}/src-c10.txt.env"
rep10="${WORK}/src-c10.txt"
eq 'javac.level (step-10 cut)'  "$(envval javac.level  "${env10}")" 20 'one level below this rung'
eq 'javac.errors (step-10 cut)' "$(envval javac.errors "${env10}")" 0 \
   'clean -- the three errors the whole tree draws at 20 are all in the cut section'
eq 'javac.classes (step-10 cut)' "$(envval javac.classes "${env10}")" 42 \
   'the 45 above minus the three types this rungs section declares, which is step 09s own count'
# AND THE SAME CUT AT THE DEFAULT LEVEL, which is this cut's half of the
# byte-agreement check the whole tree makes at the top of the file. 42 classes
# at -source 20 and 42 with no -source flag at all says the count is a property
# of the tree rather than of the flag, so the subtraction 45 - 42 is a
# statement about three type declarations and not about two compilers.
c10def=$(srcgate c10def "" || true)
[ "${c10def}" = yes ] \
   || fail "the step-10 cut does not compile at the default level (source.ok=${c10def}) -- the cut tree is broken, not merely one level down"
eq 'javac.classes (step-10 cut, default)' \
   "$(envval javac.classes "${WORK}/src-c10def.txt.env")" 42 'the same 42, unprompted'

c10val() { sed -n "s/^$1=//p" "${rep10}" | head -1; }
c10recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep10}"; }

# ---- the five this rung INHERITED, at the figures step 09 froze -------------
#
# B-16 is step 09's family C carried whole, less its class-file leg, and the
# tree it was measured on is exactly this cut. Every number in the table below
# is the one step 09's own rehearsal asserts on its whole tree, re-read here by
# THIS rung's image -- which is the only place in this file where that can be
# said, because on the whole tree above three of these keys have moved, and
# they moved for a reason that is this rung's doing rather than a counter's:
# a pattern switch is a switch.
#
# THE ASSERTION IS THE WHOLE TABLE AND NOT A SPOT CHECK, for the ninth trap's
# reason. A drifting counter drifts on one field -- a text block's `len`, a
# permits list's arity, an empty switch's form -- and the leg that carries it
# is scored at every rung after the one that wrote it, by nobody who is looking.
echo "--- the step-10 cut: the inherited blocks, at step 09's own figures"
while read -r key want why; do
   got=$(c10val "$(printf '%s' "${key}" | sed 's/\./\\./g')")
   [ "${got}" = "${want}" ] \
      || fail "the step-10 cut reports ${key}=${got}, expected ${want} -- that is step 09s frozen figure, and B-16 scores it"
   printf '    %-30s %-4s %s\n' "${key}" "${got}" "${why}"
done <<'INHERITED'
ipattern.size 9 every instanceof on step 09s tree
ipattern.bound 1 one pattern already bound
ipattern.plain 8 and eight not
ipattern.convertible 5 five sites with a cast of the same expression after them
ipattern.files 2 both files
ipattern.methods 8 eight buckets
textblock.size 3 three text blocks
textblock.unreadable 0 and all three readable
textblock.files 2 both files
textblock.methods 3 three buckets
strnl.size 4 four newline literals
strnl.concat 2 two of them in a concatenation
strnl.files 2 both files
strnl.methods 3 three buckets
sealed.decl 3 three sealed roots
sealed.nonsealed 1 one non-sealed subtype
sealed.permits 5 five names across the permits clauses
sealed.files 2 both files
record.decl 1 Paused
record.components 2 at and ticks
INHERITED
# THE SWITCH KEYS ARE THE THREE THAT MOVED, written out rather than tabled,
# because this is the pair of readings the file exists to hold apart. The whole
# tree reads 11/8/6 and step 09's tree reads 6/3/2, and the difference is
# exactly this rung's five pattern switches -- all five in arrow form, four of
# them expressions. Neither figure is a correction of the other.
eq 'switch.size (cut10)'  "$(c10val 'switch\.size')"  6 'step 09s six, with this rungs five cut away'
eq 'switch.arrow (cut10)' "$(c10val 'switch\.arrow')" 3 'and its three arrows, back from eight'
eq 'switch.expr (cut10)'  "$(c10val 'switch\.expr')"  2 'and its two expressions, back from six'
eq 'switch.colon (cut10)' "$(c10val 'switch\.colon')" 3 'the colon count never moved: a pattern switch is written in arrow form'
eq 'switch.files (cut10)' "$(c10val 'switch\.files')" 2 'still both files'
eq 'switch.mixed (cut10)' "$(c10val 'switch\.mixed')" 0 'and still unreachable'
for want in 'conv=yes in=equals' 'conv=yes in=legacyHttp'; do
   c10recs ipattern | grep -qF "${want}" \
      || fail "the step-10 cut lost an inherited ipattern site: ${want}"
done
printf '    %-38s %s\n' 'the inherited ipattern sites' 'still recorded, and still convertible'

# ---- and the four that arrive whole at THIS rung ---------------------------
#
# Eighteen scalars at zero on a tree that compiles, and four empty record
# lists. This is trap 6's instruction applied to the counters this rung is the
# first to ship: the whole-tree blocks above say they run, and this says they
# say nothing about a tree that has not done the work. A reftest visitor that
# counted every method call, or a seqfirst that counted every get(), would pass
# every assertion in the whole-tree section -- those figures were frozen from a
# measurement, so they would have been frozen wrong -- and fails here, where
# the answer has to be nothing at all.
#
# IT IS ALSO THE LEVEL CONTROL FOR C-1 AND THE OTHER THREE LEGS AT ONCE. The
# step-10 cut is the tree the floor calibration scores with this step's
# verifier, and the figure it is expected to land on is arithmetic over exactly
# these zeros; a counter with a population here would move that figure without
# moving anything a human reads.
for k in reftest.size reftest.literal reftest.var reftest.files reftest.methods \
         seqfirst.size seqfirst.guarded seqfirst.files seqfirst.methods \
         seqnew.size seqnew.read seqnew.files seqnew.methods \
         swpat.size swpat.patterns swpat.nullcase swpat.files swpat.methods; do
   got=$(c10val "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got}" = 0 ] \
      || fail "the step-10 cut reports ${k}=${got}, expected 0 -- that counter has a population below this rung"
done
printf '    %-38s %s\n' 'the four new blocks, 18 keys' 'all 0 on step 09s tree'
# THE KEYS ARE PRESENT AND READ 0, which is a different statement from being
# absent, and the loop above cannot tell the two apart: c10val returns the empty
# string for a key that is not in the report and `[ "" = 0 ]` is false, so an
# absent key already fails the loop. Said once here rather than eighteen times
# in the messages.
for k in reftest.size seqfirst.size seqnew.size swpat.size; do
   grep -q "^${k}=" "${rep10}" \
      || fail "the step-10 cut report has no ${k}= line at all -- the block was not emitted"
done
printf '    %-38s %s\n' 'and the four block headers' 'emitted on a tree with nothing to report'
for b in reftest seqfirst seqnew swpat; do
   n=$(c10recs "${b}" | wc -l)
   [ "${n}" = 0 ] || fail "the step-10 cut has ${n} ${b} record(s) and its scalar says 0"
done
printf '    %-38s %s\n' 'no reftest/seqfirst/seqnew/swpat' 'the record lists are empty, not just the counts'

TREE="${SMOKE}"

echo "--- the step-09 cut: step 08's own smoke tree, compiled one level down"
flatten 09 "${WORK}/f09" yes
TREE="${WORK}/f09"
c09=$(srcgate c09 16 || true)
[ "${c09}" = yes ] || {
   echo "--- javac at -source 16 on the step-09 cut ---" >&2
   tail -40 "${WORK}/src-c09.txt.javac.log" >&2 || :
   fail "step 08's own smoke tree does not compile at -source 16 (source.ok=${c09}) -- something other than C-4 needs 17"
}
# A DISTINCT TAG, and the convention every cut in this file follows. srcgate
# names its output after the TAG and not after the level, so a cut probed with
# `srcgate 16 16` writes src-16.txt -- and at the rung that made 16 its own
# refusal level, that overwrote the whole-tree refusal log with a clean one
# after the assertion which read it had already passed. A rehearsal that
# destroys its own evidence on the way past is a rehearsal nobody can debug.
# This rung's refusal level is 20, so the collision is live one section up and
# dormant here; the tag stays c09 because a convention applied only where the
# collision is visible today is one that gets broken the first time a level
# moves, which is every rung.
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
#
# THE QUESTION IS ASKED AT THE AST NOW, AND IT USED TO BE ASKED AT THE COMPILER.
# This block once compiled the cut at -source 1.7 and read the answer off the
# exit status. That probe stopped being runnable when the image moved past JDK
# 20: javac dropped -source 1.7, so the probe began failing for a reason that
# had nothing to do with the tree, and a version of it that tolerated the
# failure would have meant "javac declined to try". The instrument that
# survives an image move is the one already running over this tree. SourceScan
# NAMES the Java 8 constructs, so the claim is made by naming them rather than
# by inferring them from a refusal, and no future --release bump can take it
# away.
#
# BOTH HALVES, because a zero is also what a visitor that has stopped running
# says. The Java 7 counters are asserted at their own non-zero figures off the
# same report, so the block reads: this tree HAS a diamond, a try-with-resources
# and three java.nio.file references, and it has no lambda, no method
# reference, no stream, no Optional, no default method and no `var`.
flatten 04 "${WORK}/f04" no
TREE="${WORK}/f04"
f04=$(srcgate f04 "" || true)
[ "${f04}" = yes ] || {
   echo "--- javac on the step-03 tree ---" >&2
   tail -40 "${WORK}/src-f04.txt.javac.log" >&2 || :
   fail "the Java 7 half of the same file does not compile at all (source.ok=${f04})"
}
f4v() { sed -n "s/^$1=//p" "${WORK}/src-f04.txt" | head -1; }
eq 'f04 files'         "$(f4v 'files')" 1 'Smoke.java alone -- the step-04 cut drops Lib.java'
eq 'f04 javac.classes' "$(envval javac.classes "${WORK}/src-f04.txt.env")" 2 "step 04's own count"
eq 'f04 newgeneric.diamond'  "$(f4v 'newgeneric\.diamond')"   1 'a diamond, which is Java 7 and not Java 8'
eq 'f04 newgeneric.explicit' "$(f4v 'newgeneric\.explicit')"  2 'and two explicit type arguments beside it'
eq 'f04 newgeneric.raw'      "$(f4v 'newgeneric\.raw')"       1 'and one raw'
eq 'f04 try.resources'       "$(f4v 'try\.resources')"        1 'a try-with-resources, also Java 7'
eq 'f04 close.manual'        "$(f4v 'close\.manual')"         1 'beside the manual close it was meant to replace'
eq 'f04 niofile.ref'         "$(f4v 'niofile\.ref')"          2 'java.nio.file, which arrived in 7'
eq 'f04 niofile.staticref'   "$(f4v 'niofile\.staticref')"    1 'one of the two through a static import'
eq 'f04 iofile.new'          "$(f4v 'iofile\.new')"           3 'and three java.io.File constructions that did not move'
for k in lambda.size lambda.files lambda.expr lambda.methodrefable \
         methodref.size \
         anonymousfunctional.size anonymousfunctional.convertible \
         anonymousfunctional.forward \
         stream.producers stream.ops stream.files stream.opfiles stream.opmethods \
         foreach.size foreach.collect foreach.collectmethods \
         iface.decl iface.sam iface.default iface.static \
         optret.size optret.files optop.size optop.methods optop.get \
         collect.size collect.methods collect.tolist collect.toset \
         collect.tocollection collect.other \
         objects.size objects.methods jtime.size jtime.files jtime.methods \
         millis.size millis.methods \
         retnull.size retnull.methods retnull.files retnull.api \
         accum.size accum.methods loop.size \
         var.size var.methods var.files; do
   got=$(f4v "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got}" = 0 ] \
      || fail "the step-04 cut reports ${k}=${got}, expected 0 -- the Java 7 half of Smoke.java has acquired a Java 8 construct"
done
printf '    %-30s %s\n' 'the step-03 half, 48 keys' 'every Java 8 counter reads 0'
# `var` IS A JAVA 10 CONSTRUCT AND ITS CANDIDATE COUNTER IS NOT. varcand records
# a local whose initialiser names its own type, which is a shape Java 5 code is
# full of, so two candidates here is the right answer and a zero would say the
# candidate visitor had stopped rather than that the tree was clean. This is the
# same both-halves argument as the Java 7 figures above, made for the one
# counter whose name makes it look like it belongs with the zeros.
eq 'f04 var.candidates'     "$(f4v 'var\.candidates')"     2 'two locals a `var` could replace, and neither written that way'
eq 'f04 var.candidatefiles' "$(f4v 'var\.candidatefiles')" 1 'in the one file the cut leaves'

echo "src-rehearsal: SourceScan counts a real FOUR-file tree as measured; this rung own two read eleven raw declarations in two files over seven buckets -- three forced, two of them imposed by a TestNG interface on the shelf and one by an interface in the tree, and one bucket that is in the open set and the forced set at once -- beside a warning count that is 3 with the flags off, 20 under the rung own -Xlint string and 31 with the -deprecation carve-out taken back out of it, while the plugin report is the same bytes under all three and javac.warnings is the only env key that moves; the two instruments agree on eleven and disagree on the set in both directions -- javac warns at a raw supertype rawdecl does not count, and rawdecl keeps a declaration javac is silent about because the line that declares it is a @SuppressWarnings -- which is the whole of why C-3 cannot be annotated away; B-19 four blocks below read one module import in the one file that has one, forty-two JDK imports in four retention kinds -- thirty-seven plain, three static, one nested, one on-demand -- carrying one ambiguous name and keyed on forty-two (file, q) pairs where q alone would leave thirty-six, and eleven ScopedValue sites beside four ThreadLocal ones whose two declarations are the two rows the gate own form=decl regex matches; the near misses beside them produce nothing -- the org.testng import is skipped rather than classified, and the ThreadLocal that stayed is counted rather than converted; the whole tree compiles at 25 with 56 classes from four sources and refuses 24 with exactly one error, which names a module import and names Mod.java; the step-12 cut is step 11 own tree at -source 24, clean, at step 11 52 classes from TWO sources -- Mod.java and Raw.java are both off it, and off all eight cuts, because flatten writes two files by name -- with thirteen inherited counters at step 11 figures and all twenty-two counters step 12 moved below itself back at theirs -- svc at 6/2/2 where the whole tree reads 7/3/3 -- and of B-19 own four blocks three read zero while jdkimport reads 37, because step 11 tree already had thirty-seven JDK imports in it; and the seven cuts below that take the tree down to step 05 rung by rung, unchanged"
