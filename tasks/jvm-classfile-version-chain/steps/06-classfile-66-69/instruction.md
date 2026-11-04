# Step 6 — class files 66 through 69

## 1. Implement this milestone

The virtual machine in `/app/jvm` runs class files of major version 45 through
65 today. Take it to **major version 69**: the format `javac` produced for
Java 22 through 25. Everything it already does has to keep working alongside
the new behaviour.

**Unchanged interface.** `make -C /app/jvm` produces `/app/jvm/bin/java`; the
C sources and headers live under `/app/jvm/src/` (text only — object files and
binaries go elsewhere under `/app/jvm/`). The launcher must be a native ELF
executable produced by `cc` from your C sources; a shell script or interpreter
wrapper is not accepted. It must not list `libjvm`, `libjava`, or `libjli` in
its ELF dynamic-section `NEEDED` entries as `readelf -d` reports them; a
launcher that links against those libraries is not accepted.
The launcher is the `java` command of the *JDK Tool Specification*, its home is
the parent of the directory holding the executable, and the platform class
library is read from `<home>/lib/platform`, with its source beside it in
`platform-src/` while you work. Every method declared `native` there is the
virtual machine's job. `--enable-native-access=<modules>` is accepted and
selects nothing.

**The build starts from source.** `make -C /app/jvm` must produce the launcher
from a clean tree that holds only `/app/jvm/src/`, `/app/jvm/Makefile` and a
fresh copy of the compiled platform class library at `/app/jvm/lib/platform`:
everything else under `/app/jvm/` — objects, archives, shared libraries, the
launcher, and the rest of `lib/` with them — is absent. So the build has to
produce the launcher from those two paths alone: nothing it needs may live
anywhere else under `/app/jvm/`, and `lib/platform-src/` is not there when it
runs.

**The version window moves.** Major versions 45 through 69 load and run; 44 and
below, and 70 and above, are refused with `UnsupportedClassVersionError`.

**Preview class files, from the other side (JVMS 4.1).** A class file whose
`minor_version` is 65535 is usable only when its `major_version` is exactly the
highest version this virtual machine supports *and* `--enable-preview` was
given. A preview class file built for an earlier release — 65.65535, or
66.65535 — is now refused with `UnsupportedClassVersionError` whether or not the
flag is present. The same file that ran before must stop running.

**The foreign function and memory API is final (JEP 454).** `java.lang.foreign`
is no longer a preview API: a program that uses `Linker`, `SymbolLookup`,
`Arena`, `MemorySegment`, `FunctionDescriptor` and `ValueLayout` is an ordinary
class file and runs without `--enable-preview`. The behaviour is the one the
*Java SE API Specification* gives those types: symbol lookup returning an empty
`Optional` for an unknown name, downcall handles that pass `JAVA_BYTE`,
`JAVA_INT`, `JAVA_LONG` and `ADDRESS` arguments and return values, confined
arenas that free what they allocated, a global arena that refuses to close, and
bounds-checked segment access raising `IndexOutOfBoundsException`.

**Defining classes from bytes the program produced (JVMS 5.3.5).**
`MethodHandles.Lookup::defineClass(byte[])` defines an ordinary, findable class
in the lookup class's package and loader — `Class.forName` returns the same
`Class` object — while `defineHiddenClass` on the same bytes produces a fresh,
unfindable class each time.

**Object footprint.** An object header of more than one word is now too much: a
program that keeps 500,000 two-field objects alive, plus the array that
references them, must run to completion under `-Xmx24m`. Field layout has to
pack, not pad, and an array has to store its elements at their natural width.

**A generational collector.** Making the live set 150 times larger must not make
an allocation-heavy program cost materially more: measured with callgrind, the
instructions executed by the same allocation loop with a 150,000 object live set
must be **at most 1.60 times** those executed with a 1,000 object live set.
Both arms are run with `-Xmx32m` so the
collector is forced to collect repeatedly across the full allocation run. A
collector that walks the whole live set on every collection cannot meet that;
one that collects a young generation and remembers the references into it from
older objects can.

**A compiled tier.** The virtual machine must execute a hot method in a form
that costs materially fewer instructions than plain bytecode interpretation:
measured with callgrind, the instructions executed by a small integer and array
benchmark run normally must be **at most 0.80 times** those executed by the
same benchmark run with the launcher's own `-Xint`.
`-Xint` must therefore keep interpreting: it is the option that turns the
compiled tier off, exactly as the *JDK Tool Specification* describes it.
In both measurements every measured run must complete normally: exit status 0
and the program's correct result on standard output.

**How a conformance test runs.** Unchanged: a test runs `/app/jvm/bin/java` with the
arguments it names and compares the bytes on standard output, the process exit
status, and standard error reduced to its first line plus every later line
that is either a `\tat ...` frame whose class is not in the `java.`, `jdk.` or
`sun.` namespaces, or a `\t... ` continuation, or a `Caused by:` line
truncated to the exception's class name. Runs use the `C` locale
(`LANG=LC_ALL=C`), a clean `PATH`, and no `JAVA_HOME`; each one has 90 seconds
of wall clock, and the whole set has twenty minutes — a test not reached
inside that fails. Up to seven run at once, with the behaviours interleaved,
so the launcher has to tolerate other copies of itself running beside it.
Each test runs in a directory of its own, as an unprivileged user that can read
`/app/jvm/bin/java` and `/app/jvm/lib/platform` and write only in that
directory, with two gibibytes of address space and `HOME` pointing at a
scratch directory.

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
own: `CONSTANT_Dynamic` constants, defining a
class at run time, hidden classes, malformed `StackMapTable` attributes,
malformed class files, preview class files, the class-file version window, the
compact string and object representations; and the foreign function and memory
API. Every earlier milestone's behaviour must still hold, as it should be at this
milestone.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification*, Java SE 25
edition (local copy: `/opt/docs/jvms/se25/jvms25.txt`, with its PDF beside it) —
4.1 (preview minor version), 5.3.5 (deriving a class from bytes); JEP 454 and
519; the *JDK Tool Specification* for the `java` command, including `-Xint`
(local copy: `/opt/docs/jdk-tools/jdk25/java.md`); and the *Java SE API
Specification* for `java.lang.foreign` and `java.lang.invoke.MethodHandles.Lookup`
(local copies: `/opt/docs/java-se-api/se25/java.base/`).

**In this workspace.**

* `/app/jvm/lib/platform-src/` — the class library's source and its `README.md`.
  `grep -rn native /app/jvm/lib/platform-src` lists what the VM must provide.
* `/app/jvm/lib/platform/` — the same library, compiled. The clean tree the
  build runs from holds a fresh copy of it, so do not change it.
* `/app/tests/` — a sample conformance corpus; `bash
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
