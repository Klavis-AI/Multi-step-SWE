# Step 2 — move the project's own test suite to JUnit 5

You are picking up the tree the previous step left at `/app/arquillian`: the same Arquillian
integration-testing framework, now built by Gradle through a committed wrapper, running on
Java 8 and compiling to Java 6 bytecode.

Its tests have not moved. The project's own suite is written against JUnit 4.8.1, with
Mockito 1.8.3 driven by JUnit 4's `@RunWith` runner, and TestNG 5.14.6 in one module. This
step moves the tests **this project writes** onto the JUnit Platform, and leaves the library
those tests are about exactly as it is.

That distinction is the whole of this step, and it is not a formality here. Arquillian exists
in order to drive other people's test frameworks: `arquillian-junit` publishes a class that
*is* a `org.junit.runner.Runner`, and `arquillian-testng` publishes one that implements
`org.testng.IHookable`. Those published types, and the frameworks they integrate with, do not
move. What moves is the framework the project's own test classes are written in.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and
everything it needs in order to build is in it.

## Requirements

1. **Everything the previous step established still holds.** None of it may be given up to
   make this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still
     in the tree, and the build still runs with Java 8 as the only JVM available;
   - **every class compiled from the library's own sources is still class-file major version
     50** — main and test alike, in the tree and inside the published jars. Test code you
     rewrite in this step is library source like any other, so whatever you write has to
     compile at that level. (Build logic that drives Gradle is not part of the library.)
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module
     jars, twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types,
     methods, constructors and fields, with the same signatures;
   - every test class and test method that exists today still exists, and no more of them are
     switched off than are switched off today;
   - the suite still runs as `./gradlew --continue test`, every test case reported today is
     still reported in the XML results that run writes, and nothing that passes today may fail
     afterwards;
   - that run leaves one compiled copy of each inherited test class in the tree — wherever
     your build writes its output — and no second copy somewhere else, and each module's test
     sources stay where they are today, under `<module>/src/test/java`. A tree that holds no
     compiled copy of an inherited test class, or two of them in different places, is not a
     tree in which that test can be said to have run;
   - each of those classes also stands being run on its own — one class, a runner for the
     framework that class is written in, this project's own compiled output and its own
     dependencies on the classpath, the test libraries among them at JUnit Jupiter **5.14.4** on JUnit Platform
     **1.14.4** and Mockito **4.11.0** — and reaches that runner's verdict: the class loads, its
     cases run, the runner reports. A class that will not load, one that has lost its runnable
     methods, and one that needs minutes where the whole suite needs seconds are all trees in
     which that class did not run, and so is one whose classes together turn a re-run that
     takes seconds today into one that takes a quarter of an hour;
   - nothing compiled from this project — main code or test code, in the tree or in the jars
     it publishes — calls `Runtime.halt`, `Runtime.exit`, `System.exit` or `sun.misc.Unsafe`,
     whether by name, by reflection, or through a native library of its own, and nothing this
     step asks for needs to add one;
   - every compiled class the build leaves behind is one a class-file reader can open: a tree
     carrying bytecode that will not parse is a tree whose classes cannot be read at all;
   - one property comes with the code you inherit: a test in the suite asserts on the order in
     which `Class.getMethods()` returns a class's methods. The JVM does not specify that
     order, so that test passes or fails depending on how the classes happened to get loaded,
     and it may land either way here. You are not required to make it deterministic — and you
     must not delete it or switch it off either;
   - runtime behaviour of the published artifacts is unchanged — same service registrations,
     same wiring, same exception types and messages, same ordering.

2. **The project's own test classes are JUnit 5 tests, and only that.** There are **55** test
   classes in the tree today. Six of them are the compatibility surface described in
   requirement 7; each of the other **49** has to come out of this step as a Jupiter test and
   nothing else — its `@Test`, its lifecycle annotations and its disabling annotation all from
   `org.junit.jupiter.api`, and no JUnit 4 or TestNG test annotation left on it. A class that
   carries both frameworks is half migrated, and half migrated is not migrated.

3. **The retired JUnit 4 types are gone from those 49 classes.** Not just the annotations: no
   reference of any kind — annotation, imported type, the owner of a call, a constant — to
   `org.junit.*` or `junit.framework.*` may remain in what those classes compile to. Moving
   the annotations across while every assertion still calls `org.junit.Assert` is not the
   migration this asks for.

