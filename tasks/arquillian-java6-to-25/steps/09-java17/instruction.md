# Step 9 — Java 17: patterns, text blocks, and closed hierarchies

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, talking to its container through `java.net.http`, with the two Jakarta EE
packages it could move moved, compiling to Java 11 bytecode, and with every published jar naming
the module it intends to be and finding its providers through `java.util.ServiceLoader`.

This step moves the tree onto Java 17 and then spends that level. Java 17 is the first release in
which `sealed` is a language feature rather than a preview, and it is the release the three
smaller changes below have all been waiting on since 14, 15 and 16.

The four language changes here are not four unrelated conveniences. Each one replaces a
construction where the code says *how* with one where it says *what*, and each one deletes a
specific way of being wrong: a cast that repeats a type the test just established; an escaped
document that nobody can read as the document it is; an open hierarchy whose exhaustiveness lives
in a comment; a switch whose correctness depends on a `break` being present.

**The class-file level moves at this step, and it is the only level that does.** The build ran on
Java 11 at step 8 and compiled to Java 11; here it runs on Java 17 and compiles to Java 17.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and everything
it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to make
   this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still in
     the tree, and the build still runs with **Java 17** as the only JVM available, wherever that
     JDK is installed.
     Several JDKs are installed in this environment and the one `java` resolves to by default is
     not necessarily this step's; what the requirement asks is that the build need nothing beyond
     Java 17 to run;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **This rung adds nothing to that surface
     and takes nothing from it.** Requirement 6 closes eleven hierarchies and requirement 4 deletes
     twenty-one casts; neither is allowed to add, remove or re-sign a published member on the way;
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
     wiring, same exception types and messages, same ordering. **Requirement 5 is a change to how a
     document is written and not to what it says**, and the document in question is one this
     project serves over HTTP: byte for byte, it is the same page afterwards.

2. **The previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **7** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   the filesystem sites that are `java.nio.file` still are, and the **4** that are still `java.io`
   are still `java.io`, with the behaviour that migration had to preserve unchanged; and the
   explicit type arguments step 3 removed do not come back — the **23** sites that spell out their
   type arguments and are *not* on an anonymous class body are still 23. The **71** constructor
   calls written with `<>` are still written with `<>`.

   From step 4, all three still expected: **20** files hold a lambda or a method reference where
   the anonymous single-method bodies were; the **24** interfaces with exactly one abstract method
   still all carry `@FunctionalInterface`; and the **24** methods that hold a stream pipeline still
   hold one. **Those 24 annotations are a live constraint on requirement 6 and not just a
   carried-forward count** — a sealed interface is not a functional interface, and the compiler
   will say so.

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
   query, and the **14** `setAccessible` calls are still 14; **no** `new Integer(…)` or any of its
   seven wrapper siblings; the **7** fixed collections that became factory calls are still factory
   calls and the **7** live views over mutable fields are still live views; the `Optional`
   combinators step 6 introduced are still carrying their control flow; and **78** declarations
   use `var`.

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

   **The diamonds and the `var` declarations above are where this step is most likely to undo
   earlier work by accident.** Requirement 4 deletes twenty-one casts and requirement 5 deletes
   fifteen writes; none of those lines holds a diamond or a `var`, and if your edit removes one
   anyway, it has undone part of this requirement.

3. **The compiled artifacts are Java 17.** Every class compiled from the library's own sources is
   class-file **major version 61** — main and test alike, in the tree and inside the published
   jars — and it is the only major version present. (Build logic that drives Gradle is not part of
   the library.)

