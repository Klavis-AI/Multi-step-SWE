#!/usr/bin/env bash
# Step 4 reference solution: the build moves from Spark 3.2.4 to 3.3.4, and the
# 63 programs that built their own SparkConf now take their entry point from a
# SparkSession they configure -- with the ANSI dialect switched on there.
#
# One property in `pom.xml` and one idiom repeated across 78 source files. The
# property is one line for the reason step 3 gave: this repository already
# parameterises all three Spark artifacts on `${spark.version}`. That is a fact
# about the tree and it is exactly why this step cannot be *only* the property.
#
# The runtime move, and what does not arrive with it. 3.3.4 is one release past
# 3.2.4 and, on this corpus, it is behaviourally silent: `rung-diff c04 c05b` is
# SAME=140 / ORDER=0 / VALUE=0 / RC=0 (NOTES 6.4.1). Nothing stops compiling,
# nothing crashes, nothing prints differently. So unlike step 3, this fence
# hands the solution no semantic change to absorb -- and unlike step 3, it hands
# it no default either. `spark.sql.ansi.enabled` is false at 3.3 and stays false
# through 3.5; the engine will not switch it on for anybody. Measured on the
# tree this step is handed: 0 of 83 live sessions set it, and 0 report it true.
#
# The source move, and why the entry point is the site. ANSI is the dialect
# Spark is migrating towards -- overflow and bad casts become errors instead of
# silent nulls -- and at 4.0 it is the default. Switching it on one rung early is
# how a codebase finds out which of its casts were wrong while it still has a
# choice about it. Turning it on is a per-session decision, and that is the
# problem this tree has: there is no shared session or context factory anywhere
# in it (NOTES 6.4.3). 63 mains each build `new SparkConf()`, hand it to
# `new JavaSparkContext(conf)`, and never create a session at all; 15 of those
# then ask `SparkSession.builder().getOrCreate()` for the session the context
# already made, which works only because the context came first. Setting one
# config on that arrangement means setting it 63 times on 63 objects that are
# not sessions.
#
# So the move is: each program builds one `SparkSession`, configured where it is
# built, and derives its `JavaSparkContext` from that session's `SparkContext`
# rather than from a conf of its own. The conf disappears; the context stays,
# because 63 of these programs genuinely need an RDD-level handle and this step
# is not about taking it away from them. What changes is where it comes from.
#
#   before:  SparkConf conf = new SparkConf().setMaster(M).setAppName(N);
#            JavaSparkContext sc = new JavaSparkContext(conf);
#            ... 15 files then: SparkSession spark = SparkSession.builder().getOrCreate();
#
#   after:   SparkSession spark = SparkSession.builder()
#                    .master(M)
#                    .appName(N)
#                    .config("spark.sql.ansi.enabled", "true")
#                    .getOrCreate();
#            JavaSparkContext sc = new JavaSparkContext(spark.sparkContext());
#
# `new JavaSparkContext(SparkContext)` and `new JavaSparkContext(SparkConf)` are
# the same three words in the source and two different constructors in the class
# file, and the gate reads the descriptor rather than the words. That is not a
# detail of the instrument: the derived form is the one that inherits whatever
# the session was configured with, and the conf form is the one that does not.
#
# The master and app name are carried across verbatim, including the four
# `local[1]`s among 62 `local[*]`s, the one file that sets no master at all
# (`MmtfBenchmark`, which takes it from the submit line), and the ten demos whose
# `setAppName` names `CustomReportDemo` rather than themselves. Those are the
# names the 140 recordings were made under. Repairing them here would be a
# different step's work and would move output this step is required to preserve.
#
# 15 further mains already built a session and never built a conf. They are not
# part of the consolidation -- there is nothing to consolidate -- but they are
# entry points, so they get the same config line and nothing else. The 15
# library classes that call `SparkSession.builder().getOrCreate()` to *reach* the
# active session are left alone on purpose: a library that configures the
# session its caller built is the bug this step is removing, not a smaller
# version of the fix.
#
# src/test is not touched. Its 42 SparkConf constructions are outside this
# step's requirement (the gate reads src/main), the suite is replaced by a
# pristine overlay before it is scored, and forcing ANSI on the tests would be
# changing the thing that measures rather than the thing measured.
#
# Every replacement asserts its own occurrence count before anything is written,
# and every file is read back off disk afterwards (trap 7). The tree-wide sweeps
# at the bottom count off the bytes on disk rather than off what this script
# believes it wrote.
#
# Why this is not "edit pom.xml and stop". The property alone reaches 3.3.4, so
# a one-line tree answers all of m1 -- and nothing else. It builds no session,
# sets no dialect, and the behaviour families are gated on the consolidation
# having happened, so it collects m1's 0.10 of 1.00 and stops there.
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

