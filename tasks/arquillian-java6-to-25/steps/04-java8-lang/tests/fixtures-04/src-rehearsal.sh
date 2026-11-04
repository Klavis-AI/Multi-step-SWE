#!/usr/bin/env bash
# Build-time rehearsal for family C's instrument: arqgate.SourceScan, the
# javac plugin, driven through the shipped source-gate.sh by the shipped
# srcgate() with the shipped classpath expression.
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
#   * the compile runs twice, at 1.8 and at 1.7, which is C-4's whole
#     mechanism. The clean control is the 1.8 run; its paired synthetic
#     failure is the 1.7 run, which must fail, and must fail only about the
#     language level.
#
# The 1.7 half is the one that cannot be faked from the AST: the counters would
# report the same lambdas and the same pipelines whether or not javac would
# accept the tree at either level.
#
# WHAT STEP 04 ADDS TO STEP 03'S VERSION OF THIS FILE
# ---------------------------------------------------
# The three per-site record blocks family C's arrival halves read -- anonfn,
# lambda/methodref, and stream -- and the discriminating case for each. Step 03
# needed totals; step 04 asks WHICH FILE gained a construct, so a block that
# increments a counter without recording where is a gate that cannot be built
# on. Each block is asserted by its field values and not by line number, so
# editing the comment at the top of Smoke.java does not break the rehearsal.
#
# And then WHICH METHOD, which is round 2 of the static review rather than the
# design: a file is a large enough place that one dummy pipeline per file paid
# C-3's arrival half outright. The last C-3 stanza is the whole of that finding
# reproduced on this tree -- Smoke.java satisfies the file-scope conjunction and
# fails the method-scope one, and both are computed here from the same records.
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
[ -f "${SMOKE}/mod/src/main/java/smoke/Smoke.java" ] \
   || fail "the smoke tree is not staged at ${SMOKE}"

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

# ------------------------------------------------------------- the two probes
# `|| true` on every srcgate call, and not decoration. srcgate ends in
# `sed ... | head -1`, whose status is 2 when source-gate.sh exited before
# writing the .env -- which is exactly the interesting failure. test.sh runs
# under `set -u` alone (rule 8: it must always reach report()), so there it
# lands as an empty ok and reads as "no". Here, under -e and pipefail, the bare
# assignment would end the rehearsal on the assignment line and never print
# which probe failed.
ok18=$(srcgate 18 1.8 || true)
ok17=$(srcgate 17 1.7 || true)
[ "${ok18}" = yes ] || {
   echo "--- javac at 1.8 ---" >&2; tail -40 "${WORK}/src-18.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at -source 1.8 (source.ok=${ok18})"
}
[ "${ok17}" = no ] || fail "the smoke tree compiled at -source 1.7 (source.ok=${ok17})"

env18="${WORK}/src-18.txt.env"
rep18="${WORK}/src-18.txt"
env17="${WORK}/src-17.txt.env"

envval() { sed -n "s/^$1=//p" "$2" | head -1; }
repval() { sed -n "s/^$1=//p" "${rep18}" | head -1; }

eq() { # eq <what> <got> <want> <why>
   if [ "${2}" != "${3}" ]; then
      fail "${1}: got ${2}, expected ${3} -- ${4}"
   fi
   printf '    %-26s %-4s %s\n' "$1" "$2" "$4"
}

echo "--- the 1.8 compile"
eq 'javac.rc'      "$(envval javac.rc      "${env18}")" 0   'a clean compile'
eq 'javac.errors'  "$(envval javac.errors  "${env18}")" 0   'and no errors'
eq 'javac.sources' "$(envval javac.sources "${env18}")" 1   'one file under */src/main/java'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${env18}")" "${shelf}" \
   'SRCCP is the whole staged shelf'
eq 'files'         "$(repval files)"         1 'the plugin analysed every file it was handed'

