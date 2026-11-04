# Workspace handoff — mmtf-spark, step 2

This is the document `instruction.md` points at. It describes the workspace you
have been given and states, in Spark's own terms, everything this step
requires. Every requirement is written here or in `instruction.md`,
and everything written here as a requirement applies.

---

## 1. What is in this workspace

`/app` is the [mmtf-spark](https://github.com/sbl-sdsc/mmtf-spark) working tree
as the work before you left it: one Maven project, no modules, building against
**Spark 3.0.3 for Scala 2.12** on JDK 8.

| | |
|---|---|
| `pom.xml` | at the root; `edu.sdsc.mmtf:mmtf-spark:0.3.0-SNAPSHOT`, resolving Spark 3.0.3 |
| `src/main/java` | 180 sources — the library, the demos, and the CLI applications |
| `src/test/java` | 50 sources — the repository's own JUnit suite |
| `docs/`, `README.md` | upstream documentation |
| `HANDOFF-NOTES.md` | if it is there, what the work before you had to say about this tree |

The toolchain on this box:

| | |
|---|---|
| `JAVA_HOME=/opt/jdk/8` | JDK 8; 11, 17 and 21 are also under `/opt/jdk/` |
| `/opt/maven/bin/mvn` | Maven 3.9 |
| `SPARK_HOME=/opt/spark` | the Spark **2.4.8 for Scala 2.11** distribution this box was installed with — older than the line this tree now builds against, and it will not run this tree's jar (§4) |
| `MMTF_FULL=/data/full`, `MMTF_REDUCED=/data/reduced` | the structure corpus, Hadoop sequence files, read-only |

**Build.** The repository's own instruction, and the one the project must build with:

```
cd /app && mvn -B clean package -DskipTests
```

It must produce exactly one runnable jar matching `target/mmtf-spark-*.jar`
once `original-*`, `*-sources.jar` and `*-javadoc.jar` are set aside: the
shaded uber jar. Every program runs from that jar and nothing else, so a
build that stops producing it, or that starts producing two, meets none of §2's
behaviour requirements no matter what the sources say.

**Run one program.** This is the line §2's behaviour is defined at, verbatim, one job at a
time, from a per-job working directory, with a 900-second ceiling on each:

```
MMTF_FULL=/data/full MMTF_REDUCED=/data/reduced \
spark-submit --class edu.sdsc.mmtf.spark.io.demos.ReadMmtfReduced \
    --master 'local[1]' --driver-memory 3g \
    --conf spark.ui.enabled=false \
    --conf spark.driver.host=127.0.0.1 \
    --conf spark.driver.bindAddress=127.0.0.1 \
    --driver-java-options -XX:ActiveProcessorCount=1 \
    target/mmtf-spark-0.3.0-SNAPSHOT.jar
```

`-XX:ActiveProcessorCount=1` is not decoration and it is the reason the rest of
the line is written out. 58 of the classes here call `setMaster("local[*]")` in
source, which beats `--master`, and `local[*]` is `availableProcessors()`; at
any width above one the executor threads interleave on the same stdout and the
same program prints two different answers on two runs. What §2 asks of the
programs is that each of them, run at exactly this line, still prints what it
prints on the tree as you received it; the line is fixed because the output
depends on it. Run it any other way while you work if you like — how you
develop is up to you — but a difference you see at `local[*]` and not at
this line is the width, not you.

The behaviour is also defined on a filesystem that serves every directory's entries in byte
order of their names, for the same reason. Hadoop's local listing does not sort,
so the order a corpus directory's `part-*` files are listed in is the order they
become partitions — and with it which rows a seeded sample draws and the order a
partition-ordered print comes out in. The filesystem on this box keeps an order
of its own, so a value you see move with the listing order, on either tree, is
the filesystem, not you.

Two things go with that line, neither of which
changes what a program computes. The corpus is read from `MMTF_FULL` and `MMTF_REDUCED` and
may be mounted somewhere other than `/data`, so a class that reaches for the
literal path instead of the variable is a class that stops working. And the
driver JVM may be started with an observer attached — a class path entry of its
own and a listener registered on the session — to record which jobs and
executions the run produced, because that is the fact §3 is about. Both are
conditions of §2 rather than flags to reproduce: a program that depends on the
absence of either does not meet §2.

**Network.** Public. Several demos call web services (RCSB, EBI/SIFTS,
UniProt, DrugBank, SWISS-MODEL, JPred, MyVariant, PDB-REDO, …); on this box
those hosts resolve to an in-container mirror that serves fixed responses over
https and ftp, so they answer without reaching the real host and give the same answer every time. It
starts itself from a login shell; you do not have to configure anything.
`archive.apache.org`, `repo1.maven.org` and the rest of the real internet are
not mirrored and behave normally.

**The repository's own tests.** All 47 classes, which is more than plain `mvn -B
test` runs: this pom's surefire configuration excludes nine long-running test
classes by default — all nine of them are in the tree — and §2 requires those
nine too. The command that runs the full set, and the one it must pass under:

```
cd /app && mvn -B test -Dtest='**/*Test.java' -DfailIfNoTests=false \
    -Dsurefire.timeout=900 -Dspark.driver.host=127.0.0.1
```

47 test classes, 142 tests, 0 failures and 0 errors on the tree you received,
and they pass under the conditions this tree's own tests already meet: no single
test needs more than 900 seconds to finish, and every one of them passes with
the Spark driver bound to `127.0.0.1`. A test that only comes back green on a
longer leash, or only with the driver on some other address, is a test that did
not pass.

Count the files and you will find 50 `*Test*.java` sources under `src/test`, not
47; three of them contribute nothing to that command and always have.
`SecondaryStructureExtractorTest2` does not end in `Test.java`, so the selector
above never asks for it. `StructureToCathDomainsTest` and `WildTypeTest` are
asked for, but every `@Test` method in both arrives commented out, so surefire
finds nothing to run in either and writes no report for either. All three are
how upstream left the tree and none of them is something to fix here: 47 classes
and 142 tests is what a clean run prints, and 50 or 49 would mean you changed
something §2 did not ask you to.

---

## 2. What must still be true when you are done — behaviour

This step changes which engine runs the work, not the science. Both of these
take as their reference the tree exactly as you received it:

1. **Every program still answers the same, and so does every library class it
   calls.** Two kinds of job are covered, under the same rule. The first is
   each `main` under `edu.sdsc.mmtf.spark` — the demos, the `applications`
   CLIs, and the few library classes with entry points — each with a fixed
   set of arguments. The second is this project's own **public API**, called
   directly: when the `alignments`, `datasets`, `filters`, `interactions`, `io`,
   `mappers`, `ml`, `utils` and `webfilters` classes are constructed and their
   public methods invoked from outside the project, what they return — row
   counts, schemas, column values, the exception a bad argument raises — must
   be what that same call returns on the tree as you received it. So a public
   signature, a return type, a schema or a thrown exception that changes is a
   behaviour change even when no `main` in the tree would have noticed. In both
   cases the process must exit with the same status — except where the status
   is itself decided by which rows a sampled subset happened to draw, which no
   version of Spark promises to reproduce either — and write the same thing to
   standard output. This includes the unhappy paths: a missing argument, an
   unreadable path, an unknown option or `--help` must still fail (or succeed)
   the same way and print the same message. Only standard output counts,
   and only after run-to-run noise has been normalised away: framework log
   lines, JVM and class-loader warnings, stack traces and blank lines are
   removed, and absolute paths, wall-clock durations and the date stamps this
   tree bakes into generated file names are rewritten. A handful of jobs print
   a value no version of Spark promises to reproduce — an unseeded sample, a
   `Word2Vec` training race, a `groupBy().count()` whose rows tie, a count
   taken over a sample that *is* seeded but whose draw no Spark version
   promises to keep from one version to the next, and rows whose order the
   query itself does not fix, as when a bare limit decides how much input was
   touched or a sort leaves a tie unbroken — and those lines are set aside too.
   What is left is what the program itself decided: row counts, identifiers,
   schemas, computed values, the order a program does fix, and the text of the
   messages it prints. All of that must match.

2. **The repository's own test suite still passes.** All 142 of them. A test
   you delete, rename, `@Ignore` or weaken is a test that did not pass: all
   142 the tree ships with must pass.

Both hold on JDK 8 and on Spark 3.0.3 — this step moves neither.

The fifteen classes §3 names are inside §2, not beside it. Each of them prints
one number, and that number is one of the things §2 covers: whatever you build
to take the count through the engine has to have exactly as many rows as the
thing being counted has elements today, and an aggregate has to come back the
same Java `long`. A tree that moves the work and changes the answer has failed
both halves of this document, not traded one for the other.

And both are questions about a tree that **made this step's move**. A tree that
changed nothing reproduces most of §2 — not because it preserved
anything but because nothing happened to it — and that is not what §2 is
asking: until at least one of the fifteen classes below takes its terminal
result through the SQL engine, §2 is a question about work that has not started,
and it is not met. One of the fifteen is enough to satisfy that sentence;
§3 sets out what the fifteen must do on their own terms.

---

## 3. What this step asks for — the migration

mmtf-spark is written against Spark's RDD API, and the RDD API is the one part
of Spark the query engine cannot see into. Where a class ends in
`rdd.count()`, Spark runs exactly what the lineage says, in the order the
lineage says it, with no plan and no optimization; the same count taken over a
typed `Dataset` goes through Catalyst instead. This step makes that move for
the **terminal result of fifteen named classes**, and only for the terminal
result: the pipelines that feed them stay RDD pipelines.

**What "through the SQL engine" means here, precisely.** When Spark runs a job
on behalf of a `Dataset`/`DataFrame` action, it tags that job with a
`spark.sql.execution.id` local property, and the tag is what places the job
under a SQL execution — one execution per action, with the jobs it needed
inside it. A job submitted by an RDD action — `count()`, `reduce()`,
`collect()`, `foreach()` on a `JavaRDD` or `JavaPairRDD` — carries no such tag
and belongs to no execution. That run-time fact is the first of the
requirements below, and what you do with the `Dataset` between building it and
reading a result off it is your business: `toDF`, `as(Encoders…)`, a temp view
queried with `spark.sql(…)`, any chain of transformations. You can watch the
fact
directly — `--conf spark.eventLog.enabled=true --conf
spark.eventLog.dir=file:/tmp/ev` writes one `SparkListenerSQLExecutionStart`
per execution and a `Properties` block per job — or in the SQL tab of the Spark
UI, which lists executions and nothing else.

One consequence is worth stating before the list, because it decides how much
work this is: a terminal `Dataset` action pulls the whole RDD lineage feeding
it *into* that execution rather than running it in a job beside one. You do not
have to move a pipeline to move its result.

**And three more facts, about the jar rather than the run.** The run-time fact
above says an execution happened. It does not, on its own, say that the number
the class prints came out of it — a `Dataset` built beside a `count()` that
stays produces the tag and computes nothing new; a `count()` deleted in favour
of a printed constant produces the right output and no execution at all; and a
`Dataset` built and then never read is neither of those and still not the move.
So four facts must hold of each of the fifteen, the job inside the execution and
three about its own class file. When you are done, each of the fifteen:

* calls `count()` or `reduce(…)` on no `JavaRDD` and no `JavaPairRDD` anywhere
  in its own class file;
* hands an RDD to the engine — `SparkSession.createDataset` or
  `SparkSession.createDataFrame`, or the `SQLContext` spelling of either, in
  the overload that takes an `RDD` or a `JavaRDD`. The overloads that take a
  `java.util.List` are not this: a `Dataset` built from a literal is not the
  data the class read;
* reads its result back off the `Dataset` that produced — `count`, `reduce`,
  `agg`, `head`, `first`, `collect`, `collectAsList` or `take` on
  `org.apache.spark.sql.Dataset`.

Nothing else about their RDD code is touched by this — the pipelines that feed
the terminal stay as they are, and so do the three `foreach` prints named
below, which are not terminal results.

**m1 — the reader's own count, through the engine.** Seven classes whose body
is: read MMTF, sometimes filter it, print a count.

* `edu.sdsc.mmtf.spark.filters.demos.FilterByResolution`
* `edu.sdsc.mmtf.spark.filters.demos.FilterBySequenceRegex`
* `edu.sdsc.mmtf.spark.io.demos.MmtfBenchmark`
* `edu.sdsc.mmtf.spark.io.demos.ReadMmtfFull`
* `edu.sdsc.mmtf.spark.io.demos.ReadMmtfReduced`
* `edu.sdsc.mmtf.spark.io.demos.ReadLocalMmtf`
* `edu.sdsc.mmtf.spark.io.demos.ReadLocalMmtfHadoopFile`

For the first five, the count is the only terminal action the class has, and
when you are done **no job they submit runs outside a SQL execution** — not
"one of them is inside one": all of them are, which is what the consequence
above gives you for free once the count is the only action left. `ReadLocalMmtf`
and `ReadLocalMmtfHadoopFile` also print every structure with a `foreach`,
which is an RDD action this step does not ask you to move, so for those two the
requirement is that **at least one** job runs inside a SQL execution and the
`foreach` may stay exactly where it is.

**m2 — a mapper's output count, through the engine.** The same move one stage
further down the pipeline: what these four count is what a `flatMapToPair(…)`
produced, so what you count has to be built from the mapped pair RDD and not
from the reader's.

* `edu.sdsc.mmtf.spark.mappers.demos.MapToBioAssembly`
* `edu.sdsc.mmtf.spark.mappers.demos.MapToBioAssembly2`
* `edu.sdsc.mmtf.spark.mappers.demos.MapToProteinDimers`
* `edu.sdsc.mmtf.spark.webfilters.demos.CreateRepresentativeSet`

The first three have the count as their only terminal action and get m1's
stricter form: no job outside a SQL execution. `MapToBioAssembly2` exits 1 on
the tree you received and is held to the same requirement regardless — it fails inside
the work that computes its count, after that work has started, so the execution
is there all the same. `CreateRepresentativeSet` coalesces and writes a sequence
file after its count, and that write is an RDD job this step leaves alone, so
it needs **at least one** job inside a SQL execution.

**m3 — the aggregate, which is not a count.**
`edu.sdsc.mmtf.spark.mappers.demos.MapReduceExample` is the one class here
whose terminal result is not a count: it is
`map(t -> t._2.getNumAtoms()).reduce((a, b) -> a + b)`. The total has to be
computed by the engine, over a typed column, and the number printed has to be
the same total as today — the arithmetic is over 32-bit atom counts and the
printed value is a Java `long`, so an aggregate that widens or narrows
differently from the `reduce` it replaces is a behaviour change under §2. No
job of this class may run outside a SQL execution.

**m4 — the three web-filtered counts.** Read what these three do before you
change anything.

* `edu.sdsc.mmtf.spark.webfilters.demos.AuthorSearchDemo`
* `edu.sdsc.mmtf.spark.webfilters.demos.KeywordSearch`
* `edu.sdsc.mmtf.spark.webfilters.demos.KinaseSearch`

Each of them constructs a `PdbjMineSearch`, and `PdbjMineSearch` queries a web
service and reads the answer through the SQL engine. So each of these three
*already* runs SQL executions on the tree you received, and "at least one job
inside an execution" is already true of all three before you touch them — it
is not what this asks. What this asks is one more than they already have: when
you are done each of the three runs **at least three** jobs inside a SQL
execution, the third being the demo's own count. `PdbjMineSearch` is a library
class and this step does not ask you to change it; measure what the three do
now, and the requirement will be obvious and so will whether you have met it.

### Not through a black box

Three shortcuts produce a real SQL execution, or the appearance of one, without
producing a migration. All three are ruled out
entirely: a tree that does any of them has faked this step rather than done
part of it.

None of these prohibitions is about a spelling, and a reflective call is still
the call it makes: the names below are ruled out in the sources, in the built
jar's constant pool, and in the run. Not one of them appears anywhere in the
230 sources you were handed, so holding to this rules out nothing you would
otherwise have written.

The rule goes one step past the names, so it
is said plainly rather than left to be inferred: no class this build compiles
may call into `java.lang.reflect` at all, or look a member or a type up by name
through `java.lang.Class` — `forName`, `getMethod`, `getDeclaredMethod`,
`getConstructor`, `getDeclaredConstructor` and their plural siblings. That is
how a prohibited call is made without the call appearing in the source or in
the constant pool, so it is ruled out entirely rather than case by case.
The one `java.lang.Class` member this repository does use is `getSimpleName`,
in 124 of its files, and it is not one of them; nothing under `src/main` or
`src/test` reflects today.

* **No opaque encoder.** Nothing in the tree, and nothing in the built jar, may
  use `Encoders.kryo` or `Encoders.javaSerialization`. Wrapping
  `StructureDataInterface` in a Kryo encoder does give you a `Dataset`, a
  terminal action and a real execution id; what it does not give you is a plan,
  because the engine sees one black-box column and can reason about nothing
  inside it. The values you hand the engine here have to be values it can see:
  a structure id, a chain id, a number of atoms. This is also why the
  requirement is at the terminal result and not at the whole pipeline —
  `JavaPairRDD<String, StructureDataInterface>` has no honest encoder, and it
  is not asked to have one.
* **No hand-set execution id.** Nothing in the tree, and nothing in the built
  jar, may call `setLocalProperty`. `spark.sql.execution.id` is a writable
  local property: setting it by hand puts the tag described at the top of this
  section onto a job that never ran inside an execution, which is a claim about
  the work rather than a change to it.
* **No reaching for the listener.** Nothing in the tree, and nothing in the
  built jar, may name `spark.extraListeners`, the listener class installed
  through it, or any of the environment variables set for that
  listener, and neither may carry a class of its own in that listener's
  package — a class in it is the strongest form of naming it. The tag
  described at the top of this section is observed by a
  `SparkListener` attached on the `spark-submit` line, and the driver it runs
  in is your own process — so the observer itself is reachable from your code,
  even though what it reports is not. Reaching for it puts a claim about the
  work where the record of it should be, and is ruled out with the two
  above.

### The reader stays an RDD reader

`edu.sdsc.mmtf.spark.io.MmtfReader` is this tree's way into the corpus: 57 of
its 78 `main`s are written against it. When you are done, its `read*` and
`download*`
methods still return `JavaPairRDD<String, StructureDataInterface>`, and the
built jar still calls at least one of them. Answering this step by moving the
*reader* into the engine — a `Dataset`-returning `readSequenceFile`, with the
demos counting that — is the opaque-encoder shortcut with a different owner:
what the engine would get is still a column of `StructureDataInterface`, the
other 42 of those 57 would all have to change to keep compiling, and it is ruled
out the same way. `MmtfWriter` is left alone for the same reason, and the
`foreach` prints are left alone because they are not terminal results.

### And throughout

The tree still parses as Java, `pom.xml` still resolves to an effective POM and
a dependency list, and the build still compiles the project's own classes — all
**230** `.java` sources the tree ships with are still there, and the build
produces at least 150 `edu/sdsc/mmtf/` classes. (230, not "most of them": none
of the four requirements above is answered by deleting a source file, so none of
them is allowed to be.) The jar has to run, not merely link: a plain
`spark-submit` of one of the demos off it must start a Spark session and exit 0.
Warnings are answered rather than silenced: `@SuppressWarnings` must not name
`deprecation`, `removal`, `unchecked`, `serial` or `all` anywhere in the tree,
and the total number of `@SuppressWarnings` annotations must not rise above the
ten the tree already carries. A suppression is not a fix.

Everything the work before you was asked for is still asked for, as a
precondition here: the resolved graph is still
Scala 2.12 with every `org.apache.spark` artifact at 3.0.3, `com.databricks:
spark-xml` is still gone from the pom and from the jar, nothing in the jar
refers to `org.spark_project.` or `org.sparkproject.` or to
`Word2VecModelReader` while `ProteinSequenceEncoder` still loads a saved model
through `Word2VecModel`'s public loader, every plugin version still comes from
this pom, nothing the build resolves can still move, and two clean builds still
agree byte for byte. None of that may be undone, in
either direction.

---

## 4. The runtime this step targets

Your jar must run on:

| | |
|---|---|
| Spark | `spark-3.0.3-bin-hadoop3.2` |
| JDK | 8 |

Neither moves at this step. The `/opt/spark` you were installed with is the
Spark 2.4.8 build for Scala 2.11 and will not run this tree's jar; the 3.0.3
distribution is served by `archive.apache.org`, which is reachable from this
box, if you want that runtime here to try things against. It may already be
installed somewhere on this box from the work before you — check before you
download it. Installing it is optional;
it is mentioned because "it compiled" and "it ran inside an execution" are
different claims and only the second one is what this step asks for. It is a
`bin-hadoop3.2` distribution, so it brings its own Hadoop and needs nothing on
`SPARK_DIST_CLASSPATH`.

---

## 5. Left to you

So you do not spend the budget on it: this step sets no requirement on which API
you build your `Dataset` through, on which encoder you use as long as the engine
can see into it, on how many columns it has or what you name them, on whether you
keep the `SparkConf` and `JavaSparkContext` these classes construct or how you get
a `SparkSession` in a class that already has a context, on how many partitions
anything has, on whether classes other than the fifteen also move (the two
prohibitions above are tree-wide, but a sixteenth class is neither asked for nor
ruled out), on formatting, imports, comments, `README.md` or anything under
`docs/`; on `/app/HANDOFF-NOTES.md`, which is for whoever opens this tree next;
or on how long the build or the programs take.

---

## 6. Handoff

The tree you leave in `/app` is the tree whoever opens this workspace next
works in. Leave it buildable, and leave a short note in
`/app/HANDOFF-NOTES.md` — what you changed, what you had to work around, and
anything a reader coming to this tree cold would otherwise have to rediscover.
What goes in it is up to you. Do not edit this file.
