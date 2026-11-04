#!/usr/bin/env bash
# Step 5 reference solution: the build moves from Spark 3.3.4 on Scala 2.12
# under Java 8 to Spark 3.4.4 on Scala 2.13 under Java 17, and the six csv reads
# that ask the engine to infer their types pin date inference off at the site, so
# that the move does not change the types those readers produce.
#
# Three axes in the pom and one idiom in five source files. Unlike steps 3 and
# 4, the pom half here is not one property: the Scala suffix is part of the
# artifact id, so `${spark.version}` carries the version and nothing carries the
# binary compatibility. A tree that bumps only the property resolves
# `spark-core_2.12:3.4.4` -- a real artifact, and the wrong one -- which is why
# `m1-spark-344` reads the resolved coordinate rather than the property.
#
# The JDK axis, and the one thing it makes a tree do for itself. Nothing in
# `src/main` uses anything JEP 320 removed: zero occurrences of `javax.xml.bind`,
# `sun.misc`, `SecurityManager`, `Thread.stop`, `com.sun.*` or `AccessController`,
# and the only `newInstance()` calls are reflection this step does not touch
# (NOTES 6.5.3). What does break is the *suite*: on 17, `StorageUtils` reaches
# `sun.nio.ch.DirectBuffer` while the session is starting, and under JPMS that is
# an IllegalAccessError before a single test body runs -- 256 errors against 0.
# `spark-submit` passes Spark's own `JavaModuleOptions` for itself, which is why
# every replayed program is unaffected and only the forked test JVM is; so the
# fix belongs on surefire's `argLine`, and it is one open, not the launcher's
# fifteen (measured down to one in NOTES 6.5.2).
#
# Why the pom also moves `jdk.version` and `maven.enforcer.jdk-version`: no
# plugin in this build reads either -- they are the upstream project's statement
# of what it targets, and no gate rule reads them. They move because leaving a
# pom that says 1.8 in four places and 17 in two is not a migration, it is half
# of one. A candidate who leaves them scores exactly the same.
#
# The source move, and why it is the reads. 3.4 adds `preferDate` to
# `CSVOptions`, defaulting true: a csv column that looks like a date is inferred
# `DateType` at 3.4.4 where it was `TimestampType` at 3.3.4 (NOTES 6.5.1, cited
# to `spark-catalyst_2.13-3.4.4.jar` `CSVOptions.<init>` 708/711/712). That is a
# silent change of answer in every reader that says `inferSchema=true` and
# nothing else, and this repository has six of them. A runtime move is not
# licensed to change what the code's readers produce, so the migration pins the
# old answer at every one of the six rather than merely mentioning the option:
#
#   before:  .option("inferSchema", "true")
#   after:   .option("inferSchema", "true").option("preferDate", "false")
#
# `false` and not `true`, and the difference is observed rather than argued.
# `CustomReportService.getDataset("pmc", "pubmedId", "depositionDate")` is a read
# the census now takes, through `HarnessDriver-csvdates`: on mmtf-rung-r05:1
# (Spark 3.3.4) `depositionDate` infers `timestamp` both by default and under
# `preferDate=false`, while on mmtf-rung-r09:1 (Spark 3.4.4) it infers `date` by
# default and `timestamp` under `preferDate=false`. So `false` is the value that
# holds the type still across the move, and it is the one the recording carries:
# a tree that writes `true`, or that leaves the reads silent, prints `date` where
# c09 says `timestamp` and loses that job. `m3-csv-dates-pinned` reads the same
# requirement off the sources, and `m3-csv-dates-stated` refuses the silent read.
#
# The seventh csv read is not edited and is not an exception. `DatasetFileConverter:100`
# is a bare `spark.read().csv(path)` with no `inferSchema`: it does not ask the
# engine to infer, so it has nothing to state, and it falls outside the rule by
# the rule's own shape rather than by a file-name carve-out. `m3-csv-sites-kept`
# counts all seven, so deleting it to shrink the problem is not a route either.
#
# What this step is not. It does not declare schemas. Two of the three json
# readers detect "service found nothing" by asking the frame how many columns
# came back (`G2SDataset:135`, `MyVariantDataset:115`), and a declared schema
# makes that test never fire -- so a requirement to declare would have made the
# 141-job replay unpassable by the very tree that satisfied it (NOTES 6.5.3).
# It also does not add a JAXB dependency: the runtime is genuinely needed at
# 4.0.1 and Spark 3.4.4 still supplies it transitively, so at this rung the pin
# would be graded ritual (NOTES 6.5.2). Both were requirements until they were
# measured.
#
# Every replacement asserts its own occurrence count before anything is written,
# and every file is read back off disk afterwards (trap 7). The tree-wide sweeps
# at the bottom count off the bytes on disk rather than off what this script
# believes it wrote.
#
# Why this is not "edit pom.xml and stop". The whole pom move -- version, both
# suffixes, the compiler level, the fork's open -- answers m1 and m2 and leaves
# m3 red, and the behaviour families are milestone-gated on the reads having been
# stated. That tree collects 0.30 of 1.00. A tree that bumps only
# `<spark.version>` collects 0.000000.
set -euo pipefail

