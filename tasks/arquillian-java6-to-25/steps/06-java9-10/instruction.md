# Step 6 — Java 9 and 10

You are picking up the tree the previous step left at `/app/arquillian`: the Arquillian
integration-testing framework, built by Gradle through a committed wrapper, with the project's own
test suite on JUnit 5, with its anonymous single-method bodies and its collecting loops already
written the Java 8 way, and with Java 8's own library — `Optional`, collectors, `Objects`,
`java.time` — adopted where it belongs.

Five steps of work have all happened at one compile level. Everything so far compiles to Java 8
bytecode, and the reason that could go on being true is that Java 8 is where the language and the
library this tree uses lived. It is not where they live now. Java 9 deprecated the reflection
idioms this codebase leans on — `Class.newInstance()`, which propagates a constructor's checked
exception out of a method that never declared it, and `AccessibleObject.isAccessible()`, which
answers a different question from the one every caller here asks it. Java 9 added the collection
factories that say "this collection is finished" in the type instead of in a comment, and the
`Optional` combinators that let a lookup's absent case stay inside the `Optional` instead of being
unpacked into an `if`. Java 9 deprecated the wrapper constructors. Java 10 added `var`, which lets
a declaration stop saying the same type name twice. This step adopts all of that, and moves the
compiled artifacts to Java 10 with it.

**This step moves the JDK.** The build runs on Java 11, not Java 8, and Java 11 is the only JVM the
build may rely on. Several JDKs may be installed here; do not assume the one you happen to build
with is the one that will be used. Java 11 is the compiler; Java 10 is the level it compiles *to*.

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
     constructors and fields, with the same signatures. **This one constrains this step's work
     directly and you should read it that way.** A narrowed surface and a widened one are the same
     defect, so where a rewrite this step asks for needs a signature to change, it can only be a
     signature that surface does not contain — and making a method public to get around a compile
     error trades more than it buys;
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

   **There is exactly one exception to that last point, it belongs to requirement 4, and it is
   required rather than merely tolerated.** Read requirement 4 before you decide what "behaviour
   unchanged" means for the reflective failure paths.

2. **The three previous steps' own work stays done, and this step's work does not come at its expense.**

   From step 3, all three still expected: **9** `try` statements acquire their resources in a
   resource clause and **no** resource in this tree is closed by a hand-written `close()` call;
   **11** filesystem sites are `java.nio.file` and the **two** that are still `java.io` are still
   `java.io`, with the behaviour that migration had to preserve unchanged; and the
   explicit type arguments step 3 removed do not come back — the **23** sites that spell out their
   type arguments and are *not* on an anonymous class body are still 23.

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

   **One honest exception, and only one.** The tree carries **74** constructor calls written with
   `<>` today. **One** of them constructs a set that requirement 7 asks to become a factory call,
   and no factory form has a type-argument position for a `<>` to sit in. That one diamond goes
   when the site it belongs to is converted, and the tree that has done this step carries **73**.
   That is the conversion, not a regression of it. No diamond outside that one may be lost, and the
   explicit form must not come back anywhere — including as a side effect of requirement 9, which
   is why requirement 9 asks for 80 of 81 rather than all 81.

3. **The compiled artifacts are Java 10.** Every class compiled from the library's own sources is
   class-file **major version 54** — main and test alike, in the tree and inside the published
   jars — and it is the only major version present. (Build logic that drives Gradle is not part of
   the library.)

   Ask for it by *release*, not merely by target. A build that sets only the bytecode target
   compiles the new level against the *current* JDK's library, so code that calls a Java 11 method
   compiles clean and then fails on a Java 10 runtime. Capping the platform API at the level you
   are targeting is the point of asking by release, and it is the only way this requirement and the
   first note below can both be true. How the build asks for it is your business; the requirement
   is the artifacts — major 54 everywhere, and nothing else present. Ask by release anyway: a tree
   that reaches major 54 by target alone satisfies the letter of this and still carries a Java 11
   call that will fail on a Java 10 runtime.

4. **Reflective instantiation goes through a constructor, and the failure paths change with it.**
   The tree has **2** calls to `Class.newInstance()`, both in main code. Neither remains: the class
   is asked for its no-argument constructor and the constructor is asked to instantiate.

   The tree also has **11** places that already instantiate through a `Constructor`. Afterwards
   there are at least **13** — the 11 that were there, plus the 2 that moved. Deleting reflection
   is not migrating it: a tree that drives the first count to zero by removing the code and naming
   the classes directly has not met this requirement at all.

   **Those two sites change what a caller sees when instantiation fails, and that change is part of
   the migration rather than a regression of it.** `Class.newInstance()` propagates the
   constructor's exception — checked or not — exactly as thrown. `Constructor.newInstance()` wraps
   it in `java.lang.reflect.InvocationTargetException`. And a class with no no-argument constructor
   is discovered by `getDeclaredConstructor()` throwing `NoSuchMethodException` *before* any
   instantiation is attempted, instead of by `InstantiationException` during one. Three of this
   project's observable failure paths are exactly those, and all three are **expected to change**,
   in exactly the way the replacement API produces. Do not add compensating unwrapping
   — a `catch (InvocationTargetException e) { throw e.getCause(); }`, or a rethrow that reconstructs
   the old exception chain — to keep the old surfacing. Hiding the change is not the migration, and
   it is not what requirement 1's promise about behaviour is protecting. Everything else this
   project does behaves exactly as it does today.

