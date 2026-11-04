#!/usr/bin/env bash
# Dump the public+protected API signature set of a set of jars.
#
#   apisig.sh <out.txt> <jar> [<jar> ...]
#
# One normalized line per declaration, sorted and de-duplicated:
#
#   <artifact-id> TYPE   <type-declaration>
#   <artifact-id> MEMBER <type-name> :: <member-declaration>
#
# Anonymous and local classes ($1, $2, ...) and module-info are dropped: they
# are not API. So are javac's synthetic members (lambda$..., access$...,
# $deserializeLambda$) -- see the note at the member branch.
#
# Bridge methods in a CLASS are NOT dropped. A bridge is a function of the
# generic signature, so keeping it makes a signature change visible that erasure
# would otherwise hide, and a bridge in a class body exists at every class-file
# version this ladder passes through -- so keeping it does not make the frozen
# expectation depend on the compile target.
#
# Bridge methods in an INTERFACE are dropped, and that asymmetry is the point.
# A class-file of version 50 or 51 may not carry an interface method with a
# body, so javac puts the bridge for a covariant interface override in every
# implementing class instead. At version 52 it does that AND emits the bridge in
# the interface, as a default method. So `interface ProtocolDef extends
# ContainerDef` redeclaring `ProtocolDef property(String, String)` gains
#
#    public org.jboss...ContainerDef property(java.lang.String, java.lang.String);
#
# on the interface itself the moment the target moves to 1.8 -- with no source
# change anywhere, and with that same bridge already counted on ProtocolDefImpl
# at both versions. B-6 is an inherited gate whose expectation is frozen once,
# against a surface measured at version 50, and read again at 50, 51, 52, 54,
# 55, 61, 65 and 69. Charging this member as a widened surface would make the
# gate measure the compile target rather than the API, and would cost the golden
# solution 0.14 of family B for doing step 04 correctly.
#
# Identified by ACC_SYNTHETIC/ACC_BRIDGE from `javap -v`, not by syntax. The
# syntactic rule -- "an interface method that is not abstract" -- would also
# swallow a default method the author wrote, which IS a widened surface and must
# still be charged. So there are two javap passes: `javap -protected` produces
# the declaration text, exactly as before, and `javap -v -protected` produces
# nothing but a list of interface members whose flags say javac wrote them,
# which is then subtracted. The constant-pool dump that makes `javap -v` useless
# to diff is irrelevant to reading a flags line.
#
# The two passes print the same declaration text for the same member, so every
# line of the drop list must match a line of the kept set. If one does not, the
# two parsers have diverged and this script fails rather than scoring: a gate
# that reports `broken` costs B-6 visibly, a gate that drops the wrong line
# costs it silently.
#
# This is the shippable twin of authoring/apisig.py, which is the same algorithm
# in Python. The verifier images carry a JDK and busybox-grade userland and no
# Python, so the scored gate runs this one; the Python one stays as the
# independent implementation the expectation is cross-checked against. They are
# required to agree byte for byte -- see the api self-test.
#
# javap and not ASM because the question this answers is "did the public surface
# change", and javap is the JDK's own answer to it. That makes the JDK version a
# variable, so it was measured rather than assumed: over this corpus, JDK 8 and
# JDK 25 javap produce byte-identical output (NOTES 6.2).
set -uo pipefail
# No pathname expansion anywhere in this script: the class names below are
# expanded unquoted into javap's operands, and they come from jar entries the
# candidate's build wrote. Nothing here relies on a glob.
set -f

# A javap operand is a Java binary name and nothing else: dot-separated ASCII
# identifier segments, `$` allowed (Outer$Inner), or javac's own package-info
# as the last segment (an annotated package-info.java compiles to one, and
# javap has always been handed it). Wave H round 4, defensive: this script runs
# as root.
binname='^([A-Za-z_$][A-Za-z0-9_$]*[.])*([A-Za-z_$][A-Za-z0-9_$]*|package-info)$'

out="${1:?usage: apisig.sh <out.txt> <jar>...}"
shift
[ "$#" -gt 0 ] || { echo "apisig.sh: no jars given" >&2; exit 1; }

jars=( "$@" )
cp="$(IFS=:; echo "${jars[*]}")"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

