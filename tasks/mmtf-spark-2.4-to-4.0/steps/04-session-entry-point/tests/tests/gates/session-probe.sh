#!/usr/bin/env bash
# Run a Spark application with the gate probe attached and print its facts.
#
# Defaults to gate.GateMain, the stand-in job, which is what the rehearsal
# drives. Point --class and --jar at a candidate main to probe that instead;
# nothing about the attachment touches the application's code.
#
#   usage: session-probe.sh [--conf k=v]... [--mode MODE]
#                           [--class CLS --jar JAR] [-- args...]
#
# Always exits 0. A submit that fails becomes an `error` fact, because a gate
# that reports nothing and a gate that reports a failure must not look alike.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
JAVA_HOME=${JAVA_HOME:-/opt/jdk}
SPARK_HOME=${SPARK_HOME:-/opt/spark}
export JAVA_HOME SPARK_HOME

CLS=gate.GateMain
JAR=
MODE=sql
CONFS=()
while [ $# -gt 0 ]; do
    case $1 in
        --conf)  CONFS+=(--conf "$2"); shift 2 ;;
        --mode)  MODE=$2; shift 2 ;;
        --class) CLS=$2; shift 2 ;;
        --jar)   JAR=$2; shift 2 ;;
        --)      shift; break ;;
        *)       break ;;
    esac
done

PJAR=$("$HERE/probe-jar.sh" 2>/tmp/probe-jar.err)
if [ -z "${PJAR:-}" ] || [ ! -f "$PJAR" ]; then
    printf 'error\tprobe-jar\t%s\n' \
        "$(tr '\t\n' '  ' < /tmp/probe-jar.err | cut -c1-200)"
    exit 0
fi
[ -n "$JAR" ] || { JAR=$PJAR; set -- "$MODE" "$@"; }

W=$(mktemp -d)
out=$W/facts.tsv
# PROBE_VIA_FD drives the road the 140-job replay uses: the job is handed a
# descriptor rather than a path, so nothing in its environment says where its
# facts go and nothing it runs afterwards can rewrite them. The path road stays
# the default because the standalone probes below run the verifier's own class,
# where there is no candidate to keep the path from.
catpid=
if [ -n "${PROBE_VIA_FD:-}" ]; then
    fifo=$W/probe.fifo
    mkfifo -m 600 "$fifo"
    ( body=$(cat < "$fifo")
      [ -n "$body" ] && printf '%s\n' "$body" > "$out" ) &
    catpid=$!
    exec 9<>"$fifo"
    probeenv=(GATE_PROBE_FD=9)
else
    probeenv=(GATE_PROBE_OUT="$out")
fi

# local[1] and 3g are the width and heap the behavior suite replays at and the
# handoff document prints, and they are the default here so that the gate's
# reading of a session is a reading of the session the step is graded on.
# Nothing step 1 asks of the probe depends on partitioning; steps that turn AQE
# on, or read a plan, will, and a probe that had quietly run twice as wide as
# the suite would have answered a different question than the one asked.
env "${probeenv[@]}" "$SPARK_HOME/bin/spark-submit" \
    --class "$CLS" \
    --master "local[${PROBE_CORES:-1}]" \
    --driver-memory "${PROBE_MEM:-3g}" \
    --driver-java-options "-XX:ActiveProcessorCount=${PROBE_CORES:-1}" \
    --driver-class-path "$PJAR" \
    --conf spark.ui.enabled=false \
    --conf spark.driver.host=127.0.0.1 \
    --conf spark.driver.bindAddress=127.0.0.1 \
    --conf spark.extraListeners=gate.GateProbe \
    ${CONFS[@]+"${CONFS[@]}"} \
    "$JAR" "$@" > "$W/stdout" 2> "$W/stderr"
rc=$?
if [ -n "$catpid" ]; then
    exec 9>&-
    wait "$catpid" 2>/dev/null || true
fi

# One sorted block, the shell's own fact included. LC_ALL=C because the probe
# sorts with Collections.sort, which is byte order on ASCII, and a locale that
# folds case would put this file's output in a different order to the file the
# probe wrote -- a difference that shows up only on whichever box has the other
# locale.
{
    if [ -s "$out" ]; then
        cat "$out"
    else
        printf 'error\tsubmit\trc=%s %s\n' "$rc" \
            "$(grep -m1 -E 'Exception|Error:' "$W/stderr" 2>/dev/null \
               | tr '\t\n' '  ' | cut -c1-160)"
    fi
    # The application's own exit status is a fact in its own right: step 3 asks
    # whether the job ran, and a probe file written by a crashed job is not the
    # same evidence as one written by a job that finished.
    printf 'probe\tsubmit.rc\t%s\n' "$rc"
} | LC_ALL=C sort
[ -n "${PROBE_KEEP:-}" ] && { echo "session-probe: kept $W" >&2; } || rm -rf "$W"
exit 0