echo "--- C-1: anonymous bodies leaving, lambdas and method references arriving"
# Four anonymous bodies, three of them over a functional interface: the fourth
# is step 03's `new HashMap<String,String>(){..}`, whose supertype has plenty of
# abstract methods. C-1's departure half reads anonymousfunctional.size, so a
# counter that folded the HashMap body in would make the target unreachable.
eq 'anonymous.size'            "$(repval 'anonymous\.size')"            4 'including step 03s HashMap body'
eq 'anonymousfunctional.size'  "$(repval 'anonymousfunctional\.size')"  3 'three are over a SAM'
# ...and only one of the three can become a lambda. C-1's departure half is
# scored on anonymousfunctional.size and NOT on convertible, precisely because
# `convertible` is the number a candidate moves by adding a member. Both are
# asserted so that a change collapsing them together is named here.
eq 'anonymousfunctional.convertible' "$(repval 'anonymousfunctional\.convertible')" 1 \
   'one; the others declare a field and name this'
eq 'anonymousfunctional.forward' "$(repval 'anonymousfunctional\.forward')" 1 \
   'and it forwards its parameter, so it could be a method reference'
eq 'lambda.size'               "$(repval 'lambda\.size')"               5 'three declared, two inside the pipelines'
eq 'lambda.files'              "$(repval 'lambda\.files')"              1 'C-1 arrival reads this'
eq 'lambda.expr'               "$(repval 'lambda\.expr')"               4 'one of the five is a block'
eq 'methodref.size'            "$(repval 'methodref\.size')"            2 'one bound, one static'

# The per-site records, which is what the arrival half actually reads. A total
# cannot answer "which of twenty named files gained a lambda", and the records
# are asserted by their FIELD VALUES rather than by line number so that editing
# the comment at the top of Smoke.java does not break this.
recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep18}"; }
hasrec() { # hasrec <block> <pattern> <why>
   recs "$1" | grep -qF "$2" || fail "$1 has no record matching '$2' -- $3"
   printf '    %-26s %s\n' "$1[..] $2" "$3"
}
norec() { # norec <block> <pattern> <why>
   recs "$1" | grep -qF "$2" && fail "$1 records '$2' and must not -- $3"
   printf '    %-26s %s\n' "no $1 $2" "$3"
}
hasrec anonfn 'convertible=yes why=- forward=yes' 'the convertible body, and it forwards'
hasrec anonfn 'convertible=no why=members'        'the one declaring a field, with the reason'
hasrec anonfn 'convertible=no why=self'           'the one naming this, with the reason'
norec  anonfn 'convertible=no why=members forward=yes' \
   'forward is only ever read for a site the step could ask anyone to convert'
hasrec lambda 'body=expr methodrefable=yes' 'the one lambda still one step short of a ::'
hasrec lambda 'body=block'                  'and the block-bodied one, which is not'
hasrec methodref 'INVOKE shout'   'the bound reference names the method it invokes'
hasrec methodref 'INVOKE valueOf' 'and so does the static one'

echo "--- C-2: the SAM census and the annotation"
eq 'iface.decl'         "$(repval 'iface\.decl')"         3 'three interfaces declared'
# Both declares two abstract methods and is not a SAM. C-2 is scored as a SET
# of (path, Name) pairs and this is the negative that keeps the set honest: a
# candidate cannot satisfy it by annotating everything, because javac rejects
# @FunctionalInterface on Both.
eq 'iface.sam'          "$(repval 'iface\.sam')"          2 'Both has two, so it is not one'
eq 'iface.samannotated' "$(repval 'iface\.samannotated')" 1 'one says so out loud'
eq 'iface.abstract'     "$(repval 'iface\.abstract')"     4 'four abstract methods across the three'
hasrec interfacedecl 'Handler abstract=1 annotated=yes' 'the annotated SAM'
hasrec interfacedecl 'Mapper abstract=1 annotated=no'   'the unannotated SAM'
hasrec interfacedecl 'Both abstract=2 annotated=no'     'and the non-SAM, refused'
# @FunctionalInterface is compiler-enforced, so in a tree whose source.ok is yes
# this can never exceed the SAM count. Asserted as the inequality rather than as
# two numbers, because it is the property C-2's proof of correctness rests on.
[ "$(repval 'iface\.samannotated')" -le "$(repval 'iface\.sam')" ] \
   || fail "samannotated exceeds sam in a tree that compiles -- one of the two counters is wrong"

