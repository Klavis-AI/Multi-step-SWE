# Step 11 — Java 22: the security manager's API, unnamed variables, and a VarHandle for private fields

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted, with Java 9's reflection and
collection work done, talking to its container through `java.net.http`, with the two Jakarta EE
packages it could move moved, with every published jar naming the module it intends to be and
finding its providers through `java.util.ServiceLoader`, with its casts folded into `instanceof`
tests, its event vocabulary sealed, and — from the step just finished — compiling to Java 21
bytecode, dispatching through pattern `switch`es and reading first elements by name.

This step moves the tree onto Java 22 and spends that level on three things, one of which is
overdue by a decade and two of which the language has only just made sayable.

The overdue one is `java.security.AccessController`. Seven copies of a helper class named
`SecurityActions` wrap ordinary reflection in `doPrivileged` calls, because in 2009 a framework
that might run under a security manager wrapped everything that way. **There is no security
manager any more** — it has been deprecated for removal since Java 17 and cannot be enabled at
all from Java 24 — so those wrappers now cost an exception-unwrapping ladder each and buy nothing.
The other two are `_`, which lets a `catch` clause say that it does not intend to look at what it
caught, and the `VarHandle`, which replaces a mutable `setAccessible` flag on a shared `Field`
object with an accessor that grants the access without changing any shared state.

**The class-file level moves at this step, and it is the only level that does.** Step 10 compiled
to Java 21; here the tree compiles to Java 22. It is the level that moves and not the JVM: there
is no Java 22 JDK in this environment and none is needed, because a JDK compiles to levels older
than its own. Requirement 3 is about what the class files are, not about what wrote them.

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
     installed. Requirement 3 fixes the level, and it fixes it in the class files rather than in
     the JVM;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **This rung adds nothing to that
     surface, and the only thing it removes is the one named here.** Every method of the
     `SecurityActions` classes requirement 4 rewrites keeps the signature and the visibility it has
     today — one of those classes is public and several of their methods are `public static`, so
     some of them are part of that surface. Requirements 4, 5 and 6
     rewrite method bodies, and requirement 4 deletes one *private* nested enum per file —
     those seven types are unreachable from outside the class that declares them, so nothing a
     consumer of these modules can name or call changes. **Those seven are the only types this
     rung removes**, and nothing is added;
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
     wiring, same exception types and messages, same ordering. **Requirements 4 and 6 are the two
     places where a correct-looking rewrite can change behaviour**, and each says exactly which
     exception moves if it is done carelessly.

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
   hold one. **That first figure was 20 at step 10 and is 13 here, and the seven that left are
   this step's own doing** — the `PrivilegedExceptionAction` and `PrivilegedAction` bodies
   requirement 4 deletes are written as lambdas in seven files, and a lambda that goes with the
   call it was an argument to is not a step-4 regression. Deleting a lambda requirement 4 does not
   ask about is.

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
   query, and the `setAccessible` calls — **14** at step 10 and **10** here — are still 10, **and
   the four that left are requirement 6's four and no others**; **no** `new Integer(…)` or any of
   its seven wrapper siblings; the **7** fixed collections that became factory calls are still
   factory calls and the **7** live views over mutable fields are still live views; the `Optional`
   combinators step 6 introduced are still carrying their control flow; and **78** declarations
   use `var`. The reflective constructions step 6 asked for are untouched by this rung: seven of
   them are in the seven `SecurityActions` files, one to a file, each in a method that takes a
   `Constructor` and calls `newInstance` on it, and requirement 4 rewrites the methods above that
   call without reaching the call itself.

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
   a pattern variable still bind one, and the **7** that were left alone — out of **24** in the
   tree — are still plain tests with nothing bound; the HTML document the servlet test runner
   serves is still written as one text block and the tooling formatter still holds all **7** of its
   newline-bearing literals; the **11** sealed roots still name their **35** direct subtypes in a
   `permits` clause, closing over **40** types, and the **11** types this project is extended
   through are still open; and both `switch` statements are still arrow form. **The first figure
   moved for this step's reasons too**: the seven `SecurityActions#getConstructor` ladders
   requirement 4 deletes each bind a pattern in `if (t instanceof NoSuchMethodException nsme)`,
   and those seven bindings go with the ladders — the methods that held them stay. The **7** that
   step 9 deliberately left plain are untouched and all seven must still be there.

   From step 10, all three still expected: the **13** reflective type tests that became type
   patterns are still type patterns and the **14** that could not be converted — out of **27** in
   main sources — are still reflective calls; the **4** pattern `switch`es still dispatch over
   their **10** arms and the **2** chains that had to stay chains are still chains; and the **7**
   guarded first-element reads still say `getFirst()` while the **9** that could not say it still
   read the way they did.

