# Step 10 — Java 21: type patterns, pattern switch, and sequenced collections

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, talking to its container through `java.net.http`, with the two Jakarta EE
packages it could move moved, with every published jar naming the module it intends to be and
finding its providers through `java.util.ServiceLoader`, and — from the step just finished —
compiling to Java 17 bytecode, with its casts folded into `instanceof` tests, its served HTML page
written as a text block, its event vocabulary sealed and its two switches in arrow form.

This step moves the tree onto Java 21 and then spends that level on the three things this tree
still says the long way round.

All three are the same sentence written three ways: **the code already knows the answer, and it is
asking the question in a library rather than in the language.** A reflective type test asks the
runtime what a value is, when the type is written right there in the source. A chain of those
tests over one value is a `switch` that has not been allowed to say so. And a read of a
collection's first element goes through an index or an iterator because the interface the code
holds could not express *first* until this release.

**The class-file level moves at this step, and it is the only level that does.** The build ran on
Java 17 at step 9 and compiled to Java 17; here it runs on Java 21 and compiles to Java 21.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and everything
it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to make
   this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still in
     the tree, and the build still runs with **Java 21** as the only JVM available, wherever that
     JDK is installed.
     Several JDKs are installed in this environment and the one `java` resolves to by default is
     not necessarily this step's; what the requirement asks is that the build need nothing beyond
     Java 21 to run;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **This rung adds nothing to that surface
     and takes nothing from it.** Requirements 4, 5 and 6 rewrite the inside of fourteen method
     bodies and not one published signature;
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
     wiring, same exception types and messages, same ordering. **Requirement 5 is the one place
     where a correct-looking rewrite can change behaviour**, and the paragraph there says exactly
     what the language does differently and which four methods it does it in.

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

   From step 9, all four still expected: the **21** `instanceof` tests in **18** methods that bind
   a pattern variable still bind one, and the **7** that were left alone — out of **28** in the
   tree — are still plain tests with nothing bound; the HTML document the servlet test runner
   serves is still written as one text block and the tooling formatter still holds all **7** of its
   newline-bearing literals; the **11** sealed roots still name their **35** direct subtypes in a
   `permits` clause, closing over **40** types, and the **11** types this project is extended
   through are still open; and both `switch` statements are still arrow form.

   **The bound `instanceof` patterns above are where this step is most likely to undo earlier work
   by accident.** Requirement 5 turns four chains into `switch` statements — and none of those four
   is made of `instanceof` tests. The chains step 9 bound patterns in, including the nested one in
   `ObserverImpl#invoke` and the pair in `Reflections`, are not this step's work and are not
   improved by being made into switches: an `instanceof` test that has become a `case` label is an
   `instanceof` test that is gone, and this requirement no longer holds for it.

3. **The compiled artifacts are Java 21.** Every class compiled from the library's own sources is
   class-file **major version 65** — main and test alike, in the tree and inside the published
   jars — and it is the only major version present. (Build logic that drives Gradle is not part of
   the library.) The build names the same level wherever it names one: a tree that moves some of
   its projects and leaves the rest at the level they came from carries two majors, and only one
   of those is this step's.

4. **Where the code asks the runtime a question about a type it already knows, it asks the
   language instead.** There are **35** reflective type tests in this tree and **27** of them are
   in main sources. **13**, spread over **7** methods, are of the form `X.class.isInstance(value)`
   — a type written out at compile time on one side, a value on the other. That is what
   `value instanceof X` says, and afterwards those 13 say it: the reflective call is gone from the
   method and the type it named is named by a type test in the language, either an `instanceof` or
   a `case` label from requirement 5.

   They are in the two servlet protocol packagers' `generateDeployment` (three each), the OSGi
   packager's `generateDeployment`, the deployment generator's `buildTestableDeployments`, the CDI
   protocol processor's `process` (two), and the resource enricher's `isPrimitiveNull` and
   `resolveResource` (two).

   **The other 14, in 11 methods, stay exactly as they are — keeping them is part of this
   requirement, not an exception to it.**
   A type pattern needs two things written in the source: a value to bind and a type to name. These
   fourteen have neither, in two different ways, and both are compile errors rather than matters
   of taste:

   - **nine have a `Class` value where the type must go** — `c.isInstance(v)` or
     `c.isAssignableFrom(t)` where `c` is a variable, in the manager's context and extension
     lookups and its two observer resolutions, in the protocol metadata's `getContext` and
     `hasContext`, in the deployment exception handler's `containsType`, and twice in the OSGi
     enricher's `enrich`. There is no form of `instanceof` whose right-hand side is a value;
   - **five have a `Class` object where the value must go** — `X.class.isAssignableFrom(someClass)`,
     four of them in the annotation deployment scenario generator and one in the EJB enricher's
     `injectClass`. These ask whether one *type* is assignable to another, which is not a question
     about any value at all.

   A held test that has been *deleted* is a held test that is gone, and is the same failure as one
   that was wrongly rewritten. Deleting a convertible one does not satisfy this requirement either:
   the requirement is two things about the same method — the reflective call has gone from it
   *and* the type it named is named in the language there — and a method that lost the code
   altogether has only the first.