APP=${1:-/app}
if [ -f "$APP" ]; then APP=$(dirname "$APP"); fi     # tolerate a pom.xml path
SRC=$APP/src/main/java/edu/sdsc/mmtf/spark
POM=$APP/pom.xml
[ -d "$SRC" ] || { echo "solve: no sources at $SRC" >&2; exit 1; }
[ -f "$POM" ] || { echo "solve: no pom at $POM" >&2; exit 1; }

python3 - "$SRC" "$POM" <<'PYEOF'
import os, sys

root, pom = sys.argv[1], sys.argv[2]

INFER = '.option("inferSchema", "true")'
STATED = '.option("inferSchema", "true").option("preferDate", "false")'
OPENS = '--add-opens=java.base/sun.nio.ch=ALL-UNNAMED'


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def write_back(path, rel, src, checks):
    """Write, read the bytes back off disk, and assert the file reads right."""
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(src)
    out = read(path)
    bad = [w for w, ok in ((w, p(out)) for w, p in checks) if not ok]
    if bad:
        sys.exit("solve: %s: edits landed but the file does not read right: %s"
                 % (rel, ", ".join(bad)))


def substitute(src, rel, edits):
    """Anchored replacement, each with the number of hits it must have."""
    for old, new, want in edits:
        got = src.count(old)
        if got != want:
            sys.exit("solve: %s: %r occurs %d time(s), expected %d"
                     % (rel, old, got, want))
        src = src.replace(old, new)
    return src


# ------------------------------------------------------------------ the rung
#
# Nine anchored edits in one file. The three artifact ids are spelled out rather
# than swept with a 2.12 -> 2.13 replacement: the string "2.12" also appears in
# version numbers in this pom's plugin block, and a sweep that caught one of
# those would move a plugin version and be discovered by a build failure three
# minutes later rather than here.
src = read(pom)
src = substitute(src, "pom.xml", [
    ("<spark.version>3.3.4</spark.version>",
     "<spark.version>3.4.4</spark.version>", 1),
    ("<artifactId>spark-core_2.12</artifactId>",
     "<artifactId>spark-core_2.13</artifactId>", 1),
    ("<artifactId>spark-sql_2.12</artifactId>",
     "<artifactId>spark-sql_2.13</artifactId>", 1),
    ("<artifactId>spark-mllib_2.12</artifactId>",
     "<artifactId>spark-mllib_2.13</artifactId>", 1),
    ("<jdk.version>1.8</jdk.version>",
     "<jdk.version>17</jdk.version>", 1),
    ("<maven.enforcer.jdk-version>1.8</maven.enforcer.jdk-version>",
     "<maven.enforcer.jdk-version>17</maven.enforcer.jdk-version>", 1),
    ("<maven.compiler.source>1.8</maven.compiler.source>",
     "<maven.compiler.source>17</maven.compiler.source>", 1),
    ("<maven.compiler.target>1.8</maven.compiler.target>",
     "<maven.compiler.target>17</maven.compiler.target>", 1),
    ("<argLine>-Xmx4g</argLine>",
     "<argLine>-Xmx4g %s</argLine>" % OPENS, 1),
])
write_back(pom, "pom.xml", src, [
    ("the property reads 3.4.4",
     lambda s: "<spark.version>3.4.4</spark.version>" in s),
    ("no 3.3.4 left anywhere in the pom", lambda s: "3.3.4" not in s),
    ("all three spark artifacts still read the property",
     lambda s: s.count("<version>${spark.version}</version>") == 3),
    ("no _2.12 artifact id left", lambda s: "_2.12" not in s),
    ("three _2.13 artifact ids", lambda s: s.count("_2.13") == 3),
    ("the compiler asks for 17",
     lambda s: s.count("<maven.compiler.source>17</maven.compiler.source>") == 1
     and s.count("<maven.compiler.target>17</maven.compiler.target>") == 1),
    ("no 1.8 left anywhere in the pom", lambda s: "1.8" not in s),
    ("the forked test JVM is opened once", lambda s: s.count(OPENS) == 1),
    ("the tests are not skipped",
     lambda s: "skipTests" not in s and "maven.test.skip" not in s),
])
print("solve: pom.xml, 3.3.4/2.12/1.8 -> 3.4.4/2.13/17 with the test fork opened")