echo "--- C-3: collecting loops leaving, pipelines arriving"
eq 'foreach.size'      "$(repval 'foreach\.size')"      3 'three enhanced for statements'
# One collects. The other two are the two refusals: a two-statement body, and a
# two-argument put that no stream form improves. A counter matching the name
# `add` alone would report two here.
eq 'foreach.collect'   "$(repval 'foreach\.collect')"   1 'one of them collects into a list'
eq 'foreach.unresolved' "$(repval 'foreach\.unresolved')" 0 'nothing failed to resolve'
eq 'stream.producers'  "$(repval 'stream\.producers')"  2 'two pipelines, entered once each'
eq 'stream.ops'        "$(repval 'stream\.ops')"        4 'four stages between them'
eq 'stream.files'      "$(repval 'stream\.files')"      1 'C-3 arrival reads this'
# A producer is not a pipeline. C-3's arrival half asks for a producer AND a
# stage in the SAME file, so the two file counts are separate keys and both are
# read: `xs.stream();` as a statement would move stream.files and leave this at
# zero, which is the whole reason the second key exists.
eq 'stream.opfiles'    "$(repval 'stream\.opfiles')"    1 'both pipelines are in the one file'
# Six loops, in all four spellings, and the census is shape-blind on purpose.
# C-3's no-reshape term reads this total against the baseline's: a `while` that
# walks the same collection takes `foreach.collect` to zero exactly as far as a
# pipeline does, and the total is the only figure that knows the loop is still
# there. A census that saw only enhanced `for`s would read 3 here and would read
# identically on a tree that respelled its collecting loops.
eq 'loop.size'         "$(repval 'loop\.size')"         6 'three foreach, one for, one while, one do'
# The three ways to miscount a pipeline, each pinned by a record and not by a
# total -- which is the point of the block existing:
#
#   stream()  the producer, a member select on a value
#   range()   a producer whose RECEIVER is a stream type rather than a stream
#             value; a counter reading the receiver's type alone files it as a
#             stage and reports one producer here
#   filter()  inside counted(), a stage that both takes and returns a stream; a
#             counter calling every stream-returning call an entry reports two
#             entries in one pipeline. AND a method merely NAMED filter is
#             declared in this file, so a name-matching counter finds a stage
#             in a method with no pipeline in it at all.
hasrec stream 'ENTER stream' 'the plain producer'
hasrec stream 'ENTER range'  'the static factory, which is an entry and not a stage'
norec  stream 'ENTER filter' 'the stage, and the method merely named filter, are neither'
norec  stream 'ENTER count'  'nor is the terminal operation'
hasrec foreach 'collect=yes' 'the collecting loop is recorded as such'
hasrec foreach 'collect=no'  'and the refusals are recorded, not dropped'
# The four spellings, each named in a record rather than inferred from the
# total. 3 + 1 + 1 + 1 is 6 by four different arrangements, and only one of them
# is a census that visits all four node types.
# Counted on the whole field rather than matched as a substring, because
# hasrec() matches substrings and " for" is a substring of " foreach": a
# spelling check written that way would pass on a census that found nothing but
# enhanced `for`s, which is precisely the census this term exists to be more
# than.
spellcount() { recs loop | awk -v k="$1" '$2 == k' | grep -c . || true; }
eq 'loop foreach' "$(spellcount foreach)" 3 'and the arrangement is 3/1/1/1, not any other'
eq 'loop for'     "$(spellcount for)"     1 'the indexed for, counted once'
eq 'loop while'   "$(spellcount while)"   1 'the while, counted once'
eq 'loop do'      "$(spellcount do)"      1 'the do, counted once and not as a while'
eq 'loop sum'     "$(( $(spellcount foreach) + $(spellcount for) \
                     + $(spellcount while) + $(spellcount do) ))" \
   "$(repval 'loop\.size')" 'the total is the records and not a separate counter'
