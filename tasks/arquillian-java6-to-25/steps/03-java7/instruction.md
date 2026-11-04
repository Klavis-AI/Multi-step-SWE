# Step 3 — make it a Java 7 tree

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, running on Java 8,
compiling to Java 6 bytecode, with the project's own test suite on JUnit 5.

The language it is written in has not moved since 2011. It is Java 6 source: explicit type
arguments on every constructor call, resources closed by hand in `finally` blocks, and
`java.io.File` everywhere a path is handled. Java 7 gave all three of those a better spelling,
and this step adopts them.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and
everything it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to
   make this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still
     in the tree, and the build still runs with Java 8 as the only JVM available;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module
     jars, twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types,
     methods, constructors and fields, with the same signatures. That holds for signatures
     that mention `java.io.File` too: a parameter or a return type is part of the surface, and
     requirement 6 is about what the code does inside;
   - every test class and test method that exists today still exists, and no more of them are
     switched off than are switched off today;
   - the suite still runs as `./gradlew --continue test`, every test case reported today is
     still reported in the XML results that run writes, and nothing that passes today may fail
     afterwards;
   - that run leaves one compiled copy of each inherited test class in the tree — wherever
     your build writes its output — and no second copy somewhere else. The library's sources
     also stay where they are today: main code under `<module>/src/main/java`, test code under
     `<module>/src/test/java`;
   - each of those test classes also stands being run on its own — one class, a runner for the
     framework that class is written in, this project's own compiled output and its own
     dependencies on the classpath, the test libraries among them at JUnit Jupiter **5.14.4** on JUnit Platform
     **1.14.4** and Mockito **4.11.0** — and reaches that runner's verdict: the class loads, its
     cases run, the runner reports — and in seconds, as today: a class that needs minutes where the
     suite needs seconds, or classes that together turn a re-run of seconds into a quarter of an
     hour, have not run;
   - the project's own 49 migrated test classes are still Jupiter tests and nothing else, and
     still free of `org.junit.*` and `junit.framework.*`. The six compatibility classes that
     are *about* JUnit 4 keep their JUnit 4, exactly as before;
   - nothing compiled from this project — main code or test code, in the tree or in the jars
     it publishes — calls `Runtime.halt`, `Runtime.exit`, `System.exit` or `sun.misc.Unsafe`,
     whether by name, by reflection, or through a native library of its own, and nothing this
     step asks for needs to add one;
   - every compiled class the build leaves behind is one a class-file reader can open;
   - the test that asserts on the order in which `Class.getMethods()` returns a class's methods
     comes with the code you inherit. The JVM does not specify that order, so it may land
     either way here. You are not required to make it deterministic — and you must not delete
     it or switch it off either;
   - runtime behaviour of the published artifacts is unchanged — same service registrations,
     same wiring, same exception types and messages, same ordering.

2. **The bytecode is Java 7.** Every class compiled from the library's own sources is
   **class-file major version 51** — main and test alike, in the tree and inside the published
   jars — and there is only one major version present. This is the one inherited expectation
   that changes: the tree you are given is major 50 everywhere, and a tree that leaves some
   classes at 50 and takes others to 51 is a half-migrated tree, not a migrated one. (Build
   logic that drives Gradle is not part of the library.)

3. **The tree requires Java 7 to compile.** Not merely *permits* it: the sources you leave
   have to be sources that a Java 6 compiler rejects, and every complaint it makes has to be
   about the language level itself rather than something else that happens to fail. A tree
   that declares 1.7 in its build script and still compiles clean as Java 6 source has changed
   a number, not a codebase.

   How thoroughly it is rejected is part of this, not a side effect of it: a Java 6 compiler
   should object to the work requirements 4, 5 and 6 ask for, across the tree, rather than to
   a single construct dropped in to make the point. One diamond in one file is a tree that
   still is a Java 6 codebase, with one line that is not.