4. **`@Test(expected = …)` becomes `assertThrows`.** Jupiter has no `expected` attribute.
   Nineteen of the twenty `@Test(expected = …)` methods in the tree are on must-migrate
   classes, and all nineteen have to lose the attribute *and* the class that held each one has
   to gain the `assertThrows` call that replaces it. The twentieth is inside the compatibility
   surface and stays. Both halves have to be true at once and in the same class: deleting such
   a method is not retiring its `expected` attribute — requirement 1 keeps every test method
   — and nineteen `assertThrows` calls added elsewhere are not the calls that replace these
   nineteen.

5. **`@RunWith` is gone from those classes.** A JUnit 4 runner has no Jupiter spelling — a
   runner becomes an extension. Sixteen of the seventeen `@RunWith` sites in the tree are on
   must-migrate classes and every one of them names Mockito's JUnit 4 runner; all sixteen have
   to go. The seventeenth is inside the compatibility surface and stays.

6. **The assertions survive the rewrite.** These are totals over the whole tree rather than
   class by class, so an assertion is free to move between classes: the tree today makes
   **272** assertion calls, **155** of which pass a message, and neither total may end up
   lower. Jupiter's share of the assertion calls also has to be at least the legacy share —
   keeping the totals up while the assertions themselves stay on JUnit 4 is not moving them. A
   `@Test` method that asserts nothing satisfies every requirement above and fails this one.

7. **The compatibility surface keeps its JUnit 4.** Six of the 55 inherited test classes are
   *about* JUnit 4 rather than merely written in it, and requirements 2 to 6 do not apply to
   them:

   - the ones that construct `org.junit.runner.Runner` instances, run a fixture class through
     them and assert on the `org.junit.runner.Result` that comes back — deleting those
     references would delete the thing under test;
   - the fixture classes that such a runner is pointed at. A Jupiter `@Test` is not a runnable
     method as far as a JUnit 4 runner is concerned, so converting one of these silently
     empties the test that drives it;
   - the `arquillian-testng` module's own two test classes, for the same reason on the other
     framework.

   By name they are `JUnitIntegrationTestCase`, the fixture class nested in it, and
   `JUnitTestRunnerTestCase` in `arquillian-junit`; the JMX protocol's `DummyTestCase`; and
   `TestNGDeploymentAppenderTestCase` and `TestNGTestRunnerTestCase` in `arquillian-testng`.

   You are not required to leave those six untouched — an outside-in test that drives a JUnit
   4 runner can perfectly well be a Jupiter test itself — but you are not required to move
   them either, and what they name is allowed.

   Whichever way you take them, those six go on running, and their test cases go on being
   reported in the XML results of the same `./gradlew --continue test` run as everything
   else. A class you leave written against JUnit 4 has to be discovered and executed by that
   one run too, so putting the project's test task on `useJUnitPlatform()` and stopping there
   is not enough: requirement 1 does not allow a case that is reported today to go missing.

8. **`arquillian-testng` keeps running its tests with TestNG.** The JUnit Platform's TestNG
   engine needs TestNG 7, and TestNG 7 renamed internals that this module's *product* code
   compiles against (`org.testng.internal.AnnotationTypeEnum` is gone, and `ITestNGMethod`'s
   reflective handle moved behind `getConstructorOrMethod()`). Bumping that library is a
   third-party dependency migration, not a test-framework one, and it is not what this step is
   about. That module goes on running its suite with `useTestNG()`, and its TestNG test methods
   go on being reported in the XML results that `./gradlew --continue test` writes.

## Notes

- This step does not raise the JDK. The build still runs on Java 8, which puts a ceiling on
  the libraries you can use: JUnit Jupiter 6 requires Java 17 and Mockito 5 requires Java 11,
  and neither of those JDKs is the one this build runs on. The newest releases that do run
  here are Jupiter **5.14.4** with JUnit Platform **1.14.4**, and Mockito **4.11.0**.
- Holding the Java 6 target while the tests depend on libraries built for Java 8 is not a
  straight dependency bump. Gradle matches the variants a dependency publishes against the
  target JVM version it infers from the build, and the library versions above declare Java 8,
  so a build that still says Java 6 everywhere is refused them when it resolves its test
  dependencies, before any of this step's code is compiled. Both sides of that are
  requirements here — class-file major version 50 from requirement 1, and the libraries
  requirements 2 to 6 make the tests depend on — so the build has to be arranged to hold the
  pair at once.
- The language level that comes with a Java 6 target has no lambdas and no method references.
  `assertThrows` takes a block of code to run, and those two are the usual ways of handing a
  method one, so at this source level the block has to be handed over some other way.
- Everything a JUnit 4 test can express has a Jupiter equivalent, but not always the same
  shape: the argument order of the assertion methods is not the same, and the annotation that
  disables a test is not called the same thing. Silent behaviour changes here are still
  behaviour changes.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