# `filter(String)` is a method merely NAMED filter, and it is not a stage. Both
# real stages of the first pipeline are, and the terminal operation is a stage
# too -- a stage is any call ON a stream, which is what makes the producer/stage
# split the right one to hang C-3's arrival on.
hasrec streamop 'ON filter' 'the stage inside the pipeline'
hasrec streamop 'ON count'  'the terminal operation is a stage as well'
hasrec streamop 'ON map'    'and the stage of the IntStream pipeline'
norec  streamop 'ON stream' 'the producer is not one of its own stages'
norec  streamop 'ON range'  'nor is the static factory'

echo "--- C-3: and the SCOPE at which departure is paired with arrival"
# Round 2 of this step's static review: pairing at the file was satisfiable
# without a conversion. One `xs.stream().count();` per file -- fourteen
# statements in the real tree -- paid the arrival half in full, and any non-loop
# replacement of the 24 collecting loops took the departure half with it. The
# pairing is per METHOD now, and the two counters below are what the gate
# divides by.
eq 'foreach.collectmethods' "$(repval 'foreach\.collectmethods')" 1 \
   'all three enhanced for statements are in loops(), and one of them collects'
eq 'stream.opmethods'       "$(repval 'stream\.opmethods')"       2 \
   'the two pipelines are in counted() and ranged()'
# Each record carries the method it sits in, and a bucket is <file>#<method>.
# Asserted per record rather than only through the two counts: a plugin that
# wrote a constant in= field would satisfy both counts above and none of these.
hasrec foreach  'collect=yes in=loops'   'the collecting loop names the method holding it'
hasrec foreach  'collect=no in=loops'    'and so do the refusals'
hasrec stream   'ENTER stream in=counted' 'the producer of the first pipeline'
hasrec stream   'ENTER range in=ranged'   'and of the second'
hasrec streamop 'ON filter in=counted'    'a stage, in the method that entered'
hasrec streamop 'ON count in=counted'     'and its terminal operation'
hasrec streamop 'ON map in=ranged'        'the other pipeline stage'
hasrec streamop 'ON count in=ranged'      'and its terminal operation'
norec  foreach  'in=-' 'no collecting loop here sits outside a method'
norec  stream   'in=-' 'and neither does a producer'

# The control, and it is the finding itself. Smoke.java holds a collecting loop
# and two whole pipelines IN THE SAME FILE and in three different methods, so
# the file-scope conjunction pays it and the method-scope conjunction does not.
# This is the tree the round-1 gate would have scored 1.00 on C-3's arrival half
# for code that converts nothing, measured rather than argued.
buckets() { # buckets <block> [substring filter]
   recs "$1" \
      | { if [ -n "${2:-}" ]; then grep -F "$2"; else cat; fi; } \
      | awk '{ split($1, a, ":"); m = "";
               for (i = 2; i <= NF; i++) if (substr($i, 1, 3) == "in=") m = substr($i, 4);
               if (m != "") print a[1] "#" m }' \
      | LC_ALL=C sort -u
}
files() { # files <block> [substring filter]
   buckets "$1" "${2:-}" | awk -F'#' '{ print $1 }' | LC_ALL=C sort -u
}
buckets foreach  'collect=yes' > "${WORK}/bk-collect.txt"
buckets stream   ''            > "${WORK}/bk-prod.txt"
buckets streamop ''            > "${WORK}/bk-stage.txt"
files   foreach  'collect=yes' > "${WORK}/fl-collect.txt"
files   stream   ''            > "${WORK}/fl-prod.txt"
files   streamop ''            > "${WORK}/fl-stage.txt"
covfile=$(LC_ALL=C comm -12 "${WORK}/fl-prod.txt" "${WORK}/fl-stage.txt" \
   | LC_ALL=C comm -12 "${WORK}/fl-collect.txt" - | grep -c . || true)
