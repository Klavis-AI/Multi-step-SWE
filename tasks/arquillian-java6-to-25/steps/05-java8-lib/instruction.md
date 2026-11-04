# Step 5 — reach for the Java 8 library

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, running on Java 8 and
compiling to Java 8 bytecode, with the project's own test suite on JUnit 5, and with its anonymous
single-method bodies, its collecting loops and its constructor calls already written the Java 8 and
Java 7 way.

The language caught up one step ago. The library did not. A lookup that finds nothing still says so
by returning `null`, and every caller still has to remember to ask. A pipeline that has just been
written as a stream still ends by pushing elements into a collection the method built by hand two
lines earlier. Three value classes still hand-roll `equals` and `hashCode` around null checks and a
`31 * result` accumulator. Four places still ask the system for a count of milliseconds and do
arithmetic on it. Java 8 shipped a library for all four, and this step adopts it.

This step does not move the JDK and does not move the bytecode. Java 8 compiles it before and
after; what changes is which parts of Java 8's own library the code is written against.

Work in `/app/arquillian`. What you leave there has to be a working tree: it builds, and everything
it needs in order to build is in it.

## Requirements

1. **Everything the previous steps established still holds.** None of it may be given up to make
   this step's work easier; every one of these is still true of the tree you leave:

   - no file named `pom.xml` anywhere, and `./gradlew publishToMavenLocal` still works;
   - the Gradle wrapper — `gradlew`, its jar, and properties naming a distribution — is still in
     the tree, and the build still runs with Java 8 as the only JVM available;
   - the same published artifacts, coordinate for coordinate: sixteen POMs, twelve module jars,
     twelve `-sources` jars and three `-tests` jars, at version `1.0.0-SNAPSHOT`;
   - the same public API on every published module — the same public and protected types, methods,
     constructors and fields, with the same signatures. **This one constrains this step's work
     directly and you should read it that way.** Wrapping a return value changes a signature. The
     work requirement 4 asks for is confined to methods that are not part of that surface, and the
     confinement is the requirement, not a hint: a published method that starts returning
     `Optional<T>` instead of `T` has changed the API, and neither a narrowed surface nor a widened
     one is allowed;
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
   - every compiled class the build leaves behind is one a class-file reader can open;
   - the test that asserts on the order in which `Class.getMethods()` returns a class's methods
     comes with the code you inherit. The JVM does not specify that order, so it may land either
     way here. You are not required to make it deterministic — and you must not delete it or switch
     it off either;
   - runtime behaviour of the published artifacts is unchanged — same service registrations, same
     wiring, same exception types and messages, same ordering. **This step is a library migration,
     and a library migration that changes what a caller sees is not one.**

2. **The two previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **9** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   **11** filesystem sites are `java.nio.file` and the **two** that are still `java.io` are still
   `java.io` — counted the way step 3 counted its 13, as constructions of **and** variables
   declared at `java.io.File`, `FileInputStream`, `FileOutputStream`, `FileReader` and
   `FileWriter`, so a declaration counts even when its initialiser is a static factory rather
   than a `new` and a census built only out of `new` will not find both of the two. The declared
   type of a resource in a `try` clause counts the same way: a tree with step 3's work done
   acquires its one remaining `FileOutputStream` through a variable declared at `OutputStream`,
   and narrowing that declaration to `FileOutputStream` puts a third one back. And the explicit
   type arguments step 3 removed do not come back — the **23** sites that spell out their type
   arguments and are *not* on an anonymous class body are still 23.

   From step 4, all three still expected: **20** files hold a lambda or a method reference where
   the anonymous single-method bodies were; the **24** interfaces with exactly one abstract method
   still all carry `@FunctionalInterface`; and the **24** methods that hold a stream pipeline still
   hold one — each of them still both starting a stream and doing something with it.

   **One honest exception, and only one.** The tree carries **93** constructor calls written with
   `<>` today. **19** of them construct the collection that requirement 5's sites accumulate into —
   `List<Foo> found = new ArrayList<>();` two lines above the loop that fills it — and a collector
   constructs nothing for a `<>` to sit on. Those 19 go when the sites they belong to are
   converted, and the tree that has done this step carries **74**. That is the conversion, not a
   regression of it. What must not happen is the explicit form coming back anywhere, and no
   diamond outside those 19 may be lost.

