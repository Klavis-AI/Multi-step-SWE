# Workspace handoff — mmtf-spark, step 4

This is the document `instruction.md` points at. It describes the workspace you
have been given and states, in Spark's own terms, everything this step
requires. Every requirement is written here or in `instruction.md`,
and everything written here as a requirement applies.

---

## 1. What is in this workspace

`/app` is the [mmtf-spark](https://github.com/sbl-sdsc/mmtf-spark) working tree
as the work before you left it: one Maven project, no modules, building against
**Spark 3.2.4 for Scala 2.12** on JDK 8, with the terminal result of fifteen
named classes already taken through the SQL engine rather than off an RDD, and
with the statistic sets it prints named in its own code rather than chosen by
the engine.

| | |
|---|---|
| `pom.xml` | at the root; `edu.sdsc.mmtf:mmtf-spark:0.3.0-SNAPSHOT`, resolving Spark 3.2.4 |
| `src/main/java` | 180 sources — the library, the demos, and the CLI applications |
| `src/test/java` | 50 sources — the repository's own JUnit suite |
| `docs/`, `README.md` | upstream documentation |
| `HANDOFF-NOTES.md` | if it is there, what the work before you had to say about this tree |

The toolchain on this box:

| | |
|---|---|
| `JAVA_HOME=/opt/jdk/8` | JDK 8; 11, 17 and 21 are also under `/opt/jdk/` |
| `/opt/maven/bin/mvn` | Maven 3.9 |
| `SPARK_HOME=/opt/spark` | the Spark **2.4.8 for Scala 2.11** distribution this box was installed with — three whole lines older than the one this step targets, and it will not run this tree's jar (§4) |
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

This matters here in a second way it did not before. §3 asks you to move where
each of those 58 requests is made — out of a `SparkConf` and onto a
`SparkSession` builder. `SparkSession.builder().master("local[*]")` and
`new SparkConf().setMaster("local[*]")` reach the same setting, so moving one to
the other changes nothing at all. Dropping the request while you move it, or
adding one to a program that never made it, is a different matter — though not
at the line above, where `availableProcessors()` is 1 and `local[*]` and
`--master local[1]` are the same single thread, so §2 cannot tell you about it
in either direction. Move what is there anyway: `local[*]` is the width every
other way of running this tree gets, and a program that quietly stopped asking
for it, or quietly started, answers differently the first time somebody runs it
on eight cores.

Three things go with that line, none of which
changes what a program computes. The `spark-submit` on its `PATH` is the one that
belongs to the runtime in §4, not this box's `/opt/spark/bin/spark-submit`. The
corpus is read from `MMTF_FULL` and `MMTF_REDUCED` and may be mounted somewhere
other than `/data`, so a class that reaches for the literal path instead of the
variable is a class that stops working. And the driver JVM may be started with
an observer attached — a class path entry of its own and a listener registered
on the session — to record what each run's session was configured with, because
some of what §3 is about is a fact about the run and not about the tree. All
three are conditions of §2 rather than flags to reproduce: a program that
depends on the absence of any of them does not meet §2.

**Network.** Public. Several demos call web services (RCSB, EBI/SIFTS,
UniProt, DrugBank, SWISS-MODEL, JPred, MyVariant, PDB-REDO, …); on this box
those hosts resolve to an in-container mirror that serves fixed responses over
https and ftp, so they answer without reaching the real host and give the same answer every time. It
starts itself from a login shell; you do not have to configure anything.
`archive.apache.org`, `repo1.maven.org` and the rest of the real internet are
not mirrored and behave normally.

**The repository's own tests.** The required set is **46 classes and 140 tests**,
which is more than plain `mvn -B test` runs: this pom's surefire configuration
excludes nine long-running test classes by default — all nine of them are in
the tree — and §2 requires those nine too. The command that runs the full set,
and the one it must pass under:

```
cd /app && mvn -B test -Dtest='**/*Test.java' -DfailIfNoTests=false \
    -Dsurefire.timeout=900 -Dspark.driver.host=127.0.0.1
```

Those 140 pass on the tree you received and must still pass, under the
conditions this tree's own tests already meet: no single test needs more than
900 seconds to finish, and every one of them passes with the Spark driver bound
to `127.0.0.1`. A test that only comes back green on a longer leash, or only
with the driver on some other address, is a test that did not pass.

That command will report 47 classes and 142 tests, not 46 and 140, and one
class is deliberately outside the count in both directions:
`edu.sdsc.mmtf.spark.webservices.CustomReportServiceTest`. Its two tests assert
a schema string as a literal, and `StructType.toString()` drops the space after
a comma between 3.2.4 and 3.3.4. So those two tests pass on the tree you were
handed and **both of them will fail once you land on 3.3.4** — that is the
literal being stale, not the move being wrong, it is expected, it is outside the required
set in either direction, and it is not yours to repair: the suite that must pass
is `src/test` as you received it, not an edited copy. Fix
it if it bothers you; nothing depends on the fix. Work on the other 46.

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

This step changes which release of Spark runs the work, where each program
opens its connection to it, and which SQL dialect that connection uses. It
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
   weaken is a test that did not pass.

Both hold on JDK 8 and on **Spark 3.3.4** — the runtime §4 names and
the one §3 asks you to land on — with the ANSI dialect §3 asks you to turn on
turned on.

### What "the same" means on a step that moves the engine and the dialect

"The same" is **what these programs print on Spark 3.3.4 with
`spark.sql.ansi.enabled` true**. On this tree that happens to be exactly what
they print today, and that is a measurement rather than an assumption: every
program and call §2 covers was run at 3.3.4, and run again at 3.3.4 with ANSI forced
on for every job, and came back identical both times. Nothing at this fence
prints differently, so unlike the step before this one there is no output
change to account for and none to be careful of.

That second measurement is also the evidence for something worth knowing before
you start. The ANSI dialect turns silent arithmetic overflow and silent
`null`-on-bad-cast into errors, which is the sort of change that breaks a
codebase loudly if that codebase was relying on either. This one is not: every
cast and every arithmetic expression §2's programs and calls reach already
answers the same under the strict dialect as under the permissive one. So turning it on is
not a risk you are being asked to absorb — it is a property of this tree you are
being asked to make explicit, one release before Spark 4.0 makes it the default
and stops asking.

What that means in practice: if a program starts failing after you turn ANSI on,
the cause is a change you made and not the dialect, and the fix is that change
rather than a switch that turns the dialect's consequences back off. §3 rules
those switches out.

### And a tree that changed nothing has not done this step

A tree that changed nothing reproduces most of §2 — not because it
preserved anything but because nothing happened to it — and that is not what §2
is asking. Until no `SparkConf` is constructed anywhere under `src/main` and
none is referenced from the built jar, *and* no program under `src/main` is
left without a session of its own — in its sources and in the run alike, since
a session written into a file the program never executes is not a session that
program has — §2 is a question about work that has not started, and neither half
of it — the programs or the test suite — is met.

---

## 3. What this step asks for — the migration

One release, 3.2.4 to 3.3.4, and nothing in this tree stops compiling across
it: every program and every public API call §2 covers was built and run at both
rungs, and none of them failed to build and none of them crashed. That is worth
knowing before you start, because it tells you where the work is. It is not in
the compiler and it is not in the version number. It is in where this codebase
opens its connection to Spark.

**m1 — the build and the session land on 3.3.4.**

| | |
|---|---|
| the pom | every `org.apache.spark` artifact the build resolves is at **3.3.4** for Scala 2.12, and no `org.apache.spark` artifact resolves at any other version |
| the session | the engine each job actually ran in reports `spark.version` 3.3.4, and none of them ran in a session that reported anything else |

Two facts, deliberately not the same fact twice. The pom says what the build
resolved; the session says what the code met. A tree can get the first right
and the second wrong — a stale jar left in `target/`, a Spark of some other
version reachable on the submit classpath — and this step is about the engine,
not the declaration. Scala stays at 2.12 and the JDK stays at 8; 3.3.4 is the
last 3.3 patch, and JDK 8 is what this tree builds and runs on here.

`mvn dependency:list` is the shortest way to check the first, `spark.version`
read off a running `SparkSession` the shortest way to check the second.

m1 is not the milestone, because
the fence it names moves nothing — see §2 — and a requirement that is satisfied
by editing one property is not what this step is for.

**m2 — one session per program, and the context derived from it (the
milestone).**

Sixty-three of this codebase's programs open like this:

```java
SparkConf conf = new SparkConf().setMaster("local[*]").setAppName("Example");
JavaSparkContext sc = new JavaSparkContext(conf);
```

`SparkConf` and `JavaSparkContext` are the RDD-era entry point. Everything
downstream of them here has already moved onto `Dataset` — that move is
already made —
and a `SparkConf` handed to a `JavaSparkContext` cannot configure the SQL
engine those datasets run on, because the settings that engine reads are
session settings and there is no session to read them. There is also no shared
session or context factory anywhere in this tree, so there is no one place to
put one. The result is that every SQL-level decision this project might make has
sixty-three places it cannot be made from, and m3 is the first of them.

When you are done:

| | |
|---|---|
| no `SparkConf` is constructed anywhere under `src/main` | **63** sites today, one per file across 63 files; what counts is a construction in the parsed source, not the text |
| and none is referenced from the built jar's `edu/sdsc/mmtf` classes | the jar's method references and constant pool; so a conf built anywhere the sources do not show still counts |
| every `JavaSparkContext` is constructed from a `SparkContext` | **71** sites today: 63 take a conf and 8 already take `spark.sparkContext()`. The 8 are the shape the 63 are to become |
| no `SparkContext` is constructed directly | 0 today, and it stays 0 |
| no program under `src/main` is left without a session of its own | every file that declares a `public static void main` builds a `SparkSession` inside that `main`, as one unbroken `SparkSession.builder()` … `getOrCreate()` chain written in the body of `main`; **78** such files, and **48** of them build none today |

The context requirement is about the constructor **descriptor** in the class file,
not the source text. `new JavaSparkContext(conf)` and
`new JavaSparkContext(spark.sparkContext())` are the same three words in a
source file and two different constructors in a class file: the first takes a
`SparkConf` and is the thing this step retires, the second takes a
`SparkContext` and is the thing it asks for. Renaming a variable to `spark` is
not the move; deriving the context is.

The last row deserves its own paragraph, because it is asked file by file and
not as a total. The row above it already requires all 63 confs gone; this one
is the guard against deleting them and putting nothing in their place. The
arithmetic: 15 of the 63 conf-holding files already hold a
`SparkSession.builder()` call — those demos were already given a session and
the conf was left beside it — and 48 hold none. Give each of those 48 a
session of its own and keep the 15 where they are, and every main-bearing file
in the tree has one. A total would not say this: thirty-six builders in one
class and none in the other forty-seven would clear any total you like, and
that tree has forty-seven programs that still cannot be configured. Files with
no `main` in them are not entry points and are not asked about, so a helper
that takes a session as an argument is unaffected either way.

**Where the session goes is part of the requirement.** One session per program,
built where that program starts. Not one `SessionFactory.get(...)` that all
sixty-three call: routing every entry point through a single shared factory is
a defensible design and it is a *different* one, it leaves this tree with one
place to configure sessions instead of sixty-three, and it does not
do this step — the row above is what says so, because the 48 files
that call the factory still build no session of their own, and it is stated
here rather than left to be discovered. What that means is worth knowing before
you choose it: such a tree meets m1 and it meets m3, because the factory's
session really is configured and really does report the dialect, and it meets
neither m2 nor §2. The milestone is that each program owns the
session it runs in, so that the next person who needs one program to be
configured differently can do it where that program is.

**m3 — the ANSI dialect on, at every session this tree builds.**

| | |
|---|---|
| **every** one of this repository's programs is built with `spark.sql.ansi.enabled` set to `true` | what that program's session was explicitly asked for — in code, in `spark-defaults.conf` or on the submit line — at run time, program by program |
| and the engine **every** one of them runs in reports it as true | the effective value each running session answers with, program by program |
| and nothing, anywhere, sets it to anything else | any set value that is not `true` fails this outright, in any job of the run |

Three rows, and they ask different questions. The first says
this program asked for it; the second says the engine it ran in did it; the
third says nobody anywhere asked for the opposite. A tree that mutates its
`SparkConf` after the context is already running has the first and not the
second — the engine read that map once and will not read it again. A tree that
calls `.set(...)` on a session that already exists, or that inherits the flag
from a Spark distribution's own defaults or from a `--conf` on a submit line it
did not write, has the second and not the first; and the first is the half that
is yours, which is why it is about the run rather than a source file. A
config line in a source file is a spelling, and spellings are free here.

The word doing the work in the first two rows is **every**. They are asked of
each of this repository's programs separately, and a program that asked for
nothing fails them as squarely as one that asked for `false`: on in most
programs and unasked-for in the rest is a codebase that has learned nothing
about its own casts. The third row is asked of the run as a whole, so a job
that reaches for `false` anywhere fails it even if every program's own session
is clean.

One consequence worth stating, because it is the one route to these rows that
does not pass through the entry point m2 is about: a `SparkConf` picks up every
`spark.*` JVM system property that is set when it is constructed, so a single
`System.setProperty("spark.sql.ansi.enabled", "true")` in a static initializer
would turn the dialect on for every session in the JVM — including the ones you
were asked to write and did not. Nothing in this tree sets a system property,
in `src/main` or in `src/test`, and nothing in your tree may either; it is on
the list in the next section.

### Not through a back door

Eight shortcuts produce the appearance of this step without producing it. All
eight are ruled out entirely: a tree that does any of them has faked this step
rather than done part of it.

None of these prohibitions is about a spelling, and a reflective call is still
the call it makes: the names below are ruled out in the sources, in the built
jar's constant pool, and in the run itself. Not one of them appears anywhere
in the 230 sources you were handed, so holding to this rules out nothing you
would otherwise have written.

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

* **Do not loosen store assignment.** `spark.sql.storeAssignmentPolicy` stays
  at Spark's own default, which on this engine is `ANSI`. Setting it to
  `LEGACY` — or to any value other than `ANSI`, in code, in
  `spark-defaults.conf` or on the submit line — leaves
  `spark.sql.ansi.enabled` reading `true` while restoring exactly the silent
  truncation and silent `null` that turning the dialect on was for. It is the
  one switch that makes m3 look done while undoing what m3 is for, and it
  applies both ways: what a session was asked to set, and what the engine reports.
* **Do not reach `SparkConf` through reflection.** `Class.forName(...)`,
  a name assembled at run time, a classloader lookup: the jar's constant pool
  may not carry the class's name as text either, which covers what a
  method reference alone does not. m2 asks for the conf to be *gone*, not hidden.
* **Do not set a JVM system property.** Nothing under `src/main` may set one —
  not through `System.setProperty`, and not through the property table
  `System.getProperties()` hands back, which is the same table under another
  name. A `SparkConf` reads every `spark.*` system property that is set at the
  moment it is constructed, so one line in a static initializer configures
  every session in the process — the ones you wrote and the ones you did not. It is the one route to m3 that does not pass through
  the entry point m2 is about, and it configures any outside caller of this
  project's public API in the same process as readily as your programs. This tree sets no system property anywhere, in
  `src/main` or in `src/test`, so holding to it takes nothing away.
* **No legacy flag.** Nothing in the tree, nothing in the built jar and nothing
  in the session any job runs in may set a `spark.sql.legacy.*` property.
  Those switches exist so that an application that cannot be changed can ask a
  new engine to keep answering like an old one, and this application is being
  changed. This carries forward from the step before, and
  this step gives a fresh reason to reach for one: a cast that used to return
  `null` now throws, and a legacy flag is the quickest way to make it stop.
  However set — in code, in `spark-defaults.conf`, on the submit line — and
  however spelled: the ban covers the whole name and the leaf of it in the jar, so
  a name assembled at run time out of pieces leaves the piece behind, and it
  covers the *effective* value of those properties in each running session, which
  does not care how the name was spelled at all.
* **Do not switch adaptive query execution off, or take away its ability to
  act.** `spark.sql.adaptive.enabled` is true by default from Spark 3.2 and
  still is at 3.3, and it is held to that three ways: in what a session
  was explicitly asked to set, wherever it was asked, in the setting each
  session then reports, and in the optimiser actually re-planning at least one
  query somewhere across all of §2's jobs. Setting any of the
  sub-settings that do the re-planning
  (`spark.sql.adaptive.coalescePartitions.enabled` and its siblings) to
  anything but `true` is switching AQE off by another name: an advisory
  partition size, or a minimum partition count that leaves nothing to coalesce,
  starves the optimiser as surely as a `false` does, and so does naming
  optimiser rules to exclude. Saying `true` out loud is not a change; any other
  value on any of these keys is. Consolidating sixty-three sessions is an
  opportunity to write a new set of settings into each of them — write only the
  ones that were there and the one m3 asks for.
* **No opaque encoder.** Nothing in the tree, and nothing in the built jar, may
  use `Encoders.kryo` or `Encoders.javaSerialization`. Carried forward from the
  move that put the fifteen classes into the engine: a black-box column is a
  `Dataset` the engine can reason about nothing inside of, and undoing that here
  by wrapping is ruled out the same way it was there.
* **No hand-set execution id.** Nothing in the tree, and nothing in the built
  jar, may call `setLocalProperty`. `spark.sql.execution.id` is a writable
  local property: setting it by hand puts a claim about the work where the
  record of it should be.
* **No reaching for the listener.** Nothing in the tree, and nothing in the
  built jar, may name `spark.extraListeners`, the listener class installed
  through it, or any of the environment variables set for that
  listener, and neither may carry a class of its own in that listener's
  package — a class in it is the strongest form of naming it. What each
  session was configured with is observed by a `SparkListener` attached on the
  `spark-submit` line, and the driver it runs in is your own process — so the
  observer itself is reachable from your code, even though what it reports is
  not. Reaching for it puts a claim about the run where the record of it
  should be, and is ruled out with the seven above. So is writing into what it
  reports: the observer runs in your driver process, so its output is
  reachable, and it is also predictable — it says each of its own things once
  per process, and a second copy of one of them is a line something other than
  it wrote. So is arranging for its reading of one of the settings named above to
  fail rather than answer, or for one of §2's programs to stop
  reaching a session at all: a program that no longer builds a session reports
  nothing about `spark.version` and nothing about the dialect, and that silence is
  ruled out the same way. That one bites harder
  here than it did before, because m3 is asked of every program: a program
  that no longer runs is a program that cannot answer, and an answer is what
  is being asked for.

### And throughout

The tree still parses as Java, `pom.xml` still resolves to an effective POM and
a dependency list, and the build still compiles the project's own classes — all
**230** `.java` sources the tree ships with are still there, and the build
produces at least 150 `edu/sdsc/mmtf/` classes. (230, not "most of them":
no requirement above is answered by deleting a source file, so none of them is
allowed to be.) The jar has to run, not merely link: a plain `spark-submit` of
one of the demos off it must start a Spark session and exit 0. Warnings are
answered rather than silenced: `@SuppressWarnings` must not name `deprecation`,
`removal`, `unchecked`, `serial` or `all` anywhere in the tree, and the total
number of `@SuppressWarnings` annotations must not rise above the ten the tree
already carries. A suppression is not a fix.

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
as text; the resolved graph is still Scala 2.12, `com.databricks:spark-xml` is
still gone from the pom and from the jar, nothing in the jar refers to
`org.spark_project.` or `org.sparkproject.` or to `Word2VecModelReader` while
`ProteinSequenceEncoder` still loads a saved model through `Word2VecModel`'s
public loader, every plugin version still comes from this pom, nothing else the
build resolves can still move, and two clean builds still agree byte for byte.
None of that may be undone, in either direction. The one exception is the one this step exists
for: the previous step's fence at Spark 3.2.4 is *replaced* by this step's at
3.3.4, and moving off 3.2.4 is not undoing it.

---

## 4. The runtime this step targets

Your jar must run on:

| | |
|---|---|
| Spark | `spark-3.3.4-bin-hadoop3` |
| JDK | 8 |
| Scala | 2.12 |

The JDK does not move at this step; the Spark does, and landing on it is m1.
The `/opt/spark` you were installed with is the Spark 2.4.8 build for Scala
2.11 and will not run this tree's jar; the 3.3.4 distribution is served by
`archive.apache.org`, which is reachable from this box, if you want that
runtime here to try things against. It may already be installed somewhere on
this box from the work before you — check before you download it. Installing it
is optional; it is mentioned because
"it compiled" and "it ran with the dialect on" are different claims and only the
second one is what this step asks for. It is a `bin-hadoop3` distribution, so it brings its
own Hadoop and needs nothing on `SPARK_DIST_CLASSPATH`.

One consequence of `spark-core` being a `provided` dependency, since it will
save you a wrong conclusion: the shaded jar does not carry Spark, so the engine
your jar meets is whichever `spark-submit` launched it, and a jar built against
3.2.4 will happily run on a 3.3.4 installation. That is why m1 covers the
pom as well as the session — the pom is the half of it you control — and
why "it ran and printed the right thing" is not by itself evidence that the
build resolves what it should.

---

## 5. Left to you

So you do not spend the budget on it: this step sets no requirement on *how*
you build each session — builder chain on one line or several, whether what `getOrCreate()`
returns is kept in a local variable or a field (the chain itself is written in
`main`, as §3 says), one
`main` per file or two sharing one — on whether a program that needs no context
at all keeps one, on whether the app names and masters live in constants or are
repeated at each site, on which order you write the builder's calls in, on how
many partitions anything has, on formatting, imports, comments, `README.md` or
anything under `docs/`; on `/app/HANDOFF-NOTES.md`, which is for whoever opens
this tree next; or on how long the build or the programs take. What is required
is in §2 and §3 and nothing else is.

---

## 6. Handoff

The tree you leave in `/app` is the tree whoever opens this workspace next
works in. Leave it buildable, and leave a short note in
`/app/HANDOFF-NOTES.md` — what you changed, what you had to work around, and
anything a reader coming to this tree cold would otherwise have to rediscover.
What goes in it is up to you. Do not edit this file.
