# Step 3 — land on Spark 3.2.4, and name the statistics this tree prints

## 1. Implement this milestone

`/app` is mmtf-spark building against Spark 3.0.3 for Scala 2.12 on JDK 8.
Move the build two releases forward, to **Spark 3.2.4** for Scala 2.12, and fix
the one dependence that move exposes: twelve places where this library prints a
statistic set that the engine chose rather than one the program named.

Nothing in this tree stops compiling across that fence — that was measured
before the step was written, over every program under `edu.sdsc.mmtf.spark` and
every public API call the behaviour contract covers, and not one of them failed
to build or crashed at either rung. So the version move is easy, and it is not the work. What moves is
semantics: `Dataset.describe(cols…)` is documented as exploratory, and the
five statistics it prints are the engine's choice and free to change. This tree
prints that table twelve times as program output, so what those twelve tables
contain is currently a decision this library has left to whichever engine it is
run on. Replace the dependence with a statistic set the program states.

When you are done:

* **the build resolves Spark 3.2.4, and so does the session.** Every
  `org.apache.spark` artifact the build resolves is at 3.2.4 for Scala 2.12 and
  no `org.apache.spark` artifact resolves at any other version; and the engine
  each job actually runs in reports `spark.version` 3.2.4, with none of them
  reporting anything else. Both halves must hold, and they are not the same
  fact: `spark-core` is a `provided` dependency, so the jar does not carry
  Spark and a build that still resolves 3.0.3 will run perfectly well on a
  3.2.4 installation. The pom is the half you control, and it must resolve
  3.2.4 itself;
* **no call to `Dataset.describe(…)` remains** under `edu.sdsc.mmtf.spark`, in
  the sources or in the built jar, and **each** of the two classes that made
  those calls — `interactions.demos.AnalyzeWaterInteractions4`, with eleven of
  the twelve sites, and `datasets.demos.PdbLigandDemo` with the twelfth — calls
  an API that takes the statistic list instead: `Dataset.summary(String…)` or
  `Dataset.agg(…)`. Both classes, not one. Whichever of the two you use, the
  requirement below still holds: `functions.stddev(col(…))` names the statistic
  in your code and leaves no `stddev` anywhere in the artifact, so an `agg` that
  spells its statistics as method calls has not met it;
* **the statistic names are in the built jar as text.** The point of the step
  is that the program states what it prints, so the names have to be in the
  artifact as string constants rather than implied by the method you called.
  `stddev` in particular must be there;
* **the twelve tables still print what they print today.** Naming describe's
  own five statistics, in describe's own order, is what keeps them identical,
  and that is the intent: this is the removal of a dependence on someone else's
  choice, not an opportunity to print better statistics. `describe(…)` and
  `summary(…)` do not take the same view of the frame they are called on, so
  reproducing a table byte for byte is a question about the frame as much as
  about the statistics;
* **nothing in the tree, in the built jar or in any session reaches for a
  legacy switch.** No `spark.sql.legacy.*` property is set — anywhere, however
  spelled, and wherever set: in code, in `spark-defaults.conf` or on the submit
  line. Those switches ask a new engine to answer the way an old one did, which
  is the opposite of this step;
  `spark.sql.legacy.statisticalAggregate` and
  `spark.sql.legacy.castComplexTypesToString.enabled` are the two that would
  undo output changes 3.1 made, and they are named in the handoff along with
  what those changes are. Neither the whole property name nor the leaf of it
  may appear in the jar, so a name assembled at run time still leaves the piece
  behind, and the rule holds for the effective value of those properties in
  each running session, which does not care how the name was spelled;
* **adaptive query execution is left switched on, and left able to act.** It
  is Spark 3.2's default; a tree that sets `spark.sql.adaptive.enabled` false,
  or that switches off the sub-settings which do the re-planning, is telling
  the 3.2 engine to plan like a 3.0 one. So is one that hand-sets any of those
  sub-settings to anything else — an advisory partition size or a minimum
  partition count that leaves nothing to coalesce starves the optimiser as
  surely as switching it off — or that names optimiser rules to exclude.
  Saying `true` out loud is not a change; any other value on any of these keys
  is. This holds three ways: for what a session was explicitly asked to set,
  wherever it was asked — in code, in `spark-defaults.conf` or on the submit
  line — for the setting each session then reports, and for the optimiser, which
  must re-plan at least one query across all of the runs. The setting is a fact
  about the version and only the re-plan is a fact about the run. It changes
  nothing this corpus prints — also measured — so leaving all of it alone costs
  nothing;
