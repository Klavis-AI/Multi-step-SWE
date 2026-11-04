#!/usr/bin/env bash
# Re-derive every ref/*.norm in this directory from the recipe that made it.
#
# rule 3: the baseline is the oracle. The 140 recordings next to this script are
# the answers a candidate's tree is compared against, and a reference answer
# nobody can reproduce is a reference answer nobody can check -- including the
# person who wrote it. So the recipe that made them ships beside them, as one
# command, pinned in every part that could move.
#
# The recipe is not one recipe. Each step is graded against its own set of
# recordings, and the three authored so far were taken on three different trees
# at two different rungs of the version ladder. A script that pinned one of them
# and shipped beside all three would be exactly the failure it exists to rule
# out: a recipe that cannot produce the answers it sits next to. So the pins
# come from recipe.tsv, keyed by the census this payload's ../step.tsv names,
# and the command this script builds for the census `c04` is
#
#   docker run --rm --network none --cpus 7 \
#       -e TZ=UTC -e SPARK_LOCAL_IP=127.0.0.1 -e SOURCE_DATE_EPOCH=1546300800 \
#       -e CORES=1 -v "$TREE":/app -v "$PWD":/suite:ro -v "$OUT":/out \
#       mmtf-rung-r04:1 /suite/rederive-ref.sh
#
# where $TREE is the baseline commit with the solution/solve.sh of steps 01, 02
# and 03 applied in order -- which this script assembles itself, from the
# scripts the package ships, so that "the golden ladder reproduces these
# recordings" is a thing the reader runs rather than a thing the author says.
#
# Run with no arguments on a host that has docker and that is what happens; run
# inside a container and the inner half runs directly. Either way the script
# ends by comparing what it just produced against ref/ through the suite's own
# norm.py and projection.tsv, and exits non-zero on any difference the scorer
# would see.
#
# What each pin is for:
#
#   the rung    recipe.tsv's spark/scala/jdk columns, read off
#               $SPARK_HOME/jars/spark-core_<scala>-<spark>.jar and `java
#               -version` inside the container, before a single job runs. This
#               is the pin that matters most and the one that was missing: these
#               jobs take their engine from the image, not from the tree's pom
#               (spark-core is `provided`, the jar is shaded without it), so the
#               same 140 jobs in the wrong image come back as 140 plausible
#               recordings that differ from these in two places. At c04 those
#               two are AnalyzeWaterInteractions4 -- stddev over a single-row
#               group, NaN before Spark 3.1 and null after (SPARK-13860) -- and
#               HarnessDriver-g2s, a nested array of structs cast to string.
#               Running this in the 2.4 recording image is now a refusal in the
#               first second rather than a 25-minute wrong answer.
#   the ladder  recipe.tsv's ladder column: the steps whose solve.sh make the
#               tree, applied to BASELINE in order. `-` is the untouched
#               baseline.
#   BASELINE    sbl-sdsc/mmtf-spark at commit
#               e74298a6511732c5d6905eeec86b3a73e3d24447. It does not have to be
#               built: the jar is built inside the image, from the image's own
#               warmed Maven repository, which is one fewer thing that can
#               differ between the host and the run.
#   CORES=1     -XX:ActiveProcessorCount for every job, and local[1]. 17 of
#               these jobs are width-races: replaying at another width compares
#               against a different recording (NOTES 4.5).
#   --cpus 7    Harbor's quota. Kept here so the machine underneath cannot
#               change the answer by having more of itself available.
#   --network none  every job is offline; the corpus and the mirror are local.
#   TZ / SOURCE_DATE_EPOCH  a frozen clock (trap 10).
#
#   OUT      where the re-derived recordings go   (default /out, or ./rederived)
#   SUITE    this directory                       (default $0's dir)
#   CENSUS   which recipe.tsv row to use          (default ../step.tsv's census)
#   BASELINE the baseline checkout                (host mode; required unless
#                                                  TREE is given)
#   TREE     a prepared tree, used as-is          (host mode; skips the ladder)
#   IMAGE    the image to run in                  (default the row's; any image
#                                                  at the row's rung will do,
#                                                  and the step's own verifier
#                                                  image is one)
#   STEPS    where to find <id>-*/solution/solve.sh  (default: the steps/
#                                                  directory of the package this
#                                                  payload was cut from)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SUITE=${SUITE:-$HERE}
OUT=${OUT:-}