ANSI = '.config("spark.sql.ansi.enabled", "true")'
SS_IMPORT = 'import org.apache.spark.sql.SparkSession;'
CONF_IMPORT = 'import org.apache.spark.SparkConf;'


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


# ------------------------------------------------------------------ the rung
#
# The three spark artifacts all read ${spark.version}, so the fence the gate
# checks -- every org.apache.spark artifact resolving at 3.3.4 and none at
# anything else -- follows from this one property. Asserted rather than
# assumed: if a later edit ever pins one of them directly, the count below is
# still 1 and the resolved-version rule is what catches it.
src = read(pom)
n = src.count("<spark.version>3.2.4</spark.version>")
if n != 1:
    sys.exit("solve: pom.xml: the spark.version property matched %d time(s), "
             "expected 1" % n)
src = src.replace("<spark.version>3.2.4</spark.version>",
                  "<spark.version>3.3.4</spark.version>")
write_back(pom, "pom.xml", src, [
    ("the property reads 3.3.4",
     lambda s: "<spark.version>3.3.4</spark.version>" in s),
    ("no 3.2.4 left anywhere in the pom", lambda s: "3.2.4" not in s),
    ("all three spark artifacts still read the property",
     lambda s: s.count("<version>${spark.version}</version>") == 3),
])
print("solve: pom.xml, spark.version 3.2.4 -> 3.3.4")


# ------------------------------------------------------- the conf/context pair
#
# Driven off the shape rather than off a 63-row table of file names. Measured
# first: all 63 sites in src/main match this one pattern, the setter vocabulary
# across them is exactly setMaster and setAppName, the context variable is `sc`
# in all 63, and the conf variable is referenced nowhere outside the pair. A
# literal table would have had to encode the tree's five different indentation
# habits, and the next reader would have to decide whether they mattered.
PAIR = re.compile(
    r'^([ \t]*)SparkConf[ \t]+(\w+)[ \t]*=[ \t]*new SparkConf\(\)'
    r'((?:[ \t\r\n]*\.\w+\([^;]*?\))*)[ \t]*;[ \t]*\r?\n'
    r'([ \t]*)JavaSparkContext[ \t]+(\w+)[ \t]*='
    r'[ \t]*new JavaSparkContext\(\2\)[ \t]*;[ \t]*$', re.M)

# The bare builder step 2 left in 15 of these files, which asked for the session
# the JavaSparkContext had already created. Once the pair above builds a
# configured session of its own, this line re-declares the same variable in the
# same scope and the file stops compiling -- so it is deleted, and the deletion
# is counted rather than attempted. It is also the whole reason m2 is a
# consolidation and not an addition: these files had two entry points.
BARE = re.compile(
    r'^[ \t]*SparkSession[ \t]+spark[ \t]*=[ \t]*'
    r'SparkSession\.builder\(\)\.getOrCreate\(\);[ \t]*\r?\n', re.M)

SETTER = re.compile(r'\.(\w+)\(')
SPARK_IMPORT = re.compile(r'^import org\.apache\.spark\.[\w.]+;$', re.M)


def chain_parts(chain):
    """(master-arg, appName-arg) off the setter chain, or exit."""
    calls = {}
    for m in re.finditer(r'\.(setMaster|setAppName)\(', chain):
        depth, i = 1, m.end()
        while depth:
            if chain[i] == '(':
                depth += 1
            elif chain[i] == ')':
                depth -= 1
            i += 1
        calls[m.group(1)] = ' '.join(chain[m.end():i - 1].split())
    unknown = [c for c in SETTER.findall(chain)
               if c not in ('setMaster', 'setAppName', 'getSimpleName')]
    if unknown:
        sys.exit("solve: unexpected setter(s) on a SparkConf chain: %s"
                 % ' '.join(sorted(set(unknown))))
    if 'setAppName' not in calls:
        sys.exit("solve: a SparkConf chain names no application: %s" % chain)
    return calls.get('setMaster'), calls['setAppName']


def add_import(src):
    """Insert the SparkSession import in sorted position in the spark block."""
    if SS_IMPORT in src:
        return src, 0
    lines = SPARK_IMPORT.findall(src)
    if not lines:
        sys.exit("solve: no org.apache.spark import block to insert into")
    after = [l for l in lines if l < SS_IMPORT]
    anchor = after[-1] if after else lines[0]
    if after:
        return src.replace(anchor, anchor + "\n" + SS_IMPORT, 1), 1
    return src.replace(anchor, SS_IMPORT + "\n" + anchor, 1), 1


