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
#   * the compile runs at 1.8 and at 1.7 and the two must disagree.
#
# WHAT STEP 05 ADDS TO STEP 04'S VERSION OF THIS FILE
# ---------------------------------------------------
# Eight record blocks that did not exist below this rung -- retnull, optret,
# optop, accum, collect, objects, jtime, millis -- and a different relationship
# to them. Step 04's gates could lean on totals, because `foreach.collect`
# falling from 24 to 0 is a fact about 24 specific loops. Nothing here is like
# that: `retnull.size` falling from 42 to 26 is satisfied by deleting sixteen
# `return null;` statements, and `optret.size` rising from 0 to 16 is satisfied
# by sixteen new methods in a file of one's own. So every gate at this rung is
# scored over a frozen set of <file>#<method> BUCKETS, and what has to be
# rehearsed is not that a counter increments but that each record carries the
# path, the method and the discriminating field that puts it in the right
# bucket with the right answer. Every block below is therefore asserted twice:
# once as a total, and once as the specific records that make the total mean
# what the gate reads it as.
#
# TWO FILES, AND THAT IS NOT DECORATION. `retnull.files`, `optret.files` and
# `jtime.files` all read 1 on any one-file tree whatever distinctFiles() does,
# and every gate at this rung intersects <file>#<method> buckets, so a one-file
# rehearsal would hold the file component of every key constant and could not
# tell a correct key from one that drops the path. Lib.java also carries the
# near misses that need a NAME without an ELEMENT -- a class of one's own called
# Optional, another called Objects, a static method called toList -- which is
# the whole of what "SourceScan resolves through javac's elements" buys, and it
# cannot be said in the file where the real imports are.
#
# THE FLATTEN CONTROL IS STRONGER HERE THAN IT WAS AT STEP 04. Smoke.java is
# step 04's Smoke.java with a section appended, verbatim, so cutting at the
# step-05 marker and dropping Lib.java must reproduce step 04's OWN frozen
# figures exactly -- not "zero for the new counters", which a constant would
# also produce, but twenty-six numbers a different rung measured on a different
# day. That is a cross-rung control on counter drift, and it is the reason the
# step-04 half of the file is copied rather than rewritten.
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
# build and this image is a JDK 8, so a staging line that pulled it in would
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
   printf '    %-30s %-4s %s\n' "$1" "$2" "$4"
}

recs() { sed -n "s/^$1\[[0-9]*\]=//p" "${rep18}"; }
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

echo "--- the 1.8 compile"
eq 'javac.rc'      "$(envval javac.rc      "${env18}")" 0   'a clean compile'
eq 'javac.errors'  "$(envval javac.errors  "${env18}")" 0   'and no errors'
eq 'javac.sources' "$(envval javac.sources "${env18}")" 2   'two files under */src/main/java'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${env18}")" "${shelf}" \
   'SRCCP is the whole staged shelf'
eq 'javac.classes' "$(envval javac.classes "${env18}")" 17  'and the nested types were all written'
eq 'files'         "$(repval files)"         2 'the plugin analysed every file it was handed'

# ============================================================== C-1: Optional
#
# Three halves: departure (a bucket holds no retnull record), arrival (a bucket
# declares a method returning java.util.Optional) and consumption (the share of
# instance calls ON an Optional that are not get()).
echo "--- C-1: the null-returning lookup helpers, and what replaces them"
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
# C-1 is confined to the PRIVATE population exactly because B-6 is an exact set
# (want=1147 hit=1147 extra=0) and widening a published signature is charged
# there like deleting it -- so this pair of fields is the one the instruction's
# scope rests on.
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
# it would read four here and a candidate could pay C-1 by declaring a class.
norec optret 'name=impostors' 'a class called Optional is not java.util.Optional'

