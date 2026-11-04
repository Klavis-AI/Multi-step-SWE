#!/usr/bin/env bash
# Step 1 reference solution: move the build to Scala 2.12 and Spark 3.0.3, deal
# with the three things Spark 3.0 takes away, drop the dependency that has no
# 2.12 build, pin the plugins, and make the package phase reproducible.
#
# Three files of Java and one pom. The first two of the three below are not
# optional: at Spark 3.0 this tree does not compile until each is answered,
# which is the whole reason the Scala 2.12 fence is inside this step rather
# than being a step of its own (NOTES 5.13, 6.1.13). The third compiles
# either way and is found by running the repository's own suite.
#
#   org.spark_project.guava       Spark's shaded Guava through 2.4, renamed in
#                                 3.0 and not published under the old name.
#                                 StructureToBioassembly used it for one
#                                 double[] -> float[] conversion.
#   Word2VecModel.Word2VecModelReader
#                                 no longer accessible; Word2VecModel.load is
#                                 the public API that replaces it, in two
#                                 methods of ProteinSequenceEncoder.
#   CSV timestamp inference       2.4 fell back to a lenient parser when the
#                                 timestampFormat did not match, so a bare
#                                 date inferred as TimestampType; 3.0 does not,
#                                 and CustomReportServiceTest says so.
#
# The pom also pins commons-cli, which nothing in this step requires. It still
# arrives transitively at 3.0.3, by way of spark-core -> hadoop-client 2.7.4 ->
# hadoop-common, and a tree that declares nothing compiles and runs: measured,
# NOTES 6.1.26. The pin is kept rather than dropped because it moves no rule
# and no fixture, and because the rungs further up this line -- where
# hadoop-client-api shades commons-cli away -- do force it.
#
# The one dependency that cannot come along -- com.databricks:spark-xml_2.11,
# which has no 2.12 release at 0.4.x -- is not imported by any source in this
# tree, so removing it costs nothing at compile time and removes 153
# com/databricks classes from the uber jar.
#
# Each replacement asserts its own occurrence count before anything is written,
# and every file is read back off disk afterwards (trap 7). A migration script
# that silently matched nothing is the failure mode worth guarding here: it
# would leave a tree that still builds on 2.4, still passes the repository's
# own suite, and answers none of the six requirements.
set -euo pipefail

APP=${1:-/app}
if [ -f "$APP" ]; then APP=$(dirname "$APP"); fi     # tolerate a pom.xml path
POM=$APP/pom.xml
SRC=$APP/src/main/java/edu/sdsc/mmtf/spark
[ -f "$POM" ] || { echo "solve: no pom at $POM" >&2; exit 1; }
[ -d "$SRC" ] || { echo "solve: no sources at $SRC" >&2; exit 1; }

python3 - "$POM" <<'PYEOF'
import re, sys
import xml.etree.ElementTree as ET

path = sys.argv[1]
src = open(path, encoding="utf-8").read()
before = src

def sub(old, new, count, what):
    """Replace `old` exactly `count` times, or say which edit did not land."""
    global src
    seen = src.count(old)
    if seen != count:
        sys.exit("solve: %s matched %d time(s), expected %d" % (what, seen, count))
    src = src.replace(old, new)

# --- m1: Scala 2.12, and one Spark version across every Spark artifact -------
# 3.0.3 is the last 3.0.x. The Scala suffix and the Spark version move together
# because they have to: Spark 3 publishes no 2.11 build at all.
sub("<spark.version>2.4.0</spark.version>",
    "<spark.version>3.0.3</spark.version>\n"
    "\t\t<project.build.outputTimestamp>2019-01-01T00:00:00Z</project.build.outputTimestamp>",
    1, "spark.version")
for a in ("spark-core", "spark-sql", "spark-mllib"):
    sub("<artifactId>%s_2.11</artifactId>" % a,
        "<artifactId>%s_2.12</artifactId>" % a, 1, a)

# --- ungraded: pin the commons-cli the three CLI applications import --------
# 1.4 rather than the 1.2 hadoop-client 2.7.4 still supplies transitively.
# Either satisfies the three imports; see the header for why this is here at
# all when no rule reads it.
sub("""\t<dependencies>
\t\t<dependency>
\t\t\t<groupId>junit</groupId>""",
    """\t<dependencies>
\t\t<dependency>
\t\t\t<groupId>commons-cli</groupId>
\t\t\t<artifactId>commons-cli</artifactId>
\t\t\t<version>1.4</version>
\t\t</dependency>
\t\t<dependency>
\t\t\t<groupId>junit</groupId>""",
    1, "commons-cli dependency")

# --- m2: the artifact with no 2.12 build ------------------------------------
sub("""        <dependency>
            <groupId>com.databricks</groupId>
            <artifactId>spark-xml_2.11</artifactId>
            <version>0.4.1</version>
        </dependency>
""", "", 1, "spark-xml dependency")

