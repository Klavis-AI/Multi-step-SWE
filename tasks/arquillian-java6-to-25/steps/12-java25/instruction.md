# Step 12 — Java 25: module imports, a scoped value, and the header layout the JVM is asked for

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, talking to its container through `java.net.http`, with the two Jakarta EE
packages it could move moved, with every published jar naming the module it intends to be and
finding its providers through `java.util.ServiceLoader`, with its casts folded into `instanceof`
tests, its event vocabulary sealed, dispatching through pattern `switch`es and reading first
elements by name, and — from the step just finished — free of the security manager's API,
declaring the `catch` parameters it never reads as `_`, and reaching four private fields through a
`VarHandle`.

This step moves the tree onto Java 25.

It spends that release on three things and asks the JVM for a fourth. The tree says what it imports
in the units the platform is actually shipped in — `import module java.base;` in place of the
single-type imports it subsumes. The one piece of per-thread state in this library whose lifetime
is the extent of a call says so, as a `ScopedValue` rather than as a `ThreadLocal` plus a
`finally`. And every test JVM the build forks is started with the object-header layout Java 25
made a product option.

**The class-file level moves at this step, and it is the only level that does.** Step 11 compiled
to Java 22; here the tree compiles to Java 25. As at every earlier rung it is the level that moves
and not the JVM: requirement 3 is about what the class files are, not about what wrote them — but
at this rung it is also about the *minor* version, and the Notes say why.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and everything
it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to make
   this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still in
     the tree, and the build still runs through it. Several JDKs are installed in
     this environment and the one `java` resolves to by default is not necessarily the one the
     build runs under, so a build that works only because `java` happened to resolve somewhere is
     not a build that works. What the requirement asks is that the build need nothing beyond
     Java 25 to run: it still runs with **Java 25** as the only JVM available, wherever that JDK is
     installed;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **This rung neither adds to that surface
     nor removes from it.** Requirement 4 rewrites import declarations, which name types and
     declare none; requirement 6 changes the type of one *private* field and the body of one
     package-private method. Nothing a consumer of these modules can name or call changes;
   - every test class and test method that exists today still exists, and no more of them are
     switched off than are switched off today;
   - the suite still runs as `./gradlew --continue test`, every test case reported today is still
     reported in the XML results that run writes, and nothing that passes today may fail
     afterwards;
   - that run leaves one compiled copy of each inherited test class in the tree — wherever your
     build writes its output — and no second copy somewhere else. The library's sources also stay
     where they are today: main code under `<module>/src/main/java`, test code under
     `<module>/src/test/java`;
   - each of those test classes also stands being run on its own — one class, a runner for the
     framework that class is written in, this project's own compiled output and its own
     dependencies on the classpath, the test libraries among them at JUnit Jupiter **5.14.4** on JUnit Platform
     **1.14.4** and Mockito **4.11.0** — and reaches that runner's verdict: the class loads, its cases
     run, the runner reports — and in seconds, as today: a class that needs minutes where the
     suite needs seconds, or classes that together turn a re-run of seconds into a quarter of an
     hour, have not run;
   - the project's own 49 migrated test classes are still Jupiter tests and nothing else, and still
     free of `org.junit.*` and `junit.framework.*`. The six compatibility classes that are *about*
     JUnit 4 keep their JUnit 4, exactly as before;
   - nothing compiled from this project — main code or test code, in the tree or in the jars it
     publishes — calls `Runtime.halt`, `Runtime.exit`, `System.exit` or `sun.misc.Unsafe`, whether
     by name, by reflection, or through a native library of its own, and nothing this step asks for
     needs to add one;
   - every compiled class the build leaves behind is one a class-file reader can open, and there is
     still exactly one class-file version across all of them;
   - the test that asserts on the order in which `Class.getMethods()` returns a class's methods
     comes with the code you inherit. The JVM does not specify that order, so it may land either
     way here. You are not required to make it deterministic — and you must not delete it or switch
     it off either;
   - runtime behaviour of the published artifacts is unchanged — same service registrations, same
     wiring, same exception types and messages, same ordering. **Requirement 6 is where a
     correct-looking rewrite changes behaviour**, and it says exactly which exception moves if it
     is done carelessly.

