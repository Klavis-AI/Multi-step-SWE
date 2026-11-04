# Step 2 — class files 50 through 52

## 1. Implement this milestone

The virtual machine in `/app/jvm` runs class files of major version 45 through
49 today. Take it to **major version 52**: the format `javac` produced for
Java 6, 7 and 8. Everything it already does has to keep working alongside
the new behaviour.

**Unchanged interface.** `make -C /app/jvm` produces the launcher
`/app/jvm/bin/java`; the C sources and headers live under `/app/jvm/src/`
(text only — object files and binaries go elsewhere under `/app/jvm/`). The
launcher must be a native ELF executable produced by `cc` from your C sources;
a shell script or interpreter wrapper is not accepted. It must not list
`libjvm`, `libjava`, or `libjli` in its ELF dynamic-section `NEEDED` entries as
`readelf -d` reports them: a launcher that links against those libraries is
not accepted, because such a launcher is wrapping an existing JVM rather than
implementing one. The launcher is the `java` command of
the *JDK Tool Specification*:

```
java [options] -cp <path> <mainclass> [args...]
java [options] -jar <file.jar> [args...]
```

with `-cp` / `-classpath`, `-D<name>=<value>`, `-Xmx<size>`, `-Xss<size>` and
`-jar`. Its home is the parent of the directory holding the executable and the
platform class library is read from `<home>/lib/platform`. The class library in
`/app/jvm/lib/platform` has grown for this milestone; its source is beside it in
`/app/jvm/lib/platform-src/`, and every method declared `native` there is still
the virtual machine's job.

**The build starts from source.** `make -C /app/jvm` must produce the launcher
from a clean tree that holds only `/app/jvm/src/`, `/app/jvm/Makefile` and a
fresh copy of the compiled platform class library at `/app/jvm/lib/platform`:
everything else under `/app/jvm/` — objects, archives, shared libraries, the
launcher, and the rest of `lib/` with them — is absent. So the build has to
produce the launcher from those two paths alone: nothing it needs may live
anywhere else under `/app/jvm/`, and `lib/platform-src/` is not there when it
runs.

**The version window moves.** Major versions 45 through 52 load and run; 44 and
below, and 53 and above, are refused with `UnsupportedClassVersionError`
(JVMS 4.1). Class files at 50, 51 and 52 that the machine refuses today must now
run.

**Verification changes (JVMS 4.10.1, Java SE 6/7/8 editions).** From major
version 50 a class file carries a `StackMapTable` attribute and is verified by
**type checking** against it rather than by type inference:

* the attribute is checked structurally while the class file is parsed — frame
  types, verification type infos, constant-pool references, and the attribute
  length all have to add up, and `max_stack` / `max_locals` have to hold what the
  frames declare. A file that fails those checks is a `ClassFormatError`;
* the frames are then the expected types at their offsets: each frame offset
  must be an instruction boundary, the incoming state at a branch target must be
  assignable to the frame there, and the state after an unconditional transfer
  of control is the frame, not the fall-through. A method that fails is a
  `VerifyError` at link time;
* major version 50 alone keeps the Java SE 6 failover: a class file at exactly
  50 whose type checking fails is verified again by type inference, and only a
  second failure is a `VerifyError`. From 51 there is no failover.

**Dynamic call sites (JVMS 4.4.8–4.4.10, 4.7.23, 5.4.3.6, 6.5).** Major 51 adds
`CONSTANT_MethodHandle`, `CONSTANT_MethodType` and `CONSTANT_InvokeDynamic` to
the constant pool and `invokedynamic` to the instruction set:

* a class whose code contains `invokedynamic` and carries no `BootstrapMethods`
  attribute is a `ClassFormatError`;
* resolving an `invokedynamic` instruction runs the bootstrap method with the
  lookup object, the call-site name and the invocation `MethodType`, followed by
  the static arguments, and the `CallSite` it returns is linked once and reused;
* `java.lang.invoke.MethodHandle.invokeExact` and `invoke` are **signature
  polymorphic** (JVMS 2.9.3): resolution succeeds for whatever descriptor the
  call site names, the arguments are passed as they appear, and the return type
  is the one at the call site.
