# Step 5 — Spark 3.4.4 on Scala 2.13 and JDK 17, and hold the types the readers infer

## 1. Implement this milestone

`/app` is mmtf-spark building against Spark 3.3.4 for Scala 2.12 on JDK 8, with
each program holding a `SparkSession` of its own and running in Spark's ANSI
dialect. Move the build one release forward, to **Spark 3.4.4**, and move the
two axes that release lets you move one at a time: the Scala binary version to
**2.13** and the JDK this project compiles for to **17**. Then do the work that
move is here to make room for: hold the column types this tree's own readers
produce steady across it.

The three axes are moved together because this release is where each of them
still has a working other side. 3.4.4 is among the last Spark releases to
publish a Scala 2.12 build and a Scala 2.13 build of every artifact, and it
runs on JDK 8 as well as on JDK 17, so the suffix can move while the JDK this
tree compiles for is unchanged, and the Java level can move while the engine is
one this tree has already run on. 2.13 is the side that carries forward, and
the toolchain it expects is not the one this build targets, so the two are one
decision rather than two that can be taken apart.

Two of the three cost this repository nothing, and knowing that up front tells
you where the work is not. There is no Scala source in this tree; its whole
contact with the Scala library is `Tuple2` in 45 files and `Tuple4` in one,
and those are the same classes in both builds. Nothing in it stops compiling on
JDK 17 either. What JDK 17 does cost is one line of build configuration, and it
is not in the Java: Spark's storage layer reaches reflectively into a package
the JDK keeps to itself in order to release mapped memory, JDK 17 no longer
exports that package to code outside the platform, and the JVM your tests fork
dies inside that reflection with an `IllegalAccessError` — which names the
module and the package it was denied — before a single test body runs, unless
that package is opened to it. `spark-submit` opens what the programs need;
nothing opens anything for the tests.

The work is in what this tree's readers ask the engine to guess. Most of its
csv reads hand the engine a file and `inferSchema`, and Spark 3.4 changes what
inference does with a date-shaped column: inference now tries a date type
before a timestamp type, so a `yyyy-MM-dd` column that 3.3.4 gave back as a
`TimestampType` comes back as a `DateType` here, by default and with nothing in
the code changed. One of those readers asks the RCSB report service for a
deposition date, so the type of a column in a `Dataset` this repository hands
back changes at this release without one line of this repository changing. A
runtime move is not licensed to change the types this project's own readers
produce. The repair is not a global switch — the legacy property that would be
one is forbidden outright, like every `spark.sql.legacy.*` property (below) — but an option written
where the read is made, pinning date inference off at that site, so that the
reader comes back with the type it came back with before the move. Spark 3.4
adds the option that decides this; it is in the csv data source option table in
Spark's own documentation, beside `inferSchema`.

When you are done:

* **the build resolves Spark 3.4.4 for Scala 2.13.** Every `org.apache.spark`
  artifact the build resolves is at 3.4.4 and no `org.apache.spark` artifact
  resolves at any other version; every Scala-suffixed coordinate the build
  resolves carries `_2.13` and none carries any other suffix. The two are
  separate requirements and not one stated twice: `spark-core_2.12:3.4.4` is a
  real artifact and the wrong one, so a build can land the release and miss the
  migration. The engine each job runs in must report `spark.version` 3.4.4 too,
  and that is a precondition on the run rather than one of these two:
  `spark-core` is `provided`, so the jar carries no Spark and the version a job
  comes up on is the installation's, not your pom's. The pom is the half you
  control, and it must resolve these itself. It is the fence and not the
  work: a requirement satisfied by editing a property and a suffix is not what
  this step is for;
* **the project is compiled for Java 17, and the JVM its tests fork can reach
  what Spark reaches for.** Three conditions again. The build asks for Java 17 —
  `source` and `target` (or `release`) at 17, where they read `1.8` today. The
  jar it produces carries it: every one of this repository's classes in
  `target/mmtf-spark-*.jar` at Java 17's class-file version and none at any
  other. Asking is not producing, which is why both are required —
  `maven-compiler-plugin` compiles at 1.8 when nothing tells it otherwise, so a
  tree that moves everything else and leaves the level alone ships Java 8 class
  files out of a JDK 17 build without saying so, which was measured on this
  tree before the step was written. And surefire's forked JVM is started able
  to reach the JDK-internal package Spark's storage layer uses, which is what
  makes the repository's own suite survive the JDK move; running the tests in
  Maven's own JVM instead leaves whatever you configured for that fork inert
  and takes the suite down with it;