2. **The previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **7** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   the filesystem sites that are `java.nio.file` still are, and the **4** that are still `java.io`
   are still `java.io`, with the behaviour that migration had to preserve unchanged; and the
   explicit type arguments step 3 removed do not come back — the **23** sites that spell out their
   type arguments and are *not* on an anonymous class body are still 23. The **71** constructor
   calls written with `<>` are still written with `<>`.

   From step 4, all three still expected: **13** files hold a lambda or a method reference where
   the anonymous single-method bodies were; the **24** interfaces with exactly one abstract method
   still all carry `@FunctionalInterface`; and the **24** methods that hold a stream pipeline still
   hold one.

   From step 5, all four still expected: the **16** lookup methods that return `Optional` still
   do, and the tree still contains no `Optional.get()` call; the **23** main-code sites that
   collect a stream still collect it, through `Stream.collect` and the same `Collectors`
   factory each uses today; the **3** value classes still use the standard null-safe
   field comparison and the standard multi-field hash; and the **4** timing sites still take their
   values from `java.time`. The twelve published module jars still reference `java.util.Optional`,
   `java.util.stream.Collectors`, `java.util.Objects` and `java.time`, each from a spread of
   published classes.

   From step 6, all six still expected: **no** `Class.newInstance()` call anywhere, and at least
   **13** places still instantiate through a `Constructor`; **no** `AccessibleObject.isAccessible()`
   query, and the **10** `setAccessible` calls step 11 left are still 10; **no** `new Integer(…)`
   or any of its seven wrapper siblings; the **7** fixed collections that became factory calls are
   still factory calls and the **7** live views over mutable fields are still live views; the
   `Optional` combinators step 6 introduced are still carrying their control flow; and **78**
   declarations use `var`.

   From step 7, all four still expected: the **22** references to `jakarta.annotation` and
   `jakarta.ejb` are still `jakarta.`, and no `javax.annotation` or `javax.ejb` reference has come
   back; the **112** references to the six `javax.` packages that had to stay are still there, in
   the same **43** (package, place) pairs — a held reference that has been deleted is a held
   reference that is gone; the **8** `toArray` calls still pass a constructor reference and none
   of them sizes an array at the call site; and the servlet protocol's method executor still
   performs its exchange through `java.net.http`, still names a redirect policy, and holds **no**
   reference to the `HttpURLConnection` era.

   From step 8, both still expected: all **12** published module jars still declare the
   `Automatic-Module-Name` they were given, artifact for artifact; and the **2** service lookups
   still perform their lookup through `java.util.ServiceLoader.load`, the tree still holds **no**
   `ClassLoader.getResources` call, and the **11** by-name class and resource loads that are not
   service lookups are all still there doing what they did.

   From step 9, all four still expected: the **17** `instanceof` tests in **14** methods that bind
   a pattern variable still bind one, and the **7** that were left alone — out of **24** in the tree
   — are still plain tests with nothing bound, each of these in the method that holds it today,
   so a construct that survives but has moved to another method has not been kept; the HTML
   document the servlet test runner serves is still written as one text block and the tooling
   formatter still holds all **7** of its newline-bearing literals; the **11** sealed roots still
   name their **35** direct subtypes in a `permits` clause, closing over **40** types, and the
   **11** types this project is extended through are still open; and both `switch` statements are
   still arrow form.

   From step 10, all three still expected: the **13** reflective type tests that became type
   patterns are still type patterns and the **14** that could not be converted — out of **27** in
   main sources — are still reflective calls; the **4** pattern `switch`es still dispatch over
   their **10** arms and the **2** chains that had to stay chains are still chains; and the **7**
   guarded first-element reads still say `getFirst()` while the **9** that could not say it still
   read the way they did.

   From step 11, all three still expected: **none** of the 67 `java.security` references it
   removed has come back, and the **13** things that had to survive that removal still do — **7**
   files still construct reflectively through a `Constructor` and **6** annotation-scanning methods
   still reach their `Field` and `Method` objects; **28** `catch` clauses in main sources declare
   their parameter as `_` and **no** clause declares a parameter it does not read, the **51** that
   read theirs keeping their names — each, like the `instanceof` tests above, in the method that
   holds it today; and the **4** private-field sites still go through a `VarHandle`, **8**
   calls, two in each of the **4** methods that make them today, with the **10** `setAccessible`
   calls that could not convert still there.

   **This rung's requirement 4 deletes several hundred import declarations, which is the one kind
   of edit that can undo any of the above by accident.** An import that is deleted where no module
   import replaces it does not compile, which you will see; an import deleted where a *different*
   type of the same simple name becomes visible compiles and means something else, which you will
   not. Requirement 5 is where those cases are named.