* `MethodHandles.lookup()` is caller sensitive, and the `Lookup` finders
  (`findStatic`, `findVirtual`, `findSpecial`, `findConstructor`, `findGetter`,
  `findSetter`, `findStaticGetter`, `findStaticSetter`) produce handles that
  behave as the *Java SE API Specification* describes, including
  `NoSuchMethodException`, `NoSuchFieldException` and `IllegalAccessException`.
  `MethodHandle.bindTo` on a handle that has no parameters, or whose first
  parameter type is primitive, throws `IllegalArgumentException` with the
  message `no leading reference parameter`.
* `java.lang.invoke.LambdaMetafactory.metafactory` and `altMetafactory` are the
  bootstrap methods `javac` emits for lambda expressions and method references,
  so lambdas, instance-bound and unbound method references, constructor
  references, captured locals, captured `this` and captured fields all have to
  work, and an exception thrown inside a lambda body propagates to the caller of
  the functional-interface method like any other.

**Interfaces gain bodies (JSR 335, JVMS 4.6, 5.4.3.4, 5.4.6).** Major 52
interfaces may declare `default` and `static` methods:

* `invokestatic` on a `CONSTANT_InterfaceMethodref` resolves to the static
  method declared in that interface;
* method selection for `invokevirtual` / `invokeinterface` searches the class
  hierarchy first and then the maximally specific superinterface methods, so a
  `default` method inherited through a superinterface is found, a more specific
  one overrides a less specific one, and a class that no longer implements an
  abstract method fails with `AbstractMethodError`;
* `invokespecial` on a direct superinterface — what `X.super.m()` compiles to —
  calls that interface's method without re-dispatching.

**Language shapes `javac` emits at these versions** must run: try-with-resources
including its `AutoCloseable` closing order, multi-catch, `switch` on `String`,
binary and underscore-separated literals, the diamond operator, and everything
the earlier window already covered.

**How a conformance test runs.** Unchanged: a test runs `/app/jvm/bin/java` with the
arguments it names and compares the bytes on standard output, the process exit
status, and standard error reduced to its first line plus every later line
that is either a `\tat …` frame whose class is not in the `java.`, `jdk.` or
`sun.` namespaces, or a `\t... ` continuation, or a `Caused by:` line
truncated to the exception's class name (the text of a linkage error is not
specified). Runs use the `C` locale (`LANG=LC_ALL=C`), a clean `PATH`, and no
`JAVA_HOME`; each one has 90 seconds of wall clock, and the whole set has
twenty minutes — a test not reached inside that fails. Up to seven run at
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
own: `invokedynamic`, `tableswitch` and
`lookupswitch`, interface methods, default and static, lambdas and method
references, malformed `StackMapTable` attributes, malformed class files,
method handles, the class-file version window; and the exception table.
Every earlier milestone's behaviour must still hold, as it should be at this
milestone.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification*, Java SE 6, 7
and 8 editions (local copies of the 7 and 8 editions:
`/opt/docs/jvms/se7/jvms7.txt` and `/opt/docs/jvms/se8/jvms8.txt`, each with its
PDF beside it) — §4.7.4 (`StackMapTable`), §4.7.23 (`BootstrapMethods`), §4.10.1
(verification by type checking), §2.9 and §5.4.3.5–5.4.3.6 (signature
polymorphic methods and call site specifiers), §5.4.6 (method selection), §6.5
(`invokedynamic`); the *JDK Tool Specification* for the `java` command (local
copy: `/opt/docs/jdk-tools/jdk8/java.txt`); and the *Java SE API Specification*
for `java.lang.invoke`.

**In this workspace.**

* `/app/jvm/lib/platform-src/` — the class library's source and
  `platform-src/README.md`, which states the VM↔library boundary.
  `grep -rn native /app/jvm/lib/platform-src` lists what the VM must provide.
* `/app/jvm/lib/platform/` — the same library, compiled. The clean tree the
  build runs from holds a fresh copy of it, so do not change it.
* `/app/tests/` — a sample conformance corpus. `bash
  /app/tests/run-tests.sh` runs it. It is representative, not exhaustive.
* `/app/WORKSPACE_HANDOFF.md` — what the previous session left behind. Keep it
  current.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the `/app` workspace and preserve the supplied source, manifests,
  documents, and handoff files. Do not create a prose answer or alternate entry
  point unless Section 1 explicitly requires one.
- Do not cheat by using online solutions or hints specific to this task. The
  artifact must be your own C code: `/app/jvm` must contain the C sources it is
  built from, and the launcher may not embed, link against, or execute another
  Java virtual machine, nor may the build or any run fetch, unpack or keep
  another Java runtime or its class library anywhere it can write.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.
