# Step 13 — Static closure: the warnings the tree was hiding, fixed rather than silenced

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, talking to its container through `java.net.http`, with the two Jakarta EE
packages it could move moved, with every published jar naming the module it intends to be and
finding its providers through `java.util.ServiceLoader`, with its casts folded into `instanceof`
tests, its event vocabulary sealed, dispatching through pattern `switch`es and reading first
elements by name, free of the security manager's API, reaching four private fields through a
`VarHandle`, and — from the step just finished — importing modules rather than types, holding one
piece of per-thread state as a `ScopedValue`, and forking its test JVMs with compact object
headers.

This is the last step, and it moves no language level at all.

Every rung before this one added something. This one takes something away: the tree carries **44**
`@SuppressWarnings` annotations, spread over **26** files and holding **48** values between them,
and they are the reason nobody has had to look at what the compiler thinks of this code. Compile
it with `-Xlint:all -Xlint:-deprecation` as it stands and javac prints **11** warnings. Delete the
44 annotations first and it prints **36**. The 25 in the gap are real findings that twelve rungs of
work have been carried out on top of.

This step ends at **0 warnings** and **4 annotations**. The distance between those two numbers is
the whole step: **6** of the 36 findings name something this tree does not own and are suppressed
deliberately -- four annotations cover the six, because two of them sit on a method with two raw
parameters each. The other **30 are fixed in the code**.

**Deleting the annotations is not the work; it is the first line of it.** A tree with no
`@SuppressWarnings` and no other change has taken every one of the 25 hidden findings and made it
visible, which is the opposite of what requirement 4 asks for. A tree that adds eleven more
annotations reads 0 warnings and has fixed nothing. Neither meets this step: requirements 4 and 5
hold together, and only a tree that has both deleted and fixed meets them.

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
     nor removes from it, and on this rung that is a constraint with teeth.** Two of the fixes
     requirement 6 asks for are inside method bodies; the rest narrow the types of *private* fields
     and *local* variables. Making a class `final` to silence a warning changes the surface, and so
     does renaming a public or protected method a linter complains about, whatever the linter's
     reason;
   - every test class and test method that exists today still exists, and no more of them are
     switched off than are switched off today. **A warning removed by deleting the code that
     carried it is not a warning fixed**, and the cheapest route to zero on requirements 4 and 6 is
     to delete the two TestNG transformers. Requirement 3 holds the class-file count for that reason;
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
     wiring, same exception types and messages, same ordering. **Requirement 4 is where this rung
     puts that at risk**: thirty of its findings are fixed in code that runs, and it names the
     families they fall in. Requirement 7 states what must still be true.

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

   From step 12, all four still expected: **188** `import module` declarations across **171** files,
   naming **6** modules between them, with **15** files naming a module besides `java.base` and
   **4** naming no `java.base` type at all; the **43** single-type JDK imports that a module import
   cannot replace — **25** ambiguous, **16** static, **2** nested — still written out, and **no**
   single-type JDK import that a module import *does* subsume written out beside it; **1**
   `ThreadLocal` converted to a `ScopedValue` bound at the outermost `fire`, with the other **7**
   still `ThreadLocal`s; and `-XX:+UseCompactObjectHeaders` still reaching every `Test` task of all
   **12** projects that have one.

   **This rung's requirement 6 narrows type declarations, which is the one kind of edit that can
   undo any of the above by accident.** A declaration narrowed to the wrong argument compiles and
   means something else; a declaration narrowed where the tree's own generics did not allow it
   pushes the error somewhere the warning never was.

3. **Nothing about the compiled artifacts moves.** Every class compiled from the library's own
   sources is still class-file **major version 69** and **minor version 0**, and 69 is still the
   only major version present. The build still names 25 wherever it names a level — **a comment
   naming a level the build has left is the same defect it always was** — and it still asks for
   it by *release* rather than by target alone.

   **And there are still 435 of them.** Main and test together, the build produces **435** class
   files, exactly as many as it produced before this step. That number is in this requirement
   because requirements 4 and 6 both go to zero if the code carrying the findings is deleted, and
   for no other reason.

4. **The tree compiles with no warnings at all.** Compiled with `-Xlint:all -Xlint:-deprecation`,
   over the library's own main and test sources, javac emits **0** warnings. The tree you are
   given emits 36 once its suppressions are removed, in six families:

   | family | n |
   |---|---|
   | `unchecked` | 18 |
   | `rawtypes` | 11 |
   | `cast` | 3 |
   | `this-escape` | 2 |
   | `serial` | 1 |
   | `dangling-doc-comments` | 1 |

   **The `serial` finding is the one whose fix has a name, and the name is given to you.** It is
   `writeObject(Object, HttpServletResponse)` in
   `protocols/servlet/…/protocol/servlet/runner/ServletTestRunner.java`: a private method in a
   `Serializable` class, carrying a name javac's `serial` lint reserves for the serialization
   hook, with a signature that is not the hook's. Suppressing it is not available — requirement 5
   fixes the budget at four annotations and this is not one of them — so the fix is to rename it.
   **Rename it to `writeSerializedResult`**, and move its two call sites in the same file with it.
   The name is stated rather than left to your judgement because this class's declared methods,
   private ones included, are visible through reflection, so the new name becomes part of what the
   class exposes and has to be this one.

   **`deprecation` is excluded and stays excluded.** It is not part of this requirement, you are
   not asked to move it, and a tree that upgrades a dependency to shift it has done dependency work
   rather than this step's.

   Those flags define the property; they are not a build setting. You do not have to put `-Xlint:all` in the build to
   satisfy this, though putting it there is a reasonable way to find out where you stand. If you do,
   leave `-Werror` out of it: it turns every future warning on this tree into a build failure, which
   is a policy decision this step was not asked to make on the project's behalf.