3. **The compiled artifacts are Java 25.** Every class compiled from the library's own sources is
   class-file **major version 69** and **minor version 0** — main and test alike, in the tree and
   inside the published jars — and 69 is the only major version present. (Build logic that drives
   Gradle is not part of the library.) The build names the same level wherever it names one: a tree
   that moves some of its projects and leaves the rest at the level they came from carries two
   majors, and only one of those is this step's.

   **The minor version is part of this requirement and is not decoration.** A class file compiled
   behind `--enable-preview` carries major 69 and minor **65535**, and a JVM refuses to load it
   unless that same flag is on and the release matches exactly. Nothing this step asks for is a
   preview feature. A tree that needs the flag to compile has not moved to a release; it has moved
   to a flag.

4. **The tree imports modules, not types, wherever a module import can say it.** There are **495**
   single-type imports of JDK types, in **171** of this tree's main and test sources. They come out
   of exactly **six** modules:

   | module | files that need it |
   |---|---|
   | `java.base` | 167 |
   | `java.naming` | 8 |
   | `java.logging` | 7 |
   | `java.management` | 4 |
   | `java.xml` | 1 |
   | `java.net.http` | 1 |

   Afterwards each of those 171 files declares `import module M;` for **each** module it takes a
   JDK type from and for no other — **188** declarations in all, because **15** files name a module
   besides `java.base` and **4** name no `java.base` type at all.

   **The set is per file and it cannot be computed from the package name.** `import module M;` is a
   type-import-on-demand over the packages module `M` *exports*, and which module exports a package
   is a property of the JDK rather than of the spelling: `java.util.List` comes from `java.base`
   and `java.util.logging.Logger` comes from `java.logging`. A file that imports both needs both
   declarations. Ask the platform which module exports a package rather than deciding from the
   prefix.

   **The other 151 files in scope import no JDK type at all and must not acquire one.** A module
   import added to a file that needs none is not harmless: it makes several thousand simple names
   visible in that file, and the next name collision this project introduces will be found by
   whoever adds the class, not by whoever added the import.

5. **452 of those 495 lines go, and 43 stay.** A module import subsumes a single-type import of a
   type in an exported package of that module — and nothing else. The 43 that survive are:

   - **16 static imports.** `import static java.util.Arrays.asList;` names a *member*, and a module
     import is a type import. Deleting one of these does not compile;
   - **2 nested types**, imported through the class that encloses them. A type-import-on-demand
     does not walk into a class, so a module import does not make a nested type's simple name
     visible;
   - **25 that are ambiguous**, and every one of them is `import java.lang.annotation.Annotation;`.
     `java.base` exports both `java.lang.annotation` and `java.lang.classfile`, and since Java 24
     both of them publish a public type called `Annotation`. Under `import module java.base;` the
     simple name resolves to two types and the file does not compile — unless the single-type
     import stays, which takes precedence over any on-demand import and settles it.

   **A rule that reads "the module import replaces these" deletes all 495, and 18 of the 43 it
   should not touch compile either way.** The 25 ambiguous ones the compiler teaches you about, by
   refusing to build. The 16 static and the 2 nested ones it does not: a file that loses them and
   still compiles has lost them for the next reader to wonder about. Deleting all 495 does not
   meet this requirement.

6. **The one piece of state whose lifetime is the extent of a call becomes a `ScopedValue`, and the
   seven that are not do not.** This tree's main sources hold **8** `ThreadLocal` fields. Exactly
   **one** has the shape: `ManagerImpl.handledThrowables`, in `impl-base`, which is set up on entry
   to `fire(…)`, read by the handlers that call runs, and removed by hand in `shutdown()`.
   That is what `ScopedValue.where(…).run(…)` says directly — a binding that exists for the
   duration of a call and is gone afterwards, with no teardown to forget.

   Afterwards that field is a `ScopedValue`, bound around the work `fire` does, and the explicit
   teardown is gone with it.

   **The other seven stay `ThreadLocal`s — keeping them is part of this requirement, not an
   exception to it.** A
   `ScopedValue` is immutable inside its extent and invisible outside it, so a field that is
   assigned *after* the binding, or read by code that was not called from inside it, cannot become
   one. All seven of them: the event stack that same manager tears down in a different method from
   the one that fills it; a context store whose live period runs from one caller's `activate()` to
   a different caller's `deactivate()`; a save-register holding a thread's previous context class
   loader from the call that sets it until a later one restores it; and four fields on the two
   test-framework entry points, written by one callback and read by another — one of them `public
   static`, set by the runner's base class for a different class to read and clear. "It is a
   `ThreadLocal`, so make it a `ScopedValue`" is the wrong reading of this construct, and a tree
   that converts all eight has not met this requirement.

   **And the conversion has a shape, and the method's re-entrancy decides it.** `fire` is
   re-entrant: every `Event.fire` an observer makes runs it again, inside the call it is already
   in, and the set this field holds is *cleared on entry* today so that the outermost call is the
   one that owns it. Bind a fresh set on every entry and an inner handler's record is invisible to
   the outer `catch`, which re-fires an exception this tree's own comment says must be rethrown.
   Bind once, at the outermost entry, and let the re-entrant calls see the binding that is already
   there.

