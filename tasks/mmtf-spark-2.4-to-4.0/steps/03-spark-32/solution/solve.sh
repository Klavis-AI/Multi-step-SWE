#!/usr/bin/env bash
# Step 3 reference solution: the build moves from Spark 3.0.3 to 3.2.4, and the
# twelve places that printed a statistic set the engine chose now name the set
# they want.
#
# One property in `pom.xml` and twelve lines of Java in two files. The property
# is one line because this repository already parameterises all three Spark
# artifacts on `${spark.version}`; that is a fact about the tree, not a
# shortcut, and it is exactly why this step cannot be *only* the property --
# see the last paragraph.
#
# The runtime move, and what arrives with it. 3.2.4 is two releases past 3.0.3
# and it changes two defaults that this library's output depends on:
#
#   * adaptive query execution is on by default from 3.2.0 (SPARK-33679). The
#     optimiser re-plans a stage after seeing its shuffle statistics, which is
#     why the gate asks for a re-plan event and not only for the setting: the
#     setting is a fact about the version, the event is a fact about the run.
#     Measured on this corpus at both rungs -- four of five sampled jobs report
#     a non-zero `adaptiveUpdate` at 3.2.4 and all five report zero at 3.0.3
#     (NOTES 6.3.11).
#   * `spark.sql.legacy.statisticalAggregate` arrives in 3.1 (SPARK-13860) and
#     defaults to false, so `stddev` over a single-row group renders NULL
#     rather than NaN. At 3.0.3 the key does not exist and the probe reads it
#     `<unset>`; there is no way to have the 3.1 semantics on a 3.0 engine.
#
# Neither of those is something the tree sets. They are things the tree *stops
# overriding* -- which is why the step's prohibition is on the legacy hatch and
# not on a switch the solution has to flip. A solution that set
# `spark.sql.legacy.statisticalAggregate=true` to "keep the old numbers" would
# pass the version rules and fail the step, and the gate says so by name.
#
# The source move, and why describe() is the site. `Dataset.describe(cols...)`
# is documented as exploratory and its statistic set is the engine's choice:
# count, mean, stddev, min, max today, whatever a later release decides
# tomorrow. `summary(statistics...)` takes the set as an argument, so the
# program states what it prints. Naming exactly describe's five statistics
# keeps the printed table byte-identical at this rung -- that is deliberate,
# and it is checked: the 140 recorded jobs are replayed against the recordings
# taken before this step, and a solution that changed what the demos print
# would lose the replay family it is supposed to keep.
#
# `select(col("<name>"))` before `summary` and not `summary` on the whole
# frame: describe("distance1") restricts to one column and summary() does not.
# `col(...)` rather than a bare `select("<name>")` because two of the twelve
# names are hyphenated (`O-Odistance`, `O-Ndistance`); the tree already calls
# `col("angle1-2")` a few lines above, so this is the idiom the file uses, and
# both files already import `static org.apache.spark.sql.functions.col`.
#
# Three things this deliberately does not do:
#
#   * it does not set `spark.sql.adaptive.enabled` anywhere. It is true by
#     default at 3.2.4 and setting it would make the tree assert a property of
#     the engine it is running on, which is the thing the version move is for.
#   * it does not touch the other 138 recorded jobs. The rung move is what
#     re-runs them; if any of them changes what it prints, the replay says so
#     and that is the measurement this step is buying.
#   * it does not undo step 2. The fourteen Dataset counters and
#     MapReduceExample's `agg(sum(...))` are held as preconditions here, and
#     the sweep at the bottom asserts they survived.
#
# Every replacement asserts its own occurrence count before anything is
# written, and every file is read back off disk afterwards (trap 7).
#
# Why this is not "edit pom.xml and stop". The property alone reaches 3.2.4 --
# so a one-line tree answers all of m1, which is the whole of this step's
# version axis. That is measured, not feared: the calibration tree that edits
# only the property scores 0.16 of 1.00 (NOTES 6.3.11). The other 0.84 is the
# twelve call sites and the replay that has to keep agreeing afterwards, which
# is why the gate's behavior half asks for describe() to be gone *and* for a
# named set to have taken over, and refuses to pay either half on its own.
#
# What the property does *not* buy is the adaptive default or the
# statistical-aggregate default: item 4.3 measured those on the untouched tree
# and found them already true, because the replay submits through the image's
# /opt/spark and `spark-core` is `provided`. They are standing rules here
# (`gates/rules/rung3.tsv`), not requirements -- preconditions and prohibitions
# rather than payment.
set -euo pipefail