* **nothing in the tree, and nothing in the built jar, undoes the step before
  this one or reaches for the record instead of the result.** No
  `Encoders.kryo`, no `Encoders.javaSerialization`, no `setLocalProperty`, and
  no jar naming `spark.extraListeners`, the listener class installed through
  it, or any of the environment variables set for that listener on the
  `spark-submit` line, and no class of your own in that listener's package,
  which is the strongest form of naming it: that listener, attached on the
  `spark-submit` line, records what each session was configured with and what
  its optimiser did, and a jar that reaches for it has replaced the record
  rather than made it true. So has a tree that arranges for the listener's
  reading of one of the settings above to fail rather than answer, or for one of
  these programs to stop reaching a session at all: a setting that was never
  read is not thereby a compliant one. A reflective call is still that
  call — none of these names may appear in the sources, in the jar's constant
  pool or in the run — and the prohibition is wider than these names: nothing this
  build compiles may call into `java.lang.reflect` at all, or look a member or
  a type up by name through `java.lang.Class` — `forName`, `getMethod`,
  `getDeclaredMethod`, `getConstructor`, `getDeclaredConstructor` and their
  plural siblings. `getSimpleName`, which 124 of this tree's files already use,
  is not one of them, and nothing here reflects today.

The legacy switches, the adaptive switch, the opaque encoders, the hand-set
execution id and the reach for the listener are each a faked step rather than a
partial one, and each is forbidden outright. None of those names appears in the
repository's original 230 sources, so the change itself never needs them —
but check the tree you are handed rather than assume it is clean: one
that arrived already carrying, say, a `spark.sql.legacy.*` setting is in breach
exactly as if you had added it.

Behaviour that must still hold: every program under `edu.sdsc.mmtf.spark`, and
this project's public API called directly from outside it, still exit with the
same status and print the same thing to stdout — the error and `--help` paths
included — and all 47 of the repository's own JUnit test classes, 142 tests,
still pass. **"The same" means what these programs print on Spark 3.2.4**, not
what this tree prints today at 3.0.3: two of these programs print a different
*value* on 3.2.4 than they print here, and the handoff names both, which Spark
change moved each one, and why neither is yours to undo. Other lines move across
a version fence without the program having changed its answer — §2 of the
handoff says which kinds of difference the behaviour contract sets aside, and
reordering and re-jitter are among them — and a changed value is not one of
those. Neither behaviour nor the migration may be traded for the other: both
are required. The behaviour must hold on a tree that made this move, though: a
tree that changed nothing preserves its output trivially and has not done this
step. The behaviour is defined at one fixed `spark-submit` line, because what
the programs print depends on it; that line, the command that runs all 47 test
classes, and the thresholds your jar must meet are in the handoff document named below.

## 2. Read these local references

The requirements on the build apply to the POMs alone: anything under
`.mvn/` (`jvm.config`, `maven.config`, `extensions.xml`) or in a `.mavenrc`
does not count toward meeting them.

**Normative:** `/app/WORKSPACE_HANDOFF.md` — the workspace, the build and run
commands, the behaviour contract with the two output changes this fence brings
and what they must not be answered with, both migration requirements stated in
full, and the runtime this step targets. It is the definition of this step;
read it first.

**Additional:** `/app/README.md` and `/app/docs/` for what the programs do;
`/app/HANDOFF-NOTES.md` for what the work before you had to say about this
tree; upstream's Spark SQL API documentation for `Dataset.describe`,
`Dataset.summary` and `Dataset.agg` — `describe` documents its own statistic
set as subject to change and points at `summary` for a stated one; and the
Spark 3.2.0 and 3.1.0 release notes for what changed across this fence,
including the adaptive-execution default (SPARK-33679) and the
statistical-aggregate rendering (SPARK-13860). The 3.2.4 distribution is served
by `archive.apache.org` and is reachable from this box if you want that runtime
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
  returning `JavaPairRDD<String, StructureDataInterface>`, the resolved graph
  is still Scala 2.12, `com.databricks:spark-xml` stays gone, the jar stays
  clear of `org.spark_project.`, `org.sparkproject.` and `Word2VecModelReader`
  while `ProteinSequenceEncoder` still loads a model through `Word2VecModel`'s
  public loader, plugin versions still come from this pom, nothing the build
  resolves can move, and two clean builds still agree byte for byte. None of it
  may be undone. The one thing that does move is
  the Spark version itself: the previous step's 3.0.3 fence is replaced by this
  step's 3.2.4 fence, and moving off 3.0.3 is not undoing it.
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