5. **The four multi-arm dispatches over one value become pattern switches, and the two that look
   exactly like them do not.** **6** methods in this tree dispatch on the runtime type of a single
   expression through a chain of two or three reflective type tests. **4** of them, **10** arms
   between them, test one *value*, and afterwards each is a `switch` over that value whose arms are
   type patterns — one arm per type the chain tested, at least as many patterns as the chain had
   arms:

   - both servlet protocol packagers' `generateDeployment`, three arms each over the application
     archive: `EnterpriseArchive`, `JavaArchive`, `WebArchive`;
   - the CDI protocol processor's `process`, two arms over the protocol archive: `JavaArchive`,
     `WebArchive`;
   - the resource enricher's `resolveResource`, two arms over the injection point: `Field`,
     `Method`.

   **The chain itself is gone from each of those four methods.** The types its arms named are
   named by the `switch`'s patterns and by nothing else standing beside it: no `instanceof` left
   in the same method still asks whether that value is an `EnterpriseArchive`, a `JavaArchive`, a
   `WebArchive`, a `Field` or a `Method`. Rewriting the arms one at a time into
   `if (v instanceof EnterpriseArchive e) … else if (v instanceof JavaArchive j) …` and putting a
   `switch` after them leaves the same chain in the method under a different spelling, and it is
   two dispatches over one value where the tree had one. Requirement 4 is met at those ten arms by
   the `case` labels; a second copy in `instanceof` form is not a second way of meeting it.

   **The two that stay are the annotation deployment scenario generator's `generateDeployment` and
   `validate`.** They have the same shape — two arms, one selector, `Archive` and `Descriptor` —
   and the selector is `deploymentMethod.getReturnType()`, a `Class` object. The arms ask whether
   one type is assignable to another; a pattern switch matches a *value* against a type and there
   is no value here to match. Switching on the type's name instead is not the same program: it
   replaces a subtype test with a string comparison, and it passes the two tests this tree happens
   to have for it while breaking every subclass the framework accepts. Those four arms are four of
   the fourteen reflective tests requirement 4 keeps, so leaving both of these dispatches as the
   chains they are today is that requirement seen from the other side and not a second obligation.

   **This is the one requirement in this step that can change behaviour while compiling perfectly,
   and the difference is null.** An `if (X.class.isInstance(v))` chain is simply false for a null
   `v` and falls through to whatever follows it. A `switch` over type patterns throws
   `NullPointerException` **before any arm is considered** unless the switch has a `case null`
   label. The four chains do four different things with an input no arm matches, and each must go
   on doing it:

   - the two packagers' chains fall out of the bottom for an archive none of their three arms
     accepts, into an `IllegalArgumentException` the method itself raises naming the packager and
     the archive's own class, and they must still end there; a null archive never reaches either
     of them, because the deployment object it arrives in rejects one in its own constructor;
   - the CDI processor's `process` returns quietly when the archive it is handed is neither kind,
     null included, and it must still return quietly;
   - the resource enricher's `resolveResource` returns null for an element that is neither a
     `Field` nor a `Method`, and a null element already throws `NullPointerException`.

   Decide each one from what the chain does today, not from a rule: `case null` is right at some of
   these and wrong at others, and a tree that adds it everywhere has changed one of the four as
   surely as a tree that adds it nowhere.