# --- m5: no snapshot repository ----------------------------------------------
# Nothing in the graph resolves out of it -- it is a 2015 leftover with
# `releases` enabled as well, which is a build that can change under itself.
sub("""\t<repositories>
\t\t<repository>
\t\t\t<id>sonatype-snapshots</id>
\t\t\t<url>https://oss.sonatype.org/content/repositories/snapshots/</url>
\t\t\t<releases>
\t\t\t\t<enabled>true</enabled>
\t\t\t</releases>
\t\t\t<snapshots>
\t\t\t\t<enabled>true</enabled>
\t\t\t</snapshots>
\t\t</repository>
\t</repositories>
""", "", 1, "sonatype-snapshots repository")

# --- m4: the plugins Maven would otherwise choose for us ---------------------
# Seven of the eleven plugins in the effective model came from the super POM,
# which means their versions are a property of the Maven on the machine. These
# are the versions the super POM of Maven 3.9.9 happens to bind, written down.
PINS = "".join(
    "\t\t\t<plugin>\n"
    "\t\t\t\t<groupId>org.apache.maven.plugins</groupId>\n"
    "\t\t\t\t<artifactId>%s</artifactId>\n"
    "\t\t\t\t<version>%s</version>\n"
    "\t\t\t</plugin>\n" % (a, v)
    for a, v in (("maven-clean-plugin", "3.3.2"),
                 ("maven-resources-plugin", "3.3.1"),
                 ("maven-compiler-plugin", "3.11.0"),
                 ("maven-jar-plugin", "3.3.0"),
                 ("maven-install-plugin", "3.1.1"),
                 ("maven-deploy-plugin", "3.1.1"),
                 ("maven-site-plugin", "3.12.1")))
sub("\t\t\t<plugin>\n\t\t\t\t<artifactId>maven-surefire-plugin</artifactId>",
    PINS + "\t\t\t<plugin>\n\t\t\t\t<artifactId>maven-surefire-plugin</artifactId>",
    1, "plugin pins")

# --- m6: the same bytes twice -------------------------------------------------
# Shade 3.1.0 writes dependency-reduced-pom.xml into the source tree on every
# package, and its jar's entry order and timestamps move run to run. 3.5.1
# honours project.build.outputTimestamp; turning the reduced POM off is what
# stops the build leaving a file behind that the next build then reads.
sub("<artifactId>maven-shade-plugin</artifactId>\n\t\t\t\t<version>3.1.0</version>",
    "<artifactId>maven-shade-plugin</artifactId>\n\t\t\t\t<version>3.5.1</version>",
    1, "shade version")
sub("\t\t\t\t<configuration>\n\t\t\t\t\t<artifactSet>",
    "\t\t\t\t<configuration>\n"
    "\t\t\t\t\t<createDependencyReducedPom>false</createDependencyReducedPom>\n"
    "\t\t\t\t\t<artifactSet>",
    1, "createDependencyReducedPom")

if src == before:
    sys.exit("solve: nothing changed")
ET.fromstring(src)                      # well-formed before it is written
open(path, "w", encoding="utf-8").write(src)

# Read the file back and assert the properties off the bytes on disk, not off
# the string in memory (trap 7).
out = open(path, encoding="utf-8").read()
checks = [
    ("2.12 only",     not re.search(r"<artifactId>[a-z-]+_2\.11</artifactId>", out)),
    ("2.12 present",  out.count("_2.12</artifactId>") == 3),
    ("spark 3.0.3",   "<spark.version>3.0.3</spark.version>" in out),
    ("commons-cli",   "<artifactId>commons-cli</artifactId>" in out),
    ("no spark-xml",  "spark-xml" not in out),
    ("no snapshots",  "sonatype-snapshots" not in out),
    ("outputTimestamp", "project.build.outputTimestamp" in out),
    ("plugins pinned", out.count("<groupId>org.apache.maven.plugins</groupId>") >= 7),
    ("shade 3.5.1",   "<version>3.5.1</version>" in out),
    ("no reduced pom", "<createDependencyReducedPom>false</createDependencyReducedPom>" in out),
]
bad = [n for n, ok in checks if not ok]
if bad:
    sys.exit("solve: edits landed but the pom does not read right: " + ", ".join(bad))
print("solve: %s, %d -> %d bytes, %d check(s) hold"
      % (path, len(before), len(out), len(checks)))
PYEOF

python3 - "$SRC" <<'PYEOF'
import os, sys

root = sys.argv[1]
edited = []

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

