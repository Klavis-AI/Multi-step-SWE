# Step 4 — make it a Java 8 tree

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, running on Java 8,
compiling to Java 7 bytecode, with the project's own test suite on JUnit 5 and its constructor
calls, resource handling and path handling already written the Java 7 way.

What has not moved is the shape of the code. Behaviour that belongs to a call site is still
written as an anonymous class with one method in it; every traversal is still a hand-written
loop, even the ones that do nothing but read a collection and build another one. Java 8 gave
both of those a better spelling, and this step adopts them.

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
     methods, constructors and fields, with the same signatures. A body that becomes a lambda
     is a body; a method that changes its parameter or return type is the API, and this step
     needs none of those to change;
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

2. **The previous step's own work stays done, and this step's work does not come at its expense.**
   Three constructs arrived one step ago and all three are still expected:

   - **93** constructor calls carry `<>` rather than spelling out their type arguments, across
     47 files, and the explicit forms do not come back;
   - **9** `try` statements acquire their resources in a resource clause; **no** resource in
     this tree is closed by a hand-written `close()` call, and none comes back;
   - **11** filesystem sites are `java.nio.file` — `Path`, `Paths`, `Files` — and the **two**
     that are still `java.io` are still `java.io`, for the reason the previous step gave: on
     those two, `java.nio.file` throws a different exception with a different message, and
     requirement 1 does not allow that to change. Finding which two was part of that step;
     leaving them alone is part of this one. Counted the way that step counted its 13:
     constructions of **and** variables declared at `java.io.File`, `FileInputStream`,
     `FileOutputStream`, `FileReader` and `FileWriter`. A declaration counts even when its
     initialiser is a static factory rather than a `new`, so `File f = File.createTempFile(…)`
     is one of the two and a census built only out of `new` will not find it. The declared type
     of a resource in a `try` clause counts the same way: a tree with step 3's work done acquires
     its one remaining `FileOutputStream` through a variable declared at `OutputStream`, and
     narrowing that declaration to `FileOutputStream` puts a third `java.io` declaration back;

   **One honest exception, and only one.** An anonymous class body can carry explicit type
   arguments; a lambda cannot. **14** of the tree's remaining explicit-type-argument sites sit
   on anonymous bodies that requirement 5 asks you to convert, and those 14 go with them —
   that is the conversion, not a regression of it. What is counted here is the **23** sites
   that are *not* on an anonymous body, and 23 is what the tree has today and must still have.

3. **The bytecode is Java 8.** Every class compiled from the library's own sources is
   **class-file major version 52** — main and test alike, in the tree and inside the published
   jars — and there is only one major version present. This is the one inherited expectation
   that changes: the tree you are given is major 51 everywhere, and a tree that leaves some
   classes at 51 and takes others to 52 is a half-migrated tree, not a migrated one. (Build
   logic that drives Gradle is not part of the library.)

4. **The tree requires Java 8 to compile.** Not merely *permits* it: the sources you leave have
   to be sources that a Java 7 compiler rejects, and every complaint it makes has to be about
   the language level itself rather than something else that happens to fail. A tree that
   declares 1.8 in its build script and still compiles clean as Java 7 source has changed a
   number, not a codebase.

   How thoroughly it is rejected is part of this, not a side effect of it: a Java 7 compiler
   should object to the work requirements 5, 6 and 7 ask for, across the tree, rather than to
   a single construct dropped in to make the point. One lambda in one file is a tree that still
   is a Java 7 codebase, with one line that is not.

5. **Lambdas and method references where the anonymous single-method bodies are.** The tree
   creates **48** anonymous class bodies that implement an interface with exactly one abstract
   method. **36** of them have a lambda form and must take it, spread across **20** of the
   tree's files. The other **12** do not have one and stay exactly as they are: eleven declare
   a field or a second method of their own, and one refers to itself with `this`. You do not
   have to guess which — a body with anything in it but the one method it implements is not a
   lambda, and the compiler agrees.

   A method reference is accepted anywhere a lambda is: `this::handle` and `s -> handle(s)`
   are the same conversion as far as this step is concerned, and the clearer of the two at each
   site is the right one.

   This has to be a conversion and not an addition. Thirty-six anonymous bodies stop existing,
   and the twenty files that held them hold a lambda or a method reference instead. Deleting
   the code moves the first half and not the second; writing thirty-six lambdas into a file of
   their own moves the second and not the first. Neither is the migration.