4. **Where an `instanceof` test is followed by a cast to the type it just tested, the test binds
   the value.** There are **28** `instanceof` tests in this tree. **21** of them, spread over **18**
   methods, are immediately followed by a cast of *the same expression* to *the same type* — the
   shape a type pattern exists to replace. Afterwards all 21 bind a pattern variable and the casts
   they made unnecessary are gone.

   The largest group is the same unwrap-and-rethrow repeated verbatim in the seven
   `SecurityActions` classes: the cause taken out of a `PrivilegedActionException` is tested for
   `NoSuchMethodException` and then thrown as one. `Reflections` has two each in `isType` and
   `getType`, and `ObserverImpl#invoke` has two in one nested chain. The rest are one
   apiece, in main code and in tests alike.

   **The other 7 stay exactly as they are — keeping them is part of this requirement, not an
   exception to it.** They are
   `instanceof` tests with no cast after them: the value is tested and then used as the type it
   already had, or tested only to decide a branch. Binding a variable there introduces a name
   nothing reads, and a sweep that converted all 28 has not understood what it was converting. A
   held test that has been *deleted* is a held test that is gone, and is the same failure as one
   that was wrongly converted.

   Two of the 21 need more than a mechanical rewrite and are the reason this is a requirement about
   *meaning* rather than about syntax:

   - a pattern variable's scope is the region where the test is definitely true, so a conversion
     that changes which branch reads the value has to move the read with it;
   - where the expression being tested is a field rather than a local, a pattern binds a fresh
     variable and does **not** re-read the field. That is the behaviour you want. It is also a
     change in what the code does if anything between the test and the use can reassign the field,
     so satisfy yourself that nothing does before you convert.

5. **The HTML page the servlet test runner writes becomes one text block, and the XML the tooling
   formatter builds does not.** `ServletTestRunner#doGet` spells out an HTML document as **16**
   consecutive constant writes. Afterwards it writes one text block, and no string literal in that
   method contains a newline escape.

   **The page it serves is byte-identical afterwards.** This document goes out over HTTP and is
   part of what this project does, so the conversion is only correct if the bytes do not
   move: the incidental indentation a text block strips is determined by the *least-indented* line
   including the closing delimiter, a text block's content always ends with a newline unless the
   closing delimiter is on the content's last line, and a trailing space is only preserved if you
   ask for it. Those three rules are the whole of the difficulty, and getting one of them wrong
   produces a document that looks right in the source and is wrong on the wire.

   **`ToolingDeploymentFormatter` keeps all 7 of its newline-bearing literals**, in `format` and
   `formatNode`. It builds an XML fragment by appending one- and two-character pieces around
   computed values — an archive name, a node path — so there is no run of adjacent constants to
   lift, and a text block wrapped around interleaved computed values is not the same string. This
   is the half of the requirement that says a text block is for a *document*, not for any literal
   that happens to contain a newline.

6. **The event vocabulary becomes closed, and everything this project is extended through stays
   open.** A `permits` clause here may only name a type declared in the root's own package. The
   language lets a permitted subtype sit in a different package only when the root and the subtype
   are in the same *named* module, and nothing in this tree declares a module, so a clause that
   reaches outside the root's package is a compile error rather than a wider hierarchy. The
   hierarchies this tree can close at all are therefore the ones whose direct subtypes already sit
   beside their root.

   **11** roots become `sealed`, each naming its direct subtypes in a `permits` clause. They are
   the event hierarchies: the container control, deployment and multi-control events; the container
   and deployer events; the enrichment events; and the suite, class, test and lifecycle events.
   They are a closed vocabulary in fact — the framework fires these and only these — and saying so
   lets the compiler enforce what the comments currently assert. A `permits` clause that is a name
   short does not compile, so this is a requirement you can check.

   **These must stay open, and they are the point of the requirement.** Sealing an extension point
   is not a migration, it is a breaking change made silently:

   - `ResourceProvider`, `Typed`, `AbstractContext`, `ArquillianDescriptorImpl`, `ContainerDefImpl`
     and `ResultCallback` — types whose whole purpose is to be implemented or extended, in this
     tree and outside it. They have several subtypes here because they are meant to;
   - `LifecycleEvent` and `ExecutionEvent` — by shape these belong with the eleven, and they are
     the two the compiler takes off the list for you. Both carry `@FunctionalInterface`, which step
     4 put there and requirement 2 still holds. A sealed interface is not a functional interface;
     you cannot have both, and dropping the annotation to seal the type trades a requirement you
     still owe for one you were never asked for;
   - the three `Service` and `Service2` types declared inside the tests. A test's own stand-in for
     an interface is not part of the vocabulary the framework fires, and the tests are free to grow
     another one tomorrow.

   **A permitted subtype must itself be `final`, `sealed` or `non-sealed`** — the compiler will
   not accept one that is none of the three. The eleven clauses name **35** subtypes between them.
   Six of those names are on the sealed list themselves, because this vocabulary nests — a
   container control event permits a deployment event, which permits two of its own — so the eleven
   hierarchies close over **40** distinct types. The 29 that are not roots are leaves: nothing in
   this tree extends any of them, and which of the three modifiers you give them is yours to
   choose.

