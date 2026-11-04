#!/usr/bin/env bash
# Build the gate tools. Run in the authoring sandbox and, unchanged, inside each
# step's verifier image at image-build time.
#
#   build.sh [<gates-dir>]
#
# Up to three jars, because they have different floors:
#
#   build/arqgate.jar         Scan, and -- where the payload carries them --
#                             Survey and MakeControl. Targeted at 8, so the same
#                             jar runs in the JDK 8 images of steps 1-2 and in
#                             the JDK 25 image of step 13. Needs only ASM.
#
#   build/arqsource.jar       SourceScan, a javac plugin. Compiled against the
#                             building JDK's own com.sun.source, with no
#                             --release: the Compiler Tree API is not part of
#                             any --release API surface, and a plugin has to
#                             match the javac it plugs into anyway. Used from
#                             step 3 on, where the floor is JDK 11.
#
#   build/arqrewrite.jar      The OpenRewrite driver, --release 17. Used from
#                             step 9 on.
#
# Each is built only when its source is in the payload, and each is verified to
# contain the class it exists for. A verifier image stages the gates it uses and
# no others -- step 1 grades a class-file major version and has no use for 101 MB
# of recipe jars -- so "source absent" is a normal condition here. What is not
# normal is a jar that built from nothing: an empty arqgate.jar would load, run,
# and report a clean tree forever, so the contents are asserted rather than
# assumed.
#
# ASCII-only sources throughout, so no -encoding flag is needed and none can be
# forgotten.
set -euo pipefail

gates="${1:-$(cd "$(dirname "$0")" && pwd)}"
src="${gates}/src"
lib="${gates}/lib"
build="${gates}/build"
javac_bin="${JAVAC:-javac}"

rm -rf "${build}"
mkdir -p "${build}/classes-bytecode"

# --release is a JDK 9 flag. Steps 1-2 build this jar with the JDK 8 javac in
# their own verifier image, where it does not exist; -source/-target 8 is what
# that javac calls the same thing. Probed rather than inferred from a version
# string, because the question is whether this javac accepts the flag.
if "${javac_bin}" --release 8 -version > /dev/null 2>&1; then
   eight=( --release 8 )
else
   # -Xlint:-options silences "bootstrap classpath not set", which is noise when
   # the compiling JDK *is* 8.
   eight=( -source 8 -target 8 -Xlint:-options )
fi

# unzip -l into a file, never into a pipe.
#
# `unzip -l x.jar | grep -q PATTERN` looks like an assertion and is really a
# race: grep -q exits at its first match, unzip may still be writing, and the
# EPIPE that follows becomes the pipeline's status under `set -o pipefail`
# (which is set above). Whether it fires depends on how much of the listing fit
# in the pipe buffer before grep left -- so the check passes for a name near the
# end of a jar and fails for the same name near the start, on a loaded machine
# and not on an idle one. An assertion that is wrong sometimes is worse than one
# that is wrong always.
jar_has()
{
   local jar="$1" pattern="$2" listing
   listing="$(mktemp)"
   unzip -l "${jar}" > "${listing}"
   local rc=0
   grep -q "${pattern}" "${listing}" || rc=1
   rm -f "${listing}"
   return "${rc}"
}

# ------------------------------------------------------------------ bytecode

