# Step 1 — a Java virtual machine for class files 45 through 49

## 1. Implement this milestone

Write, in C, a Java virtual machine that runs class files of major version 45
through 49 — the format `javac` produced for Java 1.1 through Java 5 — against
the class library that is already in this workspace.

**How it is built and started.** `make -C /app/jvm` must produce the launcher
`/app/jvm/bin/java`. Keep the C sources and headers under `/app/jvm/src/` and
the makefile at `/app/jvm/Makefile`: those two paths, plus
`/app/WORKSPACE_HANDOFF.md`, are the deliverable and what the build is run
from, so anything the build needs has to live there. Keep
`/app/jvm/src/` to text — C sources and headers only — and send object files,
libraries and the executable somewhere else under `/app/jvm/` (the makefile
decides where); only text under those paths is part of the deliverable.
The launcher `/app/jvm/bin/java` must be a native ELF executable produced by
`cc` from your C sources; a shell script or interpreter wrapper is not
accepted. It must not list `libjvm`, `libjava`, or `libjli` in its ELF
dynamic-section `NEEDED` entries as `readelf -d` reports them: a launcher that
links against those libraries is not accepted, because such a launcher is
wrapping an existing JVM rather than implementing one.
That launcher is the `java` command of the *JDK Tool Specification*, and
nothing else is added to it:

```
java [options] -cp <path> <mainclass> [args...]
java [options] -jar <file.jar> [args...]
```

with the options `-cp` / `-classpath`, `-D<name>=<value>`, `-Xmx<size>`,
`-Xss<size>`, `-jar`. The class path is `:`-separated and holds directories and
JAR files. The launcher finds its own home the way the real one
does — the parent of the directory holding the executable — and reads the
platform class library from `<home>/lib/platform`, i.e. `/app/jvm/lib/platform`.
`-jar` reads `Main-Class` from the archive's `META-INF/MANIFEST.MF`, and reports
`Error: Unable to access jarfile <file>` when the archive cannot be opened.

**The build starts from source.** `make -C /app/jvm` must produce the launcher
from a clean tree that holds only `/app/jvm/src/`, `/app/jvm/Makefile` and a
fresh copy of the compiled platform class library at `/app/jvm/lib/platform`:
everything else under `/app/jvm/` — objects, archives, shared libraries, the
launcher, and the rest of `lib/` with them — is absent. So the build has to
produce the launcher from those two paths alone: nothing it needs may live
anywhere else under `/app/jvm/`, and `lib/platform-src/` is not there when it
runs.

**What it must do**, all of it as the *Java Virtual Machine Specification*,
Java SE 5 Edition, defines it:

* **Class files (JVMS 4).** Parse and statically check the whole format:
  the constant pool and every cross-reference in it, access flag combinations,
  names and descriptors, field and method tables, the `Code`, `Exceptions`,
  `ConstantValue`, `LineNumberTable`, `LocalVariableTable` and `SourceFile`
  attributes and their internal consistency. A file that breaks a static
  constraint is rejected with `ClassFormatError`.
* **Strings and the constant pool.** A `CONSTANT_String` is resolved to the
  one interned instance for its characters, so two occurrences of the same
  literal — including one the compiler folded from `"He" + "llo"` — are the
  same object, `new String("Hello")` is not, and `intern()` returns the
  interned one. `java.lang.String`'s hash is the JLS one, `h*31 + c` over the
  characters with 0 for the empty string, and the native methods
  `java.lang.String` and `StringBuilder` declare in
  `/app/jvm/lib/platform-src/` are the virtual machine's to provide.
* **The version window.** Major versions 45 to 49 are supported. A class file
  outside that window is rejected with `UnsupportedClassVersionError`
  (JVMS 4.1), whether it is older or newer.