4. **The diamond operator, where Java 7 can infer it.** The tree makes **143** constructor
   calls that spell out their type arguments — `new HashMap<String, String>()`. **27** of them
   are on anonymous class bodies, and `<>` on one of those is a Java **9** construct, so those
   27 stay exactly as they are. Of the **116** that remain, **93** — spread across **47** of
   the tree's files — can take `<>` under Java 7's inference and must. The other 23 cannot:
   Java 7 infers a constructor's type arguments from the assignment target alone, so a
   `new HashMap<>()` handed straight to a parameter of type `Map<String, String>` infers
   `HashMap<Object, Object>` and does not compile. You do not have to guess which 23 — the
   compiler names every one of them, by file and by line.

   This has to be a conversion and not an addition: 93 sites that carried explicit type
   arguments stop carrying them, and 93 sites carry `<>`. Deleting the generic code moves the
   first half and not the second, and writing 93 new diamond sites alongside the 143 old ones
   moves the second and not the first. Neither is the migration.

5. **Resources are acquired in a `try` header, not closed by hand.** **11** resources in this
   tree are closed by an explicit `close()` call, and all 11 of those calls go: **9** `try`
   statements acquire their resources in a resource clause instead, and close them for you, in
   reverse order, even when the body throws. Deleting a `close()` call is not moving it — a
   deleted close is a leaked handle. And none come back: a tree that retires these 11 and
   hand-closes something new elsewhere has the same count it started with, which is the same
   tree it started with.

   Note that several of these are the reason to do it at all: the pattern in this tree is to
   open the resource *before* the `try`, so a constructor that throws in between leaks it.

6. **Paths are `java.nio.file`, where that preserves behaviour.** The tree reaches the
   filesystem through `java.io` at **13** sites — 8 constructions and 5 declarations, of
   `java.io.File` itself and of the four stream and reader types that are built from one:
   `FileInputStream`, `FileOutputStream`, `FileReader`, `FileWriter`. **11** of them move to
   `java.nio.file.Path`, `Paths` and `Files`.

   **Two do not, and finding which two is part of this step.** `java.nio.file` is not a
   drop-in replacement for `java.io.File`; for some of what this tree does through these sites it
   throws a different exception with a different message, and requirement 1 — which also freezes
   the published signatures — does not allow either to change.
   Nothing marks them — you find them the way you would find them in your own codebase: by
   running what you changed and comparing it against what it did before. A tree that migrated
   every `java.io` path it could reach is not being thorough here; it is requirement 1 broken.

   Where the code hands a path to a third-party API that still takes a `File` — ShrinkWrap's
   exporters, for instance — meeting it at the boundary is the migration, not a failure of it.

7. **None of this is done with raw types or with suppressions.** `new HashMap()` is not
   `new HashMap<>()`: both drop the explicit type arguments and both compile, but one is a
   diamond and the other is a raw type that erases the element types and compiles with a
   warning. The tree has **no** raw constructor calls today and must still have none. It
   carries **44** `@SuppressWarnings` sites today, and nothing this step asks for needs a
   forty-fifth. A suppression added to quiet what the migration broke leaves it broken and
   hidden, which is worse than leaving it broken.

## Notes

- This step does not raise the JDK. The build still runs on Java 8, and Java 8 is what
  compiles Java 7 source here. Nothing in this step needs a library that Java 8 cannot load,
  and no dependency version has to move.
- Java 8's own conveniences are not available to you: the source level is 7, so there are no
  lambdas, no method references and no default methods. `<>` on an anonymous class body is
  Java 9 and is likewise out of reach. A construct that compiles under Java 8's default
  settings is not automatically one this step can use.
- Java 7's inference is weaker than Java 8's in exactly one way that matters here, and it is
  the one described in requirement 4. Expect to compile, read the errors, and put the type
  arguments back where the compiler asks for them.
- `Files.exists`, `Files.createTempFile` and `Files.newOutputStream` are not silent
  replacements for `File.exists`, `File.createTempFile` and `new FileOutputStream`. They
  differ in what they throw, what they accept, and what they say when they refuse. The
  tree's own tests will tell you about some of that; requirement 1 is what tells you about
  the rest.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