3. **The bytecode is still Java 8.** Every class compiled from the library's own sources is
   class-file major version 52 — main and test alike, in the tree and inside the published jars —
   and there is only one major version present. Unlike the last step, this is inherited rather than
   asked for: the tree you are given is already major 52 everywhere and must still be. Nothing this
   step needs requires a different level. (Build logic that drives Gradle is not part of the
   library.)

4. **A lookup that can find nothing says so in its type.** The tree has **42** places that
   `return null`. **16** of them are methods that are not part of any published module's public or
   protected surface, spread across **10** files: private helpers that look something up — in a
   registry, in a descriptor, among a class's annotations — and return `null` when it is not there.
   Those 16 methods return `Optional` instead.

   All 16, and only those 16. The other 26 are out of scope and must stay as they are: 22 are on
   the published surface that requirement 1 freezes, three are inside anonymous class bodies where
   `return null` is a `Callable<Void>`'s idiom rather than a lookup result, and one of the 16 holds
   two of the 42 — one method, two `return null` statements, both of which go.

   **This has to be a migration and not a rename, and that shows at the callers.** A method
   that returns `Optional` and a caller that immediately calls `.get()` on it is the same null
   dereference with more syntax between it and the crash; `if (o.isPresent()) return o.get();` is
   the null check moved rather than removed. What is wanted is that the callers *consume* the
   Optional — `orElse`, `orElseGet`, `orElseThrow`, `ifPresent`, `map`, `filter`. **The tree you
   leave should contain no `Optional.get()` call at all.**

   Be careful about what you hand back. `Optional.get()` on an empty Optional throws where the old
   code returned `null`; requirement 1 does not allow a caller's observable behaviour to change,
   so wherever the old `null` meant "absent, and the caller handles it", the new code has to mean
   the same thing and behave the same way.

5. **A loop that fills a collection is a collector.** The previous step turned 24 traversals into
   stream pipelines but left every one of them ending the same way it always had: walk the stream,
   push each element into a collection the method constructed by hand. **23** of those sites are in
   the library's main code, across **13** files, and they collect instead.

   The collector has to match the collection the site was building, because that is what the
   callers already depend on:

   - **18** of the 23 build a `List`, and take a list collector;
   - **4** build a `HashSet`, and take a set collector;
   - **1** builds a `LinkedHashSet` — deliberately, because the order in which it iterates decides
     the order in which services are instantiated — and takes a collector that builds *that*
     collection. A plain set collector makes no ordering guarantee, and this is one of the places
     requirement 1's promise about ordering is real and observable.

   A collector that builds an explicitly-supplied collection is always acceptable, including where
   a plain list or set collector would also have done — four of the 23 mutate the result
   afterwards, and naming the collection is the honest answer there. It has to be a collector: the
   three-argument `collect` that takes a supplier, an accumulator and a combiner is a hand-written
   accumulation wearing a stream's clothes, and it does not satisfy requirement 5. What is not
   acceptable is going the other way: a collector that makes no ordering guarantee where the site
   was building an ordered set.

   The same "conversion, not addition" rule as the last step, and it applies where the loop was:
   the 23 hand-written accumulations stop existing, and the methods that held them collect instead.
   The 24th such site is in test code and is not part of this.

6. **The value classes use the standard helpers for `equals` and `hashCode`.** Exactly **3** classes
   in the library's main code declare both `equals(Object)` and `hashCode()`, and all three
   hand-roll the same two bodies: a null-checking if/else chain in `equals`, and a `31 * result`
   accumulation in `hashCode`.

   In all three, `equals` compares its fields with the standard null-safe field comparison, and
   `hashCode` combines its fields with the standard multi-field hash. It must be the combining
   form: the single-argument null-safe hash of one object does not combine fields, and a `hashCode`
   built from it is still the hand-rolled accumulator with one call swapped into it.

   Both halves, in both methods, in all three classes — decorating some other method with a
   null-check helper is not this requirement. And `hashCode` must still agree with `equals` on the
   same field list it does today; requirement 1's promise covers these three classes like any
   other.

