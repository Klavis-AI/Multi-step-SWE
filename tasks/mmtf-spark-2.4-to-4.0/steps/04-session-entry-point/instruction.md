# Step 4 — one session per program, and turn the ANSI dialect on

## 1. Implement this milestone

`/app` is mmtf-spark building against Spark 3.2.4 for Scala 2.12 on JDK 8.
Move the build one release forward, to **Spark 3.3.4** for Scala 2.12, and then
do the work that move is here to make room for: give each program one
`SparkSession` of its own, built where that program starts, configured to run
in Spark's ANSI SQL dialect, and take its `JavaSparkContext` from that session.

Nothing in this tree stops compiling or changes what it prints across that
fence — that was measured before the step was written, over every program under
`edu.sdsc.mmtf.spark`, and every one of them exited the same way and printed
the same bytes on each side of the fence. So the version move is free, and it
is not the work.

The work is where this codebase enters Spark. Sixty-three of its programs open
by building a `SparkConf` and handing it to a `JavaSparkContext`:

```java
SparkConf conf = new SparkConf().setMaster("local[*]").setAppName(...);
JavaSparkContext sc = new JavaSparkContext(conf);
```

`SparkConf` and `JavaSparkContext` are the RDD-era entry point. Everything
downstream of them in this tree has already moved to `Dataset` — that move is
already made in the tree you are handed — and a `SparkConf` cannot configure
the SQL engine those
datasets run on, because the settings that engine reads are session settings.
So every SQL-level decision this project might want to make has sixty-three
places it cannot be made from, and there is no shared factory anywhere in this
tree to make it in instead. That is what this step fixes, and it is the reason
it is worth fixing at all: the setting this step goes on to turn on is a
session setting, and so is every other decision about the SQL engine, so while
the entry point stays where it is there are sixty-three places to make such a
change and nowhere to make it once.

The setting to make first is the one that says what the fix bought:
`spark.sql.ansi.enabled`. Spark's ANSI dialect turns silent arithmetic overflow
and silent `null`-on-bad-cast into errors. It is off by default at 3.3 and
stays off through 3.5; Spark **4.0 turns it on by default**, and a codebase that
has not found out which of its casts were wrong before then finds out from
production. Turning it on a rung early is how this tree finds out here. On this
corpus the answer is that the casts are already clean — every program and every
public API call the behaviour contract covers answers identically with it
forced on, which is measured, not hoped — so this costs the programs' output
nothing.

When you are done:

* **the build resolves Spark 3.3.4, and so does the session.** Every
  `org.apache.spark` artifact the build resolves is at 3.3.4 for Scala 2.12 and
  no `org.apache.spark` artifact resolves at any other version; and the engine
  each job actually runs in reports `spark.version` 3.3.4, with none of them
  reporting anything else. Both halves must hold, and they are not the same
  fact: `spark-core` is a `provided` dependency, so the jar does not carry
  Spark and a build that still resolves 3.2.4 will run perfectly well on a
  3.3.4 installation. The pom is the half you control, and it must resolve
  3.3.4 itself. This is the fence and not the milestone — it is a small part of
  the migration, and a tree that moves the version and stops has done little of
  this step;
* **no `SparkConf` is constructed anywhere under `src/main`**, and none is
  referenced from the built jar's `edu/sdsc/mmtf` classes. Sixty-three sites
  today, in sixty-three files, one per file. Both halves matter: the source
  half is about the syntax tree rather than the text, and the jar half covers a
  conf built anywhere else, or under another name;
* **every `JavaSparkContext` is constructed from a `SparkContext`**, and no
  `SparkContext` is constructed directly. There are seventy-one
  `new JavaSparkContext(...)` sites; sixty-three take a conf and eight already
  take `spark.sparkContext()`. The eight are the shape the sixty-three are to
  become. This is a fact about the constructor **descriptor** in the class file,
  not about the source text — `new JavaSparkContext(conf)` and
  `new JavaSparkContext(spark.sparkContext())` are the same three words in a
  source file and two different constructors in a class file — so the way to
  satisfy it is to genuinely derive the context, and renaming a variable to
  `spark` does not;