5. **The tree ends with four `@SuppressWarnings` annotations, and they are these four.** Not four
   of your choosing: 30 of the 36 findings are fixable in this code and must be fixed, and these
   four annotations carry the 6 that are not.

   | file | value |
   |---|---|
   | `impl-base/…/impl/core/UncheckedThrow.java` | `unchecked` |
   | `junit/…/junit/Arquillian.java` | `this-escape` |
   | `testng/…/testng/RemoveDependsOnTransformer.java` | `rawtypes` |
   | `testng/…/testng/TestDataProviderTransformer.java` | `rawtypes` |

   Why each is not fixable here. In `UncheckedThrow` the unchecked cast *is* the sneaky-throw
   mechanism, so removing it removes the method. In `Arquillian` the runner registers itself
   before its constructor returns, because JUnit constructs it and then calls it; the only other
   fix is `final`, which changes the published API requirement 1 holds fixed. In both transformers
   TestNG 5.14.6 declares `IAnnotationTransformer.transform` with raw parameters, so narrowing
   them stops the method overriding anything — which is also why requirement 6 leaves those four
   declarations raw.

   Two of those four the tree already carries; two of them you will have to add, because before
   this step nothing compiled with `-Xlint:all` and those two warnings were never being emitted.
   Put each one on the declaration it excuses rather than on the type that contains it — an
   annotation on a class silences every warning in the file, including the ones nobody has looked
   at yet. A comment beside each saying why it is there is what makes it legible to the next
   reader, since none of the four explains itself.

   Three more things are part of this requirement:

   - **each names its values explicitly.** `@SuppressWarnings("all")` is not a budget entry, it is
     the absence of one: it is a fifth value on top of the four, and the four are the whole budget;
   - **no annotation anywhere on the tree has the value `unused`.** The tree you are given
     carries **27** of them, all in test sources. They suppress nothing: `unused` is not a javac
     lint key at all, so none of the 27 has ever silenced a single diagnostic javac would emit.
     They are editor furniture that survived twelve rungs, and the fact that removing all 27
     changes javac's output by nothing is what makes them free to remove and worth nothing to keep.

6. **The raw type declarations go, except the ones that cannot.** The tree declares **11** variables
   at a raw type — nine as plain declarations and two as the argument of a `Collection<…>`. Four of
   them are the `Class` and `Constructor` parameters of the two `transform` methods in requirement
   5's table; **those four stay raw**, which is what the suppressions beside them are for.

   The other **7** are narrowed to the type arguments they actually hold, and afterwards **0** raw
   declarations remain that were not forced. They sit in **5** files: a container local in
   `impl-base`'s lifecycle controller, two protocol collections in its protocol registry creator,
   and four mock fields across three test cases.

   **This is the requirement a tool gets wrong quietly.** Narrowing a declaration to `<Object>` to
   stop the warning satisfies nothing — the generic parameter was there to be used. Narrowing one
   to a type the surrounding code does not actually produce moves the failure from the compiler's
   warning stream to its error stream, or past both to a `ClassCastException` a test finds.

7. **Behaviour is unchanged, and on this rung that is not a formality.** The fixes requirements 4
   and 6 ask for reach into real dispatch: `ManagerImpl`'s context registration, `ExceptionProxy`'s
   constructor lookup, `CDIInjectionEnricher.injectNonContextualInstance`, `RemoteTestExecuter`'s
   protocol executor lookup and `ContainerLifecycleController.setupContainer` each sit on a cast or
   a raw type that carries a warning, and each changes what a type resolves to at a call site.

   Afterwards `./gradlew --continue test` reports the same suite it reports today: **160** tests,
   **7** skipped, **0** failures, **0** errors, across **53** result XMLs, with the same case ids.

## Notes

- `@SuppressWarnings` has `RetentionPolicy.SOURCE`. It is not in the class file at all, so nothing
  reading bytecode can see one. That is why requirement 5 is stated over the sources and why it is
  stated at all: a requirement that only counted javac's output would be satisfied by an annotation.
- `-Xlint:all` does not turn on every warning javac has a name for, and it turns on several this
  code has never been compiled under. `this-escape` arrived in Java 21 and `dangling-doc-comments`
  in Java 23; both are findings about code that has not changed in years, and both are real.
- `this-escape` is javac telling you that a constructor lets `this` reach code that can call back
  into the object before the constructor finished. The fixes are: do not publish `this` from the
  constructor, or make the class `final` so no subclass can observe the half-built state. For a
  JUnit runner constructed by JUnit and named from `@RunWith`, neither is available.
- `dangling-doc-comments` is a `/** … */` comment in a position where it documents nothing — before
  a package declaration, or between a member and the next one. It is fixed by making it a comment
  that documents the thing after it, or by making it an ordinary `/* … */` block; one that is empty
  and documents nothing can simply go. Deleting the text of one that says something is not the fix.
- A raw type and an unchecked cast are usually the same defect seen twice: a value comes out of a
  raw expression as an `Object`, and the cast that puts it back into a typed variable is unchecked.
  Fixing the declaration usually removes both. That is why requirement 6 names the declarations and
  not the casts.
- `Class` is generic; `Class<?>` is not a raw type and is not a weaker statement than `Class<T>`
  when nothing in the method body needs `T`. `Collection<Protocol>` is raw in its *argument*, and
  `Collection<Protocol<?>>` is the narrowing, not `Collection<Object>`.
- A mock field declared at a raw type is a raw declaration like any other. The mocking framework
  does not need the argument, which is exactly why nobody wrote one.
- The suppression budget is **4**, an exact number rather than a guideline. A fifth
  annotation anywhere on the tree — including one you add to a file this step never touches — is
  over budget.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
