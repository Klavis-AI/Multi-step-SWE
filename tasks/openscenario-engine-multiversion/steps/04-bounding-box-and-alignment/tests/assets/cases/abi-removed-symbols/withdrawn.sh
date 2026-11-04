#!/usr/bin/env bash
# NEW. The published surface does not only grow at this step, it shrinks: the
# semi-fixed custom camera entry point is withdrawn. Asserting its ABSENCE is
# what separates "added the nine new names" from "followed the release
# through" -- presence checks alone can never notice a name that should have
# gone away, and a tree that still exports it has not done what was asked.
#
# This is deliberately ONE name, not three. SE_WritePPMImage and
# SE_WriteTGAImage are sometimes described as withdrawn at this release; they
# are not. Measured with `nm -D --defined-only` on reference builds of BOTH
# v2.27.5 and v2.31.10, both are still `T` in libesminiLib.so at the target,
# and both are still declared in esminiLib.hpp there. Requiring their absence
# would fail every correct solution, so this case does not.
source /tests/caselib.sh

find_lib() {  # find_lib <soname>
    local d dirs
    IFS=: read -r -a dirs <<< "$LIBDIR"
    for d in "${dirs[@]}"; do
        [ -f "$d/$1" ] && { printf '%s\n' "$d/$1"; return 0; }
    done
    return 1
}

se_so=$(find_lib libesminiLib.so) || fail "libesminiLib.so not found under LIBDIR=$LIBDIR"
nm -D --defined-only "$se_so" > "$WORK/se.sym" 2>"$WORK/se.err" || fail "nm failed on $se_so"

need_not_grep '^[0-9a-f]+ [TWBDR] SE_AddCustomSemiFixedCamera$' "$WORK/se.sym" \
    "SE_AddCustomSemiFixedCamera is withdrawn from the exported surface at this release"

# Guard against the degenerate way that check could pass: an empty or
# unreadable symbol table. The sibling custom-camera entry point is NOT
# withdrawn and must still be there, so the table is real and the absence
# above means something.
need_grep '^[0-9a-f]+ [TWBDR] SE_AddCustomCamera$' "$WORK/se.sym" \
    "the neighbouring SE_AddCustomCamera is untouched, so the symbol table really was read"

# The withdrawal is a source-level fact as well as a link-level one, and the two
# are independent: a tree can drop the definition (so `nm -D` stops listing it)
# while leaving the declaration in the public header, which leaves a caller
# compiling against a prototype that no longer resolves. The instruction's
# "withdrawn" means gone from both, so grade both. The header is located from
# $RESOURCES' parent, the same anchor six-artifacts.sh uses, and externals/ is
# excluded so a vendored copy of some other project's header cannot answer.
app=$(dirname "$RESOURCES")
: > "$WORK/eslib_hpp.cat"
nhdr=0
while IFS= read -r h; do
    cat "$h" >> "$WORK/eslib_hpp.cat"
    nhdr=$((nhdr + 1))
done < <(find "$app" -maxdepth 8 -name esminiLib.hpp -not -path '*/externals/*' 2>/dev/null | sort)
[ "$nhdr" -ge 1 ] || fail "no esminiLib.hpp found under $app: the public header of libesminiLib.so must be in the tree"
need_file "$WORK/eslib_hpp.cat" "the concatenated esminiLib.hpp text"

# The instruction's obligation is that the name is no longer DECLARED in the
# header ("must no longer be declared in `esminiLib.hpp`"), so the search runs
# over the header's code text with comments removed. A solution that drops the
# declaration but leaves the name in a deprecation note or a commented-out
# block is conforming and must not fail; a live declaration or definition
# survives the strip and does.
awk '
  { out = ""; i = 1; n = length($0)
    while (i <= n) {
      if (inblock) {
        p = index(substr($0, i), "*/")
        if (p == 0) { i = n + 1; continue }
        i = i + p + 1; inblock = 0; continue
      }
      if (substr($0, i, 2) == "//") break
      if (substr($0, i, 2) == "/*") { inblock = 1; i += 2; continue }
      out = out substr($0, i, 1); i++
    }
    print out
  }
' "$WORK/eslib_hpp.cat" > "$WORK/eslib_hpp.code"
need_file "$WORK/eslib_hpp.code" "esminiLib.hpp with comments stripped"

need_not_grep 'SE_AddCustomSemiFixedCamera' "$WORK/eslib_hpp.code" \
    "SE_AddCustomSemiFixedCamera is withdrawn from the public header too, not just from the symbol table"

# Same degenerate-pass guard as above, applied to the header this time: the
# neighbouring declaration must still be readable in the STRIPPED text we just
# searched, so a stripping step that ate the header cannot pass this by
# default.
need_grep 'SE_AddCustomCamera\(' "$WORK/eslib_hpp.code" \
    "SE_AddCustomCamera is still declared in esminiLib.hpp, so the header really was read"

# The two names most often mistaken for withdrawn at this release are asserted
# PRESENT, so that a candidate which over-applies the removal is caught here
# rather than silently scoring. Both are `T` in libesminiLib.so at v2.27.5 and
# at v2.31.10; the comment at the top of this file records the measurement.
need_grep '^[0-9a-f]+ [TWBDR] SE_WritePPMImage$' "$WORK/se.sym" \
    "SE_WritePPMImage is NOT withdrawn at this release and must still be exported"
need_grep '^[0-9a-f]+ [TWBDR] SE_WriteTGAImage$' "$WORK/se.sym" \
    "SE_WriteTGAImage is NOT withdrawn at this release and must still be exported"
