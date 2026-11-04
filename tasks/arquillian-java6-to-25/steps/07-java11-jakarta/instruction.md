# Step 7 — Java 11 and the jakarta namespace

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with Java 7's and Java 8's idioms adopted where they belong, with Java 9's
reflection and collection work done, and compiling to Java 10 bytecode.

Java 11 is the first long-term-support release after 8, and it is where two things this tree needs
became standard. `java.net.http.HttpClient` shipped — incubating at 9 and 10, standard at 11 — and
retired the `HttpURLConnection` idiom, which is a connection object you configure with setters,
read a status code off, and have to remember to disconnect. And the Java EE specifications that
Oracle handed to the Eclipse Foundation finished moving house: the `javax.*` packages they owned
became `jakarta.*`, because the `javax` namespace did not come with the donation. That second one
is the largest single rename in the platform's history and it is the reason this step exists as its
own rung.

It is also a rename with a boundary, and the boundary is what this step is really asking you to
find. Not every `javax.` package moved. The ones that are part of the JDK, or that were never
Oracle's to donate, are still `javax.` in Java 25 and will be forever. A tree that treats
`javax.` as a prefix to be replaced does not compile, and a tree that leaves the EE packages alone
does not migrate. Both halves are part of this requirement, and
neither is optional.

**The JDK does not move at this step.** The build ran on Java 11 at step 6 and still does. What
moves is the level it compiles *to*: Java 10 becomes Java 11, and Java 11 is the highest level this
compiler emits.

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
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **There is exactly one exception and it
     belongs to requirement 4.** One published method takes a parameter whose type is in a package
     requirement 4 moves. Its parameter type moves with the package — that is not a change you may
     decline, and it is not a change you may compensate for by keeping an overload behind. Every
     other member of that surface is frozen as hard as it has been at every step so far: a narrowed
     surface and a widened one are the same defect, and making a method public to get around a
     compile error trades more than it buys;
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

   **There is exactly one exception to that last point, it belongs to requirement 7, and it is
   required rather than merely tolerated.** Read requirement 7 before you decide what "behaviour
   unchanged" means for the failure paths of the exchange it rewrites.

2. **The four previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **9** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   the filesystem sites that are `java.nio.file` still are, and the **4** that are still `java.io`
   are still `java.io`, with the behaviour that migration had to preserve unchanged; and the
   explicit type arguments step 3 removed do not come back — the **23** sites that spell out their
   type arguments and are *not* on an anonymous class body are still 23. The **73** constructor
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
   combinators step 6 introduced are still carrying their control flow; and the **80** declarations
   that use `var` still use it.

   Step 6's class-file level is the one thing on this list that does **not** hold, because
   requirement 3 moves it. That is this step's work, not a regression of step 6's.

3. **The compiled artifacts are Java 11.** Every class compiled from the library's own sources is
   class-file **major version 55** — main and test alike, in the tree and inside the published
   jars — and it is the only major version present. (Build logic that drives Gradle is not part of
   the library.)

   Ask for it by *release*, not merely by target, for the same reason step 6 did: a build that sets
   only the bytecode target compiles against whatever library the running JDK has, and a call to
   something a later JDK added compiles clean and fails at run time. Here the two numbers finally
   agree — the compiler is 11 and the level is 11 — which makes it tempting to stop asking by
   release at all. Do not. The two settings do not mean the same thing just because their numbers
   match here: a target names the bytecode and leaves the API surface to whichever JDK happens to
   run the build, where asking by release names both. A build that states only the target is
   relying on a coincidence of the machine for a promise it should be making itself, and the
   failure that coincidence hides is a run-time one — latent whether or not the two numbers ever
   come apart.

   Wherever the build states that level, it states it consistently. The level the build states is
   the level the artifacts it produces are expected to carry, so a build that names Java 11 in one
   place and Java 10 in another is describing two different sets of artifacts, and which set you
   get depends on which statement the tool you happen to run pays attention to.