6. **The seven guarded first-element reads say `getFirst()`, and the nine that cannot say it keep
   what they have.** There are **26** reads of a collection's first element in this tree and **16**
   of them are in main sources. **7** read `get(0)` from a `java.util.List` under a guard on the
   same receiver — the container and protocol registries' default lookups, the deployment
   scenario's default deployment, the URL resource provider's `lookup`, the JUnit runner's
   `multiExecute`, and the JMX method executor's and the OSGi enricher's `findOrCreateMBeanServer`.
   Afterwards all seven
   read the first element by name, through the interface that has a name for it.

   **The other nine stay, and they fail two different conditions:**

   - **seven cannot be converted at all**, because `getFirst()` arrived on `SequencedCollection`,
     which `List` and `Deque` extend and `Set`, `Collection` and a plain `Iterable` do not. A
     `LinkedHashSet` held in a `Set` variable has a first element at run time and no `getFirst()`
     at compile time. These are the service loader's `onlyOne` (twice), both servlet packagers'
     `handleArchive`, the TestNG listener's `getTestResult` (twice) and the SPI's `TestRunners`;
   - **two would change what a caller sees.** The event adaptor's `test` and the JUnit runner's
     `convertToTestResult` read `get(0)` with **no guard**, on a result list a caller controls.
     `get(0)` on an empty list throws `IndexOutOfBoundsException`; `getFirst()` throws
     `NoSuchElementException`. Requirement 1 says the exceptions this library throws do not move.

   As in requirement 4, deleting a read does not satisfy this either: the old spelling has to
   have gone from the method *and* a first-element read by name has to be there in its place.

7. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none, and it carries **44** `@SuppressWarnings`
   sites that nothing here needs a forty-fifth of. Nothing in requirements 4 through 6 produces a
   warning that wants suppressing: a type pattern is more precisely typed than the reflective call
   it replaces, a pattern switch over a sealed-free hierarchy of ordinary classes carries no
   exhaustiveness obligation when it is a statement with a default, and `getFirst()` is an ordinary
   interface method. A suppression added here is hiding something this step did not ask you to do.

## Notes

- Java 21's language is available to you and the ones after it are not. Pattern matching for
  `switch` is standard at 21 and was a preview feature at 17, 18, 19 and 20, which is why this is
  the release the step waits for; `SequencedCollection` and its `getFirst()` arrived at 21 as well.
  Nothing here needs `--enable-preview`, and a build that asks for it is asking for something else.
- Asking javac for a level by *release* and asking it only for a bytecode *target* are two
  different instructions. A build that states only the target compiles its sources against
  whatever library the JDK running the build happens to ship, so a call to a method a later JDK
  added compiles clean, lands in a class file at the older major version, and fails the first time
  that line runs. `--release` pins the API surface alongside the bytecode, and javac then refuses
  the call where it is written. `getFirst()` is exactly such a call, so this distinction is not
  academic at this rung.
- A build that names one level in one place and a different level in another is describing two
  different sets of artifacts, and which set comes out of it depends on which of the two statements
  the tool being run reads. Comments carry that same claim to everyone except the compiler: a
  comment above a level setting that still names the level the build has left is a line the next
  reader has to measure the build to disbelieve.
- A `switch` statement over type patterns still needs to be exhaustive or to have a `default`, and
  the four here dispatch on ordinary open types, so a `default` is what makes them compile. That
  `default` is also where the behaviour the chain had for an unmatched input belongs — everything
  except null, which requirement 5 covers separately, because `case null` and `default` are
  different labels answering different questions.
- A pattern variable's scope is the region where its test is definitely true, so a conversion that
  changes which branch reads the value has to move the read with it. Where the expression being
  tested is a field rather than a local, a pattern binds a fresh variable and does **not** re-read
  the field; satisfy yourself that nothing between the test and the use can reassign it.
- `SequencedCollection` also brings `getLast()`, `addFirst`, `addLast` and `reversed()`. None of
  them is asked for here: `getLast()` reads a different element than `get(0)` did and `reversed()`
  is not a read at all, so a site that acquired one of those instead of `getFirst()` has changed
  what the method returns.
- `List.getFirst()` on an empty list throws `NoSuchElementException` and `List.get(0)` throws
  `IndexOutOfBoundsException`. Where a read is guarded by a test on the same receiver, neither
  throw is reachable and the substitution is invisible to a caller. That guard is the whole of why
  seven of the sixteen are convertible.
- Record patterns are also Java 21 and this step does not ask for any: nothing in this tree is a
  record, and step 9's requirement that the published API not move is why none can be introduced —
  a record declares canonical accessors beside the getters the library already publishes.
- Virtual threads are also Java 21 and this step does not ask for any either. This library hands
  work to the containers it drives; it owns no request thread pool and creates no executor, so
  there is nothing here for a virtual thread to replace.
- `java.security.AccessController` has been deprecated for removal since Java 17 and the compiler
  names it at every call. The warning is accurate and none of the requirements above is about it,
  so a build whose only removal warnings are on those lines is as clean as this step asks for.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
