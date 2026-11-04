# Step 2 — take the terminal result of fifteen classes through Spark's SQL engine

## 1. Implement this milestone

`/app` is mmtf-spark building against Spark 3.0.3 for Scala 2.12 on JDK 8. Every
program in it is written against the RDD API, which is the one part of Spark the
query engine cannot see into: where a class ends in `rdd.count()`, Spark runs
exactly what the lineage says and Catalyst never sees the work. Move the
**terminal result** of fifteen named classes onto the SQL engine — the count, or
in one case the total, that each of them prints — and leave the pipelines that
feed them as the RDD pipelines they are.

"Through the SQL engine" is a run-time fact, not a spelling. Spark tags each job
it submits for a `Dataset`/`DataFrame` action with a `spark.sql.execution.id`
local property, which is what places that job under a SQL execution; a job
submitted by an RDD action carries no such tag and belongs to no execution. That
is what "through the SQL engine" means, and `toDF`, `as(Encoders…)` and a temp view queried
with `spark.sql(…)` are all as good as the two constructors named below.

Three facts must hold of each of the fifteen, not one, and the other two are
about the built jar rather than the run:

* it no longer calls `count()` or `reduce(…)` on a `JavaRDD` or a
  `JavaPairRDD`;
* it hands an RDD to the engine — `SparkSession.createDataset` or
  `createDataFrame` (or the `SQLContext` equivalents), in the overload that
  takes an `RDD` or a `JavaRDD`. The overloads that take a `java.util.List` are
  not this fact: a `Dataset` built from a literal is not the data you read;
* it takes its result back off the `Dataset` that produced — `count`, `reduce`,
  `agg`, `head`, `first`, `collect`, `collectAsList` or `take` on
  `org.apache.spark.sql.Dataset`.

So a `Dataset` built beside a `count()` that stays has not moved the result; a
`count()` deleted in favour of a printed constant has not computed it; and a
`Dataset` built and then not read is neither. All three are faked steps rather
than partial ones. Every other RDD call in these classes is untouched by
this: the pipelines that feed the terminal, and the `foreach` three of them use
to print, are the RDD code they are.

When you are done:

* the seven reader-count demos — `FilterByResolution`, `FilterBySequenceRegex`,
  `MmtfBenchmark`, `ReadMmtfFull`, `ReadMmtfReduced`, `ReadLocalMmtf`,
  `ReadLocalMmtfHadoopFile` — compute the number they print inside a SQL
  execution. For the five whose count is their only terminal action, **no** job
  they submit runs outside one;
* the four mapper-count demos — `MapToBioAssembly`, `MapToBioAssembly2`,
  `MapToProteinDimers`, `CreateRepresentativeSet` — count what their
  `flatMapToPair` produced inside a SQL execution, built from the mapped pair
  RDD rather than from the reader's; for the three of them whose count is their
  only terminal action, again no job outside one;
* `MapReduceExample` totals its atoms with an engine-side aggregate over a
  typed column instead of an RDD `reduce`, prints the same Java `long`, and
  submits no job outside a SQL execution;
* the three web-filtered demos — `AuthorSearchDemo`, `KeywordSearch`,
  `KinaseSearch` — run **at least three** jobs inside a SQL execution. Two of
  those three are already there before you start, and finding out why is part of
  this requirement: measure what these classes do now rather than copying the
  shape of the twelve above;
* nothing in the tree, and nothing in the built jar, hands the engine an opaque
  value or forges the tag. No `Encoders.kryo`, no `Encoders.javaSerialization`,
  no `setLocalProperty` — anywhere, and however it is spelled: a reflective call
  is still that call, and none of the three may appear in the sources, in the
  jar's constant pool, or in the run. The prohibition on reflection is
  wider than these three names: nothing this build compiles may call into
  `java.lang.reflect` at all, or look a member or a type up by name through
  `java.lang.Class` — `forName`, `getMethod`, `getDeclaredMethod`,
  `getConstructor`, `getDeclaredConstructor` and their plural siblings.
  `getSimpleName`, which 124 of this tree's files already use, is not one of
  them, and nothing here reflects today. Each of the three names produces a real SQL
  execution and no migration, so any one of them is a faked step rather than a
  partial one;
