#!/usr/bin/env bash
# Run the behavior suite against a built tree, and write one recording per job.
#
# This is the portable half of build/run-mains.sh. run-mains.sh is the authoring
# instrument: it builds the plan, asserts the corpus, merges coverage and knows
# the host's paths. What it does *inside* the container -- expand a recipe,
# spark-submit it, capture stdout and the exit status -- is what a verifier has
# to do too, four months later, on a tree that has been migrated seven times.
#
# So that half lives here, takes its paths from the environment, and is shipped
# into every step's tests/ directory. It is not a rewrite of run-mains.sh's
# behavior: it is checked by replaying the census with it and diffing the .norm
# files against the ones run-mains.sh recorded (build/pkg/suite/check-suite.sh).
# An instrument that cannot reproduce a known answer is not measuring anything
# (trap 6).
#
#   APP     tree to run, with a built jar under target/   (default /app)
#   SUITE   this directory: jobs.tsv, harness/*.java      (default $0's dir)
#   OUT     where recordings go                           (default /out)
#   DATA    corpus: full/ reduced/ cif/ pdb/              (default /data)
#   JACOCO  jacocoagent.jar; coverage is off when unset   (default unset)
#   PHASES  which phases to run                           (default "1 2")
#   CORES   -XX:ActiveProcessorCount for each job       (default unset)
#   PROBE   gate-probe.jar; the listener is attached to
#           every job when set, and to none when not      (default unset)
#
# PROBE is opt-in rather than always-on for one reason: the recordings in ref/
# were taken without a listener, and a runner that quietly attached one would
# be replaying a different configuration from the one the answers came from.
# A step that needs the probe's facts asks for it, and build/pkg/suite/
# check-suite.sh has to show -- for that step, on that jar -- that asking
# changes no recording. Attaching a listener is not supposed to change what a
# job prints; "not supposed to" is not a measurement.
#
# When PROBE is set each job is handed two things and no path: GATE_PROBE_FD=9,
# the write end of a pipe this script drains into $OUT/probe/<row>.tsv, and
# GATE_PROBE_LABEL=<row name>, the key the probe writes its per-job facts
# under. The application's own spark.app.name would be the obvious key and it
# is not unique across this plan: three mains call themselves
# CustomReportDemo, and two rows are one class run with different arguments.
# The row name is unique by construction -- it is this file's output filename
# -- so it is the name a gate can attribute a red job to.
#
# Exit status is 0 if every job in the plan ran, whatever those jobs returned.
# Deciding what the recordings mean is the scorer's job, not this script's.
set -u

SUITE=${SUITE:-$(cd "$(dirname "$0")" && pwd)}
APP=${APP:-/app}
OUT=${OUT:-/out}
DATA=${DATA:-/data}
JACOCO=${JACOCO:-}
PHASES=${PHASES:-1 2}
CORES=${CORES:-}
PROBE=${PROBE:-}
# Directory order. Hadoop's local listing does not sort, so the order a corpus
# directory's part files become partitions in -- and with it every seeded
# RDD.sample draw and every partition-ordered print -- is whatever order the
# host filesystem returns (ext4: hash order under a per-filesystem seed, so a
# different order on every machine). Every job therefore runs under an
# LD_PRELOAD that returns each directory in byte order of its names
# (sortdir.c, built into the verifier image), and ref/ was recorded under it.
SORTDIR=${SORTDIR:-/opt/sortdir/libsortdir.so}

PROD=/tmp/prod
HJAR=/tmp/harness.jar

# log4j2 configures itself before Spark's logging is up, and its status logger
# resolves the local host while doing so. These jobs run with no network, and
# docker then leaves the container's own name out of /etc/hosts, so the lookup
# fails and log4j2 writes an UnknownHostException -- carrying a wall clock and
# the container id, which is new every run -- to *stdout*, ahead of anything the
# job prints. All 140 Spark 3.3 recordings in census c05 began with two of them,
# and the c04-to-c05 diff was 140 jobs moved and 0 the same (NOTES 4.9.4).
#
# norm.py drops the line, but that is the safety net, not the fix: a recording
# taken with the entry and one taken without it must normalise identically, and
# this is what makes the recording clean in the first place. It warns rather
# than exits -- a read-only /etc/hosts is not a reason to refuse to record.
if ! grep -q "[[:space:]]$(hostname)\$" /etc/hosts 2>/dev/null; then
    printf "127.0.0.1 %s\n" "$(hostname)" >> /etc/hosts 2>/dev/null \
        || echo "run-suite: cannot add $(hostname) to /etc/hosts;" \
                "log4j2 will log a host-name error norm.py has to absorb" >&2
