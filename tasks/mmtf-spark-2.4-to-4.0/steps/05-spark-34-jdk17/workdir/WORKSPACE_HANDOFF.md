# Workspace handoff — mmtf-spark, step 5

This is the document `instruction.md` points at. It describes the workspace you
have been given and states, in Spark's, Maven's and the JDK's own terms,
everything this step requires. Every requirement is written here or in
`instruction.md`, and everything written here as a requirement applies.

---

## 1. What is in this workspace

`/app` is the [mmtf-spark](https://github.com/sbl-sdsc/mmtf-spark) working tree
as the work before you left it: one Maven project, no modules, building against
**Spark 3.3.4 for Scala 2.12** on JDK 8, with every program under `src/main`
opening a `SparkSession` of its own and asking that session for the ANSI SQL
dialect, and with no `SparkConf` left anywhere in the tree.

| | |
|---|---|
| `pom.xml` | at the root; `edu.sdsc.mmtf:mmtf-spark:0.3.0-SNAPSHOT`, resolving Spark 3.3.4 |
| `src/main/java` | 180 sources — the library, the demos, and the CLI applications |
| `src/test/java` | 50 sources — the repository's own JUnit suite |
| `docs/`, `README.md` | upstream documentation |
| `HANDOFF-NOTES.md` | if it is there, what the work before you had to say about this tree |

The toolchain on this box:

| | |
|---|---|
| `JAVA_HOME=/opt/jdk/8` | JDK 8, and it is first on `PATH`; **11, 17 and 21 are also under `/opt/jdk/`** |
| `/opt/maven/bin/mvn` | Maven 3.9 |
| `SPARK_HOME=/opt/spark` | the Spark **2.4.8 for Scala 2.11** distribution this box was installed with — four whole lines older than the one this step targets, and it will not run this tree's jar (§4) |
| `MMTF_FULL=/data/full`, `MMTF_REDUCED=/data/reduced` | the structure corpus, Hadoop sequence files, read-only |

**The default JDK on this box is not the one this step targets**, and that is
worth knowing in the first minute rather than the thirtieth. `JAVA_HOME` and
`PATH` point at JDK 8, so `mvn` runs on 8 unless you say otherwise, and a pom
that asks to be compiled for Java 17 cannot be compiled by `javac` 8 at all —
the build stops with `invalid target release: 17` rather than quietly producing
something. JDK 17 is installed at `/opt/jdk/17`. How you point
the build at it is up to you.

**Build.** The repository's own instruction, and the one the project must build
with,
**run on the JDK named in §4**:

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
the line is written out. 58 of the classes here ask for `local[*]` in their own
source, which beats `--master`, and `local[*]` is `availableProcessors()`; at
any width above one the executor threads interleave on the same stdout and the
same program prints two different answers on two runs. What §2 asks of the
programs is that each of them, run at exactly this line, still prints what it
prints on the runtime §4 names; the line is fixed because the output depends on
it. Run it any other way while you work if you like — how you develop is up to
you — but a difference you see at `local[*]` and not at this line is the
width, not you.

The behaviour is also defined on a filesystem that serves every directory's entries in byte
order of their names, for the same reason. Hadoop's local listing does not sort,
so the order a corpus directory's `part-*` files are listed in is the order they
become partitions — and with it which rows a seeded sample draws and the order a
partition-ordered print comes out in. The filesystem on this box keeps an order
of its own, so a value you see move with the listing order, on either tree, is
the filesystem, not you.

Three things go with that line, none of which
changes what a program computes. The `spark-submit` on its `PATH` is the one that
belongs to the runtime in §4, not this box's `/opt/spark/bin/spark-submit`. The
corpus is read from `MMTF_FULL` and `MMTF_REDUCED` and may be mounted somewhere
other than `/data`, so a class that reaches for the literal path instead of the
variable is a class that stops working. And the driver JVM may be started with
an observer attached — a class path entry of its own and a listener registered
on the session — to note what each run's session was configured with, because
some of what §3 asks for is a fact about the run and not about the tree. All
three are conditions of §2 rather than flags to reproduce: a program that
depends on the absence of any of them does not meet §2.

One thing that line does **not** need, and the reason is worth having before
you start debugging something that is not broken: `spark-submit` opens the JDK
modules Spark itself reflects into, so every program launched through it starts
cleanly on JDK 17 with nothing added to this line. The forked JVM your *tests*
run in is a different JVM and gets none of that — see §3, m2, where it is a
stated requirement.

**Network.** Public. Several demos call web services (RCSB, EBI/SIFTS,
UniProt, DrugBank, SWISS-MODEL, JPred, MyVariant, PDB-REDO, …); on this box
those hosts resolve to an in-container mirror that serves fixed responses over
https and ftp, so they answer without reaching the real host and give the same
answer every time. It starts itself from a login shell; you do not have to
configure anything. `archive.apache.org`, `repo1.maven.org` and the rest of the
real internet are not mirrored and behave normally.

**The repository's own tests.** The required set is **46 classes and 140 tests**,
and it is larger than the one a plain `mvn -B test` runs: this pom's surefire configuration excludes nine
long-running test classes by default — all nine of them are in the tree — and
§2 requires those nine too. The command that runs the whole set, and the one it
must pass under:

```
cd /app && mvn -B test -Dtest='**/*Test.java' -DfailIfNoTests=false \
    -Dsurefire.timeout=900 -Dspark.driver.host=127.0.0.1
```

They pass on the tree you received, the one class below excepted, and must
still pass, under the conditions this tree's own tests already meet: no single
test needs more than 900 seconds to finish, and every one of them passes with
the Spark driver bound to `127.0.0.1`. A test that only comes back green on a
longer leash, or only with the driver on some other address, is a test that did
not pass.

A clean run of that command reports 47 classes and 142 tests, not 46 and 140,
and one class is deliberately outside the count in both directions:
`edu.sdsc.mmtf.spark.webservices.CustomReportServiceTest`. Both of its tests
compare a schema against a rendering they hardcode as a string literal, and
`StructType.toString()` has printed that rendering with one space fewer since
the release before this one, so both have been failing since then and both
still fail here. That is the literal being stale, not the move being wrong, it
is expected, it is outside the required set in either direction, and it is not
yours to repair: the suite that must pass is `src/test` as you received it, not
an edited copy. Fix it if it bothers you; nothing
depends on the fix. Work on the other 46.

Count the files and you will find 50 `*Test*.java` sources under `src/test`, not
47; three of them contribute nothing to that command and always have.
`SecondaryStructureExtractorTest2` does not end in `Test.java`, so the selector
above never asks for it. `StructureToCathDomainsTest` and `WildTypeTest` are
asked for, but every `@Test` method in both arrives commented out, so surefire
finds nothing to run in either and writes no report for either. All three are
how upstream left the tree and none of them is something to fix here: 47 classes
and 142 tests is what a clean run prints, 46 and 140 of them are the required set, and 50
or 49 would mean you changed something §2 did not ask you to.

---

## 2. What must still be true when you are done — behaviour

This step changes which release of Spark runs the work, which Scala binary
version that release is built for, and which JDK compiles and runs it. It
changes no science.

1. **Every program still answers the same, and so does every library class it
   calls.** Two kinds of job are covered, under the same rule. The first is
   each `main` under `edu.sdsc.mmtf.spark` — the demos, the `applications`
   CLIs, and the few library classes with entry points — each with a fixed
   set of arguments. The second is this project's own **public API**, called
   directly: when the `alignments`, `datasets`, `filters`, `interactions`, `io`,
   `mappers`, `ml`, `utils` and `webfilters` classes are constructed and their
   public methods invoked from outside the project, what they return — row
   counts, schemas, column values, the exception a bad argument raises — must
   be what that same call returns. So a public signature, a return type, a
   schema or a thrown exception that changes is a behaviour change even when no
   `main` in the tree would have noticed. In both cases the process must exit
   with the same status — except where the status is itself decided by which
   rows a sampled subset happened to draw, which no version of Spark promises
   to reproduce either — and write the same thing to standard output. This
   includes the unhappy paths: a missing argument, an unreadable path, an
   unknown option or `--help` must still fail (or succeed) the same way and
   print the same message. Only standard output counts, and only after
   run-to-run noise has been normalised away: framework log lines, JVM and
   class-loader warnings, stack traces and blank lines are removed, and
   absolute paths, wall-clock durations and the date stamps this tree bakes
   into generated file names are rewritten. A handful of jobs print a value no
   version of Spark promises to reproduce — an unseeded sample, a `Word2Vec`
   training race, a `groupBy().count()` whose rows tie, a count taken over a
   sample that *is* seeded but whose draw no Spark version promises to keep
   from one version to the next, and rows whose order the query itself does not
   fix, as when a bare limit decides how much input was touched or a sort
   leaves a tie unbroken — and those lines are set aside too. What is left is
   what the program itself decided: row counts, identifiers, schemas, computed
   values, the order a program does fix, and the text of the messages it
   prints. All of that must match.

2. **The repository's own test suite still passes.** All 140 of the required
   tests, in the 46 required classes. A test you delete, rename, `@Ignore` or
   weaken is a test that did not pass, and at this step there is a second way to
   fail this that did not exist before — a
   suite that does not run at all is a suite that did not pass, however it came
   not to run.

Both hold on **Spark 3.4.4 for Scala 2.13, on JDK 17** — the runtime
§4 names and the one §3 asks you to land on — with the ANSI dialect the step
before this one turned on still on.

### What "the same" means when the release, the Scala build and the JDK all move

"The same" is **what these programs print on Spark 3.4.4 for Scala 2.13 under
JDK 17**, and not what they print on the runtime you were handed. A difference
the new runtime produces on its own is not yours to reproduce or to prevent.

On this tree the two coincide once §3's work is done: what these programs print
at the new runtime, with the inferring reads pinned as m3 asks, is line for
line after the normalisation above what they print today. That is a measurement
rather than an assumption, and it is stated because it tells you both where the
work is and where it is not. There is no output change at this fence you have
to invent a rewrite for, and no program here that needs restructuring to keep
printing what it prints. There is one change you have to *stop*, and stopping
it is m3: a csv read left to infer on its own hands back a different column
type at the new release, and that is the new runtime changing this project's
output rather than its own.

Two facts about the move are worth having for the same reason:

* **Scala 2.12 → 2.13 costs this repository nothing.** There is no Scala source
  in this tree. The only `scala.*` types its Java imports are `Tuple2` and
  `Tuple4` — 45 files import the first and one the second, and nothing under
  `src/test` imports either — and those are the same classes in both binary
  versions. The suffix is a coordinate in `pom.xml`, not a porting job.
* **Nothing in this tree stops compiling on JDK 17.** Every source here builds
  under 17, and every program §2 covers was run there. The JDK move shows up in
  exactly two places that are yours: what the jar is compiled *for*, and the
  JVM your tests fork (§3, m2).

If a program starts failing after you move, then, the cause is a change you
made and not the release — and the fix is that change, rather than a switch
that asks the new engine to behave like the old one. §3 rules those switches
out.

### And a tree that changed nothing has not done this step

A tree that changed nothing still prints nearly all of §2 — not
because it preserved anything, but because the engine and the JDK it runs
on are provided by the runtime and not by it. Printing §2 therefore does not
mean any of §3 was done: §3 is about the pom the build resolves, the
class files in the jar the build produced, and the read sites in the sources
themselves. Both are asked for, and the
migration is the half that does not come with the runtime.

It goes one step further than that, and that is stated rather than left to be
found out. Until

* the build resolves **Spark 3.4.4 for Scala 2.13** and no `org.apache.spark`
  artifact at any other version, with no coordinate left at any other Scala
  suffix, **and**
* the jar the build produces carries **every one of this repository's classes
  at Java 17's class-file version** and none at any other, **and**
* every csv read under `src/main` that asks the engine to infer a schema **pins
  date inference off**, with this tree's csv read sites still there and the pin
  present in the built jar,

§2 is a question about work that has not started, and it is not met: the
programs and the test suite alike. The three conditions above are a subset of
what §3 asks for, so they add nothing to it.

---

## 3. What this step asks for — the migration

One release, 3.3.4 to 3.4.4; one Scala binary version, 2.12 to 2.13; one JDK,
8 to 17. Three axes at once, and §2 has already said what two of them cost this
repository: nothing. That is worth knowing before you start, because it tells
you where the work is. It is not in the Scala types and it is not in the Java
language level. It is in the version this build resolves, in what the jar is
compiled for, and in what this tree's readers ask the engine to guess.

**m1 — the build lands on Spark 3.4.4 for Scala 2.13.**

| | |
|---|---|
| the pom | every `org.apache.spark` artifact the build resolves is at **3.4.4**, and no `org.apache.spark` artifact resolves at any other version. Every one of them resolves at 3.3.4 today |
| the suffix | every Scala-suffixed coordinate the build resolves carries **`_2.13`**, and none carries any other suffix. Every suffixed coordinate the build resolves reads `2.12` today |

Two facts, deliberately not one fact twice. Both are about the pom — the version and
the suffix are separate claims, and `spark-core_2.12:3.4.4` is a real artifact
and the wrong one, so a build can land the release and miss the migration.

That the engine each job actually ran in reports `spark.version` **3.4.4**, and
that none of them ran in a session reporting anything else, is required too, as
a precondition on the run rather than a third fact of m1: `spark-core` is
`provided` scope here, so the jar carries no engine and the version a job comes
up on is the installation's rather than this pom's.

`mvn dependency:list` is the shortest way to check the two facts above,
`spark.version` read off a running `SparkSession` the shortest way to see what
the run came up on.

m1 is the fence and not the milestone. The fence it names moves nothing by
itself — see §2 — and a requirement that is satisfied by editing a property and
a suffix is not what this step is for.

**m2 — the project is compiled for Java 17, and the JVM its tests fork is
opened.**

| | |
|---|---|
| the build asks for Java **17** | the source and target level the build compiles at — today both read `1.8`, from a property |
| and the jar it produced actually carries it | **every** class of this repository in the built jar at Java 17's class-file version, and none at any other. Every one of them is at Java 8's today |
| the forked test JVM can reach the JDK-internal package Spark's storage layer uses | the `<argLine>` surefire starts that JVM with — today `-Xmx4g`, and nothing else |

**Asking is not producing, which is why the first two rows are both here.** A
pom can ask for 17 and hand you a jar full of Java 8 class files, and the jar is
the thing that runs: `maven-compiler-plugin` compiles at `1.8` when nothing
tells it otherwise, so a tree that moves everything else and leaves the level
out compiles silently at the old version, and an explicit `<release>8</release>`
written beside a `<source>17</source>` does the same thing louder. Equally, the
same tree built by `javac` 17 with the level left at `1.8` still emits Java 8
class files — measured on this tree, both ways — so the machine you build on is
not the claim either. What is asked for is that the build says 17 and the jar
carries it.

**The third row is what JDK 17 costs this repository, and it is one line of
build configuration.** Spark's storage layer reaches reflectively into a package
the JDK keeps to itself in order to free mapped memory. JDK 17 no longer exports
that package to code outside the platform, so on 17 the forked JVM surefire runs
the tests in dies inside that reflection *before any test body executes* — every
test in every class that brings up a Spark session reports an
`IllegalAccessError`, which names the module and the package the access was
denied to, on a tree that reports none at all on JDK 8. Opening that package to
the forked test JVM is the repair, and it is asked for here rather than being
left to be discovered because §2 makes the whole suite a requirement. Two things
about where it goes: `spark-submit` opens what the programs need itself, so §2's
programs need nothing (§1), and it is the *forked* test JVM that is asked
about — running the tests inside Maven's own JVM leaves whatever you configured
for that fork inert, which fails this row and takes the suite down with it.

**m3 — every read that asks the engine to infer comes back with the types it
came back with before the move (the milestone's source half).**

| | |
|---|---|
| every csv read under `src/main` that asks the engine to infer a schema pins date inference **off**, at the site where the read is made | none of them says anything about dates today |
| every csv read site this tree has is still a csv read site under `src/main` | none consolidated into a helper, none deleted |
| and the pin reached the built jar | no jar this tree builds today carries it |
| every read under `src/main` names its format where the read is made — not computed into a variable, not left to `.load()`'s parquet default | every read in the tree today names a literal format |
| and every csv or json read that declares no schema passes its options one at a time rather than as a map | no such read in the tree today uses the map form |

**What this is about, in Spark's terms.** Spark 3.4 changes what csv schema
inference does with a date-shaped column: inference now tries a date type
before a timestamp type, so a `yyyy-MM-dd` column that arrived as a
`TimestampType` at 3.3.4 arrives as a `DateType` at 3.4.4, by default and with
nothing in the code changed. Spark 3.4 also adds the csv option that decides
it, and it sits beside `inferSchema` in the csv data source option table in
Spark's own documentation. This repository hands exactly that shape to
inference — one of its readers asks the RCSB report service for a deposition
date and lets the engine guess the column's type — so the type of a column in a
`Dataset` this tree returns moves underneath it at this release, without one
line of this tree changing.

A runtime move is not licensed to change the types this project's own readers
produce. So what is asked for is not that the read say *something* about dates.
It is that the read **pins date inference off**, at the site where the read is
made, so that the column comes back with the type it came back with before the
move. A read that pins it on has stated the new behaviour rather than held the
old one, and has not answered this. The pin is written in that read's own
option chain, with the option's key spelled exactly as Spark's documentation
spells it and its value written as a literal — not through a constant, a
variable or a map — so the read says what it asks for where it is made. The switch that would answer it for a whole
session is forbidden outright — see below — because a global switch is a claim
about the engine, and what is wanted here is a claim about a read.

Reads that ask the engine for nothing are outside this and are to be left
alone: a read that passes no options has no inference to pin, and a read that
declares a schema of its own is not inferring and has no date question to
answer. Neither add inference to a read in order to have something to pin, nor
take it away from one to escape the question — a read that stops inferring
drops out of this requirement without having answered it, and the requirement
is over the reads that infer on the tree you were handed.

Three of those rows — the second, the fourth and the fifth — are not extra work
and they are green on the tree you were handed. They are there because the
first row is also a statement about *absence* — no inferring csv read left
unpinned — and absence is satisfied by having no such reads at all. Deleting
them, or moving a read's format into a variable (`spark.read().format(fmt)`),
or leaving it to a bare `.load(path)` — which reads parquet and says so nowhere
— or passing its options as a map instead of one at a time, would each turn
that requirement into a requirement about nothing. So the read sites stay where
they are (**this step is not a refactor** — consolidating two reads into one
helper breaks the count even though nothing is wrong with the tree), the
format and the options stay legible where the read is made, and the pin has to
survive into the jar the build produces: a source edit in a file the build does
not reach, or one that never compiled, has not been made as far as §2's
programs are concerned.

**The requirement is over every csv read that infers, not over most of them.**
One left unpinned is a reader whose answer still moves with the engine, which
is the thing this requirement exists to stop, and it is not met until none of
them is left that way.

It is asked from both sides, and this is the part worth knowing before you pick
a route. "No inferring csv read left unpinned" is true of a tree that has no
inferring csv reads left, so it is not the whole of what §2's precondition asks: it
asks that alongside the count of reads that do pin date inference off. Every
csv read this tree asks the engine to infer has to still ask, and has to pin.
A read that stops asking — deleted, consolidated into a helper, hidden behind a
computed format, or handed a schema of its own — drops out of the absence
without answering anything, and fails §2's precondition as well as m3.
Again: **this step is not a refactor** — leave the read sites where they are.

**And m3 means all of them.** Move the rung completely and pin all but one of
the inferring reads, and neither m3 nor §2 is met — for work that is nearly all
done. That is stated here rather than discovered:
the step is the rung *and* the reads, and a read still asking the engine to
guess at a column whose meaning moved is a reader this step's claim is false
for.

### Not through a back door

Ten shortcuts produce the appearance of this step without producing it. All ten
are ruled out entirely: a tree that does any of them has faked this step rather
than done part of it.

None of these prohibitions is about a spelling, and a reflective call is still
the call it makes: the names below are ruled out in the sources, in the built
jar's constant pool, and in the run itself. Not one of them appears anywhere
in the sources you were handed, so holding to this rules out nothing you would
otherwise have written.

The rule goes one step past the names, so it
is said plainly rather than left to be inferred: no class this build compiles
may call into `java.lang.reflect` at all, or look a member or a type up by name
through `java.lang.Class`. That is how a prohibited call is made without the
call appearing in the source or in the constant pool, so it is ruled out entirely
rather than case by case. The one `java.lang.Class` member this
repository does use is `getSimpleName`, in many of its files, and it is not one
of those lookups; nothing under `src/main` or `src/test` reflects today.

* **Do not stop the suite running.** This is the first step at which the
  repository's own tests can be lost by not doing the work — until the test JVM
  is opened, nearly every one of them errors — and the cheap way to make a red
  suite stop being red is to stop running it. So: surefire is not configured to
  skip tests, and neither is the property that does the same thing without
  naming the plugin. Both spellings are ruled out in the pom. What is *not*
  prohibited, because it is inert here rather than permitted: the suite's
  command in §1 passes an explicit test selector that overrides surefire's own
  includes and excludes, so the `<excludes>` list this pom already carries has
  no effect on it, and growing it has none either.
* **No legacy flag.** Nothing in the tree, nothing in the built jar and nothing
  in the session any job runs in may set a `spark.sql.legacy.*` property.
  Those switches exist so that an application that cannot be changed can ask a
  new engine to keep answering like an old one, and this application is being
  changed. This carries forward, and it is not answered by
  arguing that some legacy property would have settled m3 more cheaply: a
  session-wide property is a claim about the engine where m3 asks for a claim
  about a read. However set — in code, in `spark-defaults.conf`, on the submit
  line — and however spelled: the ban covers the whole name and the leaf of it
  in the jar, so a name assembled at run time out of pieces leaves the piece
  behind, and it covers the *effective* value of those properties in each running
  session, which does not care how the name was spelled at all.
* **Do not loosen store assignment.** `spark.sql.storeAssignmentPolicy` stays
  at Spark's own default, which on this engine is `ANSI`. Setting it to
  `LEGACY` — or to any value other than `ANSI`, in code, in
  `spark-defaults.conf` or on the submit line — leaves the dialect the step
  before this one turned on reading `true` while restoring exactly the silent
  truncation and silent `null` it was turned on to stop. It applies both ways:
  what a session was asked to set, and what the engine reports.
* **Do not switch adaptive query execution off, or take away its ability to
  act.** `spark.sql.adaptive.enabled` is true by default and stays that way,
  and it is held to that three ways: in what a session was explicitly
  asked to set, wherever it was asked, in the setting each session then
  reports, and in the optimiser actually re-planning at least one query
  somewhere across all of §2's jobs. Setting any of the sub-settings
  that do the re-planning (`spark.sql.adaptive.coalescePartitions.enabled` and
  its siblings) to anything but `true` is switching AQE off by another name: an
  advisory partition size, or a minimum partition count that leaves nothing to
  coalesce, starves the optimiser as surely as a `false` does, and so does
  naming optimiser rules to exclude. Saying `true` out loud is not a change;
  any other value on any of these keys is.
* **Do not set a JVM system property.** Nothing under `src/main` may set one —
  not through `System.setProperty`, and not through the property table
  `System.getProperties()` hands back, which is the same table under another
  name. It is the route that configures every session in a process at once,
  including the ones you did not write and any outside caller of
  this project's public API in the same process. This tree
  sets no system property anywhere, in `src/main` or in `src/test`, so holding
  to it takes nothing away.
* **No opaque encoder.** Nothing in the tree, and nothing in the built jar, may
  use `Encoders.kryo` or `Encoders.javaSerialization`. A black-box column is a
  `Dataset` the engine can reason about nothing inside of, which undoes by
  wrapping what an earlier step achieved.
* **No hand-set execution id.** Nothing in the tree, and nothing in the built
  jar, may call `setLocalProperty`. `spark.sql.execution.id` is a writable
  local property: setting it by hand puts a claim about the work where the
  record of it should be.
* **Do not reach a forbidden type through reflection.** `Class.forName(...)`, a
  name assembled at run time, a classloader lookup: the jar's constant pool may
  not carry any forbidden class's name as text either, which covers what a
  method reference alone does not. What earlier steps asked to be *gone* is
  asked to be gone, not hidden.
* **Do not answer a source requirement in a file the build does not compile.**
  m3's third row is the general form of this and it is stated there; it is
  repeated here because the same move is available to every requirement about
  the sources — a class outside the build's source roots, an excluded
  directory, a file the compiler never sees. The jar is the tree that
  runs.
* **No reaching for the run's listener.** Some of what §3 asks about is
  a fact about a run rather than about the tree, so a listener records each run from
  inside its own driver process (§1) — which makes the observer reachable from
  your code, and none of it is yours to touch. Nothing in the tree and nothing
  in the built jar may install an observer of its own on a session, name the
  one a run attaches or anything that configures it, stand in for it by
  carrying a class of its own where it lives, or put a row of its own onto the
  channel it reports on — in the names it goes by, nothing in the tree and
  nothing in the built jar may name `spark.extraListeners`, the listener class
  installed through it or the environment variables set for that
  listener, and neither may carry a class of its own in that listener's
  package, a class in it being the strongest form of naming it. Each of those puts a claim about the run where the
  record of it should be, and each is ruled out with the nine above. So is
  arranging for the reading of one of the settings named above to fail rather
  than answer, or for one of §2's programs to stop reaching a session
  at all: a program that no longer builds a session reports nothing about
  `spark.version`, and that silence is ruled out the same way.

### And throughout

The tree still parses as Java, `pom.xml` still resolves to an effective POM and
a dependency list, and the build still compiles the project's own classes —
**every** `.java` source the tree ships with is still there, and the build
produces at least 150 `edu/sdsc/mmtf/` classes. (Every one of them, not "most
of them": no requirement above is answered by deleting a source file, so none
of them is allowed to be.) The jar has to run, not merely link: a plain
`spark-submit` of one of the demos off it must start a Spark session and exit
0. Warnings are answered rather than silenced: `@SuppressWarnings` must not
name `deprecation`, `removal`, `unchecked`, `serial` or `all` anywhere in the
tree, and the total number of `@SuppressWarnings` annotations must not rise
above what the tree already carries. A suppression is not
a fix. Two clean builds still have to agree byte for byte.

Everything the work before you was asked for is still asked for, as a
precondition here: the terminal result of the
fifteen classes moved onto the SQL engine before you — `FilterByResolution`,
`FilterBySequenceRegex`, `MmtfBenchmark`, `ReadMmtfFull`, `ReadMmtfReduced`,
`ReadLocalMmtf`, `ReadLocalMmtfHadoopFile`, `MapToBioAssembly`,
`MapToBioAssembly2`, `MapToProteinDimers`, `MapReduceExample`,
`CreateRepresentativeSet`, `AuthorSearchDemo`, `KeywordSearch` and
`KinaseSearch` — still reaches it, and `MmtfReader`'s `read*` and `download*`
methods still return `JavaPairRDD<String, StructureDataInterface>`; no call to
`Dataset.describe(...)` remains in the sources or in the jar and the statistic
set that replaced it is still named in this tree's own code and still in the jar
as text; no `SparkConf` is constructed anywhere under `src/main` or referenced
from the jar, every `JavaSparkContext` is still derived from a `SparkContext`,
every program under `src/main` still builds a session of its own inside its
`main`, as one unbroken `SparkSession.builder()` … `getOrCreate()` chain, and still
brings one up when it runs, and `spark.sql.ansi.enabled` is still explicitly
asked for by every one of them, still reported true by every engine they run
in, and still contradicted nowhere; `com.databricks:spark-xml` is still gone
from the pom and from the jar, nothing in the jar refers to `org.spark_project.`
or `org.sparkproject.` or to `Word2VecModelReader` while `ProteinSequenceEncoder`
still loads a saved model through `Word2VecModel`'s public loader, every plugin
version still comes from this pom, and nothing else the build resolves can still
move. None of that may be undone, in either direction.

The one exception is the one this step exists for, and it has two halves: the
previous steps' fence at Spark 3.3.4 is *replaced* by this step's at 3.4.4, and
their fence at Scala `_2.12` is replaced by this step's at `_2.13`. Moving off
either is not undoing them.

---

## 4. The runtime this step targets

Your jar must run on:

| | |
|---|---|
| Spark | `spark-3.4.4-bin-hadoop3-scala2.13` |
| JDK | 17 |
| Scala | 2.13 |

All three move at this step, and landing on the first and the third is m1 while
landing on the second is m2. Note which 3.4.4 distribution that is: Spark
publishes `spark-3.4.4-bin-hadoop3` *and*
`spark-3.4.4-bin-hadoop3-scala2.13`, they are different downloads, and the
second one is the one your jar has to match. Both are served by
`archive.apache.org`, which is reachable from this box, if you want that runtime
here to try things against; it may already be installed somewhere on this box
from the work before you, so check before you download it. Installing it is
optional; it is mentioned because "it
compiled" and "it ran on the engine and the JVM this step names" are different
claims and only the second one is what §2 and §3 ask about. It is a
`bin-hadoop3`
distribution, so it brings its own Hadoop and needs nothing on
`SPARK_DIST_CLASSPATH`. JDK 17 is on this box already, at the path §1 names;
it is not what `java` resolves to.

One consequence of `spark-core` being a `provided` dependency, since it will
save you a wrong conclusion, and it bites harder at this step than at any step
before it: the shaded jar does not carry Spark, so the engine your jar meets is
whichever `spark-submit` launched it and the JVM it meets is whichever JDK that
`spark-submit` ran on — neither is decided by your pom. A jar whose class files
are all at Java 8's version runs perfectly well on JDK 17. A jar built against
3.3.4 for Scala 2.12 runs on a 3.4.4-for-2.13 installation too, for everything
this tree does, because this tree calls Spark's Java-facing API and the Scala
suffix it was compiled against does not appear in those signatures. So both
halves of "it ran, and it printed the right thing" are available to a tree that
did none of this step's work — which is why m1 is about the pom the build
resolves and not the version a run reports, why m2 covers the class
files in the jar as well as the pom, and why §2 is not met until they
are.

---

## 5. Left to you

So you do not spend the budget on it: this step sets no requirement on *how* the
compiler
level is spelled — two properties, a `<release>`, a plugin configuration block,
all of them at once — on whether the two properties named `jdk.version` and
`maven.enforcer.jdk-version` are updated, left alone or deleted, since no
plugin in this build reads either of them and no requirement here concerns them;
on where among the others the option that pins date inference is written, or in
what order the options are written, or where in the builder chain it sits (its
key is still spelled exactly as Spark's documentation spells it, and its value
is a literal, as §3 says) —
though it does have to be part of the same builder chain as the `read()` call
it applies to, since that chain is what settles what that read asks the engine
for; on how many JVMs surefire forks or what else is on that JVM's argument
line as long as it forks and that opening is on it; on how sessions are
constructed beyond each `main` still building its own as that one chain, on how
many partitions anything has, on formatting, imports, comments, `README.md` or
anything under `docs/`; on `/app/HANDOFF-NOTES.md`, which is for whoever opens this
tree next; or on how long the build or the programs take. What is required is in
§2 and §3 and nothing else is.

---

## 6. Handoff

The tree you leave in `/app` is the tree whoever opens this workspace next
works in. Leave it buildable, and leave a short note in
`/app/HANDOFF-NOTES.md` — what you changed, what you had to work around, and
anything a reader coming to this tree cold would otherwise have to rediscover.
What goes in it is up to you. Do not edit this file.