7. **The test JVMs this build forks are started with compact object headers.** Java 25 made
   `-XX:+UseCompactObjectHeaders` a product option. Afterwards every `Test` task of all **12**
   projects that have one starts its forked JVM with that flag.

   Where you configure it is yours to choose — a subproject, an `allprojects` block, a convention
   plugin, a `tasks.withType(Test)` in the root — as long as **every** forked test JVM gets it. A
   project whose second `Test` task was left behind still forks a JVM configured like a Java 22
   one.

   The flag needs no unlocking on this release: `-XX:+UnlockExperimentalVMOptions` was required in
   front of it on Java 24 and is redundant here.

8. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none, and it carries **44** `@SuppressWarnings`
   sites that nothing here needs a forty-fifth of. A module import produces no warning, a
   `ScopedValue` produces no warning, and a suppression added here is hiding something this step
   did not ask you to do.

## Notes

- Java 25's language and runtime are available to you, and the parts of them that are still preview
  are not. Module import declarations (JEP 511) and scoped values (JEP 506) are final in 25, and
  compact object headers (JEP 519) are a product option in 25. Structured concurrency and primitive
  types in patterns are still preview and nothing here needs them.
- `import module M;` imports the public types of every package `M` *exports*, and it also imports,
  transitively, the modules `M` `requires transitive`. That second half is why `import module
  java.base;` is enough for a file whose JDK types all live in `java.base`, and why it is *not*
  enough for a file that also names `java.util.logging.Logger`: `java.base` does not require
  `java.logging`.
- Which module exports a package is a question with an exact answer, and the platform will give it
  to you: `ModuleFinder.ofSystem()` enumerates the system modules, and each descriptor's `exports`
  set names the packages it publishes. A table of prefixes written by hand will be wrong for
  `java.util.logging`, for `javax.naming`, and for the four other places this tree happens to
  depend on.
- A single-type import takes precedence over any import-on-demand in the same file (JLS §6.4.1).
  That is the whole mechanism behind requirement 5's 25 `Annotation` imports: the module import
  makes two `Annotation`s visible on demand, and the single-type import that stays says which one
  this file means.
- `java.lang.classfile` arrived in `java.base` in Java 24 (JEP 484). Nothing in this tree uses it.
  It still changes what this tree's files mean, because it publishes a simple name they already
  use.
- A `ScopedValue` is not a faster `ThreadLocal`. It is a different statement: a value that is
  visible for the dynamic extent of one call, to everything that call reaches, and to nothing else.
  `where(…)` followed by `run(…)` or `call(…)` is the only way to bind it, `get()` throws if it
  is not bound, and `isBound()`
  is how a re-entrant method asks whether an outer call has already bound it. Nothing sets it; if
  the code you are converting *sets* the value after it starts using it, that code is not a scoped
  value.
- A `ThreadLocal` declared with an anonymous subclass overriding `initialValue()` is a class file
  of its own, and it stops being emitted when the field stops being a `ThreadLocal`. That is
  expected, and requirement 1's "exactly one class-file version" is about versions rather than
  counts.
- The number of import lines in a file is not what this step asks about, and neither is the
  number of characters. A file that replaced its imports with `import module java.base;` and lost
  a static import is shorter and does not say what it said before.
- A build that names one level in one place and a different level in another is describing two
  different sets of artifacts, and which set comes out of it depends on which of the two statements
  the tool being run reads. Comments carry that same claim to everyone except the compiler: a
  comment above a level setting that still names the level the build has left is a line the next
  reader has to measure the build to disbelieve.
- Asking javac for a level by *release* and asking it only for a bytecode *target* are two
  different instructions: a release fixes the language level, the platform API the sources are
  checked against and the class-file version in one statement, and a target fixes only the last of
  the three and leaves the other two to whichever JDK happens to run the build.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