* **every csv read under `src/main` that asks the engine to infer a schema pins
  date inference off where the read is made**, and the option that pins it is
  present in the built jar. None of those reads says anything about dates
  today. What is asked for is not that the read say *something* about dates: it
  is that the columns it hands back carry the types they carried before the
  move, so a read that pins date inference *on* states the new behaviour
  instead of holding the old one and has not answered this. Reads that ask the
  engine for nothing, and reads that declare a schema of their own, are not
  inferring and have no date question to answer — leave them as they are, and
  neither add inference to a read in order to have something to pin nor take it
  away from one to escape the question. Write the pin in the read's own option
  chain, with the option's key spelled exactly as Spark's documentation spells
  it and its value written as a literal — not through a constant, a variable or
  a map — so that the read says what it asks for where it is made. This is the milestone, because it is
  the half no edit to `pom.xml` can reach;
* **and the reads stay legible where they are made.** Every csv read site this
  tree has is still a csv read site — this step is not a refactor, so leave the
  read sites where they are; consolidating two into one helper fails this
  even though nothing is wrong with the tree — every read under `src/main`
  still names its format at the call site rather than computing it into a
  variable or leaving it to a bare `.load()`, and every csv or json read that
  declares no schema still passes its options one at a time rather than as a
  map. All three are true of the tree you are handed and none of them is extra
  work; they are stated because the requirement above is also a requirement
  about an absence — no inferring csv read left unpinned — and deleting the
  inferring reads, or hiding what they ask for, would satisfy an absence
  without doing anything. That absence is not the whole of the requirement, and
  taking the question away from a read instead of answering it is not a
  shortcut to the same result: every csv read this tree asks the engine to
  infer has to still ask, and has to pin. One that stops asking — deleted,
  hidden behind a computed format, or handed a schema of its own — fails this
  requirement, and leaves the tree without the move that the behaviour contract
  is asked of;
* **the suite is not stopped from running.** This is the first step at which
  the repository's own tests can be lost by not doing the work rather than by
  breaking something, and the cheap way to make a red suite stop being red is
  to stop running it. Surefire is not configured to skip tests, and neither is
  the property that does the same thing without naming the plugin. Skipping the
  suite is a faked step, not a partial one;
* **and nothing in the tree, in the built jar or in any session reaches for a
  legacy switch, loosens store assignment, switches adaptive execution off,
  sets a JVM system property, or reaches for the listener that observes the
  runs.** Every prohibition from the steps behind this one carries forward
  unchanged: no `spark.sql.legacy.*` property however spelled and
  wherever set — those switches exist so that an application that cannot be
  changed can ask a new engine to keep answering like an old one, and this
  application is being changed; `spark.sql.storeAssignmentPolicy` left at
  `ANSI`; adaptive execution left on and left able to act; no `Encoders.kryo`,
  no `Encoders.javaSerialization`, no `setLocalProperty`, no
  `System.setProperty`; and nothing of your own installed on, standing in for,
  named by or written into whatever observes a run from inside its driver — no
  jar naming `spark.extraListeners`, the listener class installed through it or
  the environment variables set for it on the `spark-submit` line, and no class
  of your own in that listener's package. Nor
  may a forbidden name be reached through reflection: nothing this build
  compiles may call into `java.lang.reflect`, or look a member or a type up by
  name through `java.lang.Class`. `getSimpleName`, which many of this tree's
  files already use, is not one of those lookups, and nothing here reflects
  today.

Skipping the suite, the store-assignment loosening, the system property, the
legacy switches, the adaptive switch, the opaque encoders, the hand-set
execution id, the reach for the listener and the reflective lookup are each a
faked step rather than a partial one, and each is forbidden outright. None of
those names appears anywhere in the sources you are
handed, so the change itself never needs them.

Behaviour that must still hold: every program under `edu.sdsc.mmtf.spark`, and
this project's public API called directly from outside it, still exit with the
same status and print the same thing to stdout — the error and `--help` paths
included — and the repository's own JUnit suite still passes, all **46** test
classes and **140** tests of it. A suite that does
not run at all is a suite that did not pass, however it came not to run.
**"The same" means what these programs print on Spark 3.4.4 for Scala 2.13, on
JDK 17, with the ANSI dialect still on**, and on this tree, with this step's
own work done, that is byte-for-byte what they print today: the three version
fences move nothing here by themselves, measured over every program and every
public API call the contract covers before this step was written, and pinning
the inferring reads is what holds the one thing that would otherwise move.
Some lines move across a version fence without the program having changed its
answer — §2 of the handoff says which kinds of difference the behaviour
contract sets aside, and reordering and re-jitter are among them.