eq 'optop.size'    "$(repval 'optop\.size')"    14 'five make, nine use'
eq 'optop.methods' "$(repval 'optop\.methods')" 6  'across six buckets'
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
# a rename: `findMatch(..).get()` is `findMatch(..)` with more syntax.
recs optop | awk '$2 == "use"' > "${WORK}/optuse.txt"
optuse=$(grep -c . "${WORK}/optuse.txt" || true)
optget=$(awk '$3 == "get"' "${WORK}/optuse.txt" | grep -c . || true)
eq 'optop uses'  "${optuse}" 9 'nine instance calls on an Optional'
eq 'optop gets'  "${optget}" 1 'one of which is get()'
cons=$(awk -v n="$(( optuse - optget ))" -v d="${optuse}" \
   'BEGIN { printf "%.6f", (d <= 0) ? 0 : n / d }')
eq 'C-1 consumption share' "${cons}" 0.888889 '8 of 9 -- a tree of all-get() reads 0.000000'
# `unwrapped()` is `if (o.isPresent()) return o.get();` written as a ternary: two
# uses, one of them get(), which is the 0.5 the gate's comment names as the right
# answer for a tree that moved the null check rather than removing it. Both its
# records are in one bucket, so the share is computable per bucket as well as
# over the tree, and this is the fixture that says so.
ubuck=$(recs optop | awk '$0 ~ /in=unwrapped/' | grep -c . || true)
ubget=$(recs optop | awk '$0 ~ /in=unwrapped/ && $3 == "get"' | grep -c . || true)
eq 'unwrapped uses' "${ubuck}" 2 'isPresent and get, in one bucket'
eq 'unwrapped gets' "${ubget}" 1 'so that bucket alone reads 0.5'

# ============================================================ C-2: Collectors
#
# Departure: of the wanted buckets, how many hold no accum record. Fidelity: how
# many hold a collect record whose factory is ACCEPTED for the factory
# baseline/sinks.txt names.
echo "--- C-2: the accumulate sites step 04 left standing, and the collectors"
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

# =============================================================== C-3: Objects
echo "--- C-3: java.util.Objects in the value types"
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
eq 'C-3 files migrated' "${vt_hit}" 1 'Smoke.java yes; Lib.java uses hashCode, not hash'

# ================================================================ C-4: time
echo "--- C-4: java.time where the epoch-millis calls were"
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
echo "--- B-11: step 04's constructs, and their co-existence with this step's C-2"
eq 'lambda.size'      "$(repval 'lambda\.size')"      15 'five from step 04, ten more here'
eq 'lambda.files'     "$(repval 'lambda\.files')"     1  'all of them in Smoke.java'
eq 'methodref.size'   "$(repval 'methodref\.size')"   13 'two from step 04, eleven more'
eq 'iface.samannotated' "$(repval 'iface\.samannotated')" 1 'one @FunctionalInterface'
eq 'iface.sam'        "$(repval 'iface\.sam')"        3  'Handler, Mapper, Lookup'
eq 'iface.decl'       "$(repval 'iface\.decl')"       4  'Both has two abstract methods'
eq 'iface.default'    "$(repval 'iface\.default')"    1  'Lookup#byDefault'
# @FunctionalInterface is compiler-enforced, so in a tree whose source.ok is yes
# this can never exceed the SAM count. Asserted as the inequality rather than as
# two numbers, because it is the property B-11's second sub-gate rests on.
[ "$(repval 'iface\.samannotated')" -le "$(repval 'iface\.sam')" ] \
   || fail "samannotated exceeds sam in a tree that compiles -- one of the two counters is wrong"
eq 'foreach.collectmethods' "$(repval 'foreach\.collectmethods')" 2 'loops() and unwound()'
eq 'stream.opmethods'       "$(repval 'stream\.opmethods')"       10 'ten buckets hold a stage'

