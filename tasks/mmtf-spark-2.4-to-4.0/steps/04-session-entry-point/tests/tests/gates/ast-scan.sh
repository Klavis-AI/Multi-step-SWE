#!/usr/bin/env bash
# Compile AstScan once, then run it over the given source roots.
#
# The JDK's parser is in a different place depending on which JDK this is:
# tools.jar on 8, the jdk.compiler module on 9 and later. Both are present in
# every image this runs in -- every rung ships a full JDK, not a JRE -- so the
# only portability work is putting tools.jar on the path when it exists.
#
# The compiled class is cached beside this script, keyed on the JDK, because a
# class compiled by JDK 8 and one compiled by JDK 21 are different files and a
# shared cache would hand one rung the other's.
#
#   usage: ast-scan.sh <source-root> [<source-root> ...]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
JAVA_HOME=${JAVA_HOME:-/opt/jdk}
# Emptied when absent, so the ${TOOLS:+} expansions below are driven by
# whether the file exists rather than by whether the variable was assigned.
TOOLS=$JAVA_HOME/lib/tools.jar
[ -f "$TOOLS" ] || TOOLS=
CP=$HERE${TOOLS:+:$TOOLS}

ver=$("$JAVA_HOME/bin/javac" -version 2>&1 | tr -c '[:alnum:].' '-')
cache=${GATE_CACHE:-/tmp/gate-ast-$ver}
mkdir -p "$cache"
if [ ! -f "$cache/AstScan.class" ] || [ "$HERE/AstScan.java" -nt "$cache/AstScan.class" ]; then
    "$JAVA_HOME/bin/javac" -nowarn -cp "$CP" -d "$cache" "$HERE/AstScan.java"
fi
exec "$JAVA_HOME/bin/java" -cp "$cache${TOOLS:+:$TOOLS}" AstScan "$@"