covmeth=$(LC_ALL=C comm -12 "${WORK}/bk-prod.txt" "${WORK}/bk-stage.txt" \
   | LC_ALL=C comm -12 "${WORK}/bk-collect.txt" - | grep -c . || true)
eq 'file-scope coverage'   "${covfile}" 1 'the round-1 conjunction pays this tree in full'
eq 'method-scope coverage' "${covmeth}" 0 'and the round-2 conjunction pays it nothing'

echo "--- B-10: what step 03 won, and must still be visible here"
# Step 03's half of the file, unchanged, so that a drift in either rung's
# reading of the same construct is a disagreement between two rehearsals rather
# than two separately plausible numbers.
#
# explicit is the one number that differs from step 03's reading, and on purpose:
# loops() declares new ArrayList<String>() as its destination, which is a third
# explicit site. Asserted as 3 with the reason attached rather than quietly
# reconciled.
eq 'newgeneric.explicit'      "$(repval 'newgeneric\.explicit')"      3 'two HashMaps plus loops destination'
eq 'newgeneric.explicitanon'  "$(repval 'newgeneric\.explicitanon')"  1 'one of them has a class body'
# B-10's non-regression term reads explicit - explicitanon and not explicit,
# because an anonymous class that becomes a lambda takes its type arguments with
# it. The subtraction is the gate's, and this is where it is rehearsed.
eq 'explicit - explicitanon' \
   "$(( $(repval 'newgeneric\.explicit') - $(repval 'newgeneric\.explicitanon') ))" 2 \
   'the non-anonymous remainder, which is what B-10 holds'
eq 'newgeneric.diamond'       "$(repval 'newgeneric\.diamond')"       1 'new ArrayList<>()'
eq 'newgeneric.diamondfiles'  "$(repval 'newgeneric\.diamondfiles')"  1 'in one file'
eq 'newgeneric.raw'           "$(repval 'newgeneric\.raw')"           1 'new HashMap(), raw -- the penalty reads this'
eq 'suppress.sites'           "$(repval 'suppress\.sites')"           1 'one @SuppressWarnings'
eq 'suppress.pairs'           "$(repval 'suppress\.pairs')"           2 'carrying two values'
eq 'try.size'                 "$(repval 'try\.size')"                 2 'two try statements'
eq 'try.resources'            "$(repval 'try\.resources')"            1 'one of them has a resource clause'
eq 'try.resourcevars'         "$(repval 'try\.resourcevars')"         1 'declaring one resource'
eq 'close.manual'             "$(repval 'close\.manual')"             1 'the hand-written close() in the finally'
eq 'iofile.new'               "$(repval 'iofile\.new')"               3 'three java.io constructions'
eq 'iofile.decl'             "$(repval 'iofile\.decl')"               1 'and one File-typed variable'
eq 'niofile.ref'             "$(repval 'niofile\.ref')"               2 'Paths.get and Files.exists, both member selects'
eq 'niofile.staticref'       "$(repval 'niofile\.staticref')"         1 'isDirectory(p), through the static import'
eq 'niofile.files'           "$(repval 'niofile\.files')"             1 'in one file'

# The list blocks, not just the totals. A counter that increments without
# recording where is one the trajectory analysis cannot follow back to a line.
for blk in diamond close iofile niofile suppress anonfn lambda methodref stream \
           streamop loop foreach interfacedecl; do
   grep -qE "^${blk}\[0\]=.*Smoke\.java:[0-9]+" "${rep18}" \
      || fail "${blk}[0] does not name a line of Smoke.java"
done

