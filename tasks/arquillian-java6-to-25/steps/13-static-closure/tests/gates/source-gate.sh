#!/usr/bin/env bash
# Run arqgate.SourceScan over a whole source tree and write its report.
#
#   source-gate.sh <tree> <out.txt> [<classpath>]
#
#   GATE_CP_LIST  file of repo-relative jar paths, one per line, resolved
#                 against GATE_M2. Used when no classpath argument is given.
#   GATE_M2       jar pool the list resolves against (default ~/.m2/repository)
#   GATE_GATES    the gates directory holding build/arqsource.jar
#   GATE_SCOPE    all (default) | main | test -- which src/ trees to scan
#   GATE_SOURCE   a -source/-target level to compile at, e.g. 1.7. Unset means
#                 this JDK's default, which is what the frozen baseline was
#                 taken at. Setting it turns the gate into a second, independent
#                 instrument: javac's own acceptance of the tree at a named
#                 language level, which no AST counter can be talked out of.
#   JAVAC         the javac to use
#
# A scorer-owned compile, on purpose. The gate never asks the candidate's build
# for a report about the candidate's code: it finds every .java under every
# module's src/, compiles them in one pass with the plugin attached, and reports
# what the plugin saw. Rule 11 wants the whole repo scanned from artifacts and
# ASTs, and a build file is exactly the thing a candidate can edit.
#
# The classpath is given, never guessed. The first version of this script built
# one by globbing the jar pool, and the pool's sort order put junit 3.8.1 ahead
# of 4.8.1: five compile errors, every one of them phrased as a fact about the
# code ("annotation interface not applicable to this kind of declaration").
# Sorting by version instead fixed four of them and broke a fifth differently --
# testenrichers/cdi pins weld-core 1.1.0.Beta1 and 1.1.0.Final dropped the
# package it imports. A pinned list is the only form of this that is not a
# guess; authoring/gates/out/baseline-cp.txt is the pristine tree's, taken from
# `mvn dependency:build-classpath` over all sixteen projects.
#
# The compile's own success is not assumed and is not required: javac may fail
# on some unit and the plugin still writes a report of what it did see. The
# report therefore carries `source.ok`, which is yes only when javac found no
# errors AND the plugin analysed every source file it was handed. A caller that
# scores a "clean tree" without checking it is scoring a broken classpath.
set -uo pipefail

tree="${1:?usage: source-gate.sh <tree> <out.txt> [<classpath>]}"
out="${2:?usage: source-gate.sh <tree> <out.txt> [<classpath>]}"
cp_in="${3:-}"

gates="${GATE_GATES:-$(cd "$(dirname "$0")" && pwd)}"
m2="${GATE_M2:-${HOME}/.m2/repository}"
javac_bin="${JAVAC:-javac}"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

tree="$(cd "${tree}" && pwd)"

# Every module's sources in one pass, main and test alike. Two reasons not to go
# module by module: javac resolves inter-module references from source instead of
# from jars that may not have been built, and one pass means one report rather
# than sixteen that have to be summed without double-counting a shared file.
case "${GATE_SCOPE:-all}" in
   all)  scope=( -path '*/src/main/java/*' -o -path '*/src/test/java/*' ) ;;
   main) scope=( -path '*/src/main/java/*' ) ;;
   test) scope=( -path '*/src/test/java/*' ) ;;
   *) echo "source-gate: GATE_SCOPE must be all, main or test" >&2; exit 2 ;;