# --- the recipe ---------------------------------------------------------------
# step.tsv is the payload's stamp: mk-step-tests.sh writes the step id and the
# census into it, test.sh asserts it, and it is inside the manifest. Reading the
# census from there rather than from an argument is what makes this script's
# answer a property of the payload it shipped in.
CENSUS=${CENSUS:-$(awk -F'\t' '$1 == "census" {print $2}' "$SUITE/../step.tsv" 2>/dev/null)}
[ -n "${CENSUS:-}" ] || {
    echo "rederive-ref: no census in $SUITE/../step.tsv and CENSUS is unset" >&2; exit 2; }
[ -f "$SUITE/recipe.tsv" ] || { echo "rederive-ref: no recipe.tsv in $SUITE" >&2; exit 2; }
row=$(awk -F'\t' -v c="$CENSUS" '!/^#/ && $1 == c {print; exit}' "$SUITE/recipe.tsv")
[ -n "$row" ] || {
    echo "rederive-ref: recipe.tsv has no row for census '$CENSUS'" >&2
    echo "rederive-ref: rows: $(awk -F'\t' '!/^#/ && NF {printf "%s ", $1}' "$SUITE/recipe.tsv")" >&2
    exit 2; }
IFS=$'\t' read -r _ R_RUNG R_SPARK R_SCALA R_JDK R_LADDER R_IMAGE R_NOTE <<< "$row"
echo "rederive-ref: census $CENSUS -- $R_NOTE"
echo "rederive-ref: rung $R_RUNG, spark $R_SPARK, scala $R_SCALA, jdk $R_JDK, ladder $R_LADDER"

# --- host mode: assemble the tree and build the pinned command ----------------
if [ ! -f /.dockerenv ] && [ "${INNER:-0}" != 1 ]; then
    command -v docker > /dev/null 2>&1 || {
        echo "rederive-ref: no docker here and not inside a container;" \
             "set INNER=1 with APP and DATA to run the inner half" >&2; exit 2; }
    IMAGE=${IMAGE:-$R_IMAGE}
    docker image inspect "$IMAGE" > /dev/null 2>&1 || {
        echo "rederive-ref: no image $IMAGE here. Any image at rung $R_RUNG will" \
             "do -- spark $R_SPARK, scala $R_SCALA, jdk $R_JDK -- and this step's" \
             "own verifier image is one: build it from the tests/ directory this" \
             "payload came out of and pass IMAGE=. The rung is asserted inside" \
             "the container either way." >&2; exit 2; }

    W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
    if [ -n "${TREE:-}" ]; then
        echo "rederive-ref: TREE given, using it as-is and skipping the ladder"
    else
        BASELINE=${BASELINE:?rederive-ref: set BASELINE to a checkout of the baseline commit}
        [ -f "$BASELINE/pom.xml" ] || {
            echo "rederive-ref: $BASELINE has no pom.xml" >&2; exit 2; }
        TREE=$W/app; mkdir -p "$TREE"
        tar -C "$BASELINE" --exclude=.git --exclude=target -cf - . | tar -C "$TREE" -xf -
        if [ "$R_LADDER" != "-" ]; then
            # The solve scripts come from the package this payload was cut from.
            # Walked to rather than counted to: the payload sits five directories
            # down from the package root when it is shipped and somewhere else
            # entirely when it is unpacked, and a hard-coded ../../../../.. is a
            # path that is right in exactly one layout.
            if [ -z "${STEPS:-}" ]; then
                d=$HERE
                while [ "$d" != / ]; do
                    [ -f "$d/task.toml" ] && { STEPS=$d/steps; break; }
                    d=$(dirname "$d")
                done
            fi
            [ -n "${STEPS:-}" ] && [ -d "$STEPS" ] || {
                echo "rederive-ref: census $CENSUS needs the solve.sh of step(s)" \
                     "$R_LADDER and no steps/ directory was found above $HERE;" \
                     "set STEPS to the package's steps/ directory" >&2; exit 2; }
            for s in $R_LADDER; do
                # One hit, by construction: step directories are <id>-<name>.
                set -- "$STEPS/$s"-*/solution/solve.sh
                [ $# = 1 ] && [ -f "$1" ] || {
                    echo "rederive-ref: $# solution/solve.sh for step $s under $STEPS" >&2
                    exit 2; }
                bash "$1" "$TREE" > "$W/solve.$s.log" 2>&1 || {
                    sed 's/^/  /' "$W/solve.$s.log"
                    echo "rederive-ref: step $s's solve.sh failed over the baseline" >&2
                    exit 2; }
                printf 'rederive-ref: ladder %s | %s\n' "$s" "$(tail -1 "$W/solve.$s.log")"
            done
        fi
    fi
    OUT=${OUT:-$PWD/rederived}
    mkdir -p "$OUT"
    echo "rederive-ref: $TREE -> $OUT, in $IMAGE at CORES=1"
    # Not exec: $W holds the assembled tree and the EXIT trap that removes it
    # does not survive an exec.
    docker run --rm --network none --cpus 7 \
        -e TZ=UTC -e SPARK_LOCAL_IP=127.0.0.1 -e SOURCE_DATE_EPOCH=1546300800 \
        -e CORES=1 -e CENSUS="$CENSUS" \
        -v "$TREE:/app" -v "$SUITE:/suite:ro" -v "$OUT:/out" \
        "$IMAGE" /suite/rederive-ref.sh
    exit $?