: > "${work}/lines"
: > "${work}/drop"
for jar in "${jars[@]}"; do
   [ -f "${jar}" ] || { echo "apisig.sh: no such jar: ${jar}" >&2; exit 1; }
   art="$(basename "${jar}")"
   art="${art%-1.0.0-SNAPSHOT.jar}"

   unzip -Z1 "${jar}" 2>/dev/null \
      | grep '\.class$' \
      | grep -v '\$[0-9]' \
      | grep -v 'module-info' \
      | sed 's/\.class$//; s|/|.|g' \
      | LC_ALL=C sort > "${work}/names"
   [ -s "${work}/names" ] || continue
   # Refused, not dropped: one entry that is not a binary name fails the whole
   # surface (exit 1, which test.sh reads as gate.api=broken, B-6 = 0). A name
   # dropped quietly would be a class whose surface went unread.
   nbad=$(LC_ALL=C grep -cvE "${binname}" "${work}/names" || true)
   if [ "${nbad}" != 0 ]; then
      printf 'apisig.sh: %s class entr(ies) in %s are not Java binary names, e.g. %q -- not scoring a surface read this way\n' \
         "${nbad}" "${art}" "$(LC_ALL=C grep -m1 -vE "${binname}" "${work}/names")" >&2
      exit 1
   fi

   # Word splitting is deliberate: every name was just checked to hold no
   # whitespace and no glob character (and globbing is off), and javap takes
   # them as separate operands.
   # shellcheck disable=SC2046
   javap -protected -classpath "${cp}" $(cat "${work}/names") 2> "${work}/javap.err" \
      | awk -v art="${art}" '
         {
            s = $0
            sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s)
         }
         s == "" || s == "}" { next }
         s ~ /^Compiled from/ { next }
         s ~ /\{$/ {
            cur = substr(s, 1, length(s) - 1)
            sub(/[ \t]+$/, "", cur); gsub(/[ \t]+/, " ", cur)
            print art " TYPE " cur
            next
         }
         cur == "" { next }
         {
            m = s; gsub(/[ \t]+/, " ", m)
            # Members javac writes for itself. A lambda in a class body desugars
            # to a private static lambda$..., which javap -protected never shows;
            # a lambda in an *interface* body desugars to a method the class file
            # has to make public, because a version-52 class file may not carry a
            # private interface method. So `NO_OP = () -> { }` in an interface
            # arrives here as `public static void lambda$static$0()` and reads as
            # a widened public surface. It is not one: the member is
            # ACC_SYNTHETIC, it has no source declaration, and every API-diff tool
            # in the field (japicmp, revapi) drops it.
            #
            # Dropped by NAME here, and not by the flags pass below, because the
            # flags pass only looks inside interfaces: access$000 lives in a
            # class, where the deliberate decision is to keep synthetic bridges,
            # and javac gives it a name no source file can spell. The interface
            # bridge is the opposite case -- javac names it `property`, which a
            # source file can and does spell -- so that one needs the flags.
            # index() and not a regex: '$' needs escaping in one awk and not in
            # the next, and this predicate is not worth finding out which.
            if (index(m, "lambda$") || index(m, "access$") \
                || index(m, "$deserializeLambda$")) next
            t = cur; sub(/<.*/, "", t)
            print art " MEMBER " t " :: " m
         }
      ' >> "${work}/lines"

   # Pass 2: the drop list. Same classes, same classpath, nothing kept but the
   # interface members javac wrote for itself.
   #
   # `javap -v` has a different surface grammar from `javap -protected`: the
   # type declaration does not carry the opening brace, `{` and `}` sit alone at
   # column 0, and the constant pool sits between them at an indentation that
   # shifts as the entry numbers widen. So this walks it by indentation and only
   # trusts the region between the lone braces -- which puts every class-level
   # attribute, the `Classfile` banner and the whole constant pool outside the
   # part that is read. Inside, a member declaration is at indent 2 and its
   # attributes at indent 4 or deeper, so `flags:` at indent 4 belongs to the
   # last declaration seen and to nothing else.
   #
   # shellcheck disable=SC2046
   javap -v -protected -classpath "${cp}" $(cat "${work}/names") 2> "${work}/javapv.err" \
      | awk -v art="${art}" '
         {
            n = match($0, /[^ ]/)
            ind = (n ? n - 1 : -1)
            s = $0; sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s)
         }
         ind == 0 && s == "{" { inbody = 1; pend = ""; next }
         ind == 0 && s == "}" { inbody = 0; pend = ""; next }
         ind == 0 && s ~ /^Classfile / { cur = ""; iface = 0; inbody = 0; next }
         ind == 0 && s == "Constant pool:" { next }
         ind == 0 {
            cur = s; gsub(/[ \t]+/, " ", cur)
            # "interface" as a word, so a class whose name contains it does not
            # read as one. javap writes `public interface X`, `public abstract
            # interface X` for an annotation type, and never `... interfaceish`.
            iface = (cur ~ /(^|[ \t])interface[ \t]/) ? 1 : 0
            next
         }
         !inbody { next }
         ind == 2 { pend = s; gsub(/[ \t]+/, " ", pend); next }
         ind == 4 && s ~ /^flags:/ {
            if (iface && pend != "" \
                && (index(s, "ACC_SYNTHETIC") || index(s, "ACC_BRIDGE")))
            {
               t = cur; sub(/<.*/, "", t)
               print art " MEMBER " t " :: " pend
            }
            next
         }
      ' >> "${work}/drop"

   if [ -s "${work}/javap.err" ]; then
      echo "apisig.sh: javap complained about ${art}:" >&2
      sed -n '1,10p' "${work}/javap.err" >&2
   fi
   if [ -s "${work}/javapv.err" ]; then
      echo "apisig.sh: javap -v complained about ${art}:" >&2
      sed -n '1,10p' "${work}/javapv.err" >&2
   fi
done

LC_ALL=C sort -u "${work}/lines" > "${work}/kept"
LC_ALL=C sort -u "${work}/drop" > "${work}/dropped"

# Every dropped line has to be a line the first pass actually produced. If one
# is not, the two parsers read the same member differently and the subtraction
# is doing something other than what it says.
nd=$(grep -c . "${work}/dropped" || true)
nh=$(LC_ALL=C comm -12 "${work}/kept" "${work}/dropped" | grep -c . || true)
if [ "${nd}" != "${nh}" ]; then
   echo "apisig.sh: ${nd} interface members flagged for dropping, ${nh} of them found" >&2
   echo "apisig.sh: the two javap passes disagree -- not scoring a surface read this way" >&2
   LC_ALL=C comm -13 "${work}/kept" "${work}/dropped" | sed 's/^/  unmatched: /' >&2
   exit 1
fi

LC_ALL=C comm -23 "${work}/kept" "${work}/dropped" > "${out}"
cp "${work}/dropped" "${out}.dropped"
echo "$(grep -c . "${out}") declarations -> ${out}"
if [ "${nd}" != 0 ]; then
   echo "${nd} compiler-written interface members dropped -> ${out}.dropped"
   sed 's/^/  dropped: /' "${out}.dropped"
fi