esac
# GATE_MODULES: the library and nothing else (wave H round 4, G2-A1). A
# space-separated list of module directories, relative to the tree. When it is
# set, a source is compiled only from a listed module's src/main/java and
# src/test/java (as GATE_SCOPE selects): the instructions keep the library's
# sources there and say build logic is not part of the library. Java build
# logic in an included build (build-logic/src/main/java) is compiled by Gradle
# as a build of its own, and compiled here a plain helper that names its
# Gradle-importing sibling -- which the import rule below drops -- failed
# every scorer compile, so a correct tree read as "no report". test.sh always
# sets it; unset, the whole tree is compiled as before, which is what
# fixtures/src-rehearsal.sh's smoke tree is. A class shipped from a source
# anywhere else is still caught: srcbind requires every module-jar class to be
# one this compile produced, and -sourcepath (below) keeps javac from reaching
# any source this list left out.
roots=()
if [ -n "${GATE_MODULES:-}" ]; then
   read -r -a mods <<< "${GATE_MODULES}"
   for m in "${mods[@]+"${mods[@]}"}"; do
      case "${m}" in
         /*|*..*|*[!A-Za-z0-9._/-]*)
            printf 'source-gate: GATE_MODULES entry %q is not a module directory\n' "${m}" >&2
            exit 2 ;;
      esac
      case "${GATE_SCOPE:-all}" in
         all)  roots+=( "${tree}/${m}/src/main/java/" "${tree}/${m}/src/test/java/" ) ;;
         main) roots+=( "${tree}/${m}/src/main/java/" ) ;;
         test) roots+=( "${tree}/${m}/src/test/java/" ) ;;
      esac
   done
   [ "${#roots[@]}" -gt 0 ] || { echo "source-gate: GATE_MODULES names no module" >&2; exit 2; }
fi
# Build logic is left out, in both of the places Gradle keeps it. Every step's
# instruction says build logic written to drive Gradle is not part of the
# library, and Gradle compiles it as a build of its own against the Gradle API,
# which this compile's classpath does not carry -- so a Java convention plugin
# failed this compile outright and read as "no report", zeroing every
# source-derived gate of a tree whose library was fine.
#   - buildSrc/ is pruned whole (migration rubric review round 2);
#   - a Java source anywhere else that imports the Gradle API -- an included
#     build's `build-logic/src/main/java/...`, the pattern Gradle documents next
#     to buildSrc -- is dropped by that import (review round 3). No library or
#     test source of this project imports org.gradle, and one that did could
#     not compile against this classpath anyway, so the only trees this changes
#     are trees that would otherwise have read "no report" here.
# buildSrc and Gradle-API classes never reach the published jars, and the
# library's and the tests' own sources are held to <module>/src/{main,test}/java
# by the steps' other requirements.
#
# NUL-separated from find to javac, and quoted on the way in (wave H, B7). This
# compile runs as root over names the candidate chose, and javac splits an
# @argfile on whitespace: written one bare path per line, a source whose name
# held a space reached javac as two arguments rather than one path. So the list
# javac reads, sources.args, carries every path in double quotes with `\` and
# `"` backslash-escaped -- the @argfile syntax that JDK 8's parser and the JDK
# 9+ one both accept (measured on javac 8, 11, 17, 21 and 25: names holding a
# space, a tab, `"`, `\`, `#` or `'` each compile as one path). Every listed
# path is "${tree}/...", absolute, so no token can begin with `-`. A name with a
# line break is refused instead of written: a line break ends an argfile token
# even inside quotes, and sources.txt -- which srcbind reads one path per line,
# and which stays the plain list it always was -- would split it in two. A
# refusal reads like a tree that does not compile: source.ok=no, javac never
# run, and javac.refused counting the names.
find "${tree}" -path "${tree}/buildSrc" -prune -o \
   -type f -name '*.java' \( "${scope[@]}" \) -print0 \
   | sort -z > "${work}/sources.all"
: > "${work}/sources.txt"
n_bad=0
bad=
n_out=0
while IFS= read -r -d '' f; do
   # Outside the listed modules: not part of this compile (GATE_MODULES above),
   # so not handed to javac and not listed in .sources -- decided first, since
   # nothing below ever sees the name.
   if [ "${#roots[@]}" -gt 0 ]; then
      inlib=no
      for r in "${roots[@]}"; do
         case "${f}" in "${r}"*) inlib=yes; break ;; esac
      done
      if [ "${inlib}" = no ]; then n_out=$((n_out + 1)); continue; fi
   fi
   case "${f}" in
      *$'\n'*|*$'\r'*) n_bad=$((n_bad + 1)); bad="${f}"; continue ;;
      "${tree}"/*) ;;
      *) n_bad=$((n_bad + 1)); bad="${f}"; continue ;;
   esac
   if LC_ALL=C grep -qE '^[[:space:]]*import[[:space:]]+(static[[:space:]]+)?org\.gradle\.' -- "${f}"; then
      continue
   fi
   printf '%s\n' "${f}" >> "${work}/sources.txt"
done < "${work}/sources.all"
n_src="$(wc -l < "${work}/sources.txt")"
[ "${n_out}" -eq 0 ] \
   || echo "source-gate: ${n_out} source(s) outside GATE_MODULES left out of this compile" >&2
if [ "${n_bad}" -gt 0 ]; then
   {
      echo "javac.rc=refused"
      echo "javac.level=${GATE_SOURCE:-default}"
      echo "javac.sources=${n_src}"
      echo "javac.refused=${n_bad}"
      echo "javac.errors=0"
      echo "javac.warnings=0"
      echo "javac.classes=0"
      echo "source.ok=no"
   } | tee "${out}.env"
   printf 'source-gate: error: %s source path(s) refused, javac not run: a path with a line break (or outside the tree) cannot be passed as one argument, e.g. %q\n' \
      "${n_bad}" "${bad}" | tee "${out}.javac.log" >&2
   : > "${out}.sources"
   : > "${out}.classes"
   exit 3
fi
if [ "${n_src}" -eq 0 ]; then
   echo "source-gate: no sources under ${tree}" >&2
   exit 2
fi
sed -e 's/[\\"]/\\&/g' -e 's/^/"/' -e 's/$/"/' "${work}/sources.txt" > "${work}/sources.args"

if [ -z "${cp_in}" ]; then
   list="${GATE_CP_LIST:-}"
   if [ -z "${list}" ] || [ ! -f "${list}" ]; then
      echo "source-gate: no classpath argument and no GATE_CP_LIST; refusing to guess" >&2
      exit 2
   fi
   missing=0
   : > "${work}/cp.txt"
   while IFS= read -r rel; do
      [ -n "${rel}" ] || continue
      case "${rel}" in '#'*) continue ;; esac
      if [ -f "${m2}/${rel}" ]; then
         echo "${m2}/${rel}" >> "${work}/cp.txt"
      else
         echo "source-gate: not in ${m2}: ${rel}" >&2
         missing=$((missing + 1))
      fi
   done < "${list}"
   # A jar that is not there shortens the classpath silently and turns into
   # compile errors somewhere else entirely. Refuse instead.
   if [ "${missing}" -gt 0 ]; then
      echo "source-gate: ${missing} jar(s) from ${list} are not in the pool" >&2
      exit 2
   fi
   cp_in="$(tr '\n' ':' < "${work}/cp.txt")"
fi

mkdir -p "${work}/classes"
# -proc:none so nothing on the processor path is mistaken for an annotation
# processor; javac plugins are loaded by a separate ServiceLoader and are
# unaffected. -nowarn because the gate reads the plugin's report, never javac's
# console output -- the whole point of the source half is that the warning
# stream can be silenced by the code under test.
#
# GATE_LINT, added for step 13 and ADDITIVE: unset, it expands to the -nowarn
# this line has always carried, so every rung staged before step 13 runs the
# identical javac command and reads the identical report. Step 13 sets it to
# `-Xlint:all -Xlint:-deprecation` because that rung's subject IS javac's
# console output -- and the sentence above is the reason that leg cannot stand
# alone: the warning stream can be silenced by the code under test, so step 13
# scores it only paired with SourceScan's suppress.sites, which cannot be.
# -deprecation is carved out because the family is chain-dependent (NOTES
# 16.89): the two dependency chains resolve junit:junit at 4.8.1 and 4.13.2 and
# the two versions deprecate different members, so the count is legal at two
# values and no baseline can name one.
# -sourcepath is pinned to the tree, and is not decoration. javac's default
# source path is the class path, so a dependency jar that also ships its sources
# gets implicitly compiled: mockito-all-1.8.3 carries 469 .java files and
# org.osgi.core-4.2.0 carries 50, and the gate would report findings from
# somebody else's code as if they were the candidate's. (Measured -- the first
# such unit crashed the plugin, which is the only reason it was noticed.)
# With GATE_MODULES set it is an empty directory instead of the tree, so javac
# compiles exactly the sources listed here and reaches no other: a reference to
# one left out -- outside the modules, or dropped by the import rule -- fails
# the compile, rather than compiling code this gate never listed or scanned.
# -source/-target together, never -source alone: javac accepts `-source 1.6`
# while still emitting the current default's class files, and a probe that let
# that through would be asking about the parser and not about the tree.
srcpath="${tree}"
if [ "${#roots[@]}" -gt 0 ]; then
   srcpath="${work}/nosrc"
   mkdir -p "${srcpath}"
fi
level=()
if [ -n "${GATE_SOURCE:-}" ]; then
   level=( -source "${GATE_SOURCE}" -target "${GATE_SOURCE}" )
fi

# LC_ALL=C, and not housekeeping. Two things downstream read javac's English:
# `grep -c ': error:'` here, and the scorer's count of
# "is not supported in -source 1.6", which is the whole of C-4's second half.
# javac localises both strings, so a verifier image built where the JDK picks up
# a non-English locale would report 0 errors on a tree that does not compile and
# 0 level errors on a tree that flips exactly as required -- a silent green.
LC_ALL=C LANG=C "${javac_bin}" ${GATE_LINT:--nowarn} -proc:none -encoding UTF-8 \
   "${level[@]+"${level[@]}"}" \
   -cp "${cp_in}" \
   -sourcepath "${srcpath}" \
   -processorpath "${gates}/build/arqsource.jar" \
   "-Xplugin:ArqSourceScan ${out} ${tree}" \
   -d "${work}/classes" \
   @"${work}/sources.args" > "${work}/javac.log" 2>&1
rc=$?

cp "${work}/javac.log" "${out}.javac.log"

# Two lists beside the report, for the scorer to bind this compile to what the
# candidate's build published (wave D, G14): the sources this compile was
# handed, and every class file it wrote. Additive -- nothing above reads them,
# and the report and .env are the same bytes they always were.
cp "${work}/sources.txt" "${out}.sources"
( cd "${work}/classes" && find . -type f -name '*.class' | sed 's|^\./||' | LC_ALL=C sort ) \
   > "${out}.classes" 2>/dev/null || : > "${out}.classes"
errors="$(grep -c ': error:' "${work}/javac.log")"
# Counted unconditionally and reported as 0 under the default -nowarn, so the
# key exists on every rung and a reader never has to know whether GATE_LINT was
# set. LC_ALL=C above is what makes ': warning: [' a reliable match.
warnings="$(grep -c ': warning: \[' "${work}/javac.log")"

# The report is written from the plugin's finished() hook, and that hook does
# not run when javac aborts -- which is exactly what a compile at a level the
# tree has outgrown does. So "no report" is a fact ABOUT THE TREE at this level,
# and it is written out as one: an .env saying source.ok=no, with the error
# count that produced it. Exiting here without writing the .env is what the
# first version did, and it left a caller unable to tell a tree that will not
# compile from a gate that was never wired up.
if [ ! -s "${out}" ]; then
   {
      echo "javac.rc=${rc}"
      echo "javac.level=${GATE_SOURCE:-default}"
      echo "javac.sources=${n_src}"
      echo "javac.cpjars=$(printf '%s' "${cp_in}" | tr ':' '\n' | grep -c '\.jar$')"
      echo "javac.errors=${errors}"
      echo "javac.warnings=${warnings}"
      echo "javac.classes=0"
      echo "source.ok=no"
   } | tee "${out}.env"
   echo "source-gate: the plugin wrote no report (javac rc=${rc}, ${errors} error(s)); last 40 lines:" >&2
   tail -40 "${work}/javac.log" >&2
   exit 3
fi

files="$(sed -n 's/^files=//p' "${out}")"
# rc is part of the health check, not just errors. A plugin that throws takes
# javac down with rc=4 while leaving a report that looks complete; the first run
# of this gate did exactly that and still printed source.ok=yes.
okflag=no
if [ "${rc}" -eq 0 ] && [ "${errors}" -eq 0 ] && [ "${files}" = "${n_src}" ]; then
   okflag=yes
fi

{
   echo "javac.rc=${rc}"
   echo "javac.level=${GATE_SOURCE:-default}"
   echo "javac.sources=${n_src}"
   echo "javac.cpjars=$(printf '%s' "${cp_in}" | tr ':' '\n' | grep -c '\.jar$')"
   echo "javac.errors=${errors}"
   echo "javac.warnings=${warnings}"
   echo "javac.classes=$(find "${work}/classes" -name '*.class' | wc -l)"
   echo "source.ok=${okflag}"
} | tee "${out}.env"

# The console summary, for a human watching a gate run. Every key the report
# carries is listed: a key that is collected and not printed here is one a
# reader concludes is absent (measured -- the Java 7 facts were assumed missing
# for exactly this reason, having been in the report file all along). The
# report file, not this, is what the scorer reads.
grep -E '^(files|anonymous|anonymousfunctional|anonymousunresolved|suppress|native|newgeneric|try|close|iofile|niofile)[.=]' "${out}" \
   | grep -v '\[' || true
exit 0