# --- F_GUAVA -----------------------------------------------------------------
# Spark 2.4 shaded Guava into org.spark_project; 3.0 shades it into
# org.sparkproject. Retargeting the import there compiles -- the classes really
# are on the classpath -- and is the thing m3-no-spark3-shade exists to refuse:
# the call was one double[] -> float[] conversion, so it becomes one loop and
# the dependency disappears rather than moving from one Spark internal to the
# next.
rewrite("mappers/StructureToBioassembly.java", [
    ("import org.spark_project.guava.primitives.Doubles;\n"
     "import org.spark_project.guava.primitives.Floats;\n", "",
     "the two org.spark_project imports"),
    ("float[] floatMatrix = Floats.toArray(Doubles.asList(currMatrix));",
     "float[] floatMatrix = new float[currMatrix.length];\n"
     "\t\t\t\t\t\tfor (int fi = 0; fi < currMatrix.length; fi++) "
     "{ floatMatrix[fi] = (float) currMatrix[fi]; }",
     "the Floats.toArray conversion"),
], [
    ("no org.spark_project", lambda s: "org.spark_project" not in s),
    ("no Floats/Doubles",    lambda s: "Floats." not in s and "Doubles." not in s),
    ("the conversion is still there", lambda s: "float[] floatMatrix" in s),
])

# --- F_W2V -------------------------------------------------------------------
# Word2VecModel.Word2VecModelReader is not accessible in Spark 3. The public
# loader is Word2VecModel.load, which is what MLReadable has always meant here;
# the reader was the implementation behind it.
rewrite("ml/ProteinSequenceEncoder.java", [
    ("import org.apache.spark.ml.feature.Word2VecModel.Word2VecModelReader;\n", "",
     "the Word2VecModelReader import"),
    ("        Word2VecModelReader reader = new Word2VecModelReader();\n"
     "        model = reader.load(fileName);",
     "        model = Word2VecModel.load(fileName);",
     "the loader in overlappingNgramWord2VecEncode"),
    ("\t\t Word2VecModelReader reader = new Word2VecModelReader();\n"
     "\t     model = reader.load(fileName);",
     "\t\tmodel = Word2VecModel.load(fileName);",
     "the loader in shifted3GramWord2VecEncode"),
], [
    ("no Word2VecModelReader", lambda s: "Word2VecModelReader" not in s),
    ("two public loads",       lambda s: s.count("Word2VecModel.load(fileName)") == 2),
])

# --- F_CSVTS -----------------------------------------------------------------
# Spark 2.4's CSV schema inference fell back to DateTimeUtils.stringToTime when
# the configured timestampFormat did not parse, so a bare `1990-01-01` came
# back as TimestampType. Spark 3.0 dropped that fallback -- the CSV source now
# parses with java.time against the pattern and nothing else -- and the same
# column infers as StringType, which the repository's own
# CustomReportServiceTest asserts against. Saying the format out loud is the
# smallest honest answer: it is what the data always was, and it does not
# depend on which version's inference is doing the reading. The gate has no
# rule for this one on purpose (CHECKLIST-4.2 section 9): an explicit schema
# and spark.sql.legacy.timeParserPolicy are answers too, and only the tests can
# tell whether the answer given works.
rewrite("datasets/CustomReportService.java", [
    ('\t\tDataset<Row> dataset = spark.read().format("csv").option("header", "true").option("inferSchema", "true")\n'
     '\t\t\t\t.load(inputFileName);',
     '\t\tDataset<Row> dataset = spark.read().format("csv").option("header", "true").option("inferSchema", "true")\n'
     '\t\t\t\t.option("timestampFormat", "yyyy-MM-dd")\n'
     '\t\t\t\t.load(inputFileName);',
     "the csv reader in readCsv"),
], [
    ("the timestampFormat is set", lambda s: '.option("timestampFormat", "yyyy-MM-dd")' in s),
    ("inferSchema is still on",    lambda s: '.option("inferSchema", "true")' in s),
    ("one reader, not two",        lambda s: s.count('.load(inputFileName)') == 1),
])

for rel, n0, n1, k in edited:
    print("solve: %s, %d -> %d bytes, %d edit(s)" % (rel, n0, n1, k))
PYEOF

# The three commons-cli consumers are untouched: their imports were always
# correct and stay correct at 3.0.3. Asserted rather than assumed -- an edit
# that deleted one of them would be a source file lost to the migration, and
# would not otherwise show up here.
cli=$(grep -rl 'org\.apache\.commons\.cli' "$SRC" | wc -l)
[ "$cli" = 3 ] || { echo "solve: $cli file(s) import org.apache.commons.cli, expected 3" >&2; exit 1; }

# Shade 3.1.0 may have left one of these behind during authoring or during an
# earlier attempt of the agent's; with createDependencyReducedPom off nothing
# regenerates it, and leaving it would be a stray file in the source tree.
rm -f "$APP/dependency-reduced-pom.xml"

echo "solve: step 1 complete"