* **no program under `src/main` is left without a session of its own.** Every
  file that declares a `public static void main` builds a `SparkSession`
  inside that `main` — one unbroken `SparkSession.builder()` … `getOrCreate()`
  chain written in the body of `main` itself, not in a field initializer, a
  helper method, or a builder kept in a variable and finished later. There are **78** such files and **48** of them build none
  today: the sixty-three conf openers, less the fifteen that already hold a
  builder because those demos were already given a session and the conf was
  left beside it. Each of the forty-eight needs one of its own; the thirty that
  have one need it kept where it is. This is asked file by file, and that is
  the point of it — not one shared session factory that every program calls. A
  `SessionFactory.get(...)` that sixty-three mains ask for is a reasonable
  design and it is a different one: it leaves this tree with a single place to
  configure sessions instead of sixty-three, and it has not done this step,
  because the forty-eight files that call it still build no session. Such a tree
  meets the version requirement and the dialect — its one session really is
  configured — but not this one, and without this one the step is not done. The milestone is that each program owns the session it runs in, so
  that the next person who has to configure one program differently can do it
  where that program is. A file with no `main` is nobody's entry point and is
  not asked about;
* **`spark.sql.ansi.enabled` is true in every one of this repository's
  programs — in the settings its session is built with, and in the engine that
  session then runs in.** Three conditions, asking three different things. The
  first is what the program *asked for*: the flag is part of the configuration
  the session is built with, so the program carries its own dialect wherever it
  is run. The second is what the engine *reports*, which is the value that
  actually governs the casts. The third is that nothing, anywhere, set the flag
  to anything other than true. A tree that mutates its `SparkConf` after the
  context is already running has the first and not the second — the engine read
  that map once and will not read it again. A tree that calls `.set(...)` on a
  session that already exists, or that inherits the value from a Spark
  distribution's own defaults or from a submit line it did not write, has the
  second and not the first, and the first is the half that is yours. Both are
  asked of **every** program, not of most of them: on in some and unasked-for
  in the rest is not on. And "set" includes the JVM it runs in — a `SparkConf`
  picks up every `spark.*` system property that is set when it is constructed,
  so a `System.setProperty("spark.sql.ansi.enabled", "true")` in a static
  initializer would put the dialect on sessions this step asked you to write
  and you had not. Nothing under `src/main` sets a system property today, and
  nothing may;
* **the ANSI dialect's consequences are not bought off.**
  `spark.sql.storeAssignmentPolicy` stays at Spark's own default, which on
  this engine is `ANSI`. Setting it to `LEGACY` — or to any value other than
  `ANSI`, in code, in `spark-defaults.conf` or on the submit line — leaves
  `spark.sql.ansi.enabled` reading `true` while restoring
  exactly the silent-null behaviour turning ANSI on was for. It is a faked step
  rather than a partial one, and it is forbidden both in what a session sets and
  in what the engine reports;
* **nothing in the tree, in the built jar or in any session reaches for a
  legacy switch, switches adaptive execution off, or reaches for the
  listener.** All of the step-3 prohibitions carry forward unchanged: no
  `spark.sql.legacy.*` property however spelled and wherever set; adaptive
  execution left on and left able to act; no `Encoders.kryo`, no
  `Encoders.javaSerialization`, no `setLocalProperty`, and no jar naming
  `spark.extraListeners`, the listener class installed through it, the
  environment variables set for it on the `spark-submit` line, or any class of
  your own in that listener's package. ANSI mode gives a fresh reason to reach
  for a legacy flag — casts that used to return null now throw — which is
  precisely why they still apply here;
* **and `SparkConf` is not reached through reflection.** The name may not
  appear in the jar's constant pool any more than in the sources, so
  `Class.forName("org.apache.spark.SparkConf")` is no way around the
  requirement above. A reflective call is still that call, and
  the prohibition is wider than the one class: nothing this build compiles may
  call into `java.lang.reflect`, or look a member or a type up by name through
  `java.lang.Class` — `forName`, `getMethod`, `getDeclaredMethod`,
  `getConstructor` and their siblings. `getSimpleName`, which 124 of this
  tree's files already use, is not one of them, and nothing here reflects
  today.

The store-assignment loosening, the system property, the legacy switches, the
adaptive switch, the opaque encoders, the hand-set execution id, the reach for
the listener and the reflective conf are each a faked step rather than a partial
one, and each is forbidden outright. None of those names appears anywhere in the
230 sources you are handed, so the change itself never needs them.