4. **The two Jakarta EE packages this tree can move, move.** The tree holds **22** references to
   `javax.annotation` and `javax.ejb` — **17** and **5** — across **3** files, main code and test
   code alike, and they sit in **6** places in those files. A place is one file and one method in
   it; where a file names one of the two outside every method — on its fields, or in a declaration
   — everything it names there is one place of its own, and one of the six is exactly that, a file
   whose annotations are all on fields. Every one of the twenty-two references becomes the
   `jakarta.` spelling of the same name: `javax.annotation.Resource` becomes
   `jakarta.annotation.Resource`, `javax.ejb.EJB` becomes `jakarta.ejb.EJB`. Afterwards the tree
   holds **no** reference to either `javax.` package, and **22** to the two `jakarta.` ones.

   **The rename is not a source-only edit.** These annotations come from API artifacts the build
   declares, and the `jakarta.` types are in different artifacts under different coordinates. A
   tree whose sources import `jakarta.annotation.Resource` while its build still resolves the
   `javax.annotation` API does not compile. Move the declarations with the imports — **every**
   declaration of them. Where a module declares an EE API once for its main code and again for its
   tests, both are the same fact stated twice and both move; leaving one behind puts two
   incompatible spellings of the same annotation on one project's classpath, and the failure
   surfaces in the module that resolves second rather than in the one you edited.

   A jakarta-namespace API is available to the build for each of these two, at a version that
   compiles against the rest of this tree. Do not reach for a newer one: these APIs changed more
   than their package name across their later releases, and the point of this step is the namespace
   and not the version.

5. **Every other `javax.` reference stays exactly where it is.** **16** files in this tree name one
   of six further `javax.` packages, and between them they hold **112** references to those
   packages — every place one of their types is named in a body or a declaration. The import lines
   are a census of their own and none of the 112 is one. Those references sit in **42** places,
   counted the way requirement 4 counts a place, in **15** of the sixteen files; six of the
   forty-two are fields or type declarations with no method around them, and the sixteenth file
   names one of the six packages and holds no other reference to it. Because a package that is
   held is held per place, those 42 places make **43** (package, place) pairs: one method reaches
   for a JNDI type and a CDI type both, and it is holding two packages rather than one. Both
   numbers are held: a pair that stops naming its package is a pair lost, and a reference that
   goes from a pair still naming the package is a reference lost:

   - `javax.management` (**40**) and `javax.naming` (**24**) and `javax.xml` (**4**) are **part of
     the JDK**. They were never Java EE's, they did not move to the Eclipse Foundation, and they
     are still `javax.` in Java 25. There is no `jakarta.management`. Renaming these does not
     produce a tree that resolves against something else; it produces a tree that resolves against
     nothing.
   - `javax.enterprise` (**32**) and `javax.inject` (**3**) are CDI, and `javax.servlet` (**9**) is
     the Servlet API. These *are* EE packages and they *did* move — but the containers this tree
     compiles and tests against are the 2011 ones, and a 2011 container implements the `javax.`
     contract. Moving the annotation without moving the container gives you a tree that compiles
     against one specification and runs against another. The containers are the ones this tree has,
     so `javax.` is the spelling these packages keep in it: they are held here, and holding them is
     part of this step's work rather than an omission from it.

   **This is the requirement a blanket rename fails.** A search-and-replace of `javax.` to
   `jakarta.` across the tree satisfies requirement 4
   completely and destroys this one completely. So does reaching for an automated migration recipe
   named for the whole namespace rather than for the two packages that move. Deleting the code
   that holds these references is not an escape either — a held reference that has been deleted is
   a held reference that is gone.

   Requirement 4 and this requirement are one question asked from two sides, and the tree does not
   let you answer one without answering the other: the two files that hold the most `javax.`
   references in the tree each hold **both** kinds, in adjacent methods. `ResourceInjectionEnricher`
   moves its `javax.annotation` and holds its `javax.naming`. `EJBInjectionEnricher` moves its
   `javax.ejb` and holds its `javax.naming`. Read the package, not the prefix.

6. **Arrays come back from collections without being sized at the call site.** The tree has **8**
   calls that pass a freshly allocated array to `Collection.toArray` — `toArray(new Foo[0])`,
   `toArray(new Foo[list.size()])` — in **5** methods of the main code. All eight pass a
   constructor reference instead: `toArray(Foo[]::new)`.

   This is the `toArray` overload Java 11 added, and it says the thing the old spelling was
   working around. The array argument never existed to carry data; it existed to carry the
   component *type*, because generics erase and the collection cannot name `Foo` at run time. A
   constructor reference carries the type without allocating anything, and it takes the length as
   an argument, so the "size it exactly or size it zero" question the old form forces disappears.

   **Write the type argument out.** Where the element type is generic, `Foo[]::new` and
   `Foo<?>[]::new` are not the same reference, and the raw one compiles with a warning and hands
   back an array the caller's generic signature does not describe. Getting to eight constructor
   references by way of a raw element type is worse than not converting, because the compile still
   succeeds.