* **Loading, linking, resolution (JVMS 5.3–5.4).** Resolve superclasses and
  superinterfaces on load; verify and prepare on first active use; resolve
  constant-pool references lazily on first execution, with the errors the
  specification names for each failure — `NoClassDefFoundError`,
  `ClassCircularityError`, `IncompatibleClassChangeError`, `NoSuchMethodError`,
  `NoSuchFieldError`, `IllegalAccessError`, `AbstractMethodError`,
  `InstantiationError` — and access control per JVMS 5.4.4. A class that was
  compiled against a different version of another class must fail exactly where
  the specification says it fails, and not before.
* **Verification (JVMS 4.9, SE 5 edition).** Class files at these versions carry
  no `StackMapTable`, so verification is the type-inference dataflow of that
  edition: instruction structure, branch targets, operand stack and local
  variable types, the rules for uninitialized objects, the `max_stack` and
  `max_locals` limits. A method that fails is rejected with `VerifyError`, at
  link time, not at load time.
* **Initialization (JVMS 5.5).** `<clinit>` runs once, on first active use, after
  the superclass is initialized, with the recursion rule for a class that is
  already being initialized on this thread; a `<clinit>` that completes abruptly
  leaves the class erroneous and wraps a non-`Error` in
  `ExceptionInInitializerError`.
* **Interpretation (JVMS 6).** Every instruction of the Java SE 5 instruction
  set, with the arithmetic of JVMS 2.3 and 2.8 — two's-complement `int` and
  `long` with wraparound, IEEE 754 `float` and `double` including NaN
  comparisons, the shift-distance masks, the saturating floating-point-to-integer
  conversions, `Integer.MIN_VALUE / -1`, the sign of `irem`/`lrem`.
* **The object model.** Instance and static fields with the hiding rules, array
  creation including `multianewarray` and default element values, array
  covariance with `ArrayStoreException`, `checkcast` / `instanceof` over classes,
  interfaces and arrays, virtual and interface dispatch, `invokespecial` for
  constructors, private methods and `super`.
* **Exceptions.** The exception table search of JVMS 2.10 and 6.5 `athrow`,
  synchronized-method and `monitorenter` unlocking while unwinding, and the
  runtime exceptions the specification requires the VM itself to raise
  (`NullPointerException`, `ArithmeticException`,
  `ArrayIndexOutOfBoundsException`, `NegativeArraySizeException`,
  `ArrayStoreException`, `ClassCastException`, `StackOverflowError`,
  `OutOfMemoryError`). At this class-file rung those carry the detail messages
  the Java SE 5 / 6 platform produced: `ArrayIndexOutOfBoundsException`'s message
  is the offending index on its own, `NegativeArraySizeException` has no
  message, and the message of an `ArrayStoreException` raised by `aastore` is the
  name of the stored value's class as `Class.getName` returns it (for example
  `java.lang.Integer`).
* **Stack traces.** `Throwable.fillInStackTrace` records the frames that are on
  the stack at the throw point, most recent first, each with the declaring class,
  the method name, the `SourceFile` name and the line number from
  `LineNumberTable`. An exception that leaves `main` is reported by the launcher
  as `Exception in thread "main" ` followed by `Throwable.printStackTrace`
  output on standard error, and the process exits with status 1.
* **Monitors and threads.** `monitorenter` / `monitorexit` and `ACC_SYNCHRONIZED`
  with reentrancy, `Object.wait` / `notify` / `notifyAll` with
  `IllegalMonitorStateException` for a thread that does not own the monitor, and
  `java.lang.Thread` running on real operating-system threads so that
  `Thread.start`, `Thread.join` and `Thread.isAlive` behave as specified.
* **The heap.** `-Xmx` bounds it, allocation that cannot be satisfied after
  collection throws `OutOfMemoryError`, and unreachable objects are reclaimed:
  a program that allocates far more than `-Xmx` in total while keeping a small
  live set has to run to completion.
* **System properties.** `System.getProperties` is populated by the VM before
  `System` is initialized, including every `-D` option, `line.separator`,
  `user.dir`, `path.separator`, `file.separator`, and `file.encoding` /
  `native.encoding` / `stdout.encoding` taken from the platform locale. The
  platform library encodes what it prints with the default charset it finds
  there.