[ -f "${src}/arqgate/Scan.java" ] || { echo "build.sh: ${src}/arqgate/Scan.java is missing" >&2; exit 1; }
# Scan is mandatory; everything else in the payload is compiled because it is
# there. stage.sh decides what a step gets, so an enumerated list here would be
# a second place to keep that decision -- and the two would drift. What is NOT
# optional is that every source present turns into a class: the assertion loop
# below is generated from this same array, so adding a gate source cannot
# silently produce a jar that does not contain it.
bytecode_src=( "${src}/arqgate/Scan.java" )
bytecode_cls=( arqgate/Scan.class )
for f in "${src}"/arqgate/*.java; do
   base="$(basename "${f}" .java)"
   [ "${base}" = Scan ] && continue
   # SourceScan, Plan and Rewrite are compiled by their own sections below,
   # against different classpaths (the compiler plugin API, the OpenRewrite
   # jars).
   case "${base}" in SourceScan|Plan|Rewrite) continue;; esac
   bytecode_src+=( "${f}" )
   bytecode_cls+=( "arqgate/${base}.class" )
done
echo "gates: bytecode jar from ${#bytecode_src[@]} source(s): ${bytecode_cls[*]}"

"${javac_bin}" "${eight[@]}" -nowarn \
   -cp "${lib}/asm-9.10.1.jar:${lib}/asm-tree-9.10.1.jar" \
   -d "${build}/classes-bytecode" \
   "${bytecode_src[@]}"

jar cf "${build}/arqgate.jar" -C "${build}/classes-bytecode" .
for cls in "${bytecode_cls[@]}"; do
   jar_has "${build}/arqgate.jar" " ${cls}\$" \
      || { echo "build.sh: arqgate.jar has no ${cls}" >&2; exit 1; }
done

# -------------------------------------------------------------------- source
#
# javac finds a plugin through ServiceLoader on the processor path, so the
# services entry is not optional decoration -- without it, -Xplugin:ArqSourceScan
# fails with "plugin not found" and a gate that was never asked to run reports
# a clean tree.

if [ -f "${src}/arqgate/SourceScan.java" ]; then
   mkdir -p "${build}/classes-source"
   # com.sun.source lives in rt.jar's sibling tools.jar on JDK 8 and in the
   # jdk.compiler module from JDK 9 on. So on 8 -- and only on 8 -- the Compiler
   # Tree API is not on javac's own default classpath, and compiling a plugin
   # against it fails with 79 `cannot find symbol: class ClassTree`. Probed by
   # file rather than by version string: what matters is whether this JDK keeps
   # the API in a jar.
   tools=()
   for cand in "${JAVA_HOME:-}/lib/tools.jar" \
               "$(dirname "$(dirname "$(readlink -f "$(command -v "${javac_bin}")")")")/lib/tools.jar"; do
      if [ -f "${cand}" ]; then tools=( -cp "${cand}" ); break; fi
   done
   [ "${#tools[@]}" -eq 0 ] && echo "gates: no tools.jar, assuming a modular JDK"
   "${javac_bin}" -nowarn "${tools[@]}" -d "${build}/classes-source" "${src}/arqgate/SourceScan.java"

   mkdir -p "${build}/classes-source/META-INF/services"
   echo "arqgate.SourceScan" \
      > "${build}/classes-source/META-INF/services/com.sun.source.util.Plugin"

   jar cf "${build}/arqsource.jar" -C "${build}/classes-source" .
   jar_has "${build}/arqsource.jar" 'META-INF/services/com\.sun\.source\.util\.Plugin' \
      || { echo "build.sh: arqsource.jar has no plugin service entry" >&2; exit 1; }
   rm -rf "${build}/classes-source"
else
   echo "gates: SourceScan.java not in this payload, skipping arqsource.jar"
fi

# ---------------------------------------------------------------------- plan
#
# arqgate.Plan, the edit-plan emitter step 04's golden transforms read. Its own
# jar rather than a second class in arqsource.jar, and the reason is rule 6
# rather than tidiness: a plan that names every site the solution edits is the
# answer key in machine-readable form, and the one place it must never appear is
# a verifier image whose /tests the candidate could read. A separate jar cannot
# be staged by accident -- stage.sh copies gate SOURCES by name and this one is
# on no step's list -- and if it ever were, it would arrive as a jar nothing
# loads rather than as a file something reads.
#
# Same compile as SourceScan: the Compiler Tree API, no --release.

if [ -f "${src}/arqgate/Plan.java" ]; then
   mkdir -p "${build}/classes-plan"
   ptools=()
   for cand in "${JAVA_HOME:-}/lib/tools.jar" \
               "$(dirname "$(dirname "$(readlink -f "$(command -v "${javac_bin}")")")")/lib/tools.jar"; do
      if [ -f "${cand}" ]; then ptools=( -cp "${cand}" ); break; fi
   done
   "${javac_bin}" -nowarn "${ptools[@]}" -d "${build}/classes-plan" "${src}/arqgate/Plan.java"

   mkdir -p "${build}/classes-plan/META-INF/services"
   echo "arqgate.Plan" \
      > "${build}/classes-plan/META-INF/services/com.sun.source.util.Plugin"

   jar cf "${build}/arqplan.jar" -C "${build}/classes-plan" .
   jar_has "${build}/arqplan.jar" 'arqgate/Plan\.class' \
      || { echo "build.sh: arqplan.jar has no arqgate/Plan.class" >&2; exit 1; }
   rm -rf "${build}/classes-plan"
else
   echo "gates: Plan.java not in this payload, skipping arqplan.jar"
fi

# ------------------------------------------------------------------- rewrite
#
# --release 17, because OpenRewrite 8.x is Java 17+.

rw="${GATE_RW:-${lib}/rewrite}"
if [ -f "${src}/arqgate/Rewrite.java" ] && [ -d "${rw}" ] && ls "${rw}"/*.jar > /dev/null 2>&1; then
   mkdir -p "${build}/classes-rewrite"
   "${javac_bin}" --release 17 -nowarn \
      -cp "$(ls "${rw}"/*.jar | tr '\n' ':')" \
      -d "${build}/classes-rewrite" \
      "${src}/arqgate/Rewrite.java"
   jar cf "${build}/arqrewrite.jar" -C "${build}/classes-rewrite" .
   jar_has "${build}/arqrewrite.jar" 'arqgate/Rewrite\.class' \
      || { echo "build.sh: arqrewrite.jar has no arqgate/Rewrite.class" >&2; exit 1; }
   rm -rf "${build}/classes-rewrite"
else
   echo "gates: OpenRewrite runtime not in this payload, skipping arqrewrite.jar"
fi

rm -rf "${build}/classes-bytecode"
echo "gates: built $(ls -1 "${build}" | tr '\n' ' ')"