# The stream census, and the finding in it. Collectors.toList() files as a
# stream PRODUCER, because java.util.stream.Collector lives in the
# java.util.stream package and isStream() is package-based. That is why
# stream.producers reads 17 on a tree with 9 pipelines in it.
#
# IT REACHES NO GATE, and that is measured rather than assumed. At this rung
# stream.producers, stream.ops, stream.files and stream.opfiles are published as
# facts only; the one gate that reads stream records is B-11's third sub-gate,
# which asks for a producer AND an op in the same bucket, and a bare Collectors
# call supplies no op. Lib.java#toList is the fixture: it holds `ENTER toList`
# and nothing else, and it must not be a covered bucket.
#
# Changing SourceScan to exclude Collector would mean rebuilding and
# re-verifying every verifier from step 03 up, so the behaviour is frozen here
# as a known fact instead. NOTES 10.6.
eq 'stream.producers' "$(repval 'stream\.producers')" 17 'nine entries plus eight Collectors calls'
hasrec stream 'ENTER toList in=toList' \
   'Collectors.toList() files as a producer -- known, frozen, and reaches no gate'
hasrec stream 'ENTER stream in=collected' 'the real producer of the same pipeline'
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
# regression gate that goes RED for doing the CURRENT step correctly. Step 04
# left 24 accumulate sites standing inside the pipelines it built, and this
# step's C-2 converts 23 of them. If converting one took its bucket out of
# B-11's third sub-gate, the golden would lose 0.60 of B-11 for doing what the
# instruction asks. Both shapes are in this tree and both must be covered.
covered_buckets 'Smoke.java#accumulate' \
   || fail "an UNCONVERTED accumulate bucket is not covered -- B-11's denominator is wrong"
covered_buckets 'Smoke.java#collected' \
   || fail "a CONVERTED bucket lost its B-11 coverage -- C-2 and B-11 would fight"
covered_buckets 'Smoke.java#collectedOrdered' \
   || fail "toCollection breaks B-11 coverage"
printf '    %-30s %s\n' 'B-11 vs C-2' \
   'accumulate (not converted) and collected (converted) are both covered'
# ...and the control, without which the two assertions above are satisfied by a
# gate that says yes to everything. unwound() is a collecting enhanced-for with
# no pipeline at all: it holds a foreach record and no stream record, so it is
# the bucket B-11's third sub-gate exists to refuse.
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
# 9 and not step 04's 1: the sinks and collectors this step's half constructs are
# all diamonds. B-10's non-regression term reads explicit, not diamond, so this
# rises freely -- asserted anyway, because a term that read diamond would make
# every step above this one unreachable and the number is where that shows.
eq 'newgeneric.diamond'      "$(repval 'newgeneric\.diamond')"      9 'step 05s sinks are all diamonds'
eq 'newgeneric.diamondfiles' "$(repval 'newgeneric\.diamondfiles')" 2 'in both files'
eq 'newgeneric.raw'          "$(repval 'newgeneric\.raw')"          1 'new HashMap(), raw -- the penalty reads this'
eq 'suppress.sites'          "$(repval 'suppress\.sites')"          1 'one @SuppressWarnings'
eq 'suppress.pairs'          "$(repval 'suppress\.pairs')"          2 'carrying two values'
eq 'try.resources'           "$(repval 'try\.resources')"           1 'one try-with-resources'
eq 'close.manual'            "$(repval 'close\.manual')"            1 'the hand-written close() in the finally'
eq 'iofile.new'              "$(repval 'iofile\.new')"              3 'three java.io constructions'
eq 'niofile.ref'             "$(repval 'niofile\.ref')"             2 'Paths.get and Files.exists'
eq 'niofile.staticref'       "$(repval 'niofile\.staticref')"       1 'isDirectory(p), through the static import'

# The list blocks, not just the totals. A counter that increments without
# recording where is one the trajectory analysis cannot follow back to a line.
for blk in diamond close iofile niofile suppress anonfn lambda methodref stream \
           streamop loop foreach interfacedecl retnull optret optop accum \
           collect objects jtime millis; do
   grep -qE "^${blk}\[0\]=.*\.java:[0-9]+" "${rep18}" \
      || fail "${blk}[0] does not name a line of a source file"
done