* nothing in the tree, and nothing in the built jar, reaches for the record
  instead of the result. A listener installed on the `spark-submit` line records
  the jobs each run submits, and those jobs are what the four requirements above
  are about. A jar that names `spark.extraListeners`, that listener's own class,
  or any of the environment variables set for it on that line has replaced the
  record rather than done the step, and so has one that carries a class of its
  own in that listener's package, which is the strongest form of naming it. Both
  are faked steps exactly as the three names above are. None of those
  names appears anywhere in the tree you are handed, so the change itself never
  needs them;
* `edu.sdsc.mmtf.spark.io.MmtfReader` is still an RDD reader. Its `read*` and
  `download*` methods still return `JavaPairRDD<String,
  StructureDataInterface>`, and the built jar still calls one of them. Moving
  the reader into the engine instead of the demos' results is the same shortcut
  with a different owner and just as much a faked step.

Behaviour that must still hold, compared against the tree you received: every
program under `edu.sdsc.mmtf.spark`, and this project's public API called
directly from outside it, still exit with the same status and print the same
thing to stdout — the error and `--help` paths included — and all 47 of the
repository's own JUnit test classes, 142 tests, still pass. The fifteen classes
are inside that, not exempt from it: each prints one number today and must print
that same number when the engine computes it, so whatever you count has to have
as many rows as the thing being counted has elements. Neither behaviour nor the
migration may be traded for the other: both are required. The behaviour must hold
on a tree that made this move, though: a tree that changed nothing preserves its
output trivially and has not done this step. The behaviour is defined at one
fixed `spark-submit` line, because what the programs print depends on it; that
line, the command that runs all 47 test classes, and the thresholds your jar
must meet are in the handoff document named below.

## 2. Read these local references

The requirements on the build apply to the POMs alone: anything under
`.mvn/` (`jvm.config`, `maven.config`, `extensions.xml`) or in a `.mavenrc`
does not count toward meeting them.

**Normative:** `/app/WORKSPACE_HANDOFF.md` — the workspace, the build and run
commands, the behaviour contract, and each of the four migration requirements
stated in full, with every class named and with which of the two forms — no job
outside an execution, or at least one job inside one — applies to it. It is the
definition of this step; read it first.

**Additional:** `/app/README.md` and `/app/docs/` for what the programs do;
`/app/HANDOFF-NOTES.md` for what the work before you had to say about this tree;
upstream's "Spark SQL, DataFrames and Datasets Guide" for the `Dataset` and
`Encoder` APIs, `SparkSession.createDataset` and the typed aggregate functions;
and the Spark 3.0.3 monitoring documentation for how executions and jobs are
reported — `--conf spark.eventLog.enabled=true --conf
spark.eventLog.dir=file:/tmp/ev` writes one `SparkListenerSQLExecutionStart`
per execution and a properties block per job, and the SQL tab of the Spark UI
lists executions and nothing else. Either is enough to check any claim above
yourself.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. The Java sources are yours to edit —
  that is the step — but the tree you leave behind is still the same project:
  all 230 `.java` sources are still there, the supplied documents and handoff
  files are still there, and nothing gains a prose answer or an alternate entry
  point. The jar `mvn -B clean package -DskipTests` produces is the artifact,
  and it must stay the single shaded uber jar matching `target/mmtf-spark-*.jar`.
- Everything the work before you was asked for must still hold: the build
  still resolves Spark 3.0.3 for Scala 2.12 and
  nothing else, `com.databricks:spark-xml` stays gone, the jar stays clear of
  `org.spark_project.`, `org.sparkproject.` and `Word2VecModelReader` while
  `ProteinSequenceEncoder` still loads a model through `Word2VecModel`'s public
  loader, plugin versions still come from this pom, nothing the build resolves
  can move, and two clean builds still agree byte for byte. None of it may be
  undone.
- Do not cheat by using online solutions or hints specific to this task.
- The repository's own suite under `/app/src/test` must keep passing all 47 test
  classes and 142 tests it ships with — including the nine classes this pom's
  surefire configuration excludes from a plain `mvn -B test` — and none of them
  may be deleted, `@Ignore`d, skipped or weakened. Warnings are to be answered
  rather than silenced: no `@SuppressWarnings` naming `deprecation`, `removal`,
  `unchecked`, `serial` or `all` may be added beyond the tree's ten existing
  suppressions, which are a ceiling.
- Leave the tree buildable, and write what you did and what you worked around
  to `/app/HANDOFF-NOTES.md`, which is for whoever opens this tree next.