Neither behaviour nor the migration may be traded for the other; both are asked
for on their own. The behaviour is asked of a tree that made this move, though,
and at this step that matters more than at any before it, because the engine and
the JVM the programs run on are supplied by the runtime rather than by your
build, so a tree that changed nothing still prints nearly all of the behaviour
contract. Printing it therefore does not mean the move was made: the move is
a fact about the pom the build resolves, the class files in the jar the build
produced, and the read sites in the sources themselves — the build on 3.4.4 for
2.13, the jar carrying this repository's classes at Java 17's
class-file version, and every inferring csv read pinned with its sites intact
and the pin present in the jar. Until all of that holds, matching output is not
preserved behaviour. Each of those three requirements must hold in full: a tree
that moves the rung completely and pins all but one of the inferring reads has
met neither that requirement nor the behaviour contract. The behaviour is
defined at one fixed `spark-submit` line, because what the programs print
depends on it; that line, the command that runs the test suite, and the
thresholds your jar is held to are in the handoff document named below.

## 2. Read these local references

The requirements on the build apply to the POMs alone: anything under
`.mvn/` (`jvm.config`, `maven.config`, `extensions.xml`) or in a `.mavenrc`
does not count toward meeting them.

**Normative:** `/app/WORKSPACE_HANDOFF.md` — the workspace, the build and run
commands, the behaviour contract, all three migration requirements stated in
full with their thresholds, the counts the suite is held to, and the
runtime this step targets. It is the
definition of this step; read it first.

**Additional:** `/app/README.md` and `/app/docs/` for what the programs do;
`/app/HANDOFF-NOTES.md` for what the work before you had to say about this
tree; upstream's Spark 3.4.0 release notes and its CSV data source option
reference, which documents every option csv schema inference reads and what
each of them does; the
Spark documentation on which Scala and Java versions each release supports;
`maven-compiler-plugin`'s documentation on `source`, `target` and `release`,
and `maven-surefire-plugin`'s on `argLine` and forking; and JDK 17's own notes
on strong encapsulation of JDK-internal packages. The
`spark-3.4.4-bin-hadoop3-scala2.13` distribution — which is a different
download from `spark-3.4.4-bin-hadoop3` — is served by `archive.apache.org`
and is reachable from this box if you want that runtime here to check any claim
above yourself; JDK 17 is already installed, at the path the handoff names.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. The Java sources and `pom.xml` are
  yours to edit — that is the step — but the tree you leave behind is still the
  same project: every `.java` source it ships with is still there, the supplied
  documents and handoff files are still there, and nothing gains a prose answer
  or an alternate entry point. The jar `mvn -B clean package -DskipTests`
  produces is the artifact, and it must stay the single shaded uber jar
  matching `target/mmtf-spark-*.jar`. Build it with the JDK the handoff names,
  not with whatever `java` resolves to.
- Everything the work before you was asked for must still hold: the terminal
  result of the fifteen classes the handoff lists
  by name still reaches the SQL engine, `MmtfReader` is still an RDD reader
  returning `JavaPairRDD<String, StructureDataInterface>`, no call to
  `Dataset.describe(...)` remains and the statistic set that replaced it is
  still named in your code and still in the jar as text, no `SparkConf` is
  constructed under `src/main` or referenced from the jar and every
  `JavaSparkContext` is still derived from a `SparkContext`, every program
  still builds a session of its own inside its `main`, as one unbroken
  `SparkSession.builder()` … `getOrCreate()` chain, and still asks for `spark.sql.ansi.enabled`
  and still gets it, `com.databricks:spark-xml` stays gone, the jar stays clear
  of `org.spark_project.`, `org.sparkproject.` and `Word2VecModelReader` while
  `ProteinSequenceEncoder` still loads a model through `Word2VecModel`'s public
  loader, plugin versions still come from this pom, nothing else the build
  resolves can move, and two clean builds still agree byte for byte. None of it
  may be undone. The two things that do move are the ones
  this step exists for: the previous steps' Spark 3.3.4 fence is replaced by
  this step's 3.4.4 fence and their Scala `_2.12` fence by this step's
  `_2.13`, and moving off either is not undoing it.
- Do not cheat by using online solutions or hints specific to this task.
- The repository's own suite under `/app/src/test` must pass as it ships, not
  as edited, and in full — including the classes this pom's surefire
  configuration excludes from a plain `mvn -B test` — and none of its tests may
  be deleted, `@Ignore`d, skipped or weakened; a suite the build was configured
  not to run has not passed. One class,
  `webservices.CustomReportServiceTest`, fails here and is meant to: both of its
  tests compare a schema against a rendering they hardcode as a string literal,
  and `StructType.toString()` has printed that rendering with one space fewer
  since the release before this one. That is the literal being stale rather than
  anything in this tree being wrong; it sits outside the 46 and 140, it is not
  the work of this step, and it is not yours to repair. Warnings are to be
  answered rather than silenced: no `@SuppressWarnings` naming `deprecation`,
  `removal`, `unchecked`, `serial` or `all` may be added beyond the suppressions
  the tree already carries, which are a ceiling.
- Leave the tree buildable, and write what you did and what you worked around
  to `/app/HANDOFF-NOTES.md`, which is for whoever opens this tree next.