7. **The two colon-form switches become arrow form.** This tree contains exactly **2** `switch`
   statements — one over an enum in `DeployableTestBuilder`, one over an `int` in
   `DynamicServiceLoaderTestCase` — and both are two-case statements executed for their side
   effects, each case ending in `break`. Afterwards both are arrow form and neither needs a `break`
   to be correct.

   This is a small requirement and it is here because the form is the point: an arrow case cannot
   fall through, so the `break` stops being load-bearing and starts being impossible to forget.
   Neither of these is a switch *expression* and neither needs to become one; arrow form on a
   statement switch carries no exhaustiveness obligation.

8. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none, and it carries **44** `@SuppressWarnings`
   sites that nothing here needs a forty-fifth of. Nothing in requirements 4 through 7 produces a
   warning that wants suppressing: a type pattern is *more* precisely typed than the cast it
   replaces, a text block is a `String`, and a `permits` clause is checked rather than inferred.
   A suppression added here is hiding something this step did not ask you to do.

## Notes

- Java 17's language is available to you and the ones after it are not. `sealed` is standard at 17
  and was a preview feature at 15 and 16, which is why this is the release the step waits for;
  pattern `instanceof` arrived at 16, text blocks at 15 and arrow switches at 14. Nothing here
  needs `--enable-preview`, and a build that asks for it is asking for something else.
- Asking javac for a level by *release* and asking it only for a bytecode *target* are two
  different instructions. A build that states only the target compiles its sources against
  whatever library the JDK running the build happens to ship, so a call to a method a later JDK
  added compiles clean, lands in a class file at the older major version, and fails the first time
  that line runs. `--release` pins the API surface alongside the bytecode, and javac then refuses
  the call where it is written.
- A build that names one level in one place and a different level in another is describing two
  different sets of artifacts, and which set comes out of it depends on which of the two statements
  the tool being run reads. Comments carry that same claim to everyone except the compiler: a
  comment above a level setting that still names the level the build has left is a line the next
  reader has to measure the build to disbelieve.
- Record classes are also available at this level (final since Java 16), and this step does not
  ask for any. Every data-like type in
  this tree is published, and a record declares canonical accessors — `name()` beside the
  `getName()` the library already publishes. On this tree that is a change to what the library
  publishes rather than to how it is written, and requirement 1 freezes that surface.
- Pattern matching for `switch` is not available here: it was a preview at 17 and did not become
  standard until 21. Requirement 7 is about the arrow form of an ordinary switch and nothing more.
- `sealed`, `non-sealed` and `permits` are contextual keywords, not reserved words. Code that uses
  `sealed` as an identifier still compiles, and the compiler will not warn you that you meant the
  modifier.
- A `sealed` type's `permits` clause is compiled into the class file, so a subtype added later
  without amending the clause fails at compile time rather than slipping through. That is the
  enforcement you are buying, and it is also why the clause has to be complete on the first try.
- `java.security.AccessController` is deprecated for removal as of Java 17, which is why the
  compiler starts naming it here. The warning is accurate and none of the requirements above is
  about it: those calls stay exactly as they are, warning and all.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