6. **The interfaces that now take lambdas say so.** The tree declares **65** interfaces, and
   **24** of them have exactly one abstract method — they are what the anonymous bodies of
   requirement 5, and the pipelines of requirement 7, are written against. All **24** carry
   `@FunctionalInterface`.

   That annotation is a contract, not a decoration: it is what makes the compiler refuse a
   later change that adds a second abstract method and silently breaks every call site. Put it
   on all 24 and on nothing else — `@FunctionalInterface` on a type that is not one is a
   compile error, so the compiler will tell you if you have miscounted in that direction, and
   only in that one.

7. **Streams where the collecting loops are.** The tree contains **114** enhanced `for`
   statements. **24** of them do one thing: walk a collection and put some or all of what they
   find into another collection. Those 24 become stream pipelines, across **14** of the tree's
   files.

   The other **90** stay loops, and leaving them alone is correct. A `for` whose body does two
   things, or whose body puts a key and a value into a map, or that has to catch something,
   has no stream form clearer than the loop it already is — rewriting those is not what this
   step asks for.

   The same "conversion, not addition" rule applies, at the place where the loop was: the
   24 loops go, and the **24 methods** that held them — spread across those 14 files — hold a
   pipeline instead. A pipeline belongs to the outermost method it is written in, so
   turning an anonymous class around it into a lambda, as requirement 5 asks, leaves it where
   it was. A pipeline written in some other method while the loops stay is not the migration,
   and neither is deleting the loops.

   Nor is respelling them. The tree iterates in **139** places altogether — the 114 enhanced
   `for` statements, 15 indexed `for`s, 10 `while`s and no `do` — and a tree that has done this
   step iterates in **115**. Writing one of the 24 as a `while` or as an indexed `for` leaves
   that 139 where it is, and is not the conversion. And a pipeline is a stream with something done
   to it: a bare `xs.stream()` whose result goes nowhere is not one.

8. **None of this is done with raw types or with suppressions.** `new HashMap()` is not
   `new HashMap<>()`: both drop the explicit type arguments and both compile, but one is a
   diamond and the other is a raw type that erases the element types and compiles with a
   warning. The tree has **no** raw constructor calls today and must still have none. It
   carries **44** `@SuppressWarnings` sites today, and nothing this step asks for needs a
   forty-fifth. A stream over a collection whose element type was inferred loosely is exactly
   where an unchecked cast gets papered over; a suppression added to quiet what the migration
   broke leaves it broken and hidden, which is worse than leaving it broken.

## Notes

- This step does not raise the JDK. The build still runs on Java 8; what changes is that the
  source level catches up with it. Nothing in this step needs a library that Java 8 cannot
  load, and no dependency version has to move.
- Java 8's own conveniences are now available to you, and the ones after it still are not.
  `var` is Java 10, collection factories are Java 9, and `<>` on an anonymous class body is
  Java 9 — the tree's 27 explicit type-argument sites on anonymous class bodies are not an
  invitation to reach for it.
- A lambda is not a shorter anonymous class. It has no `this` of its own, it cannot declare
  fields, and it captures effectively-final locals only. Requirement 5's twelve exclusions are
  those rules, not a judgement call.
- The suite's `assertThrows` calls take an `Executable`, which is a functional interface like
  any other; converting those bodies is fine and changes nothing about requirement 1's promise
  that the 49 classes are still Jupiter tests.
- A loop and a stream do not fail the same way. A loop body may throw a checked exception; a
  `Consumer` may not. A loop may add at an index; a collector may not. Where the loop is doing
  something a pipeline cannot express, it is one of the 90.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
