#!/usr/bin/env bash
# Step 2 reference solution: fifteen demos take their terminal result through
# the SQL engine instead of off an RDD.
#
# Fifteen files of Java, one edit each, and no build change at all: the rung
# does not move at this step and `environment/` is byte-identical to step 1's.
#
# The move, and why it is the one the measurement chose (NOTES 6.2.5). A
# `JavaPairRDD<String, StructureDataInterface>` cannot be encoded -- the value
# is an interface with no schema, and the only encoder that would take it is
# `Encoders.kryo`, which produces a Dataset the engine cannot plan over and
# which this step's gate refuses by name. What *is* encodable is the key: the
# structure id, one `Encoders.STRING()` column. So the count is taken over
#
#     spark.createDataset(pdb.keys().rdd(), Encoders.STRING()).count()
#
# and the printed number does not change, because `keys()` preserves
# cardinality. It is one job, and the job is inside a SQL execution: the
# terminal Dataset action drags the whole RDD lineage -- the sequence-file
# read, the filters, the flatMapToPair -- into the execution rather than
# submitting an RDD job beside it. That was measured across four honest Dataset
# shapes before this step was designed, and all four land on one job (NOTES
# 6.2.5, the shapes table).
#
# MapReduceExample is the one row whose answer is not a count, and it gets the
# aggregate shape from the same table: the mapped atom counts become a typed
# `Dataset<Integer>` and the total comes back from `agg(sum(...)).head()`. The
# printed value is the same 528527 -- the corpus is far from overflowing an
# int, so the SQL `sum`'s long arithmetic and the RDD reduce's int arithmetic
# agree here -- and that is asserted against the recording rather than assumed.
#
# Three things this deliberately does not do:
#
#   * it does not touch `MmtfReader`. Its contract is a JavaPairRDD keyed by
#     structure id, 57 of this library's 78 mains are written against it,
#     and a
#     Dataset reader over an opaque interface is the Kryo cheat with a
#     different owner. Two standing rules say so (NOTES 6.2.1).
#   * it does not rewrite `SparkConf`/`JavaSparkContext` into
#     `SparkSession.builder().master(...)`. That is a different migration, it
#     would move the width every recording was taken at (NOTES 4.5.2), and
#     nothing this step is graded on reads it. `SparkSession.builder()
#     .getOrCreate()` returns a session over the context that already exists.
#   * it does not move the `foreach` prints or the `MmtfWriter` calls of
#     ReadLocalMmtf, ReadLocalMmtfHadoopFile, KeywordSearch and
#     CreateRepresentativeSet. Those are RDD-native terminals that this step
#     leaves alone, which is why their rules ask for a job *inside* an
#     execution rather than for no job outside one.
#
# Every replacement asserts its own occurrence count before anything is
# written, and every file is read back off disk afterwards (trap 7). The
# failure mode worth guarding here is a script that silently matched nothing:
# it would leave a tree that still builds, still passes the repository's own
# suite, still reproduces all 140 recordings, and answers none of the four
# requirements.
set -euo pipefail

APP=${1:-/app}
if [ -f "$APP" ]; then APP=$(dirname "$APP"); fi     # tolerate a pom.xml path
SRC=$APP/src/main/java/edu/sdsc/mmtf/spark
[ -d "$SRC" ] || { echo "solve: no sources at $SRC" >&2; exit 1; }

python3 - "$SRC" <<'PYEOF'
import os, re, sys

root = sys.argv[1]
edited = []

ANCHOR = "import org.apache.spark.api.java.JavaSparkContext;\n"
SQL_IMPORTS = (ANCHOR
               + "import org.apache.spark.sql.Dataset;\n"
               + "import org.apache.spark.sql.Encoders;\n"
               + "import org.apache.spark.sql.SparkSession;\n")


def rewrite(rel, edits, checks):
    """Apply every edit exactly once, then read the file back and assert."""
    path = os.path.join(root, rel)
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


# The fourteen counters. Each one has exactly one line of the shape
#
#     System.out.println("<label>" + <rdd>.count());
#
# and the rewrite is driven off that line rather than off a per-file literal:
# the indent is whatever the file already used (these demos mix a tab and four
# spaces with two tabs), so the replacement cannot introduce a whitespace
# change that the next reader has to explain.
COUNTERS = [
    ("filters/demos/FilterByResolution.java",        "pdb"),
    ("filters/demos/FilterBySequenceRegex.java",     "pdb"),
    ("io/demos/MmtfBenchmark.java",                  "pdb"),
    ("io/demos/ReadMmtfFull.java",                   "pdb"),
    ("io/demos/ReadMmtfReduced.java",                "pdb"),
    ("io/demos/ReadLocalMmtf.java",                  "pdb"),
    ("io/demos/ReadLocalMmtfHadoopFile.java",        "pdb"),
    ("mappers/demos/MapToBioAssembly.java",          "bioassemblies"),
    ("mappers/demos/MapToBioAssembly2.java",         "bioassemblies"),
    ("mappers/demos/MapToProteinDimers.java",        "pdb"),
    ("webfilters/demos/CreateRepresentativeSet.java", "pdb"),
    ("webfilters/demos/AuthorSearchDemo.java",       "pdb"),
    ("webfilters/demos/KeywordSearch.java",          "pdb"),
    ("webfilters/demos/KinaseSearch.java",           "pdb"),
]

LINE = re.compile(r'^([ \t]*)System\.out\.println\((".*?")\s*\+\s*'
                  r'(\w+)\.count\(\)\);[ \t]*$', re.M)

