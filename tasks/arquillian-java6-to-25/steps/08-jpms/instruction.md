# Step 8 — Automatic module names and the platform's service loader

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, compiling to Java 11 bytecode, talking to its container through
`java.net.http`, and with the two Jakarta EE packages it could move moved.

Java 9 gave the platform a module system, and twelve of the sixteen projects here publish a jar
that a module path will have to name. This step is the first half of that migration and it stops
there deliberately, because the first half is the half with a deadline.

A jar on the module path is a module whether or not anybody asked. If it declares nothing, the
platform **derives** a name for it from the jar's *file name* — `arquillian-impl-base-1.0.0-SNAPSHOT.jar`
becomes `arquillian.impl.base` — and that derived name is what every downstream `requires` has to
be written against. It is also a name that changes when the file name changes: a version bump, a
classifier, a repackaging, and every consumer breaks. `Automatic-Module-Name` in the jar's manifest
replaces the guess with a promise. The promise is cheap to make now and impossible to change later,
which is why it comes before anything else in a JPMS migration and why this step asks for it on its
own.

The second thing this step asks for is the reason the first one is not enough. A module-aware
runtime finds service providers through `java.util.ServiceLoader`, which reads a module's
`provides` clause or a classpath jar's `META-INF/services` entries and is the only lookup the
module system can see through. This tree does not use it. It reads `META-INF/services` by hand —
`ClassLoader.getResources`, parse the lines, `Class.forName` each one — and that code works on the
classpath and is invisible to the module graph, because a resource in a named module is not
readable from outside it. Two methods in this tree do that. Eleven others reach into a class loader
for something that is **not** a service, and `java.util.ServiceLoader` cannot express any of them.
Telling the two from the eleven is most of this step.