APP=${1:-/app}
if [ -f "$APP" ]; then APP=$(dirname "$APP"); fi     # tolerate a pom.xml path
SRC=$APP/src/main/java/edu/sdsc/mmtf/spark
POM=$APP/pom.xml
[ -d "$SRC" ] || { echo "solve: no sources at $SRC" >&2; exit 1; }
[ -f "$POM" ] || { echo "solve: no pom at $POM" >&2; exit 1; }

python3 - "$SRC" "$POM" <<'PYEOF'
import os, re, sys

root, pom = sys.argv[1], sys.argv[2]
edited = []


def rewrite(path, rel, edits, checks):
    """Apply every edit exactly once, then read the file back and assert."""
    if not os.path.exists(path):
        sys.exit("solve: no such file: %s" % path)
    src = open(path, encoding="utf-8").read()
    n0 = len(src)
    for old, new, what in edits:
        seen = src.count(old)
        if seen != 1:
            sys.exit("solve: %s: %s matched %d time(s), expected 1"
                     % (rel, what, seen))
        src = src.replace(old, new)
    open(path, "w", encoding="utf-8").write(src)
    out = open(path, encoding="utf-8").read()
    bad = [w for w, ok in ((w, p(out)) for w, p in checks) if not ok]
    if bad:
        sys.exit("solve: %s: edits landed but the file does not read right: %s"
                 % (rel, ", ".join(bad)))
    edited.append((rel, n0, len(out), len(edits)))


# ------------------------------------------------------------------ the rung
#
# The three spark artifacts all read ${spark.version}, so the fence the gate
# checks -- every org.apache.spark artifact resolving at 3.2.4 and none at
# anything else -- follows from this one property. Asserted rather than
# assumed: if a later edit ever pins one of them directly, the count below is
# still 1 and the resolved-version rule is what catches it.
rewrite(pom, "pom.xml", [
    ("<spark.version>3.0.3</spark.version>",
     "<spark.version>3.2.4</spark.version>",
     "the spark.version property"),
], [
    ("the property reads 3.2.4",
     lambda s: "<spark.version>3.2.4</spark.version>" in s),
    ("no 3.0.3 left anywhere in the pom", lambda s: "3.0.3" not in s),
    ("all three spark artifacts still read the property",
     lambda s: s.count("<version>${spark.version}</version>") == 3),
])

# ----------------------------------------------------------- the twelve sites
#
# Each site has the shape
#
#     <receiver>.describe("<column>").show();
#
# and the rewrite is driven off that shape rather than off a per-file literal:
# this file mixes two tabs and eight spaces for the same nesting depth, and a
# literal table would bake that inconsistency into the patch where the next
# reader would have to decide whether it mattered.
SITES = [
    ("interactions/demos/AnalyzeWaterInteractions4.java", "AnalyzeWaterInteractions4", 11),
    ("datasets/demos/PdbLigandDemo.java",                 "PdbLigandDemo",              1),
]

# describe()'s own five, in describe()'s own order, so the printed table does
# not move. Held in a constant rather than repeated at eleven call sites: the
# point of the step is that the program states its statistic set once, and
# eleven copies of the same five strings would state it eleven times and leave
# eleven places for them to drift apart.
DECL = ('%(ind)s/** The statistics these summaries print, named rather than left\n'
        '%(ind)s * to the engine: this is describe()\'s set, stated by the\n'
        '%(ind)s * program so that it cannot change underneath it. */\n'
        '%(ind)sprivate static final String[] STATS =\n'
        '%(ind)s%(ind)s{ "count", "mean", "stddev", "min", "max" };\n')