pairs = bares = imports_added = 0
conf_files = []

for dirpath, _, names in os.walk(root):
    for name in sorted(names):
        if not name.endswith(".java"):
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        src = read(path)
        if "new SparkConf(" not in src:
            continue
        hits = list(PAIR.finditer(src))
        if len(hits) != src.count("new SparkConf("):
            sys.exit("solve: %s: %d SparkConf construction(s) but %d match the "
                     "conf/context pair" % (rel, src.count("new SparkConf("),
                                            len(hits)))

        # Rebuilt back to front so the earlier spans keep their offsets.
        for m in reversed(hits):
            ind, ind2 = m.group(1), m.group(4)
            cont = ind + ("\t\t" if "\t" in ind else "        ")
            master, app = chain_parts(m.group(3))
            block = [ind + "SparkSession spark = SparkSession.builder()"]
            if master is not None:
                block.append(cont + ".master(" + master + ")")
            block.append(cont + ".appName(" + app + ")")
            block.append(cont + ANSI)
            block.append(cont + ".getOrCreate();")
            block.append(ind2 + "JavaSparkContext " + m.group(5)
                         + " = new JavaSparkContext(spark.sparkContext());")
            src = src[:m.start()] + "\n".join(block) + src[m.end():]
            pairs += 1

        src, dropped = BARE.subn("", src)
        bares += dropped
        if dropped > 1:
            sys.exit("solve: %s: %d bare builders, expected at most 1"
                     % (rel, dropped))

        if CONF_IMPORT + "\n" not in src:
            sys.exit("solve: %s: no SparkConf import to remove" % rel)
        src = src.replace(CONF_IMPORT + "\n", "", 1)
        src, added = add_import(src)
        imports_added += added

        write_back(path, rel, src, [
            ("no SparkConf constructed", lambda s: "new SparkConf(" not in s),
            ("no SparkConf named at all", lambda s: "SparkConf" not in s),
            ("the session is imported", lambda s: SS_IMPORT in s),
            ("exactly one session is built",
             lambda s: s.count("SparkSession.builder()") == 1),
            ("the dialect is set on it", lambda s: s.count(ANSI) == 1),
            ("the context derives from the session",
             lambda s: "new JavaSparkContext(spark.sparkContext())" in s),
            ("no context from a conf",
             lambda s: not re.search(r'new JavaSparkContext\(\s*conf\s*\)', s)),
            ("no raw SparkContext", lambda s: "new SparkContext(" not in s),
            ("no legacy hatch", lambda s: "spark.sql.legacy." not in s),
            ("no loosened store assignment",
             lambda s: "storeAssignmentPolicy" not in s),
            ("no opaque encoder", lambda s: "Encoders.kryo" not in s
                                            and "javaSerialization" not in s),
            ("no forged id", lambda s: "setLocalProperty" not in s),
            ("describe is still gone", lambda s: ".describe(" not in s),
        ])
        conf_files.append(rel)

print("solve: %d conf/context pair(s) consolidated across %d file(s); "
      "%d bare builder(s) removed, %d SparkSession import(s) added"
      % (pairs, len(conf_files), bares, imports_added))


# ------------------------------------------------- the mains that had no conf
#
# 15 entry points already built a session and never built a conf, so there is
# nothing here to consolidate -- only the dialect to set, in the one place each
# of them builds its session. Three shapes among them (`SparkSession.builder()`
# on one line, `SparkSession` and `.builder()` split across two, and one main
# that calls getOrCreate() for its side effect and discards the result), so the
# insertion anchors on `.getOrCreate()` and not on the assignment.
#
# The 15 library classes that call the same builder are deliberately not in this
# set. `main` is the discriminator, and it is the right one: a class with an
# entry point is a program and configures its own session; a class without one
# is reached by a program that already did. Four of them build more than one
# (`PdbToUniProt` six, `UniProt` two), which is itself the shape of a tree with
# no session factory -- and still not this step's to change.
GET = re.compile(r'([ \t\r\n]*)\.getOrCreate\(\)')
mains = 0