5. **The deprecated accessibility query is gone, and the access it guarded still happens.** The
   tree has **12** places in main code that ask an `AccessibleObject` whether it `isAccessible()`
   before calling `setAccessible(true)` on it. That query is deprecated in Java 9, because it
   reports whether the access *flag has been set* and not whether access is *permitted*, which is
   what every one of these twelve is using it for — and `setAccessible(true)` on an
   already-accessible object is a no-op, so removing the guard is the entire migration. All twelve
   go.

   The tree has **14** `setAccessible` calls — the twelve those guards protect and two that never
   had one — and afterwards it still has 14, with each of the twelve sites still making its call.
   Removing the guard is this requirement; removing the guard *and* the call is breaking reflection
   so that a count reaches zero. The guards go; every one of the calls they guarded stays, and
   makes its call. `canAccess` is permitted and not required: none of the
   twelve sites has the object instance it would need.

6. **The wrapper constructors are gone.** `new Integer(…)` and its siblings for `Boolean`, `Byte`,
   `Character`, `Double`, `Float`, `Long` and `Short` were deprecated in Java 9 — they promise a
   distinct instance nobody here wants. The tree has **12** such calls in 5 files: one in main
   code, eleven in test code. **Test code is in scope for this requirement**, and none of the twelve
   remains.

   Every one of them is an argument or an initialiser where the value is immediately used as a
   number or a boolean, so `valueOf` — or, in most of them, the literal on its own with autoboxing
   doing the work — says the same thing. The one in main code is inside a comparator, where the
   primitive comparison helper on the wrapper type says it better still.

7. **Hand-built collections become factory calls — and the live views do not.** Main code has
   **14** calls that build or wrap a collection through the pre-Java-9 idioms. They divide in two,
   and the division is by what the call *means*, not by how it is spelled:

   - **7 of them build a fixed collection** — the elements are known at the call, or have just been
     assembled locally, and nothing ever changes the result afterwards. These become factory calls:
     each of the seven ends up with a `List`/`Set`/`Map` factory where the hand-building was.
   - **7 of them publish a live view over a field the object keeps mutating** — the caller is meant
     to see later changes, and the wrapper exists to stop the caller making them. **These stay
     exactly as they are.**

   Most of the first kind are obvious — `Arrays.asList` over values the call itself supplies, an
   empty list, a set built out of a list. But at least one of them wears the second kind's
   spelling: it is a `Collections.unmodifiable…` call like the seven that must be held, and what
   makes it a construction rather than a view is that its argument is a **local the method has
   just finished building**, not a field the object keeps. Read the argument, not the method name.
   Wrap-a-local is a copy and converts; wrap-a-field is a window and stays.

   This is not a formality and it is not symmetric. Replacing a live view with a snapshot changes
   what callers observe, and one of the seven that must not move backs a collection whose
   iteration order is behaviour its callers depend on — a copy of it does not keep that order, and
   does not fail to keep it the same way twice. A blanket rewrite of every wrapping call in sight
   converts all fourteen, and seven of those fourteen are the ones that must not move. Converting
   nothing is not the alternative on offer either: that simply leaves this requirement undone.
   Seven convert and seven stay, and which is which is in the argument. Read the argument.

   Test code is out of scope for this requirement.

8. **`Optional` carries the control flow instead of being unpacked into an `if`.** Step 5 removed
   every `Optional.get()` from this tree; what it left behind is the shape one rung back from that
   — `if (o.isPresent()) { … } else { … }`, and chains of them. **6** such sites remain, in **3**
   methods of the main code — `ContainerRegistryCreator.createRegistry` and
   `ProtocolRegistry.findDefaultProtocol` in `impl-base`, and
   `ExceptionProxy.buildObjectFromClassConstructors` in `spi` — and Java 9 gave all three a
   combinator that says what they mean: a
   fallback that is only computed if the first lookup came up empty, a two-branch consumption of
   present-or-absent, and an `Optional` flattened into a stream. Those three — `or`,
   `ifPresentOrElse`, `stream` — are what this requirement asks for. `map` and `orElseGet` also
   remove an `isPresent()` and are Java 8 spellings the previous step could already have used, so
   they are not this step's work.

   Think in methods, not in sites: two of the three methods hold several `isPresent()`
   tests that collapse into one combinator, and a method that is half converted is not converted.
   All three methods are converted, each of them completely.

   They are these three methods whatever shape the previous step left them in. One that already
   reaches its answer without an `isPresent()` test — a loop that returns early, say — is still
   one of the three, and still ends this step saying what it means through `or`,
   `ifPresentOrElse` or `stream`.

   Two cautions, both of which this tree will make you meet. The fallback combinator takes a
   *supplier*, so anything the old `else` branch computed eagerly — including a test of some other
   value — moves inside it, or you have changed when it runs. And one of the three methods needs a
   type variable it does not have, so it has to become generic; it is private, and therefore not on
   the surface requirement 1 freezes, which is what makes that available to you. Widening a
   published signature to solve the same problem is not.

