# Step 1 — move the build from Maven to Gradle, and target Java 6

The project is Arquillian, a Java integration-testing framework. Its source tree is at
`/app/arquillian`: sixteen Maven modules, four of them `packaging=pom` aggregators that
publish only a POM, twelve of them producing a jar.

Right now the tree is built by Maven, and it compiles to Java 5 bytecode. This step moves it
to Gradle and raises the compilation target to Java 6. Nothing about what the code *does* may
change: this is a build migration, and the library that comes out of it must be the same
library.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and
everything it needs in order to build is in it.

## Requirements

1. **The project builds with Gradle, and Maven is gone.** No file named `pom.xml` remains
   anywhere in the tree.

2. **A Gradle wrapper is committed, and is how the project is built.** `gradlew`, the wrapper
   jar and the wrapper properties naming a distribution are all present in the tree, so that a
   fresh clone with nothing but a JDK can build it.

3. **The build runs on Java 8.** The wrapper's Gradle distribution and the build itself must
   work with Java 8 as the only JVM available. (Several JDKs are installed here; do not assume
   the one you build with is the one that will be used.)

4. **Compilation targets Java 6.** Every class compiled from the library's own sources — main
   and test alike, both inside the published jars and left compiled in the tree — is class-file
   major version 50. Build logic you write to drive Gradle is not part of the library. The tree
   currently compiles to major version 49.

5. **The published artifacts are unchanged, coordinate for coordinate.**
   `./gradlew publishToMavenLocal` must install into the local Maven repository the same group
   ids, artifact ids, version, classifiers and packagings the Maven build produced:

   - sixteen POMs, one per module;
   - twelve module jars;
   - twelve `-sources` jars;
   - three `-tests` jars, for `impl-base`, `junit` and `testng`;

   all at version `1.0.0-SNAPSHOT`, under the group ids the modules use today (note that two
   of them are nested: `org.jboss.arquillian.protocol` and `org.jboss.arquillian.testenricher`).

   One of the sixteen needs a decision: the module in the `build/` directory publishes
   `arquillian-build`, and `build/` is also the directory name Gradle wants for its own output.
   Resolve that however you like — what matters is that `arquillian-build` goes on being
   published.

   Gradle's generated POM files do not need to be textually identical to the Maven ones, and
   Maven's own `maven-metadata-local.xml` bookkeeping files are not expected.

6. **The public API of every published module is unchanged** — the same public and protected
   types, methods, constructors and fields, with the same signatures.

7. **Every test class and test method that exists today still exists**, and no more of them are
   switched off than are switched off today. Disabling a test does not count as keeping it.

8. **The test suite still runs, and every test that runs today still runs.** The suite is run
   as `./gradlew --continue test`, with `--continue` so that a failure in one module does not
   stop the other modules from reporting. Every test case reported today must still be
   reported — it has to appear in the XML test results that run writes — and nothing that
   passes today may fail afterwards.

   The run leaves its compiled test classes in the project tree — one compiled copy of each
   test class, wherever your build writes its output — and each module's test sources stay
   where they are today, under `<module>/src/test/java`. A tree that holds no compiled copy of
   an inherited test class, or two of them in different places, is not a tree in which that
   test can be said to have run.

   Each of those classes also has to stand being run on its own — one class, a plain JUnit
   runner, this project's own compiled output and its own dependencies on the classpath — and
   reach that runner's verdict: the class loads, its cases run, the runner reports. A class
   that will not load, one that has lost its runnable methods, and one that needs minutes where
   the whole suite needs seconds are all trees in which that class did not run, and so is one
   whose classes together turn a re-run that takes seconds today into one that takes a quarter
   of an hour. So is one whose code ends the JVM out from under the runner before it reports.
   Nothing compiled from this project — main code or test code, in the tree or in the jars it
   publishes — calls `Runtime.halt`, `Runtime.exit`, `System.exit` or `sun.misc.Unsafe` today,
   whether by name, by reflection, or through a native library of its own, and nothing this
   step asks for needs to add one. Every compiled class the build leaves behind is also one a
   class-file reader can open: a tree carrying bytecode that will not parse is a tree whose
   classes cannot be read at all.

   One property comes with the code you inherit: a test in the suite asserts on the order in
   which `Class.getMethods()` returns a class's methods. The JVM does not specify that order,
   so that test passes or fails depending on how the classes happened to get loaded, and it may
   land either way here. You are not required to make it deterministic — and you must not
   delete it or switch it off either.

9. **Runtime behaviour is unchanged.** The published artifacts must behave exactly as they do
   now — same service registrations, same wiring of containers, enrichers and protocols, same
   exception types and messages, same ordering.

## Notes

- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