# ============================================== the 1.7 probe and its negation
echo "--- the 1.7 probe"
e17=$(envval javac.errors "${env17}")
# Four errors and not more: javac reports a level error once per FILE per
# feature, so two files carrying lambdas and method references between them give
# 1 method-reference error in Lib.java and lambda + method-reference + default-
# method errors in Smoke.java.
lvl17=$(grep -c 'not supported in -source 1.7' "${WORK}/src-17.txt.javac.log" || true)
eq 'javac.errors@1.7' "${e17}"   4 'one per file per feature'
eq 'level errors'     "${lvl17}" 4 'and every one of them is about the language level'
# At this rung `gate.src.ok17` is published as a FACT and is not a term in any
# gate: step 04's C-4 asked whether the tree requires Java 8 and B-4 asks the
# compiled form of that question at full weight, so re-charging it here would
# pay twice. The probe is rehearsed anyway, because a fact in facts.txt that is
# wrong is a fact the trajectory analysis will reason from.
[ "${lvl17}" -gt 0 ] && [ "${lvl17}" = "${e17}" ] \
   || fail "the 1.7 probe failed for some reason other than the language level"

# ================================================== the two flatten controls
#
# Cut at a section marker and re-compile. Rewriting each construct would be
# eight fragile substitutions; the marker is one sed address and the result is a
# tree a lower rung already measured.
flatten() { # flatten <marker> <dir> [keep-lib]
   local marker=$1 dir=$2
   mkdir -p "${dir}/mod/src/main/java/smoke"
   sed "/^   \/\/ -* step ${marker}\$/,\$d" \
      "${SMOKE}/mod/src/main/java/smoke/Smoke.java" \
      > "${dir}/mod/src/main/java/smoke/Smoke.java"
   echo '}' >> "${dir}/mod/src/main/java/smoke/Smoke.java"
   # The cut has to have removed something. A marker that no longer matches
   # would leave the file whole and the failure would read as "the control does
   # not work" rather than "the sed address is stale".
   local cut whole
   cut=$(grep -c . "${dir}/mod/src/main/java/smoke/Smoke.java")
   whole=$(grep -c . "${SMOKE}/mod/src/main/java/smoke/Smoke.java")
   [ "${cut}" -lt "${whole}" ] \
      || fail "the step-${marker} section marker did not match -- nothing was cut"
}

