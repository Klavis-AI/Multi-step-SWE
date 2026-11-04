#!/usr/bin/env bash
# Build-time rehearsal for family C's instrument: arqgate.SourceScan, the
# javac plugin, driven through the shipped source-gate.sh by the shipped
# srcgate() with the shipped classpath expression.
#
# Why this exists, stated plainly. fixtures/c/ holds nineteen hand-written
# SourceScan reports and c-rehearsal.sh scores each one, which proves that the
# arithmetic over the counters is the arithmetic that was calibrated. It proves
# nothing whatever about the counters, because a hand-written report says
# exactly what it was written to say. The counters are the half that touches
# the candidate's tree, so leaving them unrehearsed would mean the whole of
# family C rests on an instrument no test in the image has ever run.
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
#   * the compile runs twice, at 1.7 and at 1.6, which is C-4's whole
#     mechanism. The clean control is the 1.7 run; its paired synthetic
#     failure is the 1.6 run, which must fail, and must fail with exactly two
#     errors, both of them "is not supported in -source 1.6".
#
# The 1.6 half is the one that cannot be faked from the AST: the counters would
# report the same diamonds and the same resource clause whether or not javac
# would accept the tree at either level.
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
ok17=$(srcgate 17 1.7 || true)
ok16=$(srcgate 16 1.6 || true)
[ "${ok17}" = yes ] || {
   echo "--- javac at 1.7 ---" >&2; tail -40 "${WORK}/src-17.txt.javac.log" >&2 || :
   fail "the smoke tree does not compile at -source 1.7 (source.ok=${ok17})"
}
[ "${ok16}" = no ] || fail "the smoke tree compiled at -source 1.6 (source.ok=${ok16})"

env17="${WORK}/src-17.txt.env"
rep17="${WORK}/src-17.txt"
env16="${WORK}/src-16.txt.env"

envval() { sed -n "s/^$1=//p" "$2" | head -1; }
repval() { sed -n "s/^$1=//p" "${rep17}" | head -1; }

eq() { # eq <what> <got> <want> <why>
   if [ "${2}" != "${3}" ]; then
      fail "${1}: got ${2}, expected ${3} -- ${4}"
   fi
   printf '    %-24s %-4s %s\n' "$1" "$2" "$4"
}

echo "--- the 1.7 compile"
eq 'javac.rc'      "$(envval javac.rc      "${env17}")" 0   'a clean compile'
eq 'javac.errors'  "$(envval javac.errors  "${env17}")" 0   'and no errors'
eq 'javac.sources' "$(envval javac.sources "${env17}")" 1   'one file under */src/main/java'
eq 'javac.cpjars'  "$(envval javac.cpjars  "${env17}")" "${shelf}" \
   'SRCCP is the whole staged shelf'
eq 'files'         "$(repval files)"         1 'the plugin analysed every file it was handed'

