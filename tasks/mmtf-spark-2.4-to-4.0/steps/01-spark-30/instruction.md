# Step 1 — move mmtf-spark to Spark 3.0.3 on Scala 2.12

## 1. Implement this milestone

`/app` is mmtf-spark as upstream left it: one Maven project built against Spark
2.4.0 for Scala 2.11, on JDK 8. Spark 2.4 is the last line published for Scala
2.11 and Spark 3.0 is the first that is Scala 2.12 only, so those two moves are
one move. Spark 3.0 is also the release that takes several things this
tree uses away. Three of them stop it compiling and are impossible to miss; at
least one changes an answer instead, and only the repository's own tests say
so. Put the build on Spark 3.0.3 for Scala 2.12, answer what 3.0 removes, and
make it a build that gives the same answer twice.

When you are done:

* nothing in the project's resolved dependency graph is built for a Scala
  version other than **2.12**, and every `org.apache.spark` artifact in it
  resolves at **3.0.3** — one Spark version, not a mix, and no other Spark
  version in the graph at all;
* `com.databricks:spark-xml` is gone — not declared, not resolved, and no
  `com/databricks/` class left in the built jar. It has no Scala 2.12 release in
  the 0.4.x line it is pinned at. Find out what in this tree imports it before
  you plan a replacement;
* the jar the build produces no longer reaches for anything Spark 3.0 removed.
  Two things in particular. No reference to a class under `org.spark_project.`
  — Spark's shaded third-party libraries through 2.4 — and none under
  `org.sparkproject.` either, which is where 3.0 moved that shade: swapping one
  Spark internal for another is not the conversion. And `ProteinSequenceEncoder`
  still loads a saved `Word2VecModel` through that class's own public loader,
  rather than through the nested reader 3.0 stops letting you at. Deleting the
  code that used them is not an answer to them: the conversion still has to
  happen and the model still has to load;
* every Maven plugin in the project's effective build takes its version from
  `pom.xml`, not from Maven's super POM: the ones `mvn package` runs, and the
  ones Maven's lifecycle mappings bind for `install`, `deploy` and `site` as
  well, although this build never reaches them;
* nothing the build resolves can move: no repository with snapshots enabled, no
  `SNAPSHOT`, `LATEST`, `RELEASE` or version range, and
  `project.build.outputTimestamp` is set;
* `mvn -B clean package -DskipTests` from clean, run twice, produces
  byte-identical jars and writes nothing into the source tree outside
  `target/`.

Behaviour that must still hold, compared against the tree you received: every
program under `edu.sdsc.mmtf.spark`, and this project's public API called
directly from outside it, still exit with the same status and print the same
thing to stdout — the error and `--help` paths included — and all 47 of the
repository's own JUnit test classes, 142 tests, still pass. Neither may be
traded for the migration, nor the migration for them: all of it is required.
The behaviour must hold on the migrated build, though, and only there: behaviour
a tree preserved by never leaving Spark 2.4 is not behaviour it preserved. The
behaviour is defined at one fixed `spark-submit` line, because what the programs
print depends on it; that line, the command that runs all 47 test classes, the
thresholds your jar must meet and the runtime it must run on are in the handoff
document named below.

## 2. Read these local references

The requirements on the build apply to the POMs alone: anything under
`.mvn/` (`jvm.config`, `maven.config`, `extensions.xml`) or in a `.mavenrc`
does not count toward meeting them.

**Normative:** `/app/WORKSPACE_HANDOFF.md` — the workspace, the build and run
commands, the behaviour contract, and each of the six migration requirements
stated in full. It is the definition of this step; read it first.

**Additional:** `/app/README.md` and `/app/docs/` for what the programs do and
how they are invoked; `/app/pom.xml` for the build as it stands; upstream's
"Migration Guide: Spark Core" and "Migration Guide: SQL, Datasets and
DataFrame" for 2.4 → 3.0, the Spark 3.0.3 release notes, and the Scala 2.12
migration notes; and `archive.apache.org` for the
`spark-3.0.3-bin-hadoop3.2` distribution if you want that runtime on this box
to try your jar against.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. `pom.xml` and the Java sources are
  yours to edit — that is the step — but the tree you leave behind is still
  the same project: all 230 `.java` sources are still there, the supplied
  documents and handoff files are still there, and nothing gains a prose answer
  or an alternate entry point. The jar `mvn -B clean package -DskipTests`
  produces is the artifact, and it must stay the single shaded uber jar
  matching `target/mmtf-spark-*.jar`.
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
