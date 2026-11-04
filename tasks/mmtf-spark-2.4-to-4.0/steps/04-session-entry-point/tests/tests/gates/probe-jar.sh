#!/usr/bin/env bash
# Build (once) and print the path of the probe jar for the Spark in this image.
#
# The jar is cached under /tmp keyed on both the JDK and the Spark build,
# because the probe is compiled against whichever Spark it will be loaded by
# and the rungs do not share one. A cache keyed on only the JDK would hand the
# 4.0.1 run a class compiled against 2.4.8, which loads and then reports the
# wrong thing -- the failure this whole directory exists to make impossible.
#
#   usage: probe-jar.sh          # prints /tmp/gate-probe-<key>/gate-probe.jar
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
JAVA_HOME=${JAVA_HOME:-/opt/jdk}
SPARK_HOME=${SPARK_HOME:-/opt/spark}

jver=$("$JAVA_HOME/bin/javac" -version 2>&1 | tr -c '[:alnum:].' '-')
# spark-core_2.12-2.4.8.jar -> 2.12-2.4.8: the Scala binary version is in the
# key too, since 2.12 and 2.13 are different scala.Tuple2 classes.
score=$(basename "$(ls "$SPARK_HOME"/jars/spark-core_*.jar 2>/dev/null | head -1)" .jar)
key=$jver-${score#spark-core_}
cache=${GATE_PROBE_CACHE:-/tmp/gate-probe-$key}
jar=$cache/gate-probe.jar

newest=$(ls -t "$HERE/GateProbe.java" "$HERE/GateMain.java" | head -1)
if [ ! -f "$jar" ] || [ "$newest" -nt "$jar" ]; then
    rm -rf "$cache"
    mkdir -p "$cache/classes"
    cp=$(echo "$SPARK_HOME"/jars/*.jar | tr ' ' ':')
    "$JAVA_HOME/bin/javac" -nowarn -cp "$cp" -d "$cache/classes" \
        "$HERE/GateProbe.java" "$HERE/GateMain.java" >&2
    "$JAVA_HOME/bin/jar" cf "$jar" -C "$cache/classes" gate
    # Assert the two classes are actually in there. A jar built from an empty
    # -d directory is a valid jar, and spark.extraListeners would then fail the
    # application with ClassNotFoundException at startup rather than here.
    for c in gate/GateProbe.class gate/GateMain.class; do
        "$JAVA_HOME/bin/jar" tf "$jar" | grep -qx "$c" \
            || { echo "probe-jar: $c missing from $jar" >&2; exit 1; }
    done
fi
echo "$jar"
