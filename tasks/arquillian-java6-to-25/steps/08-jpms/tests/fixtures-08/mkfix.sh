#!/usr/bin/env bash
# Derive the family-C adversarial fixtures from the two real measurements.
#
#   mkfix.sh <pinned j5.txt> <golden j5.txt> <tests dir> <outdir>
#
# Nothing here is hand-written data. j5-pinned.txt is arqgate.Junit5 run over the
# step-01 golden tree -- the tree step 02 starts from -- and j5-golden.txt is the
# same tool over the step-02 golden solution. Every fixture below is one stated
# edit to one of those two, so a fixture cannot drift away from the shape the
# gate actually sees: it IS that shape, with one property moved.
#
# The summary counters j5.call.jupiter / .msg / .legacy / .legacy.msg / .throws
# are exactly the column sums of the per-class j5.calls= lines (measured on both
# inputs), so any fixture that edits those lines recomputes the five summaries
# from them rather than carrying a number somebody typed. Before the per-class
# `throws=` field existed, three fixtures had to synthesise the assertThrows
# summary by hand; those three hand-computations are gone, and F8's is kept as an
# ASSERTION instead -- see the cross-check there.
#
# The nine fixtures, and the gate each one exists to point at:
#
#   F1 dummy-jupiter    every must-migrate class gains one Jupiter @Test and
#                       changes nothing else. The cheapest possible pass of a
#                       C-1 written as set membership. -> C-1
#   F2 half-migrated    the first half of the must-migrate set is taken from the
#                       golden tree and the rest from the pinned one. -> all six
#                       gates, partial credit
#   F3 annotations-only the annotations moved to Jupiter and the assertion calls
#                       did not: org.junit.Assert is still what every method
#                       calls. -> C-2 and C-5
#   F5 no-assertthrows  the golden tree with its assertThrows calls gone: the
#                       @Test(expected=) sites are no longer there either, so
#                       only the disappearance half of C-3 is earned. -> C-3
#   F6 thin-assertions  the golden tree with its assertion calls dropped from the
#                       far end until 60 remain -- a migration that kept the
#                       methods and deleted what they check. assertThrows is an
#                       assertion call, so the thinned classes lose theirs with
#                       the rest; C-3's arrival half moves with C-5 rather than
#                       staying at 1. -> C-5, and C-3's arrival half
#   F7 deleted          the pinned tree with all 49 must-migrate classes removed
#                       outright. Every departure question -- retired type gone,
#                       expected= gone, @RunWith gone -- is answered perfectly
#                       and none of them honestly. -> C-2, C-3, C-4
#   F10 major52         the golden tree with every must-migrate class recompiled
#                       to class-file major 52 -- the shortcut that makes
#                       Jupiter's lambdas compile by giving up the Java 6 target.
#                       Nothing else about the tree moves. -> C-6
#   F9 throws-offsite   the golden tree with every assertThrows call moved off
#                       the ten classes that owned the frozen @Test(expected=)
#                       sites and onto one class that owned none. The tree-wide
#                       count is still 19 and the sites are still gone, so a C-3
#                       whose arrival half is a total scores this 1.0; the sites
#                       were not rewritten. -> C-3's arrival half
#   F8 partly-deleted   the golden tree with the first 10 must-migrate classes
#                       removed. The same move at a tenth of the scale, so the
#                       gates are shown to degrade in proportion rather than to
#                       have one cliff at zero. -> C-1 to C-4
set -euo pipefail

P="${1:?usage: mkfix.sh <pinned> <golden> <tests dir> <outdir>}"
G="${2:?}"
TESTS="${3:?}"
OUT="${4:?}"
mkdir -p "${OUT}"

# The must-migrate set, from the scorer's own frozen baselines: the 55 test
# classes the 2011 run recorded, minus the compatibility surface. Derived here
# rather than passed in, and then cross-checked -- c-rehearsal.sh diffs this file
# against the one the SHIPPED C block computes for itself, so a fixture built
# against a different 49 than the gate grades cannot go unnoticed.
MUST="${OUT}/must.txt"
grep '^test\.class=' "${TESTS}/baseline/tests.txt" \
   | sed 's/^test\.class=//' | tr '.' '/' | LC_ALL=C sort -u > "${OUT}/.all.txt"