7. **The servlet protocol talks to its container through `java.net.http`.** One method — the one
   that drives the test-execution request in the servlet protocol's method executor — holds **14**
   references to the `HttpURLConnection` era: the connection types themselves, and the calls that
   only exist on them (`openConnection`, `getResponseCode`, `getResponseMessage`, `setUseCaches`,
   `setDefaultUseCaches`, `disconnect`). None of the fourteen remains. In their place, at least
   **3** distinct `java.net.http` types appear in that file — the client, the request, and the
   response are three of them — and the exchange is performed through them.

   **Name a redirect policy.** `HttpURLConnection` follows redirects by default; `HttpClient` does
   not, and a client built without saying so will report a 302 to a caller that was written
   expecting the destination. Whatever policy you choose, the built client has to state one, and
   what a redirect does for a caller of this exchange afterwards is what it does today.

   Two further differences are yours to handle rather than to preserve accidentally. The old code
   polls until a deadline, retrying on 404 and treating any other non-200 as fatal; that loop is
   the method's contract and it survives the rewrite unchanged. And the old code reads the response
   as a stream off the live connection, where the new one is handed a response whose body is
   already a value — so the deserialisation that used to read from the connection now reads from
   what came back.

   **Two of this exchange's failure paths change what a caller sees, and that change is part of the
   migration rather than a regression of it.** The message this project builds for a non-200 status
   quotes the status line it was handed — the code, and the server's reason phrase beside it. A
   `java.net.http` response carries the code and the headers and no reason phrase at all,
   deliberately, because HTTP/2 has none; so that message names the status and nothing else
   afterwards. And an address whose scheme is not HTTP is refused earlier, and by someone else:
   today this project finds it itself, after a connection object has already been made, and reports
   it as an `IllegalStateException` of its own wording, where the new request refuses the URI while
   the request is still being built and what reaches the caller is the builder's own
   `IllegalArgumentException`, naming the scheme it would not accept. Both are **expected to
   change**, in exactly the way the replacement API produces. Do not rebuild the reason phrase from
   a table of your own, and do not catch the builder's refusal in order to re-throw the old type
   with the old wording; hiding the change is not the migration, and it is not what requirement 1's
   promise about behaviour is protecting. Everything else about this exchange behaves exactly as it
   does today: which requests go out, which status codes mean what, the poll that retries on 404
   until its one-second budget is spent and what it does when it is interrupted, the message the
   deadline failure reports, and what a redirect does. Those two paths are the only ones that move.

   **Three `java.net.URLConnection` references elsewhere in the tree are not this requirement.**
   They are in a test, inside a class that exists to be deployed into a container as a fixture, and
   they describe what that fixture does rather than how this project makes a request. They stay.

8. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none. It carries **44** `@SuppressWarnings` sites
   today, and nothing this step asks for needs a forty-fifth. Requirement 6 is exactly where an
   unchecked-cast suppression gets reached for, and requirement 4 is exactly where one gets reached
   for to quiet a half-migrated classpath; in both cases the suppression leaves the problem in place
   and hides it, which is worse than leaving it visible. Neither a new suppression nor a returning
   raw type is an acceptable price for anything else on this list.

## Notes

- This step raises the bytecode to Java 11 and leaves the JDK at 11. No JDK version has to move,
  and two dependency coordinates do.
- Java 11's library is available to you and the ones after it are not. `Stream.toList` is Java 16,
  `Collectors.teeing` is Java 12, the switch expression is Java 14 — none of them will compile here
  if you ask for the level by release, and all of them will compile and then be wrong if you do not.
  Everything this step asks for exists at 11: `java.net.http`, `Collection.toArray(IntFunction)`,
  `Optional.isEmpty`, `String.strip`, `Predicate.not`.
- The `javax` to `jakarta` rename has no compiler support and never will: the two namespaces are
  unrelated as far as javac is concerned, so nothing here produces a deprecation warning and a
  clean build says nothing about whether you found the boundary. The compiler will tell you when
  you have renamed a package that does not exist on the classpath, which catches the JDK packages
  loudly, and it will tell you nothing at all about the EE packages you moved too early — those
  resolve, compile, and fail against a container that is not there to fail against yet.
- Both halves of requirement 4's move — the import and the declared dependency — have to land
  together, and neither one is safe alone. Sources ahead of the build do not compile; the build
  ahead of the sources compiles against an API nothing imports.
- `java.net.http` is a small API with a few sharp edges worth knowing before you start: the client
  is built and reused rather than opened per request, the request carries the URI, and the response
  body is whatever the body handler you pass produces — bytes, a string, a stream — decided when
  you send rather than read off the connection afterwards. There is no `disconnect`.
- Nothing in this step requires a module declaration. `java.net.http` is a module and using it does
  not make this project one; adding a `module-info.java` changes how every consumer of these twelve
  jars resolves them, which requirement 1 does not allow.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