* **The library's native methods.** Every method declared `native` in
  `/app/jvm/lib/platform-src/` is implemented by the VM, with the behaviour the
  Java SE API specification gives it. That set of declarations is the complete
  list; there are no other VM-provided entry points.

**How a conformance test runs.** A test runs `/app/jvm/bin/java` with the arguments it
names and compares three things against a recording made from a reference
OpenJDK build on the same class files: the bytes written to standard output,
the process exit status, and standard error reduced to its first line plus
every later line that is either a `\tat …` frame whose class is not in the
`java.`, `jdk.` or `sun.` namespaces, or a `\t... ` continuation, or a `Caused
by:` line truncated to the exception's class name (the text of a linkage error
is not specified). Runs use the `C` locale (`LANG=LC_ALL=C`), a clean `PATH`,
and no `JAVA_HOME`; each one has 90 seconds of wall clock, and the whole set
has twenty minutes — a test not reached inside that fails. Up to seven run at
once, with the behaviours interleaved, so the launcher has to tolerate other
copies of itself running beside it. Each test runs in a directory of its own,
as an unprivileged user
that can read `/app/jvm/bin/java` and `/app/jvm/lib/platform` and write only
in that directory, with two gibibytes of address space and `HOME` pointing at
a scratch directory.

When the **main class itself** cannot be loaded, linked or initialised, the
launcher reports it rather than throwing, and the process exits with status 1:
`Error: Could not find or load main class <name>` or `Error: Unable to
initialize main class <name>`, each followed by a `Caused by:` line naming the
throwable, or `Error: LinkageError occurred while loading main class <name>`,
followed by a tab-indented line holding the throwable.
Which text applies depends on where the failure happens: no class file for the
name gives `Could not find or load main class`; a class file that cannot be
read as a class at all -- malformed, or of a version this launcher does not
accept as it was run -- gives `LinkageError occurred while loading main
class`; and a main class that reads but then fails verification gives
`Unable to initialize main class`.

**What this milestone covers.** This milestone covers these behaviours of its
own: `Iterable` iteration, `instanceof` and
`checkcast`, `tableswitch` and `lookupswitch`, arrays and array stores, bridge
methods, class initialisation order, enums, exceptions, `finally` and stack
traces, field resolution and shadowing, inner and nested classes, integer,
long, float and double arithmetic, malformed class files, monitors, numeric
conversions, resolution and linkage errors, stack traces and line numbers,
strings, system properties, the class-file version window, the heap bound and
the collector, the launcher's own options, the stack bound and
`StackOverflowError`, threads (including `Object.wait` / `notify` / `notifyAll`), varargs and boxing; and
virtual and interface dispatch.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification, Java SE 5
Edition*, chapters 2 (§2.3, §2.8, §2.10), 4, 5 and 6; the *JDK Tool
Specification* for the `java` command (local copy:
`/opt/docs/jdk-tools/jdk5/java.html`); and the *Java SE API Specification* for
the classes the platform library declares (local copies of the Java SE 5.0
pages: `/opt/docs/java-se-api/se5/`).

**In this workspace.**

* `/app/jvm/lib/platform-src/` — the source of the class library the VM runs,
  and `/app/jvm/lib/platform-src/README.md`, which states the VM↔library
  boundary. `grep -rn native /app/jvm/lib/platform-src` lists every method the
  VM has to provide.
* `/app/jvm/lib/platform/` — the same library, compiled. The clean tree the
  build runs from holds a fresh copy of it, so do not change it.
* `/app/tests/` — a sample conformance corpus: class files, the exact
  `java` arguments, and the behaviour recorded from a reference JDK. Run it with
  `bash /app/tests/run-tests.sh`. It is representative, not exhaustive.
* `/app/WORKSPACE_HANDOFF.md` — keep it current.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- Do not cheat by using online solutions or hints specific to this task. The
  artifact must be your own C code: `/app/jvm` must contain the C sources it is
  built from, and the launcher may not embed, link against, or execute another
  Java virtual machine, nor may the build or any run fetch, unpack or keep
  another Java runtime or its class library anywhere it can write.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.
