# Step 3 — class files 53 through 55

## 1. Implement this milestone

The virtual machine in `/app/jvm` runs class files of major version 45 through
52 today. Take it to **major version 55**: the format `javac` produced for
Java 9, 10 and 11. Everything it already does has to keep working alongside
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
virtual machine's job.

**The build starts from source.** `make -C /app/jvm` must produce the launcher
from a clean tree that holds only `/app/jvm/src/`, `/app/jvm/Makefile` and a
fresh copy of the compiled platform class library at `/app/jvm/lib/platform`:
everything else under `/app/jvm/` — objects, archives, shared libraries, the
launcher, and the rest of `lib/` with them — is absent. So the build has to
produce the launcher from those two paths alone: nothing it needs may live
anywhere else under `/app/jvm/`, and `lib/platform-src/` is not there when it
runs.

**The version window moves.** Major versions 45 through 55 load and run; 44 and
below, and 56 and above, are refused with `UnsupportedClassVersionError`.

**The class library changed under you.** `java.lang.String` now stores its
characters the way JEP 254 specifies: a `byte[]` that holds one byte per
character when every character fits in Latin-1, and two bytes per character in
UTF-16 (big-endian) otherwise, with a `coder` field saying which. Strings the
virtual machine creates itself — string constants, class names, exception
messages — have to use that representation, and the heap has to store a
`byte[]` as bytes: a program that keeps 200,000 twenty-character Latin-1 strings
alive must run to completion under `-Xmx24m`.

**The module system (JSR 376, JVMS 4.7.25-4.7.26, 5.3.6).**

* `module-info.class` — a class file with `ACC_MODULE`, no name, and the
  `Module` and `ModulePackages` attributes — describes a module: what it
  requires, what packages it exports and to whom. The platform library is the
  module `java.base` and carries one.
* `-p` / `--module-path <path>` names directories holding exploded modules;
  `-m <module>[/<mainclass>]` starts the program from a module;
  `--add-modules <names>` resolves extra modules; `--add-exports
  <module>/<package>=<target>` opens a package the module did not export.
  `--class-path` joins `-cp` and `-classpath` as a spelling of the class path.
* Resolution starts from the root module and follows `requires`; a module reads
  what it requires, every module reads `java.base`, and the unnamed module (the
  class path) reads every resolved module.
* Strong encapsulation: a type in a package that its module does not export to
  the reader is not accessible, and referring to it fails with
  `IllegalAccessError`. `--add-exports` makes it accessible again.
* `java.lang.Module` reports this: `getName`, `isNamed`, `canRead`,
  `isExported(String)` and `isExported(String, Module)` answer for the module a
  class belongs to, and `Class::getModule` returns it.

**Nestmates (JEP 181, JVMS 4.7.28-4.7.29, 5.4.4).** Class files at major 55
carry `NestHost` and `NestMembers` instead of relying on synthetic accessor
methods, so a private member is directly accessible to every member of the same
nest: the compiler emits a direct `invokevirtual`/`invokespecial`/`getfield`
where it used to emit a bridge. `Class::getNestHost`, `Class::getNestMembers`
and `Class::isNestmateOf` report the nest. A private method resolved through
`invokevirtual` or `invokeinterface` is the selected method — no re-dispatch.

**Dynamic constants (JEP 309, JVMS 4.4.10, 5.4.3.6).** `CONSTANT_Dynamic` joins
the constant pool at major 55. `ldc` / `ldc_w` / `ldc2_w` of such an entry runs
its bootstrap method once and pushes the result, whose type is the entry's field
descriptor — so `ldc` of an entry whose descriptor is `J` or `D`, or `ldc2_w` of
one that is not, is a `VerifyError`. The bootstrap method index has to be a
valid index into `BootstrapMethods`; a class file where it is not is a
`ClassFormatError`. `java.lang.invoke.ConstantBootstraps` supplies
`nullConstant`, `primitiveClass` and `getStaticFinal`.

**Indified string concatenation (JEP 280).** From major 53 `javac` compiles the
`+` operator on strings to an `invokedynamic` whose bootstrap method is
`java.lang.invoke.StringConcatFactory.makeConcatWithConstants`. Its recipe
string uses the character U+0001 for an argument and U+0002 for a constant taken
from the static arguments; everything else in the recipe is literal text. The
result must equal what `String.valueOf` of each operand concatenated would
produce, for every primitive type, `null`, and `Object`.

**Private interface methods (JEP 213).** Major 53 interfaces may declare
`private` and `private static` methods, callable from the interface's own
default and static methods.

**Diagnostic texts move with the release.** `ArrayIndexOutOfBoundsException`
raised by the virtual machine now reads `Index <i> out of bounds for length <n>`
and `NegativeArraySizeException` carries the requested length (JDK-8170769), in
place of the bare index and the absent message of the earlier releases. An
`ArrayStoreException` raised by `aastore` keeps its message: the name of the
stored value's class as `Class.getName` returns it (for example
`java.lang.Integer`).

**How a conformance test runs.** Unchanged: a test runs `/app/jvm/bin/java` with the
arguments it names and compares the bytes on standard output, the process exit
status, and standard error reduced to its first line plus every later line
that is either a `\tat ...` frame whose class is not in the `java.`, `jdk.` or
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
own: `CONSTANT_Dynamic` constants, interface
methods, default and static, malformed `StackMapTable` attributes, malformed
class files, nestmates, out-of-bounds messages, string concatenation through
`StringConcatFactory`, strings, the class-file version window, the compact
string and object representations, the language features of this release on
the VM, the launcher's own options; and the module system.
Every earlier milestone's behaviour must still hold, as it should be at this
milestone.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification*, Java SE 9 and
11 editions (local copies: `/opt/docs/jvms/se9/jvms9.txt` and
`/opt/docs/jvms/se11/jvms11.txt`, each with its PDF beside it) — 4.1
(`ACC_MODULE`), 4.4.10 (`CONSTANT_Dynamic`), 4.7.25-4.7.29 (`Module`,
`ModulePackages`, `NestHost`, `NestMembers`), 5.3.6 (module resolution and
readability), 5.4.4 (access control with nests); *The Java Language
Specification* for private interface methods (local copy of the Java SE 11
edition: `/opt/docs/jls/se11/jls11.txt`); JEP 181, 213, 254, 280, 309 and JSR
376; the *JDK Tool Specification* for the `java` command's module options
(local copy: `/opt/docs/jdk-tools/jdk11/java.txt`); and the *Java SE API
Specification* for `java.lang.Module`, `java.lang.invoke.ConstantBootstraps` and
`java.lang.invoke.StringConcatFactory` (local copies:
`/opt/docs/java-se-api/se11/java.base/`).

**In this workspace.**

* `/app/jvm/lib/platform-src/` — the class library's source and its `README.md`.
  `grep -rn native /app/jvm/lib/platform-src` lists what the VM must provide.
* `/app/jvm/lib/platform/` — the same library, compiled, including its
  `module-info.class`. The clean tree the build runs from holds a fresh copy
  of it, so do not change it.
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