9. **Declarations stop saying the same type twice.** The tree has **81** local-variable
   declarations of the strictest redundant kind — the written type and the initialiser's type are
   the same type, spelled twice on one line, `Foo x = new Foo(…)` — 34 in main code and 47 in test
   code. **80** of them use `var` instead. Not one of the 81 shares its declaration with another
   variable, so none is blocked by that.

   The other 415 declarations that *could* be written with `var` are a style question and are not
   asked for, though using `var` more widely is not a defect either. What is a defect is getting
   there by the wrong route: deleting declarations is not converting them, and `var` sprinkled over
   declarations outside the 81 does not stand in for converting them.

   **One of the 81 must not be converted**, and it is why this requirement asks for 80. A `var`
   declaration infers its type from the initialiser alone, so where the initialiser is a `<>`
   construction the only way to convert is to write the type argument out on the right — which
   turns a diamond back into an explicit type-argument site and undoes a step-3 gain requirement 2
   still holds. Where those two pull against each other, requirement 2 wins. Exactly one of the 81
   has a `<>` initialiser; leave it as it is. Converting it anyway does nothing for this requirement
   and gives back a diamond requirement 2 holds.

10. **None of this is done with raw types or with suppressions.** The tree has **no** raw
    constructor calls today and must still have none. It carries **44** `@SuppressWarnings` sites
    today, and nothing this step asks for needs a forty-fifth. The factory calls of requirement 7
    and the inferred types of requirement 9 are exactly where an unchecked cast gets papered over;
    a suppression added to quiet what the migration broke leaves it broken and hidden, which is
    worse than leaving it broken. Neither a new suppression nor a returning raw type is an
    acceptable price for anything else on this list.

## Notes

- This step raises the JDK to 11 and the bytecode to Java 10. Those are two different numbers and
  this is the first step where they differ. No dependency version has to move.
- Java 10's library is available to you and the ones after it are not. `Optional.isEmpty` is
  Java 11, `Stream.toList` is Java 16, `Collectors.teeing` is Java 12 — none of them will compile
  here if you ask for the level by release, and all of them will compile and then be wrong if you
  do not. Everything this step asks for exists at 9 or 10: `Optional.or`,
  `Optional.ifPresentOrElse`, `Optional.stream`, `List.of`, `Set.of`, `Map.of`, `List.copyOf`,
  `Set.copyOf`, and `var`.
- The Java 9 collection factories are not drop-in replacements for every collection. They reject
  `null` elements, `Set.of` and `Map.of` reject duplicates outright rather than absorbing them the
  way a `HashSet` constructor does, and the iteration order of `Set.of` is neither the insertion
  order nor stable between JVM runs. All three of those are the right behaviour for a collection
  that is genuinely fixed and known-good, and all three are reasons the seven sites of requirement
  7 that must not move are the ones that must not move. Where a site of the seven that *do* move
  builds a set out of values that could repeat, satisfy yourself that they cannot before you use a
  factory that would throw if they did.
- `var` is not a keyword and does not change what the variable's type is — it is inferred once, at
  the declaration, from the initialiser. It cannot be used where there is no initialiser, where the
  initialiser is `null`, a lambda, a method reference or an array initialiser without a type, or
  where the inferred type would be an anonymous class. Requirement 9's 81 are the declarations
  where none of that applies and the two type names are literally the same.
- The deprecation warnings this compiler emits over the tree you were given are a good census of
  requirements 4, 5 and 6 — and only of those three. Requirements 7, 8 and 9 are about shapes javac
  has no warning for, and the wrapper constructors are deprecated *without* being marked for
  removal, so a lint run that only reports removals will report nothing at all.
- The tree is already clean of JDK-internal APIs, and asking for the level by release is what
  keeps it that way: `--release` compiles against the documented API only, so an internal class is
  a compile error rather than a warning. Nothing here asks you to do anything about that; it is
  part of why moving the compiler is as quiet as it is.
- Nothing in this step requires a module declaration. `module-info.java` is Java 9's headline
  feature and it is not what this step is asking for; adding one changes how every consumer of
  these twelve jars resolves them, which requirement 1 does not allow.
- Do not cheat by using online solutions or hints specific to this task.
- The build must not depend on writing anywhere outside the project tree, `$GRADLE_USER_HOME`
  and `$HOME`.
- The tree's existing tests are the project's own, and you are free to run them as often as you
  like.