**The JDK does not move at this step, and neither does the class-file level.** The build ran on
Java 11 at step 7 and still does, and it still compiles to Java 11. An `Automatic-Module-Name` is a
manifest attribute; it is legal at every class-file version this ladder has reached, and nothing
here needs a newer one.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and everything
it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to make
   this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still in
     the tree, and the build still runs with **Java 11** as the only JVM available, wherever that
     JDK is installed.
     Several JDKs are installed in this environment and the one `java` resolves to
     by default is not this step's; what the requirement asks is that the build need nothing
     beyond Java 11 to run;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`. **Requirement 3
     is about those twelve module jars and about no others.** A `-sources` jar holds `.java` files
     and no packages of classes; it is not a module, it does not go on a module path, and giving it
     a module name is not what this step is asking for;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. This rung adds nothing to that surface and
     takes nothing from it. Requirements 5 and 6 are about thirteen methods' worth of lookup
     code, and what changes there changes **bodies** and no declarations: a method that stops
     scanning `META-INF/services` by hand keeps its name, its parameters and its return type;
   - every test class and test method that exists today still exists, and no more of them are
     switched off than are switched off today;
   - the suite still runs as `./gradlew --continue test`, every test case reported today is still
     reported in the XML results that run writes, and nothing that passes today may fail afterwards;
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
     wiring, same exception types and messages, same ordering.

   **There is exactly one exception to that last point, it belongs to requirement 5, and it is
   required rather than merely tolerated.** `java.util.ServiceLoader` does not read a
   provider-configuration file the way the two hand-written scanners did, and the difference is
   visible from outside. Read requirement 5 before you decide what "behaviour unchanged" means for
   a malformed `META-INF/services` file.

2. **The previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **7** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   the filesystem sites that are `java.nio.file` still are, and the **4** that are still `java.io`
   are still `java.io`, with the behaviour that migration had to preserve unchanged; and the
   explicit type arguments step 3 removed do not come back — the **23** sites that spell out their
   type arguments and are *not* on an anonymous class body are still 23. The **71** constructor
   calls written with `<>` are still written with `<>`.

   Those two numbers were 9 and 73 when step 7 published this tree, and they are 7 and 71 here for
   a reason that belongs to this step rather than to you: requirement 5 deletes the two
   hand-written scanning loops, and between them they held two resource clauses and two diamonds.
   You are not asked to put them back. You are asked not to lose any of the others.

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
   use `var` — step 6 left 80, and the two that go are the two resource variables in the loops
   requirement 5 deletes.

   From step 7, all four still expected: the **22** references to `jakarta.annotation` and
   `jakarta.ejb` are still `jakarta.`, and no `javax.annotation` or `javax.ejb` reference has come
   back; the **112** references to the six `javax.` packages that had to stay are still there, in
   the same **43** (package, place) pairs — a held reference that has been deleted is a held
   reference that is gone; the **8** `toArray` calls still pass a constructor reference and none
   of them sizes an array at the call site; and the servlet protocol's method executor still
   performs its exchange through `java.net.http`, still names a redirect policy, and holds **no**
   reference to the `HttpURLConnection` era.

   **Step 7's class-file level is the one thing this step does not move.** It was raised to Java 11
   there and it stays at Java 11 here.

3. **Every published module jar declares the module name it intends to keep.** All **12** of them:
   the manifest of each published module jar carries an `Automatic-Module-Name` attribute. The
   four projects that publish no classes — the build-logic project and the three parent projects —
   are not modules and are not asked for one.

   `Automatic-Module-Name` is a manifest attribute, so it is set where a jar's manifest is set,
   which for this build is the `jar` task. Stating it once, in one place, for the twelve projects
   that need it, is worth more than twelve copies of the same three lines.

   **The names are the only thing this requirement adds.** No `module-info.java` — see the note at
   the foot of this file for why the strong form of JPMS is not reachable on this tree and is not
   being asked for — and nothing that changes which classes a jar contains or which packages it
   exports.

4. **The name each jar declares is the root package of the code that jar ships.** This is the
   requirement, and the artifact id is not it.

   A module name is a reverse-DNS name, and the name a jar should claim is the longest package
   prefix that every class in its own main sources shares. For ten of the twelve artifacts that is
   exactly what the artifact id suggests: `arquillian-testenricher-cdi` ships
   `org.jboss.arquillian.testenricher.cdi`, and dropping the `arquillian-` prefix, turning the
   remaining hyphens into dots and prefixing `org.jboss.arquillian.` gets you there.

   **Two of the twelve are not where their id says, and finding which two — and what each of them
   actually ships — is part of this step.** Nothing marks them, and the rule above is the only
   thing that finds them: an artifact id is a coordinate rather than a package declaration, chosen
   by whoever named the project and free to say what the artifact is for instead of what is inside
   it. Where the id and the package statements disagree, the package statements are the name. Read
   the package statements, not the id.

   A name derived from the id is right ten times out of twelve, which is exactly what makes it
   worth checking all twelve. The names belong to their jars **pairwise** — this jar, this name — so
   a tree that publishes the right twelve names attached to the wrong twelve jars has declared none
   of them correctly.

   **Why it matters to declare the right name at all, given that the derived name would also be
   wrong.** Because the two are wrong in different ways. A derived name is wrong *temporarily* and
   silently: it works until someone renames a file. A declared name is a public promise, and a
   declared name that lies about where the code lives is a promise the project cannot keep — the
   day these twelve jars grow a `module-info.java`, a `requires com.example.reporting` resolves to
   a module whose packages are all `com.example.report.*`, and every import in the consumer fails
   to read. This is the one thing in a JPMS migration that is genuinely irreversible, which is
   why this step asks for it first.

5. **The two service lookups go through `java.util.ServiceLoader`.** Exactly two methods in this
   tree answer the question "give me every provider of this interface" by reading
   `META-INF/services` by hand:

   - `spi/src/main/java/org/jboss/arquillian/spi/util/ServiceLoader.java#reload`
   - `impl-base/src/main/java/org/jboss/arquillian/impl/DynamicServiceLoader.java#load`

   Both open the provider-configuration resources off a class loader with `getResources`, read the
   lines, and load each named class. Afterwards **neither** does: the tree holds **no**
   `ClassLoader.getResources` call at all, and both of those methods perform their lookup by
   calling `java.util.ServiceLoader.load`. A declaration of the type is not a lookup — a field or a
   local of type `java.util.ServiceLoader` that nothing calls `load` on does not satisfy this.

   **The platform's loader has a different error contract, and adopting the loader means adopting
   the contract.** The hand-written scanners skip a line they cannot use and return whatever is
   left. `java.util.ServiceLoader` raises `ServiceConfigurationError` for the whole lookup when a
   named class is missing, does not implement the service, or has no public no-argument
   constructor — and, where the platform loader is also what constructs the providers, when one is
   not public, is abstract, or throws from its constructor. A lookup that hands back provider
   *classes* constructs nothing, and whatever the code around it then does with those classes keeps
   the behaviour it has today. It is also more permissive about
   well-formedness than the hand-written parsers were: surrounding whitespace, a leading space, an
   inline `#` comment and the same class named twice in one file are all legal and all were
   rejected here. Both of those changes are **expected**, and they are the exception requirement 1
   named. Do not wrap the platform loader in a per-line `try`/`catch` to reproduce the old
   skip-the-bad-line behaviour: that compiles, it satisfies this requirement's letter, and it is
   wrong. The new error contract is part of what this step migrates *to*, and a tree that hides it
   has not migrated it.

