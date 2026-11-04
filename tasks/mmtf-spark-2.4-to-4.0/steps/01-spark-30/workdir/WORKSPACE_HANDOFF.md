# Workspace handoff — mmtf-spark, step 1

This is the document `instruction.md` points at. It describes the workspace you
have been given and states, in Maven's and Spark's own terms, everything this
step requires. Every requirement is written here or in
`instruction.md`, and everything written here as a requirement applies.

---

## 1. What is in this workspace

`/app` is the [mmtf-spark](https://github.com/sbl-sdsc/mmtf-spark) working tree
at commit `e74298a6511732c5d6905eeec86b3a73e3d24447`, exactly as upstream left
it. One Maven project, no modules:

| | |
|---|---|
| `pom.xml` | at the root; `edu.sdsc.mmtf:mmtf-spark:0.3.0-SNAPSHOT` |
| `src/main/java` | 180 sources — the library, the demos, and the CLI applications |
| `src/test/java` | 50 sources — the repository's own JUnit suite |
| `docs/`, `README.md` | upstream documentation |

The toolchain on this box:

| | |
|---|---|
| `JAVA_HOME=/opt/jdk/8` | JDK 8; 11, 17 and 21 are also under `/opt/jdk/` |
| `/opt/maven/bin/mvn` | Maven 3.9 |
| `SPARK_HOME=/opt/spark` | Spark **2.4.8 for Scala 2.11** — the line the tree builds against **today**, not the one this step targets (§4) |
| `MMTF_FULL=/data/full`, `MMTF_REDUCED=/data/reduced` | the structure corpus, Hadoop sequence files, read-only |

**Build.** The repository's own instruction, and the one the project must build with:

```
cd /app && mvn -B clean package -DskipTests
```

It must produce exactly one runnable jar matching `target/mmtf-spark-*.jar`
once `original-*`, `*-sources.jar` and `*-javadoc.jar` are set aside: the
shaded uber jar. Every program runs from that jar and nothing else, so a
build that stops producing it, or that starts producing two, meets none of §2's
behavior requirements no matter what the sources say.

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
depends on it, so one thread is part of what "the same thing to standard
output" means here. Run it any other way while you work if you like -- how
you develop is up to you -- but a difference you see at `local[*]` and not
at this line is the width, not you.

The behaviour is also defined on a filesystem that serves every directory's entries in byte
order of their names, for the same reason. Hadoop's local listing does not sort,
so the order a corpus directory's `part-*` files are listed in is the order they
become partitions -- and with it which rows a seeded sample draws and the order a
partition-ordered print comes out in. The filesystem on this box keeps an order
of its own, so a value you see move with the listing order, on either tree, is
the filesystem, not you.

**Network.** Public. Several demos call web services (RCSB, EBI/SIFTS,
UniProt, DrugBank, SWISS-MODEL, JPred, MyVariant, PDB-REDO, …); on this box
those hosts resolve to an in-container mirror that serves fixed responses
over https and ftp, so they answer without reaching the real host and give the same answer every time.
It starts itself from a login shell; you do not have to configure anything.
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

47 test classes, 142 tests, 0 failures and 0 errors today, and they pass under
the conditions this tree's own tests already meet: no single test needs more
than 900 seconds to finish, and every one of them passes with the Spark driver
bound to `127.0.0.1`. A test that only comes back green on a longer leash, or
only with the driver on some other address, is a test that did not pass.

Count the files and you will find 50 `*Test*.java` sources under `src/test`, not
47; three of them contribute nothing to that command and always have.
`SecondaryStructureExtractorTest2` does not end in `Test.java`, so the selector
above never asks for it. `StructureToCathDomainsTest` and `WildTypeTest` are
asked for, but every `@Test` method in both arrives commented out, so surefire
finds nothing to run in either and writes no report for either. All
three are how upstream left the tree at this commit and none of them is
something to fix here: 47 classes and 142 tests is what a clean run prints, and
50 or 49 would mean you changed something §2 did not ask you to. (Plain
`mvn -B test` runs 39 classes and 124 tests and is not the command §2 means. The
`<excludes>` block that makes the difference is in `pom.xml` and you are free
to leave it exactly as it is -- nothing here asks you to change surefire's
configuration, only to keep all 47 classes passing when they are asked for by
name.)

Those two zeroes are a fact about the tree as you received it, built the way you
received it. They are not a promise that the same 142 stay green once the build
moves. A library can change what it infers, what it accepts or what it throws,
and turn a test red without a line of that test changing. A test that goes red
under the new build is telling you something about the migration: the answer is
in the code it tests, never in the test and never in surefire's configuration.

---

## 2. What must still be true when you are done — behaviour

This step changes the build, not the science. Both of these take as their reference
the tree exactly as you received it:

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

Both hold on JDK 8 — this step does not move the Java version.

And both are questions about the **migrated** build. The Spark artifacts this
pom declares are `provided`: they are compiled against and then left out of the
jar, and whatever the pom resolved, the jar is run against the Spark the runtime
supplies. So a tree that never moved off 2.4 still reproduces a good deal of §2
-- not because it preserved anything but because the jar it built
happens to still load. That is not what §2 is asking:
until the build resolves Spark somewhere other than the 2.4 line, §2 is a
question about an artifact that does not exist yet, and it is not met. Any move
off 2.4 satisfies that -- m1 below asks for 3.0.3 exactly, on its own terms, but
§2 does not ask for a *correct* move, only for a real one.

---

## 3. What this step asks for — the migration

Spark 2.4 is the last line built for both Scala 2.11 and Scala 2.12; Spark 3.0
is the first that is Scala 2.12 only. The two moves are therefore one move, and
this step makes it: onto Spark 3.0.3 — the last 3.0.x — for Scala 2.12, and
onto a build that repeats. Spark 3.0 is a major release and it takes things
away; m3 below is the part of that the *build* stands on. It is not all of it —
what a library removes quietly, and answers differently afterwards, is covered by
§2 rather than named here.

**m1 — Scala 2.12, Spark 3.0.3.** The project's resolved dependency graph
contains nothing built for a Scala version other than 2.12: no `_2.11`, no
`_2.13`, in any artifact at any depth, direct or transitive. `spark-core` for
Scala 2.12 resolves at version **3.0.3**, and every other `org.apache.spark`
artifact in the graph resolves at 3.0.3 as well — one Spark version, not a mix.
(3.0.3 exactly, in other words: a graph that resolves any other Spark version,
in either direction, does not satisfy this.)

**m2 — `com.databricks:spark-xml` is gone.** The 0.4.x line it is pinned at has
no Scala 2.12 release, so it cannot come along. When you are done it is not
declared in `pom.xml`, does not appear anywhere in the resolved graph, and no
`com/databricks/` class is left inside the built jar. It is declared in the pom
and shaded into the artifact today, so all three of those have to be answered
and not just the first.

**m3 — what Spark 3.0 takes away.** This is the part of the step the pom cannot
answer on its own: at 3.0.3 this tree does not compile until both of these are
dealt with, and the built artifact has to show that they were dealt with
rather than worked around.

* **Spark's shaded Guava.** Through 2.4, Spark shaded its third-party libraries
  under `org.spark_project.`; 3.0 renamed that prefix to `org.sparkproject.`
  and publishes nothing under the old one. When you are done, nothing this
  project compiles refers to a class under either prefix — for any member, from
  anywhere in the tree. The new one is ruled out exactly as the old one is: reaching
  for it instead of converting is trading one Spark internal for another, and
  whatever the conversion was, do it in this project's own code or through a
  library this project declares.

* **`Word2VecModel.Word2VecModelReader`.** The nested reader
  `ProteinSequenceEncoder` constructs is not accessible in Spark 3. When you are
  done nothing in the jar refers to `Word2VecModelReader`, **and**
  `ProteinSequenceEncoder` still reaches the public loading API of
  `org.apache.spark.ml.feature.Word2VecModel` — `Word2VecModel.load(path)` or
  `Word2VecModel.read().load(path)`, either spelling. The second half of that is
  not decoration: deleting the code that loads the model satisfies the first
  half and is not a migration, so both are required.

**m4 — every plugin version comes from the project.** No plugin in this
project's effective build may take its version from Maven's super POM. That
is every plugin the effective POM lists under `<build><plugins>`: the ones
`mvn package` runs, and also the ones Maven's lifecycle mappings bind for a
`jar` project's `install` and `deploy` phases and for the `site` lifecycle,
which this build never reaches but whose versions the installed Maven would
otherwise choose all the same. Each one is pinned in this project's own
`pom.xml` — in
`<build><plugins>`, in `<pluginManagement>`, or in a `<profile>`'s copy of
either. It is the file itself that has to carry the pin, not an inherited value: a
version that reaches the build from somewhere other than `/app/pom.xml` does
not count as pinned here. Which versions you pin is your choice; that they are
the project's choice and not the installed Maven's is the requirement. (The
super POM's defaults change with the Maven release, which is exactly the kind of
drift the rest of this step is about.)

**m5 — nothing the build resolves can move.** No repository the build reads has
snapshot resolution enabled. No dependency or plugin resolves to a version
ending `-SNAPSHOT`, or to the moving keywords `LATEST` or `RELEASE`, or to a
version range. The project sets `project.build.outputTimestamp` to a fixed
value; which value is yours to choose.

**m6 — two clean builds agree.** Running `mvn -B clean package -DskipTests`
twice from a clean tree produces byte-identical artifacts: the same jars, to
the byte, both times. And the build writes nothing into the source tree outside
`target/` — no generated POM, no scratch file, nothing that makes the second
build start from a different tree than the first. Both builds must succeed.

**And throughout:** the tree still parses as Java, `pom.xml` still resolves to
an effective POM and a dependency list, and the build still compiles the
project's own classes — all **230** `.java` sources the tree ships with are
still there, and the build produces at least 150 `edu/sdsc/mmtf/` classes.
(230, not "most of them": none of the six requirements above is answered by
deleting a source file, so none of them is allowed to be.) The jar has to run,
not merely link: a plain `spark-submit` of one of the demos off it must start a
Spark session and exit 0. Warnings are not silenced rather than answered:
`@SuppressWarnings` must not name `deprecation`, `removal`, `unchecked`,
`serial` or `all` anywhere in the tree, and the total number of
`@SuppressWarnings` annotations must not rise above the ten the tree already
carries. A suppression is not a fix.

---

## 4. The runtime this step targets

Your jar must run on:

| | |
|---|---|
| Spark | `spark-3.0.3-bin-hadoop3.2` |
| JDK | 8 |

That archive is served by `archive.apache.org`, which is reachable from this
box, if you want the runtime here to try things out — the `/opt/spark` you were
given is the Spark 2.4.8 build for Scala 2.11 and will not run a Scala 2.12 jar
built against Spark 3. Installing it is optional; it is mentioned because "it built" and "it runs"
are different claims and only the second one is what this step asks for. It is a `bin-hadoop3.2`
distribution, so it brings its own Hadoop and needs nothing on
`SPARK_DIST_CLASSPATH`.

For orientation rather than as a requirement: Spark is declared `provided` in
this pom, so the uber jar carries the project and its own libraries and the
runtime supplies Spark. The scope is not a requirement; it is mentioned because
it explains why the jar is the size it is and why the Spark distribution the
jar meets at run time is not the one Maven resolved.

---

## 5. Left to you

So you do not spend the budget on it: this step sets no requirement on which
Java sources you changed, as long as all 230 are still there; on how you write
whatever takes over from what Spark 3 removed, on which artifact you declare for
the dependency Spark 3 stopped supplying, on your plugin versions, or on the
value of your output timestamp; on formatting, imports, comments, `README.md` or
anything under `docs/`; on `/app/HANDOFF-NOTES.md`, which is for whoever opens
this tree next; or on how long the build or the programs take.

---

## 6. Handoff

The tree you leave in `/app` is the tree whoever opens this workspace next
works in. Leave it buildable, and leave a short note in
`/app/HANDOFF-NOTES.md` — what you changed, what you had to work around, and
anything a reader coming to this tree cold would otherwise have to rediscover.
What goes in it is up to you. Do not edit this file.
