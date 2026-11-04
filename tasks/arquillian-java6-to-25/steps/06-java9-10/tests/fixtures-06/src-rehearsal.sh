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
#   * the three probes are the three the scorer makes -- `srcgate 10 10`,
#     `srcgate 8 8`, `srcgate def ""` -- and the first two must disagree.
#
# WHAT STEP 06 ADDS TO STEP 05'S VERSION OF THIS FILE
# ---------------------------------------------------
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
# THE FLATTEN CHAIN IS THREE RUNGS DEEP HERE. Smoke.java is step 05's Smoke.java
# with a section appended and Lib.java is step 05's Lib.java with two top-level
# classes appended, both verbatim, so cutting BOTH files at the step-06 marker
# must reproduce step 05's OWN frozen figures -- about forty of them, measured by
# a different rung's rehearsal on a different day. Cutting Smoke.java at the
# step-05 marker and dropping Lib.java must then reproduce step 04's twenty-six.
# A counter that drifted between any two rungs is a disagreement between two
# measurements rather than two separately plausible numbers.
#
# The step-06 cut is also this rung's ANSWER-CAN-BE-YES control, and it replaces
# step 05's: the tree that fails at -source 8 must be the SAME tree that passes
# once the six `var` declarations are removed, compiled by the same probe at the
# same level. Without it `ok8=no` is satisfied by a probe that says no to
# everything, and ok8 is a term in this step's B-4.
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
for call in 'ok10=$(srcgate 10 10)' 'ok8=$(srcgate 8 8)' 'okdef=$(srcgate def "")'; do
   grep -qF "${call}" "${TESTS}/test.sh" \
      || fail "test.sh no longer contains the call this rehearsal drives: ${call}"
done

# ----------------------------------------------------------- the three probes
# `|| true` on every srcgate call, and not decoration. srcgate ends in
# `sed ... | head -1`, whose status is 2 when source-gate.sh exited before
# writing the .env -- which is exactly the interesting failure. test.sh runs
# under `set -u` alone (rule 8: it must always reach report()), so there it
# lands as an empty ok and reads as "no". Here, under -e and pipefail, the bare
# assignment would end the rehearsal on the assignment line and never print
# which probe failed.
ok10=$(srcgate 10 10 || true)
ok8=$(srcgate 8 8 || true)
okdef=$(srcgate def "" || true)
[ "${ok10}" = yes ] || {
   echo "--- javac at -source 10 ---" >&2; tail -40 "${WORK}/src-10.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at -source 10 (source.ok=${ok10})"
}
[ "${ok8}" = no ] || fail "the smoke tree compiled at -source 8 (source.ok=${ok8})"
[ "${okdef}" = yes ] || {
   echo "--- javac at the image default ---" >&2; tail -40 "${WORK}/src-def.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at the image's default level (source.ok=${okdef})"
}

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

# The default-level probe, which test.sh falls back to when ok10 is no. Its
# report must be the SAME report: the counters below are properties of the
# source text and not of the level javac was asked for, and that is what makes
# `srcgate def ""` a usable fallback rather than a second instrument with its
# own readings. Compared as whole files, because naming the keys that must match
# would be naming the keys someone thought of.
eq 'javac.level (default probe)' "$(envval javac.level "${WORK}/src-def.txt.env")" default \
   'GATE_SOURCE empty means no -source flag at all'
cmp -s "${rep10}" "${WORK}/src-def.txt" \
   || fail "the default-level report differs from the -source 10 report -- the counters depend on the level"
printf '    %-30s %s\n' 'default vs -source 10' 'byte-identical reports, so the fallback reads the same tree'

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

echo "src-rehearsal: SourceScan counts a real two-file tree as measured; every step-06 record carries its path, its method and the field the gate discriminates on; and the same tree without its six var declarations is step 05's, at step 05's forty figures"