Behaviour that must still hold: every program under `edu.sdsc.mmtf.spark`, and
this project's public API called directly from outside it, still exit with the
same status and print the same thing to stdout — the error and `--help` paths
included — and the repository's own JUnit suite still passes, all **46** test
classes and **140** tests of it. **"The same" means what these programs
print on Spark 3.3.4 with the ANSI dialect on**, and on this tree that is
byte-for-byte what they print today: the fence moves nothing and the dialect
moves nothing, both measured over every program and every public API call the
contract covers before this step was written. So unlike the fence behind it,
this one has no output change to account for.
Some lines move across a version fence without the program having changed
its answer — §2 of the handoff says which kinds of difference the behaviour
contract sets aside, and reordering and re-jitter are among them.

Neither behaviour nor the migration may be traded for the other: both are
required. The behaviour must hold on a tree that made this move, though: a tree
that changed nothing preserves its output trivially and has not done this step.
The behaviour is defined at one fixed `spark-submit` line, because what the
programs print depends on it; that line, the command that runs all 46 test
classes, and the thresholds your jar must meet are in the handoff document named below.

## 2. Read these local references

The requirements on the build apply to the POMs alone: anything under
`.mvn/` (`jvm.config`, `maven.config`, `extensions.xml`) or in a `.mavenrc`
does not count toward meeting them.

**Normative:** `/app/WORKSPACE_HANDOFF.md` — the workspace, the build and run
commands, the behaviour contract, all three migration requirements stated in
full with their thresholds, the entry-point counts they are stated against,
and the runtime this step targets. It is the definition of this step;
read it first.

**Additional:** `/app/README.md` and `/app/docs/` for what the programs do;
`/app/HANDOFF-NOTES.md` for what the work before you had to say about this
tree; upstream's Spark SQL documentation on the ANSI compliance dialect, which
covers `spark.sql.ansi.enabled` and `spark.sql.storeAssignmentPolicy` and what
each of them governs; the `SparkSession.Builder` and `SparkSession.sparkContext`
API documentation for the shape this step asks for; and the Spark 3.3.0 release
notes for what changed across this fence. The 3.3.4 distribution is served by
`archive.apache.org` and is reachable from this box if you want that runtime
here to check any claim above yourself.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. The Java sources and `pom.xml` are
  yours to edit — that is the step — but the tree you leave behind is still the
  same project: all 230 `.java` sources are still there, the supplied documents
  and handoff files are still there, and nothing gains a prose answer or an
  alternate entry point. The jar `mvn -B clean package -DskipTests` produces is
  the artifact, and it must stay the single shaded uber jar matching
  `target/mmtf-spark-*.jar`.
- Everything the work before you was asked for must still hold: the terminal
  result of the fifteen classes the handoff lists
  by name still reaches the SQL engine, `MmtfReader` is still an RDD reader
  returning `JavaPairRDD<String, StructureDataInterface>`, no call to
  `Dataset.describe(...)` remains and the statistic set that replaced it is
  still named in your code and still in the jar as text, the resolved graph is
  still Scala 2.12, `com.databricks:spark-xml` stays gone, the jar stays clear
  of `org.spark_project.`, `org.sparkproject.` and `Word2VecModelReader` while
  `ProteinSequenceEncoder` still loads a model through `Word2VecModel`'s public
  loader, plugin versions still come from this pom, nothing else the build
  resolves can move, and two clean builds still agree byte for byte. None of it
  may be undone. The one thing that does move is
  the Spark version itself: the previous step's 3.2.4 fence is replaced by this
  step's 3.3.4 fence, and moving off 3.2.4 is not undoing it.
- Do not cheat by using online solutions or hints specific to this task.
- The repository's own suite under `/app/src/test` must pass as it ships, not
  as edited: all 46 test classes and 140 tests it holds — including the classes
  this pom's surefire configuration excludes from a plain `mvn -B test` — and
  none of them may be deleted, `@Ignore`d, skipped or weakened. One class,
  `webservices.CustomReportServiceTest`, is outside those 46 at this rung: its
  two tests assert a schema string that Spark 3.3 renders with one space fewer,
  they fail here, they are expected to, and they are not yours to repair.
  Warnings are to be answered rather than silenced: no `@SuppressWarnings`
  naming `deprecation`, `removal`, `unchecked`, `serial` or `all` may be added
  beyond the tree's ten existing suppressions, which are a ceiling.
- Leave the tree buildable, and write what you did and what you worked around
  to `/app/HANDOFF-NOTES.md`, which is for whoever opens this tree next.