grep -v '^#' "${TESTS}/baseline/compat.txt" | grep . \
   | tr '.' '/' | LC_ALL=C sort -u > "${OUT}/.compat.txt"
LC_ALL=C comm -23 "${OUT}/.all.txt" "${OUT}/.compat.txt" > "${MUST}"
[ "$(grep -c . "${MUST}")" -gt 0 ] || { echo "mkfix: the must-migrate set is empty" >&2; exit 1; }

# The owner of a per-entry line, normalised the way every C gate normalises it:
# the text between `=` and the first `|` or `#`, with any nested-class suffix
# dropped. Held in one place so a fixture and the gate it feeds cannot disagree
# about what class a line belongs to.
owner_awk='function owner(line,   v) {
   v = line
   sub(/^[^=]*=/, "", v)
   sub(/[|#].*$/, "", v)
   sub(/\$.*$/, "", v)
   return v
}'

# The five call summaries, recomputed from the per-class lines. Called by every
# fixture that touches a j5.calls= line. The parts are read BY NAME out of the
# line, not by position, so a sixth field added to the tool later lands in the
# sum it belongs to instead of being silently dropped or double-counted.
recount() {
   awk "${owner_awk}"'
      /^j5\.calls=/ {
         v = $0; sub(/^[^|]*\|/, "", v)
         n = split(v, parts, ",")
         for (i = 1; i <= n; i++) { split(parts[i], kv, "="); s[kv[1]] += kv[2] }
         print; next
      }
      /^j5\.call\.jupiter=/         { next }
      /^j5\.call\.jupiter\.msg=/    { next }
      /^j5\.call\.jupiter\.throws=/ { next }
      /^j5\.call\.legacy=/          { next }
      /^j5\.call\.legacy\.msg=/     { next }
      { print }
      END {
         printf "j5.call.jupiter=%d\n",        s["jupiter"] + 0
         printf "j5.call.jupiter.msg=%d\n",    s["jupiter.msg"] + 0
         printf "j5.call.jupiter.throws=%d\n", s["throws"] + 0
         printf "j5.call.legacy=%d\n",         s["legacy"] + 0
         printf "j5.call.legacy.msg=%d\n",     s["legacy.msg"] + 0
      }' "$1"
}

# ---------------------------------------------------------------- F1
#
# The pinned tree, with one Jupiter @Test bolted onto each must-migrate class.
# arqgate.Junit5 reports the framework field as a comma-joined set, so that edit
# shows up as `junit4` becoming `junit4,junit5` and as nothing else at all: no
# reference removed, no assertion moved, no @RunWith dropped.
awk "${owner_awk}"'
   FILENAME == ARGV[1] { m[$0] = 1; next }
   /^j5\.test=/ && (owner($0) in m) && $0 !~ /junit5/ { print $0 ",junit5"; next }
   { print }' "${MUST}" "${P}" > "${OUT}/F1.txt"

# ---------------------------------------------------------------- F2
#
# Half the work. The first 25 of the 49 must-migrate classes, in sort order, are
# taken from the golden tree; every other line -- the other 24, the six compat
# classes, and every production class -- is the pinned tree's. Both halves are
# whole classes, because a migration is done a class at a time.
head -25 "${MUST}" > "${OUT}/.f2-done.txt"
{
   awk "${owner_awk}"'
      FILENAME == ARGV[1] { d[$0] = 1; next }
      /^j5\./ && $0 ~ /^j5\.(test|asserts|legacyref|legacytest|expected|runwith|extendwith|calls)=/ {
         if (owner($0) in d) next
         print; next
      }
      /^j5\.(tests|legacytests|legacyrefs|expected\.sites|call\.)/ { next }
      { print }' "${OUT}/.f2-done.txt" "${P}"
   awk "${owner_awk}"'
      FILENAME == ARGV[1] { d[$0] = 1; next }
      $0 ~ /^j5\.(test|asserts|legacyref|legacytest|expected|runwith|extendwith|calls)=/ {
         if (owner($0) in d) print }' "${OUT}/.f2-done.txt" "${G}"
   # The migrated half's assertThrows calls arrive with its own j5.calls= lines,
   # which carry the per-class `throws=` column, so recount adds them up like
   # every other column. This block used to synthesise the summary by counting
   # the migrated half's frozen @Test(expected=) sites; that hand-computation is
   # gone, and with it the chance of it disagreeing with the per-class lines.
} > "${OUT}/.f2-raw.txt"
recount "${OUT}/.f2-raw.txt" > "${OUT}/F2.txt"

# ---------------------------------------------------------------- F3
#
# The annotations moved and the assertions did not. Every line that carries an
# annotation -- j5.test, j5.runwith, j5.extendwith, j5.expected -- comes from the
# golden tree, and every line that records what the code REFERS TO or CALLS comes
# from the pinned one. That is a real and common half-migration: `@Test` is now
# org.junit.jupiter.api.Test while every body still calls org.junit.Assert.
{
   awk '$0 !~ /^j5\.(test|runwith|extendwith|expected)=/ &&
        $0 !~ /^j5\.(tests|legacytests|legacyrefs|expected\.sites|call\.)/ { print }' "${P}"
   awk '$0 ~ /^j5\.(test|runwith|extendwith|expected)=/ { print }' "${G}"
   # The j5.calls= lines all come from the pinned tree, whose `throws=` column is
   # zero everywhere, so recount reaches 0 by addition. Nothing to state here.
} > "${OUT}/.f3-raw.txt"
recount "${OUT}/.f3-raw.txt" > "${OUT}/F3.txt"

# ---------------------------------------------------------------- F5
#
# The golden tree with no assertThrows anywhere. The @Test(expected=) sites are
# gone -- they have to be, Jupiter has no such attribute -- so the disappearance
# half of C-3 is fully earned and the arrival half is not.
# The edit is made where the calls are RECORDED -- the per-class `throws=` column
# -- and the summary is recomputed from that, because C-3's arrival half asks
# which class made the call. Zeroing only the summary would have left the
# per-class lines saying the calls are still there, and the fixture would have
# been measuring the scorer against a report the tool could never emit.
sed 's/,throws=[0-9]*$/,throws=0/' "${G}" > "${OUT}/.f5-raw.txt"
recount "${OUT}/.f5-raw.txt" > "${OUT}/F5.txt"
[ "$(sed -n 's/^j5\.call\.jupiter\.throws=//p' "${OUT}/F5.txt")" = 0 ] \
   || { echo "mkfix: F5 still reports assertThrows calls" >&2; exit 1; }

# ---------------------------------------------------------------- F9
#
# The vector C-3's arrival half exists to refuse, built as the smallest edit that
# produces it: the calls stay, the count stays, only the CLASS changes. The
# jupiter column moves with the throws column because assertThrows is one of the
# Jupiter assertion calls -- moving the call and not its jupiter tally would edit
# C-5 as well, and this fixture is meant to move exactly one gate.
#
# The recipient is derived, not named: the first must-migrate class in sort order
# that makes calls and owns no frozen site. Naming one here would be a second
# place to keep a fact the baseline already holds.
sed -n 's/^j5\.base\.expected\.site=//p' "${TESTS}/baseline/junit5.txt" \
   | sed 's/#.*$//; s/\$.*$//' | LC_ALL=C sort -u > "${OUT}/.f9-siteowners.txt"
moved=$(awk "${owner_awk}"'
   FILENAME == ARGV[1] { s[$0] = 1; next }
   /^j5\.calls=/ && (owner($0) in s) {
      v = $0; sub(/^[^|]*[|]/, "", v); np = split(v, parts, ",")
      for (i = 1; i <= np; i++) { split(parts[i], kv, "="); if (kv[1] == "throws") t += kv[2] }
   }
   END { printf "%d\n", t + 0 }' "${OUT}/.f9-siteowners.txt" "${G}")
[ "${moved}" -gt 0 ] || { echo "mkfix: F9 found no assertThrows calls to move" >&2; exit 1; }
recipient=$(awk "${owner_awk}"'
   FILENAME == ARGV[1] { m[$0] = 1; next }
   FILENAME == ARGV[2] { s[$0] = 1; next }
   /^j5\.calls=/ { c = owner($0); if ((c in m) && !(c in s)) print c }' \
   "${MUST}" "${OUT}/.f9-siteowners.txt" "${G}" | LC_ALL=C sort -u | head -1)
[ -n "${recipient}" ] || { echo "mkfix: F9 found no class to move the calls to" >&2; exit 1; }
awk -v rec="${recipient}" -v moved="${moved}" "${owner_awk}"'
   FILENAME == ARGV[1] { s[$0] = 1; next }
   /^j5\.calls=/ {
      c = owner($0)
      pfx = $0; sub(/[|].*$/, "", pfx)
      v = $0; sub(/^[^|]*[|]/, "", v)
      np = split(v, parts, ",")
      split("", val, ":"); split("", name, ":")
      for (i = 1; i <= np; i++) { split(parts[i], kv, "="); name[i] = kv[1]; val[kv[1]] = kv[2] + 0 }
      if (c in s)        { val["jupiter"] -= val["throws"]; val["throws"] = 0 }
      else if (c == rec) { val["jupiter"] += moved; val["throws"] += moved }
      out = ""
      for (i = 1; i <= np; i++) out = out (i > 1 ? "," : "") name[i] "=" val[name[i]]
      print pfx "|" out
      next
   }
   { print }' "${OUT}/.f9-siteowners.txt" "${G}" > "${OUT}/.f9-raw.txt"
recount "${OUT}/.f9-raw.txt" > "${OUT}/F9.txt"
# The count did not change, none of it is on a site owner any more, and all of it
# is on the one class that got it.
gthr=$(sed -n 's/^j5\.call\.jupiter\.throws=//p' "${G}" | head -1)
f9thr=$(sed -n 's/^j5\.call\.jupiter\.throws=//p' "${OUT}/F9.txt" | head -1)
[ "${f9thr}" = "${gthr}" ] \
   || { echo "mkfix: F9 reports ${f9thr} assertThrows calls, not the golden tree's ${gthr}" >&2; exit 1; }
onsite=$(awk "${owner_awk}"'
   FILENAME == ARGV[1] { s[$0] = 1; next }
   /^j5\.calls=/ && (owner($0) in s) {
      v = $0; sub(/^[^|]*[|]/, "", v); np = split(v, parts, ",")
      for (i = 1; i <= np; i++) { split(parts[i], kv, "="); if (kv[1] == "throws") t += kv[2] }
   }
   END { printf "%d\n", t + 0 }' "${OUT}/.f9-siteowners.txt" "${OUT}/F9.txt")
[ "${onsite}" = 0 ] \
   || { echo "mkfix: F9 still leaves ${onsite} assertThrows call(s) on a frozen-site class" >&2; exit 1; }
got=$(awk -v rec="${recipient}" "${owner_awk}"'
   /^j5\.calls=/ && owner($0) == rec {
      v = $0; sub(/^[^|]*[|]/, "", v); np = split(v, parts, ",")
      for (i = 1; i <= np; i++) { split(parts[i], kv, "="); if (kv[1] == "throws") t += kv[2] }
   }
   END { printf "%d\n", t + 0 }' "${OUT}/F9.txt")
[ "${got}" = "${moved}" ] \
   || { echo "mkfix: F9's recipient holds ${got} assertThrows call(s), not ${moved}" >&2; exit 1; }
echo "mkfix: F9 moved ${moved} assertThrows call(s) off 10 frozen-site classes onto ${recipient}"

# ---------------------------------------------------------------- F10
#
# The golden tree as a build that raised its source level would have left it.
# Only the major version moves, and only on the must-migrate classes: the
# compat classes and the whole of src/main stay at 50, because the shortcut is
# taken in the test compile task and nowhere else. Every other gate in family C
# reads exactly what it read on the golden tree.
awk "${owner_awk}"'
   FILENAME == ARGV[1] { m[$0] = 1; next }
   /^j5\.major=/ {
      c = $0; sub(/^j5\.major=/, "", c); sub(/[|].*$/, "", c); sub(/\$.*$/, "", c)
      if (c in m) { pfx = $0; sub(/[|].*$/, "", pfx); print pfx "|52"; next }
      print; next
   }
   { print }' "${MUST}" "${G}" > "${OUT}/F10.txt"
# The edit landed on the must-migrate classes and on nothing else.
n50=$(grep -c '^j5\.major=.*|50$' "${OUT}/F10.txt" || true)
n52=$(grep -c '^j5\.major=.*|52$' "${OUT}/F10.txt" || true)
g50=$(grep -c '^j5\.major=.*|50$' "${G}" || true)
g52=$(grep -c '^j5\.major=.*|52$' "${G}" || true)
[ "${g52}" = 0 ] || { echo "mkfix: the golden tree already has ${g52} class file(s) above major 50" >&2; exit 1; }
[ "${n52}" -gt 0 ] && [ "$(( n50 + n52 ))" = "${g50}" ] \
   || { echo "mkfix: F10 has ${n50}+${n52} major lines against the golden tree's ${g50}" >&2; exit 1; }
own52=$(awk "${owner_awk}"'
   FILENAME == ARGV[1] { m[$0] = 1; next }
   /^j5\.major=.*[|]52$/ {
      c = $0; sub(/^j5\.major=/, "", c); sub(/[|].*$/, "", c); sub(/\$.*$/, "", c)
      if (!(c in m)) { bad++ } else { o[c] = 1 }
   }
   END { n = 0; for (k in o) n++; printf "%d %d\n", n, bad + 0 }' "${MUST}" "${OUT}/F10.txt")
set -- ${own52}
[ "$2" = 0 ] || { echo "mkfix: F10 raised the major version on $2 class file(s) outside the must-migrate set" >&2; exit 1; }
[ "$1" = "$(grep -c . "${MUST}")" ] \
   || { echo "mkfix: F10 raised the major version on $1 of the must-migrate classes, not all of them" >&2; exit 1; }
echo "mkfix: F10 recompiled $1 must-migrate class(es), ${n52} class file(s), to major 52"

# ---------------------------------------------------------------- F6
#
# The golden tree with its assertions thinned to 60. Classes are emptied from the
# end of the file forward until the Jupiter total has fallen to 60 or below, so
# the classes that keep their assertions are whole ones. Everything else about
# the tree -- annotations, references, runners, assertThrows -- is untouched, so
# C-1 through C-4 stay at 1 and only C-5 moves.
awk "${owner_awk}"'
   /^j5\.calls=/ { keep[++n] = $0; next }
   { rest[++r] = $0 }
   END {
      for (i = 1; i <= n; i++) {
         v = keep[i]; sub(/^[^|]*\|/, "", v)
         np = split(v, parts, ",")
         for (j = 1; j <= np; j++) { split(parts[j], kv, "="); c[i, kv[1]] = kv[2] }
         tot += c[i, "jupiter"] + c[i, "legacy"]
      }
      for (i = n; i >= 1 && tot > 60; i--) {
         tot -= c[i, "jupiter"] + c[i, "legacy"]
         drop[i] = 1
      }
      for (i = 1; i <= n; i++) {
         if (i in drop) continue
         print keep[i]
      }
      for (i = 1; i <= r; i++) print rest[i]
   }' "${G}" > "${OUT}/.f6-raw.txt"
recount "${OUT}/.f6-raw.txt" > "${OUT}/F6.txt"

# ---------------------------------------------------------------- F7, F8
#
# Deletion. Both are built by the same function, because they are the same edit:
# every line belonging to a named class goes, INCLUDING its j5.seen= line, and
# the summaries are recomputed from what is left. Dropping the seen line is the
# point -- a fixture that removed a class's findings but left its presence
# record would be testing nothing, since presence is exactly what the departure
# gates now consult.
#
# F7 deletes all 49 from the pinned tree: the shape of an agent who read "retire
# the JUnit 4 annotations" and removed the files instead. F8 deletes 10 from the
# GOLDEN tree: a finished migration that lost a fifth of its classes on the way,
# which is the case that has to lose about a fifth rather than everything.
delete_classes() {
   local src="$1" victims="$2"
   awk "${owner_awk}"'
      FILENAME == ARGV[1] { v[$0] = 1; next }
      /^j5\.seen=/ {
         c = $0; sub(/^j5\.seen=/, "", c); sub(/\$.*$/, "", c)
         if (c in v) next
         seen++; print; next
      }
      /^j5\.major=/ {
         c = $0; sub(/^j5\.major=/, "", c); sub(/[|].*$/, "", c); sub(/\$.*$/, "", c)
         if (c in v) next
         print; next
      }
      $0 ~ /^j5\.(test|asserts|legacyref|legacytest|expected|runwith|extendwith|calls)=/ {
         if (owner($0) in v) next
         print; next
      }
      /^j5\.(tests|legacytests|legacyrefs|expected\.sites|seen\.count)/ { next }
      { print }
      END { printf "j5.seen.count=%d\n", seen + 0 }' "${victims}" "${src}"
}

delete_classes "${P}" "${MUST}" > "${OUT}/.f7-raw.txt"
recount "${OUT}/.f7-raw.txt" > "${OUT}/F7.txt"

head -10 "${MUST}" > "${OUT}/.f8-victims.txt"
# The assertThrows calls that left with the deleted classes: one per frozen
# @Test(expected=) site whose class is a victim. Counted off the scorer's own
# baseline, independently of the measurement being edited.
#
# This number used to BUILD the fixture. It no longer does -- deleting a class
# takes its j5.calls= line and the line's `throws=` column with it, so recount
# arrives at the survivors' total by addition. So the baseline count is spent on
# something better: it is asserted against what the per-class columns actually
# lost. The two are computed from different files by different means and have to
# agree, which is the claim C-3's new arrival half rests on -- that the frozen
# @Test(expected=) sites and the classes holding the assertThrows calls are the
# same classes.
lost=$(awk -F'[=#]' 'NR == FNR { v[$0] = 1; next }
                     $1 == "j5.base.expected.site" {
                        c = $2; sub(/\$.*$/, "", c); if (c in v) n++
                     }
                     END { printf "%d\n", n + 0 }' \
          "${OUT}/.f8-victims.txt" "${TESTS}/baseline/junit5.txt")
delete_classes "${G}" "${OUT}/.f8-victims.txt" > "${OUT}/.f8-raw.txt"
recount "${OUT}/.f8-raw.txt" > "${OUT}/F8.txt"
gthr=$(sed -n 's/^j5\.call\.jupiter\.throws=//p' "${G}" | head -1)
f8thr=$(sed -n 's/^j5\.call\.jupiter\.throws=//p' "${OUT}/F8.txt" | head -1)
[ "$(( gthr - f8thr ))" = "${lost}" ] \
   || { echo "mkfix: F8's 10 classes own ${lost} frozen expected= site(s) but took \
$(( gthr - f8thr )) assertThrows call(s) with them" >&2; exit 1; }
echo "mkfix: F8 loses ${lost} assertThrows call(s) with its 10 classes, \
per-class columns and frozen baseline agreeing"

# A deletion fixture is worth nothing unless the classes really left and the
# rest of the tree really stayed, so both ends are asserted on both fixtures.
#
# Counted over OWNERS and not over j5.seen lines: a j5.seen line is emitted per
# class file, so deleting one source file takes its nested classes with it. That
# is what deleting a source file does, and the first draft of these assertions
# read the 126 lines F7 removes as 126 classes and called the fixture broken.

# owners_present <report> <name list> -- how many of the named top-level classes
# still have at least one class file in the report.
owners_present()
{
   awk 'FILENAME == ARGV[1] { v[$0] = 1; next }
        /^j5\.seen=/ {
           c = $0; sub(/^j5\.seen=/, "", c); sub(/\$.*$/, "", c)
           if (c in v) o[c] = 1
        }
        END { n = 0; for (k in o) n++; printf "%d\n", n }' "$2" "$1"
}

# victim_lines <report> <name list> -- how many j5.seen lines those classes own.
victim_lines()
{
   awk 'FILENAME == ARGV[1] { v[$0] = 1; next }
        /^j5\.seen=/ {
           c = $0; sub(/^j5\.seen=/, "", c); sub(/\$.*$/, "", c)
           if (c in v) n++
        }
        END { printf "%d\n", n + 0 }' "$2" "$1"
}

must_n=$(grep -c . "${MUST}")
pseen=$(grep -c '^j5\.seen=' "${P}" || true)
gseen=$(grep -c '^j5\.seen=' "${G}" || true)
f7seen=$(grep -c '^j5\.seen=' "${OUT}/F7.txt" || true)
f8seen=$(grep -c '^j5\.seen=' "${OUT}/F8.txt" || true)
[ "${pseen}" -gt 0 ] && [ "${gseen}" -gt 0 ] \
   || { echo "mkfix: a measurement has no j5.seen lines -- rebuild the gate jar" >&2; exit 1; }

# The premise both fixtures edit: every must-migrate class is present on both
# measured trees. If that were not so, the gates would already be reading a
# departure where there is an absence and these fixtures would be redundant.
for pair in "pinned ${P}" "golden ${G}"; do
   set -- ${pair}
   n=$(owners_present "$2" "${MUST}")
   [ "${n}" = "${must_n}" ] \
      || { echo "mkfix: only ${n} of ${must_n} must-migrate classes are present on the $1 tree" >&2
           exit 1; }
done

# F7: none of the 49 left, and nothing else went with them.
n=$(owners_present "${OUT}/F7.txt" "${MUST}")
[ "${n}" = 0 ] || { echo "mkfix: F7 still holds ${n} must-migrate class(es)" >&2; exit 1; }
want=$(victim_lines "${P}" "${MUST}")
[ "$(( pseen - f7seen ))" = "${want}" ] \
   || { echo "mkfix: F7 removed $(( pseen - f7seen )) class files, not the ${want} the 49 own" >&2
        exit 1; }

# F8: the ten named left and the other 39 did not.
n=$(owners_present "${OUT}/F8.txt" "${OUT}/.f8-victims.txt")
[ "${n}" = 0 ] || { echo "mkfix: F8 still holds ${n} of its 10 victims" >&2; exit 1; }
n=$(owners_present "${OUT}/F8.txt" "${MUST}")
[ "${n}" = "$(( must_n - 10 ))" ] \
   || { echo "mkfix: F8 holds ${n} must-migrate classes, not $(( must_n - 10 ))" >&2; exit 1; }
want=$(victim_lines "${G}" "${OUT}/.f8-victims.txt")
[ "$(( gseen - f8seen ))" = "${want}" ] \
   || { echo "mkfix: F8 removed $(( gseen - f8seen )) class files, not the ${want} the 10 own" >&2
        exit 1; }
echo "mkfix: F7 removed ${must_n} class(es) / $(( pseen - f7seen )) class file(s) of ${pseen}, " \
     "F8 removed 10 / $(( gseen - f8seen )) of ${gseen}"

rm -f "${OUT}"/.f*-raw.txt "${OUT}/.f2-done.txt" "${OUT}/.all.txt" "${OUT}/.compat.txt" \
      "${OUT}/.f8-victims.txt" "${OUT}/.f9-siteowners.txt"
for f in F1 F2 F3 F5 F6 F7 F8 F9 F10; do
   [ -s "${OUT}/${f}.txt" ] || { echo "mkfix: ${f} came out empty" >&2; exit 1; }
done
echo "mkfix: $(cd "${OUT}" && wc -l F1.txt F2.txt F3.txt F5.txt F6.txt F7.txt F8.txt F9.txt \
                   F10.txt | tr '\n' ' ')"