echo "--- the counters family C reads"
# new HashMap<String,String>() twice: the plain field and the one with a body.
eq 'newgeneric.explicit'     "$(repval 'newgeneric\.explicit')"     2 'two explicit type-argument sites'
# ...the second of which is anonymous, where <> is Java 9 and not Java 7. C-1
# subtracts these, so a counter that folded them in would make the conversion
# target unreachable.
eq 'newgeneric.explicitanon' "$(repval 'newgeneric\.explicitanon')" 1 'one of them has a class body'
eq 'newgeneric.diamond'      "$(repval 'newgeneric\.diamond')"      1 'new ArrayList<>()'
eq 'newgeneric.explicitfiles' "$(repval 'newgeneric\.explicitfiles')" 1 'in one file'
eq 'newgeneric.diamondfiles' "$(repval 'newgeneric\.diamondfiles')" 1 'C-1 breadth reads this'
# The anti-gaming penalty reads raw. A raw new that counted as a diamond would
# pay a candidate for deleting type arguments.
eq 'newgeneric.raw'          "$(repval 'newgeneric\.raw')"          1 'new HashMap(), raw'
eq 'suppress.sites'          "$(repval 'suppress\.sites')"          1 'one @SuppressWarnings'
eq 'suppress.pairs'          "$(repval 'suppress\.pairs')"          2 'carrying two values'
eq 'suppress.value.rawtypes' "$(repval 'suppress\.value\.rawtypes')" 1 'counted by name'
eq 'suppress.value.unchecked' "$(repval 'suppress\.value\.unchecked')" 1 'and both names'
eq 'try.size'                "$(repval 'try\.size')"                2 'two try statements'
# C-2 reads resources, not try.size: the try/finally must not count.
eq 'try.resources'           "$(repval 'try\.resources')"           1 'one of them has a resource clause'
eq 'try.resourcevars'        "$(repval 'try\.resourcevars')"        1 'declaring one resource'
eq 'close.manual'            "$(repval 'close\.manual')"            1 'the hand-written close() in the finally'
# new File, new FileInputStream(File), new FileInputStream(String).
eq 'iofile.new'              "$(repval 'iofile\.new')"              3 'three java.io constructions'
eq 'iofile.decl'             "$(repval 'iofile\.decl')"             1 'and one File-typed variable'
eq 'niofile.use'             "$(repval 'niofile\.use')"             1 'the Path variable'
# C-3's arrival half reads ref PLUS staticref. A migration that only changes
# declarations moves use and not either of them.
#
# The two counters are two spellings of the same thing, and the split is the
# point. `Files.exists(p)` is a member select on the type, which is what
# niofile.ref counts; `isDirectory(p)` under `import static
# java.nio.file.Files.isDirectory` is a bare identifier with no type anywhere in
# the expression, and ref cannot see it. Round 1 of the static review found that
# a correct migration written in the second style would score C-3's arrival half
# below 1 for spelling. Both are asserted here so that a counter which folded
# them together, or dropped either, is named by the failure.
eq 'niofile.ref'             "$(repval 'niofile\.ref')"             2 'Paths.get and Files.exists, both member selects'
eq 'niofile.staticref'       "$(repval 'niofile\.staticref')"       1 'isDirectory(p), through the static import'
eq 'niofile.files'           "$(repval 'niofile\.files')"           1 'in one file'

# The list blocks, not just the totals. A counter that increments without
# recording where is one the trajectory analysis cannot follow back to a line.
for blk in diamond close iofile niofile suppress; do
   grep -qE "^${blk}\[0\]=.*Smoke\.java:[0-9]+" "${rep17}" \
      || fail "${blk}[0] does not name a line of Smoke.java"
done

echo "--- the 1.6 probe, C-4's synthetic failure"
e16=$(envval javac.errors "${env16}")
lvl16=$(grep -c 'not supported in -source 1.6' "${WORK}/src-16.txt.javac.log")
eq 'javac.errors@1.6' "${e16}"   2 'the diamond and the resource clause'
eq 'level errors'     "${lvl16}" 2 'both of them about the language level'
# Exactly the predicate test.sh computes, run on numbers a compiler produced.
[ "${lvl16}" -gt 0 ] && [ "${lvl16}" = "${e16}" ] \
   || fail "C-4's predicate does not hold on a tree built to satisfy it"
# ...and its negation is reachable: drop the two Java 7 constructs and the same
# probe must stop failing. Without this the rehearsal would pass on a probe
# that reports "not supported" for every tree ever handed to it.
mkdir -p "${WORK}/flat/mod/src/main/java/smoke"
sed -e 's/new ArrayList<>()/new ArrayList<String>()/' \
    -e 's/try (InputStream r = new FileInputStream("y")) {/InputStream r = new FileInputStream("y"); {/' \
    "${SMOKE}/mod/src/main/java/smoke/Smoke.java" \
    > "${WORK}/flat/mod/src/main/java/smoke/Smoke.java"
TREE="${WORK}/flat"
flat16=$(srcgate flat16 1.6 || true)
[ "${flat16}" = yes ] || {
   echo "--- javac at 1.6 on the flattened tree ---" >&2
   tail -40 "${WORK}/src-flat16.txt.javac.log" >&2 || :
   fail "the Java 6 form of the same file was rejected at -source 1.6 (source.ok=${flat16})"
}
flatdia=$(sed -n 's/^newgeneric\.diamond=//p' "${WORK}/src-flat16.txt" | head -1)
flatres=$(sed -n 's/^try\.resources=//p' "${WORK}/src-flat16.txt" | head -1)
eq 'flattened diamond'   "${flatdia}" 0 'the counters follow the edit'
eq 'flattened resources' "${flatres}" 0 'in both directions'

echo "src-rehearsal: SourceScan counts a real tree as measured, and C-4's two probes disagree about it"