echo "--- the 1.7 probe, C-4's synthetic failure"
e17=$(envval javac.errors "${env17}")
# `not supported in -source`, and NOT step 03's `is not supported in -source`.
# javac inflects the verb with the feature name: the diamond operator "is not
# supported", lambda expressions and method references "are not supported". Step
# 03's pattern matches none of step 04's errors, which cost this rehearsal a
# silent abort -- `grep -c` returns 1 on zero matches, and under `set -e` the
# assignment took the script out without printing a line. The scorer's own C-4
# arithmetic greps for the same thing and carries the same comment.
lvl17=$(grep -c 'not supported in -source 1.7' "${WORK}/src-17.txt.javac.log" || true)
# Two, and not one per lambda: javac reports a level error once per FILE per
# feature. That is exactly why the step's own target.level17.errors is 47 on a
# 322-file tree with 60 lambdas -- 33 files carrying a lambda and 14 carrying a
# method reference -- and getting that wrong is how a threshold of 20 ends up
# either unreachable or free.
eq 'javac.errors@1.7' "${e17}"   2 'one lambda error and one method-reference error'
eq 'level errors'     "${lvl17}" 2 'both of them about the language level'
# Exactly the predicate test.sh computes, run on numbers a compiler produced.
[ "${lvl17}" -gt 0 ] && [ "${lvl17}" = "${e17}" ] \
   || fail "C-4's predicate does not hold on a tree built to satisfy it"

# ...and its negation is reachable: strip the Java 8 half and the same probe
# must stop failing. Without this the rehearsal would pass on a probe that
# reports "not supported" for every tree ever handed to it.
#
# Cut at the section marker and re-close the class, rather than rewriting each
# construct: the result is step 03's smoke file, which is known to compile at
# 1.7, and the cut is one sed address instead of eight fragile substitutions.
mkdir -p "${WORK}/flat/mod/src/main/java/smoke"
sed '/^   \/\/ -* step 04$/,$d' "${SMOKE}/mod/src/main/java/smoke/Smoke.java" \
   > "${WORK}/flat/mod/src/main/java/smoke/Smoke.java"
echo '}' >> "${WORK}/flat/mod/src/main/java/smoke/Smoke.java"
# The cut has to have removed something. A marker that no longer matches would
# leave the file whole, the 1.7 compile would fail, and the failure would read
# as "the control does not work" rather than "the sed address is stale".
flatlines=$(grep -c . "${WORK}/flat/mod/src/main/java/smoke/Smoke.java")
wholelines=$(grep -c . "${SMOKE}/mod/src/main/java/smoke/Smoke.java")
[ "${flatlines}" -lt "${wholelines}" ] \
   || fail "the step-04 section marker did not match -- nothing was cut"

TREE="${WORK}/flat"
flat17=$(srcgate flat17 1.7 || true)
[ "${flat17}" = yes ] || {
   echo "--- javac at 1.7 on the flattened tree ---" >&2
   tail -40 "${WORK}/src-flat17.txt.javac.log" >&2 || :
   fail "the Java 7 half of the same file was rejected at -source 1.7 (source.ok=${flat17})"
}
flat="${WORK}/src-flat17.txt"
fv() { sed -n "s/^$1=//p" "${flat}" | head -1; }
eq 'flattened lambda.size'     "$(fv 'lambda\.size')"     0 'the counters follow the edit'
eq 'flattened methodref.size'  "$(fv 'methodref\.size')"  0 'in both directions'
eq 'flattened samannotated'    "$(fv 'iface\.samannotated')" 0 'and across every family-C gate'
eq 'flattened stream.producers' "$(fv 'stream\.producers')" 0 'not just the one being looked at'
eq 'flattened foreach.collect' "$(fv 'foreach\.collect')" 0 'the collecting loop went with it'
eq 'flattened stream.opfiles'  "$(fv 'stream\.opfiles')"  0 'the stage census follows the cut'
# 6 -> 0. Every loop in this file is in the half that was cut, so the census has
# to empty. A key wired to a constant would read 6 here and the no-reshape term
# would be measuring nothing at all.
eq 'flattened loop.size'       "$(fv 'loop\.size')"       0 'and so does the loop census'
# ...while step 03's constructs are untouched by the cut, which is what makes
# this a control on the Java 8 counters specifically rather than on the plugin.
eq 'flattened diamond'         "$(fv 'newgeneric\.diamond')" 1 'step 03s half survived the cut'
eq 'flattened try.resources'   "$(fv 'try\.resources')"      1 'and is still counted'

echo "src-rehearsal: SourceScan counts a real tree as measured, and C-4's two probes disagree about it"