echo "--- the flatten control: cutting step 05 off must reproduce step 04's tree"
# Lib.java is not copied. Smoke.java's step-03 and step-04 halves are step 04's
# own file verbatim, so what is left differs from the tree step 04 measured only
# in the import block and the header javadoc, neither of which is counted. Every
# figure below was frozen by step 04's rehearsal on a different day, and they are
# asserted here rather than re-derived: a counter that drifted between the two
# rungs is a disagreement between two measurements, not two plausible numbers.
flatten 05 "${WORK}/f05"
TREE="${WORK}/f05"
f05=$(srcgate f05 1.8 || true)
[ "${f05}" = yes ] || {
   echo "--- javac at 1.8 on the flattened tree ---" >&2
   tail -40 "${WORK}/src-f05.txt.javac.log" >&2 || :
   fail "step 04's own smoke tree was rejected at 1.8 (source.ok=${f05})"
}
fv() { sed -n "s/^$1=//p" "${WORK}/src-f05.txt" | head -1; }
eq 'flat files'                "$(fv 'files')"                        1 'one file again'
eq 'flat anonymous.size'       "$(fv 'anonymous\.size')"              4 "step 04's figure"
eq 'flat anonymousfunctional'  "$(fv 'anonymousfunctional\.size')"    3 "step 04's figure"
eq 'flat .convertible'         "$(fv 'anonymousfunctional\.convertible')" 1 "step 04's figure"
eq 'flat .forward'             "$(fv 'anonymousfunctional\.forward')" 1 "step 04's figure"
eq 'flat lambda.size'          "$(fv 'lambda\.size')"                 5 "step 04's figure"
eq 'flat lambda.expr'          "$(fv 'lambda\.expr')"                 4 "step 04's figure"
eq 'flat methodref.size'       "$(fv 'methodref\.size')"              2 "step 04's figure"
eq 'flat iface.decl'           "$(fv 'iface\.decl')"                  3 "step 04's figure"
eq 'flat iface.sam'            "$(fv 'iface\.sam')"                   2 "step 04's figure"
eq 'flat iface.samannotated'   "$(fv 'iface\.samannotated')"          1 "step 04's figure"
eq 'flat iface.abstract'       "$(fv 'iface\.abstract')"              4 "step 04's figure"
eq 'flat foreach.size'         "$(fv 'foreach\.size')"                3 "step 04's figure"
eq 'flat foreach.collect'      "$(fv 'foreach\.collect')"             1 "step 04's figure"
eq 'flat foreach.collectmethods' "$(fv 'foreach\.collectmethods')"    1 "step 04's figure"
eq 'flat loop.size'            "$(fv 'loop\.size')"                   6 "step 04's figure"
eq 'flat stream.producers'     "$(fv 'stream\.producers')"            2 "step 04's figure"
eq 'flat stream.ops'           "$(fv 'stream\.ops')"                  4 "step 04's figure"
eq 'flat stream.opmethods'     "$(fv 'stream\.opmethods')"            2 "step 04's figure"
eq 'flat newgeneric.explicit'  "$(fv 'newgeneric\.explicit')"         3 "step 04's figure"
eq 'flat newgeneric.diamond'   "$(fv 'newgeneric\.diamond')"          1 "step 04's figure"
eq 'flat newgeneric.raw'       "$(fv 'newgeneric\.raw')"              1 "step 04's figure"
eq 'flat try.resources'        "$(fv 'try\.resources')"               1 "step 04's figure"
eq 'flat close.manual'         "$(fv 'close\.manual')"                1 "step 04's figure"
eq 'flat iofile.new'           "$(fv 'iofile\.new')"                  3 "step 04's figure"
eq 'flat niofile.ref'          "$(fv 'niofile\.ref')"                 2 "step 04's figure"
eq 'flat niofile.staticref'    "$(fv 'niofile\.staticref')"           1 "step 04's figure"
# ...and every counter this step introduced goes to zero. Not "goes down": to
# zero, because every construct that moves one of them is in the half that was
# cut or in the file that was not copied. A key wired to a constant reads the
# full-tree figure here.
for k in retnull.size retnull.methods retnull.files retnull.api \
         optret.size optret.files optop.size optop.methods optop.get \
         accum.size accum.methods collect.size collect.methods \
         collect.tolist collect.toset collect.tocollection collect.other \
         objects.size objects.methods jtime.size jtime.files jtime.methods \
         millis.size millis.methods; do
   got=$(fv "$(printf '%s' "${k}" | sed 's/\./\\./g')")
   [ "${got:-0}" = 0 ] || fail "flattened ${k} is ${got}, not 0 -- the counter is not following the edit"
done
printf '    %-30s %s\n' 'flat step-05 counters' 'all 24 keys read 0'

echo "--- the 1.7 control: the same probe must be capable of saying yes"
# Without this the rehearsal would pass on a probe that reports "not supported"
# for every tree ever handed to it. Cut at the step-04 marker instead and the
# remainder is step 03's file, which has no Java 8 construct in it at all.
flatten 04 "${WORK}/f04"
TREE="${WORK}/f04"
f04=$(srcgate f04 1.7 || true)
[ "${f04}" = yes ] || {
   echo "--- javac at 1.7 on the step-03 tree ---" >&2
   tail -40 "${WORK}/src-f04.txt.javac.log" >&2 || :
   fail "the Java 7 half of the same file was rejected at 1.7 (source.ok=${f04})"
}
printf '    %-30s %s\n' 'ok17 on the step-03 half' 'yes -- the probe discriminates'

echo "src-rehearsal: SourceScan counts a real two-file tree as measured, and every step-05 bucket key carries its path, its method and its discriminating field"
