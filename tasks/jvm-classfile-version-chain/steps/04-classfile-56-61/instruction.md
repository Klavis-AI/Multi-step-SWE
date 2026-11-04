# Step 4 — class files 56 through 61

## 1. Implement this milestone

The virtual machine in `/app/jvm` runs class files of major version 45 through
55 today. Take it to **major version 61**: the format `javac` produced for
Java 12 through 17. Everything it already does has to keep working alongside
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

**The version window moves.** Major versions 45 through 61 load and run; 44 and
below, and 62 and above, are refused with `UnsupportedClassVersionError`.

**Records (JEP 395, JVMS 4.7.30).** A class with the `Record` attribute whose
direct superclass is `java.lang.Record` is a record class. `javac` compiles its
`equals`, `hashCode` and `toString` to an `invokedynamic` whose bootstrap method
is `java.lang.runtime.ObjectMethods.bootstrap`, with the record class, a
semicolon-separated list of component names, and one accessor `MethodHandle` per
component as static arguments:

* `equals` is true when the argument is an instance of the same record class and
  every component is equal — `==` for primitives, with `Float.compare` /
  `Double.compare` semantics for floating point, and `Object.equals` for
  references, `null` matching `null`;
* `hashCode` combines the components' hashes, and equal records hash equally;
* `toString` reads `Name[c1=v1, c2=v2]`, where `Name` is the record class's
  simple name as `Class::getSimpleName` gives it;
* `Class::isRecord` and `Class::getRecordComponents` report the components in
  declaration order, with their names and types.

**Sealed classes (JEP 409, JVMS 4.7.31).** A class or interface with the
`PermittedSubclasses` attribute may only be extended or implemented by the
classes it lists. A class that names a sealed supertype it is not permitted to
extend fails to load with `IncompatibleClassChangeError`. `Class::isSealed` and
`Class::getPermittedSubclasses` report it.

**Hidden classes (JEP 371, JVMS 5.3.5).**
`MethodHandles.Lookup::defineHiddenClass(byte[], boolean, ClassOption...)`
defines a class that is not discoverable: it is not entered into the class
loader's table, `Class.forName` cannot find it, and every call produces a fresh
class even from identical bytes. Its `Class::getName` is the name in the class
file followed by `/` and an identifier that distinguishes it — a name no
ordinary class can have, since `/` cannot appear in a binary name — and
`Class::isHidden` is true for it and false for every ordinary class. The lookup
returned has the hidden class as its lookup class.

**Helpful NullPointerException messages (JEP 358).** A `NullPointerException`
the virtual machine raises now carries a message that names the operation that
failed and, when the bytecode says where the null came from, its origin:

```
Cannot invoke "Owner.method(ArgType)" because <origin> is null
Cannot read field "name" because <origin> is null
Cannot assign field "name" because <origin> is null
Cannot read the array length because <origin> is null
Cannot load from object array because <origin> is null
Cannot store to int array because <origin> is null
Cannot throw exception because <origin> is null
Cannot enter synchronized block because <origin> is null
```

`Owner` and argument types are printed as the binary simple name, so a
nested class appears as `Outer$Inner`. `<origin>` is
`"name"` for a local variable that the `LocalVariableTable` names,
`"<localN>"` for one it does not, `"Class.field"` for a static field,
`"<origin>.field"` for an instance field, `"null"` for the null literal, and
`the return value of "Class.method()"` for a method result. When the origin
cannot be determined the message is the operation alone, with no `because`
clause. The array kinds are `object`, `int`, `long`, `float`, `double`,
`byte/boolean`, `char` and `short`.

**Switch shapes.** `javac` at these versions compiles arrow switches, switch
expressions with `yield`, switches over strings and over enums, and pattern
`instanceof`, to `tableswitch` / `lookupswitch` and ordinary calls; text blocks
are ordinary string constants. All of it has to run.

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
own: `CONSTANT_Dynamic` constants,
`tableswitch` and `lookupswitch`, helpful `NullPointerException` messages,
hidden classes, malformed `StackMapTable` attributes, malformed class files,
records, sealed classes; and the class-file version window.
Every earlier milestone's behaviour must still hold, as it should be at this
milestone.

## 2. Read these local references

**Normative semantics.** *The Java Virtual Machine Specification*, Java SE 17
edition (local copy: `/opt/docs/jvms/se17/jvms17.txt`, with its PDF beside it) —
4.7.30 (`Record`), 4.7.31 (`PermittedSubclasses`), 5.3.5 (deriving a hidden
class), 5.4.3.6; JEP 358, 371, 395 and 409; the *JDK Tool Specification* for the
`java` command (local copy: `/opt/docs/jdk-tools/jdk17/java.txt`); and the
*Java SE API Specification* for `java.lang.runtime.ObjectMethods`,
`java.lang.invoke.MethodHandles.Lookup`, `java.lang.Class` and
`java.lang.reflect.RecordComponent` (local copies:
`/opt/docs/java-se-api/se17/java.base/`).

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