3. **The compiled artifacts are Java 22.** Every class compiled from the library's own sources is
   class-file **major version 66** — main and test alike, in the tree and inside the published
   jars — and it is the only major version present. (Build logic that drives Gradle is not part of
   the library.) The build names the same level wherever it names one: a tree that moves some of
   its projects and leaves the rest at the level they came from carries two majors, and only one
   of those is this step's.

4. **The security manager's API leaves this tree, and the work it was wrapping stays.** There are
   **67** references to `java.security` in **7** files, all of them named `SecurityActions.java` —
   in `impl-base` twice (`…impl` and `…impl.enricher.resource`), in `spi`, in the servlet
   protocol's runner package, and in the CDI, EJB and resource test enrichers. They are **20**
   `AccessController.doPrivileged` calls and **47** mentions of the four types those calls need:
   `AccessController`, `PrivilegedAction`, `PrivilegedExceptionAction` and
   `PrivilegedActionException`. Afterwards this tree contains **none of the 67**, which leaves the
   `java.security` imports that served them with nothing left to name.

   The 20 calls are in four kinds of method: **7** `getConstructor`, **7**
   `getThreadContextClassLoader`, **4** `getFieldsWithAnnotation` and **2**
   `getMethodsWithAnnotation`.

   **What each wrapper was wrapping is what has to survive it**, and that is the whole of this
   requirement's difficulty. `doPrivileged` runs a body and returns its value; deleting the call
   means running the body directly:

   - `getThreadContextClassLoader` returns `AccessController.doPrivileged(GetTcclAction.INSTANCE)`,
     where `GetTcclAction` is a private nested enum implementing `PrivilegedAction<ClassLoader>`
     whose `run()` returns `Thread.currentThread().getContextClassLoader()`. Afterwards the method
     reads the context class loader directly and the nested enum is gone from all seven files —
     it implements a type this requirement removes, so it cannot stay;
   - `getConstructor` wraps `clazz.getConstructor(argumentTypes)` in a
     `PrivilegedExceptionAction` and then spends twenty lines unwrapping the
     `PrivilegedActionException` that wrapper makes it throw: pull out the cause, rethrow it if it
     is a `NoSuchMethodException`, otherwise cast it to `RuntimeException` and, if even that
     fails, raise a `RuntimeException` about code that should never be reached. **All of it is
     bookkeeping for the wrapper and none of it survives the wrapper's removal.** The method
     declares `throws NoSuchMethodException`, `Class.getConstructor` throws exactly that, and a
     direct call raises it directly. The declared signature does not move, and the method still
     obtains its constructor through `Class.getConstructor` and still hands back a `Constructor`;
   - the four `getFieldsWithAnnotation` and two `getMethodsWithAnnotation` bodies walk a class
     hierarchy collecting annotated `Field`s and `Method`s. They keep doing that, `setAccessible`
     call included — see requirement 6.

   **Thirteen things must still be true of those seven files afterwards, and they are what says
   the code was unwrapped rather than deleted.** All **7** files still construct reflectively
   through a `Constructor` — one construction to a file, a level below the methods this
   requirement rewrites, and that is the point: the ladder above it goes and the
   construction it was there to serve does not. And all **6** annotation-scanning methods still
   scan: four `getFieldsWithAnnotation` and two `getMethodsWithAnnotation`, each still reaching
   its `Field` or `Method` objects and still calling `setAccessible` on them. Seven and six.

   A file that has lost its `getConstructor`, or a scanner that now returns an empty list, has
   removed the `java.security` references and the reason the class exists along with them. Deleting
   the seven `SecurityActions` classes outright removes all 67 references and fails this
   requirement completely.

5. **A `catch` clause that does not look at what it caught says so.** There are **93** `catch`
   clauses in this tree's main sources, in **68** methods across **43** files. **58** name a
   parameter and read it. **35** name a parameter and never read it — the name is there because
   the language required one, and every one of those 35 is what JEP 456 exists for.

   Afterwards **no** `catch` clause in this tree's main sources declares a parameter it does not
   read. That happens in two ways:

   - **28** of the 35 stay where they are and take `_` as their parameter, which declares nothing;
   - **7** leave the tree with the ladders that hold them — they are inside the `getConstructor`
     unwrapping requirement 4 deletes, so they go when that bookkeeping goes.

   **The 51 clauses that still read their parameter keep their names.** `_` is not tidier than a
   name that gets used; it is a different statement, and it does not compile beside a read.
   Renaming a read parameter to `_` makes the line that reads it a compile error, which is the
   language enforcing the same thing this requirement asks for. A tree that converts only the 28
   and leaves the other 7 has done the visible half. A tree that deletes a `catch` clause to get
   its parameter off the books has changed what the method does with a thrown exception, and
   requirement 1 says the exceptions this library raises do not move.