for dirpath, _, names in os.walk(root):
    for name in sorted(names):
        if not name.endswith(".java"):
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root)
        src = read(path)
        if rel in conf_files or "SparkConf" in src:
            continue
        if not re.search(r'public static void main\s*\(', src):
            continue
        if "SparkSession" not in src or ".builder()" not in src:
            continue
        hits = list(GET.finditer(src))
        if len(hits) != 1:
            sys.exit("solve: %s: %d getOrCreate() call(s), expected 1"
                     % (rel, len(hits)))
        m = hits[0]
        gap = m.group(1)
        if "\n" in gap:
            # Already wrapped: the config line takes the same break and indent.
            lead = gap
        else:
            # Chained on one line: break before the config rather than after it,
            # so the added line is indented like the rest of the wrap and
            # getOrCreate() stays where the file put it.
            bol = src.rfind("\n", 0, m.start()) + 1
            lead = "\n" + re.match(r'[ \t]*', src[bol:]).group(0)
        src = src[:m.start()] + lead + ANSI + gap + ".getOrCreate()" \
            + src[m.end():]
        write_back(path, rel, src, [
            ("the dialect is set", lambda s: s.count(ANSI) == 1),
            ("exactly one session is built",
             lambda s: len(re.findall(r'\.builder\(\)', s)) == 1),
            ("no SparkConf arrived", lambda s: "SparkConf" not in s),
            ("no legacy hatch", lambda s: "spark.sql.legacy." not in s),
            ("no loosened store assignment",
             lambda s: "storeAssignmentPolicy" not in s),
        ])
        mains += 1

print("solve: %d session-building main(s) given the dialect" % mains)

if pairs != 63:
    sys.exit("solve: %d pair(s) consolidated, expected 63" % pairs)
if len(conf_files) != 63:
    sys.exit("solve: %d file(s) had a conf, expected 63" % len(conf_files))
if bares != 15:
    sys.exit("solve: %d bare builder(s) removed, expected 15" % bares)
if imports_added != 47:
    sys.exit("solve: %d import(s) added, expected 47" % imports_added)
if mains != 15:
    sys.exit("solve: %d conf-less main(s) edited, expected 15" % mains)
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

for bad in 'SparkConf' 'new SparkContext\(' 'storeAssignmentPolicy' \
           'Encoders\.kryo' 'javaSerialization' 'setLocalProperty' \
           'spark\.sql\.legacy\.' '\.describe\(' ; do
    if grep -rqE -- "$bad" "$SRC"; then
        echo "solve: $bad occurs in src/main" >&2; exit 1
    fi
done
if grep -qE 'spark\.sql\.legacy\.|storeAssignmentPolicy' "$POM"; then
    echo "solve: a prohibited setting occurs in the pom" >&2; exit 1
fi

# 102 = 63 consolidated entry points + 15 mains that already had one + 24 calls
# in the 15 library classes that reach the active session and are not touched.
# 99 of the 102 are written on one line; the three that split `SparkSession`
# from `.builder()` are pre-existing and none of them is a file this step edits.
n=$(count 'SparkSession.builder()'); m=$(count '.builder()')
[ "$n" = 99 ] || { echo "solve: $n one-line builder(s), expected 99" >&2; exit 1; }
[ "$m" = 102 ] || { echo "solve: $m builder call(s), expected 102" >&2; exit 1; }

# 78 = one per entry point. Not one per session: the library classes share the
# session their caller configured, which is the point of consolidating it.
n=$(count 'spark.sql.ansi.enabled')
[ "$n" = 78 ] || { echo "solve: $n ANSI site(s), expected 78" >&2; exit 1; }

# All 71 contexts derive from a session, not 63: eight sites in the library and
# demo classes already did, which is where the idiom came from. The two spellings
# are the two names those files give the session variable.
n=$(count 'new JavaSparkContext('); a=$(count 'new JavaSparkContext(spark.sparkContext())')
b=$(count 'new JavaSparkContext(session.sparkContext())')
[ "$n" = 71 ] || { echo "solve: $n context(s), expected 71" >&2; exit 1; }
[ "$((a + b))" = 71 ] || { echo "solve: $a+$b derived, expected all 71" >&2; exit 1; }

# Steps 2 and 3 are preconditions here, not this step's work. A step-4 solution
# that arrived on a tree where either had been undone would be scored against
# held rules it cannot see; asserted so the failure reads as "the tree handed to
# me was wrong" rather than as a row of unexplained reds.
c=$(grep -rlF 'Dataset<String> ids = spark.createDataset(' "$SRC" | wc -l)
[ "$c" = 14 ] || { echo "solve: $c step-2 counter(s) present, expected 14" >&2; exit 1; }
grep -qF 'agg(sum(atoms.col("value")))' "$SRC/mappers/demos/MapReduceExample.java" \
    || { echo "solve: step 2's MapReduceExample edit is missing" >&2; exit 1; }
c=$(grep -rcF '.summary(STATS).show();' "$SRC" | awk -F: '{t += $2} END {print t+0}')
[ "$c" = 12 ] || { echo "solve: $c step-3 site(s) present, expected 12" >&2; exit 1; }

echo "solve: step 4 complete"