7. **Time comes from the time library.** **4** methods in the library's main code — three of them
   in executor classes, one of them a constructor — ask the system for a count of milliseconds,
   **5** calls between them, and compute durations and timestamps out of the arithmetic. All four
   take their value from `java.time` instead.

   One of the four holds two of the five: it takes a reading at the start of an interval and
   another at the end. Both go — converting one end of a duration and leaving
   the other is not a converted timing site.

   What changes is the *source* of the value, not where it is stored. The class whose constructor
   is one of the four publishes the two recorded readings as `long` getters, and those are on the
   published surface requirement 1 freezes: they keep their signatures and keep returning what they
   return today. Take the reading from the time library and convert at the published boundary.

8. **The library this project publishes actually uses all four.** Requirements 4 through 7 are
   about source, and source that never reaches a published jar has not migrated anything a consumer
   of this project can see. The twelve published module jars must reference `java.util.Optional`,
   `java.util.stream.Collectors`, `java.util.Objects` and `java.time`, and must reference each of
   them from a spread of published classes rather than from one. One `Optional` in one class makes
   the name appear in the build output while leaving the library exactly as it was; what is being
   asked is that each of the four is genuinely part of the published code, in proportion to how
   much of that library leg there was to do.

   Each of the four goes with the requirement it belongs to: the `Optional` spread comes from the
   work requirement 4 actually did, the `Collectors` spread from requirement 5's, and so on for 6
   and 7. Jars that name the library while the source has not moved to it are exactly the case
   this requirement exists to exclude, and they do not satisfy it.

   Nothing here asks you to add uses beyond requirements 4 through 7. Doing those four across the
   main code, in the modules that actually publish, satisfies this comfortably.

9. **None of this is done with raw types or with suppressions.** The tree has **no** raw
   constructor calls today and must still have none — `new HashMap()` and `new HashMap<>()` both
   drop the explicit type arguments, but one erases the element types and compiles with a warning.
   It carries **44** `@SuppressWarnings` sites today, and nothing this step asks for needs a
   forty-fifth. Collectors and `Optional` over a collection whose element type was inferred loosely
   are exactly where an unchecked cast gets papered over; a suppression added to quiet what the
   migration broke leaves it broken and hidden, which is worse than leaving it broken.

## Notes

- This step does not raise the JDK and does not raise the bytecode. The build still runs on Java 8
  and still compiles to Java 8; what changes is which parts of Java 8's library the code uses.
  No dependency version has to move.
- Java 8's library is available to you and the ones after it are not. `Optional.or`,
  `Optional.stream` and `Optional.ifPresentOrElse` are Java 9, and `Optional.isEmpty` is Java 11;
  `Collectors.toUnmodifiableList` and `Stream.toList` are Java 10 and 16; `List.of` is Java 9. The
  Java 8 forms of all of these exist and are what this step is asking for.
- `Optional` is a return type, not a field type and not a parameter type. It is not serializable,
  and this project serializes some of what it passes around. The 16 methods of requirement 4 are
  methods, and nothing asks you to store one.
- A collector and a hand-written accumulation do not fail the same way. A collector cannot add at
  an index, cannot put a key and a value into a map, and cannot throw a checked exception out of
  the middle of a traversal. Requirement 5's 23 sites are the ones that do none of those; the
  previous step already separated them out, and the loops it left as loops stay loops.
- `Objects.equals` and `Objects.hash` are null-safe by construction, which is why the hand-rolled
  bodies they replace are mostly null checks. Replacing the checks and keeping the field list the
  same is the whole of requirement 6.
- Requirement 7 is about where a value comes from, not about where it is kept. The time library
  has a type for an instant and a type for an interval between two of them, and it has the
  conversions back to milliseconds that keep requirement 1's promise about the published `long`
  getters. Both ends of an interval come from the same source.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