for rel, var in COUNTERS:
    path = os.path.join(root, rel)
    src = open(path, encoding="utf-8").read()
    hits = [m for m in LINE.finditer(src)]
    if len(hits) != 1:
        sys.exit("solve: %s: %d count line(s), expected 1" % (rel, len(hits)))
    m = hits[0]
    if m.group(3) != var:
        sys.exit("solve: %s: counts %s, expected %s" % (rel, m.group(3), var))
    ind = m.group(1)
    new = (ind + "// take the count through the SQL engine: counting a typed\n"
           + ind + "// Dataset of the structure ids runs the read, the filters and\n"
           + ind + "// the mappers inside one SQL execution\n"
           + ind + "SparkSession spark = SparkSession.builder().getOrCreate();\n"
           + ind + "Dataset<String> ids = spark.createDataset("
           + var + ".keys().rdd(), Encoders.STRING());\n"
           + ind + "System.out.println(" + m.group(2) + " + ids.count());")
    rewrite(rel, [
        (ANCHOR, SQL_IMPORTS, "the three org.apache.spark.sql imports"),
        (m.group(0), new, "the terminal count"),
    ], [
        ("no RDD count left", lambda s, v=var: (v + ".count()") not in s),
        ("the Dataset counts", lambda s: "ids.count()" in s),
        ("one session",        lambda s: s.count("SparkSession.builder()") == 1),
        ("no opaque encoder",  lambda s: "Encoders.kryo" not in s
                                         and "javaSerialization" not in s),
        ("no forged id",       lambda s: "setLocalProperty" not in s),
        ("the reader is still read",
         lambda s: "MmtfReader" in s and "JavaPairRDD" in s),
    ])

# MapReduceExample: the one row whose printed answer is a total rather than a
# count. `agg(sum(...)).head()` is the third of NOTES 6.2.5's four honest
# shapes and lands on the same one-job-inside-one-execution as the others.
rewrite("mappers/demos/MapReduceExample.java", [
    ("package edu.sdsc.mmtf.spark.mappers.demos;\n\n",
     "package edu.sdsc.mmtf.spark.mappers.demos;\n\n"
     "import static org.apache.spark.sql.functions.sum;\n\n",
     "the static sum import"),
    (ANCHOR, SQL_IMPORTS, "the three org.apache.spark.sql imports"),
    ("\t    // count number of atoms\n"
     "\t    long numAtoms = pdb.map(t -> t._2.getNumAtoms()).reduce((a,b) -> a+b);",
     "\t    // count number of atoms, summed by the SQL engine over a typed\n"
     "\t    // Dataset rather than reduced off the RDD\n"
     "\t    SparkSession spark = SparkSession.builder().getOrCreate();\n"
     "\t    Dataset<Integer> atoms = spark.createDataset(\n"
     "\t            pdb.map(t -> t._2.getNumAtoms()).rdd(), Encoders.INT());\n"
     "\t    long numAtoms = atoms.agg(sum(atoms.col(\"value\"))).head().getLong(0);",
     "the map-reduce total"),
], [
    ("no RDD reduce",     lambda s: ".reduce(" not in s),
    ("the engine sums",   lambda s: 'agg(sum(atoms.col("value")))' in s),
    ("the map survives",  lambda s: "pdb.map(t -> t._2.getNumAtoms())" in s),
    ("one session",       lambda s: s.count("SparkSession.builder()") == 1),
    ("no opaque encoder", lambda s: "Encoders.kryo" not in s
                                    and "javaSerialization" not in s),
    ("no forged id",      lambda s: "setLocalProperty" not in s),
])

for rel, n0, n1, k in edited:
    print("solve: %s, %d -> %d bytes, %d edit(s)" % (rel, n0, n1, k))
print("solve: %d file(s) rewritten" % len(edited))
if len(edited) != 15:
    sys.exit("solve: expected 15 files, rewrote %d" % len(edited))
PYEOF

# Tree-wide, off the bytes on disk rather than off what the script believes it
# wrote. The first two are the two anti-cheat prohibitions the gate prices as
# standing rules; the third and fourth are the reader's contract. A solution
# that tripped any of them would score zero for migrated-ness with every
# requirement green, which is the confusing failure worth refusing here.
# `grep -rq` and not `$(grep | wc -l)`: under `set -o pipefail` a grep that
# matches nothing fails the pipeline, and the success case here is exactly the
# one where it matches nothing.
for bad in 'Encoders\.kryo' 'javaSerialization' 'setLocalProperty'; do
    if grep -rqE "$bad" "$SRC"; then
        echo "solve: $bad occurs in the tree" >&2; exit 1
    fi
done
R=$SRC/io/MmtfReader.java
grep -q 'JavaPairRDD<String, StructureDataInterface> readSequenceFile' "$R" \
    || { echo "solve: MmtfReader no longer returns a JavaPairRDD" >&2; exit 1; }
if grep -q 'Dataset' "$R"; then
    echo "solve: MmtfReader was moved into the SQL engine" >&2; exit 1
fi

# Fifteen files, counted here as well as per file, because a per-file check
# cannot see a file that was never visited. The literal is the one this script
# writes and not `SparkSession.builder()`, which 27 files of this tree already
# contained before the step -- a count over that would have been 30 on a
# correct tree and 27 on a tree where nothing happened, and the check would
# have been measuring the library rather than the step.
n=$(grep -rlF 'Dataset<String> ids = spark.createDataset(' "$SRC" | wc -l)
[ "$n" = 14 ] || { echo "solve: $n counter(s) migrated, expected 14" >&2; exit 1; }
grep -qF 'agg(sum(atoms.col("value")))' "$SRC/mappers/demos/MapReduceExample.java" \
    || { echo "solve: MapReduceExample still reduces off the RDD" >&2; exit 1; }

echo "solve: step 2 complete"