6. **The four private-field reflection sites get a `VarHandle`, and the ten that cannot keep
   `setAccessible`.** There are **14** `setAccessible` calls in this tree's main sources, across
   **12** files. **4** of them are plain reads and writes of a private field through a `Field`
   object: the tooling deployment formatter's `getInternalFieldValue` (a read), and the event
   point's `set`, the injection point's `set` and the resource enricher's `enrich` (three writes).
   Afterwards each of those four obtains an accessor for the field instead — a lookup with private
   access to the field's declaring class, unreflected into a `VarHandle` — and reads or writes
   through that handle. Both calls are made in each of those four methods — **4**
   `privateLookupIn` and **4** `unreflectVarHandle`, **8** in all — so the accessor is obtained in
   the method that uses it, not cached in a field or built in a constructor. A `privateLookupIn`
   whose result nothing unreflects is a lookup nobody used.

   **The other ten stay exactly as they are — keeping them is part of this requirement, not an
   exception to it:**

   - **six are in the annotation scanners** requirement 4 keeps — four `getFieldsWithAnnotation`
     and two `getMethodsWithAnnotation`, across four `SecurityActions` files. These hand `Field`
     and `Method` objects back to their callers and never access anything themselves. There is no
     access here to route through a handle, so there is no conversion to make;
   - **four are invocation sites whose exceptions callers can see** — the dynamic service loader's
     and the core reflections helper's and the SPI service loader's `createInstance`, and the map
     object's `populate`. `Constructor.newInstance` wraps whatever the constructor throws in an
     `InvocationTargetException` and reports an inaccessible member as `IllegalAccessException`;
     `MethodHandle.invoke` wraps nothing and a `Lookup` refuses differently. Converting these
     changes what a caller catches, which requirement 1 forbids.

   As in requirement 4, deleting a site does not satisfy this: the four that convert have to be
   reading and writing the same fields afterwards, and the ten that stay have to still be there.

7. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none, and it carries **44** `@SuppressWarnings`
   sites that nothing here needs a forty-fifth of. Nothing in requirements 4 through 6 produces a
   warning that wants suppressing — in fact requirement 4 removes some, because every
   `AccessController` mention in this tree is a deprecation warning today. A suppression added
   here is hiding something this step did not ask you to do.

## Notes

- Java 22's language is available to you and the ones after it are not. Unnamed variables and
  patterns — JEP 456, the `_` of requirement 5 — are standard at 22 and were a preview feature at
  21, which is why this is the release the step waits for. Nothing here needs `--enable-preview`,
  and a build that asks for it is asking for something else.
- `_` has been a reserved word since Java 9 precisely so that it could become this, so a tree that
  still compiles at 21 has not done requirement 5. That is a fact about the level and not about
  any one file: javac reports the keyword once per compilation unit it appears in, so the number
  of complaints a 21 compiler makes is a count of files and never a count of conversions.
- `MethodHandles.privateLookupIn` and `Lookup.unreflectVarHandle` are both Java 9, not Java 22.
  Requirement 6 is not waiting on this release; it is here because it is the same subject as
  requirements 4 and 5 — reflection that the platform has since given a better answer for — and
  because the security-manager removal is what makes the `setAccessible` question worth asking at
  all.
- A `VarHandle` obtained from a `Field` accesses that field without the `Field`'s `accessible`
  flag being left switched on for whoever holds the object next. `setAccessible(true)` mutates
  shared state on a reflective object; a `VarHandle` is an accessor with the access decision
  already made. That is the reason for requirement 6 and not the line count.
- `VarHandle.get` and `set` are signature-polymorphic: their return and parameter types come from
  the call site, so a `get` assigned to `Object` returns `Object` and a `get` assigned to `int`
  unboxes. They also throw `WrongMethodTypeException` and `ClassCastException` where `Field.get`
  threw `IllegalArgumentException`, so a converted site's `catch` has to be read again rather than
  copied. The four sites requirement 6 names are the four where nothing observes the difference.
- `AccessController.doPrivileged` is not merely discouraged; it is deprecated for removal and, from
  Java 24, the security manager it existed to serve cannot be enabled at all. A `doPrivileged` call
  in a tree running on a modern JDK executes its action and nothing else. This is why requirement 4
  is a deletion rather than a replacement: there is no modern spelling of "run this with more
  privilege", because there is no less-privileged state to escape.
- `PrivilegedActionException` is a checked exception that only `doPrivileged` throws. Code that
  catches it is code that exists to undo the wrapper, so it goes with the wrapper. Be careful that
  what it was unwrapping *to* still comes out: `getConstructor` declares
  `throws NoSuchMethodException` and its caller depends on getting one.
- A build that names one level in one place and a different level in another is describing two
  different sets of artifacts, and which set comes out of it depends on which of the two statements
  the tool being run reads. Comments carry that same claim to everyone except the compiler: a
  comment above a level setting that still names the level the build has left is a line the next
  reader has to measure the build to disbelieve.
- Asking javac for a level by *release* and asking it only for a bytecode *target* are two
  different instructions, and requirement 5 is a language feature rather than a library call, so a
  build that sets only the target will accept `_` if the JDK running it is new enough and produce
  class files at the older major, which fails requirement 3.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