fi

[ -r "$SORTDIR" ] || {
    echo "run-suite: no $SORTDIR -- the directory-order shim ref/ was recorded" \
         "under; without it the answers follow this host's filesystem" >&2; exit 2; }
for d in "$SUITE" "$APP" "$DATA"; do
    [ -d "$d" ] || { echo "run-suite: no such directory: $d" >&2; exit 2; }
done
[ -f "$SUITE/jobs.tsv" ] || { echo "run-suite: no jobs.tsv in $SUITE" >&2; exit 2; }
for d in full reduced cif pdb; do
    [ -d "$DATA/$d" ] || { echo "run-suite: corpus has no $d/ under $DATA" >&2; exit 2; }
done

# The jar the README names. A tree that built no jar, or built several, is a
# distinct failure from a tree whose jobs misbehave, and it is worth saying so
# rather than reporting 117 ClassNotFoundExceptions.
shopt -s nullglob
jars=("$APP"/target/mmtf-spark-*.jar)
shopt -u nullglob
keep=()
for j in "${jars[@]}"; do
    case ${j##*/} in original-*|*-sources.jar|*-javadoc.jar) ;; *) keep+=("$j") ;; esac
done
if [ ${#keep[@]} -ne 1 ]; then
    echo "run-suite: expected one target/mmtf-spark-*.jar under $APP, found ${#keep[@]}" >&2
    printf 'run-suite:   %s\n' "${keep[@]}" >&2
    exit 3
fi
JAR=${keep[0]}
echo "run-suite: jar $JAR ($(wc -c < "$JAR") bytes)"

mkdir -p "$OUT/reqlog" "$OUT/exec" "$PROD"
if [ -n "$PROBE" ]; then
    # Asserted here rather than discovered 140 jobs later. spark.extraListeners
    # is constructed by SparkContext and a class it cannot load *throws*, which
    # would turn a probe that was only meant to observe into 140 dead jobs.
    [ -f "$PROBE" ] || { echo "run-suite: no probe jar at $PROBE" >&2; exit 7; }
    mkdir -p "$OUT/probe"
    echo "run-suite: probe $PROBE ($(wc -c < "$PROBE") bytes), facts to $OUT/probe"
fi
: > "$OUT/summary.tsv"

# build/harness holds the entry points the tree does not have for itself, and it
# is compiled here against the jar this run is about to measure -- never against
# one built earlier. That is what makes it pin the API: a migration that renames
# a signature the harness names fails this compile, which is a louder and
# earlier answer than a silently empty recording.
#
# The jar is shaded (biojava and mmtf-java are inside it) but Spark is provided
# scope, so the compile classpath is that jar plus the image's own runtime.
if ls "$SUITE"/harness/*.java > /dev/null 2>&1; then
    rm -rf /tmp/harness-classes && mkdir -p /tmp/harness-classes
    # Bounded: the classpath is the candidate's jar, so this compile is the
    # candidate's to make slow.
    if timeout -k 30 600 javac -nowarn \
            -cp "$JAR:$(echo "$SPARK_HOME"/jars/*.jar | tr ' ' ':')" \
            -d /tmp/harness-classes "$SUITE"/harness/*.java 2>&1; then
        jar cf "$HJAR" -C /tmp/harness-classes .
    fi
    # Asserted, not assumed: a harness that failed to compile makes every
    # harness job a ClassNotFoundException, which reads as a red demo rather
    # than as a broken instrument (trap 7).
    if [ ! -s "$HJAR" ]; then
        echo "run-suite: harness compile produced no jar; aborting" >&2
        exit 4
    fi
    echo "run-suite: harness $(jar tf "$HJAR" | grep -c '[.]class$') class(es), $(wc -c < "$HJAR") bytes"
fi

# Placeholder expansion happens at job time, because {GLOB} can only be resolved
# once phase 1 has written the thing it matches.
expand() {
    # local - restores the shell options on return. Globbing is off for the
    # split, because a {GLOB:...} token contains * and would otherwise be
    # expanded against the cwd before it is ever read, and back on only where a
    # pattern is deliberately resolved.
    local - job=$1 raw=$2 tok out=
    set -f
    for tok in $raw; do
        case $tok in
            *"{GLOB:"*)
                local pat=${tok#*\{GLOB:}; pat=${pat%\}*}
                set +f; local hits=($PROD/$pat); set -f
                # nullglob is off, so an unmatched pattern comes back as itself:
                # the count alone would read "1" and the message would be a lie
                # about why the job was skipped.
                [ -e "${hits[0]}" ] || hits=()
                if [ ${#hits[@]} -ne 1 ]; then
                    echo "GLOB $pat matched ${#hits[@]} path(s)" >&2
                    return 1
                fi
                tok=${hits[0]} ;;
            *"{COPYOF:"*)
                # Same one-hit glob, so a product whose producer stamps the
                # clock into its name can be copied by shape.
                local src=${tok#*\{COPYOF:}; src=${src%\}*}
                set +f; local srcs=($PROD/$src); set -f
                [ -e "${srcs[0]}" ] || srcs=()
                if [ ${#srcs[@]} -ne 1 ]; then
                    echo "COPYOF $src matched ${#srcs[@]} path(s)" >&2
                    return 1
                fi
                cp -r "${srcs[0]}" "/tmp/out/$job/$(basename "${srcs[0]}")"
                tok=/tmp/out/$job/$(basename "${srcs[0]}") ;;
        esac
        tok=${tok//\{OUT\}//tmp/out/$job}
        tok=${tok//\{PROD\}/$PROD}
        tok=${tok//\{DATA\}//tmp/data}
        tok=${tok//\{CIF\}/$DATA/cif}
        tok=${tok//\{PDB\}/$DATA/pdb}
        out="$out $tok"
    done
    printf "%s" "${out# }"
}

run_phase() {
    local want=$1 phase job cls raw wantrc args rc t0 t1 extra dopts MASTERN
    local -a agent
    while IFS="$(printf '\t')" read -r phase job cls wantrc raw; do
        # An empty expectation compares unequal to every exit status, so a
        # mis-ordered plan would report red on every row and the reader would be
        # told 117 times that something is wrong. Asserted rather than trusted.
        case $wantrc in
            ""|*[!0-9]*)
                echo "run-suite: job $job has no expected status; aborting" >&2
                exit 5 ;;
        esac
        [ "$phase" = "$want" ] || continue
        rm -rf /tmp/data && mkdir -p /tmp/data
        cp -r "$DATA/full" "$DATA/reduced" /tmp/data/
        rm -rf "/tmp/out/$job" "/tmp/cwd/$job"
        mkdir -p "/tmp/out/$job" "/tmp/cwd/$job"
        : > /var/log/mmtf-mirror-requests.log

        if ! args=$(expand "$job" "$raw" 2>>"$OUT/$job.err"); then
            # The .out is created empty even though nothing ran: recordings are
            # diffed file by file, and a run where one side is missing a file is
            # a diff that cannot be taken.
            : > "$OUT/$job.out"
            printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$job" "$cls" 250 0 0 "$wantrc" >> "$OUT/summary.tsv"
            printf '%-52s %s\n' "$job" "SKIPPED: placeholder unresolved"
            continue
        fi

        # The harness jar rides along on --jars; the primary resource stays the
        # jar of the tree under test, so every job is the same spark-submit.
        extra=
        case $cls in harness.*) extra="--jars $HJAR" ;; esac
        # One --driver-java-options, because spark-submit keeps the last one it
        # is given and silently drops the others: coverage and core count were
        # two flags until the second one turned the first off.
        #
        # CORES is what makes a recording reproducible. 58 of the 78 classes
        # call setMaster("local[*]") in source, which beats --master, and
        # "local[*]" is availableProcessors(); with more than one thread the
        # executors write to the same stdout and the interleaving is a race.
        # 17 of 117 jobs recorded two different answers in two consecutive runs
        # of the same image at the same width because of it (NOTES 4.5).
        # -XX:ActiveProcessorCount is honoured by every JDK on the ladder (8u191
        # and later, and all of 11/17/21), so it pins the width from outside the
        # source without touching the source.
        dopts=
        # --master has to follow CORES or it undoes it: 20 of the 117 jobs are
        # classes that never call setMaster, so --master is the only thing that
        # sets their width, and three of those 20 are in the flaky set.
        MASTERN=${CORES:-2}
        [ -n "$CORES" ] && dopts="-XX:ActiveProcessorCount=$CORES"
        [ -n "$JACOCO" ] && dopts="$dopts -javaagent:$JACOCO=destfile=$OUT/exec/$job.exec,append=false,includes=edu.sdsc.mmtf.*"
        agent=()
        [ -n "$dopts" ] && agent=(--driver-java-options "${dopts# }")
        # The probe rides on the driver's class path, never on the primary
        # resource: the jar under test has to stay the jar under test, and a
        # --jars entry would put the listener on the executors' class path as
        # well, where it has nothing to observe and one more chance to fail.
        probeopt=()
        [ -n "$PROBE" ] && probeopt=(--driver-class-path "$PROBE"
                                     --conf spark.extraListeners=gate.GateProbe)

        # The probe's output does not travel by a path the job is told. Round 3
        # of the item-5 review found the road: GATE_PROBE_OUT named the file in
        # the child's own environment, the child is the candidate's JVM, and a
        # shutdown hook -- or anything after spark.stop() -- could rewrite the
        # file the listener had just written. Every conf prohibition the gate
        # reads is `absent`, so rewriting one file is worth the whole step and
        # costs 1 of 247 scored inputs.
        #
        # So the facts go down a pipe instead. The job is handed a file
        # descriptor, not a path, and a pipe cannot be retracted: a forger who
        # finds fd 9 can append to what the probe said but cannot unsay it, and
        # the rows the gate reads are `absent` selectors that a true row still
        # trips. The reader buffers and writes the file only if bytes arrived,
        # so "one file per application that reached a SparkContext" -- which is
        # what replay-probed counts -- still means exactly that.
        catpid=
        if [ -n "$PROBE" ]; then
            fifo="$OUT/probe/.$job.fifo"
            rm -f "$fifo"
            mkfifo -m 600 "$fifo"
            ( body=$(cat < "$fifo")
              [ -n "$body" ] && printf '%s\n' "$body" > "$OUT/probe/$job.tsv" ) &
            catpid=$!
            # Read-write so the reader does not see EOF between the open above
            # and the job's own open below.
            exec 9<>"$fifo"
        fi

        t0=$(date +%s)
        # cwd is per job and writable. Exactly one class in the tree writes a
        # relative path (DemoQueryVsAll.java:88, "alignments.parquet"), and with
        # cwd read-only it did all its work and then died on the write.
        ( cd "/tmp/cwd/$job" &&
          MMTF_FULL=/tmp/data/full MMTF_REDUCED=/tmp/data/reduced \
          LD_PRELOAD="$SORTDIR" \
          GATE_PROBE_FD="${PROBE:+9}" \
          GATE_PROBE_LABEL="${PROBE:+$job}" \
          timeout -k 10 900 spark-submit --class "$cls" $extra \
            --master "local[$MASTERN]" --driver-memory 3g \
            --conf spark.ui.enabled=false \
            --conf spark.driver.host=127.0.0.1 \
            --conf spark.driver.bindAddress=127.0.0.1 \
            "${agent[@]}" ${probeopt[@]+"${probeopt[@]}"} \
            "$JAR" $args ) >"$OUT/$job.out" 2>>"$OUT/$job.err"
        rc=$?
        t1=$(date +%s)
        if [ -n "$catpid" ]; then
            exec 9>&-
            wait "$catpid" 2>/dev/null || true
            rm -f "$OUT/probe/.$job.fifo"
        fi
        cp /var/log/mmtf-mirror-requests.log "$OUT/reqlog/$job.log" 2>/dev/null || true
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$job" "$cls" "$rc" "$((t1 - t0))" \
            "$(wc -c < "$OUT/$job.out")" "$wantrc" >> "$OUT/summary.tsv"
        printf '%-52s rc=%-4s %ss%s\n' "$job" "$rc" "$((t1 - t0))" \
            "$([ "$rc" = "$wantrc" ] || echo "   want=$wantrc")"
    done < "$SUITE/jobs.tsv"
}

for p in $PHASES; do
    echo "=== phase $p"
    run_phase "$p"
done
cat "$OUT"/reqlog/*.log > "$OUT/requests.log" 2>/dev/null || true

# A job that never wrote a summary row vanished from the run, which is the same
# shape of error as a step vanishing from a multi-step mean (trap 3). Counted
# here so the scorer is handed a complete set or an explicit failure.
planned=$(grep -c . "$SUITE/jobs.tsv")
ran=$(grep -c . "$OUT/summary.tsv")
echo "run-suite: planned=$planned ran=$ran"
if [ "$planned" != "$ran" ]; then
    awk -F'\t' 'NR==FNR{seen[$1]=1;next} !($2 in seen){print "run-suite: VANISHED " $2}' \
        "$OUT/summary.tsv" "$SUITE/jobs.tsv" >&2
    exit 6
fi
exit 0
