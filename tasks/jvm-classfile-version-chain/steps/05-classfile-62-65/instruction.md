# Step 5 — class files 62 through 65

## 1. Implement this milestone

The virtual machine in `/app/jvm` runs class files of major version 45 through
61 today. Take it to **major version 65**: the format `javac` produced for
Java 18 through 21. Everything it already does has to keep working alongside
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
virtual machine's job. Two more of the launcher's own options appear in this
milestone: `--enable-preview` and `--enable-native-access=<modules>` (accepted;
it selects nothing here).

**The build starts from source.** `make -C /app/jvm` must produce the launcher
from a clean tree that holds only `/app/jvm/src/`, `/app/jvm/Makefile` and a
fresh copy of the compiled platform class library at `/app/jvm/lib/platform`:
everything else under `/app/jvm/` — objects, archives, shared libraries, the
launcher, and the rest of `lib/` with them — is absent. So the build has to
produce the launcher from those two paths alone: nothing it needs may live
anywhere else under `/app/jvm/`, and `lib/platform-src/` is not there when it
runs.

**The version window moves.** Major versions 45 through 65 load and run; 44 and
below, and 66 and above, are refused with `UnsupportedClassVersionError`.

**Preview class files (JVMS 4.1).** A class file whose `minor_version` is 65535
depends on preview features. Such a file is usable only when **both** hold: its
`major_version` is exactly the highest version this virtual machine supports,
and `--enable-preview` was given. Otherwise it is refused with
`UnsupportedClassVersionError` — a preview class file built for an older release
is never usable, with or without the flag.

**Virtual threads (JEP 444).** `Thread.ofVirtual()`, `Thread.ofPlatform()` and
`Thread.startVirtualThread(Runnable)` create threads;
`Thread::isVirtual` distinguishes them. A virtual thread is not an operating
system thread: it runs on a small pool of carriers, and blocking it in
`Thread.sleep` releases its carrier so another virtual thread can run there.
Twenty thousand virtual threads that each sleep and then update shared state
must all run and be joinable, inside a 256 MB heap — an implementation that
maps each virtual thread onto its own operating-system thread cannot do that.
`Thread::join` and `Thread::isAlive` report a virtual thread's completion the
same way they report a platform thread's.

**The foreign function and memory API (JEP 442, preview at these versions).**
`java.lang.foreign` calls native code and reads and writes native memory:

* `Linker.nativeLinker()`, `Linker::defaultLookup` and
  `SymbolLookup::find(String)` resolve a symbol in the process, returning an
  empty `Optional` when there is none;
* `Linker::downcallHandle(MemorySegment, FunctionDescriptor, Linker.Option...)`
  produces a `MethodHandle` that calls it, with the argument and return layouts
  the descriptor names — `ValueLayout.JAVA_BYTE`, `JAVA_INT`, `JAVA_LONG` and
  `ADDRESS` are the ones used here, and an `ADDRESS` argument may be passed as a
  `MemorySegment`;
* `Arena.ofConfined()` and `Arena.global()` allocate zeroed native memory;
  a confined arena frees what it allocated when it is closed, and closing the
  global arena is an `UnsupportedOperationException`;
* `MemorySegment::get` and `::set` read and write at an offset with a value
  layout, bounds-checked against the segment's size, and an out-of-range access
  is an `IndexOutOfBoundsException`.

Since this API is preview at these class-file versions, a program that uses it
is a preview class file and follows the rule above.

**UTF-8 by default (JEP 400).** The default charset — `file.encoding`, and what
`String::getBytes` and `new String(byte[])` use — is UTF-8 regardless of the
platform locale. The standard output and error streams keep encoding with the
platform's own encoding, which the virtual machine reports in
`native.encoding` and `stdout.encoding`, so a non-ASCII string prints as
replacement characters under a C locale while its `getBytes()` gives UTF-8
bytes.

**Pattern matching (JEP 440, 441).** `javac` compiles a `switch` over patterns
to an `invokedynamic` whose bootstrap method is
`java.lang.runtime.SwitchBootstraps.typeSwitch`: the call site takes the
selector and a starting index and returns the index of the first matching
label, the number of labels when nothing matches, and -1 for a `null` selector.
Labels are `Class` objects, `String`s and enum names. Record patterns
deconstruct through the record's accessors. Guarded patterns (`when`) are
ordinary conditions the compiler emits around the result.

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
own: `CONSTANT_Dynamic` constants, hidden
classes, malformed `StackMapTable` attributes, malformed class files, pattern
matching in `switch`, preview class files, records, the class-file version
window, the default charset, the foreign function and memory API; and virtual
threads. Every earlier milestone's behaviour must still hold, as it should be at this
milestone.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification*, Java SE 21
edition (local copy: `/opt/docs/jvms/se21/jvms21.txt`, with its PDF beside it) —
4.1 (preview minor version); JEP 400, 440, 441, 442 and 444; the *JDK Tool
Specification* for the `java` command (local copy:
`/opt/docs/jdk-tools/jdk21/java.txt`); and the *Java SE API Specification* for
`java.lang.Thread`, `java.lang.foreign` and `java.lang.runtime.SwitchBootstraps`
(local copies: `/opt/docs/java-se-api/se21/java.base/`).

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