fi

# --- inner half: the rung, asserted before anything is run --------------------
OUT=${OUT:-/out}
APP=${APP:-/app}
DATA=${DATA:-/data}
mkdir -p "$OUT"
for f in run-suite.sh norm.py jobs.tsv; do
    [ -f "$SUITE/$f" ] || { echo "rederive-ref: no $f in $SUITE" >&2; exit 2; }
done
[ -d "$SUITE/ref" ] || { echo "rederive-ref: no ref/ in $SUITE" >&2; exit 2; }

# The distribution's own name for itself. spark-core is the one jar every
# distribution has and the only one whose name carries both numbers, so this
# reads the engine that will run the jobs rather than the <spark.version> the
# tree asks for -- which, at `provided` scope, nothing here consults.
spark_dist() {  # spark_dist <spark home> -> "<scala> <spark>", or nothing
    local j
    for j in "$1"/jars/spark-core_*.jar; do
        [ -f "$j" ] || return 1
        j=${j##*/spark-core_}; j=${j%.jar}
        printf '%s %s' "${j%%-*}" "${j#*-}"
        return 0
    done
}
want="$R_SCALA $R_SPARK"
got=$(spark_dist "${SPARK_HOME:-/opt/spark}") || {
    echo "rederive-ref: no spark-core jar under ${SPARK_HOME:-/opt/spark}/jars" >&2; exit 2; }
# Trap 6: an assertion that cannot fail has not passed. Before the real one is
# believed, the same comparison is run against a distribution that is deliberately
# the wrong rung, and has to reject it.
# The control has to be a rung the census is not at: 2.4.8/2.11 for every
# census but r23, which *is* recorded there and gets 3.0.3/2.12 instead.
ctljar=spark-core_2.11-2.4.8.jar
[ "$want" = "2.11 2.4.8" ] && ctljar=spark-core_2.12-3.0.3.jar
ctl=$(mktemp -d); mkdir -p "$ctl/jars"; : > "$ctl/jars/$ctljar"
ctlgot=$(spark_dist "$ctl"); rm -rf "$ctl"
if [ "$ctlgot" = "$want" ]; then
    echo "rederive-ref: the rung check's control is at the same rung as census" \
         "$CENSUS, so it cannot show the check discriminating; fix the control" >&2
    exit 2
fi
if [ "$got" != "$want" ]; then
    echo "rederive-ref: this image carries spark-core_$(echo "$got" | tr ' ' '-')," \
         "census $CENSUS was recorded at spark-core_$(echo "$want" | tr ' ' '-')." \
         "These 140 jobs take their engine from the image, so replaying here" \
         "would produce recordings that are not these." >&2
    exit 2
fi
# `java -version` says 1.8.0_462 at JDK 8 and 17.0.15 at JDK 17, so the major
# version is the first component unless the first component is 1.
jdk=$(java -version 2>&1 | sed -n '1s/.*version "\([0-9]*\)\.\([0-9]*\).*/\1 \2/p' \
      | awk '{print ($1 == 1 ? $2 : $1)}')
[ "$jdk" = "$R_JDK" ] || {
    echo "rederive-ref: this image is JDK $jdk, census $CENSUS was recorded on JDK $R_JDK" >&2
    exit 2; }
echo "rederive-ref: rung ok -- spark-core_$R_SCALA-$R_SPARK, JDK $R_JDK (control rejected $ctlgot)"

# The jar, built here rather than brought in. The image's Maven repository was
# warmed from a tree on this ladder, so the build is offline; a tree that needs
# something the image does not have fails here, loudly, instead of failing 140
# times as a ClassNotFoundException.
shopt -s nullglob
jars=("$APP"/target/mmtf-spark-*.jar)
shopt -u nullglob
if [ ${#jars[@]} = 0 ]; then
    echo "rederive-ref: no jar under $APP/target; building it offline"
    ( cd "$APP" && mvn -B -q -o clean package -DskipTests ) || {
        echo "rederive-ref: mvn package failed over $APP" >&2; exit 2; }
fi

SUITE=$SUITE APP=$APP OUT=$OUT DATA=$DATA CORES=1 PHASES="1 2" \
    bash "$SUITE/run-suite.sh" || {
    echo "rederive-ref: run-suite.sh failed" >&2; exit 2; }
# The comparison, and a control on the comparison. Two numbers, because there
# are two honest ones: how many recordings come back byte-for-byte after
# norm.py, and how many come back equal under the comparison the scorer
# actually makes -- norm.py *and* projection.tsv. A job that differs only
# inside what its own projection row drops has not failed to reproduce; it is
# the documented value-nondeterminism that row exists for, and calling it a
# failure here would train whoever runs this to ignore the output. A job that
# differs after projection is a real one, and is what the exit status reports.
#
# Projected-text equality is at least as strict as scoring: score.py splits the
# same projected text into labeled blocks and pays per block, so anything that
# passes here passes there.
#
# The control corrupts one answer that matched and re-reads it through the same
# code -- an instrument that cannot fail has not passed (trap 6). The victim is
# the first matched job with no projection row, so that the projection cannot
# be what swallows the corruption, and the corrupting line is one no rule in
# the table can match.
python3 - "$SUITE" "$OUT" <<'PY'
import sys, pathlib, importlib.util
suite, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])


def load(name):
    spec = importlib.util.spec_from_file_location(name, str(suite / (name + '.py')))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


norm, proj = load('norm'), load('projection')
table = proj.load(str(suite / 'projection.tsv'))


def read(job, extra=''):
    raw = out / (job + '.out')
    if not raw.exists():
        return None
    return norm.normalize(raw.read_text(errors='replace') + extra)


want = sorted(p.stem for p in (suite / 'ref').glob('*.norm'))
exact = projected = 0
bad, soft, matched = [], [], []
for job in want:
    got = read(job)
    if got is None:
        bad.append(('ABSENT', job, 'no output from the replay'))
        continue
    (out / (job + '.norm')).write_text(got)
    ref = (suite / 'ref' / (job + '.norm')).read_text()
    if got == ref:
        exact += 1
        matched.append(job)
    if proj.project(job, got, table) == proj.project(job, ref, table):
        projected += 1
        if got != ref:
            soft.append(job)
    else:
        bad.append(('DIFF', job, 'differs after projection'))
for job in soft:
    print('  SOFT    %-44s only where projection.tsv drops: %s'
          % (job, table[job][2] if job in table else 'NOTHING -- and that is a bug'))
for kind, job, why in bad:
    print('  %-7s %-44s %s' % (kind, job, why))
print('rederive-ref: %d of %d recording(s) reproduce byte-for-byte after norm.py'
      % (exact, len(want)))
print('rederive-ref: %d of %d reproduce under the comparison the scorer makes'
      % (projected, len(want)))

plain = [j for j in matched if j not in table]
if not plain:
    print('rederive-ref: CONTROL SKIPPED -- no unprojected recording matched, '
          'so there is nothing safe to corrupt', file=sys.stderr)
    sys.exit(1)
victim = plain[0]
ref = (suite / 'ref' / (victim + '.norm')).read_text()
corrupt = read(victim, extra='zzz-control: a line no job ever printed\n')
if proj.project(victim, corrupt, table) == proj.project(victim, ref, table):
    print('rederive-ref: CONTROL FAILED -- a corrupted answer still compared equal',
          file=sys.stderr)
    sys.exit(1)
print('rederive-ref: control -- corrupting %s is seen' % victim)
sys.exit(0 if not bad else 1)
PY