# ------------------------------------------------------- the inferring reads
#
# Driven off the option string rather than off a five-row table of file names,
# for the reason the requirement is not written as one: the property is "a read
# that asks the engine to infer states its date handling", and a table would be
# a list of the places that happened to be true today. All six sites spell the
# option identically, which was measured before this was written; a site spelled
# any other way would fail the total below rather than be silently skipped.
stated = 0
files = []
for dirpath, _, names in os.walk(root):
    for name in sorted(names):
        if not name.endswith(".java"):
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        src = read(path)
        n = src.count(INFER)
        if not n:
            continue
        src = substitute(src, rel, [(INFER, STATED, n)])
        write_back(path, rel, src, [
            ("every inferring read states its dates",
             lambda s, n=n: s.count(STATED) == n),
            ("no inferring read was left silent",
             lambda s, n=n: s.count(INFER) == s.count(STATED)),
            ("no schema was declared instead", lambda s: ".schema(" not in s),
            ("no option map", lambda s: ".options(" not in s),
        ])
        stated += n
        files.append(rel)

if stated != 6:
    sys.exit("solve: %d inferring read(s) stated, expected 6" % stated)
if len(files) != 5:
    sys.exit("solve: %d file(s) carried one, expected 5" % len(files))
print("solve: %d inferring csv read(s) in %d file(s) now state their dates: %s"
      % (stated, len(files), " ".join(sorted(files))))
PYEOF

# Tree-wide, off the bytes on disk rather than off what the script believes it
# wrote. A per-file check cannot see a file that was never visited, and the
# counts below are the ones that would move if the shape driving this script
# ever stopped describing the tree.
#
# `grep -rq` rather than `$(grep | wc -l)` for the absence group: under
# `set -o pipefail` a grep that matches nothing fails the pipeline, and matching
# nothing is the success case here.
count() { grep -rcF -- "$1" "$SRC" | awk -F: '{t += $2} END {print t+0}'; }

# The six inferring reads, and the seventh csv read that is not one of them.
n=$(count '.option("inferSchema", "true")')
m=$(count '.option("preferDate", "false")')
[ "$n" = 6 ] || { echo "solve: $n inferring read(s), expected 6" >&2; exit 1; }
[ "$m" = 6 ] || { echo "solve: $m stated read(s), expected 6" >&2; exit 1; }
n=$(count '.read()')
[ "$n" = 17 ] || { echo "solve: $n read site(s), expected 17" >&2; exit 1; }
grep -qF 'spark.read().format("csv").load(inputFile)' \
    "$SRC/applications/DatasetFileConverter.java" \
    || { echo "solve: the non-inferring csv read is gone" >&2; exit 1; }

# Two of the three shapes m3's anti-vacuity rules exist to refuse. Neither is
# anywhere in this tree and neither is introduced here; asserted so that a future
# edit to this script cannot quietly make the reads unreadable to the gate. The
# third -- a read whose format is not a literal at the site -- is left to
# `m3-read-stated-at-the-site`, because the only text a grep could look for is
# `.format(`, and this tree has 36 of those and 34 are `String.format`.
for bad in '\.schema(' '\.options(' ; do
    if grep -rqF -- "$bad" "$SRC"; then
        echo "solve: $bad occurs in src/main" >&2; exit 1
    fi
done

# The inherited prohibitions, still held at this step.
for bad in 'SparkConf' 'new SparkContext\(' 'storeAssignmentPolicy' \
           'Encoders\.kryo' 'javaSerialization' 'setLocalProperty' \
           'spark\.sql\.legacy\.' '\.describe\(' ; do
    if grep -rqE -- "$bad" "$SRC"; then
        echo "solve: $bad occurs in src/main" >&2; exit 1
    fi
done
if grep -qE 'spark\.sql\.legacy\.|storeAssignmentPolicy|skipTests|maven\.test\.skip' "$POM"; then
    echo "solve: a prohibited setting occurs in the pom" >&2; exit 1
fi

# Steps 2, 3 and 4 are preconditions here, not this step's work. A step-5
# solution that arrived on a tree where any of them had been undone would be
# scored against held rules it cannot see; asserted so the failure reads as "the
# tree handed to me was wrong" rather than as a row of unexplained reds.
c=$(grep -rlF 'Dataset<String> ids = spark.createDataset(' "$SRC" | wc -l)
[ "$c" = 14 ] || { echo "solve: $c step-2 counter(s) present, expected 14" >&2; exit 1; }
grep -qF 'agg(sum(atoms.col("value")))' "$SRC/mappers/demos/MapReduceExample.java" \
    || { echo "solve: step 2's MapReduceExample edit is missing" >&2; exit 1; }
c=$(count '.summary(STATS).show();')
[ "$c" = 12 ] || { echo "solve: $c step-3 site(s) present, expected 12" >&2; exit 1; }
c=$(count 'spark.sql.ansi.enabled')
[ "$c" = 78 ] || { echo "solve: $c step-4 dialect site(s), expected 78" >&2; exit 1; }
c=$(count 'new JavaSparkContext(')
a=$(count 'new JavaSparkContext(spark.sparkContext())')
b=$(count 'new JavaSparkContext(session.sparkContext())')
[ "$c" = 71 ] || { echo "solve: $c context(s), expected 71" >&2; exit 1; }
[ "$((a + b))" = 71 ] || { echo "solve: $a+$b derived, expected all 71" >&2; exit 1; }

echo "solve: step 5 complete"