6. **The eleven lookups that are not service lookups stay exactly as they are.** Thirteen methods
   in this tree reach into a class loader. Two of them are requirement 5. The other **11** ask for
   one named class, or for one named resource, and `java.util.ServiceLoader` cannot express any of
   them:

   - `SecurityActions#isClassPresent` in four projects — impl-base's resource enricher, and the
     CDI, EJB and resource test-enrichers. It asks whether one class is on the classpath;
     `ServiceLoader` has no way to ask that.
   - `FilteredURLClassLoader#getResource` and `#loadClass` — a `ClassLoader` subclass's own
     overrides, which delegate a lookup to a parent. Rewriting a class loader's `loadClass` to call
     `ServiceLoader` is not a migration, it is a recursion.
   - `JMXTestRunner`'s constructor, `ServletTestRunner#doGet`, `EJBInjectionEnricher#injectClass`
     and `ResourceInjectionEnricher#injectClass` — each loads the one class a string names.
   - `spi ServiceLoader#createInstance(String)` — the by-name load that sits in the same file as
     requirement 5's `reload`, and is not part of it. It stays, public and callable, because the
     published API surface requirement 1 freezes includes it and because code in this tree calls it.

   **A tree that rewrote all thirteen would not compile, and if it did it would fail in five
   subsystems.** Keeping these eleven is not optional, and deleting one of them is the same failure
   as breaking it: a by-name load that is gone is a by-name load that no longer holds.

7. **The service registrations the tree publishes today are the ones it publishes afterwards.**
   Every `META-INF/services` file that is in the tree today is still in it, naming the same
   providers. That includes one file that is not a service file at all in the platform's sense:
   `impl-base/src/main/resources/META-INF/services/org.jboss.arquillian.impl.configuration.api.ArquillianDescriptor`,
   whose lines are `implClass=` / `importerClass=` / `defaultName=` properties, because that is
   what `Descriptors.importAs` in the pinned shrinkwrap-descriptors 0.1.4 reads back out of it.

   **That file is why `arquillian-impl-base` cannot have a module descriptor derived for it, and it
   stays anyway.** `ModuleFinder` requires every non-comment line of every provider file in a jar
   to be a binary class name present in that jar, so it refuses this one and reports the jar as not
   derivable. Deleting it makes the jar derivable and breaks the configuration mechanism the whole
   framework reads its descriptor through. Eleven of the twelve jars are derivable both before and
   after this step; that is the correct number, it is not a defect, and a tree that raises it to
   twelve by deleting this file has traded a working framework for a cleaner report.

8. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none, and it carries **44** `@SuppressWarnings`
   sites that nothing here needs a forty-fifth of. Requirement 5 is where one gets reached for:
   `ServiceLoader.load` is generic, the two methods it replaces hand back a collection the callers
   already have a type for, and an unchecked cast with a suppression over it is the shortest route
   from one to the other. It is also the route that leaves the type error in place and hides it.
   `ServiceLoader.load(Class<S>, ClassLoader)` is typed; ask it for the right type and no cast is
   needed.

## Notes

- Nothing in this step needs a `module-info.java`, and adding one is not a stronger answer to
  requirement 3 — it is a different and wrong one. A module declaration may not `requires` an
  automatic module by a name the platform derived, because a derived name is not stable; and 74 of
  the 80 jars this tree compiles against are automatic modules with no declared name of their own.
  A `module-info.java` written against them would pin this build to today's file names, which is
  the exact failure `Automatic-Module-Name` exists to prevent. The strong form comes after the
  ecosystem underneath it declares itself, and it is not reachable here.
- `Automatic-Module-Name` has rules of its own. The value has to be a legal module name — dotted
  segments, each a valid Java identifier, no hyphens and no digit-leading segments — and the
  platform checks it when it reads the jar. A name it rejects makes the jar *less* usable on a
  module path than no name at all, because a jar with no attribute at least gets a derived name.
- The manifest wraps. A manifest line longer than 72 bytes is continued on the next line with a
  leading space, and every one of these names is long enough for that to happen. If you go looking
  for the attribute in a built jar, read the manifest with something that understands the format;
  a `grep` for the whole attribute on one line finds nothing in a perfectly correct jar.
- `java.util.ServiceLoader` predates the module system — it is Java 6 — so nothing about
  requirement 5 needs a newer language level, and the `Stream`-returning `stream()` method it grew
  in Java 9 is available here. Java 11's library is available to you and the ones after it are
  not: `Stream.toList` is Java 16 and will not compile if you ask for the level by release.
- Nothing about the module system produces a compiler warning on the classpath, and a clean build
  says nothing about whether the twelve names are right. `jar --describe-module` will tell you what
  the platform reads out of a built jar, including which name it would have derived if you had not
  declared one.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
