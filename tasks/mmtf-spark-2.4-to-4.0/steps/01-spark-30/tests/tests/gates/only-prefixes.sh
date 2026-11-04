#!/usr/bin/env bash
# Print the --only list class-scan.py is to be given for a tree: one line,
# comma-separated, no trailing newline of its own beyond the one line.
#
#   usage: only-prefixes.sh [app-root]     (default /app)
#
# `--only` is an allowlist, and a hard-coded one is a carve-out: a helper class
# in a package the gate does not read is a helper whose constant pool -- its
# reflective lookups, the names it spells -- is not a fact anyone checks, and
# the rules that read a jar would be reading a subset of it chosen by the
# candidate (round-3 review, R5). So the list is derived from the tree itself:
# every package directory the build produced under target/classes, and every
# package directory the sources declare under src/main/java. Both, because
# either one alone is deletable by the thing being measured -- a pom that
# removes its own target/classes after packaging would narrow the scan back to
# the floor, and the jar it left behind would still hold the helper.
#
# edu/sdsc/mmtf stays in as a floor for a build that produced nothing, and
# `gate` is in so no-probe-impersonation has somewhere to look; nothing honest
# compiles into either of the last two.
#
# The scan is of the jar and the jar is shaded, so this list is also what keeps
# the shaded dependencies out of it: measured on the gold tree, the whole jar
# carries 318 references into java.lang.reflect and 84 Class name lookups, all
# of them in okhttp, jackson, forester and jgrapht, and none of them under a
# prefix this list names. Dropping --only would turn no-reflective-dispatch red
# on the golden tree.
#
# It lives in gates/ rather than inline in a step's test.sh because the
# rehearsals read a tree with the same scanner, and a rehearsal that scanned a
# different subset than the shipped verifier would be measuring a different
# gate (trap 6).
set -u
APP=${1:-/app}
only=edu/sdsc/mmtf,com/databricks,gate
pkgs=$(
    { [ -d "$APP/target/classes" ] &&
      ( cd "$APP/target/classes" && find . -name '*.class' )
      [ -d "$APP/src/main/java" ] &&
      ( cd "$APP/src/main/java" && find . -name '*.java' )
    } 2>/dev/null |
    sed 's|^\./||; s|\.class$||; s|\.java$||; s|/[^/]*$||' | sort -u | paste -sd,)
[ -n "$pkgs" ] && only="$only,$pkgs"
# A tree that really does carry a gate/ class -- the impersonation the rules
# are looking for -- contributes a prefix the floor already named, and a list
# with `gate,gate` in it scans the same classes but makes the count test.sh
# logs one too many. Duplicates out, first occurrence kept.
printf '%s\n' "$only" | tr , '\n' | awk '!seen[$0]++' | paste -sd,