LINE = re.compile(r'^([ \t]*)(\w+)\.describe\((".*?")\)\.show\(\);[ \t]*$', re.M)

for rel, cls, want in SITES:
    path = os.path.join(root, rel)
    src = open(path, encoding="utf-8").read()
    hits = list(LINE.finditer(src))
    if len(hits) != want:
        sys.exit("solve: %s: %d describe site(s), expected %d"
                 % (rel, len(hits), want))
    edits = [("public class %s {\n" % cls,
              "public class %s {\n\n" % cls + DECL % {"ind": "    "},
              "the STATS declaration")]
    for m in hits:
        ind, recv, colname = m.group(1), m.group(2), m.group(3)
        edits.append((m.group(0),
                      ind + recv + ".select(col(" + colname
                      + ")).summary(STATS).show();",
                      "the describe site for " + colname))
    rewrite(path, rel, edits, [
        ("no describe left",  lambda s: ".describe(" not in s),
        ("the set is named",  lambda s: "summary(STATS)" in s),
        ("stddev is stated",  lambda s: '"stddev"' in s),
        ("one declaration",   lambda s: s.count("String[] STATS") == 1),
        ("col is imported",
         lambda s: "import static org.apache.spark.sql.functions.col;" in s),
        ("no legacy hatch",   lambda s: "spark.sql.legacy." not in s),
        ("no opaque encoder", lambda s: "Encoders.kryo" not in s
                                        and "javaSerialization" not in s),
        ("no forged id",      lambda s: "setLocalProperty" not in s),
    ])

for rel, n0, n1, k in edited:
    print("solve: %s, %d -> %d bytes, %d edit(s)" % (rel, n0, n1, k))
print("solve: %d file(s) rewritten" % len(edited))
if len(edited) != 3:
    sys.exit("solve: expected 3 files, rewrote %d" % len(edited))
PYEOF

# Tree-wide, off the bytes on disk rather than off what the script believes it
# wrote. The first three are standing prohibitions carried from step 2; the
# fourth is this step's own, and it is swept over the whole tree rather than
# over the two edited files because a legacy switch set in a class nobody
# edited is exactly as effective as one set in a class somebody did.
# `grep -rq` and not `$(grep | wc -l)`: under `set -o pipefail` a grep that
# matches nothing fails the pipeline, and the success case here is the one
# where it matches nothing.
for bad in 'Encoders\.kryo' 'javaSerialization' 'setLocalProperty' 'spark\.sql\.legacy\.'; do
    if grep -rqE "$bad" "$SRC"; then
        echo "solve: $bad occurs in the tree" >&2; exit 1
    fi
done
if grep -qE 'spark\.sql\.legacy\.' "$POM"; then
    echo "solve: spark.sql.legacy. occurs in the pom" >&2; exit 1
fi

# Twelve sites, counted here as well as per file, because a per-file check
# cannot see a file that was never visited. The literal is the one this script
# writes; `summary(` alone would also have matched nothing before the step and
# `.show()` would have matched 80-odd lines that have nothing to do with it.
n=$(grep -rcF '.summary(STATS).show();' "$SRC" | awk -F: '{t += $2} END {print t+0}')
[ "$n" = 12 ] || { echo "solve: $n site(s) migrated, expected 12" >&2; exit 1; }
if grep -rq '\.describe(' "$SRC"; then
    echo "solve: a describe() call remains in the tree" >&2; exit 1
fi

# Step 2 is a precondition here, not this step's work, and a step-3 solution
# that arrived on a tree where it had been undone would be scored against
# fourteen held rules it cannot see. Asserted so the failure reads as "the tree
# handed to me was wrong" rather than as fourteen unexplained reds.
c=$(grep -rlF 'Dataset<String> ids = spark.createDataset(' "$SRC" | wc -l)
[ "$c" = 14 ] || { echo "solve: $c step-2 counter(s) present, expected 14" >&2; exit 1; }
grep -qF 'agg(sum(atoms.col("value")))' "$SRC/mappers/demos/MapReduceExample.java" \
    || { echo "solve: step 2's MapReduceExample edit is missing" >&2; exit 1; }

echo "solve: step 3 complete"
